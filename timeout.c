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

#include <string.h>
#include <libubox/utils.h>
#include "timeout.h"
/**
 * Comparator, compares timeouts
 * @param k1
 * @param k2
 * @param ptr
 * @return 0 if =, 1 if <, -1 if >
 */
static int usteer_timeout_cmp(const void *k1, const void *k2, void *ptr){
	uint32_t ref = (uint32_t) (intptr_t) ptr;
	int32_t t1 = (uint32_t) (intptr_t) k1 - ref;
	int32_t t2 = (uint32_t) (intptr_t) k2 - ref;

	if (t1 < t2)
		return -1;
	else if (t1 > t2)
		return 1;
	else
		return 0;
}
/**
 * Difference of usteer time out node and time
 * @param t usteer_timeout node (avl node)
 * @param time
 * @return
 */
static int32_t usteer_timeout_delta(struct usteer_timeout *t, uint32_t time){
	uint32_t val = (uint32_t) (intptr_t) t->node.key;
	return val - time;
}
/**
 * Recalculate time out
 * @param q with avl nodes
 * @param time
 */
static void usteer_timeout_recalc(struct usteer_timeout_queue *q, uint32_t time){
	struct usteer_timeout *t;
	int32_t delta;

	/**
	 * if no element in q
	 */
	if (avl_is_empty(&q->tree)) {
	    /**
	     * cancel uloop timeout
	     */
		uloop_timeout_cancel(&q->timeout);
		return;
	}
    /**
     * get first element in the q
     *
     * q->tree, pointer to avl-tree
     * usteer_timeout t, pointer to a node element (don't need to be initialized)
     * node, name of the avl_node element inside the larger struct
     */
	t = avl_first_element(&q->tree, t, node);
    /**
     * get the difference between usteer timeout and time
     */
	delta = usteer_timeout_delta(t, time);
	/**
	 * if the difference is < 1 , it means that the time is close to the real timeout, yet smaller
	 * in that case set delta to 1
	 */
	if (delta < 1)
		delta = 1;

	/**
	 * update the uloop timeout with the delta
	 */
	uloop_timeout_set(&q->timeout, delta);
}
/**
 *
 * @return current time
 */
static uint32_t ampgr_timeout_current_time(void){
	/**
	 * POSIX.1b structure for a time value, nanoseconds
	 */
    struct timespec ts;
	uint32_t val;

	/**
	 * Get current value of clock CLOCK_ID and store it in TP.
	 */
	clock_gettime(CLOCK_MONOTONIC, &ts);
	val = ts.tv_sec * 1000;
	val += ts.tv_nsec / 1000000;

	return val;
}
/**
 *
 * used as a pointer function
 * @param timeout
 */
static void usteer_timeout_cb(struct uloop_timeout *timeout){
	struct usteer_timeout_queue *q;
	struct usteer_timeout *t, *tmp;
	bool found;
	uint32_t time;

	/**
	 * ??
	 * That weird macro black magic, it does some kind of weird conversion
	 * NEEDS TO BE CLARIFIED
	 */
	q = container_of(timeout, struct usteer_timeout_queue, timeout);
	do {
		found = false;
		/**
		 * get current time
		 */
		time = ampgr_timeout_current_time();
        /**
         * Loop over all elements of an avl_tree,
         * This loop can be used if the current element might
         * be removed from the tree during the loop
         * q->tree, pointer to avl-tree
         * t, pointer to a node of the tree, this element will contain the current node of the tree during the loop
         * node, name of the avl_node element inside the larger struct
         * usteer_timeout, tmp pointer to a tree element which is used to store the next node during the loop
         */
		avl_for_each_element_safe(&q->tree, t, node, tmp) {
			if (usteer_timeout_delta(t, time) > 0)
				break;

			usteer_timeout_cancel(q, t);
			if (q->cb)
				q->cb(q, t);
			found = true;
		}
	} while (found);

	usteer_timeout_recalc(q, time);
}
/**
 *
 * @param q
 */
void usteer_timeout_init(struct usteer_timeout_queue *q){
    /**
     *
     * Initialize a new avl_tree struct
     * q->tree, pointer to avl-tree
     * usteer_timeout_cmp, pointer to comparator for the tree
     * allow_dups true if the tree allows multiple
     *   elements with the same
     * ptr custom parameter for comparator
     */
	avl_init(&q->tree, usteer_timeout_cmp, true, NULL);
	q->timeout.cb = usteer_timeout_cb;
}
/**
 * Auxiliary function, see 'usteer_timeout_cancel',
 * Remove avl node, cancel timeout
 * @param q pointer to tree
 * @param t pointer to node
 */
static void __usteer_timeout_cancel(struct usteer_timeout_queue *q,
				                    struct usteer_timeout *t){
    /**
     * Remove a node from an avl tree
     * q->tree, pointer to tree
     * t->node, pointer to node
     */
	avl_delete(&q->tree, &t->node);
}
/**
 *
 * @param q
 * @param t
 * @param msecs
 */
void usteer_timeout_set(struct usteer_timeout_queue *q, struct usteer_timeout *t,
		                int msecs){
	uint32_t time = ampgr_timeout_current_time();
	uint32_t val = time + msecs;
	bool recalc = false;

	q->tree.cmp_ptr = (void *) (intptr_t) time;
	if (usteer_timeout_isset(t)) {
		if (avl_is_first(&q->tree, &t->node))
			recalc = true;

		__usteer_timeout_cancel(q, t);
	}

	t->node.key = (void *) (intptr_t) val;
	avl_insert(&q->tree, &t->node);
	if (avl_is_first(&q->tree, &t->node))
		recalc = true;

	if (recalc)
		usteer_timeout_recalc(q, time);
}
/**
 * Cancel timeout, uses auxiliary function '__usteer_timeout_cancel(q, t)'
 * @param q with avl nodes
 * @param t avl node for timeout
 */
void usteer_timeout_cancel(struct usteer_timeout_queue *q,
			               struct usteer_timeout *t){
    /**
     * see timeout.h
     */
	if (!usteer_timeout_isset(t))
		return;

	__usteer_timeout_cancel(q, t);
	/**
	 * set sizeof(t->node.list) bytes of t->node.list to 0
	 */
	memset(&t->node.list, 0, sizeof(t->node.list));
}
/**
 * flush timeout queue
 * @param q
 */
void usteer_timeout_flush(struct usteer_timeout_queue *q){
	struct usteer_timeout *t, *tmp;
    /**
     * cancel uloop timeout
     */
	uloop_timeout_cancel(&q->timeout);
	/**
	 * loop that removes all elements of the tree and cleans up the tree root.
	 * does not rebalance the tree after each removal
	 * q->tree, pointer to avl-tree
     * t, pointer to a node of the tree, this element will contain the current node of the tree during the loop
     * node, name of the avl_node element inside the larger struct
     * usteer_timeout tmp, pointer to a tree element which is used to store the next node during the loop
	 */
	avl_remove_all_elements(&q->tree, t, node, tmp) {
        /**
        * set sizeof(t->node.list) bytes of t->node.list to 0
        */
		memset(&t->node.list, 0, sizeof(t->node.list));
		if (q->cb)
			q->cb(q, t);
	}
}
