// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/list.h>
#include <linux/jiffies.h>
#include <trace/events/sched.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/sort.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/spinlock.h>

#include "game_ctrl.h"

/*
 * render thread information
 */
struct render_thread_info_t {
	pid_t rt_tid;
	struct task_struct *rt_task;
	struct list_head waker_list;
} render_thread_info[MAX_RT_NUM];

/*
 * render thread waker information
 */
struct rt_waker_info_t {
	struct list_head node;
	pid_t waker_tid;
	u64 total;
	u32 increment;
	u64 last_wake_ts;
};

static DEFINE_RWLOCK(rt_info_rwlock);
static atomic_t need_stat_wake = ATOMIC_INIT(0);
static unsigned int rt_num = 0;

static struct rt_waker_info_t *g_waker_mempool = NULL;
static struct list_head g_mempool_list;
static struct list_head *g_mempool_list_ptr = &g_mempool_list;

static struct rt_waker_info_t *waker_mempool_create(void)
{
	struct rt_waker_info_t *waker_mempool = NULL;

	waker_mempool = kzalloc(sizeof(struct rt_waker_info_t) * MAX_TID_COUNT, GFP_ATOMIC);

	return waker_mempool;
}

static void waker_mempool_init(struct rt_waker_info_t *waker_mempool)
{
	int i;

	if (likely(waker_mempool)) {
		INIT_LIST_HEAD(g_mempool_list_ptr);
		for (i = 0; i < MAX_TID_COUNT; i++)
			list_add_tail(&waker_mempool[i].node, g_mempool_list_ptr);
	}
}

static struct rt_waker_info_t *waker_mempool_alloc_one(void)
{
	struct list_head *pos;
	struct rt_waker_info_t *waker = NULL;

	if (!list_empty(g_mempool_list_ptr)) {
		pos = g_mempool_list_ptr->next;
		list_del(pos);
		waker = list_entry(pos, struct rt_waker_info_t, node);
	}

	return waker;
}

static void waker_mempool_free_one(struct rt_waker_info_t *waker)
{
	if (likely(waker)) {
		list_del(&waker->node);
		list_add_tail(&waker->node, g_mempool_list_ptr);
	}
}

static void waker_mempool_destory(struct rt_waker_info_t *waker_mempool)
{
	if (likely(waker_mempool))
		kfree(waker_mempool);
}

static void add_rt_waker_stat(struct render_thread_info_t *info)
{
	struct list_head *pos;
	struct rt_waker_info_t *waker;
	struct rt_waker_info_t *new_waker;
	pid_t waker_tid = current->pid;
	u64 ts = ktime_get_ns();

	list_for_each(pos, &info->waker_list) {
		waker = list_entry(pos, struct rt_waker_info_t, node);
		if (waker->waker_tid == waker_tid) {
			waker->total++;
			waker->increment++;
			waker->last_wake_ts = ts;
			return;
		}
	}

	new_waker = waker_mempool_alloc_one();
	if (new_waker) {
		new_waker->waker_tid = waker_tid;
		new_waker->total = 1;
		new_waker->increment = 1;
		new_waker->last_wake_ts = ts;
		list_add_tail(&new_waker->node, &info->waker_list);
	}
}

void g_rt_try_to_wake_up(struct task_struct *task)
{
	int i;
	pid_t wakee_tid = task->pid;
	unsigned long flags;

	if (atomic_read(&need_stat_wake) == 0)
		return;

	/*
	 * only update waker stat when lock is available,
	 * if not available, skip these information
	 */
	if (write_trylock_irqsave(&rt_info_rwlock, flags)) {
		for (i = 0; i < rt_num; i++) {
			if (wakee_tid == render_thread_info[i].rt_tid) {
				/* waker and wakee belongs to same pid */
				if (current->tgid == render_thread_info[i].rt_task->tgid)
					add_rt_waker_stat(&render_thread_info[i]);
				break;
			}
		}
		write_unlock_irqrestore(&rt_info_rwlock, flags);
	}
}

void rt_task_dead(struct task_struct *task)
{
	int i;
	struct list_head *pos, *n;
	struct rt_waker_info_t *waker;
	unsigned long flags;

	if (atomic_read(&need_stat_wake) == 0)
		return;

	if (write_trylock_irqsave(&rt_info_rwlock, flags)) {
		for (i = 0; i < rt_num; i++) {
			if (task->tgid == render_thread_info[i].rt_tid) {
				list_for_each_safe(pos, n, &render_thread_info[i].waker_list) {
					waker = list_entry(pos, struct rt_waker_info_t, node);
					if (task->pid == waker->waker_tid) {
						waker_mempool_free_one(waker);
						break;
					}
				}
			}
		}
		write_unlock_irqrestore(&rt_info_rwlock, flags);
	}
}

/*
 *  Ascending order by increment
 */
static int cmp_task_wake_inc(const void *a, const void *b)
{
	struct rt_waker_info_t *prev, *next;

	prev = (struct rt_waker_info_t *)a;
	next = (struct rt_waker_info_t *)b;
	if (unlikely(!prev || !next))
		return 0;

	return next->increment - prev->increment;
}

static bool get_task_name(pid_t tid, char *name) {
	struct task_struct * task = NULL;
	bool ret = false;

	rcu_read_lock();
	task = find_task_by_vpid(tid);
	if (task) {
		strncpy(name, task->comm, TASK_COMM_LEN);
		ret = true;
	}
	rcu_read_unlock();

	return ret;
}

#define RT_PAGE_SIZE 2048
static int rt_info_show(struct seq_file *m, void *v)
{
	unsigned long flags;
	struct rt_waker_info_t *results1, *results2;
	int i, num1 = 0, num2 = 0;
	pid_t wakee1_tid, wakee2_tid;
	struct list_head *pos, *n;
	struct rt_waker_info_t *waker;
	char *page;
	char waker_name[TASK_COMM_LEN];
	ssize_t len = 0;
	u64 ts = ktime_get_ns();

	page = kzalloc(RT_PAGE_SIZE, GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	results1 = kmalloc(sizeof(struct rt_waker_info_t) * MAX_TID_COUNT, GFP_KERNEL);
	if (!results1) {
		kfree(page);
		return -ENOMEM;
	}

	read_lock_irqsave(&rt_info_rwlock, flags);
	if (rt_num >= 1) {
		wakee1_tid = render_thread_info[0].rt_tid;
		list_for_each_safe(pos, n, &render_thread_info[0].waker_list) {
			waker = list_entry(pos, struct rt_waker_info_t, node);
			if (waker->increment) {
				results1[num1].waker_tid = waker->waker_tid;
				results1[num1].total = waker->total;
				results1[num1].increment = waker->increment;
				waker->increment = 0;
				num1++;
			} else if (ts - waker->last_wake_ts > MAX_TASK_INACTIVE_TIME) {
				waker_mempool_free_one(waker);
			}
		}
	}
	if (rt_num >= 2 && likely(num1 < MAX_TID_COUNT)) {
		wakee2_tid = render_thread_info[1].rt_tid;
		results2 = &results1[num1];
		list_for_each_safe(pos, n, &render_thread_info[1].waker_list) {
			waker = list_entry(pos, struct rt_waker_info_t, node);
			if (waker->increment) {
				results2[num2].waker_tid = waker->waker_tid;
				results2[num2].total = waker->total;
				results2[num2].increment = waker->increment;
				waker->increment = 0;
				num2++;
				if (unlikely(num1 + num2 >= MAX_TID_COUNT))
					break;
			} else if (ts - waker->last_wake_ts > MAX_TASK_INACTIVE_TIME) {
				waker_mempool_free_one(waker);
			}
		}
	}
	read_unlock_irqrestore(&rt_info_rwlock, flags);

	if (num1 > 0) {
		sort(results1, num1, sizeof(struct rt_waker_info_t), &cmp_task_wake_inc, NULL);
		num1 = min(num1, MAX_TASK_NR);
		for (i = 0; i < num1; i++) {
			if (get_task_name(results1[i].waker_tid, waker_name)) {
				len += snprintf(page + len, RT_PAGE_SIZE - len, "%d;%s;%d;%d;%d\n",
					results1[i].waker_tid, waker_name, wakee1_tid,
					results1[i].total, results1[i].increment);
			}
		}
	}
	if (num2 > 0) {
		sort(results2, num2, sizeof(struct rt_waker_info_t), &cmp_task_wake_inc, NULL);
		num2 = min(num2, MAX_TASK_NR);
		for (i = 0; i < num2; i++) {
			if (get_task_name(results2[i].waker_tid, waker_name)) {
				len += snprintf(page + len, RT_PAGE_SIZE - len, "%d;%s;%d;%d;%d\n",
					results2[i].waker_tid, waker_name, wakee2_tid,
					results2[i].total, results2[i].increment);
			}
		}
	}
	if (len > 0)
		seq_puts(m, page);

	kfree(results1);
	kfree(page);

	return 0;
}

static int rt_info_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, rt_info_show, inode);
}

static ssize_t rt_info_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	unsigned long flags;
	int i, ret;
	char page[128] = {0};
	char *iter = page;
	struct task_struct *task;
	pid_t tid;
	pid_t tids[MAX_RT_NUM];

	if (count > sizeof(page) - 1)
		count = sizeof(page) - 1;
	if (copy_from_user(page, buf, count))
		return -EFAULT;

	atomic_set(&need_stat_wake, 0);

	write_lock_irqsave(&rt_info_rwlock, flags);

	for (i = 0; i < rt_num; i++) {
		if (render_thread_info[i].rt_task)
			put_task_struct(render_thread_info[i].rt_task);
	}

	rt_num = 0;
	while (iter != NULL) {
		/* input should be "123 234" */
		ret = sscanf(iter, "%d", &tid);
		if (ret != 1)
			break;

		iter = strchr(iter + 1, ' ');

		rcu_read_lock();
		task = find_task_by_vpid(tid);
		if (task)
			get_task_struct(task);
		rcu_read_unlock();

		if (task) {
			tids[rt_num] = tid;

			render_thread_info[rt_num].rt_tid = tid;
			render_thread_info[rt_num].rt_task = task;
			INIT_LIST_HEAD(&render_thread_info[rt_num].waker_list);

			rt_num++;
			if (rt_num >= MAX_RT_NUM)
				break;
		}
	}

	if (rt_num) {
		if (!g_waker_mempool) {
			g_waker_mempool = waker_mempool_create();
			if (!g_waker_mempool) {
				rt_num = 0;
				write_unlock_irqrestore(&rt_info_rwlock, flags);
				return -ENOMEM;
			}
		}
		waker_mempool_init(g_waker_mempool);
		atomic_set(&need_stat_wake, 1);
	} else {
		if (g_waker_mempool) {
			waker_mempool_destory(g_waker_mempool);
			g_waker_mempool = NULL;
		}
	}

	if (rt_num)
		rt_set_dstate_interested_threads(tids, rt_num);

	write_unlock_irqrestore(&rt_info_rwlock, flags);

	return count;
}

static const struct file_operations rt_info_proc_ops = {
	.open		= rt_info_proc_open,
	.write		= rt_info_proc_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int rt_num_show(struct seq_file *m, void *v)
{
	unsigned long flags;
	char page[256] = {0};
	int i, available = 0;
	struct list_head *pos;
	ssize_t len = 0;

	read_lock_irqsave(&rt_info_rwlock, flags);

	len += snprintf(page + len, sizeof(page) - len, "rt_num %d\n", rt_num);
	for (i = 0; i < rt_num; i++) {
		len += snprintf(page + len, sizeof(page) - len, "pid:%d tid:%d comm:%s\n",
			render_thread_info[i].rt_task->tgid, render_thread_info[i].rt_task->pid,
			render_thread_info[i].rt_task->comm);
	}
	if (rt_num) {
		list_for_each(pos, g_mempool_list_ptr)
			available++;
		len += snprintf(page + len, sizeof(page) - len,
			"waker_mempool total:%d available:%d\n", MAX_TID_COUNT, available);
	}

	read_unlock_irqrestore(&rt_info_rwlock, flags);

	seq_puts(m, page);

	return 0;
}

static int rt_num_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, rt_num_show, inode);
}

static const struct file_operations rt_num_proc_ops = {
	.open		= rt_num_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int rt_info_init()
{
	if (unlikely(!game_opt_dir))
		return -ENOTDIR;

	proc_create_data("render_thread_info", 0664, game_opt_dir, &rt_info_proc_ops, NULL);
	proc_create_data("rt_num", 0444, game_opt_dir, &rt_num_proc_ops, NULL);

	return 0;
}
