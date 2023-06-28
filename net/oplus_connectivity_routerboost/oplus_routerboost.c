/************************************************************************************
** File: - oplus_routerboost.c
** Copyright (C), 2008-2020, OPLUS Mobile Comm Corp., Ltd
**
** Description:
**     1. Add for RouterBoost GKI
**
** Version: 1.0
** Date :   2021-04-19
** TAG:     OPLUS_FEATURE_WIFI_ROUTERBOOST
**
** ---------------------Revision History: ---------------------
** <author>                           <data>      <version>    <desc>
** ---------------------------------------------------------------
************************************************************************************/
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/sysctl.h>
#include <net/route.h>
#include <net/ip.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/version.h>
#include <net/tcp.h>
#include <linux/random.h>
#include <net/sock.h>
#include <net/dst.h>
#include <linux/file.h>
#include <net/tcp_states.h>
#include <linux/workqueue.h>
#include <linux/netlink.h>
#include <net/sch_generic.h>
#include <net/pkt_sched.h>
#include <net/netfilter/nf_queue.h>
#include <linux/netfilter/xt_state.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_owner.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/ipv4/nf_conntrack_ipv4.h>
#include <net/genetlink.h>
#include "oplus_routerboost.h"

int routerboost_debug = 0;
static int routerboost_netlink_pid = 0;

static struct ctl_table_header *routerboost_sysctl_table_header = NULL;

/* ---- sysctl process func start ---- */
static struct ctl_table routerboost_sysctl_table[] = {
	{
		.procname	= "routerboost_debug",
		.data		= &routerboost_debug,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{ }
};

static int routerboost_sysctl_register(void)
{
	routerboost_sysctl_table_header = register_net_sysctl(&init_net,
					  "net/oplus_routerboost", routerboost_sysctl_table);
	return routerboost_sysctl_table_header == NULL ? -ENOMEM : 0;
}

static void routerboost_sysctl_unregister(void)
{
	if (routerboost_sysctl_table_header != NULL) {
		unregister_net_sysctl_table(routerboost_sysctl_table_header);
	}
}
/* ---- sysctl process func end ---- */

/* ---- genl process func start ---- */
static int handle_nlmsg_set_android_pid(struct sk_buff *skb)
{
	struct nlmsghdr *nlhdr = nlmsg_hdr(skb);
	routerboost_netlink_pid = nlhdr->nlmsg_pid;
	return 0;
}

static int handle_nlmsg_set_kernel_debug(struct nlattr *nla)
{
	routerboost_debug = *(u32 *)NLA_DATA(nla);
	debug("routerboost_debug = %d\n", routerboost_debug);
	return 0;
}

static int handle_nlmsg_set_game_uid(struct nlattr *nla)
{
	u32 uid = *(u32 *)NLA_DATA(nla);
	set_game_monitor_uid(uid);/*defined in oplus_routerboost_game_monitor.c*/
	return 0;
}

static int handle_nlmsg_set_wlan_index(struct nlattr *nla)
{
	u32 monitor_wlan_index = *(u32 *)NLA_DATA(nla);
	set_game_monitor_wlan_index(monitor_wlan_index);/*defined in oplus_routerboost_game_monitor.c*/
	return 0;
}

static int handle_nlmsg_set_game_playing_state(struct nlattr *nla)
{
	u32 is_playing = *(u32 *)NLA_DATA(nla);
	set_game_playing_state(is_playing);/*defined in oplus_routerboost_game_monitor.c*/
	return 0;
}

static int routerboost_genl_nlmsg_handle(struct sk_buff *skb,
		struct genl_info *info)
{
	int ret = 0;
	struct nlmsghdr *nlhdr;
	struct genlmsghdr *genlhdr;
	struct nlattr *nla;

	nlhdr = nlmsg_hdr(skb);
	genlhdr = nlmsg_data(nlhdr);
	nla = genlmsg_data(genlhdr);

	if (routerboost_debug) {
		debug("routerboost_genl_nlmsg_handle, the nla->nla_type = %u, len = %u\n",
		      nla->nla_type, nla->nla_len);
	}

	switch (nla->nla_type) {
	case ROUTERBOOST_MSG_SET_ANDROID_PID:
		ret = handle_nlmsg_set_android_pid(skb);
		break;

	case ROUTERBOOST_MSG_SET_KERNEL_DEBUG:
		ret = handle_nlmsg_set_kernel_debug(nla);
		break;

	case ROUTERBOOST_MSG_SET_GAME_UID:
		ret = handle_nlmsg_set_game_uid(nla);
		break;

	case ROUTERBOOST_MSG_SET_WLAN_INDEX:
		ret = handle_nlmsg_set_wlan_index(nla);
		break;

	case ROUTERBOOST_MSG_SET_GAME_PLAYING_STATE:
		ret = handle_nlmsg_set_game_playing_state(nla);
		break;

	default:
		break;
	}

	return ret;
}

static const struct genl_ops routerboost_genl_ops[] = {
	{
		.cmd = ROUTERBOOST_CMD_DOWNLINK,
		.flags = 0,
		.doit = routerboost_genl_nlmsg_handle,
		.dumpit = NULL,
	},
};

static struct genl_family routerboost_genl_family = {
	.id = 0,
	.hdrsize = 0,
	.name = ROUTERBOOST_FAMILY,
	.version = ROUTERBOOST_FAMILY_VERSION,
	.maxattr = ROUTERBOOSt_MSG_MAX,
	.ops = routerboost_genl_ops,
	.n_ops = ARRAY_SIZE(routerboost_genl_ops),
};

static inline int genl_msg_prepare_usr_msg(u8 cmd, size_t size, pid_t pid,
		struct sk_buff **skbp)
{
	struct sk_buff *skb;
	/* create a new netlink msg */
	skb = genlmsg_new(size, GFP_ATOMIC);

	if (skb == NULL) {
		return -ENOMEM;
	}

	/* Add a new netlink message to an skb */
	genlmsg_put(skb, pid, 0, &routerboost_genl_family, 0, cmd);
	*skbp = skb;
	return 0;
}

static inline int genl_msg_mk_usr_msg(struct sk_buff *skb, int type, void *data,
				      int len)
{
	int ret;

	/* add a netlink attribute to a socket buffer */
	if ((ret = nla_put(skb, type, len, data)) != 0) {
		return ret;
	}

	return 0;
}

/* send to user space */
int routerboost_genl_msg_send_to_user(int msg_type, char *payload,
				      int payload_len)
{
	int ret = 0;
	void *head;
	struct sk_buff *skbuff;
	size_t size;

	if (!routerboost_netlink_pid) {
		debug("routerboost_netlink_pid == 0!!\n");
		return -1;
	}

	/*allocate new buffer cache */
	size = nla_total_size(payload_len);
	ret = genl_msg_prepare_usr_msg(ROUTERBOOST_CMD_UPLINK, size,
				       routerboost_netlink_pid, &skbuff);

	if (ret) {
		return ret;
	}

	ret = genl_msg_mk_usr_msg(skbuff, msg_type, payload, payload_len);

	if (ret) {
		kfree_skb(skbuff);
		return ret;
	}

	head = genlmsg_data(nlmsg_data(nlmsg_hdr(skbuff)));
	genlmsg_end(skbuff, head);

	/* send data */
	ret = genlmsg_unicast(&init_net, skbuff, routerboost_netlink_pid);

	if (ret < 0) {
		debug("routerboost_send_to_user fail, can not unicast skbuff, ret = %d\n", ret);
		return -1;
	}

	return 0;
}

static int routerboost_genl_register(void)
{
	int ret;
	ret = genl_register_family(&routerboost_genl_family);

	if (ret) {
		debug("genl_register_family:%s error,ret = %d\n", ROUTERBOOST_FAMILY, ret);
		return ret;

	} else {
		debug("genl_register_family complete, id = %d!\n", routerboost_genl_family.id);
	}

	return 0;
}

static void routerboost_genl_unregister(void)
{
	genl_unregister_family(&routerboost_genl_family);
}
/* ---- genl process func end ---- */

static int __init routerboost_module_init(void)
{
	int ret = 0;

	ret = routerboost_genl_register();

	if (ret < 0) {
		debug(" module can not init netlink.\n");
	}

	ret |= routerboost_sysctl_register();
	ret |= routerboost_game_monitor_init();

	debug(" enter.\n");
	return ret;
}

static void __exit routerboost_module_exit(void)
{
	routerboost_genl_unregister();
	routerboost_sysctl_unregister();
	routerboost_game_monitor_exit();
	debug(" exit.\n");
}

module_init(routerboost_module_init);
module_exit(routerboost_module_exit);
MODULE_LICENSE("GPL v2");
