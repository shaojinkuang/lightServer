/*
 * server.cpp
 *
 *  Created on: 2012-12-8
 *      Author: Administrator
 */

#include "server.h"


/*
 * Free list management for connections.
 */

static conn **freeconns;
static int freetotal;
static int freecurr;

static void drive_machine(conn *c) {
	bool stop = false;
	int sfd, flags = 1;
	socklen_t addrlen;
	struct sockaddr_storage addr;
	struct linger ling = { 1, 0 };
	int res;

	assert(c != NULL);

	while (!stop) {

		switch (c->state) {
		case conn_listening:
			addrlen = sizeof(addr);
			if ((sfd = accept(c->sfd, (struct sockaddr *) &addr, &addrlen))
					== -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					/* these are transient, so don't log anything */
					stop = true;
				} else if (errno == EMFILE) {
					if (settings.verbose > 0)
						fprintf(stderr, "Too many open connections\n");
					accept_new_conns(false);
					stop = true;
				} else {
					perror("accept()");
					stop = true;
				}
				break;
			}
			if ((flags = fcntl(sfd, F_GETFL, 0)) < 0
					|| fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
				perror("setting O_NONBLOCK");
				close(sfd);
				break;
			}
			DEBUG_LOGGER(dsmplog, "accept new socket[%d], wait for request",
					sfd);
			setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void *) &flags,
					sizeof(flags));
			setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *) &ling,
					sizeof(ling));
			dispatch_conn_new(sfd, conn_read, EV_READ | EV_PERSIST,
					DATA_BUFFER_SIZE, false);
			break;

		case conn_read:
			//DEBUG_LOGGER(dsmplog, "-------------socket [%d] has incoming data-------------", c->sfd);
			if (try_read_cli(c) != 0) {
				continue;
			}
			if ((c->udp ? try_read_udp(c) : try_read_tcp(c)) != 0) {
				continue;
			}
			/* we have no command line and no data to read from network */
			if (!update_event(c, EV_READ | EV_PERSIST)) {
				if (settings.verbose > 0)
					fprintf(stderr, "Couldn't update event\n");
				conn_set_state(c, conn_closing);
				break;
			}
			stop = true;
			break;

		case conn_nread:
			/* first check if we have leftovers in the conn_read buffer */
			if (c->rbytes > 0) {
				int tocopy = c->rbytes > c->rlbytes ? c->rlbytes : c->rbytes;
				memcpy(c->ritem, c->rcurr, tocopy);
				c->ritem += tocopy;
				c->rlbytes -= tocopy;
				c->rcurr += tocopy;
				c->rbytes -= tocopy;
				break;
			}

			/*  now try reading from the socket */
			res = read(c->sfd, c->ritem, c->rlbytes);
			if (res > 0) {
				STATS_LOCK();
				stats.bytes_read += res;
				STATS_UNLOCK();
				c->ritem += res;
				c->rlbytes -= res;
				break;
			}
			if (res == 0) { /* end of stream */
				conn_set_state(c, conn_closing);
				break;
			}
			if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
				if (!update_event(c, EV_READ | EV_PERSIST)) {
					if (settings.verbose > 0)
						fprintf(stderr, "Couldn't update event\n");
					conn_set_state(c, conn_closing);
					break;
				}
				stop = true;
				break;
			}
			/* otherwise we have a real error, on which we close the connection */
			if (settings.verbose > 0)
				fprintf(stderr, "Failed to read, and not due to blocking\n");
			conn_set_state(c, conn_closing);
			break;

		case conn_swallow:
			/* we are reading sbytes and throwing them away */
			if (c->sbytes == 0) {
				conn_set_state(c, conn_read);
				break;
			}

			/* first check if we have leftovers in the conn_read buffer */
			if (c->rbytes > 0) {
				int tocopy = c->rbytes > c->sbytes ? c->sbytes : c->rbytes;
				c->sbytes -= tocopy;
				c->rcurr += tocopy;
				c->rbytes -= tocopy;
				break;
			}

			/*  now try reading from the socket */
			res = read(c->sfd, c->rbuf,
					c->rsize > c->sbytes ? c->sbytes : c->rsize);
			if (res > 0) {
				STATS_LOCK();
				stats.bytes_read += res;
				STATS_UNLOCK();
				c->sbytes -= res;
				break;
			}
			if (res == 0) { /* end of stream */
				conn_set_state(c, conn_closing);
				break;
			}
			if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
				if (!update_event(c, EV_READ | EV_PERSIST)) {
					if (settings.verbose > 0)
						fprintf(stderr, "Couldn't update event\n");
					conn_set_state(c, conn_closing);
					break;
				}
				stop = true;
				break;
			}
			/* otherwise we have a real error, on which we close the connection */
			if (settings.verbose > 0)
				fprintf(stderr, "Failed to read, and not due to blocking\n");
			conn_set_state(c, conn_closing);
			break;

		case conn_write:
			/*
			 * We want to write out a simple response. If we haven't already,
			 * assemble it into a msgbuf list (this will be a single-entry
			 * list for TCP or a two-entry list for UDP).
			 */
			if (c->iovused == 0 || (c->udp && c->iovused == 1)) {
				if (add_iov(c, c->wcurr, c->wbytes) != 0
						|| (c->udp && build_udp_headers(c) != 0)) {
					if (settings.verbose > 0)
						fprintf(stderr, "Couldn't build response\n");
					conn_set_state(c, conn_closing);
					break;
				}
			}

			/* fall through... */

		case conn_mwrite:
			switch (transmit(c)) {
			case TRANSMIT_COMPLETE:
				if (c->state == conn_mwrite) {
					while (c->suffixleft > 0) {
						char *suffix = *(c->suffixcurr);
						if (suffix_add_to_freelist(suffix)) {
							/* Failed to add to freelist, don't leak */
							free(suffix);
						}
						c->suffixcurr++;
						c->suffixleft--;
					}
					conn_set_state(c, conn_read);
				} else if (c->state == conn_write) {
					if (c->write_and_free) {
						free(c->write_and_free);
						c->write_and_free = 0;
					}
					conn_set_state(c, c->write_and_go);
				} else {
					if (settings.verbose > 0)
						fprintf(stderr, "Unexpected state %d\n", c->state);
					conn_set_state(c, conn_closing);
				}
				break;

			case TRANSMIT_INCOMPLETE:
			case TRANSMIT_HARD_ERROR:
				break; /* Continue in state machine. */

			case TRANSMIT_SOFT_ERROR:
				stop = true;
				break;
			}
			break;

		case conn_closing:
			if (c->udp)
				conn_cleanup(c);
			else
				conn_close(c);
			stop = true;
			break;
		}
	}

	return;
}

static int new_socket(struct addrinfo *ai) {
	int sfd;
	int flags;

	if ((sfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1) {
		perror("socket()");
		return -1;
	}

	if ((flags = fcntl(sfd, F_GETFL, 0)) < 0
			|| fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
		perror("setting O_NONBLOCK");
		close(sfd);
		return -1;
	}
	return sfd;
}

static int server_socket(const int port, const bool is_udp) {
	int sfd;
	struct linger ling = { 0, 0 };
	struct addrinfo *ai;
	struct addrinfo *next;
	struct addrinfo hints;
	char port_buf[NI_MAXSERV];
	int error;
	int success = 0;

	int flags = 1;

	/*
	 * the memset call clears nonstandard fields in some impementations
	 * that otherwise mess things up.
	 */
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
	if (is_udp) {
		hints.ai_protocol = IPPROTO_UDP;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_family = AF_INET; /* This left here because of issues with OSX 10.5 */
	} else {
		hints.ai_family = AF_UNSPEC;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_socktype = SOCK_STREAM;
	}

	snprintf(port_buf, NI_MAXSERV, "%d", port);
	error = getaddrinfo(settings.inter, port_buf, &hints, &ai);
	if (error != 0) {
		if (error != EAI_SYSTEM)
			fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(error));
		else
			perror("getaddrinfo()");

		return 1;
	}

	for (next = ai; next; next = next->ai_next) {
		conn *listen_conn_add;
		if ((sfd = new_socket(next)) == -1) {
			freeaddrinfo(ai);
			return 1;
		}

		setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void *) &flags,
				sizeof(flags));
		if (is_udp) {
			maximize_sndbuf(sfd);
		} else {
			setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, (void *) &flags,
					sizeof(flags));
			setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *) &ling,
					sizeof(ling));
			setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, (void *) &flags,
					sizeof(flags));
		}

		if (bind(sfd, next->ai_addr, next->ai_addrlen) == -1) {
			if (errno != EADDRINUSE) {
				perror("bind()");
				close(sfd);
				freeaddrinfo(ai);
				return 1;
			}
			close(sfd);
			continue;
		} else {
			success++;
			if (!is_udp && listen(sfd, 1024) == -1) {
				perror("listen()");
				close(sfd);
				freeaddrinfo(ai);
				return 1;
			}
		}

		if (is_udp) {
			int c;

			for (c = 0; c < settings.num_threads; c++) {
				/* this is guaranteed to hit all threads because we round-robin */
				dispatch_conn_new(sfd, conn_read, EV_READ | EV_PERSIST,
						UDP_READ_BUFFER_SIZE, 1);
			}
		} else {
			if (!(listen_conn_add = conn_new(sfd, conn_listening,
					EV_READ | EV_PERSIST, 1, false, main_base))) {
				fprintf(stderr, "failed to create listening connection\n");
				exit (EXIT_FAILURE);
			}

			listen_conn_add->next = listen_conn;
			listen_conn = listen_conn_add;
		}
	}

	freeaddrinfo(ai);

	/* Return zero iff we detected no errors in starting up connections */
	return success == 0;
}

/*
 * if we have a complete line in the buffer, process it.
 */
static int try_read_cli(conn *c) {
	gettimeofday(&c->timestamp, NULL);
//    DEBUG_LOGGER(dsmplog, "start to read data on socket[%d]", c->sfd);
	char *el, *cont;

	assert(c != NULL);
	assert(c->rcurr <= (c->rbuf + c->rsize));

	if (c->rbytes == 0)
		return 0;
	//el = memchr(c->rcurr, '\n', c->rbytes);
	//if (!el)
	//    return 0;
	//cont = el + 1;
	//if ((el - c->rcurr) > 1 && *(el - 1) == '\r') {
	//    el--;
	//}
	//*el = '\0';

	//assert(cont <= (c->rcurr + c->rbytes));

	process_server(c, c->rbuf);

	//c->rbytes -= (cont - c->rcurr);
	c->rbytes = 0;
	//c->rcurr = cont;

	//assert(c->rcurr <= (c->rbuf + c->rsize));

	return 1;
}

/*
 * read from network as much as we can, handle buffer overflow and connection
 * close.
 * before reading, move the remaining incomplete fragment of a command
 * (if any) to the beginning of the buffer.
 * return 0 if there's nothing to read on the first read.
 */
static int try_read_network(conn *c) {
	int gotdata = 0;
	int res;

	assert(c != NULL);

	if (c->rcurr != c->rbuf) {
		if (c->rbytes != 0) /* otherwise there's nothing to copy */
			memmove(c->rbuf, c->rcurr, c->rbytes);
		c->rcurr = c->rbuf;
	}

	while (1) {
		if (c->rbytes >= c->rsize) {
			char *new_rbuf = (char*) realloc(c->rbuf, c->rsize * 2);
			if (!new_rbuf) {
				if (settings.verbose > 0)
					fprintf(stderr, "Couldn't realloc input buffer\n");
				c->rbytes = 0; /* ignore what we read */
				out_string(c, "SERVER_ERROR out of memory reading request");
				c->write_and_go = conn_closing;
				return 1;
			}
			c->rcurr = c->rbuf = new_rbuf;
			c->rsize *= 2;
		}

		/* unix socket mode doesn't need this, so zeroed out.  but why
		 * is this done for every command?  presumably for UDP
		 * mode.  */
		if (!settings.socketpath) {
			c->request_addr_size = sizeof(c->request_addr);
		} else {
			c->request_addr_size = 0;
		}

		int avail = c->rsize - c->rbytes;
		res = read(c->sfd, c->rbuf + c->rbytes, avail);
		if (res > 0) {
			STATS_LOCK();
			stats.bytes_read += res;
			STATS_UNLOCK();
			gotdata = 1;
			c->rbytes += res;
			if (res == avail) {
				continue;
			} else {
				break;
			}
		}
		if (res == 0) {
			/* connection closed */
			conn_set_state(c, conn_closing);
			return 1;
		}
		if (res == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			/* Should close on unhandled errors. */
			conn_set_state(c, conn_closing);
			return 1;
		}
	}
	return gotdata;
}

static int try_read_tcp(conn *c) {
	int gotdata = 0;
	int res;
	int len;
	assert(c != NULL);

	memset(c->rbuf, 0x00, c->rsize);

	// head
	res = read(c->sfd, c->rbuf, Len4MsgHead);
	if (res > 0) {
		STATS_LOCK();
		stats.bytes_read += res;
		STATS_UNLOCK();
		gotdata = 1;
		c->rbytes += res;
		if (res < Len4MsgHead) {
			c->rbytes = 0;
			return 0;
		}
	}
	if (res == 0) {
		/* connection closed */
		conn_set_state(c, conn_closing);
		return 1;
	}
	if (res == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;
		/* Should close on unhandled errors. */
		conn_set_state(c, conn_closing);
		return 1;
	}

	memcpy(&len, c->rbuf + Len4MsgHead - 4, 4);
	len = ntohl(len);
	if (len < 0 || len > (DATA_BUFFER_SIZE - Len4MsgHead)) {
		c->rbytes = 0;
		return 0;
	}

	// body
	int avail = c->rsize - c->rbytes;
	res = read(c->sfd, c->rbuf + c->rbytes, len);
	if (res > 0) {
		STATS_LOCK();
		stats.bytes_read += res;
		STATS_UNLOCK();
		gotdata = 1;
		c->rbytes += res;
	}
	if (res == 0) {
		/* connection closed */
		conn_set_state(c, conn_closing);
		return 1;
	}
	if (res == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			c->rbytes = 0;
			return 0;
		}
		/* Should close on unhandled errors. */
		conn_set_state(c, conn_closing);
		return 1;
	}

	return gotdata;
}

/*
 * read a UDP request.
 * return 0 if there's nothing to read.
 */
static int try_read_udp(conn *c) {
	int res;

	assert(c != NULL);

	c->request_addr_size = sizeof(c->request_addr);
	res = recvfrom(c->sfd, c->rbuf, c->rsize, 0, &c->request_addr,
			&c->request_addr_size);
	if (res > 8) {
		unsigned char *buf = (unsigned char *) c->rbuf;
		STATS_LOCK();
		stats.bytes_read += res;
		STATS_UNLOCK();

		/* Beginning of UDP packet is the request ID; save it. */
		c->request_id = buf[0] * 256 + buf[1];

		/* If this is a multi-packet request, drop it. */
		if (buf[4] != 0 || buf[5] != 1) {
			out_string(c, "SERVER_ERROR multi-packet request not supported");
			return 0;
		}

		/* Don't care about any of the rest of the header. */
		res -= 8;
		memmove(c->rbuf, c->rbuf + 8, res);

		c->rbytes += res;
		c->rcurr = c->rbuf;
		return 1;
	}
	return 0;
}

/*
 * Frees a connection.
 */
void conn_free(conn *c) {
	if (c) {
		if (c->hdrbuf)
			free(c->hdrbuf);
		if (c->msglist)
			free(c->msglist);
		if (c->rbuf)
			free(c->rbuf);
		if (c->wbuf)
			free(c->wbuf);
//        if (c->ilist)
		//           free(c->ilist);
		if (c->suffixlist)
			free(c->suffixlist);
		if (c->iov)
			free(c->iov);
		free(c);
	}
}

static void conn_init(void) {
	freetotal = 200;
	freecurr = 0;
	if ((freeconns = (conn **) malloc(sizeof(conn *) * freetotal)) == NULL) {
		fprintf(stderr, "malloc()\n");
	}
	return;
}

/*
 * Returns a connection from the freelist, if any. Should call this using
 * conn_from_freelist() for thread safety.
 */
conn *do_conn_from_freelist() {
	conn *c;

	if (freecurr > 0) {
		c = freeconns[--freecurr];
	} else {
		c = NULL;
	}

	return c;
}

/*
 * Adds a connection to the freelist. 0 = success. Should call this using
 * conn_add_to_freelist() for thread safety.
 */
bool do_conn_add_to_freelist(conn *c) {
	if (freecurr < freetotal) {
		freeconns[freecurr++] = c;
		return false;
	} else {
		/* try to enlarge free connections array */
		conn **new_freeconns = (conn**) realloc(freeconns,
				sizeof(conn *) * freetotal * 2);
		if (new_freeconns) {
			freetotal *= 2;
			freeconns = new_freeconns;
			freeconns[freecurr++] = c;
			return false;
		}
	}
	return true;
}

conn *conn_new(const int sfd, const int init_state, const int event_flags,
		const int read_buffer_size, const bool is_udp,
		struct event_base *base) {
	conn *c = conn_from_freelist();

	if (NULL == c) {
		if (!(c = (conn *) malloc(sizeof(conn)))) {
			fprintf(stderr, "malloc()\n");
			return NULL;
		}
		c->rbuf = c->wbuf = 0;
		c->suffixlist = 0;
		c->iov = 0;
		c->msglist = 0;
		c->hdrbuf = 0;

		c->rsize = read_buffer_size;
		c->wsize = DATA_BUFFER_SIZE;
		c->suffixsize = SUFFIX_LIST_INITIAL;
		c->iovsize = IOV_LIST_INITIAL;
		c->msgsize = MSG_LIST_INITIAL;
		c->hdrsize = 0;

		c->rbuf = (char *) malloc((size_t) c->rsize);
		c->wbuf = (char *) malloc((size_t) c->wsize);
		c->suffixlist = (char **) malloc(sizeof(char *) * c->suffixsize);
		c->iov = (struct iovec *) malloc(sizeof(struct iovec) * c->iovsize);
		c->msglist = (struct msghdr *) malloc(
				sizeof(struct msghdr) * c->msgsize);

		if (c->rbuf == 0 || c->wbuf == 0 || c->iov == 0 || c->msglist == 0
				|| c->suffixlist == 0) {
			if (c->rbuf != 0)
				free(c->rbuf);
			if (c->wbuf != 0)
				free(c->wbuf);
			if (c->suffixlist != 0)
				free(c->suffixlist);
			if (c->iov != 0)
				free(c->iov);
			if (c->msglist != 0)
				free(c->msglist);
			free(c);
			fprintf(stderr, "malloc()\n");
			return NULL;
		}

		STATS_LOCK();
		stats.conn_structs++;
		STATS_UNLOCK();
	}

	if (settings.verbose > 1) {
		if (init_state == conn_listening)
			fprintf(stderr, "<%d server listening\n", sfd);
		else if (is_udp)
			fprintf(stderr, "<%d server listening (udp)\n", sfd);
		else
			fprintf(stderr, "<%d new client connection\n", sfd);
	}

	c->sfd = sfd;
	c->udp = is_udp;
	c->state = init_state;
	c->rlbytes = 0;
	c->rbytes = c->wbytes = 0;
	c->wcurr = c->wbuf;
	c->rcurr = c->rbuf;
	c->ritem = 0;
	c->suffixcurr = c->suffixlist;
	c->suffixleft = 0;
	c->iovused = 0;
	c->msgcurr = 0;
	c->msgused = 0;

	c->write_and_go = conn_read;
	c->write_and_free = 0;
	c->bucket = -1;
	c->gen = 0;

	c->noreply = false;

	event_set(&c->event, sfd, event_flags, event_handler, (void *) c);
	event_base_set(base, &c->event);
	c->ev_flags = event_flags;

	if (event_add(&c->event, 0) == -1) {
		if (conn_add_to_freelist(c)) {
			conn_free(c);
		}
		perror("event_add");
		return NULL;
	}

	STATS_LOCK();
	stats.curr_conns++;
	stats.total_conns++;
	STATS_UNLOCK();

	return c;
}

static void conn_cleanup(conn *c) {
	assert(c != NULL);

	/*if (c->item) {
	 item_remove(c->item);
	 c->item = 0;
	 }

	 if (c->ileft != 0) {
	 for (; c->ileft > 0; c->ileft--,c->icurr++) {
	 item_remove(*(c->icurr));
	 }
	 }*/

	if (c->suffixleft != 0) {
		for (; c->suffixleft > 0; c->suffixleft--, c->suffixcurr++) {
			if (suffix_add_to_freelist(*(c->suffixcurr))) {
				free(*(c->suffixcurr));
			}
		}
	}

	if (c->write_and_free) {
		free(c->write_and_free);
		c->write_and_free = 0;
	}
}

/*
 * Frees a connection.
 */
void conn_free(conn *c) {
	if (c) {
		if (c->hdrbuf)
			free(c->hdrbuf);
		if (c->msglist)
			free(c->msglist);
		if (c->rbuf)
			free(c->rbuf);
		if (c->wbuf)
			free(c->wbuf);
//        if (c->ilist)
		//           free(c->ilist);
		if (c->suffixlist)
			free(c->suffixlist);
		if (c->iov)
			free(c->iov);
		free(c);
	}
}

static void conn_close(conn *c) {
	assert(c != NULL);

	/* delete the event, the socket and the conn */
	event_del(&c->event);

	if (settings.verbose > 1)
		fprintf(stderr, "<%d connection closed.\n", c->sfd);

	close(c->sfd);
	accept_new_conns(true);
	conn_cleanup(c);

	/* if the connection has big buffers, just free it */
	if (c->rsize > READ_BUFFER_HIGHWAT || conn_add_to_freelist(c)) {
		conn_free(c);
	}

	STATS_LOCK();
	stats.curr_conns--;
	STATS_UNLOCK();

	return;
}

/*
 * Shrinks a connection's buffers if they're too big.  This prevents
 * periodic large "get" requests from permanently chewing lots of server
 * memory.
 *
 * This should only be called in between requests since it can wipe output
 * buffers!
 */
static void conn_shrink(conn *c) {
	assert(c != NULL);

	if (c->udp)
		return;

	if (c->rsize > READ_BUFFER_HIGHWAT && c->rbytes < DATA_BUFFER_SIZE) {
		char *newbuf;

		if (c->rcurr != c->rbuf)
			memmove(c->rbuf, c->rcurr, (size_t) c->rbytes);

		newbuf = (char *) realloc((void *) c->rbuf, DATA_BUFFER_SIZE);

		if (newbuf) {
			c->rbuf = newbuf;
			c->rsize = DATA_BUFFER_SIZE;
		}
		/* TODO check other branch... */
		c->rcurr = c->rbuf;
	}

	if (c->msgsize > MSG_LIST_HIGHWAT) {
		struct msghdr *newbuf = (struct msghdr *) realloc((void *) c->msglist,
				MSG_LIST_INITIAL * sizeof(c->msglist[0]));
		if (newbuf) {
			c->msglist = newbuf;
			c->msgsize = MSG_LIST_INITIAL;
		}
		/* TODO check error condition? */
	}

	if (c->iovsize > IOV_LIST_HIGHWAT) {
		struct iovec *newbuf = (struct iovec *) realloc((void *) c->iov,
				IOV_LIST_INITIAL * sizeof(c->iov[0]));
		if (newbuf) {
			c->iov = newbuf;
			c->iovsize = IOV_LIST_INITIAL;
		}
		/* TODO check return value */
	}
}

/*
 * Sets a connection's current state in the state machine. Any special
 * processing that needs to happen on certain state transitions can
 * happen here.
 */
static void conn_set_state(conn *c, int state) {
	assert(c != NULL);

	if (state != c->state) {
		if (state == conn_read) {
			conn_shrink(c);
//            assoc_move_next_bucket();
		}
		c->state = state;
	}
}

/*
 * Ensures that there is room for another struct iovec in a connection's
 * iov list.
 *
 * Returns 0 on success, -1 on out-of-memory.
 */
static int ensure_iov_space(conn *c) {
	assert(c != NULL);

	if (c->iovused >= c->iovsize) {
		int i, iovnum;
		struct iovec *new_iov = (struct iovec *) realloc(c->iov,
				(c->iovsize * 2) * sizeof(struct iovec));
		if (!new_iov)
			return -1;
		c->iov = new_iov;
		c->iovsize *= 2;

		/* Point all the msghdr structures at the new list. */
		for (i = 0, iovnum = 0; i < c->msgused; i++) {
			c->msglist[i].msg_iov = &c->iov[iovnum];
			iovnum += c->msglist[i].msg_iovlen;
		}
	}

	return 0;
}

/*
 * Adds data to the list of pending data that will be written out to a
 * connection.
 *
 * Returns 0 on success, -1 on out-of-memory.
 */

static int add_iov(conn *c, const void *buf, int len) {
	struct msghdr *m;
	int leftover;
	bool limit_to_mtu;

	assert(c != NULL);

	do {
		m = &c->msglist[c->msgused - 1];

		/*
		 * Limit UDP packets, and the first payloads of TCP replies, to
		 * UDP_MAX_PAYLOAD_SIZE bytes.
		 */
		limit_to_mtu = c->udp || (1 == c->msgused);

		/* We may need to start a new msghdr if this one is full. */
		if (m->msg_iovlen == IOV_MAX
				|| (limit_to_mtu && c->msgbytes >= UDP_MAX_PAYLOAD_SIZE)) {
			add_msghdr(c);
			m = &c->msglist[c->msgused - 1];
		}

		if (ensure_iov_space(c) != 0)
			return -1;

		/* If the fragment is too big to fit in the datagram, split it up */
		if (limit_to_mtu && len + c->msgbytes > UDP_MAX_PAYLOAD_SIZE) {
			leftover = len + c->msgbytes - UDP_MAX_PAYLOAD_SIZE;
			len -= leftover;
		} else {
			leftover = 0;
		}

		m = &c->msglist[c->msgused - 1];
		m->msg_iov[m->msg_iovlen].iov_base = (void *) buf;
		m->msg_iov[m->msg_iovlen].iov_len = len;

		c->msgbytes += len;
		c->iovused++;
		m->msg_iovlen++;

		buf = ((char *) buf) + len;
		len = leftover;
	} while (leftover > 0);

	return 0;
}


static void process_server(conn *c, char *command) {

    token_t tokens[MAX_TOKENS];
    size_t ntokens;
    int comm;

    assert(c != NULL);

    if (settings.verbose > 1)
        fprintf(stderr, "<%d %s\n", c->sfd, command);

    /*c->msgcurr = 0;
    c->msgused = 0;
    c->iovused = 0;

    if (add_msghdr(c) != 0) {
        out_string(c, "SERVER_ERROR out of memory preparing response");
        return;
    }*/

    char buf[Len4TmpString + 1] = { 0 };
	int nLen = 0;
	service_handler(c, c->rcurr, c->rbytes, buf, nLen);

    if (nLen != 0)
	    out_stringn(c, buf, nLen);

    return;
}


/*
 * if we have a complete line in the buffer, process it.
 */
static int try_read_cli(conn *c) {
    gettimeofday(&c->timestamp,NULL);
//    DEBUG_LOGGER(dsmplog, "start to read data on socket[%d]", c->sfd);
    char *el, *cont;

    assert(c != NULL);
    assert(c->rcurr <= (c->rbuf + c->rsize));

    if (c->rbytes == 0)
        return 0;
    //el = memchr(c->rcurr, '\n', c->rbytes);
    //if (!el)
    //    return 0;
    //cont = el + 1;
    //if ((el - c->rcurr) > 1 && *(el - 1) == '\r') {
    //    el--;
    //}
    //*el = '\0';

    //assert(cont <= (c->rcurr + c->rbytes));


    process_server(c, c->rbuf);

    //c->rbytes -= (cont - c->rcurr);
    c->rbytes = 0;
    //c->rcurr = cont;

    //assert(c->rcurr <= (c->rbuf + c->rsize));

    return 1;
}


/*
 * read from network as much as we can, handle buffer overflow and connection
 * close.
 * before reading, move the remaining incomplete fragment of a command
 * (if any) to the beginning of the buffer.
 * return 0 if there's nothing to read on the first read.
 */
static int try_read_network(conn *c) {
    int gotdata = 0;
    int res;

    assert(c != NULL);

    if (c->rcurr != c->rbuf) {
        if (c->rbytes != 0) /* otherwise there's nothing to copy */
            memmove(c->rbuf, c->rcurr, c->rbytes);
        c->rcurr = c->rbuf;
    }

    while (1) {
        if (c->rbytes >= c->rsize) {
            char *new_rbuf = (char*)realloc(c->rbuf, c->rsize * 2);
            if (!new_rbuf) {
                if (settings.verbose > 0)
                    fprintf(stderr, "Couldn't realloc input buffer\n");
                c->rbytes = 0; /* ignore what we read */
                out_string(c, "SERVER_ERROR out of memory reading request");
                c->write_and_go = conn_closing;
                return 1;
            }
            c->rcurr = c->rbuf = new_rbuf;
            c->rsize *= 2;
        }

        /* unix socket mode doesn't need this, so zeroed out.  but why
         * is this done for every command?  presumably for UDP
         * mode.  */
        if (!settings.socketpath) {
            c->request_addr_size = sizeof(c->request_addr);
        } else {
            c->request_addr_size = 0;
        }

        int avail = c->rsize - c->rbytes;
        res = read(c->sfd, c->rbuf + c->rbytes, avail);
        if (res > 0) {
            STATS_LOCK();
            stats.bytes_read += res;
            STATS_UNLOCK();
            gotdata = 1;
            c->rbytes += res;
            if (res == avail) {
                continue;
            } else {
                break;
            }
        }
        if (res == 0) {
            /* connection closed */
            conn_set_state(c, conn_closing);
            return 1;
        }
        if (res == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            /* Should close on unhandled errors. */
            conn_set_state(c, conn_closing);
            return 1;
        }
    }
    return gotdata;
}


static int try_read_tcp(conn *c) {
    int gotdata = 0;
    int res;
	int len;
    assert(c != NULL);

	memset(c->rbuf, 0x00, c->rsize);

    // head
	res = read(c->sfd, c->rbuf, Len4MsgHead);
    if (res > 0) {
        STATS_LOCK();
        stats.bytes_read += res;
        STATS_UNLOCK();
        gotdata = 1;
        c->rbytes += res;
        if (res < Len4MsgHead){
			c->rbytes = 0;
			return 0;
        }
    }
    if (res == 0) {
        /* connection closed */
        conn_set_state(c, conn_closing);
        return 1;
    }
    if (res == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        /* Should close on unhandled errors. */
        conn_set_state(c, conn_closing);
        return 1;
    }

    memcpy(&len, c->rbuf + Len4MsgHead - 4, 4);
    len = ntohl(len);
    if ( len < 0  || len > (DATA_BUFFER_SIZE - Len4MsgHead))
	{
	    c->rbytes = 0;
	    return 0;
	}

    // body
	int avail = c->rsize - c->rbytes;
    res = read(c->sfd, c->rbuf + c->rbytes, len);
    if (res > 0) {
        STATS_LOCK();
        stats.bytes_read += res;
        STATS_UNLOCK();
        gotdata = 1;
        c->rbytes += res;
    }
    if (res == 0) {
        /* connection closed */
        conn_set_state(c, conn_closing);
        return 1;
    }
    if (res == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK){
			c->rbytes = 0;
			return 0;
        }
        /* Should close on unhandled errors. */
        conn_set_state(c, conn_closing);
        return 1;
    }

    return gotdata;
}



/*
 * Sets whether we are listening for new connections or not.
 */
void accept_new_conns(const bool do_accept) {
    conn *next;

    if (! is_listen_thread())
        return;

    for (next = listen_conn; next; next = next->next) {
        if (do_accept) {
            update_event(next, EV_READ | EV_PERSIST);
            if (listen(next->sfd, 1024) != 0) {
                perror("listen");
            }
        }
        else {
            update_event(next, 0);
            if (listen(next->sfd, 0) != 0) {
                perror("listen");
            }
        }
  }
}


/*
 * Transmit the next chunk of data from our list of msgbuf structures.
 *
 * Returns:
 *   TRANSMIT_COMPLETE   All done writing.
 *   TRANSMIT_INCOMPLETE More data remaining to write.
 *   TRANSMIT_SOFT_ERROR Can't write any more right now.
 *   TRANSMIT_HARD_ERROR Can't write (c->state is set to conn_closing)
 */
static int transmit(conn *c) {
    assert(c != NULL);

    if (c->msgcurr < c->msgused &&
            c->msglist[c->msgcurr].msg_iovlen == 0) {
        /* Finished writing the current msg; advance to the next. */
        c->msgcurr++;
    }
    if (c->msgcurr < c->msgused) {
        ssize_t res;
        struct msghdr *m = &c->msglist[c->msgcurr];

        res = sendmsg(c->sfd, m, 0);
        if (res > 0) {
            STATS_LOCK();
            stats.bytes_written += res;
            STATS_UNLOCK();

            /* We've written some of the data. Remove the completed
               iovec entries from the list of pending writes. */
            while (m->msg_iovlen > 0 && res >= m->msg_iov->iov_len) {
                res -= m->msg_iov->iov_len;
                m->msg_iovlen--;
                m->msg_iov++;
            }

            /* Might have written just part of the last iovec entry;
               adjust it so the next write will do the rest. */
            if (res > 0) {
                m->msg_iov->iov_base = (char*)m->msg_iov->iov_base + res;
                m->msg_iov->iov_len -= res;
            }
            return TRANSMIT_INCOMPLETE;
        }
        if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!update_event(c, EV_WRITE | EV_PERSIST)) {
                if (settings.verbose > 0)
                    fprintf(stderr, "Couldn't update event\n");
                conn_set_state(c, conn_closing);
                return TRANSMIT_HARD_ERROR;
            }
            return TRANSMIT_SOFT_ERROR;
        }
        /* if res==0 or res==-1 and error is not EAGAIN or EWOULDBLOCK,
           we have a real error, on which we close the connection */
        if (settings.verbose > 0)
            perror("Failed to write, and not due to blocking");

        if (c->udp)
            conn_set_state(c, conn_read);
        else
            conn_set_state(c, conn_closing);
        return TRANSMIT_HARD_ERROR;
    } else {
        return TRANSMIT_COMPLETE;
    }
}


static int new_socket(struct addrinfo *ai) {
    int sfd;
    int flags;

    if ((sfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1) {
        perror("socket()");
        return -1;
    }

    if ((flags = fcntl(sfd, F_GETFL, 0)) < 0 ||
        fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("setting O_NONBLOCK");
        close(sfd);
        return -1;
    }
    return sfd;
}


/*
 * Sets a socket's send buffer size to the maximum allowed by the system.
 */
static void maximize_sndbuf(const int sfd) {
    socklen_t intsize = sizeof(int);
    int last_good = 0;
    int min, max, avg;
    int old_size;

    /* Start with the default size. */
    if (getsockopt(sfd, SOL_SOCKET, SO_SNDBUF, &old_size, &intsize) != 0) {
        if (settings.verbose > 0)
            perror("getsockopt(SO_SNDBUF)");
        return;
    }

    /* Binary-search for the real maximum. */
    min = old_size;
    max = MAX_SENDBUF_SIZE;

    while (min <= max) {
        avg = ((unsigned int)(min + max)) / 2;
        if (setsockopt(sfd, SOL_SOCKET, SO_SNDBUF, (void *)&avg, intsize) == 0) {
            last_good = avg;
            min = avg + 1;
        } else {
            max = avg - 1;
        }
    }

    if (settings.verbose > 1)
        fprintf(stderr, "<%d send buffer was %d, now %d\n", sfd, old_size, last_good);
}

static int server_socket(const int port, const bool is_udp) {
    int sfd;
    struct linger ling = {0, 0};
    struct addrinfo *ai;
    struct addrinfo *next;
    struct addrinfo hints;
    char port_buf[NI_MAXSERV];
    int error;
    int success = 0;

    int flags =1;

    /*
     * the memset call clears nonstandard fields in some impementations
     * that otherwise mess things up.
     */
    memset(&hints, 0, sizeof (hints));
    hints.ai_flags = AI_PASSIVE|AI_ADDRCONFIG;
    if (is_udp)
    {
        hints.ai_protocol = IPPROTO_UDP;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_family = AF_INET; /* This left here because of issues with OSX 10.5 */
    } else {
        hints.ai_family = AF_UNSPEC;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_socktype = SOCK_STREAM;
    }

    snprintf(port_buf, NI_MAXSERV, "%d", port);
    error= getaddrinfo(settings.inter, port_buf, &hints, &ai);
    if (error != 0) {
      if (error != EAI_SYSTEM)
        fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(error));
      else
        perror("getaddrinfo()");

      return 1;
    }

    for (next= ai; next; next= next->ai_next) {
        conn *listen_conn_add;
        if ((sfd = new_socket(next)) == -1) {
            freeaddrinfo(ai);
            return 1;
        }

        setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void *)&flags, sizeof(flags));
        if (is_udp) {
            maximize_sndbuf(sfd);
        } else {
            setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags));
            setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *)&ling, sizeof(ling));
            setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags));
        }

        if (bind(sfd, next->ai_addr, next->ai_addrlen) == -1) {
            if (errno != EADDRINUSE) {
                perror("bind()");
                close(sfd);
                freeaddrinfo(ai);
                return 1;
            }
            close(sfd);
            continue;
        } else {
          success++;
          if (!is_udp && listen(sfd, 1024) == -1) {
              perror("listen()");
              close(sfd);
              freeaddrinfo(ai);
              return 1;
          }
      }

      if (is_udp)
      {
        int c;

        for (c = 0; c < settings.num_threads; c++) {
            /* this is guaranteed to hit all threads because we round-robin */
            dispatch_conn_new(sfd, conn_read, EV_READ | EV_PERSIST,
                              UDP_READ_BUFFER_SIZE, 1);
        }
      }
	  else
	  {
        if (!(listen_conn_add = conn_new(sfd, conn_listening,
                                         EV_READ | EV_PERSIST, 1, false, main_base))) {
            fprintf(stderr, "failed to create listening connection\n");
            exit(EXIT_FAILURE);
        }

        listen_conn_add->next = listen_conn;
        listen_conn = listen_conn_add;
      }
    }

    freeaddrinfo(ai);

    /* Return zero iff we detected no errors in starting up connections */
    return success == 0;
}

