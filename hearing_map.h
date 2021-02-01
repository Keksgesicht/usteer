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

int getChannelFromFreq(int freq);
int getOPClassFromChannel(int channel);

void usteer_beacon_request_check(struct sta_info *si);

void usteer_beacon_cleanup(struct sta_info *si, uint64_t time);
void usteer_handle_event_beacon(struct ubus_object *obj, struct blob_attr *msg);

char *usteer_node_get_mac(struct usteer_node *node);

#endif
