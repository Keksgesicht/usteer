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

#include <libubox/blobmsg_json.h>

#include "node.h"
#include "hearing_map.h"

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
	if (channel >= 36 ||
		channel <= 48 ){

		return 115; // 1 in nicht global
	}
	else if(channel >= 52 ||
			channel <= 64 ){

		return 118; // 2
	}
	else if(
			channel >= 100 ||
			channel <= 140 ){

		return 121; // 3
	}
	else if(channel >= 1  ||
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
	__BEACON_REP_MAX,
};

static const struct blobmsg_policy beacon_rep_policy[__BEACON_REP_MAX] = {
	[BEACON_REP_BSSID] = {.name = "bssid", .type = BLOBMSG_TYPE_STRING},
	[BEACON_REP_ADDR] = {.name = "address", .type = BLOBMSG_TYPE_STRING},
	[BEACON_REP_OP_CLASS] = {.name = "op-class", .type = BLOBMSG_TYPE_INT16},
	[BEACON_REP_CHANNEL] = {.name = "channel", .type = BLOBMSG_TYPE_INT16},
	[BEACON_REP_RCPI] = {.name = "rcpi", .type = BLOBMSG_TYPE_INT16},
	[BEACON_REP_RSNI] = {.name = "rsni", .type = BLOBMSG_TYPE_INT16},
};

void usteer_handle_event_beacon(struct ubus_object *obj, struct blob_attr *msg) {
	struct blob_attr *tb[__BEACON_REP_MAX];
	struct usteer_local_node *ln;

	blobmsg_parse(beacon_rep_policy, __BEACON_REP_MAX, tb, blob_data(msg), blob_len(msg));
	if(!tb[BEACON_REP_BSSID]   || !tb[BEACON_REP_ADDR] || !tb[BEACON_REP_OP_CLASS]
	|| !tb[BEACON_REP_CHANNEL] || !tb[BEACON_REP_RCPI] || !tb[BEACON_REP_RSNI])
		return;

	char *bssid = blobmsg_get_string(tb[BEACON_REP_BSSID]);
	char *address = blobmsg_get_string(tb[BEACON_REP_ADDR]);
	uint16_t op_class = blobmsg_get_u16(tb[BEACON_REP_OP_CLASS]);
	uint16_t channel = blobmsg_get_u16(tb[BEACON_REP_CHANNEL]);
	uint16_t rcpi = blobmsg_get_u16(tb[BEACON_REP_RCPI]);
	uint16_t rsni = blobmsg_get_u16(tb[BEACON_REP_RSNI]);
	ln = container_of(obj, struct usteer_local_node, ev.obj);

	MSG(DEBUG, "received beacon-report {op-class=%d, channel=%d, rcpi=%d, rsni=%d, bssid=%s} on %s from %s",
		op_class, channel, rcpi, rsni, bssid, ln->iface, address);
}