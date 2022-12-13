// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/syscore_ops.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/cpufreq.h>
#include <linux/sched/cpufreq.h>

#include "game_ctrl.h"

struct task_runtime_info {
	struct list_head node;
	pid_t tid;
	u64 sum_exec_scale;
	u64 last_update_ts;
};

static LIST_HEAD(running_task_list);

/* exclusive file ops */
static DEFINE_MUTEX(g_mutex);
static DEFINE_RAW_SPINLOCK(g_lock);

static atomic_t need_stat_runtime = ATOMIC_INIT(0);
static struct task_struct *game_leader = NULL;
static u64 window_start;

static struct task_runtime_info *g_rinfo_mempool = NULL;
static struct list_head g_mempool_list;
static struct list_head *g_mempool_list_ptr = &g_mempool_list;

static struct task_runtime_info *rinfo_mempool_create(void)
{
	struct task_runtime_info *rinfo_mempool = NULL;

	rinfo_mempool = kzalloc(sizeof(struct task_runtime_info) * MAX_TID_COUNT, GFP_ATOMIC);

	return rinfo_mempool;
}

static void rinfo_mempool_init(struct task_runtime_info *rinfo_mempool)
{
	int i;

	if (likely(rinfo_mempool)) {
		INIT_LIST_HEAD(g_mempool_list_ptr);
		for (i = 0; i < MAX_TID_COUNT; i++)
			list_add_tail(&rinfo_mempool[i].node, g_mempool_list_ptr);
	}
}

static struct task_runtime_info *rinfo_mempool_alloc_one(void)
{
	struct list_head *pos;
	struct task_runtime_info *rinfo = NULL;

	if (!list_empty(g_mempool_list_ptr)) {
		pos = g_mempool_list_ptr->next;
		list_del(pos);
		rinfo = list_entry(pos, struct task_runtime_info, node);
	}

	return rinfo;
}

static void rinfo_mempool_free_one(struct task_runtime_info *rinfo)
{
	if (likely(rinfo)) {
		list_del(&rinfo->node);
		list_add_tail(&rinfo->node, g_mempool_list_ptr);
	}
}

static void rinfo_mempool_destory(struct task_runtime_info *rinfo_mempool)
{
	if (likely(rinfo_mempool))
		kfree(rinfo_mempool);
}

static ssize_t game_pid_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char page[32] = {0};
	int ret, pid;
	struct task_struct *leader = NULL;
	unsigned long flags;

	mutex_lock(&g_mutex);

	ret = simple_write_to_buffer(page, sizeof(page), ppos, buf, count);
	if (ret <= 0)
		goto munlock;

	ret = sscanf(page, "%d", &pid);
	if (ret != 1) {
		ret = -EINVAL;
		goto munlock;
	}

	atomic_set(&need_stat_runtime, 0);

	raw_spin_lock_irqsave(&g_lock, flags);

	/* release */
	if (pid <= 0) {
		if (game_leader) {
			put_task_struct(game_leader);
			game_leader = NULL;
			INIT_LIST_HEAD(&running_task_list);
			rinfo_mempool_destory(g_rinfo_mempool);
			g_rinfo_mempool = NULL;
		}
		ret = count;
		goto rsunlock;
	}

	/* acquire */
	rcu_read_lock();
	leader = find_task_by_vpid(pid);
	if (!leader || leader->pid != leader->tgid) { /* must be process id */
		rcu_read_unlock();
		ret = -EINVAL;
		goto rsunlock;
	} else {
		if (game_leader)
			put_task_struct(game_leader);
		game_leader = leader;
		get_task_struct(game_leader);
		rcu_read_unlock();
	}

	if (!g_rinfo_mempool) {
		g_rinfo_mempool = rinfo_mempool_create();
		if (!g_rinfo_mempool) {
			put_task_struct(game_leader);
			game_leader = NULL;
			ret = -ENOMEM;
			goto rsunlock;
		}
	}
	rinfo_mempool_init(g_rinfo_mempool);

	INIT_LIST_HEAD(&running_task_list);
	window_start = ktime_get_ns();
	atomic_set(&need_stat_runtime, 1);

	ret = count;
rsunlock:
	raw_spin_unlock_irqrestore(&g_lock, flags);
munlock:
	mutex_unlock(&g_mutex);
	return ret;
}

static ssize_t game_pid_proc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	char page[32] = {0};
	int len, ret, pid;

	mutex_lock(&g_mutex);

	if (game_leader)
		pid = game_leader->pid;
	else
		pid = -1;

	len = sprintf(page, "%d\n", pid);

	ret = simple_read_from_buffer(buf, count, ppos, page, len);

	mutex_unlock(&g_mutex);
	return ret;
}

static const struct file_operations game_pid_proc_ops = {
	.write		= game_pid_proc_write,
	.read		= game_pid_proc_read,
};

/*
 *  Ascending order by util
 */
static int cmp_task_util(const void *a, const void *b)
{
	struct task_util_info *prev, *next;

	prev = (struct task_util_info *)a;
	next = (struct task_util_info *)b;
	if (unlikely(!prev || !next))
		return 0;

	return next->util - prev->util;
}

static inline u16 cal_util(u64 sum_exec_scale, u64 window_size)
{
	u16 util;

	util = sum_exec_scale / (window_size >> 10);
	if (util > 1024)
		util = 1024;

	return util;
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

static int heavy_task_info_show(struct seq_file *m, void *v)
{
	unsigned long flags;
	struct task_util_info *results;
	struct list_head *pos, *n;
	struct task_runtime_info *rinfo;
	int i, num = 0, ret = 0;
	char page[1024] = {0};
	char task_name[TASK_COMM_LEN];
	ssize_t len = 0;
	u64 now;

	mutex_lock(&g_mutex);

	if (!game_leader) {
		ret = -ESRCH;
		goto munlock;
	}

	results = kmalloc(sizeof(struct task_util_info) * MAX_TID_COUNT, GFP_KERNEL);
	if (!results) {
		ret = -ENOMEM;
		goto munlock;
	}

	atomic_set(&need_stat_runtime, 0);

	raw_spin_lock_irqsave(&g_lock, flags);
	now = ktime_get_ns();

	list_for_each_safe(pos, n, &running_task_list) {
		rinfo = list_entry(pos, struct task_runtime_info, node);
		if (now - rinfo->last_update_ts < MAX_TASK_INACTIVE_TIME) {
			results[num].tid = rinfo->tid;
			results[num].util = cal_util(rinfo->sum_exec_scale, now - window_start);
			rinfo->sum_exec_scale = 0;
			num++;
		} else {
			rinfo_mempool_free_one(rinfo);
		}
	}
	raw_spin_unlock_irqrestore(&g_lock, flags);

	sort(results, num, sizeof(struct task_util_info), &cmp_task_util, NULL);
	num = min(num, MAX_TASK_NR);
	for (i = 0; i < num; i++) {
		if (results[i].util <= 0)
			break;
		if (get_task_name(results[i].tid, task_name)) {
			len += snprintf(page + len, sizeof(page) - len, "%d;%s;%d\n",
				results[i].tid, task_name, results[i].util);
		}
	}
	if (len > 0)
		seq_puts(m, page);

	kfree(results);

	now = ktime_get_ns();
	window_start = now;
	atomic_set(&need_stat_runtime, 1);

munlock:
	mutex_unlock(&g_mutex);
	return ret;
}

static int heavy_task_info_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, heavy_task_info_show, inode);
}

static const struct file_operations heavy_task_info_proc_ops = {
	.open		= heavy_task_info_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static inline unsigned int get_cur_freq(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get_raw(cpu);

	return (policy == NULL) ? 0 : policy->cur;
}

static inline unsigned int get_max_freq(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get_raw(cpu);

	return (policy == NULL) ? 0 : policy->cpuinfo.max_freq;
}

#define DIV64_U64_ROUNDUP(X, Y) div64_u64((X) + (Y - 1), Y)
static inline u64 scale_exec_time(u64 delta, struct rq *rq)
{
	u64 task_exec_scale;
	unsigned int cur_freq, max_freq;
	int cpu = cpu_of(rq);

	cur_freq = get_cur_freq(cpu);
	max_freq = get_max_freq(cpu);

	if (unlikely(cur_freq <= 0) || unlikely(max_freq <= 0) || unlikely(cur_freq > max_freq))
		return delta;

	task_exec_scale = DIV64_U64_ROUNDUP(cur_freq *
				arch_scale_cpu_capacity(NULL, cpu),
				max_freq);

	return (delta * task_exec_scale) >> 10;
}

void g_update_task_runtime(struct task_struct *task, u64 runtime)
{
	unsigned long flags;
	u64 now, exec_scale;
	struct list_head *pos;
	struct task_runtime_info *rinfo;
	struct task_runtime_info *new_rinfo;
	struct rq *rq = task_rq(task);

	if (atomic_read(&need_stat_runtime) == 0)
		return;

	raw_spin_lock_irqsave(&g_lock, flags);

	if (!game_leader || task->tgid != game_leader->tgid)
		goto rsunlock;

	now = ktime_get_ns();
	exec_scale = scale_exec_time(runtime, rq);

	list_for_each(pos, &running_task_list) {
		rinfo = list_entry(pos, struct task_runtime_info, node);
		if (rinfo->tid == task->pid) {
			rinfo->sum_exec_scale += exec_scale;
			rinfo->last_update_ts = now;
			goto rsunlock;
		}
	}

	new_rinfo = rinfo_mempool_alloc_one();
	if (new_rinfo) {
		new_rinfo->tid = task->pid;
		new_rinfo->sum_exec_scale = exec_scale;
		new_rinfo->last_update_ts = now;
		list_add_tail(&new_rinfo->node, &running_task_list);
	}

rsunlock:
	raw_spin_unlock_irqrestore(&g_lock, flags);
}

void g_rt_task_dead(struct task_struct *task)
{
	unsigned long flags;
	struct list_head *pos, *n;
	struct task_runtime_info *rinfo;

	if (atomic_read(&need_stat_runtime) == 0)
		return;

	raw_spin_lock_irqsave(&g_lock, flags);

	if (!game_leader || task->tgid != game_leader->tgid)
		goto rsunlock;

	list_for_each_safe(pos, n, &running_task_list) {
		rinfo = list_entry(pos, struct task_runtime_info, node);
		if (rinfo->tid == task->pid) {
			rinfo_mempool_free_one(rinfo);
			break;
		}
	}

rsunlock:
	raw_spin_unlock_irqrestore(&g_lock, flags);
}

int task_util_init(void)
{
	if (unlikely(!game_opt_dir))
		return -ENOTDIR;
	proc_create_data("game_pid", 0664, game_opt_dir, &game_pid_proc_ops, NULL);
	proc_create_data("heavy_task_info", 0444, game_opt_dir, &heavy_task_info_proc_ops, NULL);

	return 0;
}
