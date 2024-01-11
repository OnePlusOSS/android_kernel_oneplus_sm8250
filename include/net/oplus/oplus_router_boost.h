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

#ifndef _OPLUS_ROUTER_BOOST_H
#define _OPLUS_ROUTER_BOOST_H

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

extern int (*oplus_router_boost_handler)(struct sock *sk, struct sk_buff *skb);

void oplus_router_boost_set_enable(struct nlmsghdr *nlh);
void oplus_router_boost_set_app_uid(struct nlmsghdr *nlh);
int oplus_router_boost_get_enable(void);
int oplus_router_boost_get_app_uid(void);
void oplus_router_boost_set_params(struct nlmsghdr * nlh);

#endif /* _OPLUS_ROUTER_BOOST_H */
