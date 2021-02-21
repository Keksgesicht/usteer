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
 *   Copyright (C) 2021 Tomas Duchac <tomasduchac@protonmail.ch>
 * 	 Copyright (C) 2021 Philip Jonas Franz <R41Da@gmx.de>
 */

#ifndef __APMGR_HEARING_MAP_H
#define __APMGR_HEARING_MAP_H

#include <sys/types.h>

#include "node.h"
#include "usteer.h"

struct beacon_report {
	struct list_head sta_list;
	struct sta_info *address;
	uint8_t bssid[6];
	uint16_t rcpi;
	uint16_t rsni;
	uint16_t op_class;
	uint16_t channel;
	uint64_t usteer_time;
};

int getChannelFromFreq(int freq);
int getOPClassFromChannel(int channel);

void usteer_ubus_hearing_map(struct blob_buf *bm, struct sta_info *si);
void usteer_beacon_request_check(struct sta_info *si);
void usteer_beacon_report_cleanup(struct sta_info *si, uint8_t *bssid);
void usteer_handle_event_beacon_report(struct usteer_local_node *ln, struct blob_attr *msg);

#endif
