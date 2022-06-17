/* SPDX-License-Identifier: GPL-2.0-only */
/*
* Copyright (C) 2018-2020 Oplus. All rights reserved.
*/

#include <net/oplus/oplus_apps_monitor.h>
#include <net/oplus/oplus_apps_power_monitor.h>

#define MONITOR_APPS_NUM_MAX     (32)
#define IFACE_NUM_MAX            (2)
#define WLAN_INDEX               (0)
#define CELL_INDEX               (1)
#define ALL_IF_INDEX             (IFACE_NUM_MAX)

#define DEFAULT_RTT_EXCE_THRED  (100)
#define DEFAULT_RTT_GOOD_THRED  (150)
#define DEFAULT_RTT_FAIR_THRED  (200)
#define DEFAULT_RTT_POOR_THRED  (250)

#define APP_UID_IS_EMPTY         (-1)
#define TIMER_EXPIRES HZ

//the dual sta feature, the wlan/cell index is changed in oplus_sla.c
#define DUAL_STA_FEATRUE_FLAG    (1)
#define DUAL_STA_WLAN0_INDEX     (0)
#define DUAL_STA_WLAN1_INDEX     (1)
#define DUAL_STA_CELL_INDEX      (2)

/*NLMSG_MIN_TYPE is 0x10,so we start at 0x11*/
enum{
	APPS_MONITOR_SET_ANDROID_PID        = 0x11,
	APPS_MONITOR_SET_APPS_UID           = 0x12,
	APPS_MONITOR_GET_APPS_CELL_RTT      = 0x13,
	APPS_MONITOR_GET_APPS_WLAN_RTT      = 0x14,
	APPS_MONITOR_GET_APPS_ALL_RTT       = 0x15,
	APPS_MONITOR_REPORT_APPS_CELL_RTT   = 0x16,
	APPS_MONITOR_REPORT_APPS_WLAN_RTT   = 0x17,
	APPS_MONITOR_REPORT_APPS_ALL_RTT    = 0x18,
	APPS_MOINTOR_SET_RTT_THRED          = 0x19,
	APPS_MONITOR_GET_DEV_RTT            = 0x20,
	APPS_MONITOR_REPORT_DEV_RTT         = 0x21,
	//added for power monitor function
	APPS_POWER_MONITOR_MSG_DL_CTRL = 0x30,
	APPS_POWER_MONITOR_MSG_DL_RPT_CTRL,
	APPS_POWER_MONITOR_MSG_UL_INFO,
	APPS_POWER_MONITOR_MSG_UL_BEAT_ALARM,
	APPS_POWER_MONITOR_MSG_UL_PUSH_ALARM,
	APPS_POWER_MONITOR_MSG_UL_TRAFFIC_ALARM,
};

typedef struct rtt_params {
	u64 rtt_exce_count;
	u64 rtt_good_count;
	u64 rtt_fair_count;
	u64 rtt_poor_count;
	u64 rtt_bad_count;
	u64 rtt_total_count;
} rtt_params;

typedef struct rtt_params_thred {
	u32 rtt_exce_thred;
	u32 rtt_good_thred;
	u32 rtt_fair_thred;
	u32 rtt_poor_thred;
} rtt_params_thred;

typedef struct monitor_app_params {
	int app_uid;
	rtt_params app_rtt[IFACE_NUM_MAX];
} monitor_app_params;

static int g_monitor_apps_num = 0;
static monitor_app_params g_monitor_apps_table[MONITOR_APPS_NUM_MAX];
static rtt_params g_monitor_dev_table[IFACE_NUM_MAX];
static rtt_params_thred g_rtt_params_thred = {DEFAULT_RTT_EXCE_THRED, DEFAULT_RTT_GOOD_THRED, DEFAULT_RTT_FAIR_THRED, DEFAULT_RTT_POOR_THRED};

static u32 apps_monitor_netlink_pid = 0;
static int apps_monitor_debug = 0;
static int rrt_period_report_enable = 1;
static int rrt_period_report_timer = 5; //5 sec
static struct sock *apps_monitor_netlink_sock;
static struct timer_list apps_monitor_timer;

static DEFINE_MUTEX(apps_monitor_netlink_mutex);
static struct ctl_table_header *apps_monitor_table_hrd;
static rwlock_t apps_monitor_lock;

#define apps_monitor_read_lock() 			read_lock_bh(&apps_monitor_lock);
#define apps_monitor_read_unlock() 			read_unlock_bh(&apps_monitor_lock);
#define apps_monitor_write_lock() 			write_lock_bh(&apps_monitor_lock);
#define apps_monitor_write_unlock()			write_unlock_bh(&apps_monitor_lock);

/* send to user space */
int apps_monitor_netlink_send_to_user(int msg_type, char *payload, int payload_len)
{
	int ret = 0;
	struct sk_buff *skbuff;
	struct nlmsghdr *nlh;

	if (!apps_monitor_netlink_pid) {
		printk("oplus_apps_monitor: apps_monitor_netlink_send_to_user, can not unicast skbuff, apps_monitor_netlink_pid=0\n");
		return -1;
	}

	/*allocate new buffer cache */
	skbuff = alloc_skb(NLMSG_SPACE(payload_len), GFP_ATOMIC);
	if (skbuff == NULL) {
		printk("oplus_apps_monitor: apps_monitor_netlink_send_to_user, skbuff alloc_skb failed\n");
		return -1;
	}

	/* fill in the data structure */
	nlh = nlmsg_put(skbuff, 0, 0, msg_type, NLMSG_ALIGN(payload_len), 0);
	if (nlh == NULL) {
		printk("oplus_apps_monitor: apps_monitor_netlink_send_to_user, nlmsg_put failaure\n");
		nlmsg_free(skbuff);
		return -1;
	}

	//compute nlmsg length
	nlh->nlmsg_len = NLMSG_HDRLEN + NLMSG_ALIGN(payload_len);

	if(NULL != payload){
		memcpy((char *)NLMSG_DATA(nlh),payload,payload_len);
	}

	/* set control field,sender's pid */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0))
	NETLINK_CB(skbuff).pid = 0;
#else
	NETLINK_CB(skbuff).portid = 0;
#endif

	NETLINK_CB(skbuff).dst_group = 0;

	/* send data */
	if(apps_monitor_netlink_pid){
		ret = netlink_unicast(apps_monitor_netlink_sock, skbuff, apps_monitor_netlink_pid, MSG_DONTWAIT);
	} else {
		printk(KERN_ERR "oplus_apps_monitor: apps_monitor_netlink_send_to_user, can not unicast skbuff, apps_monitor_netlink_pid=0\n");
		kfree_skb(skbuff);
	}

	if(ret < 0){
		printk(KERN_ERR "oplus_apps_monitor: apps_monitor_netlink_send_to_user, can not unicast skbuff, ret = %d\n", ret);
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(apps_monitor_netlink_send_to_user);

static struct ctl_table apps_monitor_sysctl_table[] = {
	{
		.procname	= "apps_monitor_debug",
		.data		= &apps_monitor_debug,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
	.procname	= "rrt_period_report_enable",
	.data		= &rrt_period_report_enable,
	.maxlen		= sizeof(int),
	.mode		= 0644,
	.proc_handler	= proc_dointvec,
	},
	{ }
};

static void set_monitor_apps_param_default(void)
{
	int i = 0;
	int j = 0;

	for (i = 0; i < MONITOR_APPS_NUM_MAX; ++i) {
		g_monitor_apps_table[i].app_uid = APP_UID_IS_EMPTY;

		for (j = 0; j < IFACE_NUM_MAX; ++j) {
			g_monitor_apps_table[i].app_rtt[j].rtt_exce_count = 0;
			g_monitor_apps_table[i].app_rtt[j].rtt_fair_count = 0;
			g_monitor_apps_table[i].app_rtt[j].rtt_good_count = 0;
			g_monitor_apps_table[i].app_rtt[j].rtt_poor_count = 0;
			g_monitor_apps_table[i].app_rtt[j].rtt_bad_count = 0;
			g_monitor_apps_table[i].app_rtt[j].rtt_total_count = 0;
		}
	}
}

static void clear_apps_rtt_record(int if_index)
{
	int i = 0;
	int j = 0;

	switch (if_index) {
		case WLAN_INDEX:
			for (i = 0; i < g_monitor_apps_num; ++i) {
					g_monitor_apps_table[i].app_rtt[WLAN_INDEX].rtt_exce_count = 0;
					g_monitor_apps_table[i].app_rtt[WLAN_INDEX].rtt_fair_count = 0;
					g_monitor_apps_table[i].app_rtt[WLAN_INDEX].rtt_good_count = 0;
					g_monitor_apps_table[i].app_rtt[WLAN_INDEX].rtt_poor_count = 0;
					g_monitor_apps_table[i].app_rtt[WLAN_INDEX].rtt_bad_count = 0;
					g_monitor_apps_table[i].app_rtt[WLAN_INDEX].rtt_total_count = 0;
			}
			break;
		case CELL_INDEX:
			for (i = 0; i < g_monitor_apps_num; ++i) {
					g_monitor_apps_table[i].app_rtt[CELL_INDEX].rtt_exce_count = 0;
					g_monitor_apps_table[i].app_rtt[CELL_INDEX].rtt_fair_count = 0;
					g_monitor_apps_table[i].app_rtt[CELL_INDEX].rtt_good_count = 0;
					g_monitor_apps_table[i].app_rtt[CELL_INDEX].rtt_poor_count = 0;
					g_monitor_apps_table[i].app_rtt[CELL_INDEX].rtt_bad_count = 0;
					g_monitor_apps_table[i].app_rtt[CELL_INDEX].rtt_total_count = 0;
			}
			break;
		case ALL_IF_INDEX:
			for (i = 0; i < g_monitor_apps_num; ++i) {
				for (j = 0; j < IFACE_NUM_MAX; ++j) {
					g_monitor_apps_table[i].app_rtt[j].rtt_exce_count = 0;
					g_monitor_apps_table[i].app_rtt[j].rtt_fair_count = 0;
					g_monitor_apps_table[i].app_rtt[j].rtt_good_count = 0;
					g_monitor_apps_table[i].app_rtt[j].rtt_poor_count = 0;
					g_monitor_apps_table[i].app_rtt[j].rtt_bad_count = 0;
					g_monitor_apps_table[i].app_rtt[j].rtt_total_count = 0;
				}
			}
			break;
		default:
			break;
	}
}

static void clear_dev_rtt_record(void)
{
	int i = 0;

	for (i = 0; i < IFACE_NUM_MAX; ++i) {
			g_monitor_dev_table[i].rtt_exce_count = 0;
			g_monitor_dev_table[i].rtt_fair_count = 0;
			g_monitor_dev_table[i].rtt_good_count = 0;
			g_monitor_dev_table[i].rtt_poor_count = 0;
			g_monitor_dev_table[i].rtt_bad_count = 0;
			g_monitor_dev_table[i].rtt_total_count = 0;
	}
}

static int find_skb_uid_index_in_apps_record_table(struct sock *sk)
{
	kuid_t uid;
	kuid_t sk_uid;
	int index = 0;
	int uid_index = -1;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0))
	const struct file *filp = NULL;
#endif

	if (NULL == sk || !sk_fullsock(sk) || NULL == sk->sk_socket) {
		return uid_index;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0))
	filp = sk->sk_socket->file;

	if(NULL == filp) {
		return false;
	}

	sk_uid = filp->f_cred->fsuid;
#else
	sk_uid = sk->sk_uid;
#endif

	for(index = 0; index < g_monitor_apps_num; ++index) {
		uid = make_kuid(&init_user_ns, g_monitor_apps_table[index].app_uid);

		if(uid_eq(sk_uid, uid)) {
			uid_index = index;
			break;
		}
	}

	return uid_index;
}

void update_dev_rtt_count(int if_index, int rtt)
{
	if (rtt < 0 || if_index < 0 || if_index >= IFACE_NUM_MAX) {
		return;
	}

	if (rtt <= g_rtt_params_thred.rtt_exce_thred) {
		++(g_monitor_dev_table[if_index].rtt_exce_count);
	} else if (rtt <= g_rtt_params_thred.rtt_good_thred) {
		++(g_monitor_dev_table[if_index].rtt_good_count);
	} else if (rtt <= g_rtt_params_thred.rtt_fair_thred) {
		++(g_monitor_dev_table[if_index].rtt_fair_count);
	} else if (rtt <= g_rtt_params_thred.rtt_poor_thred) {
		++(g_monitor_dev_table[if_index].rtt_poor_count);
	} else {
		++(g_monitor_dev_table[if_index].rtt_bad_count);
	}

	++(g_monitor_dev_table[if_index].rtt_total_count);

	return;
}

void update_app_rtt_count(int if_index, int sk_uid_index, int rtt)
{
	if (rtt < 0 || if_index < 0 || if_index >= IFACE_NUM_MAX) {
		return;
	}

	if (rtt <= g_rtt_params_thred.rtt_exce_thred) {
		++(g_monitor_apps_table[sk_uid_index].app_rtt[if_index].rtt_exce_count);
	} else if (rtt <= g_rtt_params_thred.rtt_good_thred) {
		++(g_monitor_apps_table[sk_uid_index].app_rtt[if_index].rtt_good_count);
	} else if (rtt <= g_rtt_params_thred.rtt_fair_thred) {
		++(g_monitor_apps_table[sk_uid_index].app_rtt[if_index].rtt_fair_count);
	} else if (rtt <= g_rtt_params_thred.rtt_poor_thred) {
		++(g_monitor_apps_table[sk_uid_index].app_rtt[if_index].rtt_poor_count);
	} else {
		++(g_monitor_apps_table[sk_uid_index].app_rtt[if_index].rtt_bad_count);
	}

	++(g_monitor_apps_table[sk_uid_index].app_rtt[if_index].rtt_total_count);

	return;
}

void statistics_monitor_apps_rtt_via_uid(int if_index, int rtt, struct sock *sk)
{
	int sk_uid_index = -1;

#if DUAL_STA_FEATRUE_FLAG
	if (if_index == DUAL_STA_WLAN0_INDEX || if_index == DUAL_STA_WLAN1_INDEX ) {
		if_index = WLAN_INDEX;
	} else {
		if_index = CELL_INDEX;
	}
#endif

	if (rtt < 0 || sk == NULL || if_index < 0 || if_index >= IFACE_NUM_MAX) {
		return;
	}

#if 0
	printk("oplus_apps_monitor: statistics_monitor_apps_rtt_via_uid, if_index = %d, rtt = %d\n", if_index, rtt);
#endif

	apps_monitor_write_lock();
	update_dev_rtt_count(if_index, rtt);

	sk_uid_index = find_skb_uid_index_in_apps_record_table(sk);

	if (sk_uid_index < 0) {
		apps_monitor_write_unlock(); //need release lock
		return;
	}

	update_app_rtt_count(if_index, sk_uid_index, rtt);
	apps_monitor_write_unlock();
}

static int apps_monitor_sysctl_init(void)
{
	apps_monitor_table_hrd = register_net_sysctl(&init_net, "net/oplus_apps_monitor",
		                                          apps_monitor_sysctl_table);
	return apps_monitor_table_hrd == NULL ? -ENOMEM : 0;
}

static int apps_monitor_set_android_pid(struct sk_buff *skb)
{
	apps_monitor_netlink_pid = NETLINK_CB(skb).portid;
	printk("oplus_apps_monitor: apps_monitor_netlink_set_android_pid pid=%d\n",apps_monitor_netlink_pid);
	return 0;
}

static int apps_monitor_set_apps_uid(struct nlmsghdr *nlh)
{
	int index = 0;
	u32 *uidInfo = (u32 *)NLMSG_DATA(nlh);
	u32 apps_uid_num = uidInfo[0];
	u32 *apps_uid = &(uidInfo[1]);

	if (apps_uid_num >= MONITOR_APPS_NUM_MAX) {
		printk("oplus_apps_monitor: the input apps_uid_num is bigger than MONITOR_APPS_NUM_MAX! \n");
		return -EINVAL;
	}

	set_monitor_apps_param_default();
	g_monitor_apps_num = apps_uid_num;

	for (index = 0; index < apps_uid_num; ++index) {
		g_monitor_apps_table[index].app_uid = apps_uid[index];

		if (apps_monitor_debug) {
			printk("oplus_apps_monitor: apps_monitor_netlink_set_apps_uid, g_monitor_apps_table[%d].app_uid = %d \n", index, g_monitor_apps_table[index].app_uid);
		}
	}

	return 0;
}

static int apps_monitor_set_rtt_thred(struct nlmsghdr *nlh)
{
	u32 *uidInfo = (u32 *)NLMSG_DATA(nlh);
	u32 rtt_thred_num = uidInfo[0];
	u32 *rtt_thred = &(uidInfo[1]);

	if (rtt_thred_num > sizeof(rtt_params_thred) / sizeof(u32)) {
		printk("oplus_apps_monitor: the input rtt_thred_num is bigger than except! the input rtt_thred_num=  %d \n", rtt_thred_num);
		return -EINVAL;
	}

	g_rtt_params_thred.rtt_exce_thred = rtt_thred[0];
	g_rtt_params_thred.rtt_good_thred = rtt_thred[1];
	g_rtt_params_thred.rtt_fair_thred = rtt_thred[2];
	g_rtt_params_thred.rtt_poor_thred = rtt_thred[3];

	return 0;
}

static void print_apps_rtt_record(void) {
	int i = 0;
	int j = 0;

	for (i = 0; i < g_monitor_apps_num; ++i) {
		for(j = 0; j < IFACE_NUM_MAX; ++j) {
			printk("oplus_apps_monitor: print_apps_rtt_record, the uid = %d, the if_index = %d, RTT = %llu:%llu:%llu:%llu:%llu:%llu\n",
													g_monitor_apps_table[i].app_uid,
													j,
													g_monitor_apps_table[i].app_rtt[j].rtt_exce_count,
													g_monitor_apps_table[i].app_rtt[j].rtt_good_count,
													g_monitor_apps_table[i].app_rtt[j].rtt_fair_count,
													g_monitor_apps_table[i].app_rtt[j].rtt_poor_count,
													g_monitor_apps_table[i].app_rtt[j].rtt_bad_count,
													g_monitor_apps_table[i].app_rtt[j].rtt_total_count);
		}
	}
}

static void print_dev_rtt_record(void) {
	int i = 0;

	for(i = 0; i < IFACE_NUM_MAX; ++i) {
		printk("oplus_apps_monitor: print_dev_rtt_record, the if_index = %d, RTT = %llu:%llu:%llu:%llu:%llu:%llu\n",
												i,
												g_monitor_dev_table[i].rtt_exce_count,
												g_monitor_dev_table[i].rtt_good_count,
												g_monitor_dev_table[i].rtt_fair_count,
												g_monitor_dev_table[i].rtt_poor_count,
												g_monitor_dev_table[i].rtt_bad_count,
												g_monitor_dev_table[i].rtt_total_count);
	}
}

static int apps_monitor_report_apps_rtt_to_user(int if_index)
{
#define MAX_RTT_MSG_LEN (2048)
	int ret = 0;
	int index = 0;
	int step = 0;
	int rtt_params_size = sizeof(rtt_params);
	int int_size = sizeof(int);
	static char send_msg[MAX_RTT_MSG_LEN] = {0};

	memset(send_msg, 0, MAX_RTT_MSG_LEN);

	if ((rtt_params_size + int_size) * IFACE_NUM_MAX * g_monitor_apps_num > MAX_RTT_MSG_LEN) {
		printk("oplus_apps_monitor: apps_monitor_report_apps_rtt_to_user, the RTT Msg is too big, len = %0x \n",
															(rtt_params_size + int_size) * g_monitor_apps_num);
		return -EINVAL;
	}

	switch (if_index) {
		case WLAN_INDEX:
			for(index = 0; index < g_monitor_apps_num; ++index) {
				step = (rtt_params_size * index)  + index * int_size;
				memcpy(send_msg + step, &g_monitor_apps_table[index].app_uid, int_size);
				memcpy(send_msg + step + int_size, &(g_monitor_apps_table[index].app_rtt[WLAN_INDEX]), rtt_params_size);
			}

			ret = apps_monitor_netlink_send_to_user(APPS_MONITOR_REPORT_APPS_WLAN_RTT, (char *) send_msg,
														(rtt_params_size + int_size) * g_monitor_apps_num);
			break;
		case CELL_INDEX:
			for(index = 0; index < g_monitor_apps_num; ++index) {
				step = (rtt_params_size * index)  + index * int_size;
				memcpy(send_msg + step, &g_monitor_apps_table[index].app_uid, int_size);
				memcpy(send_msg + step + int_size, &(g_monitor_apps_table[index].app_rtt[CELL_INDEX]), rtt_params_size);
			}

			ret = apps_monitor_netlink_send_to_user(APPS_MONITOR_REPORT_APPS_CELL_RTT, (char *) send_msg,
														(rtt_params_size + int_size) * g_monitor_apps_num);
			break;
		case ALL_IF_INDEX:
			for(index = 0; index < g_monitor_apps_num; ++index) {
				step = rtt_params_size * IFACE_NUM_MAX * index + index * int_size;
				memcpy(send_msg + step, &g_monitor_apps_table[index].app_uid, int_size);
				memcpy(send_msg + step + int_size, &(g_monitor_apps_table[index].app_rtt), rtt_params_size * IFACE_NUM_MAX);
			}

			ret = apps_monitor_netlink_send_to_user(APPS_MONITOR_REPORT_APPS_ALL_RTT, (char *) send_msg,
									(rtt_params_size + int_size) * IFACE_NUM_MAX * g_monitor_apps_num);
			break;
		default:
			printk("oplus_apps_monitor: apps_monitor_report_apps_rtt_to_user, the if_index is unvalue! \n");
			return -EINVAL;
	}

	if (ret == 0) {
		if (apps_monitor_debug) {
			print_apps_rtt_record();
		}
		//report success,clear the rtt record in kernel
		clear_apps_rtt_record(if_index);
	} else {
		printk("oplus_apps_monitor: apps_monitor_netlink_send_rtt_to_user fail! \n");
	}

	return ret;
}

static int apps_monitor_report_dev_rtt_to_user(void)
{
#define MAX_DEV_RTT_MSG_LEN (256)
	int ret = 0;
	int index = 0;
	int step = 0;
	int rtt_params_size = sizeof(rtt_params);
	static char send_msg[MAX_DEV_RTT_MSG_LEN] = {0};

	memset(send_msg, 0, MAX_DEV_RTT_MSG_LEN);

	if (rtt_params_size * IFACE_NUM_MAX > MAX_DEV_RTT_MSG_LEN) {
		printk("oplus_apps_monitor: apps_monitor_report_dev_rtt_to_user, the RTT Msg is too big, len = %0x \n",
															rtt_params_size * IFACE_NUM_MAX);
		return -EINVAL;
	}

	for(index = 0; index < IFACE_NUM_MAX; ++index) {
		step = rtt_params_size * index;
		memcpy(send_msg + step, &g_monitor_dev_table[index], rtt_params_size);
	}

	ret = apps_monitor_netlink_send_to_user(APPS_MONITOR_REPORT_DEV_RTT, (char *) send_msg,
															rtt_params_size * IFACE_NUM_MAX);

	if (ret == 0) {
		if (apps_monitor_debug) {
			print_dev_rtt_record();
		}
		//report success,clear the rtt record in kernel
		clear_dev_rtt_record();
	} else {
		printk("oplus_apps_monitor: apps_monitor_report_dev_rtt_to_user fail! \n");
	}

	return ret;
}

static int apps_monitor_netlink_nlmsg_handle(struct sk_buff *skb, struct nlmsghdr *nlh, struct netlink_ext_ack *extack)
{
	int ret = 0;

	if (apps_monitor_debug) {
		printk("oplus_apps_monitor: apps_monitor_netlink_recv_handle, the nlh->nlmsg_type = %0x \n", nlh->nlmsg_type);
	}

	switch (nlh->nlmsg_type) {
		case APPS_MONITOR_SET_ANDROID_PID:
			ret = apps_monitor_set_android_pid(skb);
			break;
		case APPS_MONITOR_SET_APPS_UID:
			ret = apps_monitor_set_apps_uid(nlh);
			break;
		case APPS_MONITOR_GET_APPS_CELL_RTT:
			ret = apps_monitor_report_apps_rtt_to_user(CELL_INDEX);
			break;
		case APPS_MONITOR_GET_APPS_WLAN_RTT:
			ret = apps_monitor_report_apps_rtt_to_user(WLAN_INDEX);
			break;
		case APPS_MONITOR_GET_APPS_ALL_RTT:
			ret = apps_monitor_report_apps_rtt_to_user(ALL_IF_INDEX);
			break;
		case APPS_MOINTOR_SET_RTT_THRED:
			ret = apps_monitor_set_rtt_thred(nlh);
			break;
		case APPS_MONITOR_GET_DEV_RTT:
			ret = apps_monitor_report_dev_rtt_to_user();
			break;
		case APPS_POWER_MONITOR_MSG_DL_CTRL:
			printk("[app_monitor]:rececice APPS_POWER_MONITOR_MSG_DL_CTRL\n");
			ret = app_monitor_dl_ctl_msg_handle(nlh);
			break;
		case APPS_POWER_MONITOR_MSG_DL_RPT_CTRL:
			printk("[app_monitor]:rececice APPS_POWER_MONITOR_MSG_DL_RPT_CTRL\n");
			ret = app_monitor_dl_report_msg_handle(nlh);
			break;
		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}

static void apps_monitor_netlink_recv(struct sk_buff *skb)
{
	mutex_lock(&apps_monitor_netlink_mutex);
	netlink_rcv_skb(skb, &apps_monitor_netlink_nlmsg_handle);
	mutex_unlock(&apps_monitor_netlink_mutex);
}

static int apps_monitor_netlink_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input	= apps_monitor_netlink_recv,
	};

	apps_monitor_netlink_sock = netlink_kernel_create(&init_net, NETLINK_OPLUS_APPS_MONITOR, &cfg);

	return apps_monitor_netlink_sock == NULL ? -ENOMEM : 0;
}

static void apps_monitor_netlink_exit(void)
{
	netlink_kernel_release(apps_monitor_netlink_sock);
	apps_monitor_netlink_sock = NULL;
}

static void apps_monitor_timer_function(void) {
	if (rrt_period_report_enable && apps_monitor_netlink_pid != 0) {
		apps_monitor_report_apps_rtt_to_user(ALL_IF_INDEX);
		apps_monitor_report_dev_rtt_to_user();
	}

	mod_timer(&apps_monitor_timer, jiffies + rrt_period_report_timer * TIMER_EXPIRES);
}

static int is_need_period_timer(void) {
	return rrt_period_report_enable; //For more period funs, {return xx_period_enable | xx_period_enable}
}

static void apps_monitor_timer_init(void)
{
	timer_setup(&apps_monitor_timer, (void*)apps_monitor_timer_function, 0);
	apps_monitor_timer.expires = jiffies + rrt_period_report_timer * TIMER_EXPIRES;
	add_timer(&apps_monitor_timer);
}

static void apps_monitor_timer_del(void)
{
	del_timer(&apps_monitor_timer);
}

static int __init oplus_apps_monitor_init(void)
{
	int ret = 0;

	rwlock_init(&apps_monitor_lock);
	set_monitor_apps_param_default();
	ret = apps_monitor_netlink_init();

	if (ret < 0) {
		printk("oplus_apps_monitor: oplus_apps_monitor_init module failed to init netlink.\n");
	} else {
		printk("oplus_apps_monitor: oplus_apps_monitor_init module init netlink successfully.\n");
	}

	ret |= apps_monitor_sysctl_init();

	if (is_need_period_timer()) {
		apps_monitor_timer_init();
	}

       oplus_app_power_monitor_init();
	return ret;
}

static void __exit oplus_apps_monitor_fini(void)
{
	clear_apps_rtt_record(ALL_IF_INDEX);
	clear_dev_rtt_record();
	apps_monitor_netlink_exit();

	if (is_need_period_timer()) {
		apps_monitor_timer_del();
	}

	if(apps_monitor_table_hrd){
		unregister_net_sysctl_table(apps_monitor_table_hrd);
	}

	oplus_app_power_monitor_fini();
}

module_init(oplus_apps_monitor_init);
module_exit(oplus_apps_monitor_fini);
