/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
//#include "dsmp.h"
#include "service.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <config.h>
#include "logger.h"

/* some POSIX systems need the following definition
 * to get mlockall flags out of sys/mman.h.  */
#ifndef _P1003_1B_VISIBLE
#define _P1003_1B_VISIBLE
#endif
/* need this to get IOV_MAX on some platforms. */
#ifndef __need_IOV_MAX
#define __need_IOV_MAX
#endif
#include <pwd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <limits.h>

#ifdef HAVE_MALLOC_H
/* OpenBSD has a malloc.h, but warns to use stdlib.h instead */
#ifndef __OpenBSD__
#include <malloc.h>
#endif
#endif

/* FreeBSD 4.x doesn't have IOV_MAX exposed. */
#ifndef IOV_MAX
#if defined(__FreeBSD__) || defined(__APPLE__)
# define IOV_MAX 1024
#endif
#endif

/*
 * forward declarations
 */
static void drive_machine(conn *c);
static int new_socket(struct addrinfo *ai);
static int server_socket(const int port, const bool is_udp);
static int try_read_cli(conn *c);
static int try_read_network(conn *c);
static int try_read_tcp(conn *c);
static int try_read_udp(conn *c);


/* defaults */
static void settings_init(void);

/* event handling, network IO */
static void event_handler(const int fd, const short which, void *arg);
static void conn_close(conn *c);
static void conn_init(void);
static void accept_new_conns(const bool do_accept);
static bool update_event(conn *c, const int new_flags);
static void process_command(conn *c, char *command);
static int transmit(conn *c);
static int ensure_iov_space(conn *c);
static int add_iov(conn *c, const void *buf, int len);
static int add_msghdr(conn *c);

/* time handling */
static void set_current_time(void);  /* update the global variable holding
                              global 32-bit seconds-since-start time
                              (to avoid 64 bit time_t) */

static void conn_free(conn *c);

/** exported globals **/
struct stats stats;
struct settings settings;
LOGGER* dsmplog;

/** file scope variables **/
static item **todelete = NULL;
static int delcurr;
static int deltotal;
static conn *listen_conn = NULL;
static struct event_base *main_base;

#define TRANSMIT_COMPLETE   0
#define TRANSMIT_INCOMPLETE 1
#define TRANSMIT_SOFT_ERROR 2
#define TRANSMIT_HARD_ERROR 3

static int *buckets = 0; /* bucket->generation array for a managed instance */

#define REALTIME_MAXDELTA 60*60*24*30
/*
 * given time value that's either unix time or delta from current unix time, return
 * unix time. Use the fact that delta can't exceed one month (and real time value can't
 * be that low).
 */
static rel_time_t realtime(const time_t exptime) {
    /* no. of seconds in 30 days - largest possible delta exptime */

    if (exptime == 0) return 0; /* 0 means never expire */

    if (exptime > REALTIME_MAXDELTA) {
        /* if item expiration is at/before the server started, give it an
           expiration time of 1 second after the server started.
           (because 0 means don't expire).  without this, we'd
           underflow and wrap around to some large value way in the
           future, effectively making items expiring in the past
           really expiring never */
        if (exptime <= stats.started)
            return (rel_time_t)1;
        return (rel_time_t)(exptime - stats.started);
    } else {
        return (rel_time_t)(exptime + current_time);
    }
}

static void settings_init(void) {
    settings.access=0700;
    settings.port = 1314;
    settings.udpport = 0;
    /* By default this string should be NULL for getaddrinfo() */
    settings.inter = NULL;
    settings.maxbytes = 64 * 1024 * 1024; /* default is 64MB */
    settings.maxconns = 1024;         /* to limit connections-related memory to about 5MB */
    settings.verbose = 1;
    settings.oldest_live = 0;
    settings.evict_to_free = 1;       /* push old items out of cache when memory runs out */
    settings.socketpath = NULL;       /* by default, not using a unix socket */
    settings.managed = false;
    settings.factor = 1.25;
    settings.chunk_size = 48;         /* space for a modest key and value */

    settings.num_threads = 10;

    settings.prefix_delimiter = ':';
    settings.detail_enabled = 0;

    settings.nActiveMQ = 0;
    settings.nRetry = 0;
}

/* returns true if a deleted item's delete-locked-time is over, and it
   should be removed from the namespace */
static bool item_delete_lock_over (item *it) {
    assert(it->it_flags & ITEM_DELETED);
    return (current_time >= it->exptime);
}

/*
 * Adds a message header to a connection.
 *
 * Returns 0 on success, -1 on out-of-memory.
 */
static int add_msghdr(conn *c)
{
    struct msghdr *msg;

    assert(c != NULL);

    if (c->msgsize == c->msgused) {
        msg = (struct msghdr*)realloc(c->msglist, c->msgsize * 2 * sizeof(struct msghdr));
        if (! msg)
            return -1;
        c->msglist = msg;
        c->msgsize *= 2;
    }

    msg = c->msglist + c->msgused;

    /* this wipes msg_iovlen, msg_control, msg_controllen, and
       msg_flags, the last 3 of which aren't defined on solaris: */
    memset(msg, 0, sizeof(struct msghdr));

    msg->msg_iov = &c->iov[c->iovused];

    if (c->request_addr_size > 0) {
        msg->msg_name = &c->request_addr;
        msg->msg_namelen = c->request_addr_size;
    }

    c->msgbytes = 0;
    c->msgused++;

    if (c->udp) {
        /* Leave room for the UDP header, which we'll fill in later. */
        return add_iov(c, NULL, UDP_HEADER_SIZE);
    }

    return 0;
}


/*
 * Free list management for connections.
 */

static conn **freeconns;
static int freetotal;
static int freecurr;


static void conn_init(void) {
    freetotal = 200;
    freecurr = 0;
    if ((freeconns = (conn **)malloc(sizeof(conn *) * freetotal)) == NULL) {
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
        conn **new_freeconns = (conn**)realloc(freeconns, sizeof(conn *) * freetotal * 2);
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
                const int read_buffer_size, const bool is_udp, struct event_base *base) {
    conn *c = conn_from_freelist();

    if (NULL == c) {
        if (!(c = (conn *)malloc(sizeof(conn)))) {
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

        c->rbuf = (char *)malloc((size_t)c->rsize);
        c->wbuf = (char *)malloc((size_t)c->wsize);
        c->suffixlist = (char **)malloc(sizeof(char *) * c->suffixsize);
        c->iov = (struct iovec *)malloc(sizeof(struct iovec) * c->iovsize);
        c->msglist = (struct msghdr *)malloc(sizeof(struct msghdr) * c->msgsize);

        if (c->rbuf == 0 || c->wbuf == 0  || c->iov == 0 ||
                c->msglist == 0 || c->suffixlist == 0) {
            if (c->rbuf != 0) free(c->rbuf);
            if (c->wbuf != 0) free(c->wbuf);
            if (c->suffixlist != 0) free(c->suffixlist);
            if (c->iov != 0) free(c->iov);
            if (c->msglist != 0) free(c->msglist);
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

    event_set(&c->event, sfd, event_flags, event_handler, (void *)c);
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
            if(suffix_add_to_freelist(*(c->suffixcurr))) {
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
            memmove(c->rbuf, c->rcurr, (size_t)c->rbytes);

        newbuf = (char *)realloc((void *)c->rbuf, DATA_BUFFER_SIZE);

        if (newbuf) {
            c->rbuf = newbuf;
            c->rsize = DATA_BUFFER_SIZE;
        }
        /* TODO check other branch... */
        c->rcurr = c->rbuf;
    }

    if (c->msgsize > MSG_LIST_HIGHWAT) {
        struct msghdr *newbuf = (struct msghdr *) realloc((void *)c->msglist, MSG_LIST_INITIAL * sizeof(c->msglist[0]));
        if (newbuf) {
            c->msglist = newbuf;
            c->msgsize = MSG_LIST_INITIAL;
        }
    /* TODO check error condition? */
    }

    if (c->iovsize > IOV_LIST_HIGHWAT) {
        struct iovec *newbuf = (struct iovec *) realloc((void *)c->iov, IOV_LIST_INITIAL * sizeof(c->iov[0]));
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
 * Free list management for suffix buffers.
 */

static char **freesuffix;
static int freesuffixtotal;
static int freesuffixcurr;

static void suffix_init(void) {
    freesuffixtotal = 500;
    freesuffixcurr  = 0;

    freesuffix = (char **)malloc( sizeof(char *) * freesuffixtotal );
    if (freesuffix == NULL) {
        fprintf(stderr, "malloc()\n");
    }
    return;
}

/*
 * Returns a suffix buffer from the freelist, if any. Should call this using
 * suffix_from_freelist() for thread safety.
 */
char *do_suffix_from_freelist() {
    char *s;

    if (freesuffixcurr > 0) {
        s = freesuffix[--freesuffixcurr];
    } else {
        /* If malloc fails, let the logic fall through without spamming
         * STDERR on the server. */
        s = (char*)malloc( SUFFIX_SIZE );
    }

    return s;
}

/*
 * Adds a connection to the freelist. 0 = success. Should call this using
 * conn_add_to_freelist() for thread safety.
 */
bool do_suffix_add_to_freelist(char *s) {
    if (freesuffixcurr < freesuffixtotal) {
        freesuffix[freesuffixcurr++] = s;
        return false;
    } else {
        /* try to enlarge free connections array */
        char **new_freesuffix = (char**)realloc(freesuffix, freesuffixtotal * 2);
        if (new_freesuffix) {
            freesuffixtotal *= 2;
            freesuffix = new_freesuffix;
            freesuffix[freesuffixcurr++] = s;
            return false;
        }
    }
    return true;
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
        struct iovec *new_iov = (struct iovec *)realloc(c->iov,
                                (c->iovsize * 2) * sizeof(struct iovec));
        if (! new_iov)
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
        if (m->msg_iovlen == IOV_MAX ||
            (limit_to_mtu && c->msgbytes >= UDP_MAX_PAYLOAD_SIZE)) {
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
        m->msg_iov[m->msg_iovlen].iov_base = (void *)buf;
        m->msg_iov[m->msg_iovlen].iov_len = len;

        c->msgbytes += len;
        c->iovused++;
        m->msg_iovlen++;

        buf = ((char *)buf) + len;
        len = leftover;
    } while (leftover > 0);

    return 0;
}


/*
 * Constructs a set of UDP headers and attaches them to the outgoing messages.
 */
static int build_udp_headers(conn *c) {
    int i;
    unsigned char *hdr;

    assert(c != NULL);

    if (c->msgused > c->hdrsize) {
        void *new_hdrbuf;
        if (c->hdrbuf)
            new_hdrbuf = realloc(c->hdrbuf, c->msgused * 2 * UDP_HEADER_SIZE);
        else
            new_hdrbuf = malloc(c->msgused * 2 * UDP_HEADER_SIZE);
        if (! new_hdrbuf)
            return -1;
        c->hdrbuf = (unsigned char *)new_hdrbuf;
        c->hdrsize = c->msgused * 2;
    }

    hdr = c->hdrbuf;
    for (i = 0; i < c->msgused; i++) {
        c->msglist[i].msg_iov[0].iov_base = hdr;
        c->msglist[i].msg_iov[0].iov_len = UDP_HEADER_SIZE;
        *hdr++ = c->request_id / 256;
        *hdr++ = c->request_id % 256;
        *hdr++ = i / 256;
        *hdr++ = i % 256;
        *hdr++ = c->msgused / 256;
        *hdr++ = c->msgused % 256;
        *hdr++ = 0;
        *hdr++ = 0;
        //pointer of type `void *' used in arithmetic
        assert((char *) hdr == (char *)c->msglist[i].msg_iov[0].iov_base + UDP_HEADER_SIZE);
    }

    return 0;
}


static void out_string(conn *c, const char *str) {
    size_t len;

    assert(c != NULL);

    if (c->noreply) {
        if (settings.verbose > 1)
            fprintf(stderr, ">%d NOREPLY %s\n", c->sfd, str);
        c->noreply = false;
        conn_set_state(c, conn_read);
        return;
    }

    if (settings.verbose > 1)
        fprintf(stderr, ">%d %s\n", c->sfd, str);

    len = strlen(str);
    if ((len + 2) > c->wsize) {
        /* ought to be always enough. just fail for simplicity */
        str = "SERVER_ERROR output line too long";
        len = strlen(str);
    }

    memcpy(c->wbuf, str, len);
    memcpy(c->wbuf + len, "\r\n", 3);
    c->wbytes = len + 2;
    c->wcurr = c->wbuf;

    conn_set_state(c, conn_write);
    c->write_and_go = conn_read;
    return;
}

static void out_stringn(conn *c, const char *str, int len) {

    assert(c != NULL);

    if (c->noreply) {
        if (settings.verbose > 1)
            fprintf(stderr, ">%d NOREPLY %s\n", c->sfd, str);
        c->noreply = false;
        conn_set_state(c, conn_read);
        return;
    }

    if (settings.verbose > 1)
        fprintf(stderr, ">%d %s\n", c->sfd, str);

    if (len  > c->wsize) {
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
    gettimeofday(&now,NULL); 
    int nTime = (now.tv_sec - c->timestamp.tv_sec)*1000+  (now.tv_usec - c->timestamp.tv_usec)/1000; 
//    DEBUG_LOGGER(dsmplog, "succeed to output response to client, total[%d]", nTime);
    return;
}

typedef struct token_s {
    char *value;
    size_t length;
} token_t;

#define COMMAND_TOKEN 0
#define SUBCOMMAND_TOKEN 1
#define KEY_TOKEN 1
#define KEY_MAX_LENGTH 250

#define MAX_TOKENS 8

/*
 * Tokenize the command string by replacing whitespace with '\0' and update
 * the token array tokens with pointer to start of each token and length.
 * Returns total number of tokens.  The last valid token is the terminal
 * token (value points to the first unprocessed character of the string and
 * length zero).
 *
 * Usage example:
 *
 *  while(tokenize_command(command, ncommand, tokens, max_tokens) > 0) {
 *      for(int ix = 0; tokens[ix].length != 0; ix++) {
 *          ...
 *      }
 *      ncommand = tokens[ix].value - command;
 *      command  = tokens[ix].value;
 *   }
 */
static size_t tokenize_command(char *command, token_t *tokens, const size_t max_tokens) {
    char *s, *e;
    size_t ntokens = 0;

    assert(command != NULL && tokens != NULL && max_tokens > 1);

    for (s = e = command; ntokens < max_tokens - 1; ++e) {
        if (*e == ' ') {
            if (s != e) {
                tokens[ntokens].value = s;
                tokens[ntokens].length = e - s;
                ntokens++;
                *e = '\0';
            }
            s = e + 1;
        }
        else if (*e == '\0') {
            if (s != e) {
                tokens[ntokens].value = s;
                tokens[ntokens].length = e - s;
                ntokens++;
            }

            break; /* string end */
        }
    }

    /*
     * If we scanned the whole string, the terminal value pointer is null,
     * otherwise it is the first unprocessed character.
     */
    tokens[ntokens].value =  *e == '\0' ? NULL : e;
    tokens[ntokens].length = 0;
    ntokens++;

    return ntokens;
}

/* set up a connection to write a buffer then free it, used for stats */
static void write_and_free(conn *c, char *buf, int bytes) {
    if (buf) {
        c->write_and_free = buf;
        c->wcurr = buf;
        c->wbytes = bytes;
        conn_set_state(c, conn_write);
        c->write_and_go = conn_read;
    } else {
        out_string(c, "SERVER_ERROR out of memory writing stats");
    }
}

static inline void set_noreply_maybe(conn *c, token_t *tokens, size_t ntokens)
{
    int noreply_index = ntokens - 2;

    /*
      NOTE: this function is not the first place where we are going to
      send the reply.  We could send it instead from process_command()
      if the request line has wrong number of tokens.  However parsing
      malformed line for "noreply" option is not reliable anyway, so
      it can't be helped.
    */
    if (tokens[noreply_index].value
        && strcmp(tokens[noreply_index].value, "noreply") == 0) {
        c->noreply = true;
    }
}

/*
 * Adds an item to the deferred-delete list so it can be reaped later.
 *
 * Returns the result to send to the client.
 */
char *do_defer_delete(item *it, time_t exptime)
{
    if (delcurr >= deltotal) {
        item **new_delete = (item **)realloc(todelete, sizeof(item *) * deltotal * 2);
        if (new_delete) {
            todelete = new_delete;
            deltotal *= 2;
        } else {
            /*
             * can't delete it immediately, user wants a delay,
             * but we ran out of memory for the delete queue
             */
//            item_remove(it);    /* release reference */
            return "SERVER_ERROR out of memory expanding delete queue";
        }
    }

    /* use its expiration time as its deletion time now */
    it->exptime = realtime(exptime);
    it->it_flags |= ITEM_DELETED;
    todelete[delcurr++] = it;

    return "DELETED";
}

static void process_verbosity_command(conn *c, token_t *tokens, const size_t ntokens) {
    unsigned int level;

    assert(c != NULL);

    set_noreply_maybe(c, tokens, ntokens);

    level = strtoul(tokens[1].value, NULL, 10);
    settings.verbose = level > MAX_VERBOSITY_LEVEL ? MAX_VERBOSITY_LEVEL : level;
    out_string(c, "OK");
    return;
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
 * read a UDP request.
 * return 0 if there's nothing to read.
 */
static int try_read_udp(conn *c) {
    int res;

    assert(c != NULL);

    c->request_addr_size = sizeof(c->request_addr);
    res = recvfrom(c->sfd, c->rbuf, c->rsize,
                   0, &c->request_addr, &c->request_addr_size);
    if (res > 8) {
        unsigned char *buf = (unsigned char *)c->rbuf;
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

static bool update_event(conn *c, const int new_flags) {
    assert(c != NULL);

    struct event_base *base = c->event.ev_base;
    if (c->ev_flags == new_flags)
        return true;
    if (event_del(&c->event) == -1) return false;
    event_set(&c->event, c->sfd, new_flags, event_handler, (void *)c);
    event_base_set(base, &c->event);
    c->ev_flags = new_flags;
    if (event_add(&c->event, 0) == -1) return false;
    return true;
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

static void drive_machine(conn *c) {
    bool stop = false;
    int sfd, flags = 1;
    socklen_t addrlen;
    struct sockaddr_storage addr;
	struct linger ling = {1, 0};
    int res;

    assert(c != NULL);

    while (!stop) {

        switch(c->state) {
        case conn_listening:
            addrlen = sizeof(addr);
            if ((sfd = accept(c->sfd, (struct sockaddr *)&addr, &addrlen)) == -1) {
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
            if ((flags = fcntl(sfd, F_GETFL, 0)) < 0 ||
                fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
                perror("setting O_NONBLOCK");
                close(sfd);
                break;
            }
            DEBUG_LOGGER(dsmplog, "accept new socket[%d], wait for request", sfd);
			setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void *)&flags, sizeof(flags));
			setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *)&ling, sizeof(ling));
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
            res = read(c->sfd, c->rbuf, c->rsize > c->sbytes ? c->sbytes : c->rsize);
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
                if (add_iov(c, c->wcurr, c->wbytes) != 0 ||
                    (c->udp && build_udp_headers(c) != 0)) {
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
                        if(suffix_add_to_freelist(suffix)) {
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
                break;                   /* Continue in state machine. */

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

void event_handler(const int fd, const short which, void *arg) {
    conn *c;

    c = (conn *)arg;
    assert(c != NULL);

    c->which = which;

    /* sanity */
    if (fd != c->sfd) {
        if (settings.verbose > 0)
            fprintf(stderr, "Catastrophic: event fd doesn't match conn fd!\n");
        conn_close(c);
        return;
    }

    drive_machine(c);

    /* wait for next event */
    return;
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

static int new_socket_unix(void) {
    int sfd;
    int flags;

    if ((sfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
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

static int server_socket_unix(const char *path, int access_mask) {
    int sfd;
    struct linger ling = {0, 0};
    struct sockaddr_un addr;
    struct stat tstat;
    int flags =1;
    int old_umask;

    if (!path) {
        return 1;
    }

    if ((sfd = new_socket_unix()) == -1) {
        return 1;
    }

    /*
     * Clean up a previous socket file if we left it around
     */
    if (lstat(path, &tstat) == 0) {
        if (S_ISSOCK(tstat.st_mode))
            unlink(path);
    }

    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void *)&flags, sizeof(flags));
    setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags));
    setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *)&ling, sizeof(ling));

    /*
     * the memset call clears nonstandard fields in some impementations
     * that otherwise mess things up.
     */
    memset(&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);
    old_umask=umask( ~(access_mask&0777));
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind()");
        close(sfd);
        umask(old_umask);
        return 1;
    }
    umask(old_umask);
    if (listen(sfd, 1024) == -1) {
        perror("listen()");
        close(sfd);
        return 1;
    }
    if (!(listen_conn = conn_new(sfd, conn_listening,
                                     EV_READ | EV_PERSIST, 1, false, main_base))) {
        fprintf(stderr, "failed to create listening connection\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}

/*
 * We keep the current time of day in a global variable that's updated by a
 * timer event. This saves us a bunch of time() system calls (we really only
 * need to get the time once a second, whereas there can be tens of thousands
 * of requests a second) and allows us to use server-start-relative timestamps
 * rather than absolute UNIX timestamps, a space savings on systems where
 * sizeof(time_t) > sizeof(unsigned int).
 */
volatile rel_time_t current_time;
static struct event clockevent;
static struct event tryevent;

/* time-sensitive callers can call it by hand with this, outside the normal ever-1-second timer */
static void set_current_time(void) {
    struct timeval timer;

    gettimeofday(&timer, NULL);
    current_time = (rel_time_t) (timer.tv_sec - stats.started);
}

static void clock_handler(const int fd, const short which, void *arg) {
    struct timeval t = {1, 0};
    static bool initialized = false;

    if (initialized) {
        /* only delete the event if it's actually there. */
        evtimer_del(&clockevent);
    } else {
        initialized = true;
    }

    evtimer_set(&clockevent, clock_handler, 0);
    event_base_set(main_base, &clockevent);
    evtimer_add(&clockevent, &t);

    set_current_time();
}

static void try_handler(const int fd, const short which, void *arg) {
    struct timeval t = {10, 0};
    static bool initialized = false;

    if (initialized) {
        /* only delete the event if it's actually there. */
        evtimer_del(&tryevent);
    } else {
        initialized = true;
    }

    evtimer_set(&tryevent, try_handler, 0);
    event_base_set(main_base, &tryevent);
    evtimer_add(&tryevent, &t);
}

static void save_pid(const pid_t pid, const char *pid_file) {
    FILE *fp;
    if (pid_file == NULL)
        return;

    if ((fp = fopen(pid_file, "w")) == NULL) {
        fprintf(stderr, "Could not open the pid file %s for writing\n", pid_file);
        return;
    }

    fprintf(fp,"%ld\n", (long)pid);
    if (fclose(fp) == -1) {
        fprintf(stderr, "Could not close the pid file %s.\n", pid_file);
        return;
    }
}

static void remove_pidfile(const char *pid_file) {
  if (pid_file == NULL)
      return;

  if (unlink(pid_file) != 0) {
      fprintf(stderr, "Could not remove the pid file %s.\n", pid_file);
  }

}


static void sig_handler(const int sig) {
    printf("SIGINT handled.\n");

    exit(EXIT_SUCCESS);
}

#if defined(HAVE_GETPAGESIZES) && defined(HAVE_MEMCNTL)
/*
 * On systems that supports multiple page sizes we may reduce the
 * number of TLB-misses by using the biggest available page size
 */
int enable_large_pages(void) {
    int ret = -1;
    size_t sizes[32];
    int avail = getpagesizes(sizes, 32);
    if (avail != -1) {
        size_t max = sizes[0];
        struct memcntl_mha arg = {0};
        int ii;

        for (ii = 1; ii < avail; ++ii) {
            if (max < sizes[ii]) {
                max = sizes[ii];
            }
        }

        arg.mha_flags   = 0;
        arg.mha_pagesize = max;
        arg.mha_cmd = MHA_MAPSIZE_BSSBRK;

        if (memcntl(0, 0, MC_HAT_ADVISE, (caddr_t)&arg, 0, 0) == -1) {
            fprintf(stderr, "Failed to set large pages: %s\n",
                    strerror(errno));
            fprintf(stderr, "Will use default page size\n");
        } else {
            ret = 0;
        }
    } else {
        fprintf(stderr, "Failed to get supported pagesizes: %s\n",
                strerror(errno));
        fprintf(stderr, "Will use default page size\n");
    }

    return ret;
}
#endif

static int get_valid_str(char *strSpace)
{
	char str[Len4TmpString + 1] = { 0 };      /* 临时串 */
	int i,j;
	int length=(int)strlen(strSpace);

	memset(str,0x00,sizeof(str));
	for (i = 0; i< length; i++)
    {
		if ((strSpace[i] != ' ') && (strSpace[i] != '\t') && (strSpace[i] != '\n') && (strSpace[i] != '\r'))
			break;
	}
	/* 去掉头部空格及TAB键 i 值为第一个非空字符位置*/
	
	if (i >= length)  /* 为空串 */
	    return -1;
	    
	j = i;
	for ( i = length -1; i > j-1; i--)
	{
		if ((strSpace[i] != ' ') && (strSpace[i] != '\t') && (strSpace[i] != '\n') && (strSpace[i] != '\r'))
			break;
    }
    /* 去掉尾部空格及TAB键 i 值为最后一个非空字符位置*/
    
	strncpy(str,strSpace+j,i-j+1); /* 将中间子串放到临时串中 */
	memset(strSpace,0,length); 
	strncpy(strSpace,str,i-j+1);

	return 0;
}

static int get_cfg(const char * strDomainName,  const char * strFieldName, char * strFieldResult, int nLen)
{
   return config::instance()->ReadField(strDomainName,strFieldName,nLen,strFieldResult);
}

static int read_cfg()
{
    int nRet = 0;
	char* pHomePath = NULL;
    char sCfgFile[128 + 1] = { 0 };
	char tmp[Len4b32 + 1] = { 0 };
    
    pHomePath = getenv( "HOME" );
    if ( NULL == pHomePath )
    {
    	// 当前目录
    	strcpy(sCfgFile, "./dsmp.cfg");
    }
    else
    {
        // 获取配置文件名
        snprintf(sCfgFile, sizeof(sCfgFile),"%s/dsmp/cfg/dsmp.cfg", pHomePath);
		if ( 0 != access(sCfgFile, F_OK) )
	    {
	       strcpy(sCfgFile, "dsmp.cfg");
	    }
    }

	if ( 0 != access(sCfgFile, F_OK) )
    {
        return -1;
    }

    if (config::instance()->Load(sCfgFile) != 0)
	{
	   printf("load config failed!");
	   return -1;
	}
	
	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("System", "Port", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.port = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("System", "Daemon", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.daemonize = atoi(tmp);

	nRet = get_cfg("System", "Username", settings.sUserName, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("System", "Password", settings.sPwd, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("System", "Database", settings.sDataBase, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("System", "ThreadNum", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.num_threads = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("System", "PartId", tmp, Len4b32);
	if ( 0 != nRet )
	{
		return -1;
	}
	
	settings.nPartId= atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Database", "DbNum", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.num_db = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Database", "MaxSize", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.max_db = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Database", "Interval", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.interval_db = atoi(tmp);

	nRet = get_cfg("PubDB", "Database", settings.sPubDataBase, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("PubDB", "Username", settings.sPubUserName, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("PubDB", "Password", settings.sPubPwd, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("PubDB", "MaxSize", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.max_pub_db = atoi(tmp);

	nRet = get_cfg("SSI", "Host", settings.sSSIHost, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("SSI", "Port", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nSSIPort = atoi(tmp);

    nRet = get_cfg("SSI", "SrvInfo", settings.sSrvInfo, Len4b128);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("PushMail", "Host", settings.sPMailHost, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("PushMail", "Port", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nPMailPort = atoi(tmp);

    nRet = get_cfg("PushMail", "SrvInfo", settings.sPMailSrvInfo, Len4b128);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("Fetion", "Host", settings.sFetionHost, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Fetion", "Port", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nFetionPort = atoi(tmp);

    nRet = get_cfg("Fetion", "SrvInfo", settings.sFetionSrvInfo, Len4b128);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("Message", "Path", settings.sMsgPath, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("Message", "Host", settings.sMsgHost, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Message", "Port", tmp, Len4b32);
	if ( 0 != nRet )
	{
        return -1;
    }
    settings.nMsgPort = atoi(tmp);

	nRet = get_cfg("MessageOp", "Path", settings.sMsgOpPath, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("MessageOp", "Host", settings.sMsgOpHost, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("MessageOp", "Port", tmp, Len4b32);
	if ( 0 != nRet )
	{
        return -1;
    }
    settings.nMsgOpPort = atoi(tmp);

    nRet = get_cfg("CallBack", "Host", settings.sCBHost, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("CallBack", "Port", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nCBPort = atoi(tmp);

	nRet = get_cfg("CMail", "Host", settings.sCMailHost, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("CMail", "Port", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nCMailPort = atoi(tmp);

	nRet = get_cfg("CMail", "Domain", settings.sDomain, Len4b128);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("CMail", "RenRenIndex", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
    settings.nRenRenIndex = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("CMail", "ActivtyNo", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
    settings.nActivtyNo = atoi(tmp);

	//wxj add ,for open to other carriers
	nRet = get_cfg("CMail", "SpsidForOtherCarrier", settings.sSpsidForOtherCarrier, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}
	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("CMail", "IndexForOtherCarrier", tmp, Len4b32);
	if ( 0 != nRet )
	{
        return -1;
    }
    settings.nIndexForOtherCarrier = atoi(tmp);

    memset(tmp, 0x00, sizeof(tmp));
    nRet = get_cfg("CMail", "BasicMailSystemType", tmp, Len4b32);
    if(0 != nRet)
    {
        return -1;
    }
    settings.nBasicMailSystemType = atoi(tmp);

    nRet = get_cfg("RMailAppendOp", "2002AppendOp", settings.s2002AppendOp, Len4b128);
    if(0 != nRet)
    {
        return -1;
    }

    fprintf( stderr, "\n%s Line.%d--> 111 settings.s2002AppendOp[%s] \n", __FILE__, __LINE__, settings.s2002AppendOp);
	
    // activemq
	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("ActiveMQ", "Open", tmp, Len4b32);
	if ( 0 != nRet )
	{
        return -1;
    }
    settings.nActiveMQ = atoi(tmp);

	nRet = get_cfg("ActiveMQ", "BrokerUrl", settings.sBrokerUrl, Len4b256);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("ActiveMQ", "Destination", settings.sDestination, Len4b128);
	if ( 0 != nRet )
	{
	    return -1;
    }

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("ActiveMQ", "Retry", tmp, Len4b32);
	if ( 0 != nRet )
	{
        return -1;
    }
    settings.nRetry = atoi(tmp);

	//nRet = get_cfg(sCfgFile, "ActiveMQ", "DynamicZoneType", settings.sDynamicZoneType, Len4b32);
	//if ( 0 != nRet )
	//{
    //    return -1;
    //}

	nRet = get_cfg("Log", "Path", settings.sLogPath, Len4b128);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Log", "Maxsize", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nLogSize = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Log", "Hour", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nLogHour = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Log", "Check", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nLogCheck = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Log", "Level", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nLogLevel = atoi(tmp);

	if ( settings.nLogLevel < 1 || settings.nLogLevel > 5 )
	{
	    settings.nLogLevel = 1;
	}

	//WebServer
	nRet = get_cfg("WebServer", "Host", settings.sWebHost, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("WebServer", "path", settings.sWebPath, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("WebServer", "Port", tmp, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nWebPort = atoi(tmp);
	
	memset(tmp, 0x00, sizeof(tmp));
    nRet = get_cfg("WebServer", "SendFlag", tmp, Len4b64);
    if ( 0 != nRet )
    {
        return -1;
    }
    settings.nSendFlag = atoi(tmp);


    //WapClean
    nRet = get_cfg("WapClean", "Host", settings.sWapCleanHost, Len4b64);
    if ( 0 != nRet )
    {
        return -1;
    }

    nRet = get_cfg("WapClean", "path", settings.sWapCleanPath, Len4b64);
    if ( 0 != nRet )
    {
        return -1;
    }

    memset(tmp, 0x00, sizeof(tmp));
    nRet = get_cfg("WapClean", "Port", tmp, Len4b64);
    if ( 0 != nRet )
    {
        return -1;
    }
    settings.nWapCleanPort = atoi(tmp);

    memset(tmp, 0x00, sizeof(tmp));
    nRet = get_cfg("WapClean", "SendFlag", tmp, Len4b64);
    if ( 0 != nRet )
    {
        return -1;
    }
    settings.nWapCleanSendFlag = atoi(tmp);



    //ud router memcache 连接池
    nRet = get_cfg("Memcache", "libmemcached_pool_server", settings.sMemcachePoolServer, Len4b128);
    if(0 != nRet)
    {
        return -1;
    }

    memset(tmp, 0x00, sizeof(tmp));
    nRet = get_cfg("Memcache", "libmemcached_pool_size", tmp, Len4b64);
    if(0 != nRet)
    {
        return -1;
    }
    settings.nMemcachePoolSize = atoi(tmp);

    fprintf( stderr, "\n%s Line.%d--> settings.sMemcachePoolServer[%s], nMemcachePoolSize[%d] \n", __FILE__, __LINE__, settings.sMemcachePoolServer, settings.nMemcachePoolSize );

    //PS memcache 连接池
    nRet = get_cfg("PSMemcache", "ps_libmemcached_pool_server", settings.sPSMemcachePoolServer, Len4b128);
    if(0 != nRet)
    {
        return -1;
    }

    memset(tmp, 0x00, sizeof(tmp));
    nRet = get_cfg("PSMemcache", "ps_libmemcached_pool_size", tmp, Len4b64);
    if(0 != nRet)
    {
        return -1;
    }
    settings.nPSMemcachePoolSize = atoi(tmp);

    fprintf( stderr, "\n%s Line.%d--> settings.sPSMemcachePoolServer[%s], nPSMemcachePoolSize[%d] \n", __FILE__, __LINE__, settings.sPSMemcachePoolServer, settings.nPSMemcachePoolSize );


    return 0;
}

int main (int argc, char **argv) {
    int c;
    int x;
    //bool daemonize = true;
    bool preallocate = false;
    int maxcore = 1;
    char *username = NULL;
    char *pid_file = NULL;
    struct passwd *pw;
    struct sigaction sa;
    struct rlimit rlim;
    /* listening socket */
    static int *l_socket = NULL;

    /* udp socket */
    static int *u_socket = NULL;
    static int u_socket_count = 0;

    /* handle SIGINT */
    signal( SIGQUIT , SIG_IGN );
    signal( SIGPIPE , SIG_IGN ); 
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    /* init settings */
    settings_init();

    /* read cfg*/
	if ( 0 != read_cfg() )
	{
	    fprintf(stderr, "failed to read config file\n");
	    return 1;
	}

    /*
     * If needed, increase rlimits to allow as many connections
     * as needed.
     */
    if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
        fprintf(stderr, "failed to getrlimit number of files\n");
        exit(EXIT_FAILURE);
    } else {
        int maxfiles = settings.maxconns;
        if (rlim.rlim_cur < maxfiles)
            rlim.rlim_cur = maxfiles + 3;
        if (rlim.rlim_max < rlim.rlim_cur)
            rlim.rlim_max = rlim.rlim_cur;
        if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
            fprintf(stderr, "failed to set rlimit for open files. Try running as root or requesting smaller maxconns value.\n");
            exit(EXIT_FAILURE);
        }
    }

    /* lose root privileges if we have them */
    if (getuid() == 0 || geteuid() == 0) {
        if (username == 0 || *username == '\0') {
            fprintf(stderr, "can't run as root without the -u switch\n");
            return 1;
        }
        if ((pw = getpwnam(username)) == 0) {
            fprintf(stderr, "can't find the user %s to switch to\n", username);
            return 1;
        }
        if (setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0) {
            fprintf(stderr, "failed to assume identity of user %s\n", username);
            return 1;
        }
    }

    /* daemonize if requested */
    /* if we want to ensure our ability to dump core, don't chdir to / */
    if (settings.daemonize == 1) {
        int res;
        res = daemon(maxcore, settings.verbose);
        if (res == -1) {
            fprintf(stderr, "failed to daemon() in order to daemonize\n");
            return 1;
        }
    }

    /* initialize main thread libevent instance */
    main_base = (event_base*)event_init();

    /* initialize other stuff */ 
    conn_init();
    suffix_init();

    /*
     * ignore SIGPIPE signals; we can use errno==EPIPE if we
     * need that information
     */
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if (sigemptyset(&sa.sa_mask) == -1 ||
        sigaction(SIGPIPE, &sa, 0) == -1) {
        perror("failed to ignore SIGPIPE; sigaction");
        exit(EXIT_FAILURE);
    }
	
    /* start up worker threads if MT mode */
    thread_init(settings.num_threads, main_base);

    /* save the PID in if we're a daemon, do this after thread_init due to
       a file descriptor handling bug somewhere in libevent */
    if (settings.daemonize == 1)
        save_pid(getpid(), pid_file);
	
    /* initialise clock event */
    clock_handler(0, 0, 0);

    /* create server socket */
	if (server_socket(settings.port, 0)) {
            fprintf(stderr, "failed to listen\n");
            exit(EXIT_FAILURE);
    }

    /* init log*/
	dsmplog = logger_init(settings.sLogPath, settings.nLogSize, 
	    settings.nLogHour, settings.nLogCheck, settings.nLogLevel);	
	
	INFO_LOGGER(dsmplog, "init log success");

	int iRet = 0;
    //profile server memcache集群连接池初始化

    /* enter the event loop */
    event_base_loop(main_base, 0);
    /* remove the PID file if we're a daemon */
    if (settings.daemonize == 1)
        remove_pidfile(pid_file);
    /* Clean up strdup() call for bind() address */
    if (settings.inter)
      free(settings.inter);
    if (l_socket)
      free(l_socket);
    if (u_socket)
      free(u_socket);

	logger_close(&dsmplog);

    return 0;
}