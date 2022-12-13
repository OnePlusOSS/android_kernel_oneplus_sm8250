// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[HYBRIDSWAP]" fmt

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/sched/task.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/memcontrol.h>
#include <linux/swap.h>
#include <linux/random.h>
#include <linux/genhd.h>
#ifdef CONFIG_FG_TASK_UID
#include <linux/healthinfo/fg.h>
#endif

#include "../zram_drv.h"
#include "../zram_drv_internal.h"
#include "hybridswap_internal.h"
#include "hybridswap.h"

#define PRE_EOL_INFO_OVER_VAL		2
#define LIFE_TIME_EST_OVER_VAL		8
#define DEFAULT_STORED_WM_RATIO		90
#define DEVICE_NAME_LEN			64
#define MIN_RECLAIM_ZRAM_SZ		(1024 * 1024)
#define esentry_extid(e)	((e) >> ESWAP_SHIFT)
#define esentry_pgid(e)		(((e) & ((1 << ESWAP_SHIFT) - 1)) >> PAGE_SHIFT)
#define esentry_pgoff(e)	((e) & (PAGE_SIZE - 1))
#define DUMP_BUF_LEN			512
#define HYBRIDSWAP_KEY_INDEX		0
#define HYBRIDSWAP_KEY_SIZE		64
#define HYBRIDSWAP_KEY_INDEX_SHIFT	3
#define HYBRIDSWAP_MAX_INFILGHT_NUM	256
#define HYBRIDSWAP_SECTOR_SHIFT		9
#define HYBRIDSWAP_PAGE_SIZE_SECTOR	(PAGE_SIZE >> HYBRIDSWAP_SECTOR_SHIFT)
#define HYBRIDSWAP_READ_TIME		10
#define HYBRIDSWAP_WRITE_TIME		100
#define HYB_FAULT_OUT_TIME		10
#define CLASS_NAME_LEN			32
#define MBYTE_SHIFT			20
#define ENTRY_PTR_SHIFT			23
#define ENTRY_MCG_SHIFT_HALF		8
#define ENTRY_LOCK_BIT		ENTRY_MCG_SHIFT_HALF
#define ENTRY_DATA_BIT		(ENTRY_PTR_SHIFT + ENTRY_MCG_SHIFT_HALF + \
		ENTRY_MCG_SHIFT_HALF + 1)

struct zs_eswap_para {
	struct hybridswap_page_pool *pool;
	size_t alloc_size;
	bool fast;
	bool nofail;
};

struct hybridswap_cfg {
	atomic_t enable;
	atomic_t out_to_eswap_enable;
	struct hybstatus *stat;
	struct workqueue_struct *reclaim_wq;
	struct zram *zram;

	atomic_t dev_life;
	unsigned long quota_day;
	struct timer_list lpc_timer;
	struct work_struct lpc_work;
};

struct async_req {
	struct mem_cgroup *mcg;
	unsigned long size;
	unsigned long out_size;
	unsigned long reclaimined_sz;
	struct work_struct work;
	int nice;
	bool preload;
};

struct io_priv {
	struct zram *zram;
	enum hybridswap_class class;
	struct hybridswap_page_pool page_pool;
};

struct io_work_arg {
	void *iohandle;
	struct hybridswap_entry *ioentry;
	struct hybridswap_buffer io_buf;
	struct io_priv data;
	struct hybridswap_key_point_record record;
};

struct hyb_sgm_time {
	ktime_t submit_bio;
	ktime_t end_io;
};

struct hyb_sgm {
	struct work_struct stopio_work;
	struct hyb_sgm_time time;
	struct hybridswap_io_req *req;
	struct hybridswap_entry *io_entries_fifo[BIO_MAX_PAGES];
	struct list_head io_entries;
	sector_t segment_sector;
	int eswap_cnt;
	u32 bio_result;
	int page_cnt;
};

struct hyb_info {
	unsigned long size;
	int total_objects;
	int nr_es;
	int memcg_num;

	unsigned long *bitmask;
	atomic_t last_alloc_bit;

	struct hyb_entries_table *eswap_table;
	struct hyb_entries_head *eswap;

	struct hyb_entries_table *objects;
	struct hyb_entries_head *maps;
	struct hyb_entries_head *lru;

	atomic_t stored_exts;
	atomic_t *eswap_stored_pages;

	unsigned int memcgid_cnt[MEM_CGROUP_ID_MAX + 1];
};

struct hyb_entries_head {
	unsigned int mcg_left : 8;
	unsigned int lock : 1;
	unsigned int prev : 23;
	unsigned int mcg_right : 8;
	unsigned int data : 1;
	unsigned int next : 23;
};

struct hyb_entries_table {
	struct hyb_entries_head *(*fetch_node)(int, void *);
	void *private;
};

#define index_node(index, tab) ((tab)->fetch_node((index), (tab)->private))
#define next_index(index, tab) (index_node((index), (tab))->next)
#define prev_index(index, tab) (index_node((index), (tab))->prev)
#define is_last_index(index, hindex, tab) (next_index(index, tab) == (hindex))
#define is_first_index(index, hindex, tab) (prev_index(index, tab) == (hindex))
#define hyb_entries_for_each_entry(index, hindex, tab) \
	for ((index) = next_index((hindex), (tab)); \
		(index) != (hindex); (index) = next_index((index), (tab)))
#define hyb_entries_for_each_entry_safe(index, tmp, hindex, tab) \
	for ((index) = next_index((hindex), (tab)), (tmp) = next_index((index), (tab)); \
		(index) != (hindex); (index) = (tmp), (tmp) = next_index((index), (tab)))
#define hyb_entries_for_each_entry_reverse(index, hindex, tab) \
	for ((index) = prev_index((hindex), (tab)); \
		(index) != (hindex); (index) = prev_index((index), (tab)))
#define hyb_entries_for_each_entry_reverse_safe(index, tmp, hindex, tab) \
	for ((index) = prev_index((hindex), (tab)), (tmp) = prev_index((index), (tab)); \
		(index) != (hindex); (index) = (tmp), (tmp) = prev_index((index), (tab)))

static unsigned long warn_level[HYB_CLASS_BUTT] = {
	0, 200, 500, 0
};

const char *key_point_name[HYB_KYE_POINT_BUTT] = {
	"START",
	"INIT",
	"IOENTRY_ALLOC",
	"FIND_ESWAP",
	"IO_ESWAP",
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

static const char class_name[HYB_CLASS_BUTT][CLASS_NAME_LEN] = {
	"out_to_eswap",
	"page_fault",
	"batches",
	"readahead"
};

static const char *fg_bg[2] = {"BG", "FG"};

bool hyb_io_work_begin_flag;
struct hybridswap_cfg global_settings;

static u8 hybridswap_io_key[HYBRIDSWAP_KEY_SIZE];
static struct workqueue_struct *hybridswap_proc_read_workqueue;
static struct workqueue_struct *hybridswap_proc_write_workqueue;
static char loop_device[DEVICE_NAME_LEN];

struct mem_cgroup *find_memcg_by_id(unsigned short memcgid);
int obj_index(struct hyb_info *infos, int index);
int eswap_index(struct hyb_info *infos, int index);
int memcgindex(struct hyb_info *infos, int index);
void free_hyb_info(struct hyb_info *infos);
struct hyb_info *alloc_hyb_info(unsigned long ori_size, unsigned long comp_size);
void hybridswap_check_infos_eswap(struct hyb_info *infos);
void hybridswap_free_eswap(struct hyb_info *infos, int eswapid);
int hybridswap_alloc_eswap(struct hyb_info *infos, struct mem_cgroup *mcg);
int fetch_eswap(struct hyb_info *infos, int eswapid);
void put_eswap(struct hyb_info *infos, int eswapid);
int fetch_memcg_eswap(struct hyb_info *infos, struct mem_cgroup *mcg);
int fetch_memcg_zram_entry(struct hyb_info *infos, struct mem_cgroup *mcg);
int fetch_eswap_zram_entry(struct hyb_info *infos, int eswapid);
struct hyb_entries_table *alloc_table(struct hyb_entries_head *(*fetch_node)(int, void *),
		void *private, gfp_t gfp);
void hyb_lock_with_idx(int index, struct hyb_entries_table *table);
void hyb_unlock_with_idx(int index, struct hyb_entries_table *table);
void hyb_entries_init(int index, struct hyb_entries_table *table);
void hyb_entries_add_nolock(int index, int hindex, struct hyb_entries_table *table);
void hyb_entries_add_tail_nolock(int index, int hindex, struct hyb_entries_table *table);
void hyb_entries_del_nolock(int index, int hindex, struct hyb_entries_table *table);
void hyb_entries_add(int index, int hindex, struct hyb_entries_table *table);
void hyb_entries_add_tail(int index, int hindex, struct hyb_entries_table *table);
void hyb_entries_del(int index, int hindex, struct hyb_entries_table *table);
unsigned short hyb_entries_fetch_memcgid(int index, struct hyb_entries_table *table);
void hyb_entries_set_memcgid(int index, struct hyb_entries_table *table, int memcgid);
bool hyb_entries_set_priv(int index, struct hyb_entries_table *table);
bool hyb_entries_clear_priv(int index, struct hyb_entries_table *table);
bool hyb_entries_test_priv(int index, struct hyb_entries_table *table);
bool hyb_entries_empty(int hindex, struct hyb_entries_table *table);
void zram_set_mcg(struct zram *zram, u32 index, int memcgid);
struct mem_cgroup *zram_fetch_mcg(struct zram *zram, u32 index);
int zram_fetch_mcg_last_index(struct hyb_info *infos,
		struct mem_cgroup *mcg,
		int *index, int max_cnt);
int swap_maps_fetch_eswap_index(struct hyb_info *infos,
		int eswapid, int *index);
void swap_sorted_list_add(struct zram *zram, u32 index, struct mem_cgroup *memcg);
void swap_sorted_list_add_tail(struct zram *zram, u32 index, struct mem_cgroup *mcg);
void swap_sorted_list_del(struct zram *zram, u32 index);
void swap_maps_insert(struct zram *zram, u32 index);
void swap_maps_destroy(struct zram *zram, u32 index);

static void hybridswapiowrkshow(struct seq_file *m, struct hybstatus *stat)
{
	int i;

	for (i = 0; i < HYB_CLASS_BUTT; ++i) {
		seq_printf(m, "hybridswap_%s_total_lat: %lld\n",
			class_name[i],
			atomic64_read(&stat->lat[i].total_lat));
		seq_printf(m, "hybridswap_%s_max_lat: %lld\n",
			class_name[i],
			atomic64_read(&stat->lat[i].max_lat));
		seq_printf(m, "hybridswap_%s_timeout_cnt: %lld\n",
			class_name[i],
			atomic64_read(&stat->lat[i].timeout_cnt));
	}

	for (i = 0; i < 2; i++) {
		seq_printf(m, "page_fault_timeout_100ms_cnt(%s): %lld\n",
			fg_bg[i],
			atomic64_read(&stat->fault_stat[i].timeout_100ms_cnt));
		seq_printf(m, "page_fault_timeout_500ms_cnt(%s): %lld\n",
			fg_bg[i],
			atomic64_read(&stat->fault_stat[i].timeout_500ms_cnt));
	}
}

static void hybstatuss_show(struct seq_file *m,
	struct hybstatus *stat)
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

static void hyb_info_info_show(struct seq_file *m,
	struct hybstatus *stat)
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
			(MBYTE_SHIFT - ESWAP_SHIFT));
	seq_printf(m, "hybridswap_store_memcg_cnt: %lld\n",
		atomic64_read(&stat->mcg_cnt));
	seq_printf(m, "hybridswap_store_eswap_cnt: %lld\n",
		atomic64_read(&stat->eswap_cnt));
	seq_printf(m, "hybridswap_store_frag_info_cnt: %lld\n",
		atomic64_read(&stat->frag_cnt));
}

static void hybridswap_fail_show(struct seq_file *m,
	struct hybstatus *stat)
{
	int i;

	for (i = 0; i < HYB_CLASS_BUTT; ++i) {
		seq_printf(m, "hybridswap_%s_io_fail_cnt: %lld\n",
			class_name[i],
			atomic64_read(&stat->io_fail_cnt[i]));
		seq_printf(m, "hybridswap_%s_alloc_fail_cnt: %lld\n",
			class_name[i],
			atomic64_read(&stat->alloc_fail_cnt[i]));
	}
}

int hybridswap_psi_show(struct seq_file *m, void *v)
{
	struct hybstatus *stat = NULL;

	if (!hybridswap_core_enabled())
		return -EINVAL;

	stat = hybridswap_fetch_stat_obj();
	if (unlikely(!stat)) {
		hybp(HYB_ERR, "can't fetch stat obj!\n");
		return -EINVAL;
	}

	hybstatuss_show(m, stat);
	hyb_info_info_show(m, stat);
	hybridswapiowrkshow(m, stat);
	hybridswap_fail_show(m, stat);

	return 0;
}

unsigned long long hybridswap_fetch_zram_pagefault(void)
{
	struct hybstatus *stat = NULL;

	if (!hybridswap_core_enabled())
		return 0;

	stat = hybridswap_fetch_stat_obj();
	if (unlikely(!stat)) {
		hybp(HYB_ERR, "can't fetch stat obj!\n");

		return 0;
	}

	return atomic64_read(&stat->fault_cnt);
}

bool hybridswap_reclaim_work_running(void)
{
	struct hybstatus *stat = NULL;

	if (!hybridswap_core_enabled())
		return false;

	stat = hybridswap_fetch_stat_obj();
	if (unlikely(!stat)) {
		hybp(HYB_ERR, "can't fetch stat obj!\n");

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
		val = atomic64_read(&mcg_hybs->hybridswap_outextcnt) << ESWAP_SHIFT;
		break;
	case MCG_ESWAPIN_CNT:
		val = atomic64_read(&mcg_hybs->hybridswap_incnt);
		break;
	case MCG_ESWAPIN_SZ:
		val = atomic64_read(&mcg_hybs->hybridswap_inextcnt) << ESWAP_SHIFT;
		break;
	case MCG_DISK_SPACE:
		extcnt = atomic_read(&mcg_hybs->hybridswap_extcnt);
		if (extcnt < 0)
			extcnt = 0;
		val = ((unsigned long long) extcnt) << ESWAP_SHIFT;
		break;
	case MCG_DISK_SPACE_PEAK:
		extcnt = atomic_read(&mcg_hybs->hybridswap_peakextcnt);
		if (extcnt < 0)
			extcnt = 0;
		val = ((unsigned long long) extcnt) << ESWAP_SHIFT;
		break;
	default:
		break;
	}

	return val;
}

void hybridswap_fail_record(enum hybridswap_fail_point point,
	u32 index, int eswapid, unsigned char *task_comm)
{
	struct hybstatus *stat = NULL;
	unsigned long flags;
	unsigned int copylen = strlen(task_comm) + 1;

	stat = hybridswap_fetch_stat_obj();
	if (unlikely(!stat)) {
		hybp(HYB_ERR, "can't fetch stat obj!\n");
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
		stat->record.record[stat->record.num].eswapid = eswapid;
		stat->record.record[stat->record.num].time = ktime_get();
		memcpy(stat->record.record[stat->record.num].task_comm,
			task_comm, copylen);
		stat->record.num++;
	}
	spin_unlock_irqrestore(&stat->record.lock, flags);
}

static void hybridswap_fail_record_fetch(
	struct hybridswap_fail_record_info *record_info)
{
	struct hybstatus *stat = NULL;
	unsigned long flags;

	if (!hybridswap_core_enabled())
		return;

	stat = hybridswap_fetch_stat_obj();
	if (unlikely(!stat)) {
		hybp(HYB_ERR, "can't fetch stat obj!\n");
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

	hybridswap_fail_record_fetch(&record_info);

	size += scnprintf(buf + size, PAGE_SIZE,
			"hybridswap_fail_record_num: %d\n", record_info.num);
	for (i = 0; i < record_info.num; ++i)
		size += scnprintf(buf + size, PAGE_SIZE - size,
			"point[%u]time[%lld]taskname[%s]index[%u]eswapid[%d]\n",
			record_info.record[i].point,
			ktime_us_delta(ktime_get(),
				record_info.record[i].time),
			record_info.record[i].task_comm,
			record_info.record[i].index,
			record_info.record[i].eswapid);

	return size;
}

ssize_t hybridswap_report_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return hybridswap_fail_record_show(buf);
}

static inline meminfo_show(struct hybstatus *stat, char *buf, ssize_t len)
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
	zram_total_pags = fetch_nr_zram_total();
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
	struct hybstatus *stat = NULL;

	if (!hybridswap_core_enabled())
		return 0;

	stat = hybridswap_fetch_stat_obj();
	if (unlikely(!stat)) {
		hybp(HYB_INFO, "can't fetch stat obj!\n");
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
		"dropped_eswap_size:", atomic64_read(&stat->dropped_eswap_size) / SZ_1K);
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"notify_free:", atomic64_read(&stat->notify_free));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"frag_cnt:", atomic64_read(&stat->frag_cnt));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"mcg_cnt:", atomic64_read(&stat->mcg_cnt));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"ext_cnt:", atomic64_read(&stat->eswap_cnt));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"miss_free:", atomic64_read(&stat->miss_free));
	size += scnprintf(buf + size, PAGE_SIZE - size, "%-32s %12llu\n",
		"memcgid_clear:", atomic64_read(&stat->memcgid_clear));
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
	struct hybstatus *stat = NULL;

	if (!hybridswap_core_enabled())
		return 0;

	stat = hybridswap_fetch_stat_obj();
	if (unlikely(!stat)) {
		hybp(HYB_INFO, "can't fetch stat obj!\n");
		return 0;
	}

	return meminfo_show(stat, buf, PAGE_SIZE);
}

static void hybridswap_iostatus_bytes(struct hybridswap_io_req *req)
{
	struct hybstatus *stat = hybridswap_fetch_stat_obj();

	if (!stat || !req->page_cnt)
		return;

	if (req->io_para.class == HYB_RECLAIM_IN) {
		atomic64_add(req->page_cnt * PAGE_SIZE, &stat->reclaimin_bytes);
		atomic64_add(req->page_cnt * PAGE_SIZE, &stat->reclaimin_bytes_daily);
		atomic64_add(atomic64_read(&req->real_load), &stat->reclaimin_real_load);
		atomic64_inc(&stat->reclaimin_cnt);
	} else {
		atomic64_add(req->page_cnt * PAGE_SIZE, &stat->batchout_bytes);
		atomic64_inc(&stat->batchout_cnt);
	}
}

static void hybridswap_key_init(void)
{
	get_random_bytes(hybridswap_io_key, HYBRIDSWAP_KEY_SIZE);
}

static void hybridswap_io_req_release(struct kref *ref)
{
	struct hybridswap_io_req *req =
		container_of(ref, struct hybridswap_io_req, refcount);

	if (req->io_para.complete_notify && req->io_para.private)
		req->io_para.complete_notify(req->io_para.private);

	kfree(req);
}

static void hyb_sgm_free(struct hybridswap_io_req *req,
	struct hyb_sgm *segment)
{
	int i;

	for (i = 0; i < segment->eswap_cnt; ++i) {
		INIT_LIST_HEAD(&segment->io_entries_fifo[i]->list);
		req->io_para.done_callback(segment->io_entries_fifo[i], -EIO, req);
	}
	kfree(segment);
}

static void hybridswap_limit_doing(struct hybridswap_io_req *req)
{
	int ret;

	if (!req->limit_doing_flag)
		return;

	if (atomic_read(&req->eswap_doing) >= HYBRIDSWAP_MAX_INFILGHT_NUM) {
		do {
			hybp(HYB_DEBUG, "wait doing start\n");
			ret = wait_event_timeout(req->io_wait,
					atomic_read(&req->eswap_doing) <
					HYBRIDSWAP_MAX_INFILGHT_NUM,
					msecs_to_jiffies(100));
		} while (!ret);
	}
}

static void hybridswap_wait_io_finish(struct hybridswap_io_req *req)
{
	int ret;
	unsigned int wait_time;

	if (!req->wait_io_finish_flag || !req->page_cnt)
		return;

	if (req->io_para.class == HYB_FAULT_OUT) {
		hybp(HYB_DEBUG, "fault out wait finish start\n");
		wait_for_completion_io_timeout(&req->io_end_flag,
				MAX_SCHEDULE_TIMEOUT);

		return;
	}

	wait_time = (req->io_para.class == HYB_RECLAIM_IN) ?
		HYBRIDSWAP_WRITE_TIME : HYBRIDSWAP_READ_TIME;

	do {
		hybp(HYB_DEBUG, "wait finish start\n");
		ret = wait_event_timeout(req->io_wait,
			(!atomic_read(&req->eswap_doing)),
			msecs_to_jiffies(wait_time));
	} while (!ret);
}

static void hybridswap_doing_inc(struct hyb_sgm *segment)
{
	mutex_lock(&segment->req->refmutex);
	kref_get(&segment->req->refcount);
	mutex_unlock(&segment->req->refmutex);
	atomic_add(segment->page_cnt, &segment->req->eswap_doing);
}

static void hybridswap_doing_dec(struct hybridswap_io_req *req,
	int num)
{
	if ((atomic_sub_return(num, &req->eswap_doing) <
		HYBRIDSWAP_MAX_INFILGHT_NUM) && req->limit_doing_flag &&
		wq_has_sleeper(&req->io_wait))
		wake_up(&req->io_wait);
}

static void hybridswap_io_end_wake_up(struct hybridswap_io_req *req)
{
	if (req->io_para.class == HYB_FAULT_OUT) {
		complete(&req->io_end_flag);
		return;
	}

	if (wq_has_sleeper(&req->io_wait))
		wake_up(&req->io_wait);
}

static void hybridswap_ioentry_proc(struct hyb_sgm *segment)
{
	int i;
	struct hybridswap_io_req *req = segment->req;
	struct hybridswap_key_point_record *record = req->io_para.record;
	int page_num;
	ktime_t callback_start;
	unsigned long long callback_start_ravg_sum;

	for (i = 0; i < segment->eswap_cnt; ++i) {
		INIT_LIST_HEAD(&segment->io_entries_fifo[i]->list);
		page_num = segment->io_entries_fifo[i]->pages_sz;
		hybp(HYB_DEBUG, "eswap_id[%d] %d page_num %d\n",
			i, segment->io_entries_fifo[i]->eswapid, page_num);
		callback_start = ktime_get();
		callback_start_ravg_sum = hybridswap_fetch_ravg_sum();
		if (req->io_para.done_callback)
			req->io_para.done_callback(segment->io_entries_fifo[i],
				0, req);
		hybperf_async_perf(record, HYB_CALL_BACK,
			callback_start, callback_start_ravg_sum);
		hybridswap_doing_dec(req, page_num);
	}
}

static void hybridswap_errio_record(enum hybridswap_fail_point point,
	struct hybridswap_io_req *req, int eswapid)
{
	if (req->io_para.class == HYB_FAULT_OUT)
		hybridswap_fail_record(point, 0, eswapid,
			req->io_para.record->task_comm);
}

static void hybridswap_iostatus_fail(enum hybridswap_class class)
{
	struct hybstatus *stat = hybridswap_fetch_stat_obj();

	if (!stat || (class >= HYB_CLASS_BUTT))
		return;

	atomic64_inc(&stat->io_fail_cnt[class]);
}

static void hybridswap_errio_proc(struct hybridswap_io_req *req,
	struct hyb_sgm *segment)
{
	hybp(HYB_ERR, "segment sector 0x%llx, eswap_cnt %d\n",
		segment->segment_sector, segment->eswap_cnt);
	hybp(HYB_ERR, "class %u, bio_result %u\n",
		req->io_para.class, segment->bio_result);
	hybridswap_iostatus_fail(req->io_para.class);
	hybridswap_errio_record(HYB_FAULT_OUT_IO_FAIL, req,
		segment->io_entries_fifo[0]->eswapid);
	hybridswap_doing_dec(req, segment->page_cnt);
	hybridswap_io_end_wake_up(req);
	hyb_sgm_free(req, segment);
	kref_put_mutex(&req->refcount, hybridswap_io_req_release,
		&req->refmutex);
}

static void hybridswap_io_end_work(struct work_struct *work)
{
	struct hyb_sgm *segment =
		container_of(work, struct hyb_sgm, stopio_work);
	struct hybridswap_io_req *req = segment->req;
	struct hybridswap_key_point_record *record = req->io_para.record;
	int old_nice = task_nice(current);
	ktime_t work_start;
	unsigned long long work_start_ravg_sum;

	if (unlikely(segment->bio_result)) {
		hybridswap_errio_proc(req, segment);
		return;
	}

	hybp(HYB_DEBUG, "segment sector 0x%llx, eswap_cnt %d passed\n",
		segment->segment_sector, segment->eswap_cnt);
	hybp(HYB_DEBUG, "class %u, bio_result %u passed\n",
		req->io_para.class, segment->bio_result);

	set_user_nice(current, req->nice);

	hybperf_async_perf(record, HYB_SCHED_WORK,
		segment->time.end_io, 0);
	work_start = ktime_get();
	work_start_ravg_sum = hybridswap_fetch_ravg_sum();

	hybridswap_ioentry_proc(segment);

	hybperf_async_perf(record, HYB_END_WORK, work_start,
		work_start_ravg_sum);

	hybridswap_io_end_wake_up(req);

	kref_put_mutex(&req->refcount, hybridswap_io_req_release,
		&req->refmutex);
	kfree(segment);

	set_user_nice(current, old_nice);
}

static void hybridswap_end_io(struct bio *bio)
{
	struct hyb_sgm *segment = bio->bi_private;
	struct hybridswap_io_req *req = NULL;
	struct workqueue_struct *workqueue = NULL;
	struct hybridswap_key_point_record *record = NULL;

	if (unlikely(!segment || !(segment->req))) {
		hybp(HYB_ERR, "segment or req null\n");
		bio_put(bio);

		return;
	}

	req = segment->req;
	record = req->io_para.record;

	hybperf_async_perf(record, HYB_END_IO,
		segment->time.submit_bio, 0);

	workqueue = (req->io_para.class == HYB_RECLAIM_IN) ?
		hybridswap_proc_write_workqueue : hybridswap_proc_read_workqueue;
	segment->time.end_io = ktime_get();
	segment->bio_result = bio->bi_status;

	queue_work(workqueue, &segment->stopio_work);
	bio_put(bio);
}

static bool hybridswap_eswap_merge_back(
	struct hyb_sgm *segment,
	struct hybridswap_entry *ioentry)
{
	struct hybridswap_entry *tail_ioentry =
		list_last_entry(&segment->io_entries,
			struct hybridswap_entry, list);

	return ((tail_ioentry->addr +
		tail_ioentry->pages_sz * HYBRIDSWAP_PAGE_SIZE_SECTOR) ==
		ioentry->addr);
}

static bool hybridswap_eswap_merge_front(
	struct hyb_sgm *segment,
	struct hybridswap_entry *ioentry)
{
	struct hybridswap_entry *head_ioentry =
		list_first_entry(&segment->io_entries,
			struct hybridswap_entry, list);

	return (head_ioentry->addr ==
		(ioentry->addr +
		ioentry->pages_sz * HYBRIDSWAP_PAGE_SIZE_SECTOR));
}

static bool hybridswap_eswap_merge(struct hybridswap_io_req *req,
	struct hybridswap_entry *ioentry)
{
	struct hyb_sgm *segment = req->segment;

	if (segment == NULL)
		return false;

	if ((segment->page_cnt + ioentry->pages_sz) > BIO_MAX_PAGES)
		return false;

	if (hybridswap_eswap_merge_front(segment, ioentry)) {
		list_add(&ioentry->list, &segment->io_entries);
		segment->io_entries_fifo[segment->eswap_cnt++] = ioentry;
		segment->segment_sector = ioentry->addr;
		segment->page_cnt += ioentry->pages_sz;
		return true;
	}

	if (hybridswap_eswap_merge_back(segment, ioentry)) {
		list_add_tail(&ioentry->list, &segment->io_entries);
		segment->io_entries_fifo[segment->eswap_cnt++] = ioentry;
		segment->page_cnt += ioentry->pages_sz;
		return true;
	}

	return false;
}

static struct bio *hybridswap_bio_alloc(enum hybridswap_class class)
{
	gfp_t gfp = (class != HYB_RECLAIM_IN) ? GFP_ATOMIC : GFP_NOIO;
	struct bio *bio = bio_alloc(gfp, BIO_MAX_PAGES);

	if (!bio && (class == HYB_FAULT_OUT))
		bio = bio_alloc(GFP_NOIO, BIO_MAX_PAGES);

	return bio;
}

static int hybridswap_bio_add_page(struct bio *bio,
	struct hyb_sgm *segment)
{
	int i;
	int k = 0;
	struct hybridswap_entry *ioentry = NULL;
	struct hybridswap_entry *tmp = NULL;

	list_for_each_entry_safe(ioentry, tmp, &segment->io_entries, list)  {
		for (i = 0; i < ioentry->pages_sz; i++) {
			ioentry->dest_pages[i]->index =
				bio->bi_iter.bi_sector + k;
			if (unlikely(!bio_add_page(bio,
				ioentry->dest_pages[i], PAGE_SIZE, 0))) {
				return -EIO;
			}
			k += HYBRIDSWAP_PAGE_SIZE_SECTOR;
		}
	}

	return 0;
}

static void hybridswap_set_bio_opf(struct bio *bio,
	struct hyb_sgm *segment)
{
	if (segment->req->io_para.class == HYB_RECLAIM_IN) {
		bio->bi_opf |= REQ_BACKGROUND;
		return;
	}

	bio->bi_opf |= REQ_SYNC;
}

int hybridswap_submit_bio(struct hyb_sgm *segment)
{
	unsigned int op =
		(segment->req->io_para.class == HYB_RECLAIM_IN) ?
		REQ_OP_WRITE : REQ_OP_READ;
	struct hybridswap_entry *head_ioentry =
		list_first_entry(&segment->io_entries,
			struct hybridswap_entry, list);
	struct hybridswap_key_point_record *record =
		segment->req->io_para.record;
	struct bio *bio = NULL;

	hybperfiowrkstart(record, HYB_BIO_ALLOC);
	bio = hybridswap_bio_alloc(segment->req->io_para.class);
	hybperfiowrkend(record, HYB_BIO_ALLOC);
	if (unlikely(!bio)) {
		hybp(HYB_ERR, "bio is null.\n");
		hybridswap_errio_record(HYB_FAULT_OUT_BIO_ALLOC_FAIL,
			segment->req, segment->io_entries_fifo[0]->eswapid);

		return -ENOMEM;
	}

	bio->bi_iter.bi_sector = segment->segment_sector;
	bio_set_dev(bio, segment->req->io_para.bdev);
	bio->bi_private = segment;
	bio_set_op_attrs(bio, op, 0);
	bio->bi_end_io = hybridswap_end_io;
	hybridswap_set_bio_opf(bio, segment);

	if (unlikely(hybridswap_bio_add_page(bio, segment))) {
		bio_put(bio);
		hybp(HYB_ERR, "bio_add_page fail\n");
		hybridswap_errio_record(HYB_FAULT_OUT_BIO_ADD_FAIL,
			segment->req, segment->io_entries_fifo[0]->eswapid);

		return -EIO;
	}

	hybridswap_doing_inc(segment);
	hybp(HYB_DEBUG, "submit bio sector %llu eswapid %d\n",
		segment->segment_sector, head_ioentry->eswapid);
	hybp(HYB_DEBUG, "eswap_cnt %d class %u\n",
		segment->eswap_cnt, segment->req->io_para.class);

	segment->req->page_cnt += segment->page_cnt;
	segment->req->segment_cnt++;
	segment->time.submit_bio = ktime_get();

	hybperfiowrkstart(record, HYB_SUBMIT_BIO);
	submit_bio(bio);
	hybperfiowrkend(record, HYB_SUBMIT_BIO);

	return 0;
}

static int hybridswap_new_segment_init(struct hybridswap_io_req *req,
	struct hybridswap_entry *ioentry)
{
	gfp_t gfp = (req->io_para.class != HYB_RECLAIM_IN) ?
		GFP_ATOMIC : GFP_NOIO;
	struct hyb_sgm *segment = NULL;
	struct hybridswap_key_point_record *record = req->io_para.record;

	hybperfiowrkstart(record, HYB_SEGMENT_ALLOC);
	segment = kzalloc(sizeof(struct hyb_sgm), gfp);
	if (!segment && (req->io_para.class == HYB_FAULT_OUT))
		segment = kzalloc(sizeof(struct hyb_sgm), GFP_NOIO);
	hybperfiowrkend(record, HYB_SEGMENT_ALLOC);
	if (unlikely(!segment)) {
		hybridswap_errio_record(HYB_FAULT_OUT_SEGMENT_ALLOC_FAIL,
			req, ioentry->eswapid);

		return -ENOMEM;
	}

	segment->req = req;
	INIT_LIST_HEAD(&segment->io_entries);
	list_add_tail(&ioentry->list, &segment->io_entries);
	segment->io_entries_fifo[segment->eswap_cnt++] = ioentry;
	segment->page_cnt = ioentry->pages_sz;
	INIT_WORK(&segment->stopio_work, hybridswap_io_end_work);
	segment->segment_sector = ioentry->addr;
	req->segment = segment;

	return 0;
}

static int hybridswap_io_submit(struct hybridswap_io_req *req,
	bool merge_flag)
{
	int ret;
	struct hyb_sgm *segment = req->segment;

	if (!segment || ((merge_flag) && (segment->page_cnt < BIO_MAX_PAGES)))
		return 0;

	hybridswap_limit_doing(req);

	ret = hybridswap_submit_bio(segment);
	if (unlikely(ret)) {
		hybp(HYB_WARN, "submit bio failed, ret %d\n", ret);
		hyb_sgm_free(req, segment);
	}
	req->segment = NULL;

	return ret;
}

static bool hybridswap_check_io_para_err(struct hybridswap_io *io_para)
{
	if (unlikely(!io_para)) {
		hybp(HYB_ERR, "io_para null\n");

		return true;
	}

	if (unlikely(!io_para->bdev ||
		(io_para->class >= HYB_CLASS_BUTT))) {
		hybp(HYB_ERR, "io_para err, class %u\n",
			io_para->class);

		return true;
	}

	if (unlikely(!io_para->done_callback)) {
		hybp(HYB_ERR, "done_callback err\n");

		return true;
	}

	return false;
}

static bool hybridswap_check_entry_err(
	struct hybridswap_entry *ioentry)
{
	int i;

	if (unlikely(!ioentry)) {
		hybp(HYB_ERR, "ioentry null\n");

		return true;
	}

	if (unlikely((!ioentry->dest_pages) ||
		(ioentry->eswapid < 0) ||
		(ioentry->pages_sz > BIO_MAX_PAGES) ||
		(ioentry->pages_sz <= 0))) {
		hybp(HYB_ERR, "eswapid %d, page_sz %d\n", ioentry->eswapid,
			ioentry->pages_sz);

		return true;
	}

	for (i = 0; i < ioentry->pages_sz; ++i) {
		if (!ioentry->dest_pages[i]) {
			hybp(HYB_ERR, "dest_pages[%d] is null\n", i);
			return true;
		}
	}

	return false;
}

static int hybridswap_io_eswapent(void *iohandle,
	struct hybridswap_entry *ioentry)
{
	int ret;
	struct hybridswap_io_req *req = (struct hybridswap_io_req *)iohandle;

	if (unlikely(hybridswap_check_entry_err(ioentry))) {
		hybridswap_errio_record(HYB_FAULT_OUT_IO_ENTRY_PARA_FAIL,
			req, ioentry ? ioentry->eswapid : -EINVAL);
		req->io_para.done_callback(ioentry, -EIO, req);

		return -EFAULT;
	}

	hybp(HYB_DEBUG, "eswap id %d, pages_sz %d, addr %llx\n",
		ioentry->eswapid, ioentry->pages_sz,
		ioentry->addr);

	if (hybridswap_eswap_merge(req, ioentry))
		return hybridswap_io_submit(req, true);

	ret = hybridswap_io_submit(req, false);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "submit fail %d\n", ret);
		req->io_para.done_callback(ioentry, -EIO, req);

		return ret;
	}

	ret = hybridswap_new_segment_init(req, ioentry);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "hybridswap_new_segment_init fail %d\n", ret);
		req->io_para.done_callback(ioentry, -EIO, req);

		return ret;
	}

	return 0;
}

int hyb_io_work_begin(void)
{
	if (hyb_io_work_begin_flag)
		return 0;

	hybridswap_proc_read_workqueue = alloc_workqueue("proc_hybridswap_read",
		WQ_HIGHPRI | WQ_UNBOUND, 0);
	if (unlikely(!hybridswap_proc_read_workqueue))
		return -EFAULT;

	hybridswap_proc_write_workqueue = alloc_workqueue("proc_hybridswap_write",
		WQ_CPU_INTENSIVE, 0);
	if (unlikely(!hybridswap_proc_write_workqueue)) {
		destroy_workqueue(hybridswap_proc_read_workqueue);

		return -EFAULT;
	}

	hybridswap_key_init();

	hyb_io_work_begin_flag = true;

	return 0;
}

void *hybridswap_plug_start(struct hybridswap_io *io_para)
{
	gfp_t gfp;
	struct hybridswap_io_req *req = NULL;

	if (unlikely(hybridswap_check_io_para_err(io_para)))
		return NULL;

	gfp = (io_para->class != HYB_RECLAIM_IN) ?
		GFP_ATOMIC : GFP_NOIO;
	req = kzalloc(sizeof(struct hybridswap_io_req), gfp);
	if (!req && (io_para->class == HYB_FAULT_OUT))
		req = kzalloc(sizeof(struct hybridswap_io_req), GFP_NOIO);

	if (unlikely(!req)) {
		hybp(HYB_ERR, "io_req null\n");

		return NULL;
	}

	kref_init(&req->refcount);
	mutex_init(&req->refmutex);
	atomic_set(&req->eswap_doing, 0);
	init_waitqueue_head(&req->io_wait);
	req->io_para.bdev = io_para->bdev;
	req->io_para.class = io_para->class;
	req->io_para.done_callback = io_para->done_callback;
	req->io_para.complete_notify = io_para->complete_notify;
	req->io_para.private = io_para->private;
	req->io_para.record = io_para->record;
	req->limit_doing_flag =
		(io_para->class == HYB_RECLAIM_IN) ||
		(io_para->class == HYB_PRE_OUT);
	req->wait_io_finish_flag =
		(io_para->class == HYB_RECLAIM_IN) ||
		(io_para->class == HYB_FAULT_OUT);
	req->nice = task_nice(current);
	init_completion(&req->io_end_flag);

	return (void *)req;
}

int hybridswap_read_eswap(void *iohandle,
	struct hybridswap_entry *ioentry)
{
	return hybridswap_io_eswapent(iohandle, ioentry);
}

int hybridswap_write_eswap(void *iohandle,
	struct hybridswap_entry *ioentry)
{
	return hybridswap_io_eswapent(iohandle, ioentry);
}

int hybridswap_plug_finish(void *iohandle)
{
	int ret;
	struct hybridswap_io_req *req = (struct hybridswap_io_req *)iohandle;

	hybperfiowrkstart(req->io_para.record, HYB_IO_ESWAP);
	ret = hybridswap_io_submit(req, false);
	if (unlikely(ret))
		hybp(HYB_ERR, "submit fail %d\n", ret);

	hybperfiowrkend(req->io_para.record, HYB_IO_ESWAP);
	hybridswap_wait_io_finish(req);
	hybperfiowrkpoint(req->io_para.record, HYB_WAKE_UP);

	hybridswap_iostatus_bytes(req);
	hybperf_io_stat(req->io_para.record, req->page_cnt,
		req->segment_cnt);

	kref_put_mutex(&req->refcount, hybridswap_io_req_release,
		&req->refmutex);

	hybp(HYB_DEBUG, "io schedule finish succ\n");

	return ret;
}

static void hybridswap_dump_point_lat(
	struct hybridswap_key_point_record *record, ktime_t start)
{
	int i;

	for (i = 0; i < HYB_KYE_POINT_BUTT; ++i) {
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

	for (i = 0; i < HYB_KYE_POINT_BUTT; ++i)
		if (record->key_point[i].record_cnt)
			point = i;

	point++;
	if (point < HYB_KYE_POINT_BUTT)
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

	start = record->key_point[HYB_START].first_time;
	total_time = ktime_us_delta(curr_time, start);
	count += snprintf(log + count,
		(size_t)(DUMP_BUF_LEN - count),
		"totaltime(us) %lld class %u task %s nice %d",
		total_time, record->class, record->task_comm, record->nice);

	if (perf_end_flag)
		count += snprintf(log + count, (size_t)(DUMP_BUF_LEN - count),
			" page %d segment %d speed(bps) %lld level %llu",
			record->page_cnt, record->segment_cnt,
			hybridswap_calc_speed(record->page_cnt, total_time),
			record->warn_level);
	else
		count += snprintf(log + count, (size_t)(DUMP_BUF_LEN - count),
			" state %c", task_state_to_char(record->task));

	hybridswap_dump_no_record_point(record, log, &count);

	hybp(HYB_ERR, "perf end flag %u %s\n", perf_end_flag, log);
	hybridswap_dump_point_lat(record, start);
	dump_stack();
}

static unsigned long hybperf_warn_level(
	enum hybridswap_class class)
{
	if (unlikely(class >= HYB_CLASS_BUTT))
		return 0;

	return warn_level[class];
}

void hybperf_warning(struct timer_list *t)
{
	struct hybridswap_key_point_record *record =
		from_timer(record, t, lat_monitor);
	static unsigned long last_dumpiowrkjiffies = 0;

	if (!record->warn_level)
		return;

	if (jiffies_to_msecs(jiffies - last_dumpiowrkjiffies) <= 60000)
		return;

	hybridswap_dump_lat(record, ktime_get(), false);

	if (likely(record->task))
		sched_show_task(record->task);
	last_dumpiowrkjiffies = jiffies;
	record->warn_level <<= 2;
	record->timeout_flag = true;
	mod_timer(&record->lat_monitor,
		jiffies + msecs_to_jiffies(record->warn_level));
}

static void hybperf_init_monitor(
	struct hybridswap_key_point_record *record,
	enum hybridswap_class class)
{
	record->warn_level = hybperf_warn_level(class);

	if (!record->warn_level)
		return;

	record->task = current;
	get_task_struct(record->task);
	timer_setup(&record->lat_monitor, hybperf_warning, 0);
	mod_timer(&record->lat_monitor,
			jiffies + msecs_to_jiffies(record->warn_level));
}

static void hybperf_stop_monitor(
	struct hybridswap_key_point_record *record)
{
	if (!record->warn_level)
		return;

	del_timer_sync(&record->lat_monitor);
	put_task_struct(record->task);
}

static void hybperf_init(struct hybridswap_key_point_record *record,
	enum hybridswap_class class)
{
	int i;

	for (i = 0; i < HYB_KYE_POINT_BUTT; ++i)
		spin_lock_init(&record->key_point[i].time_lock);

	record->nice = task_nice(current);
	record->class = class;
	get_task_comm(record->task_comm, current);
	hybperf_init_monitor(record, class);
}

void hybperf_start_proc(
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

void hybperf_end_proc(
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

void hybperf_async_perf(
	struct hybridswap_key_point_record *record,
	enum hybridswap_key_point type, ktime_t start,
	unsigned long long start_ravg_sum)
{
	unsigned long long current_ravg_sum = ((type == HYB_CALL_BACK) ||
		(type == HYB_END_WORK)) ? hybridswap_fetch_ravg_sum() : 0;
	unsigned long flags;

	spin_lock_irqsave(&record->key_point[type].time_lock, flags);
	hybperf_start_proc(record, type, start, start_ravg_sum);
	hybperf_end_proc(record, type, ktime_get(),
		current_ravg_sum);
	spin_unlock_irqrestore(&record->key_point[type].time_lock, flags);
}

void hybperfiowrkpoint(
	struct hybridswap_key_point_record *record,
	enum hybridswap_key_point type)
{
	hybperf_start_proc(record, type, ktime_get(),
		hybridswap_fetch_ravg_sum());
	record->key_point[type].end_cnt++;
}

void hybperf_start(
	struct hybridswap_key_point_record *record,
	ktime_t stsrt, unsigned long long start_ravg_sum,
	enum hybridswap_class class)
{
	hybperf_init(record, class);
	hybperf_start_proc(record, HYB_START, stsrt,
		start_ravg_sum);
	record->key_point[HYB_START].end_cnt++;
}

void hybperfiowrkstat(
	struct hybridswap_key_point_record *record)
{
	int task_is_fg = 0;
	struct hybstatus *stat = hybridswap_fetch_stat_obj();
	s64 curr_lat;
	s64 timeout_value[HYB_CLASS_BUTT] = {
		2000000, 100000, 500000, 2000000
	};

	if (!stat || (record->class >= HYB_CLASS_BUTT))
		return;

	curr_lat = ktime_us_delta(record->key_point[HYB_DONE].first_time,
		record->key_point[HYB_START].first_time);
	atomic64_add(curr_lat, &stat->lat[record->class].total_lat);
	if (curr_lat > atomic64_read(&stat->lat[record->class].max_lat))
		atomic64_set(&stat->lat[record->class].max_lat, curr_lat);
	if (curr_lat > timeout_value[record->class])
		atomic64_inc(&stat->lat[record->class].timeout_cnt);
	if (record->class == HYB_FAULT_OUT) {
		if (curr_lat <= timeout_value[HYB_FAULT_OUT])
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

void hybperf_end(struct hybridswap_key_point_record *record)
{
	int loglevel;

	hybperf_stop_monitor(record);
	hybperfiowrkpoint(record, HYB_DONE);
	hybperfiowrkstat(record);

	loglevel = record->timeout_flag ? HYB_ERR : HYB_DEBUG;
	if (loglevel > hybridswap_loglevel())
		return;

	hybridswap_dump_lat(record,
		record->key_point[HYB_DONE].first_time, true);
}

void hybperfiowrkstart(
	struct hybridswap_key_point_record *record,
	enum hybridswap_key_point type)
{
	hybperf_start_proc(record, type, ktime_get(),
		hybridswap_fetch_ravg_sum());
}

void hybperfiowrkend(
	struct hybridswap_key_point_record *record,
	enum hybridswap_key_point type)
{
	hybperf_end_proc(record, type, ktime_get(),
		hybridswap_fetch_ravg_sum());
}

void hybperf_io_stat(
	struct hybridswap_key_point_record *record, int page_cnt,
	int segment_cnt)
{
	record->page_cnt = page_cnt;
	record->segment_cnt = segment_cnt;
}

static struct io_eswapent *alloc_io_eswapent(struct hybridswap_page_pool *pool,
				  bool fast, bool nofail)
{
	int i;
	struct io_eswapent *io_eswap = hybridswap_malloc(sizeof(struct io_eswapent),
						     fast, nofail);

	if (!io_eswap) {
		hybp(HYB_ERR, "alloc io_eswap failed\n");
		return NULL;
	}

	io_eswap->eswapid = -EINVAL;
	io_eswap->pool = pool;
	for (i = 0; i < (int)ESWAP_PG_CNT; i++) {
		io_eswap->pages[i] = hybridswap_alloc_page(pool, GFP_ATOMIC,
							fast, nofail);
		if (!io_eswap->pages[i]) {
			hybp(HYB_ERR, "alloc page[%d] failed\n", i);
			goto page_free;
		}
	}
	return io_eswap;
page_free:
	for (i = 0; i < (int)ESWAP_PG_CNT; i++)
		if (io_eswap->pages[i])
			hybridswap_page_recycle(io_eswap->pages[i], pool);
	hybridswap_free(io_eswap);

	return NULL;
}

static void discard_io_eswapent(struct io_eswapent *io_eswap, unsigned int op)
{
	struct zram *zram = NULL;
	int i;

	if (!io_eswap) {
		hybp(HYB_ERR, "NULL io_eswap\n");
		return;
	}
	if (!io_eswap->mcg)
		zram = io_eswap->zram;
	else
		zram = MEMCGRP_ITEM(io_eswap->mcg, zram);
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		goto out;
	}
	for (i = 0; i < (int)ESWAP_PG_CNT; i++)
		if (io_eswap->pages[i])
			hybridswap_page_recycle(io_eswap->pages[i], io_eswap->pool);
	if (io_eswap->eswapid < 0)
		goto out;
	hybp(HYB_DEBUG, "eswap = %d, op = %d\n", io_eswap->eswapid, op);
	if (op == REQ_OP_READ) {
		put_eswap(zram->infos, io_eswap->eswapid);
		goto out;
	}
	for (i = 0; i < io_eswap->cnt; i++) {
		u32 index = io_eswap->index[i];

		zram_slot_lock(zram, index);
		if (io_eswap->mcg)
			swap_sorted_list_add_tail(zram, index, io_eswap->mcg);
		zram_clear_flag(zram, index, ZRAM_UNDER_WB);
		zram_slot_unlock(zram, index);
	}
	hybridswap_free_eswap(zram->infos, io_eswap->eswapid);
out:
	hybridswap_free(io_eswap);
}

static void copy_to_pages(u8 *src, struct page *pages[],
		   unsigned long eswpentry, int size)
{
	u8 *dst = NULL;
	int pg_id = esentry_pgid(eswpentry);
	int offset = esentry_pgoff(eswpentry);

	if (!src) {
		hybp(HYB_ERR, "NULL src\n");
		return;
	}
	if (!pages) {
		hybp(HYB_ERR, "NULL pages\n");
		return;
	}
	if (size < 0 || size > (int)PAGE_SIZE) {
		hybp(HYB_ERR, "size = %d invalid\n", size);
		return;
	}
	dst = page_to_virt(pages[pg_id]);
	if (offset + size <= (int)PAGE_SIZE) {
		memcpy(dst + offset, src, size);
		return;
	}
	if (pg_id == ESWAP_PG_CNT - 1) {
		hybp(HYB_ERR, "eswap overflow, addr = %lx, size = %d\n",
			 eswpentry, size);
		return;
	}
	memcpy(dst + offset, src, PAGE_SIZE - offset);
	dst = page_to_virt(pages[pg_id + 1]);
	memcpy(dst, src + PAGE_SIZE - offset, offset + size - PAGE_SIZE);
}

static void copy_from_pages(u8 *dst, struct page *pages[],
		     unsigned long eswpentry, int size)
{
	u8 *src = NULL;
	int pg_id = esentry_pgid(eswpentry);
	int offset = esentry_pgoff(eswpentry);

	if (!dst) {
		hybp(HYB_ERR, "NULL dst\n");
		return;
	}
	if (!pages) {
		hybp(HYB_ERR, "NULL pages\n");
		return;
	}
	if (size < 0 || size > (int)PAGE_SIZE) {
		hybp(HYB_ERR, "size = %d invalid\n", size);
		return;
	}

	src = page_to_virt(pages[pg_id]);
	if (offset + size <= (int)PAGE_SIZE) {
		memcpy(dst, src + offset, size);
		return;
	}
	if (pg_id == ESWAP_PG_CNT - 1) {
		hybp(HYB_ERR, "eswap overflow, addr = %lx, size = %d\n",
			 eswpentry, size);
		return;
	}
	memcpy(dst, src + offset, PAGE_SIZE - offset);
	src = page_to_virt(pages[pg_id + 1]);
	memcpy(dst + PAGE_SIZE - offset, src, offset + size - PAGE_SIZE);
}

static bool zram_test_skip(struct zram *zram, u32 index, struct mem_cgroup *mcg)
{
	if (zram_test_flag(zram, index, ZRAM_WB))
		return true;
	if (zram_test_flag(zram, index, ZRAM_UNDER_WB))
		return true;
	if (zram_test_flag(zram, index, ZRAM_BATCHING_OUT))
		return true;
	if (zram_test_flag(zram, index, ZRAM_SAME))
		return true;
	if (mcg != zram_fetch_mcg(zram, index))
		return true;
	if (!zram_get_obj_size(zram, index))
		return true;

	return false;
}

static bool zram_test_overwrite(struct zram *zram, u32 index, int eswapid)
{
	if (!zram_test_flag(zram, index, ZRAM_WB))
		return true;
	if (eswapid != esentry_extid(zram_get_handle(zram, index)))
		return true;

	return false;
}

static void update_size_info(struct zram *zram, u32 index)
{
	struct hybstatus *stat;
	int size = zram_get_obj_size(zram, index);
	struct mem_cgroup *mcg;
	memcg_hybs_t *hybs;
	int eswapid;

	if (!zram_test_flag(zram, index, ZRAM_IN_BD))
		return;

	eswapid = esentry_extid(zram_get_handle(zram, index));
	hybp(HYB_INFO, "eswapid %d index %d\n", eswapid, index);

	if (eswapid >= 0 && eswapid < zram->infos->nr_es)
		atomic_dec(&zram->infos->eswap_stored_pages[eswapid]);
	else {
		hybp(HYB_ERR, "eswap = %d invalid\n", eswapid);
		eswapid = -1;
	}

	stat = hybridswap_fetch_stat_obj();
	if (stat) {
		atomic64_add(size, &stat->dropped_eswap_size);
		atomic64_sub(size, &stat->stored_size);
		atomic64_dec(&stat->stored_pages);
	} else
		hybp(HYB_ERR, "NULL stat\n");

	mcg = zram_fetch_mcg(zram, index);
	if (mcg) {
		hybs = MEMCGRP_ITEM_DATA(mcg);

		if (hybs) {
			atomic64_sub(size, &hybs->hybridswap_stored_size);
			atomic64_dec(&hybs->hybridswap_stored_pages);
		} else
			hybp(HYB_ERR, "NULL hybs\n");
	} else
		hybp(HYB_ERR, "NULL mcg\n");
	zram_clear_flag(zram, index, ZRAM_IN_BD);
}

static void move_to_hybridswap(struct zram *zram, u32 index,
		       unsigned long eswpentry, struct mem_cgroup *mcg)
{
	int size;
	struct hybstatus *stat = hybridswap_fetch_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}
	if (index >= (u32)zram->infos->total_objects) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return;
	}
	if (!mcg) {
		hybp(HYB_ERR, "NULL mcg\n");
		return;
	}

	size = zram_get_obj_size(zram, index);

	zram_clear_flag(zram, index, ZRAM_UNDER_WB);

	zs_free(zram->mem_pool, zram_get_handle(zram, index));
	atomic64_sub(size, &zram->stats.compr_data_size);
	atomic64_dec(&zram->stats.pages_stored);

	zram_set_mcg(zram, index, mcg->id.id);
	zram_set_flag(zram, index, ZRAM_IN_BD);
	zram_set_flag(zram, index, ZRAM_WB);
	zram_set_obj_size(zram, index, size);
	if (size == PAGE_SIZE)
		zram_set_flag(zram, index, ZRAM_HUGE);
	zram_set_handle(zram, index, eswpentry);
	swap_maps_insert(zram, index);

	atomic64_add(size, &stat->stored_size);
	atomic64_add(size, &MEMCGRP_ITEM(mcg, hybridswap_stored_size));
	atomic64_inc(&stat->stored_pages);
	atomic_inc(&zram->infos->eswap_stored_pages[esentry_extid(eswpentry)]);
	atomic64_inc(&MEMCGRP_ITEM(mcg, hybridswap_stored_pages));
}

static void __move_to_zram(struct zram *zram, u32 index, unsigned long handle,
			struct io_eswapent *io_eswap)
{
	struct hybstatus *stat = hybridswap_fetch_stat_obj();
	struct mem_cgroup *mcg = io_eswap->mcg;
	int size = zram_get_obj_size(zram, index);

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}

	zram_slot_lock(zram, index);
	if (zram_test_overwrite(zram, index, io_eswap->eswapid)) {
		zram_slot_unlock(zram, index);
		zs_free(zram->mem_pool, handle);
		return;
	}
	swap_maps_destroy(zram, index);
	zram_set_handle(zram, index, handle);
	zram_clear_flag(zram, index, ZRAM_WB);
	if (mcg)
		swap_sorted_list_add_tail(zram, index, mcg);
	zram_set_flag(zram, index, ZRAM_FROM_HYBRIDSWAP);
	atomic64_add(size, &zram->stats.compr_data_size);
	atomic64_inc(&zram->stats.pages_stored);
	zram_clear_flag(zram, index, ZRAM_IN_BD);
	zram_slot_unlock(zram, index);

	atomic64_inc(&stat->batchout_pages);
	atomic64_sub(size, &stat->stored_size);
	atomic64_dec(&stat->stored_pages);
	atomic64_add(size, &stat->batchout_real_load);
	atomic_dec(&zram->infos->eswap_stored_pages[io_eswap->eswapid]);
	if (mcg) {
		atomic64_sub(size, &MEMCGRP_ITEM(mcg, hybridswap_stored_size));
		atomic64_dec(&MEMCGRP_ITEM(mcg, hybridswap_stored_pages));
	}
}

static int move_to_zram(struct zram *zram, u32 index, struct io_eswapent *io_eswap)
{
	unsigned long handle, eswpentry;
	struct mem_cgroup *mcg = NULL;
	int size, i;
	u8 *dst = NULL;

	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return -EINVAL;
	}
	if (index >= (u32)zram->infos->total_objects) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return -EINVAL;
	}
	if (!io_eswap) {
		hybp(HYB_ERR, "NULL io_eswap\n");
		return -EINVAL;
	}

	mcg = io_eswap->mcg;
	zram_slot_lock(zram, index);
	eswpentry = zram_get_handle(zram, index);
	if (zram_test_overwrite(zram, index, io_eswap->eswapid)) {
		zram_slot_unlock(zram, index);
		return 0;
	}
	size = zram_get_obj_size(zram, index);
	zram_slot_unlock(zram, index);

	for (i = esentry_pgid(eswpentry) - 1; i >= 0 && io_eswap->pages[i]; i--) {
		hybridswap_page_recycle(io_eswap->pages[i], io_eswap->pool);
		io_eswap->pages[i] = NULL;
	}
	handle = hybridswap_zsmalloc(zram->mem_pool, size, io_eswap->pool);
	if (!handle) {
		hybp(HYB_ERR, "alloc handle failed, size = %d\n", size);
		return -ENOMEM;
	}
	dst = zs_map_object(zram->mem_pool, handle, ZS_MM_WO);
	copy_from_pages(dst, io_eswap->pages, eswpentry, size);
	zs_unmap_object(zram->mem_pool, handle);
	__move_to_zram(zram, index, handle, io_eswap);

	return 0;
}

static int eswap_unlock(struct io_eswapent *io_eswap)
{
	int eswapid;
	struct mem_cgroup *mcg = NULL;
	struct zram *zram = NULL;
	int k;
	unsigned long eswpentry;
	int real_load = 0, size;

	if (!io_eswap) {
		hybp(HYB_ERR, "NULL io_eswap\n");
		goto out;
	}

	mcg = io_eswap->mcg;
	if (!mcg) {
		hybp(HYB_ERR, "NULL mcg\n");
		goto out;
	}
	zram = MEMCGRP_ITEM(mcg, zram);
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		goto out;
	}
	eswapid = io_eswap->eswapid;
	if (eswapid < 0)
		goto out;

	eswapid = io_eswap->eswapid;
	if (MEMCGRP_ITEM(mcg, in_swapin))
		goto out;
	hybp(HYB_DEBUG, "add eswapid = %d, cnt = %d.\n",
			eswapid, io_eswap->cnt);
	eswpentry = ((unsigned long)eswapid) << ESWAP_SHIFT;
	for (k = 0; k < io_eswap->cnt; k++)
		zram_slot_lock(zram, io_eswap->index[k]);
	for (k = 0; k < io_eswap->cnt; k++) {
		move_to_hybridswap(zram, io_eswap->index[k], eswpentry, mcg);
		size = zram_get_obj_size(zram, io_eswap->index[k]);
		eswpentry += size;
		real_load += size;
	}
	put_eswap(zram->infos, eswapid);
	io_eswap->eswapid = -EINVAL;
	for (k = 0; k < io_eswap->cnt; k++)
		zram_slot_unlock(zram, io_eswap->index[k]);
	hybp(HYB_DEBUG, "add eswap OK.\n");
out:
	discard_io_eswapent(io_eswap, REQ_OP_WRITE);
	if (mcg)
		css_put(&mcg->css);

	return real_load;
}

static void eswap_add(struct io_eswapent *io_eswap,
		       enum hybridswap_class class)
{
	struct mem_cgroup *mcg = NULL;
	struct zram *zram = NULL;
	int eswapid;
	int k;

	if (!io_eswap) {
		hybp(HYB_ERR, "NULL io_eswap\n");
		return;
	}

	mcg = io_eswap->mcg;
	if (!mcg)
		zram = io_eswap->zram;
	else
		zram = MEMCGRP_ITEM(mcg, zram);
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		goto out;
	}

	eswapid = io_eswap->eswapid;
	if (eswapid < 0)
		goto out;

	io_eswap->cnt = swap_maps_fetch_eswap_index(zram->infos,
						 eswapid,
						 io_eswap->index);
	hybp(HYB_DEBUG, "eswapid = %d, cnt = %d.\n", eswapid, io_eswap->cnt);
	for (k = 0; k < io_eswap->cnt; k++) {
		int ret = move_to_zram(zram, io_eswap->index[k], io_eswap);

		if (ret < 0)
			goto out;
	}
	hybp(HYB_DEBUG, "eswap add OK, free eswapid = %d.\n", eswapid);
	hybridswap_free_eswap(zram->infos, io_eswap->eswapid);
	io_eswap->eswapid = -EINVAL;
	if (mcg) {
		atomic64_inc(&MEMCGRP_ITEM(mcg, hybridswap_inextcnt));
		atomic_dec(&MEMCGRP_ITEM(mcg, hybridswap_extcnt));
	}
out:
	discard_io_eswapent(io_eswap, REQ_OP_READ);
	if (mcg)
		css_put(&mcg->css);
}

static void eswap_clear(struct zram *zram, int eswapid)
{
	int *index = NULL;
	int cnt;
	int k;
	struct hybstatus *stat = hybridswap_fetch_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}

	index = kzalloc(sizeof(int) * ESWAP_MAX_OBJ_CNT, GFP_NOIO);
	if (!index)
		index = kzalloc(sizeof(int) * ESWAP_MAX_OBJ_CNT,
				GFP_NOIO | __GFP_NOFAIL);

	cnt = swap_maps_fetch_eswap_index(zram->infos, eswapid, index);

	for (k = 0; k < cnt; k++) {
		zram_slot_lock(zram, index[k]);
		if (zram_test_overwrite(zram, index[k], eswapid)) {
			zram_slot_unlock(zram, index[k]);
			continue;
		}
		zram_set_mcg(zram, index[k], 0);
		zram_set_flag(zram, index[k], ZRAM_MCGID_CLEAR);
		atomic64_inc(&stat->memcgid_clear);
		zram_slot_unlock(zram, index[k]);
	}

	kfree(index);
}

static int shrink_entry(struct zram *zram, u32 index, struct io_eswapent *io_eswap,
		 unsigned long eswap_off)
{
	unsigned long handle;
	int size;
	u8 *src = NULL;
	struct hybstatus *stat = hybridswap_fetch_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return -EINVAL;
	}
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return -EINVAL;
	}
	if (index >= (u32)zram->infos->total_objects) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return -EINVAL;
	}

	zram_slot_lock(zram, index);
	handle = zram_get_handle(zram, index);
	if (!handle || zram_test_skip(zram, index, io_eswap->mcg)) {
		zram_slot_unlock(zram, index);
		return 0;
	}
	size = zram_get_obj_size(zram, index);
	if (eswap_off + size > ESWAP_SIZE) {
		zram_slot_unlock(zram, index);
		return -ENOSPC;
	}

	src = zs_map_object(zram->mem_pool, handle, ZS_MM_RO);
	copy_to_pages(src, io_eswap->pages, eswap_off, size);
	zs_unmap_object(zram->mem_pool, handle);
	io_eswap->index[io_eswap->cnt++] = index;

	swap_sorted_list_del(zram, index);
	zram_set_flag(zram, index, ZRAM_UNDER_WB);
	if (zram_test_flag(zram, index, ZRAM_FROM_HYBRIDSWAP)) {
		atomic64_inc(&stat->reout_pages);
		atomic64_add(size, &stat->reout_bytes);
	}
	zram_slot_unlock(zram, index);
	atomic64_inc(&stat->reclaimin_pages);

	return size;
}

static int shrink_entry_list(struct io_eswapent *io_eswap)
{
	struct mem_cgroup *mcg = NULL;
	struct zram *zram = NULL;
	unsigned long stored_size;
	int *swap_index = NULL;
	int swap_cnt, k;
	int swap_size = 0;

	if (!io_eswap) {
		hybp(HYB_ERR, "NULL io_eswap\n");
		return -EINVAL;
	}

	mcg = io_eswap->mcg;
	zram = MEMCGRP_ITEM(mcg, zram);
	hybp(HYB_DEBUG, "mcg = %d\n", mcg->id.id);
	stored_size = atomic64_read(&MEMCGRP_ITEM(mcg, zram_stored_size));
	hybp(HYB_DEBUG, "zram_stored_size = %ld\n", stored_size);
	if (stored_size < ESWAP_SIZE) {
		hybp(HYB_INFO, "%lu is smaller than ESWAP_SIZE\n", stored_size);
		return -ENOENT;
	}

	swap_index = kzalloc(sizeof(int) * ESWAP_MAX_OBJ_CNT, GFP_NOIO);
	if (!swap_index)
		return -ENOMEM;
	io_eswap->eswapid = hybridswap_alloc_eswap(zram->infos, mcg);
	if (io_eswap->eswapid < 0) {
		kfree(swap_index);
		return io_eswap->eswapid;
	}
	swap_cnt = zram_fetch_mcg_last_index(zram->infos, mcg, swap_index,
						ESWAP_MAX_OBJ_CNT);
	io_eswap->cnt = 0;
	for (k = 0; k < swap_cnt && swap_size < (int)ESWAP_SIZE; k++) {
		int size = shrink_entry(zram, swap_index[k], io_eswap, swap_size);

		if (size < 0)
			break;
		swap_size += size;
	}
	kfree(swap_index);
	hybp(HYB_DEBUG, "fill eswap = %d, cnt = %d, overhead = %ld.\n",
		 io_eswap->eswapid, io_eswap->cnt, ESWAP_SIZE - swap_size);
	if (swap_size == 0) {
		hybp(HYB_ERR, "swap_size = 0, free eswapid = %d.\n",
			io_eswap->eswapid);
		hybridswap_free_eswap(zram->infos, io_eswap->eswapid);
		io_eswap->eswapid = -EINVAL;
		return -ENOENT;
	}

	return swap_size;
}

void hybridswap_manager_deinit(struct zram *zram)
{
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}

	free_hyb_info(zram->infos);
	zram->infos = NULL;
}

int hybridswap_manager_init(struct zram *zram)
{
	int ret;

	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		ret = -EINVAL;
		goto out;
	}

	zram->infos = alloc_hyb_info(zram->disksize,
					  zram->nr_pages << PAGE_SHIFT);
	if (!zram->infos) {
		ret = -ENOMEM;
		goto out;
	}
	return 0;
out:
	hybridswap_manager_deinit(zram);

	return ret;
}

void hybridswap_manager_memcg_init(struct zram *zram,
						struct mem_cgroup *memcg)
{
	memcg_hybs_t *hybs;

	if (!memcg || !zram || !zram->infos) {
		hybp(HYB_ERR, "invalid zram or mcg_hyb\n");
		return;
	}

	hyb_entries_init(memcgindex(zram->infos, memcg->id.id), zram->infos->objects);
	hyb_entries_init(memcgindex(zram->infos, memcg->id.id), zram->infos->eswap_table);

	hybs = MEMCGRP_ITEM_DATA(memcg);
	hybs->in_swapin = false;
	atomic64_set(&hybs->zram_stored_size, 0);
	atomic64_set(&hybs->zram_page_size, 0);
	atomic64_set(&hybs->hybridswap_stored_pages, 0);
	atomic64_set(&hybs->hybridswap_stored_size, 0);
	atomic64_set(&hybs->hybridswap_allfaultcnt, 0);
	atomic64_set(&hybs->hybridswap_outcnt, 0);
	atomic64_set(&hybs->hybridswap_incnt, 0);
	atomic64_set(&hybs->hybridswap_faultcnt, 0);
	atomic64_set(&hybs->hybridswap_outextcnt, 0);
	atomic64_set(&hybs->hybridswap_inextcnt, 0);
	atomic_set(&hybs->hybridswap_extcnt, 0);
	atomic_set(&hybs->hybridswap_peakextcnt, 0);
	mutex_init(&hybs->swap_lock);

	smp_wmb();
	hybs->zram = zram;
	hybp(HYB_DEBUG, "new memcg in zram, id = %d.\n", memcg->id.id);
}

void hybridswap_manager_memcg_deinit(struct mem_cgroup *mcg)
{
	struct zram *zram = NULL;
	struct hyb_info *infos = NULL;
	struct hybstatus *stat = hybridswap_fetch_stat_obj();
	int last_index = -1;
	memcg_hybs_t *hybs;

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}

	hybs = MEMCGRP_ITEM_DATA(mcg);
	if (!hybs->zram)
		return;

	zram = hybs->zram;
	if (!zram->infos) {
		hybp(HYB_WARN, "mcg %p name %s id %d zram %p infos is NULL\n",
			mcg, hybs->name,   mcg->id.id, zram);
		return;
	}

	hybp(HYB_DEBUG, "deinit mcg %d %s\n", mcg->id.id, hybs->name);
	if (mcg->id.id == 0)
		return;

	infos = zram->infos;
	while (1) {
		int index = fetch_memcg_zram_entry(infos, mcg);

		if (index == -ENOENT)
			break;

		if (index < 0) {
			hybp(HYB_ERR, "invalid index\n");
			return;
		}

		if (last_index == index) {
			hybp(HYB_ERR, "dup index %d\n", index);
			dump_stack();
		}

		zram_slot_lock(zram, index);
		if (index == last_index || mcg == zram_fetch_mcg(zram, index)) {
			hyb_entries_del(obj_index(zram->infos, index),
					memcgindex(zram->infos, mcg->id.id),
					zram->infos->objects);
			zram_set_mcg(zram, index, 0);
			zram_set_flag(zram, index, ZRAM_MCGID_CLEAR);
			atomic64_inc(&stat->memcgid_clear);
		}
		zram_slot_unlock(zram, index);
		last_index = index;
	}

	hybp(HYB_DEBUG, "deinit mcg %d %s, entry done\n", mcg->id.id, hybs->name);
	while (1) {
		int eswapid = fetch_memcg_eswap(infos, mcg);

		if (eswapid == -ENOENT)
			break;

		eswap_clear(zram, eswapid);
		hyb_entries_set_memcgid(eswap_index(infos, eswapid), infos->eswap_table, 0);
		put_eswap(infos, eswapid);
	}
	hybp(HYB_DEBUG, "deinit mcg %d %s, eswap done\n", mcg->id.id, hybs->name);
	hybs->zram = NULL;
}
void hybridswap_swap_sorted_list_add(struct zram *zram,
			    u32 index, struct mem_cgroup *memcg)
{
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}
	if (index >= (u32)zram->infos->total_objects) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return;
	}

	swap_sorted_list_add(zram, index, memcg);
}

void hybridswap_swap_sorted_list_del(struct zram *zram, u32 index)
{
	struct hybstatus *stat = hybridswap_fetch_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}
	if (index >= (u32)zram->infos->total_objects) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return;
	}

	zram_clear_flag(zram, index, ZRAM_FROM_HYBRIDSWAP);
	if (zram_test_flag(zram, index, ZRAM_MCGID_CLEAR)) {
		zram_clear_flag(zram, index, ZRAM_MCGID_CLEAR);
		atomic64_dec(&stat->memcgid_clear);
	}

	if (zram_test_flag(zram, index, ZRAM_WB)) {
		update_size_info(zram, index);
		swap_maps_destroy(zram, index);
		zram_clear_flag(zram, index, ZRAM_WB);
		zram_set_mcg(zram, index, 0);
		zram_set_handle(zram, index, 0);
	} else {
		swap_sorted_list_del(zram, index);
	}
}

unsigned long hybridswap_eswap_create(struct mem_cgroup *mcg,
				      int *eswapid,
				      struct hybridswap_buffer *buf,
				      void **private)
{
	struct io_eswapent *io_eswap = NULL;
	int reclaim_size;

	if (!mcg) {
		hybp(HYB_ERR, "NULL mcg\n");
		return 0;
	}
	if (!eswapid) {
		hybp(HYB_ERR, "NULL eswapid\n");
		return 0;
	}
	(*eswapid) = -EINVAL;
	if (!buf) {
		hybp(HYB_ERR, "NULL buf\n");
		return 0;
	}
	if (!private) {
		hybp(HYB_ERR, "NULL private\n");
		return 0;
	}

	io_eswap = alloc_io_eswapent(buf->pool, false, true);
	if (!io_eswap)
		return 0;
	io_eswap->mcg = mcg;
	reclaim_size = shrink_entry_list(io_eswap);
	if (reclaim_size < 0) {
		discard_io_eswapent(io_eswap, REQ_OP_WRITE);
		(*eswapid) = reclaim_size;
		return 0;
	}
	io_eswap->real_load = reclaim_size;
	css_get(&mcg->css);
	(*eswapid) = io_eswap->eswapid;
	buf->dest_pages = io_eswap->pages;
	(*private) = io_eswap;
	hybp(HYB_DEBUG, "mcg = %d, eswapid = %d\n", mcg->id.id, io_eswap->eswapid);

	return reclaim_size;
}

void hybridswap_eswap_register(void *private, struct hybridswap_io_req *req)
{
	struct io_eswapent *io_eswap = private;

	if (!io_eswap) {
		hybp(HYB_ERR, "NULL io_eswap\n");
		return;
	}
	hybp(HYB_DEBUG, "eswapid = %d\n", io_eswap->eswapid);
	atomic64_add(eswap_unlock(io_eswap), &req->real_load);
}

void hybridswap_eswap_objs_del(struct zram *zram, u32 index)
{
	int eswapid;
	struct mem_cgroup *mcg = NULL;
	unsigned long eswpentry;
	int size;
	struct hybstatus *stat = hybridswap_fetch_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}
	if (!zram || !zram->infos) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}
	if (index >= (u32)zram->infos->total_objects) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return;
	}
	if (!zram_test_flag(zram, index, ZRAM_WB)) {
		hybp(HYB_ERR, "not WB object\n");
		return;
	}

	eswpentry = zram_get_handle(zram, index);
	size = zram_get_obj_size(zram, index);
	atomic64_sub(size, &stat->stored_size);
	atomic64_dec(&stat->stored_pages);
	atomic64_add(size, &stat->dropped_eswap_size);
	mcg = zram_fetch_mcg(zram, index);
	if (mcg) {
		atomic64_sub(size, &MEMCGRP_ITEM(mcg, hybridswap_stored_size));
		atomic64_dec(&MEMCGRP_ITEM(mcg, hybridswap_stored_pages));
	}

	zram_clear_flag(zram, index, ZRAM_IN_BD);
	if (!atomic_dec_and_test(
			&zram->infos->eswap_stored_pages[esentry_extid(eswpentry)]))
		return;
	eswapid = fetch_eswap(zram->infos, esentry_extid(eswpentry));
	if (eswapid < 0)
		return;

	atomic64_inc(&stat->notify_free);
	if (mcg)
		atomic64_inc(&MEMCGRP_ITEM(mcg, hybridswap_eswap_notify_free));
	hybp(HYB_DEBUG, "free eswapid = %d\n", eswapid);
	hybridswap_free_eswap(zram->infos, eswapid);
}

int hybridswap_find_eswap_by_index(unsigned long eswpentry,
				 struct hybridswap_buffer *buf,
				 void **private)
{
	int eswapid;
	struct io_eswapent *io_eswap = NULL;
	struct zram *zram = NULL;

	if (!buf) {
		hybp(HYB_ERR, "NULL buf\n");
		return -EINVAL;
	}
	if (!private) {
		hybp(HYB_ERR, "NULL private\n");
		return -EINVAL;
	}

	zram = buf->zram;
	eswapid = fetch_eswap(zram->infos, esentry_extid(eswpentry));
	if (eswapid < 0)
		return eswapid;
	io_eswap = alloc_io_eswapent(buf->pool, true, true);
	if (!io_eswap) {
		hybp(HYB_ERR, "io_eswap alloc failed\n");
		put_eswap(zram->infos, eswapid);
		return -ENOMEM;
	}

	io_eswap->eswapid = eswapid;
	io_eswap->zram = zram;
	io_eswap->mcg = find_memcg_by_id(
				hyb_entries_fetch_memcgid(eswap_index(zram->infos, eswapid),
						  zram->infos->eswap_table));
	if (io_eswap->mcg)
		css_get(&io_eswap->mcg->css);
	buf->dest_pages = io_eswap->pages;
	(*private) = io_eswap;
	hybp(HYB_DEBUG, "fetch entry = %lx eswap = %d\n", eswpentry, eswapid);

	return eswapid;
}

int hybridswap_find_eswap_by_memcg(struct mem_cgroup *mcg,
		struct hybridswap_buffer *buf,
		void **private)
{
	int eswapid;
	struct io_eswapent *io_eswap = NULL;

	if (!mcg) {
		hybp(HYB_ERR, "NULL mcg\n");
		return -EINVAL;
	}
	if (!buf) {
		hybp(HYB_ERR, "NULL buf\n");
		return -EINVAL;
	}
	if (!private) {
		hybp(HYB_ERR, "NULL private\n");
		return -EINVAL;
	}

	eswapid = fetch_memcg_eswap(MEMCGRP_ITEM(mcg, zram)->infos, mcg);
	if (eswapid < 0)
		return eswapid;
	io_eswap = alloc_io_eswapent(buf->pool, true, false);
	if (!io_eswap) {
		hybp(HYB_ERR, "io_eswap alloc failed\n");
		put_eswap(MEMCGRP_ITEM(mcg, zram)->infos, eswapid);
		return -ENOMEM;
	}
	io_eswap->eswapid = eswapid;
	io_eswap->mcg = mcg;
	css_get(&mcg->css);
	buf->dest_pages = io_eswap->pages;
	(*private) = io_eswap;
	hybp(HYB_DEBUG, "fetch mcg = %d, eswap = %d\n", mcg->id.id, eswapid);

	return eswapid;
}

void hybridswap_eswap_destroy(void *private, enum hybridswap_class class)
{
	struct io_eswapent *io_eswap = private;

	if (!io_eswap) {
		hybp(HYB_ERR, "NULL io_eswap\n");
		return;
	}

	hybp(HYB_DEBUG, "eswapid = %d\n", io_eswap->eswapid);
	eswap_add(io_eswap, class);
}

void hybridswap_eswap_exception(enum hybridswap_class class,
			       void *private)
{
	struct io_eswapent *io_eswap = private;
	struct mem_cgroup *mcg = NULL;
	unsigned int op = (class == HYB_RECLAIM_IN) ?
			  REQ_OP_WRITE : REQ_OP_READ;

	if (!io_eswap) {
		hybp(HYB_ERR, "NULL io_eswap\n");
		return;
	}

	hybp(HYB_DEBUG, "eswapid = %d, op = %d\n", io_eswap->eswapid, op);
	mcg = io_eswap->mcg;
	discard_io_eswapent(io_eswap, op);
	if (mcg)
		css_put(&mcg->css);
}

struct mem_cgroup *hybridswap_zram_fetch_mcg(struct zram *zram, u32 index)
{
	return zram_fetch_mcg(zram, index);
}

void zram_set_mcg(struct zram *zram, u32 index, int memcgid)
{
	hyb_entries_set_memcgid(obj_index(zram->infos, index),
				zram->infos->objects, memcgid);
}

struct mem_cgroup *zram_fetch_mcg(struct zram *zram, u32 index)
{
	unsigned short memcgid;

	memcgid = hyb_entries_fetch_memcgid(obj_index(zram->infos, index),
				zram->infos->objects);

	return find_memcg_by_id(memcgid);
}

int zram_fetch_mcg_last_index(struct hyb_info *infos,
				 struct mem_cgroup *mcg,
				 int *index, int max_cnt)
{
	int cnt = 0;
	u32 i, tmp;

	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return 0;
	}
	if (!infos->objects) {
		hybp(HYB_ERR, "NULL table\n");
		return 0;
	}
	if (!mcg) {
		hybp(HYB_ERR, "NULL mcg\n");
		return 0;
	}
	if (!index) {
		hybp(HYB_ERR, "NULL index\n");
		return 0;
	}

	hyb_lock_with_idx(memcgindex(infos, mcg->id.id), infos->objects);
	hyb_entries_for_each_entry_reverse_safe(i, tmp,
		memcgindex(infos, mcg->id.id), infos->objects) {
		if (i >= (u32)infos->total_objects) {
			hybp(HYB_ERR, "index = %d invalid\n", i);
			continue;
		}
		index[cnt++] = i;
		if (cnt >= max_cnt)
			break;
	}
	hyb_unlock_with_idx(memcgindex(infos, mcg->id.id), infos->objects);

	return cnt;
}

int swap_maps_fetch_eswap_index(struct hyb_info *infos,
			       int eswapid, int *index)
{
	int cnt = 0;
	u32 i;

	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return 0;
	}
	if (!infos->objects) {
		hybp(HYB_ERR, "NULL table\n");
		return 0;
	}
	if (!index) {
		hybp(HYB_ERR, "NULL index\n");
		return 0;
	}
	if (eswapid < 0 || eswapid >= infos->nr_es) {
		hybp(HYB_ERR, "eswap = %d invalid\n", eswapid);
		return 0;
	}

	hyb_lock_with_idx(eswap_index(infos, eswapid), infos->objects);
	hyb_entries_for_each_entry(i, eswap_index(infos, eswapid), infos->objects) {
		if (cnt >= (int)ESWAP_MAX_OBJ_CNT) {
			WARN_ON_ONCE(1);
			break;
		}
		index[cnt++] = i;
	}
	hyb_unlock_with_idx(eswap_index(infos, eswapid), infos->objects);

	return cnt;
}

void swap_sorted_list_add(struct zram *zram, u32 index, struct mem_cgroup *memcg)
{
	unsigned long size;
	struct hybstatus *stat = hybridswap_fetch_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}
	if (index >= (u32)zram->infos->total_objects) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return;
	}
	if (zram_test_flag(zram, index, ZRAM_WB)) {
		hybp(HYB_ERR, "WB object, index = %d\n", index);
		return;
	}
#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
	if (zram_test_flag(zram, index, ZRAM_CACHED)) {
		hybp(HYB_ERR, "CACHED object, index = %d\n", index);
		return;
	}
	if (zram_test_flag(zram, index, ZRAM_CACHED_COMPRESS)) {
		hybp(HYB_ERR, "CACHED_COMPRESS object, index = %d\n", index);
		return;
	}
#endif
	if (zram_test_flag(zram, index, ZRAM_SAME))
		return;

	zram_set_mcg(zram, index, memcg->id.id);
	hyb_entries_add(obj_index(zram->infos, index),
			memcgindex(zram->infos, memcg->id.id),
			zram->infos->objects);

	size = zram_get_obj_size(zram, index);

	atomic64_add(size, &MEMCGRP_ITEM(memcg, zram_stored_size));
	atomic64_inc(&MEMCGRP_ITEM(memcg, zram_page_size));
	atomic64_add(size, &stat->zram_stored_size);
	atomic64_inc(&stat->zram_stored_pages);
}

void swap_sorted_list_add_tail(struct zram *zram, u32 index, struct mem_cgroup *mcg)
{
	unsigned long size;
	struct hybstatus *stat = hybridswap_fetch_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}
	if (!mcg || !MEMCGRP_ITEM(mcg, zram) || !MEMCGRP_ITEM(mcg, zram)->infos) {
		hybp(HYB_ERR, "invalid mcg\n");
		return;
	}
	if (index >= (u32)zram->infos->total_objects) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return;
	}
	if (zram_test_flag(zram, index, ZRAM_WB)) {
		hybp(HYB_ERR, "WB object, index = %d\n", index);
		return;
	}
#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
	if (zram_test_flag(zram, index, ZRAM_CACHED)) {
		hybp(HYB_ERR, "CACHED object, index = %d\n", index);
		return;
	}
	if (zram_test_flag(zram, index, ZRAM_CACHED_COMPRESS)) {
		hybp(HYB_ERR, "CACHED_COMPRESS object, index = %d\n", index);
		return;
	}
#endif
	if (zram_test_flag(zram, index, ZRAM_SAME))
		return;

	zram_set_mcg(zram, index, mcg->id.id);
	hyb_entries_add_tail(obj_index(zram->infos, index),
			memcgindex(zram->infos, mcg->id.id),
			zram->infos->objects);

	size = zram_get_obj_size(zram, index);

	atomic64_add(size, &MEMCGRP_ITEM(mcg, zram_stored_size));
	atomic64_inc(&MEMCGRP_ITEM(mcg, zram_page_size));
	atomic64_add(size, &stat->zram_stored_size);
	atomic64_inc(&stat->zram_stored_pages);
}

void swap_sorted_list_del(struct zram *zram, u32 index)
{
	struct mem_cgroup *mcg = NULL;
	unsigned long size;
	struct hybstatus *stat = hybridswap_fetch_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}
	if (!zram || !zram->infos) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}
	if (index >= (u32)zram->infos->total_objects) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return;
	}
	if (zram_test_flag(zram, index, ZRAM_WB)) {
		hybp(HYB_ERR, "WB object, index = %d\n", index);
		return;
	}

	mcg = zram_fetch_mcg(zram, index);
	if (!mcg || !MEMCGRP_ITEM(mcg, zram) || !MEMCGRP_ITEM(mcg, zram)->infos)
		return;
	if (zram_test_flag(zram, index, ZRAM_SAME))
		return;

	size = zram_get_obj_size(zram, index);
	hyb_entries_del(obj_index(zram->infos, index),
			memcgindex(zram->infos, mcg->id.id),
			zram->infos->objects);
	zram_set_mcg(zram, index, 0);

	atomic64_sub(size, &MEMCGRP_ITEM(mcg, zram_stored_size));
	atomic64_dec(&MEMCGRP_ITEM(mcg, zram_page_size));
	atomic64_sub(size, &stat->zram_stored_size);
	atomic64_dec(&stat->zram_stored_pages);
}

void swap_maps_insert(struct zram *zram, u32 index)
{
	unsigned long eswpentry;
	u32 eswapid;

	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}
	if (index >= (u32)zram->infos->total_objects) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return;
	}

	eswpentry = zram_get_handle(zram, index);
	eswapid = esentry_extid(eswpentry);
	hyb_entries_add_tail(obj_index(zram->infos, index),
			eswap_index(zram->infos, eswapid),
			zram->infos->objects);
}

void swap_maps_destroy(struct zram *zram, u32 index)
{
	unsigned long eswpentry;
	u32 eswapid;

	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}
	if (index >= (u32)zram->infos->total_objects) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return;
	}

	eswpentry = zram_get_handle(zram, index);
	eswapid = esentry_extid(eswpentry);
	hyb_entries_del(obj_index(zram->infos, index),
		eswap_index(zram->infos, eswapid),
		zram->infos->objects);
}

static struct hyb_entries_head *fetch_node_default(int index, void *private)
{
	struct hyb_entries_head *table = private;

	return &table[index];
}

struct hyb_entries_table *alloc_table(struct hyb_entries_head *(*fetch_node)(int, void *),
				  void *private, gfp_t gfp)
{
	struct hyb_entries_table *table =
				kmalloc(sizeof(struct hyb_entries_table), gfp);

	if (!table)
		return NULL;
	table->fetch_node = fetch_node ? fetch_node : fetch_node_default;
	table->private = private;

	return table;
}

void hyb_lock_with_idx(int index, struct hyb_entries_table *table)
{
	struct hyb_entries_head *node = index_node(index, table);

	if (!node) {
		hybp(HYB_ERR, "index = %d, table = %pK\n", index, table);
		return;
	}
	bit_spin_lock(ENTRY_LOCK_BIT, (unsigned long *)node);
}

void hyb_unlock_with_idx(int index, struct hyb_entries_table *table)
{
	struct hyb_entries_head *node = index_node(index, table);

	if (!node) {
		hybp(HYB_ERR, "index = %d, table = %pK\n", index, table);
		return;
	}
	bit_spin_unlock(ENTRY_LOCK_BIT, (unsigned long *)node);
}

bool hyb_entries_empty(int hindex, struct hyb_entries_table *table)
{
	bool ret = false;

	hyb_lock_with_idx(hindex, table);
	ret = (prev_index(hindex, table) == hindex) && (next_index(hindex, table) == hindex);
	hyb_unlock_with_idx(hindex, table);

	return ret;
}

void hyb_entries_init(int index, struct hyb_entries_table *table)
{
	struct hyb_entries_head *node = index_node(index, table);

	if (!node) {
		hybp(HYB_ERR, "index = %d, table = %pS func %pS\n",
			index, table, table->fetch_node);
		return;
	}
	memset(node, 0, sizeof(struct hyb_entries_head));
	node->prev = index;
	node->next = index;
}

void hyb_entries_add_nolock(int index, int hindex, struct hyb_entries_table *table)
{
	struct hyb_entries_head *node = NULL;
	struct hyb_entries_head *head = NULL;
	struct hyb_entries_head *next = NULL;
	int nindex;

	node = index_node(index, table);
	if (!node) {
		hybp(HYB_ERR,
			 "NULL node, index = %d, hindex = %d, table = %pK\n",
			 index, hindex, table);
		return;
	}
	head = index_node(hindex, table);
	if (!head) {
		hybp(HYB_ERR,
			 "NULL head, index = %d, hindex = %d, table = %pK\n",
			 index, hindex, table);
		return;
	}
	next = index_node(head->next, table);
	if (!next) {
		hybp(HYB_ERR,
			 "NULL next, index = %d, hindex = %d, table = %pK\n",
			 index, hindex, table);
		return;
	}

	nindex = head->next;
	if (index != hindex)
		hyb_lock_with_idx(index, table);
	node->prev = hindex;
	node->next = nindex;
	if (index != hindex)
		hyb_unlock_with_idx(index, table);
	head->next = index;
	if (nindex != hindex)
		hyb_lock_with_idx(nindex, table);
	next->prev = index;
	if (nindex != hindex)
		hyb_unlock_with_idx(nindex, table);
}

void hyb_entries_add_tail_nolock(int index, int hindex, struct hyb_entries_table *table)
{
	struct hyb_entries_head *node = NULL;
	struct hyb_entries_head *head = NULL;
	struct hyb_entries_head *tail = NULL;
	int tindex;

	node = index_node(index, table);
	if (!node) {
		hybp(HYB_ERR,
			 "NULL node, index = %d, hindex = %d, table = %pK\n",
			 index, hindex, table);
		return;
	}
	head = index_node(hindex, table);
	if (!head) {
		hybp(HYB_ERR,
			 "NULL head, index = %d, hindex = %d, table = %pK\n",
			 index, hindex, table);
		return;
	}
	tail = index_node(head->prev, table);
	if (!tail) {
		hybp(HYB_ERR,
			 "NULL tail, index = %d, hindex = %d, table = %pK\n",
			 index, hindex, table);
		return;
	}

	tindex = head->prev;
	if (index != hindex)
		hyb_lock_with_idx(index, table);
	node->prev = tindex;
	node->next = hindex;
	if (index != hindex)
		hyb_unlock_with_idx(index, table);
	head->prev = index;
	if (tindex != hindex)
		hyb_lock_with_idx(tindex, table);
	tail->next = index;
	if (tindex != hindex)
		hyb_unlock_with_idx(tindex, table);
}

void hyb_entries_del_nolock(int index, int hindex, struct hyb_entries_table *table)
{
	struct hyb_entries_head *node = NULL;
	struct hyb_entries_head *prev = NULL;
	struct hyb_entries_head *next = NULL;
	int pindex, nindex;

	node = index_node(index, table);
	if (!node) {
		hybp(HYB_ERR,
			 "NULL node, index = %d, hindex = %d, table = %pK\n",
			 index, hindex, table);
		return;
	}
	prev = index_node(node->prev, table);
	if (!prev) {
		hybp(HYB_ERR,
			 "NULL prev, index = %d, hindex = %d, table = %pK\n",
			 index, hindex, table);
		return;
	}
	next = index_node(node->next, table);
	if (!next) {
		hybp(HYB_ERR,
			 "NULL next, index = %d, hindex = %d, table = %pK\n",
			 index, hindex, table);
		return;
	}

	if (index != hindex)
		hyb_lock_with_idx(index, table);
	pindex = node->prev;
	nindex = node->next;
	node->prev = index;
	node->next = index;
	if (index != hindex)
		hyb_unlock_with_idx(index, table);
	if (pindex != hindex)
		hyb_lock_with_idx(pindex, table);
	prev->next = nindex;
	if (pindex != hindex)
		hyb_unlock_with_idx(pindex, table);
	if (nindex != hindex)
		hyb_lock_with_idx(nindex, table);
	next->prev = pindex;
	if (nindex != hindex)
		hyb_unlock_with_idx(nindex, table);
}

void hyb_entries_add(int index, int hindex, struct hyb_entries_table *table)
{
	hyb_lock_with_idx(hindex, table);
	hyb_entries_add_nolock(index, hindex, table);
	hyb_unlock_with_idx(hindex, table);
}

void hyb_entries_add_tail(int index, int hindex, struct hyb_entries_table *table)
{
	hyb_lock_with_idx(hindex, table);
	hyb_entries_add_tail_nolock(index, hindex, table);
	hyb_unlock_with_idx(hindex, table);
}

void hyb_entries_del(int index, int hindex, struct hyb_entries_table *table)
{
	hyb_lock_with_idx(hindex, table);
	hyb_entries_del_nolock(index, hindex, table);
	hyb_unlock_with_idx(hindex, table);
}

unsigned short hyb_entries_fetch_memcgid(int index, struct hyb_entries_table *table)
{
	struct hyb_entries_head *node = index_node(index, table);
	int memcgid;

	if (!node) {
		hybp(HYB_ERR, "index = %d, table = %pK\n", index, table);
		return 0;
	}

	hyb_lock_with_idx(index, table);
	memcgid = (node->mcg_left << ENTRY_MCG_SHIFT_HALF) | node->mcg_right;
	hyb_unlock_with_idx(index, table);

	return memcgid;
}

void hyb_entries_set_memcgid(int index, struct hyb_entries_table *table, int memcgid)
{
	struct hyb_entries_head *node = index_node(index, table);

	if (!node) {
		hybp(HYB_ERR, "index = %d, table = %pK, mcg = %d\n",
			 index, table, memcgid);
		return;
	}

	hyb_lock_with_idx(index, table);
	node->mcg_left = (u32)memcgid >> ENTRY_MCG_SHIFT_HALF;
	node->mcg_right = (u32)memcgid & ((1 << ENTRY_MCG_SHIFT_HALF) - 1);
	hyb_unlock_with_idx(index, table);
}

bool hyb_entries_set_priv(int index, struct hyb_entries_table *table)
{
	struct hyb_entries_head *node = index_node(index, table);
	bool ret = false;

	if (!node) {
		hybp(HYB_ERR, "index = %d, table = %pK\n", index, table);
		return false;
	}
	hyb_lock_with_idx(index, table);
	ret = !test_and_set_bit(ENTRY_DATA_BIT, (unsigned long *)node);
	hyb_unlock_with_idx(index, table);

	return ret;
}

bool hyb_entries_test_priv(int index, struct hyb_entries_table *table)
{
	struct hyb_entries_head *node = index_node(index, table);
	bool ret = false;

	if (!node) {
		hybp(HYB_ERR, "index = %d, table = %pK\n", index, table);
		return false;
	}
	hyb_lock_with_idx(index, table);
	ret = test_bit(ENTRY_DATA_BIT, (unsigned long *)node);
	hyb_unlock_with_idx(index, table);

	return ret;
}

bool hyb_entries_clear_priv(int index, struct hyb_entries_table *table)
{
	struct hyb_entries_head *node = index_node(index, table);
	bool ret = false;

	if (!node) {
		hybp(HYB_ERR, "index = %d, table = %pK\n", index, table);
		return false;
	}

	hyb_lock_with_idx(index, table);
	ret = test_and_clear_bit(ENTRY_DATA_BIT, (unsigned long *)node);
	hyb_unlock_with_idx(index, table);

	return ret;
}

struct mem_cgroup *find_memcg_by_id(unsigned short memcgid)
{
	struct mem_cgroup *mcg = NULL;

	rcu_read_lock();
	mcg = mem_cgroup_from_id(memcgid);
	rcu_read_unlock();

	return mcg;
}

static bool frag_info_dec(bool prev_flag, bool next_flag,
			 struct hybstatus *stat)
{
	if (prev_flag && next_flag) {
		atomic64_inc(&stat->frag_cnt);
		return false;
	}

	if (prev_flag || next_flag)
		return false;

	return true;
}

static bool frag_info_inc(bool prev_flag, bool next_flag,
			 struct hybstatus *stat)
{
	if (prev_flag && next_flag) {
		atomic64_dec(&stat->frag_cnt);
		return false;
	}

	if (prev_flag || next_flag)
		return false;

	return true;
}

static bool pre_is_conted(struct hyb_info *infos, int eswapid, int memcgid)
{
	int prev;

	if (is_first_index(eswap_index(infos, eswapid), memcgindex(infos, memcgid),
			 infos->eswap_table))
		return false;
	prev = prev_index(eswap_index(infos, eswapid), infos->eswap_table);

	return (prev >= 0) && (eswap_index(infos, eswapid) == prev + 1);
}

static bool ne_is_conted(struct hyb_info *infos, int eswapid, int memcgid)
{
	int next;

	if (is_last_index(eswap_index(infos, eswapid), memcgindex(infos, memcgid),
			infos->eswap_table))
		return false;
	next = next_index(eswap_index(infos, eswapid), infos->eswap_table);

	return (next >= 0) && (eswap_index(infos, eswapid) + 1 == next);
}

static void eswap_frag_info_sub(struct hyb_info *infos, int eswapid)
{
	bool prev_flag = false;
	bool next_flag = false;
	int memcgid;
	struct hybstatus *stat = hybridswap_fetch_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}

	if (!infos->eswap_table) {
		hybp(HYB_ERR, "NULL table\n");
		return;
	}
	if (eswapid < 0 || eswapid >= infos->nr_es) {
		hybp(HYB_ERR, "eswap = %d invalid\n", eswapid);
		return;
	}

	memcgid = hyb_entries_fetch_memcgid(eswap_index(infos, eswapid), infos->eswap_table);
	if (memcgid <= 0 || memcgid >= infos->memcg_num) {
		hybp(HYB_ERR, "memcgid = %d invalid\n", memcgid);
		return;
	}

	atomic64_dec(&stat->eswap_cnt);
	infos->memcgid_cnt[memcgid]--;
	if (infos->memcgid_cnt[memcgid] == 0) {
		atomic64_dec(&stat->mcg_cnt);
		atomic64_dec(&stat->frag_cnt);
		return;
	}

	prev_flag = pre_is_conted(infos, eswapid, memcgid);
	next_flag = ne_is_conted(infos, eswapid, memcgid);

	if (frag_info_dec(prev_flag, next_flag, stat))
		atomic64_dec(&stat->frag_cnt);
}

static void eswap_frag_info_add(struct hyb_info *infos, int eswapid)
{
	bool prev_flag = false;
	bool next_flag = false;
	int memcgid;
	struct hybstatus *stat = hybridswap_fetch_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}

	if (!infos->eswap_table) {
		hybp(HYB_ERR, "NULL table\n");
		return;
	}
	if (eswapid < 0 || eswapid >= infos->nr_es) {
		hybp(HYB_ERR, "eswap = %d invalid\n", eswapid);
		return;
	}

	memcgid = hyb_entries_fetch_memcgid(eswap_index(infos, eswapid), infos->eswap_table);
	if (memcgid <= 0 || memcgid >= infos->memcg_num) {
		hybp(HYB_ERR, "memcgid = %d invalid\n", memcgid);
		return;
	}

	atomic64_inc(&stat->eswap_cnt);
	if (infos->memcgid_cnt[memcgid] == 0) {
		infos->memcgid_cnt[memcgid]++;
		atomic64_inc(&stat->frag_cnt);
		atomic64_inc(&stat->mcg_cnt);
		return;
	}
	infos->memcgid_cnt[memcgid]++;

	prev_flag = pre_is_conted(infos, eswapid, memcgid);
	next_flag = ne_is_conted(infos, eswapid, memcgid);

	if (frag_info_inc(prev_flag, next_flag, stat))
		atomic64_inc(&stat->frag_cnt);
}

static int eswap_bit2id(struct hyb_info *infos, int bit)
{
	if (bit < 0 || bit >= infos->nr_es) {
		hybp(HYB_ERR, "bit = %d invalid\n", bit);
		return -EINVAL;
	}

	return infos->nr_es - bit - 1;
}

static int eswap_id2bit(struct hyb_info *infos, int id)
{
	if (id < 0 || id >= infos->nr_es) {
		hybp(HYB_ERR, "id = %d invalid\n", id);
		return -EINVAL;
	}

	return infos->nr_es - id - 1;
}

int obj_index(struct hyb_info *infos, int index)
{
	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return -EINVAL;
	}
	if (index < 0 || index >= infos->total_objects) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return -EINVAL;
	}

	return index;
}

int eswap_index(struct hyb_info *infos, int index)
{
	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return -EINVAL;
	}
	if (index < 0 || index >= infos->nr_es) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return -EINVAL;
	}

	return index + infos->total_objects;
}

int memcgindex(struct hyb_info *infos, int index)
{
	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return -EINVAL;
	}
	if (index <= 0 || index >= infos->memcg_num) {
		hybp(HYB_ERR, "index = %d invalid, memcg_num %d\n", index,
			infos->memcg_num);
		return -EINVAL;
	}

	return index + infos->total_objects + infos->nr_es;
}

static struct hyb_entries_head *fetch_objects_node(int index, void *private)
{
	struct hyb_info *infos = private;

	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return NULL;
	}
	if (index < 0) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return NULL;
	}
	if (index < infos->total_objects)
		return &infos->lru[index];
	index -= infos->total_objects;
	if (index < infos->nr_es)
		return &infos->maps[index];
	index -= infos->nr_es;
	if (index > 0 && index < infos->memcg_num) {
		struct mem_cgroup *mcg = find_memcg_by_id(index);

		if (!mcg)
			goto err_out;
		return (struct hyb_entries_head *)(&MEMCGRP_ITEM(mcg, swap_sorted_list));
	}
err_out:
	hybp(HYB_ERR, "index = %d invalid, mcg is NULL\n", index);

	return NULL;
}

static void free_obj_list_table(struct hyb_info *infos)
{
	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return;
	}

	if (infos->lru) {
		vfree(infos->lru);
		infos->lru = NULL;
	}
	if (infos->maps) {
		vfree(infos->maps);
		infos->maps = NULL;
	}

	kfree(infos->objects);
	infos->objects = NULL;
}

static int init_obj_list_table(struct hyb_info *infos)
{
	int i;

	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return -EINVAL;
	}

	infos->lru = vzalloc(sizeof(struct hyb_entries_head) * infos->total_objects);
	if (!infos->lru) {
		hybp(HYB_ERR, "infos->lru alloc failed\n");
		goto err_out;
	}
	infos->maps = vzalloc(sizeof(struct hyb_entries_head) * infos->nr_es);
	if (!infos->maps) {
		hybp(HYB_ERR, "infos->maps alloc failed\n");
		goto err_out;
	}
	infos->objects = alloc_table(fetch_objects_node, infos, GFP_KERNEL);
	if (!infos->objects) {
		hybp(HYB_ERR, "infos->objects alloc failed\n");
		goto err_out;
	}
	for (i = 0; i < infos->total_objects; i++)
		hyb_entries_init(obj_index(infos, i), infos->objects);
	for (i = 0; i < infos->nr_es; i++)
		hyb_entries_init(eswap_index(infos, i), infos->objects);

	hybp(HYB_INFO, "hybridswap obj list table init OK.\n");
	return 0;
err_out:
	free_obj_list_table(infos);
	hybp(HYB_ERR, "hybridswap obj list table init failed.\n");

	return -ENOMEM;
}

static struct hyb_entries_head *fetch_eswap_table_node(int index, void *private)
{
	struct hyb_info *infos = private;

	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return NULL;
	}

	if (index < infos->total_objects)
		goto err_out;
	index -= infos->total_objects;
	if (index < infos->nr_es)
		return &infos->eswap[index];
	index -= infos->nr_es;
	if (index > 0 && index < infos->memcg_num) {
		struct mem_cgroup *mcg = find_memcg_by_id(index);

		if (!mcg)
			return NULL;
		return (struct hyb_entries_head *)(&MEMCGRP_ITEM(mcg, eswap_lru));
	}
err_out:
	hybp(HYB_ERR, "index = %d invalid\n", index);

	return NULL;
}

static void free_eswap_list_table(struct hyb_info *infos)
{
	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return;
	}

	if (infos->eswap)
		vfree(infos->eswap);

	kfree(infos->eswap_table);
}

static int init_eswap_list_table(struct hyb_info *infos)
{
	int i;

	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return -EINVAL;
	}
	infos->eswap = vzalloc(sizeof(struct hyb_entries_head) * infos->nr_es);
	if (!infos->eswap)
		goto err_out;
	infos->eswap_table = alloc_table(fetch_eswap_table_node, infos, GFP_KERNEL);
	if (!infos->eswap_table)
		goto err_out;
	for (i = 0; i < infos->nr_es; i++)
		hyb_entries_init(eswap_index(infos, i), infos->eswap_table);
	hybp(HYB_INFO, "hybridswap eswap list table init OK.\n");
	return 0;
err_out:
	free_eswap_list_table(infos);
	hybp(HYB_ERR, "hybridswap eswap list table init failed.\n");

	return -ENOMEM;
}

void free_hyb_info(struct hyb_info *infos)
{
	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return;
	}

	vfree(infos->bitmask);
	vfree(infos->eswap_stored_pages);
	free_obj_list_table(infos);
	free_eswap_list_table(infos);
	vfree(infos);
}

struct hyb_info *alloc_hyb_info(unsigned long ori_size,
					    unsigned long comp_size)
{
	struct hyb_info *infos = vzalloc(sizeof(struct hyb_info));

	if (!infos) {
		hybp(HYB_ERR, "infos alloc failed\n");
		goto err_out;
	}
	if (comp_size & (ESWAP_SIZE - 1)) {
		hybp(HYB_ERR, "disksize = %ld align invalid (32K align needed)\n",
			 comp_size);
		goto err_out;
	}
	infos->size = comp_size;
	infos->nr_es = comp_size >> ESWAP_SHIFT;
	infos->memcg_num = MEM_CGROUP_ID_MAX;
	infos->total_objects = ori_size >> PAGE_SHIFT;
	infos->bitmask = vzalloc(BITS_TO_LONGS(infos->nr_es) * sizeof(long));
	if (!infos->bitmask) {
		hybp(HYB_ERR, "infos->bitmask alloc failed, %lu\n",
				BITS_TO_LONGS(infos->nr_es) * sizeof(long));
		goto err_out;
	}
	infos->eswap_stored_pages = vzalloc(sizeof(atomic_t) * infos->nr_es);
	if (!infos->eswap_stored_pages) {
		hybp(HYB_ERR, "infos->eswap_stored_pages alloc failed\n");
		goto err_out;
	}
	if (init_obj_list_table(infos)) {
		hybp(HYB_ERR, "init obj list table failed\n");
		goto err_out;
	}
	if (init_eswap_list_table(infos)) {
		hybp(HYB_ERR, "init eswap list table failed\n");
		goto err_out;
	}
	hybp(HYB_INFO, "infos %p size %lu nr_es %lu memcg_num %lu total_objects %lu\n",
			infos, infos->size, infos->nr_es, infos->memcg_num,
			infos->total_objects);
	hybp(HYB_INFO, "hyb_info init OK.\n");
	return infos;
err_out:
	free_hyb_info(infos);
	hybp(HYB_ERR, "hyb_info init failed.\n");

	return NULL;
}

void hybridswap_check_infos_eswap(struct hyb_info *infos)
{
	int i;

	if (!infos)
		return;

	for (i = 0; i < infos->nr_es; i++) {
		int cnt = atomic_read(&infos->eswap_stored_pages[i]);
		int eswapid = eswap_index(infos, i);
		bool data = hyb_entries_test_priv(eswapid, infos->eswap_table);
		int memcgid = hyb_entries_fetch_memcgid(eswapid, infos->eswap_table);

		if (cnt < 0 || (cnt > 0 && memcgid == 0))
			hybp(HYB_ERR, "%8d %8d %8d %8d %4d\n", i, cnt, eswapid,
				memcgid, data);
	}
}

void hybridswap_free_eswap(struct hyb_info *infos, int eswapid)
{
	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return;
	}
	if (eswapid < 0 || eswapid >= infos->nr_es) {
		hybp(HYB_ERR, "INVALID eswap %d\n", eswapid);
		return;
	}
	hybp(HYB_DEBUG, "free eswap id = %d.\n", eswapid);

	hyb_entries_set_memcgid(eswap_index(infos, eswapid), infos->eswap_table, 0);
	if (!test_and_clear_bit(eswap_id2bit(infos, eswapid), infos->bitmask)) {
		hybp(HYB_ERR, "bit not set, eswap = %d\n", eswapid);
		WARN_ON_ONCE(1);
	}
	atomic_dec(&infos->stored_exts);
}

static int alloc_bitmask(unsigned long *bitmask, int max, int last_bit)
{
	int bit;

	if (!bitmask) {
		hybp(HYB_ERR, "NULL bitmask.\n");
		return -EINVAL;
	}
retry:
	bit = find_next_zero_bit(bitmask, max, last_bit);
	if (bit == max) {
		if (last_bit == 0) {
			hybp(HYB_ERR, "alloc bitmask failed.\n");
			return -ENOSPC;
		}
		last_bit = 0;
		goto retry;
	}
	if (test_and_set_bit(bit, bitmask))
		goto retry;

	return bit;
}

int hybridswap_alloc_eswap(struct hyb_info *infos, struct mem_cgroup *mcg)
{
	int last_bit;
	int bit;
	int eswapid;
	int memcgid;

	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return -EINVAL;
	}
	if (!mcg) {
		hybp(HYB_ERR, "NULL mcg\n");
		return -EINVAL;
	}

	last_bit = atomic_read(&infos->last_alloc_bit);
	hybp(HYB_DEBUG, "last_bit = %d.\n", last_bit);
	bit = alloc_bitmask(infos->bitmask, infos->nr_es, last_bit);
	if (bit < 0) {
		hybp(HYB_ERR, "alloc bitmask failed.\n");
		return bit;
	}
	eswapid = eswap_bit2id(infos, bit);
	memcgid = hyb_entries_fetch_memcgid(eswap_index(infos, eswapid), infos->eswap_table);
	if (memcgid) {
		hybp(HYB_ERR, "already has mcg %d, eswap %d\n",
				memcgid, eswapid);
		goto err_out;
	}
	hyb_entries_set_memcgid(eswap_index(infos, eswapid), infos->eswap_table, mcg->id.id);

	atomic_set(&infos->last_alloc_bit, bit);
	atomic_inc(&infos->stored_exts);
	hybp(HYB_DEBUG, "eswap %d init OK.\n", eswapid);
	hybp(HYB_DEBUG, "memcgid = %d, eswap id = %d\n", mcg->id.id, eswapid);

	return eswapid;
err_out:
	clear_bit(bit, infos->bitmask);
	WARN_ON_ONCE(1);
	return -EBUSY;
}

int fetch_eswap(struct hyb_info *infos, int eswapid)
{
	int memcgid;

	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return -EINVAL;
	}
	if (eswapid < 0 || eswapid >= infos->nr_es) {
		hybp(HYB_ERR, "eswap = %d invalid\n", eswapid);
		return -EINVAL;
	}

	if (!hyb_entries_clear_priv(eswap_index(infos, eswapid), infos->eswap_table))
		return -EBUSY;
	memcgid = hyb_entries_fetch_memcgid(eswap_index(infos, eswapid), infos->eswap_table);
	if (memcgid) {
		eswap_frag_info_sub(infos, eswapid);
		hyb_entries_del(eswap_index(infos, eswapid), memcgindex(infos, memcgid),
			    infos->eswap_table);
	}
	hybp(HYB_DEBUG, "eswap id = %d\n", eswapid);

	return eswapid;
}

void put_eswap(struct hyb_info *infos, int eswapid)
{
	int memcgid;

	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return;
	}
	if (eswapid < 0 || eswapid >= infos->nr_es) {
		hybp(HYB_ERR, "eswap = %d invalid\n", eswapid);
		return;
	}

	memcgid = hyb_entries_fetch_memcgid(eswap_index(infos, eswapid), infos->eswap_table);
	if (memcgid) {
		hyb_lock_with_idx(memcgindex(infos, memcgid), infos->eswap_table);
		hyb_entries_add_nolock(eswap_index(infos, eswapid), memcgindex(infos, memcgid),
			infos->eswap_table);
		eswap_frag_info_add(infos, eswapid);
		hyb_unlock_with_idx(memcgindex(infos, memcgid), infos->eswap_table);
	}
	if (!hyb_entries_set_priv(eswap_index(infos, eswapid), infos->eswap_table)) {
		hybp(HYB_ERR, "private not set, eswap = %d\n", eswapid);
		WARN_ON_ONCE(1);
		return;
	}
	hybp(HYB_DEBUG, "put eswap %d.\n", eswapid);
}

int fetch_memcg_eswap(struct hyb_info *infos, struct mem_cgroup *mcg)
{
	int memcgid;
	int eswapid = -ENOENT;
	int index;

	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return -EINVAL;
	}
	if (!infos->eswap_table) {
		hybp(HYB_ERR, "NULL table\n");
		return -EINVAL;
	}
	if (!mcg) {
		hybp(HYB_ERR, "NULL mcg\n");
		return -EINVAL;
	}

	memcgid = mcg->id.id;
	hyb_lock_with_idx(memcgindex(infos, memcgid), infos->eswap_table);
	hyb_entries_for_each_entry(index, memcgindex(infos, memcgid), infos->eswap_table)
		if (hyb_entries_clear_priv(index, infos->eswap_table)) {
			eswapid = index - infos->total_objects;
			break;
		}
	if (eswapid >= 0 && eswapid < infos->nr_es) {
		eswap_frag_info_sub(infos, eswapid);
		hyb_entries_del_nolock(index, memcgindex(infos, memcgid), infos->eswap_table);
		hybp(HYB_DEBUG, "eswap id = %d\n", eswapid);
	}
	hyb_unlock_with_idx(memcgindex(infos, memcgid), infos->eswap_table);

	return eswapid;
}

int fetch_memcg_zram_entry(struct hyb_info *infos, struct mem_cgroup *mcg)
{
	int memcgid, idx;
	int index = -ENOENT;

	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return -EINVAL;
	}
	if (!infos->objects) {
		hybp(HYB_ERR, "NULL table\n");
		return -EINVAL;
	}
	if (!mcg) {
		hybp(HYB_ERR, "NULL mcg\n");
		return -EINVAL;
	}

	memcgid = mcg->id.id;
	hyb_lock_with_idx(memcgindex(infos, memcgid), infos->objects);
	hyb_entries_for_each_entry(idx, memcgindex(infos, memcgid), infos->objects) {
		index = idx;
		break;
	}
	hyb_unlock_with_idx(memcgindex(infos, memcgid), infos->objects);

	return index;
}

int fetch_eswap_zram_entry(struct hyb_info *infos, int eswapid)
{
	int index = -ENOENT;
	int idx;

	if (!infos) {
		hybp(HYB_ERR, "NULL infos\n");
		return -EINVAL;
	}
	if (!infos->objects) {
		hybp(HYB_ERR, "NULL table\n");
		return -EINVAL;
	}
	if (eswapid < 0 || eswapid >= infos->nr_es) {
		hybp(HYB_ERR, "eswap = %d invalid\n", eswapid);
		return -EINVAL;
	}

	hyb_lock_with_idx(eswap_index(infos, eswapid), infos->objects);
	hyb_entries_for_each_entry(idx, eswap_index(infos, eswapid), infos->objects) {
		index = idx;
		break;
	}
	hyb_unlock_with_idx(eswap_index(infos, eswapid), infos->objects);

	return index;
}

void *hybridswap_malloc(size_t size, bool fast, bool nofail)
{
	void *mem = NULL;

	if (likely(fast)) {
		mem = kzalloc(size, GFP_ATOMIC);
		if (likely(mem || !nofail))
			return mem;
	}

	mem = kzalloc(size, GFP_NOIO);

	return mem;
}

void hybridswap_free(const void *mem)
{
	kfree(mem);
}

struct page *hybridswap_alloc_page_common(void *data, gfp_t gfp)
{
	struct page *page = NULL;
	struct zs_eswap_para *eswap_para = (struct zs_eswap_para *)data;

	if (eswap_para->pool) {
		spin_lock(&eswap_para->pool->page_pool_lock);
		if (!list_empty(&eswap_para->pool->page_pool_list)) {
			page = list_first_entry(
					&eswap_para->pool->page_pool_list,
					struct page, lru);
			list_del(&page->lru);
		}
		spin_unlock(&eswap_para->pool->page_pool_lock);
	}

	if (!page) {
		if (eswap_para->fast) {
			page = alloc_page(GFP_ATOMIC);
			if (likely(page))
				goto out;
		}
		if (eswap_para->nofail)
			page = alloc_page(GFP_NOIO);
		else
			page = alloc_page(gfp);
	}
out:
	return page;
}

unsigned long hybridswap_zsmalloc(struct zs_pool *zs_pool,
		size_t size, struct hybridswap_page_pool *pool)
{
	gfp_t gfp = __GFP_DIRECT_RECLAIM | __GFP_KSWAPD_RECLAIM |
		__GFP_NOWARN | __GFP_HIGHMEM |	__GFP_MOVABLE;
	return zs_malloc(zs_pool, size, gfp);
}

unsigned long zram_zsmalloc(struct zs_pool *zs_pool, size_t size, gfp_t gfp)
{
	return zs_malloc(zs_pool, size, gfp);
}

struct page *hybridswap_alloc_page(struct hybridswap_page_pool *pool,
					gfp_t gfp, bool fast, bool nofail)
{
	struct zs_eswap_para eswap_para;

	eswap_para.pool = pool;
	eswap_para.fast = fast;
	eswap_para.nofail = nofail;

	return hybridswap_alloc_page_common((void *)&eswap_para, gfp);
}

void hybridswap_page_recycle(struct page *page, struct hybridswap_page_pool *pool)
{
	if (pool) {
		spin_lock(&pool->page_pool_lock);
		list_add(&page->lru, &pool->page_pool_list);
		spin_unlock(&pool->page_pool_lock);
	} else {
		__free_page(page);
	}
}

bool hybridswap_out_to_eswap_enable(void)
{
	return !!atomic_read(&global_settings.out_to_eswap_enable);
}

void hybridswap_set_out_to_eswap_disable(void)
{
	atomic_set(&global_settings.out_to_eswap_enable, false);
}

void hybridswap_set_out_to_eswap_enable(bool en)
{
	atomic_set(&global_settings.out_to_eswap_enable, en ? 1 : 0);
}

bool hybridswap_core_enabled(void)
{
	return !!atomic_read(&global_settings.enable);
}

void hybridswap_set_enable(bool en)
{
	hybridswap_set_out_to_eswap_enable(en);

	if (!hybridswap_core_enabled())
		atomic_set(&global_settings.enable, en ? 1 : 0);
}

struct hybstatus *hybridswap_fetch_stat_obj(void)
{
	return global_settings.stat;
}

bool hybridswap_dev_life(void)
{
	return !!atomic_read(&global_settings.dev_life);
}

void hybridswap_set_dev_life(bool en)
{
	atomic_set(&global_settings.dev_life, en ? 1 : 0);
}

unsigned long hybridswap_quota_day(void)
{
	return global_settings.quota_day;
}

void hybridswap_set_quota_day(unsigned long val)
{
	global_settings.quota_day = val;
}

bool hybridswap_reach_life_protect(void)
{
	struct hybstatus *stat = hybridswap_fetch_stat_obj();
	unsigned long quota = hybridswap_quota_day();

	if (hybridswap_dev_life())
		quota /= 10;
	return atomic64_read(&stat->reclaimin_bytes_daily) > quota;
}

static void hybridswap_life_protect_ctrl_work(struct work_struct *work)
{
	struct tm tm;
	struct timespec64 ts;
	struct hybstatus *stat = hybridswap_fetch_stat_obj();

	ktime_get_real_ts64(&ts);
	time64_to_tm(ts.tv_sec - sys_tz.tz_minuteswest * 60, 0, &tm);

	if (tm.tm_hour > 2)
		atomic64_set(&stat->reclaimin_bytes_daily, 0);
}

static void hybridswap_life_protect_ctrl_timer(struct timer_list *t)
{
	schedule_work(&global_settings.lpc_work);
	mod_timer(&global_settings.lpc_timer,
		  jiffies + HYBRIDSWAP_CHECK_GAP * HZ);
}

void hybridswap_close_bdev(struct block_device *bdev, struct file *backing_dev)
{
	if (bdev)
		blkdev_put(bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);

	if (backing_dev)
		filp_close(backing_dev, NULL);
}

struct file *hybridswap_open_bdev(const char *file_name)
{
	struct file *backing_dev = NULL;

	backing_dev = filp_open(file_name, O_RDWR|O_LARGEFILE, 0);
	if (unlikely(IS_ERR(backing_dev))) {
		hybp(HYB_ERR, "open the %s failed! eno = %lld\n",
				file_name, PTR_ERR(backing_dev));
		backing_dev = NULL;
		return NULL;
	}

	if (unlikely(!S_ISBLK(backing_dev->f_mapping->host->i_mode))) {
		hybp(HYB_ERR, "%s isn't a blk device\n", file_name);
		hybridswap_close_bdev(NULL, backing_dev);
		return NULL;
	}

	return backing_dev;
}

int hybridswap_bind(struct zram *zram, const char *file_name)
{
	struct file *backing_dev = NULL;
	struct inode *inode = NULL;
	unsigned long nr_pages;
	struct block_device *bdev = NULL;
	int err;

	backing_dev = hybridswap_open_bdev(file_name);
	if (unlikely(!backing_dev))
		return -EINVAL;

	inode = backing_dev->f_mapping->host;
	bdev = blkdev_get_by_dev(inode->i_rdev,
			FMODE_READ | FMODE_WRITE | FMODE_EXCL, zram);
	if (IS_ERR(bdev)) {
		hybp(HYB_ERR, "%s blkdev_fetch failed!\n", file_name);
		err = PTR_ERR(bdev);
		bdev = NULL;
		goto out;
	}

	nr_pages = (unsigned long)i_size_read(inode) >> PAGE_SHIFT;
	err = set_blocksize(bdev, PAGE_SIZE);
	if (unlikely(err)) {
		hybp(HYB_ERR,
				"%s set blocksize failed! eno = %d\n", file_name, err);
		goto out;
	}

	zram->bdev = bdev;
	zram->backing_dev = backing_dev;
	zram->nr_pages = nr_pages;
	return 0;

out:
	hybridswap_close_bdev(bdev, backing_dev);

	return err;
}

static inline unsigned long fetch_original_used_swap(void)
{
	struct sysinfo val;

	si_swapinfo(&val);

	return val.totalswap - val.freeswap;
}

void hybstatus_init(struct hybstatus *stat)
{
	int i;

	atomic64_set(&stat->reclaimin_cnt, 0);
	atomic64_set(&stat->reclaimin_bytes, 0);
	atomic64_set(&stat->reclaimin_real_load, 0);
	atomic64_set(&stat->dropped_eswap_size, 0);
	atomic64_set(&stat->reclaimin_bytes_daily, 0);
	atomic64_set(&stat->reclaimin_pages, 0);
	atomic64_set(&stat->reclaimin_infight, 0);
	atomic64_set(&stat->batchout_cnt, 0);
	atomic64_set(&stat->batchout_bytes, 0);
	atomic64_set(&stat->batchout_real_load, 0);
	atomic64_set(&stat->batchout_pages, 0);
	atomic64_set(&stat->batchout_inflight, 0);
	atomic64_set(&stat->fault_cnt, 0);
	atomic64_set(&stat->hybridswap_fault_cnt, 0);
	atomic64_set(&stat->reout_pages, 0);
	atomic64_set(&stat->reout_bytes, 0);
	atomic64_set(&stat->zram_stored_pages, 0);
	atomic64_set(&stat->zram_stored_size, 0);
	atomic64_set(&stat->stored_pages, 0);
	atomic64_set(&stat->stored_size, 0);
	atomic64_set(&stat->notify_free, 0);
	atomic64_set(&stat->frag_cnt, 0);
	atomic64_set(&stat->mcg_cnt, 0);
	atomic64_set(&stat->eswap_cnt, 0);
	atomic64_set(&stat->miss_free, 0);
	atomic64_set(&stat->memcgid_clear, 0);
	atomic64_set(&stat->skip_track_cnt, 0);
	atomic64_set(&stat->null_memcg_skip_track_cnt, 0);
	atomic64_set(&stat->used_swap_pages, fetch_original_used_swap());
	atomic64_set(&stat->stored_wm_scale, DEFAULT_STORED_WM_RATIO);

	for (i = 0; i < HYB_CLASS_BUTT; ++i) {
		atomic64_set(&stat->io_fail_cnt[i], 0);
		atomic64_set(&stat->alloc_fail_cnt[i], 0);
		atomic64_set(&stat->lat[i].total_lat, 0);
		atomic64_set(&stat->lat[i].max_lat, 0);
	}

	stat->record.num = 0;
	spin_lock_init(&stat->record.lock);
}

static bool hybridswap_global_setting_init(struct zram *zram)
{
	if (unlikely(global_settings.stat))
		return false;

	global_settings.zram = zram;
	hybridswap_set_enable(false);
	global_settings.stat = hybridswap_malloc(
			sizeof(struct hybstatus), false, true);
	if (unlikely(!global_settings.stat)) {
		hybp(HYB_ERR, "global stat allocation failed!\n");
		return false;
	}

	hybstatus_init(global_settings.stat);
	global_settings.reclaim_wq = alloc_workqueue("hybridswap_reclaim",
			WQ_CPU_INTENSIVE, 0);
	if (unlikely(!global_settings.reclaim_wq)) {
		hybp(HYB_ERR, "reclaim workqueue allocation failed!\n");
		hybridswap_free(global_settings.stat);
		global_settings.stat = NULL;

		return false;
	}

	global_settings.quota_day = HYBRIDSWAP_QUOTA_DAY;
	INIT_WORK(&global_settings.lpc_work, hybridswap_life_protect_ctrl_work);
	global_settings.lpc_timer.expires = jiffies + HYBRIDSWAP_CHECK_GAP * HZ;
	timer_setup(&global_settings.lpc_timer, hybridswap_life_protect_ctrl_timer, 0);
	add_timer(&global_settings.lpc_timer);

	hybp(HYB_DEBUG, "global settings init success\n");
	return true;
}

void hybridswap_global_setting_deinit(void)
{
	destroy_workqueue(global_settings.reclaim_wq);
	hybridswap_free(global_settings.stat);
	global_settings.stat = NULL;
	global_settings.zram = NULL;
	global_settings.reclaim_wq = NULL;
}

struct workqueue_struct *hybridswap_fetch_reclaim_workqueue(void)
{
	return global_settings.reclaim_wq;
}

static int hybridswap_core_init(struct zram *zram)
{
	int ret;

	if (loop_device[0] == '\0') {
		hybp(HYB_ERR, "please setting loop_device first\n");
		return -EINVAL;
	}

	if (!hybridswap_global_setting_init(zram))
		return -EINVAL;

	ret = hybridswap_bind(zram, loop_device);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "bind storage device failed! %d\n", ret);
		hybridswap_global_setting_deinit();
	}

	return 0;
}

int hybridswap_set_enable_init(bool en)
{
	int ret;

	if (hybridswap_core_enabled() || !en)
		return 0;

	if (!global_settings.stat) {
		hybp(HYB_ERR, "global_settings.stat is null!\n");

		return -EINVAL;
	}

	ret = hybridswap_manager_init(global_settings.zram);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "init manager failed! %d\n", ret);

		return -EINVAL;
	}

	ret = hyb_io_work_begin();
	if (unlikely(ret)) {
		hybp(HYB_ERR, "init schedule failed! %d\n", ret);
		hybridswap_manager_deinit(global_settings.zram);

		return -EINVAL;
	}

	return 0;
}

ssize_t hybridswap_core_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "val is error!\n");

		return -EINVAL;
	}

	if (hybridswap_set_enable_init(!!val))
		return -EINVAL;

	hybridswap_set_enable(!!val);

	return len;
}

ssize_t hybridswap_core_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int len = snprintf(buf, PAGE_SIZE, "hybridswap %s out_to_eswap %s\n",
			hybridswap_core_enabled() ? "enable" : "disable",
			hybridswap_out_to_eswap_enable() ? "enable" : "disable");

	return len;
}

ssize_t hybridswap_loop_device_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct zram *zram;
	int ret = 0;

	if (len > (DEVICE_NAME_LEN - 1)) {
		hybp(HYB_ERR, "buf %s len %d is too long\n", buf, len);
		return -EINVAL;
	}

	memcpy(loop_device, buf, len);
	loop_device[len] = '\0';
	strstrip(loop_device);

	zram = dev_to_zram(dev);
	down_write(&zram->init_lock);
	if (zram->disksize == 0) {
		hybp(HYB_ERR, "disksize is 0\n");
		goto out;
	}

	ret = hybridswap_core_init(zram);
	if (ret)
		hybp(HYB_ERR, "hybridswap_core_init init failed\n");

out:
	up_write(&zram->init_lock);
	return len;
}

ssize_t hybridswap_loop_device_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int len = 0;

	len = sprintf(buf, "%s\n", loop_device);

	return len;
}

ssize_t hybridswap_dev_life_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "val is error!\n");

		return -EINVAL;
	}

	hybridswap_set_dev_life(!!val);

	return len;
}

ssize_t hybridswap_dev_life_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int len = 0;

	len = sprintf(buf, "%s\n",
		      hybridswap_dev_life() ? "enable" : "disable");

	return len;
}

ssize_t hybridswap_quota_day_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "val is error!\n");

		return -EINVAL;
	}

	hybridswap_set_quota_day(val);

	return len;
}

ssize_t hybridswap_quota_day_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int len = 0;

	len = sprintf(buf, "%llu\n", hybridswap_quota_day());

	return len;
}

ssize_t hybridswap_zram_increase_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	char *type_buf = NULL;
	unsigned long val;
	struct zram *zram = dev_to_zram(dev);

	type_buf = strstrip((char *)buf);
	if (kstrtoul(type_buf, 0, &val))
		return -EINVAL;

	zram->increase_nr_pages = (val << 8);
	return len;
}

ssize_t hybridswap_zram_increase_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t size = 0;
	struct zram *zram = dev_to_zram(dev);

	size += scnprintf(buf + size, PAGE_SIZE - size,
		"%lu\n", zram->increase_nr_pages >> 8);

	return size;
}

int mem_cgroup_stored_wm_scale_write(
		struct cgroup_subsys_state *css, struct cftype *cft, s64 val)
{
	if (val > MAX_RATIO || val < MIN_RATIO)
		return -EINVAL;

	if (!global_settings.stat)
		return -EINVAL;

	atomic64_set(&global_settings.stat->stored_wm_scale, val);

	return 0;
}

s64 mem_cgroup_stored_wm_scale_read(
		struct cgroup_subsys_state *css, struct cftype *cft)
{
	if (!global_settings.stat)
		return -EINVAL;

	return atomic64_read(&global_settings.stat->stored_wm_scale);
}

int hybridswap_stored_info(unsigned long *total, unsigned long *used)
{
	if (!total || !used)
		return -EINVAL;

	if (!global_settings.stat || !global_settings.zram) {
		*total = 0;
		*used = 0;
		return 0;
	}

	*used = atomic64_read(&global_settings.stat->eswap_cnt) * ESWAP_PG_CNT;
	*total = global_settings.zram->nr_pages;

	return 0;
}

bool hybridswap_stored_wm_ok(void)
{
	unsigned long scale, stored_pages, total_pages, wm_scale;
	int ret;

	if (!hybridswap_core_enabled())
		return false;

	ret = hybridswap_stored_info(&total_pages, &stored_pages);
	if (ret)
		return false;

	scale = (stored_pages * 100) / (total_pages + 1);
	wm_scale = atomic64_read(&global_settings.stat->stored_wm_scale);

	return scale <= wm_scale;
}

int hybridswap_core_enable(void)
{
	int ret;

	ret = hybridswap_set_enable_init(true);
	if (ret) {
		hybp(HYB_ERR, "set true failed, ret=%d\n", ret);
		return ret;
	}

	hybridswap_set_enable(true);
	return 0;
}

void hybridswap_core_disable(void)
{
	(void)hybridswap_set_enable_init(false);
	hybridswap_set_enable(false);
}

static void hybridswap_memcg_iter(
		int (*iter)(struct mem_cgroup *, void *), void *data)
{
	struct mem_cgroup *mcg = fetch_next_memcg(NULL);
	int ret;

	while (mcg) {
		ret = iter(mcg, data);
		hybp(HYB_DEBUG, "%pS mcg %d %s %s, ret %d\n",
					iter, mcg->id.id,
					MEMCGRP_ITEM(mcg, name),
					ret ? "failed" : "pass",
					ret);
		if (ret) {
			fetch_next_memcg_break(mcg);
			return;
		}
		mcg = fetch_next_memcg(mcg);
	}
}

void hybridswap_record(struct zram *zram, u32 index,
				struct mem_cgroup *memcg)
{
	memcg_hybs_t *hybs;
	struct hybstatus *stat;

	if (!hybridswap_core_enabled())
		return;

	if (!memcg || !memcg->id.id) {
		stat = hybridswap_fetch_stat_obj();
		if (stat)
			atomic64_inc(&stat->null_memcg_skip_track_cnt);
		return;
	}

	hybs = MEMCGRP_ITEM_DATA(memcg);
	if (!hybs) {
		hybs = hybridswap_cache_alloc(memcg, false);
		if (!hybs) {
			stat = hybridswap_fetch_stat_obj();
			if (stat)
				atomic64_inc(&stat->skip_track_cnt);
			return;
		}
	}

	if (unlikely(!hybs->zram)) {
		spin_lock(&hybs->zram_init_lock);
		if (!hybs->zram)
			hybridswap_manager_memcg_init(zram, memcg);
		spin_unlock(&hybs->zram_init_lock);
	}

	hybridswap_swap_sorted_list_add(zram, index, memcg);

#ifdef CONFIG_HYBRIDSWAP_SWAPD
	zram_slot_unlock(zram, index);
	if (!zram_watermark_ok())
		wake_all_swapd();
	zram_slot_lock(zram, index);
#endif
}

void hybridswap_untrack(struct zram *zram, u32 index)
{
	if (!hybridswap_core_enabled())
		return;

	while (zram_test_flag(zram, index, ZRAM_UNDER_WB) ||
			zram_test_flag(zram, index, ZRAM_BATCHING_OUT)) {
		zram_slot_unlock(zram, index);
		udelay(50);
		zram_slot_lock(zram, index);
	}

	hybridswap_swap_sorted_list_del(zram, index);
}

static unsigned long memcg_reclaim_size(struct mem_cgroup *memcg)
{
	memcg_hybs_t *hybs = MEMCGRP_ITEM_DATA(memcg);
	unsigned long zram_size, cur_size, new_size;

	if (!hybs)
		return 0;

	zram_size = atomic64_read(&hybs->zram_stored_size);
        if (hybs->force_swapout) {
		hybs->can_eswaped = zram_size;
                return zram_size;
        }

	cur_size = atomic64_read(&hybs->hybridswap_stored_size);
	new_size = (zram_size + cur_size) *
			atomic_read(&hybs->zram2ufs_scale) / 100;

	hybs->can_eswaped = (new_size > cur_size) ? (new_size - cur_size) : 0;
	return hybs->can_eswaped;
}

static int hybridswap_permcg_sz(struct mem_cgroup *memcg, void *data)
{
	unsigned long *out_size = (unsigned long *)data;

	*out_size += memcg_reclaim_size(memcg);
	return 0;
}

static void hybridswap_flush_cb(enum hybridswap_class class,
		void *pri, struct hybridswap_io_req *req)
{
	switch (class) {
	case HYB_FAULT_OUT:
	case HYB_PRE_OUT:
	case HYB_BATCH_OUT:
		hybridswap_eswap_destroy(pri, class);
		break;
	case HYB_RECLAIM_IN:
		hybridswap_eswap_register(pri, req);
		break;
	default:
		break;
	}
}

static void hybridswap_flush_done(struct hybridswap_entry *ioentry,
		int err, struct hybridswap_io_req *req)
{
	struct io_priv *data;

	if (unlikely(!ioentry))
		return;

	data = (struct io_priv *)(ioentry->private);
	if (likely(!err)) {
		hybridswap_flush_cb(data->class,
				ioentry->manager_private, req);
#ifdef CONFIG_HYBRIDSWAP_SWAPD
		if (!zram_watermark_ok())
			wake_all_swapd();
#endif
	} else {
		hybridswap_eswap_exception(data->class,
				ioentry->manager_private);
	}
	hybridswap_free(ioentry);
}

static void hybridswap_free_pagepool(struct io_work_arg *iowork)
{
	struct page *free_page = NULL;

	spin_lock(&iowork->data.page_pool.page_pool_lock);
	while (!list_empty(&iowork->data.page_pool.page_pool_list)) {
		free_page = list_first_entry(
				&iowork->data.page_pool.page_pool_list,
				struct page, lru);
		list_del_init(&free_page->lru);
		__free_page(free_page);
	}
	spin_unlock(&iowork->data.page_pool.page_pool_lock);
}

static void hybridswap_plug_complete(void *data)
{
	struct io_work_arg *iowork  = (struct io_work_arg *)data;

	hybridswap_free_pagepool(iowork);

	hybperf_end(&iowork->record);

	hybridswap_free(iowork);
}

static void *hybridswap_init_plug(struct zram *zram,
		enum hybridswap_class class,
		struct io_work_arg *iowork)
{
	struct hybridswap_io io_para;

	io_para.bdev = zram->bdev;
	io_para.class = class;
	io_para.private = (void *)iowork;
	io_para.record = &iowork->record;
	INIT_LIST_HEAD(&iowork->data.page_pool.page_pool_list);
	spin_lock_init(&iowork->data.page_pool.page_pool_lock);
	io_para.done_callback = hybridswap_flush_done;
	switch (io_para.class) {
	case HYB_RECLAIM_IN:
		io_para.complete_notify = hybridswap_plug_complete;
		iowork->io_buf.pool = NULL;
		break;
	case HYB_BATCH_OUT:
	case HYB_PRE_OUT:
		io_para.complete_notify = hybridswap_plug_complete;
		iowork->io_buf.pool = &iowork->data.page_pool;
		break;
	case HYB_FAULT_OUT:
		io_para.complete_notify = NULL;
		iowork->io_buf.pool = NULL;
		break;
	default:
		break;
	}
	iowork->io_buf.zram = zram;
	iowork->data.zram = zram;
	iowork->data.class = io_para.class;
	return hybridswap_plug_start(&io_para);
}

static void hybridswap_fill_entry(struct hybridswap_entry *ioentry,
		struct hybridswap_buffer *io_buf,
		void *private)
{
	ioentry->addr = ioentry->eswapid * ESWAP_SECTOR_SIZE;
	ioentry->dest_pages = io_buf->dest_pages;
	ioentry->pages_sz = ESWAP_PG_CNT;
	ioentry->private = private;
}

static int hybridswap_reclaim_check(struct mem_cgroup *memcg,
					unsigned long *require_size)
{
	memcg_hybs_t *hybs = MEMCGRP_ITEM_DATA(memcg);

	if (unlikely(!hybs) || unlikely(!hybs->zram))
		return -EAGAIN;
	if (unlikely(hybs->in_swapin))
		return -EAGAIN;
	if (!hybs->force_swapout && *require_size < MIN_RECLAIM_ZRAM_SZ)
		return -EAGAIN;

	return 0;
}

static int hybridswap_update_reclaim_sz(unsigned long *require_size,
		unsigned long *mcg_reclaimed_sz,
		unsigned long reclaim_size)
{
	*mcg_reclaimed_sz += reclaim_size;

	if (*require_size > reclaim_size)
		*require_size -= reclaim_size;
	else
		*require_size = 0;

	return 0;
}

static void hybstatus_alloc_fail(enum hybridswap_class class,
		int err)
{
	struct hybstatus *stat = hybridswap_fetch_stat_obj();

	if (!stat || (err != -ENOMEM) || (class >= HYB_CLASS_BUTT))
		return;

	atomic64_inc(&stat->alloc_fail_cnt[class]);
}

static int hybridswap_reclaim_eswap(struct mem_cgroup *memcg,
		struct io_work_arg *iowork,
		unsigned long *require_size,
		unsigned long *mcg_reclaimed_sz,
		int *errio)
{
	int ret;
	unsigned long reclaim_size;

	hybperfiowrkstart(&iowork->record, HYB_IOENTRY_ALLOC);
	iowork->ioentry = hybridswap_malloc(
			sizeof(struct hybridswap_entry), false, true);
	hybperfiowrkend(&iowork->record, HYB_IOENTRY_ALLOC);
	if (unlikely(!iowork->ioentry)) {
		hybp(HYB_ERR, "alloc io entry failed!\n");
		*require_size = 0;
		*errio = -ENOMEM;
		hybstatus_alloc_fail(HYB_RECLAIM_IN, -ENOMEM);

		return *errio;
	}

	hybperfiowrkstart(&iowork->record, HYB_FIND_ESWAP);
	reclaim_size = hybridswap_eswap_create(
			memcg, &iowork->ioentry->eswapid,
			&iowork->io_buf, &iowork->ioentry->manager_private);
	hybperfiowrkend(&iowork->record, HYB_FIND_ESWAP);
	if (unlikely(!reclaim_size)) {
		if (iowork->ioentry->eswapid != -ENOENT)
			*require_size = 0;
		hybridswap_free(iowork->ioentry);
		return -EAGAIN;
	}

	hybridswap_fill_entry(iowork->ioentry, &iowork->io_buf,
			(void *)(&iowork->data));

	hybperfiowrkstart(&iowork->record, HYB_IO_ESWAP);
	ret = hybridswap_write_eswap(iowork->iohandle, iowork->ioentry);
	hybperfiowrkend(&iowork->record, HYB_IO_ESWAP);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "hybridswap write failed! %d\n", ret);
		*require_size = 0;
		*errio = ret;
		hybstatus_alloc_fail(HYB_RECLAIM_IN, ret);

		return *errio;
	}

	ret = hybridswap_update_reclaim_sz(require_size, mcg_reclaimed_sz,
				reclaim_size);
	if (MEMCGRP_ITEM(memcg, force_swapout))
		  return 0;
	return ret;
}

static int hybridswap_permcg_reclaim(struct mem_cgroup *memcg,
		unsigned long require_size, unsigned long *mcg_reclaimed_sz)
{
	int ret, extcnt;
	int errio = 0;
	unsigned long require_size_before = 0;
	struct io_work_arg *iowork = NULL;
	ktime_t start = ktime_get();
	unsigned long long start_ravg_sum = hybridswap_fetch_ravg_sum();
	memcg_hybs_t *hybs = MEMCGRP_ITEM_DATA(memcg);

	ret = hybridswap_reclaim_check(memcg, &require_size);
	if (ret)
		return ret == -EAGAIN ? 0 : ret;

	iowork = hybridswap_malloc(sizeof(struct io_work_arg), false, true);
	if (unlikely(!iowork)) {
		hybp(HYB_ERR, "alloc iowork failed!\n");
		hybstatus_alloc_fail(HYB_RECLAIM_IN, -ENOMEM);

		return -ENOMEM;
	}

	hybperf_start(&iowork->record, start, start_ravg_sum,
			HYB_RECLAIM_IN);
	hybperfiowrkstart(&iowork->record, HYB_INIT);
	iowork->iohandle = hybridswap_init_plug(hybs->zram,
			HYB_RECLAIM_IN, iowork);
	hybperfiowrkend(&iowork->record, HYB_INIT);
	if (unlikely(!iowork->iohandle)) {
		hybp(HYB_ERR, "plug start failed!\n");
		hybperf_end(&iowork->record);
		hybridswap_free(iowork);
		hybstatus_alloc_fail(HYB_RECLAIM_IN, -ENOMEM);
		ret = -EIO;
		goto out;
	}

	require_size_before = require_size;
	while (require_size) {
		if (hybridswap_reclaim_eswap(memcg, iowork,
					&require_size, mcg_reclaimed_sz, &errio))
			break;

		atomic64_inc(&hybs->hybridswap_outextcnt);
		extcnt = atomic_inc_return(&hybs->hybridswap_extcnt);
		if (extcnt > atomic_read(&hybs->hybridswap_peakextcnt))
			atomic_set(&hybs->hybridswap_peakextcnt, extcnt);
	}

	ret = hybridswap_plug_finish(iowork->iohandle);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "hybridswap write flush failed! %d\n", ret);
		hybstatus_alloc_fail(HYB_RECLAIM_IN, ret);
		require_size = 0;
	} else {
		ret = errio;
	}
	atomic64_inc(&hybs->hybridswap_outcnt);

out:
	hybp(HYB_INFO, "memcg %s %lu %lu out_to_eswap %lu KB eswap %lu zram %lu %d\n",
		hybs->name, require_size_before, require_size,
		(require_size_before - require_size) >> 10,
		atomic64_read(&hybs->hybridswap_stored_size),
		atomic64_read(&hybs->zram_stored_size), ret);
	return ret;
}

static void hybridswap_reclaimin_inc(void)
{
	struct hybstatus *stat;

	stat = hybridswap_fetch_stat_obj();
	if (unlikely(!stat))
		return;
	atomic64_inc(&stat->reclaimin_infight);
}

static void hybridswap_reclaimin_dec(void)
{
	struct hybstatus *stat;

	stat = hybridswap_fetch_stat_obj();
	if (unlikely(!stat))
		return;
	atomic64_dec(&stat->reclaimin_infight);
}

static int hybridswap_permcg_reclaimin(struct mem_cgroup *memcg,
					void *data)
{
	struct async_req *rq = (struct async_req *)data;
	unsigned long mcg_reclaimed_size = 0, require_size;
	int ret;
	memcg_hybs_t *hybs;

	hybs = MEMCGRP_ITEM_DATA(memcg);
	if (!hybs)
		return 0;

	require_size = hybs->can_eswaped * rq->size / rq->out_size;
	if (require_size < MIN_RECLAIM_ZRAM_SZ)
		return 0;

	if (!mutex_trylock(&hybs->swap_lock))
		return 0;

	ret = hybridswap_permcg_reclaim(memcg, require_size,
						&mcg_reclaimed_size);
	rq->reclaimined_sz += mcg_reclaimed_size;
	mutex_unlock(&hybs->swap_lock);

	hybp(HYB_INFO, "memcg %s mcg_reclaimed_size %lu rq->reclaimined_sz %lu rq->size %lu rq->out_size %lu ret %d\n",
		hybs->name, mcg_reclaimed_size, rq->reclaimined_sz,
		rq->size, rq->out_size, ret);

	if (!ret && rq->reclaimined_sz >= rq->size)
		return -EINVAL;

	return ret;
}

static void hybridswap_reclaim_work(struct work_struct *work)
{
	struct async_req *rq = container_of(work, struct async_req, work);
	int old_nice = task_nice(current);

	set_user_nice(current, rq->nice);
	hybridswap_reclaimin_inc();
	hybridswap_memcg_iter(hybridswap_permcg_reclaimin, rq);
	hybridswap_reclaimin_dec();
	set_user_nice(current, old_nice);
	hybp(HYB_INFO, "SWAPOUT want %lu MB real %lu Mb\n", rq->size >> 20,
		rq->reclaimined_sz >> 20);
	hybridswap_free(rq);
}

unsigned long hybridswap_out_to_eswap(unsigned long size)
{
	struct async_req *rq = NULL;
	unsigned long out_size = 0;

	if (!hybridswap_core_enabled() || !hybridswap_out_to_eswap_enable()
	    || hybridswap_reach_life_protect() || !size)
		return 0;

	hybridswap_memcg_iter(hybridswap_permcg_sz, &out_size);
	if (!out_size)
		return 0;

	rq = hybridswap_malloc(sizeof(struct async_req), false, true);
	if (unlikely(!rq)) {
		hybp(HYB_ERR, "alloc async req fail!\n");
		hybstatus_alloc_fail(HYB_RECLAIM_IN, -ENOMEM);
		return 0;
	}

	if (out_size < size)
		size = out_size;
	rq->size = size;
	rq->out_size = out_size;
	rq->reclaimined_sz = 0;
	rq->nice = task_nice(current);
	INIT_WORK(&rq->work, hybridswap_reclaim_work);
	queue_work(hybridswap_fetch_reclaim_workqueue(), &rq->work);

	return out_size > size ? size : out_size;
}

static int hybridswap_batches_eswap(struct io_work_arg *iowork,
		struct mem_cgroup *mcg,
		bool preload,
		int *errio)
{
	int ret;

	hybperfiowrkstart(&iowork->record, HYB_IOENTRY_ALLOC);
	iowork->ioentry = hybridswap_malloc(
			sizeof(struct hybridswap_entry), !preload, preload);
	hybperfiowrkend(&iowork->record, HYB_IOENTRY_ALLOC);
	if (unlikely(!iowork->ioentry)) {
		hybp(HYB_ERR, "alloc io entry failed!\n");
		*errio = -ENOMEM;
		hybstatus_alloc_fail(HYB_BATCH_OUT, -ENOMEM);

		return *errio;
	}

	hybperfiowrkstart(&iowork->record, HYB_FIND_ESWAP);
	iowork->ioentry->eswapid = hybridswap_find_eswap_by_memcg(
			mcg, &iowork->io_buf,
			&iowork->ioentry->manager_private);
	hybperfiowrkend(&iowork->record, HYB_FIND_ESWAP);
	if (iowork->ioentry->eswapid < 0) {
		hybstatus_alloc_fail(HYB_BATCH_OUT,
				iowork->ioentry->eswapid);
		hybridswap_free(iowork->ioentry);
		return -EAGAIN;
	}

	hybridswap_fill_entry(iowork->ioentry, &iowork->io_buf,
			(void *)(&iowork->data));

	hybperfiowrkstart(&iowork->record, HYB_IO_ESWAP);
	ret = hybridswap_read_eswap(iowork->iohandle, iowork->ioentry);
	hybperfiowrkend(&iowork->record, HYB_IO_ESWAP);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "hybridswap read failed! %d\n", ret);
		hybstatus_alloc_fail(HYB_BATCH_OUT, ret);
		*errio = ret;

		return *errio;
	}

	return 0;
}

static int hybridswap_do_batches_init(struct io_work_arg **out_sched,
		struct mem_cgroup *mcg, bool preload)
{
	struct io_work_arg *iowork = NULL;
	ktime_t start = ktime_get();
	unsigned long long start_ravg_sum = hybridswap_fetch_ravg_sum();

	iowork = hybridswap_malloc(sizeof(struct io_work_arg),
			!preload, preload);
	if (unlikely(!iowork)) {
		hybp(HYB_ERR, "alloc iowork failed!\n");
		hybstatus_alloc_fail(HYB_BATCH_OUT, -ENOMEM);

		return -ENOMEM;
	}

	hybperf_start(&iowork->record, start, start_ravg_sum,
			preload ? HYB_PRE_OUT : HYB_BATCH_OUT);

	hybperfiowrkstart(&iowork->record, HYB_INIT);
	iowork->iohandle = hybridswap_init_plug(MEMCGRP_ITEM(mcg, zram),
			preload ? HYB_PRE_OUT : HYB_BATCH_OUT,
			iowork);
	hybperfiowrkend(&iowork->record, HYB_INIT);
	if (unlikely(!iowork->iohandle)) {
		hybp(HYB_ERR, "plug start failed!\n");
		hybperf_end(&iowork->record);
		hybridswap_free(iowork);
		hybstatus_alloc_fail(HYB_BATCH_OUT, -ENOMEM);

		return -EIO;
	}

	*out_sched = iowork;

	return 0;
}

static int hybridswap_do_batches(struct mem_cgroup *mcg,
		unsigned long size, bool preload)
{
	int ret = 0;
	int errio = 0;
	struct io_work_arg *iowork = NULL;

	if (unlikely(!mcg || !MEMCGRP_ITEM(mcg, zram))) {
		hybp(HYB_WARN, "no zram in mcg!\n");
		ret = -EINVAL;
		goto out;
	}

	ret = hybridswap_do_batches_init(&iowork, mcg, preload);
	if (unlikely(ret))
		goto out;

	MEMCGRP_ITEM(mcg, in_swapin) = true;
	while (size) {
		if (hybridswap_batches_eswap(iowork, mcg, preload, &errio))
			break;
		size -= ESWAP_SIZE;
	}

	ret = hybridswap_plug_finish(iowork->iohandle);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "hybridswap read flush failed! %d\n", ret);
		hybstatus_alloc_fail(HYB_BATCH_OUT, ret);
	} else {
		ret = errio;
	}

	if (atomic64_read(&MEMCGRP_ITEM(mcg, hybridswap_stored_size)) &&
			hybridswap_loglevel() >= HYB_INFO)
		hybridswap_check_infos_eswap((MEMCGRP_ITEM(mcg, zram)->infos));

	atomic64_inc(&MEMCGRP_ITEM(mcg, hybridswap_incnt));
	MEMCGRP_ITEM(mcg, in_swapin) = false;
out:
	return ret;
}

static void hybridswap_batchout_inc(void)
{
	struct hybstatus *stat;

	stat = hybridswap_fetch_stat_obj();
	if (unlikely(!stat))
		return;
	atomic64_inc(&stat->batchout_inflight);
}

static void hybridswap_batchout_dec(void)
{
	struct hybstatus *stat;

	stat = hybridswap_fetch_stat_obj();
	if (unlikely(!stat))
		return;
	atomic64_dec(&stat->batchout_inflight);
}

int hybridswap_batches(struct mem_cgroup *mcg,
		unsigned long size, bool preload)
{
	int ret;

	if (!hybridswap_core_enabled())
		return 0;

	hybridswap_batchout_inc();
	ret = hybridswap_do_batches(mcg, size, preload);
	hybridswap_batchout_dec();

	return ret;
}

static void hybridswap_fault_stat(struct zram *zram, u32 index)
{
	struct mem_cgroup *mcg = NULL;
	struct hybstatus *stat = hybridswap_fetch_stat_obj();

	if (unlikely(!stat))
		return;

	atomic64_inc(&stat->fault_cnt);

	mcg = hybridswap_zram_fetch_mcg(zram, index);
	if (mcg)
		atomic64_inc(&MEMCGRP_ITEM(mcg, hybridswap_allfaultcnt));
}

static void hybridswap_fault2_stat(struct zram *zram, u32 index)
{
	struct mem_cgroup *mcg = NULL;
	struct hybstatus *stat = hybridswap_fetch_stat_obj();

	if (unlikely(!stat))
		return;

	atomic64_inc(&stat->hybridswap_fault_cnt);

	mcg = hybridswap_zram_fetch_mcg(zram, index);
	if (mcg)
		atomic64_inc(&MEMCGRP_ITEM(mcg, hybridswap_faultcnt));
}

static bool hybridswap_page_fault_check(struct zram *zram,
		u32 index, unsigned long *zentry)
{
	if (!hybridswap_core_enabled())
		return false;

	hybridswap_fault_stat(zram, index);

	if (!zram_test_flag(zram, index, ZRAM_WB))
		return false;

	zram_set_flag(zram, index, ZRAM_BATCHING_OUT);
	*zentry = zram_get_handle(zram, index);
	zram_slot_unlock(zram, index);
	return true;
}

static int hybridswap_page_fault_fetch_eswap(struct zram *zram,
		struct io_work_arg *iowork,
		unsigned long zentry,
		u32 index)
{
	int wait_cycle = 0;

	iowork->io_buf.zram = zram;
	iowork->data.zram = zram;
	iowork->io_buf.pool = NULL;
	hybperfiowrkstart(&iowork->record, HYB_FIND_ESWAP);
	iowork->ioentry->eswapid = hybridswap_find_eswap_by_index(zentry,
			&iowork->io_buf, &iowork->ioentry->manager_private);
	hybperfiowrkend(&iowork->record, HYB_FIND_ESWAP);
	if (unlikely(iowork->ioentry->eswapid == -EBUSY)) {
		while (1) {
			zram_slot_lock(zram, index);
			if (!zram_test_flag(zram, index, ZRAM_WB)) {
				zram_slot_unlock(zram, index);
				hybridswap_free(iowork->ioentry);
#ifdef CONFIG_HYBRIDSWAP_SWAPD
				if (wait_cycle >= 1000)
					atomic_long_dec(&page_fault_pause);
#endif
				return -EAGAIN;
			}
			zram_slot_unlock(zram, index);

			hybperfiowrkstart(&iowork->record,
					HYB_FIND_ESWAP);
			iowork->ioentry->eswapid =
				hybridswap_find_eswap_by_index(zentry,
						&iowork->io_buf,
						&iowork->ioentry->manager_private);
			hybperfiowrkend(&iowork->record,
					HYB_FIND_ESWAP);
			if (likely(iowork->ioentry->eswapid != -EBUSY))
				break;

			if (wait_cycle < 100)
				udelay(50);
			else
				usleep_range(50, 100);
			wait_cycle++;
#ifdef CONFIG_HYBRIDSWAP_SWAPD
			if (wait_cycle == 1000) {
				atomic_long_inc(&page_fault_pause);
				atomic_long_inc(&page_fault_pause_cnt);
			}
#endif
		}
	}
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	if (wait_cycle >= 1000)
		atomic_long_dec(&page_fault_pause);
#endif
	if (iowork->ioentry->eswapid < 0) {
		hybstatus_alloc_fail(HYB_FAULT_OUT,
				iowork->ioentry->eswapid);

		return iowork->ioentry->eswapid;
	}
	hybridswap_fault2_stat(zram, index);
	hybridswap_fill_entry(iowork->ioentry, &iowork->io_buf,
			(void *)(&iowork->data));
	return 0;
}

static int hybridswap_page_fault_exit_check(struct zram *zram,
		u32 index, int ret)
{
	zram_slot_lock(zram, index);
	if (likely(!ret)) {
		if (unlikely(zram_test_flag(zram, index, ZRAM_WB))) {
			hybp(HYB_ERR, "still in WB status!\n");
			ret = -EIO;
		}
	}
	zram_clear_flag(zram, index, ZRAM_BATCHING_OUT);

	return ret;
}

static int hybridswap_page_fault_eswap(struct zram *zram, u32 index,
		struct io_work_arg *iowork, unsigned long zentry)
{
	int ret;

	hybperfiowrkstart(&iowork->record, HYB_IOENTRY_ALLOC);
	iowork->ioentry = hybridswap_malloc(sizeof(struct hybridswap_entry),
			true, true);
	hybperfiowrkend(&iowork->record, HYB_IOENTRY_ALLOC);
	if (unlikely(!iowork->ioentry)) {
		hybp(HYB_ERR, "alloc io entry failed!\n");
		hybstatus_alloc_fail(HYB_FAULT_OUT, -ENOMEM);
		hybridswap_fail_record(HYB_FAULT_OUT_ENTRY_ALLOC_FAIL,
				index, 0, iowork->record.task_comm);
		return -ENOMEM;
	}

	ret = hybridswap_page_fault_fetch_eswap(zram, iowork, zentry, index);
	if (ret)
		return ret;

	hybperfiowrkstart(&iowork->record, HYB_IO_ESWAP);
	ret = hybridswap_read_eswap(iowork->iohandle, iowork->ioentry);
	hybperfiowrkend(&iowork->record, HYB_IO_ESWAP);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "hybridswap read failed! %d\n", ret);
		hybstatus_alloc_fail(HYB_FAULT_OUT, ret);
	}

	return ret;
}

int hybridswap_page_fault(struct zram *zram, u32 index)
{
	int ret = 0;
	int errio;
	struct io_work_arg iowork;
	unsigned long zentry;
	ktime_t start = ktime_get();
	unsigned long long start_ravg_sum = hybridswap_fetch_ravg_sum();

	if (!hybridswap_page_fault_check(zram, index, &zentry))
		return ret;

	memset(&iowork.record, 0, sizeof(struct hybridswap_key_point_record));
	hybperf_start(&iowork.record, start, start_ravg_sum,
			HYB_FAULT_OUT);

	hybperfiowrkstart(&iowork.record, HYB_INIT);
	iowork.iohandle = hybridswap_init_plug(zram,
			HYB_FAULT_OUT, &iowork);
	hybperfiowrkend(&iowork.record, HYB_INIT);
	if (unlikely(!iowork.iohandle)) {
		hybp(HYB_ERR, "plug start failed!\n");
		hybstatus_alloc_fail(HYB_FAULT_OUT, -ENOMEM);
		ret = -EIO;
		hybridswap_fail_record(HYB_FAULT_OUT_INIT_FAIL,
				index, 0, iowork.record.task_comm);

		goto out;
	}

	errio = hybridswap_page_fault_eswap(zram, index, &iowork, zentry);
	ret = hybridswap_plug_finish(iowork.iohandle);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "hybridswap flush failed! %d\n", ret);
		hybstatus_alloc_fail(HYB_FAULT_OUT, ret);
	} else {
		ret = (errio != -EAGAIN) ? errio : 0;
	}
out:
	hybperfiowrkstart(&iowork.record, HYB_ZRAM_LOCK);
	ret = hybridswap_page_fault_exit_check(zram, index, ret);
	hybperfiowrkend(&iowork.record, HYB_ZRAM_LOCK);
	hybperf_end(&iowork.record);

	return ret;
}

bool hybridswap_delete(struct zram *zram, u32 index)
{
	if (!hybridswap_core_enabled())
		return true;

	if (zram_test_flag(zram, index, ZRAM_UNDER_WB)
			|| zram_test_flag(zram, index, ZRAM_BATCHING_OUT)) {
		struct hybstatus *stat = hybridswap_fetch_stat_obj();

		if (stat)
			atomic64_inc(&stat->miss_free);
		return false;
	}

	if (!zram_test_flag(zram, index, ZRAM_WB))
		return true;

	hybridswap_eswap_objs_del(zram, index);

	return true;
}

void hybridswap_mem_cgroup_deinit(struct mem_cgroup *memcg)
{
	if (!hybridswap_core_enabled())
		return;

	hybridswap_manager_memcg_deinit(memcg);
}

void hybridswap_force_reclaim(struct mem_cgroup *mcg)
{
	unsigned long mcg_reclaimed_size = 0, require_size;
	memcg_hybs_t *hybs;

	if (!hybridswap_core_enabled() || !hybridswap_out_to_eswap_enable()
	    || hybridswap_reach_life_protect())
		return;

	if (!mcg)
		return;

	hybs = MEMCGRP_ITEM_DATA(mcg);
	if (!hybs || !hybs->zram)
		return;

	mutex_lock(&hybs->swap_lock);
	require_size = atomic64_read(&hybs->zram_stored_size);
	hybs->force_swapout = true;
	hybridswap_permcg_reclaim(mcg, require_size, &mcg_reclaimed_size);
	hybs->force_swapout = false;
	mutex_unlock(&hybs->swap_lock);
}

void mem_cgroup_id_remove_hook(void *data, struct mem_cgroup *memcg)
{
	if (!memcg->android_oem_data1)
		return;

	hybridswap_mem_cgroup_deinit(memcg);
	hybp(HYB_DEBUG, "hybridswap remove mcg id = %d\n", memcg->id.id);
}
