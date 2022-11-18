/************************************************************************************
** File: - oplus_routerboost_game_monitor.c
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

#define DETECT_PKT_THRED (32)

static u32 detect_game_uid = 0;
static char detect_ifname[10] = {0};

static int detect_playing_pkt_cnt = 0;
static int is_game_playing = 0;

void set_game_monitor_uid(u32 uid)
{
	detect_game_uid = uid;
	debug("detect_game_uid = %d\n", detect_game_uid);
}

void set_game_monitor_wlan_index(u32 monitor_wlan_index)
{
	if (monitor_wlan_index == 0) {
		strcpy(detect_ifname, WLAN0);

	} else if (monitor_wlan_index == 1) {
		strcpy(detect_ifname, WLAN1);

	} else {
		memset(detect_ifname, 0, sizeof(detect_ifname));
	}

	debug("detect_ifname = %s\n", detect_ifname);
}

void set_game_playing_state(u32 is_playing)
{
	if (is_playing == 1) {
		is_game_playing = 1;

	} else {
		is_game_playing = 0;
	}

	if (is_game_playing) {
		/*a new game start, reset the detect_main_stream_pkt_cnt to detect the game main stream*/
		detect_playing_pkt_cnt = 0;
	}

	debug("set_game_playing_state = %d\n", is_game_playing);
}

static int is_game_monitor_interface(char *dev_name)
{
	if (NULL == dev_name) {
		return 0;
	}

	if (!strcmp(dev_name, detect_ifname)) {
		return 1;
	}

	return 0;
}

static int is_game_monitor_stream(u32 uid)
{
	if (detect_game_uid != 0 && uid == detect_game_uid) {
		return 1;
	}

	return 0;
}

static int is_game_monitor_enable(void)
{
	return detect_game_uid; /*if detect_game_uid = 0, it means not need detect*/
}

static int is_game_monitor_need_detect_pkt(void)
{
	if (is_game_playing && (detect_playing_pkt_cnt <= DETECT_PKT_THRED)) {
		return 1;

	} else {
		return 0;
	}
}

static void detect_game_stream_info(struct sk_buff *skb)
{
	int send_msg[10] = {0};

	u32 uid = 0;
	int srcport = 0;
	int dstport = 0;
	unsigned char *dstip = NULL;
	unsigned char *srcip = NULL;

	struct sock *sk = NULL;
	struct iphdr *iph = NULL;
	struct nf_conn *ct = NULL;
	struct net_device *dev = NULL;
	enum ip_conntrack_info ctinfo;
	const struct file *filp = NULL;

	if (NULL == skb) {
		return;
	}

	if (!is_game_monitor_enable()) {
		return;
	}

	dev = skb_dst(skb)->dev;

	if (NULL == dev) {
		return;
	}

	if (!is_game_monitor_interface(dev->name)) {
		return;
	}

	ct = nf_ct_get(skb, &ctinfo);

	if (NULL == ct) {
		return;
	}

	sk = skb_to_full_sk(skb);

	if (NULL == sk || NULL == sk->sk_socket) {
		return;
	}

	filp = sk->sk_socket->file;

	if (NULL == filp) {
		return;
	}

	uid = filp->f_cred->fsuid.val;

	if (!is_game_monitor_stream(uid)) {
		return;
	}

	iph = ip_hdr(skb);

	if (NULL == iph) {
		return;
	}

	if (iph->protocol == IPPROTO_UDP /*|| iph->protocol == IPPROTO_TCP*/) {
		dstport = ntohs(udp_hdr(skb)->dest);
		srcport = ntohs(udp_hdr(skb)->source);
		dstip = (unsigned char *)&iph->daddr;
		srcip = (unsigned char *)&iph->saddr;

		send_msg[0] = uid;
		send_msg[1] = iph->protocol;

		send_msg[2] = dstip[0];
		send_msg[3] = dstip[1];
		send_msg[4] = dstip[2];
		send_msg[5] = dstip[3];

		send_msg[6] = srcip[0];
		send_msg[7] = srcip[1];
		send_msg[8] = srcip[2];
		send_msg[9] = srcip[3];

		if (ctinfo == IP_CT_NEW) {
			/*send detect new game stream info to userspace*/
			routerboost_genl_msg_send_to_user(ROUTERBOOST_MSG_REPOET_GAME_STREEM_INFO,
							  (char *)send_msg, sizeof(send_msg));

		} else if (is_game_monitor_need_detect_pkt()) {
			/*send the first DETECT_PKT_THRED pkt info to userspace*/
			/*From these DETECT_PKT_THRED packets, statistics identify the game's main data stream*/
			routerboost_genl_msg_send_to_user(ROUTERBOOST_MSG_REPOET_GAME_PKT_INFO,
							  (char *)send_msg, sizeof(send_msg));
			++detect_playing_pkt_cnt;

			if (detect_playing_pkt_cnt == DETECT_PKT_THRED) {
				routerboost_genl_msg_send_to_user(ROUTERBOOST_MSG_REPOET_GAME_PKT_INFO_FIN,
								  (char *)send_msg, sizeof(send_msg));
			}
		}
	}
}

static unsigned int routerboost_nf_output_hook_func(void *priv,
		struct sk_buff *skb, const struct nf_hook_state *state)
{
	int ret = NF_ACCEPT;
	detect_game_stream_info(skb);
	return ret;
}

static struct nf_hook_ops routerboost_nf_hook_ops[] __read_mostly = {
	{
		.hook		= routerboost_nf_output_hook_func,
		.pf		    = NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_CONNTRACK + 2,
	}
};

int routerboost_game_monitor_init(void)
{
	int ret = 0;
	ret = nf_register_net_hooks(&init_net, routerboost_nf_hook_ops,
				    ARRAY_SIZE(routerboost_nf_hook_ops));

	debug(" enter.\n");
	return ret;
}

void routerboost_game_monitor_exit(void)
{
	nf_unregister_net_hooks(&init_net, routerboost_nf_hook_ops,
				ARRAY_SIZE(routerboost_nf_hook_ops));
	debug(" exit.\n");
}
