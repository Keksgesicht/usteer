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
 *   Copyright (C) 2021 Jan Braun <jan-kai@braun-bs.de>
 *   Copyright (C) 2021 Nico Petermann <nico.petermann3@gmail.com>
 */

#include <sys/types.h>
#include <stdlib.h>
#ifdef linux
#include <netinet/ether.h>
#endif

#include <libubox/blobmsg_json.h>

#include "node.h"
#include "usteer.h"
#include "hearing_map.h"

char *usteer_node_get_mac(struct usteer_node *node) {
	struct blobmsg_policy policy[3] = {
		{ .type = BLOBMSG_TYPE_STRING },
		{ .type = BLOBMSG_TYPE_STRING },
		{ .type = BLOBMSG_TYPE_STRING },
	};
	struct blob_attr *tb[3];
	blobmsg_parse_array(policy, ARRAY_SIZE(tb), tb, blobmsg_data(node->rrm_nr), blobmsg_data_len(node->rrm_nr));
	if (!tb[0])
		return "";
	return blobmsg_get_string(tb[0]);
}

int getChannelFromFreq(int freq) {
	/* see 802.11-2007 17.3.8.3.2 and Annex J */
	if (freq == 2484)
		return 14;
	else if (freq < 2484)
		return (freq - 2407) / 5;
	else if (freq >= 4910 && freq <= 4980)
		return (freq - 4000) / 5;
	else if (freq <= 45000) /* DMG band lower limit */
		return (freq - 5000) / 5;
	else if (freq >= 58320 && freq <= 64800)
		return (freq - 56160) / 2160;
	else
		return 0;
}

int getOPClassFromChannel(int channel) {
	if (channel >= 36 &&
		channel <= 48 ){

		return 115; // 1 in nicht global
	}
	else if(channel >= 52 &&
			channel <= 64 ){

		return 118; // 2
	}
	else if(
			channel >= 100 &&
			channel <= 140 ){

		return 121; // 3
	}
	else if(channel >= 1  &&
			channel <= 13 ){

		return 81; // 4
	}
	else{
		return 0; // z.B channel 14 nicht in dokument
	}
}

enum {
	BEACON_REP_ADDR,
	BEACON_REP_OP_CLASS,
	BEACON_REP_CHANNEL,
	BEACON_REP_RCPI,
	BEACON_REP_RSNI,
	BEACON_REP_BSSID,
	BEACON_REP_DURATION,
	BEACON_REP_START,
	__BEACON_REP_MAX,
};

static const struct blobmsg_policy beacon_rep_policy[__BEACON_REP_MAX] = {
	[BEACON_REP_BSSID] = {.name = "bssid", .type = BLOBMSG_TYPE_STRING},
	[BEACON_REP_ADDR] = {.name = "address", .type = BLOBMSG_TYPE_STRING},
	[BEACON_REP_OP_CLASS] = {.name = "op-class", .type = BLOBMSG_TYPE_INT16},
	[BEACON_REP_CHANNEL] = {.name = "channel", .type = BLOBMSG_TYPE_INT16},
	[BEACON_REP_RCPI] = {.name = "rcpi", .type = BLOBMSG_TYPE_INT16},
	[BEACON_REP_RSNI] = {.name = "rsni", .type = BLOBMSG_TYPE_INT16},
	[BEACON_REP_DURATION] = {.name = "duration", .type = BLOBMSG_TYPE_INT16},
	[BEACON_REP_START] = {.name = "start-time", .type = BLOBMSG_TYPE_INT64},
};

static inline struct usteer_node*
get_usteer_node_from_bssid(char* bssid)
{
	struct usteer_node *node;
	avl_for_each_element(&local_nodes, node, avl) {
		if (strcmp(bssid, usteer_node_get_mac(node)) == 0)
			return node;
	}
	struct usteer_remote_node *rn;
	avl_for_each_element(&remote_nodes, rn, avl) {
		if (strcmp(bssid, usteer_node_get_mac(&rn->node)) == 0)
			return &rn->node;
	}
	return NULL;
}

static void usteer_beacon_del(struct beacon_report *br) {
	if (br->bssid)
		list_del(&br->node_list);
	list_del(&br->sta_list);
	free(br);
}

void usteer_beacon_cleanup(struct sta_info *si, uint64_t time) {
	struct beacon_report *br, *tmp;
	list_for_each_entry_safe(br, tmp, &si->beacon, sta_list)
		if (br->start_time != time)
			usteer_beacon_del(br);
}

void usteer_handle_event_beacon(struct ubus_object *obj, struct blob_attr *msg) {
	struct usteer_local_node *ln = container_of(obj, struct usteer_local_node, ev.obj);
	struct usteer_node *node = &ln->node;
	struct blob_attr *tb[__BEACON_REP_MAX];
	struct beacon_report *br;
	struct sta_info *si;
	struct sta *sta;

	blobmsg_parse(beacon_rep_policy, __BEACON_REP_MAX, tb, blob_data(msg), blob_len(msg));
	if(!tb[BEACON_REP_BSSID] || !tb[BEACON_REP_ADDR])
		return;
	br = calloc(1, sizeof(struct beacon_report));

	char *bssid = blobmsg_get_string(tb[BEACON_REP_BSSID]);
	char *address = blobmsg_get_string(tb[BEACON_REP_ADDR]);
	uint8_t *addr = (uint8_t *) ether_aton(address);
	br->rcpi = blobmsg_get_u16(tb[BEACON_REP_RCPI]);
	br->rsni = blobmsg_get_u16(tb[BEACON_REP_RSNI]);
	br->op_class = blobmsg_get_u16(tb[BEACON_REP_OP_CLASS]);
	br->channel = blobmsg_get_u16(tb[BEACON_REP_CHANNEL]);
	br->duration = blobmsg_get_u16(tb[BEACON_REP_DURATION]);
	br->start_time = blobmsg_get_u64(tb[BEACON_REP_START]);

	sta = usteer_sta_get(addr, false);
	if(!sta) return;
	si = usteer_sta_info_get(sta, node, NULL);
	if (!si) return;
	br->address = si;
	br->bssid = get_usteer_node_from_bssid(bssid);

	MSG(DEBUG, "received beacon-report {op-class=%d, channel=%d, rcpi=%d, rsni=%d, start=%llu, bssid=%s} on %s from %s",
		br->op_class, br->channel, br->rcpi, br->rsni, br->start_time, bssid, ln->iface, address);
	list_add(&br->sta_list, &si->beacon);
	if (br->bssid) {
		MSG(DEBUG, "bssid=%s is %s node %p", bssid, br->bssid->type ? "remote" : "local", br->bssid);
		list_add(&br->node_list, &br->bssid->beacon);
	}
	usteer_beacon_cleanup(si, br->start_time);
}