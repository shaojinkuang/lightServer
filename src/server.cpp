/*
 * server.cpp
 *
 *  Created on: 2012-12-8
 *      Author: Administrator
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "handler.h"
#include "logger.h"
#include "server.h"
#include "service.h"

struct event_base *main_base = NULL;		//主线程中 event_base 的实例指针
conn *listen_conn = NULL;

void drive_machine(conn *c)
{
	bool stop = false;
	int sfd, flags = 1;
	socklen_t addrlen;
	struct sockaddr_storage addr;
	struct linger ling =
	{ 1, 0 };
	int res;

	assert(c != NULL);

	while (!stop)
	{

		switch (c->state)
		{
			case conn_listening:
				addrlen = sizeof(addr);
				if ((sfd = accept(c->sfd, (struct sockaddr *) &addr, &addrlen)) == -1)
				{
					if (errno == EAGAIN || errno == EWOULDBLOCK)
					{
						/* these are transient, so don't log anything */
						stop = true;
					}
					else
						if (errno == EMFILE)
						{
							if (settings.verbose > 0)
								fprintf(stderr, "Too many open connections\n");
							accept_new_conns(false);
							stop = true;
						}
						else
						{
							perror("accept()");
							stop = true;
						}
					break;
				}
				if ((flags = fcntl(sfd, F_GETFL, 0)) < 0
						|| fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0)
				{
					perror("setting O_NONBLOCK");
					close(sfd);
					break;
				}
				DEBUG_LOGGER(dsmplog, "accept new socket[%d], wait for request", sfd);
				setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void *) &flags,
						sizeof(flags));
				setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *) &ling, sizeof(ling));
				dispatch_conn_new(sfd, conn_read, EV_READ | EV_PERSIST,
						DATA_BUFFER_SIZE, false);
				break;

			case conn_read:
				//DEBUG_LOGGER(dsmplog, "-------------socket [%d] has incoming data-------------", c->sfd);
				if (try_read_cli(c))
				{ //数据处理完毕,可以读取下一个请求包
					continue;
				}
				if (try_read_tcp(c))
				{
					continue;
				}
				/* we have no command line and no data to read from network */
				if (!update_event(c, EV_READ | EV_PERSIST))
				{
					if (settings.verbose > 0)
						fprintf(stderr, "Couldn't update event\n");
					conn_set_state(c, conn_closing);
					break;
				}
				stop = true;
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

static int new_socket(struct addrinfo *ai)
{
	int sfd;
	int flags;

	if ((sfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1)
	{
		perror("socket()");
		return -1;
	}

	if ((flags = fcntl(sfd, F_GETFL, 0)) < 0
			|| fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0)
	{
		perror("setting O_NONBLOCK");
		close(sfd);
		return -1;
	}
	return sfd;
}

static int server_socket(const int port, const bool is_udp)
{
	int sfd;
	struct linger ling =
	{ 0, 0 };
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
	if (is_udp)
	{
		hints.ai_protocol = IPPROTO_UDP;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_family = AF_INET; /* This left here because of issues with OSX 10.5 */
	}
	else
	{
		hints.ai_family = AF_UNSPEC;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_socktype = SOCK_STREAM;
	}

	snprintf(port_buf, NI_MAXSERV, "%d", port);
	error = getaddrinfo(settings.inter, port_buf, &hints, &ai);
	if (error != 0)
	{
		if (error != EAI_SYSTEM)
			fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(error));
		else
			perror("getaddrinfo()");

		return 1;
	}

	for (next = ai; next; next = next->ai_next)
	{
		conn *listen_conn_add;
		if ((sfd = new_socket(next)) == -1)
		{
			freeaddrinfo(ai);
			return 1;
		}

		setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void *) &flags, sizeof(flags));
		if (is_udp)
		{
			maximize_sndbuf(sfd);
		}
		else
		{
			setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, (void *) &flags, sizeof(flags));
			setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *) &ling, sizeof(ling));
			setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, (void *) &flags, sizeof(flags));
		}

		if (bind(sfd, next->ai_addr, next->ai_addrlen) == -1)
		{
			if (errno != EADDRINUSE)
			{
				perror("bind()");
				close(sfd);
				freeaddrinfo(ai);
				return 1;
			}
			close(sfd);
			continue;
		}
		else
		{
			success++;
			if (!is_udp && listen(sfd, 1024) == -1)
			{
				perror("listen()");
				close(sfd);
				freeaddrinfo(ai);
				return 1;
			}
		}

		if (is_udp)
		{
			int c;

			for (c = 0; c < settings.num_threads; c++)
			{
				/* this is guaranteed to hit all threads because we round-robin */
				dispatch_conn_new(sfd, conn_read, EV_READ | EV_PERSIST,
						UDP_READ_BUFFER_SIZE, 1);
			}
		}
		else
		{
			if (!(listen_conn_add = conn_new(sfd, conn_listening,
					EV_READ | EV_PERSIST, 1, false, main_base)))
			{
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
/**
 *@brief 处理客户端请求包
 *@return 0 数据包不完整，需要继续读取           1 数据包完整，处理完数据包
 */
static int try_read_cli(conn *c)
{
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

/**
 *@brief 读数据
 *@return 0 没有读取到完整的包 , 此时置 rbytes 为0
 *@return 非0       情况一，读取成功返回的字节
 *@return 非0（即1） 情况二，读取失败，返回1并将连接状态置为conn_closing
 */
static int try_read_tcp(conn *c)
{
	int gotdata = 0;
	int res;
	int len;
	assert(c != NULL);

	memset(c->rbuf, 0x00, c->rsize);

	// head
	res = read(c->sfd, c->rbuf, Len4MsgHead);
	if (res == 0)
	{
		/* connection closed */
		conn_set_state(c, conn_closing);
		return 1;
	}
	if (res < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;
		/* Should close on unhandled errors. */
		conn_set_state(c, conn_closing);
		return 1;
	}

	gotdata = 1;
	c->rbytes += res;
	if (res < Len4MsgHead)
	{
		c->rbytes = 0;
		return 0;
	}

	memcpy(&len, c->rbuf + Len4MsgHead - 4, 4);
	len = ntohl(len);
	if (len < 0 || len > (DATA_BUFFER_SIZE - Len4MsgHead))
	{
		c->rbytes = 0;
		return 0;
	}

	// body
	int avail = c->rsize - c->rbytes;
	res = read(c->sfd, c->rbuf + c->rbytes, len);
	if (res == 0)
	{
		/* connection closed */
		conn_set_state(c, conn_closing);
		return 1;
	}
	if (res < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			c->rbytes = 0;
			return 0;
		}
		/* Should close on unhandled errors. */
		conn_set_state(c, conn_closing);
		return 1;
	}

	gotdata = 1;
	c->rbytes += res;

	return gotdata;
}

/*
 * Frees a connection.
 */
void conn_free(conn *c)
{
	if (c)
	{
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

static void conn_init(void)
{
	freetotal = 200;
	freecurr = 0;
	if ((freeconns = (conn **) malloc(sizeof(conn *) * freetotal)) == NULL)
	{
		fprintf(stderr, "malloc()\n");
	}
	return;
}

///*
// * Returns a connection from the freelist, if any. Should call this using
// * conn_from_freelist() for thread safety.
// */
conn *do_conn_from_freelist()
{
	conn *c;

	if (freecurr > 0)
	{
		c = freeconns[--freecurr];
	}
	else
	{
		c = NULL;
	}

	return c;
}

/*
 * Adds a connection to the freelist. 0 = success. Should call this using
 * conn_add_to_freelist() for thread safety.
 */
bool do_conn_add_to_freelist(conn *c)
{
	if (freecurr < freetotal)
	{
		freeconns[freecurr++] = c;
		return false;
	}
	else
	{
		/* try to enlarge free connections array */
		conn **new_freeconns = (conn**) realloc(freeconns,
				sizeof(conn *) * freetotal * 2);
		if (new_freeconns)
		{
			freetotal *= 2;
			freeconns = new_freeconns;
			freeconns[freecurr++] = c;
			return false;
		}
	}
	return true;
}

conn *conn_new(const int sfd, const int init_state, const int event_flags,
		const int read_buffer_size, const bool is_udp, struct event_base *base)
{
	conn *c = conn_from_freelist();

	if (NULL == c)
	{
		if (!(c = (conn *) malloc(sizeof(conn))))
		{
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
		c->msglist = (struct msghdr *) malloc(sizeof(struct msghdr) * c->msgsize);

		if (c->rbuf == 0 || c->wbuf == 0 || c->iov == 0 || c->msglist == 0
				|| c->suffixlist == 0)
		{
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

	}

	if (settings.verbose > 1)
	{
		if (init_state == conn_listening)
			fprintf(stderr, "<%d server listening\n", sfd);
		else
			if (is_udp)
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

	if (event_add(&c->event, 0) == -1)
	{
		if (conn_add_to_freelist(c))
		{
			conn_free(c);
		}
		perror("event_add");
		return NULL;
	}

	return c;
}

static void conn_cleanup(conn *c)
{
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

	if (c->suffixleft != 0)
	{
		for (; c->suffixleft > 0; c->suffixleft--, c->suffixcurr++)
		{
			if (suffix_add_to_freelist(*(c->suffixcurr)))
			{
				free(*(c->suffixcurr));
			}
		}
	}

	if (c->write_and_free)
	{
		free(c->write_and_free);
		c->write_and_free = 0;
	}
}

void conn_close(conn *c)
{
	assert(c != NULL);

	/* delete the event, the socket and the conn */
	event_del(&c->event);

	if (settings.verbose > 1)
		fprintf(stderr, "<%d connection closed.\n", c->sfd);

	close(c->sfd);
	accept_new_conns(true);
	conn_cleanup(c);

	/* if the connection has big buffers, just free it */
	if (c->rsize > READ_BUFFER_HIGHWAT || conn_add_to_freelist(c))
	{
		conn_free(c);
	}

	return;
}

///*
// * Shrinks a connection's buffers if they're too big.  This prevents
// * periodic large "get" requests from permanently chewing lots of server
// * memory.
// *
// * This should only be called in between requests since it can wipe output
// * buffers!
// */

static void conn_shrink(conn *c)
{
	assert(c != NULL);

	if (c->udp)
		return;

	if (c->rsize > READ_BUFFER_HIGHWAT && c->rbytes < DATA_BUFFER_SIZE)
	{
		char *newbuf;

		if (c->rcurr != c->rbuf)
			memmove(c->rbuf, c->rcurr, (size_t) c->rbytes);

		newbuf = (char *) realloc((void *) c->rbuf, DATA_BUFFER_SIZE);

		if (newbuf)
		{
			c->rbuf = newbuf;
			c->rsize = DATA_BUFFER_SIZE;
		}
		/* TODO check other branch... */
		c->rcurr = c->rbuf;
	}

	if (c->msgsize > MSG_LIST_HIGHWAT)
	{
		struct msghdr *newbuf = (struct msghdr *) realloc((void *) c->msglist,
				MSG_LIST_INITIAL * sizeof(c->msglist[0]));
		if (newbuf)
		{
			c->msglist = newbuf;
			c->msgsize = MSG_LIST_INITIAL;
		}
		/* TODO check error condition? */
	}

	if (c->iovsize > IOV_LIST_HIGHWAT)
	{
		struct iovec *newbuf = (struct iovec *) realloc((void *) c->iov,
				IOV_LIST_INITIAL * sizeof(c->iov[0]));
		if (newbuf)
		{
			c->iov = newbuf;
			c->iovsize = IOV_LIST_INITIAL;
		}
		/* TODO check return value */
	}
}

///*
// * Sets a connection's current state in the state machine. Any special
// * processing that needs to happen on certain state transitions can
// * happen here.
// */

static void conn_set_state(conn *c, int state)
{
	assert(c != NULL);

	if (state != c->state)
	{
		if (state == conn_read)
		{
			conn_shrink(c);
//            assoc_move_next_bucket();
		}
		c->state = state;
	}
}

///*
// * Ensures that there is room for another struct iovec in a connection's
// * iov list.
// *
// * Returns 0 on success, -1 on out-of-memory.
// */

//static int ensure_iov_space(conn *c) {
//	assert(c != NULL);
//
//	if (c->iovused >= c->iovsize) {
//		int i, iovnum;
//		struct iovec *new_iov = (struct iovec *) realloc(c->iov,
//				(c->iovsize * 2) * sizeof(struct iovec));
//		if (!new_iov)
//			return -1;
//		c->iov = new_iov;
//		c->iovsize *= 2;
//
//		/* Point all the msghdr structures at the new list. */
//		for (i = 0, iovnum = 0; i < c->msgused; i++) {
//			c->msglist[i].msg_iov = &c->iov[iovnum];
//			iovnum += c->msglist[i].msg_iovlen;
//		}
//	}
//
//	return 0;
//}

///*
// * Adds data to the list of pending data that will be written out to a
// * connection.
// *
// * Returns 0 on success, -1 on out-of-memory.
// */

static void process_server(conn *c, char *command)
{

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

	char buf[Len4TmpString + 1] =
	{ 0 };
	int nLen = 0;
	service_handler(c, c->rcurr, c->rbytes, buf, nLen);

	if (nLen != 0)
		out_stringn(c, buf, nLen);

	return;
}



/*
 * Sets whether we are listening for new connections or not.
 */

void accept_new_conns(const bool do_accept)
{
	conn *next;

	if (!is_listen_thread())
		return;

	for (next = listen_conn; next; next = next->next)
	{
		if (do_accept)
		{
			update_event(next, EV_READ | EV_PERSIST);
			if (listen(next->sfd, 1024) != 0)
			{
				perror("listen");
			}
		}
		else
		{
			update_event(next, 0);
			if (listen(next->sfd, 0) != 0)
			{
				perror("listen");
			}
		}
	}
}

static void maximize_sndbuf(const int sfd)
{
	socklen_t intsize = sizeof(int);
	int last_good = 0;
	int min, max, avg;
	int old_size;

	/* Start with the default size. */
	if (getsockopt(sfd, SOL_SOCKET, SO_SNDBUF, &old_size, &intsize) != 0)
	{
		if (settings.verbose > 0)
			perror("getsockopt(SO_SNDBUF)");
		return;
	}

	/* Binary-search for the real maximum. */
	min = old_size;
	max = MAX_SENDBUF_SIZE;

	while (min <= max)
	{
		avg = ((unsigned int) (min + max)) / 2;
		if (setsockopt(sfd, SOL_SOCKET, SO_SNDBUF, (void *) &avg, intsize) == 0)
		{
			last_good = avg;
			min = avg + 1;
		}
		else
		{
			max = avg - 1;
		}
	}

	if (settings.verbose > 1)
		fprintf(stderr, "<%d send buffer was %d, now %d\n", sfd, old_size,
				last_good);
}

static void out_stringn(conn *c, const char *str, int len)
{

	assert(c != NULL);

	if (c->noreply)
	{
		if (settings.verbose > 1)
			fprintf(stderr, ">%d NOREPLY %s\n", c->sfd, str);
		c->noreply = false;
		conn_set_state(c, conn_read);
		return;
	}

	if (settings.verbose > 1)
		fprintf(stderr, ">%d %s\n", c->sfd, str);

	if (len > c->wsize)
	{
		/* ought to be always enough. just fail for simplicity */
		str = "SERVER_ERROR output line too long\r\n";
		len = strlen(str);
	}

	//memcpy(c->wbuf, str, len);
	//c->wbytes = len;
	//c->wcurr = c->wbuf;

	send(c->sfd, str, len, 0);
	conn_set_state(c, conn_read);
	//conn_set_state(c, conn_write);
	//c->write_and_go = conn_read;

	struct timeval now;
	gettimeofday(&now, NULL);
	int nTime = (now.tv_sec - c->timestamp.tv_sec) * 1000
			+ (now.tv_usec - c->timestamp.tv_usec) / 1000;
//    DEBUG_LOGGER(dsmplog, "succeed to output response to client, total[%d]", nTime);
	return;
}
