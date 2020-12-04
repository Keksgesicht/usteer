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

#include "usteer.h"

/**
 * Compares two mac address by comparing the memory.
 * 
 * @param k1 Pointer to the first MAC-address.
 * @param k2 Pointer to the second MAC-address.
 * @param ptr Parameter unused.
 * @return Returns true iff the two MAC-addresses match.
 */
static int avl_macaddr_cmp(const void *k1, const void *k2, void *ptr){
	return memcmp(k1, k2, 6);
}

AVL_TREE(stations, avl_macaddr_cmp, false, NULL);
static struct usteer_timeout_queue tq;

/**
 * Deletes the given station instance from the existing stations. Also
 * frees the memory of the deleted station.
 * 
 * @param sta The station to delete.
 */
static void usteer_sta_del(struct sta *sta){
	MSG(DEBUG, "Delete station " MAC_ADDR_FMT "\n",
	    MAC_ADDR_DATA(sta->addr));

	/* Remove station from avl-tree and free memory */
	avl_delete(&stations, &sta->avl);
	free(sta);
}

/**
 * Deletes the given station information. If the associated station
 * has no connected nodes, the station is also deleted.
 * 
 * @param si The station information to delete.
 */
static void usteer_sta_info_del(struct sta_info *si){
	struct sta *sta = si->sta;

	MSG(DEBUG, "Delete station " MAC_ADDR_FMT " entry for node %s\n",
	    MAC_ADDR_DATA(sta->addr), usteer_node_name(si->node));

	usteer_timeout_cancel(&tq, &si->timeout);
	list_del(&si->list);
	list_del(&si->node_list);
	free(si);

	if (list_empty(&sta->nodes))
		usteer_sta_del(sta);
}

/**
 *
 * @param node
 */
void usteer_sta_node_cleanup(struct usteer_node *node){
	struct sta_info *si, *tmp;

	free(node->rrm_nr);
	node->rrm_nr = NULL;

	list_for_each_entry_safe(si, tmp, &node->sta_info, node_list)
		usteer_sta_info_del(si);
}

/**
 *
 * @param q
 * @param t
 */
static void usteer_sta_info_timeout(struct usteer_timeout_queue *q, struct usteer_timeout *t){
	struct sta_info *si = container_of(t, struct sta_info, timeout);

	MSG_T_STA("local_sta_timeout", si->sta->addr,
		"timeout expired, deleting sta info\n");

	usteer_sta_info_del(si);
}

/**
 * Searches if the nodes contained in the 'sta' parameter instance contain the given
 * usteer_node 'node'. If the node is found, the node is returned. If not and the 'create'
 * parameter is NULL, the function returns NULL.
 * 
 * If the parameter is not NULL, a new 'sta_info' instance is created for the usteer_node.
 * This instance is added to the existing sta->nodes.
 * 
 * Sets the target of the parameter 'create' to true if a new sta_info instance is created, and
 * to false if an existing node is returned.
 * 
 * @param sta The station to search the usteer_node instance in.
 * @param node The node that is searched.
 * @param create Used to signal that a new node should be created if the sta_info instance does 
 * 		not exist. Also useed as return status code, set to true if a new instance was created,
 * 		set to false if an existing instance was returned.
 * @return NULL if the paramter 'create' is NULL and no instance exists, or the newly created or 
 * existing sta_info instance.
 */
struct sta_info * usteer_sta_info_get(struct sta *sta, struct usteer_node *node, bool *create){
	struct sta_info *si;

	/**
	 * See 'list.h', iterates over all sta nodes.
	 * Checks if the node of the current value of 'si' is
	 * equal to the given usteer_node, and if yes, returns the existing
	 * sta_info instance. Also sets the target of 'create' to false.
	 */
	list_for_each_entry(si, &sta->nodes, list) {
		if (si->node != node)
			continue;

		/* Signal that no new node was created, but an existing node was returned */
		if (create)
			*create = false;

		/* Found existing node, return found node */
		return si;
	}

	/* Existing instance was not found, new creation is not wanted, return null */
	if (!create)
		return NULL;

	/* Log that a new instance is created */
	MSG(DEBUG, "Create station " MAC_ADDR_FMT " entry for node %s\n",
	    MAC_ADDR_DATA(sta->addr), usteer_node_name(node));

	/* Create a new sta_info instance. */
	si = calloc(1, sizeof(*si));
	si->node = node;
	si->sta = sta;
	list_add(&si->list, &sta->nodes);
	list_add(&si->node_list, &node->sta_info);

	/* Set the creation time of the sta_info instance to the current time. */
	si->created = current_time;

	/* Signal that a new instance was created */
	*create = true;

	return si;
}

/**
 * Updates the timeout for the given station info instance. If the station is
 * connected, the timeout is canceled by calling 'usteer_timeout_cancel'. If 
 * the given timeout parameter is greater than 0, the 'usteer_timeout_set' function
 * is called.
 * 
 * In any other case, the given station info instance is deleted.
 * 
 * @param si The station info where a timeout update should be performed.
 * @param timeout 
 */
void usteer_sta_info_update_timeout(struct sta_info *si, int timeout){
	if (si->connected == 1)
		usteer_timeout_cancel(&tq, &si->timeout);
	else if (timeout > 0)
		usteer_timeout_set(&tq, &si->timeout, timeout);
	else
		usteer_sta_info_del(si);
}

/**
 * Attempts to find a station with the given MAC-Address in the existing stations.
 * If the station is found, the instance is returned. If not and the 'create' paramter
 * is set to false, the function returns NULL. If the parameter is set to true, a new 
 * station with the given address is created and added to the existing stations. This
 * new instance is then returned.
 * 
 * @param addr The MAC-Address of the searched station.
 * @param create Wether to create a new station if the station does not exist.
 * @return The found or created station instance, or NULL if no station exists and 'create' is false.
 */
struct sta * usteer_sta_get(const uint8_t *addr, bool create){
	struct sta *sta;

	/* Attempt to find the station with the given address. */
	sta = avl_find_element(&stations, addr, sta, avl);
	if (sta)
		return sta;

	/* Station does not exist and should not be created, return null. */
	if (!create)
		return NULL;

	/* Log that a new station is created. */
	MSG(DEBUG, "Create station entry " MAC_ADDR_FMT "\n", MAC_ADDR_DATA(addr));

	/* Initialize a new station instance. */
	sta = calloc(1, sizeof(*sta));
	memcpy(sta->addr, addr, sizeof(sta->addr));
	sta->avl.key = sta->addr;

	/* Add the station to the existing stations */
	avl_insert(&stations, &sta->avl);

	INIT_LIST_HEAD(&sta->nodes);

	return sta;
}

/**
 * Updates the given station information instance. If the station has
 * a signal, the station signal is updated, to either the given parameter 'signal',
 * or to NO_SIGNAL if 'avg' is false and the station is connected and has a signal.
 * 
 * Also, the 'seen' field is updated to the current time. Also updates the station
 * information timeout.
 * 
 * @param si The station information intance to update.
 * @param signal ?
 * @param avg ?
 */
void usteer_sta_info_update(struct sta_info *si, int signal, bool avg){
	/* ignore probe request signal when connected */
	if (si->connected == 1 && si->signal != NO_SIGNAL && !avg)
		signal = NO_SIGNAL;

	if (signal != NO_SIGNAL)
		si->signal = signal;

	si->seen = current_time;
	usteer_sta_info_update_timeout(si, config.local_sta_timeout);
}

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
                             enum usteer_event_type type, int freq, int signal){
	struct sta *sta;
	struct sta_info *si;
	uint32_t diff;
	bool ret;
	bool create;

	sta = usteer_sta_get(addr, true);
	if (!sta)
		return -1;

	/* Based on the frequency, update the station that this frequency has been seen from the station. */
	if (freq < 4000)
		sta->seen_2ghz = 1;
	else
		sta->seen_5ghz = 1;

	si = usteer_sta_info_get(sta, node, &create);
	usteer_sta_info_update(si, signal, false);
	si->roam_scan_done = current_time;
	si->stats[type].requests++;

	diff = si->stats[type].blocked_last_time - current_time;
	if (diff > config.sta_block_timeout) {
		si->stats[type].blocked_cur = 0;
		MSG_T_STA("sta_block_timeout", addr, "timeout expired\n");
	}

	ret = usteer_check_request(si, type);
	if (!ret) {
		si->stats[type].blocked_cur++;
		si->stats[type].blocked_total++;
		si->stats[type].blocked_last_time = current_time;
	} else {
		si->stats[type].blocked_cur = 0;
	}

	if (create)
		usteer_send_sta_update(si);

	return ret;
}

/**
 *
 */
static void __usteer_init usteer_sta_init(void){
	usteer_timeout_init(&tq);
	tq.cb = usteer_sta_info_timeout;
}
