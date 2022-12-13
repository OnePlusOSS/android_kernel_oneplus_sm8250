/* drivers/misc/uid_sys_stats.c
 *
 * Copyright (C) 2014 - 2015 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/atomic.h>
#include <linux/cpufreq_times.h>
#include <linux/err.h>
#include <linux/hashtable.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/profile.h>
#include <linux/rtmutex.h>
#include <linux/sched/cputime.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#ifdef CONFIG_OPLUS_FEATURE_UID_PERF
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/perf_event.h>
#include <linux/freezer.h>
#include <linux/cpuset.h>
#include <linux/cpufreq.h>
#endif

#define UID_HASH_BITS	10
DECLARE_HASHTABLE(hash_table, UID_HASH_BITS);

static DEFINE_RT_MUTEX(uid_lock);
static struct proc_dir_entry *cpu_parent;
static struct proc_dir_entry *io_parent;
static struct proc_dir_entry *proc_parent;

struct io_stats {
	u64 read_bytes;
	u64 write_bytes;
	u64 rchar;
	u64 wchar;
	u64 fsync;
};

#define UID_STATE_FOREGROUND	0
#define UID_STATE_BACKGROUND	1
#define UID_STATE_BUCKET_SIZE	2

#define UID_STATE_TOTAL_CURR	2
#define UID_STATE_TOTAL_LAST	3
#define UID_STATE_DEAD_TASKS	4
#define UID_STATE_SIZE		5

#define MAX_TASK_COMM_LEN 256

struct task_entry {
	char comm[MAX_TASK_COMM_LEN];
	pid_t pid;
	struct io_stats io[UID_STATE_SIZE];
	struct hlist_node hash;
};

struct uid_entry {
	uid_t uid;
	u64 utime;
	u64 stime;
	u64 active_utime;
	u64 active_stime;
	int state;
	struct io_stats io[UID_STATE_SIZE];
	struct hlist_node hash;
#ifdef CONFIG_UID_SYS_STATS_DEBUG
	DECLARE_HASHTABLE(task_entries, UID_HASH_BITS);
#endif
#ifdef CONFIG_OPLUS_FEATURE_UID_PERF
	long long counts[UID_PERF_EVENTS];
	atomic64_t checkout[UID_PERF_EVENTS];
	atomic64_t cg_checkout[UID_GROUP_SIZE]; /* MUSTFIX only inst pevent for calc */
#endif
};

static u64 compute_write_bytes(struct task_struct *task)
{
	if (task->ioac.write_bytes <= task->ioac.cancelled_write_bytes)
		return 0;

	return task->ioac.write_bytes - task->ioac.cancelled_write_bytes;
}

static void compute_io_bucket_stats(struct io_stats *io_bucket,
					struct io_stats *io_curr,
					struct io_stats *io_last,
					struct io_stats *io_dead)
{
	/* tasks could switch to another uid group, but its io_last in the
	 * previous uid group could still be positive.
	 * therefore before each update, do an overflow check first
	 */
	int64_t delta;

	delta = io_curr->read_bytes + io_dead->read_bytes -
		io_last->read_bytes;
	io_bucket->read_bytes += delta > 0 ? delta : 0;
	delta = io_curr->write_bytes + io_dead->write_bytes -
		io_last->write_bytes;
	io_bucket->write_bytes += delta > 0 ? delta : 0;
	delta = io_curr->rchar + io_dead->rchar - io_last->rchar;
	io_bucket->rchar += delta > 0 ? delta : 0;
	delta = io_curr->wchar + io_dead->wchar - io_last->wchar;
	io_bucket->wchar += delta > 0 ? delta : 0;
	delta = io_curr->fsync + io_dead->fsync - io_last->fsync;
	io_bucket->fsync += delta > 0 ? delta : 0;

	io_last->read_bytes = io_curr->read_bytes;
	io_last->write_bytes = io_curr->write_bytes;
	io_last->rchar = io_curr->rchar;
	io_last->wchar = io_curr->wchar;
	io_last->fsync = io_curr->fsync;

	memset(io_dead, 0, sizeof(struct io_stats));
}

#ifdef CONFIG_UID_SYS_STATS_DEBUG
static void get_full_task_comm(struct task_entry *task_entry,
		struct task_struct *task)
{
	int i = 0, offset = 0, len = 0;
	/* save one byte for terminating null character */
	int unused_len = MAX_TASK_COMM_LEN - TASK_COMM_LEN - 1;
	char buf[MAX_TASK_COMM_LEN - TASK_COMM_LEN - 1];
	struct mm_struct *mm = task->mm;

	/* fill the first TASK_COMM_LEN bytes with thread name */
	__get_task_comm(task_entry->comm, TASK_COMM_LEN, task);
	i = strlen(task_entry->comm);
	while (i < TASK_COMM_LEN)
		task_entry->comm[i++] = ' ';

	/* next the executable file name */
	if (mm) {
		down_read(&mm->mmap_sem);
		if (mm->exe_file) {
			char *pathname = d_path(&mm->exe_file->f_path, buf,
					unused_len);

			if (!IS_ERR(pathname)) {
				len = strlcpy(task_entry->comm + i, pathname,
						unused_len);
				i += len;
				task_entry->comm[i++] = ' ';
				unused_len--;
			}
		}
		up_read(&mm->mmap_sem);
	}
	unused_len -= len;

	/* fill the rest with command line argument
	 * replace each null or new line character
	 * between args in argv with whitespace */
	len = get_cmdline(task, buf, unused_len);
	while (offset < len) {
		if (buf[offset] != '\0' && buf[offset] != '\n')
			task_entry->comm[i++] = buf[offset];
		else
			task_entry->comm[i++] = ' ';
		offset++;
	}

	/* get rid of trailing whitespaces in case when arg is memset to
	 * zero before being reset in userspace
	 */
	while (task_entry->comm[i-1] == ' ')
		i--;
	task_entry->comm[i] = '\0';
}

static struct task_entry *find_task_entry(struct uid_entry *uid_entry,
		struct task_struct *task)
{
	struct task_entry *task_entry;

	hash_for_each_possible(uid_entry->task_entries, task_entry, hash,
			task->pid) {
		if (task->pid == task_entry->pid) {
			/* if thread name changed, update the entire command */
			int len = strnchr(task_entry->comm, ' ', TASK_COMM_LEN)
				- task_entry->comm;

			if (strncmp(task_entry->comm, task->comm, len))
				get_full_task_comm(task_entry, task);
			return task_entry;
		}
	}
	return NULL;
}

static struct task_entry *find_or_register_task(struct uid_entry *uid_entry,
		struct task_struct *task)
{
	struct task_entry *task_entry;
	pid_t pid = task->pid;

	task_entry = find_task_entry(uid_entry, task);
	if (task_entry)
		return task_entry;

	task_entry = kzalloc(sizeof(struct task_entry), GFP_ATOMIC);
	if (!task_entry)
		return NULL;

	get_full_task_comm(task_entry, task);

	task_entry->pid = pid;
	hash_add(uid_entry->task_entries, &task_entry->hash, (unsigned int)pid);

	return task_entry;
}

static void remove_uid_tasks(struct uid_entry *uid_entry)
{
	struct task_entry *task_entry;
	unsigned long bkt_task;
	struct hlist_node *tmp_task;

	hash_for_each_safe(uid_entry->task_entries, bkt_task,
			tmp_task, task_entry, hash) {
		hash_del(&task_entry->hash);
		kfree(task_entry);
	}
}

static void set_io_uid_tasks_zero(struct uid_entry *uid_entry)
{
	struct task_entry *task_entry;
	unsigned long bkt_task;

	hash_for_each(uid_entry->task_entries, bkt_task, task_entry, hash) {
		memset(&task_entry->io[UID_STATE_TOTAL_CURR], 0,
			sizeof(struct io_stats));
	}
}

static void add_uid_tasks_io_stats(struct uid_entry *uid_entry,
		struct task_struct *task, int slot)
{
	struct task_entry *task_entry = find_or_register_task(uid_entry, task);
	struct io_stats *task_io_slot = &task_entry->io[slot];

	task_io_slot->read_bytes += task->ioac.read_bytes;
	task_io_slot->write_bytes += compute_write_bytes(task);
	task_io_slot->rchar += task->ioac.rchar;
	task_io_slot->wchar += task->ioac.wchar;
	task_io_slot->fsync += task->ioac.syscfs;
}

static void compute_io_uid_tasks(struct uid_entry *uid_entry)
{
	struct task_entry *task_entry;
	unsigned long bkt_task;

	hash_for_each(uid_entry->task_entries, bkt_task, task_entry, hash) {
		compute_io_bucket_stats(&task_entry->io[uid_entry->state],
					&task_entry->io[UID_STATE_TOTAL_CURR],
					&task_entry->io[UID_STATE_TOTAL_LAST],
					&task_entry->io[UID_STATE_DEAD_TASKS]);
	}
}

static void show_io_uid_tasks(struct seq_file *m, struct uid_entry *uid_entry)
{
	struct task_entry *task_entry;
	unsigned long bkt_task;

	hash_for_each(uid_entry->task_entries, bkt_task, task_entry, hash) {
		/* Separated by comma because space exists in task comm */
		seq_printf(m, "task,%s,%lu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
				task_entry->comm,
				(unsigned long)task_entry->pid,
				task_entry->io[UID_STATE_FOREGROUND].rchar,
				task_entry->io[UID_STATE_FOREGROUND].wchar,
				task_entry->io[UID_STATE_FOREGROUND].read_bytes,
				task_entry->io[UID_STATE_FOREGROUND].write_bytes,
				task_entry->io[UID_STATE_BACKGROUND].rchar,
				task_entry->io[UID_STATE_BACKGROUND].wchar,
				task_entry->io[UID_STATE_BACKGROUND].read_bytes,
				task_entry->io[UID_STATE_BACKGROUND].write_bytes,
				task_entry->io[UID_STATE_FOREGROUND].fsync,
				task_entry->io[UID_STATE_BACKGROUND].fsync);
	}
}
#else
static void remove_uid_tasks(struct uid_entry *uid_entry) {};
static void set_io_uid_tasks_zero(struct uid_entry *uid_entry) {};
static void add_uid_tasks_io_stats(struct uid_entry *uid_entry,
		struct task_struct *task, int slot) {};
static void compute_io_uid_tasks(struct uid_entry *uid_entry) {};
static void show_io_uid_tasks(struct seq_file *m,
		struct uid_entry *uid_entry) {}
#endif

static struct uid_entry *find_uid_entry(uid_t uid)
{
	struct uid_entry *uid_entry;
	hash_for_each_possible(hash_table, uid_entry, hash, uid) {
		if (uid_entry->uid == uid)
			return uid_entry;
	}
	return NULL;
}

static struct uid_entry *find_or_register_uid(uid_t uid)
{
	struct uid_entry *uid_entry;

	uid_entry = find_uid_entry(uid);
	if (uid_entry)
		return uid_entry;

	uid_entry = kzalloc(sizeof(struct uid_entry), GFP_ATOMIC);
	if (!uid_entry)
		return NULL;

	uid_entry->uid = uid;
#ifdef CONFIG_UID_SYS_STATS_DEBUG
	hash_init(uid_entry->task_entries);
#endif
	hash_add(hash_table, &uid_entry->hash, uid);

	return uid_entry;
}


#ifdef CONFIG_OPLUS_FEATURE_UID_PERF
static bool uid_perf_debug;
module_param_named(uid_perf_debug, uid_perf_debug, bool, 0664);

static bool uid_perf_enable;

static int uid_perf_event_id[UID_PERF_EVENTS] = {
	0x8,  /* "raw-inst-retired"    */
	0x11, /* "raw-cpu-cycles"      */
	0x24 /* "raw-stall-backend"   */
};

static int uid_perf_event_type[UID_PERF_EVENTS] = {
	PERF_TYPE_RAW,
	PERF_TYPE_RAW,
	PERF_TYPE_RAW
};

static int uid_perf_event_pin[UID_PERF_EVENTS] = {
	0,
	0,
	0
};

module_param_array(uid_perf_event_id, int, NULL, 0664);
module_param_array(uid_perf_event_type, int, NULL, 0664);
module_param_array(uid_perf_event_pin, int, NULL, 0664);

static DEFINE_SPINLOCK(uid_perf_lock);
static struct task_struct *uid_perf_add_thread;
static struct task_struct *uid_perf_remove_thread;
static struct list_head uid_perf_add_list = LIST_HEAD_INIT(uid_perf_add_list);
static struct list_head uid_perf_remove_list = LIST_HEAD_INIT(uid_perf_remove_list);

static bool snapshot_active = false;
bool get_uid_perf_enable(void)
{
	return (uid_perf_enable && snapshot_active);
}

struct uid_work {
	struct task_struct *task;
	struct list_head node;
};

void uid_perf_work_add(struct task_struct *task, bool force)
{
	unsigned long flag;
	struct uid_work *work;

	if (!uid_perf_enable && !force)
		return;

	work = kmalloc(sizeof(struct uid_work), GFP_ATOMIC);
	if (!work)
		return;

	if (uid_perf_debug)
		pr_err("add task %s %d to add list\n", task->comm, task->pid);

	spin_lock_irqsave(&uid_perf_lock, flag);
	get_task_struct(task);
	work->task = task;
	list_add_tail(&work->node, &uid_perf_add_list);
	spin_unlock_irqrestore(&uid_perf_lock, flag);

	wake_up_process(uid_perf_add_thread);
}

static void uid_perf_work_remove(struct task_struct *task)
{
	unsigned long flag;
	struct uid_work *work;

	work = kmalloc(sizeof(struct uid_work), GFP_ATOMIC);
	if (!work)
		return;

	if (uid_perf_debug)
		pr_err("add task %s %d to remove list\n", task->comm, task->pid);

	spin_lock_irqsave(&uid_perf_lock, flag);
	get_task_struct(task);
	work->task = task;
	list_add_tail(&work->node, &uid_perf_remove_list);
	spin_unlock_irqrestore(&uid_perf_lock, flag);

	wake_up_process(uid_perf_remove_thread);
}

static inline void reset_group_data(struct task_struct *task)
{
	int i;

	for (i = 0; i < UID_GROUP_SIZE; ++i) {
		task->uid_group_prev_counts[i] = 0;
		task->uid_group[i] = 0;
	}
}

static inline void uid_remove_and_disable_one_pevent(struct task_struct  *task, int idx)
{
	if (!task->uid_pevents[idx])
		return;

	perf_event_disable(task->uid_pevents[idx]);
	perf_event_release_kernel(task->uid_pevents[idx]);
	task->uid_prev_counts[idx] = 0;
	task->uid_pevents[idx] = NULL;
	reset_group_data(task);

	if (uid_perf_debug)
		pr_err("task %s %d disable pevent %d\n", task->comm, task->pid, idx);
}

static void uid_create_and_enable_one_pevent(struct task_struct *task, int idx, bool grouping)
{
	struct perf_event *pevent;
	struct perf_event_attr attr;

	if (uid_perf_event_id[idx] == -1)
		return;

	if (!task->uid_pevents[idx]) {
		memset(&attr, 0, sizeof(struct perf_event_attr));
		attr.size = sizeof(struct perf_event_attr);
		attr.inherit = 0;
		attr.read_format = 7;
		attr.sample_type = 455;
		attr.config = uid_perf_event_id[idx];
		attr.type = uid_perf_event_type[idx];
		attr.pinned = uid_perf_event_pin[idx];
		if (grouping) {
			attr.read_format = PERF_FORMAT_GROUP;
			attr.inherit = 1;
		}

		/* TODO add overflow handler */
		pevent = perf_event_create_kernel_counter(&attr, -1, task, NULL, NULL);
		if (IS_ERR(pevent)) {
			pr_err("task %s %d pevent %d create failed\n", task->comm, task->pid, idx);
		} else {
			task->uid_pevents[idx] = pevent;
			if (uid_perf_debug)
				pr_err("task %s %d pevent %d created\n",
					task->comm, task->pid, idx);
		}
	}
}

void uid_check_out_pevent(struct task_struct *task)
{
	struct uid_entry *uid_entry = NULL;
	u64 val, enabled, running, delta, cg_delta;
	uid_t uid;
	int i, idx;
	struct task_struct *tgid = NULL;

	for (i = 0; i < UID_PERF_EVENTS; ++i) {
		if (task->uid_pevents[i]) {
			val = perf_event_read_value(task->uid_pevents[i], &enabled, &running);
			delta = val - task->uid_prev_counts[i];
			/* MUSTFIX only calc instruction counter for cpuset */
			if (i == 0) {
				idx = cpuset_get_cgrp_idx(task);
				if (idx > -1)
					cg_delta = val - task->uid_group_prev_counts[idx];
				else
					pr_err("%s: idx is invalid value idx=%d task=%s pid=%d", __func__, idx, task->comm, task->pid);
			}
			uid_remove_and_disable_one_pevent(task, i);

			/* add back to tgid entry */
			if (!tgid) {
				rcu_read_lock();
				tgid = find_task_by_vpid(task->tgid);
				if (tgid)
					get_task_struct(tgid);
				rcu_read_unlock();
			}

			if (tgid) {
				/* 2 cases
					1. normal task leave, add delta to tgid
					2. leader task leave, add delta to uid entry
				 */
				if (tgid->pid == task->pid) {
					if (!uid_entry) {
						uid = from_kuid_munged(current_user_ns(), task_uid(tgid));
						uid_entry = find_uid_entry(uid);
					}

					if (uid_entry) {
						atomic64_add(delta, &uid_entry->checkout[i]);
						/* MUSTFIX cgroup only calc instruction pevent */
						if (i == 0 && idx > -1) {
							atomic64_add(cg_delta, &uid_entry->cg_checkout[idx]);
							if (uid_perf_debug) {
								pr_err("%s: task %s %d %d %d val: %llu cg_delta: %llu cg_prev: %llu\n",
									__func__, task->comm, task->pid, task->tgid,
									uid, val,
									cg_delta, (val - cg_delta));
							}
						}
						if (uid_perf_debug) {
							pr_err("task %s %d %d %d leave event %d val: %llu delta: %llu prev: %llu\n",
								task->comm, task->pid, task->tgid,
								uid, i, val, delta,
								task->uid_prev_counts[i]);
						}
					}
				} else {
					/* TODO change to atomic */
					tgid->uid_leaving_counts[i] += delta;
					/* MUSTFIX cpuset checkout pevent conter only for instructions */
					if (i == 0 & idx > -1) {
						if (uid_perf_debug)
							pr_err("%s: tgid leaving task=%d tgid=%d cg_delta=%llu idx=%d tgid original count=%llu",
							__func__, task->pid, tgid->pid, cg_delta, idx, tgid->uid_group[idx]);

						tgid->uid_group[idx] += cg_delta;
					}
				}

				if (uid_perf_debug)
					pr_err("task %s %d %d uid %d leave event %d val: %llu cur %llu\n", task->comm, task->pid, task->tgid, uid, i, delta, tgid->uid_leaving_counts[i]);
			}
		}
	}

	if (tgid)
		put_task_struct(tgid);
}

struct cpufreq_stats {
	unsigned int total_trans;
	unsigned long long last_time;
	unsigned int max_state;
	unsigned int state_num;
	unsigned int last_index;
	u64 *time_in_state;
	spinlock_t lock;
	unsigned int *freq_table;
	unsigned int *trans_table;
};

static inline void uid_cpufreq_stats_update(struct cpufreq_stats *st)
{
	unsigned long long cur_time;
	unsigned long flags;

	spin_lock_irqsave(&st->lock, flags);
	cur_time = get_jiffies_64();
	st->time_in_state[st->last_index] += cur_time - st->last_time;
	st->last_time = cur_time;
	spin_unlock_irqrestore(&st->lock, flags);
}

static u64 uid_get_norm_cpu_time(void)
{
	struct cpufreq_policy *pol;
	struct cpufreq_stats *st;
	int i = 0, j;
	u64 total = 0, cur_time;
	u64 norm_freq = UINT_MAX, cur_freq;

	if (1)
		return 0;

	while (i != nr_cpu_ids) {
		pol = cpufreq_cpu_get(i);
		if (!pol)
			continue;
		st = (struct cpufreq_stats *) pol->stats;

		/* update state */
		uid_cpufreq_stats_update(st);

		for (j = 0; j < st->state_num; ++j) {
			cur_freq = st->freq_table[j];
			norm_freq = min(norm_freq, cur_freq);
			cur_time = st->time_in_state[j];
			total += cur_time * cur_freq / norm_freq;
			if (uid_perf_debug) {
				pr_err("norm_count: cpu %d freq %llu norm %llu st %llu total %llu\n",
				i, cur_freq, norm_freq, cur_time, total);
			}
		}
		i += cpumask_weight(pol->related_cpus);
		cpufreq_cpu_put(pol);
	}
	return total;
}

static int uid_perf_show(struct seq_file *m, void *v)
{
	struct task_struct *task, *temp;
	struct user_namespace *user_ns = current_user_ns();
	unsigned long bkt;
	struct uid_entry *uid_entry = NULL;
	uid_t uid;
	u64 enabled, running;
	int i, idx, j;
	u64 val[UID_PERF_EVENTS];
	s64 time = ktime_to_ms(ktime_get());

	rcu_read_lock();
	do_each_thread(temp, task) {
		uid = from_kuid_munged(user_ns, task_uid(task));

		/* avoid double accounting of dying threads */
		if (!(task->flags & PF_EXITING)) {
			get_task_struct(task);
			rcu_read_unlock();

			seq_printf(m, "%s,%d,%d,%d", task->comm, task->pid, task->tgid, uid);
			for (i = 0; i < UID_PERF_EVENTS; ++i) {
				val[i] = 0;

				if (task->uid_pevents[i]) {
					val[i] = perf_event_read_value(task->uid_pevents[i],
						&enabled, &running);
					task->uid_prev_counts[i] = val[i];
				}
				seq_printf(m, ",%llu", val[i] + task->uid_leaving_counts[i]);
			}

			/* get current cgroup which task is belong to. */
			idx = cpuset_get_cgrp_idx_locked(task);
			if (idx > -1) {
				/* MUSTFIX only inst event in the cpuset */
				for (i = 0; i < 1; ++i) {
					if (val[i] == 0)
						continue;
					/* update the pevent counter in current cgroup */
					if (task->uid_group_prev_counts[idx] > 0) {
						task->uid_group[idx] += (val[i] - task->uid_group_prev_counts[idx]);
						if (uid_perf_debug)
							pr_err("%s: pid=%d comm=%s val=%llu cgid_idx=%d prev_count=%llu uid_group=%llu",
							__func__,
							task->pid,
							task->comm,
							val[i],
							idx,
							task->uid_group_prev_counts[idx],
							task->uid_group[idx]);
					} else {
						/* first  snapshot in the current cgroup */
						task->uid_group_prev_counts[idx] = val[i];
						if (uid_perf_debug)
							pr_err("%s: pid=%d comm=%s val=%llu cgid_idx=%d prev_count=%llu",
								__func__,
								task->pid,
								task->comm,
								val[i],
								idx,
								task->uid_group_prev_counts[idx]);
					}
				}
			}
			/* output all counter of all groups */
			for (j = 0; j < UID_GROUP_SIZE; ++j)
				seq_printf(m, ",%llu", task->uid_group[j]);

			seq_puts(m, "\n");

			rcu_read_lock();
			if (snapshot_active)
				reset_group_data(task);
			put_task_struct(task);
		}
	} while_each_thread(temp, task);
	rcu_read_unlock();

	/* FIXME for some reasons this functions will be called while use 'cat' */
	/* update early leave counter */
	hash_for_each(hash_table, bkt, uid_entry, hash) {
		seq_printf(m, "uid-%d,0,0,%d", uid_entry->uid, uid_entry->uid);
		for (i = 0; i < UID_PERF_EVENTS; ++i)
			seq_printf(m, ",%llu", atomic64_read(&uid_entry->checkout[i]));
		/* cgroup counters */
		for (i = 0; i < UID_GROUP_SIZE; ++i)
			seq_printf(m, ",%llu", atomic64_read(&uid_entry->cg_checkout[i]));
		seq_puts(m, "\n");
	}
	/*cpuset switch counting will be align snapshot_active scope */
	snapshot_active = !snapshot_active;

	seq_printf(m, "%lld,%llu\n", time, uid_get_norm_cpu_time());
	return 0;
}

static int uid_perf_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, uid_perf_show, NULL, 0x1 << 20);
}

static const struct file_operations uid_perf_fops = {
	.open		= uid_perf_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int uid_perf_enable_store(const char *buf, const struct kernel_param *kp)
{
	struct task_struct *task, *temp;
	int val;

	if (sscanf(buf, "%d\n", &val) <= 0)
		return 0;

	if (uid_perf_enable == val)
		return 0;

	if (val == 0)
		uid_perf_enable = val;

	/* TODO should protect from race */
	read_lock(&tasklist_lock);
	for_each_process_thread(temp, task) {
		if (val) {
			/* quick check if has any pevent exists, check next */
			if (task->flags & PF_EXITING || task->uid_pevents[0])
				continue;

			uid_perf_work_add(task, true);
		} else {
			if (task->flags & PF_EXITING)
				continue;
			uid_perf_work_remove(task);
		}
	}
	read_unlock(&tasklist_lock);

	if (val == 1)
		uid_perf_enable = val;

	if (val)
		wake_up_process(uid_perf_add_thread);
	else
		wake_up_process(uid_perf_remove_thread);

	return 0;
}

static int uid_perf_enable_show(char *buf, const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", uid_perf_enable);
}

static struct kernel_param_ops uid_perf_enable_ops = {
	.set = uid_perf_enable_store,
	.get = uid_perf_enable_show,
};
module_param_cb(uid_perf_enable, &uid_perf_enable_ops, NULL, 0664);

static int __uid_perf_add_work(void *unused)
{
	unsigned long flag;
	int i;

	while (!kthread_should_stop()) {
		set_current_state(TASK_RUNNING);
		spin_lock_irqsave(&uid_perf_lock, flag);
		while (!list_empty(&uid_perf_add_list)) {
			struct uid_work *work =
				list_first_entry(&uid_perf_add_list, struct uid_work, node);
			struct task_struct *task = work->task;

			list_del(&work->node);
			kfree(work);
			spin_unlock_irqrestore(&uid_perf_lock, flag);

			/* init perf event */
			if (task && !(task->flags & PF_EXITING)) {
				for (i = 0; i < UID_PERF_EVENTS; ++i)
					uid_create_and_enable_one_pevent(task, i, false);
			}
			put_task_struct(task);
			spin_lock_irqsave(&uid_perf_lock, flag);
		}

		spin_unlock_irqrestore(&uid_perf_lock, flag);

		set_current_state(TASK_INTERRUPTIBLE);
		freezable_schedule();
	}

	return 0;
}

static int __uid_perf_remove_work(void *unused)
{
	unsigned long flag;
	int i;

	while (!kthread_should_stop()) {
		set_current_state(TASK_RUNNING);
		spin_lock_irqsave(&uid_perf_lock, flag);
		while (!list_empty(&uid_perf_remove_list)) {
			struct uid_work *work =
				list_first_entry(&uid_perf_remove_list, struct uid_work, node);
			struct task_struct *task = work->task;

			list_del(&work->node);
			kfree(work);
			spin_unlock_irqrestore(&uid_perf_lock, flag);

			/* remove perf event */
			if (task && !(task->flags & PF_EXITING)) {
				for (i = 0; i < UID_PERF_EVENTS; ++i)
					uid_remove_and_disable_one_pevent(task, i);
			}
			put_task_struct(task);
			spin_lock_irqsave(&uid_perf_lock, flag);
		}

		spin_unlock_irqrestore(&uid_perf_lock, flag);

		set_current_state(TASK_INTERRUPTIBLE);
		freezable_schedule();
	}

	return 0;
}
#endif
static int uid_cputime_show(struct seq_file *m, void *v)
{
	struct uid_entry *uid_entry = NULL;
	struct task_struct *task, *temp;
	struct user_namespace *user_ns = current_user_ns();
	u64 utime;
	u64 stime;
	unsigned long bkt;
	uid_t uid;

	rt_mutex_lock(&uid_lock);

	hash_for_each(hash_table, bkt, uid_entry, hash) {
		uid_entry->active_stime = 0;
		uid_entry->active_utime = 0;
	}

	rcu_read_lock();
	do_each_thread(temp, task) {
		uid = from_kuid_munged(user_ns, task_uid(task));
		if (!uid_entry || uid_entry->uid != uid)
			uid_entry = find_or_register_uid(uid);
		if (!uid_entry) {
			rcu_read_unlock();
			rt_mutex_unlock(&uid_lock);
			pr_err("%s: failed to find the uid_entry for uid %d\n",
				__func__, uid);
			return -ENOMEM;
		}
		/* avoid double accounting of dying threads */
		if (!(task->flags & PF_EXITING)) {
			task_cputime_adjusted(task, &utime, &stime);
			uid_entry->active_utime += utime;
			uid_entry->active_stime += stime;
		}
	} while_each_thread(temp, task);
	rcu_read_unlock();

	hash_for_each(hash_table, bkt, uid_entry, hash) {
		u64 total_utime = uid_entry->utime +
							uid_entry->active_utime;
		u64 total_stime = uid_entry->stime +
							uid_entry->active_stime;
		seq_printf(m, "%d: %llu %llu\n", uid_entry->uid,
			ktime_to_us(total_utime), ktime_to_us(total_stime));
	}

	rt_mutex_unlock(&uid_lock);
	return 0;
}

static int uid_cputime_open(struct inode *inode, struct file *file)
{
	return single_open(file, uid_cputime_show, PDE_DATA(inode));
}

static const struct file_operations uid_cputime_fops = {
	.open		= uid_cputime_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int uid_remove_open(struct inode *inode, struct file *file)
{
	return single_open(file, NULL, NULL);
}

static ssize_t uid_remove_write(struct file *file,
			const char __user *buffer, size_t count, loff_t *ppos)
{
	struct uid_entry *uid_entry;
	struct hlist_node *tmp;
	char uids[128];
	char *start_uid, *end_uid = NULL;
	uid_t uid_start = 0, uid_end = 0;
	u64 uid;

	if (count >= sizeof(uids))
		count = sizeof(uids) - 1;

	if (copy_from_user(uids, buffer, count))
		return -EFAULT;

	uids[count] = '\0';
	end_uid = uids;
	start_uid = strsep(&end_uid, "-");

	if (!start_uid || !end_uid)
		return -EINVAL;

	if (kstrtouint(start_uid, 10, &uid_start) != 0 ||
		kstrtouint(end_uid, 10, &uid_end) != 0) {
		return -EINVAL;
	}

	/* Also remove uids from /proc/uid_time_in_state */
	cpufreq_task_times_remove_uids(uid_start, uid_end);

	rt_mutex_lock(&uid_lock);

	for (uid = uid_start; uid <= uid_end; uid++) {
		hash_for_each_possible_safe(hash_table, uid_entry, tmp,
							hash, uid) {
			if (uid == uid_entry->uid) {
				remove_uid_tasks(uid_entry);
				hash_del(&uid_entry->hash);
				kfree(uid_entry);
			}
		}
	}

	rt_mutex_unlock(&uid_lock);
	return count;
}

static const struct file_operations uid_remove_fops = {
	.open		= uid_remove_open,
	.release	= single_release,
	.write		= uid_remove_write,
};


static void add_uid_io_stats(struct uid_entry *uid_entry,
			struct task_struct *task, int slot)
{
	struct io_stats *io_slot = &uid_entry->io[slot];

	/* avoid double accounting of dying threads */
	if (slot != UID_STATE_DEAD_TASKS && (task->flags & PF_EXITING))
		return;

	io_slot->read_bytes += task->ioac.read_bytes;
	io_slot->write_bytes += compute_write_bytes(task);
	io_slot->rchar += task->ioac.rchar;
	io_slot->wchar += task->ioac.wchar;
	io_slot->fsync += task->ioac.syscfs;

	add_uid_tasks_io_stats(uid_entry, task, slot);
}

static void update_io_stats_all_locked(void)
{
	struct uid_entry *uid_entry = NULL;
	struct task_struct *task, *temp;
	struct user_namespace *user_ns = current_user_ns();
	unsigned long bkt;
	uid_t uid;

	hash_for_each(hash_table, bkt, uid_entry, hash) {
		memset(&uid_entry->io[UID_STATE_TOTAL_CURR], 0,
			sizeof(struct io_stats));
		set_io_uid_tasks_zero(uid_entry);
	}

	rcu_read_lock();
	do_each_thread(temp, task) {
		uid = from_kuid_munged(user_ns, task_uid(task));
		if (!uid_entry || uid_entry->uid != uid)
			uid_entry = find_or_register_uid(uid);
		if (!uid_entry)
			continue;
		add_uid_io_stats(uid_entry, task, UID_STATE_TOTAL_CURR);
	} while_each_thread(temp, task);
	rcu_read_unlock();

	hash_for_each(hash_table, bkt, uid_entry, hash) {
		compute_io_bucket_stats(&uid_entry->io[uid_entry->state],
					&uid_entry->io[UID_STATE_TOTAL_CURR],
					&uid_entry->io[UID_STATE_TOTAL_LAST],
					&uid_entry->io[UID_STATE_DEAD_TASKS]);
		compute_io_uid_tasks(uid_entry);
	}
}

static void update_io_stats_uid_locked(struct uid_entry *uid_entry)
{
	struct task_struct *task, *temp;
	struct user_namespace *user_ns = current_user_ns();

	memset(&uid_entry->io[UID_STATE_TOTAL_CURR], 0,
		sizeof(struct io_stats));
	set_io_uid_tasks_zero(uid_entry);

	rcu_read_lock();
	do_each_thread(temp, task) {
		if (from_kuid_munged(user_ns, task_uid(task)) != uid_entry->uid)
			continue;
		add_uid_io_stats(uid_entry, task, UID_STATE_TOTAL_CURR);
	} while_each_thread(temp, task);
	rcu_read_unlock();

	compute_io_bucket_stats(&uid_entry->io[uid_entry->state],
				&uid_entry->io[UID_STATE_TOTAL_CURR],
				&uid_entry->io[UID_STATE_TOTAL_LAST],
				&uid_entry->io[UID_STATE_DEAD_TASKS]);
	compute_io_uid_tasks(uid_entry);
}


static int uid_io_show(struct seq_file *m, void *v)
{
	struct uid_entry *uid_entry;
	unsigned long bkt;

	rt_mutex_lock(&uid_lock);

	update_io_stats_all_locked();

	hash_for_each(hash_table, bkt, uid_entry, hash) {
		seq_printf(m, "%d %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
				uid_entry->uid,
				uid_entry->io[UID_STATE_FOREGROUND].rchar,
				uid_entry->io[UID_STATE_FOREGROUND].wchar,
				uid_entry->io[UID_STATE_FOREGROUND].read_bytes,
				uid_entry->io[UID_STATE_FOREGROUND].write_bytes,
				uid_entry->io[UID_STATE_BACKGROUND].rchar,
				uid_entry->io[UID_STATE_BACKGROUND].wchar,
				uid_entry->io[UID_STATE_BACKGROUND].read_bytes,
				uid_entry->io[UID_STATE_BACKGROUND].write_bytes,
				uid_entry->io[UID_STATE_FOREGROUND].fsync,
				uid_entry->io[UID_STATE_BACKGROUND].fsync);

		show_io_uid_tasks(m, uid_entry);
	}

	rt_mutex_unlock(&uid_lock);
	return 0;
}

static int uid_io_open(struct inode *inode, struct file *file)
{
	return single_open(file, uid_io_show, PDE_DATA(inode));
}

static const struct file_operations uid_io_fops = {
	.open		= uid_io_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int uid_procstat_open(struct inode *inode, struct file *file)
{
	return single_open(file, NULL, NULL);
}

static ssize_t uid_procstat_write(struct file *file,
			const char __user *buffer, size_t count, loff_t *ppos)
{
	struct uid_entry *uid_entry;
	uid_t uid;
	int argc, state;
	char input[128];

	if (count >= sizeof(input))
		return -EINVAL;

	if (copy_from_user(input, buffer, count))
		return -EFAULT;

	input[count] = '\0';

	argc = sscanf(input, "%u %d", &uid, &state);
	if (argc != 2)
		return -EINVAL;

	if (state != UID_STATE_BACKGROUND && state != UID_STATE_FOREGROUND)
		return -EINVAL;

	rt_mutex_lock(&uid_lock);

	uid_entry = find_or_register_uid(uid);
	if (!uid_entry) {
		rt_mutex_unlock(&uid_lock);
		return -EINVAL;
	}

	if (uid_entry->state == state) {
		rt_mutex_unlock(&uid_lock);
		return count;
	}

	update_io_stats_uid_locked(uid_entry);

	uid_entry->state = state;

	rt_mutex_unlock(&uid_lock);

	return count;
}

static const struct file_operations uid_procstat_fops = {
	.open		= uid_procstat_open,
	.release	= single_release,
	.write		= uid_procstat_write,
};

static int process_notifier(struct notifier_block *self,
			unsigned long cmd, void *v)
{
	struct task_struct *task = v;
	struct uid_entry *uid_entry;
	u64 utime, stime;
	uid_t uid;

	if (!task)
		return NOTIFY_OK;

	rt_mutex_lock(&uid_lock);
	uid = from_kuid_munged(current_user_ns(), task_uid(task));
	uid_entry = find_or_register_uid(uid);
	if (!uid_entry) {
		pr_err("%s: failed to find uid %d\n", __func__, uid);
		goto exit;
	}

	task_cputime_adjusted(task, &utime, &stime);
	uid_entry->utime += utime;
	uid_entry->stime += stime;

	add_uid_io_stats(uid_entry, task, UID_STATE_DEAD_TASKS);

exit:
	rt_mutex_unlock(&uid_lock);
	return NOTIFY_OK;
}

static struct notifier_block process_notifier_block = {
	.notifier_call	= process_notifier,
};

static int __init proc_uid_sys_stats_init(void)
{
	hash_init(hash_table);

	cpu_parent = proc_mkdir("uid_cputime", NULL);
	if (!cpu_parent) {
		pr_err("%s: failed to create uid_cputime proc entry\n",
			__func__);
		goto err;
	}

	proc_create_data("remove_uid_range", 0222, cpu_parent,
		&uid_remove_fops, NULL);
	proc_create_data("show_uid_stat", 0444, cpu_parent,
		&uid_cputime_fops, NULL);

#ifdef CONFIG_OPLUS_FEATURE_UID_PERF
	proc_create_data("show_uid_perf", 0444, cpu_parent,
		&uid_perf_fops, NULL);
	uid_perf_add_thread = kthread_create(__uid_perf_add_work, NULL, "uid_add_thread");
	uid_perf_remove_thread = kthread_create(__uid_perf_remove_work, NULL, "uid_remove_thread");
#endif

	io_parent = proc_mkdir("uid_io", NULL);
	if (!io_parent) {
		pr_err("%s: failed to create uid_io proc entry\n",
			__func__);
		goto err;
	}

	proc_create_data("stats", 0444, io_parent,
		&uid_io_fops, NULL);

	proc_parent = proc_mkdir("uid_procstat", NULL);
	if (!proc_parent) {
		pr_err("%s: failed to create uid_procstat proc entry\n",
			__func__);
		goto err;
	}

	proc_create_data("set", 0222, proc_parent,
		&uid_procstat_fops, NULL);

	profile_event_register(PROFILE_TASK_EXIT, &process_notifier_block);

	return 0;

err:
	remove_proc_subtree("uid_cputime", NULL);
	remove_proc_subtree("uid_io", NULL);
	remove_proc_subtree("uid_procstat", NULL);
	return -ENOMEM;
}

early_initcall(proc_uid_sys_stats_init);
