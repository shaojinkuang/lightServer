/*
 * handler.h
 *  event handling
 *  Created on: 2012-12-8
 *      Author: Administrator
 */

#ifndef HANDLER_H_
#define HANDLER_H_

#include "thread.h"

static void event_handler(const int fd, const short which, void *arg);
static bool update_event(conn *c, const int new_flags);
static int add_iov(conn *c, const void *buf, int len);
static int add_msghdr(conn *c);


#endif /* HANDLER_H_ */
