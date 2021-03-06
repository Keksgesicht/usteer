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
#include "usteer.h"
#include "hearing_map.h"

static struct blob_buf b;

struct usteer_node*
get_usteer_node_from_bssid(uint8_t *bssid)
{
	struct usteer_node *node;
	avl_for_each_element(&local_nodes, node, avl) 
	{
		if (memcmp(&node->bssid, bssid, 6) == 0)
			return node;
	}
	struct usteer_remote_node *rn;
	avl_for_each_element(&remote_nodes, rn, avl) {
		if (memcmp(&rn->node.bssid, bssid, 6) == 0)
			return &rn->node;
	}
	return NULL;
}

static inline void
usteer_beacon_report_free(struct beacon_report *br)
{
	list_del(&br->sta_list);
	free(br);
}

static bool
usteer_beacon_report_delete(struct beacon_report *br, uint8_t *bssid)
{
	if (bssid && memcmp(br->bssid, bssid, 6) == 0) {
		usteer_beacon_report_free(br);
		return true;
	}
	uint64_t time_diff = current_time - br->usteer_time;
	uint64_t time_diff_conf = config.beacon_report_invalide_timeout * 1000;
	if (time_diff_conf < time_diff) {
		usteer_beacon_report_free(br);
		return true;
	}
	return false;
}

void usteer_ubus_hearing_map(struct blob_buf *bm, struct sta_info *si) 
{
	struct beacon_report *br;
	void *_hm, *_nr;

	_hm = blobmsg_open_table(bm, "hearing_map");
	list_for_each_entry(br, &si->beacon_reports, sta_list) {
		_nr = blobmsg_open_table(bm, ether_ntoa((struct ether_addr *) br->bssid));
		struct usteer_node *node = get_usteer_node_from_bssid(br->bssid);
		if (node)
			blobmsg_add_string(bm, "node", usteer_node_name(node));

		uint64_t diff_secs = (current_time - br->usteer_time) / 1000;
		uint64_t ttl = config.beacon_report_invalide_timeout - diff_secs;

		blobmsg_add_u16(bm, "rcpi", br->rcpi);
		blobmsg_add_u16(bm, "rsni", br->rsni);
		blobmsg_add_u16(bm, "op_class", br->op_class);
		blobmsg_add_u16(bm, "channel", br->channel);
		blobmsg_add_u64(bm, "time-to-live", ttl);
		blobmsg_close_table(bm, _nr);
	}
	blobmsg_close_table(bm, _hm);
}

static void
usteer_beacon_request_send(struct sta_info * si, int freq, uint8_t mode)
{
	static struct ubus_request req;
	struct usteer_local_node *ln = container_of(si->node, struct usteer_local_node, node);
	int channel = get_channel_from_freq(freq);
	int opClass = get_op_class_from_channel(channel);
	
	blob_buf_init(&b, 0);
	blobmsg_printf(&b, "addr", MAC_ADDR_FMT, MAC_ADDR_DATA(si->sta->addr));
	blobmsg_add_u32(&b, "mode", mode);
	blobmsg_add_u32(&b, "duration", 200);
	blobmsg_add_u32(&b, "channel", channel);
	blobmsg_add_u32(&b, "op_class", opClass);
	blobmsg_add_string(&b, "bssid", "ff:ff:ff:ff:ff:ff");

	ubus_invoke_async(ubus_ctx, ln->obj_id, "rrm_beacon_req", b.head, &req);
	req.data_cb = NULL;
	MSG(DEBUG, "send beacon-request {channel=%d, mode=%hhu} on %s to "MAC_ADDR_FMT,
		channel, mode, ln->iface, MAC_ADDR_DATA(si->sta->addr));
}

int get_channel_from_freq(int freq) 
{
	/* see 802.11-2007 17.3.8.3.2 and Annex J */
	if (freq == 2484)
		return 14;
	else if (freq < 2484)
		return (freq - 2407) / 5;
	else if (freq >= 4910 && freq <= 4980)
		return (freq - 4000) / 5;
	else if (freq <= 45000)
		return (freq - 5000) / 5;
	else if (freq >= 58320 && freq <= 64800)
		return (freq - 56160) / 2160;
	else
		return 0;
}

int get_op_class_from_channel(int channel) 
{
	if (channel >= 36 && channel <= 48 )
		return 115;
	else if(channel >= 52 && channel <= 64 )
		return 118;
	else if(channel >= 100 && channel <= 140 )
		return 121;
	else if(channel >= 1 && channel <= 13 )
		return 81;
	else
		return 0;
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

static uint8_t
usteer_get_beacon_request_mode(struct sta_info *si, int freq)
{
	struct beacon_request *br = &si->beacon_request;
	if (si->node->freq == freq) {
		long time_diff = br->lastReportTime - br->lastRequestTime;
		if (0 < time_diff)
			br->failed_requests /= 2;
		br->failed_requests++;
	}

	int failed_requests = br->failed_requests;
	if (freq < 4000) {
		if (failed_requests < 3)
			return 1;
		if (failed_requests < 7)
			return 0;
	}
	if (freq > 4000 && failed_requests < 5)
		return 0;

	return 2;
}

static inline int
usteer_beacon_request_next_band(struct sta_info *si, int freq)
{
	struct usteer_node *node;
	bool band_already_scanned = true;

	/*
	 * search for next band that was not used in this scan request
	 * assums that no two local_nodes are using the same channel/frequency
	 */
	avl_for_each_element(&local_nodes, node, avl) {
		if (freq == node->freq) {
			band_already_scanned = false;
			continue;
		}
		if (band_already_scanned)
			continue;
		return node->freq;
	}
	node = avl_first_element(&local_nodes, node, avl); // last -> first (cycle list)
	return node->freq;
}

void usteer_beacon_request_check(struct sta_info *si) 
{
	struct beacon_request *br = &si->beacon_request;
	struct usteer_node *node = si->node;
	uint64_t ctime = current_time;
	int freq = br->band;

	/*
	 * based on the current reception, determine a the frequency beacon requests are sent.
	 * Adjust signal range from (-90 to -30) to (-30 to 30)
	 * */
	float adj_signal = (float) (si->signal + 60);
	float dyn_freq = config.beacon_request_frequency +
					 (config.beacon_request_signal_modifier * (adj_signal / (1 + abs(adj_signal))));
	if (freq == node->freq && ctime - br->lastRequestTime < dyn_freq)
		return;

	uint8_t mode = usteer_get_beacon_request_mode(si, freq); // run before lastRequestTime is renewed
	usteer_beacon_request_send(si, freq, mode);

	/* do only once in a scan row (multiple bands) */
	if (br->band == node->freq) {
		br->lastRequestTime = ctime;
		uint8_t bssid[6];
		memset(bssid, 255, 6); // FF:FF:FF:FF:FF:FF should be a invalid bssid
		usteer_beacon_report_cleanup(si, bssid);
	}

	/* select next band for scanning */
	br->band = usteer_beacon_request_next_band(si, freq);
}

void usteer_beacon_report_cleanup(struct sta_info *si, uint8_t *bssid) 
{
	struct beacon_report *br, *tmp;
	list_for_each_entry_safe(br, tmp, &si->beacon_reports, sta_list)
		bssid ? usteer_beacon_report_delete(br, bssid)
		      : usteer_beacon_report_free(br);
}

void usteer_handle_event_beacon_report(struct usteer_local_node *ln, struct blob_attr *msg) 
{
	struct usteer_node *node = &ln->node;
	struct blob_attr *tb[__BEACON_REP_MAX];
	struct beacon_report *br;
	struct sta_info *si;
	struct sta *sta;

	blobmsg_parse(beacon_rep_policy, __BEACON_REP_MAX, tb, blob_data(msg), blob_len(msg));
	if(!tb[BEACON_REP_BSSID] || !tb[BEACON_REP_ADDR])
		return;

	char *address = blobmsg_get_string(tb[BEACON_REP_ADDR]);
	uint8_t *addr_sta = (uint8_t *) ether_aton(address);
	sta = usteer_sta_get(addr_sta, false);
	if(!sta) return;
	si = usteer_sta_info_get(sta, node, NULL);
	if (!si) return;

	char *bssid = blobmsg_get_string(tb[BEACON_REP_BSSID]);
	uint8_t *addr = (uint8_t *) ether_aton(bssid);
	if(!get_usteer_node_from_bssid(addr))
		return;

	br = malloc(sizeof(struct beacon_report));
	br->address = si;
	memcpy(br->bssid, addr, sizeof(br->bssid));
	br->rcpi = blobmsg_get_u16(tb[BEACON_REP_RCPI]);
	br->rsni = blobmsg_get_u16(tb[BEACON_REP_RSNI]);
	br->op_class = blobmsg_get_u16(tb[BEACON_REP_OP_CLASS]);
	br->channel = blobmsg_get_u16(tb[BEACON_REP_CHANNEL]);
	br->usteer_time = current_time; // beacon_report_invalide_timeout
	si->beacon_request.lastReportTime = br->usteer_time;

	MSG(DEBUG, "received beacon-report {op-class=%d, channel=%d, rcpi=%d, rsni=%d, bssid=%s} on %s from %s",
		br->op_class, br->channel, br->rcpi, br->rsni, bssid, ln->iface, address);
	usteer_beacon_report_cleanup(si, br->bssid);
	list_add(&br->sta_list, &si->beacon_reports);
}