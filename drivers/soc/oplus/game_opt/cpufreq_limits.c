// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

#include "game_ctrl.h"

/* To handle cpufreq min/max request */
struct cpu_freq_status {
	unsigned int min;
	unsigned int max;
};

static DEFINE_PER_CPU(struct cpu_freq_status, game_cpu_stats);

static cpumask_var_t limit_mask_min;
static cpumask_var_t limit_mask_max;

static ssize_t set_cpu_min_freq(const char *buf, size_t count)
{
	int i, j, ntokens = 0;
	unsigned int val, cpu;
	unsigned int min_freq;
	const char *cp = buf;
	struct cpu_freq_status *i_cpu_stats;
	struct cpufreq_policy policy;

	while ((cp = strpbrk(cp + 1, " :")))
		ntokens++;

	/* CPU:value pair */
	if (!(ntokens % 2))
		return -EINVAL;

	cp = buf;
	cpumask_clear(limit_mask_min);
	for (i = 0; i < ntokens; i += 2) {
		if (sscanf(cp, "%u:%u", &cpu, &val) != 2)
			return -EINVAL;
		if (cpu > (num_present_cpus() - 1))
			return -EINVAL;

		i_cpu_stats = &per_cpu(game_cpu_stats, cpu);

		i_cpu_stats->min = val;
		cpumask_set_cpu(cpu, limit_mask_min);

		cp = strnchr(cp, strlen(cp), ' ');
		cp++;
	}

	/*
	 * Since on synchronous systems policy is shared amongst multiple
	 * CPUs only one CPU needs to be updated for the limit to be
	 * reflected for the entire cluster. We can avoid updating the policy
	 * of other CPUs in the cluster once it is done for at least one CPU
	 * in the cluster
	 */
	get_online_cpus();
	for_each_cpu(i, limit_mask_min) {
		i_cpu_stats = &per_cpu(game_cpu_stats, i);
		min_freq = i_cpu_stats->min;

		if (cpufreq_get_policy(&policy, i))
			continue;

		if (cpu_online(i) && (policy.min != i_cpu_stats->min))
			cpufreq_update_policy(i);

		for_each_cpu(j, policy.related_cpus) {
			cpumask_clear_cpu(j, limit_mask_min);
			i_cpu_stats = &per_cpu(game_cpu_stats, j);
			i_cpu_stats->min = min_freq;
		}
	}
	put_online_cpus();

	return count;
}

static ssize_t cpu_min_freq_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char page[256] = {0};
	int ret;

	ret = simple_write_to_buffer(page, sizeof(page) - 1, ppos, buf, count);
	if (ret <= 0)
		return ret;

	return set_cpu_min_freq(page, ret);
}

static int cpu_min_freq_show(struct seq_file *m, void *v)
{
	int cpu;

	for_each_present_cpu(cpu)
		seq_printf(m, "%d:%u ", cpu, per_cpu(game_cpu_stats, cpu).min);
	seq_printf(m, "\n");

	return 0;
}

static int cpu_min_freq_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, cpu_min_freq_show, inode);
}

static const struct file_operations cpu_min_freq_proc_ops = {
	.open		= cpu_min_freq_proc_open,
	.write 		= cpu_min_freq_proc_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static ssize_t set_cpu_max_freq(const char *buf, size_t count)
{
	int i, j, ntokens = 0;
	unsigned int val, cpu;
	unsigned int max_freq;
	const char *cp = buf;
	struct cpu_freq_status *i_cpu_stats;
	struct cpufreq_policy policy;

	while ((cp = strpbrk(cp + 1, " :")))
		ntokens++;

	/* CPU:value pair */
	if (!(ntokens % 2))
		return -EINVAL;

	cp = buf;
	cpumask_clear(limit_mask_max);
	for (i = 0; i < ntokens; i += 2) {
		if (sscanf(cp, "%u:%u", &cpu, &val) != 2)
			return -EINVAL;
		if (cpu > (num_present_cpus() - 1))
			return -EINVAL;

		i_cpu_stats = &per_cpu(game_cpu_stats, cpu);

		i_cpu_stats->max = val;
		cpumask_set_cpu(cpu, limit_mask_max);

		cp = strnchr(cp, strlen(cp), ' ');
		cp++;
	}

	get_online_cpus();
	for_each_cpu(i, limit_mask_max) {
		i_cpu_stats = &per_cpu(game_cpu_stats, i);
		max_freq = i_cpu_stats->max;
		if (cpufreq_get_policy(&policy, i))
			continue;

		if (cpu_online(i) && (policy.max != i_cpu_stats->max))
			cpufreq_update_policy(i);

		for_each_cpu(j, policy.related_cpus) {
			cpumask_clear_cpu(j, limit_mask_max);
			i_cpu_stats = &per_cpu(game_cpu_stats, j);
			i_cpu_stats->max = max_freq;
		}
	}
	put_online_cpus();

	return count;
}

static ssize_t cpu_max_freq_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char page[256] = {0};
	int ret;

	ret = simple_write_to_buffer(page, sizeof(page) - 1, ppos, buf, count);
	if (ret <= 0)
		return ret;

	return set_cpu_max_freq(page, ret);
}

static int cpu_max_freq_show(struct seq_file *m, void *v)
{
	int cpu;

	for_each_present_cpu(cpu)
		seq_printf(m, "%d:%u ", cpu, per_cpu(game_cpu_stats, cpu).max);
	seq_printf(m, "\n");

	return 0;
}

static int cpu_max_freq_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, cpu_max_freq_show, inode);
}

static const struct file_operations cpu_max_freq_proc_ops = {
	.open		= cpu_max_freq_proc_open,
	.write 		= cpu_max_freq_proc_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int game_stat_adjust_notify(struct notifier_block *nb, unsigned long val,
							void *data)
{
	struct cpufreq_policy *policy = data;
	unsigned int cpu = policy->cpu;
	struct cpu_freq_status *cpu_st = &per_cpu(game_cpu_stats, cpu);
	unsigned int min = cpu_st->min, max = cpu_st->max;

	if (val != CPUFREQ_ADJUST)
		return NOTIFY_OK;

	pr_debug("game_cpu_stats: CPU%u policy before: %u:%u kHz\n", cpu,
						policy->min, policy->max);
	pr_debug("game_cpu_stats: CPU%u seting min:max %u:%u kHz\n", cpu, min, max);

	cpufreq_verify_within_limits(policy, min, max);

	pr_debug("game_cpu_stats: CPU%u policy after: %u:%u kHz\n", cpu,
						policy->min, policy->max);

	return NOTIFY_OK;
}

static struct notifier_block game_stat_cpufreq_nb = {
	.notifier_call = game_stat_adjust_notify,
};

int cpufreq_limits_init()
{
	unsigned int cpu;

	if (unlikely(!game_opt_dir))
		return -ENOTDIR;

	if (!alloc_cpumask_var(&limit_mask_min, GFP_KERNEL))
		return -ENOMEM;

	if (!alloc_cpumask_var(&limit_mask_max, GFP_KERNEL)) {
		free_cpumask_var(limit_mask_min);
		return -ENOMEM;
	}
	cpufreq_register_notifier(&game_stat_cpufreq_nb, CPUFREQ_POLICY_NOTIFIER);

	for_each_present_cpu(cpu) {
		per_cpu(game_cpu_stats, cpu).max = UINT_MAX;
		per_cpu(game_cpu_stats, cpu).min = 0;
	}

	proc_create_data("cpu_min_freq", 0664, game_opt_dir, &cpu_min_freq_proc_ops, NULL);
	proc_create_data("cpu_max_freq", 0664, game_opt_dir, &cpu_max_freq_proc_ops, NULL);

	return 0;
}
