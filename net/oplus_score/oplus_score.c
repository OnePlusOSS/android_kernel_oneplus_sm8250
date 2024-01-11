/***********************************************************
** Copyright (C), 2008-2019, oplus Mobile Comm Corp., Ltd.
** File: oplus_score.c
** Description: Add for kernel data info send to user space.
**
** Version: 1.0
** Date : 2019/10/02
**
** ------------------ Revision History:------------------------
** <author> <data> <version > <desc>
****************************************************************/

#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/kernel.h>
#include <net/route.h>
#include <net/ip.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/version.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/random.h>
#include <net/dst.h>
#include <linux/file.h>
#include <net/tcp_states.h>
#include <linux/netlink.h>
#include <net/genetlink.h>
#include <linux/netfilter_ipv4.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/inet_connection_sock.h>
#include <linux/spinlock.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include <linux/preempt.h>

#define IFNAME_LEN 16
#define IPV4ADDRTOSTR(addr) \
((unsigned char *)&addr)[0], \
((unsigned char *)&addr)[1], \
((unsigned char *)&addr)[2], \
((unsigned char *)&addr)[3]

#define	OPLUS_TRUE	1
#define	OPLUS_FALSE	0
#define REPORT_PERIOD 1
#define SCORE_WINDOW 3
#define OPLUS_UPLINK 1
#define OPLUS_DOWNLINK 0
#define DIR_INGRESS 0
#define DIR_EGRESS 1
#define LINUX_KERNEL_510	((LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 256)))


struct link_score_msg_st
{
	u32 link_index;
	s32 uplink_score;
	s32 downlink_score;
	u32 uplink_srtt;
	u32 uplink_packets;
	u32 uplink_retrans_packets;
	u32 uplink_retrans_rate;
	u32 downlink_srtt;
	u32 downlink_packets;
	u32 downlink_retrans_packets;
	u32 downlink_retrans_rate;
	u32 downlink_rate;
	u32 uplink_udp_packets;
	u32 downlink_udp_packets;
	u32 uplink_tcp_rate;
	u32 uplink_udp_rate;
	u32 downlink_tcp_rate;
	u32 downlink_udp_rate;
	u32 uid;
};

struct score_param_st
{
	u32 score_debug;
	u32 threshold_retansmit;
	u32 threshold_normal;
	u32 smooth_factor;
	u32 protect_score;
	u32 threshold_gap;
};

struct tcp_sample_st
{
	u8  acked;
	u16 proto;
	u32 saddr;
	u32 daddr;
	u16 sport;
	u16 dport;
	u32 seq;
	u32 tstamp;
};

struct uplink_score_info_st{
	u32 link_index;
	u32 uid;
	u32 uplink_rtt_stamp;
	u32 uplink_retrans_packets;
	u32 uplink_packets;
	u32 uplink_udp_packets;
	u32 uplink_udp_rate;
	u32 uplink_tcp_rate;
	s32 uplink_score_save[SCORE_WINDOW];
	u32 uplink_score_index;
	u32 uplink_srtt;
	u32 uplink_rtt_num;
	u32 seq;
	s32 uplink_score_count;
	u32 uplink_nodata_count;
	struct tcp_sample_st sample;
	char ifname[IFNAME_LEN];
};

struct downlink_score_info_st{
	u32 link_index;
	u32 uid;
	u32 downlink_update_stamp;
	u32 downlink_retrans_packets;
	u32 downlink_packets;
	u32 downlink_udp_packets;
	u32 downlink_udp_rate;
	u32 downlink_tcp_rate;
	s32 downlink_score_save[SCORE_WINDOW];
	u32 downlink_score_index;
	u32 downlink_srtt;
	u32 downlink_rtt_num;
	u32 seq;
	s32 downlink_score_count;
	u32 downlink_nodata_count;
	char ifname[IFNAME_LEN];
};

#define MAX_LINK_SCORE 100
#define MAX_LINK_NUM 4
#define MAX_LINK_APP_NUM 16
#define FOREGROUND_UID_MAX_NUM 4

static int oplus_score_link_num = 0;
static int oplus_score_uplink_num = 0;
static int oplus_score_downlink_num = 0;
static int oplus_score_enable_flag = 1;
static u32 oplus_score_foreground_uid[FOREGROUND_UID_MAX_NUM];
static u32 oplus_score_active_link[MAX_LINK_NUM];
static u32 oplus_score_user_pid = 0;
static spinlock_t uplink_score_lock;
static struct uplink_score_info_st uplink_score_info[MAX_LINK_APP_NUM];
static spinlock_t downlink_score_lock;
static struct downlink_score_info_st downlink_score_info[MAX_LINK_APP_NUM];
static struct score_param_st oplus_score_param_info;
static struct ctl_table_header *oplus_score_table_hrd = NULL;
static struct timer_list oplus_score_report_timer;
static int oplus_score_debug = 0;
static u32 para_rtt = 8;
static u32 para_rate = 8;
static u32 para_loss = 16;

#define WORST_VIDEO_RTT_THRESH 1000
#define WORST_RTT_THRESH 400
#define WORSE_RTT_THRESH 300
#define NORMAL_RTT_THRESH 200
#define BAD_GAME_RTT_THREAD 170
#define WORST_BASE_SCORE 59
#define WORSE_BASE_SCORE 70
#define NORMAL_BASE_SCORE 80
#define GOOD_BASE_SCORE  100

/*for test*/
static u32 test_link_index = 0;
#define PACKET_PER_SEC 240
#define VIDEO_GOOD_RATE 400
#define LOWER_RATE_PACKET  20
#define LOWEST_RATE_PACKET  10
#define SCORE_KEEP 3
#define VALID_RTT_THRESH 10


enum score_msg_type_et{
	OPLUS_SCORE_MSG_UNSPEC,
	OPLUS_SCORE_MSG_ENABLE,
	OPLUS_SCORE_MSG_FOREGROUND_ANDROID_UID,
	OPLUS_SCORE_MSG_REQUEST_SCORE,
	OPLUS_SCORE_MSG_ADD_LINK,
	OPLUS_SCORE_MSG_DEL_LINK,
	OPLUS_SCORE_MSG_CLEAR_LINK,
	OPLUS_SCORE_MSG_CONFIG,
	OPLUS_SCORE_MSG_REPORT_NETWORK_SCORE,
	OPLUS_SCORE_MSG_ADD_UID,
	OPLUS_SCORE_MSG_REMOVE_UID,
	__OPLUS_SCORE_MSG_MAX,
};
#define OPLUS_SCORE_MSG_MAX (__OPLUS_SCORE_MSG_MAX - 1)

enum score_cmd_type_et{
	OPLUS_SCORE_CMD_UNSPEC,
	OPLUS_SCORE_CMD_DOWNLINK,
	__OPLUS_SCORE_CMD_MAX,
};
#define OPLUS_SCORE_CMD_MAX (__OPLUS_SCORE_CMD_MAX - 1)

#define OPLUS_SCORE_FAMILY_VERSION	1
#define OPLUS_SCORE_FAMILY_NAME "net_score"
#define NLA_DATA(na)		((char *)((char*)(na) + NLA_HDRLEN))
#define GENL_ID_GENERATE	0
static int oplus_score_netlink_rcv_msg(struct sk_buff *skb, struct genl_info *info);
static const struct genl_ops oplus_score_genl_ops[] =
{
	{
		.cmd = OPLUS_SCORE_CMD_DOWNLINK,
		.flags = 0,
		.doit = oplus_score_netlink_rcv_msg,
		.dumpit = NULL,
	},
};

static struct genl_family oplus_score_genl_family =
{
	.id = 0,
	.hdrsize = 0,
	.name = OPLUS_SCORE_FAMILY_NAME,
	.version = OPLUS_SCORE_FAMILY_VERSION,
	.maxattr = OPLUS_SCORE_MSG_MAX,
	.ops = oplus_score_genl_ops,
	.n_ops = ARRAY_SIZE(oplus_score_genl_ops),
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
	.resv_start_op = OPLUS_SCORE_CMD_DOWNLINK + 1,
#endif
};

static int oplus_score_send_netlink_msg(int msg_type, char *payload, int payload_len);
/* score = 100 - rtt * 10 / 500 - 100 * loss_rate * 8*/

static s32 oplus_get_smooth_score(int link, int flag)
{
	int i;
	int j = 0;
	u32 score_sum = 0;
	if (flag) {
		for (i = 0; i < SCORE_WINDOW; i++) {
			if (uplink_score_info[link].uplink_score_save[i] > 0) {
				score_sum += uplink_score_info[link].uplink_score_save[i];
				j++;
			}
		}
	} else {
		for (i = 0; i < SCORE_WINDOW; i++) {
			if (downlink_score_info[link].downlink_score_save[i] > 0) {
				score_sum += downlink_score_info[link].downlink_score_save[i];
				j++;
			}
		}
	}

	if (j == 0) {
		return -1;
	} else {
		return score_sum / j;
	}
}

static int oplus_update_score_count(int link, int score, int flag)
{
	int ret = OPLUS_FALSE;
	if (score == -1) {
		return OPLUS_TRUE;
	}

	if (flag) {
		if(score > WORSE_BASE_SCORE) {
			uplink_score_info[link].uplink_score_count++;
			if (uplink_score_info[link].uplink_score_count > SCORE_KEEP) {
				uplink_score_info[link].uplink_score_count = SCORE_KEEP;
			}
		}
		else if (score <= WORST_BASE_SCORE && score >= 0) {
			uplink_score_info[link].uplink_score_count--;
			if (uplink_score_info[link].uplink_score_count < -SCORE_KEEP) {
				uplink_score_info[link].uplink_score_count = -SCORE_KEEP;
			}
		}

		if (((score > WORST_BASE_SCORE) && (uplink_score_info[link].uplink_score_count >= 0)) ||
			((score <= WORST_BASE_SCORE) && (uplink_score_info[link].uplink_score_count <= 0)) ||
			(uplink_score_info[link].uplink_udp_packets > 0)) {
			ret = OPLUS_TRUE;
		}

		if (oplus_score_debug) {
			printk("[oplus_score]:uplink_report=%d,score=%d,score_count=%d\n",
					ret, score, uplink_score_info[link].uplink_score_count);
		}
	} else {
		if(score > WORSE_BASE_SCORE) {
			downlink_score_info[link].downlink_score_count++;
			if (downlink_score_info[link].downlink_score_count > SCORE_KEEP) {
				downlink_score_info[link].downlink_score_count = SCORE_KEEP;
			}
		}
		else if (score <= WORST_BASE_SCORE && score >= 0) {
			downlink_score_info[link].downlink_score_count--;
			if (downlink_score_info[link].downlink_score_count < -SCORE_KEEP) {
				downlink_score_info[link].downlink_score_count = -SCORE_KEEP;
			}
		}

		if (((score <= WORST_BASE_SCORE) && (downlink_score_info[link].downlink_score_count <= 0)) ||
			((score > WORST_BASE_SCORE) && (downlink_score_info[link].downlink_score_count >= 0)) ||
			(downlink_score_info[link].downlink_udp_packets > 0)) {
			ret = OPLUS_TRUE;
		}

		if (oplus_score_debug) {
			printk("[oplus_score]:downlink_report=%d,score=%d,score_count=%d\n",
					ret, score, downlink_score_info[link].downlink_score_count);
			}
	}

	return ret;
}

void oplus_score_calc_and_report(void)
{
	struct link_score_msg_st link_score_msg;
	int i;
	u32 uplink_index, downlink_index;
	u32 uplink_packets, uplink_tcp_rate, uplink_udp_packets, uplink_udp_rate, uplink_retrans_packets, uplink_srtt, uplink_uid;
	u32 downlink_packets, downlink_tcp_rate, downlink_udp_packets, downlink_udp_rate, downlink_retrans_packets, downlink_srtt, downlink_uid;
	s32 uplink_score, downlink_score;
	u32 uplink_seq, downlink_seq;
	u32 retrans_rate = 0;
	s32 uplink_smooth_score, downlink_smooth_score;
	u32 index = 0;
	int uplink_report, downlink_report;
	char ifname[IFNAME_LEN];
	u32 uplink_nodata_count;
	u32 downlink_nodata_count;
	int downlink_rate = 0;
	u32 uplink_total_packets = 0;
	u32 downlink_total_packets = 0;
	u64 sample_rtt = 0;

	for (i = 0; i < MAX_LINK_APP_NUM; i++) {
		if(uplink_score_info[i].uid == 0 || uplink_score_info[i].link_index == 0 || downlink_score_info[i].uid == 0 || downlink_score_info[i].link_index == 0) {
			continue;
		}

		uplink_smooth_score = -1;
		downlink_smooth_score = -1;
		uplink_report = 0;
		downlink_report = 0;

		if (oplus_score_debug) {
			printk("[oplus_score]:enter oplus_score_calc_and_report up_uid:%u,up_link:%u,down_uid:%u,down_link:%u",
				uplink_score_info[i].uid, uplink_score_info[i].link_index, downlink_score_info[i].uid, downlink_score_info[i].link_index);
		}

		spin_lock_bh(&downlink_score_lock);
		if (downlink_score_info[i].link_index == 0) {
			spin_unlock_bh(&downlink_score_lock);
			continue;
		}

		downlink_uid = downlink_score_info[i].uid;
		downlink_index = downlink_score_info[i].link_index;
		downlink_packets = downlink_score_info[i].downlink_packets;
		downlink_tcp_rate = downlink_score_info[i].downlink_tcp_rate;
		downlink_udp_packets = downlink_score_info[i].downlink_udp_packets;
		downlink_udp_rate = downlink_score_info[i].downlink_udp_rate;
		downlink_retrans_packets = downlink_score_info[i].downlink_retrans_packets;
		downlink_total_packets = downlink_packets + downlink_retrans_packets;
		downlink_srtt = downlink_score_info[i].downlink_srtt;
		downlink_seq = downlink_score_info[i].seq;
		downlink_score_info[i].downlink_packets = 0;
		downlink_score_info[i].downlink_tcp_rate = 0;
		downlink_score_info[i].downlink_udp_packets = 0;
		downlink_score_info[i].downlink_udp_rate = 0;
		downlink_score_info[i].downlink_retrans_packets = 0;
		/*downlink_score_info[i].downlink_srtt = 0;*/
		downlink_score_info[i].downlink_rtt_num = 1;
		downlink_nodata_count = downlink_score_info[i].downlink_nodata_count;
		spin_unlock_bh(&downlink_score_lock);

		spin_lock_bh(&uplink_score_lock);
		if (uplink_score_info[i].link_index == 0) {
			spin_unlock_bh(&uplink_score_lock);
			continue;
		}
		uplink_index = uplink_score_info[i].link_index;
		if (uplink_index != downlink_index) {
			printk("[oplus_score]:link error:uplink_index=%u,downlink_index=%u\n",
				uplink_index, downlink_index);
			spin_unlock_bh(&uplink_score_lock);
			continue;
		}
		memcpy((void*)ifname, (void*)uplink_score_info[i].ifname, IFNAME_LEN);
		uplink_uid = uplink_score_info[i].uid;
		uplink_packets = uplink_score_info[i].uplink_packets;
		uplink_tcp_rate = uplink_score_info[i].uplink_tcp_rate;
		uplink_udp_packets = uplink_score_info[i].uplink_udp_packets;
		uplink_udp_rate = uplink_score_info[i].uplink_udp_rate;
		uplink_retrans_packets = uplink_score_info[i].uplink_retrans_packets;
		uplink_total_packets = uplink_packets + uplink_retrans_packets;

		if (uplink_score_info[i].sample.acked == OPLUS_FALSE) {
		    sample_rtt = (jiffies - uplink_score_info[i].sample.tstamp) * 1000/HZ;
		    printk("[oplus_score]:calc_and_report: sample_seq = %u, sample_rtt = %llu\n",
		    uplink_score_info[i].sample.seq, sample_rtt);
			if (sample_rtt >= 1000) {
				uplink_score_info[i].uplink_rtt_num++;
				uplink_score_info[i].uplink_srtt += sample_rtt;
				if (sample_rtt >= 10000) {
					uplink_score_info[i].sample.acked = OPLUS_TRUE;
					if (oplus_score_debug) {
						printk("[oplus_score]:oplus_score_calc_and_report:sample seq:%u timeout close\n",
							uplink_score_info[i].sample.seq);
					}
				}
			}
		}

		if(uplink_score_info[i].uplink_rtt_num > 0) {
			uplink_srtt = uplink_score_info[i].uplink_srtt / uplink_score_info[i].uplink_rtt_num;
		} else {
			uplink_srtt = 0;
		}
		uplink_seq = uplink_score_info[i].seq;
		uplink_nodata_count = uplink_score_info[i].uplink_nodata_count;
		uplink_score_info[i].uplink_packets = 0;
		uplink_score_info[i].uplink_tcp_rate = 0;
		uplink_score_info[i].uplink_udp_packets = 0;
		uplink_score_info[i].uplink_udp_rate = 0;
		uplink_score_info[i].uplink_retrans_packets = 0;
		/*uplink_score_info[i].uplink_srtt = 0;*/
		if (uplink_score_info[i].uplink_rtt_num) {
			uplink_score_info[i].uplink_rtt_num = 0;
			uplink_score_info[i].uplink_srtt = 0;
		}

		if (uplink_total_packets == 0) {
			if (oplus_score_debug) {
				printk("[oplus_score]:uplink no_data\n");
			}
			uplink_score = -1;
			uplink_score_info[i].uplink_nodata_count++;
		} else {
			uplink_score_info[i].uplink_nodata_count = 0;
			retrans_rate = 100 * uplink_retrans_packets / uplink_total_packets;
			if (uplink_packets > ((para_rate * PACKET_PER_SEC) >> 3)) {
				uplink_score = (s32)(GOOD_BASE_SCORE - (retrans_rate * para_loss) / 8);
			} /*else if (uplink_srtt > BAD_GAME_RTT_THREAD && (uplink_total_packets < 10) && (downlink_total_packets < 10)) {
				uplink_score = (s32)(WORST_BASE_SCORE - (WORST_BASE_SCORE * retrans_rate * para_loss) / 800);
			} */else {
				if (uplink_srtt > WORST_VIDEO_RTT_THRESH) {
					uplink_score = (s32)(WORST_BASE_SCORE - (WORST_BASE_SCORE * retrans_rate * para_loss) / 800);
				} else if (uplink_srtt > WORST_RTT_THRESH) {
					downlink_rate = downlink_packets * 1000 / uplink_srtt;
					if (downlink_rate == 0) {
						uplink_score = -1;
					} else if (downlink_rate > VIDEO_GOOD_RATE) {
						uplink_score = (s32)(GOOD_BASE_SCORE - (GOOD_BASE_SCORE * retrans_rate * para_loss) / 800);
					} else if (downlink_rate > PACKET_PER_SEC) {
						uplink_score = (s32)(NORMAL_BASE_SCORE - (NORMAL_BASE_SCORE * retrans_rate * para_loss) / 800);
					} else {
						uplink_score = (s32)(WORST_BASE_SCORE - (WORST_BASE_SCORE * retrans_rate * para_loss) / 800);
					}
				} else if (uplink_srtt > WORSE_RTT_THRESH) {
					uplink_score = (s32)(WORSE_BASE_SCORE - (WORSE_BASE_SCORE * retrans_rate * para_loss) / 800);
				} else if (uplink_srtt > NORMAL_RTT_THRESH) {
					uplink_score = (s32)(NORMAL_BASE_SCORE - (NORMAL_BASE_SCORE * retrans_rate * para_loss) / 800);
				} else {
					uplink_score = (s32)(GOOD_BASE_SCORE - (retrans_rate * para_loss) / 8);
				}
			}
			if (uplink_score <= 0) {
				uplink_score = 1;
			}
		}

		uplink_report = oplus_update_score_count(i, uplink_score, OPLUS_UPLINK);
		if (uplink_report) {
			index = uplink_score_info[i].uplink_score_index++ % SCORE_WINDOW;
			uplink_score_info[i].uplink_score_save[index] = uplink_score;
		}
		uplink_smooth_score = oplus_get_smooth_score(i, OPLUS_UPLINK);
		/*if (uplink_smooth_score == -1) {
			uplink_report = 0;
		}*/
		spin_unlock_bh(&uplink_score_lock);

		/*added for score3.0 by linjinbin*/
		link_score_msg.uplink_srtt = uplink_srtt;
		link_score_msg.uplink_packets = uplink_packets;
		link_score_msg.uplink_tcp_rate = uplink_tcp_rate;
		link_score_msg.uplink_udp_packets = uplink_udp_packets;
		link_score_msg.uplink_udp_rate = uplink_udp_rate;
		link_score_msg.uplink_retrans_packets = uplink_retrans_packets;
		link_score_msg.uplink_retrans_rate = retrans_rate;

		/*start downlink score calc*/
		if (downlink_total_packets == 0) {
			if (oplus_score_debug) {
				printk("[oplus_score]:downlink no_data,if=%s\n", ifname);
			}
			downlink_score = -1;
			downlink_score_info[i].downlink_nodata_count++;
			retrans_rate = 0;
		} else {
			downlink_score_info[i].downlink_nodata_count = 0;
			retrans_rate = 100 * downlink_retrans_packets / downlink_total_packets;
			if (downlink_packets > ((para_rate * PACKET_PER_SEC) >> 3)) {
				downlink_score = (s32)(GOOD_BASE_SCORE - retrans_rate * para_loss / 4);
			} else {
				if (uplink_srtt > WORST_RTT_THRESH) {
					downlink_score = (s32)(WORST_BASE_SCORE - (WORST_BASE_SCORE * retrans_rate * para_loss) / 800);
				} else {
					downlink_score = (s32)(100 - (2 * downlink_retrans_packets * uplink_srtt) / 10);
				}
			}

			if (downlink_score < 0) {
				downlink_score = 1;
			}
		}

		if ((downlink_total_packets < 15) && (downlink_score > WORST_BASE_SCORE)) {
			downlink_score = -1;
		}

		spin_lock_bh(&downlink_score_lock);
		downlink_report = oplus_update_score_count(i, downlink_score, OPLUS_DOWNLINK);
		if (downlink_report) {
			index = downlink_score_info[i].downlink_score_index++ % SCORE_WINDOW;
			downlink_score_info[i].downlink_score_save[index] = downlink_score;
		}
		downlink_smooth_score = oplus_get_smooth_score(i, OPLUS_DOWNLINK);
		/*if (downlink_smooth_score == -1) {
			downlink_report = 0;
		}*/
		spin_unlock_bh(&downlink_score_lock);

		/*added for score3.0 by linjinbin*/
		link_score_msg.downlink_srtt = downlink_srtt;
		link_score_msg.downlink_packets = downlink_packets;
		link_score_msg.downlink_tcp_rate = downlink_tcp_rate;
		link_score_msg.downlink_udp_packets = downlink_udp_packets;
		link_score_msg.downlink_udp_rate = downlink_udp_rate;
		link_score_msg.downlink_retrans_packets = downlink_retrans_packets;
		link_score_msg.downlink_retrans_rate  = retrans_rate;
		link_score_msg.downlink_rate = downlink_rate;

		if(uplink_uid == downlink_uid) {
			link_score_msg.uid = uplink_uid;
		} else {
			printk("[oplus_score]:uplink_uid = %u, downlink_uid = %u", uplink_uid, downlink_uid);
		}

		if (uplink_report || downlink_report) {
			link_score_msg.link_index = uplink_index;
			if (uplink_smooth_score == -1) {
				link_score_msg.uplink_score = downlink_smooth_score;
			} else {
				link_score_msg.uplink_score = uplink_smooth_score;
			}

			if (downlink_smooth_score == -1) {
				link_score_msg.downlink_score = uplink_smooth_score;
			} else {
				link_score_msg.downlink_score = downlink_smooth_score;
			}

			oplus_score_send_netlink_msg(OPLUS_SCORE_MSG_REPORT_NETWORK_SCORE, (char *)&link_score_msg, sizeof(link_score_msg));
			if (oplus_score_debug || (downlink_smooth_score <= WORST_BASE_SCORE) || (uplink_smooth_score <= WORST_BASE_SCORE)) {
				if (net_ratelimit())
					printk("[oplus_score]:report_score1:link=%u,if=%s,up_score=%d,down_score=%d,uid=%u,ul_p=%d,dl_p=%d\n",
						uplink_index, ifname, link_score_msg.uplink_score, link_score_msg.downlink_score,
						oplus_score_foreground_uid[0], uplink_report, downlink_report);
			}
		}

		if (oplus_score_debug) {
				printk("[oplus_score]:report_score_all:link=%u,uplink_score=%d,us_score=%d,downlink_score=%d,ds_score=%d,uid=%u,ul_p=%d,dl_p=%d\n",
					uplink_index, uplink_score, uplink_smooth_score, downlink_score,
					downlink_smooth_score, oplus_score_foreground_uid[0], uplink_report, downlink_report);
		}
	}

	return;
}

static void oplus_score_report_timer_function(struct timer_list *t)
{
	oplus_score_calc_and_report();
	mod_timer(&oplus_score_report_timer, jiffies + REPORT_PERIOD * HZ);
}

static void oplus_score_report_timer_init(void)
{
	printk("[oplus_score]:report_timer_init\n");
	timer_setup(&oplus_score_report_timer, oplus_score_report_timer_function, 0);
}

static void oplus_score_report_timer_start(void)
{
	printk("[oplus_score]:report_timer_start\n");
	/*oplus_score_report_timer.function = (void *)oplus_score_report_timer_function;*/
	oplus_score_report_timer.expires = jiffies + REPORT_PERIOD * HZ;
	mod_timer(&oplus_score_report_timer, oplus_score_report_timer.expires);
}

static void oplus_score_report_timer_del(void)
{
	printk("[oplus_score]:report_timer_del\n");
	del_timer_sync(&oplus_score_report_timer);
}

static inline int genl_msg_prepare_usr_msg(u8 cmd, size_t size, pid_t pid, struct sk_buff **skbp)
{
	struct sk_buff *skb;
	/* create a new netlink msg */
	skb = genlmsg_new(size, GFP_ATOMIC);
	if (skb == NULL) {
		return -ENOMEM;
	}

	/* Add a new netlink message to an skb */
	genlmsg_put(skb, pid, 0, &oplus_score_genl_family, 0, cmd);
	*skbp = skb;
	return 0;
}

static inline int genl_msg_mk_usr_msg(struct sk_buff *skb, int type, void *data, int len)
{
	int ret;
	/* add a netlink attribute to a socket buffer */
	if ((ret = nla_put(skb, type, len, data)) != 0) {
		return ret;
	}

	return 0;
}

static inline int uplink_get_array_index_by_link_index(int link_index)
{
	int array_index = -1;
	int i;

	for (i = 0; i < MAX_LINK_NUM; i++) {
		if (uplink_score_info[i].link_index == link_index) {
			return i;
		}
	}

	return array_index;
}

static inline int get_array_index_by_link_index_uid(int link_index, int uid)
{
	int array_index = -1;
	int i;
	if (oplus_score_debug) {
		printk("[oplus_score]: get_array_index_by_link_index_uid  link_index=%u, uid=%u", link_index, uid);
	}

	for (i = 0; i < MAX_LINK_APP_NUM; i++) {
		if ((uplink_score_info[i].link_index == link_index) && (uplink_score_info[i].uid == uid)) {
			return i;
		}
	}

	return array_index;
}

static inline int downlink_get_array_index_by_link_index(int link_index)
{
	int array_index = -1;
	int i;
	if (oplus_score_debug) {
		printk("[oplus_score]: downlink_get_array_index_by_link_index %u", link_index);
	}

	for (i = 0; i < MAX_LINK_NUM; i++) {
		if (downlink_score_info[i].link_index == link_index) {
			return i;
		}
	}

	return array_index;
}

static inline int is_foreground_uid(int uid)
{
	int i;
	for (i = 0; i < FOREGROUND_UID_MAX_NUM; i++) {
		if (uid == oplus_score_foreground_uid[i]) {
			return OPLUS_TRUE;
		}
	}

	return OPLUS_FALSE;
}

/* send to user space */
static int oplus_score_send_netlink_msg(int msg_type, char *payload, int payload_len)
{
	int ret = 0;
	void * head;
	struct sk_buff *skbuff;
	size_t size;

	if (!oplus_score_user_pid) {
		printk("[oplus_score]: oplus_score_send_netlink_msg,oplus_score_user_pid=0\n");
		return -1;
	}

	/* allocate new buffer cache */
	size = nla_total_size(payload_len);
	ret = genl_msg_prepare_usr_msg(OPLUS_SCORE_CMD_DOWNLINK, size, oplus_score_user_pid, &skbuff);
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
	ret = genlmsg_unicast(&init_net, skbuff, oplus_score_user_pid);
	if(ret < 0) {
		printk("[oplus_score]:oplus_score_send_netlink_msg error, ret = %d\n", ret);
		return -1;
	}

	return 0;
}

static uid_t get_uid_from_sock(const struct sock *sk)
{
	uid_t sk_uid;
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	const struct file *filp = NULL;
	#endif
	if (NULL == sk) {
		return 0;
	}
	if (NULL == sk || !sk_fullsock(sk) || NULL == sk->sk_socket) {
		return 0;
	}
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	filp = sk->sk_socket->file;
	if (NULL == filp) {
		return 0;
	}
	sk_uid = __kuid_val(filp->f_cred->fsuid);
	#else
	sk_uid = __kuid_val(sk->sk_uid);
	if (oplus_score_debug) {
		printk("[oplus_score]:get_uid_from_sock sk_uid=%u", sk_uid);
	}
	#endif
	return sk_uid;
}

static void oplus_score_uplink_stat(struct sk_buff *skb, struct sock *sk)
{
	int link_index;
	int i;

	struct tcp_sock *tp;
	struct iphdr *iph = ip_hdr(skb);
	struct ipv6hdr *ipv6h = ipv6_hdr(skb);
	struct tcphdr *tcph = tcp_hdr(skb);
	struct udphdr *udph = udp_hdr(skb);
	struct inet_connection_sock *icsk;
	u32 srtt;
	u32 rtt;
	uid_t uid;
	u64 sample_rtt;


	if (!sk) {
		return;
	}

	uid = get_uid_from_sock(sk);

	tp = tcp_sk(sk);

	icsk = inet_csk(sk);
	link_index = skb->dev->ifindex;
	if (oplus_score_debug) {
		printk("[oplus_score]:oplus_score_uplink_stat link=%d,uid=%u\n", link_index, uid);
	}

	spin_lock_bh(&uplink_score_lock);
	i = get_array_index_by_link_index_uid(link_index, uid);

	if (i < 0) {
		if (oplus_score_debug) {
			printk("[oplus_score]:i < 0");
			printk("[oplus_score]:upskb_linkerr,link=%d,if=%s,seq=%u,ack=%u,sport=%u,dport=%u,uid=%u\n",
				link_index, skb->dev->name, ntohl(tcph->seq), ntohl(tcph->ack_seq), ntohs(tcph->source),
				ntohs(tcph->dest), (u32)(sk->sk_uid.val));
		}
		spin_unlock_bh(&uplink_score_lock);
		return;
	}

	if (((skb->protocol == htons(ETH_P_IP) && (iph->protocol == IPPROTO_TCP)) ||
			(skb->protocol == htons(ETH_P_IPV6) && (ipv6h->nexthdr == NEXTHDR_TCP))) && tcph) {
		if (icsk->icsk_ca_state >= TCP_CA_Recovery && tp->high_seq !=0 && before(ntohl(tcph->seq), tp->high_seq)) {
			uplink_score_info[i].uplink_retrans_packets++;
		} else {
			uplink_score_info[i].uplink_packets++;
		}
		uplink_score_info[i].uplink_tcp_rate += skb->len;
		if (oplus_score_debug) {
			printk("[oplus_score]:uplink=%d,if=%s,seq=%u,high_seq=%u,uplink_retran=%u,npacket=%u,uplink_tcp_rate=%u,uid=%u,sport=%u,dport=%u,state=%d,len=%u\n",
					link_index, skb->dev->name, ntohl(tcph->seq), tp->high_seq, uplink_score_info[i].uplink_retrans_packets,
					uplink_score_info[i].uplink_packets, uplink_score_info[i].uplink_tcp_rate, (u32)(sk->sk_uid.val), ntohs(tcph->source),
					ntohs(tcph->dest), sk->sk_state, skb->len);
		}

		uplink_score_info[i].seq = ntohl(tcph->seq);
		srtt = (tp->srtt_us >> 3) / 1000;
		rtt = tp->rack.rtt_us / 1000;
		if (oplus_score_debug) {
			sample_rtt = jiffies*1000/HZ - tp->rack.mstamp/1000;
			printk("[oplus_score]:rtt_sample_up:srtt=%u, rtt=%u, rtt_num=%u, uid=%u, rack_stamp=%llu, delay=%llu\n",
				srtt, rtt, uplink_score_info[i].uplink_rtt_num, (u32)(sk->sk_uid.val), tp->rack.mstamp, sample_rtt);
		}

		if (rtt > VALID_RTT_THRESH) {
			uplink_score_info[i].uplink_rtt_num++;
			uplink_score_info[i].uplink_srtt += rtt;
		}

		if ((uplink_score_info[i].sample.acked == OPLUS_TRUE) &&
		        ((skb->protocol == htons(ETH_P_IP)) && (ntohs(iph ->tot_len) > (iph->ihl + tcph->doff) * 4))) {
		    uplink_score_info[i].sample.proto = skb->protocol;
		    uplink_score_info[i].sample.saddr = iph->saddr;
		    uplink_score_info[i].sample.daddr = iph->daddr;
		    uplink_score_info[i].sample.sport = tcph->source;
		    uplink_score_info[i].sample.dport = tcph->dest;
		    uplink_score_info[i].sample.seq = ntohl(tcph->seq);
		    uplink_score_info[i].sample.tstamp = (u32)jiffies;
		    uplink_score_info[i].sample.acked = OPLUS_FALSE;
		    if (oplus_score_debug) {
		        printk("[oplus_score]:uplink_stat:sample seq:%u, proto:%u,saddr:%u,daddr:%u,sport:%u,dport:%u\n",
		            uplink_score_info[i].sample.seq,
		            uplink_score_info[i].sample.proto,
		            uplink_score_info[i].sample.saddr,
		            uplink_score_info[i].sample.daddr,
		            uplink_score_info[i].sample.sport,
		            uplink_score_info[i].sample.dport);
		    }
		} else if ((uplink_score_info[i].sample.acked == OPLUS_FALSE)
		    && (uplink_score_info[i].sample.proto == skb->protocol)
		    && (uplink_score_info[i].sample.saddr == iph->saddr)
		    && (uplink_score_info[i].sample.daddr == iph->daddr)
		    && (uplink_score_info[i].sample.sport == tcph->source)
		    && (uplink_score_info[i].sample.dport == tcph->dest)
		        && (tcph->fin || tcph->rst)) {
		    uplink_score_info[i].sample.acked = OPLUS_TRUE;
		    if (oplus_score_debug) {
		        printk("[oplus_score]:uplink_stat:sample seq:%u close\n",
		            uplink_score_info[i].sample.seq);
		    }
		}
	} else if (((skb->protocol == htons(ETH_P_IP) && (iph->protocol == IPPROTO_UDP)) ||
			(skb->protocol == htons(ETH_P_IPV6) && (ipv6h->nexthdr == NEXTHDR_UDP))) && udph) {
		if (oplus_score_debug) {
			printk("[oplus_score]:oplus_score_uplink_stat udp package");
		}
		uplink_score_info[i].uplink_udp_packets++;
		uplink_score_info[i].uplink_udp_rate += skb->len;
	}
	spin_unlock_bh(&uplink_score_lock);

	return;
}

static int is_downlink_retrans_pack(u32 skb_seq, struct sock *sk)
{
	struct tcp_sock *tp = (struct tcp_sock*)sk;
	struct inet_connection_sock *icsk = inet_csk(sk);
	u32 now = (u32)jiffies;

	if((skb_seq == tp->rcv_nxt) && (!RB_EMPTY_ROOT(&tp->out_of_order_queue))) {
		int m = (int)(now - icsk->icsk_ack.lrcvtime) * 1000 / HZ;
		int half_rtt = (tp->srtt_us / 8000) >> 1;
		if ((tp->srtt_us != 0) && (m > 50)) {
			if (oplus_score_debug) {
				printk("[oplus_score]:now=%u,lrcttime=%u,half_rtt=%d,m=%d,Hz=%u,rtt=%u,seq=%u\n",
					now, icsk->icsk_ack.lrcvtime, half_rtt, m, HZ, tp->srtt_us, skb_seq);
			}
			return OPLUS_TRUE;
		}
	}

	return OPLUS_FALSE;
}

static void oplus_score_downlink_stat(struct sk_buff *skb, struct sock *sk)
{
	int link_index;
	int i;
	struct tcp_sock *tp;
	struct iphdr *iph = ip_hdr(skb);
	struct ipv6hdr *ipv6h = ipv6_hdr(skb);
	struct tcphdr *tcph = tcp_hdr(skb);
	struct udphdr *udph = udp_hdr(skb);
	struct inet_connection_sock *icsk;
	u32 srtt;
	uid_t uid;

	if (!sk) {
		return;
	}

	uid = get_uid_from_sock(sk);

	tp = tcp_sk(sk);
	icsk = inet_csk(sk);
	link_index = skb->dev->ifindex;

	if (oplus_score_debug) {
		printk("[oplus_score]:oplus_score_downlink_stat link=%d,uid=%u\n", link_index, uid);
	}

	if (((skb->protocol == htons(ETH_P_IP) && (iph->protocol == IPPROTO_TCP)) ||
			(skb->protocol == htons(ETH_P_IPV6)
                && (ipv6h->nexthdr == NEXTHDR_TCP))) && tcph) {
        spin_lock_bh(&uplink_score_lock);
		i = get_array_index_by_link_index_uid(link_index, uid);
		if (i >= 0) {
		    if ((uplink_score_info[i].sample.acked == OPLUS_FALSE)
		        && (uplink_score_info[i].sample.proto == skb->protocol)
		        && (uplink_score_info[i].sample.saddr == iph->daddr)
		        && (uplink_score_info[i].sample.daddr == iph->saddr)
		        && (uplink_score_info[i].sample.sport == tcph->dest)
		        && (uplink_score_info[i].sample.dport == tcph->source)) {
		        if (tcph->fin
		            || tcph->rst
		            || (tcph->ack
		                && (ntohl(tcph->ack_seq) > uplink_score_info[i].sample.seq))) {
		            uplink_score_info[i].sample.acked = OPLUS_TRUE;
					if (oplus_score_debug) {
						printk("[oplus_score]:uplink_stat:sample seq:%u per close\n",
							uplink_score_info[i].sample.seq);
					}
		        }
		    }
		} else {
		    if (oplus_score_debug) {
		        printk("[oplus_score]:downskb_uplinkerr, link=%d\n", link_index);
		    }
		}
		spin_unlock_bh(&uplink_score_lock);
	}

	spin_lock_bh(&downlink_score_lock);
	i = get_array_index_by_link_index_uid(link_index, uid);
	if (i < 0) {
		if (oplus_score_debug) {
			printk("[oplus_score]:downskb_linkerr,link=%d,if=%s,seq=%u,ack=%u,sport=%u,dport=%u,uid=%u\n",
				link_index, skb->dev->name, ntohl(tcph->seq), ntohl(tcph->ack_seq), ntohs(tcph->source),
				ntohs(tcph->dest), (u32)(sk->sk_uid.val));
		}
		spin_unlock_bh(&downlink_score_lock);
		return;
	}

	if (((skb->protocol == htons(ETH_P_IP) && (iph->protocol == IPPROTO_TCP)) ||
			(skb->protocol == htons(ETH_P_IPV6) && (ipv6h->nexthdr == NEXTHDR_TCP))) && tcph) {
		if ((sk->sk_state != TCP_SYN_SENT) && (is_downlink_retrans_pack(ntohl(tcph->seq), sk))) {
			downlink_score_info[i].downlink_retrans_packets++;
		} else {
			downlink_score_info[i].downlink_packets++;
		}
		downlink_score_info[i].downlink_tcp_rate += skb->len;
		if (oplus_score_debug) {
			printk("[oplus_score]:downlink = %d, if = %s, proto = %u, seq = %u, ack_seq = %u, rcv_nxt = %u, downlink_retran = %u,"
					"npacket = %u, down_tcp_rate = %u, uid = %u, sport = %u, dport = %u, state = %d, len = %u\n",
					link_index, skb->dev->name, skb->protocol, ntohl(tcph->seq), ntohl(tcph->ack_seq),
					tp->rcv_nxt, downlink_score_info[i].downlink_retrans_packets,
					downlink_score_info[i].downlink_packets, downlink_score_info[i].downlink_tcp_rate, (u32)(sk->sk_uid.val), ntohs(tcph->source),
					ntohs(tcph->dest), sk->sk_state, skb->len);
		}

		downlink_score_info[i].seq = ntohl(tcph->seq);
		srtt = (tp->rcv_rtt_est.rtt_us >> 3) / 1000;
		if (oplus_score_debug) {
			printk("[oplus_score]:rtt_sample_down:rtt=%u,rtt_num=%u,uid=%u\n",
				srtt, downlink_score_info[i].downlink_rtt_num, (u32)(sk->sk_uid.val));
		}

		if (srtt) {
			downlink_score_info[i].downlink_rtt_num++;
			if (downlink_score_info[i].downlink_rtt_num != 0) {
				downlink_score_info[i].downlink_srtt =
					(downlink_score_info[i].downlink_srtt * (downlink_score_info[i].downlink_rtt_num -1) + srtt) / downlink_score_info[i].downlink_rtt_num;
			}
			downlink_score_info[i].downlink_update_stamp = (u32)jiffies;
		}
	} else if (((skb->protocol == htons(ETH_P_IP) && (iph->protocol == IPPROTO_UDP)) ||
			(skb->protocol == htons(ETH_P_IPV6) && (ipv6h->nexthdr == NEXTHDR_UDP))) && udph) {
		downlink_score_info[i].downlink_udp_packets++;
		downlink_score_info[i].downlink_udp_rate += skb->len;
	}

	spin_unlock_bh(&downlink_score_lock);

	return;
}

static inline int skb_v4_check(struct sk_buff *skb, struct sock **psk)
{
	struct sock *sk = NULL;
	uid_t sk_uid;
	struct iphdr *iph = NULL;
	struct tcphdr *tcph = NULL;
	struct udphdr *udph = NULL;
	struct net_device *dev;

	iph = ip_hdr(skb);
	tcph = tcp_hdr(skb);
	udph = udp_hdr(skb);

	dev = skb->dev;
	if (!dev) {
		printk("[oplus_score]:!dev");
		return OPLUS_FALSE;
	}

	if (skb->protocol != htons(ETH_P_IP) || (!iph)) {
		return OPLUS_FALSE;
	}

	if ((iph->protocol == IPPROTO_TCP) && tcph) {
		if (oplus_score_debug) {
			printk("[oplus_score]:skb_v4_check current is tcp package");
		}
	} else if ((iph->protocol == IPPROTO_UDP) && udph) {
		if (oplus_score_debug) {
			printk("[oplus_score]:skb_v4_check current is udp package");
		}
	} else {
		return OPLUS_FALSE;
	}

	/* udp package also need calc */
	/*if (iph->protocol != IPPROTO_TCP || !tcph)
		return OPLUS_FALSE;*/

	sk = skb_to_full_sk(skb);

#if (!LINUX_KERNEL_510)
	if (!sk && (iph->protocol == IPPROTO_UDP) && udph) {
		sk = __udp4_lib_lookup(dev_net(dev), iph->saddr, udph->source, iph->daddr, udph->dest, inet_iif(skb), inet_sdif(skb),
				&udp_table, skb);
	}
#endif

	if (!sk) {
		return OPLUS_FALSE;
	}

	sk_uid = get_uid_from_sock(sk);
	if(sk_uid == 0) {
		return OPLUS_FALSE;
	}

	/* check uid is foreground*/
	if (!is_foreground_uid(sk_uid)) {
		if (oplus_score_debug) {
			printk("[oplus_score]:skb_v4_check not foreground uid");
		}
		return OPLUS_FALSE;
	}

	*psk = sk;

	return OPLUS_TRUE;
}

static inline int skb_v6_check(struct sk_buff *skb, struct sock **psk)
{
	struct sock * sk = NULL;
	uid_t sk_uid;
	struct tcphdr *tcph = NULL;
	struct udphdr *udph = NULL;
	struct ipv6hdr *ipv6h = NULL;
	struct net_device *dev;

	ipv6h = ipv6_hdr(skb);
	tcph = tcp_hdr(skb);
	udph = udp_hdr(skb);
	if (skb->protocol != htons(ETH_P_IPV6) || (!ipv6h))
		return OPLUS_FALSE;


	sk = skb_to_full_sk(skb);

#if (!LINUX_KERNEL_510)
	if (!sk && (ipv6h->nexthdr == NEXTHDR_UDP) && udph) {
		sk = __udp6_lib_lookup(dev_net(skb->dev), &ipv6h->saddr, udph->source, &ipv6h->daddr, udph->dest, inet6_iif(skb), inet6_iif(skb),
				&udp_table, skb);
	}
#endif

	if (!sk) {
		return OPLUS_FALSE;
	}

	if ((ipv6h->nexthdr == NEXTHDR_TCP) && tcph) {
		if (oplus_score_debug) {
			printk("[oplus_score]:skb_v6_check current is tcp package");
		}
	} else if ((ipv6h->nexthdr == NEXTHDR_UDP) && udph) {
		if (oplus_score_debug) {
			printk("[oplus_score]:skb_v6_check current is udp package");
		}
	} else {
		return OPLUS_FALSE;
	}

	/* check uid is foreground*/
	sk_uid = get_uid_from_sock(sk);
	if (!is_foreground_uid(sk_uid)) {
		if (oplus_score_debug) {
			printk("[oplus_score]:skb_v6_check !is_foreground_uid");
		}
		return OPLUS_FALSE;
	}

	dev = skb->dev;
	if (!dev) {
		printk("[oplus_score]:skb_v6_check !dev");
		return OPLUS_FALSE;
	}

	*psk = sk;

	return OPLUS_TRUE;
}

static unsigned int oplus_score_postrouting_hook4(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	struct sock *sk = NULL;

	if (!oplus_score_enable_flag)
		return NF_ACCEPT;

	if (skb_v4_check(skb, &sk) == OPLUS_FALSE) {
		return NF_ACCEPT;
	}

	oplus_score_uplink_stat(skb, sk);

	return NF_ACCEPT;
}

static unsigned int oplus_score_postrouting_hook6(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	struct sock *sk = NULL;

	if (!oplus_score_enable_flag)
		return NF_ACCEPT;

	if (skb_v6_check(skb, &sk) == OPLUS_FALSE) {
		return NF_ACCEPT;
	}

	oplus_score_uplink_stat(skb, sk);

	return NF_ACCEPT;
}

static unsigned int oplus_score_input_hook4(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	struct sock *sk = NULL;
	if (!oplus_score_enable_flag) {
		return NF_ACCEPT;
	}

	if (skb_v4_check(skb, &sk) == OPLUS_FALSE) {
		return NF_ACCEPT;
	}

	oplus_score_downlink_stat(skb, sk);

	return NF_ACCEPT;
}

static unsigned int oplus_score_input_hook6(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	struct sock *sk = NULL;
	if (!oplus_score_enable_flag)
		return NF_ACCEPT;

	if (skb_v6_check(skb, &sk) == OPLUS_FALSE) {
		return NF_ACCEPT;
	}

	oplus_score_downlink_stat(skb, sk);

	return NF_ACCEPT;
}

static struct nf_hook_ops oplus_score_netfilter_ops[] __read_mostly =
{
	{
		.hook		= oplus_score_postrouting_hook4,
		.pf			= NFPROTO_IPV4,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP_PRI_FILTER + 1,
	},
	{
		.hook		= oplus_score_input_hook4,
		.pf			= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_FILTER + 1,
	},
		{
		.hook		= oplus_score_postrouting_hook6,
		.pf			= NFPROTO_IPV6,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP_PRI_FILTER + 1,
	},
	{
		.hook		= oplus_score_input_hook6,
		.pf			= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_FILTER + 1,
	},
};

static void oplus_score_enable(struct nlattr *nla)
{
	u32 *data = (u32*)NLA_DATA(nla);
	oplus_score_enable_flag = data[0];
	printk("[oplus_score]:oplus_score_enable_flag = %u", oplus_score_enable_flag);
	return;
}

static int checkScoreInfoExist(int uid, u32 link_index) {
	int i;
	spin_lock_bh(&uplink_score_lock);
	for (i = 0; i < MAX_LINK_APP_NUM; i++) {
		if(uplink_score_info[i].uid == uid && uplink_score_info[i].link_index == link_index) {
			printk("[oplus_score]:checkScoreInfoExist uid and link already exist");
			spin_unlock_bh(&uplink_score_lock);
			return OPLUS_TRUE;
		}
	}
	spin_unlock_bh(&uplink_score_lock);
	return OPLUS_FALSE;
}

static void oplus_score_create_score_info(int uid, u32 link_index) {
	int m;

	if(uid == 0 || link_index == 0) {
		printk("[oplus_score]: oplus_score_create_score_info return for uid:%u,link_index:%u", uid, link_index);
		return;
	}

	if (oplus_score_debug) {
		printk("[oplus_score]: oplus_score_create_score_info create uid:%u,link_index:%u", uid, link_index);
	}

	if(checkScoreInfoExist(uid, link_index)) {
		printk("[oplus_score]: score info exist. uid:%u,link_index:%u", uid, link_index);
		return;
	}

	/*create uid+netlink uplink_score_info*/
	spin_lock_bh(&uplink_score_lock);
	for (m = 0; m < MAX_LINK_APP_NUM; m++) {
		if(uplink_score_info[m].uid != 0)
			continue;

		uplink_score_info[m].link_index = link_index;
		uplink_score_info[m].uid = uid;
		uplink_score_info[m].uplink_retrans_packets = 0;
		uplink_score_info[m].uplink_packets = 0;
		uplink_score_info[m].uplink_tcp_rate = 0;
		uplink_score_info[m].uplink_udp_rate = 0;
		uplink_score_info[m].uplink_udp_packets = 0;
		uplink_score_info[m].uplink_nodata_count = 0;
		/*uplink_score_info[i].uplink_score = MAX_LINK_SCORE;*/
		uplink_score_info[m].uplink_rtt_num = 0;
		uplink_score_info[m].sample.acked = OPLUS_TRUE;
		break;
	}
	spin_unlock_bh(&uplink_score_lock);

	spin_lock_bh(&downlink_score_lock);
	for (m = 0; m < MAX_LINK_APP_NUM; m++) {
		if(downlink_score_info[m].uid != 0)
			continue;

		downlink_score_info[m].link_index = link_index;
		downlink_score_info[m].uid = uid;
		downlink_score_info[m].downlink_retrans_packets = 0;
		downlink_score_info[m].downlink_packets = 0;
		downlink_score_info[m].downlink_tcp_rate = 0;
		downlink_score_info[m].downlink_udp_packets = 0;
		downlink_score_info[m].downlink_udp_rate = 0;
		downlink_score_info[m].downlink_nodata_count = 0;
		/*downlink_score_info[m].downlink_score = MAX_LINK_SCORE;*/
		break;
	}
	spin_unlock_bh(&downlink_score_lock);
}

static void oplus_score_add_uid(struct nlattr *nla)
{
	u32 *data;
	int u, i, num, uid;

	data = (u32 *)NLA_DATA(nla);
	num = data[0];
	if (num <= 0 || num > FOREGROUND_UID_MAX_NUM) {
		printk("[oplus_score]: foreground uid num out of range, num = %d", num);
		return;
	}

	for (u = 0; u < num; u++) {
		uid = data[u + 1];

		for (i = 0; i < FOREGROUND_UID_MAX_NUM; i++) {
			if(oplus_score_foreground_uid[i] == 0) {
				oplus_score_foreground_uid[i] = uid;
				break;
			}
		}
		printk("[oplus_score]: add uid, num = %d, index = %d, uid=%u\n", num, u, uid);

		for(i = 0; i < MAX_LINK_NUM; i++) {
			if(oplus_score_active_link[i] != 0) {
				oplus_score_create_score_info(uid, oplus_score_active_link[i]);
				printk("[oplus_score]: oplus_score_add_uid oplus_score_create_score_info uid:%u,link_index:%u", uid, oplus_score_active_link[i]);
			}
		}
	}
	return;
}

static void oplus_score_remove_uid(struct nlattr *nla)
{
	u32 *data;
	int i, j, num, uid;

	data = (u32 *)NLA_DATA(nla);
	num = data[0];

	for (i = 0; i < num; i++) {
		uid = data[i + 1];
		printk("[oplus_score]: remove uid, num = %d, index = %d, uid=%u\n", num, i, data[i + 1]);

		spin_lock_bh(&uplink_score_lock);
		for (j = 0; j < MAX_LINK_APP_NUM; j++) {
			if(uplink_score_info[j].uid == uid) {
				memset(&uplink_score_info[j], 0, sizeof(struct uplink_score_info_st));
			}
		}
		spin_unlock_bh(&uplink_score_lock);

		spin_lock_bh(&downlink_score_lock);
		for (j = 0; j < MAX_LINK_APP_NUM; j++) {
			if(downlink_score_info[j].uid == uid) {
				memset(&downlink_score_info[j], 0, sizeof(struct downlink_score_info_st));
			}
		}
		spin_unlock_bh(&downlink_score_lock);

		for (j = 0; j < FOREGROUND_UID_MAX_NUM; j++) {
			if(oplus_score_foreground_uid[j] == uid) {
				oplus_score_foreground_uid[j] = 0;
			}
		}
	}
	return;
}


static void oplus_score_request_score(struct nlattr *nla)
{
	u32 *data;
	u32 link_index;
	int i;

	struct link_score_msg_st link_score_msg;
	data = (u32 *)NLA_DATA(nla);
	link_index = data[0];

	memset(&link_score_msg, 0, sizeof(struct link_score_msg_st));
	spin_lock_bh(&uplink_score_lock);
	i = uplink_get_array_index_by_link_index(link_index);
	if (i < 0) {
		/* printk("[oplus_score]:uplink get index falure!\n"); */
		spin_unlock_bh(&uplink_score_lock);
		return;
	}
	link_score_msg.uplink_score = oplus_get_smooth_score(link_index, OPLUS_UPLINK);
	spin_unlock_bh(&uplink_score_lock);

	spin_lock_bh(&downlink_score_lock);
	i = downlink_get_array_index_by_link_index(link_index);
	if (i < 0) {
		/* printk("[oplus_score]:downlink get index falure!\n");*/
		spin_unlock_bh(&downlink_score_lock);
		return;
	}
	link_score_msg.downlink_score = oplus_get_smooth_score(link_index, OPLUS_DOWNLINK);
	spin_unlock_bh(&downlink_score_lock);

	link_score_msg.link_index = link_index;
	printk("[oplus_score]:request_report:link=%d,uscore=%d,dscore=%d\n", link_index, link_score_msg.uplink_score, link_score_msg.downlink_score);
	oplus_score_send_netlink_msg(OPLUS_SCORE_MSG_REPORT_NETWORK_SCORE, (char *)&link_score_msg, sizeof(link_score_msg));
	return;
}

static void oplus_score_add_link(struct nlattr *nla)
{
	u32 *data;
	u32 link_index;
	struct net_device *dev;
	bool is_link_exist = false;
	int i;

	data = (u32 *)NLA_DATA(nla);
	link_index = data[0];

	if (link_index == 0) {
		printk("[oplus_score]:error, link index is 0!\n");
		return;
	}

	dev = dev_get_by_index(&init_net, link_index);
	if(!dev) {
		printk("[oplus_score]:dev is null,index=%d\n", link_index);
		return;
	}

	printk("[oplus_score]:to add_link index=%u,dev_name=%s, uplink_num=%d,downlink_num=%d!\n",
			link_index, dev->name, oplus_score_uplink_num, oplus_score_downlink_num);


	if (oplus_score_link_num == MAX_LINK_NUM) {
		printk("[oplus_score]:error, uplink num reach max.\n");
		dev_put(dev);
		return;
	}

	for (i = 0; i < MAX_LINK_NUM; i++) {
		if(oplus_score_active_link[i] == link_index) {
			printk("[oplus_score]:warning,uplink already exist,index = %u.\n", link_index);
			dev_put(dev);
			return;
		}
	}

	for (i = 0; i < MAX_LINK_NUM; i++) {
		if(oplus_score_active_link[i] != 0) {
			printk("[oplus_score]:add uplink=%u. continue\n", oplus_score_active_link[i]);
			continue;
		}
		printk("[oplus_score]:oplus_score_add_link add link_index = %u.\n", link_index);
		oplus_score_active_link[i] = link_index;
		oplus_score_link_num++;
		break;
	}

	spin_lock_bh(&uplink_score_lock);
	for(i = 0; i < MAX_LINK_APP_NUM; i++) {
		if(uplink_score_info[i].link_index == 0) {
			break;
		}
		if(uplink_score_info[i].link_index == link_index) {
			is_link_exist = true;
			break;
		}
	}
	spin_unlock_bh(&uplink_score_lock);

	if(!is_link_exist) {
		for(i = 0; i < FOREGROUND_UID_MAX_NUM; i++) {
			oplus_score_create_score_info(oplus_score_foreground_uid[i], link_index);
			printk("[oplus_score]: oplus_score_add_link create uid:%u,link_index:%u", oplus_score_foreground_uid[i], link_index);
		}
	}

	dev_put(dev);

	return;
}

static void oplus_score_del_link(struct nlattr *nla)
{
	u32 *data;
	u32 i, j, link_index;

	data = (u32 *)NLA_DATA(nla);
	link_index = data[0];

	printk("[oplus_score]:to del_link index=%u,uplink_num=%d,downlink_num=%d!\n",
			link_index, oplus_score_uplink_num, oplus_score_downlink_num);

	if (link_index == 0) {
		printk("[oplus_score]:error, link index is 0!\n");
		return;
	}

	for (i = 0; i < MAX_LINK_NUM; i++) {
		if (oplus_score_active_link[i] == link_index) {
			oplus_score_active_link[i] = 0;
			oplus_score_link_num--;
			printk("[oplus_score]:oplus_score_del_link remove link_index = %u.\n", link_index);
		}
	}

	spin_lock_bh(&uplink_score_lock);
	for (j = 0; j < MAX_LINK_APP_NUM; j++) {
		if(uplink_score_info[j].link_index == link_index) {
			memset(&uplink_score_info[j], 0, sizeof(struct uplink_score_info_st));
		}
	}
	spin_unlock_bh(&uplink_score_lock);

	spin_lock_bh(&downlink_score_lock);
	for (j = 0; j < MAX_LINK_APP_NUM; j++) {
		if(downlink_score_info[j].link_index == link_index) {
			memset(&downlink_score_info[j], 0, sizeof(struct downlink_score_info_st));
		}
	}
	spin_unlock_bh(&downlink_score_lock);

	return;
}

static void oplus_score_clear_link(struct nlattr *nla)
{
	int i;

	spin_lock_bh(&uplink_score_lock);
	for (i = 0; i < MAX_LINK_NUM; i++) {
		memset(&uplink_score_info[i], 0, sizeof(struct uplink_score_info_st));
	}
	oplus_score_uplink_num = 0;
	spin_unlock_bh(&uplink_score_lock);

	spin_lock_bh(&downlink_score_lock);
	for (i = 0; i < MAX_LINK_NUM; i++) {
		memset(&downlink_score_info[i], 0, sizeof(struct downlink_score_info_st));
	}
	oplus_score_downlink_num = 0;
	spin_unlock_bh(&downlink_score_lock);

	return;
}

static void oplus_score_config(struct nlattr *nla)
{
	struct score_param_st *config;
	config = (struct score_param_st*)NLA_DATA(nla);
	oplus_score_param_info = *config;
	oplus_score_debug = oplus_score_param_info.score_debug;
	return;
}

static int oplus_score_netlink_rcv_msg(struct sk_buff *skb, struct genl_info *info)
{
	int ret = 0;
	struct nlmsghdr *nlhdr;
	struct genlmsghdr *genlhdr;
	struct nlattr *nla;

	nlhdr = nlmsg_hdr(skb);
	genlhdr = nlmsg_data(nlhdr);
	nla = genlmsg_data(genlhdr);

	oplus_score_user_pid = nlhdr->nlmsg_pid;
	if (oplus_score_debug) {
		printk("[oplus_score]:set oplus_score_user_pid=%u.\n", oplus_score_user_pid);
	}

	/* to do: may need to some head check here*/
	if (oplus_score_debug) {
		printk("[oplus_score]:score_netlink_rcv_msg type=%u.\n", nla->nla_type);
	}
	switch (nla->nla_type) {
	case OPLUS_SCORE_MSG_ENABLE:
		oplus_score_enable(nla);
		break;
	case OPLUS_SCORE_MSG_ADD_UID:
		oplus_score_add_uid(nla);
		break;
	case OPLUS_SCORE_MSG_REMOVE_UID:
		oplus_score_remove_uid(nla);
		break;
	case OPLUS_SCORE_MSG_REQUEST_SCORE:
		oplus_score_request_score(nla);
		break;
	case OPLUS_SCORE_MSG_ADD_LINK:
		oplus_score_add_link(nla);
		break;
	case OPLUS_SCORE_MSG_DEL_LINK:
		oplus_score_del_link(nla);
		break;
	case OPLUS_SCORE_MSG_CLEAR_LINK:
		oplus_score_clear_link(nla);
		break;
	case OPLUS_SCORE_MSG_CONFIG:
		oplus_score_config(nla);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int oplus_score_netlink_init(void)
{
	int ret;
	ret = genl_register_family(&oplus_score_genl_family);
	if (ret) {
		printk("[oplus_score]:genl_register_family:%s failed,ret = %d\n", OPLUS_SCORE_FAMILY_NAME, ret);
		return ret;
	} else {
		printk("[oplus_score]:genl_register_family complete, id = %d!\n", oplus_score_genl_family.id);
	}

	return 0;
}

static void oplus_score_netlink_exit(void)
{
	genl_unregister_family(&oplus_score_genl_family);
}

static int proc_set_test_link_index(struct ctl_table *ctl, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;
	u32 data[2];
	struct nlattr *nla = (struct nlattr*)data;
	u32 old_link_index = test_link_index;

	ret = proc_dointvec(ctl, write, buffer, lenp, ppos);
	if (oplus_score_debug) {
		printk("[oplus_score]:proc_set_test_link,write=%d,ret=%d\n", write, ret);
	}
	if (ret == 0) {
		if (test_link_index) {
			data[1] = test_link_index;
			oplus_score_add_link(nla);
		} else {
			data[1] = old_link_index;
			oplus_score_del_link(nla);
		}
	}

	return ret;
}

static struct ctl_table oplus_score_sysctl_table[] =
{
	{
		.procname	= "oplus_score_debug",
		.data		= &oplus_score_debug,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "oplus_para_rtt",
		.data		= &para_rtt,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "oplus_score_para_rate",
		.data		= &para_rate,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "oplus_score_para_loss",
		.data		= &para_loss,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "test_link_index",
		.data		= &test_link_index,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_set_test_link_index,
	},
	{ }
};

static int oplus_score_sysctl_init(void)
{
	oplus_score_table_hrd = register_net_sysctl(&init_net, "net/oplus_score", oplus_score_sysctl_table);
	return oplus_score_table_hrd == NULL ? -ENOMEM : 0;
}

static void oplus_score_param_init(void)
{
	oplus_score_uplink_num = 0;
	oplus_score_uplink_num = 0;
	oplus_score_enable_flag = 1;
	oplus_score_user_pid = 0;
	memset(oplus_score_foreground_uid, 0, sizeof(oplus_score_foreground_uid));
	memset(oplus_score_active_link, 0, sizeof(oplus_score_active_link));
	memset(&uplink_score_info, 0, sizeof(uplink_score_info));
	memset(&downlink_score_info, 0, sizeof(downlink_score_info));
	oplus_score_param_info.score_debug = 0;
	oplus_score_param_info.threshold_retansmit = 10;
	oplus_score_param_info.threshold_normal = 100;
	oplus_score_param_info.smooth_factor = 20;
	oplus_score_param_info.protect_score = 60;
	oplus_score_param_info.threshold_gap = 5;
}

static int __init oplus_score_init(void)
{
	int ret = 0;

	ret = oplus_score_netlink_init();
	if (ret < 0) {
		printk("[oplus_score]:init module failed to init netlink, ret =% d\n",  ret);
		return ret;
	} else {
		printk("[oplus_score]:init module init netlink successfully.\n");
	}

	oplus_score_param_init();
	spin_lock_init(&uplink_score_lock);
	spin_lock_init(&downlink_score_lock);
	ret = nf_register_net_hooks(&init_net, oplus_score_netfilter_ops, ARRAY_SIZE(oplus_score_netfilter_ops));
	if (ret < 0) {
		printk("oplus_score_init netfilter register failed, ret=%d\n", ret);
		oplus_score_netlink_exit();
		return ret;
	} else {
		printk("oplus_score_init netfilter register successfully.\n");
	}

	oplus_score_sysctl_init();
	oplus_score_report_timer_init();
	oplus_score_report_timer_start();
	return ret;
}

static void __exit oplus_score_fini(void)
{
	oplus_score_netlink_exit();
	nf_unregister_net_hooks(&init_net, oplus_score_netfilter_ops, ARRAY_SIZE(oplus_score_netfilter_ops));
	if (oplus_score_table_hrd) {
		unregister_net_sysctl_table(oplus_score_table_hrd);
	}
	oplus_score_report_timer_del();
}

module_init(oplus_score_init);
module_exit(oplus_score_fini);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("oplus_score");
