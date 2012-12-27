/*
 * handler.h
 *  event handling
 *  Created on: 2012-12-8
 *      Author: Administrator
 */

#ifndef HANDLER_H_
#define HANDLER_H_

#include "thread.h"

void event_handler(const int fd, const short which, void *arg);
bool update_event(conn *c, const int new_flags);


#endif /* HANDLER_H_ */
