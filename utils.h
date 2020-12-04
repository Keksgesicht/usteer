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

#ifndef __APMGR_UTILS_H
#define __APMGR_UTILS_H

#define MSG(_nr, _format, ...) debug_msg(MSG_##_nr, __func__, __LINE__, _format, ##__VA_ARGS__)
#define MSG_CONT(_nr, _format, ...) debug_msg_cont(MSG_##_nr, _format, ##__VA_ARGS__)

/**
 * Used to format data into a string in MAC-address format.
 */
#define MAC_ADDR_FMT "%02x:%02x:%02x:%02x:%02x:%02x"

#define MAC_ADDR_DATA(_a) \
	((const uint8_t *)(_a))[0], \
	((const uint8_t *)(_a))[1], \
	((const uint8_t *)(_a))[2], \
	((const uint8_t *)(_a))[3], \
	((const uint8_t *)(_a))[4], \
	((const uint8_t *)(_a))[5]

#define MSG_T_STA(_option, _sta_addr, _format, ...) \
	MSG(DEBUG_ALL, "TESTCASE=" _option ",STA=" MAC_ADDR_FMT ": "  _format, \
	MAC_ADDR_DATA(_sta_addr), ##__VA_ARGS__)

#define MSG_T(_option,  _format, ...) \
	MSG(DEBUG_ALL, "TESTCASE=" _option ": "  _format,  ##__VA_ARGS__)

/** 
 * The usteer debug levels as enum. The index of the enum value
 * corresponds to the debug levels listed for example in the usteer documentation.
 */
enum usteer_debug {
	MSG_FATAL, 		// = 0
	MSG_INFO, 		// = 1
	MSG_VERBOSE, 	// = 2
	MSG_DEBUG, 		// = 3
	MSG_NETWORK, 	// = 4
	MSG_DEBUG_ALL, 	// = 5
};

		/* -----------< See 'main.c'. >-----------*/
extern void debug_msg(int level, const char *func, int line, const char *format, ...);
extern void debug_msg_cont(int level, const char *format, ...);

#define __usteer_init __attribute__((constructor))

#endif
