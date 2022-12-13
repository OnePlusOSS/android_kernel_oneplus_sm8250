/************************************************************************************
** File: - oplus_router_boost.c
** VENDOR_EDIT
** Copyright (C), 2008-2020, OPLUS Mobile Comm Corp., Ltd
**
** Description:
**		1. Add for oplus router boost
**
** Version: 1.0
** Date :	2020-05-09
** TAG	:	OPLUS_FEATURE_WIFI_ROUTERBOOST
**
** ---------------------Revision History: ---------------------
**	<author>					  <data>	 <version >   <desc>
** ---------------------------------------------------------------
**
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
#include "net/oplus/oplus_router_boost.h"

#define DBG_DETAIL (0)
#define LOG_TAG "[oplus_router_boost] %s line:%d "

#if DBG_DETAIL
#define debug(fmt, args...) printk(LOG_TAG fmt, __FUNCTION__, __LINE__, ##args)
#else
#define debug(fmt, args...)
#endif

static u32 g_router_boost_app_uid = 0;
static int g_router_boost_enabled = 0;
static int g_router_boost_tcp_enabled = 0;

int (*oplus_router_boost_handler)(struct sock *sk, struct sk_buff *skb) = NULL;


/* every connection has 64 recently ids
 * the smaller it is, we more likely fail to dedeup pkt
 * which is not very critical
 */
#define DEDUP_IDS_LEN 128
/*
 * we stores 128 connections at max
 * the smaller it is, we more likely drop necessary pkt
 * which is critical
 */
#define MAX_CONNECTIONS 16

/*
 * this struct maintains server's recent id list
 * As for linux, ip-to-ip pair owns one ip id generator
 */
struct connection_ids {
	__be32 src_ip;
	int32_t recently_ids[DEDUP_IDS_LEN];  /*use 32-bit and init with -1, to avoid 0-id ambiguity*/
	unsigned char next_index;
};

static DEFINE_SPINLOCK(g_id_list_lock);
static struct connection_ids g_id_list[MAX_CONNECTIONS];
static unsigned short g_next_conn_index = 0;
static unsigned long g_dup_pkt_recv_cnt = 0;
static unsigned long g_dup_pkt_drop_cnt = 0;

int handle_router_boost(struct sock *sk, struct sk_buff *skb) {
	kuid_t sk_uid, uid;
	int cid = -1; /*not found*/
	int i = 0;

	if(!g_router_boost_enabled) {
		return 0;
	}

	if (!g_router_boost_tcp_enabled && ip_hdr(skb)->protocol == IPPROTO_TCP) {
		return 0;
	}

	if (g_router_boost_app_uid == 0) {
		return 0;
	}

	sk_uid = sk->sk_uid;
	uid = make_kuid(&init_user_ns, g_router_boost_app_uid);

	if (!uid_eq(sk_uid, uid)) {
		return 0;
	}

	spin_lock_bh(&g_id_list_lock);
	/* Firstly, get the related connection */
	for(i = 0; i < MAX_CONNECTIONS; i++) {
		if(g_id_list[i].src_ip == ip_hdr(skb)->saddr) {
			cid = i;
			break;
		}
	}

	if (cid >= 0) {
		unsigned char next_index = 0;

		/* Secondly, search id list, if match, then drop pkt */
		for(i = 0; i < DEDUP_IDS_LEN; i++) {
			if(g_id_list[cid].recently_ids[i] == ip_hdr(skb)->id) {
				++g_dup_pkt_drop_cnt;
				debug("drop id = %u, drop = %u, recv = %u\n", ntohs(ip_hdr(skb)->id), g_dup_pkt_drop_cnt, g_dup_pkt_recv_cnt);
				spin_unlock_bh(&g_id_list_lock);
				return -1;
			}
		}
		/* found connection, but no match id, so record it and pass up*/
		next_index = g_id_list[cid].next_index;

		g_id_list[cid].recently_ids[next_index] = ip_hdr(skb)->id;
		g_id_list[cid].src_ip = ip_hdr(skb)->saddr;
		g_id_list[cid].next_index = (next_index + 1) % DEDUP_IDS_LEN;
	} else {
		/* add new connection into set*/
		g_id_list[g_next_conn_index].src_ip = ip_hdr(skb)->saddr;
		/* init id list with -1 */
		for(i = 0; i < DEDUP_IDS_LEN; i++) {
			g_id_list[g_next_conn_index].recently_ids[i] = -1;
		}
		g_id_list[g_next_conn_index].recently_ids[0] = ip_hdr(skb)->id;
		g_id_list[g_next_conn_index].next_index = 1;

		g_next_conn_index = (g_next_conn_index + 1) % MAX_CONNECTIONS;
	}
	spin_unlock_bh(&g_id_list_lock);

	++g_dup_pkt_recv_cnt;

	debug("recv id = %u, drop = %u, recv = %u\n", ntohs(ip_hdr(skb)->id), g_dup_pkt_drop_cnt, g_dup_pkt_recv_cnt);
	return 0;
}


void oplus_router_boost_set_params(struct nlmsghdr *nlh) {
	int *data = (int *)NLMSG_DATA(nlh);

	g_router_boost_enabled = data[0];
	g_router_boost_app_uid = data[1];
	g_router_boost_tcp_enabled = data[2];
	printk("oplus_router_boost: g_router_boost_enabled = %d, g_router_boost_app_uid = %d, g_router_boost_tcp_enabled=%d\n", data[0], data[1], data[2]);
}

void oplus_router_boost_set_enable(struct nlmsghdr *nlh)
{
	int *data = (int *)NLMSG_DATA(nlh);

	/*
	 * 1 ---> enable
	 * 0 ---> disable
	 */
	g_router_boost_enabled = data[0];

	debug("enable = %d\n", data[0]);
}

void oplus_router_boost_set_app_uid(struct nlmsghdr *nlh)
{
	int *data = (int *)NLMSG_DATA(nlh);

	/*
	 * 1 ---> enable
	 * 0 ---> disable
	 */
	g_router_boost_app_uid = data[0];

	debug("oplus_router_boost_app_uid = %d\n", data[0]);
}

int oplus_router_boost_get_enable(void) {
	return g_router_boost_enabled;
}

int oplus_router_boost_get_app_uid(void) {
	return g_router_boost_app_uid;
}

static int __init oplus_router_boost_init(void)
{
	oplus_router_boost_handler = handle_router_boost;
	spin_lock_init(&g_id_list_lock);
	return 0;
}

static void __exit oplus_router_boost_fini(void)
{
	oplus_router_boost_handler = NULL;
}

module_init(oplus_router_boost_init);
module_exit(oplus_router_boost_fini);

