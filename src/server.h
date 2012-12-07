/*
 * server.h
 * 	network IO
 *
 *  Created on: 2012-12-8
 *      Author: Administrator
 */

#ifndef SERVER_H_
#define SERVER_H_

static void drive_machine(conn *c);
static int new_socket(struct addrinfo *ai);
static int server_socket(const int port, const bool is_udp);
static int try_read_cli(conn *c);
static int try_read_network(conn *c);
static int try_read_tcp(conn *c);
static int transmit(conn *c);

static void conn_close(conn *c);
static void conn_init(void);
static void accept_new_conns(const bool do_accept);
static void conn_free(conn *c);
static void conn_cleanup(conn *c);
static void conn_shrink(conn *c);
static void conn_set_state(conn *c, int state);

conn *do_conn_from_freelist();
bool do_conn_add_to_freelist(conn *c);
conn *conn_new(const int sfd, const int init_state, const int event_flags, const int read_buffer_size, const bool is_udp, struct event_base *base);

static int ensure_iov_space(conn *c);
static int add_iov(conn *c, const void *buf, int len);

static void process_server(conn *c, char *command);

static void maximize_sndbuf(const int sfd);


#endif /* SERVER_H_ */
