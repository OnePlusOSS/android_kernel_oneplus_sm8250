// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

/****************************************************************
** ------------------ Revision History:------------------------
** <author> <data> <version > <desc>
** Asiga 2019/07/31 1.0 build this module
****************************************************************/
#ifndef __OPLUS_NWPOWER_H_
#define __OPLUS_NWPOWER_H_

#include <net/sock.h>
#include <linux/skbuff.h>

#define OPLUS_TCP_TYPE_V4               1
#define OPLUS_TCP_TYPE_V6               2
#define OPLUS_NET_OUTPUT                0
#define OPLUS_NET_INPUT                 1

#define OPLUS_NW_WAKEUP_SUM                 8
#define OPLUS_NW_MPSS                       0
#define OPLUS_NW_QRTR                       1
#define OPLUS_NW_MD                         2
#define OPLUS_NW_WIFI                       3
#define OPLUS_NW_TCP_IN                     4
#define OPLUS_NW_TCP_OUT                    5
#define OPLUS_NW_TCP_RE_IN                  6
#define OPLUS_NW_TCP_RE_OUT                 7

//Add for Mpss wakeup
extern void oplus_match_modem_wakeup(void);
extern void oplus_match_wlan_wakeup(void);

//Add for QMI wakeup
extern void oplus_match_qrtr_service_port(int type, int id, int port);
extern void oplus_match_qrtr_wakeup(int src_node, int src_port, int dst_port, unsigned int arg1, unsigned int arg2);
extern void oplus_update_qrtr_flag(int val);

//Add for IPA wakeup(tcp input)
extern void oplus_match_ipa_ip_wakeup(int type, struct sk_buff *skb);
extern void oplus_match_ipa_tcp_wakeup(int type, struct sock *sk);
extern void oplus_ipa_schedule_work(void);

//Add for tcp output
extern void oplus_match_tcp_output(struct sock *sk);

//Add for tcp retrans
extern void oplus_match_tcp_input_retrans(struct sock *sk);
extern void oplus_match_tcp_output_retrans(struct sock *sk);

//#ifdef OPLUS_FEATURE_NWPOWER_NETCONTROLLER
extern bool oplus_check_socket_in_blacklist(int is_input, struct socket *sock);
//#endif /* OPLUS_FEATURE_NWPOWER_NETCONTROLLER */

#endif /* __OPLUS_NWPOWER_H_ */