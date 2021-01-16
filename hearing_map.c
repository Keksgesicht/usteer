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
