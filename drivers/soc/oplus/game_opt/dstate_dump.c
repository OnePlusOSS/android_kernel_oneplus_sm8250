// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <trace/events/sched.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>

#include "game_ctrl.h"

#define MAX_INTERESTED_THREAD_NUM 10
#define MAX_DSTATE_STACK_TRACE_DEPTH 32

static DEFINE_RAW_SPINLOCK(lock);
static atomic_t dump_enable = ATOMIC_INIT(1); /* default enable */
static atomic_t dstate_duration = ATOMIC_INIT(5); /* default 5ms */
static pid_t rt_interested_threads[MAX_RT_NUM];
static int rt_thread_num = 0;
static pid_t interested_threads[MAX_INTERESTED_THREAD_NUM];
static int thread_num = 0;
static struct proc_dir_entry *dstate_dir = NULL;

static void g_dump_waker_stack(unsigned int waker_nr_entries, unsigned long *waker_entries)
{
	int i;
	char buf[128];

	for (i = waker_nr_entries - 1; i >= 0; i--) {
		snprintf(buf, sizeof(buf), "%pS\n", (void *)waker_entries[i]);
		trace_printk(buf);
	}
}

static void g_dump_wakee_stack(unsigned int wakee_nr_entries, unsigned long *wakee_entries)
{
	int i;
	char buf[128];

	for (i = wakee_nr_entries - 1; i >= 0; i--) {
		snprintf(buf, sizeof(buf), "%pS\n", (void *)wakee_entries[i]);
		trace_printk(buf);
	}
}

static void g_dstate_dump_stack(struct task_struct *task, u64 delay_ms)
{
	void * caller;
	unsigned int waker_nr_entries;
	unsigned long waker_entries[MAX_DSTATE_STACK_TRACE_DEPTH];
	unsigned int wakee_nr_entries;
	unsigned long wakee_entries[MAX_DSTATE_STACK_TRACE_DEPTH];
	char buf[256];

	struct stack_trace waker_dummy;
	struct stack_trace wakee_dummy;
	waker_dummy.nr_entries = 0;
	waker_dummy.max_entries = MAX_DSTATE_STACK_TRACE_DEPTH;
	waker_dummy.entries = &waker_entries[0];
	waker_dummy.skip = 0;
	save_stack_trace(&waker_dummy);
	waker_nr_entries = waker_dummy.nr_entries;

	wakee_dummy.nr_entries = 0;
	wakee_dummy.max_entries = MAX_DSTATE_STACK_TRACE_DEPTH;
	wakee_dummy.entries = &wakee_entries[0];
	wakee_dummy.skip = 0;
	save_stack_trace_tsk(task, &wakee_dummy);
	wakee_nr_entries = wakee_dummy.nr_entries;
	caller = (void *)get_wchan(task);

	snprintf(buf, sizeof(buf), "delay_ms=%d, waker_nr_entries=%d, wakee_nr_entries=%d,"
		" waker:%s tid:%d pid:%d, wakee:%s tid:%d pid:%d, blocked_func=%pS\n",
		delay_ms, waker_nr_entries, wakee_nr_entries,
		current->comm, current->pid, current->tgid, task->comm, task->pid, task->tgid, caller);
	trace_printk(buf);

	g_dump_waker_stack(waker_nr_entries, waker_entries);
	g_dump_wakee_stack(wakee_nr_entries, wakee_entries);
}

void rt_set_dstate_interested_threads(pid_t *tids, int num) {
	int i;

	num = min(num, MAX_RT_NUM);
	raw_spin_lock(&lock);
	for (i = 0; i < num; i++)
		rt_interested_threads[i] = tids[i];
	rt_thread_num = num;
	raw_spin_unlock(&lock);
}

static bool is_dstate_interested_threads(pid_t tid) {
	int i;
	bool ret = false;

	raw_spin_lock(&lock);
	for (i = 0; i < rt_thread_num; i++) {
		if (tid == rt_interested_threads[i]) {
			ret = true;
			goto unlock;
		}
	}
	for (i = 0; i < thread_num; i++) {
		if (tid == interested_threads[i]) {
			ret = true;
			break;
		}
	}
unlock:
	raw_spin_unlock(&lock);

	return ret;
}

void g_sched_stat_blocked(struct task_struct *task, u64 delay_ns)
{
	u64 delay_ms = delay_ns >> 20;

	if (atomic_read(&dump_enable) &&
		!task->in_iowait &&
		delay_ms >= atomic_read(&dstate_duration) &&
		is_dstate_interested_threads(task->pid)) {
		g_dstate_dump_stack(task, delay_ms);
	}
}

static ssize_t dump_enable_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char page[32] = {0};
	int ret, enable;

	ret = simple_write_to_buffer(page, sizeof(page), ppos, buf, count);
	if (ret <= 0)
		return -EINVAL;

	ret = sscanf(page, "%d", &enable);
	if (ret != 1)
		return -EINVAL;

	if (enable > 0)
		atomic_set(&dump_enable, 1);
	else
		atomic_set(&dump_enable, 0);

	return count;
}

static ssize_t dump_enable_proc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	char page[32] = {0};
	int len;

	len = sprintf(page, "%d\n", atomic_read(&dump_enable));
	return simple_read_from_buffer(buf, count, ppos, page, len);
}

static const struct file_operations dump_enable_proc_ops = {
	.write		= dump_enable_proc_write,
	.read		= dump_enable_proc_read,
};

static ssize_t duration_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char page[32] = {0};
	int ret, duration;

	ret = simple_write_to_buffer(page, sizeof(page), ppos, buf, count);
	if (ret <= 0)
		return -EINVAL;

	ret = sscanf(page, "%d", &duration);
	if (ret != 1)
		return -EINVAL;

	if (duration >= 0)
		atomic_set(&dstate_duration, duration);

	return count;
}

static ssize_t duration_proc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	char page[32] = {0};
	int len;

	len = sprintf(page, "%d\n", atomic_read(&dstate_duration));

	return simple_read_from_buffer(buf, count, ppos, page, len);
}

static const struct file_operations duration_proc_ops = {
	.write		= duration_proc_write,
	.read		= duration_proc_read,
};

static ssize_t interested_tids_proc_write(struct file *file, const char __user *buf,
					size_t count, loff_t *ppos)
{
	char page[256] = {0};
	char *iter = page;
	pid_t tid;
	pid_t tids[MAX_INTERESTED_THREAD_NUM];
	int num, i, ret;

	if (count > sizeof(page) - 1)
		count = sizeof(page) - 1;
	if (copy_from_user(page, buf, count))
		return -EFAULT;

	num = 0;
	while (iter != NULL) {
		/* input should be "123 234 ..." */
		ret = sscanf(iter, "%d", &tid);
		if (ret != 1)
			break;

		iter = strchr(iter + 1, ' ');

		tids[num++] = tid;
		if (num >= MAX_INTERESTED_THREAD_NUM)
			break;
	}

	raw_spin_lock(&lock);
	for (i = 0; i < num; i++)
		interested_threads[i] = tids[i];
	thread_num = num;
	raw_spin_unlock(&lock);

	return count;
}

static ssize_t interested_tids_proc_read(struct file *file, char __user *buf,
					size_t count, loff_t *ppos)
{
	char page[256] = {0};
	ssize_t len = 0;
	int i;

	raw_spin_lock(&lock);
	for (i = 0; i < thread_num; i++)
		len += snprintf(page + len, sizeof(page) - len, "%d ", interested_threads[i]);
	if (thread_num > 0)
		len += snprintf(page + len, sizeof(page) - len, "\n");
	else
		len += snprintf(page + len, sizeof(page) - len, "0\n");
	raw_spin_unlock(&lock);

	return simple_read_from_buffer(buf, count, ppos, page, len);
}

static const struct file_operations interested_tids_proc_ops = {
	.write		= interested_tids_proc_write,
	.read		= interested_tids_proc_read,
};

int dstate_dump_init(void)
{
	if (unlikely(!game_opt_dir))
		return -ENOTDIR;

	dstate_dir = proc_mkdir("dstate", game_opt_dir);
	if (!dstate_dir) {
		pr_err("fail to mkdir /proc/game_opt/dstate\n");
		return -ENOMEM;
	}

	proc_create_data("dump_enable", 0664, dstate_dir, &dump_enable_proc_ops, NULL);
	proc_create_data("duration", 0664, dstate_dir, &duration_proc_ops, NULL);
	proc_create_data("interested_tids", 0664, dstate_dir, &interested_tids_proc_ops, NULL);

	return 0;
}
