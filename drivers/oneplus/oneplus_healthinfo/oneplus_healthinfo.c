/**********************************************************************************
* Copyright (c)  2008-2015 OnePlus Mobile Comm Corp., Ltd
* Description:     Healthinfo Monitor  Kernel Driver
*
* Version   : 1.0
* Date       : 2019-04-24
***********************************************************************************/

#include <linux/oem/oneplus_healthinfo.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/blkdev.h>
#ifdef CONFIG_ONEPLUS_TASKLOAD_INFO
#include <linux/sched.h>
#include <linux/sched/signal.h>
#endif
#ifdef CONFIG_ONEPLUS_MEM_MONITOR
#include <linux/oem/memory_monitor.h>
#endif
/*2020-06-22 ，OSP-5970 , monitor cpu info **/
#include <linux/cpufreq.h>
#include "../../../../kernel/sched/sched.h"
#include "../../../../kernel/sched/walt.h"
#include "../../../../include/linux/cred.h"

struct io_latency_para{
	bool ctrl;
	bool logon;
	bool trig;

	int low_thresh_ms;
	u64 low_cnt;

	int high_thresh_ms;
	u64 high_cnt;

	u64 total_us;
	u64 emmc_total_us;
	u64 total_cnt;
	u64 fg_low_cnt;
	u64 fg_high_cnt;
	u64 fg_total_ms;
	u64 fg_total_cnt;
	u64 fg_max_delta_ms;
	u64 delta_ms;

	//fg
	u64 iosize_write_count_fg;
	u64 iosize_write_us_fg;
	u64 iosize_500ms_syncwrite_count_fg;
	u64 iosize_200ms_syncwrite_count_fg;
	u64 iosize_500ms_asyncwrite_count_fg;
	u64 iosize_200ms_asyncwrite_count_fg;
	u64 iosize_read_count_fg;
	u64 iosize_read_us_fg;
	u64 iosize_500ms_read_count_fg;
	u64 iosize_200ms_read_count_fg;

	//bg
	u64 iosize_write_count_bg;
	u64 iosize_write_us_bg;
	u64 iosize_2s_asyncwrite_count_bg;
	u64 iosize_500ms_asyncwrite_count_bg;
	u64 iosize_200ms_asyncwrite_count_bg;
	u64 iosize_2s_syncwrite_count_bg;
	u64 iosize_500ms_syncwrite_count_bg;
	u64 iosize_200ms_syncwrite_count_bg;
	u64 iosize_read_count_bg;
	u64 iosize_read_us_bg;
	u64 iosize_2s_read_count_bg;
	u64 iosize_500ms_read_count_bg;
	u64 iosize_200ms_read_count_bg;

	//4k
	u64 iosize_4k_read_count;
	u64 iosize_4k_read_us;
	u64 iosize_4k_write_count;
	u64 iosize_4k_write_us;
};

struct io_latency_para oneplus_io_para;

#define BUFFER_SIZE_S 256
#define BUFFER_SIZE_M 512
#define BUFFER_SIZE_L 1024

/* rt info monitor */
bool rt_info_ctrl;
u64 rt_low_thresh = DEFAULT_RT_LT;
u64 rt_high_thresh = DEFAULT_RT_HT;

struct sched_stat_rt_para rt_para[NR_CPUS];
struct cpu_preempt_stat preempt_para[NR_CPUS];

#ifdef CONFIG_ONEPLUS_TASKLOAD_INFO
static struct timer_list task_load_info_timer;
u64 ohm_read_thresh = 1048576; /* default 1MB per 5s */
u64 ohm_write_thresh = 1048576; /* default 1MB per 5s */
u64 ohm_runtime_thresh_fg = 4000000000; /* default 4s per 5s */
u64 ohm_runtime_thresh_bg = 1500000000; /* default 1.5s per 5s */
static u32 ohm_sample_time = 5; /* default 5s */
#endif

struct sched_stat_para oneplus_sched_para[OHM_SCHED_TOTAL];
static char *sched_list[OHM_TYPE_TOTAL] = {
	/* SCHED_STATS 0 -11 */
	"iowait",
	"sched_latency",
	"fsync",
	"emmcio",
	"downread",
	"downwrite",
	/*2020-06-22 ，OSP-5970 , monitor cpu info **/
	"dstate",
	"sched_default_05",
	"sched_default_06",
	"sched_default_07",
	"sched_default_10",
	"sched_default_11",
	/* OTHER_TYPE 12 - */
	"cur_cpu_load",
	"memory_monitor",
	"io_panic",
	"rt_info",
	"preempt_latency"
};

/******  Action  ******/
#define MAX_OHMEVENT_PARAM 4
#define TRIG_MIN_INTERVAL 400
static struct kobject *ohm_kobj;
static char *ohm_detect_env[MAX_OHMEVENT_PARAM] = { "OHMACTION=uevent", NULL };
static bool ohm_action_ctrl;
static unsigned long last_trig;
static unsigned long iowait_summ_start;
static int iowait_summ_period;
static int iowait_summ_thresh;
static int iowait_total;
static bool iowait_summ_reset = true;

static atomic_t ohm_scheduled;
static struct kthread_worker __rcu *ohm_kworker;
static struct kthread_delayed_work ohm_work;

void ohm_action_trig(int type)
{
	struct kthread_worker *kworker;

	if (!ohm_action_ctrl) {
		ohm_err_deferred("ctrl off\n");
		return;
	}
	ohm_debug_deferred("%s trig action\n", sched_list[type]);
	if (OHM_MEM_MON == type || OHM_SCHED_FSYNC == type) {
		if (!ohm_kobj) {
			ohm_err_deferred("kobj NULL\n");
			return;
		}
		if (atomic_cmpxchg(&ohm_scheduled, 0, 1) != 0)
			return;
		sprintf(ohm_detect_env[1], "OHMTYPE=%s", sched_list[type]);
		sprintf(ohm_detect_env[2], "NOLEVEL");
		ohm_detect_env[MAX_OHMEVENT_PARAM - 1] = NULL;
		rcu_read_lock();
		kworker = rcu_dereference(ohm_kworker);
		if (likely(kworker))
			kthread_queue_delayed_work(kworker, &ohm_work, msecs_to_jiffies(1));
		rcu_read_unlock();
	}
}

void ohm_action_trig_level(int type, bool highlevel)
{
	struct kthread_worker *kworker;

	if (!ohm_action_ctrl) {
		ohm_err_deferred("ctrl off\n");
		return;
	}
	ohm_debug_deferred("%s trig action\n", sched_list[type]);
	if (OHM_MEM_MON == type || OHM_SCHED_FSYNC == type || OHM_SCHED_IOWAIT == type) {
		if (!ohm_kobj) {
			ohm_err_deferred("kobj NULL\n");
			return;
		}
		if (!time_after(jiffies, last_trig + msecs_to_jiffies(TRIG_MIN_INTERVAL)))
			return;
		if (atomic_cmpxchg(&ohm_scheduled, 0, 1) != 0)
			return;
		ohm_debug_deferred("%s trig action\n", sched_list[type]);
		sprintf(ohm_detect_env[1], "OHMTYPE=%s", sched_list[type]);
		sprintf(ohm_detect_env[2], "OHMLEVEL=%s", highlevel?"HIGH":"LOW");
		ohm_detect_env[MAX_OHMEVENT_PARAM - 1] = NULL;
		rcu_read_lock();
		kworker = rcu_dereference(ohm_kworker);
		if (likely(kworker))
			kthread_queue_delayed_work(kworker, &ohm_work, msecs_to_jiffies(1));
		rcu_read_unlock();
		last_trig = jiffies;
	}
}

void ohm_detect_work(struct kthread_work *work)
{
	ohm_debug_deferred("Uevent Para: %s, %s\n", ohm_detect_env[0], ohm_detect_env[1]);
	kobject_uevent_env(ohm_kobj, KOBJ_CHANGE, ohm_detect_env);
	ohm_debug_deferred("Uevent Done!\n");
	atomic_set(&ohm_scheduled, 0);
}

void ohm_action_init(void)
{
	int i = 0;
	struct kthread_worker *kworker;

	for (i = 1; i < MAX_OHMEVENT_PARAM - 1; i++) {
		ohm_detect_env[i] = kzalloc(50, GFP_KERNEL);
		if (!ohm_detect_env[i]) {
			ohm_err("kzalloc ohm uevent param failed\n");
			goto ohm_action_init_free_memory;
		}
	}

	ohm_kobj = kset_find_obj(module_kset, KBUILD_MODNAME);
	if (!ohm_kobj) {
		goto ohm_action_init_kobj_failed;
	}
	last_trig = jiffies;
	kworker = kthread_create_worker(0, "healthinfo");
	if (IS_ERR(kworker)) {
		goto ohm_action_init_free_memory;
	}
	kthread_init_delayed_work(&ohm_work, ohm_detect_work);
	rcu_assign_pointer(ohm_kworker, kworker);
	atomic_set(&ohm_scheduled, 0);
	ohm_debug("Success !\n");
	return;

ohm_action_init_kobj_failed:
	ohm_err("Ohm kobj init err\n");
ohm_action_init_free_memory:
	for (i--; i > 0; i--) {
		kfree(ohm_detect_env[i]);
	}
	ohm_err("Failed !\n");
}

/******  Sched record  ******/
/*2020-06-22 ，OSP-5970 , monitor cpu info **/
static inline void ohm_sched_stat_record_common(struct sched_stat_para *sched_stat,struct sched_stat_common *stat_common, u64 delta_ms)
{
	stat_common->total_ms += delta_ms;
	stat_common->total_cnt++;
	if (delta_ms > stat_common->max_ms) {
		stat_common->max_ms = delta_ms;
	}
	if (delta_ms >= sched_stat->high_thresh_ms) {
		stat_common->high_cnt++;
	} else if (delta_ms >= sched_stat->low_thresh_ms) {
		stat_common->low_cnt++;
	}
}

void ohm_schedstats_record(int sched_type, struct task_struct *task, u64 delta_ms)
{

	struct sched_stat_para *sched_stat = &oneplus_sched_para[sched_type];

	if (unlikely(!sched_stat->ctrl)){
		return;
	}

	sched_stat->delta_ms = delta_ms;
	ohm_sched_stat_record_common(sched_stat, &sched_stat->all, delta_ms);

	if (task_is_fg(task)) {
		ohm_sched_stat_record_common(sched_stat, &sched_stat->fg, delta_ms);
		if (sched_type == OHM_SCHED_IOWAIT) {
			if (time_after(jiffies, iowait_summ_start + msecs_to_jiffies(iowait_summ_period)) || iowait_summ_reset) {
				iowait_total = delta_ms;
				iowait_summ_start = jiffies - msecs_to_jiffies(delta_ms);
				iowait_summ_reset = false;
			} else {
				iowait_total += delta_ms;
			}
		}
		if (unlikely(delta_ms >= sched_stat->high_thresh_ms)){
			if (oneplus_sched_para[sched_type].logon) {
				ohm_debug_deferred("[%s / %s] high_cnt, delay = %llu ms\n",
							sched_list[sched_type], "fg", delta_ms);
			}

			if (oneplus_sched_para[sched_type].trig)
				ohm_action_trig_level(sched_type, true);

		} else if (delta_ms >= sched_stat->low_thresh_ms) {
			if (oneplus_sched_para[sched_type].trig)
                                ohm_action_trig_level(sched_type, false);
		} else if (iowait_total >= iowait_summ_thresh) {
			iowait_summ_reset = true;
			if (oneplus_sched_para[sched_type].trig)
				ohm_action_trig_level(sched_type, false);
		}
	}

	return;
}

void ohm_overload_record(struct rq *rq, u64 delta)
{
	struct sched_stat_para *sched_stat = NULL;
	sched_stat = rq->cluster->overload;
        sched_stat->delta_ms = delta;
        ohm_sched_stat_record_common(sched_stat, &sched_stat->all, delta);
	return;
}
static inline void ohm_preempt_stat_record_common(struct cpu_preempt_stat *sched_stat,struct sched_stat_common *stat_common, u64 delta_ms)
{
    stat_common->total_ms += delta_ms;
    stat_common->total_cnt++;

    if (delta_ms > stat_common->max_ms) {
        stat_common->max_ms = delta_ms;
    }

    if (delta_ms >= sched_stat->high_thresh_ms) {
        stat_common->high_cnt++;
    } else if (delta_ms >= sched_stat->low_thresh_ms) {
        stat_common->low_cnt++;
    }

}

void ohm_preempt_record(u64 delta,int cpu)
{
	struct cpu_preempt_stat *preempt_stat = &preempt_para[cpu];

	if (!preempt_stat)
		return ;

	ohm_preempt_stat_record_common(preempt_stat,&(preempt_stat->preempt_common),delta);
	return ;

}

struct irq_latency_para irq_latency_stat[NR_CPUS];
void ohm_irqsoff_record(u64 delta, int cpu)
{
	irq_latency_stat[cpu].total += delta;
	irq_latency_stat[cpu].total_cnt++;

	if (delta > irq_latency_stat[cpu].max) {
		irq_latency_stat[cpu].max = delta;
	}

	if (delta >= irq_latency_stat[cpu].high_thresh) {
		irq_latency_stat[cpu].high_cnt++;
	} else if (delta >= irq_latency_stat[cpu].low_thresh) {
		irq_latency_stat[cpu].low_cnt++;
	}
}


/******  stuck info read  start ******/
/* 2020-06-17, add for stuck monitor*/
void update_stuck_trace_info(struct task_struct *tsk, int trace_type, unsigned int cpu, u64 delta)
{

	static unsigned int ltt_cpu_nr = 0;
	/* this just for 8150,4+3+1*/
	static unsigned int mid_cpu_end = 6;
	static unsigned int big_cpu_end = 7;

	if (!tsk->stuck_trace) {
		return;
	}

	if (!ltt_cpu_nr) {
		ltt_cpu_nr = cpumask_weight(topology_core_cpumask(ltt_cpu_nr));
		printk("fuyou_update_stuck_trace_info ltt_cpu_nr = %u",ltt_cpu_nr);
	}

	if (trace_type == STUCK_TRACE_RUNNABLE) { // runnable
		tsk->oneplus_stuck_info.runnable_state           += delta;
	} else if (trace_type == STUCK_TRACE_DSTATE) { // D state
		tsk->oneplus_stuck_info.d_state.cnt++;
		if (tsk->in_iowait) {
			tsk->oneplus_stuck_info.d_state.iowait_ns    += delta;
		} else if (tsk->in_mutex) {
			tsk->oneplus_stuck_info.d_state.mutex_ns     += delta;
		} else if (tsk->in_downread) {
			tsk->oneplus_stuck_info.d_state.downread_ns  += delta;
		} else if (tsk->in_downwrite) {
			tsk->oneplus_stuck_info.d_state.downwrite_ns += delta;
		} else {
			tsk->oneplus_stuck_info.d_state.other_ns     += delta;
		}
	} else if (trace_type == STUCK_TRACE_SSTATE) { // S state
		tsk->oneplus_stuck_info.s_state.cnt++;
		if (tsk->in_binder) {
			tsk->oneplus_stuck_info.s_state.binder_ns    += delta;
		} else if (tsk->in_futex) {
			tsk->oneplus_stuck_info.s_state.futex_ns     += delta;
		} else if (tsk->in_epoll) {
			tsk->oneplus_stuck_info.s_state.epoll_ns     += delta;
		} else {
			tsk->oneplus_stuck_info.s_state.other_ns     += delta;
		}
	} else if (trace_type == STUCK_TRACE_RUNNING) { // running
		if (cpu < ltt_cpu_nr) {
			tsk->oneplus_stuck_info.ltt_running_state += delta;
		} else if (cpu <= mid_cpu_end) {
			tsk->oneplus_stuck_info.mid_running_state += delta;
		} else if (cpu == big_cpu_end) {
			tsk->oneplus_stuck_info.big_running_state += delta;
		}
	}
}

/******  Flash IO Latency record  ******/
void ohm_iolatency_record(struct request *req, unsigned int nr_bytes, int fg, u64 delta_us)
{
	u64 delta_ms = delta_us / 1000;

	if (!oneplus_io_para.ctrl)
		return;
	if (!req)
		return;
	if (fg)
	{
		oneplus_io_para.fg_total_ms += delta_ms;
		oneplus_io_para.fg_total_cnt++;
		if (delta_ms > oneplus_io_para.fg_max_delta_ms)
		{
			oneplus_io_para.fg_max_delta_ms = delta_ms;
		}
	}

	if (delta_ms >= oneplus_io_para.high_thresh_ms)
	{
		oneplus_io_para.high_cnt++;

		if (oneplus_io_para.logon)
		{
			ohm_debug("[io latency / %s] high_cnt, delay = %llu ms\n",
					  (fg ? "fg" : "bg"), delta_ms);
		}
		if (fg)
		{
			oneplus_io_para.fg_high_cnt++;
			if (oneplus_io_para.trig)
				ohm_action_trig(OHM_SCHED_EMMCIO);
		}
	}
	else if (delta_ms >= oneplus_io_para.low_thresh_ms)
	{
		oneplus_io_para.low_cnt++;
		if (fg)
		{
			oneplus_io_para.fg_low_cnt++;
		}
	}

	if (fg)
	{
		if ((req_op(req) != REQ_OP_DISCARD) && (req_op(req) != REQ_OP_SECURE_ERASE))
		{
			if (req_op(req) == REQ_OP_WRITE || req_op(req) == REQ_OP_WRITE_SAME)
			{
				oneplus_io_para.iosize_write_count_fg++;
				oneplus_io_para.iosize_write_us_fg += delta_us;
				if (rq_is_sync(req))
				{
					if (delta_ms > 500)
					{
						oneplus_io_para.iosize_500ms_syncwrite_count_fg++;
					}
					else if (delta_ms > 200)
					{
						oneplus_io_para.iosize_200ms_syncwrite_count_fg++;
					}
				}
				else
				{
					if (delta_ms > 500)
					{
						oneplus_io_para.iosize_500ms_asyncwrite_count_fg++;
					}
					else if (delta_ms > 200)
					{
						oneplus_io_para.iosize_200ms_asyncwrite_count_fg++;
					}
				}
			}
			else
			{
				oneplus_io_para.iosize_read_count_fg++;
				oneplus_io_para.iosize_read_us_fg += delta_us;
				if (delta_ms > 500)
				{
					oneplus_io_para.iosize_500ms_read_count_fg++;
				}
				else if (delta_ms > 200)
				{
					oneplus_io_para.iosize_200ms_read_count_fg++;
				}
			}
		}
	}
	else
	{
		if ((req_op(req) != REQ_OP_DISCARD) && (req_op(req) != REQ_OP_SECURE_ERASE))
		{
			if (req_op(req) == REQ_OP_WRITE || req_op(req) == REQ_OP_WRITE_SAME)
			{
				oneplus_io_para.iosize_write_count_bg++;
				oneplus_io_para.iosize_write_us_bg += delta_us;
				if (rq_is_sync(req))
				{
					if (delta_ms > 2000)
					{
						oneplus_io_para.iosize_2s_syncwrite_count_bg++;
						if (oneplus_io_para.trig)
							ohm_action_trig(OHM_SCHED_EMMCIO);
					}
					else if (delta_ms > 500)
					{
						oneplus_io_para.iosize_500ms_syncwrite_count_bg++;
					}
					else if (delta_ms > 200)
					{
						oneplus_io_para.iosize_200ms_syncwrite_count_bg++;
					}
				}
				else
				{
					if (delta_ms > 2000)
					{
						oneplus_io_para.iosize_2s_asyncwrite_count_bg++;
						if (oneplus_io_para.trig)
							ohm_action_trig(OHM_SCHED_EMMCIO);
					}
					else if (delta_ms > 500)
					{
						oneplus_io_para.iosize_500ms_asyncwrite_count_bg++;
					}
					else if (delta_ms > 200)
					{
						oneplus_io_para.iosize_200ms_asyncwrite_count_bg++;
					}
				}
			}
			else
			{
				oneplus_io_para.iosize_read_count_bg++;
				oneplus_io_para.iosize_read_us_bg += delta_us;
				if (delta_ms > 2000)
				{
					oneplus_io_para.iosize_2s_read_count_bg++;
					if (oneplus_io_para.trig)
						ohm_action_trig(OHM_SCHED_EMMCIO);
				}
				else if (delta_ms > 500)
				{
					oneplus_io_para.iosize_500ms_read_count_bg++;
				}
				else if (delta_ms > 200)
				{
					oneplus_io_para.iosize_200ms_read_count_bg++;
				}
			}
		}
	}
	//4k
	if ((req_op(req) != REQ_OP_DISCARD) && (req_op(req) != REQ_OP_SECURE_ERASE))
	{
		if (req_op(req) == REQ_OP_WRITE || req_op(req) == REQ_OP_WRITE_SAME)
		{
			if (blk_rq_bytes(req) == 4096)
			{
				oneplus_io_para.iosize_4k_write_count++;
				oneplus_io_para.iosize_4k_write_us += delta_us;
			}
		}
		else
		{
			if (blk_rq_bytes(req) == 4096)
			{
				oneplus_io_para.iosize_4k_read_count++;
				oneplus_io_para.iosize_4k_read_us += delta_us;
			}
		}
	}
	oneplus_io_para.delta_ms = delta_ms;
	oneplus_io_para.total_us += delta_us;
	oneplus_io_para.emmc_total_us += req->flash_io_latency;
	oneplus_io_para.total_cnt++;

	return;
}

/****  Ctrl init  ****/
/*
				CTRL - TOTAL -32
				CTRL0:          logon
iowait record;

				CTRL1:
sched latency;

				CTRL2:          logon           trig
fsync record;

				CTRL3:
emmcio record;
				******
				******
				CTRL12:
cpu load cur;

				CTRL13:         logon           trig
mem mon;
......;
......;
				CTRL31:
......;
*/
#define OHM_LIST_MAGIC          0x5a000000
#define OHM_CTRL_MAX            32
#define OHM_INT_MAX             20
#define OHM_CTRL_IOWAIT         BIT(OHM_SCHED_IOWAIT)
#define OHM_CTRL_SCHEDLATENCY   BIT(OHM_SCHED_SCHEDLATENCY)
#define OHM_CTRL_FSYNC          BIT(OHM_SCHED_FSYNC)
#define OHM_CTRL_EMMCIO         BIT(OHM_SCHED_EMMCIO)
/*2020-06-22 ，OSP-5970 , monitor cpu info **/
#define OHM_CTRL_DSTATE         BIT(OHM_SCHED_DSTATE)
#define OHM_CTRL_SCHEDTOTAL     (OHM_CTRL_EMMCIO | OHM_CTRL_FSYNC | OHM_CTRL_SCHEDLATENCY | OHM_CTRL_IOWAIT | OHM_CTRL_DSTATE)
#define OHM_CTRL_CPU_CUR        BIT(OHM_CPU_LOAD_CUR)
#define OHM_CTRL_MEMMON         BIT(OHM_MEM_MON)
#define OHM_CTRL_IOPANIC_MON    BIT(OHM_IOPANIC_MON)


/*
ohm_ctrl_list    = 0x5a0fffff
ohm_logon_list = 0x5a002005
ohm_trig_list    = 0x5a002000
*/

/*Default*/
static int ohm_ctrl_list = OHM_LIST_MAGIC | OHM_CTRL_CPU_CUR | OHM_CTRL_MEMMON | OHM_CTRL_SCHEDTOTAL;
static int ohm_logon_list = OHM_LIST_MAGIC;
static int ohm_trig_list = OHM_LIST_MAGIC | OHM_CTRL_IOWAIT;

bool ohm_cpu_ctrl = true;
bool ohm_cpu_logon;
bool ohm_cpu_trig;

bool ohm_memmon_ctrl;
bool ohm_memmon_logon;
bool ohm_memmon_trig;

bool ohm_iopanic_mon_ctrl;
bool ohm_iopanic_mon_logon;
bool ohm_iopanic_mon_trig;
/*2020-06-22 ，OSP-5970 , monitor cpu info **/
bool ohm_irqsoff_ctrl = false;
bool ohm_preempt_ctrl = true;
bool ohm_rtinfo_ctrl = false;

/******  Para Update  *****/
#define LOW_THRESH_MS_DEFAULT   10
#define HIGH_THRESH_MS_DEFAULT  50
/* low thresh 10~1000ms*/
#define LOW_THRESH_MS_LOW       10
#define LOW_THRESH_MS_HIGH      1000
/* high thresh 100~5000ms*/
#define HIGH_THRESH_MS_LOW      50
#define HIGH_THRESH_MS_HIGH     5000

struct thresh_para {
	int l_ms;
	int h_ms;
};

struct thresh_para ohm_thresh_para[OHM_SCHED_TOTAL] = {
	{ LOW_THRESH_MS_DEFAULT, HIGH_THRESH_MS_DEFAULT},
	{ LOW_THRESH_MS_DEFAULT, HIGH_THRESH_MS_DEFAULT},
	{ LOW_THRESH_MS_DEFAULT, HIGH_THRESH_MS_DEFAULT},
	{ LOW_THRESH_MS_DEFAULT, HIGH_THRESH_MS_DEFAULT},
	{ LOW_THRESH_MS_DEFAULT, HIGH_THRESH_MS_DEFAULT},
};

void ohm_para_update(void)
{
	int i;
	for (i = 0; i < OHM_SCHED_TOTAL; i++) {
		if (ohm_thresh_para[i].l_ms < LOW_THRESH_MS_LOW
			|| ohm_thresh_para[i].l_ms > LOW_THRESH_MS_HIGH
			|| ohm_thresh_para[i].h_ms < HIGH_THRESH_MS_LOW
			|| ohm_thresh_para[i].h_ms > HIGH_THRESH_MS_HIGH) {
			/********** Legal Check **********/
			ohm_err("Para illegal: sched_type %s, l_ms %d, h_ms %d\n",
					sched_list[i], ohm_thresh_para[i].l_ms, ohm_thresh_para[i].h_ms);
			ohm_thresh_para[i].l_ms = LOW_THRESH_MS_DEFAULT;
			ohm_thresh_para[i].h_ms = HIGH_THRESH_MS_DEFAULT;
			return;
		}
		oneplus_sched_para[i].low_thresh_ms = ohm_thresh_para[i].l_ms;
		oneplus_sched_para[i].high_thresh_ms = ohm_thresh_para[i].h_ms;
	}
	ohm_debug("Success update ohm_para!\n");
}

/****  Init  ****/
static inline void ohm_rt_para_init(void)
{
		int i,j;

		for (i = 0; i < NR_CPUS; i++) {
			rt_para[i].each_cpu_rt = 0;
			rt_para[i].each_cpu_rt_total = 0;
	   		for (j = 0 ;j < 3 ; j++) {
               rt_para[i].thresh_cnt[j] = 0;
        	}
		rt_para[i].lt_info = (struct longest_task_info*)kzalloc(sizeof(struct longest_task_info), GFP_ATOMIC);
		}
		ohm_rtinfo_ctrl = true;
	   return;
}
static inline void ohm_preempt_para_init(void)
{
		int i;
		for(i = 0; i < NR_CPUS; i++){
			preempt_para[i].high_thresh_ms = 10;
			preempt_para[i].low_thresh_ms = 1;
				preempt_para[i].preempt_common.high_cnt = 0;
				preempt_para[i].preempt_common.low_cnt = 0;
				preempt_para[i].preempt_common.max_ms = 0;
				preempt_para[i].preempt_common.total_cnt = 0;
				preempt_para[i].preempt_common.total_ms = 0;
		}
		ohm_preempt_ctrl = true;
		return ;
}
static inline void _ohm_para_init(struct sched_stat_para *sched_para)
{
	   sched_para->delta_ms = 0;
	   memset(&sched_para->all, 0 , sizeof(struct sched_stat_common));
	   memset(&sched_para->ux, 0 , sizeof(struct sched_stat_common));
	   memset(&sched_para->fg, 0 , sizeof(struct sched_stat_common));

	   return;
}

void ohm_trig_init(void)
{
	int i;
	ohm_memmon_trig = (ohm_trig_list & OHM_CTRL_MEMMON) ? true : false;
	ohm_cpu_trig = (ohm_trig_list & OHM_CTRL_CPU_CUR) ? true : false;
	ohm_iopanic_mon_trig = (ohm_trig_list & OHM_CTRL_IOPANIC_MON) ? true : false;
	for (i = 0; i < OHM_SCHED_TOTAL; i++) {
		oneplus_sched_para[i].trig = (ohm_trig_list & BIT(i)) ? true : false;
		if(i == OHM_SCHED_EMMCIO )
			oneplus_io_para.trig = (ohm_trig_list & BIT(i)) ? true : false;
	}
	return;
}

void ohm_logon_init(void)
{
	int i;
	ohm_cpu_logon = (ohm_logon_list & OHM_CTRL_CPU_CUR) ? true : false;
	ohm_memmon_logon = (ohm_logon_list & OHM_CTRL_MEMMON) ? true : false;
	ohm_iopanic_mon_logon = (ohm_logon_list & OHM_CTRL_IOPANIC_MON) ? true : false;
	for (i = 0; i < OHM_SCHED_TOTAL; i++) {
		oneplus_sched_para[i].logon = (ohm_logon_list & BIT(i)) ? true : false;
		if(i == OHM_SCHED_EMMCIO )
			oneplus_io_para.logon = (ohm_logon_list & BIT(i)) ? true : false;
	}
	return;
}

void ohm_ctrl_init(void)
{
	int i;
	ohm_cpu_ctrl = (ohm_ctrl_list & OHM_CTRL_CPU_CUR) ? true : false;
	ohm_memmon_ctrl = (ohm_ctrl_list & OHM_CTRL_MEMMON) ? true : false;
	ohm_iopanic_mon_ctrl = (ohm_ctrl_list & OHM_CTRL_IOPANIC_MON) ? true : false;
	for (i = 0; i < OHM_SCHED_TOTAL; i++) {
		oneplus_sched_para[i].ctrl = (ohm_ctrl_list & BIT(i)) ? true : false;
		if(i == OHM_SCHED_EMMCIO )
			oneplus_io_para.ctrl = (ohm_ctrl_list & BIT(i)) ? true : false;
	}
	ohm_irqsoff_ctrl = true;
	return;
}

void ohm_para_init(void)
{
	int i;
	for (i = 0; i < OHM_SCHED_TOTAL; i++) {
		memset(&oneplus_sched_para[i], 0, sizeof(struct sched_stat_para));
		oneplus_sched_para[i].low_thresh_ms = LOW_THRESH_MS_DEFAULT;
		oneplus_sched_para[i].high_thresh_ms = HIGH_THRESH_MS_DEFAULT;
	}
	for (i = 0 ;i < OHM_SCHED_TOTAL ;i++ )
	_ohm_para_init(&oneplus_sched_para[i]);
	oneplus_sched_para[OHM_SCHED_EMMCIO].low_thresh_ms = LOW_THRESH_MS_DEFAULT;
	oneplus_sched_para[OHM_SCHED_EMMCIO].high_thresh_ms = HIGH_THRESH_MS_DEFAULT;

	oneplus_io_para.low_thresh_ms = 100;
	oneplus_io_para.high_thresh_ms = 200;

	for (i = 0 ;i < NR_CPUS ;i++ ){
                memset(&irq_latency_stat, 0 , sizeof(struct irq_latency_para));
                irq_latency_stat[i].high_thresh = 500;
                irq_latency_stat[i].low_thresh = 100;
        }
        ohm_rt_para_init();
        ohm_preempt_para_init();
	ohm_ctrl_init();
	ohm_logon_init();
	ohm_trig_init();
	ohm_debug("origin list: ctrl 0x%08x, logon 0x%08x, trig 0x%08x\n", ohm_ctrl_list, ohm_logon_list, ohm_trig_list);
	return;
}

/******  Cur cpuloading  ******/

static ssize_t cpu_load_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[BUFFER_SIZE_S] = {0};
	int len = 0;
	int load = ohm_get_cur_cpuload(ohm_cpu_ctrl);

	if (load < 0)
		load = 0;
	len = sprintf(page, "cur_cpuloading: %d\n""cur_cpu_ctrl: %s\n""cur_cpu_logon: %s\n""cur_cpu_trig: %s\n",
					load, (ohm_cpu_ctrl ? "true" : "false"), (ohm_cpu_logon ? "true" : "false"), (ohm_cpu_trig ? "true" : "false"));

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations proc_cpu_load_fops = {
	.read = cpu_load_read,
};

/******  Sched latency stat  *****/
static ssize_t sched_latency_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[1024] = {0};
	int len = 0;
	int type = OHM_SCHED_SCHEDLATENCY;

	len = sprintf(page, "sched_low_thresh_ms: %d\n""sched_high_thresh_ms: %d\n"
				"sched_all_low_cnt: %lld\n""sched_all_high_cnt: %lld\n"
				"sched_all_total_ms: %lld\n""sched_all_total_cnt: %lld\n"
				"sched_all_max_ms: %lld\n""sched_fg_low_cnt: %lld\n"
				"sched_fg_high_cnt: %lld\n""sched_fg_total_ms: %lld\n"
				"sched_fg_total_cnt: %lld\n""sched_fg_max_ms: %lld\n"
				"sched_delta_ms: %lld\n""sched_latency_ctrl: %s\n"
				"sched_latency_logon: %s\n""sched_latency_trig: %s\n",
				oneplus_sched_para[type].low_thresh_ms,
				oneplus_sched_para[type].high_thresh_ms,
				oneplus_sched_para[type].all.max_ms,
				oneplus_sched_para[type].all.high_cnt,
				oneplus_sched_para[type].all.low_cnt,
				oneplus_sched_para[type].all.total_ms,
				oneplus_sched_para[type].all.total_cnt,
				oneplus_sched_para[type].fg.max_ms,
				oneplus_sched_para[type].fg.high_cnt,
				oneplus_sched_para[type].fg.low_cnt,
				oneplus_sched_para[type].fg.total_ms,
				oneplus_sched_para[type].fg.total_cnt,
				oneplus_sched_para[type].delta_ms,
				oneplus_sched_para[type].ctrl ? "true" : "false",
				oneplus_sched_para[type].logon ? "true" : "false",
				oneplus_sched_para[type].trig ? "true" : "false");

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations proc_sched_latency_fops = {
	.read = sched_latency_read,
};

/****** Sched iowait stat  *****/
static ssize_t iowait_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[1024] = {0};
	int len = 0;
	int type = OHM_SCHED_IOWAIT;

	len = sprintf(page,"iowait_ctrl: %s\n""iowait_logon: %s\n""iowait_trig: %s\n" \
				"iowait_delta_ms: %u\n""iowait_low_thresh_ms: %u\n""iowait_high_thresh_ms: %u\n" \
				"iowait_all_max_ms: %llu\n""iowait_all_high_cnt: %llu\n""iowait_all_low_cnt: %llu\n" \
				"iowait_all_total_ms: %llu\n""iowait_all_total_cnt: %llu\n" \
				"iowait_fg_max_ms: %llu\n""iowait_fg_high_cnt: %llu\n""iowait_fg_low_cnt: %llu\n" \
				"iowait_fg_total_ms: %llu\n""iowait_fg_total_cnt: %llu\n", \
				oneplus_sched_para[type].ctrl ? "true":"false", \
				oneplus_sched_para[type].logon ? "true":"false", \
				oneplus_sched_para[type].trig ? "true":"false", \
				oneplus_sched_para[type].delta_ms, \
				oneplus_sched_para[type].low_thresh_ms, \
				oneplus_sched_para[type].high_thresh_ms, \
				oneplus_sched_para[type].all.max_ms, \
				oneplus_sched_para[type].all.high_cnt, \
				oneplus_sched_para[type].all.low_cnt, \
				oneplus_sched_para[type].all.total_ms, \
				oneplus_sched_para[type].all.total_cnt, \
				oneplus_sched_para[type].fg.max_ms, \
				oneplus_sched_para[type].fg.high_cnt, \
				oneplus_sched_para[type].fg.low_cnt, \
				oneplus_sched_para[type].fg.total_ms, \
				oneplus_sched_para[type].fg.total_cnt);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;

	return (len < count ? len : count);
}

static const struct file_operations proc_iowait_fops = {
	.read = iowait_read,
};

/****** Sched sync wait stat  ******/
static ssize_t fsync_wait_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[1024] = {0};
	int len = 0;
	int type = OHM_SCHED_FSYNC;

	len = sprintf(page, "fsync_ctrl: %s\n""fsync_logon: %s\n""fsync_trig: %s\n" \
				"fsync_delta_ms: %llu\n""fsync_low_thresh_ms: %u\n""fsync_high_thresh_ms: %u\n" \
				"fsync_all_max_ms: %llu\n""fsync_all_high_cnt: %llu\n""fsync_all_low_cnt: %llu\n" \
				"fsync_all_total_ms: %llu\n""fsync_all_total_cnt: %llu\n" \
				"fsync_fg_max_ms: %llu\n""fsync_fg_high_cnt: %llu\n""fsync_fg_low_cnt: %llu\n" \
				"fsync_fg_total_ms: %llu\n""fsync_fg_total_cnt: %llu\n", \
				oneplus_sched_para[type].ctrl ? "true":"false", \
				oneplus_sched_para[type].logon ? "true":"false", \
				oneplus_sched_para[type].trig ? "true":"false", \
				oneplus_sched_para[type].delta_ms, \
				oneplus_sched_para[type].low_thresh_ms, \
				oneplus_sched_para[type].high_thresh_ms, \
				oneplus_sched_para[type].all.max_ms, \
				oneplus_sched_para[type].all.high_cnt, \
				oneplus_sched_para[type].all.low_cnt, \
				oneplus_sched_para[type].all.total_ms, \
				oneplus_sched_para[type].all.total_cnt, \
				oneplus_sched_para[type].fg.max_ms, \
				oneplus_sched_para[type].fg.high_cnt, \
				oneplus_sched_para[type].fg.low_cnt, \
				oneplus_sched_para[type].fg.total_ms, \
				oneplus_sched_para[type].fg.total_cnt);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;

	return (len < count ? len : count);
}

static const struct file_operations proc_fsync_wait_fops = {
	.read = fsync_wait_read,
};

/****** emcdrv_iowait stat  ******/
/* Emmc - 1 ; Ufs - 2 */
int ohm_flash_type = OHM_FLASH_TYPE_UFS;
static ssize_t emmcio_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	int len = 0;
	char *page = kzalloc(2048, GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	//int type = OHM_SCHED_EMMCIO;
	len = sprintf(page, "emcdrv_iowait_low_thresh_ms: %d\n" //low thresh parameter
						"emcdrv_iowait_low_cnt: %lld\n"
						//high thresh parameter
						"emcdrv_iowait_high_thresh_ms: %d\n"
						"emcdrv_iowait_high_cnt: %lld\n"
						//total parameter
						"emcdrv_iowait_total_ms: %lld\n"
						"flashio_total_latency: %lld\n"
						"blockio_total_latency: %lld\n"
						"emcdrv_iowait_total_cnt: %lld\n"
						//fg latency parameter
						"emcdrv_iowait_fg_low_cnt: %lld\n"
						"emcdrv_iowait_fg_high_cnt: %lld\n"
						"emcdrv_iowait_fg_total_ms: %lld\n"
						"emcdrv_iowait_fg_total_cnt: %lld\n"
						"emcdrv_iowait_fg_max_ms: %lld\n"
						"emcdrv_iowait_delta_ms: %lld\n"
						// fg
						"iosize_write_count_fg: %lld\n"
						"iosize_write_us_fg: %lld\n"
						"iosize_500ms_syncwrite_count_fg: %lld\n"
						"iosize_200ms_syncwrite_count_fg: %lld\n"
						"iosize_500ms_asyncwrite_count_fg: %lld\n"
						"iosize_200ms_asyncwrite_count_fg: %lld\n"
						"iosize_read_count_fg: %lld\n"
						"iosize_read_us_fg: %lld\n"
						"iosize_500ms_read_count_fg: %lld\n"
						"iosize_200ms_read_count_fg: %lld\n"
						//bg
						"iosize_write_count_bg: %lld\n"
						"iosize_write_us_bg: %lld\n"
						"iosize_2s_asyncwrite_count_bg: %lld\n"
						"iosize_500ms_asyncwrite_count_bg: %lld\n"
						"iosize_200ms_asyncwrite_count_bg: %lld\n"
						"iosize_2s_syncwrite_count_bg: %lld\n"
						"iosize_500ms_syncwrite_count_bg: %lld\n"
						"iosize_200ms_syncwrite_count_bg: %lld\n"
						"iosize_read_count_bg: %lld\n"
						"iosize_read_us_bg: %lld\n"
						"iosize_2s_read_count_bg: %lld\n"
						"iosize_500ms_read_count_bg: %lld\n"
						"iosize_200ms_read_count_bg: %lld\n"
						//4k
						"iosize_4k_read_count: %lld\n"
						"iosize_4k_read_ms: %lld\n"
						"iosize_4k_write_count: %lld\n"
						"iosize_4k_write_ms: %lld\n"
						// option
						"emcdrv_iowait_ctrl: %s\n"
						"emcdrv_iowait_logon: %s\n"
						"emcdrv_iowait_trig: %s\n",
				  oneplus_io_para.low_thresh_ms, //low thresh parameter
				  oneplus_io_para.low_cnt,
				  //high thresh parameter
				  oneplus_io_para.high_thresh_ms,
				  oneplus_io_para.high_cnt,
				  //total parameter
				  (oneplus_io_para.total_us / 1000),
				  (oneplus_io_para.emmc_total_us / 1000),
				  (oneplus_io_para.total_us - oneplus_io_para.emmc_total_us) / 1000,
				  oneplus_io_para.total_cnt,
				  //fg latency parameter
				  oneplus_io_para.fg_low_cnt,
				  oneplus_io_para.fg_high_cnt,
				  oneplus_io_para.fg_total_ms,
				  oneplus_io_para.fg_total_cnt,
				  oneplus_io_para.fg_max_delta_ms,
				  oneplus_io_para.delta_ms,
				  //fg
				  oneplus_io_para.iosize_write_count_fg,
				  oneplus_io_para.iosize_write_us_fg,
				  oneplus_io_para.iosize_500ms_syncwrite_count_fg,
				  oneplus_io_para.iosize_200ms_syncwrite_count_fg,
				  oneplus_io_para.iosize_500ms_asyncwrite_count_fg,
				  oneplus_io_para.iosize_200ms_asyncwrite_count_fg,
				  oneplus_io_para.iosize_read_count_fg,
				  oneplus_io_para.iosize_read_us_fg,
				  oneplus_io_para.iosize_500ms_read_count_fg,
				  oneplus_io_para.iosize_200ms_read_count_fg,
				  //bg
				  oneplus_io_para.iosize_write_count_bg,
				  oneplus_io_para.iosize_write_us_bg,
				  oneplus_io_para.iosize_2s_asyncwrite_count_bg,
				  oneplus_io_para.iosize_500ms_asyncwrite_count_bg,
				  oneplus_io_para.iosize_200ms_asyncwrite_count_bg,
				  oneplus_io_para.iosize_2s_syncwrite_count_bg,
				  oneplus_io_para.iosize_500ms_syncwrite_count_bg,
				  oneplus_io_para.iosize_200ms_syncwrite_count_bg,
				  oneplus_io_para.iosize_read_count_bg,
				  oneplus_io_para.iosize_read_us_bg,
				  oneplus_io_para.iosize_2s_read_count_bg,
				  oneplus_io_para.iosize_500ms_read_count_bg,
				  oneplus_io_para.iosize_200ms_read_count_bg,
				  //4k
				  oneplus_io_para.iosize_4k_read_count,
				  (oneplus_io_para.iosize_4k_read_us / 1000),
				  oneplus_io_para.iosize_4k_write_count,
				  (oneplus_io_para.iosize_4k_write_us / 1000),
				  // option
				  oneplus_io_para.ctrl ? "true" : "false",
				  oneplus_io_para.logon ? "true" : "false",
				  oneplus_io_para.trig ? "true" : "false");

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		kfree(page);
		return -EFAULT;
	}
	kfree(page);
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations proc_emmcio_fops = {
	.read = emmcio_read,
};

/*2020-06-22 ，OSP-5970 , monitor cpu info **/
static inline ssize_t sched_data_to_user(char __user *buff, size_t count, loff_t *off, char *format_str, int len)
{
    if (len > *off) {
        len -= *off;
    } else {
        len = 0;
    }
    if (raw_copy_to_user(buff, format_str, (len < count ? len : count))) {
        return -EFAULT;
    }
    *off += len < count ? len : count;

    return (len < count ? len : count);
}

/****** dstat statistics  ******/

#define LATENCY_STRING_FORMAT(BUF, MODULE, SCHED_STAT) sprintf(BUF, \
        #MODULE"_ctrl: %s\n"#MODULE"_logon: %s\n"#MODULE"_trig: %s\n" \
        #MODULE"_delta_ms: %llu\n"#MODULE"_low_thresh_ms: %u\n"#MODULE"_high_thresh_ms: %u\n" \
        #MODULE"_all_max_ms: %llu\n"#MODULE"_all_high_cnt: %llu\n"#MODULE"_all_low_cnt: %llu\n" \
        #MODULE"_all_total_ms: %llu\n"#MODULE"_all_total_cnt: %llu\n" \
        #MODULE"_fg_max_ms: %llu\n"#MODULE"_fg_high_cnt: %llu\n"#MODULE"_fg_low_cnt: %llu\n" \
        #MODULE"_fg_total_ms: %llu\n"#MODULE"_fg_total_cnt: %llu\n", \
        SCHED_STAT->ctrl ? "true":"false", \
        SCHED_STAT->logon ? "true":"false", \
        SCHED_STAT->trig ? "true":"false", \
        SCHED_STAT->delta_ms, \
        SCHED_STAT->low_thresh_ms, \
        SCHED_STAT->high_thresh_ms, \
        SCHED_STAT->all.max_ms, \
        SCHED_STAT->all.high_cnt, \
        SCHED_STAT->all.low_cnt, \
        SCHED_STAT->all.total_ms, \
        SCHED_STAT->all.total_cnt, \
        SCHED_STAT->fg.max_ms, \
        SCHED_STAT->fg.high_cnt, \
        SCHED_STAT->fg.low_cnt, \
        SCHED_STAT->fg.total_ms, \
        SCHED_STAT->fg.total_cnt)

static ssize_t dstate_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
        char page[BUFFER_SIZE_L] = {0};
        int len = 0;
        int type = OHM_SCHED_DSTATE;

	struct sched_stat_para *sched_stat = &oneplus_sched_para[type];

	len = LATENCY_STRING_FORMAT(page, dstate, sched_stat);
	return sched_data_to_user(buff, count, off, page, len);
}

static const struct file_operations proc_dstate_fops = {
       .read = dstate_read,
};

/******  irqs latency  ******/
#define COMMON_STRING_FORMAT(BUF, MODULE, SCHED_STAT, NUM) sprintf(BUF, \
				"cpu%d_"#MODULE"_max: %llu\n""cpu%d_"#MODULE"_high_cnt: %llu\n""cpu%d_"#MODULE"_low_cnt: %llu\n" \
				"cpu%d_"#MODULE"_total: %llu\n""cpu%d_"#MODULE"_total_cnt: %llu\n", \
				NUM, \
				SCHED_STAT->max, \
				NUM, \
				SCHED_STAT->high_cnt, \
				NUM, \
				SCHED_STAT->low_cnt, \
				NUM, \
				SCHED_STAT->total, \
				NUM, \
				SCHED_STAT->total_cnt)

static ssize_t irq_latency_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
    int len = 0,i;
	struct irq_latency_para *sched_stat;
	char *page = kzalloc(2048,GFP_KERNEL);
    if (!page)
        return -ENOMEM;

	for ( i = 0; i < NR_CPUS; i++) {
		len += sprintf(page + len, "cpu%d:\n", i);
		sched_stat = &irq_latency_stat[i];
		len += COMMON_STRING_FORMAT(page + len, irq_latency, sched_stat, i);
	}

	len += sprintf(page+len, "ohm_irqsoff_ctrl:%s \n", ohm_irqsoff_ctrl ? "true":"false");

    return sched_data_to_user(buff, count, off, page, len);
}

static const struct file_operations proc_irq_latency_fops = {
       .read = irq_latency_read,
};

/****** Preempt state *****/
#define PREEMPT_STRING_FORMAT(BUF, SCHED_STAT, NUM) sprintf(BUF,\
				"cpu%d_preempt_max_ms: %llu\n""cpu%d_preempt_high_cnt: %llu\n""cpu%d_preempt_low_cnt: %llu\n" \
				"cpu%d_preempt_total_ms: %llu\n""cpu%d_preempt_total_cnt: %llu\n", \
				NUM, \
				SCHED_STAT.max_ms, \
				NUM, \
				SCHED_STAT.high_cnt, \
				NUM, \
				SCHED_STAT.low_cnt, \
				NUM, \
				SCHED_STAT.total_ms, \
				NUM, \
				SCHED_STAT.total_cnt)

static ssize_t preempt_latency_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	int len = 0,j;
	char *page = kzalloc(2048,GFP_KERNEL);
	if (!page)
        return -ENOMEM;

	len = sprintf(page, "Show preemp-latency stat on per-cpu \n""\nCpu count:%d\n",NR_CPUS);
		for ( j = 0; j < NR_CPUS; j++){
			len += sprintf(page+len,"cpu:%d\n", j);
			len += PREEMPT_STRING_FORMAT(page+len, preempt_para[j].preempt_common, j);
		}

	if (len > *off) {
			len -= *off;
	} else {
			len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		  kfree(page);
		  return -EFAULT;
	}
	kfree(page);
	*off += len < count ? len : count;
	return (len < count ? len : count);

}
static const struct file_operations proc_preempt_latency_fops = {
       .read = preempt_latency_read,
};

/****** RT Sched  stat *****/
void rt_thresh_times_record(struct task_struct *p, unsigned int cpu)
{
	u64 exec_runtime_ns = p->rtend_time - p->rtstart_time ;
	if(rt_para[cpu].lt_info->max_exec_ns < exec_runtime_ns && exec_runtime_ns > 0 && exec_runtime_ns < MAX_RT_EXEC ) {
		rt_para[cpu].lt_info->max_exec_ns	= exec_runtime_ns;
		strcpy(rt_para[cpu].lt_info->comment,p->comm);
	}
	if (exec_runtime_ns < MAX_RT_EXEC && (exec_runtime_ns) > DEFAULT_RT_HT){
	   rt_para[cpu].thresh_cnt[0]++;
	}
	else if ( DEFAULT_RT_HT> exec_runtime_ns && exec_runtime_ns > DEFAULT_RT_LT){
		rt_para[cpu].thresh_cnt[1]++;
	}
	else if ( DEFAULT_RT_LT> exec_runtime_ns && exec_runtime_ns > 0)
		rt_para[cpu].thresh_cnt[2]++;
	return ;
}
void rt_info_record(struct rt_rq *rt_rq, unsigned int cpu)
{
	if (!rt_rq)
		return;
	rt_para[cpu].each_cpu_rt = rt_rq->rt_time;
	return ;
}
void rt_total_record(u64 delta_exec, unsigned int cpu)
{
	 rt_para[cpu].each_cpu_rt_total += delta_exec;
	 return;
}
static char *thresh_list[3] = {"high","mid","low"};

static ssize_t cpu_rtime_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	int len = 0,i,j;
	char *page = kzalloc(2048,GFP_KERNEL);
	if (!page)
        	return -ENOMEM;

	len = sprintf(page, "Collect information about rt processes on per cpu \n""\nCpu count:%d\n""High_thresh_ns:%d\n""Low_thresh_ns:%d\n",
			NR_CPUS,DEFAULT_RT_HT,DEFAULT_RT_LT);
	for (i = 0; i < NR_CPUS; i++) {
		len += sprintf(page+len,"\ncpu%d: \n""cpu%d_total_runtimes_ns: %lld\n""cpu%d_curr_runtimes_ns: %lld\n""cpu%d_comm:%s \n"
		"cpu%d_exec_time_ns: %lld\n",i, i, rt_para[i].each_cpu_rt_total, i, rt_para[i].each_cpu_rt, i, rt_para[i].lt_info->comment, 
		i, rt_para[i].lt_info->max_exec_ns);
		for (j = 0; j < 3; j++){
			if(j != 0 )
				len += sprintf(page+len,"cpu%d_%s_thresh_count: %llu\n", i, thresh_list[j], rt_para[i].thresh_cnt[j]);
		else
			len += sprintf(page+len,"cpu%d_%s_thresh_count: %d\n", i, thresh_list[j], rt_para[i].thresh_cnt[j]);
                }
	}

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}
static const struct file_operations proc_cpu_rtime_stat_fops = {
	.read = cpu_rtime_read,
 };

/****** cpu overload  ******/

#define OVERLOAD_STRING_FORMAT(BUF, SCHED_STAT, NUM) sprintf(BUF, \
				"cluster%d_overload_delta_ms: %llu\n""cluster%d_overload_low_thresh_ms: %u\n" \
				"cluster%d_overload_high_thresh_ms: %u\n""cluster%d_overload_all_max_ms: %llu\n" \
				"cluster%d_overload_all_high_cnt: %llu\n""cluster%d_overload_all_low_cnt: %llu\n" \
				"cluster%d_overload_all_total_ms: %llu\n""cluster%d_overload_all_total_cnt: %llu\n" ,\
				NUM, \
				SCHED_STAT->delta_ms, \
				NUM, \
				SCHED_STAT->low_thresh_ms, \
				NUM, \
				SCHED_STAT->high_thresh_ms, \
				NUM, \
				SCHED_STAT->all.max_ms, \
				NUM, \
				SCHED_STAT->all.high_cnt, \
				NUM, \
				SCHED_STAT->all.low_cnt, \
				NUM, \
				SCHED_STAT->all.total_ms, \
				NUM, \
				SCHED_STAT->all.total_cnt)

static ssize_t overload_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[BUFFER_SIZE_L + BUFFER_SIZE_M] = {0};
	int len = 0;
	int nr_clusters = MAX_CLUSTERS;
	struct sched_cluster *cluster;
	len += sprintf(page + len, "nr_clusters:%d\n", nr_clusters);
	for_each_sched_cluster(cluster){
	    len += sprintf(page+len, "cluster%d:\n", cluster->id);
	    len += OVERLOAD_STRING_FORMAT(page+len, cluster->overload, cluster->id);
	}
	return sched_data_to_user(buff, count, off, page, len);
}
static const struct file_operations proc_overload_fops = {
	.read = overload_read,
};

/******  mem monitor read  ******/
#ifdef CONFIG_ONEPLUS_MEM_MONITOR
static ssize_t alloc_wait_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[1024] = {0};
	int len = 0;

	len = sprintf(page, "total_alloc_wait_h_cnt: %lld\n""total_alloc_wait_l_cnt: %lld\n"
				"fg_alloc_wait_h_cnt: %lld\n""fg_alloc_wait_l_cnt: %lld\n"
				"total_alloc_wait_max_ms: %lld\n""total_alloc_wait_max_order: %lld\n"
				"fg_alloc_wait_max_ms: %lld\n""fg_alloc_wait_max_order: %lld\n"
				"alloc_wait_ctrl: %s\n""alloc_wait_logon: %s\n""alloc_wait_trig: %s\n",
				allocwait_para.total_alloc_wait_h_cnt, allocwait_para.total_alloc_wait_l_cnt,
				allocwait_para.fg_alloc_wait_h_cnt, allocwait_para.fg_alloc_wait_l_cnt,
				allocwait_para.total_alloc_wait_max_ms, allocwait_para.total_alloc_wait_max_order,
				allocwait_para.fg_alloc_wait_max_ms, allocwait_para.fg_alloc_wait_max_order,
				ohm_memmon_ctrl ? "true" : "false", ohm_memmon_logon ? "true":"false", ohm_memmon_trig ? "true":"false");

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations proc_alloc_wait_fops = {
	.read = alloc_wait_read,
};
#endif /*CONFIG_ONEPLUS_MEM_MONITOR*/

/******  Proc para   ******/
static ssize_t ohm_para_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	len = sprintf(page, "action: %s\n""ctrl: 0x%08x\n""logon: 0x%08x\n""trig: 0x%08x\n",
					(ohm_action_ctrl ? "true":"false"), ohm_ctrl_list, ohm_logon_list, ohm_trig_list);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t ohm_para_write(struct file *file, const char __user *buff, size_t len, loff_t *ppos)
{
	char write_data[32] = {0};
	char ctrl_list[32] = {0};

	if (raw_copy_from_user(&write_data, buff, len)) {
		ohm_err("write error.\n");
		return -EFAULT;
	}
	write_data[len] = '\0';
	if (write_data[len - 1] == '\n') {
		write_data[len - 1] = '\0';
	}

	if (0 == strncmp(write_data, "ohmctrl", 7)) {
		strncpy(ctrl_list, &write_data[7], OHM_INT_MAX);
		ctrl_list[OHM_INT_MAX] = '\0';
		ohm_ctrl_list = (int)simple_strtol(ctrl_list, NULL, 10);
		ohm_ctrl_init();
	} else if (0 == strncmp(write_data, "ohmlogon", 8)) {
		strncpy(ctrl_list, &write_data[8], OHM_INT_MAX);
		ctrl_list[OHM_INT_MAX] = '\0';
		ohm_logon_list = (int)simple_strtol(ctrl_list, NULL, 10);
		ohm_logon_init();
	} else if (0 == strncmp(write_data, "ohmtrig", 7)) {
		strncpy(ctrl_list, &write_data[7], OHM_INT_MAX);
		ctrl_list[OHM_INT_MAX] = '\0';
		ohm_trig_list = (int)simple_strtol(ctrl_list, NULL, 10);
		ohm_trig_init();
	} else if (0 == strncmp(write_data, "ohmparaupdate", 13)) {
		ohm_para_update();
		return len;
	} else {
		ohm_err("input illegal\n");
		return -EFAULT;
	}
	ohm_debug("write: %s, set: %s, ctrl: 0x%08x, logon: 0x%08x, trig: 0x%08x\n",
				write_data, ctrl_list, ohm_ctrl_list, ohm_logon_list, ohm_trig_list);
	return len;
}

static const struct file_operations proc_para_fops = {
	.read = ohm_para_read,
	.write = ohm_para_write,
};

/******  iowait hung show  ******/
unsigned int  iowait_hung_cnt;
unsigned int  iowait_panic_cnt;
static ssize_t iowait_hung_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[1024] = {0};
	int len = 0;

	len = sprintf(page, "iowait_hung_cnt: %u\n""iowait_panic_cnt: %u\n"
				"ohm_iopanic_mon_ctrl: %s\n""ohm_iopanic_mon_logon: %s\n""ohm_iopanic_mon_trig: %s\n",
				iowait_hung_cnt, iowait_panic_cnt,
				(ohm_iopanic_mon_ctrl ? "true" : "false"), (ohm_iopanic_mon_logon ? "true" : "false"), (ohm_iopanic_mon_trig ? "true" : "false"));

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations proc_iowait_hung_fops = {
	.read = iowait_hung_read,
};

#ifdef CONFIG_ONEPLUS_TASKLOAD_INFO
/******  rw_overload show  ******/
static int rw_overload_show(struct seq_file *s, void *v)
{
	struct task_struct *p;
	struct task_struct *group;
	u64 window_index = sample_window.window_index;
	u64 timestamp = sample_window.timestamp;
	u64 spead;
	u64 task_index;
	bool index = ODD(window_index);
	seq_printf(s, "window_index:%llu timestamp:%llu\n", window_index, timestamp);
	seq_printf(s, "%-10s\t%-10s\t%-16s\t%-16s\t%-8s\t%-16s\n", "TID", "TGID", "COMM", "spead", "r/w", "task_index");
	rcu_read_lock();
	do_each_thread(group, p) {
		if (window_index != (p->tli[!index].task_sample_index + 1))
			continue;
		if (p->tli[!index].tli_overload_flag & TASK_WRITE_OVERLOAD_FLAG) {
			spead = p->tli[!index].write_bytes;
			task_index = p->tli[!index].task_sample_index;
			seq_printf(s, "%-10d\t%-10d\t%-16s\t%-16llu\t%-8s\t%-16llu\n", p->pid, p->tgid, p->comm, spead, "write", task_index);
		}
		if (p->tli[!index].tli_overload_flag & TASK_READ_OVERLOAD_FLAG) {
			spead = p->tli[!index].read_bytes;
			task_index = p->tli[!index].task_sample_index;
			seq_printf(s, "%-10d\t%-10d\t%-16s\t%-16llu\t%-8s\t%-16llu\n", p->pid, p->tgid, p->comm, spead, "read", task_index);
		}
	} while_each_thread(group, p);
	rcu_read_unlock();
	return 0;
}

static int rw_overload_open(struct inode *inode, struct file *file)
{
	return single_open(file, rw_overload_show, NULL);
}

static const struct file_operations proc_rw_overload_fops = {
	.open = rw_overload_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/******  runtime_overload show  ******/
static int runtime_overload_show(struct seq_file *s, void *v)
{
	struct task_struct *p;
	struct task_struct *group;
	u64 window_index = sample_window.window_index;
	u64 timestamp = sample_window.timestamp;
	u64 runtime;
	u64 task_index;
	u64 rt;
	bool index = ODD(window_index);
	seq_printf(s, "window_index:%llu timestamp:%llu\n", window_index, timestamp);
	seq_printf(s, "%-10s\t%-10s\t%-16s\t%-16s\t%-6s\t%-6s\t%-16s\n", "TID", "TGID", "COMM", "runtime", "FG/BG", "RT", "task_index");
	do_each_thread(group, p) {
		if (window_index != (p->tli[!index].task_sample_index + 1))
			continue;
		rt = p->tli[!index].tli_overload_flag & TASK_RT_THREAD_FLAG;
		if (p->tli[!index].tli_overload_flag & TASK_CPU_OVERLOAD_FG_FLAG) {
			runtime = p->tli[!index].runtime[1];
			task_index = p->tli[!index].task_sample_index;
			seq_printf(s, "%-10d\t%-10d\t%-16s\t%-16llu\t%-6s\t%-6s\t%-16llu\n", p->pid, p->tgid, p->comm, runtime, "FG", rt?"YES":"NO", task_index);
		}
		if (p->tli[!index].tli_overload_flag & TASK_CPU_OVERLOAD_BG_FLAG) {
			runtime = p->tli[!index].runtime[0];
			task_index = p->tli[!index].task_sample_index;
			seq_printf(s, "%-10d\t%-10d\t%-16s\t%-16llu\t%-6s\t%-6s\t%-16llu\n", p->pid, p->tgid, p->comm, runtime, "BG", rt?"YES":"NO", task_index);
		}
	} while_each_thread(group, p);
	return 0;
}

static int runtime_overload_open(struct inode *inode, struct file *file)
{
	return single_open(file, runtime_overload_show, NULL);
}

static const struct file_operations proc_runtime_overload_fops = {
	.open = runtime_overload_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif
/******  End  ******/

#define HEALTHINFO_PROC_NODE "oneplus_healthinfo"
static struct proc_dir_entry *oneplus_healthinfo;

#ifdef CONFIG_ONEPLUS_TASKLOAD_INFO
static void adjust_window() {
	sample_window.timestamp = jiffies_64;
	sample_window.window_index++;
	mod_timer(&task_load_info_timer, jiffies + ohm_sample_time*HZ);  /* 5s */
}
#endif

static int __init oneplus_healthinfo_init(void)
{
	int ret = 0;
	struct proc_dir_entry *pentry;

	ohm_para_init();
	ohm_action_init();
	oneplus_healthinfo =  proc_mkdir(HEALTHINFO_PROC_NODE, NULL);
	iowait_total = 0;
	iowait_summ_start = jiffies;
	iowait_summ_period = 100;
	iowait_summ_thresh = 20;
	if (!oneplus_healthinfo) {
		ohm_err("can't create oneplus_healthinfo proc\n");
		goto ERROR_INIT_VERSION;
	}
/******  ctrl  *****/
	pentry = proc_create("para_update", S_IRUGO | S_IWUGO, oneplus_healthinfo, &proc_para_fops);
	if (!pentry) {
		ohm_err("create healthinfo_switch proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

/******  Stat  ******/
	pentry = proc_create("fsync_wait", S_IRUGO, oneplus_healthinfo, &proc_fsync_wait_fops);
	if (!pentry) {
		ohm_err("create fsync_wait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("cpu_loading", S_IRUGO, oneplus_healthinfo, &proc_cpu_load_fops);
	if (!pentry) {
		ohm_err("create cpu_loading proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("iowait", S_IRUGO, oneplus_healthinfo, &proc_iowait_fops);
	if (!pentry) {
		ohm_err("create iowait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("sched_latency", S_IRUGO, oneplus_healthinfo, &proc_sched_latency_fops);
	if (!pentry) {
		ohm_err("create sched_latency proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("emcdrv_iowait", S_IRUGO, oneplus_healthinfo, &proc_emmcio_fops);
	if (!pentry) {
		ohm_err("create emmc_driver_io_wait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("iowait_hung", S_IRUGO, oneplus_healthinfo, &proc_iowait_hung_fops);
	if (!pentry) {
		ohm_err("create iowait_hung proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

/*2020-06-22 ，OSP-5970 , monitor cpu info **/
	pentry = proc_create("dstate", S_IRUGO, oneplus_healthinfo, &proc_dstate_fops);
	if (!pentry) {
                ohm_err("create dstate proc failed.\n");
                goto ERROR_INIT_VERSION;
        }

	pentry = proc_create("overload", S_IRUGO, oneplus_healthinfo, &proc_overload_fops);
	if(!pentry) {
		ohm_err("create overload proc failed.\n");
		goto ERROR_INIT_VERSION;
	}
	pentry = proc_create("cpu_rt_info", S_IRUGO, oneplus_healthinfo, &proc_cpu_rtime_stat_fops);
	if(!pentry) {
		ohm_err("create cpu_rtime_read proc failed.\n");
		goto ERROR_INIT_VERSION;
	}
	pentry = proc_create("preempt_latency", S_IRUGO, oneplus_healthinfo, &proc_preempt_latency_fops);
		if(!pentry) {
			ohm_err("create preempt latency proc failed.\n");
			goto ERROR_INIT_VERSION;
	}
	pentry = proc_create("irq_latency", S_IRUGO, oneplus_healthinfo, &proc_irq_latency_fops);
		if(!pentry) {
			ohm_err("create irq_latency proc failed.\n");
			goto ERROR_INIT_VERSION;
	}

#ifdef CONFIG_ONEPLUS_MEM_MONITOR
	pentry = proc_create("alloc_wait", S_IRUGO, oneplus_healthinfo, &proc_alloc_wait_fops);
	if (!pentry) {
		ohm_err("create alloc_wait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

#endif /*CONFIG_ONEPLUS_MEM_MONITOR*/
#ifdef CONFIG_ONEPLUS_TASKLOAD_INFO
	sample_window.timestamp = jiffies;
	sample_window.window_index = 0;
	timer_setup(&task_load_info_timer, NULL, TIMER_DEFERRABLE);
	task_load_info_timer.function = &adjust_window;
	task_load_info_timer.expires = jiffies + ohm_sample_time*HZ;
	add_timer(&task_load_info_timer);

	pentry = proc_create("rw_overload", S_IRUGO, oneplus_healthinfo, &proc_rw_overload_fops);
	if (!pentry) {
		ohm_err("create rw_overload proc failed.\n");
		goto ERROR;
	}

	pentry = proc_create("runtime_overload", S_IRUGO, oneplus_healthinfo, &proc_runtime_overload_fops);
	if (!pentry) {
		ohm_err("create runtime_overload proc failed.\n");
		goto ERROR;
	}
#endif

	ohm_debug("Success \n");
	return ret;
#ifdef CONFIG_ONEPLUS_TASKLOAD_INFO
ERROR:
	del_timer(&task_load_info_timer);
#endif
ERROR_INIT_VERSION:
	remove_proc_entry(HEALTHINFO_PROC_NODE, NULL);
	return -ENOENT;
}

module_init(oneplus_healthinfo_init);

module_param_named(ohm_action_ctrl, ohm_action_ctrl, bool, S_IRUGO | S_IWUSR);
module_param_named(ohm_iowait_l_ms, oneplus_sched_para[OHM_SCHED_IOWAIT].low_thresh_ms, int, S_IRUGO | S_IWUSR);
module_param_named(ohm_iowait_h_ms, oneplus_sched_para[OHM_SCHED_IOWAIT].high_thresh_ms, int, S_IRUGO | S_IWUSR);
module_param_named(ohm_schedlatency_l_ms, ohm_thresh_para[OHM_SCHED_SCHEDLATENCY].l_ms, int, S_IRUGO | S_IWUSR);
module_param_named(ohm_schedlatency_h_ms, ohm_thresh_para[OHM_SCHED_SCHEDLATENCY].h_ms, int, S_IRUGO | S_IWUSR);
module_param_named(ohm_fsync_l_ms, ohm_thresh_para[OHM_SCHED_FSYNC].l_ms, int, S_IRUGO | S_IWUSR);
module_param_named(ohm_fsync_h_ms, ohm_thresh_para[OHM_SCHED_FSYNC].h_ms, int, S_IRUGO | S_IWUSR);
module_param_named(ohm_emmcio_l_ms, ohm_thresh_para[OHM_SCHED_EMMCIO].l_ms, int, S_IRUGO | S_IWUSR);
module_param_named(ohm_emmcio_h_ms, ohm_thresh_para[OHM_SCHED_EMMCIO].h_ms, int, S_IRUGO | S_IWUSR);
module_param_named(iowait_summ_period, iowait_summ_period, int, S_IRUGO | S_IWUSR);
module_param_named(iowait_summ_thresh, iowait_summ_thresh, int, S_IRUGO | S_IWUSR);
#ifdef CONFIG_ONEPLUS_TASKLOAD_INFO
module_param_named(ohm_write_thresh, ohm_write_thresh, ullong, S_IRUGO | S_IWUSR);
module_param_named(ohm_read_thresh, ohm_read_thresh, ullong, S_IRUGO | S_IWUSR);
module_param_named(ohm_runtime_thresh_fg, ohm_runtime_thresh_fg, ullong, S_IRUGO | S_IWUSR);
module_param_named(ohm_runtime_thresh_bg, ohm_runtime_thresh_bg, ullong, S_IRUGO | S_IWUSR);
module_param_named(ohm_sample_time, ohm_sample_time, uint, S_IRUGO | S_IWUSR);
#endif

MODULE_DESCRIPTION("OnePlus healthinfo monitor");
MODULE_LICENSE("GPL v2");
