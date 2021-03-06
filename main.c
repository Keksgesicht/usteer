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

#include <unistd.h>
#include <stdarg.h>
#include <syslog.h>

#include "usteer.h"

struct ubus_context *ubus_ctx;
struct usteer_config config = {};
uint64_t current_time;

LIST_HEAD(node_handlers);

const char * const event_types[__EVENT_TYPE_MAX] = {
	[EVENT_TYPE_PROBE] = "probe",
	[EVENT_TYPE_AUTH] = "auth",
	[EVENT_TYPE_ASSOC] = "assoc",
	[EVENT_TYPE_BEACON] = "beacon-report",
};

void debug_msg(int level, const char *func, int line, const char *format, ...)
{
	va_list ap;

	if (config.debug_level < level)
		return;

	if (!config.syslog)
		fprintf(stderr, "[%s:%d] ", func, line);

	va_start(ap, format);
	if (config.syslog)
		vsyslog(level >= MSG_DEBUG ? LOG_DEBUG : LOG_INFO, format, ap);
	else
		vfprintf(stderr, format, ap);
	va_end(ap);

}

void debug_msg_cont(int level, const char *format, ...)
{
	va_list ap;

	if (config.debug_level < level)
		return;

	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
}

void usteer_init_defaults(void)
{
	memset(&config, 0, sizeof(config));

	config.sta_block_timeout = 30 * 1000;
	config.local_sta_timeout = 120 * 1000;
	config.local_sta_update = 1 * 1000;
	config.max_retry_band = 5;
	config.seen_policy_timeout = 30 * 1000;
	config.band_steering_threshold = 5;
	config.load_balancing_threshold = 5;
	config.vendor_update_interval = 60 * 1000;
	config.remote_update_interval = 1000;
	config.initial_connect_delay = 0;
	config.remote_node_timeout = 120 * 1000;

	config.roam_kick_delay = 100;
	config.roam_scan_tries = 3;
	config.roam_scan_interval = 10 * 1000;
	config.roam_trigger_interval = 60 * 1000;

	config.load_kick_enabled = false;
	config.load_kick_threshold = 75;
	config.load_kick_delay = 10 * 1000;
	config.load_kick_min_clients = 10;
	config.load_kick_reason_code = 5; /* WLAN_REASON_DISASSOC_AP_BUSY */

	config.kick_client_active_sec = 30;
	config.kick_client_active_bits = 50000;

	config.beacon_report_invalide_timeout = 200;
	config.beacon_request_frequency = 30 * 1000;
	config.beacon_request_signal_modifier = 20 * 1000;

	config.debug_level = MSG_FATAL;

	config.remote_disabled = false;
}

void usteer_update_time(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	current_time = (uint64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [options]\n"
		"Options:\n"
		" -v:           Increase debug level (repeat for more messages):\n"
		"               1: info messages\n"
		"               2: debug messages\n"
		"               3: verbose debug messages\n"
		"               4: include network messages\n"
		"               5: include extra testing messages\n"
		" -i <name>:    Connect to other instances on interface <name>\n"
		" -s:		Output log messages via syslog instead of stderr\n"
		"\n", prog);
	return 1;
}

int main(int argc, char **argv)
{
	int ch;

	usteer_init_defaults();

	while ((ch = getopt(argc, argv, "i:sv")) != -1) {
		switch(ch) {
		case 'v':
			config.debug_level++;
			break;
		case 's':
			config.syslog = true;
			break;
		case 'i':
			usteer_interface_add(optarg);
			break;
		default:
			return usage(argv[0]);
		}
	}

	openlog("usteer", 0, LOG_USER);

	usteer_update_time();
	uloop_init();

	ubus_ctx = ubus_connect(NULL);
	if (!ubus_ctx) {
		fprintf(stderr, "Failed to connect to ubus\n");
		return -1;
	}

	ubus_add_uloop(ubus_ctx);
	usteer_ubus_init(ubus_ctx);
	usteer_interface_init();
	usteer_local_nodes_init(ubus_ctx);
	uloop_run();

	uloop_done();
	return 0;
}
