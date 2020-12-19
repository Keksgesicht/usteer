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

#include <sys/types.h>
#include <sys/socket.h>
#include <net/ethernet.h>
#ifdef linux
#include <netinet/ether.h>
#endif

#include "usteer.h"
#include "node.h"

static struct blob_buf b;

static int
usteer_ubus_get_clients(struct ubus_context *ctx, struct ubus_object *obj,
		       struct ubus_request_data *req, const char *method,
		       struct blob_attr *msg)
{
	struct sta_info *si;
	struct sta *sta;
	char str[20];
	void *_s, *_cur_n;

	blob_buf_init(&b, 0);
	avl_for_each_element(&stations, sta, avl) {
		sprintf(str, MAC_ADDR_FMT, MAC_ADDR_DATA(sta->addr));
		_s = blobmsg_open_table(&b, str);
		list_for_each_entry(si, &sta->nodes, list) {
			_cur_n = blobmsg_open_table(&b, usteer_node_name(si->node));
			blobmsg_add_u8(&b, "connected", si->connected);
			blobmsg_add_u32(&b, "signal", si->signal);
			blobmsg_close_table(&b, _cur_n);
		}
		blobmsg_close_table(&b, _s);
	}
	ubus_send_reply(ctx, req, b.head);
	return 0;
}

static struct blobmsg_policy client_arg[] = {
	{ .name = "address", .type = BLOBMSG_TYPE_STRING, },
};

static void
usteer_ubus_add_stats(struct sta_info_stats *stats, const char *name)
{
	void *s;

	s = blobmsg_open_table(&b, name);
	blobmsg_add_u32(&b, "requests", stats->requests);
	blobmsg_add_u32(&b, "blocked_cur", stats->blocked_cur);
	blobmsg_add_u32(&b, "blocked_total", stats->blocked_total);
	blobmsg_close_table(&b, s);
}

static int
usteer_ubus_get_client_info(struct ubus_context *ctx, struct ubus_object *obj,
			   struct ubus_request_data *req, const char *method,
			   struct blob_attr *msg)
{
	struct sta_info *si;
	struct sta *sta;
	struct blob_attr *mac_str;
	uint8_t *mac;
	void *_n, *_cur_n, *_s;
	int i;

	blobmsg_parse(client_arg, 1, &mac_str, blob_data(msg), blob_len(msg));
	if (!mac_str)
		return UBUS_STATUS_INVALID_ARGUMENT;

	mac = (uint8_t *) ether_aton(blobmsg_data(mac_str));
	if (!mac)
		return UBUS_STATUS_INVALID_ARGUMENT;

	sta = usteer_sta_get(mac, false);
	if (!sta)
		return UBUS_STATUS_NOT_FOUND;

	blob_buf_init(&b, 0);
	blobmsg_add_u8(&b, "2ghz", sta->seen_2ghz);
	blobmsg_add_u8(&b, "5ghz", sta->seen_5ghz);
	_n = blobmsg_open_table(&b, "nodes");
	list_for_each_entry(si, &sta->nodes, list) {
		_cur_n = blobmsg_open_table(&b, usteer_node_name(si->node));
		blobmsg_add_u8(&b, "connected", si->connected);
		blobmsg_add_u32(&b, "signal", si->signal);
		_s = blobmsg_open_table(&b, "stats");
		for (i = 0; i < __EVENT_TYPE_MAX; i++)
			usteer_ubus_add_stats(&si->stats[EVENT_TYPE_PROBE], event_types[i]);
		blobmsg_close_table(&b, _s);
		blobmsg_add_u64(&b, "average_data_rate", usteer_local_node_active_bytes(si));
		blobmsg_close_table(&b, _cur_n);
	}
	blobmsg_close_table(&b, _n);

	ubus_send_reply(ctx, req, b.head);

	return 0;
}

enum cfg_type {
	CFG_BOOL,
	CFG_I32,
	CFG_U32,
	CFG_ARRAY_CB,
	CFG_STRING_CB,
};

struct cfg_item {
	enum cfg_type type;
	union {
		bool *BOOL;
		uint32_t *U32;
		int32_t *I32;
		struct {
			void (*set)(struct blob_attr *data);
			void (*get)(struct blob_buf *buf);
		} CB;
	} ptr;
};

#define __config_items \
	_cfg(BOOL, syslog), \
	_cfg(U32, debug_level), \
	_cfg(U32, sta_block_timeout), \
	_cfg(U32, local_sta_timeout), \
	_cfg(U32, local_sta_update), \
	_cfg(U32, max_retry_band), \
	_cfg(U32, seen_policy_timeout), \
	_cfg(U32, load_balancing_threshold), \
	_cfg(U32, band_steering_threshold), \
	_cfg(U32, remote_update_interval), \
	_cfg(I32, min_connect_snr), \
	_cfg(I32, min_snr), \
	_cfg(I32, roam_scan_snr), \
	_cfg(U32, roam_scan_tries), \
	_cfg(U32, roam_scan_interval), \
	_cfg(I32, roam_trigger_snr), \
	_cfg(U32, roam_trigger_interval), \
	_cfg(U32, roam_kick_delay), \
	_cfg(U32, signal_diff_threshold), \
	_cfg(U32, initial_connect_delay), \
	_cfg(BOOL, load_kick_enabled), \
	_cfg(U32, load_kick_threshold), \
	_cfg(U32, load_kick_delay), \
	_cfg(U32, load_kick_min_clients), \
	_cfg(U32, load_kick_reason_code), \
	_cfg(U32, kick_client_active_sec), \
    _cfg(U32, kick_client_active_kbits), \
	_cfg(ARRAY_CB, interfaces), \
	_cfg(STRING_CB, node_up_script)

enum cfg_items {
#define _cfg(_type, _name) CFG_##_name
	__config_items,
#undef _cfg
	__CFG_MAX,
};

static const struct blobmsg_policy config_policy[__CFG_MAX] = {
#define _cfg_policy(_type, _name) [CFG_##_name] = { .name = #_name, .type = BLOBMSG_TYPE_ ## _type }
#define _cfg_policy_BOOL(_name) _cfg_policy(BOOL, _name)
#define _cfg_policy_U32(_name) _cfg_policy(INT32, _name)
#define _cfg_policy_I32(_name) _cfg_policy(INT32, _name)
#define _cfg_policy_ARRAY_CB(_name) _cfg_policy(ARRAY, _name)
#define _cfg_policy_STRING_CB(_name) _cfg_policy(STRING, _name)
#define _cfg(_type, _name) _cfg_policy_##_type(_name)
	__config_items,
#undef _cfg
};

static const struct cfg_item config_data[__CFG_MAX] = {
#define _cfg_data_BOOL(_name) .ptr.BOOL = &config._name
#define _cfg_data_U32(_name) .ptr.U32 = &config._name
#define _cfg_data_I32(_name) .ptr.I32 = &config._name
#define _cfg_data_ARRAY_CB(_name) .ptr.CB = { .set = config_set_##_name, .get = config_get_##_name }
#define _cfg_data_STRING_CB(_name) .ptr.CB = { .set = config_set_##_name, .get = config_get_##_name }
#define _cfg(_type, _name) [CFG_##_name] = { .type = CFG_##_type, _cfg_data_##_type(_name) }
	__config_items,
#undef _cfg
};

static int
usteer_ubus_get_config(struct ubus_context *ctx, struct ubus_object *obj,
		      struct ubus_request_data *req, const char *method,
		      struct blob_attr *msg)
{
	int i;

	blob_buf_init(&b, 0);
	for (i = 0; i < __CFG_MAX; i++) {
		switch(config_data[i].type) {
		case CFG_BOOL:
			blobmsg_add_u8(&b, config_policy[i].name,
					*config_data[i].ptr.BOOL);
			break;
		case CFG_I32:
		case CFG_U32:
			blobmsg_add_u32(&b, config_policy[i].name,
					*config_data[i].ptr.U32);
			break;
		case CFG_ARRAY_CB:
		case CFG_STRING_CB:
			config_data[i].ptr.CB.get(&b);
			break;
		}
	}
	ubus_send_reply(ctx, req, b.head);
	return 0;
}

static int
usteer_ubus_set_config(struct ubus_context *ctx, struct ubus_object *obj,
		      struct ubus_request_data *req, const char *method,
		      struct blob_attr *msg)
{
	struct blob_attr *tb[__CFG_MAX];
	int i;

	if (!strcmp(method, "set_config"))
		usteer_init_defaults();

	blobmsg_parse(config_policy, __CFG_MAX, tb, blob_data(msg), blob_len(msg));
	for (i = 0; i < __CFG_MAX; i++) {
		if (!tb[i])
			continue;

		switch(config_data[i].type) {
		case CFG_BOOL:
			*config_data[i].ptr.BOOL = blobmsg_get_u8(tb[i]);
			break;
		case CFG_I32:
		case CFG_U32:
			*config_data[i].ptr.U32 = blobmsg_get_u32(tb[i]);
			break;
		case CFG_ARRAY_CB:
		case CFG_STRING_CB:
			config_data[i].ptr.CB.set(tb[i]);
			break;
		}
	}

	return 0;
}

static void
usteer_dump_node_info(struct usteer_node *node)
{
	void *c;

	c = blobmsg_open_table(&b, usteer_node_name(node));
	blobmsg_add_u32(&b, "freq", node->freq);
	blobmsg_add_u32(&b, "n_assoc", node->n_assoc);
	blobmsg_add_u32(&b, "noise", node->noise);
	blobmsg_add_u32(&b, "load", node->load);
	blobmsg_add_u32(&b, "max_assoc", node->max_assoc);
	if (node->rrm_nr)
		blobmsg_add_field(&b, BLOBMSG_TYPE_ARRAY, "rrm_nr",
				  blobmsg_data(node->rrm_nr),
				  blobmsg_data_len(node->rrm_nr));
	blobmsg_close_table(&b, c);
}

static int
usteer_ubus_local_info(struct ubus_context *ctx, struct ubus_object *obj,
		      struct ubus_request_data *req, const char *method,
		      struct blob_attr *msg)
{
	struct usteer_node *node;

	blob_buf_init(&b, 0);

	avl_for_each_element(&local_nodes, node, avl)
		usteer_dump_node_info(node);

	ubus_send_reply(ctx, req, b.head);

	return 0;
}

static int
usteer_ubus_remote_info(struct ubus_context *ctx, struct ubus_object *obj,
		       struct ubus_request_data *req, const char *method,
		       struct blob_attr *msg)
{
	struct usteer_remote_node *rn;

	blob_buf_init(&b, 0);

	avl_for_each_element(&remote_nodes, rn, avl)
		usteer_dump_node_info(&rn->node);

	ubus_send_reply(ctx, req, b.head);

	return 0;
}

static const struct ubus_method usteer_methods[] = {
	UBUS_METHOD_NOARG("local_info", usteer_ubus_local_info),
	UBUS_METHOD_NOARG("remote_info", usteer_ubus_remote_info),
	UBUS_METHOD_NOARG("get_clients", usteer_ubus_get_clients),
	UBUS_METHOD("get_client_info", usteer_ubus_get_client_info, client_arg),
	UBUS_METHOD_NOARG("get_config", usteer_ubus_get_config),
	UBUS_METHOD("set_config", usteer_ubus_set_config, config_policy),
	UBUS_METHOD("update_config", usteer_ubus_set_config, config_policy),
};

static struct ubus_object_type usteer_obj_type =
	UBUS_OBJECT_TYPE("usteer", usteer_methods);

static struct ubus_object usteer_obj = {
	.name = "usteer",
	.type = &usteer_obj_type,
	.methods = usteer_methods,
	.n_methods = ARRAY_SIZE(usteer_methods),
};

static void
usteer_add_nr_entry(struct usteer_node *ln, struct usteer_node *node)
{
	struct blobmsg_policy policy[3] = {
		{ .type = BLOBMSG_TYPE_STRING },
		{ .type = BLOBMSG_TYPE_STRING },
		{ .type = BLOBMSG_TYPE_STRING },
	};
	struct blob_attr *tb[3];

	if (!node->rrm_nr)
		return;

	if (strcmp(ln->ssid, node->ssid) != 0)
		return;

	blobmsg_parse_array(policy, ARRAY_SIZE(tb), tb,
			    blobmsg_data(node->rrm_nr),
			    blobmsg_data_len(node->rrm_nr));
	if (!tb[2])
		return;

	blobmsg_add_field(&b, BLOBMSG_TYPE_STRING, "",
			  blobmsg_data(tb[2]),
			  blobmsg_data_len(tb[2]));
}

int usteer_ubus_notify_client_disassoc(struct sta_info *si)
{
	struct usteer_local_node *ln = container_of(si->node, struct usteer_local_node, node);
	struct usteer_remote_node *rn;
	struct usteer_node *node;
	void *c;

	blob_buf_init(&b, 0);
	blobmsg_printf(&b, "addr", MAC_ADDR_FMT, MAC_ADDR_DATA(si->sta->addr));
	blobmsg_add_u32(&b, "duration", config.roam_kick_delay);
	c = blobmsg_open_array(&b, "neighbors");
	avl_for_each_element(&local_nodes, node, avl)
		usteer_add_nr_entry(si->node, node);
	avl_for_each_element(&remote_nodes, rn, avl)
		usteer_add_nr_entry(si->node, &rn->node);
	blobmsg_close_array(&b, c);
	return ubus_invoke(ubus_ctx, ln->obj_id, "wnm_disassoc_imminent", b.head, NULL, 0, 100);
}

int usteer_ubus_trigger_client_scan(struct sta_info *si)
{
	struct usteer_local_node *ln = container_of(si->node, struct usteer_local_node, node);

	si->scan_band = !si->scan_band;

	MSG_T_STA("load_kick_reason_code", si->sta->addr,
		"tell hostapd to issue a client beacon request (5ghz: %d)\n",
		si->scan_band);

	blob_buf_init(&b, 0);
	blobmsg_printf(&b, "addr", MAC_ADDR_FMT, MAC_ADDR_DATA(si->sta->addr));
	blobmsg_add_u32(&b, "mode", 1);
	blobmsg_add_u32(&b, "duration", 65535);
	blobmsg_add_u32(&b, "channel", 255);
	blobmsg_add_u32(&b, "op_class", si->scan_band ? 1 : 12);
	return ubus_invoke(ubus_ctx, ln->obj_id, "rrm_beacon_req", b.head, NULL, 0, 100);
}

void usteer_ubus_kick_client(struct sta_info *si)
{
	struct usteer_local_node *ln = container_of(si->node, struct usteer_local_node, node);

	MSG_T_STA("load_kick_reason_code", si->sta->addr,
		"tell hostapd to kick client with reason code %u\n",
		config.load_kick_reason_code);

	blob_buf_init(&b, 0);
	blobmsg_printf(&b, "addr", MAC_ADDR_FMT, MAC_ADDR_DATA(si->sta->addr));
	blobmsg_add_u32(&b, "reason", config.load_kick_reason_code);
	blobmsg_add_u8(&b, "deauth", 1);
	ubus_invoke(ubus_ctx, ln->obj_id, "del_client", b.head, NULL, 0, 100);
	si->connected = 0;
	si->roam_kick = current_time;
}

void usteer_ubus_init(struct ubus_context *ctx)
{
	ubus_add_object(ctx, &usteer_obj);
}
