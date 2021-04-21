/**********************************************************************************
* Copyright (c)  2008-2015  OnePlus Mobile Comm Corp., Ltd
* Description:    OnePlus Healthinfo Monitor
*                          Record Kernel Resourse Abnormal Stat
* Version    : 2.0
* Date       : 2019-04-24
***********************************************************************************/

#ifndef _ONEPLUS_HEALTHINFO_H_
#define _ONEPLUS_HEALTHINFO_H_

#include <linux/latencytop.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/cpuidle.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/slab.h>
/*2020-06-22 ，OSP-5970 , monitor cpu info **/
#include <linux/ratelimit.h>


#ifdef CONFIG_ONEPLUS_MEM_MONITOR
#include <linux/oem/memory_monitor.h>
#endif /*CONFIG_ONEPLUS_MEM_MONITOR*/

#define ohm_err(fmt, ...) \
	printk(KERN_ERR "[OHM_ERR][%s]"fmt, __func__, ##__VA_ARGS__)
#define ohm_debug(fmt, ...) \
	printk(KERN_INFO "[OHM_INFO][%s]"fmt, __func__, ##__VA_ARGS__)
#define ohm_debug_deferred(fmt, ...) \
	printk_deferred(KERN_INFO "[OHM_INFO][%s]"fmt, __func__, ##__VA_ARGS__)
#define ohm_err_deferred(fmt, ...) \
    printk_deferred(KERN_ERR "[OHM_ERR][%s]"fmt, __func__, ##__VA_ARGS__)

#define OHM_FLASH_TYPE_EMC 1
#define OHM_FLASH_TYPE_UFS 2

#define OHM_SCHED_TYPE_MAX 12
/*2020-06-22 ，OSP-5970 , monitor cpu info **/
#define DEFAULT_RT_LT 1000000
#define DEFAULT_RT_HT 5000000
#define MAX_RT_EXEC 10000000
#ifdef CONFIG_ONEPLUS_HEALTHINFO
/*2020-06-17 add for stuck info*/
enum {
    STUCK_TRACE_RUNNABLE = 0,
    STUCK_TRACE_DSTATE,
    STUCK_TRACE_SSTATE,
    STUCK_TRACE_RUNNING,
};
#endif /*CONFIG_ONEPLUS_HEALTHINFO*/
enum {
	/* SCHED_STATS 0 -11 */
	OHM_SCHED_IOWAIT = 0,
	OHM_SCHED_SCHEDLATENCY,
	OHM_SCHED_FSYNC,
	OHM_SCHED_EMMCIO,
	/*2020-06-22 ，OSP-5970 , monitor cpu info **/
	OHM_SCHED_DSTATE,
	OHM_SCHED_TOTAL,
	/* OTHER_TYPE 12 - */
	OHM_CPU_LOAD_CUR = OHM_SCHED_TYPE_MAX,
	OHM_MEM_MON,
	/*2020-06-22 ，OSP-5970 , monitor cpu info **/
	OHM_RT_MON,
	OHM_PREEMPT_LATENCY,
	OHM_IOPANIC_MON,
	OHM_TYPE_TOTAL
};

struct rq;
struct rt_rq;

struct sched_stat_common {
		u64 max_ms;
		u64 high_cnt;
		u64 low_cnt;
		u64 total_ms;
		u64 total_cnt;
};

struct sched_stat_para {
        bool ctrl;
        bool logon;
        bool trig;
        int low_thresh_ms;
        int high_thresh_ms;
        u64 low_cnt;
        u64 high_cnt;
        u64 total_ms;
        u64 total_cnt;
        u64 fg_low_cnt;
        u64 fg_high_cnt;
        u64 fg_total_ms;
        u64 fg_total_cnt;
        u64 fg_max_delta_ms;
        u64 delta_ms;
        struct sched_stat_common all;
        struct sched_stat_common fg;
        struct sched_stat_common ux;
};

/* Preempt structure*/
struct cpu_preempt_stat{
		struct sched_stat_common preempt_common;
		int low_thresh_ms;
        int high_thresh_ms;
};

/*rt structure*/
struct longest_task_info {
		char comment[16];
		u64 max_exec_ns;
};
struct sched_stat_rt_para {
		u64 each_cpu_rt;
		u64 each_cpu_rt_total;
		u64 thresh_cnt[3];
		struct longest_task_info *lt_info;
};
/* irq latency structure*/
struct irq_latency_para {
		int low_thresh;
		int high_thresh;
		u64 max;
		u64 high_cnt;
		u64 low_cnt;
		u64 total;
		u64 total_cnt;
};

extern bool ohm_rtinfo_ctrl;
extern bool ohm_preempt_ctrl;
extern bool ohm_irqsoff_ctrl;

extern void ohm_overload_record(struct rq *rq, u64 delta_ms);
extern void rt_thresh_times_record(struct task_struct *p, unsigned int cpu);
extern void rt_info_record(struct rt_rq *rt_rq, unsigned int cpu);
extern void rt_total_record(u64 delta_exec, unsigned int cpu);
extern void ohm_preempt_record(u64 delta, int cpu);
extern void ohm_irqsoff_record(u64 delta, int cpu);

extern int ohm_get_cur_cpuload(bool ctrl);

#endif /* _ONEPLUS_HEALTHINFO_H_*/
