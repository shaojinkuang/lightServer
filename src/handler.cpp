/*
 * handler.cpp
 *
 *  Created on: 2012-12-8
 *      Author: Administrator
 */

#include <assert.h>
#include <stdio.h>
#include "server.h"
#include "handler.h"

void event_handler(const int fd, const short which, void *arg)
{
	conn *c;

	c = (conn *) arg;
	assert(c != NULL);

	c->which = which;

	/* sanity */
	if (fd != c->sfd)
	{
		if (settings.verbose > 0)
			fprintf(stderr, "Catastrophic: event fd doesn't match conn fd!\n");
		conn_close(c);
		return;
	}

	drive_machine(c);

	/* wait for next event */
	return;
}

bool update_event(conn *c, const int new_flags)
{
	assert(c != NULL);

	struct event_base *base = c->event.ev_base;
	if (c->ev_flags == new_flags)
		return true;
	if (event_del(&c->event) == -1)
		return false;
	event_set(&c->event, c->sfd, new_flags, event_handler, (void *) c);
	event_base_set(base, &c->event);
	c->ev_flags = new_flags;
	if (event_add(&c->event, 0) == -1)
		return false;
	return true;
}

/*
 * Adds data to the list of pending data that will be written out to a
 * connection.
 *
 * Returns 0 on success, -1 on out-of-memory.
 */


