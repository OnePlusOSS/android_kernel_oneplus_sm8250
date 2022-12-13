// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

/****************************************************************
** ------------------ Revision History:------------------------
** <author> <data> <version > <desc>
** Asiga 2019/07/31 1.0 build this module
****************************************************************/
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/qrtr.h>
#include <linux/ipc_logging.h>
#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/netlink.h>
#include <linux/version.h>
#include <net/tcp.h>
#include <net/oplus_nwpower.h>
#ifdef OPLUS_FEATURE_POWERINFO_STANDBY
#include <soc/oplus/oplus_wakelock_profiler.h>
#endif /* OPLUS_FEATURE_POWERINFO_STANDBY */

static void tcp_output_hook_work_callback(struct work_struct *work);
static void tcp_input_hook_work_callback(struct work_struct *work);
static void tcp_output_tcpsynretrans_hook_work_callback(struct work_struct *work);
static void tcp_input_tcpsynretrans_hook_work_callback(struct work_struct *work);

static int nwpower_send_to_user(int msg_type,char *msg_data, int msg_len);

//Add for feature switch
static atomic_t qrtr_wakeup_hook_boot = ATOMIC_INIT(0);
static atomic_t ipa_wakeup_hook_boot = ATOMIC_INIT(0);
static atomic_t tcpsynretrans_hook_boot = ATOMIC_INIT(0);

//Add for qmi wakeup msg
#define GLINK_MODEM_NODE_ID    0x3
#define GLINK_ADSP_NODE_ID     0x5
#define GLINK_CDSP_NODE_ID     0xa
#define GLINK_SLPI_NODE_ID     0x9
#define GLINK_NPU_NODE_ID      0xb

#define OPLUS_MAX_QRTR_SERVICE_LEN         120

atomic_t qrtr_first_msg = ATOMIC_INIT(0);
u64 oplus_nw_wakeup[OPLUS_NW_WAKEUP_SUM] = {0};
static u64 service_wakeup_times[OPLUS_MAX_QRTR_SERVICE_LEN][4] = {{0}};

//Add for ipa wakeup msg
#define OPLUS_MAX_RECORD_IP_LEN             60
#define OPLUS_TRANSMISSION_INTERVAL         3 * 1000//3s
#define OPLUS_TCP_RETRANSMISSION_INTERVAL   1 * 1000//1s
#define OPLUS_MAX_RECORD_APP_WAKEUP_LEN     100

struct tcp_hook_struct {
	u32 uid;
	u32 pid;
	bool is_ipv6;
	u32 ipv4_addr;
	u64 ipv6_addr1;
	u64 ipv6_addr2;
	u64 set[OPLUS_MAX_RECORD_IP_LEN*3];
};

struct tcp_hook_simple_struct {
	u32 count;
	u64 set[OPLUS_MAX_RECORD_APP_WAKEUP_LEN*3+1];
};
static struct tcp_hook_simple_struct app_wakeup_monitor_list = {
	.set = {0},
};

static bool tcp_input_sch_work = false;
static atomic_t tcp_is_input = ATOMIC_INIT(0);//1=v4_input,2=v6_input,3=output,0=default
static struct timespec tcp_last_transmission_stamp;
static struct tcp_hook_struct tcp_output_list = {
	.is_ipv6 = false,
	.set = {0},
};
static struct tcp_hook_struct tcp_input_list = {
	.is_ipv6 = false,
	.set = {0},
};
static struct tcp_hook_struct tcp_output_retrans_list = {
	.is_ipv6 = false,
	.set = {0},
};
static struct tcp_hook_struct tcp_input_retrans_list = {
	.is_ipv6 = false,
	.set = {0},
};
DECLARE_WORK(tcp_output_hook_work, tcp_output_hook_work_callback);
DECLARE_WORK(tcp_input_hook_work, tcp_input_hook_work_callback);
DECLARE_WORK(tcp_output_tcpsynretrans_hook_work, tcp_output_tcpsynretrans_hook_work_callback);
DECLARE_WORK(tcp_input_tcpsynretrans_hook_work, tcp_input_tcpsynretrans_hook_work_callback);

//Add for modem eap buffer
u64 oplus_mdaci_nw_wakeup[OPLUS_NW_WAKEUP_SUM] = {0};
static u64 mdaci_service_wakeup_times[OPLUS_MAX_QRTR_SERVICE_LEN][4] = {{0}};
static struct tcp_hook_struct mdaci_tcp_output_list = {
	.is_ipv6 = false,
	.set = {0},
};
static struct tcp_hook_struct mdaci_tcp_input_list = {
	.is_ipv6 = false,
	.set = {0},
};
static struct tcp_hook_struct mdaci_tcp_output_retrans_list = {
	.is_ipv6 = false,
	.set = {0},
};
static struct tcp_hook_struct mdaci_tcp_input_retrans_list = {
	.is_ipv6 = false,
	.set = {0},
};

//Add for Netlink
enum{
	NW_POWER_ANDROID_PID                   = 0x11,
	NW_POWER_BOOT_MONITOR                  = 0x12,
	NW_POWER_STOP_MONITOR                  = 0x13,
	NW_POWER_STOP_MONITOR_UNSL             = 0x14,
	NW_POWER_UNSL_MONITOR                  = 0x15,
	NW_POWER_REQUEST_MDACI                 = 0x16,
	NW_POWER_REPORT_MDACI                  = 0x17,
	NW_POWER_BLACK_LIST                    = 0x18,
	NW_POWER_REQUEST_BLACK_REJECT          = 0x19,
	NW_POWER_REPORT_BLACK_REJECT           = 0x1A,
	NW_POWER_REQUEST_APP_WAKEUP            = 0x1D,
	NW_POWER_REPORT_APP_WAKEUP             = 0x1E,
	NW_POWER_REPORT_MDACI_APP_WAKEUP       = 0x1F,
};
static DEFINE_MUTEX(netlink_mutex);
static u32 oplus_nwpower_pid = 0;
static struct sock *oplus_nwpower_sock;

//Add for unsl wakeup msg
#define KERNEL_UNSL_MONITOR_LEN 7
static u64 wakeup_unsl_msg[KERNEL_UNSL_MONITOR_LEN] = {0};

#define KERNEL_UNSL_APP_WAKEUP_LEN 300
static u32 blacklist_len = 0;
static u32 blacklist_uid[KERNEL_UNSL_APP_WAKEUP_LEN] = {0};

#define OPLUS_MAX_RECORD_BLACK_REJECT_LEN 100
#define KERNEL_UNSL_BLACK_REJECT_LEN 201
static u32 record_blacklist_reject_index = 0;
static u64 blacklist_reject_uid[KERNEL_UNSL_BLACK_REJECT_LEN] = {0};

/*Add for qrtr bts info*/
#define BTS_BUFFER_SIZE (80)
static char bts_net_wakeup_buffer[BTS_BUFFER_SIZE+1];

/*Add for mdaci wakeup apps*/
static struct tcp_hook_simple_struct mdaci_app_wakeup_monitor_list = {
	.set = {0},
};

static uid_t get_uid_from_sock(const struct sock *sk)
{
	uid_t sk_uid;
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	const struct file *filp = NULL;
	#endif
	if(NULL == sk || !sk_fullsock(sk) || NULL == sk->sk_socket) {
		return 0;
	}
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	filp = sk->sk_socket->file;
	if(NULL == filp) {
		return 0;
	}
	sk_uid = __kuid_val(filp->f_cred->fsuid);
	#else
	sk_uid = __kuid_val(sk->sk_uid);
	#endif
	return sk_uid;
}

static void nwpower_unsl_blacklist_reject() {
	if (record_blacklist_reject_index > 0) {
		blacklist_reject_uid[0] = record_blacklist_reject_index;
		nwpower_send_to_user(NW_POWER_REPORT_BLACK_REJECT, (char*)blacklist_reject_uid, sizeof(blacklist_reject_uid));
		memset(blacklist_reject_uid, 0x0, OPLUS_MAX_RECORD_BLACK_REJECT_LEN * sizeof(u32));
		record_blacklist_reject_index = 0;
	}
}

extern bool oplus_check_socket_in_blacklist(int is_input, struct socket *sock) {
	int i = 0;
	uid_t uid = 0;
	struct timespec now_ts;
	if (blacklist_len > 0 && sock && sock->sk && (sock->sk->sk_family == 2 || sock->sk->sk_family == 10)){
		uid = get_uid_from_sock(sock->sk);
		if(uid == 0) {
			return false;
		}
		now_ts = current_kernel_time();
		for (i = 0; i < blacklist_len; i++) {
			if(blacklist_uid[i] == uid) {
				if (is_input != 1) {
					if (record_blacklist_reject_index >= OPLUS_MAX_RECORD_BLACK_REJECT_LEN) {
						nwpower_unsl_blacklist_reject();
					}
					if (record_blacklist_reject_index < OPLUS_MAX_RECORD_BLACK_REJECT_LEN) {
						blacklist_reject_uid[record_blacklist_reject_index*2+1] = now_ts.tv_sec * 1000 + now_ts.tv_nsec / 1000000;
						blacklist_reject_uid[record_blacklist_reject_index*2+2] = (u64)uid << 32 | is_input;
						record_blacklist_reject_index++;
					}
				}
				printk("[oplus_netcontroller] blacklist reject, stamp=%ld, is_input=%d, uid=%u",
					now_ts.tv_sec * 1000 + now_ts.tv_nsec / 1000000, is_input, uid);
				return true;
			}
		}
		return false;
	}
	return false;
}

static int nwpower_set_blacklist_uids(struct nlmsghdr *nlh) {
	u32 *data;
	int i = 0;
	data = (u32 *)NLMSG_DATA(nlh);
	memset(blacklist_uid, 0x0, KERNEL_UNSL_APP_WAKEUP_LEN * sizeof(u32));
	blacklist_len = *data;
	for (i = 0; i < blacklist_len; i++) {
		if (i >= KERNEL_UNSL_APP_WAKEUP_LEN) {
			printk("[oplus_netcontroller] uids length exceed the limit!");
			break;
		} else {
			blacklist_uid[i] = *(data + i + 1);
		}
	}
	return 0;
}

extern void oplus_match_modem_wakeup() {
	atomic_set(&qrtr_first_msg, 1);
	oplus_nw_wakeup[OPLUS_NW_MPSS]++;
	oplus_mdaci_nw_wakeup[OPLUS_NW_MPSS]++;
	snprintf(bts_net_wakeup_buffer, BTS_BUFFER_SIZE, "qmi");
}

extern void oplus_match_wlan_wakeup() {
	oplus_nw_wakeup[OPLUS_NW_WIFI]++;
	oplus_mdaci_nw_wakeup[OPLUS_NW_WIFI]++;
}

static void match_qrtr_new_service_port(int id, int port, u64 qrtr[][4]) {
	int i;
	for (i = 0; i < OPLUS_MAX_QRTR_SERVICE_LEN; ++i) {
		if (qrtr[i][0] == 0) {
			qrtr[i][0] = 1;
			qrtr[i][1] = id;
			qrtr[i][2] = port;
			//printk("[oplus_nwpower] QrtrNewService[%d]: ServiceID: %d, PortID: %d", i, id, port);
			break;
		} else {
			if (qrtr[i][1] == id && qrtr[i][2] == port) {
				//printk("[oplus_nwpower] QrtrNewService[%d]: Ignore.");
				break;
			}
		}
	}
}

static void match_qrtr_del_service_port(int id, u64 qrtr[][4]) {
	int i;
	for (i = 0; i < OPLUS_MAX_QRTR_SERVICE_LEN; ++i) {
		if (qrtr[i][0] == 1 && qrtr[i][1] == id) {
			qrtr[i][0] = 0;
			//printk("[oplus_nwpower] QrtrDelService[%d]: ServiceID: %d", i, id);
			break;
		}
	}
}

extern void oplus_match_qrtr_service_port(int type, int id, int port) {
	if (type == QRTR_TYPE_NEW_SERVER) {
		match_qrtr_new_service_port(id, port, service_wakeup_times);
		match_qrtr_new_service_port(id, port, mdaci_service_wakeup_times);
	} else if (type == QRTR_TYPE_DEL_SERVER) {
		match_qrtr_del_service_port(id, service_wakeup_times);
		match_qrtr_del_service_port(id, mdaci_service_wakeup_times);
	}
}

void bts_net_clear(void)
{
	memset(bts_net_wakeup_buffer, 0, sizeof(bts_net_wakeup_buffer));
}
bool bts_net_exist(void)
{
        int platform_id = get_cached_platform_id();
        if (platform_id == LAGOON) {
                bts_net_clear();
        }
	return bts_net_wakeup_buffer[0] == 0 ? false : true;
}
ssize_t bts_net_fill(char * desc, ssize_t size)
{
	return scnprintf(desc, size > BTS_BUFFER_SIZE ? BTS_BUFFER_SIZE : size, "999 %s\n", bts_net_wakeup_buffer);
}

static void __oplus_match_qrtr_wakeup(int src_node, int src_port, int dst_port, unsigned int arg1, unsigned int arg2, u64 qrtr[][4], u64 wakeup[OPLUS_NW_WAKEUP_SUM], bool prt) {
	int i;
	int repeat[4] = {0};
	int repeat_index = 0;
	if (atomic_read(&qrtr_wakeup_hook_boot) == 1 && atomic_read(&qrtr_first_msg) == 1) {
		for (i = 0; i < OPLUS_MAX_QRTR_SERVICE_LEN; ++i) {
			if (qrtr[i][0] == 1 && (qrtr[i][2] == src_port || qrtr[i][2] == dst_port)) {
				if (repeat_index < 4) repeat[repeat_index++] = i;
			}
		}
		if (repeat_index == 1) {
			qrtr[repeat[0]][3]++;
			if (prt)
				printk("[oplus_nwpower] QrtrWakeup: ServiceID: %d, NodeID: %d, PortID: %d, Msg: [%08x %08x], Count: %d",
					qrtr[repeat[0]][1], src_node, qrtr[repeat[0]][2], arg1, arg2, qrtr[repeat[0]][3]);
		} else if (repeat_index > 1) {
			qrtr[repeat[repeat_index-1]][3]++;
			if (prt)
				printk("[oplus_nwpower] QrtrWakeup: ServiceID: [%d/%d/%d/%d], NodeID: %d, PortID: %d, Msg: [%08x %08x], Count: %d",
					qrtr[repeat[0]][1], qrtr[repeat[1]][1],
					repeat_index > 2 ? qrtr[repeat[2]][1]:-1,
					repeat_index > 3 ? qrtr[repeat[3]][1]:-1,
					src_node, qrtr[repeat[repeat_index-1]][2], arg1, arg2, qrtr[repeat[repeat_index-1]][3]);
		} else {
			if (prt)
				printk("[oplus_nwpower] QrtrWakeup: ServiceID: %d, NodeID: %d, PortID: %d, Msg: [%08x %08x], Count: %d",
					-1, src_node, -1, arg1, arg2, -1);
		}
		wakeup[OPLUS_NW_QRTR]++;
		if (src_node == GLINK_MODEM_NODE_ID) {
			wakeup[OPLUS_NW_MD]++;

		} else if (src_node == GLINK_ADSP_NODE_ID) {
		} else if (src_node == GLINK_CDSP_NODE_ID) {
		} else if (src_node == GLINK_SLPI_NODE_ID) {
		} else if (src_node == GLINK_NPU_NODE_ID) {
		}
	}
}

extern void oplus_match_qrtr_wakeup(int src_node, int src_port, int dst_port, unsigned int arg1, unsigned int arg2) {
	__oplus_match_qrtr_wakeup(src_node, src_port, dst_port, arg1, arg2, service_wakeup_times, oplus_nw_wakeup, true);
	__oplus_match_qrtr_wakeup(src_node, src_port, dst_port, arg1, arg2, mdaci_service_wakeup_times, oplus_mdaci_nw_wakeup, false);
	atomic_set(&qrtr_first_msg, 0);
}

extern void oplus_update_qrtr_flag(int flag){
	if (atomic_read(&qrtr_wakeup_hook_boot) == 1) {
		atomic_set(&qrtr_first_msg, flag);
	}
}

static void print_qrtr_wakeup(bool unsl, u64 qrtr[][4], u64 wakeup[OPLUS_NW_WAKEUP_SUM], bool prt) {
	u64 temp[5][4] = {{0}};
	u64 max_count = 0;
	u64 max_count_id = 0;
	int j;
	int i;
	int k;
	for (j = 0; j < 5; ++j) {
		for (i = 0; i < OPLUS_MAX_QRTR_SERVICE_LEN; ++i) {
			if (qrtr[i][0] == 1 && qrtr[i][3] > max_count) {
				max_count = qrtr[i][3];
				max_count_id = i;
			}
		}
		for (k = 0;k < 4; ++k) {
			temp[j][k] = qrtr[max_count_id][k];
		}
		max_count = 0;
		qrtr[max_count_id][3] = 0;
		if (unsl) {
			if (temp[j][3] > 0) wakeup_unsl_msg[j] = temp[j][2] << 32 | ((u32)temp[j][1] << 16 | (u16)temp[j][3]);
		}
		if (temp[j][3] > 0) printk("[oplus_nwpower] QrtrWakeupMax[%d]: ServiceID: %d, PortID: %d, Count: %d",
			j, temp[j][1], temp[j][2], temp[j][3]);
	}
	if (unsl) {
		wakeup_unsl_msg[5] = (wakeup[OPLUS_NW_MD] << 48) |
			((wakeup[OPLUS_NW_QRTR] & 0xFFFF) << 32) |
			((wakeup[OPLUS_NW_WIFI] & 0xFFFF) << 16) |
			(wakeup[OPLUS_NW_MPSS] & 0xFFFF);
	}
	if (prt)
		printk("[oplus_nwpower] AllWakeups: Mpss: %d, Qrtr: %d, Modem: %d, WiFi: %d",
			wakeup[OPLUS_NW_MPSS], wakeup[OPLUS_NW_QRTR], wakeup[OPLUS_NW_MD], wakeup[OPLUS_NW_WIFI]);
}

extern void oplus_match_ipa_ip_wakeup(int type, struct sk_buff *skb) {
	struct timespec now_ts;
	struct iphdr *tmp_v4iph;
	struct ipv6hdr *tmp_v6iph;
	tcp_input_sch_work = false;
	if (atomic_read(&ipa_wakeup_hook_boot) == 1) {
		if (atomic_read(&tcp_is_input) == 0) {
			now_ts = current_kernel_time();
			if (((now_ts.tv_sec * 1000 + now_ts.tv_nsec / 1000000) - (tcp_last_transmission_stamp.tv_sec * 1000 + tcp_last_transmission_stamp.tv_nsec / 1000000)) > OPLUS_TRANSMISSION_INTERVAL) {
				if (type == OPLUS_TCP_TYPE_V4) {
					tmp_v4iph = ip_hdr(skb);
					atomic_set(&tcp_is_input, OPLUS_TCP_TYPE_V4);
					tcp_input_list.ipv4_addr = tmp_v4iph->saddr;
					tcp_input_list.is_ipv6 = false;
					mdaci_tcp_input_list.ipv4_addr = tcp_input_list.ipv4_addr;
					mdaci_tcp_input_list.is_ipv6 = false;
				} else {
					tmp_v6iph = ipv6_hdr(skb);
					atomic_set(&tcp_is_input, OPLUS_TCP_TYPE_V6);
					tcp_input_list.ipv6_addr1 = (u64)ntohl(tmp_v6iph->saddr.s6_addr32[0]) << 32 | ntohl(tmp_v6iph->saddr.s6_addr32[1]);
					tcp_input_list.ipv6_addr2 = (u64)ntohl(tmp_v6iph->saddr.s6_addr32[2]) << 32 | ntohl(tmp_v6iph->saddr.s6_addr32[3]);
					tcp_input_list.is_ipv6 = true;
					mdaci_tcp_input_list.ipv6_addr1 = tcp_input_list.ipv6_addr1;
					mdaci_tcp_input_list.ipv6_addr2 = tcp_input_list.ipv6_addr2;
					mdaci_tcp_input_list.is_ipv6 = true;
				}
				tcp_input_list.uid = 0;
				tcp_input_list.pid = 0;
				mdaci_tcp_input_list.uid = 0;
				mdaci_tcp_input_list.pid = 0;
			}
		}
		tcp_last_transmission_stamp = current_kernel_time();
	}
}

extern void oplus_match_ipa_tcp_wakeup(int type, struct sock *sk) {
	if (atomic_read(&ipa_wakeup_hook_boot) == 1) {
		if (atomic_read(&tcp_is_input) == type && !tcp_input_sch_work) {
			if (sk->sk_state != TCP_TIME_WAIT) {
				tcp_input_list.uid = get_uid_from_sock(sk);
				tcp_input_list.pid = sk->sk_oplus_pid;
				mdaci_tcp_input_list.uid = tcp_input_list.uid;
				mdaci_tcp_input_list.pid = tcp_input_list.pid;
			}
			schedule_work(&tcp_input_hook_work);
			tcp_input_sch_work = true;
		}
		sk->oplus_last_rcv_stamp[0] = sk->oplus_last_rcv_stamp[1];
		sk->oplus_last_rcv_stamp[1] = tcp_last_transmission_stamp.tv_sec * 1000 + tcp_last_transmission_stamp.tv_nsec / 1000000;
	}
}

extern void oplus_ipa_schedule_work() {
	if (atomic_read(&ipa_wakeup_hook_boot) == 1 && atomic_read(&tcp_is_input) == 1 && !tcp_input_sch_work) {
		schedule_work(&tcp_input_hook_work);
		tcp_input_sch_work = true;
	}
}

extern void oplus_match_tcp_output(struct sock *sk) {
	struct timespec now_ts;
	if (atomic_read(&ipa_wakeup_hook_boot) == 1) {
		if (atomic_read(&tcp_is_input) == 0) {
			now_ts = current_kernel_time();
			if (((now_ts.tv_sec * 1000 + now_ts.tv_nsec / 1000000) - (tcp_last_transmission_stamp.tv_sec * 1000 + tcp_last_transmission_stamp.tv_nsec / 1000000)) > OPLUS_TRANSMISSION_INTERVAL) {
				atomic_set(&tcp_is_input, 3);
				if (sk->sk_v6_daddr.s6_addr32[0] == 0 && sk->sk_v6_daddr.s6_addr32[1] == 0) {
					tcp_output_list.ipv4_addr = sk->sk_daddr;
					tcp_output_list.is_ipv6 = false;
					mdaci_tcp_output_list.ipv4_addr = tcp_output_list.ipv4_addr;
					mdaci_tcp_output_list.is_ipv6 = false;
				} else {
					tcp_output_list.ipv6_addr1 = (u64)ntohl(sk->sk_v6_daddr.s6_addr32[0]) << 32 | ntohl(sk->sk_v6_daddr.s6_addr32[1]);
					tcp_output_list.ipv6_addr2 = (u64)ntohl(sk->sk_v6_daddr.s6_addr32[2]) << 32 | ntohl(sk->sk_v6_daddr.s6_addr32[3]);
					tcp_output_list.is_ipv6 = true;
					mdaci_tcp_output_list.ipv6_addr1 = tcp_output_list.ipv6_addr1;
					mdaci_tcp_output_list.ipv6_addr2 = tcp_output_list.ipv6_addr2;
					mdaci_tcp_output_list.is_ipv6 = true;
				}
				tcp_output_list.uid = get_uid_from_sock(sk);
				tcp_output_list.pid = sk->sk_oplus_pid;
				mdaci_tcp_output_list.uid = tcp_output_list.uid;
				mdaci_tcp_output_list.pid = tcp_output_list.pid;
				schedule_work(&tcp_output_hook_work);
			}
		}
		tcp_last_transmission_stamp = current_kernel_time();
		sk->oplus_last_send_stamp[0] = sk->oplus_last_send_stamp[1];
		sk->oplus_last_send_stamp[1] = tcp_last_transmission_stamp.tv_sec * 1000 + tcp_last_transmission_stamp.tv_nsec / 1000000;
	}
}

extern void oplus_match_tcp_input_retrans(struct sock *sk) {
	struct timespec now_ts;
	if (atomic_read(&tcpsynretrans_hook_boot) == 1) {
		now_ts = current_kernel_time();
		if (((now_ts.tv_sec * 1000 + now_ts.tv_nsec / 1000000) - sk->oplus_last_rcv_stamp[0]) > OPLUS_TCP_RETRANSMISSION_INTERVAL) {
			if (sk->sk_v6_daddr.s6_addr32[0] == 0 && sk->sk_v6_daddr.s6_addr32[1] == 0) {
				tcp_input_retrans_list.ipv4_addr = sk->sk_daddr;
				tcp_input_retrans_list.is_ipv6 = false;
				mdaci_tcp_input_retrans_list.ipv4_addr = tcp_input_retrans_list.ipv4_addr;
				mdaci_tcp_input_retrans_list.is_ipv6 = false;
			} else {
				tcp_input_retrans_list.ipv6_addr1 = (u64)ntohl(sk->sk_v6_daddr.s6_addr32[0]) << 32 | ntohl(sk->sk_v6_daddr.s6_addr32[1]);
				tcp_input_retrans_list.ipv6_addr2 = (u64)ntohl(sk->sk_v6_daddr.s6_addr32[2]) << 32 | ntohl(sk->sk_v6_daddr.s6_addr32[3]);
				tcp_input_retrans_list.is_ipv6 = true;
				mdaci_tcp_input_retrans_list.ipv6_addr1 = tcp_input_retrans_list.ipv6_addr1;
				mdaci_tcp_input_retrans_list.ipv6_addr2 = tcp_input_retrans_list.ipv6_addr2;
				mdaci_tcp_input_retrans_list.is_ipv6 = true;
			}
			tcp_input_retrans_list.uid = get_uid_from_sock(sk);
			tcp_input_retrans_list.pid = sk->sk_oplus_pid;
			mdaci_tcp_input_retrans_list.uid = tcp_input_retrans_list.uid;
			mdaci_tcp_input_retrans_list.pid = tcp_input_retrans_list.pid;
			schedule_work(&tcp_input_tcpsynretrans_hook_work);
		}
	}
}

extern void oplus_match_tcp_output_retrans(struct sock *sk) {
	struct timespec now_ts;
	if (atomic_read(&tcpsynretrans_hook_boot) == 1) {
		now_ts = current_kernel_time();
		if (((now_ts.tv_sec * 1000 + now_ts.tv_nsec / 1000000) - sk->oplus_last_send_stamp[0]) > OPLUS_TCP_RETRANSMISSION_INTERVAL) {
			if (sk->sk_v6_daddr.s6_addr32[0] == 0 && sk->sk_v6_daddr.s6_addr32[1] == 0) {
				tcp_output_retrans_list.ipv4_addr = sk->sk_daddr;
				tcp_output_retrans_list.is_ipv6 = false;
				mdaci_tcp_output_retrans_list.ipv4_addr = tcp_output_retrans_list.ipv4_addr;
				mdaci_tcp_output_retrans_list.is_ipv6 = false;
			} else {
				tcp_output_retrans_list.ipv6_addr1 = (u64)ntohl(sk->sk_v6_daddr.s6_addr32[0]) << 32 | ntohl(sk->sk_v6_daddr.s6_addr32[1]);
				tcp_output_retrans_list.ipv6_addr2 = (u64)ntohl(sk->sk_v6_daddr.s6_addr32[2]) << 32 | ntohl(sk->sk_v6_daddr.s6_addr32[3]);
				tcp_output_retrans_list.is_ipv6 = true;
				mdaci_tcp_output_retrans_list.ipv6_addr1 = tcp_output_retrans_list.ipv6_addr1;
				mdaci_tcp_output_retrans_list.ipv6_addr2 = tcp_output_retrans_list.ipv6_addr2;
				mdaci_tcp_output_retrans_list.is_ipv6 = true;
			}
			tcp_output_retrans_list.uid = get_uid_from_sock(sk);
			tcp_output_retrans_list.pid = sk->sk_oplus_pid;
			mdaci_tcp_output_retrans_list.uid = tcp_output_retrans_list.uid;
			mdaci_tcp_output_retrans_list.pid = tcp_output_retrans_list.pid;
			schedule_work(&tcp_output_tcpsynretrans_hook_work);
		}
	}
}

static void tcp_hook_insert_sort(struct tcp_hook_struct *pval) {
	int i;
	int j;
	u64 count = 0;
	u64 temp_sort[3] = {0};
	//Insert sort
	for (i = 1; i < OPLUS_MAX_RECORD_IP_LEN; ++i) {
		temp_sort[0] = pval->set[3*i];
		temp_sort[1] = pval->set[3*i+1];
		temp_sort[2] = pval->set[3*i+2];
		//If IPv4
		if (temp_sort[0] == 0 && temp_sort[1] == 0 && temp_sort[2] != 0) {
			count = (temp_sort[2] & 0xFFFC000000000000) >> 50;
		} else {
			count = temp_sort[2] & 0xFFFFFFFF;
		}
		j = i - 1;
		while (j >= 0) {
			if (pval->set[3*j] == 0 && pval->set[3*j+1] == 0 && pval->set[3*j+2] != 0) {
				if (count > (pval->set[3*j+2] & 0xFFFC000000000000) >> 50) {
					pval->set[3*(j+1)] = pval->set[3*j];
					pval->set[3*(j+1)+1] = pval->set[3*j+1];
					pval->set[3*(j+1)+2] = pval->set[3*j+2];
					--j;
				} else {
					break;
				}
			} else {
				if (count > (pval->set[3*j+2] & 0xFFFFFFFF)) {
					pval->set[3*(j+1)] = pval->set[3*j];
					pval->set[3*(j+1)+1] = pval->set[3*j+1];
					pval->set[3*(j+1)+2] = pval->set[3*j+2];
					--j;
				} else {
					break;
				}
			}
		}
		pval->set[3*(j+1)] = temp_sort[0];
		pval->set[3*(j+1)+1] = temp_sort[1];
		pval->set[3*(j+1)+2] = temp_sort[2];
	}
}

static int match_tcp_hook(struct tcp_hook_struct *pval) {
	int i;
	u32 uid = 0;
	u64 count = 0;
	bool handle = false;

	for (i = 0; i < OPLUS_MAX_RECORD_IP_LEN; ++i) {
		if (pval->is_ipv6) {
			if (pval->ipv6_addr1 == pval->set[3*i] && pval->ipv6_addr2 == pval->set[3*i+1]) {
				count = pval->set[3*i+2] & 0xFFFFFFFF;
				uid = (pval->set[3*i+2] & 0xFFFFFFFF00000000) >> 32;
				if (uid == 0) {
					pval->set[3*i+2] = (u64)(pval->uid) << 32 | (u32)(++count);
				} else {
					pval->set[3*i+2] = (pval->set[3*i+2] & 0xFFFFFFFF00000000) | (u32)(++count);
				}
				handle = true;
				break;
			} else if (pval->set[3*i+2] == 0) {
				pval->set[3*i] = pval->ipv6_addr1;
				pval->set[3*i+1] = pval->ipv6_addr2;
				count = 1;
				pval->set[3*i+2] = (u64)(pval->uid) << 32 | (u32)(count);
				handle = true;
				break;
			}
		} else {
			if (pval->ipv4_addr == (pval->set[3*i+2] & 0xFFFFFFFF)) {
				count = (pval->set[3*i+2] & 0xFFFC000000000000) >> 50;
				uid = (pval->set[3*i+2] & 0x3FFFF00000000) >> 32;
				if (uid == 0) {
					count++;
					count = count << 18 | (pval->uid & 0x3FFFF);
					pval->set[3*i+2] = count << 32 | (pval->set[3*i+2] & 0xFFFFFFFF);
				} else {
					pval->set[3*i+2] = (++count) << 50 | (pval->set[3*i+2] & 0x3FFFFFFFFFFFF);
				}
				handle = true;
				break;
			} else if (pval->set[3*i+2] == 0) {
				count = 1 << 18 | (pval->uid & 0x3FFFF);
				pval->set[3*i+2] = count << 32 | pval->ipv4_addr;
				handle = true;
				break;
			}
		}
	}
	if (!handle) {
		tcp_hook_insert_sort(pval);
		i = OPLUS_MAX_RECORD_IP_LEN - 1;
		if (pval->is_ipv6) {
			pval->set[3*i] = pval->ipv6_addr1;
			pval->set[3*i+1] = pval->ipv6_addr2;
			count = 1;
			pval->set[3*i+2] = (u64)(pval->uid) << 32 | (u32)(count);
		} else {
			pval->set[3*i] = 0;
			pval->set[3*i+1] = 0;
			count = 1 << 18 | (pval->uid & 0x3FFFF);
			pval->set[3*i+2] = (u64)count << 32 | pval->ipv4_addr;
		}
	}

	return i;
}

static bool tcp_monitor_check_uid_in_whitelist(int uid) {
	int i = 0;
	if (blacklist_len > 0) {
		for (i = 0; i < blacklist_len; i++) {
			if (i >= KERNEL_UNSL_APP_WAKEUP_LEN) {
				printk("[oplus_netcontroller] uids length exceed the limit!");
				return true;
			}
			if(blacklist_uid[i] == uid) {
				return false;
			}
		}
	}
	return true;
}

static void nwpower_unsl_app_wakeup()
{
	nwpower_send_to_user(NW_POWER_REPORT_APP_WAKEUP, (char*)app_wakeup_monitor_list.set, sizeof(app_wakeup_monitor_list.set));
	app_wakeup_monitor_list.count = 0;
	memset(app_wakeup_monitor_list.set, 0x0, sizeof(app_wakeup_monitor_list.set));
}


static void app_wakeup_monitor(struct tcp_hook_simple_struct *pval, bool is_block, bool is_input, int pid, int uid) {
	struct timespec now_ts = current_kernel_time();
	int block = is_block ? 1:0;
	int input = is_input ? 1:0;
	int whitelist = tcp_monitor_check_uid_in_whitelist(uid) ? 1:0;
	if (pval->count < OPLUS_MAX_RECORD_APP_WAKEUP_LEN) {
		pval->set[1+pval->count*3] = (u64)pid << 32 | uid;
		pval->set[1+pval->count*3+1] =
			((u64)block << 48) |
			((u64)(input & 0xFFFF) << 32) |
			((u64)(whitelist & 0xFFFF) << 16) |
			(0xFFFF);
		pval->set[1+pval->count*3+2] = now_ts.tv_sec * 1000 + now_ts.tv_nsec / 1000000;
		/*
		printk("[oplus_nwpower] count:%d, block:%d, input:%d, pid:%d, uid:%d, stamp:%ld",
			pval->set[0], block, input, pid, uid, pval->set[1+pval->count*3+2]);
		*/
		pval->set[0] = (++pval->count);
	} else {
		printk("[oplus_nwpower] warning! OPLUS_MAX_RECORD_APP_WAKEUP_LEN reached");
	}
}

static void tcp_output_hook_work_callback(struct work_struct *work) {
	int i = match_tcp_hook(&tcp_output_list);
	match_tcp_hook(&mdaci_tcp_output_list);
	app_wakeup_monitor(&app_wakeup_monitor_list, false, false, tcp_output_list.pid, tcp_output_list.uid);
	app_wakeup_monitor(&mdaci_app_wakeup_monitor_list, false, false, tcp_output_list.pid, tcp_output_list.uid);
	if (tcp_output_list.is_ipv6) {
		printk("[oplus_nwpower] IPAOutputWakeup: [%ld,****], %d, %d, %d",
			tcp_output_list.ipv6_addr1,
			tcp_output_list.pid, tcp_output_list.uid, tcp_output_list.set[3*i+2] & 0xFFFFFFFF);
			snprintf(bts_net_wakeup_buffer, BTS_BUFFER_SIZE, "ipa");
	} else {
		printk("[oplus_nwpower] IPAOutputWakeup: %#X, %d, %d, %d",
			tcp_output_list.ipv4_addr & 0xFFFFFF, tcp_output_list.pid, tcp_output_list.uid,
			(tcp_output_list.set[3*i+2] & 0xFFFC000000000000) >> 50);
			snprintf(bts_net_wakeup_buffer, BTS_BUFFER_SIZE, "ipa");
	}
	atomic_set(&tcp_is_input, 0);
}

static void tcp_input_hook_work_callback(struct work_struct *work) {
	int i = match_tcp_hook(&tcp_input_list);
	match_tcp_hook(&mdaci_tcp_input_list);
	app_wakeup_monitor(&app_wakeup_monitor_list, false, true, tcp_input_list.pid, tcp_input_list.uid);
	app_wakeup_monitor(&mdaci_app_wakeup_monitor_list, false, true, tcp_input_list.pid, tcp_input_list.uid);
	#ifdef OPLUS_FEATURE_POWERINFO_STANDBY
	wakeup_reasons_statics(IRQ_NAME_MODEM_IPA, WS_CNT_MODEM);
	#endif /* OPLUS_FEATURE_POWERINFO_STANDBY */
	if (tcp_input_list.is_ipv6) {
		printk("[oplus_nwpower] IPAInputWakeup: [%ld,****], %d, %d, %d",
			tcp_input_list.ipv6_addr1,
			tcp_input_list.pid, tcp_input_list.uid, tcp_input_list.set[3*i+2] & 0xFFFFFFFF);
			snprintf(bts_net_wakeup_buffer, BTS_BUFFER_SIZE, "ipa");
	} else {
		printk("[oplus_nwpower] IPAInputWakeup: %#X, %d, %d, %d",
			tcp_input_list.ipv4_addr & 0xFFFFFF, tcp_input_list.pid, tcp_input_list.uid,
			(tcp_input_list.set[3*i+2] & 0xFFFC000000000000) >> 50);
			snprintf(bts_net_wakeup_buffer, BTS_BUFFER_SIZE, "ipa");
	}
	atomic_set(&tcp_is_input, 0);
}

static void tcp_output_tcpsynretrans_hook_work_callback(struct work_struct *work) {
	int i = match_tcp_hook(&tcp_output_retrans_list);
	match_tcp_hook(&mdaci_tcp_output_retrans_list);
	if (tcp_output_retrans_list.is_ipv6) {
		if (tcp_output_retrans_list.ipv6_addr1 == 0 && tcp_output_retrans_list.ipv6_addr2 == 0)
			return;
		printk("[oplus_nwpower] TCPOutputRetrans: [%ld,****], %d, %d, %d",
			tcp_output_retrans_list.ipv6_addr1,
			tcp_output_retrans_list.pid, tcp_output_retrans_list.uid, tcp_output_retrans_list.set[3*i+2] & 0xFFFFFFFF);
	} else {
		if (tcp_output_retrans_list.ipv4_addr == 0)
			return;
		printk("[oplus_nwpower] TCPOutputRetrans: %#X, %d, %d, %d",
			tcp_output_retrans_list.ipv4_addr & 0xFFFFFF, tcp_output_retrans_list.pid, tcp_output_retrans_list.uid,
			(tcp_output_retrans_list.set[3*i+2] & 0xFFFC000000000000) >> 50);
	}
}

static void tcp_input_tcpsynretrans_hook_work_callback(struct work_struct *work) {
	int i = match_tcp_hook(&tcp_input_retrans_list);
	match_tcp_hook(&mdaci_tcp_input_retrans_list);
	if (tcp_input_retrans_list.is_ipv6) {
		if (tcp_input_retrans_list.ipv6_addr1 == 0 && tcp_input_retrans_list.ipv6_addr2 == 0)
			return;
		printk("[oplus_nwpower] TCPInputRetrans: [%ld,****], %d, %d, %d",
			tcp_input_retrans_list.ipv6_addr1,
			tcp_input_retrans_list.pid, tcp_input_retrans_list.uid, tcp_input_retrans_list.set[3*i+2] & 0xFFFFFFFF);
	} else {
		if (tcp_input_retrans_list.ipv4_addr == 0)
			return;
		printk("[oplus_nwpower] TCPInputRetrans: %#X, %d, %d, %d",
			tcp_input_retrans_list.ipv4_addr & 0xFFFFFF, tcp_input_retrans_list.pid, tcp_input_retrans_list.uid,
			(tcp_input_retrans_list.set[3*i+2] & 0xFFFC000000000000) >> 50);
	}
}

static int print_tcp_wakeup(const char *type, struct tcp_hook_struct *pval, bool prt) {
	int i;
	u32 count;
	u32 tcp_wakeup_times = 0;
	tcp_hook_insert_sort(pval);
	for (i = 0; i < 5;++i) {
		if (pval->set[3*i] == 0 && pval->set[3*i+1] == 0 && pval->set[3*i+2] != 0) {
			count = (pval->set[3*i+2] & 0xFFFC000000000000) >> 50;
			if (prt && count > 0) {
				printk("[oplus_nwpower] IPA%sMAX[%d]: %#X, %d, %d",
					type, i, (pval->set[3*i+2] & 0xFFFFFFFF) & 0xFFFFFF,
					(pval->set[3*i+2] & 0x3FFFF00000000) >> 32, count);
			}
		} else {
			count = pval->set[3*i+2] & 0xFFFFFFFF;
			if (prt && count > 0) {
				printk("[oplus_nwpower] IPA%sMAX[%d]: [%ld,****], %d, %d",
					type, i, pval->set[3*i],
					pval->set[3*i+2] >> 32, count);
			}
		}
	}
	for (i = 0; i < OPLUS_MAX_RECORD_IP_LEN;++i) {
		if (pval->set[3*i] == 0 && pval->set[3*i+1] == 0 && pval->set[3*i+2] != 0) {
			count = (pval->set[3*i+2] & 0xFFFC000000000000) >> 50;
		} else {
			count = pval->set[3*i+2] & 0xFFFFFFFF;
		}
		if (count > 0) {
			tcp_wakeup_times += count;
		}
	}
	return tcp_wakeup_times;
}

static void print_ipa_wakeup(bool unsl, struct tcp_hook_struct *ptcp_in, struct tcp_hook_struct *ptcp_out, struct tcp_hook_struct *ptcp_re_in, struct tcp_hook_struct *ptcp_re_out, u64 wakeup[OPLUS_NW_WAKEUP_SUM], bool prt) {
	wakeup[OPLUS_NW_TCP_IN] = print_tcp_wakeup("Input", ptcp_in, prt);
	wakeup[OPLUS_NW_TCP_OUT] = print_tcp_wakeup("Output", ptcp_out, prt);
	wakeup[OPLUS_NW_TCP_RE_IN] = print_tcp_wakeup("InputRetrans", ptcp_re_in, prt);
	wakeup[OPLUS_NW_TCP_RE_OUT] = print_tcp_wakeup("OutputRetrans", ptcp_re_out, prt);
	if (prt)
		printk("[oplus_nwpower] IPAAllWakeups: TCPInput: %d, TCPOutput: %d, TCPInputRetrans: %d, TCPOutputRetrans: %d",
			wakeup[OPLUS_NW_TCP_IN], wakeup[OPLUS_NW_TCP_OUT], wakeup[OPLUS_NW_TCP_RE_IN], wakeup[OPLUS_NW_TCP_RE_OUT]);
	if (unsl) {
		wakeup_unsl_msg[6] = (wakeup[OPLUS_NW_TCP_IN] << 48) |
			((wakeup[OPLUS_NW_TCP_OUT] & 0xFFFF) << 32) |
			((wakeup[OPLUS_NW_TCP_RE_IN] & 0xFFFF) << 16) |
			(wakeup[OPLUS_NW_TCP_RE_OUT] & 0xFFFF);
	}
}

static void reset_count(u64 qrtr[][4], struct tcp_hook_struct *ptcp_in, struct tcp_hook_struct *ptcp_out,
	struct tcp_hook_struct *ptcp_re_in, struct tcp_hook_struct *ptcp_re_out, u64 wakeup[OPLUS_NW_WAKEUP_SUM]) {
	int i;
	int j;
	for (i = 0; i < OPLUS_MAX_QRTR_SERVICE_LEN; ++i) {
		if (qrtr[i][0] == 1) {
			qrtr[i][3] = 0;
		}
	}
	for (i = 0; i < OPLUS_MAX_RECORD_IP_LEN; ++i) {
		for (j = 0; j < 3; ++j) {
			ptcp_in->set[i+j] = 0;
			ptcp_out->set[i+j] = 0;
			ptcp_re_in->set[i+j] = 0;
			ptcp_re_out->set[i+j] = 0;
		}
	}
	for (i = 0; i < OPLUS_NW_WAKEUP_SUM; ++i) {
		wakeup[i] = 0;
	}

}

static void nwpower_hook_on() {
	atomic_set(&qrtr_wakeup_hook_boot, 1);
	atomic_set(&ipa_wakeup_hook_boot, 1);
	atomic_set(&tcpsynretrans_hook_boot, 1);
}

static void nwpower_hook_off(bool unsl) {
	atomic_set(&qrtr_wakeup_hook_boot, 0);
	atomic_set(&ipa_wakeup_hook_boot, 0);
	atomic_set(&tcpsynretrans_hook_boot, 0);
	print_qrtr_wakeup(unsl, service_wakeup_times, oplus_nw_wakeup, true);
	print_ipa_wakeup(unsl, &tcp_input_list, &tcp_output_list, &tcp_input_retrans_list, &tcp_output_retrans_list, oplus_nw_wakeup, true);
	reset_count(service_wakeup_times, &tcp_input_list, &tcp_output_list, &tcp_input_retrans_list, &tcp_output_retrans_list, oplus_nw_wakeup);
	app_wakeup_monitor_list.count = 0;
	memset(app_wakeup_monitor_list.set, 0x0, sizeof(app_wakeup_monitor_list.set));
	if (unsl) {
		nwpower_send_to_user(NW_POWER_UNSL_MONITOR, (char*)wakeup_unsl_msg, sizeof(wakeup_unsl_msg));
		memset(wakeup_unsl_msg, 0x0, sizeof(wakeup_unsl_msg));
	}
}

static void nwpower_unsl_mdaci() {
	print_qrtr_wakeup(true, mdaci_service_wakeup_times, oplus_mdaci_nw_wakeup, true);
	print_ipa_wakeup(true, &mdaci_tcp_input_list, &mdaci_tcp_output_list,
					&mdaci_tcp_input_retrans_list, &mdaci_tcp_output_retrans_list, oplus_mdaci_nw_wakeup, true);
	reset_count(mdaci_service_wakeup_times, &mdaci_tcp_input_list, &mdaci_tcp_output_list,
				&mdaci_tcp_input_retrans_list, &mdaci_tcp_output_retrans_list, oplus_mdaci_nw_wakeup);
	nwpower_send_to_user(NW_POWER_REPORT_MDACI, (char*)wakeup_unsl_msg, sizeof(wakeup_unsl_msg));
	memset(wakeup_unsl_msg, 0x0, sizeof(wakeup_unsl_msg));
	nwpower_send_to_user(NW_POWER_REPORT_MDACI_APP_WAKEUP, (char*)mdaci_app_wakeup_monitor_list.set, sizeof(mdaci_app_wakeup_monitor_list.set));
	mdaci_app_wakeup_monitor_list.count = 0;
	memset(mdaci_app_wakeup_monitor_list.set, 0x0, sizeof(mdaci_app_wakeup_monitor_list.set));
}

static int nwpower_send_to_user(int msg_type,char *msg_data, int msg_len) {
	int ret = 0;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	if (oplus_nwpower_pid == 0) {
		printk("[oplus_nwpower] netlink: oplus_nwpower_pid = 0.\n");
		return -1;
	}
	skb = alloc_skb(NLMSG_SPACE(msg_len), GFP_ATOMIC);
	if (skb == NULL) {
		printk("[oplus_nwpower] netlink: alloc_skb failed.\n");
		return -2;
	}
	nlh = nlmsg_put(skb, 0, 0, msg_type, NLMSG_ALIGN(msg_len), 0);
	if (nlh == NULL) {
		printk("[oplus_nwpower] netlink: nlmsg_put failed.\n");
		nlmsg_free(skb);
		return -3;
	}
	nlh->nlmsg_len = NLMSG_HDRLEN + NLMSG_ALIGN(msg_len);
	if(msg_data != NULL) {
		memcpy((char*)NLMSG_DATA(nlh), msg_data, msg_len);
	}
	NETLINK_CB(skb).portid = 0;
	NETLINK_CB(skb).dst_group = 0;
	ret = netlink_unicast(oplus_nwpower_sock, skb, oplus_nwpower_pid, MSG_DONTWAIT);
	if(ret < 0) {
		printk(KERN_ERR "[oplus_nwpower] netlink: netlink_unicast failed, ret = %d.\n",ret);
		return -4;
	}
	return 0;
}

static int nwpower_netlink_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh, struct netlink_ext_ack *extack) {
	int ret = 0;
	switch (nlh->nlmsg_type) {
	case NW_POWER_ANDROID_PID:
		oplus_nwpower_pid = NETLINK_CB(skb).portid;
		printk("[oplus_nwpower] netlink: oplus_nwpower_pid = %d", oplus_nwpower_pid);
		break;
	case NW_POWER_BOOT_MONITOR:
		nwpower_hook_on();
		printk("[oplus_nwpower] netlink: hook_on");
		break;
	case NW_POWER_STOP_MONITOR:
		nwpower_hook_off(false);
		printk("[oplus_nwpower] netlink: hook_off");
		break;
	case NW_POWER_STOP_MONITOR_UNSL:
		nwpower_hook_off(true);
		printk("[oplus_nwpower] netlink: hook_off_unsl");
		break;
	case NW_POWER_BLACK_LIST:
		ret = nwpower_set_blacklist_uids(nlh);
		break;
	case NW_POWER_REQUEST_BLACK_REJECT:
		nwpower_unsl_blacklist_reject();
		printk("[oplus_nwpower] netlink: unsl_blacklist_reject");
		break;
	case NW_POWER_REQUEST_APP_WAKEUP:
		nwpower_unsl_app_wakeup();
		printk("[oplus_nwpower] netlink: unsl_app_wakeup");
		break;
	case NW_POWER_REQUEST_MDACI:
		nwpower_unsl_mdaci();
		printk("[oplus_nwpower] netlink: unsl_mdaci");
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static void nwpower_netlink_rcv(struct sk_buff *skb) {
	mutex_lock(&netlink_mutex);
	netlink_rcv_skb(skb, &nwpower_netlink_rcv_msg);
	mutex_unlock(&netlink_mutex);
}

static int nwpower_netlink_init(void) {
	struct netlink_kernel_cfg cfg = {
		.input = nwpower_netlink_rcv,
	};
	oplus_nwpower_sock = netlink_kernel_create(&init_net, NETLINK_OPLUS_NWPOWERSTATE, &cfg);
	return oplus_nwpower_sock == NULL ? -ENOMEM : 0;
}

static void nwpower_netlink_exit(void) {
	netlink_kernel_release(oplus_nwpower_sock);
	oplus_nwpower_sock = NULL;
}

static int __init nwpower_init(void) {
	int ret = 0;
	ret = nwpower_netlink_init();
	if (ret < 0) {
		printk("[oplus_nwpower] netlink: failed to init netlink.\n");
	} else {
		printk("[oplus_nwpower] netlink: init netlink successfully.\n");
	}
	return ret;
}

static void __exit nwpower_fini(void) {
	nwpower_netlink_exit();
}

module_init(nwpower_init);
module_exit(nwpower_fini);
