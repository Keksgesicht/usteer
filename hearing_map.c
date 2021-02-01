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
 * 	 Copyright (C) 2021 Tomas Duchac <tomasduchac@protonmail.ch>
 * 	 Copyright (C) 2021 Philip Jonas Franz <R41Da@gmx.de>
 */

#include <sys/types.h>
#include <stdlib.h>
#ifdef linux
#include <netinet/ether.h>
#endif

#include <libubox/avl-cmp.h>
#include <libubox/blobmsg_json.h>

#include "node.h"
/* #include "usteer.h" */
#include "hearing_map.h"

static struct blob_buf b;

static struct usteer_node* get_usteer_node_from_bssid(uint8_t *bssid){
	struct usteer_node *node;
	avl_for_each_element(&local_nodes, node, avl) {
		if (memcmp(&node->mac, bssid, 6) == 0)
			return node;
	}
	struct usteer_remote_node *rn;
	avl_for_each_element(&remote_nodes, rn, avl) {
		if (memcmp(&rn->node.mac, bssid, 6) == 0)
			return &rn->node;
	}
	return NULL;
}

static int
usteer_beacon_request_send(struct sta_info * si)
{
	struct usteer_local_node *ln = container_of(si->node, struct usteer_local_node, node);
	struct usteer_node *un = &ln->node;
	int freq = un->freq;
	int channel = getChannelFromFreq(freq);
	int opClass = getOPClassFromChannel(channel);
	
	blob_buf_init(&b, 0);
	blobmsg_printf(&b, "addr", MAC_ADDR_FMT, MAC_ADDR_DATA(si->sta->addr));
	blobmsg_add_u32(&b, "mode", 1);
	blobmsg_add_u32(&b, "duration", 10);
	blobmsg_add_u32(&b, "channel", channel);
	blobmsg_add_u32(&b, "op_class", opClass);
	ubus_invoke(ubus_ctx, ln->obj_id, "rrm_beacon_req", b.head, NULL,0 , 100);

	MSG(DEBUG, "Sent Beacon Request to "MAC_ADDR_FMT,  MAC_ADDR_DATA(si->sta->addr));
	return 0;
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

void usteer_beacon_request_check(struct sta_info *si) {
	struct beacon_request * br = &si->beacon_rqst;

	// based on the current reception, determine a the frequency beacon requests are sent.
	uint64_t ctime = current_time;
	MSG(DEBUG, "Current signal strength: %d", si->signal);

	/* Adjust signal range from (-90 to -30) to (-30 to 30) */
	float adj_signal =(float) (si->signal + 60);

	float dyn_freq = config.beacon_request_frequency +
					 (config.beacon_request_signal_modifier * (adj_signal / (1 + abs(adj_signal))));

	MSG(DEBUG, "dyn_freq=%f, ctime=%llu", dyn_freq, ctime);

	if (ctime - br->lastRequestTime < dyn_freq)
		return;

	usteer_beacon_request_send(si);
	br->lastRequestTime = ctime;
	si->beacon_rqst = *br;
}

static void usteer_beacon_del(struct beacon_report *br) {
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
	br = malloc(sizeof(struct beacon_report));

	char *address = blobmsg_get_string(tb[BEACON_REP_ADDR]);
	uint8_t *addr_sta = (uint8_t *) ether_aton(address);
	sta = usteer_sta_get(addr_sta, false);
	if(!sta) return;
	si = usteer_sta_info_get(sta, node, NULL);
	if (!si) return;
	br->address = si;

	char *bssid = blobmsg_get_string(tb[BEACON_REP_BSSID]);
	uint8_t *addr = (uint8_t *) ether_aton(bssid);
	memcpy(br->bssid, addr, sizeof(br->bssid));

	br->rcpi = blobmsg_get_u16(tb[BEACON_REP_RCPI]);
	br->rsni = blobmsg_get_u16(tb[BEACON_REP_RSNI]);
	br->op_class = blobmsg_get_u16(tb[BEACON_REP_OP_CLASS]);
	br->channel = blobmsg_get_u16(tb[BEACON_REP_CHANNEL]);
	br->duration = blobmsg_get_u16(tb[BEACON_REP_DURATION]);
	br->start_time = blobmsg_get_u64(tb[BEACON_REP_START]);

	MSG(DEBUG, "received beacon-report {op-class=%d, channel=%d, rcpi=%d, rsni=%d, start=%llu, bssid=%s} on %s from %s",
		br->op_class, br->channel, br->rcpi, br->rsni, br->start_time, bssid, ln->iface, address);
	list_add(&br->sta_list, &si->beacon);
	usteer_beacon_cleanup(si, br->start_time);

	node = get_usteer_node_from_bssid(br->bssid);
	if (node)
		MSG(DEBUG, "bssid=%s is %s node %p", bssid, node->type ? "remote" : "local", node);
}