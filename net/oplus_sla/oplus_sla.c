/******************************************************************************
** Copyright (C), 2019-2029, OPLUS Mobile Comm Corp., Ltd
** VENDOR_EDIT, All rights reserved.
** File: - oplus_sla.c
** Description: sla
**
** Version: 1.0
** Date : 2018/04/03
** TAG: OPLUS_FEATURE_WIFI_SLA
** ------------------------------- Revision History: ----------------------------
** <author>                                <data>        <version>       <desc>
** ------------------------------------------------------------------------------
 *******************************************************************************/

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
#include <linux/netfilter_ipv4/ipt_REJECT.h>

#ifdef OPLUS_FEATURE_APP_MONITOR
#include <net/oplus/oplus_apps_monitor.h>
#endif /* OPLUS_FEATURE_APP_MONITOR */

#ifdef OPLUS_FEATURE_WIFI_ROUTERBOOST
#include <net/oplus/oplus_router_boost.h>
#endif /* OPLUS_FEATURE_WIFI_ROUTERBOOST */

#define MARK_MASK    0x0fff
#define RETRAN_MASK  0xf000
#define RTT_MASK     0xf000
#define GAME_UNSPEC_MASK 0x8000

#define WLAN_NUM        2
#define WLAN0_INDEX     0
#define WLAN1_INDEX     1
#define CELL_INDEX      2
#define DOWNLOAD_FLAG   5
#define MAX_SYN_RETRANS 5
#define RTT_NUM         5

#define NORMAL_RTT      100
#define BACK_OFF_RTT_1	200   //200ms
#define BACK_OFF_RTT_2	300   //300ms
#define SYN_RETRAN_RTT	300   //500ms
#define MAX_RTT         500

#define DNS_TIME            10
#define CALC_DEV_SPEED_TIME 1
#define DOWNLOAD_SPEED_TIME 2
#define RECALC_WEIGHT_TIME  5    //5 seconds to recalc weight
#define MAX_SYN_NEW_COUNT   10   //statitis new syn count max number
#define LITTLE_FLOW_TIME    60   //small flow detect time internal

#define ADJUST_SPEED_NUM    5
#define DOWNLOAD_SPEED      200   // download speed level min
#define VIDEO_SPEED         300
#define DUAL_WLAN_MAX_DOWNLOAD_SPEED      2200   // 2MB/S

#define MAX_CELLULAR_SPEED  500  //LTE CALC WEITHT MAX SPEED
#define MAX_WLAN_SPEED      1000  //WIFI CALC WEITHT MAX SPEED
#define LOW_RSSI_MAX_WLAN_SPEED 500


#define CALC_WEIGHT_MIN_SPEED_1 10 //10KB/s
#define CALC_WEIGHT_MIN_SPEED_2 50 //50KB/s
#define CALC_WEIGHT_MIN_SPEED_3 100 //100KB/s

#define GAME_NUM 7
#define IFACE_NUM 3
#define IFACE_LEN 16
#define WHITE_APP_BASE    100
#define DUAL_STA_APP_BASE 200
#define WHITE_APP_NUM     64
#define DUAL_STA_APP_NUM  256
#define MAX_GAME_RTT      300
#define MAX_DETECT_PKTS  100
#define UDP_RX_WIN_SIZE   20
#define TCP_RX_WIN_SIZE   5
#define TCP_DOWNLOAD_THRESHOLD  (100*1024)    //100KB/s
#define SLA_TIMER_EXPIRES HZ
#define MINUTE_LITTE_RATE  60     //60Kbit/s
#define INIT_APP_TYPE      0
#define UNKNOW_APP_TYPE    -1
#define WLAN_SCORE_BAD_NUM 5
#define GAME_SKB_TIME_OUT  120  //120s
#define WLAN_SCORE_GOOD    75
#define WLAN_SCORE_BAD      55
#define DUAL_WLAN_SCORE_BAD 55
#define DNS_MAX_NUM         10
#define WLAN_NETWORK_INVALID 10

#define MIN_GAME_RTT       10 //ms
#define ENABLE_TO_USER_TIMEOUT 25 //second

#define MAX_FIXED_VALUE_LEN     20

#define UID_MASK   100000


/* dev info struct
  * if we need to consider wifi RSSI ?if we need consider screen state?
*/
struct oplus_dev_info{
	bool need_up;
	bool need_disable;
	int max_speed;    //KB/S
	int download_speed;    //KB/S
	int dl_mx_speed;
	int download_num;
	int little_speed_num;
	int tmp_little_speed;
	int dl_little_speed; //for detect little speed
	int dual_wifi_download;
	int current_speed;
	int left_speed;
	int minute_speed; //kbit/s
	int download_flag;
	int congestion_flag;
	int if_up;
	int syn_retran;
	int wlan_score;
	int wlan_score_bad_count;
	int weight;
	int weight_state;
	int rtt_index;
	u32 mark;
	u32 avg_rtt;
	u32 sum_rtt;     //ms
	u32 sla_rtt_num;     //ms
	u32 sla_sum_rtt;     //ms
	u32 sla_avg_rtt;     //ms
	u64 total_bytes;
	u64 minute_rx_bytes;
	u64 dl_total_bytes;
	char dev_name[IFACE_LEN];
};

struct oplus_speed_calc{
	int speed;
	int speed_done;
	int ms_speed_flag;
	u64	rx_bytes;
	u64 bytes_time;
	u64 first_time;
	u64 last_time;
	u64 sum_bytes;
};

struct oplus_sla_game_info{
	u32 game_type;
	u32 uid;
	u32 rtt;
	u32 mark;
	u32 switch_time;
	u32 rtt_150_num;
	u32 rtt_200_num;
	u32 rtt_250_num;
	u64 rtt_normal_num;
	u64 cell_bytes;
};


struct oplus_white_app_info{
    u32 count;
    u32 uid[WHITE_APP_NUM];
	u64 cell_bytes[WHITE_APP_NUM];
	u64 cell_bytes_normal[WHITE_APP_NUM];
};

struct oplus_dual_sta_info{
    u32 count;
    u32 uid[DUAL_STA_APP_NUM];
};


struct oplus_game_online{
	bool game_online;
	struct timeval last_game_skb_tv;
	u32 udp_rx_pkt_count;   //count of all received game udp packets
	u64 tcp_rx_byte_count;  //count of all received game tcp bytes
};

struct game_traffic_info{
	bool game_in_front;
	u32 udp_rx_packet[UDP_RX_WIN_SIZE]; //udp packets received per second
	u64 tcp_rx_byte[TCP_RX_WIN_SIZE];   //tcp bytes received per second
	u32 window_index;
	u32 udp_rx_min; //min rx udp packets as valid game udp rx data
	u32 udp_rx_low_thr; //udp rx packet count low threshold
	u32 in_game_true_thr;  //greater than this -> inGame==true
	u32 in_game_false_thr;  //less than this -> inGame==false
	u32 rx_bad_thr; //greater than this -> game_rx_bad==true
};

struct oplus_syn_retran_statistic{
	u32 syn_retran_num;
	u32 syn_total_num;
};

struct sla_dns_statistic{
    bool in_timer;
	u32 send_num;
    struct timeval last_tv;
};

struct oplus_rate_limit_info{
	int front_uid;
	int rate_limit_enable; //oplus_rate_limit enable or not
	int disable_rtt_num;
	int disable_rtt_sum;
	int disable_rtt;  //oplus_rate_limit disable front avg rtt
	int enable_rtt_num;
	int enable_rtt_sum;
	int enable_rtt;  //oplus_rate_limit enable front avg rtt
	struct timeval last_tv;
};

struct oplus_sla_rom_update_info{
	u32 sla_speed;        //sla speed threshold
	u32 cell_speed;       //cell max speed threshold
	u32 wlan_speed;       //wlan max speed threshold;
	u32 wlan_little_score_speed; //wlan little score speed threshold;
	u32 sla_rtt;          //sla rtt threshold
	u32 wzry_rtt;         //wzry rtt threshold
	u32 cjzc_rtt;         //cjzc rtt  threshold
	u32 wlan_bad_score;   //wifi bad score threshold
	u32 wlan_good_score;  //wifi good socre threshold
	u32 second_wlan_speed;       //wlan max speed threshold
	u32 dual_wlan_download_speed;   //dual wifi download max speed
	u32 dual_wifi_rtt;          //sla dual wifi rtt threshold
	u32 dual_wlan_bad_score;   //dual wifi bad score threshold
};

struct oplus_sla_game_rtt_params {
	int game_index;
	int tx_offset;
	int tx_len;
	u8  tx_fixed_value[MAX_FIXED_VALUE_LEN];
	int rx_offset;
	int rx_len;
	u8  rx_fixed_value[MAX_FIXED_VALUE_LEN];
};

struct oplus_smart_bw_rom_update_info {
	u32 feature_enable;
	u32 debug_level;
	u32 ll_com_score_thre;
	u32 acs_weight_thre;
	u32 mov_avg_beta;
	u32 thre_tune_dist;
	u32 sample_interval;
	u32 good_mcs;
	u32 bad_mcs;
};

enum{
	SLA_SKB_ACCEPT,
	SLA_SKB_CONTINUE,
	SLA_SKB_MARKED,
	SLA_SKB_REMARK,
	SLA_SKB_DROP,
};

enum{
	SLA_WEIGHT_NORMAL,
	SLA_WEIGHT_RECOVERY,
};

enum{
	WEIGHT_STATE_NORMAL,
	WEIGHT_STATE_USELESS,
	WEIGHT_STATE_RECOVERY,
	WEIGHT_STATE_SCORE_INVALID,
};

enum{
	CONGESTION_LEVEL_NORMAL,
	CONGESTION_LEVEL_MIDDLE,
	CONGESTION_LEVEL_HIGH,
};

enum{
	WLAN_SCORE_LOW,
	WLAN_SCORE_HIGH,
};


enum{

	WLAN0_MARK_BIT = 8,            //WLAN mark value,mask 0x0fff
	WLAN0_MARK = (1 << WLAN0_MARK_BIT),

	WLAN1_MARK_BIT = 9,            //WLAN mark value,mask 0x0fff
	WLAN1_MARK = (1 << WLAN1_MARK_BIT),

	CELL_MARK_BIT = 10,       //cellular mark value  mask 0x0fff
	CELL_MARK = (1 << CELL_MARK_BIT),

	RETRAN_BIT = 12,             //first retran mark value,  mask 0xf000
	RETRAN_MARK = (1 << RETRAN_BIT),

	RETRAN_SECOND_BIT = 13,     //second retran mark value, mask 0xf000
	RETRAN_SECOND_MARK = (1 << RETRAN_SECOND_BIT),

	RTT_MARK_BIT = 14,          //one ct only statitisc once rtt,mask 0xf000
	RTT_MARK = (1 << RTT_MARK_BIT),

	GAME_UNSPEC_MARK_BIT = 15,          //mark game skb when game not start
	GAME_UNSPEC_MARK = (1 << GAME_UNSPEC_MARK_BIT),
};


/*NLMSG_MIN_TYPE is 0x10,so we start at 0x11*/
enum{
	SLA_NOTIFY_WIFI_SCORE = 0x11,
	SLA_NOTIFY_PID = 0x12,
	SLA_ENABLE = 0x13,
	SLA_DISABLE = 0x14,
	SLA_IFACE_CHANGED = 0x15,
	SLA_NOTIFY_APP_UID = 0x1A,
	SLA_NOTIFY_GAME_RTT = 0x1B,
	SLA_NOTIFY_WHITE_LIST_APP = 0x1C,
	SLA_ENABLED = 0x1D,
	SLA_DISABLED = 0x1E,
	SLA_ENABLE_GAME_RTT = 0x1F,
	SLA_DISABLE_GAME_RTT = 0x20,
	SLA_NOTIFY_SWITCH_STATE = 0x21,
	SLA_NOTIFY_SPEED_RTT = 0x22,
	SLA_SWITCH_GAME_NETWORK  = 0x23,
	SLA_NOTIFY_SCREEN_STATE	= 0x24,
	SLA_NOTIFY_CELL_QUALITY	= 0x25,
	SLA_SHOW_DIALOG_NOW = 0x26,
	SLA_NOTIFY_SHOW_DIALOG = 0x27,
	SLA_SEND_WHITE_LIST_APP_TRAFFIC = 0x28,
	SLA_SEND_GAME_APP_STATISTIC = 0x29,
	SLA_GET_SYN_RETRAN_INFO = 0x2A,
	SLA_GET_SPEED_UP_APP = 0x2B,
	SLA_SET_DEBUG = 0x2C,
	SLA_NOTIFY_DEFAULT_NETWORK = 0x2D,
	SLA_NOTIFY_PARAMS = 0x2E,
	SLA_NOTIFY_GAME_STATE = 0x2F,
	SLA_NOTIFY_GAME_PARAMS = 0x30,
	SLA_NOTIFY_GAME_RX_PKT = 0x31,
	SLA_NOTIFY_GAME_IN_FRONT = 0x32,
	SLA_NOTIFY_PRIMARY_WIFI = 0x33,
	SLA_NOTIFY_DUAL_STA_APP = 0x34,
    //Add for WLAN Assistant Four Issues
	SLA_WEIGHT_BY_WLAN_ASSIST = 0x35,
    //end add
	SLA_NOTIFY_VPN_CONNECTED = 0x36,
	SLA_NOTIFY_DOWNLOAD_APP = 0x37,
	SLA_NOTIFY_VEDIO_APP = 0x38,
	SLA_LIMIT_SPEED_ENABLE = 0x39,
	SLA_LIMIT_SPEED_DISABLE = 0x40,
	SLA_LIMIT_SPEED_FRONT_UID = 0x41,
	SMART_BW_SET_PARAMS = 0x42,
	#ifdef OPLUS_FEATURE_WIFI_ROUTERBOOST
	SLA_NOTIFY_ROUTER_BOOST_DUPPKT_PARAMS = 0x43,
	#endif /* OPLUS_FEATURE_WIFI_ROUTERBOOST */
};


enum{
	GAME_SKB_DETECTING = 0,
	GAME_SKB_COUNT_ENOUGH = 1,
	GAME_RTT_STREAM = 2,
	GAME_VOICE_STREAM = 4,
};

enum{
	GAME_UNSPEC = 0,
	GAME_WZRY = 1,
	GAME_CJZC,
	GAME_QJCJ,
	GAME_HYXD_NM,
	GAME_HYXD,
	GAME_HYXD_ALI,
};

enum{
	SLA_MODE_INIT = 0,
	SLA_MODE_DUAL_WIFI = 1,

	/*if the dual wifi is enable,please do not send
	disable msg to kernel when rcv SLA_MODE_WIFI_CELL msg*/
	SLA_MODE_WIFI_CELL = 2,
	SLA_MODE_DUAL_WIFI_CELL = 3,

	SLA_MODE_FINISH = 4,
};


enum{
	DISABLE_DUAL_WIFI = 1,
	DISABLE_WIFI_CELL = 2,
};

//add for dual sta DCS
enum{
	INIT_ACTIVE_TYPE,
	LOW_SPEED_HIGH_RTT,
	LOW_WLAN_SCORE,
	LOW_DL_SPEED,
	WLAN_DOWNLOAD,
};

static bool enable_cell_to_user = 0;
static bool enable_second_wifi_to_user = 0;
static int oplus_sla_vpn_connected = 0;
static int game_mark = 0;
static bool inGame = false;
static bool game_cell_to_wifi = false;
static bool game_rx_bad = false;
static int udp_rx_show_toast = 0;
static int game_rtt_show_toast = 0;
int tee_use_src = 0;
static int oplus_sla_enable;
static int oplus_sla_debug = 0;
static int oplus_sla_calc_speed;
static int oplus_sla_def_net = 0;    //WLAN0->0 WLAN1->1 CELL->2
static int send_show_dailog_msg = 0;
static int game_start_state[GAME_NUM];
static int MAIN_WLAN = WLAN0_INDEX;
static int SECOND_WLAN = WLAN1_INDEX;
static int MAIN_WLAN_MARK = WLAN0_MARK;
static int SECOND_WLAN_MARK = WLAN1_MARK;
static int main_wlan_download = 1;
//add for android Q statictis tcp tx and tx
static u64 wlan0_tcp_tx = 0;
static u64 wlan0_tcp_rx = 0;
static u64 wlan1_tcp_tx = 0;
static u64 wlan1_tcp_rx = 0;

static int sla_work_mode = SLA_MODE_INIT;
static int sla_detect_mode = SLA_MODE_DUAL_WIFI;

static int init_weight_delay_count = 0;
static int dual_wifi_active_type = 0;

static bool dual_wifi_switch_enable = true;
static bool sla_switch_enable = false;
static bool sla_screen_on = true;
static bool cell_quality_good = true;
static bool need_show_dailog = true;

static volatile u32 oplus_sla_pid;
static struct sock *oplus_sla_sock;
static struct timer_list sla_timer;

static struct timeval last_speed_tv;
static struct timeval last_weight_tv;
static struct timeval last_minute_speed_tv;
static struct timeval last_download_speed_tv;
static struct timeval last_enable_cellular_tv;
static struct timeval calc_wlan_rtt_tv;
static struct timeval last_enable_second_wifi_tv;
static struct timeval last_enable_cell_tv;

static struct timeval last_show_daillog_msg_tv;
static struct timeval last_calc_small_speed_tv;

static struct sla_dns_statistic dns_info[IFACE_NUM];
static struct oplus_rate_limit_info rate_limit_info;
static struct oplus_game_online game_online_info;
static struct oplus_white_app_info white_app_list;
static struct oplus_dual_sta_info dual_wifi_app_list;
static struct oplus_dual_sta_info download_app_list;
static struct oplus_dual_sta_info vedio_app_list;
static struct oplus_sla_game_info game_uid[GAME_NUM];
static struct oplus_dev_info oplus_sla_info[IFACE_NUM];
static struct oplus_speed_calc oplus_speed_info[IFACE_NUM];
static struct oplus_syn_retran_statistic syn_retran_statistic;

static struct work_struct oplus_sla_work;
static struct workqueue_struct *workqueue_sla;

static DEFINE_MUTEX(sla_netlink_mutex);
static struct ctl_table_header *oplus_sla_table_hrd;

/*we statistic rtt when tcp state is TCP_ESTABLISHED,for somtimes(when the network has qos to let syn pass first)
  * the three handshark (syn-synack) rtt is  good but the network is worse.
*/
extern void (*statistic_dev_rtt)(struct sock *sk,long rtt);

/*sometimes when skb reject by iptables,
*it will retran syn which may make the rtt much high
*so just mark the stream(ct) with mark IPTABLE_REJECT_MARK when this happens
*/
extern void (*mark_streams_for_iptables_reject)(struct sk_buff *skb,enum ipt_reject_with);


static rwlock_t sla_lock;
static rwlock_t sla_rtt_lock;
static rwlock_t sla_game_lock;


#define sla_read_lock() 			read_lock_bh(&sla_lock);
#define sla_read_unlock() 			read_unlock_bh(&sla_lock);
#define sla_write_lock() 			write_lock_bh(&sla_lock);
#define sla_write_unlock()			write_unlock_bh(&sla_lock);

#define sla_rtt_write_lock() 		write_lock_bh(&sla_rtt_lock);
#define sla_rtt_write_unlock()		write_unlock_bh(&sla_rtt_lock);

#define sla_game_write_lock() 		write_lock_bh(&sla_game_lock);
#define sla_game_write_unlock()		write_unlock_bh(&sla_game_lock);


static struct oplus_sla_rom_update_info rom_update_info ={
	.sla_speed = 200,
	.cell_speed = MAX_CELLULAR_SPEED,
	.wlan_speed = MAX_WLAN_SPEED,
	.wlan_little_score_speed = LOW_RSSI_MAX_WLAN_SPEED,
	.sla_rtt = 230, /*test with 230ms*/
	.wzry_rtt = 200,
	.cjzc_rtt = 220,
	.wlan_bad_score = WLAN_SCORE_BAD,
	.wlan_good_score = WLAN_SCORE_GOOD,
	.second_wlan_speed = (2 * MAX_WLAN_SPEED),
	.dual_wlan_download_speed = DUAL_WLAN_MAX_DOWNLOAD_SPEED,
	.dual_wifi_rtt = 200,
	.dual_wlan_bad_score = DUAL_WLAN_SCORE_BAD,
};

static struct oplus_sla_game_rtt_params game_params[GAME_NUM];

static struct game_traffic_info default_traffic_info = {
	.game_in_front = 0,
	//.udp_rx_packet[UDP_RX_WIN_SIZE];
	//.tcp_rx_byte[TCP_RX_WIN_SIZE];
	.window_index = 0,
	.udp_rx_min = 3,
	.udp_rx_low_thr = 12,
	.in_game_true_thr = 15,
	.in_game_false_thr = 10,
	.rx_bad_thr = 4,
};

static struct game_traffic_info wzry_traffic_info = {
	.game_in_front = 0,
	//.udp_rx_packet[UDP_RX_WIN_SIZE];
	//.tcp_rx_byte[TCP_RX_WIN_SIZE];
	.window_index = 0,
	.udp_rx_min = 3,
	.udp_rx_low_thr = 12,
	.in_game_true_thr = 15,
	.in_game_false_thr = 10,
	.rx_bad_thr = 4,
};

static struct game_traffic_info cjzc_traffic_info = {
	.game_in_front = 0,
	//.udp_rx_packet[UDP_RX_WIN_SIZE];
	//.tcp_rx_byte[TCP_RX_WIN_SIZE];
	.window_index = 0,
	.udp_rx_min = 3,
	.udp_rx_low_thr = 14,
	.in_game_true_thr = 15,
	.in_game_false_thr = 10,
	.rx_bad_thr = 4,
};

/* for Smart BW RUS related */
static struct oplus_smart_bw_rom_update_info smart_bw_rom_update_info;
static bool get_smartbw_romupdate = false;
static int oplus_smart_bw_set_params(struct nlmsghdr *nlh)
{
	u32* params = (u32 *)NLMSG_DATA(nlh);
	u32 count = (nlh)->nlmsg_len - NLMSG_HDRLEN; /* this is in fact the payload length which already aligned */
	if (1) {
	        printk("oplus_smart_bw_set_params: (nlh)->nlmsg_len = %u, NLMSG_HDRLEN=%d,"
	                "NLMSG_PAYLOAD(nlh, NLMSG_HDRLEN) = %u, sizeof(oplus_smart_bw_rom) = %lu",
	                (nlh)->nlmsg_len, NLMSG_HDRLEN, count, sizeof(struct oplus_smart_bw_rom_update_info));
	}
#if 1
	if (count == sizeof(struct oplus_smart_bw_rom_update_info)) {
#endif
		get_smartbw_romupdate = true;
		smart_bw_rom_update_info.feature_enable = params[0];
		smart_bw_rom_update_info.debug_level = params[1];
		smart_bw_rom_update_info.ll_com_score_thre = params[2];
		smart_bw_rom_update_info.acs_weight_thre = params[3];
		smart_bw_rom_update_info.mov_avg_beta = params[4];
		smart_bw_rom_update_info.thre_tune_dist = params[5];
		smart_bw_rom_update_info.sample_interval = params[6];
		smart_bw_rom_update_info.good_mcs = params[7];
		smart_bw_rom_update_info.bad_mcs = params[8];
		if (smart_bw_rom_update_info.debug_level == 2 && oplus_sla_debug) {
				printk("oplus_smart_bw_set_params:set params count=%d params[0] = %d, params[1] = %d, params[2] = %d, params[3] = %d,"
						"params[4] = %d, params[5] = %d, params[6] = %d, params[7] = %d, params[8] = %d",
						count, params[0], params[1], params[2], params[3], params[4],
						params[5], params[6], params[7], params[8]);
		}
#if 1
	} else {
		printk("oplus_smart_bw_set_params:set params invalid param count:%d", count);
	}
#endif
	return	0;
}

/* Get RUS rom update which comes from FWK, true: success */
bool get_smart_bw_rom_update(int payload[], int len)
{
	if (len < sizeof(struct oplus_smart_bw_rom_update_info) / sizeof(int) || !get_smartbw_romupdate) {
	        if (1) printk("%s target payload len not match or hasn't got ROM update from FWK !\n", __func__);
	        return false;
	}

	if (1) printk("%s enter!\n", __func__);
	memcpy(payload, &smart_bw_rom_update_info, sizeof(struct oplus_smart_bw_rom_update_info));

	if (1) printk("%s exit!\n", __func__);
	return  true;
}

EXPORT_SYMBOL(get_smart_bw_rom_update);
/* send to user space */
static int oplus_sla_send_to_user(int msg_type,char *payload,int payload_len)
{
	int ret = 0;
	struct sk_buff *skbuff;
	struct nlmsghdr *nlh;

    if (!oplus_sla_pid) {
		printk("oplus_sla_netlink: oplus_sla_pid == 0!!\n");
		return -1;
	}

	/*allocate new buffer cache */
	skbuff = alloc_skb(NLMSG_SPACE(payload_len), GFP_ATOMIC);
	if (skbuff == NULL) {
		printk("oplus_sla_netlink: skbuff alloc_skb failed\n");
		return -1;
	}

	/* fill in the data structure */
	nlh = nlmsg_put(skbuff, 0, 0, msg_type, NLMSG_ALIGN(payload_len), 0);
	if (nlh == NULL) {
		printk("oplus_sla_netlink:nlmsg_put failaure\n");
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
	ret = netlink_unicast(oplus_sla_sock, skbuff, oplus_sla_pid, MSG_DONTWAIT);
	if(ret < 0){
		printk(KERN_ERR "oplus_sla_netlink: can not unicast skbuff,ret = %d\n",ret);
		return 1;
	}

	return 0;
}

static void calc_network_congestion(void)
{
	int i = 0;
	int avg_rtt = 0;
	int rtt_num = 2;

	if(oplus_sla_info[MAIN_WLAN].wlan_score <= rom_update_info.wlan_bad_score){
		rtt_num = 1;
	}

	sla_rtt_write_lock();
	for(i = 0; i < IFACE_NUM; i++){
		if(oplus_sla_info[i].if_up){

			if(oplus_sla_info[i].sla_rtt_num >= rtt_num){
				avg_rtt = 0;
				if(oplus_sla_info[i].download_flag < DOWNLOAD_FLAG){
					avg_rtt = oplus_sla_info[i].sla_sum_rtt / oplus_sla_info[i].sla_rtt_num;
					oplus_sla_info[i].sla_avg_rtt = avg_rtt;
				}

				if(oplus_sla_debug){
				        printk("oplus_sla_rtt: sla rtt ,num = %d,sum = %d,avg rtt[%d] = %d\n",
					 		oplus_sla_info[i].sla_rtt_num,oplus_sla_info[i].sla_sum_rtt,i,avg_rtt);
				}

				oplus_sla_info[i].sla_rtt_num = 0;
				oplus_sla_info[i].sla_sum_rtt = 0;

				if(avg_rtt >= BACK_OFF_RTT_2){

					oplus_sla_info[i].congestion_flag = CONGESTION_LEVEL_HIGH;
					oplus_sla_info[i].max_speed /= 2;
					oplus_sla_info[i].download_speed /= 2;
				}
				else if(avg_rtt >= BACK_OFF_RTT_1){

					oplus_sla_info[i].congestion_flag = CONGESTION_LEVEL_MIDDLE;
					oplus_sla_info[i].max_speed /= 2;
					oplus_sla_info[i].download_speed /= 2;
				}
				else{
					if(WEIGHT_STATE_NORMAL == oplus_sla_info[i].weight_state){
						oplus_sla_info[i].congestion_flag = CONGESTION_LEVEL_NORMAL;
					}
				}
			}
		}
	}
	sla_rtt_write_unlock();

	return;
}

static void init_dual_wifi_weight(void)
{
	init_weight_delay_count = 5;
	if (INIT_ACTIVE_TYPE != dual_wifi_active_type &&
		(oplus_sla_info[MAIN_WLAN].sla_avg_rtt >= BACK_OFF_RTT_2 ||
		oplus_sla_info[MAIN_WLAN].max_speed <= CALC_WEIGHT_MIN_SPEED_2)){
		oplus_sla_info[MAIN_WLAN].weight = 0;
		oplus_sla_info[SECOND_WLAN].weight = 100;
	} else if (LOW_SPEED_HIGH_RTT == dual_wifi_active_type ||
		LOW_WLAN_SCORE == dual_wifi_active_type ||
		LOW_DL_SPEED == dual_wifi_active_type){
		oplus_sla_info[MAIN_WLAN].weight = 30;
		oplus_sla_info[SECOND_WLAN].weight = 100;
	} else if (WLAN_DOWNLOAD == dual_wifi_active_type){
	    // for download active
		oplus_sla_info[MAIN_WLAN].weight = 50;
		oplus_sla_info[SECOND_WLAN].weight = 100;
	} else {
	    // for manual active or networkReuqest
		init_weight_delay_count = 0;
		oplus_sla_info[MAIN_WLAN].weight = 100;
		oplus_sla_info[SECOND_WLAN].weight = 0;
	}
	printk("oplus_sla_weight:init_dual_wifi_weight [%d] [%d]",
		oplus_sla_info[MAIN_WLAN].weight,oplus_sla_info[SECOND_WLAN].weight);
}

static void init_wifi_cell_weight(void)
{
	init_weight_delay_count = 10;
	oplus_sla_info[MAIN_WLAN].weight = 0;
	oplus_sla_info[CELL_INDEX].weight = 100;
	printk("oplus_sla_weight:init_wifi_cell_weight [%d] [%d]",
		oplus_sla_info[MAIN_WLAN].weight,oplus_sla_info[CELL_INDEX].weight);
}

static void init_dual_wifi_cell_weight(void)
{
	if (oplus_sla_info[MAIN_WLAN].sla_avg_rtt >= BACK_OFF_RTT_2){
		oplus_sla_info[MAIN_WLAN].weight = 0;

		if (oplus_sla_info[SECOND_WLAN].sla_avg_rtt >= BACK_OFF_RTT_2) {
			oplus_sla_info[SECOND_WLAN].weight = 0;
		}
		else {
			oplus_sla_info[SECOND_WLAN].weight = 30;
		}

		oplus_sla_info[CELL_INDEX].weight = 100;
	} else {

		if (oplus_sla_info[SECOND_WLAN].sla_avg_rtt >= BACK_OFF_RTT_2) {
			oplus_sla_info[MAIN_WLAN].weight = 30;
			oplus_sla_info[SECOND_WLAN].weight = 0;
		}
		else {
			oplus_sla_info[MAIN_WLAN].weight = 15;
			oplus_sla_info[SECOND_WLAN].weight = 30;
		}
		oplus_sla_info[CELL_INDEX].weight = 100;
	}
}

static void init_oplus_sla_weight(struct timeval tv,int work_mode)
{
	last_weight_tv = tv;

	if (SLA_MODE_DUAL_WIFI == work_mode) {
		init_dual_wifi_weight();
	}
	else if (SLA_MODE_WIFI_CELL == work_mode) {
		init_wifi_cell_weight();
	}
	else if (SLA_MODE_DUAL_WIFI_CELL == work_mode) {
		init_dual_wifi_cell_weight();
	}
}

static void send_enable_to_framework(int enable_mode,int active_type)
{
	int payload[2];
	if (SLA_MODE_DUAL_WIFI == enable_mode) {
		enable_second_wifi_to_user = true;
		do_gettimeofday(&last_enable_second_wifi_tv);
	}
	else if (SLA_MODE_WIFI_CELL == enable_mode) {
		enable_cell_to_user = true;
		do_gettimeofday(&last_enable_cell_tv);
	}

	if(oplus_sla_info[MAIN_WLAN].need_up){
		oplus_sla_info[MAIN_WLAN].if_up = 1;
	}

	if(oplus_sla_info[SECOND_WLAN].need_up){
		oplus_sla_info[SECOND_WLAN].if_up = 1;
	}

	payload[0] = enable_mode;
	payload[1] = active_type;
	oplus_sla_send_to_user(SLA_ENABLE,(char *)payload,sizeof(payload));
	printk("oplus_sla_netlink:mode[%d] [%d] send SLA_ENABLE to user\n",enable_mode, active_type);
}

static int enable_oplus_sla_module(struct nlmsghdr *nlh)
{
	int *data = (int *)NLMSG_DATA(nlh);
	int enable_type = data[0];
	struct timeval tv;

	sla_write_lock();

	do_gettimeofday(&tv);
	last_enable_cellular_tv = tv;
	init_oplus_sla_weight(tv,enable_type);

	oplus_sla_enable = 1;
	sla_work_mode = enable_type;
	if (SLA_MODE_DUAL_WIFI == enable_type) {
		sla_detect_mode = SLA_MODE_WIFI_CELL;
	}
	else if (SLA_MODE_WIFI_CELL == enable_type) {
		sla_detect_mode = SLA_MODE_FINISH;
	}

	printk("oplus_sla_netlink: enable type = %d\n",enable_type);
	oplus_sla_send_to_user(SLA_ENABLED, (char *)&enable_type, sizeof(int));

	sla_write_unlock();
	return 0;
}

static int enable_oplus_limit_speed()
{
	sla_write_lock();
	rate_limit_info.rate_limit_enable = 1;
	printk("enable_oplus_limit_speed ");
	sla_write_unlock();
	return 0;
}

static int disable_oplus_limit_speed()
{
	sla_write_lock();
	rate_limit_info.rate_limit_enable = 0;
	printk("disable_oplus_limit_speed ");
	sla_write_unlock();
	return 0;
}

static int oplus_limit_uid_changed(struct nlmsghdr *nlh)
{
	int uid = -1;
	int *data = (int *)NLMSG_DATA(nlh);
	uid = data[0];
	printk("oplus_limit_uid_changed uid:%d\n", uid);
	sla_write_lock();
	rate_limit_info.front_uid = uid;
	sla_write_unlock();

	return 0;
}

//add for rate limit function to statistics front uid rtt
static void statistics_front_uid_rtt(int rtt,struct sock *sk)
{
	kuid_t uid;
	const struct file *filp = NULL;
	if(sk && sk_fullsock(sk)){

		if(NULL == sk->sk_socket){
			return;
		}

		filp = sk->sk_socket->file;
		if(NULL == filp){
			return;
		}

		if(rate_limit_info.front_uid){
			uid = make_kuid(&init_user_ns, rate_limit_info.front_uid);
			if(uid_eq(filp->f_cred->fsuid, uid)){
				if(rate_limit_info.rate_limit_enable){
					rate_limit_info.enable_rtt_num++;
					rate_limit_info.enable_rtt_sum += rtt;
				} else{
					rate_limit_info.disable_rtt_num++;
					rate_limit_info.disable_rtt_sum += rtt;
				}
			}
		}
	}
	return;
}

static void calc_rtt_by_dev_index(int index, int tmp_rtt, struct sock *sk)
{

	/*do not calc rtt when the screen is off which may make the rtt too big
	*/
	if(!sla_screen_on){
	   return;
	}

	if(tmp_rtt < 30) {
		return;
	}

	if(tmp_rtt > MAX_RTT){
		tmp_rtt = MAX_RTT;
	}

	sla_rtt_write_lock();

	statistics_front_uid_rtt(tmp_rtt,sk);

	if(!rate_limit_info.rate_limit_enable){
		oplus_sla_info[index].rtt_index++;
		oplus_sla_info[index].sum_rtt += tmp_rtt;
	}

	sla_rtt_write_unlock();
	return;

}


static int find_dev_index_by_mark(__u32 mark)
{
	int i;

	for(i = 0; i < IFACE_NUM; i++){
		if(oplus_sla_info[i].if_up &&
			mark == oplus_sla_info[i].mark){
			return i;
		}
	}

	return -1;
}

static int calc_retran_syn_rtt(struct sk_buff *skb, struct nf_conn *ct)
{
	int index = -1;
	int ret = SLA_SKB_CONTINUE;
	int tmp_mark = ct->mark & MARK_MASK;
	int rtt_mark = ct->mark & RTT_MASK;

	if(rtt_mark & RTT_MARK){
		skb->mark = ct->mark;
		return SLA_SKB_MARKED;
	}

	index = find_dev_index_by_mark(tmp_mark);

	if(-1 != index) {

		calc_rtt_by_dev_index(index, SYN_RETRAN_RTT, NULL);

		//oplus_sla_info[index].syn_retran++;
		syn_retran_statistic.syn_retran_num++;

		ct->mark |= RTT_MARK;
		skb->mark = ct->mark;

		ret = SLA_SKB_MARKED;
	}

	syn_retran_statistic.syn_total_num++;

	return ret;
}

/*
LAN IP:
A:10.0.0.0-10.255.255.255
B:172.16.0.0-172.31.255.255
C:192.168.0.0-192.168.255.255
*/
static bool dst_is_lan_ip(struct sk_buff *skb)
{
	struct iphdr *iph = NULL;
	unsigned char *dstip = NULL;

	iph = ip_hdr(skb);
	if (NULL != iph) {
		dstip = (unsigned char *)&iph->daddr;
		if ((10 == dstip[0]) ||
			(192 == dstip[0] && 168 == dstip[1]) ||
			(172 == dstip[0] && dstip[1] >= 16 && dstip[1] <= 31)) {
			return true;
		}
	}
	return false;
}

/*icsk_syn_retries*/
static int syn_retransmits_packet_do_specail(struct sock *sk,
				struct nf_conn *ct,
				struct sk_buff *skb)
{

	int ret = SLA_SKB_CONTINUE;
	struct iphdr *iph;
	struct tcphdr *th = NULL;

	if((iph = ip_hdr(skb)) != NULL &&
		iph->protocol == IPPROTO_TCP){

		th = tcp_hdr(skb);
		//only statictis SYN retran packet, sometimes some RST packets also arrive here
		if(NULL != th && th->syn &&
		   !th->rst && !th->ack && !th->fin){

			ret = calc_retran_syn_rtt(skb, ct);
		}
	}

	return ret;
}


static bool is_download_app(kuid_t app_uid)
{
	int i = 0;
	kuid_t uid;

	for(i = 0;i < download_app_list.count;i++){
		if(download_app_list.uid[i]){
			uid = make_kuid(&init_user_ns,download_app_list.uid[i]);
			if(uid_eq(app_uid, uid)){
				return true;
			}
		}
	}
	return false;
}

static int mark_download_app(struct sock *sk,
				kuid_t app_uid,
				struct nf_conn *ct,
				struct sk_buff *skb)
{
	int choose_mark = 0;
	int ret = SLA_SKB_CONTINUE;

	if (SLA_MODE_DUAL_WIFI == sla_work_mode &&
		is_download_app(app_uid)) {
		// for manual active
		if (INIT_ACTIVE_TYPE == dual_wifi_active_type) {
			if ((oplus_sla_info[MAIN_WLAN].max_speed < CALC_WEIGHT_MIN_SPEED_3 &&
				oplus_sla_info[SECOND_WLAN].max_speed < CALC_WEIGHT_MIN_SPEED_3 &&
				oplus_sla_info[MAIN_WLAN].sla_avg_rtt < NORMAL_RTT) ||
				(oplus_sla_info[MAIN_WLAN].download_flag < DOWNLOAD_FLAG &&
				oplus_sla_info[MAIN_WLAN].download_speed >= DUAL_WLAN_MAX_DOWNLOAD_SPEED)) {

				choose_mark= MAIN_WLAN_MARK;

			} else if (100 == oplus_sla_info[MAIN_WLAN].weight &&
				oplus_sla_info[MAIN_WLAN].download_flag >= DOWNLOAD_FLAG &&
				oplus_sla_info[SECOND_WLAN].download_flag < DOWNLOAD_FLAG) {
				choose_mark = SECOND_WLAN_MARK;
			}
		} else {
			if ((init_weight_delay_count > 0 &&
				oplus_sla_info[SECOND_WLAN].download_flag < DOWNLOAD_FLAG) ||
				(oplus_sla_info[MAIN_WLAN].download_flag < DOWNLOAD_FLAG &&
				 oplus_sla_info[SECOND_WLAN].download_flag < DOWNLOAD_FLAG &&
				 oplus_sla_info[MAIN_WLAN].download_speed >= MAX_WLAN_SPEED &&
				 oplus_sla_info[SECOND_WLAN].download_speed >= MAX_WLAN_SPEED)) {
				if (main_wlan_download) {
					main_wlan_download = 0;
					choose_mark = MAIN_WLAN_MARK;
				} else if (!main_wlan_download) {
					main_wlan_download = 1;
					choose_mark = SECOND_WLAN_MARK;
				}
			}
		}
	}

	if (choose_mark) {
		skb->mark = choose_mark;
		ct->mark = skb->mark;
		sk->oplus_sla_mark = skb->mark;
		//sk->sk_mark = sk->oplus_sla_mark;
		return SLA_SKB_MARKED;
	}

	return ret;
}

static bool is_vedio_app(kuid_t app_uid)
{
	int i = 0;
	kuid_t uid;

	for(i = 0;i < vedio_app_list.count;i++){
		if(vedio_app_list.uid[i]){
			uid = make_kuid(&init_user_ns,vedio_app_list.uid[i]);
			if(uid_eq(app_uid, uid)){
				return true;
			}
		}
	}
	return false;
}

static int mark_video_app(struct sock *sk,
				kuid_t app_uid,
				struct nf_conn *ct,
				struct sk_buff *skb)
{
	int choose_mark = 0;
	int ret = SLA_SKB_CONTINUE;

	if (SLA_MODE_DUAL_WIFI == sla_work_mode &&
		is_vedio_app(app_uid)) {
		if (oplus_sla_info[MAIN_WLAN].download_speed >= MAX_WLAN_SPEED) {

			choose_mark = MAIN_WLAN_MARK;

		} else if (oplus_sla_info[SECOND_WLAN].download_speed > MAX_WLAN_SPEED ||
			     (oplus_sla_info[MAIN_WLAN].download_speed <= VIDEO_SPEED &&
			 ((init_weight_delay_count > 0 && oplus_sla_info[MAIN_WLAN].dl_little_speed < DOWNLOAD_SPEED) ||
			(oplus_sla_info[SECOND_WLAN].download_speed > VIDEO_SPEED && oplus_sla_info[SECOND_WLAN].sla_avg_rtt <= 150)))) {

			choose_mark= SECOND_WLAN_MARK;
		}
	}

	if (choose_mark) {
		skb->mark = choose_mark;
		ct->mark = skb->mark;
		sk->oplus_sla_mark = skb->mark;
		//sk->sk_mark = sk->oplus_sla_mark;
		return SLA_SKB_MARKED;
	}

	return ret;
}


static void is_http_get(struct nf_conn *ct,struct sk_buff *skb,
				struct tcphdr *tcph,int header_len)
{

	u32 *payload = NULL;
	payload =(u32 *)(skb->data + header_len);

	if(0 == ct->oplus_http_flag && (80 == ntohs(tcph->dest))){

		if(	*payload == 0x20544547){//http get

			ct->oplus_http_flag = 1;
			ct->oplus_skb_count = 1;
		}
	}

	return;
}

static struct tcphdr * is_valid_http_packet(struct sk_buff *skb, int *header_len)
{
	int datalen = 0;
	int tmp_len = 0;
	struct tcphdr *tcph = NULL;
	struct iphdr *iph;

	if((iph = ip_hdr(skb)) != NULL &&
		iph->protocol == IPPROTO_TCP){

		tcph = tcp_hdr(skb);
		datalen = ntohs(iph->tot_len);
		tmp_len = iph->ihl * 4 + tcph->doff * 4;

		if((datalen - tmp_len) > 64){
			*header_len = tmp_len;
			return tcph;
		}
	}
	return NULL;
}

static u32 get_skb_mark_by_weight(void)
{
	int i = 0;
	u32 sla_random = prandom_u32() & 0x7FFFFFFF;

	/*0x147AE15 = 0x7FFFFFFF /100 + 1; for we let the weight * 100 to void
	  *decimal point operation at linux kernel
	  */
	for (i = 0; i < IFACE_NUM; i++) {
		if (oplus_sla_info[i].if_up &&
			oplus_sla_info[i].weight) {
			if (sla_random < (0x147AE15 * oplus_sla_info[i].weight)) {
				return oplus_sla_info[i].mark;
			}
		}
	}

	return oplus_sla_info[MAIN_WLAN].mark;
}

static void reset_oplus_sla_calc_speed(struct timeval tv)
{
	int time_interval = 0;
	time_interval = tv.tv_sec - last_calc_small_speed_tv.tv_sec;

	if(time_interval >= 60 &&
		oplus_speed_info[MAIN_WLAN].speed_done){

		oplus_sla_calc_speed = 0;
		oplus_speed_info[MAIN_WLAN].speed_done = 0;

	}
}

static int wlan_get_speed_prepare(struct sk_buff *skb)
{

	int header_len = 0;
	struct tcphdr *tcph = NULL;
	struct nf_conn *ct = NULL;
	enum ip_conntrack_info ctinfo;

	ct = nf_ct_get(skb, &ctinfo);

	if(NULL == ct){
		return NF_ACCEPT;
	}

	if(ctinfo == IP_CT_ESTABLISHED){
		tcph = is_valid_http_packet(skb,&header_len);
		if(tcph){
			is_http_get(ct,skb,tcph,header_len);
		}
	}
	return NF_ACCEPT;
}


static int get_wlan_syn_retran(struct sk_buff *skb)
{
	int tmp_mark;
	int rtt_mark;
	struct iphdr *iph;
	struct sock *sk = NULL;
	struct nf_conn *ct = NULL;
	struct tcphdr *th = NULL;
	enum ip_conntrack_info ctinfo;

	ct = nf_ct_get(skb, &ctinfo);

	if(NULL == ct){
		return NF_ACCEPT;
	}

	if(ctinfo == IP_CT_NEW &&
		(iph = ip_hdr(skb)) != NULL &&
		iph->protocol == IPPROTO_TCP){

		th = tcp_hdr(skb);
		/*only statictis syn retran packet,
		 * sometimes some rst packet also will be here
		 */
		if(NULL != th && th->syn &&
		   !th->rst && !th->ack && !th->fin){

			/*Some third-party apps will send TCP syn
			*messages of the Intranet, resulting in retransmission
			*/
			if (dst_is_lan_ip(skb)) {
				ct->mark |= RTT_MARK;
				return NF_ACCEPT;
			}

			rtt_mark = ct->mark & RTT_MASK;
			tmp_mark = ct->mark & MARK_MASK;

			if(rtt_mark & RTT_MARK){
				return NF_ACCEPT;
			}

			if(tmp_mark != MAIN_WLAN_MARK) {
				struct net_device *dev = NULL;
				dev = skb_dst(skb)->dev;

				if (dev && !memcmp(dev->name,oplus_sla_info[MAIN_WLAN].dev_name,strlen(dev->name))) {
					if (oplus_sla_debug) {
						printk("oplus_dev_info: dev name = %s\n",dev->name);
					}

					ct->mark = MAIN_WLAN_MARK;

					sk = skb_to_full_sk(skb);
					if(sk) {
						sk->oplus_sla_mark = MAIN_WLAN_MARK;
						//sk->sk_mark = sk->oplus_sla_mark;
					}

					syn_retran_statistic.syn_total_num++;
				}
				return NF_ACCEPT;
			}

			syn_retran_statistic.syn_retran_num++;
			calc_rtt_by_dev_index(MAIN_WLAN, SYN_RETRAN_RTT, sk);

			ct->mark |= RTT_MARK;
		}
	}
	return NF_ACCEPT;
}

static void game_rtt_estimator(int game_type, u32 rtt)
{
	long m = rtt; /* RTT */
	int shift = 3;
	u32 srtt = 0;

	if(game_type &&
		game_type < GAME_NUM){

		srtt = game_uid[game_type].rtt;
		if(GAME_WZRY == game_type) {
			shift = 2;
		}

		if (srtt != 0) {

			m -= (srtt >> shift);
			srtt += m;		/* rtt = 7/8 rtt + 1/8 new */

		} else {
		    //void first time the rtt bigger than 200ms which will switch game network
		    m = m >> 2;
			srtt = m << shift;

		}
		game_uid[game_type].rtt = srtt;

		if(rtt >= 250){
			game_uid[game_type].rtt_250_num++;
		}
		else if(rtt >= 200){
			game_uid[game_type].rtt_200_num++;
		}
		else if(rtt >= 150){
			game_uid[game_type].rtt_150_num++;
		}
		else {
			game_uid[game_type].rtt_normal_num++;
		}
	}
	return;
}

static void game_app_switch_network(u32 game_type)
{
	int index = -1;
	u32 game_rtt = 0;
	u32 uid	= 0;
	int shift = 3;
	u32 time_now = 0;
	int max_rtt = MAX_GAME_RTT;
	int game_bp_info[4];
	bool wlan_bad = false;

	if(!oplus_sla_enable){
		return;
	}

	if(GAME_WZRY !=	game_type &&
	   GAME_CJZC != game_type){
		return;
	}

	if(oplus_sla_info[MAIN_WLAN].wlan_score_bad_count >= WLAN_SCORE_BAD_NUM){
		wlan_bad = true;
	}

	index = game_type;
	uid = game_uid[game_type].uid;

	if(!game_start_state[index] && !inGame){
		return;
	}

	if(GAME_WZRY == game_type) {
		shift = 2;
		max_rtt = rom_update_info.wzry_rtt;
	}
	else if(GAME_CJZC == game_type){
		max_rtt = rom_update_info.cjzc_rtt;
	}

	time_now = ktime_get_ns() / 1000000;
	game_rtt = game_uid[game_type].rtt >> shift;

	if(cell_quality_good &&
	   !game_cell_to_wifi &&
	   (wlan_bad || game_rx_bad || game_rtt >= max_rtt) &&
	   game_uid[index].mark == MAIN_WLAN_MARK){
	   if(!game_uid[index].switch_time ||
	   	 (time_now - game_uid[index].switch_time) > 60000){

		    game_uid[game_type].rtt = 0;
			game_uid[index].switch_time = time_now;
			game_uid[index].mark = CELL_MARK;

			memset(game_bp_info,0x0,sizeof(game_bp_info));
			game_bp_info[0] = game_type;
			game_bp_info[1] = CELL_MARK;
			game_bp_info[2] = wlan_bad;
			game_bp_info[3] = cell_quality_good;
			oplus_sla_send_to_user(SLA_SWITCH_GAME_NETWORK,(char *)game_bp_info,sizeof(game_bp_info));
			printk("oplus_sla_game_rtt:uid = %u,game rtt = %u,wlan_bad = %d,game_rx_bad = %d,changing to cellular...\n",
					uid,game_rtt,wlan_bad,game_rx_bad);
			return;
	   }
	}

	if(!wlan_bad &&
	   game_uid[index].mark == CELL_MARK && game_rx_bad){

		if(!game_uid[index].switch_time ||
	   	 (time_now - game_uid[index].switch_time) > 60000){

		    game_uid[game_type].rtt = 0;
			game_uid[index].switch_time = time_now;
			game_uid[index].mark = MAIN_WLAN_MARK;
			game_cell_to_wifi = true;

			memset(game_bp_info,0x0,sizeof(game_bp_info));
			game_bp_info[0] = game_type;
			game_bp_info[1] = MAIN_WLAN_MARK;
			game_bp_info[2] = wlan_bad;
			game_bp_info[3] = cell_quality_good;
			oplus_sla_send_to_user(SLA_SWITCH_GAME_NETWORK,(char *)game_bp_info,sizeof(game_bp_info));
			printk("oplus_sla_game_rtt:uid = %u,game rtt = %u,wlan_bad = %d,game_rx_bad = %d,changing to wlan...\n",
					uid,game_rtt,wlan_bad,game_rx_bad);
			return;
	   }
	}
	return;
}

static bool is_game_rtt_skb(struct nf_conn *ct, struct sk_buff *skb, bool isTx)
{
	struct iphdr *iph = NULL;
	struct udphdr *udph = NULL;
	u32 header_len;
	u8 *payload = NULL;
	u32 gameType = ct->oplus_app_type;

	if (gameType <= 0 || gameType >= GAME_NUM || game_params[gameType].game_index == 0 ||
		(iph = ip_hdr(skb)) == NULL || iph->protocol != IPPROTO_UDP) {
		//printk("oplus_sla_game_rtt_detect: not game skb.");
		return false;
	}

	if ((isTx && ct->oplus_game_up_count >= MAX_DETECT_PKTS) ||
		(!isTx && ct->oplus_game_down_count >= MAX_DETECT_PKTS)) {
		ct->oplus_game_detect_status = GAME_SKB_COUNT_ENOUGH;
		return false;
	}

	if (unlikely(skb_linearize(skb))) {
		return false;
	}
	iph = ip_hdr(skb);
	udph = udp_hdr(skb);
	header_len = iph->ihl * 4 + sizeof(struct udphdr);
	payload =(u8 *)(skb->data + header_len);
	if (isTx) {
		int tx_offset = game_params[gameType].tx_offset;
		int tx_len = game_params[gameType].tx_len;
		//u8  tx_fixed_value[MAX_FIXED_VALUE_LEN];
		//memcpy(tx_fixed_value, game_params[gameType].tx_fixed_value, MAX_FIXED_VALUE_LEN);
		if (udph->len >= (tx_offset + tx_len) &&
			memcmp(payload + tx_offset, game_params[gameType].tx_fixed_value, tx_len) == 0) {
			if (oplus_sla_debug) {
				printk("oplus_sla_game_rtt_detect:srcport[%d] this is game RTT Tx skb.\n",ntohs(udph->source));
			}
			ct->oplus_game_detect_status |= GAME_RTT_STREAM;
			return true;
		}
	} else {
		int rx_offset = game_params[gameType].rx_offset;
		int rx_len = game_params[gameType].rx_len;
		//u8  rx_fixed_value[MAX_FIXED_VALUE_LEN];
		//memcpy(rx_fixed_value, game_params[gameType].rx_fixed_value, MAX_FIXED_VALUE_LEN);
		if (udph->len >= (rx_offset + rx_len) &&
			memcmp(payload + rx_offset, game_params[gameType].rx_fixed_value, rx_len) == 0) {
			if (oplus_sla_debug) {
				printk("oplus_sla_game_rtt_detect: this is game RTT Rx skb.\n");
			}
			ct->oplus_game_detect_status |= GAME_RTT_STREAM;
			return true;
		}
	}
	return false;
}

static int mark_game_app_skb(struct nf_conn *ct,struct sk_buff *skb,enum ip_conntrack_info ctinfo)
{
	int game_index = -1;
	struct iphdr *iph = NULL;
	u32 ct_mark = 0;
	int ret = SLA_SKB_CONTINUE;

	if(ct->oplus_app_type > 0 && ct->oplus_app_type < GAME_NUM){
		ret = SLA_SKB_ACCEPT;
		game_index = ct->oplus_app_type;

		if(GAME_WZRY != game_index &&
		   GAME_CJZC != game_index){
			return ret;
		}

		ct_mark = GAME_UNSPEC_MASK & ct->mark;
		if(!game_start_state[game_index] && !inGame &&
		   (GAME_UNSPEC_MARK & ct_mark)){

			return SLA_SKB_ACCEPT;
		}

		iph = ip_hdr(skb);
		if(iph &&
		   (IPPROTO_UDP == iph->protocol ||
		    IPPROTO_TCP == iph->protocol)){

			//WZRY can not switch tcp packets
			if(GAME_WZRY == game_index &&
			   IPPROTO_TCP == iph->protocol) {
				return SLA_SKB_ACCEPT;
			}

			ct_mark	= ct->mark & MARK_MASK;

			if(GAME_CJZC == game_index &&
			    IPPROTO_TCP == iph->protocol &&
			   ((XT_STATE_BIT(ctinfo) & XT_STATE_BIT(IP_CT_ESTABLISHED)) ||
			    (XT_STATE_BIT(ctinfo) & XT_STATE_BIT(IP_CT_RELATED)))) {
				if(MAIN_WLAN_MARK == ct_mark && oplus_sla_info[MAIN_WLAN].wlan_score > 40){
					return SLA_SKB_ACCEPT;
				}
				else if(CELL_MARK == ct_mark){
					skb->mark = CELL_MARK;
					return SLA_SKB_MARKED;
				}
			}

			if (game_mark) {
			    skb->mark = game_mark;
			} else {
			    skb->mark = game_uid[game_index].mark;
			}

			if(ct_mark && skb->mark &&
			   ct_mark != skb->mark){

			   printk("oplus_sla_game_rtt:reset game ct proto= %u,srcport = %d,"
					  "ct dying = %d,ct confirmed = %d,game type = %d,ct mark = %x,skb mark = %x\n",
					  iph->protocol,ntohs(udp_hdr(skb)->source),
					  nf_ct_is_dying(ct),nf_ct_is_confirmed(ct),game_index,ct_mark,skb->mark);

			   game_uid[game_index].rtt = 0;

			   if(!nf_ct_is_dying(ct) &&
				  nf_ct_is_confirmed(ct)){
					nf_ct_kill(ct);
					return SLA_SKB_DROP;
			   }
			   else{
					skb->mark = ct_mark;
					ret = SLA_SKB_MARKED;
			   }
			}

			if(!ct_mark){
				ct->mark = (ct->mark & RTT_MASK) | game_uid[game_index].mark;
			}
			ret = SLA_SKB_MARKED;
		}
	}

	return ret;
}

static bool is_game_app_skb(struct nf_conn *ct,struct sk_buff *skb,enum ip_conntrack_info ctinfo)
{
	int i = 0;
	kuid_t uid;
	struct sock *sk = NULL;
	struct iphdr *iph = NULL;
	const struct file *filp = NULL;

	if(INIT_APP_TYPE == ct->oplus_app_type){

		sk = skb_to_full_sk(skb);
		if(NULL == sk || NULL == sk->sk_socket){
			return false;
		}

		filp = sk->sk_socket->file;
		if(NULL == filp){
			return false;
		}

        iph = ip_hdr(skb);

		uid = filp->f_cred->fsuid;
		for(i = 1;i < GAME_NUM;i++){
			if(game_uid[i].uid){
				if((uid.val % UID_MASK) == (game_uid[i].uid % UID_MASK)) {
					ct->oplus_app_type = i;
					if(oplus_sla_enable &&
					   CELL_MARK == game_uid[i].mark){
						game_uid[i].cell_bytes += skb->len;
					}
					//ct->mark = (ct->mark & RTT_MASK) | game_uid[i].mark;
					if(!game_start_state[i] && !inGame &&
						iph && IPPROTO_TCP == iph->protocol){
						ct->mark = (ct->mark & RTT_MASK) | MAIN_WLAN_MARK;
						ct->mark |= GAME_UNSPEC_MARK;
					} else{
						if (game_mark) {
						    ct->mark = (ct->mark & RTT_MASK) | game_mark;
						} else {
						    ct->mark = (ct->mark & RTT_MASK) | game_uid[i].mark;
						}
					}
					return true;
				}
			}
		}
	}
	else if(ct->oplus_app_type > 0 &&
		ct->oplus_app_type < GAME_NUM){
		i = ct->oplus_app_type;
		if(oplus_sla_enable &&
		   (oplus_sla_def_net == MAIN_WLAN || oplus_sla_def_net == SECOND_WLAN) &&
		   CELL_MARK == game_uid[i].mark){
			game_uid[i].cell_bytes += skb->len;
		}
		return true;
	}

	return false;

}

static int detect_game_up_skb(struct sk_buff *skb)
{
	struct timeval tv;
	struct iphdr *iph = NULL;
	struct nf_conn *ct = NULL;
	int ret = SLA_SKB_ACCEPT;
	enum ip_conntrack_info ctinfo;

	if(oplus_sla_vpn_connected){
		return SLA_SKB_CONTINUE;
	}

	ct = nf_ct_get(skb, &ctinfo);
	if(NULL == ct){
		return SLA_SKB_ACCEPT;
	}

	if(!is_game_app_skb(ct,skb,ctinfo)){
		return SLA_SKB_CONTINUE;
	}

	do_gettimeofday(&tv);
	game_online_info.game_online = true;
	game_online_info.last_game_skb_tv = tv;

	//TCP and udp need to switch network
	ret = SLA_SKB_CONTINUE;
	iph = ip_hdr(skb);
	if(iph && IPPROTO_UDP == iph->protocol){
		if (ct->oplus_game_up_count < MAX_DETECT_PKTS &&
		        ct->oplus_game_detect_status == GAME_SKB_DETECTING) {
		        ct->oplus_game_up_count++;
		}
		//only udp packet can trigger switching network to avoid updating game with cell.
		sla_game_write_lock();
		game_app_switch_network(ct->oplus_app_type);
		sla_game_write_unlock();

		if (is_game_rtt_skb(ct, skb, true)) {
			s64 time_now = ktime_get_ns() / 1000000;
			if (ct->oplus_game_timestamp && time_now - ct->oplus_game_timestamp > 200) {
				//Tx done and no Rx, we've lost a response pkt
				ct->oplus_game_lost_count++;
				sla_game_write_lock();
				game_rtt_estimator(ct->oplus_app_type, MAX_GAME_RTT);
				sla_game_write_unlock();

				if(game_rtt_show_toast) {
					u32 game_rtt = MAX_GAME_RTT;
					oplus_sla_send_to_user(SLA_NOTIFY_GAME_RTT,(char *)&game_rtt,sizeof(game_rtt));
				}
				if(oplus_sla_debug) {
					u32 shift = 3;
					if(GAME_WZRY == ct->oplus_app_type) {
					        shift = 2;
					}
					printk("oplus_sla_game_rtt: lost packet!! game_rtt=%u, srtt=%u\n",
					        MAX_GAME_RTT, game_uid[ct->oplus_app_type].rtt >> shift);
				}
			}
			ct->oplus_game_timestamp = time_now;
		}
	}

	return ret;
}

static bool is_game_voice_packet(struct nf_conn *ct, struct sk_buff *skb)
{
	struct iphdr *iph = NULL;
	struct udphdr *udph = NULL;
	u32 header_len;
	u8 *payload = NULL;
	u8 wzry_fixed_value[2] = {0x55, 0xf4};
	u32 wzry_offset = 9;
	u32 wzry_len = 2;
	u8 cjzc_fixed_value[3] = {0x10, 0x01, 0x01};
	u32 cjzc_offset = 4;
	u32 cjzc_len = 3;
	int game_type = ct->oplus_app_type;
	//u16 tot_len;

	if (ct->oplus_game_down_count >= MAX_DETECT_PKTS) {
		ct->oplus_game_detect_status = GAME_SKB_COUNT_ENOUGH;
		return false;
	}

	if ((iph = ip_hdr(skb)) != NULL && iph->protocol == IPPROTO_UDP) {
		if (unlikely(skb_linearize(skb))) {
			return false;
		}
		iph = ip_hdr(skb);
		udph = udp_hdr(skb);
		header_len = iph->ihl * 4 + sizeof(struct udphdr);
		payload =(u8 *)(skb->data + header_len);
		if (game_type == GAME_WZRY) {
			if (udph->len >= (wzry_offset + wzry_len) &&
				memcmp(payload + wzry_offset, wzry_fixed_value, wzry_len) == 0) {	//for cjzc voice pkt
				//printk("oplus_sla_game_rx_voice:this is voice skb!\n");
				ct->oplus_game_detect_status |= GAME_VOICE_STREAM;
				return true;
			} else {
				//memcpy(fixed_value, payload + 4, 3);
				//printk("oplus_sla_game_rx_voice:this is NOT voice skb, value=%02x%02x%02x\n",
				//        fixed_value[0], fixed_value[1], fixed_value[2]);
				return false;
			}
		} else if (game_type == GAME_CJZC) {
			if (udph->len >= (cjzc_offset + cjzc_len) &&
				memcmp(payload + cjzc_offset, cjzc_fixed_value, cjzc_len) == 0) {	//for cjzc voice pkt
				//printk("oplus_sla_game_rx_voice:this is voice skb!\n");
				ct->oplus_game_detect_status |= GAME_VOICE_STREAM;
				return true;
			} else {
				//memcpy(fixed_value, payload + 4, 3);
				//printk("oplus_sla_game_rx_voice:this is NOT voice skb, value=%02x%02x%02x\n",
				//        fixed_value[0], fixed_value[1], fixed_value[2]);
				return false;
			}
		}
	}
	return false;
}

static void record_sla_app_cell_bytes(struct nf_conn *ct, struct sk_buff *skb)
{
	int index = 0;
	u32 ct_mark = 0x0;
	//calc game or white list app cell bytes
	if(oplus_sla_enable &&
		(oplus_sla_def_net == MAIN_WLAN || oplus_sla_def_net == SECOND_WLAN)){
		if(ct->oplus_app_type > 0 &&
			ct->oplus_app_type < GAME_NUM){
			index = ct->oplus_app_type;
			if(CELL_MARK == game_uid[index].mark){
				game_uid[index].cell_bytes += skb->len;
			}
		} else if(ct->oplus_app_type >= WHITE_APP_BASE &&
					ct->oplus_app_type < DUAL_STA_APP_BASE){
			ct_mark = ct->mark & MARK_MASK;
			if(CELL_MARK == ct_mark){
				index = ct->oplus_app_type - WHITE_APP_BASE;
				if(index < WHITE_APP_NUM){
					white_app_list.cell_bytes[index] += skb->len;
				}
			}
		}
	}

	//calc white app cell bytes when sla is not enable
	if(oplus_sla_info[CELL_INDEX].if_up){

		if(!oplus_sla_info[MAIN_WLAN].if_up || oplus_sla_def_net == CELL_INDEX){
			if(ct->oplus_app_type >= WHITE_APP_BASE &&
				ct->oplus_app_type < DUAL_STA_APP_BASE){
				index = ct->oplus_app_type - WHITE_APP_BASE;
				if(index < WHITE_APP_NUM){
					white_app_list.cell_bytes_normal[index] += skb->len;
				}
			}
		}
	}
}

//add for android Q statictis tcp tx and tx
static void statistics_wlan_tcp_tx_rx(const struct nf_hook_state *state,struct sk_buff *skb)
{
	struct iphdr *iph;
	struct nf_conn *ct = NULL;
	enum ip_conntrack_info ctinfo;
	struct net_device *dev = NULL;
	struct tcphdr *th = NULL;
	unsigned int hook = state->hook;

	if((oplus_sla_info[MAIN_WLAN].if_up || oplus_sla_info[MAIN_WLAN].need_up) &&
		(iph = ip_hdr(skb)) != NULL && iph->protocol == IPPROTO_TCP){

		th = tcp_hdr(skb);
		if(NF_INET_LOCAL_OUT == hook
			&& NULL != th
			&& (th->rst || th->fin)){
			//ignore TX fin or rst
			return;
		}
		ct = nf_ct_get(skb, &ctinfo);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0))
		if(NULL == ct || (0 == ct->mark)){
#else
		if(NULL == ct || nf_ct_is_untracked(ct) || (0 == ct->mark)){
#endif
			if (NF_INET_LOCAL_OUT == hook) {
				dev = skb_dst(skb)->dev;
			} else if (NF_INET_LOCAL_IN == hook){
				dev = state->in;
			}

			if (dev && !memcmp(dev->name,oplus_sla_info[MAIN_WLAN].dev_name,strlen(dev->name))) {
				if (NF_INET_LOCAL_OUT == hook) {
					wlan0_tcp_tx++;
				} else if (NF_INET_LOCAL_IN == hook) {
					wlan0_tcp_rx++;
				}
			} else if (dev && !memcmp(dev->name,oplus_sla_info[SECOND_WLAN].dev_name,strlen(dev->name))) {
				if (NF_INET_LOCAL_OUT == hook) {
					wlan1_tcp_tx++;
				} else if (NF_INET_LOCAL_IN == hook) {
					wlan1_tcp_rx++;
				}
			}
		} else if (MAIN_WLAN_MARK == (ct->mark & MARK_MASK)) {
			if (NF_INET_LOCAL_OUT == hook) {
				wlan0_tcp_tx++;
			} else if (NF_INET_LOCAL_IN == hook) {
				wlan0_tcp_rx++;
			}
		} else if (SECOND_WLAN_MARK == (ct->mark & MARK_MASK)) {
			if (NF_INET_LOCAL_OUT == hook) {
				wlan1_tcp_tx++;
			} else if (NF_INET_LOCAL_IN == hook) {
				wlan1_tcp_rx++;
			}
		}
	}
}

static unsigned int oplus_sla_game_rtt_calc(struct sk_buff *skb)
{
	int shift = 3;
	s64 time_now;
	u32 game_rtt = 0;
	struct iphdr *iph = NULL;
	struct nf_conn *ct = NULL;
	enum ip_conntrack_info ctinfo;

	if(oplus_sla_vpn_connected){
		return NF_ACCEPT;
	}

	ct = nf_ct_get(skb, &ctinfo);
	if(NULL == ct){
		return NF_ACCEPT;
	}

	iph = ip_hdr(skb);


	//calc game or white list app cell bytes
	record_sla_app_cell_bytes(ct,skb);

	//calc game app udp packet count(except for voice packets) and tcp bytes
	if (ct->oplus_app_type > 0 && ct->oplus_app_type < GAME_NUM) {
		if (iph && IPPROTO_UDP == iph->protocol) {
			if (ct->oplus_game_down_count < MAX_DETECT_PKTS &&
				ct->oplus_game_detect_status == GAME_SKB_DETECTING) {
				ct->oplus_game_down_count++;
			}
			if (!is_game_voice_packet(ct, skb)) {
				game_online_info.udp_rx_pkt_count++;
			}
		} else if (iph && IPPROTO_TCP == iph->protocol) {
			game_online_info.tcp_rx_byte_count += skb->len;
		}
	}

	if (is_game_rtt_skb(ct, skb, false)) {
		time_now = ktime_get_ns() / 1000000;
		if(ct->oplus_game_timestamp && time_now > ct->oplus_game_timestamp){

			game_rtt = (u32)(time_now - ct->oplus_game_timestamp);
			if (game_rtt < MIN_GAME_RTT) {
				if(oplus_sla_debug){
					printk("oplus_sla_game_rtt:invalid RTT %dms\n", game_rtt);
				}
				ct->oplus_game_timestamp = 0;
				return NF_ACCEPT;
			}

			ct->oplus_game_timestamp = 0;

			if(game_rtt > MAX_GAME_RTT){
				game_rtt = MAX_GAME_RTT;
			}

			if (sla_switch_enable && cell_quality_good) {
				if (oplus_sla_enable && sla_work_mode == SLA_MODE_DUAL_WIFI) {
					/* dualsta Mode, enable sla, just game rtt is bad */
					if (game_rx_bad || game_rtt >= (MAX_GAME_RTT - 80)) {
						send_enable_to_framework(SLA_MODE_WIFI_CELL, INIT_ACTIVE_TYPE);
						printk("oplus_sla_netlink: dualsta Mode, game app send enable sla to user\n");
					}
				} else if (!enable_cell_to_user && !oplus_sla_enable) {
					/* Normal Mode, enable sla */
					if (game_rx_bad || game_rtt >= (MAX_GAME_RTT / 2)) {
						send_enable_to_framework(SLA_MODE_WIFI_CELL, INIT_ACTIVE_TYPE);
						printk("oplus_sla_netlink: Normal Mode, game app send enable sla to user\n");
					}
				}
			}

			if(game_rtt_show_toast) {
				oplus_sla_send_to_user(SLA_NOTIFY_GAME_RTT,(char *)&game_rtt,sizeof(game_rtt));
			}
			ct->oplus_game_lost_count = 0;

			sla_game_write_lock();
			game_rtt_estimator(ct->oplus_app_type,game_rtt);
			sla_game_write_unlock();

			if(oplus_sla_debug){
				if(GAME_WZRY == ct->oplus_app_type) {
					shift = 2;
				}
				printk("oplus_sla_game_rtt: game_rtt=%u, srtt=%u\n", game_rtt, game_uid[ct->oplus_app_type].rtt >> shift);
			}
		}
	}

	return NF_ACCEPT;
}


static bool is_skb_pre_bound(struct sk_buff *skb)
{
	u32 pre_mark = skb->mark & 0x10000;

	if(0x10000 == pre_mark){
		return true;
	}

	return false;
}

static bool is_sla_white_or_game_app(struct nf_conn *ct,struct sk_buff *skb)
{

	if(ct->oplus_app_type > 0 &&
		ct->oplus_app_type < DUAL_STA_APP_BASE){//game app skb
		return true;
	}

	return false;
}


static int sla_skb_reroute(struct sk_buff *skb,struct nf_conn *ct,const struct nf_hook_state *state)
{
	int err;

	err = ip_route_me_harder(state->net, skb, RTN_UNSPEC);
	if (err < 0){
		return NF_DROP_ERR(err);
	}

	return NF_ACCEPT;
}

static void statistic_dns_send_info(int index)
{
	struct timeval tv;

	if (dns_info[index].send_num < DNS_MAX_NUM) {
		dns_info[index].send_num++;
	}
	else {
		do_gettimeofday(&tv);
		dns_info[index].last_tv = tv;
		dns_info[index].in_timer = true;
	}
	return;
}

static bool is_need_change_dns_network(int index)
{
	bool ret = false;

	if (oplus_sla_info[index].weight &&
		WEIGHT_STATE_USELESS != oplus_sla_info[index].weight_state &&
		WEIGHT_STATE_SCORE_INVALID != oplus_sla_info[index].weight_state) {

		if (0 == oplus_sla_info[MAIN_WLAN].weight ||
			WEIGHT_STATE_USELESS == oplus_sla_info[MAIN_WLAN].weight_state ||
			WEIGHT_STATE_SCORE_INVALID == oplus_sla_info[MAIN_WLAN].weight_state ||
			(oplus_sla_info[index].max_speed >= 100 &&
			 oplus_sla_info[MAIN_WLAN].wlan_score_bad_count >= WLAN_SCORE_BAD_NUM)) {

			ret = true;
		}
	}

	return ret;
}

static int dns_skb_need_sla(struct nf_conn *ct,struct sk_buff *skb)
{
	int ret = SLA_SKB_CONTINUE;
	struct iphdr *iph = NULL;
        int dns_iface = MAIN_WLAN;
        u_int32_t dns_ct_mark = MAIN_WLAN_MARK;

	iph = ip_hdr(skb);
	if(NULL != iph &&
	   (iph->protocol == IPPROTO_TCP || iph->protocol == IPPROTO_UDP) &&
		53 == ntohs(udp_hdr(skb)->dest)){

		ret = SLA_SKB_ACCEPT;

	    if (SLA_MODE_DUAL_WIFI == sla_work_mode) {
			if (is_need_change_dns_network(SECOND_WLAN)) {
                //for  the dns packet will do DNAT at iptables,if do DNAT,the packet
                //will be reroute,so here just mark and accept it
				dns_iface = SECOND_WLAN;
				dns_ct_mark = SECOND_WLAN_MARK;
				skb->mark = SECOND_WLAN_MARK;
			}
	    } else if (SLA_MODE_WIFI_CELL == sla_work_mode) {
			if (is_need_change_dns_network(CELL_INDEX)) {

				dns_iface = CELL_INDEX;
				dns_ct_mark = CELL_MARK;
				skb->mark = CELL_MARK;
			}
		}

		ct->mark = dns_ct_mark;
		statistic_dns_send_info(dns_iface);
	}
	return ret;
}


static void dns_respond_statistics(struct sk_buff *skb)
{
	int index = -1;
	int tmp_mark = 0;
	struct iphdr *iph = NULL;
	struct nf_conn *ct = NULL;
	enum ip_conntrack_info ctinfo;

	if (!oplus_sla_enable) {
		return;
	}

	ct = nf_ct_get(skb, &ctinfo);

	if(NULL == ct){
		return;
	}

	iph = ip_hdr(skb);
	if(NULL != iph &&
		(iph->protocol == IPPROTO_TCP || iph->protocol == IPPROTO_UDP) &&
		53 == ntohs(udp_hdr(skb)->source)){

		tmp_mark = ct->mark & MARK_MASK;
		index = find_dev_index_by_mark(tmp_mark);

		if (-1 != index) {
			if (dns_info[index].send_num) {

			    dns_info[index].send_num = 0;
				dns_info[index].in_timer = false;
			}
		}
	}
	return;
}

static void detect_white_list_app_skb(struct sk_buff *skb)
{
	int i = 0;
	int index = -1;
	kuid_t uid;
	struct nf_conn *ct = NULL;
	enum ip_conntrack_info ctinfo;
	struct sock *sk = NULL;
	const struct file *filp = NULL;

	ct = nf_ct_get(skb, &ctinfo);

	if(NULL == ct){
		return;
	}

	/*when the app type is dual sta app,but the work mode is not
	   SLA_MODE_DUAL_WIFI, we should detect it is WIFI+CELL white
	   list app again
	*/
	if (SLA_MODE_DUAL_WIFI != sla_work_mode &&
		ct->oplus_app_type >= DUAL_STA_APP_BASE) {
		ct->oplus_app_type = INIT_APP_TYPE;
	}

	if(INIT_APP_TYPE == ct->oplus_app_type){
		sk = skb_to_full_sk(skb);
		if(NULL == sk || NULL == sk->sk_socket){
			return;
		}

		filp = sk->sk_socket->file;
		if(NULL == filp){
			return;
		}

		uid = filp->f_cred->fsuid;
		for(i = 0;i < white_app_list.count;i++){
			if(white_app_list.uid[i]){
				if((uid.val % UID_MASK) == (white_app_list.uid[i] % UID_MASK)) {
					ct->oplus_app_type = i + WHITE_APP_BASE;
					return;
				}
			}
		}

		//we need to detect the whether it is dual sta white list app
		if (SLA_MODE_DUAL_WIFI != sla_work_mode) {
			ct->oplus_app_type = UNKNOW_APP_TYPE;
		}
	}
	else if(ct->oplus_app_type >= WHITE_APP_BASE &&
			ct->oplus_app_type < DUAL_STA_APP_BASE){
		/*calc white app cell bytes when sla is not enable,
		    when the default network is change to cell,we should
		    disable dual sta from framework
		*/
		if(oplus_sla_info[CELL_INDEX].if_up){
			if(!oplus_sla_info[MAIN_WLAN].if_up ||
				oplus_sla_def_net == CELL_INDEX){
				index = ct->oplus_app_type - WHITE_APP_BASE;
				if(index < WHITE_APP_NUM){
                    white_app_list.cell_bytes_normal[index] += skb->len;
				}
			}
		}
	}
	return;
}

static bool is_dual_sta_white_app(struct nf_conn *ct,struct sk_buff *skb,kuid_t *app_uid)
{
	int i = 0;
	int last_type = UNKNOW_APP_TYPE;
	kuid_t uid;
	struct sock *sk = NULL;
	const struct file *filp = NULL;

	if (ct->oplus_app_type >= DUAL_STA_APP_BASE) {
		return true;
	}

	if(UNKNOW_APP_TYPE != ct->oplus_app_type){

		last_type = ct->oplus_app_type;

		sk = skb_to_full_sk(skb);
		if(NULL == sk || NULL == sk->sk_socket){
			return false;
		}

		filp = sk->sk_socket->file;
		if(NULL == filp){
			return false;
		}

		*app_uid = filp->f_cred->fsuid;
		uid = filp->f_cred->fsuid;
		for(i = 0;i < dual_wifi_app_list.count;i++){
			if(dual_wifi_app_list.uid[i]){
				if((uid.val % UID_MASK) == (dual_wifi_app_list.uid[i] % UID_MASK)) {
					ct->oplus_app_type = i + DUAL_STA_APP_BASE;
					return true;
				}
			}
		}
		ct->oplus_app_type = last_type;
	}

	return false;
}

static void print_stream_info(struct sk_buff *skb)
{
	u32 uid = 0;
	int srcport = 0;
	int dstport = 0;
	unsigned char *dstip;

	struct sock *sk = NULL;
	struct iphdr *iph = NULL;
	struct nf_conn *ct = NULL;
	struct net_device *dev = NULL;
	enum ip_conntrack_info ctinfo;
	const struct file *filp = NULL;

	if (oplus_sla_debug) {
		ct = nf_ct_get(skb, &ctinfo);
		if(NULL == ct){
			return;
		}

		if (ctinfo == IP_CT_NEW) {
			sk = skb_to_full_sk(skb);
			if(sk && sk_fullsock(sk)){
				if(NULL == sk->sk_socket){
					return;
				}

				filp = sk->sk_socket->file;
				if(NULL == filp){
					return;
				}

				iph = ip_hdr(skb);
				if (NULL != iph &&
		   		   (iph->protocol == IPPROTO_TCP || iph->protocol == IPPROTO_UDP)) {

					dev = skb_dst(skb)->dev;
					uid = filp->f_cred->fsuid.val;
					dstport = ntohs(udp_hdr(skb)->dest);
					srcport = ntohs(udp_hdr(skb)->source);
					dstip = (unsigned char *)&iph->daddr;

					printk("oplus_sla_stream: screen_on[%d] uid[%u]"
							" proto[%d] srcport[%d] dstport[%d] dstip[%d.%d.%d.%d] dev[%s] mark[%x]\n",
							sla_screen_on,uid,iph->protocol,srcport,dstport,
							dstip[0],dstip[1],dstip[2],dstip[3],dev?dev->name:"null",skb->mark);
				}
			}
		}
	}
	return;
}

static int sla_mark_skb(struct sk_buff *skb, const struct nf_hook_state *state)
{
	int index = 0;
	kuid_t app_uid = {0};
	u32 ct_mark = 0x0;
	struct sock *sk = NULL;
	struct nf_conn *ct = NULL;
	int ret = SLA_SKB_CONTINUE;
	enum ip_conntrack_info ctinfo;

	//if wlan assistant has change network to cell,do not mark SKB
	if(oplus_sla_def_net == CELL_INDEX){
		return NF_ACCEPT;
	}

	ct = nf_ct_get(skb, &ctinfo);
	if(NULL == ct){
		return NF_ACCEPT;
	}

	/*
	  * when the wifi is poor,the dns request allways can not rcv respones,
	  * so please let the dns packet with the cell network mark.
	  */
	ret = dns_skb_need_sla(ct,skb);
	if(SLA_SKB_ACCEPT == ret) {
		return NF_ACCEPT;
	}

	if(is_skb_pre_bound(skb) || dst_is_lan_ip(skb)){
		return NF_ACCEPT;
	}

	if(SLA_MODE_WIFI_CELL == sla_work_mode &&
		!is_sla_white_or_game_app(ct,skb)){
		return NF_ACCEPT;
	}

	if (SLA_MODE_DUAL_WIFI == sla_work_mode &&
		!is_dual_sta_white_app(ct,skb,&app_uid)) {
		return NF_ACCEPT;
	}

	ret = mark_game_app_skb(ct,skb,ctinfo);
	if(SLA_SKB_MARKED == ret){
		goto sla_reroute;
	}
	else if(SLA_SKB_ACCEPT == ret){
		return NF_ACCEPT;
	}
	else if(SLA_SKB_DROP ==	ret){
		return NF_DROP;
	}

	if(ctinfo == IP_CT_NEW){
		sk = skb_to_full_sk(skb);
		if(NULL != sk){
			ret = syn_retransmits_packet_do_specail(sk,ct,skb);
			if(SLA_SKB_MARKED == ret){
				goto sla_reroute;
			}
			else if(SLA_SKB_REMARK == ret){
				return NF_DROP;
			}
			else if(SLA_SKB_ACCEPT == ret){
				return NF_ACCEPT;
			}

			// add for download app stream
			ret = mark_download_app(sk,app_uid,ct,skb);
			if (SLA_SKB_MARKED == ret) {
				goto sla_reroute;
			}

			// add for vedio app stream
			ret = mark_video_app(sk,app_uid,ct,skb);
			if (SLA_SKB_MARKED == ret) {
				goto sla_reroute;
			}

			sla_read_lock();
			skb->mark = get_skb_mark_by_weight();
			sla_read_unlock();

			ct->mark = skb->mark;
			sk->oplus_sla_mark = skb->mark;
			//sk->sk_mark = sk->oplus_sla_mark;
		}
	}
	else if((XT_STATE_BIT(ctinfo) &	XT_STATE_BIT(IP_CT_ESTABLISHED)) ||
			(XT_STATE_BIT(ctinfo) &	XT_STATE_BIT(IP_CT_RELATED))){

		skb->mark = ct->mark & MARK_MASK;
	}

	//If the mark value of the packet is equal to WLAN0_MARK, no re routing is required
	if (MAIN_WLAN_MARK == skb->mark) {
		return NF_ACCEPT;
	}

	//calc white list app cell bytes
	if(ct->oplus_app_type >= WHITE_APP_BASE &&
		ct->oplus_app_type < DUAL_STA_APP_BASE){
		ct_mark = ct->mark & MARK_MASK;
		if(CELL_MARK == ct_mark){
			index = ct->oplus_app_type - WHITE_APP_BASE;
			if(index < WHITE_APP_NUM){
				white_app_list.cell_bytes[index] += skb->len;
			}
		}
	}

sla_reroute:
	ret = sla_skb_reroute(skb,ct,state);
	return ret;

}

/*
  * so how can we calc speed when app connect server with 443 dport(https)
  */
static void get_content_lenght(struct nf_conn *ct,struct sk_buff *skb,int header_len,int index){

	char *p = (char *)skb->data + header_len;

	char *start = NULL;
	char *end = NULL;
	int temp_len = 0;
	u64 tmp_time;
	u32 content_len = 0;
	char data_buf[256];
	char data_len[11];
	memset(data_len,0x0,sizeof(data_len));
	memset(data_buf,0x0,sizeof(data_buf));

	if(ct->oplus_http_flag != 1 || ct->oplus_skb_count > 3){
		return;
	}
	ct->oplus_skb_count++;

	temp_len = (char *)skb_tail_pointer(skb) - p;
	if(temp_len < 25){//HTTP/1.1 200 OK + Content-Length
		return;
	}

	p += 25;

	temp_len = (char *)skb_tail_pointer(skb) - p;
	if(temp_len){
		if(temp_len > (sizeof(data_buf)-1)){

			temp_len = (sizeof(data_buf)-1);
		}
		memcpy(data_buf,p,temp_len);
		start = strstr(data_buf,"Content-Length");
		if(start != NULL){
			ct->oplus_http_flag = 2;
			start += 16; //add Content-Length:

			end = strchr(start,0x0d);//get '\r\n'

			if(NULL != end){
				if((end - start) < 11){
					memcpy(data_len,start,end - start);
					sscanf(data_len,"%u",&content_len);
					//printk("oplus_sla_speed:content = %u\n",content_len);
				}
				else{
					content_len = 0x7FFFFFFF;
				}

				tmp_time = ktime_get_ns();
				oplus_speed_info[index].sum_bytes += content_len;
				if(0 == oplus_speed_info[index].bytes_time){
					oplus_speed_info[index].bytes_time = tmp_time;
				} else {
					if(oplus_speed_info[index].sum_bytes >= 20000 ||
						(tmp_time - oplus_speed_info[index].bytes_time) > 5000000000) {
						oplus_speed_info[index].bytes_time = tmp_time;
						if(oplus_speed_info[index].sum_bytes >= 20000){
							oplus_speed_info[index].ms_speed_flag = 1;
						}
						oplus_speed_info[index].sum_bytes = 0;
					}
				}
			}
		}
	}
}

/*only work for wifi +lte mode*/
static unsigned int oplus_sla_speed_calc(struct sk_buff *skb)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = NULL;
	int index = -1;
	int tmp_speed = 0;
	u64 time_now = 0;
	u64 tmp_time = 0;
	struct iphdr *iph;
	struct tcphdr *tcph;
	int datalen = 0;
	int header_len = 0;

	if(!sla_switch_enable || oplus_sla_enable){
		return NF_ACCEPT;
	}

	//only calc wlan speed
	if(oplus_sla_info[MAIN_WLAN].if_up &&
	   !oplus_sla_info[CELL_INDEX].if_up){
		index = MAIN_WLAN;
	} else {
		return NF_ACCEPT;
	}

	if(!enable_cell_to_user &&
		oplus_sla_calc_speed &&
		!oplus_speed_info[index].speed_done){

		ct = nf_ct_get(skb, &ctinfo);

		if(NULL == ct){
			return NF_ACCEPT;

		}

		if(XT_STATE_BIT(ctinfo) & XT_STATE_BIT(IP_CT_ESTABLISHED)){
			if((iph = ip_hdr(skb)) != NULL &&
					iph->protocol == IPPROTO_TCP){

				tcph = tcp_hdr(skb);
				datalen = ntohs(iph->tot_len);
				header_len = iph->ihl * 4 + tcph->doff * 4;

				if((datalen - header_len) >= 64){//ip->len > tcphdrlen

					if(!oplus_speed_info[index].ms_speed_flag){
						get_content_lenght(ct,skb,header_len,index);
					}

					if(oplus_speed_info[index].ms_speed_flag){

						time_now = ktime_get_ns();
						if(0 == oplus_speed_info[index].last_time &&
							0 == oplus_speed_info[index].first_time){
							oplus_speed_info[index].last_time = time_now;
							oplus_speed_info[index].first_time = time_now;
							oplus_speed_info[index].rx_bytes = skb->len;
							return NF_ACCEPT;
						}

						tmp_time = time_now - oplus_speed_info[index].first_time;
						oplus_speed_info[index].rx_bytes += skb->len;
						oplus_speed_info[index].last_time = time_now;
						if(tmp_time > 500000000) {
							oplus_speed_info[index].ms_speed_flag = 0;
							tmp_time = oplus_speed_info[index].last_time - oplus_speed_info[index].first_time;
							tmp_speed = (1000 *1000*oplus_speed_info[index].rx_bytes) / (tmp_time);//kB/s

							oplus_speed_info[index].speed_done = 1;
							do_gettimeofday(&last_calc_small_speed_tv);

							if(cell_quality_good &&
								!rate_limit_info.rate_limit_enable &&
								tmp_speed > CALC_WEIGHT_MIN_SPEED_1 &&
								tmp_speed < CALC_WEIGHT_MIN_SPEED_3 &&
								oplus_sla_info[index].max_speed < 100){

								send_enable_to_framework(SLA_MODE_WIFI_CELL,INIT_ACTIVE_TYPE);
								printk("oplus_sla_netlink: calc speed is small,send enable sla to user\n");
							}

							printk("oplus_sla_speed: speed[%d] = %d\n",index,tmp_speed);
						}
					}
				}
			}
		}
	}

	return NF_ACCEPT;
}

/* oplus sla hook function, mark skb and rerout skb
*/
static unsigned int oplus_sla_output_hook(void *priv,
				      struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	int ret = NF_ACCEPT;
	int game_ret = NF_ACCEPT;

	game_ret = detect_game_up_skb(skb);
	if(SLA_SKB_ACCEPT == game_ret){
		goto end_sla_output;
	}

	//we need to calc white list app cell bytes when sla not enabled
	detect_white_list_app_skb(skb);

    if(oplus_sla_enable){

		ret = sla_mark_skb(skb,state);
    }
	else{

		if(!sla_screen_on){
			goto end_sla_output;
		}

		if(oplus_sla_info[MAIN_WLAN].if_up){

			ret = get_wlan_syn_retran(skb);

			if(oplus_sla_calc_speed &&
				!oplus_speed_info[MAIN_WLAN].speed_done){
				ret = wlan_get_speed_prepare(skb);
			}
		}
	}
end_sla_output:
	//add for android Q statictis tcp tx and tx
	statistics_wlan_tcp_tx_rx(state,skb);
	print_stream_info(skb);
	return ret;
}

static unsigned int oplus_sla_input_hook(void *priv,
				      struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	//add for android Q statictis tcp tx and tx
	statistics_wlan_tcp_tx_rx(state,skb);
    dns_respond_statistics(skb);
	oplus_sla_speed_calc(skb);
	oplus_sla_game_rtt_calc(skb);

	return NF_ACCEPT;
}

static void oplus_statistic_dev_rtt(struct sock *sk,long rtt)
{
	int index = -1;
	int tmp_rtt = rtt / 1000; //us -> ms
	u32 mark = sk->oplus_sla_mark & MARK_MASK;

	if(oplus_sla_def_net == CELL_INDEX){
		index  = CELL_INDEX;
	}else if(!oplus_sla_enable) {
		if (oplus_sla_info[MAIN_WLAN].if_up){
		    index = MAIN_WLAN;
		} else if (oplus_sla_info[CELL_INDEX].if_up){
		    index = CELL_INDEX;
		}
	} else {
		index = find_dev_index_by_mark(mark);
    }

	if(-1 != index){
		calc_rtt_by_dev_index(index, tmp_rtt, sk);
		#ifdef OPLUS_FEATURE_APP_MONITOR
		/* Add for apps network monitors */
		statistics_monitor_apps_rtt_via_uid(index, tmp_rtt, sk);
		#endif /* OPLUS_FEATURE_APP_MONITOR */
	}
}

/*sometimes when skb reject by iptables,
*it will retran syn which may make the rtt much high
*so just mark the stream(ct) with mark IPTABLE_REJECT_MARK when this happens
*/
static void sla_mark_streams_for_iptables_reject(struct sk_buff *skb,enum ipt_reject_with reject_type)
{
	struct nf_conn *ct = NULL;
	enum ip_conntrack_info ctinfo;

	ct = nf_ct_get(skb, &ctinfo);
	if(NULL == ct){
		return;
	}
	ct->mark |= RTT_MARK;

	if (oplus_sla_debug) {
		if(ctinfo == IP_CT_NEW){
			struct sock *sk = skb_to_full_sk(skb);
			const struct file *filp = NULL;
			if(sk && sk_fullsock(sk)){
				if(NULL == sk->sk_socket){
					return;
				}

				filp = sk->sk_socket->file;
				if(NULL == filp){
					return;
				}
				printk("oplus_sla_iptables:uid = %u,reject type = %u\n",filp->f_cred->fsuid.val,reject_type);
			}
		}
	}

	return;
}

static void is_need_calc_wlan_small_speed(int speed)
{
	if(sla_screen_on &&
		!enable_cell_to_user &&
		!oplus_sla_enable &&
		!oplus_sla_calc_speed &&
		!rate_limit_info.rate_limit_enable){

		if(speed <= 100 &&
			speed > CALC_WEIGHT_MIN_SPEED_1 &&
			oplus_sla_info[MAIN_WLAN].minute_speed > MINUTE_LITTE_RATE){

			printk("oplus_sla_speed: detect speed is little\n");
			oplus_sla_calc_speed = 1;
			memset(&oplus_speed_info[MAIN_WLAN],0x0,sizeof(struct oplus_speed_calc));
		}
	}
}

static bool is_wlan_speed_good(int index)
{
	int i = index;
	bool ret = false;

	if(oplus_sla_info[i].max_speed >= 300 &&
	   oplus_sla_info[i].sla_avg_rtt < 150 &&
	   oplus_sla_info[i].wlan_score >= 60 &&
	   oplus_sla_info[i].download_flag < DOWNLOAD_FLAG){
		ret = true;
	}
	return ret;
}

static void auto_disable_sla_for_good_wlan(void)
{
	int work_mode = SLA_MODE_WIFI_CELL;
	int time_interval;
	struct timeval tv;

	do_gettimeofday(&tv);
	time_interval = tv.tv_sec - last_enable_cellular_tv.tv_sec;

	if(oplus_sla_enable &&
	   time_interval >= 300 &&
	   !game_online_info.game_online &&
	   SLA_MODE_WIFI_CELL == sla_work_mode &&
	   oplus_sla_info[MAIN_WLAN].sla_avg_rtt < 150 &&
	   oplus_sla_info[MAIN_WLAN].max_speed >= 300){

	   enable_cell_to_user = false;
	   oplus_sla_send_to_user(SLA_DISABLE,(char *)&work_mode,sizeof(int));

	   sla_write_lock();
	   oplus_sla_info[MAIN_WLAN].weight = 100;
	   oplus_sla_info[CELL_INDEX].weight = 0;
	   sla_write_unlock();

	   printk("oplus_sla_netlink:speed good,disable sla\n");
	}
}

static void auto_disable_dual_wifi(void)
{
	int work_mode = SLA_MODE_DUAL_WIFI;
	int time_interval;
	struct timeval tv;

	do_gettimeofday(&tv);
	time_interval = tv.tv_sec - last_enable_cellular_tv.tv_sec;

	if(oplus_sla_enable &&
	   time_interval >= 300 &&
	   !game_online_info.game_online &&
	   SLA_MODE_DUAL_WIFI == sla_work_mode &&
	   is_wlan_speed_good(MAIN_WLAN) &&
	   oplus_sla_info[SECOND_WLAN].minute_speed < MINUTE_LITTE_RATE){

	   enable_second_wifi_to_user = false;
	   oplus_sla_send_to_user(SLA_DISABLE,(char *)&work_mode,sizeof(int));

	   sla_write_lock();
	   oplus_sla_info[MAIN_WLAN].weight = 100;
	   oplus_sla_info[SECOND_WLAN].weight = 0;
	   sla_write_unlock();

	   printk("oplus_sla_netlink:speed good,disable dual wifi\n");
	}
}

static void detect_wlan_state_with_speed(void)
{
	auto_disable_dual_wifi();
	auto_disable_sla_for_good_wlan();
	is_need_calc_wlan_small_speed(oplus_sla_info[MAIN_WLAN].max_speed);
}

static void reset_network_state_by_speed(int index,int speed)
{
	int i = index;
	if(speed > 400) {
		if (oplus_sla_info[i].avg_rtt > 150) {
		    oplus_sla_info[i].avg_rtt -= 50;
		}
		if (oplus_sla_info[i].sla_avg_rtt > 150) {
			oplus_sla_info[i].sla_avg_rtt -=50;
		}
		oplus_sla_info[i].congestion_flag = CONGESTION_LEVEL_NORMAL;
	}

	//when the network speed is bigger than 50KB/s,we shoud reset the weight_state
	if (speed >= 50 &&
		WEIGHT_STATE_USELESS == oplus_sla_info[i].weight_state) {
		oplus_sla_info[i].weight_state = WEIGHT_STATE_NORMAL;
	}

	return;
}
static void detect_dl_little_speed(int index,int speed)
{
	int i = index;

	if(speed >= CALC_WEIGHT_MIN_SPEED_2){
		if (oplus_sla_info[i].little_speed_num < ADJUST_SPEED_NUM) {
			oplus_sla_info[i].little_speed_num++;
			if (speed > oplus_sla_info[i].tmp_little_speed) {
				oplus_sla_info[i].tmp_little_speed = speed;
			}

			if (ADJUST_SPEED_NUM == oplus_sla_info[i].little_speed_num) {
				oplus_sla_info[i].dl_little_speed = oplus_sla_info[i].tmp_little_speed;
			}
		}
	} else {
		oplus_sla_info[i].tmp_little_speed = 0;
		oplus_sla_info[i].little_speed_num = 0;
	}
}

static void detect_network_download(int index,int speed)
{
	int i = index;

	if(speed > DOWNLOAD_SPEED){
		if (oplus_sla_info[i].download_num < ADJUST_SPEED_NUM) {
			oplus_sla_info[i].download_num++;
			if (speed > oplus_sla_info[i].dl_mx_speed) {
				oplus_sla_info[i].dl_mx_speed = speed;
			}
		}

		//Adjust the speed to prevent speed burst leading to high speed
		if (ADJUST_SPEED_NUM == oplus_sla_info[i].download_num) {

			if (oplus_sla_info[i].download_speed > oplus_sla_info[i].dl_mx_speed) {
				oplus_sla_info[i].download_speed += oplus_sla_info[i].dl_mx_speed;
				oplus_sla_info[i].download_speed /= 2;
			}
			else {
				oplus_sla_info[i].download_speed = oplus_sla_info[i].dl_mx_speed;
			}

			if (oplus_sla_info[i].max_speed > oplus_sla_info[i].download_speed) {
				oplus_sla_info[i].max_speed = oplus_sla_info[i].download_speed;
			}

			oplus_sla_info[i].download_num = 0;
			oplus_sla_info[i].dl_mx_speed = 0;
			if (oplus_sla_debug) {
				printk("oplus_sla_speed: adjust download speed = %d\n",oplus_sla_info[i].download_speed);
			}
		}

		if (speed > (oplus_sla_info[i].download_speed / 2)) {
			if (speed < rom_update_info.dual_wlan_download_speed &&
				oplus_sla_info[i].download_speed < rom_update_info.dual_wlan_download_speed &&
				oplus_sla_info[i].dual_wifi_download < (2 * DOWNLOAD_FLAG)) {

				oplus_sla_info[i].dual_wifi_download++;
			} else if (speed >= rom_update_info.dual_wlan_download_speed ||
				oplus_sla_info[i].download_speed >= rom_update_info.dual_wlan_download_speed){

				if(oplus_sla_info[i].dual_wifi_download >= 2){
					oplus_sla_info[i].dual_wifi_download -= 2;
				}
				else if(oplus_sla_info[i].dual_wifi_download){
					oplus_sla_info[i].dual_wifi_download--;
				}
			}

			if(oplus_sla_info[i].download_flag < (2 * DOWNLOAD_FLAG)){
				oplus_sla_info[i].download_flag++;
			}
		}
	}
	else {
		oplus_sla_info[i].download_num = 0;
		oplus_sla_info[i].dl_mx_speed = 0;
	}

	if(speed < (oplus_sla_info[i].download_speed / 3)){

		if(oplus_sla_info[i].download_flag >= 2){
			oplus_sla_info[i].download_flag -= 2;
		}
		else if(oplus_sla_info[i].download_flag){
			oplus_sla_info[i].download_flag--;
		}

		if(oplus_sla_info[i].dual_wifi_download >= 2){
			oplus_sla_info[i].dual_wifi_download -= 2;
		}
		else if(oplus_sla_info[i].dual_wifi_download){
			oplus_sla_info[i].dual_wifi_download--;
		}
	}
	return;
}

static void adjust_speed_with_romupdate_param(int index)
{
	int i = index;

	if(CELL_INDEX == i &&
		oplus_sla_info[i].max_speed > rom_update_info.cell_speed){

		oplus_sla_info[i].max_speed = rom_update_info.cell_speed;
	}

	if(MAIN_WLAN == i || SECOND_WLAN == i){
		if(oplus_sla_info[i].wlan_score <= rom_update_info.wlan_bad_score &&
		   oplus_sla_info[i].max_speed > rom_update_info.wlan_little_score_speed){
			oplus_sla_info[i].max_speed = rom_update_info.wlan_little_score_speed;
		}
		else if(MAIN_WLAN == i &&
			oplus_sla_info[i].max_speed > rom_update_info.wlan_speed){
			oplus_sla_info[i].max_speed = rom_update_info.wlan_speed;
		}
		else if (SECOND_WLAN == i &&
			oplus_sla_info[i].max_speed > rom_update_info.second_wlan_speed) {
			oplus_sla_info[i].max_speed = rom_update_info.second_wlan_speed;
		}
	}
	return;
}

static void calc_download_speed(int index,u64 total_bytes,int time)
{
	int i = index;
	int dl_speed = 0;
	u64 bytes = 0;

	if (time >= DOWNLOAD_SPEED_TIME ) {
		if (0 == oplus_sla_info[i].dl_total_bytes ||
			oplus_sla_info[i].dl_total_bytes > total_bytes) {

			oplus_sla_info[i].dl_total_bytes = total_bytes;
		}
		else {
			bytes = total_bytes - oplus_sla_info[i].dl_total_bytes;
			dl_speed = bytes / time;
			dl_speed = dl_speed / 1000;
			oplus_sla_info[i].dl_total_bytes = total_bytes;

			detect_dl_little_speed(i,dl_speed);
			detect_network_download(i,dl_speed);

			if (dl_speed > oplus_sla_info[i].download_speed) {
				oplus_sla_info[i].left_speed = dl_speed - oplus_sla_info[i].download_speed;
			}
			else {
				oplus_sla_info[i].left_speed = oplus_sla_info[i].download_speed - dl_speed;
			}

			if (oplus_sla_debug) {
				printk("oplus_sla: cur download speed[%d] = %d\n",i,dl_speed);
			}
		}
	}
}

static inline int dev_isalive(const struct net_device *dev)
{
	return dev->reg_state <= NETREG_REGISTERED;
}

static void statistic_dev_speed(struct timeval tv,int time_interval)
{
	int i=0;
	int temp_speed;
	u64	temp_bytes;
	u64 total_bytes = 0;
	int tmp_minute_time;
	int tmp_minute_speed;
	int download_time = 0;
	int do_calc_minute_speed = 0;
	struct net_device *dev;
	const struct rtnl_link_stats64 *stats;
	struct rtnl_link_stats64 temp;

	tmp_minute_time = tv.tv_sec - last_minute_speed_tv.tv_sec;
	if(tmp_minute_time >= LITTLE_FLOW_TIME){
		last_minute_speed_tv = tv;
		do_calc_minute_speed = 1;
	}

	download_time = tv.tv_sec - last_download_speed_tv.tv_sec;
	if (download_time >= DOWNLOAD_SPEED_TIME) {
		last_download_speed_tv = tv;
	}

	for(i = 0; i < IFACE_NUM; i++){
		if(oplus_sla_info[i].if_up || oplus_sla_info[i].need_up){
			dev = dev_get_by_name(&init_net, oplus_sla_info[i].dev_name);
			if(dev) {
				if (dev_isalive(dev) &&
					(NULL != dev->netdev_ops) &&
					(stats = dev_get_stats(dev, &temp))){
					//first time have no value,and  maybe oplus_sla_info[i].rx_bytes will more than stats->rx_bytes
					total_bytes = stats->rx_bytes + stats->tx_bytes;
					if(0 == oplus_sla_info[i].total_bytes ||
						oplus_sla_info[i].total_bytes > total_bytes){

						oplus_sla_info[i].total_bytes = total_bytes;
						oplus_sla_info[i].minute_rx_bytes = total_bytes;
					}
					else{

						if(do_calc_minute_speed){

							temp_bytes = total_bytes - oplus_sla_info[i].minute_rx_bytes;
							oplus_sla_info[i].minute_rx_bytes = total_bytes;
							tmp_minute_speed = (8 * temp_bytes) / tmp_minute_time; //kbit/s
							oplus_sla_info[i].minute_speed = tmp_minute_speed / 1000;

							if(MAIN_WLAN == i){
								detect_wlan_state_with_speed();
							}
						}

						temp_bytes = total_bytes - oplus_sla_info[i].total_bytes;
						oplus_sla_info[i].total_bytes = total_bytes;

						temp_speed = temp_bytes / time_interval;
						temp_speed = temp_speed / 1000;//kB/s
						oplus_sla_info[i].current_speed = temp_speed;

						if(temp_speed > oplus_sla_info[i].max_speed){
							oplus_sla_info[i].max_speed = temp_speed;
						}

						if (temp_speed > oplus_sla_info[i].download_speed) {
							oplus_sla_info[i].download_speed = temp_speed;
						}

						calc_download_speed(i,total_bytes,download_time);
						reset_network_state_by_speed(i,temp_speed);
						adjust_speed_with_romupdate_param(i);

					}
				}
				dev_put(dev);
			}

			if(oplus_sla_debug /*&& net_ratelimit()*/){
				printk("oplus_sla: dev_name = %s,if_up = %d,max_speed = %d,"
						"current_speed = %d,avg_rtt = %d,congestion = %d,"
						"is_download = %d,syn_retran = %d,minute_speed = %d,"
						"weight_state = %d,download_speed = %d,dual_wifi_download = %d,dl_little_speed = %d\n",
						oplus_sla_info[i].dev_name,oplus_sla_info[i].if_up,
						oplus_sla_info[i].max_speed,oplus_sla_info[i].current_speed,
						oplus_sla_info[i].sla_avg_rtt,oplus_sla_info[i].congestion_flag,
						oplus_sla_info[i].download_flag,oplus_sla_info[i].syn_retran,
						oplus_sla_info[i].minute_speed,oplus_sla_info[i].weight_state,
						oplus_sla_info[i].download_speed,oplus_sla_info[i].dual_wifi_download,oplus_sla_info[i].dl_little_speed);
			}

		}
	}
}


static void reset_invalid_network_info(struct oplus_dev_info *node)
{
	struct timeval tv;

	//to avoid when weight_state change WEIGHT_STATE_USELESS now ,
	//but the next moment change to WEIGHT_STATE_RECOVERY
	//because of minute_speed is little than MINUTE_LITTE_RATE;
	do_gettimeofday(&tv);
	last_minute_speed_tv = tv;
	node->minute_speed = MINUTE_LITTE_RATE;

	sla_rtt_write_lock();
	node->rtt_index = 0;
	node->sum_rtt= 0;
	node->avg_rtt = 0;
	node->sla_avg_rtt = 0;
	node->sla_rtt_num = 0;
	node->sla_sum_rtt = 0;
	sla_rtt_write_unlock();

	node->max_speed = 0;
	node->left_speed = 0;
	node->current_speed = 0;
	node->syn_retran = 0;
	node->download_flag = 0;
	node->download_speed = 0;
	node->dual_wifi_download = 0;
	node->weight_state = WEIGHT_STATE_USELESS;

	if(oplus_sla_debug){
		printk("oplus_sla: reset_invalid_network_info,dev_name = %s\n",node->dev_name);
	}
}

static bool calc_weight_with_wlan_state(void)
{
	bool ret = false;

	if(is_wlan_speed_good(MAIN_WLAN)){
		ret = true;
		oplus_sla_info[MAIN_WLAN].weight = 100;
		if (SLA_MODE_WIFI_CELL == sla_work_mode) {
			oplus_sla_info[CELL_INDEX].weight = 0;
		} else if (SLA_MODE_DUAL_WIFI == sla_work_mode){
			oplus_sla_info[SECOND_WLAN].weight = 0;
		}
	}
	return ret;
}

static int calc_weight_with_speed(int speed_1,int speed_2)
{
	int tmp_weight;
	int sum_speed = speed_1 + speed_2;

	tmp_weight = (100 * speed_1) / sum_speed;

	return tmp_weight;
}

/*network_1 must be the main wifi */
static int calc_weight_with_left_speed(int network_1,int network_2)
{
	int speed1;
	int speed2;
	int index1 = network_1;
	int index2 = network_2;

	if((oplus_sla_info[index1].download_flag >= DOWNLOAD_FLAG ||
		oplus_sla_info[index2].download_flag >= DOWNLOAD_FLAG) &&
		(oplus_sla_info[index1].max_speed > CALC_WEIGHT_MIN_SPEED_2 &&
		  oplus_sla_info[index2].max_speed > CALC_WEIGHT_MIN_SPEED_2)){

		if (oplus_sla_info[index1].download_flag >= DOWNLOAD_FLAG) {
			if (is_wlan_speed_good(index2) &&
				oplus_sla_info[index1].download_speed <= MAX_WLAN_SPEED){
				if (oplus_sla_debug) {
					printk("oplus_sla_weight:network1 is download and network2 is much better\n");
				}
				oplus_sla_info[index1].weight = 0;
				oplus_sla_info[index2].weight = 100;
				return 1;
			}

			if (oplus_sla_info[index2].max_speed <= CALC_WEIGHT_MIN_SPEED_3) {
				if (oplus_sla_info[index2].avg_rtt >= rom_update_info.sla_rtt) {
					if (oplus_sla_debug) {
						printk("oplus_sla_weight:network1 is download but network1 is very bad\n");
					}
					oplus_sla_info[index1].weight = 100;
					oplus_sla_info[index2].weight = 0;
					return 1;
				}
				speed1 = oplus_sla_info[index1].max_speed;
			} else {
				speed1 = oplus_sla_info[index1].left_speed;
			}
		}
		else {
			speed1 = oplus_sla_info[index1].max_speed;
		}

		if (oplus_sla_info[index2].download_flag >= DOWNLOAD_FLAG) {
			if (is_wlan_speed_good(index1) &&
				oplus_sla_info[index2].download_speed <= MAX_WLAN_SPEED){
				if (oplus_sla_debug) {
					printk("oplus_sla_weight:network2 is download and network1 is much better\n");
				}
				oplus_sla_info[index1].weight = 100;
				oplus_sla_info[index2].weight = 0;
				return 1;
			}

			if (oplus_sla_info[index1].max_speed <= CALC_WEIGHT_MIN_SPEED_3) {
				if (oplus_sla_info[index1].avg_rtt >= rom_update_info.sla_rtt) {
					if (oplus_sla_debug) {
						printk("oplus_sla_weight:network2 is download but network1 is very bad\n");
					}
					oplus_sla_info[index1].weight = 0;
					oplus_sla_info[index2].weight = 100;
					return 1;
				}
				speed2 = oplus_sla_info[index2].max_speed;
			} else {
				speed2 = oplus_sla_info[index2].left_speed;
			}
		}
		else {
			speed2 = oplus_sla_info[index2].max_speed;
		}

		oplus_sla_info[index1].weight = calc_weight_with_speed(speed1,speed2);
		oplus_sla_info[index2].weight = 100;
		if (oplus_sla_debug) {
			printk("oplus_sla_weight: calc weight with download speed\n");
		}
		return 1;
	}

	return 0;
}

static int calc_weight_with_weight_state(int network_1,int network_2)
{
	int index1 = network_1;
	int index2 = network_2;

	if ((WEIGHT_STATE_USELESS == oplus_sla_info[index1].weight_state ||
		WEIGHT_STATE_SCORE_INVALID == oplus_sla_info[index1].weight_state) &&
		(WEIGHT_STATE_USELESS != oplus_sla_info[index2].weight_state &&
		 WEIGHT_STATE_SCORE_INVALID != oplus_sla_info[index2].weight_state)) {

		oplus_sla_info[index1].weight = 0;
		oplus_sla_info[index2].weight = 100;
		if (oplus_sla_debug) {
			printk("oplus_sla_weight: [%d] calc weight with WEIGHT_STATE_USELESS \n",index1);
		}
		return 1;
	}
	else if ((WEIGHT_STATE_USELESS != oplus_sla_info[index1].weight_state &&
		  WEIGHT_STATE_SCORE_INVALID != oplus_sla_info[index1].weight_state) &&
		 (WEIGHT_STATE_USELESS == oplus_sla_info[index2].weight_state ||
		  WEIGHT_STATE_SCORE_INVALID == oplus_sla_info[index2].weight_state)) {

		oplus_sla_info[index2].weight = 0;
		oplus_sla_info[index1].weight = 100;
		if (oplus_sla_debug) {
			printk("oplus_sla_weight: [%d]calc weight with WEIGHT_STATE_USELESS \n",index2);
		}
		return 1;
	}
	return 0;
}

static int calc_weight_with_little_speed(int network_1,int network_2)
{
	int index1 = network_1;
	int index2 = network_2;

	if((is_wlan_speed_good(index2) &&
	   oplus_sla_info[index1].max_speed <= CALC_WEIGHT_MIN_SPEED_2) ||
	   (oplus_sla_info[index1].max_speed < CALC_WEIGHT_MIN_SPEED_1 &&
		CONGESTION_LEVEL_HIGH == oplus_sla_info[index1].congestion_flag)){

		oplus_sla_info[index1].weight = 0;
		oplus_sla_info[index2].weight = 100;
		if (oplus_sla_debug) {
			printk("oplus_sla_weight:calc weight with little speed \n");
		}
		return 1;
	}

	return 0;
}

static bool is_main_wlan_poor(void)
{
	int index = MAIN_WLAN;
	int score = 10 + rom_update_info.dual_wlan_bad_score;

	if(oplus_sla_info[index].wlan_score > 10 &&
		oplus_sla_info[index].wlan_score <= score) {
		return true;
	}

	if (oplus_sla_info[index].download_flag >= DOWNLOAD_FLAG) {
		return true;
	}

	if (oplus_sla_info[index].max_speed <= rom_update_info.sla_speed &&
		oplus_sla_info[index].sla_avg_rtt >= rom_update_info.dual_wifi_rtt) {
		return true;
	}

	if (oplus_sla_info[index].dl_little_speed < rom_update_info.sla_speed &&
		oplus_sla_info[index].download_speed < VIDEO_SPEED) {
		return true;
	}

	return false;
}

/*network_1 must be the main wifi */
static void recalc_two_network_weight(int network_1,int network_2)
{
	int index1 = network_1;
	int index2 = network_2;

	int speed1 = oplus_sla_info[index1].max_speed;
	int speed2 = oplus_sla_info[index2].max_speed;

	if(oplus_sla_info[index1].if_up &&
		oplus_sla_info[index2].if_up){

		if(calc_weight_with_wlan_state() ||
		   calc_weight_with_weight_state (index1, index2) ||
		   calc_weight_with_left_speed(index1, index2) ||
		   calc_weight_with_little_speed(index1, index2)){
			goto calc_weight_finish;
		}

		if((speed1 >= CALC_WEIGHT_MIN_SPEED_2 &&
			speed2 >= CALC_WEIGHT_MIN_SPEED_2) ||
		   (CONGESTION_LEVEL_HIGH == oplus_sla_info[index1].congestion_flag ||
			CONGESTION_LEVEL_HIGH == oplus_sla_info[index2].congestion_flag) ||
			((CONGESTION_LEVEL_MIDDLE == oplus_sla_info[index1].congestion_flag ||
			  CONGESTION_LEVEL_MIDDLE == oplus_sla_info[index2].congestion_flag) &&
			 (speed1 >= CALC_WEIGHT_MIN_SPEED_1 && speed2 >= CALC_WEIGHT_MIN_SPEED_1))){

			oplus_sla_info[index1].weight = calc_weight_with_speed(speed1,speed2);
		}
		else{
			if (SLA_MODE_DUAL_WIFI == sla_work_mode &&
				INIT_ACTIVE_TYPE == dual_wifi_active_type &&
				!is_main_wlan_poor()) {

				//for maul active or NetworRequest
				oplus_sla_info[index1].weight = 100;
				oplus_sla_info[index2].weight = 0;
			} else if (SLA_MODE_DUAL_WIFI == sla_work_mode) {
				oplus_sla_info[index1].weight = 30;
				oplus_sla_info[index2].weight = 100;
			} else if (SLA_MODE_WIFI_CELL == sla_work_mode) {
				oplus_sla_info[index1].weight = 15;
				oplus_sla_info[index2].weight = 100;
			}
			goto calc_weight_finish;
		}
	}
	else if(oplus_sla_info[index1].if_up){
		oplus_sla_info[index1].weight = 100;
	}
	oplus_sla_info[index2].weight = 100;

calc_weight_finish:

	if(oplus_sla_debug){
		printk("oplus_sla_weight: work_mode = %d,weight[%d] = %d,weight[%d] = %d\n",
				sla_work_mode,index1,oplus_sla_info[index1].weight,index2,oplus_sla_info[index2].weight);
	}
	return;
}

static void recalc_dual_wifi_weight(void)
{
	recalc_two_network_weight(MAIN_WLAN,SECOND_WLAN);
	return;
}

static void recalc_wifi_cell_weight(void)
{
	recalc_two_network_weight(MAIN_WLAN,CELL_INDEX);
	oplus_sla_info[SECOND_WLAN].weight = 0;
	return;
}


static void recalc_dual_wifi_cell_weight(void)
{
	int i = 0;
	int sum_speed = 0;
	int speed1 = oplus_sla_info[0].max_speed;
	int speed2 = oplus_sla_info[1].max_speed;
	int speed3 = oplus_sla_info[2].max_speed;

	for (i = 0; i < IFACE_NUM; i++) {
		if (!oplus_sla_info[i].if_up) {
			return;
		}
	}

	if (is_wlan_speed_good(MAIN_WLAN) ||
		is_wlan_speed_good(SECOND_WLAN) ||
		oplus_sla_info[MAIN_WLAN].download_flag >= DOWNLOAD_FLAG ||
		oplus_sla_info[SECOND_WLAN].download_flag >= DOWNLOAD_FLAG) {
		recalc_two_network_weight(MAIN_WLAN,SECOND_WLAN);
		goto finish_calc;
	}

	if ((speed1 >= CALC_WEIGHT_MIN_SPEED_2 &&
		 speed2 >= CALC_WEIGHT_MIN_SPEED_2 &&
		 speed2 >= CALC_WEIGHT_MIN_SPEED_2) ||
		(oplus_sla_info[0].congestion_flag > CONGESTION_LEVEL_NORMAL &&
		 oplus_sla_info[1].congestion_flag > CONGESTION_LEVEL_NORMAL &&
		 oplus_sla_info[2].congestion_flag > CONGESTION_LEVEL_NORMAL)) {

		sum_speed = speed1 + speed2 + speed3;
		oplus_sla_info[0].weight = (100 * speed1) / sum_speed;
		oplus_sla_info[1].weight = (100 * (speed1 + speed2)) / sum_speed;
		oplus_sla_info[2].weight = 100;
		goto finish_calc;
	}

	/*INIT dual wifi cell weight*/
	init_dual_wifi_cell_weight();
finish_calc:
	if(oplus_sla_debug){
		printk("oplus_sla_weight: work_mode = %d,weight[0] = %d,weight[1] = %d,weight[2] = %d\n",
				sla_work_mode,oplus_sla_info[0].weight,oplus_sla_info[1].weight,oplus_sla_info[2].weight);
	}
	return;
}

static void recalc_sla_weight(void)
{
	if(!oplus_sla_enable){
		return;
	}

	sla_write_lock();
	if (SLA_MODE_DUAL_WIFI == sla_work_mode) {
		recalc_dual_wifi_weight();
	}
	else if (SLA_MODE_WIFI_CELL == sla_work_mode) {
		recalc_wifi_cell_weight();
	}
	else if (SLA_MODE_DUAL_WIFI_CELL  == sla_work_mode) {
		recalc_dual_wifi_cell_weight();
	}
	sla_write_unlock();

	return;
}


/*for when wlan up,but cell down,this will affect the rtt calc at wlan network(will calc big)*/
static void up_wlan_iface_by_timer(struct timeval tv)
{
	int i = 0;
	int time = 10;
	for (i = 0; i < WLAN_NUM; i++) {
		if(oplus_sla_info[i].need_up) {
			if (SECOND_WLAN == i) {
				time = 5;
			}

			if ((tv.tv_sec - calc_wlan_rtt_tv.tv_sec) >= time){

				oplus_sla_info[i].if_up = 1;
				oplus_sla_info[i].need_up = false;

				printk("oplus_sla:wlan[%d]time[%d] up wlan iface actually\n",i, time);
			}
		}
	}
}

static void enable_to_user_time_out(struct timeval tv)
{
	if(enable_cell_to_user &&
		(tv.tv_sec - last_enable_cell_tv.tv_sec) >= ENABLE_TO_USER_TIMEOUT){
		enable_cell_to_user = false;
	}

	if(enable_second_wifi_to_user &&
		(tv.tv_sec - last_enable_second_wifi_tv.tv_sec) >= ENABLE_TO_USER_TIMEOUT){
		enable_second_wifi_to_user = false;
	}

	return;
}

static void send_speed_and_rtt_to_user(void)
{
	int ret = 0;
	if (oplus_sla_info[MAIN_WLAN].if_up || oplus_sla_info[MAIN_WLAN].need_up ||
	    oplus_sla_info[CELL_INDEX].if_up) {
	    int payload[8];
	    //add for android Q statictis tcp tx and tx
	    u64 tcp_tx_rx[4];
	    char total_payload[64] = {0};

	    payload[0] = oplus_sla_info[MAIN_WLAN].max_speed;
	    payload[1] = oplus_sla_info[MAIN_WLAN].avg_rtt;

	    payload[2] = oplus_sla_info[SECOND_WLAN].max_speed;
	    payload[3] = oplus_sla_info[SECOND_WLAN].avg_rtt;

	    payload[4] = oplus_sla_info[CELL_INDEX].max_speed;
	    payload[5] = oplus_sla_info[CELL_INDEX].avg_rtt;

	    payload[6] = sla_work_mode;
		payload[7] = oplus_sla_info[MAIN_WLAN].download_speed;

	    tcp_tx_rx[0] = wlan0_tcp_rx;
	    tcp_tx_rx[1] = wlan0_tcp_tx;
	    tcp_tx_rx[2] = wlan1_tcp_rx;
	    tcp_tx_rx[3] = wlan1_tcp_tx;

	    memcpy(total_payload,payload,sizeof(payload));
	    memcpy(total_payload + sizeof(payload),tcp_tx_rx,sizeof(tcp_tx_rx));

	    ret = oplus_sla_send_to_user(SLA_NOTIFY_SPEED_RTT, (char *) total_payload, sizeof(total_payload));
	    if (oplus_sla_debug) {
	        printk("oplus_sla:send_speed_and_rtt_to_user wlan0:max_speed=%d, avg_rtt=%d  "
	                "wlan1:max_speed=%d, avg_rtt=%d  "
	                "cell:max_speed=%d, avg_rtt=%d, sla_work_mode=%d, wlan0_max_speed=%dKB/s,  "
	                "wlan0_tcp_rx = %llu, wlan0_tcp_tx = %llu, wlan1_tcp_rx = %llu, wlan1_tcp_tx = %llu\n",
	               payload[0], payload[1], payload[2], payload[3], payload[4], payload[5], payload[6], payload[7],
	               tcp_tx_rx[0],tcp_tx_rx[1], tcp_tx_rx[2],tcp_tx_rx[3]);
	    }
	}
	return;
}

static int wlan_can_enable_second_wifi(int index)
{
	int active_type = 0;

	/*if game is in front, do not active dual wifi by kernel*/
	if (wzry_traffic_info.game_in_front ||
		cjzc_traffic_info.game_in_front) {
		return active_type;
	}

	if (oplus_sla_info[index].if_up ||
		oplus_sla_info[index].need_up) {
		int score = 10 + rom_update_info.dual_wlan_bad_score;

		if (oplus_sla_info[index].max_speed <= rom_update_info.sla_speed &&
			oplus_sla_info[index].sla_avg_rtt >= rom_update_info.dual_wifi_rtt) {
			active_type = LOW_SPEED_HIGH_RTT;
			return active_type;
		}

		if(oplus_sla_info[index].wlan_score > 10 &&
			oplus_sla_info[index].wlan_score <= score) {
			printk("oplus_sla: send enable dual wifi with score[%d]\n",score);
			active_type = LOW_WLAN_SCORE;
			return active_type;
		}
	}
	return active_type;
}

static bool wlan_can_enable_cell(int index)
{
	if (oplus_sla_info[index].if_up ||
		oplus_sla_info[index].need_up) {

		if (oplus_sla_info[index].download_flag < DOWNLOAD_FLAG) {
			int score = rom_update_info.wlan_bad_score;

			if(SLA_MODE_DUAL_WIFI != sla_work_mode &&
				oplus_sla_info[index].wlan_score > 10 &&
				oplus_sla_info[index].wlan_score <= score) {
				printk("oplus_sla: send enable sla with score[%d]\n",score);
				return true;
			}

			if (oplus_sla_info[index].max_speed <= rom_update_info.sla_speed &&
				(oplus_sla_info[index].sla_avg_rtt >= rom_update_info.sla_rtt ||
				 oplus_sla_info[index].wlan_score_bad_count >= WLAN_SCORE_BAD_NUM)) {

				return true;
			}
		}
	}
	return false;
}

static void sla_show_dailog(void)
{
	int i = 0;
	int bad_count = 0;
	bool wlan_bad = false;

	/*if has send msg to up second wifi,do no
	show SLA dailog*/
	if(enable_second_wifi_to_user) {
		return;
	}

	if(sla_screen_on &&
		!sla_switch_enable &&
		need_show_dailog &&
		cell_quality_good &&
		!send_show_dailog_msg &&
		!rate_limit_info.rate_limit_enable) {

		for (i = 0; i < WLAN_NUM; i++) {
			if (oplus_sla_info[i].if_up &&
				oplus_sla_info[i].download_flag < DOWNLOAD_FLAG &&
				oplus_sla_info[i].sla_avg_rtt >= rom_update_info.sla_rtt){

				bad_count++;
				if (MAIN_WLAN == i &&
					SLA_MODE_DUAL_WIFI != sla_work_mode){
					wlan_bad = true;
				}
				else if (WLAN_NUM == bad_count){
					wlan_bad = true;
				}
			}
		}

		if (wlan_bad) {
			send_show_dailog_msg = 1;
			do_gettimeofday(&last_show_daillog_msg_tv);
			oplus_sla_send_to_user(SLA_SHOW_DIALOG_NOW,NULL,0);
			if(oplus_sla_debug){
				printk("oplus_sla_netlink:show dailog now\n");
			}
		}

	}
	return;
}

static bool dual_wlan_enable_cell(void)
{
	int sum_speed = 0;
	if (wlan_can_enable_cell(MAIN_WLAN) &&
		wlan_can_enable_cell(SECOND_WLAN)) {
		if (oplus_sla_enable) {
			printk("oplus_sla_netlink:[0] dual sta enable cell\n");
		}
		return true;
	}
	else if (!init_weight_delay_count) {
		sum_speed = oplus_sla_info[MAIN_WLAN].max_speed + oplus_sla_info[SECOND_WLAN].max_speed;
		if (sum_speed <= rom_update_info.sla_speed &&
			(oplus_sla_info[MAIN_WLAN].sla_avg_rtt >= rom_update_info.sla_rtt ||
			  oplus_sla_info[SECOND_WLAN].sla_avg_rtt >= rom_update_info.sla_rtt)) {

			if (oplus_sla_enable) {
				printk("oplus_sla_netlink:[1] dual sta enable cell\n");
			}
			return true;
		}
	}

	return false;
}

static void sla_detect_should_enable(void)
{
	int active_type = 0;
	if (sla_screen_on && CELL_INDEX != oplus_sla_def_net) {
		if (dual_wifi_switch_enable &&
			SLA_MODE_DUAL_WIFI == sla_detect_mode) {
			if (!oplus_sla_enable) {
				if (!enable_second_wifi_to_user) {
					active_type = wlan_can_enable_second_wifi(MAIN_WLAN);
					dual_wifi_active_type = active_type;
					if (active_type) {
						send_enable_to_framework(SLA_MODE_DUAL_WIFI,active_type);
					}
				}
				else if (!enable_cell_to_user &&
					sla_switch_enable &&
					cell_quality_good &&
					!rate_limit_info.rate_limit_enable &&
					wlan_can_enable_cell(MAIN_WLAN)) {
					send_enable_to_framework(SLA_MODE_WIFI_CELL,INIT_ACTIVE_TYPE);
				}
			}
		}
		else if(!enable_cell_to_user &&
				sla_switch_enable &&
				cell_quality_good &&
				!rate_limit_info.rate_limit_enable &&
				SLA_MODE_WIFI_CELL == sla_detect_mode) {

			if (SLA_MODE_DUAL_WIFI == sla_work_mode) {
				if (dual_wlan_enable_cell()) {
					send_enable_to_framework(SLA_MODE_WIFI_CELL,INIT_ACTIVE_TYPE);
				}
			}else if (!oplus_sla_enable &&
					   wlan_can_enable_cell(MAIN_WLAN)) {
					send_enable_to_framework(SLA_MODE_WIFI_CELL,INIT_ACTIVE_TYPE);
			}
		}
	}
}

static void calc_network_rtt(struct timeval tv)
{

	int index = 0;
	int avg_rtt = 0;

	sla_rtt_write_lock();
	for(index = 0; index < IFACE_NUM; index++){
		avg_rtt = 0;
		if(oplus_sla_info[index].if_up || oplus_sla_info[index].need_up){

			if(oplus_sla_info[index].rtt_index >= RTT_NUM){

				avg_rtt = oplus_sla_info[index].sum_rtt / oplus_sla_info[index].rtt_index;
				if(oplus_sla_info[index].download_flag >= DOWNLOAD_FLAG) {
 					avg_rtt = avg_rtt / 2;
				}

				oplus_sla_info[index].sla_rtt_num++;
				oplus_sla_info[index].sla_sum_rtt += avg_rtt;
				oplus_sla_info[index].avg_rtt = (7*oplus_sla_info[index].avg_rtt + avg_rtt) / 8;
				oplus_sla_info[index].sum_rtt = 0;
				oplus_sla_info[index].rtt_index = 0;
			}

			avg_rtt = oplus_sla_info[index].sla_avg_rtt;

			if(oplus_sla_debug){
				printk("oplus_sla_rtt: index = %d,wlan_rtt = %d,"
					   "wlan score bad = %u,cell_good = %d,need_show_dailog = %d,sceen_on = %d,"
					   "enable_cell_to_user = %d,enable_wifi_to_user = %d,sla_switch_enable = %d,oplus_sla_enable= %d,"
					   "oplus_sla_def_net = %d,sla_avg_rtt = %d,detect_mode = %d,work_mode = %d,game_cell_to_wifi = %d\n",
					   index,oplus_sla_info[index].avg_rtt,
					   oplus_sla_info[index].wlan_score_bad_count,
					   cell_quality_good,need_show_dailog,sla_screen_on,
					   enable_cell_to_user,enable_second_wifi_to_user,sla_switch_enable,
					   oplus_sla_enable,oplus_sla_def_net,avg_rtt,sla_detect_mode,sla_work_mode,game_cell_to_wifi);
			}
		}
	}
	sla_rtt_write_unlock();

	return;

}

static void init_game_online_info(void)
{
	int i = 0;

	game_cell_to_wifi = false;

	sla_game_write_lock();
	for(i = 1; i < GAME_NUM; i++){
		game_uid[i].mark = MAIN_WLAN_MARK;
		game_uid[i].switch_time = 0;
	}
	sla_game_write_unlock();
	if(oplus_sla_debug){
		printk("oplus_sla:init_game_online_info\n");
	}
	memset(&game_online_info,0x0,sizeof(struct oplus_game_online));
}

static void init_game_start_state(void)
{
    int i = 0;
    for(i = 1; i < GAME_NUM; i++){
        if(game_start_state[i]){
            game_start_state[i] = 0;
        }
    }
}

static void game_rx_update(void)
{
	if (!oplus_sla_vpn_connected &&
		game_online_info.game_online) {
		struct game_traffic_info* cur_info;
		int i;
		u32 game_index = 0;
		u32 tcp_rx_large_count = 0;
		u32 delta_udp_pkts = game_online_info.udp_rx_pkt_count;
		u64 delta_tcp_bytes = game_online_info.tcp_rx_byte_count;
		game_online_info.udp_rx_pkt_count = 0;
		game_online_info.tcp_rx_byte_count = 0;

		if (wzry_traffic_info.game_in_front) {
			cur_info = &wzry_traffic_info;
			game_index = 1;
		} else if (cjzc_traffic_info.game_in_front) {
			cur_info = &cjzc_traffic_info;
			game_index = 2;
		} else {
			//for debug only...
			cur_info = &default_traffic_info;
			cur_info->game_in_front = 0;
		}
		//fill in the latest delta udp rx packets and tcp rx bytes
		cur_info->udp_rx_packet[cur_info->window_index % UDP_RX_WIN_SIZE] = delta_udp_pkts;
		cur_info->tcp_rx_byte[cur_info->window_index % TCP_RX_WIN_SIZE] = delta_tcp_bytes;
		cur_info->window_index++;
		//deal with the control logic
		if (udp_rx_show_toast) {
			oplus_sla_send_to_user(SLA_NOTIFY_GAME_RX_PKT, (char *)&delta_udp_pkts, sizeof(delta_udp_pkts));
		}

		for (i = 0; i < TCP_RX_WIN_SIZE; i++) {
			if (cur_info->tcp_rx_byte[i] > TCP_DOWNLOAD_THRESHOLD) {
				tcp_rx_large_count++;
			}
		}
		if (tcp_rx_large_count == TCP_RX_WIN_SIZE) {
			if (oplus_sla_debug) {
				//tcp downloading.. this should not be in a game.
				if (oplus_sla_debug) {
					printk("oplus_sla_game_rx_update:TCP downloading...should not be in a game.\n");
				}
			}
			if (inGame) {
				inGame = false;
				game_cell_to_wifi = false;
				memset(cur_info->udp_rx_packet, 0x0, sizeof(u32)*UDP_RX_WIN_SIZE);
				cur_info->window_index = 0;
			}
		} else {
			int trafficCount = 0;
			int lowTrafficCount = 0;
			for (i = 0; i < UDP_RX_WIN_SIZE; i++) {
				if (cur_info->udp_rx_packet[i] >= cur_info->udp_rx_min) {
					trafficCount++;
					if (cur_info->udp_rx_packet[i] <= cur_info->udp_rx_low_thr) {
						lowTrafficCount++;
					}
				}
			}
			if (trafficCount >= cur_info->in_game_true_thr) {
				if (!inGame) {
					int index;
					inGame = true;
					game_cell_to_wifi = false;
					for (index = 0; index < UDP_RX_WIN_SIZE; index++) {
						cur_info->udp_rx_packet[index] = 20;
					}
					lowTrafficCount = 0;
				}
			} else if (trafficCount <= cur_info->in_game_false_thr) {
				if (inGame) {
					inGame = false;
					game_cell_to_wifi = false;
					memset(cur_info->udp_rx_packet, 0x0, sizeof(u32)*UDP_RX_WIN_SIZE);
					cur_info->window_index = 0;
				}
			}
			if (inGame && cur_info->game_in_front) {
				if (lowTrafficCount >= cur_info->rx_bad_thr &&
					game_index > 0 && game_start_state[game_index]) {//wzry has low traffic when game ends
					game_rx_bad = true;
				} else {
					game_rx_bad = false;
				}
			}
			if (oplus_sla_debug) {
				printk("oplus_sla_game_rx_update:delta_udp_pkts=%d, lowTrafficCount=%d, inGame=%d, "
				        "game_rx_bad=%d, delta_tcp_bytes=%llu, tcp_rx_large_count=%d, trafficCount=%d\n",
				        delta_udp_pkts, lowTrafficCount, inGame,
				        game_rx_bad, delta_tcp_bytes, tcp_rx_large_count, trafficCount);
			}
		}
	}
}

static void game_online_time_out(struct timeval tv)
{
	if(game_online_info.game_online &&
	   (tv.tv_sec - game_online_info.last_game_skb_tv.tv_sec) >= GAME_SKB_TIME_OUT){
		init_game_start_state();
		init_game_online_info();
	}
}

static void show_dailog_msg_time_out(struct timeval tv)
{
	int time_interval = 0;
	int time_out = 30;
	time_interval = tv.tv_sec - last_show_daillog_msg_tv.tv_sec;

	if(time_interval >= time_out && send_show_dailog_msg){
		send_show_dailog_msg = 0;
	}
}

//add for rate limit function to statistics front uid rtt
static void oplus_rate_limit_rtt_calc(struct timeval tv)
{
	int num = 0;
	int sum = 0;
	int time_interval = 0;

	time_interval = tv.tv_sec - rate_limit_info.last_tv.tv_sec;
	if(time_interval >= 10){
		sla_rtt_write_lock();

		if(rate_limit_info.disable_rtt_num >= 10){
			num = rate_limit_info.disable_rtt_num;
		    sum = rate_limit_info.disable_rtt_sum;
			rate_limit_info.disable_rtt = sum / num;
		}

		if(rate_limit_info.enable_rtt_num >= 10){
			num = rate_limit_info.enable_rtt_num;
		    sum = rate_limit_info.enable_rtt_sum;
			rate_limit_info.enable_rtt = sum / num;
		}

		rate_limit_info.disable_rtt_num = 0;
		rate_limit_info.disable_rtt_sum = 0;
		rate_limit_info.enable_rtt_num = 0;
		rate_limit_info.enable_rtt_sum = 0;
		rate_limit_info.last_tv = tv;

		sla_rtt_write_unlock();
	}
}

static void dns_statistic_timer(struct timeval tv)
{
	int i;

	for (i = 0; i < IFACE_NUM; i++) {
		if(dns_info[i].in_timer &&
			(tv.tv_sec - dns_info[i].last_tv.tv_sec) >= DNS_TIME){

			dns_info[i].send_num = 0;
			dns_info[i].in_timer = false;
			reset_invalid_network_info(&oplus_sla_info[i]);

			sla_write_lock();
			if (SLA_MODE_DUAL_WIFI == sla_work_mode) {
				calc_weight_with_weight_state (MAIN_WLAN,SECOND_WLAN);
			}
			else if (SLA_MODE_WIFI_CELL == sla_work_mode) {
				calc_weight_with_weight_state (MAIN_WLAN,CELL_INDEX);
			}
			sla_write_unlock();

			printk("oplus_sla_dns: detec dns[%d] error\n",i);
		}
	}
	return;
}

static void oplus_sla_work_queue_func(struct work_struct *work)
{
	int time_interval;
	struct timeval tv;

	do_gettimeofday(&tv);

	time_interval = tv.tv_sec - last_speed_tv.tv_sec;

	if(time_interval >= CALC_DEV_SPEED_TIME){
		last_speed_tv = tv;
		calc_network_congestion();
		statistic_dev_speed(tv,time_interval);
		calc_network_rtt(tv);
		sla_show_dailog();
		sla_detect_should_enable();
	}

	if(oplus_sla_enable){
		time_interval = tv.tv_sec - last_weight_tv.tv_sec;
		if(time_interval >= RECALC_WEIGHT_TIME){
			last_weight_tv = tv;

			if (!init_weight_delay_count) {
				recalc_sla_weight();
			} else {
				init_weight_delay_count--;
			}
		}
	}

    dns_statistic_timer(tv);
	enable_to_user_time_out(tv);
	up_wlan_iface_by_timer(tv);
	game_online_time_out(tv);
	game_rx_update();
	show_dailog_msg_time_out(tv);
	reset_oplus_sla_calc_speed(tv);
	oplus_rate_limit_rtt_calc(tv);
	send_speed_and_rtt_to_user();
}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
static void oplus_sla_timer_function(unsigned long a)
#else
static void oplus_sla_timer_function(struct timer_list *t)
#endif
{
	if (workqueue_sla) {
		queue_work(workqueue_sla, &oplus_sla_work);
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	mod_timer(&sla_timer, jiffies + SLA_TIMER_EXPIRES);
#else
	mod_timer(t, jiffies + SLA_TIMER_EXPIRES);
#endif
}

/*
	timer to statistic each dev speed,
	start it when sla is enabled
*/
static void oplus_sla_timer_init(void)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	init_timer(&sla_timer);
	sla_timer.function = oplus_sla_timer_function;
	sla_timer.data = ((unsigned long)0);
#else
	timer_setup(&sla_timer, oplus_sla_timer_function, 0);
#endif

	sla_timer.expires = jiffies +  SLA_TIMER_EXPIRES;/* timer expires in ~1s */
	add_timer(&sla_timer);

	do_gettimeofday(&last_speed_tv);
	do_gettimeofday(&last_weight_tv);
	do_gettimeofday(&last_minute_speed_tv);
	do_gettimeofday(&last_download_speed_tv);
	do_gettimeofday(&rate_limit_info.last_tv);
}


/*
	timer to statistic each dev speed,
	stop it when sla is disabled
*/
static void oplus_sla_timer_fini(void)
{
	del_timer(&sla_timer);
}


static struct nf_hook_ops oplus_sla_ops[] __read_mostly = {
	{
		.hook		= oplus_sla_output_hook,
		.pf		    = NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_OUT,
		//must be here,for  dns packet will do DNAT at mangle table with skb->mark
		.priority	= NF_IP_PRI_CONNTRACK + 1,
	},
	{
		.hook		= oplus_sla_input_hook,
		.pf		    = NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_FILTER + 1,
	},
};

static struct ctl_table oplus_sla_sysctl_table[] = {
	{
		.procname	= "oplus_sla_enable",
		.data		= &oplus_sla_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "oplus_sla_debug",
		.data		= &oplus_sla_debug,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "oplus_sla_calc_speed",
		.data		= &oplus_sla_calc_speed,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "oplus_sla_vpn_connected",
		.data		= &oplus_sla_vpn_connected,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "game_mark",
		.data		= &game_mark,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "disable_rtt",
		.data		= &rate_limit_info.disable_rtt,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "enable_rtt",
		.data		= &rate_limit_info.enable_rtt,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "udp_rx_show_toast",
		.data		= &udp_rx_show_toast,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "game_rtt_show_toast",
		.data		= &game_rtt_show_toast,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "tee_use_src",
		.data		= &tee_use_src,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{ }
};

static int oplus_sla_sysctl_init(void)
{
	oplus_sla_table_hrd = register_net_sysctl(&init_net, "net/oplus_sla",
		                                          oplus_sla_sysctl_table);
	return oplus_sla_table_hrd == NULL ? -ENOMEM : 0;
}

static void oplus_sla_send_white_list_app_traffic(void)
{
    char *p = NULL;
    char send_msg[1284];

    memset(send_msg,0x0,sizeof(send_msg));

    p = send_msg;
    memcpy(p,&white_app_list.count,sizeof(u32));

    p += sizeof(u32);
    memcpy(p,white_app_list.uid,WHITE_APP_NUM*sizeof(u32));

    p += WHITE_APP_NUM*sizeof(u32);
    memcpy(p,white_app_list.cell_bytes,WHITE_APP_NUM*sizeof(u64));

	p += WHITE_APP_NUM*sizeof(u64);
		memcpy(p,white_app_list.cell_bytes_normal,WHITE_APP_NUM*sizeof(u64));

	oplus_sla_send_to_user(SLA_SEND_WHITE_LIST_APP_TRAFFIC,
		     send_msg,sizeof(send_msg));

	return;
}

static int oplus_sla_set_debug(struct nlmsghdr *nlh)
{
	oplus_sla_debug = *(u32 *)NLMSG_DATA(nlh);
	printk("oplus_sla_netlink:set debug = %d\n", oplus_sla_debug);
	return	0;
}

static int oplus_sla_set_default_network(struct nlmsghdr *nlh)
{
	oplus_sla_def_net = *(u32 *)NLMSG_DATA(nlh);
	printk("oplus_sla_netlink:set default network = %d\n", oplus_sla_def_net);

        /* reset game udp rx data */
        memset(wzry_traffic_info.udp_rx_packet, 0x0, sizeof(u32)*UDP_RX_WIN_SIZE);
        wzry_traffic_info.window_index = 0;
        memset(cjzc_traffic_info.udp_rx_packet, 0x0, sizeof(u32)*UDP_RX_WIN_SIZE);
        cjzc_traffic_info.window_index = 0;

	return	0;
}

static void oplus_sla_send_game_app_traffic(void)
{
	oplus_sla_send_to_user(SLA_SEND_GAME_APP_STATISTIC,
		     (char *)game_uid,GAME_NUM*sizeof(struct oplus_sla_game_info));

	return;
}

static void oplus_sla_send_syn_retran_info(void)
{
	oplus_sla_send_to_user(SLA_GET_SYN_RETRAN_INFO,
		     (char *)&syn_retran_statistic,sizeof(struct oplus_syn_retran_statistic));

	memset(&syn_retran_statistic,0x0,sizeof(struct oplus_syn_retran_statistic));
	return;
}

static int disable_oplus_sla_module(struct nlmsghdr *nlh)
{
	int disable_type = 0;

	int *data = (int *)NLMSG_DATA(nlh);
	disable_type = data[0];
	printk("oplus_sla_netlink: type[%d] disable,oplus_sla_enable[%d],work_mode[%d]\n",
		disable_type,oplus_sla_enable,sla_work_mode);

	sla_write_lock();
	if(oplus_sla_enable && disable_type){
		if (SLA_MODE_DUAL_WIFI == disable_type) {
			main_wlan_download = 1;
			enable_second_wifi_to_user = false;
			dual_wifi_active_type = INIT_ACTIVE_TYPE;
		}
		else if (SLA_MODE_WIFI_CELL == disable_type) {
			enable_cell_to_user = false;
		}

		if (disable_type == sla_work_mode) {
			oplus_sla_enable = 0;
			sla_work_mode = SLA_MODE_INIT;

			if (dual_wifi_switch_enable) {
				sla_detect_mode = SLA_MODE_DUAL_WIFI;
			} else {
				sla_detect_mode = SLA_MODE_WIFI_CELL;
			}

			init_game_online_info();
			printk("oplus_sla_netlink: type[%d] disabled\n",disable_type);
		}
		oplus_sla_send_to_user(SLA_DISABLED, (char *)&disable_type, sizeof(int));
    }
	sla_write_unlock();
	return 0;
}

static int oplus_sla_iface_changed(struct nlmsghdr *nlh)
{
	int index = -1;
	int up = 0;
	char *p;
	struct oplus_dev_info *node = NULL;
	u32 mark = 0x0;

	int *data = (int *)NLMSG_DATA(nlh);
	index = data[0];
	up = data[1];
	p = (char *)(data + 2);
	printk("oplus_sla_netlink:oplus_sla_iface_changed index:%d, up:%d, ifname:%s\n", index, up, p);

	if (index >= 0 && index < IFACE_NUM) {
		struct timeval tv;
		do_gettimeofday(&tv);

		if (up) {
			sla_write_lock();
			oplus_sla_info[index].if_up = 0;

			if (index == MAIN_WLAN || index == SECOND_WLAN) {
				if (index == MAIN_WLAN) {
					mark = MAIN_WLAN_MARK;
					oplus_sla_info[MAIN_WLAN].need_up = true;
				} else {
					mark = SECOND_WLAN_MARK;
					oplus_sla_info[SECOND_WLAN].if_up = 1;
				}

				last_speed_tv = tv;
				last_weight_tv = tv;
				last_minute_speed_tv = tv;
				calc_wlan_rtt_tv = tv;
				last_download_speed_tv = tv;
				oplus_sla_info[CELL_INDEX].max_speed = 0;
				oplus_sla_info[index].dl_little_speed = DOWNLOAD_SPEED;
			} else if (index == CELL_INDEX) {
				mark = CELL_MARK;
				oplus_sla_info[CELL_INDEX].if_up = 1;
				oplus_sla_info[CELL_INDEX].wlan_score = WLAN_SCORE_GOOD;
			}
			if(p) {
				node = &oplus_sla_info[index];
				node->mark = mark;
				node->minute_speed = MINUTE_LITTE_RATE;
				memcpy(node->dev_name, p, IFACE_LEN);
				printk("oplus_sla_netlink:ifname = %s,ifup = %d\n", node->dev_name, node->if_up);
			}
			sla_write_unlock();
        } else {
			if (index == MAIN_WLAN || index == SECOND_WLAN) {
				oplus_sla_calc_speed = 0;
				enable_second_wifi_to_user = false;

				if (index == MAIN_WLAN) {
					enable_cell_to_user = false;
					oplus_sla_send_game_app_traffic();
					memset(game_uid, 0x0, GAME_NUM*sizeof(struct oplus_sla_game_info));
				}
			}

			sla_write_lock();
			memset(&dns_info[index], 0x0, sizeof(struct sla_dns_statistic));
			memset(&oplus_speed_info[index], 0x0, sizeof(struct oplus_speed_calc));
			memset(&oplus_sla_info[index], 0x0, sizeof(struct oplus_dev_info));
			sla_write_unlock();
        }
	}

	return 0;
}

static int oplus_sla_set_primary_wifi(struct nlmsghdr *nlh)
{

	int *data = (int *)NLMSG_DATA(nlh);
	if (*data == WLAN0_INDEX || *data == WLAN1_INDEX) {
		MAIN_WLAN = *data;
		SECOND_WLAN = (WLAN0_INDEX == MAIN_WLAN) ? WLAN1_INDEX : WLAN0_INDEX;
		if (WLAN0_INDEX == MAIN_WLAN) {
			MAIN_WLAN_MARK = WLAN0_MARK;
			SECOND_WLAN_MARK = WLAN1_MARK;
		}else {
			MAIN_WLAN_MARK = WLAN1_MARK;
			SECOND_WLAN_MARK = WLAN0_MARK;
		}
		printk("oplus_sla_netlink: oplus_sla_set_primary_wifi main_wlan[%d],second_wlan[%d]\n",
				MAIN_WLAN,SECOND_WLAN);
	} else {
		printk("oplus_sla_netlink: oplus_sla_set_primary_wifi invalid index = %d\n", *data);
	}

	return	0;
}

static int oplus_sla_get_pid(struct sk_buff *skb,struct nlmsghdr *nlh)
{
	oplus_sla_pid = NETLINK_CB(skb).portid;
	printk("oplus_sla_netlink:get oplus_sla_pid = %u\n", oplus_sla_pid);

	return 0;
}

static void set_weight_state_by_score(int index,int score)
{
	if (index >= 0 &&
		index < IFACE_NUM) {
		if (WLAN_NETWORK_INVALID == score &&
			WEIGHT_STATE_SCORE_INVALID != oplus_sla_info[index].weight_state) {
			printk("oplus_sla_score: network[%d] invalid\n",index);
			reset_invalid_network_info(&oplus_sla_info[index]);
			oplus_sla_info[index].weight_state = WEIGHT_STATE_SCORE_INVALID;

			sla_write_lock();
			if (SLA_MODE_DUAL_WIFI == sla_work_mode) {
				calc_weight_with_weight_state (MAIN_WLAN,SECOND_WLAN);
			}
			else if (SLA_MODE_WIFI_CELL == sla_work_mode) {
				calc_weight_with_weight_state (MAIN_WLAN,CELL_INDEX);
			}
			sla_write_unlock();

		} else if (!oplus_sla_info[index].wlan_score_bad_count &&
				WEIGHT_STATE_SCORE_INVALID == oplus_sla_info[index].weight_state) {
			printk("oplus_sla_score: network[%d] reset to normal\n",index);
			oplus_sla_info[index].weight_state = WEIGHT_STATE_NORMAL;
		}
	}

	return;
}

static int oplus_sla_set_wifi_score(struct nlmsghdr *nlh)
{
	int *data = (int *)NLMSG_DATA(nlh);

	if(NULL != data &&
		data[0] >= 0 && data[0] < IFACE_NUM) {

		int index = data[0];
		int score = data[1];

		set_weight_state_by_score(index,score);

		oplus_sla_info[index].wlan_score = score;

		if(score <= (rom_update_info.wlan_bad_score - 5)) {
			oplus_sla_info[index].wlan_score_bad_count += WLAN_SCORE_BAD_NUM;
		}
		else if(score <= rom_update_info.wlan_bad_score){
			oplus_sla_info[index].wlan_score_bad_count += 3;
		}
		else if(score >= rom_update_info.wlan_good_score){
			oplus_sla_info[index].wlan_score_bad_count = 0;
		}
		else if(score >= (rom_update_info.wlan_good_score - 5) &&
			oplus_sla_info[index].wlan_score_bad_count >= 3){
			oplus_sla_info[index].wlan_score_bad_count -= 3;
		}
		else if(score >= (rom_update_info.wlan_good_score - 10) &&
				oplus_sla_info[index].wlan_score_bad_count){
			oplus_sla_info[index].wlan_score_bad_count--;
		}

		if(oplus_sla_info[index].wlan_score_bad_count > (2*WLAN_SCORE_BAD_NUM)){
			oplus_sla_info[index].wlan_score_bad_count = 2*WLAN_SCORE_BAD_NUM;
		}
	} else {
	        printk("oplus_sla_netlink:oplus_sla_set_wifi_score invalid message!\n");
	}

	return 0;
}

static int oplus_sla_set_game_app_uid(struct nlmsghdr *nlh)
{
	u32 *uidInfo = (u32 *)NLMSG_DATA(nlh);
	u32 index = uidInfo[0];
	u32 uid = uidInfo[1];

	if (index < GAME_NUM) {
	    game_uid[index].uid = uid;
		game_uid[index].switch_time = 0;
		game_uid[index].game_type = index;
		game_uid[index].mark = MAIN_WLAN_MARK;
	    printk("oplus_sla_netlink oplus_sla_set_game_app_uid:index=%d uid=%d\n", index, uid);
	}

	return 0;
}

static int oplus_sla_set_white_list_app_uid(struct nlmsghdr *nlh)
{
    u32 *info = (u32 *)NLMSG_DATA(nlh);
	memset(&white_app_list,0x0,sizeof(struct oplus_white_app_info));
    white_app_list.count = info[0];
    if (white_app_list.count > 0 && white_app_list.count < WHITE_APP_NUM) {
        int i;
        for (i = 0; i < white_app_list.count; i++) {
            white_app_list.uid[i] = info[i + 1];
            printk("oplus_sla_netlink oplus_sla_set_white_list_app_uid count=%d, uid[%d]=%d\n",
                    white_app_list.count, i, white_app_list.uid[i]);
        }
    }
	return 0;
}

static int oplus_sla_set_game_rtt_detecting(struct nlmsghdr *nlh)
{
    if(SLA_ENABLE_GAME_RTT== nlh->nlmsg_type){
        oplus_sla_vpn_connected = 1;
    } else {
        oplus_sla_vpn_connected = 0;
    }
	printk("oplus_sla_netlink: set game rtt detect:%d\n",nlh->nlmsg_type);
	return 0;
}

static int oplus_sla_set_switch_state(struct nlmsghdr *nlh)
{
	u32 *data = (u32 *)NLMSG_DATA(nlh);

	if (data[0]) {
		sla_switch_enable = true;
	} else {
		sla_switch_enable = false;
	}

	if (data[1]) {
		dual_wifi_switch_enable = true;
		if (SLA_MODE_INIT == sla_work_mode) {
			sla_detect_mode = SLA_MODE_DUAL_WIFI;
		}
	}
	else {
		dual_wifi_switch_enable = false;
		if (SLA_MODE_INIT == sla_work_mode) {
			sla_detect_mode = SLA_MODE_WIFI_CELL;
		}
	}
	printk("oplus_sla_netlink:sla switch sla_enable = %d,dual_wifi_enable = %d\n",
			sla_switch_enable, dual_wifi_switch_enable);
	return 0;
}

static int oplus_sla_update_screen_state(struct nlmsghdr *nlh)
{
	u32 *screen_state = (u32 *)NLMSG_DATA(nlh);
	sla_screen_on =	(*screen_state)	? true : false;
	printk("oplus_sla_netlink:update screen state = %u\n",sla_screen_on);
	return	0;
}

static int oplus_sla_update_cell_quality(struct nlmsghdr *nlh)
{
	u32 *cell_quality = (u32 *)NLMSG_DATA(nlh);
	cell_quality_good =	(*cell_quality)	? true : false;
	printk("oplus_sla_netlink:update cell quality = %u\n", cell_quality_good);
	return	0;
}

static int oplus_sla_set_show_dialog_state(struct nlmsghdr *nlh)
{
	u32 *show_dailog = (u32 *)NLMSG_DATA(nlh);
	need_show_dailog = (*show_dailog) ? true : false;
	if (!need_show_dailog) {
		send_show_dailog_msg = 0;
	}
	printk("oplus_sla_netlink:set show dialog = %u\n", need_show_dailog);
	return	0;
}

static int oplus_sla_set_params(struct nlmsghdr *nlh)
{
	u32* params = (u32 *)NLMSG_DATA(nlh);
	u32 count = params[0];
	params++;
	if (count == 13) {
		rom_update_info.sla_speed = params[0];
		rom_update_info.cell_speed = params[1];
		rom_update_info.wlan_speed = params[2];
		rom_update_info.wlan_little_score_speed = params[3];
		rom_update_info.sla_rtt = params[4];
		rom_update_info.wzry_rtt = params[5];
		rom_update_info.cjzc_rtt = params[6];
		rom_update_info.wlan_bad_score = params[7];
		rom_update_info.wlan_good_score = params[8];
		rom_update_info.second_wlan_speed = params[9];
		rom_update_info.dual_wlan_download_speed = params[10];
		rom_update_info.dual_wifi_rtt = params[11];
		rom_update_info.dual_wlan_bad_score = params[12];
		printk("oplus_sla_netlink:set params count=%u params[0] = %u,params[1] = %u,"
				"params[2] = %u,params[3] = %u,params[4] = %u,params[5] = %u,params[6] = %u,"
				"params[7] = %u,params[8] = %u,params[9] = %u,params[10] = %u,params[11] = %u,params[12] = %u\n",
			    count, params[0],params[1],params[2],params[3],params[4],params[5],
			    params[6],params[7],params[8],params[9],params[10],params[11],params[12]);
	} else {
		printk("oplus_sla_netlink:set params invalid param count:%d", count);
	}

	return	0;
}

static int oplus_sla_set_game_start_state(struct nlmsghdr *nlh)
{
	int *data = (int *)NLMSG_DATA(nlh);
	int index = data[0];

	game_cell_to_wifi = false;

	if(index && index < GAME_NUM){
		game_start_state[index] = data[1];
		printk("oplus_sla_netlink:set game_start_state[%d] = %d\n",index,game_start_state[index]);
		/* reset udp rx data */
		memset(wzry_traffic_info.udp_rx_packet, 0x0, sizeof(u32)*UDP_RX_WIN_SIZE);
                wzry_traffic_info.window_index = 0;
                memset(cjzc_traffic_info.udp_rx_packet, 0x0, sizeof(u32)*UDP_RX_WIN_SIZE);
                cjzc_traffic_info.window_index = 0;
	} else {
		printk("oplus_sla_netlink: set game_start_state error,index = %d\n",index);
	}

	return	0;
}

static int oplus_sla_set_game_rtt_params(struct nlmsghdr *nlh)
{
	int *data = (int *)NLMSG_DATA(nlh);
	int index = data[0];
	int i;

	if(index > 0 && index < GAME_NUM) {
		memcpy(&game_params[index], data, sizeof(struct oplus_sla_game_rtt_params));
		printk("oplus_sla_netlink:set game params index=%d tx_offset=%d tx_len=%d tx_fix=",
		        game_params[index].game_index, game_params[index].tx_offset, game_params[index].tx_len);
		for (i = 0; i < game_params[index].tx_len; i++) {
			printk("%02x", game_params[index].tx_fixed_value[i]);
		}
		printk(" rx_offset=%d rx_len=%d rx_fix=", game_params[index].rx_offset, game_params[index].rx_len);
		for (i = 0; i < game_params[index].rx_len; i++) {
			printk("%02x", game_params[index].rx_fixed_value[i]);
		}
		printk("\n");
	} else {
		printk("oplus_sla_netlink: set game params error,index = %d\n", index);
	}

	return	0;
}

static int oplus_sla_set_game_in_front(struct nlmsghdr *nlh)
{
	int *data = (int *)NLMSG_DATA(nlh);
	int index = data[0];
	int in_front = data[1];

	if(index > 0 && index < GAME_NUM) {
		if (index == GAME_WZRY) {
			if (in_front) {
				wzry_traffic_info.game_in_front = 1;
				cjzc_traffic_info.game_in_front = 0;
			} else {
				wzry_traffic_info.game_in_front = 0;
			}
			printk("oplus_sla_netlink: set_game_in_front game index = %d, in_front=%d\n", index, in_front);
		} else if (index == GAME_CJZC) {
			if (in_front) {
				wzry_traffic_info.game_in_front = 0;
				cjzc_traffic_info.game_in_front = 1;
			} else {
				cjzc_traffic_info.game_in_front = 0;
			}
			printk("oplus_sla_netlink: set_game_in_front game index = %d, in_front=%d\n", index, in_front);
		} else {
			printk("oplus_sla_netlink: set_game_in_front unknow game, index = %d\n", index);
		}
	} else {
		printk("oplus_sla_netlink: set_game_in_front error, index = %d\n", index);
	}

	return	0;
}

static int oplus_sla_set_dual_wifi_app_uid(struct nlmsghdr *nlh)
{
    u32 *info = (u32 *)NLMSG_DATA(nlh);
	memset(&dual_wifi_app_list,0x0,sizeof(struct oplus_dual_sta_info));
    dual_wifi_app_list.count = info[0];
    if (dual_wifi_app_list.count > 0 && dual_wifi_app_list.count < DUAL_STA_APP_NUM) {
        int i;
        for (i = 0; i < dual_wifi_app_list.count; i++) {
            dual_wifi_app_list.uid[i] = info[i + 1];
            printk("oplus_sla_netlink:set_dual_wifi_app_uid count=%d, uid[%d]=%d\n",
                    dual_wifi_app_list.count, i, dual_wifi_app_list.uid[i]);
        }
    }
	return 0;
}

static int oplus_sla_set_download_app_uid(struct nlmsghdr *nlh)
{
    u32 *info = (u32 *)NLMSG_DATA(nlh);
    memset(&download_app_list,0x0,sizeof(struct oplus_dual_sta_info));
    download_app_list.count = info[0];
    if (download_app_list.count > 0 && download_app_list.count < DUAL_STA_APP_NUM) {
        int i;
        for (i = 0; i < download_app_list.count; i++) {
            download_app_list.uid[i] = info[i + 1];
            printk("oplus_sla_netlink:set_download_app_uid count=%d, uid[%d]=%d\n",
                    download_app_list.count, i, download_app_list.uid[i]);
        }
    }
	return 0;
}

static int oplus_sla_set_vedio_app_uid(struct nlmsghdr *nlh)
{
    u32 *info = (u32 *)NLMSG_DATA(nlh);
    memset(&vedio_app_list,0x0,sizeof(struct oplus_dual_sta_info));
    vedio_app_list.count = info[0];
    if (vedio_app_list.count > 0 && vedio_app_list.count < DUAL_STA_APP_NUM) {
        int i;
        for (i = 0; i < vedio_app_list.count; i++) {
            vedio_app_list.uid[i] = info[i + 1];
            printk("oplus_sla_netlink:vedio_app_list count=%d, uid[%d]=%d\n",
                    vedio_app_list.count, i, vedio_app_list.uid[i]);
        }
    }
	return 0;
}

static void oplus_sla_set_vpn_state(struct nlmsghdr *nlh)
{
	int *data = (int *)NLMSG_DATA(nlh);

	/*1 ---> vpn connected
	 *0 ---> vpn disconnected*/
	oplus_sla_vpn_connected = data[0];

	printk("oplus_sla_netlink:set vpn connecet state = %d\n", data[0]);
}

static void reset_main_wlan_info(void)
{
	struct timeval tv;

	if(oplus_sla_info[MAIN_WLAN].if_up){

		do_gettimeofday(&tv);
		last_minute_speed_tv = tv;

		oplus_sla_info[MAIN_WLAN].sum_rtt= 0;
		oplus_sla_info[MAIN_WLAN].avg_rtt = 0;
		oplus_sla_info[MAIN_WLAN].rtt_index = 0;
		oplus_sla_info[MAIN_WLAN].sla_avg_rtt = 0;
		oplus_sla_info[MAIN_WLAN].sla_rtt_num = 0;
		oplus_sla_info[MAIN_WLAN].sla_sum_rtt = 0;
		oplus_sla_info[MAIN_WLAN].syn_retran = 0;
		oplus_sla_info[MAIN_WLAN].max_speed /= 2;
		oplus_sla_info[MAIN_WLAN].left_speed = 0;
		oplus_sla_info[MAIN_WLAN].current_speed = 0;
		oplus_sla_info[MAIN_WLAN].minute_speed = MINUTE_LITTE_RATE;
		oplus_sla_info[MAIN_WLAN].congestion_flag = CONGESTION_LEVEL_NORMAL;
	}
}

static void oplus_sla_init_weight_by_wlan_assi(void)
{
	struct timeval tv;
	do_gettimeofday(&tv);

	sla_write_lock();
	init_weight_delay_count = 10;

	last_weight_tv = tv;
	oplus_sla_info[MAIN_WLAN].weight = 30;
	oplus_sla_info[CELL_INDEX].weight = 100;

	reset_main_wlan_info();

	printk("oplus_sla_weight:init_weight_by_wlan_assi[%d] [%d]\n",
		oplus_sla_info[MAIN_WLAN].weight, oplus_sla_info[CELL_INDEX].weight);
	sla_write_unlock();
}

static int sla_netlink_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh, struct netlink_ext_ack *extack)
{
	int ret = 0;

	switch (nlh->nlmsg_type) {
	case SLA_ENABLE:
		ret = enable_oplus_sla_module(nlh);
		break;
	case SLA_DISABLE:
		ret = disable_oplus_sla_module(nlh);
		break;
	case SLA_IFACE_CHANGED:
		ret = oplus_sla_iface_changed(nlh);
		break;
	case SLA_NOTIFY_PRIMARY_WIFI:
		ret = oplus_sla_set_primary_wifi(nlh);
		break;
	case SLA_NOTIFY_PID:
		ret = oplus_sla_get_pid(skb,nlh);
		break;
	case SLA_NOTIFY_WIFI_SCORE:
		ret = oplus_sla_set_wifi_score(nlh);
		break;
	case SLA_NOTIFY_APP_UID:
	    ret = oplus_sla_set_game_app_uid(nlh);
	    break;
	case SLA_NOTIFY_WHITE_LIST_APP:
		oplus_sla_send_white_list_app_traffic();
	    ret = oplus_sla_set_white_list_app_uid(nlh);
	    break;
	case SLA_ENABLE_GAME_RTT:
	case SLA_DISABLE_GAME_RTT:
	    ret = oplus_sla_set_game_rtt_detecting(nlh);
	    break;
	case SLA_NOTIFY_SWITCH_STATE:
		ret = oplus_sla_set_switch_state(nlh);
		break;
	case SLA_NOTIFY_SCREEN_STATE:
		ret = oplus_sla_update_screen_state(nlh);
		break;
	case SLA_NOTIFY_CELL_QUALITY:
		ret = oplus_sla_update_cell_quality(nlh);
		break;
	case SLA_NOTIFY_SHOW_DIALOG:
		ret = oplus_sla_set_show_dialog_state(nlh);
		break;
	case SLA_GET_SYN_RETRAN_INFO:
		oplus_sla_send_syn_retran_info();
		break;
	case SLA_GET_SPEED_UP_APP:
		oplus_sla_send_white_list_app_traffic();
		break;
	case SLA_SET_DEBUG:
		oplus_sla_set_debug(nlh);
		break;
	case SLA_NOTIFY_DEFAULT_NETWORK:
		oplus_sla_set_default_network(nlh);
		break;
	case SLA_NOTIFY_PARAMS:
		oplus_sla_set_params(nlh);
		break;
	case SLA_NOTIFY_GAME_STATE:
		oplus_sla_set_game_start_state(nlh);
		break;
	case SLA_NOTIFY_GAME_PARAMS:
		oplus_sla_set_game_rtt_params(nlh);
		break;
	case SLA_NOTIFY_GAME_IN_FRONT:
		oplus_sla_set_game_in_front(nlh);
		break;
	case SLA_NOTIFY_DUAL_STA_APP:
		oplus_sla_set_dual_wifi_app_uid(nlh);
		break;
	case SLA_WEIGHT_BY_WLAN_ASSIST:
		oplus_sla_init_weight_by_wlan_assi();
		break;
	case SLA_NOTIFY_VPN_CONNECTED:
		oplus_sla_set_vpn_state(nlh);
		break;
	case SLA_NOTIFY_DOWNLOAD_APP:
		oplus_sla_set_download_app_uid(nlh);
		break;
	case SLA_NOTIFY_VEDIO_APP:
		oplus_sla_set_vedio_app_uid(nlh);
		break;
	case SLA_LIMIT_SPEED_ENABLE:
		enable_oplus_limit_speed();
		break;
	case SLA_LIMIT_SPEED_DISABLE:
		disable_oplus_limit_speed();
		break;
	case SLA_LIMIT_SPEED_FRONT_UID:
		oplus_limit_uid_changed(nlh);
		break;
	case SMART_BW_SET_PARAMS:
		oplus_smart_bw_set_params(nlh);
		break;
	#ifdef OPLUS_FEATURE_WIFI_ROUTERBOOST
	case SLA_NOTIFY_ROUTER_BOOST_DUPPKT_PARAMS:
		oplus_router_boost_set_params(nlh);
		break;
	#endif /* OPLUS_FEATURE_WIFI_ROUTERBOOST */
	default:
		return -EINVAL;
	}

	return ret;
}


static void sla_netlink_rcv(struct sk_buff *skb)
{
	mutex_lock(&sla_netlink_mutex);
	netlink_rcv_skb(skb, &sla_netlink_rcv_msg);
	mutex_unlock(&sla_netlink_mutex);
}

static int oplus_sla_netlink_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input	= sla_netlink_rcv,
	};

	oplus_sla_sock = netlink_kernel_create(&init_net, NETLINK_OPLUS_SLA, &cfg);
	return oplus_sla_sock == NULL ? -ENOMEM : 0;
}

static void oplus_sla_netlink_exit(void)
{
	netlink_kernel_release(oplus_sla_sock);
	oplus_sla_sock = NULL;
}

static int __init oplus_sla_init(void)
{
	int ret = 0;

	rwlock_init(&sla_lock);
	rwlock_init(&sla_rtt_lock);
	rwlock_init(&sla_game_lock);

	ret = oplus_sla_netlink_init();
	if (ret < 0) {
		printk("oplus_sla module can not init oplus sla netlink.\n");
	}

	ret |= oplus_sla_sysctl_init();

	ret |= nf_register_net_hooks(&init_net,oplus_sla_ops,ARRAY_SIZE(oplus_sla_ops));
	if (ret < 0) {
		printk("oplus_sla module can not register netfilter ops.\n");
	}

	workqueue_sla= create_singlethread_workqueue("workqueue_sla");
	if (workqueue_sla) {
		INIT_WORK(&oplus_sla_work, oplus_sla_work_queue_func);
	}
	else {
		printk("oplus_sla module can not create workqueue_sla\n");
	}

	oplus_sla_timer_init();
	statistic_dev_rtt = oplus_statistic_dev_rtt;
	mark_streams_for_iptables_reject = sla_mark_streams_for_iptables_reject;

	return ret;
}


static void __exit oplus_sla_fini(void)
{
	oplus_sla_timer_fini();
	statistic_dev_rtt = NULL;
	mark_streams_for_iptables_reject = NULL;

	if (workqueue_sla) {
		flush_workqueue(workqueue_sla);
		destroy_workqueue(workqueue_sla);
	}

	oplus_sla_netlink_exit();

	if(oplus_sla_table_hrd){
		unregister_net_sysctl_table(oplus_sla_table_hrd);
	}

	nf_unregister_net_hooks(&init_net,oplus_sla_ops, ARRAY_SIZE(oplus_sla_ops));
}


module_init(oplus_sla_init);
module_exit(oplus_sla_fini);
