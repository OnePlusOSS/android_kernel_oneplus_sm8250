#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/ioctl.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/cdev.h>
#include <trace/events/power.h>
#include <linux/oem/control_center.h>
#include <linux/oem/houston.h>
#include <linux/oem/im.h>

#ifdef CONFIG_OPCHAIN
#include <oneplus/uxcore/opchain_helper.h>
#include <linux/oem/opchain_define.h>
#endif

#include <linux/sched/core_ctl.h>

/* time measurement */
#define CC_TIME_START(start) { \
	if (cc_time_measure) \
		start = ktime_get(); \
}
#define CC_TIME_END(start, end, t, tmax) { \
	if (cc_time_measure) { \
		end = ktime_get(); \
		t = ktime_to_us(ktime_sub(end, begin)); \
		if (t > tmax) \
			tmax = t; \
		cc_logv("%s: cost: %lldus, max: %lldus\n", __func__, t, tmax); \
	}\
}
static bool cc_time_measure = true;
module_param_named(time_measure, cc_time_measure, bool, 0644);

/* boost enable options */
static bool cc_cpu_boost_enable = true;
module_param_named(cpu_boost_enable, cc_cpu_boost_enable, bool, 0644);

static bool cc_ddr_boost_enable = true;
module_param_named(ddr_boost_enable, cc_ddr_boost_enable, bool, 0644);

bool cc_ddr_boost_enabled(void)
{
	return cc_ddr_boost_enable;
}

static bool cc_fps_boost_enable = true;
module_param_named(fps_boost_enable, cc_fps_boost_enable, bool, 0644);

/* turbo boost */
static bool cc_tb_freq_boost_enable = true;
module_param_named(tb_freq_boost_enable, cc_tb_freq_boost_enable, bool, 0644);

static bool cc_tb_place_boost_enable = true;
module_param_named(tb_place_boost_enable, cc_tb_place_boost_enable, bool, 0644);

static bool cc_tb_nice_last_enable = false;
module_param_named(tb_nice_last_enable, cc_tb_nice_last_enable, bool, 0644);

static int cc_tb_nice_last_period = 10; /* 10 jiffies equals to 40 ms */
module_param_named(tb_nice_last_period, cc_tb_nice_last_period, int, 0644);

static int cc_tb_idle_block_enable = true;
static int cc_tb_idle_block_enable_store(const char *buf,
		const struct kernel_param *kp)
{
	unsigned int val;
	int i;

	if (sscanf(buf, "%u\n", &val) <= 0)
		return 0;

	cc_tb_idle_block_enable = !!val;
	if (!cc_tb_idle_block_enable) {
		for (i = CCDM_TB_CPU_0_IDLE_BLOCK; i <= CCDM_TB_CPU_7_IDLE_BLOCK; ++i)
			ccdm_update_hint_1(i, ULLONG_MAX);
	}

	return 0;
}

static int cc_tb_idle_block_enable_show(char *buf,
		const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", cc_tb_idle_block_enable);
}

static struct kernel_param_ops cc_tb_idle_block_enable_ops = {
	.set = cc_tb_idle_block_enable_store,
	.get = cc_tb_idle_block_enable_show,
};
module_param_cb(tb_idle_block_enable, &cc_tb_idle_block_enable_ops, NULL, 0644);

static int cc_tb_idle_block_period = 10; /* 10 jiffies equals to 40 ms */
module_param_named(tb_idle_block_period, cc_tb_idle_block_period, int, 0644);

static unsigned long cc_expect_ddrfreq;
module_param_named(expect_ddrfreq, cc_expect_ddrfreq, ulong, 0644);

unsigned long cc_get_expect_ddrfreq(void)
{
	return cc_expect_ddrfreq;
}

/* statistics for control operations */
struct cc_stat {
	atomic64_t cnt[CC_CTL_CATEGORY_MAX];
	atomic64_t tcnt[CC_CTL_CATEGORY_MAX];
} cc_stat;

static inline void cc_stat_inc(int idx)
{
	if (likely(idx >= 0 && idx < CC_CTL_CATEGORY_MAX))
		atomic64_inc(&cc_stat.cnt[idx]);
}

/* record */
static struct cc_record {
	spinlock_t lock;
	/* priority list */
	struct list_head phead[CC_PRIO_MAX];
} cc_record[CC_CTL_CATEGORY_MAX];

/*
 * verbose output
 * lv: 0 -> verbose
 * lv: 1 -> info
 * lv: 2 -> wraning
 * lv: 3 -> error
 */
static int cc_log_lv = 1;
module_param_named(log_lv, cc_log_lv, int, 0644);

static const char *cc_category_tags[CC_CTL_CATEGORY_MAX];
static const char *cc_category_tags_mapping(int idx);

#define CC_SYSTRACE_DEBUG 0
#if CC_SYSTRACE_DEBUG
#define CC_TSK_SYSTRACE_MAGIC 80000
#define CC_SYSTRACE_MAGIC 90000

/* systrace trace marker */
static int cc_systrace_enable = 0;
module_param_named(systrace_enable, cc_systrace_enable, int, 0644);

static inline void tracing_mark_write(struct cc_command *cc, int count, bool tsk)
{
	if (cc_systrace_enable) {
		if (tsk) {
			if (cc_systrace_enable == 2) {
				int pid = cc->bind_leader ? cc->leader: cc->pid;
				trace_printk("C|%d|%s-%d|%d\n",
					CC_TSK_SYSTRACE_MAGIC + cc->category, cc_category_tags_mapping(cc->category), pid, count);
			} else
				trace_printk("C|%d|%s|%d\n",
					CC_TSK_SYSTRACE_MAGIC + cc->category, cc_category_tags_mapping(cc->category), count);
		} else {
			if (cc_systrace_enable == 2) {
				int pid = cc->bind_leader ? cc->leader : cc->pid;
				trace_printk("C|%d|%s-%d|%d\n",
					CC_SYSTRACE_MAGIC + cc->category, cc_category_tags_mapping(cc->category), pid, count);
			} else
				trace_printk("C|%d|%s|%d\n",
					CC_SYSTRACE_MAGIC + cc->category, cc_category_tags_mapping(cc->category), count);
		}
	}
}
#else
static inline void tracing_mark_write(struct cc_command *cc, int count, bool tsk) {}
#endif

static int cc_tb_cctl_boost_enable = true;
static int cc_tb_cctl_boost_enable_store(const char *buf,
		const struct kernel_param *kp)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) <= 0)
		return 0;

	cc_tb_cctl_boost_enable = !!val;

	return 0;
}

static int cc_tb_cctl_boost_enable_show(char *buf,
		const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", cc_tb_cctl_boost_enable);
}

static struct kernel_param_ops cc_tb_cctl_boost_enable_ops = {
	.set = cc_tb_cctl_boost_enable_store,
	.get = cc_tb_cctl_boost_enable_show,
};
module_param_cb(tb_cctl_boost_enable, &cc_tb_cctl_boost_enable_ops, NULL, 0644);

static void cc_queue_rq(struct cc_command *cc);

/* boost ts information */
static struct cc_boost_ts cbt[CC_BOOST_TS_SIZE];
static int boost_ts_idx = 0;
static DEFINE_SPINLOCK(boost_ts_lock);

static inline bool cc_is_reset(struct cc_command *cc)
{
	return cc->type == CC_CTL_TYPE_RESET ||
		cc->type == CC_CTL_TYPE_RESET_NONBLOCK;
}

static inline bool cc_is_query(int category)
{
	return category >= CC_CTL_CATEGORY_CLUS_0_FREQ_QUERY
		&& category <= CC_CTL_CATEGORY_DDR_FREQ_QUERY;
}

static inline bool cc_is_nonblock(struct cc_command *cc)
{
	bool nonblock = false;

	if (cc->type >= CC_CTL_TYPE_ONESHOT_NONBLOCK) {
		nonblock = true;
		cc->type -= CC_CTL_TYPE_ONESHOT_NONBLOCK;
		cc_queue_rq(cc);
	}
	return nonblock;
}

static inline void cc_remove_nonblock(struct cc_command *cc)
{
	if (cc->type >= CC_CTL_TYPE_ONESHOT_NONBLOCK)
		cc->type -= CC_CTL_TYPE_ONESHOT_NONBLOCK;
}

/* calling with lock held */
static int boost_ts_get_idx(void) {
	int idx = boost_ts_idx++;
	return idx % CC_BOOST_TS_SIZE;
}

static void cc_boost_ts_update(struct cc_command* cc)
{
	u64 ts_us = ktime_to_us(ktime_get());
	int idx = 0;
	bool reset = cc_is_reset(cc);

	if (cc->category != CC_CTL_CATEGORY_CLUS_1_FREQ &&
		cc->category != CC_CTL_CATEGORY_TB_FREQ_BOOST)
		return;

	if (cc->category == CC_CTL_CATEGORY_CLUS_1_FREQ) {
		cc_logv("[%s] boost from %u group %u category %u type %u period %u min %llu max %llu\n",
			reset ? "Exit" : "Enter",
			cc->bind_leader ? cc->leader : cc->pid,
			cc->group, cc->category, cc->type, cc->period_us, cc->params[0], cc->params[1]);
	}
	else if (cc->category == CC_CTL_CATEGORY_TB_FREQ_BOOST) {
		cc_logv(
			"[%s] turbo boost from %u group %u category %u type %u period %u hint %llu %llu %llu %llu\n",
			reset ? "Exit" : "Enter",
			cc->bind_leader ? cc->leader : cc->pid,
			cc->group, cc->category, cc->type, cc->period_us,
			cc->params[0], cc->params[1],
			cc->params[2], cc->params[3]);
	}

	spin_lock(&boost_ts_lock);

	idx = boost_ts_get_idx();
	cbt[idx].pid = cc->bind_leader? cc->leader: cc->pid;
	cbt[idx].type = reset? 0: 1;
	cbt[idx].ts_us = ts_us;
	cbt[idx].min = cc->params[0];
	cbt[idx].max = cc->params[1];
	spin_unlock(&boost_ts_lock);
}

void cc_boost_ts_collect(struct cc_boost_ts* source)
{
	spin_lock(&boost_ts_lock);
	memcpy(source, cbt, sizeof(struct cc_boost_ts) * CC_BOOST_TS_SIZE);
	memset(cbt, 0, sizeof(struct cc_boost_ts) * CC_BOOST_TS_SIZE);
	boost_ts_idx = 0;
	spin_unlock(&boost_ts_lock);
}

/* cpufreq boost qos */
enum cc_cpufreq_boost_lv {
	CC_CPUFREQ_BOOST_LV_0 = 0,
	CC_CPUFREQ_BOOST_LV_1,
	CC_CPUFREQ_BOOST_LV_2,
	CC_CPUFREQ_BOOST_LV_3,
	CC_CPUFREQ_BOOST_LV_4,

	CC_CPUFREQ_BOOST_LV_MAX
};

/* boost timestamp */

/* async work */
#define CC_ASYNC_RQ_MAX (64)
static struct cc_async_rq {
	struct cc_command cc;
	struct list_head node;
	struct work_struct work;
	int idx;
} cc_async_rq[CC_ASYNC_RQ_MAX];

static struct task_struct *cc_worker_task;
static struct list_head cc_request_list;
static struct list_head cc_pending_list;
static DEFINE_SPINLOCK(cc_async_lock);
static struct workqueue_struct *cc_wq;
extern cc_cal_next_freq_with_extra_util(
	struct cpufreq_policy *pol, unsigned int next_freq);
extern void clk_get_ddr_freq(u64* val);
static void cc_queue_rq(struct cc_command *cc);

static void __adjust_cpufreq(
	struct cpufreq_policy *pol, u32 min, u32 max, bool reset)
{
	u32 req_freq = pol->req_freq;
	u32 orig_req_freq = pol->req_freq;
	u32 next_freq = pol->req_freq;
	u32 cpu;

	spin_lock(&pol->cc_lock);

	/* quick check */
	if (pol->cc_max == max && pol->cc_min == min && !reset) {
		spin_unlock(&pol->cc_lock);
		goto out;
	}

	/* cc max/min always inside current pol->max/min */
	pol->cc_max = (pol->max >= max)? max: pol->max;
	pol->cc_min = (pol->min <= min)? min: pol->min;
	if (reset)
		req_freq = pol->req_freq;
	else
		req_freq = clamp_val(req_freq, pol->cc_min, pol->cc_max);

	spin_unlock(&pol->cc_lock);

	/* not update while current governor is not schedutil */
	if (unlikely(!pol->cc_enable))
		goto out;

	/* trigger frequency change */
	if (pol->fast_switch_enabled) {
		next_freq = cpufreq_driver_fast_switch(pol, req_freq);
		if (!next_freq || (next_freq == pol->cur))
			goto out;

		/* update cpufreq stat */
		pol->cur = next_freq;
		for_each_cpu(cpu, pol->cpus)
			trace_cpu_frequency(next_freq, cpu);
		cpufreq_stats_record_transition(pol, next_freq);
	} else {
		cpufreq_driver_target(pol, req_freq, CPUFREQ_RELATION_H);
	}
out:
	cc_logv("cc_max: %u, cc_min: %u, target: %u, orig: %u, cur: %u, gov: %d\n",
		pol->cc_max, pol->cc_min, req_freq, orig_req_freq, next_freq, pol->cc_enable);
}

/* called with get_online_cpus() */
static inline int cc_get_online_cpu(int start, int end)
{
	int idx = -1;
	for (idx = start; idx <= end; ++idx)
		if (cpu_online(idx))
			break;
	return idx;
}

static inline int cc_get_cpu_idx(int cluster)
{
	switch (cluster) {
	case 0: return cc_get_online_cpu(0, 3);
	case 1: return cc_get_online_cpu(4, 6);
	case 2: return cc_get_online_cpu(7, 7);
	}
	return -1;
}

static int __cc_adjust_cpufreq(
	u32 clus, u32 min, u32 max, bool reset)
{
	struct cpufreq_policy *pol;
	int idx;
	int ret = 0;

	get_online_cpus();

	idx = cc_get_cpu_idx(clus);
	if (idx == -1) {
		cc_logw("can' get cpu idx, input cluster %u\n", clus);
		ret = -1;
		goto out;
	}

	pol = cpufreq_cpu_get(idx);
	if (!pol) {
		ret = -1;
		cc_logw("can't get cluster %d cpufreqp policy\n", idx);
		goto out;
	}

	__adjust_cpufreq(pol, min, max, reset);

	cpufreq_cpu_put(pol);
out:
	put_online_cpus();
	return ret;
}

static void cc_adjust_cpufreq(struct cc_command* cc)
{
	u32 clus;

	if (!cc_cpu_boost_enable)
		return;

	if (cc_is_nonblock(cc))
		return;

	switch (cc->category) {
	case CC_CTL_CATEGORY_CLUS_0_FREQ: clus = 0; break;
	case CC_CTL_CATEGORY_CLUS_1_FREQ: clus = 1; break;
	case CC_CTL_CATEGORY_CLUS_2_FREQ: clus = 2; break;
	default:
		cc_logw("cpufreq query invalid, category %u\n", cc->category);
		return;
	}

	/* for min/max approach */
	if (cc->params[3] == 0) {
		u32 min;
		u32 max;
		bool reset = false;

		if (cc_is_reset(cc)) {
			min = 0;
			max = UINT_MAX;
			reset = true;
		} else {
			/* ONESHOT/PERIOD */
			min = cc->params[0];
			max = cc->params[1];
			/* validate parameters */
			if (min > max) {
				cc_logw("cpufrq incorrect, min %u, max %u\n", min, max);
				return;
			}
		}

		cc->status = __cc_adjust_cpufreq(clus, min, max, reset);
	} else if (cc->params[3] == 1) {
		/* for extra util */
		struct cpufreq_policy *pol;
		int cpu;
		unsigned int next_freq;
		u64 val = cc->params[0];

		if (!cc_tb_freq_boost_enable)
			return;

		if (cc_is_reset(cc)) {
			ccdm_update_hint_1(CCDM_TB_CLUS_0_FREQ_BOOST, 0);
			ccdm_update_hint_1(CCDM_TB_CLUS_1_FREQ_BOOST, 0);
			ccdm_update_hint_1(CCDM_TB_CLUS_2_FREQ_BOOST, 0);
		} else {
			switch (clus) {
			case 0:
				ccdm_update_hint_1(CCDM_TB_CLUS_0_FREQ_BOOST, val);
				break;
			case 1:
				ccdm_update_hint_1(CCDM_TB_CLUS_1_FREQ_BOOST, val);
				break;
			case 2:
				ccdm_update_hint_1(CCDM_TB_CLUS_2_FREQ_BOOST, val);
				break;
			}
		}

		get_online_cpus();
		/* force trigger cpufreq change */
		pol = cpufreq_cpu_get(cc_get_cpu_idx(clus));
		if (unlikely(!pol)) {
			put_online_cpus();
			return;
		}

		if (unlikely(!pol->cc_enable))
			goto out;

		/* trigger frequency change */
		next_freq =
			cc_cal_next_freq_with_extra_util(pol, pol->req_freq);

		/* reset cc_min/max */
		spin_lock(&pol->cc_lock);
		pol->cc_max = pol->max;
		pol->cc_min = pol->min;
		spin_unlock(&pol->cc_lock);

		if (pol->fast_switch_enabled) {
			next_freq = cpufreq_driver_fast_switch(pol, next_freq);
			if (!next_freq || (next_freq == pol->cur))
				goto out;

			/* update cpufreq stat */
			pol->cur = next_freq;
			for_each_cpu(cpu, pol->cpus)
				trace_cpu_frequency(next_freq, cpu);
			cpufreq_stats_record_transition(pol, next_freq);
		} else {
			cpufreq_driver_target(pol, next_freq, CPUFREQ_RELATION_H);
		}
out:
		cpufreq_cpu_put(pol);
		put_online_cpus();
	}
}

/* to change ai predict ddrfreq to devfreq */
static inline u64 cc_ddr_to_devfreq(u64 val)
{
	int i;
	u64 ddr_devfreq_avail_freq[] = { 0, 2597, 2929, 3879, 5161, 5931, 6881, 7980, 10437 };
	u64 ddr_aop_mapping_freq[] = { 0, 681, 768, 1017, 1353, 1555, 1804, 2092, 2736 };

	/* map to devfreq whlie config is enabled */
	for (i = ARRAY_SIZE(ddr_devfreq_avail_freq) - 1; i >= 0; --i) {
		if (val >= ddr_aop_mapping_freq[i])
			return ddr_devfreq_avail_freq[i];
	}
	return val;
}

void cc_set_cpu_idle_block(int cpu)
{
	u64 next_ts;
	int ccdm_idle_block_idx =
		cpu + CCDM_TB_CPU_0_IDLE_BLOCK;

	if (!cc_tb_idle_block_enable)
		return;

	next_ts = get_jiffies_64() + cc_tb_idle_block_period;
	ccdm_update_hint_1(ccdm_idle_block_idx, next_ts);
}

void cc_check_renice(void *tsk)
{
	struct task_struct *t = (struct task_struct *) tsk;
	u64 next_ts;

	if (unlikely(!im_main(t) && !im_enqueue(t) && !im_render(t)))
		return;

	if (!cc_tb_nice_last_enable)
		return;

	/* skip rt task */
	if (unlikely(t->prio < 100))
		return;

	next_ts = get_jiffies_64() + cc_tb_nice_last_period;

	if (likely(t->nice_effect_ts != next_ts)) {
		t->nice_effect_ts = next_ts;
		set_user_nice_no_cache(t, MIN_NICE);
	}
}

static void cc_tb_freq_boost(struct cc_command *cc)
{
	struct cpufreq_policy *pol;
	int clus, cpu;
	unsigned int next_freq;

	if (!cc_tb_freq_boost_enable)
		return;

	if (cc_is_reset(cc))
		ccdm_update_hint_3(CCDM_TB_FREQ_BOOST, 0, 0, 0);
	else
		ccdm_update_hint_3(CCDM_TB_FREQ_BOOST,
				cc->params[0], cc->params[1], cc->params[2]);

	get_online_cpus();
	/* force trigger cpufreq change */
	for (clus = 0; clus < 3; ++clus) {
		if (!cc->params[clus])
			continue;

		pol = cpufreq_cpu_get(cc_get_cpu_idx(clus));
		if (unlikely(!pol))
			continue;

		if (unlikely(!pol->cc_enable)) {
			cpufreq_cpu_put(pol);
			continue;
		}

		/* trigger frequency change */
		next_freq =
			cc_cal_next_freq_with_extra_util(pol, pol->req_freq);

		if (pol->fast_switch_enabled) {
			next_freq = cpufreq_driver_fast_switch(pol, next_freq);
			if (!next_freq || (next_freq == pol->cur)) {
				cpufreq_cpu_put(pol);
				continue;
			}

			/* update cpufreq stat */
			pol->cur = next_freq;
			for_each_cpu(cpu, pol->cpus)
				trace_cpu_frequency(next_freq, cpu);
			cpufreq_stats_record_transition(pol, next_freq);
		} else {
			cpufreq_driver_target(pol, next_freq, CPUFREQ_RELATION_H);
		}
		cpufreq_cpu_put(pol);
	}
	put_online_cpus();
}

static void cc_tb_place_boost(struct cc_command *cc)
{
	if (!cc_tb_place_boost_enable)
		return;

	if (cc_is_reset(cc))
		ccdm_update_hint_1(CCDM_TB_PLACE_BOOST, 0);
	else
		ccdm_update_hint_1(CCDM_TB_PLACE_BOOST, 1);
}

static void cc_query_cpufreq(struct cc_command* cc)
{
	struct cpufreq_policy *pol;
	u32 clus;
	int idx;

	get_online_cpus();

	switch (cc->category) {
	case CC_CTL_CATEGORY_CLUS_0_FREQ_QUERY: clus = 0; break;
	case CC_CTL_CATEGORY_CLUS_1_FREQ_QUERY: clus = 1; break;
	case CC_CTL_CATEGORY_CLUS_2_FREQ_QUERY: clus = 2; break;
	default:
		cc_logw("cpufreq query invalid, category %u\n", cc->category);
		goto out;
	}

	idx = cc_get_cpu_idx(clus);
	if (idx == -1) {
		cc_logw("can' get cpu idx, input cluster %u\n", clus);
		goto out;
	}

	pol = cpufreq_cpu_get(idx);
	if (!pol) {
		cc_logw("can't get cluster %d cpufreqp policy\n", idx);
		goto out;
	}
	cc->response = pol->cur;

out:
	put_online_cpus();
}

#define CC_DDRFREQ_CHECK(name, target) \
	if (!strcmp(name, target)) { \
		cc_logi("mark device %s as ddrfreq related\n", name); \
		return true; \
	}

bool cc_is_ddrfreq_related(const char* name)
{
	if (!unlikely(name))
		return false;

	/* ddrfreq voting device */
	CC_DDRFREQ_CHECK(name, "soc:qcom,gpubw");
	CC_DDRFREQ_CHECK(name, "soc:qcom,cpu-llcc-ddr-bw");
	CC_DDRFREQ_CHECK(name, "soc:qcom,cpu4-cpu-ddr-latfloor");
	CC_DDRFREQ_CHECK(name, "soc:qcom,cpu0-llcc-ddr-lat");
	CC_DDRFREQ_CHECK(name, "soc:qcom,cpu4-llcc-ddr-lat");
	//CC_DDRFREQ_CHECK(name, "aa00000.qcom,vidc:arm9_bus_ddr");
	//CC_DDRFREQ_CHECK(name, "aa00000.qcom,vidc:venus_bus_ddr");
	return false;
}

static inline u64 query_ddrfreq(void)
{
	u64 val;
	clk_get_ddr_freq(&val);
	val /= 1000000;
	/* process for easy deal with */
	if (val == 1018) val = 1017;
	else if (val == 1355) val = 1353;
	else if (val == 1805) val = 1804;
	else if (val == 2096) val = 2092;
	else if (val == 2739) val = 2736;
	return val;
}

static void cc_query_ddrfreq(struct cc_command* cc)
{
	cc->response = query_ddrfreq();
}

#define CC_DDR_RESET_VAL 0
static void cc_adjust_ddr_voting_freq(struct cc_command *cc)
{
	u64 val = cc->params[0];

	if (!cc_ddr_boost_enable)
		return;

	if (cc_is_nonblock(cc))
		return;

	val = cc_ddr_to_devfreq(val);

	if (cc->type == CC_CTL_TYPE_RESET)
		val = CC_DDR_RESET_VAL;

	cc_expect_ddrfreq = val;
}

static void cc_adjust_ddr_lock_freq(struct cc_command *cc)
{
	u64 val = cc->params[0];
	u64 cur;

	if (!cc_ddr_boost_enable)
		return;

	if (cc_is_nonblock(cc))
		return;

	if (cc->type == CC_CTL_TYPE_RESET)
		val = CC_DDR_RESET_VAL;
 
	/* check if need update */
	cur = query_ddrfreq();

//	if (cur != val)
//		aop_lock_ddr_freq(val);
}

static void cc_adjust_sched(struct cc_command *cc)
{
#ifdef CONFIG_OPCHAIN
	struct task_struct *task = NULL;
	pid_t pid = cc->params[0];
#endif

	if (cc_is_nonblock(cc))
		return;

#ifdef CONFIG_OPCHAIN
	if (cc_is_reset(cc)) {
		opc_set_boost(0);
		return;
	}

	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (task) {
		cc_logv("set task %s %d to prime core\n", task->comm, task->pid);
		task->etask_claim = UT_PERF_TOP;
		opc_set_boost(1);
	} else
		cc_logw("can't find task %d\n", pid);
	rcu_read_unlock();
#endif
}

static void cc_tb_cctl_boost(struct cc_command *cc)
{
	if (!cc_tb_cctl_boost_enable)
		return;

	if (cc_is_reset(cc)) {
		ccdm_update_hint_1(CCDM_TB_CCTL_BOOST, 0);
		core_ctl_op_boost(false, 0);
	} else {
		ccdm_update_hint_1(CCDM_TB_CCTL_BOOST, 1);
		core_ctl_op_boost(true, cc->params[0]);
	}
}

void cc_process(struct cc_command* cc)
{
	if (cc->type < CC_CTL_TYPE_ONESHOT_NONBLOCK) {
		if (!cc_is_reset(cc))
			tracing_mark_write(cc, 1, false);
	}

	cc_logv("pid: %u, group: %u, category: %u, type: %u, params: %llu %llu %llu %llu\n",
		cc->pid, cc->group, cc->category, cc->type, cc->params[0], cc->params[1], cc->params[2], cc->params[3]);

	switch (cc->category) {
	case CC_CTL_CATEGORY_CLUS_0_FREQ:
		cc_logv("cpufreq: type: %u, cluster: 0 target: %llu version: %llu\n",
			cc->type, cc->params[0], cc->params[3]);
		cc_adjust_cpufreq(cc);
		break;
	case CC_CTL_CATEGORY_CLUS_1_FREQ:
		cc_logv("cpufreq: type: %u, cluster: 1 target: %llu version: %llu\n",
			cc->type, cc->params[0], cc->params[3]);
		cc_adjust_cpufreq(cc);
		break;
	case CC_CTL_CATEGORY_CLUS_2_FREQ:
		cc_logv("cpufreq: type: %u, cluster: 2 target: %llu version: %llu\n",
			cc->type, cc->params[0], cc->params[3]);
		cc_adjust_cpufreq(cc);
		break;
	case CC_CTL_CATEGORY_FPS_BOOST:
		break;
	case CC_CTL_CATEGORY_DDR_VOTING_FREQ:
		cc_logv("ddrfreq voting: type: %u, target: %llu\n", cc->type, cc->params[0]);
		cc_adjust_ddr_voting_freq(cc);
		break;
	case CC_CTL_CATEGORY_DDR_LOCK_FREQ:
		cc_logv("ddrfreq lock: type: %u, target: %llu\n", cc->type, cc->params[0]);
		cc_adjust_ddr_lock_freq(cc);
	case CC_CTL_CATEGORY_SCHED_PRIME_BOOST:
		cc_logv("sched prime boost: type: %u, param: %llu\n", cc->type, cc->params[0]);
		cc_adjust_sched(cc);
		break;
	case CC_CTL_CATEGORY_CLUS_0_FREQ_QUERY:
		cc_query_cpufreq(cc);
		cc_logv("cpufreq query: type: %u, cluster: 0, freq: %llu\n", cc->type, cc->response);
		break;
	case CC_CTL_CATEGORY_CLUS_1_FREQ_QUERY:
		cc_query_cpufreq(cc);
		cc_logv("cpufreq query: type: %u, cluster: 1, freq: %llu\n", cc->type, cc->response);
		break;
	case CC_CTL_CATEGORY_CLUS_2_FREQ_QUERY:
		cc_query_cpufreq(cc);
		cc_logv("cpufreq query: type: %u, cluster: 2, freq: %llu\n", cc->type, cc->response);
		break;
	case CC_CTL_CATEGORY_DDR_FREQ_QUERY:
		cc_query_ddrfreq(cc);
		cc_logv("ddrfreq query: type: %u, freq: %llu\n", cc->type, cc->response);
		break;
	/* Trubo rendering */
	case CC_CTL_CATEGORY_TB_FREQ_BOOST:
		cc_logv("tb_freq_boost: type: %u, hint %llu %llu %llu %llu\n",
			cc->type, cc->params[0], cc->params[1],
			cc->params[2], cc->params[3]);
		cc_tb_freq_boost(cc);
		break;
	case CC_CTL_CATEGORY_TB_PLACE_BOOST:
		cc_logv("tb_place_boost: type: %u, hint %llu %llu %llu\n",
			cc->type, cc->params[0], cc->params[1],
			cc->params[2]);
		cc_tb_place_boost(cc);
		break;
	case CC_CTL_CATEGORY_TB_CORECTL_BOOST:
		cc_logv("tb_corectl_boost: type: %u, hint %llu\n",
			cc->type, cc->params[0]);
		cc_tb_cctl_boost(cc);
		break;
	default:
		cc_logw("category %d not support\n", cc->category);
		break;
	}

	if (cc->type < CC_CTL_TYPE_ONESHOT_NONBLOCK) {
		if (cc_is_reset(cc))
			tracing_mark_write(cc, 0, false);
	}
}

static inline void dump_cc(struct cc_command *cc, const char* func, const char* msg)
{
	cc_logv("%s: %s: pid: %d, period_us: %u, prio: %u, group: %u, category: %u, type: %u, [0]: %llu, [1]: %llu, [2]: %llu, [3]: %llu, response: %llu, bind_leader: %d, status: %d\n",
		func, msg,
		cc->pid, cc->period_us, cc->prio, cc->group, cc->category,
		cc->type, cc->params[0], cc->params[1], cc->params[2],
		cc->params[3], cc->response, cc->bind_leader, cc->status);
}

static inline struct cc_command* find_highest_cc_nolock(int category)
{
	struct cc_tsk_data* data = NULL;
	struct cc_command *cc = NULL;
	int prio;

	/* find the highest priority request to perform */
	for (prio = CC_PRIO_HIGH; !cc && prio < CC_PRIO_MAX; ++prio) {
		if (!list_empty(&cc_record[category].phead[prio])) {
			list_for_each_entry(data, &cc_record[category].phead[prio], node) {
				cc = &data->cc;
				break;
			}
		}
	}
	return cc;
}

/* find the highest priority request to perform */
static struct cc_command* find_highest_cc(int category)
{
	struct cc_command* cc;

	spin_lock(&cc_record[category].lock);
	cc = find_highest_cc_nolock(category);
	spin_unlock(&cc_record[category].lock);
	return cc;
}


static void cc_record_acq(int category, struct cc_command* cc)
{
	struct cc_command *high_cc = find_highest_cc(category);

	dump_cc(cc, __func__, "current request");
	if (high_cc) {
		dump_cc(high_cc, __func__, "highest request");
	} else {
		cc_logw("%s: can't find any request\n", __func__);
		return;
	}

	/*
	 * apply change
	 * if high_cc not equal to cc, it should be applied earlier
	 */
	if (high_cc == cc)
		cc_process(high_cc);
}

static void cc_record_rel(int category, struct cc_command *cc)
{
	struct cc_command* next_cc = find_highest_cc(category);
	bool is_nonblock = cc->type >= CC_CTL_TYPE_ONESHOT_NONBLOCK;

	/* update reset type */
	cc->type = is_nonblock? CC_CTL_TYPE_RESET_NONBLOCK: CC_CTL_TYPE_RESET;
	if (next_cc) {
		/* apply next since we detach the highest before */
		cc_logv("got pending request, re-apply\n");
		dump_cc(next_cc, __func__, "next request");
		cc_process(next_cc);
	} else {
		/* no request pending, reset finally */
		cc_logv("no pending request, release\n");
		dump_cc(cc, __func__, "reset request");
		cc_process(cc);
	}
}

static void cc_record_init(void)
{
	int i, j;

	/* init cc_record */
	for (i = 0; i < CC_CTL_CATEGORY_MAX; ++i) {
		/* assign acquire and release */
		spin_lock_init(&cc_record[i].lock);
		for (j = 0; j < CC_PRIO_MAX; ++j)
			INIT_LIST_HEAD(&cc_record[i].phead[j]);
	}
}

static void cc_tsk_acq(struct cc_tsk_data* data)
{
	struct cc_command *cc;
	u32 delay_us;
	u32 category;
	int prio;

	tracing_mark_write(cc, 1, true);

	current->cc_enable = true;

	/* add into cc_record */
	/* TODO check category & prio value */
	category = data->cc.category;
	prio = data->cc.prio;
	cc = &data->cc;
	delay_us = cc->period_us;

	/* update boost ts */
	cc_boost_ts_update(cc);

	dump_cc(cc, __func__, "current request");

	/* if already inside list, detach first */
	spin_lock(&cc_record[category].lock);
	if (!list_empty(&data->node)) {
		/* cancel queued delayed work first */
		cancel_delayed_work(&data->dwork);
		list_del_init(&data->node);
		dump_cc(cc, __func__, "[detach]");
	}
	list_add(&data->node, &cc_record[category].phead[prio]);
	dump_cc(cc, __func__, "[attach]");
	spin_unlock(&cc_record[category].lock);

	/* trigger system control */
	cc_record_acq(category, cc);

	/* queue delay work for release */
	queue_delayed_work(cc_wq, &data->dwork, usecs_to_jiffies(delay_us));

	/* update stat */
	cc_stat_inc(category);
}

static void cc_tsk_rel(struct cc_tsk_data* data)
{
	struct cc_command* cc = &data->cc;
	struct cc_command* high_cc;
	u32 category = cc->category;

	/* update boost ts */
	cc_boost_ts_update(cc);

	/* detach first */
	dump_cc(cc, __func__, "current request");

	spin_lock(&cc_record[category].lock);
	high_cc = find_highest_cc_nolock(category);
	/* detach first */
	if (!list_empty(&data->node)) {
		cancel_delayed_work(&data->dwork);
		list_del_init(&data->node);
		dump_cc(cc, __func__, "[detach]");
	} else {
		cc_logv("try to detach, but already detached\n");
	}

	if (cc != high_cc) {
		/* no need to worry, just detach and return */
		spin_unlock(&cc_record[category].lock);
		tracing_mark_write(cc, 0, true);
		return;
	}
	spin_unlock(&cc_record[category].lock);

	/* trigger system control */
	cc_record_rel(category, cc);
	tracing_mark_write(cc, 0, true);
}

static void cc_delay_rel(struct work_struct *work)
{
	struct cc_tsk_data* data = container_of(work, struct cc_tsk_data, dwork.work);
	struct cc_command* cc = &data->cc;

	/* delay work no need to use nonblock call */
	cc->type = CC_CTL_TYPE_RESET;
	cc_tsk_rel(data);
}

static struct cc_tsk_data* cc_init_ctd(void)
{
	struct cc_tsk_data *ctd = NULL;
	int i = 0;

	ctd = kzalloc(sizeof(struct cc_tsk_data) * CC_CTL_CATEGORY_MAX, GFP_KERNEL);
	if (!ctd)
		return NULL;

	for (i = 0; i < CC_CTL_CATEGORY_MAX; ++i) {
		/* init all category control */
		INIT_LIST_HEAD(&ctd[i].node);
		INIT_DELAYED_WORK(&ctd[i].dwork, cc_delay_rel);
	}
	return ctd;
}

static inline struct cc_command* get_tsk_cc(bool bind_leader, u32 category)
{
	struct task_struct* task = bind_leader? current->group_leader: current;

	/* FIXME may be race */
	/* init ctd */
	if (!task->ctd) {
		task->ctd = cc_init_ctd();
		if (!task->ctd) {
			cc_loge("task %s(%d) cc_tsk_data init failed\n", task->comm, task->pid);
			return NULL;
		}
		cc_logv("%s: pid: %s(%d) init ctd successful\n",
				__func__, task->comm, task->pid);
	}

	return &task->ctd[category].cc;
}

static inline struct cc_tsk_data* get_tsk_data(bool bind_leader, u32 category)
{
	struct task_struct* task = bind_leader? current->group_leader: current;
	return &task->ctd[category];
}

static inline int cc_tsk_copy(struct cc_command* cc, bool copy_to_user)
{
	u32 category = cc->category;

	struct cc_command* tskcc = get_tsk_cc(cc->bind_leader, category);

	if (!tskcc)
		return -1;

	if (copy_to_user)
		memcpy(cc, tskcc, sizeof(struct cc_command));
	else
		memcpy(tskcc, cc, sizeof(struct cc_command));

	return 0;
}

static inline struct task_struct *cc_get_owner(bool bind_leader)
{
	struct task_struct *task = current;

	rcu_read_lock();

	if (bind_leader)
		task = find_task_by_vpid(current->tgid);

	if (task)
		get_task_struct(task);

	rcu_read_unlock();
	return task;
}

static inline void cc_put_owner(struct task_struct *task)
{
	if (likely(task))
		put_task_struct(task);
}

// call with get/put owner`s task_struct
static void __cc_tsk_process(struct cc_command* cc)
{
	u32 category = cc->category;

	/* query can return first */
	if (cc_is_query(category)) {
		cc_process(cc);
		return;
	}

	/* copy cc */
	if (cc_tsk_copy(cc, false))
		return;

	if (cc_is_reset(cc))
		cc_tsk_rel(get_tsk_data(cc->bind_leader, category));
	else
		cc_tsk_acq(get_tsk_data(cc->bind_leader, category));

	/* copy back to userspace cc */
	cc_tsk_copy(cc, true);
}

void cc_tsk_process(struct cc_command* cc)
{
	struct task_struct *owner = NULL;

	owner = cc_get_owner(cc->bind_leader);

	if (!owner) {
		cc_logw("request owner is gone\n");
		return;
	}

	if (likely(owner->cc_enable))
		__cc_tsk_process(cc);
	else
		cc_logw("request owner is going to leave\n");

	cc_put_owner(owner);
}

/* for fork and exit, use void* to avoid include sched.h in control_center.h */
void cc_tsk_init(void* ptr)
{
	struct task_struct *task = (struct task_struct*) ptr;

	task->cc_enable = true;
	task->ctd = NULL;
}

void cc_tsk_disable(void* ptr)
{
	struct task_struct *task = (struct task_struct*) ptr;
	struct cc_tsk_data *data = task->ctd;
	u32 category;

	if (!task->ctd)
		return;

	// disable to avoid further use
	task->cc_enable = false;

	/* detach all */
	for (category = 0; category < CC_CTL_CATEGORY_MAX; ++category) {
		bool need_free = false;
		cc_logv("%s: pid: %s(%d) free category %d\n",
			__func__, task->comm, task->pid, category);
		spin_lock(&cc_record[category].lock);
		if (!list_empty(&data[category].node)) {
			need_free = true;
			list_del_init(&data[category].node);
			dump_cc(&data[category].cc, __func__, "[detach]");
		}
		spin_unlock(&cc_record[category].lock);

		if (need_free) {
			cc_logv("%s: pid: %s(%d) free category %d, need update.\n",
				__func__, task->comm, task->pid, category);
			cancel_delayed_work_sync(&data[category].dwork);
			/* since we're going to free ctd, we need to force set type to blocked version */
			data[category].cc.type = CC_CTL_TYPE_RESET;

			cc_record_rel(category, &data[category].cc);
		}
	}
}

void cc_tsk_free(void* ptr)
{
	struct task_struct *task = (struct task_struct*) ptr;

	if (!task->ctd)
		return;

	kfree(task->ctd);
	task->ctd = NULL;
}

static int cc_ctl_show(struct seq_file *m, void *v)
{
	seq_printf(m, "control center version: %s\n", CC_CTL_VERSION);
	return 0;
}

static int cc_ctl_open(struct inode *ip, struct file *fp)
{
	cc_logv("opened by %s %d\n", current->comm, current->pid);
	return single_open(fp, cc_ctl_show, NULL);;
}

static int cc_ctl_close(struct inode *ip, struct file *fp)
{
	cc_logv("closed by %s %d\n", current->comm, current->pid);
	return 0;
}

static long cc_ctl_ioctl(struct file *file, unsigned int cmd, unsigned long __user arg)
{
	ktime_t begin, end;
	s64 t;
	static s64 tmax = 0;

	if (_IOC_TYPE(cmd) != CC_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > CC_IOC_MAX) return -ENOTTY;

	CC_TIME_START(begin);

	cc_logv("%s: cmd: %u, arg: %lu\n", __func__, CC_IOC_COMMAND, arg);
	switch (cmd) {
	case CC_IOC_COMMAND:
		{
			struct cc_command cc;

			if (copy_from_user(&cc, (struct cc_command *) arg, sizeof(struct cc_command)))
				break;

			cc_tsk_process(&cc);

			if (copy_to_user((struct cc_command *) arg, &cc, sizeof(struct cc_command)))
				break;
		}
	}

	CC_TIME_END(begin, end, t, tmax);
	return 0;
}

static const struct file_operations cc_ctl_fops = {
	.owner = THIS_MODULE,
	.open = cc_ctl_open,
	.release = cc_ctl_close,
	.unlocked_ioctl = cc_ctl_ioctl,
	.compat_ioctl = cc_ctl_ioctl,

	.read = seq_read,
	.llseek = seq_lseek,
};

/* TODO try to simplify the register flow */
static dev_t cc_ctl_dev;
static struct class *driver_class;
static struct cdev cdev;
static int cc_cdev_init(void)
{
	int rc;
	struct device *class_dev;

	rc = alloc_chrdev_region(&cc_ctl_dev, 0, 1, CC_CTL_NODE);
	if (rc < 0) {
		cc_loge("alloc_chrdev_region failed %d\n", rc);
		return 0;
	}

	driver_class = class_create(THIS_MODULE, CC_CTL_NODE);
	if (IS_ERR(driver_class)) {
		rc = -ENOMEM;
		cc_loge("class_create failed %d\n", rc);
		goto exit_unreg_chrdev_region;
	}
	class_dev = device_create(driver_class, NULL, cc_ctl_dev, NULL, CC_CTL_NODE);
	if (IS_ERR(class_dev)) {
		cc_loge("class_device_create failed %d\n", rc);
		rc = -ENOMEM;
		goto exit_destroy_class;
	}
	cdev_init(&cdev, &cc_ctl_fops);
	cdev.owner = THIS_MODULE;
	rc = cdev_add(&cdev, MKDEV(MAJOR(cc_ctl_dev), 0), 1);
	if (rc < 0) {
		cc_loge("cdev_add failed %d\n", rc);
		goto exit_destroy_device;
	}
	return 0;
exit_destroy_device:
	device_destroy(driver_class, cc_ctl_dev);
exit_destroy_class:
	class_destroy(driver_class);
exit_unreg_chrdev_region:
	unregister_chrdev_region(cc_ctl_dev, 1);
	return 0;
}

static struct cc_async_rq* cc_get_rq(struct list_head* head)
{
	struct cc_async_rq *rq = NULL;

	spin_lock(&cc_async_lock);
	if (!list_empty(head)) {
		list_for_each_entry(rq, head, node) {
			list_del_init(&rq->node);
			break;
		}
	}
	spin_unlock(&cc_async_lock);

	return rq;
}

static void __cc_attach_rq(struct cc_async_rq *rq, struct list_head* head)
{
	spin_lock(&cc_async_lock);
	list_add(&rq->node, head);
	spin_unlock(&cc_async_lock);
}

static void cc_release_rq(struct cc_async_rq* rq, struct list_head* head)
{
	/* clean before release */
	memset(&rq->cc, 0, sizeof (struct cc_command));
	__cc_attach_rq(rq, head);
}

static void __cc_queue_rq(struct cc_async_rq* rq, struct list_head* head)
{
	__cc_attach_rq(rq, head);
}

static void cc_work(struct work_struct *work)
{
	/* time related */
	ktime_t begin, end;
	s64 t;
	static s64 tmax = 0;

	struct cc_async_rq* rq =
		container_of(work, struct cc_async_rq, work);

	CC_TIME_START(begin);

	/* main loop */
	cc_process(&rq->cc);

	cc_release_rq(rq, &cc_request_list);

	CC_TIME_END(begin, end, t, tmax);
}

static int cc_worker(void* arg)
{
	ktime_t begin, end;
	s64 t;
	static s64 tmax = 0;

	/* perform async system resousrce adjustment */
	while (!kthread_should_stop()) {
		struct cc_async_rq *rq;

		CC_TIME_START(begin);
redo:
		rq = cc_get_rq(&cc_pending_list);
		if (!rq) {
			goto finish;
		}
		/* main loop */
		cc_process(&rq->cc);

		cc_release_rq(rq, &cc_request_list);
		goto redo;

finish:
		CC_TIME_END(begin, end, t, tmax);

		/* sleep for next wake up */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
	return 0;
}

static void cc_queue_rq(struct cc_command *cc)
{
	struct cc_async_rq *rq = cc_get_rq(&cc_request_list);
	if (!rq) {
		cc_logw("rq not enough\n");
		return;
	}

	memcpy(&rq->cc, cc, sizeof(struct cc_command));

	if (likely(cc_wq)) {
		/* if support workqueue, using workqueue */
		queue_work(cc_wq, &rq->work);
	} else if (likely(cc_worker_task)) {
		/* if support worker, using worker */
		__cc_queue_rq(rq, &cc_pending_list);
		wake_up_process(cc_worker_task);
	} else {
		/* fall back to original version */
		cc_logw_ratelimited("cc command fall back\n");
		cc_process(&rq->cc);
		cc_release_rq(rq, &cc_request_list);
	}
}

static void cc_worker_init(void)
{
	int i;

	/* init for request/ pending/ lock */
	INIT_LIST_HEAD(&cc_request_list);
	INIT_LIST_HEAD(&cc_pending_list);

	/* init requests */
	for (i = 0; i < CC_ASYNC_RQ_MAX; ++i) {
		INIT_LIST_HEAD(&cc_async_rq[i].node);
		INIT_WORK(&cc_async_rq[i].work, cc_work);
		cc_async_rq[i].idx = i;
		spin_lock(&cc_async_lock);
		list_add(&cc_async_rq[i].node, &cc_request_list);
		spin_unlock(&cc_async_lock);
	}

	cc_worker_task = kthread_run(cc_worker, NULL, "cc_worker");
	if (IS_ERR(cc_worker_task))
		cc_loge("cc_worker create failed\n");

	cc_wq = alloc_ordered_workqueue("cc_wq", 0);
	if (!cc_wq)
		cc_loge("alloc work queue fail\n");
}

static int cc_dump_list_show(char *buf, const struct kernel_param *kp)
{
	int cnt = 0;
	int size = 0;
	struct cc_async_rq *rq;

	spin_lock(&cc_async_lock);

	/* request list */
	size = 0;
	list_for_each_entry(rq, &cc_request_list, node) {
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "%d ", rq->idx);
		++size;
	}
	if (size)
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "\n", rq->idx);
	cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "request list: size: %d\n", size);

	/* pending list */
	size = 0;
	list_for_each_entry(rq, &cc_pending_list, node) {
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "%d ", rq->idx);
		++size;
	}
	if (size)
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "\n", rq->idx);
	cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "pending list: size: %d\n", size);

	spin_unlock(&cc_async_lock);

	return cnt;
}

static struct kernel_param_ops cc_dump_list_ops = {
	.get = cc_dump_list_show,
};
module_param_cb(dump_list, &cc_dump_list_ops, NULL, 0644);

static int cc_ddr_freq_show(char *buf, const struct kernel_param *kp)
{
	int cnt = 0;
	u64 freqshow = 0;
	clk_get_ddr_freq(&freqshow);
	cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "ddrfreq: %llu\n", freqshow);
	return cnt;
}

static struct kernel_param_ops cc_ddr_freq_ops = {
	.get = cc_ddr_freq_show,
};
module_param_cb(freq_show, &cc_ddr_freq_ops, NULL, 0644);

static int cc_dump_status_show(char *buf, const struct kernel_param *kp)
{
	struct cpufreq_policy *pol;
	int cnt = 0;
	int i, idx;
	u64 val;

	/* dump cpufreq control status */
	cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "cpufreq:\n");
	for (i = 0; i < CLUSTER_NUM; ++i) {
		idx = cc_get_cpu_idx(i);
		if (idx == -1) {
			cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "cluster %d offline\n", i);
			continue;
		}
		pol = cpufreq_cpu_get(idx);
		if (!pol) {
			cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
				"cluster %d can't get policy\n", i);
			continue;
		}
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
				"cluster %d min %u max %u cur %u, cc_min %u cc_max %u\n",
			i, pol->min, pol->max, pol->cur, pol->cc_min, pol->cc_max);
		cpufreq_cpu_put(pol);
	}

	/* dump ddrfreq control status */
	val = query_ddrfreq();
	cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "ddrfreq: %llu\n", val);
	cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
		"expected ddrfreq: %lu\n", cc_expect_ddrfreq);
	return cnt;
}

static struct kernel_param_ops cc_dump_status_ops = {
	.get = cc_dump_status_show,
};
module_param_cb(dump_status, &cc_dump_status_ops, NULL, 0644);

static unsigned int ccdm_min_util_threshold = 35;
static int ccdm_min_util_threshold_store(const char *buf,
		const struct kernel_param *kp)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) <= 0)
		return 0;

	ccdm_min_util_threshold = val;

	return 0;
}

static int ccdm_min_util_threshold_show(char *buf,
		const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", ccdm_min_util_threshold);
}

static struct kernel_param_ops ccdm_min_thres_ops = {
	.set = ccdm_min_util_threshold_store,
	.get = ccdm_min_util_threshold_show,
};
module_param_cb(ccdm_min_util_thres, &ccdm_min_thres_ops, NULL, 0664);

unsigned int ccdm_get_min_util_threshold(void)
{
	return ccdm_min_util_threshold;
}

/* debug purpose, should be removed later */
static int cc_ccdm_status_show(char *buf, const struct kernel_param *kp)
{
	struct cpufreq_policy *pol;
	int cnt = 0;
	int i, idx;
	u64 val;

	/* TODO add a way to update trust/weight */
	struct ccdm_info {
		long long c_min[3];
		long long c_max[3];
		long long c_fps_boost[3];
		long long fps_boost_hint;
		long long trust[3];
		long long weight[3];
		long long c_fps_boost_ddrfreq;
		long long ddrfreq;
		long long tb_freq_boost[3];
		long long tb_place_boost_hint;
		long long tb_idle_block_hint[8];
		long long tb_cctl_boost_hint;
	} info;

	ccdm_get_status((void *) &info);

	cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "cpufreq:\n");
	for (i = 0; i < 3; ++i) {
		idx = cc_get_cpu_idx(i);
		if (idx == -1) {
			cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
				"cluster %d offline\n", i);
			continue;
		}
		pol = cpufreq_cpu_get(idx);
		if (!pol) {
			cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
				"cluster %d can't get policy\n", i);
			continue;
		}
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
			"cluster %d min %u max %u cur %u, ",
			i, pol->min, pol->max, pol->cur);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
			"ccdm: min %lld max %lld ",
			info.c_min[i], info.c_max[i]);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
			"fps_boost %lld trust %lld weight %lld\n",
			info.c_fps_boost[i], info.trust[i], info.weight[i]);
		cpufreq_cpu_put(pol);
	}
	cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
		"fps_boost hint %lld\n", info.fps_boost_hint);

	/* dump ddrfreq control status */
	val = query_ddrfreq();
	cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
		"ddrfreq: %llu exptected: %llu hint: %llu\n",
		val, info.ddrfreq, info.c_fps_boost_ddrfreq);

	for (i = 0; i < 3; ++i) {
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
		"tb_freq_boost: clus %lld, extra util %lld\n",
		i, info.tb_freq_boost[i]);
	}
	cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
		"tb_place_boost: %lld\n", info.tb_place_boost_hint);

	for (i = 0; i < 8; ++i) {
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
		"tb_idle_block[%d]: %llu %llu\n",
		i, get_jiffies_64(), (u64)info.tb_idle_block_hint[i]);
	}

	cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
		"tb_corectl_boost: %lld\n", info.tb_cctl_boost_hint);

	return cnt;
}

static struct kernel_param_ops cc_ccdm_status_ops = {
	.get = cc_ccdm_status_show,
};
module_param_cb(ccdm_status, &cc_ccdm_status_ops, NULL, 0644);

static const char *cc_category_tags[CC_CTL_CATEGORY_MAX] = {
	"cpufreq_0",
	"cpufreq_1",
	"cpufreq_2",
	"fps_boost",
	"ddrfreq voting:",
	"ddrfreq lock:",
	"sched_prime_boost",
	"cpufreq_0_query",
	"cpufreq_1_query",
	"cpufreq_2_query",
	"ddrfreq_query",
	"turbo boost freq",
	"turbo boost placement",
	"turbo boost corectl boost"
};

static const char *cc_category_tags_mapping(int idx)
{
	if (idx >= 0 && idx < CC_CTL_CATEGORY_MAX)
		return cc_category_tags[idx];

	return "";
}

static int cc_dump_record_show(char *buf, const struct kernel_param *kp)
{
	struct cc_tsk_data* data;
	const char* tag;
	u32 prio;
	int cnt = 0;
	int i;

	for (i = 0; i < CC_CTL_CATEGORY_MAX; ++i) {
		/* ignore query part */
		if (cc_is_query(i))
			continue;

		spin_lock(&cc_record[i].lock);
		tag = cc_category_tags_mapping(i);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "%s:\n", tag);
		for (prio = 0; prio < CC_PRIO_MAX; ++prio) {
			cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "  p[%d]:\n", prio);
			list_for_each_entry(data, &cc_record[i].phead[prio], node) {
				struct cc_command* cc = &data->cc;
				cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
					"    pid: %d, period_us: %u, prio: %u, group: %u, category: %u"
					", type: %u, [0]: %llu, [1]: %llu, [2]: %llu, [3]: %llu"
					", response: %llu, status: %d\n",
					cc->pid, cc->period_us, cc->prio, cc->group, cc->category,
					cc->type, cc->params[0], cc->params[1], cc->params[2],
					cc->params[3], cc->response, cc->status);
			}
		}
		spin_unlock(&cc_record[i].lock);
	}
	cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "cnt: %d\n", cnt);
	return cnt;
}

static struct kernel_param_ops cc_dump_record_ops = {
	.get = cc_dump_record_show,
};
module_param_cb(dump_record, &cc_dump_record_ops, NULL, 0644);

static int cc_dump_stat_show(char *buf, const struct kernel_param *kp)
{
	const char *tag;
	long long cc_cnt, cc_tcnt;
	int cnt = 0;
	int i;

	for (i = 0; i < CC_CTL_CATEGORY_MAX; ++i) {
		tag = cc_category_tags_mapping(i);
		cc_cnt = atomic64_read(&cc_stat.cnt[i]);
		cc_tcnt = atomic64_read(&cc_stat.tcnt[i]) + cc_cnt;
		atomic64_set(&cc_stat.cnt[i], 0);
		atomic64_set(&cc_stat.tcnt[i], cc_tcnt);

		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
			"%s: %lld %lld\n", tag, cc_cnt, cc_tcnt);
	}
	return cnt;
}

static struct kernel_param_ops cc_dump_stat_ops = {
	.get = cc_dump_stat_show,
};
module_param_cb(dump_stat, &cc_dump_stat_ops, NULL, 0644);

static const struct file_operations cc_ctl_proc_fops = {
	.owner = THIS_MODULE,
	.open = cc_ctl_open,
	.release = cc_ctl_close,
	.unlocked_ioctl = cc_ctl_ioctl,
	.compat_ioctl = cc_ctl_ioctl,

	.read = seq_read,
	.llseek = seq_lseek,
};

static inline void cc_proc_init(void)
{
	proc_create(CC_CTL_NODE, S_IFREG | 0660, NULL, &cc_ctl_proc_fops);
}

static int cc_init(void)
{
	/* FIXME
	 * remove later, so far just for compatible
	 */
	cc_cdev_init(); // create /dev/cc_ctl

	cc_proc_init(); // create /proc/cc_ctl
	cc_record_init();
	cc_worker_init();
	cc_logi("control center inited\n");
	return 0;
}

pure_initcall(cc_init);
