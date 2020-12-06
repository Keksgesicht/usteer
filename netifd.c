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

#include "usteer.h"
#include "node.h"
/**
 * blob buffer
 */
static struct blob_buf b;
/**
 * Parse interface config and set attributes
 * @param ln local usteer node
 * @param msg blob message
 */
static void netifd_parse_interface_config(struct usteer_local_node *ln, struct blob_attr *msg){
    /**
     * blobmsg_policy in blobmsg.h
     * name, name of the message
	 * type;
     */
	static const struct blobmsg_policy policy = {
		.name = "maxassoc",
		.type = BLOBMSG_TYPE_INT32,
	};
	struct blob_attr *cur;
	int val = 0;

	/**
	 * put message into policy and cur
	 */
	blobmsg_parse(&policy, 1, &cur, blobmsg_data(msg), blobmsg_data_len(msg));
	if (cur) /* if cur exists */
		val = blobmsg_get_u32(cur);
    /**
     * set max assocs and complete status
     */
	ln->node.max_assoc = val;
	ln->netifd.status_complete = true;
}
/**
 * Parse interface and call parse interface config function
 * @param ln local node
 * @param msg message
 */
static void netifd_parse_interface(struct usteer_local_node *ln, struct blob_attr *msg){
	enum {
		N_IF_CONFIG,
		N_IF_NAME,
		__N_IF_MAX
	};
	static const struct blobmsg_policy policy[__N_IF_MAX] = {
		[N_IF_CONFIG] = {
		                  .name = "config",
                          .type = BLOBMSG_TYPE_TABLE
                        },
		[N_IF_NAME]   = {
		                  .name = "ifname",
                          .type = BLOBMSG_TYPE_STRING
                       },
	};
	struct blob_attr *tb[__N_IF_MAX];

	if (blobmsg_type(msg) != BLOBMSG_TYPE_TABLE)
		return;
    /**
     * put msg into policy and blob_attr tb
     */
	blobmsg_parse(policy, __N_IF_MAX, tb, blobmsg_data(msg), blobmsg_data_len(msg));
	if (!tb[N_IF_CONFIG] || !tb[N_IF_NAME])
		return;

	if (strcmp(blobmsg_get_string(tb[N_IF_NAME]), ln->iface) != 0)
		return;
    /**
     * see 'netifd_parse_interface_config()'
     */
	netifd_parse_interface_config(ln, tb[N_IF_CONFIG]);
}
/**
 * Get info from radio, parse interfaces
 * @param ln local node
 * @param msg message
 */
static void netifd_parse_radio(struct usteer_local_node *ln, struct blob_attr *msg){
	static const struct blobmsg_policy policy = {
		.name = "interfaces",
		.type = BLOBMSG_TYPE_ARRAY,
	};
	struct blob_attr *cur, *iface;
	int rem;

	if (blobmsg_type(msg) != BLOBMSG_TYPE_TABLE)
		return;
	/**
	 * put msg into policy and iface
	 */
	blobmsg_parse(&policy, 1, &iface, blobmsg_data(msg), blobmsg_data_len(msg));
	if (!iface)
		return;
    /**
     * Iterate over interfaces and parse them
     */
	blobmsg_for_each_attr(cur, iface, rem)
		netifd_parse_interface(ln, cur);
}
/**
 * prepare radio and parse info, see 'netifd_parse_radio()'
 * used as a ubus_data_handler_t
 * @param req ubus request
 * @param type
 * @param msg message
 */
static void netifd_status_cb(struct ubus_request *req, int type, struct blob_attr *msg){
	struct usteer_local_node *ln;
	struct blob_attr *cur;
	int rem;
    /**
     * convert node into usteer local node
     * and fix pointers
     */
	ln = container_of(req, struct usteer_local_node, netifd.req);
	ln->netifd.req_pending = false;

	/**
	 * iterate over
	 */
	blobmsg_for_each_attr(cur, msg, rem)
		netifd_parse_radio(ln, cur);
}
/**
 * Update node for request
 * @param node
 */
static void netifd_update_node(struct usteer_node *node){
	struct usteer_local_node *ln;
	uint32_t id;
    /**
	 * convert node into usteer local node
	 * and fix pointers
	 */
	ln = container_of(node, struct usteer_local_node, node);
	/**
	 * following if statements are for preparation of the update
	 */
	if (ln->netifd.status_complete)
		return;

	if (ln->netifd.req_pending)
		ubus_abort_request(ubus_ctx, &ln->netifd.req);

	if (ubus_lookup_id(ubus_ctx, "network.wireless", &id))
		return;

	blob_buf_init(&b, 0);
	/**
	 * calls ubus_invoke_async_fd(ctx, obj, method, msg, req, -1)
	 * invoke a method on a specific object asynchronously
	 * ubus context
	 * object
	 * method to call
	 * message
	 * ubus request
	 */
	ubus_invoke_async(ubus_ctx, id, "status", b.head, &ln->netifd.req);
	ln->netifd.req.data_cb = netifd_status_cb;
	/**
	 * complete a request asynchronously
	 * context
	 * request
	 */
	ubus_complete_request_async(ubus_ctx, &ln->netifd.req);
	ln->netifd.req_pending = true;
}
/**
 * Initialize usteer node, prepare for request
 * calls 'netifd_update_node()'
 * @param node usteer_node
 */
static void netifd_init_node(struct usteer_node *node){
	struct usteer_local_node *ln;
    /**
	 * convert node into usteer local node
	 * and fix pointers
	 */
	ln = container_of(node, struct usteer_local_node, node);
	ln->netifd.status_complete = false;
	netifd_update_node(node);
}
/**
 * Cancel request
 * @param node
 */
static void netifd_free_node(struct usteer_node *node){
	struct usteer_local_node *ln;
	/**
	 * convert node into usteer local node
	 * and fix pointers
	 */
	ln = container_of(node, struct usteer_local_node, node);
	/**
	 * if there is a request, then cancel it within context
	 */
	if (ln->netifd.req_pending)
		ubus_abort_request(ubus_ctx, &ln->netifd.req);
}
/**
 * pointer functions, see usteer.h
 */
static struct usteer_node_handler netifd_handler = {
	.init_node = netifd_init_node,
	.update_node = netifd_update_node,
	.free_node = netifd_free_node,
};
/**
 * Initialize netifd
 */
static void __usteer_init usteer_netifd_init(void){
	list_add(&netifd_handler.list, &node_handlers);
}
