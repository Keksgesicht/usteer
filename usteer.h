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

#ifndef __APMGR_H
#define __APMGR_H

#include <libubox/avl.h>
#include <libubox/blobmsg.h>
#include <libubox/uloop.h>
#include <libubox/utils.h>
#include <libubus.h>
#include "utils.h"
#include "timeout.h"

#define NO_SIGNAL 0xff

#define __STR(x)		#x
#define _STR(x)			__STR(x)
#define APMGR_PORT		16720 /* AP */
#define APMGR_PORT_STR		_STR(APMGR_PORT)
#define APMGR_BUFLEN		(64 * 1024)

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

enum usteer_event_type {
	EVENT_TYPE_PROBE,
	EVENT_TYPE_ASSOC,
	EVENT_TYPE_AUTH,
	__EVENT_TYPE_MAX,
};

enum usteer_node_type {
	NODE_TYPE_LOCAL,
	NODE_TYPE_REMOTE,
};

struct sta_info;
struct usteer_local_node;
/**
 * A node struct comprising:
 * 1. AVL node (AVL tree from libubox/avl.h)
 * 2. list.h, sta information
 * 3. type of the usteer node, enum
 * 4. ?? blob msg attributes
 * 5. ?? blob msg some kind of script data
 * 6. wifi ssid
 * 7~ general wifi info: frequency, noise ..
 */
struct usteer_node {
    /**
    * This element is a member of a avl-tree. It must be contained in all
    * larger structs that should be put into a tree.
    */
	struct avl_node avl;

	struct list_head sta_info;

	enum usteer_node_type type;

	struct blob_attr *rrm_nr;
	struct blob_attr *script_data;
	char ssid[33];

	int freq;
	int noise;
	int n_assoc;
	int max_assoc;
	int load;
};

/**
 * request of a scan on frequency
 */
struct usteer_scan_request {
	int n_freq;
	int *freq;

	bool passive;
};

/**
 * result of a scan on frequency
 */
struct usteer_scan_result {
	uint8_t bssid[6];
	char ssid[33];

	int freq;
	int signal;
};

struct usteer_survey_data {
	uint16_t freq;
	int8_t noise;

	uint64_t time;
	uint64_t time_busy;
};

struct usteer_freq_data {
	uint16_t freq;

	uint8_t txpower;
	bool dfs;
};

/**
 * Pointer functions:
 * Initialize node
 * Free memory
 * Update node
 * ???
 * survey on a node, cb ???
 * get frequencies
 * scan
 */
struct usteer_node_handler {
	struct list_head list;

	void (*init_node)(struct usteer_node *);
	void (*free_node)(struct usteer_node *);
	void (*update_node)(struct usteer_node *);
	void (*update_sta)(struct usteer_node *, struct sta_info *);
	void (*get_survey)(struct usteer_node *, void *,
			           void (*cb)(void *priv, struct usteer_survey_data *d));
	void (*get_freqlist)(struct usteer_node *, void *,
	                     void (*cb)(void *priv, struct usteer_freq_data *f));
	int (*scan)(struct usteer_node *, struct usteer_scan_request *, void *,
	            void (*cb)(void *priv, struct usteer_scan_result *r));
};

struct usteer_config {
	bool syslog;
	uint32_t debug_level;

	uint32_t sta_block_timeout;
	uint32_t local_sta_timeout;
	uint32_t local_sta_update;

	uint32_t max_retry_band;
	uint32_t seen_policy_timeout;

	uint32_t band_steering_threshold;
	uint32_t load_balancing_threshold;

	uint32_t remote_update_interval;
	uint32_t remote_node_timeout;

	int32_t min_snr;
	int32_t min_connect_snr;
	uint32_t signal_diff_threshold;

	int32_t roam_scan_snr;
	uint32_t roam_scan_tries;
	uint32_t roam_scan_interval;

	int32_t roam_trigger_snr;
	uint32_t roam_trigger_interval;

	uint32_t roam_kick_delay;

	uint32_t initial_connect_delay;

	bool load_kick_enabled;
	uint32_t load_kick_threshold;
	uint32_t load_kick_delay;
	uint32_t load_kick_min_clients;
	uint32_t load_kick_reason_code;

	const char *node_up_script;
};

struct sta_info_stats {
	uint32_t requests;
	uint32_t blocked_cur;
	uint32_t blocked_total;
	uint32_t blocked_last_time;
};

#define __roam_trigger_states \
	_S(IDLE) \
	_S(SCAN) \
	_S(SCAN_DONE) \
	_S(WAIT_KICK) \
	_S(NOTIFY_KICK) \
	_S(KICK)

enum roam_trigger_state {
#define _S(n) ROAM_TRIGGER_##n,
	__roam_trigger_states
#undef _S
};
/**
 *
 */
struct sta_info {
	struct list_head list;
	struct list_head node_list;

	struct usteer_node *node;
	struct sta *sta;

	struct usteer_timeout timeout;

	struct sta_info_stats stats[__EVENT_TYPE_MAX];
	uint64_t created;
	uint64_t seen;
	int signal;

	enum roam_trigger_state roam_state;
	uint8_t roam_tries;
	uint64_t roam_event;
	uint64_t roam_kick;
	uint64_t roam_scan_done;

	int kick_count;

	uint8_t scan_band : 1;
	uint8_t connected : 2;
};

struct sta {
	struct avl_node avl;
	struct list_head nodes;

	uint8_t seen_2ghz : 1;
	uint8_t seen_5ghz : 1;

	uint8_t addr[6];
};
/**
 *
 */
extern struct ubus_context *ubus_ctx;
/**
 *
 */
extern struct usteer_config config;
/**
 *
 */
extern struct list_head node_handlers;
/**
 *
 */
extern struct avl_tree stations;
/**
 *
 */
extern uint64_t current_time;
/**
 *
 */
extern const char * const event_types[__EVENT_TYPE_MAX];
/**
 *
 */
void usteer_update_time(void);
/**
 *
 */
void usteer_init_defaults(void);
/**
 *
 * @param node
 * @param addr
 * @param type
 * @param freq
 * @param signal
 * @return
 */
bool usteer_handle_sta_event(struct usteer_node *node, const uint8_t *addr,
                             enum usteer_event_type type, int freq, int signal);
/**
 *
 * @param ctx
 */
void usteer_local_nodes_init(struct ubus_context *ctx);
/**
 *
 * @param ln
 */
void usteer_local_node_kick(struct usteer_local_node *ln);
/**
 *
 * @param ctx
 */
void usteer_ubus_init(struct ubus_context *ctx);
/**
 *
 * @param si
 */
void usteer_ubus_kick_client(struct sta_info *si);
/**
 *
 * @param si
 * @return
 */
int usteer_ubus_trigger_client_scan(struct sta_info *si);
/**
 *
 * @param si
 * @return
 */
int usteer_ubus_notify_client_disassoc(struct sta_info *si);
/**
 *
 * @param addr
 * @param create
 * @return
 */
struct sta *usteer_sta_get(const uint8_t *addr, bool create);
/**
 *
 * @param sta
 * @param node
 * @param create
 * @return
 */
struct sta_info *usteer_sta_info_get(struct sta *sta, struct usteer_node *node, bool *create);
/**
 *
 * @param si
 * @param timeout
 */
void usteer_sta_info_update_timeout(struct sta_info *si, int timeout);
/**
 *
 * @param si
 * @param signal
 * @param avg
 */
void usteer_sta_info_update(struct sta_info *si, int signal, bool avg);
/**
 *
 * @param node
 * @return
 */
static inline const char *usteer_node_name(struct usteer_node *node){
	return node->avl.key;
}
/**
 *
 * @param dest
 * @param val
 */
void usteer_node_set_blob(struct blob_attr **dest, struct blob_attr *val);
/**
 *
 * @param si
 * @param type
 * @return
 */
bool usteer_check_request(struct sta_info *si, enum usteer_event_type type);
/**
 *
 * @param data
 */
void config_set_interfaces(struct blob_attr *data);
/**
 *
 * @param buf
 */
void config_get_interfaces(struct blob_buf *buf);
/**
 *
 * @param data
 */
void config_set_node_up_script(struct blob_attr *data);
/**
 *
 * @param buf
 */
void config_get_node_up_script(struct blob_buf *buf);
/**
 *
 * @return
 */
int usteer_interface_init(void);
/**
 *
 * @param name
 */
void usteer_interface_add(const char *name);
/**
 *
 * @param node
 */
void usteer_sta_node_cleanup(struct usteer_node *node);
/**
 *
 * @param si
 */
void usteer_send_sta_update(struct sta_info *si);
/**
 *
 * @return
 */
int usteer_lua_init(void);
/**
 *
 * @return
 */
int usteer_lua_ubus_init(void);
/**
 *
 * @param name
 * @param arg
 */
void usteer_run_hook(const char *name, const char *arg);

#endif
