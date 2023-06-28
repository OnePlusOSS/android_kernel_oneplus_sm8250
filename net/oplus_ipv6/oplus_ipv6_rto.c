/******************************************************************************
** Copyright (C), 2019-2029, OPLUS Mobile Comm Corp., Ltd
** VENDOR_EDIT, All rights reserved.
** File: - oplus_ipv6_rto.c
** Description: ipv6 optimize
**
** Version: 1.0
** Date : 2020/09/14
** TAG: OPLUS_FEATURE_IPV6_OPTIMIZE
** ------------------------------- Revision History: ----------------------------
** <author>                                <data>        <version>       <desc>
** ------------------------------------------------------------------------------
 *******************************************************************************/
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/sysctl.h>
#include <net/route.h>
#include <net/ip.h>
#include <net/ipv6.h>
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

#define KLOGD(format, ...) {	\
	if (oplus_ipv6_rto_debug) {\
		printk("%s[%d]: ", __func__, __LINE__);\
		printk(format,  ##__VA_ARGS__);\
	}\
}

#define KLOGE(format, ...) {	\
		printk("Error %s[%d]: ", __func__, __LINE__);\
		printk(format,  ##__VA_ARGS__);\
}

#define IPV6_RTO_ENABLED 1
#define IPV6_RTO_DISABLED 0

enum{
	IPV6_RTO_ENABLE = 0x101,
	IPV6_RTO_DISABLE = 0x102,
	IPV6_RTO_UID_NOTIFY = 0x103,
	IPV6_RTO_PID_NOTIFY = 0x104,
	IPV6_RTO_DEBUG = 0x105,
	IPV4_RTO_UID_NOTIFY = 0x106
};

struct oplus_ipv6_rto_info{
	struct in6_addr v6_saddr;
	unsigned short sk_uid;
};

struct oplus_ipv4_rto_info{
	unsigned int v4_saddr;
	unsigned short sk_uid;
};

static int oplus_ipv6_rto_enable;
static int oplus_ipv6_rto_debug = 1;
static volatile u32 oplus_ipv6_rto_pid;
static struct sock *oplus_ipv6_rto_sock;
static DEFINE_MUTEX(ipv6_rto_netlink_mutex);


static int oplus_ipv6_rto_get_pid(struct sk_buff *skb,struct nlmsghdr *nlh)
{
	oplus_ipv6_rto_pid = NETLINK_CB(skb).portid;
	KLOGD("oplus_ipv6_rto_netlink:get oplus_ipv6_rto_pid = %u\n", oplus_ipv6_rto_pid);
	return 0;
}

static int ipv6_rto_netlink_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh, struct netlink_ext_ack *extack)
{
	int ret = 0;

	switch (nlh->nlmsg_type) {
	case IPV6_RTO_ENABLE:
		if(!oplus_ipv6_rto_enable) {
			oplus_ipv6_rto_enable = IPV6_RTO_ENABLED;
		}
		break;
	case IPV6_RTO_DISABLE:
		if(oplus_ipv6_rto_enable) {
			oplus_ipv6_rto_enable = IPV6_RTO_DISABLED;
		}
		break;
	case IPV6_RTO_PID_NOTIFY:
		ret = oplus_ipv6_rto_get_pid(skb, nlh);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int oplus_ipv6_rto_send_to_user(int msg_type, char *payload, int payload_len)
{
	int ret = 0;
	struct sk_buff *skbuff;
	struct nlmsghdr *nlh;

	if (!oplus_ipv6_rto_pid) {
		KLOGE("oplus_ipv6_rto_netlink: oplus_ipv6_rto_pid == 0!!\n");
		return -1;
	}

	/*allocate new buffer cache */
	skbuff = alloc_skb(NLMSG_SPACE(payload_len), GFP_ATOMIC);
	if (skbuff == NULL) {
		KLOGE("oplus_ipv6_rto_netlink: skbuff alloc_skb failed\n");
		return -1;
	}

	/* fill in the data structure */
	nlh = nlmsg_put(skbuff, 0, 0, msg_type, NLMSG_ALIGN(payload_len), 0);
	if (nlh == NULL) {
		KLOGE("oplus_ipv6_rto_netlink:nlmsg_put failaure\n");
		nlmsg_free(skbuff);
		return -1;
	}

	//compute nlmsg length
	nlh->nlmsg_len = NLMSG_HDRLEN + NLMSG_ALIGN(payload_len);
	if(NULL != payload){
		memcpy((char *)NLMSG_DATA(nlh), payload, payload_len);
	}

	/* set control field,sender's pid */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0))
	NETLINK_CB(skbuff).pid = 0;
#else
	NETLINK_CB(skbuff).portid = 0;
#endif
	NETLINK_CB(skbuff).dst_group = 0;

	/* send data */
	ret = netlink_unicast(oplus_ipv6_rto_sock, skbuff, oplus_ipv6_rto_pid, MSG_DONTWAIT);
	if(ret < 0){
		KLOGE("oplus_ipv6_rto_netlink: can not unicast skbuff,ret = %d\n", ret);
		return -1;
	}
	return 0;
}


int ipv6_rto_encounter(kuid_t uid, struct in6_addr v6_saddr)
{
	struct oplus_ipv6_rto_info rto_info;

	if (uid.val <= 0) 
	{
		KLOGE("Invalid uid\n");
		return -1;
	}
	KLOGD("ipv6 RTO at uid: %d", uid.val);

	if (!oplus_ipv6_rto_enable)
	{
		KLOGD("oplus_ipv6_rto is disabled.\n");
		return -1;
	}

	rto_info.sk_uid = uid.val;
	rto_info.v6_saddr = v6_saddr;
	oplus_ipv6_rto_send_to_user(IPV6_RTO_UID_NOTIFY, (char*)(&rto_info), sizeof(struct oplus_ipv6_rto_info));
	memset(&rto_info, 0x0, sizeof(struct oplus_ipv6_rto_info));
	return 0;
}

EXPORT_SYMBOL(ipv6_rto_encounter);

int ipv4_rto_encounter(kuid_t uid,  unsigned int v4_saddr)
{
	struct oplus_ipv4_rto_info rto_info;

	if (uid.val <= 0) 
	{
		KLOGE("Invalid uid\n");
		return -1;
	}
	KLOGD("ipv4 RTO at uid: %d", uid.val);

	if (!oplus_ipv6_rto_enable)
	{
		KLOGD("oplus_ipv6_rto is disabled.\n");
		return -1;
	}

	rto_info.sk_uid = uid.val;
	rto_info.v4_saddr = v4_saddr;
	oplus_ipv6_rto_send_to_user(IPV4_RTO_UID_NOTIFY, (char*)(&rto_info), sizeof(struct oplus_ipv4_rto_info));
	memset(&rto_info, 0x0, sizeof(struct oplus_ipv4_rto_info));
	return 0;
}

EXPORT_SYMBOL(ipv4_rto_encounter);

static void ipv6_rto_netlink_rcv(struct sk_buff *skb)
{
	mutex_lock(&ipv6_rto_netlink_mutex);
	netlink_rcv_skb(skb, &ipv6_rto_netlink_rcv_msg);
	mutex_unlock(&ipv6_rto_netlink_mutex);
}

static int oplus_ipv6_rto_netlink_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input	= ipv6_rto_netlink_rcv,
	};
	oplus_ipv6_rto_sock = netlink_kernel_create(&init_net, NETLINK_OPLUS_IPV6_RTO, &cfg);
	return oplus_ipv6_rto_sock == NULL ? -ENOMEM : 0;
}


static void oplus_ipv6_rto_netlink_exit(void)
{
	netlink_kernel_release(oplus_ipv6_rto_sock);
	oplus_ipv6_rto_sock = NULL;
}

static int __init oplus_ipv6_rto_init(void)
{
	int ret = 0;

	//Disable this feature default.
	oplus_ipv6_rto_enable = IPV6_RTO_DISABLED;
	ret = oplus_ipv6_rto_netlink_init();
	if (ret < 0) {
		KLOGE("oplus_ipv6_rto module can not init netlink.\n");
	}
	return ret;
}


static void __exit oplus_ipv6_rto_fini(void)
{
	oplus_ipv6_rto_netlink_exit();
}

module_init(oplus_ipv6_rto_init);
module_exit(oplus_ipv6_rto_fini);
