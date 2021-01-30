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
 * 
 *   Copyright (C) 2021 Nico Petermann <nico.petermann3@gmail.com>
 *   Copyright (C) 2021 Tomas Duchac <tomasduchac@protonmail.ch>
 * 	 Copyright (C) 2021 Philip Jonas Franz <R41Da@gmx.de>
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <net/ethernet.h>
#ifdef linux
#include <netinet/ether.h>
#endif
#include <net/if.h>
#include <stdlib.h>

#include <libubox/avl-cmp.h>
#include <libubox/blobmsg_json.h>
#include "usteer.h"
#include "node.h"
#include "hearing_map.h"

AVL_TREE(local_nodes, avl_strcmp, false, NULL);
static struct blob_buf b;
static char *node_up_script;

static void
usteer_local_node_state_reset(struct usteer_local_node *ln)
{
	if (ln->req_state == REQ_IDLE)
		return;

	ubus_abort_request(ubus_ctx, &ln->req);
	ln->req_state = REQ_IDLE;
}

static void
usteer_free_node(struct ubus_context *ctx, struct usteer_local_node *ln)
{
	struct usteer_node_handler *h;

	list_for_each_entry(h, &node_handlers, list) {
		if (!h->free_node)
			continue;
		h->free_node(&ln->node);
	}

	usteer_local_node_state_reset(ln);
	usteer_sta_node_cleanup(&ln->node);
	uloop_timeout_cancel(&ln->req_timer);
	uloop_timeout_cancel(&ln->update);
	avl_delete(&local_nodes, &ln->node.avl);
	ubus_unregister_subscriber(ctx, &ln->ev);
	free(ln);
}

static void
usteer_handle_remove(struct ubus_context *ctx, struct ubus_subscriber *s,
		    uint32_t id)
{
	struct usteer_local_node *ln = container_of(s, struct usteer_local_node, ev);

	usteer_free_node(ctx, ln);
}

static int
usteer_handle_event_probe(struct ubus_object *obj, const char *method, struct blob_attr *msg)
{
	enum {
		EVENT_ADDR,
		EVENT_SIGNAL,
		EVENT_TARGET,
		EVENT_FREQ,
		__EVENT_MAX
	};
	struct blobmsg_policy policy[__EVENT_MAX] = {
		[EVENT_ADDR] = { .name = "address", .type = BLOBMSG_TYPE_STRING },
		[EVENT_SIGNAL] = { .name = "signal", .type = BLOBMSG_TYPE_INT32 },
		[EVENT_TARGET] = { .name = "target", .type = BLOBMSG_TYPE_STRING },
		[EVENT_FREQ] = { .name = "freq", .type = BLOBMSG_TYPE_INT32 },
	};
	enum usteer_event_type ev_type = __EVENT_TYPE_MAX;
	struct blob_attr *tb[__EVENT_MAX];
	struct usteer_local_node *ln;
	struct usteer_node *node;
	int signal = NO_SIGNAL;
	int freq = 0;
	const char *addr_str;
	const uint8_t *addr;
	int i;
	bool ret;

	usteer_update_time();

	for (i = 0; i < ARRAY_SIZE(event_types); i++) {
		if (strcmp(method, event_types[i]) != 0)
			continue;

		ev_type = i;
		break;
	}

	ln = container_of(obj, struct usteer_local_node, ev.obj);
	node = &ln->node;
	blobmsg_parse(policy, __EVENT_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[EVENT_ADDR] || !tb[EVENT_FREQ])
		return UBUS_STATUS_INVALID_ARGUMENT;

	if (tb[EVENT_SIGNAL])
		signal = (int32_t) blobmsg_get_u32(tb[EVENT_SIGNAL]);

	if (tb[EVENT_FREQ])
		freq = blobmsg_get_u32(tb[EVENT_FREQ]);

	addr_str = blobmsg_data(tb[EVENT_ADDR]);
	addr = (uint8_t *) ether_aton(addr_str);
	if (!addr)
		return UBUS_STATUS_INVALID_ARGUMENT;

	ret = usteer_handle_sta_event(node, addr, ev_type, freq, signal);

	MSG(DEBUG, "received %s event from %s, signal=%d, freq=%d, handled:%s\n",
	    method, addr_str, signal, freq, ret ? "true" : "false");

	return 0; // stop suppressing probe messages
	// return ret ? 0 : 17 /* WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA */;
}

static int
usteer_handle_event(struct ubus_context *ctx, struct ubus_object *obj,
		   struct ubus_request_data *req, const char *method,
		   struct blob_attr *msg)
{
	if (strncmp(method, "beacon-report", 12) == 0)
		usteer_handle_event_beacon(obj, msg);
	return usteer_handle_event_probe(obj, method, msg);
}

static void
usteer_local_node_assoc_update(struct sta_info *si, struct blob_attr *data)
{
	enum {
		MSG_ASSOC,
		__MSG_MAX,
	};
	static struct blobmsg_policy policy[__MSG_MAX] = {
		[MSG_ASSOC] = { "assoc", BLOBMSG_TYPE_BOOL },
	};
	struct blob_attr *tb[__MSG_MAX];

	blobmsg_parse(policy, __MSG_MAX, tb, blobmsg_data(data), blobmsg_data_len(data));
	if (tb[MSG_ASSOC] && blobmsg_get_u8(tb[MSG_ASSOC]))
		si->connected = 1;

	if (si->node->freq < 4000)
		si->sta->seen_2ghz = 1;
	else
		si->sta->seen_5ghz = 1;
}

static void
usteer_update_client_active_bytes(struct sta_info *si, struct blob_attr *data)
{
	enum {
		MSG_RX,
		MSG_TX,
		MSG_BYTES,
		__MSG_MAX_BYTES,
		__MSG_MAX_RXTX,
	};
	static struct blobmsg_policy policy_bytes[__MSG_MAX_BYTES] = {
			[MSG_BYTES] = { "bytes", BLOBMSG_TYPE_TABLE },
	};
	static struct blobmsg_policy policy_rxtx[__MSG_MAX_RXTX] = {
			[MSG_RX] = { "rx", BLOBMSG_TYPE_INT64 },
			[MSG_TX] = { "tx", BLOBMSG_TYPE_INT64 },
	};
	struct sta_active_bytes active_bytes = si->active_bytes;
	struct blob_attr *tb_bytes[__MSG_MAX_BYTES];
	struct blob_attr *tb_rxtx[__MSG_MAX_RXTX];
	uint64_t ctime = current_time;

	if (ctime - active_bytes.last_time < config.kick_client_active_sec * 1000)
		return;

	blobmsg_parse(policy_bytes, __MSG_MAX_BYTES, tb_bytes, blobmsg_data(data), blobmsg_data_len(data));
	if (!tb_bytes[MSG_BYTES])
		return;

	blobmsg_parse(policy_rxtx, __MSG_MAX_RXTX, tb_rxtx, blobmsg_data(tb_bytes[MSG_BYTES]), blobmsg_data_len(tb_bytes[MSG_BYTES]));
	if (!tb_rxtx[MSG_RX] || !tb_rxtx[MSG_TX])
		return;

	memcpy(active_bytes.data[0], active_bytes.data[1], sizeof(active_bytes.data[1]));
	active_bytes.data[1][0] = blobmsg_get_u64(tb_rxtx[MSG_RX]);
	active_bytes.data[1][1] = blobmsg_get_u64(tb_rxtx[MSG_TX]);

	active_bytes.last_time = ctime;
	si->active_bytes = active_bytes;
}

static void
usteer_local_node_set_assoc(struct usteer_local_node *ln, struct blob_attr *cl)
{
	struct usteer_node *node = &ln->node;
	struct usteer_node_handler *h;
	struct blob_attr *cur;
	struct sta_info *si;
	struct sta *sta;
	int n_assoc = 0;
	int rem;

	list_for_each_entry(si, &node->sta_info, node_list) {
		if (si->connected)
			si->connected = 2;
	}

	blobmsg_for_each_attr(cur, cl, rem) {
		uint8_t *addr = (uint8_t *) ether_aton(blobmsg_name(cur));
		bool create;

		if (!addr)
			continue;

		sta = usteer_sta_get(addr, true);
		si = usteer_sta_info_get(sta, node, &create);
		list_for_each_entry(h, &node_handlers, list) {
			if (!h->update_sta)
				continue;

			h->update_sta(node, si);
		}
		usteer_local_node_assoc_update(si, cur);
		if (si->connected == 1)
			n_assoc++;

		usteer_update_client_active_bytes(si, cur);

		struct beacon_request * br = &si->beacon_rqst;
		
		// based on the current reception, determine a the frequency beacon requests are sent.
		uint64_t ctime = current_time;

		MSG(DEBUG, "Current signal strength: %d", si->signal);

		/* Adjust signal range from (-90 to -30) to (-30 to 30) */
		int adj_signal = si->signal + 60;
		float dyn_freq = config.beacon_request_frequency + 
			(config.beacon_request_signal_modifier * (adj_signal / (1 + abs(adj_signal))));

		if (ctime - br->lastRequestTime < dyn_freq) 
			continue; 
		
		sendBeaconReport(si);
		br->lastRequestTime = ctime;
		si->beacon_rqst = *br;

	}

	node->n_assoc = n_assoc;

	list_for_each_entry(si, &node->sta_info, node_list) {
		if (si->connected != 2)
			continue;

		si->connected = 0;
		usteer_sta_info_update_timeout(si, config.local_sta_timeout);
		MSG(VERBOSE, "station "MAC_ADDR_FMT" disconnected from node %s\n",
			MAC_ADDR_DATA(si->sta->addr), usteer_node_name(node));
	}
}

static void
usteer_local_node_list_cb(struct ubus_request *req, int type, struct blob_attr *msg)
{
	enum {
		MSG_FREQ,
		MSG_CLIENTS,
		__MSG_MAX,
	};
	static struct blobmsg_policy policy[__MSG_MAX] = {
		[MSG_FREQ] = { "freq", BLOBMSG_TYPE_INT32 },
		[MSG_CLIENTS] = { "clients", BLOBMSG_TYPE_TABLE },
	};
	struct blob_attr *tb[__MSG_MAX];
	struct usteer_local_node *ln;
	struct usteer_node *node;

	ln = container_of(req, struct usteer_local_node, req);
	node = &ln->node;

	blobmsg_parse(policy, __MSG_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[MSG_FREQ] || !tb[MSG_CLIENTS])
		return;

	node->freq = blobmsg_get_u32(tb[MSG_FREQ]);
	usteer_local_node_set_assoc(ln, tb[MSG_CLIENTS]);
}

static void
usteer_local_node_rrm_nr_cb(struct ubus_request *req, int type, struct blob_attr *msg)
{
	static const struct blobmsg_policy policy = {
		"value", BLOBMSG_TYPE_ARRAY
	};
	struct usteer_local_node *ln;
	struct blob_attr *tb;

	ln = container_of(req, struct usteer_local_node, req);

	blobmsg_parse(&policy, 1, &tb, blob_data(msg), blob_len(msg));
	if (!tb)
		return;

	usteer_node_set_blob(&ln->node.rrm_nr, tb);

	struct blobmsg_policy policy_mac[3] = {
			{ .type = BLOBMSG_TYPE_STRING },
			{ .type = BLOBMSG_TYPE_STRING },
			{ .type = BLOBMSG_TYPE_STRING },
	};
	struct blob_attr *ba[3];
	blobmsg_parse_array(policy_mac, ARRAY_SIZE(ba), ba, blobmsg_data(ln->node.rrm_nr), blobmsg_data_len(ln->node.rrm_nr));
	if (ba[0]) {
		uint8_t *addr = (uint8_t *) ether_aton(blobmsg_get_string(ba[0]));
		memcpy(ln->node.mac, addr, sizeof(ln->node.mac));
	}
}

static void
usteer_local_node_req_cb(struct ubus_request *req, int ret)
{
	struct usteer_local_node *ln;

	ln = container_of(req, struct usteer_local_node, req);
	uloop_timeout_set(&ln->req_timer, 1);
}

static void
usteer_add_rrm_data(struct usteer_local_node *ln, struct usteer_node *node)
{
	if (node == &ln->node)
		return;

	if (!node->rrm_nr)
		return;

	if (strcmp(ln->node.ssid, node->ssid) != 0)
		return;

	blobmsg_add_field(&b, BLOBMSG_TYPE_ARRAY, "",
			  blobmsg_data(node->rrm_nr),
			  blobmsg_data_len(node->rrm_nr));
}

static void
usteer_local_node_prepare_rrm_set(struct usteer_local_node *ln)
{
	struct usteer_remote_node *rn;
	struct usteer_node *node;
	void *c;

	c = blobmsg_open_array(&b, "list");
	avl_for_each_element(&local_nodes, node, avl)
		usteer_add_rrm_data(ln, node);
	avl_for_each_element(&remote_nodes, rn, avl)
		usteer_add_rrm_data(ln, &rn->node);
	blobmsg_close_array(&b, c);
}

static void
usteer_local_node_state_next(struct uloop_timeout *timeout)
{
	struct usteer_local_node *ln;

	ln = container_of(timeout, struct usteer_local_node, req_timer);

	ln->req_state++;
	if (ln->req_state >= __REQ_MAX) {
		ln->req_state = REQ_IDLE;
		return;
	}

	blob_buf_init(&b, 0);
	switch (ln->req_state) {
	case REQ_CLIENTS:
		ubus_invoke_async(ubus_ctx, ln->obj_id, "get_clients", b.head, &ln->req);
		ln->req.data_cb = usteer_local_node_list_cb;
		break;
	case REQ_RRM_SET_LIST:
		usteer_local_node_prepare_rrm_set(ln);
		ubus_invoke_async(ubus_ctx, ln->obj_id, "rrm_nr_set", b.head, &ln->req);
		ln->req.data_cb = NULL;
		break;
	case REQ_RRM_GET_OWN:
		ubus_invoke_async(ubus_ctx, ln->obj_id, "rrm_nr_get_own", b.head, &ln->req);
		ln->req.data_cb = usteer_local_node_rrm_nr_cb;
		break;
	default:
		break;
	}
	ln->req.complete_cb = usteer_local_node_req_cb;
	ubus_complete_request_async(ubus_ctx, &ln->req);
}

static void
usteer_local_node_update(struct uloop_timeout *timeout)
{
	struct usteer_local_node *ln;
	struct usteer_node_handler *h;
	struct usteer_node *node;

	ln = container_of(timeout, struct usteer_local_node, update);
	node = &ln->node;

	MSG_T("local_sta_update", "timeout (%u) expired\n",
		config.local_sta_update);

	list_for_each_entry(h, &node_handlers, list) {
		if (!h->update_node)
			continue;

		h->update_node(node);
	}

	usteer_local_node_state_reset(ln);
	uloop_timeout_set(&ln->req_timer, 1);
	usteer_local_node_kick(ln);
	uloop_timeout_set(timeout, config.local_sta_update);
}

static struct usteer_local_node *
usteer_get_node(struct ubus_context *ctx, const char *name)
{
	struct usteer_local_node *ln;
	struct usteer_node *node;
	char *str;

	ln = avl_find_element(&local_nodes, name, ln, node.avl);
	if (ln)
		return ln;

	ln = calloc_a(sizeof(*ln), &str, strlen(name) + 1);
	node = &ln->node;
	node->type = NODE_TYPE_LOCAL;
	node->avl.key = strcpy(str, name);
	ln->ev.remove_cb = usteer_handle_remove;
	ln->ev.cb = usteer_handle_event;
	ln->update.cb = usteer_local_node_update;
	ln->req_timer.cb = usteer_local_node_state_next;
	ubus_register_subscriber(ctx, &ln->ev);
	avl_insert(&local_nodes, &node->avl);
	uloop_timeout_set(&ln->update, 1);
	INIT_LIST_HEAD(&node->sta_info);

	return ln;
}

static void
usteer_node_run_update_script(struct usteer_node *node)
{
	struct usteer_local_node *ln = container_of(node, struct usteer_local_node, node);
	char *val;

	if (!node_up_script)
		return;

	val = alloca(strlen(node_up_script) + strlen(ln->iface) + 8);
	sprintf(val, "%s '%s'", node_up_script, ln->iface);
	if (system(val))
		fprintf(stderr, "failed to execute %s\n", val);
}

static void
usteer_register_node(struct ubus_context *ctx, const char *name, uint32_t id)
{
	struct usteer_local_node *ln;
	struct usteer_node_handler *h;
	const char *iface;
	int offset = sizeof("hostapd.") - 1;

	iface = name + offset;
	if (strncmp(name, "hostapd.", iface - name) != 0)
		return;

	MSG(INFO, "Connecting to local node %s\n", name);
	ln = usteer_get_node(ctx, name);
	ln->obj_id = id;
	ln->iface = usteer_node_name(&ln->node) + offset;
	ln->ifindex = if_nametoindex(iface);

	blob_buf_init(&b, 0);
	blobmsg_add_u32(&b, "notify_response", 1);
	ubus_invoke(ctx, id, "notify_response", b.head, NULL, NULL, 1000);

	blob_buf_init(&b, 0);
	blobmsg_add_u8(&b, "neighbor_report", 1);
	blobmsg_add_u8(&b, "beacon_report", 1);
	blobmsg_add_u8(&b, "bss_transition", 1);
	ubus_invoke(ctx, id, "bss_mgmt_enable", b.head, NULL, NULL, 1000);

	ubus_subscribe(ctx, &ln->ev, id);

	list_for_each_entry(h, &node_handlers, list) {
		if (!h->init_node)
			continue;

		h->init_node(&ln->node);
	}

	usteer_node_run_update_script(&ln->node);
}

static void
usteer_event_handler(struct ubus_context *ctx, struct ubus_event_handler *ev,
		    const char *type, struct blob_attr *msg)
{
	static const struct blobmsg_policy policy[2] = {
		{ .name = "id", .type = BLOBMSG_TYPE_INT32 },
		{ .name = "path", .type = BLOBMSG_TYPE_STRING },
	};
	struct blob_attr *tb[2];
	const char *path;

	blobmsg_parse(policy, 2, tb, blob_data(msg), blob_len(msg));

	if (!tb[0] || !tb[1])
		return;

	path = blobmsg_data(tb[1]);
	usteer_register_node(ctx, path, blobmsg_get_u32(tb[0]));
}

static void
usteer_register_events(struct ubus_context *ctx)
{
	static struct ubus_event_handler handler = {
	    .cb = usteer_event_handler
	};

	ubus_register_event_handler(ctx, &handler, "ubus.object.add");
}

static void
node_list_cb(struct ubus_context *ctx, struct ubus_object_data *obj, void *priv)
{
	usteer_register_node(ctx, obj->path, obj->id);
}

void config_set_node_up_script(struct blob_attr *data)
{
	const char *val = blobmsg_get_string(data);
	struct usteer_node *node;

	if (node_up_script && !strcmp(val, node_up_script))
		return;

	free(node_up_script);

	if (!strlen(val)) {
		node_up_script = NULL;
		return;
	}

	node_up_script = strdup(val);

	avl_for_each_element(&local_nodes, node, avl)
		usteer_node_run_update_script(node);
}

void config_get_node_up_script(struct blob_buf *buf)
{
	if (!node_up_script)
		return;

	blobmsg_add_string(buf, "node_up_script", node_up_script);
}

void
usteer_local_nodes_init(struct ubus_context *ctx)
{
	usteer_register_events(ctx);
	ubus_lookup(ctx, "hostapd.*", node_list_cb, NULL);
}
