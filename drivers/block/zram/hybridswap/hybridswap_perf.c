// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[HYBRIDSWAP]" fmt

#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/timer.h>
#include <linux/zsmalloc.h>
#include <linux/spinlock.h>
#include <linux/sched/task.h>
#include <linux/sched/debug.h>
#ifdef CONFIG_FG_TASK_UID
#include <linux/healthinfo/fg.h>
#endif
#include "hybridswap_internal.h"

#define DUMP_BUF_LEN 512

static unsigned long warning_threshold[HYBRIDSWAP_SCENARIO_BUTT] = {
	0, 200, 500, 0
};

const char *key_point_name[HYBRIDSWAP_KYE_POINT_BUTT] = {
	"START",
	"INIT",
	"IOENTRY_ALLOC",
	"FIND_EXTENT",
	"IO_EXTENT",
	"SEGMENT_ALLOC",
	"BIO_ALLOC",
	"SUBMIT_BIO",
	"END_IO",
	"SCHED_WORK",
	"END_WORK",
	"CALL_BACK",
	"WAKE_UP",
	"ZRAM_LOCK",
	"DONE"
};

static void hybridswap_dump_point_lat(
	struct hybridswap_key_point_record *record, ktime_t start)
{
	int i;

	for (i = 0; i < HYBRIDSWAP_KYE_POINT_BUTT; ++i) {
		if (!record->key_point[i].record_cnt)
			continue;

		hybp(HYB_ERR,
			"%s diff %lld cnt %u end %u lat %lld ravg_sum %llu\n",
			key_point_name[i],
			ktime_us_delta(record->key_point[i].first_time, start),
			record->key_point[i].record_cnt,
			record->key_point[i].end_cnt,
			record->key_point[i].proc_total_time,
			record->key_point[i].proc_ravg_sum);
	}
}

static void hybridswap_dump_no_record_point(
	struct hybridswap_key_point_record *record, char *log,
	unsigned int *count)
{
	int i;
	unsigned int point = 0;

	for (i = 0; i < HYBRIDSWAP_KYE_POINT_BUTT; ++i)
		if (record->key_point[i].record_cnt)
			point = i;

	point++;
	if (point < HYBRIDSWAP_KYE_POINT_BUTT)
		*count += snprintf(log + *count,
			(size_t)(DUMP_BUF_LEN - *count),
			" no_record_point %s", key_point_name[point]);
	else
		*count += snprintf(log + *count,
			(size_t)(DUMP_BUF_LEN - *count), " all_point_record");
}

static long long hybridswap_calc_speed(s64 page_cnt, s64 time)
{
	s64 size;

	if (!page_cnt)
		return 0;

	size = page_cnt * PAGE_SIZE * BITS_PER_BYTE;
	if (time)
		return size * USEC_PER_SEC / time;
	else
		return S64_MAX;
}

static void hybridswap_dump_lat(
	struct hybridswap_key_point_record *record, ktime_t curr_time,
	bool perf_end_flag)
{
	char log[DUMP_BUF_LEN] = { 0 };
	unsigned int count = 0;
	ktime_t start;
	s64 total_time;

	start = record->key_point[HYBRIDSWAP_START].first_time;
	total_time = ktime_us_delta(curr_time, start);
	count += snprintf(log + count,
		(size_t)(DUMP_BUF_LEN - count),
		"totaltime(us) %lld scenario %u task %s nice %d",
		total_time, record->scenario, record->task_comm, record->nice);

	if (perf_end_flag)
		count += snprintf(log + count, (size_t)(DUMP_BUF_LEN - count),
			" page %d segment %d speed(bps) %lld threshold %llu",
			record->page_cnt, record->segment_cnt,
			hybridswap_calc_speed(record->page_cnt, total_time),
			record->warning_threshold);
	else
		count += snprintf(log + count, (size_t)(DUMP_BUF_LEN - count),
			" state %c", task_state_to_char(record->task));

	hybridswap_dump_no_record_point(record, log, &count);

	hybp(HYB_ERR, "perf end flag %u %s\n", perf_end_flag, log);
	hybridswap_dump_point_lat(record, start);
	dump_stack();
}

static unsigned long hybridswap_perf_warning_threshold(
	enum hybridswap_scenario scenario)
{
	if (unlikely(scenario >= HYBRIDSWAP_SCENARIO_BUTT))
		return 0;

	return warning_threshold[scenario];
}

void hybridswap_perf_warning(struct timer_list *t)
{
	struct hybridswap_key_point_record *record =
		from_timer(record, t, lat_monitor);
	static unsigned long last_dump_lat_jiffies = 0;

	if (!record->warning_threshold)
		return;

	if (jiffies_to_msecs(jiffies - last_dump_lat_jiffies) <= 60000)
		return;

	hybridswap_dump_lat(record, ktime_get(), false);

	if (likely(record->task))
		sched_show_task(record->task);
	last_dump_lat_jiffies = jiffies;
	record->warning_threshold <<= 2;
	record->timeout_flag = true;
	mod_timer(&record->lat_monitor,
		jiffies + msecs_to_jiffies(record->warning_threshold));
}

static void hybridswap_perf_init_monitor(
	struct hybridswap_key_point_record *record,
	enum hybridswap_scenario scenario)
{
	record->warning_threshold = hybridswap_perf_warning_threshold(scenario);

	if (!record->warning_threshold)
		return;

	record->task = current;
	get_task_struct(record->task);
	timer_setup(&record->lat_monitor, hybridswap_perf_warning, 0);
	mod_timer(&record->lat_monitor,
			jiffies + msecs_to_jiffies(record->warning_threshold));
}

static void hybridswap_perf_stop_monitor(
	struct hybridswap_key_point_record *record)
{
	if (!record->warning_threshold)
		return;

	del_timer_sync(&record->lat_monitor);
	put_task_struct(record->task);
}

static void hybridswap_perf_init(struct hybridswap_key_point_record *record,
	enum hybridswap_scenario scenario)
{
	int i;

	for (i = 0; i < HYBRIDSWAP_KYE_POINT_BUTT; ++i)
		spin_lock_init(&record->key_point[i].time_lock);

	record->nice = task_nice(current);
	record->scenario = scenario;
	get_task_comm(record->task_comm, current);
	hybridswap_perf_init_monitor(record, scenario);
}

void hybridswap_perf_start_proc(
	struct hybridswap_key_point_record *record,
	enum hybridswap_key_point type, ktime_t curr_time,
	unsigned long long current_ravg_sum)
{
	struct hybridswap_key_point_info *key_point =
		&record->key_point[type];

	if (!key_point->record_cnt)
		key_point->first_time = curr_time;

	key_point->record_cnt++;
	key_point->last_time = curr_time;
	key_point->last_ravg_sum = current_ravg_sum;
}

void hybridswap_perf_end_proc(
	struct hybridswap_key_point_record *record,
	enum hybridswap_key_point type, ktime_t curr_time,
	unsigned long long current_ravg_sum)
{
	struct hybridswap_key_point_info *key_point =
		&record->key_point[type];
	s64 diff_time = ktime_us_delta(curr_time, key_point->last_time);

	key_point->proc_total_time += diff_time;
	if (diff_time > key_point->proc_max_time)
		key_point->proc_max_time = diff_time;

	key_point->proc_ravg_sum += current_ravg_sum -
		key_point->last_ravg_sum;
	key_point->end_cnt++;
	key_point->last_time = curr_time;
	key_point->last_ravg_sum = current_ravg_sum;
}

void hybridswap_perf_async_perf(
	struct hybridswap_key_point_record *record,
	enum hybridswap_key_point type, ktime_t start,
	unsigned long long start_ravg_sum)
{
	unsigned long long current_ravg_sum = ((type == HYBRIDSWAP_CALL_BACK) ||
		(type == HYBRIDSWAP_END_WORK)) ? hybridswap_get_ravg_sum() : 0;
	unsigned long flags;

	spin_lock_irqsave(&record->key_point[type].time_lock, flags);
	hybridswap_perf_start_proc(record, type, start, start_ravg_sum);
	hybridswap_perf_end_proc(record, type, ktime_get(),
		current_ravg_sum);
	spin_unlock_irqrestore(&record->key_point[type].time_lock, flags);
}

void hybridswap_perf_lat_point(
	struct hybridswap_key_point_record *record,
	enum hybridswap_key_point type)
{
	hybridswap_perf_start_proc(record, type, ktime_get(),
		hybridswap_get_ravg_sum());
	record->key_point[type].end_cnt++;
}

void hybridswap_perf_start(
	struct hybridswap_key_point_record *record,
	ktime_t stsrt, unsigned long long start_ravg_sum,
	enum hybridswap_scenario scenario)
{
	hybridswap_perf_init(record, scenario);
	hybridswap_perf_start_proc(record, HYBRIDSWAP_START, stsrt,
		start_ravg_sum);
	record->key_point[HYBRIDSWAP_START].end_cnt++;
}

void hybridswap_perf_lat_stat(
	struct hybridswap_key_point_record *record)
{
	int task_is_fg = 0;
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();
	s64 curr_lat;
	/* reclaim_in: 2s, fault_out: 100ms, batch_out: 500ms, pre_out: 2s */
	s64 timeout_value[HYBRIDSWAP_SCENARIO_BUTT] = {
		2000000, 100000, 500000, 2000000
	};

	if (!stat || (record->scenario >= HYBRIDSWAP_SCENARIO_BUTT))
		return;

	curr_lat = ktime_us_delta(record->key_point[HYBRIDSWAP_DONE].first_time,
		record->key_point[HYBRIDSWAP_START].first_time);
	atomic64_add(curr_lat, &stat->lat[record->scenario].total_lat);
	if (curr_lat > atomic64_read(&stat->lat[record->scenario].max_lat))
		atomic64_set(&stat->lat[record->scenario].max_lat, curr_lat);
	if (curr_lat > timeout_value[record->scenario])
		atomic64_inc(&stat->lat[record->scenario].timeout_cnt);
	if (record->scenario == HYBRIDSWAP_FAULT_OUT) {
		if (curr_lat <= timeout_value[HYBRIDSWAP_FAULT_OUT])
			return;
#ifdef CONFIG_FG_TASK_UID
		task_is_fg = current_is_fg() ? 1 : 0;
#endif
		if (curr_lat > 500000)
			atomic64_inc(&stat->fault_stat[task_is_fg].timeout_500ms_cnt);
		else if (curr_lat > 100000)
			atomic64_inc(&stat->fault_stat[task_is_fg].timeout_100ms_cnt);
		hybp(HYB_INFO, "task %s:%d fault out timeout us %llu fg %d\n",
			current->comm, current->pid, curr_lat, task_is_fg);
	}
}

void hybridswap_perf_end(struct hybridswap_key_point_record *record)
{
	int loglevel;

	hybridswap_perf_stop_monitor(record);
	hybridswap_perf_lat_point(record, HYBRIDSWAP_DONE);
	hybridswap_perf_lat_stat(record);

	loglevel = record->timeout_flag ? HYB_ERR : HYB_DEBUG;
	if (loglevel > hybridswap_loglevel())
		return;

	hybridswap_dump_lat(record,
		record->key_point[HYBRIDSWAP_DONE].first_time, true);
}

void hybridswap_perf_lat_start(
	struct hybridswap_key_point_record *record,
	enum hybridswap_key_point type)
{
	hybridswap_perf_start_proc(record, type, ktime_get(),
		hybridswap_get_ravg_sum());
}

void hybridswap_perf_lat_end(
	struct hybridswap_key_point_record *record,
	enum hybridswap_key_point type)
{
	hybridswap_perf_end_proc(record, type, ktime_get(),
		hybridswap_get_ravg_sum());
}

void hybridswap_perf_io_stat(
	struct hybridswap_key_point_record *record, int page_cnt,
	int segment_cnt)
{
	record->page_cnt = page_cnt;
	record->segment_cnt = segment_cnt;
}

