/* SPDX-License-Identifier: GPL-2.0-only */
/*
* Copyright (C) 2018-2020 Oplus. All rights reserved.
*/

#include <net/oplus/oplus_apps_power_monitor.h>
#include <linux/tcp.h>
#include <net/tcp.h>
#include <linux/sysctl.h>

#define TOP_APP_NUM     5
#define APP_MONITOR_HASHTABLE_SIZE 64
#define APP_MONITOR_HASH_MASK   (APP_MONITOR_HASHTABLE_SIZE - 1)
#define INVALID_APP_INDEX 0xff
#define ALARM_TYPE_BEAT 0x01
#define ALARM_TYPE_PUSH 0x02
#define ALARM_TYPE_TRAFFIC 0x04
#define PERIOD_CHECK_TIME   10   //unit second
#define ALARM_DETECT_PERIOD 600  //unit second
#define ALARM_DETECT_COUNT 10
#define REPORT_INTERVAL 1800    //unit second
#define REPORT_PERIOD 5

enum
{
    APPS_POWER_MONITOR_MSG_DL_CTRL = 0x30,
    APPS_POWER_MONITOR_MSG_DL_RPT_CTRL,
    APPS_POWER_MONITOR_MSG_UL_INFO,
    APPS_POWER_MONITOR_MSG_UL_BEAT_ALARM,
    APPS_POWER_MONITOR_MSG_UL_PUSH_ALARM,
    APPS_POWER_MONITOR_MSG_UL_TRAFFIC_ALARM,
};

#define IPV4ADDRTOSTR(addr) \
((unsigned char *)&addr)[0], \
((unsigned char *)&addr)[1], \
((unsigned char *)&addr)[2], \
((unsigned char *)&addr)[3]

#pragma pack(4)
struct app_monitor_beat_msg_st
{
    u32 uid_val;
    u32 beat_period;
    u32 beat_count;
};

struct app_monitor_push_msg_st
{
    u32 uid_val;
    u32 push_period;
    u32 push_count;
};

struct app_monitor_traffic_msg_st
{
    u32 uid_val;
    u64 total_packets;
    u64 total_bytes;
    u64 send_packets;
    u64 send_bytes;
    u64 recv_packets;
    u64 recv_bytes;
};

struct app_monitor_retrans_msg_st
{
    u32 uid_val;
    u64 app_retrans_total_packets;
    u64 app_retrans_syn_packets;
};

struct app_monitor_dl_ctl_msg_st
{
    u32 msg_len;
    u32 start_flag;
};

struct app_monitor_dl_req_report_msg_st
{
    u32 msg_len;
    u32 request_report_flag;
};

struct app_monitor_ul_report_msg_st
{
    u32 msg_len;
    u32 beat_app_count;
    u32 push_app_count;
    u32 traffic_app_count;
    u32 retrans_app_count;    //for retrams static
    u64 sys_total_packets;
    u64 sys_total_bytes;
    u64 sys_send_packets;
    u64 sys_send_bytes;
    u64 sys_recv_packets;
    u64 sys_recv_bytes;
    u64 sys_retrans_total_packets;
    u64 sys_retrans_syn_packets;
    struct app_monitor_beat_msg_st app_beat_info[TOP_APP_NUM];
    struct app_monitor_push_msg_st app_push_info[TOP_APP_NUM];
    struct app_monitor_traffic_msg_st app_traffic_info[TOP_APP_NUM];
    struct app_monitor_retrans_msg_st app_retrans_info[TOP_APP_NUM]; //for retrans static
};

struct app_monitor_ul_beat_alarm_msg_st
{
    u32 msg_len;
    u32 uid_val;
    u32 beat_period;     //unit second
};

struct app_monitor_ul_push_alarm_msg_st
{
    u32 msg_len;
    u32 uid_val;
    u32 push_period;     //unit second
};

struct app_monitor_ul_traffic_alarm_msg_st
{
    u32 msg_len;
    u32 uid_val;
    u64 total_packets;
    u64 total_bytes;
    u64 send_packets;
    u64 send_bytes;
    u64 recv_packets;
    u64 recv_bytes;
    u32 traffic_period;   //default  10 * 60 seconds;
};
#pragma pack()

struct app_monitor_app_info_st
{
    u32 uid_val;
    u64 total_packets;
    u64 total_bytes;
    u64 send_packets;
    u64 send_bytes;
    u64 recv_packets;
    u64 recv_bytes;
    u64 retrans_total_packets;
    u64 retrans_syn_packets;
    u32 last_send_time;
    u32 beat_period;
    u32 beat_count;
    u32 last_active_time;
    u32 push_period;
    u32 push_count;
    u32 beat_alarm_start_time;
    u32 push_alarm_start_time;
};

struct app_monitor_top_app_beat_info_st
{
    u32 real_app_num;
    u32 max_index;
    u32 read_flag[TOP_APP_NUM];
    struct app_monitor_app_info_st  app_beat_info[TOP_APP_NUM];
};

struct app_monitor_top_app_push_info_st
{
    u32 real_app_num;
    u32 max_index;
    u32 read_flag[TOP_APP_NUM];
    struct app_monitor_app_info_st  app_push_info[TOP_APP_NUM];
};

struct app_monitor_top_app_trafffic_info_st
{
    u32 real_app_num;
    u32 min_index;
    u32 read_flag[TOP_APP_NUM];
    struct app_monitor_app_info_st  app_traffic_info[TOP_APP_NUM];
};

struct app_monitor_top_app_retrans_info_st
{
    u32 real_app_num;
    u32 min_index;
    u32 read_flag[TOP_APP_NUM];
    struct app_monitor_app_info_st app_retrans_info[TOP_APP_NUM];
};

struct app_monitor_app_info_list_node_st
{
    struct app_monitor_app_info_st app_info;
    struct list_head   list_node;
};

struct app_monitor_list_st
{
    u32 m_count;
    struct list_head m_listhead;
};

struct sys_stati_info_st
{
    u64 sys_total_packets;
    u64 sys_total_bytes;
    u64 sys_send_packets;
    u64 sys_send_bytes;
    u64 sys_recv_packets;
    u64 sys_recv_bytes;
    u64 sys_retrans_total_packets;
    u64 sys_retrans_syn_packets;
};

struct app_monitor_top_app_trafffic_info_st top_app_traffic_info;
struct app_monitor_top_app_beat_info_st top_app_beat_info;
struct app_monitor_top_app_push_info_st top_app_push_info;
struct app_monitor_top_app_retrans_info_st top_app_retrans_info;
struct sys_stati_info_st sys_stati_info;
struct app_monitor_list_st app_monitor_list[APP_MONITOR_HASHTABLE_SIZE];
static rwlock_t app_power_monitor_lock;
static int app_monitor_enable = 0;
static int app_monitor_start = 0;
static int app_report_request = 0;
static int app_beat_alarm_period = 0;
static int app_push_alarm_period = 0;
static struct timer_list apps_monitor_report_timer;
static struct ctl_table_header *app_power_monitor_table_hrd;
static struct timeval last_report_tv;

//added for test
static void print_report_info(struct app_monitor_ul_report_msg_st *report_msg){
    int i = 0;

    printk("[app_monitor]: report_info:beat_app_count=%u, push_app_count=%u, traffic_app_count=%u, retrans_app_count=%u\n",
    report_msg->beat_app_count, report_msg->push_app_count, report_msg->traffic_app_count, report_msg->retrans_app_count);
    printk("[app_monitor]:sys_total_packet=%llu,sys_send_packet=%llu,sys_recv_packet=%llu,sys_retrans_total_packet=%llu, sys_retrans_syn_packet=%llu\n",
              report_msg->sys_total_packets, report_msg->sys_send_packets, report_msg->sys_recv_packets,
              report_msg->sys_retrans_total_packets, report_msg->sys_retrans_syn_packets);
    printk("[app_monitor]:sys_total_byte=%llu,sys_send_byte=%llu,sys_recv_byte=%llu\n",
              report_msg->sys_total_bytes, report_msg->sys_send_bytes, report_msg->sys_recv_bytes);
    for(i = 0; i < report_msg->beat_app_count; i++){
        printk("[app_monitor]:report_info:beat:i=%d,uid=%u,period=%u,count=%u\n",
               i, report_msg->app_beat_info[i].uid_val,
               report_msg->app_beat_info[i].beat_period,
               report_msg->app_beat_info[i].beat_count);
    }

    for(i = 0; i < report_msg->push_app_count; i++){
    printk("[app_monitor]:report_info:push:i=%d,uid=%u,period=%u,count=%u\n",
           i, report_msg->app_push_info[i].uid_val,
           report_msg->app_push_info[i].push_period,
           report_msg->app_push_info[i].push_count);
    }

    for(i = 0; i < report_msg->traffic_app_count; i++){
        printk("[app_monitor]:report_info:traffic:i=%d,uid=%u,t_bytes=%llu,t_packet=%llu,s_byte=%llu,s_packet=%llu,r_byte=%llu,r_packet=%llu\n",
               i, report_msg->app_traffic_info[i].uid_val,
               report_msg->app_traffic_info[i].total_bytes,
               report_msg->app_traffic_info[i].total_packets,
               report_msg->app_traffic_info[i].send_bytes,
               report_msg->app_traffic_info[i].send_packets,
               report_msg->app_traffic_info[i].recv_bytes,
               report_msg->app_traffic_info[i].recv_packets);
    }

    for(i = 0; i < report_msg->retrans_app_count; i++){
        printk("[app_monitor]:report_info:retrans:i=%d,uid=%u,total_retrans=%llu, syn_retrans=%llu\n",
               i, report_msg->app_retrans_info[i].uid_val,
               report_msg->app_retrans_info[i].app_retrans_total_packets,
               report_msg->app_retrans_info[i].app_retrans_syn_packets);
    }
}

void oplus_app_monitor_fill_beat_app_info(struct app_monitor_ul_report_msg_st *report_msg)
{
    int  i, j, k;
    int app_num = top_app_beat_info.real_app_num;
    int max_count = 0;

    for(i = 0; i < TOP_APP_NUM; i++){
        top_app_beat_info.read_flag[i] = 0;
    }

    for(i = 0; i < app_num; i++){
        max_count = 0;
        k = TOP_APP_NUM;
        for(j = 0; j < TOP_APP_NUM;  j++){
            if(top_app_beat_info.read_flag[j] || top_app_beat_info.app_beat_info[j].beat_period == 0)
            continue;

            if(!max_count || max_count < top_app_beat_info.app_beat_info[j].beat_count){
                max_count = top_app_beat_info.app_beat_info[j].beat_count;
                k = j;
            }
        }

        if(k < TOP_APP_NUM){
            report_msg->app_beat_info[i].uid_val = top_app_beat_info.app_beat_info[k].uid_val;
            report_msg->app_beat_info[i].beat_period = top_app_beat_info.app_beat_info[k].beat_period;
            report_msg->app_beat_info[i].beat_count= top_app_beat_info.app_beat_info[k].beat_count;
            top_app_beat_info.read_flag[k] = 1;
        }
    }
}

void oplus_app_monitor_fill_push_app_info(struct app_monitor_ul_report_msg_st *report_msg)
{
    int  i, j, k;
    int app_num = top_app_push_info.real_app_num;
    int max_count = 0;

    for(i = 0; i < TOP_APP_NUM; i++){
        top_app_push_info.read_flag[i] = 0;
    }

    for(i = 0; i < app_num; i++){
        max_count = 0;
        k = TOP_APP_NUM;
        for(j = 0; j < TOP_APP_NUM;  j++){
            if(top_app_push_info.read_flag[j] || top_app_push_info.app_push_info[j].push_period == 0)
                continue;

            if(!max_count || max_count < top_app_push_info.app_push_info[j].push_count){
                max_count = top_app_push_info.app_push_info[j].push_count;
                k = j;
            }
        }

        if(k < TOP_APP_NUM){
            report_msg->app_push_info[i].uid_val = top_app_push_info.app_push_info[k].uid_val;
            report_msg->app_push_info[i].push_period = top_app_push_info.app_push_info[k].push_period;
            report_msg->app_push_info[i].push_count= top_app_push_info.app_push_info[k].push_count;
            top_app_push_info.read_flag[k] = 1;
        }
    }
}

void oplus_app_monitor_fill_traffic_app_info(struct app_monitor_ul_report_msg_st *report_msg)
{
    int  i, j, k;
    int app_num = top_app_traffic_info.real_app_num;
    int max_traffic = 0;

    for(i = 0; i < TOP_APP_NUM; i++){
        top_app_traffic_info.read_flag[i] = 0;
    }

    for(i = 0; i < app_num; i++){
        max_traffic = 0;
        k = TOP_APP_NUM;
        for(j = 0; j < TOP_APP_NUM;  j++){
            if(top_app_traffic_info.read_flag[j] || top_app_traffic_info.app_traffic_info[j].total_bytes == 0)
                continue;

            if(!max_traffic || max_traffic < top_app_traffic_info.app_traffic_info[j].total_bytes){
                max_traffic = top_app_traffic_info.app_traffic_info[j].total_bytes;
                k = j;
            }
        }

        if(k < TOP_APP_NUM){
           report_msg->app_traffic_info[i].uid_val = top_app_traffic_info.app_traffic_info[k].uid_val;
           report_msg->app_traffic_info[i].total_bytes = top_app_traffic_info.app_traffic_info[k].total_bytes;
           report_msg->app_traffic_info[i].total_packets= top_app_traffic_info.app_traffic_info[k].total_packets;
           report_msg->app_traffic_info[i].send_bytes = top_app_traffic_info.app_traffic_info[k].send_bytes;
           report_msg->app_traffic_info[i].send_packets= top_app_traffic_info.app_traffic_info[k].send_packets;
           report_msg->app_traffic_info[i].recv_bytes = top_app_traffic_info.app_traffic_info[k].recv_bytes;
           report_msg->app_traffic_info[i].recv_packets= top_app_traffic_info.app_traffic_info[k].recv_packets;
           top_app_traffic_info.read_flag[k] = 1;
        }
    }
}

void oplus_app_monitor_fill_retrans_app_info(struct app_monitor_ul_report_msg_st *report_msg)
{
    int  i, j, k;
    int app_num = top_app_retrans_info.real_app_num;
    int max_retrans = 0;

    for(i = 0; i < TOP_APP_NUM; i++){
        top_app_retrans_info.read_flag[i] = 0;
    }

    for(i = 0; i < app_num; i++){
        max_retrans = 0;
        k = TOP_APP_NUM;
        for(j = 0; j < TOP_APP_NUM;  j++){
            if(top_app_retrans_info.read_flag[j] || top_app_retrans_info.app_retrans_info[j].retrans_total_packets == 0)
                continue;

            if(!max_retrans || max_retrans < top_app_retrans_info.app_retrans_info[j].retrans_total_packets){
                max_retrans = top_app_retrans_info.app_retrans_info[j].retrans_total_packets;
                k = j;
            }
        }

        if(k < TOP_APP_NUM){
           report_msg->app_retrans_info[i].uid_val = top_app_retrans_info.app_retrans_info[k].uid_val;
           report_msg->app_retrans_info[i].app_retrans_total_packets = top_app_retrans_info.app_retrans_info[k].retrans_total_packets;
           report_msg->app_retrans_info[i].app_retrans_syn_packets= top_app_retrans_info.app_retrans_info[k].retrans_syn_packets;
           top_app_retrans_info.read_flag[k] = 1;
        }
    }
}


extern int apps_monitor_netlink_send_to_user(int msg_type, char *payload, int payload_len);
void oplus_app_monitor_send_app_info_report(void)
{
    int ret;
    char msg_buf[1024] = {0};
    struct app_monitor_ul_report_msg_st *report_msg  = (struct app_monitor_ul_report_msg_st*)msg_buf;
    struct timeval now_tv;
    unsigned long flags;

    do_gettimeofday(&now_tv);
    if(!app_monitor_start || before((u32)now_tv.tv_sec , (u32)(REPORT_INTERVAL + last_report_tv.tv_sec))){
        /*printk("[app_monitor]:oplus_app_monitor_send_app_info_report,now_tv=%lld,last_tv=%lld\n",
                 (long long)now_tv.tv_sec,  (long long)last_report_tv.tv_sec);*/
        return;
    }

    write_lock_irqsave(&app_power_monitor_lock, flags);
    report_msg->msg_len = sizeof(struct app_monitor_ul_report_msg_st);
    report_msg->sys_total_packets = sys_stati_info.sys_total_packets;
    report_msg->sys_total_bytes = sys_stati_info.sys_total_bytes;
    report_msg->sys_send_packets = sys_stati_info.sys_send_packets;
    report_msg->sys_send_bytes = sys_stati_info.sys_send_bytes;
    report_msg->sys_recv_packets = sys_stati_info.sys_recv_packets;
    report_msg->sys_recv_bytes = sys_stati_info.sys_recv_bytes;
    report_msg->sys_retrans_total_packets = sys_stati_info.sys_retrans_total_packets;
    report_msg->sys_retrans_syn_packets = sys_stati_info.sys_retrans_syn_packets;
    report_msg->beat_app_count = top_app_beat_info.real_app_num;
    report_msg->push_app_count = top_app_push_info.real_app_num;
    report_msg->traffic_app_count = top_app_traffic_info.real_app_num;
    report_msg->retrans_app_count = top_app_retrans_info.real_app_num;
    oplus_app_monitor_fill_push_app_info(report_msg);
    oplus_app_monitor_fill_beat_app_info(report_msg);
    oplus_app_monitor_fill_traffic_app_info(report_msg);
    oplus_app_monitor_fill_retrans_app_info(report_msg);
    write_unlock_irqrestore(&app_power_monitor_lock, flags);

    ret = apps_monitor_netlink_send_to_user(APPS_POWER_MONITOR_MSG_UL_INFO, (char *) msg_buf, report_msg->msg_len );
    if(ret){
        printk("[app_monitor]:send app info report error!\n");
    }
    else{
        printk("[app_monitor]:send app info report success!now=%lld, now_jiffies=%u\n", (long long)now_tv.tv_sec, tcp_time_stamp);
        print_report_info(report_msg);
    }

    do_gettimeofday(&last_report_tv);
    return;
}

static void apps_monitor_report_timer_function(void){
    oplus_app_monitor_send_app_info_report();
    mod_timer(&apps_monitor_report_timer, jiffies + REPORT_PERIOD * HZ);
}

static void report_timer_init(void)
{
    printk("[app_monitor]:report_timer_init\n");
    timer_setup(&apps_monitor_report_timer, (void*)apps_monitor_report_timer_function, 0);
}
static void report_timer_start(void){
    printk("[app_monitor]:report_timer_start\n");
    apps_monitor_report_timer.function = (void*)apps_monitor_report_timer_function;
    apps_monitor_report_timer.expires = jiffies + REPORT_PERIOD * HZ;
    //add_timer(&apps_monitor_report_timer);
    mod_timer(&apps_monitor_report_timer, apps_monitor_report_timer.expires);
}

static void report_timer_restart(void)
{
    apps_monitor_report_timer.function = (void*)apps_monitor_report_timer_function;
    mod_timer(&apps_monitor_report_timer, jiffies + REPORT_PERIOD * HZ);
}

static void report_timer_del(void)
{
    printk("[app_monitor]:report_timer_del\n");
    del_timer_sync(&apps_monitor_report_timer);
}

static void oplus_app_monitor_start(void)
{
    int i;
    struct list_head *head;
    struct list_head *this_node;
    struct list_head *next_node;
    struct app_monitor_app_info_list_node_st *app_node_info;
    unsigned long flags;

    read_lock(&app_power_monitor_lock);
    if(app_monitor_enable == 1){
       read_unlock(&app_power_monitor_lock);
       return;
    }
    else{
        read_unlock(&app_power_monitor_lock);
        write_lock_irqsave(&app_power_monitor_lock, flags);
        for(i = 0; i < APP_MONITOR_HASHTABLE_SIZE; i++){
            head = &(app_monitor_list[i].m_listhead);
            this_node = head->next;
            while(this_node != head){
                next_node = this_node->next;
                list_del(this_node);
                app_node_info = list_entry(this_node, struct app_monitor_app_info_list_node_st, list_node);
                kfree(app_node_info);
                this_node = next_node;
            }

            app_monitor_list[i].m_count = 0;
        }

        app_monitor_enable = 1;
        do_gettimeofday(&last_report_tv);
        app_monitor_start = 1;
        write_unlock_irqrestore(&app_power_monitor_lock, flags);
    }

    printk("[app_monitor]:monitor start\n");
    report_timer_start();
}

static void oplus_app_monitor_stop(void)
{
    unsigned long flags;
    int i;
    struct list_head *head;
    struct list_head *this_node;
    struct list_head *next_node;
    struct app_monitor_app_info_list_node_st *app_node_info;

    write_lock_irqsave(&app_power_monitor_lock, flags);
    app_monitor_enable = 0;
    app_monitor_start = 0;
    for(i = 0; i < APP_MONITOR_HASHTABLE_SIZE; i++){
        head = &(app_monitor_list[i].m_listhead);
        this_node = head->next;
        while(this_node != head){
            next_node = this_node->next;
            list_del(this_node);
            app_node_info = list_entry(this_node, struct app_monitor_app_info_list_node_st, list_node);
            kfree(app_node_info);
            this_node = next_node;
        }

        app_monitor_list[i].m_count = 0;
    }
    write_unlock_irqrestore(&app_power_monitor_lock, flags);
    printk("[app_monitor]:monitor end\n");
    report_timer_del();
    return;
}

static inline u32 oplus_app_monitor_find_app_traffic_index(u32 uid_val)
{
    u32 index;
    u32 app_num = top_app_traffic_info.real_app_num;

    for(index = 0; index < app_num; index++){
        if(uid_val == top_app_traffic_info.app_traffic_info[index].uid_val){
            return index;
        }
    }

    return INVALID_APP_INDEX;
}

static inline u32 oplus_app_monitor_find_app_retrans_index(u32 uid_val)
{
    u32 index;
    u32 app_num = top_app_retrans_info.real_app_num;

    for(index = 0; index < app_num; index++){
        if(uid_val == top_app_retrans_info.app_retrans_info[index].uid_val){
            return index;
        }
    }

    return INVALID_APP_INDEX;
}

static inline u32 oplus_app_monitor_find_app_beat_index(u32 uid_val)
{
    u32 index;
    u32 app_num = top_app_beat_info.real_app_num;

    for(index = 0; index < app_num; index++){
        if(uid_val == top_app_beat_info.app_beat_info[index].uid_val){
            return index;
        }
    }

    return INVALID_APP_INDEX;
}

static  inline u32 oplus_app_monitor_find_app_push_index(u32 uid_val)
{
    u32 index;
    u32 app_num = top_app_push_info.real_app_num;

    for(index = 0; index < app_num; index++){
        if(uid_val == top_app_push_info.app_push_info[index].uid_val){
            return index;
        }
    }

    return INVALID_APP_INDEX;
}

void oplus_app_monitor_update_top_trafffic_app(struct app_monitor_app_info_st *app_info)
{
    u32 min_index = top_app_traffic_info.min_index;
    u32 app_index;
    u32 app_uid = app_info->uid_val;
    u32 i;

    app_index = oplus_app_monitor_find_app_traffic_index(app_uid);
    //app record exsit, just update it
    if(app_index < TOP_APP_NUM){
        top_app_traffic_info.app_traffic_info[app_index] = *app_info;
        return;
    }

    //record app traffic info is small than TOP_APP_NUM,just record  it
    if(top_app_traffic_info.real_app_num < TOP_APP_NUM){
        top_app_traffic_info.app_traffic_info[top_app_traffic_info.real_app_num] = *app_info;
        top_app_traffic_info.real_app_num++;
        return;
    }

    //compare with the min record
    if(app_info->total_bytes > top_app_traffic_info.app_traffic_info[min_index].total_bytes){
        top_app_traffic_info.app_traffic_info[min_index] = *app_info;
    }
    else{
        return;
    }

    for(i = 0; i < TOP_APP_NUM; i++){
        if(i == 0){
            min_index = i;
        }
        else if(top_app_traffic_info.app_traffic_info[min_index].total_bytes > top_app_traffic_info.app_traffic_info[i].total_bytes){
            min_index = i;
        }
    }

    top_app_traffic_info.min_index = min_index;
    return;
}


void oplus_app_monitor_update_top_retrans_app(struct app_monitor_app_info_st *app_info)
{
    u32 min_index = top_app_retrans_info.min_index;
    u32 app_index;
    u32 app_uid = app_info->uid_val;
    u32 i;

    app_index = oplus_app_monitor_find_app_retrans_index(app_uid);
    //app record exsit, just update it
    if(app_index < TOP_APP_NUM){
        top_app_retrans_info.app_retrans_info[app_index] = *app_info;
        return;
    }

    //record app traffic info is small than TOP_APP_NUM,just record  it
    if(top_app_retrans_info.real_app_num < TOP_APP_NUM){
        top_app_retrans_info.app_retrans_info[top_app_retrans_info.real_app_num] = *app_info;
        top_app_retrans_info.real_app_num++;
        return;
    }

    //compare with the min record
    if(app_info->retrans_total_packets > top_app_retrans_info.app_retrans_info[min_index].retrans_total_packets){
        top_app_retrans_info.app_retrans_info[min_index] = *app_info;
    }
    else{
        return;
    }

    for(i = 0; i < TOP_APP_NUM; i++){
        if(i == 0){
            min_index = i;
        }
        else if(top_app_retrans_info.app_retrans_info[min_index].retrans_total_packets > top_app_retrans_info.app_retrans_info[i].retrans_total_packets){
            min_index = i;
        }
    }

    top_app_retrans_info.min_index = min_index;
    return;
}


void oplus_app_monitor_update_top_beat_app(struct app_monitor_app_info_st *app_info)
{
    u32 max_index = top_app_beat_info.max_index;
    u32 app_index;
    u32 app_uid = app_info->uid_val;
    u32 i;

    app_index = oplus_app_monitor_find_app_beat_index(app_uid);
    //app record exsit, just update it
    if(app_index < TOP_APP_NUM){
        top_app_beat_info.app_beat_info[app_index] = *app_info;
        return;
    }

    //record app traffic info is small than TOP_APP_NUM,just record  it
    if(top_app_beat_info.real_app_num < TOP_APP_NUM){
        top_app_beat_info.app_beat_info[top_app_beat_info.real_app_num] = *app_info;
        top_app_beat_info.real_app_num++;
        return;
    }

    //compare with the min record
    if(app_info->beat_count > top_app_beat_info.app_beat_info[max_index].beat_count){
        top_app_beat_info.app_beat_info[max_index] = *app_info;
    }
    else{
        return;
    }

    for(i = 0; i < TOP_APP_NUM; i++){
        if(i == 0){
            max_index = i;
        }
        else if(top_app_beat_info.app_beat_info[max_index].beat_count > top_app_beat_info.app_beat_info[i].beat_count){
            max_index = i;
        }
    }

    top_app_beat_info.max_index = max_index;
    return;

}

static void oplus_app_monitor_update_top_push_app(struct app_monitor_app_info_st *app_info)
{
    u32 max_index = top_app_push_info.max_index;
    u32 app_index;
    u32 app_uid = app_info->uid_val;
    u32 i;

    app_index = oplus_app_monitor_find_app_push_index(app_uid);
    //app record exsit, just update it
    if(app_index < TOP_APP_NUM){
        top_app_push_info.app_push_info[app_index] = *app_info;
        return;
    }

    //record app traffic info is small than TOP_APP_NUM,just record  it
    if(top_app_push_info.real_app_num < TOP_APP_NUM){
        top_app_push_info.app_push_info[top_app_push_info.real_app_num] = *app_info;
        top_app_push_info.real_app_num++;
        return;
    }

    //compare with the min record
    if(app_info->push_count > top_app_push_info.app_push_info[max_index].push_count){
        top_app_push_info.app_push_info[max_index] = *app_info;
    }
    else{
        return;
    }

    for(i = 0; i < TOP_APP_NUM; i++){
        if(i == 0){
            max_index = i;
        }
        else if(top_app_push_info.app_push_info[max_index].push_count > top_app_push_info.app_push_info[i].push_count){
            max_index = i;
        }
    }

    top_app_push_info.max_index = max_index;
    return;
}

static struct app_monitor_app_info_list_node_st* oplus_app_monitor_get_app_info_by_uid(u32 uid)
{
    struct app_monitor_app_info_list_node_st *app_info_node = NULL;
    u32 hash_index = uid & APP_MONITOR_HASH_MASK;

    //list_for_each_entry(app_info_node, &app_moniter_list_head[hash_index], list_node){
    list_for_each_entry(app_info_node, &app_monitor_list[hash_index].m_listhead, list_node){
        if(app_info_node->app_info.uid_val == uid){
            return app_info_node;
        }
    }

    app_info_node = kzalloc(sizeof(struct app_monitor_app_info_list_node_st), GFP_ATOMIC);
    if(!app_info_node){
        return NULL;
    }
    else
    {
        app_info_node->app_info.uid_val = uid;
        list_add(&app_info_node->list_node, &app_monitor_list[hash_index].m_listhead);
    }

     return app_info_node;
}

void oplus_app_monitor_update_flow_stat(struct app_monitor_app_info_list_node_st *app_info_node,
                                       const  struct sk_buff *skb, int send)
{
    if(send){
        app_info_node->app_info.send_packets++;
        app_info_node->app_info.send_bytes += skb->len;
    }
    else{
        app_info_node->app_info.recv_packets++;
        app_info_node->app_info.recv_bytes += skb->len;
    }

    app_info_node->app_info.total_packets++;
    app_info_node->app_info.total_bytes += skb->len;
    return;
}

void oplus_app_monitor_update_retrans_stat(struct app_monitor_app_info_list_node_st *app_info_node,
                                       const  struct sk_buff *skb)
{
    app_info_node->app_info.retrans_total_packets++;
    if((TCP_SKB_CB(skb)->tcp_flags) & TCPHDR_SYN){
        app_info_node->app_info.retrans_syn_packets++;
    }

    return;
}


void oplus_app_monitor_send_beat_alarm(u32 uid_val, u32 period)
{
    int ret;
    char msg_buf[1024] = {0};
    struct app_monitor_ul_beat_alarm_msg_st  *beat_alarm_msg = (struct app_monitor_ul_beat_alarm_msg_st  *)msg_buf;

    beat_alarm_msg->uid_val = uid_val;
    beat_alarm_msg->beat_period = period;
    beat_alarm_msg->msg_len = sizeof(struct app_monitor_ul_beat_alarm_msg_st);
    ret = apps_monitor_netlink_send_to_user(APPS_POWER_MONITOR_MSG_UL_BEAT_ALARM, (char *) msg_buf,
                                            beat_alarm_msg->msg_len );
    if(ret){
        printk("[app_monitor]:send beat alarm info error!\n ");
    }

    return;
}

void oplus_app_monitor_send_push_alarm(u32  uid_val, u32 period)
{
    int ret;
    char msg_buf[1024] = {0};
    struct app_monitor_ul_push_alarm_msg_st  *push_alarm_msg = (struct app_monitor_ul_push_alarm_msg_st  *)msg_buf;

    push_alarm_msg->uid_val = uid_val;
    push_alarm_msg->push_period= period;
    push_alarm_msg->msg_len = sizeof(struct app_monitor_ul_push_alarm_msg_st);
    ret = apps_monitor_netlink_send_to_user(APPS_POWER_MONITOR_MSG_UL_PUSH_ALARM, (char *) msg_buf, push_alarm_msg->msg_len );
    if(ret){
        printk("[app_monitor]:send push alarm info error!\n ");
    }

    return;
}

void oplus_app_monitor_send_traffic_alarm(struct app_monitor_app_info_st *app_info)
{
    int ret;
    char msg_buf[1024] = {0};
    struct app_monitor_ul_traffic_alarm_msg_st  *traffic_alarm_msg = (struct app_monitor_ul_traffic_alarm_msg_st  *)msg_buf;

    traffic_alarm_msg->uid_val = app_info->uid_val;
    traffic_alarm_msg->msg_len = sizeof(struct app_monitor_ul_traffic_alarm_msg_st);
    traffic_alarm_msg->total_bytes = app_info->total_bytes;
    traffic_alarm_msg->total_packets = app_info->total_packets;
    traffic_alarm_msg->recv_bytes = app_info->recv_bytes;
    traffic_alarm_msg->recv_packets = app_info->recv_packets;
    traffic_alarm_msg->send_bytes = app_info->send_bytes;
    traffic_alarm_msg->send_packets = app_info->send_packets;
    traffic_alarm_msg->traffic_period = ALARM_DETECT_PERIOD;
    ret = apps_monitor_netlink_send_to_user(APPS_POWER_MONITOR_MSG_UL_TRAFFIC_ALARM,(char *) msg_buf,traffic_alarm_msg->msg_len);
    if(ret){
        printk("[app_monitor]:send traffic alarm info error!\n ");
    }

    return;
}

void oplus_app_update_sys_static_info(const struct sk_buff *skb, int send, int retrans)
{
    if(send && retrans){
        sys_stati_info.sys_retrans_total_packets++;
	 if((TCP_SKB_CB(skb)->tcp_flags) & TCPHDR_SYN){
	     sys_stati_info.sys_retrans_syn_packets++;
	 }
	 return;
    }

    sys_stati_info.sys_total_packets++;
    sys_stati_info.sys_total_bytes += skb->len;
    if(send){
        sys_stati_info.sys_send_packets++;
	 sys_stati_info.sys_send_bytes += skb->len;
    }
    else{
        sys_stati_info.sys_recv_packets++;
        sys_stati_info.sys_recv_bytes += skb->len;
    }
}

void oplus_app_monitor_update_app_info(struct sock *sk, const struct sk_buff *skb, int send, int retrans)
{
    u32 uid_val;
    u32 hash_index;
    struct app_monitor_app_info_list_node_st *app_info_node = 0;
    u32 app_samp_time; //unit:second
    struct timeval now_tv;
    u32 now_sec;
    unsigned long flags;

    uid_val = sk->sk_uid.val;
    if(uid_val < 10000 || !skb){
         return;
    }

    hash_index = uid_val & APP_MONITOR_HASH_MASK;
    do_gettimeofday(&now_tv);
    now_sec = (u32)now_tv.tv_sec;
    write_lock_irqsave(&app_power_monitor_lock, flags);
    if(!app_monitor_enable){
        goto exit_func;
    }

    oplus_app_update_sys_static_info(skb, send, retrans);

    app_info_node =  oplus_app_monitor_get_app_info_by_uid(uid_val);
    if(!app_info_node){
        printk("[app_monitor]:get app_info_node error!\n");
        goto exit_func;
    }

    if(retrans){
        oplus_app_monitor_update_retrans_stat(app_info_node, skb);
        oplus_app_monitor_update_top_retrans_app(&app_info_node->app_info);
    }

    if(!skb->len){
        goto exit_func;
    }
    //do traffic ralative operation
    oplus_app_monitor_update_flow_stat(app_info_node, skb, send);
    oplus_app_monitor_update_top_trafffic_app(&app_info_node->app_info);

    if(retrans){
        goto exit_func;
    }

    //do beat check ralative operation
    if(send){
        if(!app_info_node->app_info.last_send_time){
            goto exit_func;
        }

        app_samp_time = now_sec -  app_info_node->app_info.last_active_time;
        if(app_samp_time >= PERIOD_CHECK_TIME && app_samp_time < REPORT_INTERVAL){
            app_info_node->app_info.beat_period = (app_info_node->app_info.beat_period * app_info_node->app_info.beat_count +
                                                                  app_samp_time) / (app_info_node->app_info.beat_count + 1);
            if(!app_info_node->app_info.beat_count)
                app_info_node->app_info.beat_alarm_start_time = now_sec;

            app_info_node->app_info.beat_count++;
            oplus_app_monitor_update_top_beat_app(&app_info_node->app_info);

            if(app_info_node->app_info.beat_count % ALARM_DETECT_COUNT == 0){
                int time_gap = now_sec - app_info_node->app_info.beat_alarm_start_time;
                int period = time_gap / ALARM_DETECT_COUNT;
                if(app_info_node->app_info.beat_alarm_start_time && time_gap < ALARM_DETECT_PERIOD){
                    oplus_app_monitor_send_beat_alarm(app_info_node->app_info.uid_val, period);
                }
                app_info_node->app_info.beat_alarm_start_time = now_sec;
            }
        }
    }
    else{
        if(!app_info_node->app_info.last_active_time){
            goto exit_func;
        }

        app_samp_time = now_sec -  app_info_node->app_info.last_active_time;
        if(app_samp_time > PERIOD_CHECK_TIME && app_samp_time < REPORT_INTERVAL){
            app_info_node->app_info.push_period = (app_info_node->app_info.push_period * app_info_node->app_info.push_count +
                                                                   app_samp_time) / (app_info_node->app_info.push_count + 1);
            if(!app_info_node->app_info.push_count)
                app_info_node->app_info.push_alarm_start_time = now_sec;

            app_info_node->app_info.push_count++;
            oplus_app_monitor_update_top_push_app(&app_info_node->app_info);

            if(app_info_node->app_info.push_count % ALARM_DETECT_COUNT == 0){
                int time_gap = now_sec - app_info_node->app_info.push_alarm_start_time;
                int period = time_gap / ALARM_DETECT_COUNT;
                if(app_info_node->app_info.push_alarm_start_time && time_gap < ALARM_DETECT_PERIOD){
                    oplus_app_monitor_send_push_alarm(app_info_node->app_info.uid_val, period);
                }
                app_info_node->app_info.push_alarm_start_time = now_sec;
            }
        }
    }

exit_func:
    if(app_info_node){
        app_info_node->app_info.last_active_time = now_sec;
        if(send){
            app_info_node->app_info.last_send_time = now_sec;
        }
    }
    write_unlock_irqrestore(&app_power_monitor_lock, flags);
}
EXPORT_SYMBOL(oplus_app_monitor_update_app_info);

int app_monitor_dl_ctl_msg_handle(struct nlmsghdr *nlh)
{
    struct app_monitor_dl_ctl_msg_st *dl_ctl_msg = (struct app_monitor_dl_ctl_msg_st *)NLMSG_DATA(nlh);
    printk("[app_monitor]:dl_ctl_msg received\n");

    if(dl_ctl_msg->msg_len != sizeof(struct app_monitor_dl_ctl_msg_st)){
        printk("[app_monitor]:msg_len err, msg_len=%u, expected_len=%lu\n",
        dl_ctl_msg->msg_len, sizeof(struct app_monitor_dl_ctl_msg_st));
        return -EINVAL;
    }

    if(dl_ctl_msg->start_flag){
        oplus_app_monitor_start();
    }else{
        oplus_app_monitor_stop();
    }

    return 0;
}

static int proc_app_power_monitor_start(struct ctl_table *ctl, int write, void __user *buffer, size_t *lenp,loff_t *ppos)
{
    int ret;
    printk("[app_monitor]:proc_app_power_monitor_start\n");
    ret = proc_dointvec(ctl, write, buffer, lenp, ppos);
    if (write && ret == 0){
        if(app_monitor_start){
            oplus_app_monitor_start();
        }
        else{
            oplus_app_monitor_stop();
        }
    }

    return ret;
}

static int proc_app_power_report_request(struct ctl_table *ctl, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
    int  ret;
    printk("[app_monitor]:proc_app_power_report_request");
    ret = proc_dointvec(ctl, write, buffer, lenp, ppos);
    if (write && ret == 0){
        if(app_report_request){
            oplus_app_monitor_send_app_info_report();
        }
    }

    return ret;
}

static int proc_app_set_beat_alarm_period(struct ctl_table *ctl, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
    int  ret;
    int  index;
    printk("[app_monitor]:proc_app_set_beat_alarm_period");
    ret = proc_dointvec(ctl, write, buffer, lenp, ppos);
    if (write && ret == 0){
        index = top_app_beat_info.max_index;
        oplus_app_monitor_send_beat_alarm(top_app_beat_info.app_beat_info[index].uid_val, (u32)app_beat_alarm_period);
        printk("[app_monitor]:alarm:uid=%u,beat_period=%d", top_app_beat_info.app_beat_info[index].uid_val, app_beat_alarm_period);
    }

    return ret;
}

static int proc_app_set_push_alarm_period(struct ctl_table *ctl, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
    int  ret;
    int  index;
    printk("[app_monitor]:proc_app_set_push_alarm_period");
    ret = proc_dointvec(ctl, write, buffer, lenp, ppos);
    if (write && ret == 0){
        index = top_app_push_info.max_index;
        oplus_app_monitor_send_push_alarm(top_app_push_info.app_push_info[index].uid_val, (u32)app_push_alarm_period);
        printk("[app_monitor]:alarm:uid=%u,push_period=%d", top_app_push_info.app_push_info[index].uid_val, app_push_alarm_period);
    }

    return ret;
}

int app_monitor_dl_report_msg_handle(struct nlmsghdr *nlh)
{
    printk("[app_monitor]:dl_report_req_msg received\n");
    oplus_app_monitor_send_app_info_report();
    report_timer_restart();
    return 0;
}

static struct ctl_table app_power_monitor_sysctl_table[] =
{
    {
        .procname = "app_monitor_start",
        .data = &app_monitor_start,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_app_power_monitor_start,
    },
    {
        .procname = "app_report_request",
        .data = &app_report_request,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_app_power_report_request,
    },
    {
        .procname = "app_beat_alarn_period",
        .data = &app_beat_alarm_period,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_app_set_beat_alarm_period,
    },
    {
        .procname = "app_push_alarn_period",
        .data = &app_push_alarm_period,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_app_set_push_alarm_period,
    },
    { }
};

static int  oplus_app_power_monitor_sysctl_init(void)
{
    app_power_monitor_table_hrd = register_net_sysctl(&init_net, "net/oplus_app_power_monitor", app_power_monitor_sysctl_table);
    return app_power_monitor_table_hrd == NULL ? -ENOMEM : 0;
}

void oplus_app_power_monitor_init(void)
{
    int i;
    unsigned long flags;

    report_timer_init();
    oplus_app_power_monitor_sysctl_init();
    rwlock_init(&app_power_monitor_lock);
    write_lock_irqsave(&app_power_monitor_lock, flags);
    app_monitor_enable = 0;

    memset(&top_app_traffic_info, 0, sizeof(struct app_monitor_top_app_trafffic_info_st));
    memset(&top_app_beat_info, 0, sizeof(struct app_monitor_top_app_beat_info_st));
    memset(&top_app_push_info, 0, sizeof(struct app_monitor_top_app_push_info_st));
    memset(&top_app_retrans_info, 0, sizeof(struct app_monitor_top_app_retrans_info_st));
    memset(&sys_stati_info, 0, sizeof(struct sys_stati_info_st));

    for(i = 0; i < APP_MONITOR_HASHTABLE_SIZE; i++){
        INIT_LIST_HEAD(&app_monitor_list[i].m_listhead);
    }

    write_unlock_irqrestore(&app_power_monitor_lock, flags);
    printk("[app_monitor]:oplus_app_power_monitor_init\n");
    return;
}

void oplus_app_power_monitor_fini(void)
{
    report_timer_del();
    oplus_app_monitor_stop();
    if(app_power_monitor_table_hrd){
        unregister_net_sysctl_table(app_power_monitor_table_hrd);
    }
}



