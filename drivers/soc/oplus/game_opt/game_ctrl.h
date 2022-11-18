// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#ifndef __GAME_CTRL_H__
#define __GAME_CTRL_H__

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>
#include "../../../../kernel/sched/sched.h"

#define GAME_CTRL_MAGIC 'x'

#define MAX_CPU_NR 8
#define MAX_TASK_NR 10
#define MAX_TID_COUNT 256
#define MAX_TASK_INACTIVE_TIME 1000000000 /* 1s */
#define MAX_RT_NUM	2

enum {
	GET_CPU_LOAD = 1,
	SET_GAME_PID,
	GET_TASK_UTIL,
	SET_CPU_MIN_FREQ,
	SET_CPU_MAX_FREQ,
	GAME_CTRL_MAX_NR
};

struct cpu_load_info {
	int busy_pct;
	int util_pct;
};

struct cpu_load {
	struct cpu_load_info info[MAX_CPU_NR];
};

struct game_pid {
	pid_t pid;
};

struct task_util_info {
	pid_t tid;
	char comm[TASK_COMM_LEN];
	u16 util;
};

struct task_util {
	int num;
	struct task_util_info info[MAX_TASK_NR];
};

struct cpu_freq_info {
	/* ex: 0:960000 4:1440000 7:2284800 */
	char buf[128];
};

#define GAME_GET_CPU_LOAD \
	_IOWR(GAME_CTRL_MAGIC, GET_CPU_LOAD, struct cpu_load)

#define GAME_SET_GAME_PID \
	_IOWR(GAME_CTRL_MAGIC, SET_GAME_PID, struct game_pid)

#define GAME_GET_TASK_UTIL \
	_IOWR(GAME_CTRL_MAGIC, GET_TASK_UTIL, struct task_util)

#define GAME_SET_CPU_MIN_FREQ \
	_IOWR(GAME_CTRL_MAGIC, SET_CPU_MIN_FREQ, struct cpu_freq_info)

#define GAME_SET_CPU_MAX_FREQ \
	_IOWR(GAME_CTRL_MAGIC, SET_CPU_MAX_FREQ, struct cpu_freq_info)

extern struct proc_dir_entry *game_opt_dir;

int cpu_load_init(void);
int cpufreq_limits_init(void);
int task_util_init(void);
int rt_info_init(void);
void rt_task_dead(struct task_struct *task);
int dstate_dump_init(void);
void rt_set_dstate_interested_threads(pid_t *tids, int num);
extern void g_time_in_state_update_idle(int cpu, unsigned int new_idle_index);
extern void g_rt_try_to_wake_up(struct task_struct *task);
extern void g_update_task_runtime(struct task_struct *task, u64 runtime);
extern void g_rt_task_dead(struct task_struct *task);
extern void g_sched_stat_blocked(struct task_struct *task, u64 delay_ns);
#endif /*__GAME_CTRL_H__*/
