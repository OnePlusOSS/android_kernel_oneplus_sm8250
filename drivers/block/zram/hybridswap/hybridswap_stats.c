// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[HYBRIDSWAP]" fmt

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/atomic.h>
#include <linux/zsmalloc.h>
#include <linux/memcontrol.h>
#include <linux/proc_fs.h>

#include "../zram_drv.h"
#include "../zram_drv_internal.h"
#include "hybridswap_internal.h"
#include "hybridswap.h"

#define SCENARIO_NAME_LEN 32
#define MBYTE_SHIFT 20

static char scenario_name[HYBRIDSWAP_SCENARIO_BUTT][SCENARIO_NAME_LEN] = {
	"reclaim_in",
	"fault_out",
	"batch_out",
	"pre_out"
};

static char *fg_bg[2] = {"BG", "FG"};

static void hybridswap_lat_show(struct seq_file *m,
	struct hybridswap_stat *stat)
{
	int i;

	for (i = 0; i < HYBRIDSWAP_SCENARIO_BUTT; ++i) {
		seq_printf(m, "hybridswap_%s_total_lat: %lld\n",
			scenario_name[i],
			atomic64_read(&stat->lat[i].total_lat));
		seq_printf(m, "hybridswap_%s_max_lat: %lld\n",
			scenario_name[i],
			atomic64_read(&stat->lat[i].max_lat));
		seq_printf(m, "hybridswap_%s_timeout_cnt: %lld\n",
			scenario_name[i],
			atomic64_read(&stat->lat[i].timeout_cnt));
	}

	for (i = 0; i < 2; i++) {
		seq_printf(m, "fault_out_timeout_100ms_cnt(%s): %lld\n",
			fg_bg[i],
			atomic64_read(&stat->fault_stat[i].timeout_100ms_cnt));
		seq_printf(m, "fault_out_timeout_500ms_cnt(%s): %lld\n",
			fg_bg[i],
			atomic64_read(&stat->fault_stat[i].timeout_500ms_cnt));
	}
}

static void hybridswap_stats_show(struct seq_file *m,
	struct hybridswap_stat *stat)
{
	seq_printf(m, "hybridswap_out_times: %lld\n",
		atomic64_read(&stat->reclaimin_cnt));
	seq_printf(m, "hybridswap_out_comp_size: %lld MB\n",
		atomic64_read(&stat->reclaimin_bytes) >> MBYTE_SHIFT);
	if (PAGE_SHIFT < MBYTE_SHIFT)
		seq_printf(m, "hybridswap_out_ori_size: %lld MB\n",
			atomic64_read(&stat->reclaimin_pages) >>
				(MBYTE_SHIFT - PAGE_SHIFT));
	seq_printf(m, "hybridswap_in_times: %lld\n",
		atomic64_read(&stat->batchout_cnt));
	seq_printf(m, "hybridswap_in_comp_size: %lld MB\n",
		atomic64_read(&stat->batchout_bytes) >> MBYTE_SHIFT);
	if (PAGE_SHIFT < MBYTE_SHIFT)
		seq_printf(m, "hybridswap_in_ori_size: %lld MB\n",
		atomic64_read(&stat->batchout_pages) >>
			(MBYTE_SHIFT - PAGE_SHIFT));
	seq_printf(m, "hybridswap_all_fault: %lld\n",
		atomic64_read(&stat->fault_cnt));
	seq_printf(m, "hybridswap_fault: %lld\n",
		atomic64_read(&stat->hybridswap_fault_cnt));
}

static void hybridswap_area_info_show(struct seq_file *m,
	struct hybridswap_stat *stat)
{
	seq_printf(m, "hybridswap_reout_ori_size: %lld MB\n",
		atomic64_read(&stat->reout_pages) >>
			(MBYTE_SHIFT - PAGE_SHIFT));
	seq_printf(m, "hybridswap_reout_comp_size: %lld MB\n",
		atomic64_read(&stat->reout_bytes) >> MBYTE_SHIFT);
	seq_printf(m, "hybridswap_store_comp_size: %lld MB\n",
		atomic64_read(&stat->stored_size) >> MBYTE_SHIFT);
	seq_printf(m, "hybridswap_store_ori_size: %lld MB\n",
		atomic64_read(&stat->stored_pages) >>
			(MBYTE_SHIFT - PAGE_SHIFT));
	seq_printf(m, "hybridswap_notify_free_size: %lld MB\n",
		atomic64_read(&stat->notify_free) >>
			(MBYTE_SHIFT - EXTENT_SHIFT));
	seq_printf(m, "hybridswap_store_memcg_cnt: %lld\n",
		atomic64_read(&stat->mcg_cnt));
	seq_printf(m, "hybridswap_store_extent_cnt: %lld\n",
		atomic64_read(&stat->ext_cnt));
	seq_printf(m, "hybridswap_store_fragment_cnt: %lld\n",
		atomic64_read(&stat->frag_cnt));
}

static void hybridswap_fail_show(struct seq_file *m,
	struct hybridswap_stat *stat)
{
	int i;

	for (i = 0; i < HYBRIDSWAP_SCENARIO_BUTT; ++i) {
		seq_printf(m, "hybridswap_%s_io_fail_cnt: %lld\n",
			scenario_name[i],
			atomic64_read(&stat->io_fail_cnt[i]));
		seq_printf(m, "hybridswap_%s_alloc_fail_cnt: %lld\n",
			scenario_name[i],
			atomic64_read(&stat->alloc_fail_cnt[i]));
	}
}

int hybridswap_psi_show(struct seq_file *m, void *v)
{
	struct hybridswap_stat *stat = NULL;

	if (!hybridswap_core_enabled())
		return -EINVAL;

	stat = hybridswap_get_stat_obj();
	if (unlikely(!stat)) {
		hybp(HYB_ERR, "can't get stat obj!\n");
		return -EINVAL;
	}

	hybridswap_stats_show(m, stat);
	hybridswap_area_info_show(m, stat);
	hybridswap_lat_show(m, stat);
	hybridswap_fail_show(m, stat);

	return 0;
}

unsigned long hybridswap_get_zram_used_pages(void)
{
	struct hybridswap_stat *stat = NULL;

	if (!hybridswap_core_enabled())
		return 0;

	stat = hybridswap_get_stat_obj();
	if (unlikely(!stat)) {
		hybp(HYB_ERR, "can't get stat obj!\n");

		return 0;
	}

	return atomic64_read(&stat->zram_stored_pages);
}

unsigned long long hybridswap_get_zram_pagefault(void)
{
	struct hybridswap_stat *stat = NULL;

	if (!hybridswap_core_enabled())
		return 0;

	stat = hybridswap_get_stat_obj();
	if (unlikely(!stat)) {
		hybp(HYB_ERR, "can't get stat obj!\n");

		return 0;
	}

	return atomic64_read(&stat->fault_cnt);
}

bool hybridswap_reclaim_work_running(void)
{
	struct hybridswap_stat *stat = NULL;

	if (!hybridswap_core_enabled())
		return false;

	stat = hybridswap_get_stat_obj();
	if (unlikely(!stat)) {
		hybp(HYB_ERR, "can't get stat obj!\n");

		return 0;
	}

	return atomic64_read(&stat->reclaimin_infight) ? true : false;
}

unsigned long long hybridswap_read_mcg_stats(struct mem_cgroup *mcg,
				enum hybridswap_mcg_member mcg_member)
{
	struct mem_cgroup_hybridswap *mcg_hybs;

	unsigned long long val = 0;
	int extcnt;

	if (!hybridswap_core_enabled())
		return 0;

	mcg_hybs = MEMCGRP_ITEM_DATA(mcg);
	if (!mcg_hybs) {
		hybp(HYB_DEBUG, "NULL mcg_hybs\n");
		return 0;
	}

	switch (mcg_member) {
	case MCG_ZRAM_STORED_SZ:
		val = atomic64_read(&mcg_hybs->zram_stored_size);
		break;
	case MCG_ZRAM_STORED_PG_SZ:
		val = atomic64_read(&mcg_hybs->zram_page_size);
		break;
	case MCG_DISK_STORED_SZ:
		val = atomic64_read(&mcg_hybs->hybridswap_stored_size);
		break;
	case MCG_DISK_STORED_PG_SZ:
		val = atomic64_read(&mcg_hybs->hybridswap_stored_pages);
		break;
	case MCG_ANON_FAULT_CNT:
		val = atomic64_read(&mcg_hybs->hybridswap_allfaultcnt);
		break;
	case MCG_DISK_FAULT_CNT:
		val = atomic64_read(&mcg_hybs->hybridswap_faultcnt);
		break;
	case MCG_ESWAPOUT_CNT:
		val = atomic64_read(&mcg_hybs->hybridswap_outcnt);
		break;
	case MCG_ESWAPOUT_SZ:
		val = atomic64_read(&mcg_hybs->hybridswap_outextcnt) << EXTENT_SHIFT;
		break;
	case MCG_ESWAPIN_CNT:
		val = atomic64_read(&mcg_hybs->hybridswap_incnt);
		break;
	case MCG_ESWAPIN_SZ:
		val = atomic64_read(&mcg_hybs->hybridswap_inextcnt) << EXTENT_SHIFT;
		break;
	case MCG_DISK_SPACE:
		extcnt = atomic_read(&mcg_hybs->hybridswap_extcnt);
		if (extcnt < 0)
			extcnt = 0;
		val = ((unsigned long long) extcnt) << EXTENT_SHIFT;
		break;
	case MCG_DISK_SPACE_PEAK:
		extcnt = atomic_read(&mcg_hybs->hybridswap_peakextcnt);
		if (extcnt < 0)
			extcnt = 0;
		val = ((unsigned long long) extcnt) << EXTENT_SHIFT;
		break;
	default:
		break;
	}

	return val;
}

void hybridswap_fail_record(enum hybridswap_fail_point point,
	u32 index, int ext_id, unsigned char *task_comm)
{
	struct hybridswap_stat *stat = NULL;
	unsigned long flags;
	unsigned int copylen = strlen(task_comm) + 1;

	stat = hybridswap_get_stat_obj();
	if (unlikely(!stat)) {
		hybp(HYB_ERR, "can't get stat obj!\n");
		return;
	}

	if (copylen > TASK_COMM_LEN) {
		hybp(HYB_ERR, "task_comm len %d is err\n", copylen);
		return;
	}

	spin_lock_irqsave(&stat->record.lock, flags);
	if (stat->record.num < MAX_FAIL_RECORD_NUM) {
		stat->record.record[stat->record.num].point = point;
		stat->record.record[stat->record.num].index = index;
		stat->record.record[stat->record.num].ext_id = ext_id;
		stat->record.record[stat->record.num].time = ktime_get();
		memcpy(stat->record.record[stat->record.num].task_comm,
			task_comm, copylen);
		stat->record.num++;
	}
	spin_unlock_irqrestore(&stat->record.lock, flags);
}

static void hybridswap_fail_record_get(
	struct hybridswap_fail_record_info *record_info)
{
	struct hybridswap_stat *stat = NULL;
	unsigned long flags;

	if (!hybridswap_core_enabled())
		return;

	stat = hybridswap_get_stat_obj();
	if (unlikely(!stat)) {
		hybp(HYB_ERR, "can't get stat obj!\n");
		return;
	}

	spin_lock_irqsave(&stat->record.lock, flags);
	memcpy(record_info, &stat->record,
		sizeof(struct hybridswap_fail_record_info));
	stat->record.num = 0;
	spin_unlock_irqrestore(&stat->record.lock, flags);
}

static ssize_t hybridswap_fail_record_show(char *buf)
{
	int i;
	ssize_t size = 0;
	struct hybridswap_fail_record_info record_info = { 0 };

	hybridswap_fail_record_get(&record_info);

	size += scnprintf(buf + size, PAGE_SIZE,
			"hybridswap_fail_record_num: %d\n", record_info.num);
	for (i = 0; i < record_info.num; ++i)
		size += scnprintf(buf + size, PAGE_SIZE - size,
			"point[%u]time[%lld]taskname[%s]index[%u]ext_id[%d]\n",
			record_info.record[i].point,
			ktime_us_delta(ktime_get(),
				record_info.record[i].time),
			record_info.record[i].task_comm,
			record_info.record[i].index,
			record_info.record[i].ext_id);

	return size;
}

ssize_t hybridswap_report_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return hybridswap_fail_record_show(buf);
}

static inline meminfo_show(struct hybridswap_stat *stat, char *buf, ssize_t len)
{
	unsigned long eswap_total_pages = 0, eswap_compressed_pages = 0;
	unsigned long eswap_used_pages = 0;
	unsigned long zram_total_pags, zram_used_pages, zram_compressed;
	ssize_t size = 0;

	if (!stat || !buf || !len)
		return 0;

	(void)hybridswap_stored_info(&eswap_total_pages, &eswap_compressed_pages);
	eswap_used_pages = atomic64_read(&stat->stored_pages);
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	zram_total_pags = get_nr_zram_total();
#else
	zram_total_pags = 0;
#endif
	zram_compressed = atomic64_read(&stat->zram_stored_size);
	zram_used_pages = atomic64_read(&stat->zram_stored_pages);

	size += scnprintf(buf + size, len - size, "%-32s %12llu KB\n",
			"EST:", eswap_total_pages << (PAGE_SHIFT - 10));
	size += scnprintf(buf + size, len - size, "%-32s %12llu KB\n",
			"ESU_C:", eswap_compressed_pages << (PAGE_SHIFT - 10));
	size += scnprintf(buf + size, len - size, "%-32s %12llu KB\n",
			"ESU_O:", eswap_used_pages << (PAGE_SHIFT - 10));
	size += scnprintf(buf + size, len - size, "%-32s %12llu KB\n",
			"ZST:", zram_total_pags << (PAGE_SHIFT - 10));
	size += scnprintf(buf + size, len - size, "%-32s %12llu KB\n",
			"ZSU_C:", zram_compressed >> 10);
	size += scnprintf(buf + size, len - size, "%-32s %12llu KB\n",
			"ZSU_O:", zram_used_pages << (PAGE_SHIFT - 10));

	return size;
}

ssize_t hybridswap_stat_snap_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t size = 0;
	struct hybridswap_stat *stat = NULL;

	if (!hybridswap_core_enabled())
		return 0;

	stat = hybridswap_get_stat_obj();
	if (unlikely(!stat)) {
		hybp(HYB_INFO, "can't get stat obj!\n");
		return 0;
	}

	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"reclaimin_cnt:", atomic64_read(&stat->reclaimin_cnt));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu KB\n",
		"reclaimin_bytes:", atomic64_read(&stat->reclaimin_bytes) / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu KB\n",
		"reclaimin_real_load:", atomic64_read(&stat->reclaimin_real_load) / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu KB\n",
		"reclaimin_bytes_daily:", atomic64_read(&stat->reclaimin_bytes_daily) / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu KB\n",
		"reclaimin_pages:", atomic64_read(&stat->reclaimin_pages) * PAGE_SIZE / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"reclaimin_infight:", atomic64_read(&stat->reclaimin_infight));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"batchout_cnt:", atomic64_read(&stat->batchout_cnt));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu KB\n",
		"batchout_bytes:", atomic64_read(&stat->batchout_bytes) / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu KB\n",
		"batchout_real_load:", atomic64_read(&stat->batchout_real_load) / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu KB\n",
		"batchout_pages:", atomic64_read(&stat->batchout_pages) * PAGE_SIZE / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"batchout_inflight:", atomic64_read(&stat->batchout_inflight));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"fault_cnt:", atomic64_read(&stat->fault_cnt));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"hybridswap_fault_cnt:", atomic64_read(&stat->hybridswap_fault_cnt));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu KB\n",
		"reout_pages:", atomic64_read(&stat->reout_pages) * PAGE_SIZE / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu KB\n",
		"reout_bytes:", atomic64_read(&stat->reout_bytes) / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu KB\n",
		"zram_stored_pages:", atomic64_read(&stat->zram_stored_pages) * PAGE_SIZE / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu KB\n",
		"zram_stored_size:", atomic64_read(&stat->zram_stored_size) / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu KB\n",
		"stored_pages:", atomic64_read(&stat->stored_pages) * PAGE_SIZE / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu KB\n",
		"stored_size:", atomic64_read(&stat->stored_size) / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu KB\n",
		"reclain-batchout:", (atomic64_read(&stat->reclaimin_real_load) -
			atomic64_read(&stat->batchout_real_load)) / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12lld KB\n",
		"reclain-batchout-stored:",
			(atomic64_read(&stat->reclaimin_real_load) -
			atomic64_read(&stat->batchout_real_load) -
			atomic64_read(&stat->stored_size)) / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12lld KB\n",
		"dropped_ext_size:", atomic64_read(&stat->dropped_ext_size) / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"notify_free:", atomic64_read(&stat->notify_free));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"frag_cnt:", atomic64_read(&stat->frag_cnt));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"mcg_cnt:", atomic64_read(&stat->mcg_cnt));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"ext_cnt:", atomic64_read(&stat->ext_cnt));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"miss_free:", atomic64_read(&stat->miss_free));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"mcgid_clear:", atomic64_read(&stat->mcgid_clear));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"skip_track_cnt:", atomic64_read(&stat->skip_track_cnt));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"null_memcg_skip_track_cnt:",
		atomic64_read(&stat->null_memcg_skip_track_cnt));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"used_swap_pages:", atomic64_read(&stat->used_swap_pages) * PAGE_SIZE / SZ_1K);
	size += meminfo_show(stat, buf + size, PAGE_SIZE - size);

	return size;
}

ssize_t hybridswap_meminfo_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hybridswap_stat *stat = NULL;

	if (!hybridswap_core_enabled())
		return 0;

	stat = hybridswap_get_stat_obj();
	if (unlikely(!stat)) {
		hybp(HYB_INFO, "can't get stat obj!\n");
		return 0;
	}

	return meminfo_show(stat, buf, PAGE_SIZE);
}
