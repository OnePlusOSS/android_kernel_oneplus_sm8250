#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/cpufreq.h>
#include <linux/sched/sysctl.h>

#define NSEC_TO_MSEC(val) (val / NSEC_PER_MSEC)
#define MSEC_TO_NSEC(val) (val * NSEC_PER_MSEC)
#define CLUS_MAX 3

struct cpufreq_bouncing {
	/* statistics */
	u64 last_ts;
	u64 last_freq_update_ts;
	u64 acc;

	/* restriction */
	int limit_freq;
	int limit_level;
	u64 limit_thres;

	/* freqs */
	int freq_sorting;
	int freq_levels;
	unsigned int *freqs; // quick mapping

	/* config */
	bool enable;
	int cur_level;

	/* config: ceil */
	int max_level;
	int down_speed;
	s64 down_limit_ns;
	unsigned int max_freq;

	/* config: floor */
	int min_level;
	int up_speed;
	s64 up_limit_ns;
	unsigned int min_freq;
} cb_stuff[CLUS_MAX] = {
	/* silver */
	{
	},
	/* gold */
	{
		.enable        = true,
		.down_limit_ns = 50 * NSEC_PER_MSEC,
		.up_limit_ns   = 50 * NSEC_PER_MSEC,
		.limit_thres   = 30 * NSEC_PER_MSEC,
		.limit_freq    = 2054400,
		.limit_level   = 13,
		.down_speed    = 2,
		.up_speed      = 1,
	},
	/* gold prime */
	{
		.enable        = true,
		.down_limit_ns = 50 * NSEC_PER_MSEC,
		.up_limit_ns   = 50 * NSEC_PER_MSEC,
		.limit_thres   = 30 * NSEC_PER_MSEC,
		.limit_freq    = 2361600 ,
		.limit_level   = 14,
		.down_speed    = 2,
		.up_speed      = 1,
	}
};

/* init config */
static bool enable = false;
module_param_named(enable, enable, bool, 0644);

static bool debug = false;
module_param_named(debug, debug, bool, 0644);

static unsigned int decay = 80;
module_param_named(decay, decay, uint, 0644);

static DEFINE_PER_CPU(struct cpufreq_bouncing*, cbs);

static int cb_pol_idx = 0;

static bool cb_switch = false;

static inline struct cpufreq_bouncing* cb_get(int cpu)
{
	if (likely(cb_switch))
		return per_cpu(cbs, cpu);

	return NULL;
}

/* module parameters */
static int cb_config_store(const char *buf, const struct kernel_param *kp)
{
	/*
		for limit_thres, down/up limit will use ms as unit
		format:
		echo "2,0,5,3000,3,2000,3,2000" > config
	 */
	struct pack {
		int clus;
		bool enable;

		int limit_level;
		u64 limit_thres_ms;

		int down_speed;
		s64 down_limit_ms;

		int up_speed;
		s64 up_limit_ms;
	} v;
	struct cpufreq_bouncing *cb;

	if (debug)
		pr_info("%s\n", buf);

	if (sscanf(buf, "%d,%d,%d,%llu,%d,%lld,%d,%lld\n",
		&v.clus,
		&v.enable,
		&v.limit_level,
		&v.limit_thres_ms,
		&v.down_speed,
		&v.down_limit_ms,
		&v.up_speed,
		&v.up_limit_ms) != 8)
		goto out;

	if (v.clus < 0 || v.clus >= cb_pol_idx)
		goto out;

	cb = &cb_stuff[v.clus];

	if (v.limit_level < 0 || v.limit_level > cb->max_level)
		goto out;

	if (v.down_speed < 0 || v.down_speed > cb->freq_levels)
		goto out;

	if (v.up_speed < 0 || v.up_speed > cb->freq_levels)
		goto out;

	if (v.down_limit_ms < 0 || v.up_limit_ms < 0 ||
		v.limit_thres_ms < 0)
		goto out;

	/* begin update config */
	cb->enable = false;
	cb->last_ts = 0;
	cb->last_freq_update_ts = 0;
	cb->acc = 0;
	cb->limit_level = v.limit_level;
	if (cb->freqs)
		cb->limit_freq = cb->freqs[cb->limit_level];
	cb->limit_thres = MSEC_TO_NSEC(v.limit_thres_ms);
	cb->down_speed = v.down_speed;
	cb->down_limit_ns = MSEC_TO_NSEC(v.down_limit_ms);
	cb->up_speed = v.up_speed;
	cb->up_limit_ns = MSEC_TO_NSEC(v.up_limit_ms);
	cb->enable = v.enable;

	return 0;
out:
	pr_warn("config: invalid:%s\n", buf);
	return 0;
}

static struct kernel_param_ops cb_config_ops = {
	.set = cb_config_store,
};
module_param_cb(config, &cb_config_ops, NULL, 0220);

static int cb_dump_show(char *buf, const struct kernel_param *kp)
{
	struct cpufreq_bouncing *cb;
	int i, j, cnt = 0;

	for (i = 0; i < min(CLUS_MAX, cb_pol_idx); ++i) {
		cb = &cb_stuff[i];
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "=====\n");
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "clus %d (switch %d)\n", i, cb_switch);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "last_ts: %llu\n", cb->last_ts);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "last_freq_update_ts: %llu\n", cb->last_freq_update_ts);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "acc: %llu\n", cb->acc);

		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "limit_freq: %d\n", cb->limit_freq);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "limit_level: %d\n", cb->limit_level);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "limit_thres: %llu\n", cb->limit_thres);

		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "enable: %d\n", cb->enable);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "cur_level: %d\n", cb->cur_level);

		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "max_freq: %u %d\n", cb->max_freq, cb->max_level);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "down_speed: %d\n", cb->down_speed);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "down_limit_ns: %lld\n", cb->down_limit_ns);

		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "min_freq: %u %d\n", cb->min_freq, cb->min_level);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "up_speed: %d\n", cb->up_speed);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "up_limit_ns: %lld\n", cb->up_limit_ns);

		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "freq_sorting: %d\n", cb->freq_sorting);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "freq_levels: %d\n", cb->freq_levels);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "freq idx:");
		for (j = 0; j < cb->freq_levels; ++j)
			cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "\t%u", j);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "\n");
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "freq clk:");
		for (j = 0; j < cb->freq_levels; ++j)
			cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "\t%u", cb->freqs[j]);
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "\n");
	}

	return cnt;
}

static struct kernel_param_ops cb_dump_ops = {
	.get = cb_dump_show,
};
module_param_cb(dump, &cb_dump_ops, NULL, 0444);


unsigned int cb_get_cap(int cpu)
{
	struct cpufreq_bouncing *cb = cb_get(cpu);

	/* return UINT_MAX as no limited */
	if (unlikely(!cb))
		return UINT_MAX;

	if (unlikely(cb->freqs))
		return UINT_MAX;

	return cb->freqs[cb->cur_level];
}

unsigned int cb_cap(struct cpufreq_policy *pol, unsigned int freq)
{
	struct cpufreq_bouncing *cb;
	unsigned int capped = freq;
	int cpu;

	if (!enable)
		return freq;

	/* can remove since we're calling from sugov_fast_switch path */
	if (!pol->fast_switch_enabled)
		return freq;

	cpu = cpumask_first(pol->related_cpus);
	cb = cb_get(cpu);

	if (unlikely(!cb))
		return freq;

	if (!cb->enable)
		return freq;

	/* don't have quick mapping */
	if (unlikely(!cb->freqs))
		return freq;

	capped = min(freq, cb->freqs[cb->cur_level]);

	if (debug)
		pr_info("cpu %d, orig %u, capped %d\n", cpu, freq, capped);

	return capped;
}

static inline bool clus_isolated(int cpu)
{
	struct cpufreq_policy *pol = cpufreq_cpu_get_raw(cpu);
	int i;

	if (unlikely(!pol))
		return true;

	for_each_cpu(i, pol->related_cpus)
		if (!cpu_isolated(i))
			return false;

	return true;
}

void cb_reset(int cpu, u64 time)
{
	struct cpufreq_bouncing *cb;

	if (!enable)
		return;

	cb = cb_get(cpu);
	if (!cb)
		return;

	/* reset only when cluster has no active cpu */
	if (!clus_isolated(cpu))
		return;

	cb->last_ts = time;
	cb->last_freq_update_ts = time;
	cb->acc = 0;
	cb->cur_level = cb->max_level;
}

void cb_update(struct cpufreq_policy *pol, u64 time)
{
	// TODO can skip a bit?
	struct cpufreq_bouncing *cb;
	u64 delta, update_delta;
	int cpu;

	if (!enable)
		return;

	cpu = cpumask_first(pol->related_cpus);
	cb = cb_get(cpu);

	if (unlikely(!cb))
		return;

	if (!cb->enable)
		return;

	if (unlikely(!pol->fast_switch_enabled))
		return;

	/* for first update */
	if (unlikely(!cb->last_ts))
		cb->last_ts = cb->last_freq_update_ts = time;

	// FIXME check overflow
	// NOTE pol->cur should always updated
	delta = time - cb->last_ts;
	update_delta = time - cb->last_freq_update_ts;

	/* check cpufreq */
	if (pol->cur >= cb->limit_freq) {
		/* accumulate delta time */
		cb->acc += delta;
	} else {
		/* decay accumulate time */
		cb->acc = cb->acc * decay / 100;;
	}

	/* check if need to update limitation */
	if (cb->acc >= cb->limit_thres) {
		/* check last update */
		if (update_delta >= cb->down_limit_ns) {
			cb->cur_level -= cb->down_speed;
			cb->last_freq_update_ts = time;
		}
	} else {
		/* check last update */
		if (update_delta >= cb->up_limit_ns) {
			cb->cur_level += cb->up_speed;
			cb->last_freq_update_ts = time;
		}
	}

	/* cap level */
	cb->cur_level = max(cb->limit_level, min(cb->cur_level, cb->max_level));

	if (debug)
		pr_info("cpu %d update: ts now %llu last %llu last_update %llu delta %llu update_d %llu cur %u acc %llu, cur_level %d\n",
			cpu,
			NSEC_TO_MSEC(time),
			NSEC_TO_MSEC(cb->last_ts),
			NSEC_TO_MSEC(cb->last_freq_update_ts),
			NSEC_TO_MSEC(delta),
			NSEC_TO_MSEC(update_delta),
			pol->cur,
			NSEC_TO_MSEC(cb->acc),
			cb->cur_level);

	cb->last_ts = time;
}

static int __cpufreq_policy_parser(int cpu, int cb_idx)
{
	struct cpufreq_policy *pol = cpufreq_cpu_get_raw(cpu);
	struct cpufreq_frequency_table *table, *pos;
	struct cpufreq_bouncing* cb;

	unsigned int freq = 0, max_freq = 0, min_freq = UINT_MAX;
	int idx, freq_levels = 0;

	if (unlikely(!pol)) {
		pr_err("cpu %d can't find realted cpufreq policy\n", cpu);
		return -1;
	}

	if (cb_idx >= CLUS_MAX) {
		pr_err("clus %d out of limit\n", cb_idx);
		return -1;
	}

	cb = &cb_stuff[cb_idx];
	table = pol->freq_table;
	cb->freq_sorting = pol->freq_table_sorted;
	/* get freq_levels */
	cpufreq_for_each_valid_entry_idx(pos, table, idx) {
		++freq_levels;
		freq = pos->frequency;
		if (freq > max_freq) {
			max_freq = freq;
			cb->max_level = idx;
			cb->cur_level = idx;
			cb->max_freq = max_freq;
		}
		if (freq < min_freq) {
			min_freq = freq;
			cb->min_level = idx;
			cb->min_freq = min_freq;
		}
	}

	cb->freq_levels = freq_levels;
	cb->freqs = (unsigned int *) kmalloc(sizeof(unsigned int) * freq_levels, GFP_KERNEL);
	if (cb->freqs) {
		/* setup freqs */
		idx = 0;
		table = pol->freq_table;
		cpufreq_for_each_valid_entry_idx(pos, table, idx) {
			freq = pos->frequency;
			cb->freqs[idx] = freq;
		}
	} else {
		pr_err("can't alloc memory for freqs\n");
		cb->enable = false;
	}
	return cpu + cpumask_weight(pol->related_cpus);
}

static void cb_parse_cpufreq(void)
{
	int i = 0, j, prev = 0;
	bool valid = true;

	while (i != nr_cpu_ids && cb_pol_idx != CLUS_MAX) {
		i = __cpufreq_policy_parser(i, cb_pol_idx);
		if (i < 0)
			break;
		for (j = prev; j < i; ++j)
			per_cpu(cbs, j) = &cb_stuff[cb_pol_idx];
		prev = i;
		++cb_pol_idx;
	}

	for (i = 0; i < nr_cpu_ids && valid; ++i) {
		if (!per_cpu(cbs, i)) {
			pr_warn("break on cpu%d\n", i);
			valid = false;
		}
	}

	cb_switch = valid;
	smp_wmb();
}

static void cb_clean_up(void)
{
	struct cpufreq_bouncing *cb;
	int i;
	enable = false;
	smp_wmb();

	for (i = 0; i < CLUS_MAX; ++i) {
		cb = &cb_stuff[i];
		if (cb->freqs) {
			kfree(cb->freqs);
			cb->freqs = NULL;
		}
	}
}

static int __init cb_init(void)
{
	pr_info("cpufreq bouncing init\n");
	cb_parse_cpufreq();
	return 0;
}

static void __exit cb_exit(void)
{
	cb_clean_up();
	pr_info("cpufreq bouncing exit\n");
}
device_initcall(cb_init);

