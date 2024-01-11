// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/kernel_stat.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/energy_model.h>
#include <linux/sched/task.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/time64.h>
#include <linux/math64.h>
#include <linux/mutex.h>
#include <linux/cpumask.h>
#include <linux/sched/core_ctl.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <../../fs/proc/internal.h>
#include <linux/reciprocal_div.h>
#include "../../kernel/sched/sched.h"
#include "task_overload.h"

#define MAX_PID	(32768)
#define CLUSTER_NUM (3)

/* the max size of abnormal task in every collection period for midas*/
#define MAX_SIZE (1024)


/* UP_THRESH: the upper time percent to setting affinity
 *            for power abnormal task
 * DOWN_THRESH : the floor time percent to releasing affinity
 *               for power abnormal task
 */
#define TRIGGER_FREQ	(1300000)
#define TRIGGER_UTIL	(80)
#define CPU_THRESHOLD	(85)
#define CPU_CAP	(100)
#define SA_CGROUP_BACKGROUND (3)

#define ABNORMAL_MIN_CHECK (100)

#define SYSTEM_APP_UID (1000)
#define ROOT_APP_UID (0)
#define CPU_MAX 8

static DEFINE_SPINLOCK(tol_lock);
/* add for midas collection */
static int atd_count = 0;

static int golden_cpu = 0;
static int golden_cpu_first = 0;
static int goplus_cpu = 0;

int sysctl_abnormal_enable = 0;

static struct abnormal_tsk_info task_info[MAX_SIZE];

static struct proc_dir_entry *parent;

extern unsigned int walt_scale_demand_divisor;

#define scale_demand(d) ((d)/walt_scale_demand_divisor)

#define IS_ROOT_UID(uid) (uid == 0)
#define IS_SYSTEM_UID(uid) (uid == 1000)

bool test_task_uid(struct task_struct *task)
{
	int cur_uid = 0;
	cur_uid = task_uid(task).val;
	if (IS_ROOT_UID(cur_uid) || IS_SYSTEM_UID(cur_uid))
		return false;
	return true;
}

int get_task_cgroup_id(struct task_struct *task)
{
	struct cgroup_subsys_state *css = task_css(task, schedtune_cgrp_id);
	return css ? css->id : -1;
}

bool test_task_bg(struct task_struct *task)
{
	return (SA_CGROUP_BACKGROUND == get_task_cgroup_id(task)) ? 1 : 0;
}

bool check_skip_task_goplus(struct task_struct *task, unsigned int prev_cpu, unsigned int new_cpu)
{
	if (sysctl_abnormal_enable && (new_cpu != -1) && (new_cpu != prev_cpu) && (capacity_orig_of(new_cpu) < capacity_orig_of(prev_cpu))
				&& (task->abnormal_flag > ABNORMAL_THRESHOLD))
		return true;
	return false;
}

bool check_abnormal_freq(struct task_struct *p)
{
	unsigned int freq = 0;
	unsigned int freq_max = 0;
	freq = cpufreq_quick_get(goplus_cpu);
	freq_max = cpufreq_quick_get_max(goplus_cpu);

	if (freq > TRIGGER_FREQ && freq == freq_max) {
		return true;
	} else {
		if (p->abnormal_flag <= ABNORMAL_THRESHOLD && p->abnormal_flag > ABNORMAL_MIN_CHECK)
			p->abnormal_flag -= ABNORMAL_TIME;
	}

	return false;
}

bool check_abnormal_task_util(struct task_struct *p)
{
	int cpu;
	unsigned long thresh_load;
	struct reciprocal_value spc_rdiv = reciprocal_value(100);

	if (!p)
		return false;

	if (test_task_bg(p) && p->abnormal_flag > ABNORMAL_MIN_CHECK) {
		p->abnormal_flag -= ABNORMAL_TIME;
		return false;
	}
	cpu = task_cpu(p);
	if (is_max_capacity_cpu(cpu)) {
		thresh_load = capacity_orig_of(cpu) * TRIGGER_UTIL;
		if (task_util(p) >  reciprocal_divide(thresh_load, spc_rdiv))
			return true;
	}
	if (p->abnormal_flag <= ABNORMAL_THRESHOLD && p->abnormal_flag > ABNORMAL_MIN_CHECK)
						p->abnormal_flag -= ABNORMAL_TIME;
	return false;
}

bool check_abnormal_cpu_util(void)
{
	int i;
	int goden_sum = 0;
	int goden_cap = 0;

	for (i = 0; i < golden_cpu; i++) {
		goden_sum += cpu_util(golden_cpu_first + i) * CPU_CAP;
		goden_cap += capacity_orig_of(golden_cpu_first + i) * CPU_CAP / 2;
	}
	if ((capacity_orig_of(goplus_cpu) * CPU_THRESHOLD) < (cpu_util(goplus_cpu) * CPU_CAP) && goden_cap > goden_sum)
		return true;
	else
		return false;
}

void set_task_state(struct task_struct *p)
{
	struct abnormal_tsk_info *tsk;
	struct timeval boot_time;
	unsigned long flags;

	if (p->pid >= MAX_PID || atd_count >= MAX_SIZE)
		return;
	spin_lock_irqsave(&tol_lock, flags);
	tsk = task_info + atd_count;
	tsk->pid = p->pid;
	tsk->uid = task_uid(p).val;
	memcpy(tsk->comm, p->comm, TASK_COMM_LEN);
	tsk->limit_flag = sysctl_abnormal_enable;
	do_gettimeofday(&boot_time);
	tsk->date = boot_time.tv_sec * 1000 + boot_time.tv_usec / 1000;
	atd_count = atd_count + 1;
	spin_unlock_irqrestore(&tol_lock, flags);
}

void tol_init_cpus(void)
{
	int cpu;

	for (cpu = 0; cpu < CPU_MAX; cpu++) {
		if (!is_max_capacity_cpu(cpu) && !is_min_capacity_cpu(cpu)) {
			golden_cpu++;
			if (golden_cpu_first > cpu || golden_cpu_first == 0)
				golden_cpu_first = cpu;
		}
		if (is_max_capacity_cpu(cpu))
			goplus_cpu = cpu;
	}
}

static int tsk_show(struct seq_file *m, void *v)
{
	int idx;
	unsigned long flags;

	seq_printf(m, "pid\tuid\tlimit_flag\tcomm\tdate\n");

	spin_lock_irqsave(&tol_lock, flags);
	for (idx = 0; idx < min_t(int, atd_count, MAX_SIZE); idx++) {
		struct abnormal_tsk_info *tsk = task_info + idx;
		seq_printf(m, "%-5d\t%-5d\t%-5d\t%-16s\t%ld\n", tsk->pid, tsk->uid, tsk->limit_flag, tsk->comm, tsk->date);
	}
	spin_unlock_irqrestore(&tol_lock, flags);
	return 0;
}

static int tsk_open(struct inode *inode, struct file *file)
{
	return single_open(file, tsk_show, NULL);
}

static void tsk_clear(void)
{
	atd_count = 0;
}

static ssize_t tsk_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	if (!strcmp(buffer, "FINISH_READ")) {
		tsk_clear();
	}
	return count;
}

static ssize_t proc_skip_goplus_enabled_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[8];
	int err, val;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	buffer[count] = '\0';
	err = kstrtoint(strstrip(buffer), 10, &val);
	if (err)
		return err;

	sysctl_abnormal_enable = val;

	return count;
}

static ssize_t proc_skip_goplus_enabled_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[20];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "debug_enabled=%d\n", sysctl_abnormal_enable);

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
static const struct file_operations tsk_proc_fops = {
	.open		= tsk_open,
	.read		= seq_read,
	.write      = tsk_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations proc_skip_goplus_enabled_fops = {
	.read		= proc_skip_goplus_enabled_read,
	.write      = proc_skip_goplus_enabled_write,
	.llseek		= default_llseek,
};
#else
static const struct proc_ops tsk_proc_fops = {
	.proc_open		= tsk_open,
	.proc_read		= seq_read,
	.proc_write     = tsk_write,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
};

static const struct proc_ops proc_skip_goplus_enabled_fops = {
	.proc_write		= proc_skip_goplus_enabled_write,
	.proc_read		= proc_skip_goplus_enabled_read,
	.proc_lseek		= default_llseek,
};
#endif


static int __init proc_task_overload_init(void)
{
	struct proc_dir_entry *pentry;

	parent = proc_mkdir("task_overload", NULL);
	if (!parent) {
		goto ERROR_INIT_DIR;
	}

	pentry = proc_create("abnormal_task", 0, parent, &tsk_proc_fops);
	if (!pentry) {
		pr_err("create abnormal_task proc failed\n");
		goto ERROR_INIT_PROC;
	}
	pentry = proc_create("skip_goplus_enabled", 0666, parent, &proc_skip_goplus_enabled_fops);
	if (!pentry) {
		pr_err("create skip_goplus_enabled proc failed\n");
	}
	tol_init_cpus();
	return 0;

ERROR_INIT_PROC:
	remove_proc_entry("task_overload", NULL);
ERROR_INIT_DIR:
	pr_err("can't create task_overload proc\n");
	return -ENOENT;
}

module_init(proc_task_overload_init);
