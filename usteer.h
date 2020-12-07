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

/**
 * event types
 * probe
 * association
 * authentication
 * number of events
 */
enum usteer_event_type {
	EVENT_TYPE_PROBE,
	EVENT_TYPE_ASSOC,
	EVENT_TYPE_AUTH,
	__EVENT_TYPE_MAX,
};

/**
 * type of usteer node
 * local node
 * remote node
 */
enum usteer_node_type {
	NODE_TYPE_LOCAL,
	NODE_TYPE_REMOTE,
};

enum roam_trigger_state {
	ROAM_TRIGGER_IDLE, 
	ROAM_TRIGGER_SCAN, 
	ROAM_TRIGGER_SCAN_DONE, 
	ROAM_TRIGGER_WAIT_KICK, 
	ROAM_TRIGGER_NOTIFY_KICK, 
	ROAM_TRIGGER_KICK
};

struct sta_info;
struct usteer_local_node;

/**
 * A node struct comprising:
 * 1. AVL node (AVL tree from libubox/avl.h)
 * 2. list.h, sta information
 * 3. type of the usteer node, enum
 * 4. neighbors
 * 5. script data
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

	/**
	 * The amount of clients that are associated/connected to this node.
	 */
	int n_assoc;
	
	/**
	 * The maximum number of clients that can be connected to this node.
	 */
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
 * Initialize node:
 *      (for request see netifd.c, calls update)
 * Free node:
 *      (for request see netifd.c, cancel request)
 *      (in local_node.c free memory)
 * Update node:
 *      (for request see netifd.c)
 *
 * update station
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

/**
 * The configuration of the usteer instance. Upon startup, a config
 * is created and initialized to be all 0.
 */
struct usteer_config {

			/* -----------< logging >-----------*/
	/**
	 * When set to true, usteer will output the log messages to the syslog
	 * insted of the default, stderr.
	 */
	bool syslog;

	/**
	 * The debug level determines which log messages are generated. The higher
	 * the debug level, the more messages are generated. See function 'usage' in main.c
	 * for the possible debug levels and their included messages.
	 */
	uint32_t debug_level;


			/* -----------< sta >-----------*/
	/**
	 * Timeout after a station timeout is resetted.
	 * 
	 * Default value: 30k
	 */
	uint32_t sta_block_timeout;

	/**
	 * Timeout after connect for response
	 * 
	 * Default value: 120k (120 sec)
	 */
	uint32_t local_sta_timeout;

	/**
	 * Use: ?
	 * 
	 * Default value: 1k
	 */
	uint32_t local_sta_update;

	/**
	 * Max amount of retries before an event or request from a station is treated with
	 * urgency.
	 * 
	 * Default value: 5
	 */
	uint32_t max_retry_band;

	/**
	 * This value determines the size of the time interval after a station will not be considered
	 * a better candidate. When checking for a better candidate, a time delta between the current time
	 * and the 'seen' value is compared. If the value is greater than this value, the station will
	 * not be considered a better candidate at all.
	 *
	 * Default value: 30k
	 */
	uint32_t seen_policy_timeout;

	/**
	 * This threshold is used to calculate a metric between a current and new station.
	 * If the current station operates on 5GHz, but the new station does not, this value
	 * is added on the side of the new station. If the current station operates on 2.4GHz, the
	 * value is added for the current station.
	 * 
	 * At the end of the day, this value represents a penalty that is taken into consideration
	 * which station of the two is better. The higher this value, the higher the penalty if a 
	 * station operates on a lower frequency.
	 * 
	 * Default value: 5
	 */
	uint32_t band_steering_threshold;

	/**
	 * Similarily like 'band_steering_threshold', this value is a penalty that most probably
	 * models if it is viable to roam a client to another station by taking the generated overhead
	 * and traffic generated into consideration. The higher this value is, the higher the penalty
	 * is when determening if another station is better for a client.
	 * 
	 * Default value: 5
	 */
	uint32_t load_balancing_threshold;

	/**
	 * How frequently usteer updates remote information.
	 * 
	 * Default value: 1k
	 */
	uint32_t remote_update_interval;

	/**
	 * Time until usteer consideres a remote node as timeouted and removes it
	 * from it's known nodes.
	 * 
	 * Default value: 120k
	 */
	uint32_t remote_node_timeout;

	/**
	 * Signal-noise-ratio. Currently not used.
	 * 
	 * This value is used as a threshold that determines at what signal noise ratio a local node
	 * is kicked from a station.
	 * 
	 * Default value: 0
	 */
	int32_t min_snr;

	/**
	 * Minimum signal-to-noise ratio so that a client request is accepted.
	 * 
	 * Default value: 0
	 */
	int32_t min_connect_snr;

	/**
	 * Threshold how much better the signal strength of a new node has to be so
	 * usteer determines it as a better signal. A signal is better if:
	 * 
	 * 		new_signal - current_signal > signal_diff_threshold
	 * 
	 * Default value: 0
	 */
	uint32_t signal_diff_threshold;

	
			/* -----------< roaming >-----------*/
	/**
	 * Use: ?
	 * 
	 * Default value: 0
	 */
	int32_t roam_scan_snr;

	/**
	 * The amount of attempts a station should take to attempt to roam before kicking.
	 * 
	 * Default value: 3
	 */
	uint32_t roam_scan_tries;

	/**
	 * This value defines the frequency usteer scans for roaming possibilities.
	 * 
	 * Default value: 10k
	 */
	uint32_t roam_scan_interval;
	
	/**
	 * Use: ?
	 * 
	 * Default value: 0
	 */
	int32_t roam_trigger_snr;

	/**
	 * This value defines the frequency usteer attempts to roam clients.
	 * 
	 * Default value: 60k
	 */
	uint32_t roam_trigger_interval;

	/**
	 * Delay before a client is kicked if the client fails to roam in time.
	 * 
	 * Default value: 100
	 */
	uint32_t roam_kick_delay;


	/**
	 * The time in milliseconds usteer ignores requestes from a station after
	 * it was created. If:
	 * 
	 * 		si->created + initial_connect_delay > current_time
	 * 
	 * requests are ignored. Default value: 0
	 */
	uint32_t initial_connect_delay;


			/* -----------< load-kicking >-----------*/
	/**
	 * When enabled, nodes whose load value exceeds the 'load_kick_threshold'
	 * will be automatically kicked. Default value: false
	 */
	bool load_kick_enabled;

	/**
	 * The threshold a node has to exceed in order to be load-kicked.
	 * Default value: 75
	 */
	uint32_t load_kick_threshold;

	/**
	 * Delay before a client is load-kicked.
	 * 
	 * Default value: 10k
	 */
	uint32_t load_kick_delay;
	
	/**
	 * When load-kicking is enabled, this property determines at which
	 * point a node stops to load-kick clients based on the amount of
	 * connected clients. If the number of connected clients is less than this
	 * property, no clients will be kicked even if they are over the load-threshold.
	 * 
	 * Default value: 10
	 */
	uint32_t load_kick_min_clients;

	/**
	 * The reason why a client was load-kicked. Default value: 5 (WLAN_REASON_DISASSOC_AP_BUSY)
	 */
	uint32_t load_kick_reason_code;


	/**
	 * Use: ?
	 * 
	 * Default value: 0
	 */
	const char *node_up_script;
};

struct sta_info_stats {
	uint32_t requests;
	uint32_t blocked_cur;
	uint32_t blocked_total;
	uint32_t blocked_last_time;
};

/**
 * Holds state and information for a specific station. A station is a connected client
 * or router/access point.
 */
struct sta_info {
	struct list_head list;
	struct list_head node_list;

	struct usteer_node *node;

	/**
	 * The station that corresponds to this station information instance.
	 */
	struct sta *sta;

	struct usteer_timeout timeout;

	struct sta_info_stats stats[__EVENT_TYPE_MAX];

	/**
	 * Set to the time when this station was created or instantiated. The time
	 * is stored as a UNIX-timestamp.
	 */
	uint64_t created;

	/**
	 * Set to the current time as UNIX-Timestamp when an sta_info_update is 
	 * performed on this sta_info instance.
	 */
	uint64_t seen;

	int signal;

	enum roam_trigger_state roam_state;
	uint8_t roam_tries;
	uint64_t roam_event;

	/**
	 * Set to the time when this station/client was kicked as a UNIX-timestamp.
	 */
	uint64_t roam_kick;

	/**
	 * Set to the current time after a roam scan was completed. 
	 */
	uint64_t roam_scan_done;

	/**
	 * This counter keeps track of how many clients this station has kicked.
	 */
	int kick_count;

	/**
	 * Determines the frequency this station operates on.
	 * 
	 * 		0 = 2.4 GHz
	 * 		1 = 5   GHz
	 */
	uint8_t scan_band : 1;

	/**
	 * Defines wether this client is connected or not.
	 * 
	 * 		0 = Not Connected
	 * 		1 = Connected														Note: Not 100% sure
	 * 		2 = Connected to this station										Note: Not 100% sure
	 */
	uint8_t connected : 2;
};

/**
 * Describes a specific station.
 */
struct sta {
	struct avl_node avl;
	struct list_head nodes;

	/**
	 * Set to 1 when an sta_event occurs with a frequency 
	 * below 4000.
	 */
	uint8_t seen_2ghz : 1;
	
	/**
	 * Set to 1 when an sta_event occurs with a frequency greater
	 * or equal to 4000. See 'seen_2ghz' in 'sta'.
	 */
	uint8_t seen_5ghz : 1;

	/**
	 * The MAC address of this station.
	 */
	uint8_t addr[6];
};

		/* -----------< See 'main.c'. >-----------*/
extern struct ubus_context *ubus_ctx;
extern struct usteer_config config;
extern struct list_head node_handlers;
extern uint64_t current_time;
extern const char * const event_types[__EVENT_TYPE_MAX];

/**
 * A tree containing all currently registered stations.
 */
extern struct avl_tree stations;

		/* -----------< See 'main.c'. >-----------*/
void usteer_update_time(void);
void usteer_init_defaults(void);

		/* -----------< See 'sta.c'. >-----------*/
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
