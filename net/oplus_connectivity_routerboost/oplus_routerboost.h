
/************************************************************************************
** File: - oplus_routerboost_game_monitor.h
** Copyright (C), 2008-2020, OPLUS Mobile Comm Corp., Ltd
**
** Description:
**     1. Add for RouterBoost GKI
**
** Version: 1.0
** Date :   2021-04-19
** TAG:     OPLUS_FEATURE_WIFI_ROUTERBOOST
** OPLUS Java File Skip Rule:readability/double backslash
** ---------------------Revision History: ---------------------
** <author>                           <data>      <version>    <desc>
** ---------------------------------------------------------------
************************************************************************************/
#ifndef OPLUS_ROUTER_BOOST_H_
#define OPLUS_ROUTER_BOOST_H_

enum ROUTERBOOST_MSG {
	ROUTERBOOST_MSG_UNSPEC                      = 0,
	ROUTERBOOST_MSG_SET_ANDROID_PID             = 1,
	ROUTERBOOST_MSG_SET_KERNEL_DEBUG            = 2,
	ROUTERBOOST_MSG_SET_GAME_UID                = 3,
	ROUTERBOOST_MSG_REPOET_GAME_STREEM_INFO     = 4,
	ROUTERBOOST_MSG_SET_WLAN_INDEX              = 5,
	ROUTERBOOST_MSG_SET_GAME_PLAYING_STATE      = 6,
	ROUTERBOOST_MSG_REPOET_GAME_PKT_INFO        = 7,
	ROUTERBOOST_MSG_REPOET_GAME_PKT_INFO_FIN    = 8,
	__ROUTERBOOST_MSG_MAX
};

enum ROUTERBOOST_CMD {
	ROUTERBOOST_CMD_UNSPEC    = 0,
	ROUTERBOOST_CMD_DOWNLINK  = 1,
	ROUTERBOOST_CMD_UPLINK    = 2,
	__ROUTERBOOST_CMD_MAX
};

#define ROUTERBOOSt_MSG_MAX (__ROUTERBOOST_MSG_MAX - 1)
#define ROUTERBOOST_CMD_MAX (__ROUTERBOOST_CMD_MAX - 1)
#define ROUTERBOOST_FAMILY_VERSION	1
#define ROUTERBOOST_FAMILY "routerboost"

#define NLA_DATA(na) ((char *)((char*)(na) + NLA_HDRLEN))
#define LOG_TAG "[oplus_routerboost] %s line:%d "
#define debug(fmt, args...) printk(LOG_TAG fmt, __FUNCTION__, __LINE__, ##args)

#define WLAN0 "wlan0"
#define WLAN1 "wlan1"

/*defined in oplus_routerboost.c*/
extern int routerboost_debug;
extern int routerboost_genl_msg_send_to_user(int msg_type, char *payload, int payload_len);

/*defined in oplus_routerboost_game_monitor.c*/
extern void set_game_monitor_uid(u32 uid);
extern void set_game_monitor_wlan_index(u32 wlan_index);
extern void set_game_playing_state(u32 is_game_playing);

extern int routerboost_game_monitor_init(void);
extern void routerboost_game_monitor_exit(void);
#endif /*OPLUS_ROUTER_BOOST_H_*/
