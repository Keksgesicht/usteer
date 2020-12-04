/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 *   Copyright (C) 2020 embedd.ch 
 *   Copyright (C) 2020 Felix Fietkau <nbd@nbd.name> 
 *   Copyright (C) 2020 John Crispin <john@phrozen.org> 
 */

#ifndef __APMGR_TIMEOUT_H
#define __APMGR_TIMEOUT_H

#include <libubox/avl.h>
#include <libubox/uloop.h>

/**
 * AVL Node for timeout
 */
struct usteer_timeout {
	struct avl_node node;
};

/**
 * Queue with avl nodes
 */
struct usteer_timeout_queue {
	struct avl_tree tree;
	struct uloop_timeout timeout;
	void (*cb)(struct usteer_timeout_queue *q, struct usteer_timeout *t);
};

/**
 * Check if the node list of the given timeout instance is not empty.
 * 
 * @param t The timeout to check.
 * @return bool True if the node list of the given timeout is not NULL.
 */
static inline bool usteer_timeout_isset(struct usteer_timeout *t){
	return t->node.list.prev != NULL;
}

/**
 * Initialize time out queue
 * @param q
 */
void usteer_timeout_init(struct usteer_timeout_queue *q);

/**
 * Set time out
 * @param q where to set time out
 * @param t node with time out
 * @param msecs time out in milli seconds
 */
void usteer_timeout_set(struct usteer_timeout_queue *q, struct usteer_timeout *t,int msecs);

/**
 * Cancel time out
 * @param q where to cancel
 * @param t node to cancel
 */
void usteer_timeout_cancel(struct usteer_timeout_queue *q, struct usteer_timeout *t);

/**
 * Flush queue
 * @param q
 */
void usteer_timeout_flush(struct usteer_timeout_queue *q);

#endif
