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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>

#include <libubox/vlist.h>
#include <libubox/avl-cmp.h>
#include <libubox/usock.h>
#include "usteer.h"
#include "remote.h"
#include "node.h"

static uint32_t local_id;
static struct uloop_fd remote_fd;
static struct uloop_timeout remote_timer;
static struct uloop_timeout reload_timer;

static struct blob_buf buf;
static uint32_t msg_seq;

struct interface {
	struct vlist_node node;
	int ifindex;
};

static void
interfaces_update_cb(struct vlist_tree *tree,
		     struct vlist_node *node_new,
		     struct vlist_node *node_old);

static int remote_node_cmp(const void *k1, const void *k2, void *ptr)
{
	unsigned long v1 = (unsigned long) k1;
	unsigned long v2 = (unsigned long) k2;

	return v2 - v1;
}

static VLIST_TREE(interfaces, avl_strcmp, interfaces_update_cb, true, true);
AVL_TREE(remote_nodes, remote_node_cmp, true, NULL);

static const char *
interface_name(struct interface *iface)
{
	return iface->node.avl.key;
}

static void
interface_check(struct interface *iface)
{
	iface->ifindex = if_nametoindex(interface_name(iface));
	uloop_timeout_set(&reload_timer, 1);
}

static void
interface_init(struct interface *iface)
{
	interface_check(iface);
}

static void
interface_free(struct interface *iface)
{
	avl_delete(&interfaces.avl, &iface->node.avl);
	free(iface);
}

static void
interfaces_update_cb(struct vlist_tree *tree,
		     struct vlist_node *node_new,
		     struct vlist_node *node_old)
{
	struct interface *iface;

	if (node_new && node_old) {
		iface = container_of(node_new, struct interface, node);
		free(iface);
		iface = container_of(node_old, struct interface, node);
		interface_check(iface);
	} else if (node_old) {
		iface = container_of(node_old, struct interface, node);
		interface_free(iface);
	} else {
		iface = container_of(node_new, struct interface, node);
		interface_init(iface);
	}
}

void usteer_interface_add(const char *name)
{
	struct interface *iface;
	char *name_buf;

	iface = calloc_a(sizeof(*iface), &name_buf, strlen(name) + 1);
	strcpy(name_buf, name);
	vlist_add(&interfaces, &iface->node, name_buf);
}

void config_set_interfaces(struct blob_attr *data)
{
	struct blob_attr *cur;
	int rem;

	if (!blobmsg_check_attr_list(data, BLOBMSG_TYPE_STRING))
		return;

	vlist_update(&interfaces);
	blobmsg_for_each_attr(cur, data, rem) {
		usteer_interface_add(blobmsg_data(cur));
	}
	vlist_flush(&interfaces);
}

void config_get_interfaces(struct blob_buf *buf)
{
	struct interface *iface;
	void *c;

	c = blobmsg_open_array(buf, "interfaces");
	vlist_for_each_element(&interfaces, iface, node) {
		blobmsg_add_string(buf, NULL, interface_name(iface));
	}
	blobmsg_close_array(buf, c);
}

static void
interface_add_station(struct usteer_remote_node *node, struct blob_attr *data)
{
	struct sta *sta;
	struct sta_info *si;
	struct apmsg_sta msg;
	bool create;

	if (!parse_apmsg_sta(&msg, data)) {
		MSG(DEBUG, "Cannot parse station in message\n");
		return;
	}

	if (msg.timeout <= 0) {
		MSG(DEBUG, "Refuse to add an already expired station entry\n");
		return;
	}

	sta = usteer_sta_get(msg.addr, true);
	if (!sta)
		return;

	si = usteer_sta_info_get(sta, &node->node, &create);
	if (!si)
		return;

	si->connected = msg.connected;
	si->signal = msg.signal;
	si->seen = current_time - msg.seen;
	usteer_sta_info_update_timeout(si, msg.timeout);
}

static void
remote_node_free(struct usteer_remote_node *node)
{
	avl_delete(&beacon_nodes, &node->node.beacon);
	avl_delete(&remote_nodes, &node->avl);
	usteer_sta_node_cleanup(&node->node);
	free(node);
}

static struct usteer_remote_node *
interface_get_node(const char *addr, unsigned long id, const char *name)
{
	struct usteer_remote_node *node;
	int addr_len = strlen(addr);
	char *buf;

	node = avl_find_element(&remote_nodes, (void *) id, node, avl);
	while (node && node->avl.key == (void *) id) {
		if (!strcmp(node->name, name))
			return node;

		node = avl_next_element(node, avl);
	}

	node = calloc_a(sizeof(*node), &buf, addr_len + 1 + strlen(name) + 1);
	node->avl.key = (void *) id;
	node->node.type = NODE_TYPE_REMOTE;

	sprintf(buf, "%s#%s", addr, name);
	node->node.avl.key = buf;
	node->name = buf + addr_len + 1;
	INIT_LIST_HEAD(&node->node.sta_info);

	avl_insert(&remote_nodes, &node->avl);

	return node;
}

static void
interface_add_node(struct interface *iface, const char *addr, unsigned long id, struct blob_attr *data)
{
	struct usteer_remote_node *node;
	struct apmsg_node msg;
	struct blob_attr *cur;
	int rem;

	if (!parse_apmsg_node(&msg, data)) {
		MSG(DEBUG, "Cannot parse node in message\n");
		return;
	}

	node = interface_get_node(addr, id, msg.name);
	node->check = 0;
	node->node.freq = msg.freq;
	node->node.n_assoc = msg.n_assoc;
	node->node.max_assoc = msg.max_assoc;
	node->node.noise = msg.noise;
	node->node.load = msg.load;
	node->iface = iface;
	snprintf(node->node.ssid, sizeof(node->node.ssid), "%s", msg.ssid);
	usteer_node_set_blob(&node->node.rrm_nr, msg.rrm_nr);
	usteer_node_set_blob(&node->node.script_data, msg.script_data);

	snprintf(node->node.mac, sizeof(node->node.mac), "%s", msg.mac);
	node->node.beacon.key = &node->node.mac;
	avl_insert(&beacon_nodes, &node->node.beacon);

	blob_for_each_attr(cur, msg.stations, rem)
		interface_add_station(node, cur);
}

static void
interface_recv_msg(struct interface *iface, struct in6_addr *addr, void *buf, int len)
{
	char addr_str[INET6_ADDRSTRLEN];
	struct blob_attr *data = buf;
	struct apmsg msg;
	struct blob_attr *cur;
	int rem;

	if (blob_pad_len(data) != len) {
		MSG(DEBUG, "Invalid message length (header: %d, real: %d)\n", blob_pad_len(data), len);
		return;
	}

	if (!parse_apmsg(&msg, data)) {
		MSG(DEBUG, "Missing fields in message\n");
		return;
	}

	if (msg.id == local_id)
		return;

	MSG(NETWORK, "Received message on %s (id=%08x->%08x seq=%d len=%d)\n",
		interface_name(iface), msg.id, local_id, msg.seq, len);

	inet_ntop(AF_INET6, addr, addr_str, sizeof(addr_str));

	blob_for_each_attr(cur, msg.nodes, rem)
		interface_add_node(iface, addr_str, msg.id, cur);
}

static struct interface *
interface_find_by_ifindex(int index)
{
	struct interface *iface;

	vlist_for_each_element(&interfaces, iface, node) {
		if (iface->ifindex == index)
			return iface;
	}

	return NULL;
}

static void
interface_recv(struct uloop_fd *u, unsigned int events)
{
	static char buf[APMGR_BUFLEN];
	static char cmsg_buf[( CMSG_SPACE(sizeof(struct in6_pktinfo)) + sizeof(int)) + 1];
	static struct sockaddr_in6 sin;
	static struct iovec iov = {
		.iov_base = buf,
		.iov_len = sizeof(buf)
	};
	static struct msghdr msg = {
		.msg_name = &sin,
		.msg_namelen = sizeof(sin),
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = cmsg_buf,
		.msg_controllen = sizeof(cmsg_buf),
	};
	int len;
	if(!config.remote_disabled){
		do {
			struct interface *iface;

			len = recvmsg(u->fd, &msg, 0);
			if (len < 0) {
				switch (errno) {
				case EAGAIN:
					return;
				case EINTR:
					continue;
				default:
					perror("recvmsg");
					uloop_fd_delete(u);
					return;
				}
			}

			iface = interface_find_by_ifindex(sin.sin6_scope_id);
			if (!iface) {
				MSG(DEBUG, "Received packet from unconfigured interface %d\n", sin.sin6_scope_id);
				continue;
			}

			interface_recv_msg(iface, &sin.sin6_addr, buf, len);
		} while (1);
	}
}

static void
interface_send_msg(struct interface *iface, struct blob_attr *data)
{
	static size_t cmsg_data[( CMSG_SPACE(sizeof(struct in6_pktinfo)) / sizeof(size_t)) + 1];
	static struct sockaddr_in6 a;
	static struct iovec iov;
	static struct msghdr m = {
		.msg_name = (struct sockaddr6 *) &a,
		.msg_namelen = sizeof(a),
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = cmsg_data,
		.msg_controllen = CMSG_LEN(sizeof(struct in6_pktinfo)),
	};
	struct cmsghdr *cmsg;

	a.sin6_family = AF_INET6;
	a.sin6_port = htons(APMGR_PORT);
	inet_pton(AF_INET6, "ff02::2", &a.sin6_addr);

	memset(cmsg_data, 0, sizeof(cmsg_data));
	cmsg = CMSG_FIRSTHDR(&m);
	cmsg->cmsg_len = m.msg_controllen;
	cmsg->cmsg_level = IPPROTO_IPV6;
	cmsg->cmsg_type = IPV6_PKTINFO;

	a.sin6_scope_id = iface->ifindex;

	iov.iov_base = data;
	iov.iov_len = blob_pad_len(data);
	if(!config.remote_disabled){
		if (sendmsg(remote_fd.fd, &m, 0) < 0)
			perror("sendmsg");
	}
}

static void usteer_send_sta_info(struct sta_info *sta)
{
	int seen = current_time - sta->seen;
	void *c;

	c = blob_nest_start(&buf, 0);
	blob_put(&buf, APMSG_STA_ADDR, sta->sta->addr, 6);
	blob_put_int8(&buf, APMSG_STA_CONNECTED, !!sta->connected);
	blob_put_int32(&buf, APMSG_STA_SIGNAL, sta->signal);
	blob_put_int32(&buf, APMSG_STA_SEEN, seen);
	blob_put_int32(&buf, APMSG_STA_TIMEOUT, config.local_sta_timeout - seen);
	blob_nest_end(&buf, c);
}

static void usteer_send_node(struct usteer_node *node, struct sta_info *sta)
{
	void *c, *s, *r;

	c = blob_nest_start(&buf, 0);

	blob_put_string(&buf, APMSG_NODE_NAME, usteer_node_name(node));
	blob_put_string(&buf, APMSG_NODE_SSID, node->ssid);
	blob_put_string(&buf, APMSG_NODE_MAC, node->mac);
	blob_put_int32(&buf, APMSG_NODE_FREQ, node->freq);
	blob_put_int32(&buf, APMSG_NODE_NOISE, node->noise);
	blob_put_int32(&buf, APMSG_NODE_LOAD, node->load);
	blob_put_int32(&buf, APMSG_NODE_N_ASSOC, node->n_assoc);
	blob_put_int32(&buf, APMSG_NODE_MAX_ASSOC, node->max_assoc);
	if (node->rrm_nr) {
		r = blob_nest_start(&buf, APMSG_NODE_RRM_NR);
		blobmsg_add_field(&buf, BLOBMSG_TYPE_ARRAY, "",
				  blobmsg_data(node->rrm_nr),
				  blobmsg_data_len(node->rrm_nr));
		blob_nest_end(&buf, r);
	}

	if (node->script_data)
		blob_put(&buf, APMSG_NODE_SCRIPT_DATA,
			 blob_data(node->script_data),
			 blob_len(node->script_data));

	s = blob_nest_start(&buf, APMSG_NODE_STATIONS);
	if(!config.remote_disabled){
		if (sta) {
			usteer_send_sta_info(sta);
		} else {
			list_for_each_entry(sta, &node->sta_info, node_list)
				usteer_send_sta_info(sta);
		}
	}

	blob_nest_end(&buf, s);

	blob_nest_end(&buf, c);
}

static void
usteer_check_timeout(void)
{
	struct usteer_remote_node *node, *tmp;
	int timeout = config.remote_node_timeout / config.remote_update_interval;

	avl_for_each_element_safe(&remote_nodes, node, avl, tmp) {
		if (node->check++ > timeout)
			remote_node_free(node);
	}
}

static void *
usteer_update_init(void)
{
	blob_buf_init(&buf, 0);
	blob_put_int32(&buf, APMSG_ID, local_id);
	blob_put_int32(&buf, APMSG_SEQ, ++msg_seq);

	return blob_nest_start(&buf, APMSG_NODES);
}

static void
usteer_update_send(void *c)
{
	struct interface *iface;

	blob_nest_end(&buf, c);

	vlist_for_each_element(&interfaces, iface, node)
		interface_send_msg(iface, buf.head);
}

void
usteer_send_sta_update(struct sta_info *si)
{
	void *c = usteer_update_init();
	usteer_send_node(si->node, si);
	usteer_update_send(c);
}

static void
usteer_send_update_timer(struct uloop_timeout *t)
{
	struct usteer_node *node;
	void *c;

	MSG_T("remote_update_interval", "start remote update (interval=%u)\n",
		config.remote_update_interval);

	usteer_update_time();
	uloop_timeout_set(t, config.remote_update_interval);

	c = usteer_update_init();
	avl_for_each_element(&local_nodes, node, avl)
		usteer_send_node(node, NULL);

	usteer_update_send(c);
	usteer_check_timeout();
}

static int
usteer_init_local_id(void)
{
	FILE *f;

	f = fopen("/dev/urandom", "r");
	if (!f) {
		perror("fopen(/dev/urandom)");
		return -1;
	}

	if (fread(&local_id, sizeof(local_id), 1, f) < 1)
		return -1;

	fclose(f);
	return 0;
}

static void
usteer_reload_timer(struct uloop_timeout *t)
{
	struct sockaddr_in6 address = {AF_INET6, htons(APMGR_PORT)};
	struct ipv6_mreq group;
	int fd;

	if (remote_fd.registered) {
		uloop_fd_delete(&remote_fd);
		close(remote_fd.fd);
	}

	if((fd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		perror("socket");
		return;
	}
	if (bind(fd, (struct sockaddr *) &address, sizeof address) < 0) {
		perror("bind");
		return;
	}

	group.ipv6mr_interface = 0;
	inet_pton(AF_INET6, "ff02::2", &group.ipv6mr_multiaddr);
	if (setsockopt(fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &group, sizeof(group)) < 0) {
		perror("setsockopt(IPV6_ADD_MEMBERSHIP)");
	}

	remote_fd.fd = fd;
	remote_fd.cb = interface_recv;
	uloop_fd_add(&remote_fd, ULOOP_READ);
}

int usteer_interface_init(void)
{
	if (usteer_init_local_id())
		return -1;

	remote_timer.cb = usteer_send_update_timer;
	remote_timer.cb(&remote_timer);

	reload_timer.cb = usteer_reload_timer;
	reload_timer.cb(&reload_timer);

	return 0;
}
