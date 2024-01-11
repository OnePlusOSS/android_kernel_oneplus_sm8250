// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[HYBRIDSWAP]" fmt

#include <uapi/linux/sched/types.h>
#include <linux/sched.h>
#include <linux/memory.h>
#include <linux/freezer.h>
#include <linux/swap.h>
#include <linux/cgroup-defs.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/cpuhotplug.h>
#include <linux/cpumask.h>
#include <linux/file.h>
#include <linux/eventfd.h>
#if IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
#include <linux/msm_drm_notify.h>
#endif

#include "../zram_drv.h"
#include "../zram_drv_internal.h"
#include "hybridswap_internal.h"

enum swapd_pressure_level {
	LEVEL_LOW = 0,
	LEVEL_MEDIUM,
	LEVEL_CRITICAL,
	LEVEL_COUNT
};

struct swapd_param {
	unsigned int min_score;
	unsigned int max_score;
	unsigned int ub_mem2zram_ratio;
	unsigned int ub_zram2ufs_ratio;
	unsigned int refault_threshold;
};

struct hybridswapd_task {
	wait_queue_head_t swapd_wait;
	atomic_t swapd_wait_flag;
	struct task_struct *swapd;
	struct cpumask swapd_bind_cpumask;
};
#define PGDAT_ITEM_DATA(pgdat) ((struct hybridswapd_task*)(pgdat)->android_oem_data1)
#define PGDAT_ITEM(pgdat, item) (PGDAT_ITEM_DATA(pgdat)->item)

#define AREA_ANON_REFAULT_THRESHOLD 22000
#define ANON_REFAULT_SNAPSHOT_MIN_INTERVAL 200
#define EMPTY_ROUND_SKIP_INTERNVAL 20
#define MAX_SKIP_INTERVAL 1000
#define EMPTY_ROUND_CHECK_THRESHOLD 10
#define ZRAM_WM_RATIO 75
#define COMPRESS_RATIO 30
#define SWAPD_MAX_LEVEL_NUM 10
#define SWAPD_DEFAULT_BIND_CPUS "0-3"
#define MAX_RECLAIMIN_SZ (200llu << 20)
#define page_to_kb(nr) (nr << (PAGE_SHIFT - 10))
#define SWAPD_SHRINK_WINDOW (HZ * 10)
#define SWAPD_SHRINK_SIZE_PER_WINDOW 1024
#define PAGES_TO_MB(pages) ((pages) >> 8)
#define PAGES_PER_1MB (1 << 8)

unsigned long long global_anon_refault_ratio;
unsigned long long swapd_skip_interval;
bool last_round_is_empty;
unsigned long last_swapd_time;
struct eventfd_ctx *swapd_press_efd[LEVEL_COUNT];
atomic64_t zram_wm_ratio = ATOMIC_LONG_INIT(ZRAM_WM_RATIO);
atomic64_t compress_ratio = ATOMIC_LONG_INIT(COMPRESS_RATIO);
atomic_t avail_buffers = ATOMIC_INIT(0);
atomic_t min_avail_buffers = ATOMIC_INIT(0);
atomic_t high_avail_buffers = ATOMIC_INIT(0);
atomic_t max_reclaim_size = ATOMIC_INIT(100);
atomic64_t free_swap_threshold = ATOMIC64_INIT(0);
atomic64_t zram_crit_thres = ATOMIC_LONG_INIT(0);
atomic64_t cpuload_threshold = ATOMIC_LONG_INIT(0);
atomic64_t area_anon_refault_threshold = ATOMIC_LONG_INIT(AREA_ANON_REFAULT_THRESHOLD);
atomic64_t anon_refault_snapshot_min_interval = ATOMIC_LONG_INIT(ANON_REFAULT_SNAPSHOT_MIN_INTERVAL);
atomic64_t empty_round_skip_interval = ATOMIC_LONG_INIT(EMPTY_ROUND_SKIP_INTERNVAL);
atomic64_t max_skip_interval = ATOMIC_LONG_INIT(MAX_SKIP_INTERVAL);
atomic64_t empty_round_check_threshold = ATOMIC_LONG_INIT(EMPTY_ROUND_CHECK_THRESHOLD);
static unsigned long reclaim_exceed_sleep_ms = 50;
static unsigned long all_totalreserve_pages;

static wait_queue_head_t snapshotd_wait;
static atomic_t snapshotd_wait_flag;
static atomic_t snapshotd_init_flag = ATOMIC_LONG_INIT(0);
static struct task_struct *snapshotd_task;
static DEFINE_MUTEX(pressure_event_lock);
static pid_t swapd_pid = -1;
static unsigned long long area_last_anon_pagefault;
static unsigned long last_anon_snapshot_time;
static struct swapd_param zswap_param[SWAPD_MAX_LEVEL_NUM];
static enum cpuhp_state swapd_online;
static struct zram *swapd_zram = NULL;
static u64 max_reclaimin_size = MAX_RECLAIMIN_SZ;
atomic_long_t fault_out_pause = ATOMIC_LONG_INIT(0);
atomic_long_t fault_out_pause_cnt = ATOMIC_LONG_INIT(0);
#if IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
static struct notifier_block fb_notif;
static atomic_t display_off = ATOMIC_LONG_INIT(0);
#endif
static unsigned long swapd_shrink_window = SWAPD_SHRINK_WINDOW;
static unsigned long swapd_shrink_limit_per_window = SWAPD_SHRINK_SIZE_PER_WINDOW;
static unsigned long swapd_last_window_start;
static unsigned long swapd_last_window_shrink;
static atomic_t swapd_pause = ATOMIC_INIT(0);
static atomic_t swapd_enabled = ATOMIC_INIT(0);
static unsigned long swapd_nap_jiffies = 1;

extern unsigned long try_to_free_mem_cgroup_pages(struct mem_cgroup *memcg,
		unsigned long nr_pages,
		gfp_t gfp_mask,
		bool may_swap);
#ifdef CONFIG_OPLUS_JANK
extern u32 get_cpu_load(u32 win_cnt, struct cpumask *mask);
#endif

inline u64 get_zram_wm_ratio_value(void)
{
	return atomic64_read(&zram_wm_ratio);
}

inline u64 get_compress_ratio_value(void)
{
	return atomic64_read(&compress_ratio);
}

inline unsigned int get_avail_buffers_value(void)
{
	return atomic_read(&avail_buffers);
}

inline unsigned int get_min_avail_buffers_value(void)
{
	return atomic_read(&min_avail_buffers);
}

inline unsigned int get_high_avail_buffers_value(void)
{
	return atomic_read(&high_avail_buffers);
}

inline u64 get_swapd_max_reclaim_size(void)
{
	return atomic_read(&max_reclaim_size);
}

inline u64 get_free_swap_threshold_value(void)
{
	return atomic64_read(&free_swap_threshold);
}

inline unsigned long long get_area_anon_refault_threshold_value(void)
{
	return atomic64_read(&area_anon_refault_threshold);
}

inline unsigned long get_anon_refault_snapshot_min_interval_value(void)
{
	return atomic64_read(&anon_refault_snapshot_min_interval);
}

inline unsigned long long get_empty_round_skip_interval_value(void)
{
	return atomic64_read(&empty_round_skip_interval);
}

inline unsigned long long get_max_skip_interval_value(void)
{
	return atomic64_read(&max_skip_interval);
}

inline unsigned long long get_empty_round_check_threshold_value(void)
{
	return atomic64_read(&empty_round_check_threshold);
}

inline u64 get_zram_critical_threshold_value(void)
{
	return atomic64_read(&zram_crit_thres);
}

inline u64 get_cpuload_threshold_value(void)
{
	return atomic64_read(&cpuload_threshold);
}

static ssize_t avail_buffers_params_write(struct kernfs_open_file *of,
		char *buf, size_t nbytes, loff_t off)
{
	unsigned int avail_buffers_value;
	unsigned int min_avail_buffers_value;
	unsigned int high_avail_buffers_value;
	u64 free_swap_threshold_value;

	buf = strstrip(buf);

	if (sscanf(buf, "%u %u %u %llu",
				&avail_buffers_value,
				&min_avail_buffers_value,
				&high_avail_buffers_value,
				&free_swap_threshold_value) != 4)
		return -EINVAL;

	atomic_set(&avail_buffers, avail_buffers_value);
	atomic_set(&min_avail_buffers, min_avail_buffers_value);
	atomic_set(&high_avail_buffers, high_avail_buffers_value);
	atomic64_set(&free_swap_threshold,
			(free_swap_threshold_value * (SZ_1M / PAGE_SIZE)));

	if (atomic_read(&min_avail_buffers) == 0)
		atomic_set(&snapshotd_init_flag, 0);
	else
		atomic_set(&snapshotd_init_flag, 1);

	wake_all_swapd();

	return nbytes;
}

static int avail_buffers_params_show(struct seq_file *m, void *v)
{
	seq_printf(m, "avail_buffers: %u\n",
			atomic_read(&avail_buffers));
	seq_printf(m, "min_avail_buffers: %u\n",
			atomic_read(&min_avail_buffers));
	seq_printf(m, "high_avail_buffers: %u\n",
			atomic_read(&high_avail_buffers));
	seq_printf(m, "free_swap_threshold: %llu\n",
			(atomic64_read(&free_swap_threshold) * PAGE_SIZE / SZ_1M));

	return 0;
}

static ssize_t swapd_max_reclaim_size_write(struct kernfs_open_file *of,
		char *buf, size_t nbytes, loff_t off)
{
	const unsigned int base = 10;
	u32 max_reclaim_size_value;
	int ret;

	buf = strstrip(buf);
	ret = kstrtouint(buf, base, &max_reclaim_size_value);
	if (ret)
		return -EINVAL;

	atomic_set(&max_reclaim_size, max_reclaim_size_value);

	return nbytes;
}

static int swapd_max_reclaim_size_show(struct seq_file *m, void *v)
{
	seq_printf(m, "swapd_max_reclaim_size: %u\n",
			atomic_read(&max_reclaim_size));

	return 0;
}

static int area_anon_refault_threshold_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	if (val < 0)
		return -EINVAL;

	atomic64_set(&area_anon_refault_threshold, val);

	return 0;
}

static s64 area_anon_refault_threshold_read(struct cgroup_subsys_state *css,
		struct cftype *cft)
{
	return atomic64_read(&area_anon_refault_threshold);
}

static int empty_round_skip_interval_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	if (val < 0)
		return -EINVAL;

	atomic64_set(&empty_round_skip_interval, val);

	return 0;
}

static s64 empty_round_skip_interval_read(struct cgroup_subsys_state *css,
		struct cftype *cft)
{
	return atomic64_read(&empty_round_skip_interval);
}

static int max_skip_interval_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	if (val < 0)
		return -EINVAL;

	atomic64_set(&max_skip_interval, val);

	return 0;
}

static s64 max_skip_interval_read(struct cgroup_subsys_state *css,
		struct cftype *cft)
{
	return atomic64_read(&max_skip_interval);
}

static int empty_round_check_threshold_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	if (val < 0)
		return -EINVAL;

	atomic64_set(&empty_round_check_threshold, val);

	return 0;
}

static s64 empty_round_check_threshold_read(struct cgroup_subsys_state *css,
		struct cftype *cft)
{
	return atomic64_read(&empty_round_check_threshold);
}


static int anon_refault_snapshot_min_interval_write(
		struct cgroup_subsys_state *css, struct cftype *cft, s64 val)
{
	if (val < 0)
		return -EINVAL;

	atomic64_set(&anon_refault_snapshot_min_interval, val);

	return 0;
}

static s64 anon_refault_snapshot_min_interval_read(
		struct cgroup_subsys_state *css, struct cftype *cft)
{
	return atomic64_read(&anon_refault_snapshot_min_interval);
}

static int zram_critical_thres_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	if (val < 0)
		return -EINVAL;

	atomic64_set(&zram_crit_thres, val << (20 - PAGE_SHIFT));

	return 0;
}

static s64 zram_critical_thres_read(struct cgroup_subsys_state *css,
		struct cftype *cft)
{
	return atomic64_read(&zram_crit_thres) >> (20 - PAGE_SHIFT);
}

static s64 cpuload_threshold_read(struct cgroup_subsys_state *css,
		struct cftype *cft)

{
	return atomic64_read(&cpuload_threshold);
}

static int cpuload_threshold_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	if (val < 0)
		return -EINVAL;

	atomic64_set(&cpuload_threshold, val);

	return 0;
}

static ssize_t swapd_pressure_event_control(struct kernfs_open_file *of,
		char *buf, size_t nbytes, loff_t off)
{
	int efd;
	unsigned int level;
	struct fd efile;
	int ret;

	buf = strstrip(buf);
	if (sscanf(buf, "%d %u", &efd, &level) != 2)
		return -EINVAL;

	if (level >= LEVEL_COUNT)
		return -EINVAL;

	if (efd < 0)
		return -EBADF;

	mutex_lock(&pressure_event_lock);
	efile = fdget(efd);
	if (!efile.file) {
		ret = -EBADF;
		goto out;
	}
	swapd_press_efd[level] = eventfd_ctx_fileget(efile.file);
	if (IS_ERR(swapd_press_efd[level])) {
		ret = PTR_ERR(swapd_press_efd[level]);
		goto out_put_efile;
	}
	fdput(efile);
	mutex_unlock(&pressure_event_lock);
	return nbytes;

out_put_efile:
	fdput(efile);
out:
	mutex_unlock(&pressure_event_lock);

	return ret;
}

void swapd_pressure_report(enum swapd_pressure_level level)
{
	int ret;

	if (swapd_press_efd[level] == NULL)
		return;

	ret = eventfd_signal(swapd_press_efd[level], 1);
	hybp(HYB_INFO, "SWAP-MM: level:%u, ret:%d ", level, ret);
}

static s64 swapd_pid_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	return swapd_pid;
}

static void swapd_memcgs_param_parse(int level_num)
{
	struct mem_cgroup *memcg = NULL;
	memcg_hybs_t *hybs = NULL;
	int i;

	while ((memcg = get_next_memcg(memcg))) {
		hybs = MEMCGRP_ITEM_DATA(memcg);

		for (i = 0; i < level_num; ++i) {
			if (atomic64_read(&hybs->app_score) >= zswap_param[i].min_score &&
					atomic64_read(&hybs->app_score) <= zswap_param[i].max_score)
				break;
		}
		atomic_set(&hybs->ub_mem2zram_ratio, zswap_param[i].ub_mem2zram_ratio);
		atomic_set(&hybs->ub_zram2ufs_ratio, zswap_param[i].ub_zram2ufs_ratio);
		atomic_set(&hybs->refault_threshold, zswap_param[i].refault_threshold);
	}
}

static void update_swapd_memcg_hybs(memcg_hybs_t *hybs)
{
	int i;

	for (i = 0; i < SWAPD_MAX_LEVEL_NUM; ++i) {
		if (!zswap_param[i].min_score && !zswap_param[i].max_score)
			return;

		if (atomic64_read(&hybs->app_score) >= zswap_param[i].min_score &&
				atomic64_read(&hybs->app_score) <= zswap_param[i].max_score)
			break;
	}

	if (i == SWAPD_MAX_LEVEL_NUM)
		return;

	atomic_set(&hybs->ub_mem2zram_ratio, zswap_param[i].ub_mem2zram_ratio);
	atomic_set(&hybs->ub_zram2ufs_ratio, zswap_param[i].ub_zram2ufs_ratio);
	atomic_set(&hybs->refault_threshold, zswap_param[i].refault_threshold);
}

void update_swapd_memcg_param(struct mem_cgroup *memcg)
{
	memcg_hybs_t *hybs = MEMCGRP_ITEM_DATA(memcg);

	if (!hybs)
		return;

	update_swapd_memcg_hybs(hybs);
}

static int update_swapd_memcgs_param(char *buf)
{
	const char delim[] = " ";
	char *token = NULL;
	int level_num;
	int i;

	buf = strstrip(buf);
	token = strsep(&buf, delim);

	if (!token)
		return -EINVAL;

	if (kstrtoint(token, 0, &level_num))
		return -EINVAL;

	if (level_num > SWAPD_MAX_LEVEL_NUM || level_num < 0)
		return -EINVAL;

	mutex_lock(&reclaim_para_lock);
	for (i = 0; i < level_num; ++i) {
		token = strsep(&buf, delim);
		if (!token)
			goto out;

		if (kstrtoint(token, 0, &zswap_param[i].min_score) ||
				zswap_param[i].min_score > MAX_APP_SCORE)
			goto out;

		token = strsep(&buf, delim);
		if (!token)
			goto out;

		if (kstrtoint(token, 0, &zswap_param[i].max_score) ||
				zswap_param[i].max_score > MAX_APP_SCORE)
			goto out;

		token = strsep(&buf, delim);
		if (!token)
			goto out;

		if (kstrtoint(token, 0, &zswap_param[i].ub_mem2zram_ratio) ||
				zswap_param[i].ub_mem2zram_ratio > MAX_RATIO)
			goto out;

		token = strsep(&buf, delim);
		if (!token)
			goto out;

		if (kstrtoint(token, 0, &zswap_param[i].ub_zram2ufs_ratio) ||
				zswap_param[i].ub_zram2ufs_ratio > MAX_RATIO)
			goto out;

		token = strsep(&buf, delim);
		if (!token)
			goto out;

		if (kstrtoint(token, 0, &zswap_param[i].refault_threshold))
			goto out;
	}

	swapd_memcgs_param_parse(level_num);
	mutex_unlock(&reclaim_para_lock);
	return 0;

out:
	mutex_unlock(&reclaim_para_lock);
	return -EINVAL;
}

static ssize_t swapd_memcgs_param_write(struct kernfs_open_file *of, char *buf,
		size_t nbytes, loff_t off)
{
	int ret = update_swapd_memcgs_param(buf);

	if (ret)
		return ret;

	return nbytes;
}

static int swapd_memcgs_param_show(struct seq_file *m, void *v)
{
	int i;

	for (i = 0; i < SWAPD_MAX_LEVEL_NUM; ++i) {
		seq_printf(m, "level %d min score: %u\n",
				i, zswap_param[i].min_score);
		seq_printf(m, "level %d max score: %u\n",
				i, zswap_param[i].max_score);
		seq_printf(m, "level %d ub_mem2zram_ratio: %u\n",
				i, zswap_param[i].ub_mem2zram_ratio);
		seq_printf(m, "level %d ub_zram2ufs_ratio: %u\n",
				i, zswap_param[i].ub_zram2ufs_ratio);
		seq_printf(m, "memcg %d refault_threshold: %u\n",
				i, zswap_param[i].refault_threshold);
	}

	return 0;
}

static ssize_t swapd_nap_jiffies_write(struct kernfs_open_file *of, char *buf,
		size_t nbytes, loff_t off)
{
	unsigned long nap;

	buf = strstrip(buf);
	if (!buf)
		return -EINVAL;

	if (kstrtoul(buf, 0, &nap))
		return -EINVAL;

	swapd_nap_jiffies = nap;
	return nbytes;
}

static int swapd_nap_jiffies_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%lu\n", swapd_nap_jiffies);

	return 0;
}

static ssize_t swapd_single_memcg_param_write(struct kernfs_open_file *of,
		char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	unsigned int ub_mem2zram_ratio;
	unsigned int ub_zram2ufs_ratio;
	unsigned int refault_threshold;
	memcg_hybs_t *hybs = MEMCGRP_ITEM_DATA(memcg);

	if (!hybs)
		return -EINVAL;

	buf = strstrip(buf);

	if (sscanf(buf, "%u %u %u", &ub_mem2zram_ratio, &ub_zram2ufs_ratio,
				&refault_threshold) != 3)
		return -EINVAL;

	if (ub_mem2zram_ratio > MAX_RATIO || ub_zram2ufs_ratio > MAX_RATIO)
		return -EINVAL;

	atomic_set(&MEMCGRP_ITEM(memcg, ub_mem2zram_ratio), ub_mem2zram_ratio);
	atomic_set(&MEMCGRP_ITEM(memcg, ub_zram2ufs_ratio), ub_zram2ufs_ratio);
	atomic_set(&MEMCGRP_ITEM(memcg, refault_threshold), refault_threshold);

	return nbytes;
}


static int swapd_single_memcg_param_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(m));
	memcg_hybs_t *hybs = MEMCGRP_ITEM_DATA(memcg);

	if (!hybs)
		return -EINVAL;

	seq_printf(m, "memcg score: %lu\n",
			atomic64_read(&hybs->app_score));
	seq_printf(m, "memcg ub_mem2zram_ratio: %u\n",
			atomic_read(&hybs->ub_mem2zram_ratio));
	seq_printf(m, "memcg ub_zram2ufs_ratio: %u\n",
			atomic_read(&hybs->ub_zram2ufs_ratio));
	seq_printf(m, "memcg refault_threshold: %u\n",
			atomic_read(&hybs->refault_threshold));

	return 0;
}

static int mem_cgroup_zram_wm_ratio_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	if (val > MAX_RATIO || val < MIN_RATIO)
		return -EINVAL;

	atomic64_set(&zram_wm_ratio, val);

	return 0;
}

static s64 mem_cgroup_zram_wm_ratio_read(struct cgroup_subsys_state *css,
		struct cftype *cft)
{
	return atomic64_read(&zram_wm_ratio);
}

static int mem_cgroup_compress_ratio_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	if (val > MAX_RATIO || val < MIN_RATIO)
		return -EINVAL;

	atomic64_set(&compress_ratio, val);

	return 0;
}

static s64 mem_cgroup_compress_ratio_read(struct cgroup_subsys_state *css,
		struct cftype *cft)
{
	return atomic64_read(&compress_ratio);
}

static int memcg_active_app_info_list_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = NULL;
	unsigned long anon_size;
	unsigned long zram_size;
	unsigned long eswap_size;

	while ((memcg = get_next_memcg(memcg))) {
		u64 score;

		if (!MEMCGRP_ITEM_DATA(memcg))
			continue;

		score = atomic64_read(&MEMCGRP_ITEM(memcg, app_score));
		anon_size = memcg_anon_pages(memcg);
		eswap_size = hybridswap_read_mcg_stats(memcg,
				MCG_DISK_STORED_PG_SZ);
		zram_size = hybridswap_read_mcg_stats(memcg,
				MCG_ZRAM_STORED_PG_SZ);

		if (anon_size + zram_size + eswap_size == 0)
			continue;

		if (!strlen(MEMCGRP_ITEM(memcg, name)))
			continue;

		anon_size *= PAGE_SIZE / SZ_1K;
		zram_size *= PAGE_SIZE / SZ_1K;
		eswap_size *= PAGE_SIZE / SZ_1K;

		seq_printf(m, "%s %llu %lu %lu %lu %llu\n",
				MEMCGRP_ITEM(memcg, name), score,
				anon_size, zram_size, eswap_size,
				MEMCGRP_ITEM(memcg, reclaimed_pagefault));
	}
	return 0;
}

static unsigned long get_totalreserve_pages(void)
{
	int nid;
	unsigned long val = 0;

	for_each_node_state(nid, N_MEMORY) {
		pg_data_t *pgdat = NODE_DATA(nid);

		if (pgdat)
			val += pgdat->totalreserve_pages;
	}

	return val;
}

unsigned int system_cur_avail_buffers(void)
{
	unsigned long reclaimable;
	long buffers;
	unsigned long pagecache;
	unsigned long wmark_low = 0;
	struct zone *zone;

	buffers = global_zone_page_state(NR_FREE_PAGES) - all_totalreserve_pages;

	for_each_zone(zone)
		wmark_low += low_wmark_pages(zone);
	pagecache = global_node_page_state(NR_ACTIVE_FILE) +
		global_node_page_state(NR_INACTIVE_FILE);
	pagecache -= min(pagecache / 2, wmark_low);
	buffers += pagecache;

	reclaimable = global_node_page_state(NR_SLAB_RECLAIMABLE) +
		global_node_page_state(NR_KERNEL_MISC_RECLAIMABLE);
	buffers += reclaimable - min(reclaimable / 2, wmark_low);

	if (buffers < 0)
		buffers = 0;

	return buffers >> 8; /* pages to MB */
}

static bool min_buffer_is_suitable(void)
{
	u32 curr_buffers = system_cur_avail_buffers();

	if (curr_buffers >= get_min_avail_buffers_value())
		return true;

	return false;
}

static bool buffer_is_suitable(void)
{
	u32 curr_buffers = system_cur_avail_buffers();

	if (curr_buffers >= get_avail_buffers_value())
		return true;

	return false;
}

bool high_buffer_is_suitable(void)
{
	u32 curr_buffers = system_cur_avail_buffers();

	if (curr_buffers >= get_high_avail_buffers_value())
		return true;

	return false;
}

static void snapshot_anon_refaults(void)
{
	struct mem_cgroup *memcg = NULL;

	while ((memcg = get_next_memcg(memcg))) {
		MEMCGRP_ITEM(memcg, reclaimed_pagefault) =
			hybridswap_read_mcg_stats(memcg, MCG_ANON_FAULT_CNT);
	}

	area_last_anon_pagefault = hybridswap_get_zram_pagefault();
	last_anon_snapshot_time = jiffies;
}

/*
 * Return true means skip reclaim.
 */
bool get_memcg_anon_refault_status(struct mem_cgroup *memcg,
		pg_data_t *pgdat)
{
	const unsigned int percent_constant = 100;
	unsigned long long cur_anon_pagefault;
	unsigned long anon_total;
	unsigned long long ratio, thresh;
	memcg_hybs_t *hybs;

	if (!memcg || !MEMCGRP_ITEM_DATA(memcg))
		return false;

	hybs = MEMCGRP_ITEM_DATA(memcg);
	thresh = atomic_read(&hybs->refault_threshold);
	if (thresh == 0)
		return false;

	cur_anon_pagefault = hybridswap_read_mcg_stats(memcg, MCG_ANON_FAULT_CNT);
	if (cur_anon_pagefault == hybs->reclaimed_pagefault)
		return false;

	anon_total = memcg_anon_pages(memcg) +
		hybridswap_read_mcg_stats(memcg, MCG_DISK_STORED_PG_SZ) +
		hybridswap_read_mcg_stats(memcg, MCG_ZRAM_STORED_PG_SZ);
	ratio = (cur_anon_pagefault - hybs->reclaimed_pagefault) *
		percent_constant / (anon_total + 1);
	hybp(HYB_INFO, "memcg %16s ratio %8llu threshold %8llu\n", hybs->name,
			ratio, thresh);

	if (ratio >= thresh)
		return true;

	return false;
}

static bool hybridswap_ratio_ok(void)
{
	struct hybridswap_stat *stat = NULL;

	stat = hybridswap_get_stat_obj();
	if (unlikely(!stat)) {
		hybp(HYB_ERR, "can't get stat obj!\n");

		return false;
	}

	return (atomic64_read(&stat->zram_stored_pages) >
			atomic64_read(&stat->stored_pages));
}

static bool get_area_anon_refault_status(void)
{
	const unsigned int percent_constant = 1000;
	unsigned long long cur_anon_pagefault;
	unsigned long long cur_time;
	unsigned long long ratio = 0;

	cur_anon_pagefault = hybridswap_get_zram_pagefault();
	cur_time = jiffies;

	if (cur_anon_pagefault == area_last_anon_pagefault
			|| cur_time == last_anon_snapshot_time)
		goto false_out;

	ratio = (cur_anon_pagefault - area_last_anon_pagefault) *
		percent_constant / (jiffies_to_msecs(cur_time -
					last_anon_snapshot_time) + 1);

	global_anon_refault_ratio = ratio;

	if (ratio > get_area_anon_refault_threshold_value())
		return true;

	hybp(HYB_INFO, "current %llu t %llu last %llu t %llu ratio %llu refault_ratio %llu\n",
			cur_anon_pagefault, cur_time,
			area_last_anon_pagefault, last_anon_snapshot_time,
			ratio, area_anon_refault_threshold);
false_out:
	return false;
}

static int reclaim_exceed_sleep_ms_write(
		struct cgroup_subsys_state *css, struct cftype *cft, s64 val)
{
	if (val < 0)
		return -EINVAL;

	reclaim_exceed_sleep_ms = val;

	return 0;
}

static s64 reclaim_exceed_sleep_ms_read(
		struct cgroup_subsys_state *css, struct cftype *cft)
{
	return reclaim_exceed_sleep_ms;
}

static int max_reclaimin_size_mb_write(
		struct cgroup_subsys_state *css, struct cftype *cft, u64 val)
{
	max_reclaimin_size = (val << 20);

	return 0;
}

static u64 max_reclaimin_size_mb_read(
		struct cgroup_subsys_state *css, struct cftype *cft)
{
	return max_reclaimin_size >> 20;
}

static ssize_t swapd_shrink_parameter_write(struct kernfs_open_file *of,
		char *buf, size_t nbytes, loff_t off)
{
	unsigned long window, limit;

	buf = strstrip(buf);
	if (sscanf(buf, "%lu %lu", &window, &limit) != 2)
		return -EINVAL;

	swapd_shrink_window = msecs_to_jiffies(window);
	swapd_shrink_limit_per_window = limit;

	return nbytes;
}

static int swapd_shrink_parameter_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%-32s %lu(jiffies) %u(msec)\n", "swapd_shrink_window",
			swapd_shrink_window, jiffies_to_msecs(swapd_shrink_window));
	seq_printf(m, "%-32s %lu MB\n", "swapd_shrink_limit_per_window",
			swapd_shrink_limit_per_window);
	seq_printf(m, "%-32s %u msec\n", "swapd_last_window",
			jiffies_to_msecs(jiffies - swapd_last_window_start));
	seq_printf(m, "%-32s %lu MB\n", "swapd_last_window_shrink",
			swapd_last_window_shrink);

	return 0;
}

static int swapd_update_cpumask(struct task_struct *tsk, char *buf,
		struct pglist_data *pgdat)
{
	int retval;
	struct cpumask temp_mask;
	const struct cpumask *cpumask = cpumask_of_node(pgdat->node_id);
	struct hybridswapd_task* hyb_task = PGDAT_ITEM_DATA(pgdat);

	if (unlikely(!hyb_task)) {
		hybp(HYB_ERR, "set task %s cpumask %s node %d failed, "
				"hyb_task is NULL\n", tsk->comm, buf, pgdat->node_id);
		return -EINVAL;
	}

	cpumask_clear(&temp_mask);
	retval = cpulist_parse(buf, &temp_mask);
	if (retval < 0 || cpumask_empty(&temp_mask)) {
		hybp(HYB_ERR, "%s are invalid, use default\n", buf);
		goto use_default;
	}

	if (!cpumask_subset(&temp_mask, cpu_present_mask)) {
		hybp(HYB_ERR, "%s is not subset of cpu_present_mask, use default\n",
				buf);
		goto use_default;
	}

	if (!cpumask_subset(&temp_mask, cpumask)) {
		hybp(HYB_ERR, "%s is not subset of cpumask, use default\n", buf);
		goto use_default;
	}

	set_cpus_allowed_ptr(tsk, &temp_mask);
	cpumask_copy(&hyb_task->swapd_bind_cpumask, &temp_mask);
	return 0;

use_default:
	if (cpumask_empty(&hyb_task->swapd_bind_cpumask))
		set_cpus_allowed_ptr(tsk, cpumask);
	return -EINVAL;
}

static ssize_t swapd_bind_write(struct kernfs_open_file *of, char *buf,
		size_t nbytes, loff_t off)
{
	int ret = 0, nid;
	struct pglist_data *pgdat;

	buf = strstrip(buf);
	for_each_node_state(nid, N_MEMORY) {
		pgdat = NODE_DATA(nid);
		if (!PGDAT_ITEM_DATA(pgdat))
			continue;

		if (PGDAT_ITEM(pgdat, swapd)) {
			ret = swapd_update_cpumask(PGDAT_ITEM(pgdat, swapd),
					buf, pgdat);
			if (ret)
				break;
		}
	}

	if (ret)
		return ret;

	return nbytes;
}

static int swapd_bind_read(struct seq_file *m, void *v)
{
	int nid;
	struct pglist_data *pgdat;
	struct hybridswapd_task* hyb_task;

	seq_printf(m, "%4s %s\n", "Node", "mask");
	for_each_node_state(nid, N_MEMORY) {
		pgdat = NODE_DATA(nid);
		hyb_task = PGDAT_ITEM_DATA(pgdat);
		if (!hyb_task)
			continue;

		if (!hyb_task->swapd)
			continue;
		seq_printf(m, "%4d %*pbl\n", nid,
				cpumask_pr_args(&hyb_task->swapd_bind_cpumask));
	}

	return 0;
}

struct cftype mem_cgroup_swapd_legacy_files[] = {
	{
		.name = "active_app_info_list",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.seq_show = memcg_active_app_info_list_show,
	},
	{
		.name = "zram_wm_ratio",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write_s64 = mem_cgroup_zram_wm_ratio_write,
		.read_s64 = mem_cgroup_zram_wm_ratio_read,
	},
	{
		.name = "compress_ratio",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write_s64 = mem_cgroup_compress_ratio_write,
		.read_s64 = mem_cgroup_compress_ratio_read,
	},
	{
		.name = "swapd_pressure",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write = swapd_pressure_event_control,
	},
	{
		.name = "swapd_pid",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.read_s64 = swapd_pid_read,
	},
	{
		.name = "avail_buffers",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write = avail_buffers_params_write,
		.seq_show = avail_buffers_params_show,
	},
	{
		.name = "swapd_max_reclaim_size",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write = swapd_max_reclaim_size_write,
		.seq_show = swapd_max_reclaim_size_show,
	},
	{
		.name = "area_anon_refault_threshold",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write_s64 = area_anon_refault_threshold_write,
		.read_s64 = area_anon_refault_threshold_read,
	},
	{
		.name = "empty_round_skip_interval",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write_s64 = empty_round_skip_interval_write,
		.read_s64 = empty_round_skip_interval_read,
	},
	{
		.name = "max_skip_interval",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write_s64 = max_skip_interval_write,
		.read_s64 = max_skip_interval_read,
	},
	{
		.name = "empty_round_check_threshold",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write_s64 = empty_round_check_threshold_write,
		.read_s64 = empty_round_check_threshold_read,
	},
	{
		.name = "anon_refault_snapshot_min_interval",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write_s64 = anon_refault_snapshot_min_interval_write,
		.read_s64 = anon_refault_snapshot_min_interval_read,
	},
	{
		.name = "swapd_memcgs_param",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write = swapd_memcgs_param_write,
		.seq_show = swapd_memcgs_param_show,
	},
	{
		.name = "swapd_single_memcg_param",
		.flags = CFTYPE_WORLD_WRITABLE,
		.write = swapd_single_memcg_param_write,
		.seq_show = swapd_single_memcg_param_show,
	},
	{
		.name = "zram_critical_threshold",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write_s64 = zram_critical_thres_write,
		.read_s64 = zram_critical_thres_read,
	},
	{
		.name = "cpuload_threshold",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write_s64 = cpuload_threshold_write,
		.read_s64 = cpuload_threshold_read,
	},
	{
		.name = "reclaim_exceed_sleep_ms",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write_s64 = reclaim_exceed_sleep_ms_write,
		.read_s64 = reclaim_exceed_sleep_ms_read,
	},
	{
		.name = "swapd_bind",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write = swapd_bind_write,
		.seq_show = swapd_bind_read,
	},
	{
		.name = "max_reclaimin_size_mb",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write_u64 = max_reclaimin_size_mb_write,
		.read_u64 = max_reclaimin_size_mb_read,
	},
	{
		.name = "swapd_shrink_parameter",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write = swapd_shrink_parameter_write,
		.seq_show = swapd_shrink_parameter_show,
	},
	{
		.name = "swapd_nap_jiffies",
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_WORLD_WRITABLE,
		.write = swapd_nap_jiffies_write,
		.seq_show = swapd_nap_jiffies_show,
	},
	{ }, /* terminate */
};

void wakeup_snapshotd(void)
{
	unsigned long curr_snapshot_interval =
		jiffies_to_msecs(jiffies - last_anon_snapshot_time);

	if (curr_snapshot_interval >=
			get_anon_refault_snapshot_min_interval_value()) {
		atomic_set(&snapshotd_wait_flag, 1);
		wake_up_interruptible(&snapshotd_wait);
	}
}

static int snapshotd(void *p)
{
	int ret;

	while (!kthread_should_stop()) {
		ret = wait_event_interruptible(snapshotd_wait,
				atomic_read(&snapshotd_wait_flag));
		if (ret)
			continue;

		if (unlikely(kthread_should_stop()))
			break;

		atomic_set(&snapshotd_wait_flag, 0);

		snapshot_anon_refaults();
		count_swapd_event(SWAPD_SNAPSHOT_TIMES);
	}

	return 0;
}

static int snapshotd_run(void)
{
	atomic_set(&snapshotd_wait_flag, 0);
	init_waitqueue_head(&snapshotd_wait);
	snapshotd_task = kthread_run(snapshotd, NULL, "snapshotd");

	if (IS_ERR(snapshotd_task)) {
		hybp(HYB_ERR, "Failed to start snapshotd\n");
		return PTR_ERR(snapshotd_task);
	}

	return 0;
}

static void snapshotd_exit(void)
{
	if (snapshotd_task) {
		atomic_set(&snapshotd_wait_flag, 1);
		kthread_stop(snapshotd_task);
	}
	snapshotd_task = NULL;
}

unsigned long get_nr_zram_total(void)
{
	unsigned long nr_zram = 1;

	if (!swapd_zram)
		return nr_zram;

	nr_zram = swapd_zram->disksize >> PAGE_SHIFT;
#if (defined CONFIG_ZRAM_WRITEBACK) || (defined CONFIG_HYBRIDSWAP_CORE)
	nr_zram -= swapd_zram->increase_nr_pages;
#endif
	return nr_zram;
}

u64 get_hybridswap_meminfo(const char *type)
{
	if (!type || !swapd_zram)
		return 0;

	if (!strcmp(type, "same_pages"))
		return (u64)atomic64_read(&swapd_zram->stats.same_pages);
	if (!strcmp(type, "compr_data_size"))
		return (u64)atomic64_read(&swapd_zram->stats.compr_data_size);
	if (!strcmp(type, "pages_stored"))
		return (u64)atomic64_read(&swapd_zram->stats.pages_stored);
	return 0;
}
EXPORT_SYMBOL(get_hybridswap_meminfo);

bool zram_watermark_ok(void)
{
	long long diff_buffers;
	long long wm = 0;
	long long cur_ratio = 0;
	unsigned long zram_used = hybridswap_get_zram_used_pages();
	const unsigned int percent_constant = 100;

	diff_buffers = get_high_avail_buffers_value() -
		system_cur_avail_buffers();
	diff_buffers *= SZ_1M / PAGE_SIZE;
	diff_buffers *= get_compress_ratio_value() / 10;
	diff_buffers = diff_buffers * percent_constant / get_nr_zram_total();

	cur_ratio = zram_used * percent_constant / get_nr_zram_total();
	wm  = min(get_zram_wm_ratio_value(), get_zram_wm_ratio_value()- diff_buffers);

	return cur_ratio > wm;
}

bool free_zram_is_ok(void)
{
	unsigned long nr_used = hybridswap_get_zram_used_pages();
	unsigned long nr_total = get_nr_zram_total();
	unsigned long reserve = nr_total >> 6;

	return (nr_used < (nr_total - reserve));
}

static bool zram_need_swapout(void)
{
	bool zram_wm_ok = zram_watermark_ok();
	bool avail_buffer_wm_ok = !high_buffer_is_suitable();
	bool ufs_wm_ok = true;

#ifdef CONFIG_HYBRIDSWAP_CORE
	ufs_wm_ok = hybridswap_stored_wm_ok();
#endif

	if (zram_wm_ok && avail_buffer_wm_ok && ufs_wm_ok)
		return true;

	hybp(HYB_INFO, "zram_wm_ok %d avail_buffer_wm_ok %d ufs_wm_ok %d\n",
			zram_wm_ok, avail_buffer_wm_ok, ufs_wm_ok);

	return false;
}

bool zram_watermark_exceed(void)
{
	u64 nr_zram_used;
	u64 nr_wm = get_zram_critical_threshold_value();

	if (!nr_wm)
		return false;

	nr_zram_used = hybridswap_get_zram_used_pages();

	if (nr_zram_used > nr_wm)
		return true;

	return false;
}


#ifdef CONFIG_OPLUS_JANK
static bool is_cpu_busy(void)
{
	unsigned int cpuload = 0;
	int i;
	struct cpumask mask;

	cpumask_clear(&mask);

	for (i = 0; i < 6; i++)
		cpumask_set_cpu(i, &mask);

	cpuload = get_cpu_load(1, &mask);
	if (cpuload > get_cpuload_threshold_value()) {
		hybp(HYB_INFO, "cpuload %d\n", cpuload);
		return true;
	}

	return false;
}
#endif

static void wakeup_swapd(pg_data_t *pgdat)
{
	unsigned long curr_interval;
	struct hybridswapd_task* hyb_task = PGDAT_ITEM_DATA(pgdat);

	if (!hyb_task || !hyb_task->swapd)
		return;

	if (atomic_read(&swapd_pause)) {
		count_swapd_event(SWAPD_MANUAL_PAUSE);
		return;
	}

#if IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
	if (atomic_read(&display_off))
		return;
#endif

	if (!waitqueue_active(&hyb_task->swapd_wait))
		return;

	/* make anon pagefault snapshots */
	if (atomic_read(&snapshotd_init_flag) == 1)
		wakeup_snapshotd();

	/* wake up when the buffer is lower than min_avail_buffer */
	if (min_buffer_is_suitable()) {
		count_swapd_event(SWAPD_OVER_MIN_BUFFER_SKIP_TIMES);
		return;
	}

	curr_interval = jiffies_to_msecs(jiffies - last_swapd_time);
	if (curr_interval < swapd_skip_interval) {
		count_swapd_event(SWAPD_EMPTY_ROUND_SKIP_TIMES);
		return;
	}

	atomic_set(&hyb_task->swapd_wait_flag, 1);
	wake_up_interruptible(&hyb_task->swapd_wait);
}

void wake_all_swapd(void)
{
	pg_data_t *pgdat = NULL;
	int nid;

	for_each_online_node(nid) {
		pgdat = NODE_DATA(nid);
		wakeup_swapd(pgdat);
	}
}

bool free_swap_is_low(void)
{
	struct sysinfo info;

	si_swapinfo(&info);

	return (info.freeswap < get_free_swap_threshold_value());
}
EXPORT_SYMBOL(free_swap_is_low);

static inline u64 __calc_nr_to_reclaim(void)
{
	u32 curr_buffers;
	u64 high_buffers;
	u64 max_reclaim_size_value;
	u64 reclaim_size = 0;

	high_buffers = get_high_avail_buffers_value();
	curr_buffers = system_cur_avail_buffers();
	max_reclaim_size_value = get_swapd_max_reclaim_size();
	if (curr_buffers < high_buffers)
		reclaim_size = high_buffers - curr_buffers;

	reclaim_size = min(reclaim_size, max_reclaim_size_value);

	return reclaim_size * SZ_1M / PAGE_SIZE;
}

static inline u64 calc_shrink_ratio(pg_data_t *pgdat)
{
	struct mem_cgroup *memcg = NULL;
	const u32 percent_constant = 100;
	u64 total_can_reclaimed = 0;

	while ((memcg = get_next_memcg(memcg))) {
		s64 nr_anon, nr_zram, nr_eswap, total, can_reclaimed, thresh;
		memcg_hybs_t *hybs;

		hybs = MEMCGRP_ITEM_DATA(memcg);
		thresh = atomic_read(&hybs->ub_mem2zram_ratio);
		if (!thresh || get_memcg_anon_refault_status(memcg, pgdat)) {
			hybs->can_reclaimed = 0;
			continue;
		}

		nr_anon = memcg_anon_pages(memcg);
		nr_zram = hybridswap_read_mcg_stats(memcg, MCG_ZRAM_STORED_PG_SZ);
		nr_eswap = hybridswap_read_mcg_stats(memcg, MCG_DISK_STORED_PG_SZ);
		total = nr_anon + nr_zram + nr_eswap;

		can_reclaimed = total * thresh / percent_constant;
		if (can_reclaimed <= (nr_zram + nr_eswap))
			hybs->can_reclaimed = 0;
		else
			hybs->can_reclaimed = can_reclaimed - (nr_zram + nr_eswap);
		hybp(HYB_INFO, "memcg %s can_reclaimed %lu nr_anon %lu zram %lu eswap %lu total %lu ratio %lu thresh %lu\n",
				hybs->name, page_to_kb(hybs->can_reclaimed),
				page_to_kb(nr_anon), page_to_kb(nr_zram),
				page_to_kb(nr_eswap), page_to_kb(total),
				(nr_zram + nr_eswap) * 100 / (total + 1),
				thresh);
		total_can_reclaimed += hybs->can_reclaimed;
	}

	return total_can_reclaimed;
}

static unsigned long swapd_shrink_anon(pg_data_t *pgdat,
		unsigned long nr_to_reclaim)
{
	struct mem_cgroup *memcg = NULL;
	unsigned long nr_reclaimed = 0;
	unsigned long reclaim_memcg_cnt = 0;
	u64 total_can_reclaimed = calc_shrink_ratio(pgdat);
	unsigned long start_js = jiffies;
	unsigned long reclaim_cycles;
	bool exit = false;
	unsigned long reclaim_size_per_cycle = PAGES_PER_1MB >> 1;

	if (unlikely(total_can_reclaimed == 0))
		goto out;

	if (total_can_reclaimed < nr_to_reclaim)
		nr_to_reclaim = total_can_reclaimed;

	reclaim_cycles = nr_to_reclaim / reclaim_size_per_cycle;
	while (reclaim_cycles) {
		while ((memcg = get_next_memcg(memcg))) {
			unsigned long memcg_nr_reclaimed, memcg_to_reclaim;
			memcg_hybs_t *hybs;

			if (high_buffer_is_suitable()) {
				get_next_memcg_break(memcg);
				exit = true;
				break;
			}

			hybs = MEMCGRP_ITEM_DATA(memcg);
			if (!hybs->can_reclaimed)
				continue;

			memcg_to_reclaim = reclaim_size_per_cycle * hybs->can_reclaimed / total_can_reclaimed;
			memcg_nr_reclaimed = try_to_free_mem_cgroup_pages(memcg,
					memcg_to_reclaim, GFP_KERNEL, true);
			reclaim_memcg_cnt++;
			hybs->can_reclaimed -= memcg_nr_reclaimed;
			hybp(HYB_INFO, "memcg %s reclaim %lu want %lu\n", hybs->name,
					memcg_nr_reclaimed, memcg_to_reclaim);
			nr_reclaimed += memcg_nr_reclaimed;
			if (nr_reclaimed >= nr_to_reclaim) {
				get_next_memcg_break(memcg);
				exit = true;
				break;
			}

			if (hybs->can_reclaimed < 0)
				hybs->can_reclaimed = 0;

			if (swapd_nap_jiffies && time_after_eq(jiffies, start_js + swapd_nap_jiffies)) {
				set_current_state(TASK_INTERRUPTIBLE);
				schedule_timeout((jiffies - start_js) * 2);
				start_js = jiffies;
			}
		}
		if (exit)
			break;
		reclaim_cycles--;
	}

out:
	hybp(HYB_INFO, "total_reclaim %lu nr_to_reclaim %lu from memcg %lu total_can_reclaimed %lu\n",
			page_to_kb(nr_reclaimed), page_to_kb(nr_to_reclaim),
			reclaim_memcg_cnt, page_to_kb(total_can_reclaimed));
	return nr_reclaimed;
}

static void swapd_shrink_node(pg_data_t *pgdat)
{
	const unsigned int increase_rate = 2;
	unsigned long nr_reclaimed = 0;
	unsigned long nr_to_reclaim;
	unsigned int before_avail = system_cur_avail_buffers();
	unsigned int after_avail;

#if IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
	if (atomic_read(&display_off))
		return;
#endif

	if (high_buffer_is_suitable())
		return;

#ifdef CONFIG_OPLUS_JANK
	if (is_cpu_busy()) {
		count_swapd_event(SWAPD_CPU_BUSY_BREAK_TIMES);
		return;
	}
#endif

	if ((jiffies - swapd_last_window_start) < swapd_shrink_window) {
		if (swapd_last_window_shrink >= swapd_shrink_limit_per_window) {
			count_swapd_event(SWAPD_SKIP_SHRINK_OF_WINDOW);
			hybp(HYB_INFO, "swapd_last_window_shrink %lu, skip shrink\n",
					swapd_last_window_shrink);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(msecs_to_jiffies(reclaim_exceed_sleep_ms));
			return;
		}
	} else {
		swapd_last_window_start = jiffies;
		swapd_last_window_shrink = 0lu;
	}

	nr_to_reclaim = __calc_nr_to_reclaim();
	if (!nr_to_reclaim)
		return;

	count_swapd_event(SWAPD_SHRINK_ANON);
	nr_reclaimed = swapd_shrink_anon(pgdat, nr_to_reclaim);
	swapd_last_window_shrink += PAGES_TO_MB(nr_reclaimed);

	if (nr_reclaimed < get_empty_round_check_threshold_value()) {
		count_swapd_event(SWAPD_EMPTY_ROUND);
		if (last_round_is_empty)
			swapd_skip_interval = min(swapd_skip_interval *
					increase_rate,
					get_max_skip_interval_value());
		else
			swapd_skip_interval =
				get_empty_round_skip_interval_value();
		last_round_is_empty = true;
	} else {
		swapd_skip_interval = 0;
		last_round_is_empty = false;
	}

	after_avail = system_cur_avail_buffers();
	hybp(HYB_INFO, "total_reclaimed %lu KB, avail buffer %lu %lu MB, swapd_skip_interval %llu\n",
			nr_reclaimed * 4, before_avail, after_avail, swapd_skip_interval);
}

static int swapd(void *p)
{
	pg_data_t *pgdat = (pg_data_t *)p;
	struct task_struct *tsk = current;
	struct hybridswapd_task* hyb_task = PGDAT_ITEM_DATA(pgdat);
	static unsigned long last_reclaimin_jiffies = 0;
	long fault_out_pause_value;
	int display_un_blank = 1;

	/* save swapd pid for schedule strategy */
	swapd_pid = tsk->pid;

	/* swapd do not runnint on super core */
	cpumask_clear(&hyb_task->swapd_bind_cpumask);
	(void)swapd_update_cpumask(tsk, SWAPD_DEFAULT_BIND_CPUS, pgdat);
	set_freezable();

	swapd_last_window_start = jiffies - swapd_shrink_window;
	while (!kthread_should_stop()) {
		bool refault = false;
		u64 curr_buffers, avail;
		u64 size, swapout_size = 0;

		wait_event_freezable(hyb_task->swapd_wait,
				atomic_read(&hyb_task->swapd_wait_flag));
		atomic_set(&hyb_task->swapd_wait_flag, 0);
		if (unlikely(kthread_should_stop()))
			break;
		count_swapd_event(SWAPD_WAKEUP);
		/*swapd_pressure_report(LEVEL_LOW);*/

		if (get_area_anon_refault_status() && hybridswap_ratio_ok()) {
			refault = true;
			count_swapd_event(SWAPD_REFAULT);
			goto do_eswap;
		}

		swapd_shrink_node(pgdat);
		last_swapd_time = jiffies;
do_eswap:
		fault_out_pause_value = atomic_long_read(&fault_out_pause);
#if IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
		display_un_blank = !atomic_read(&display_off);
#endif
		if (!hybridswap_reclaim_work_running() && display_un_blank &&
				(zram_need_swapout() || refault) && !fault_out_pause_value &&
				jiffies_to_msecs(jiffies - last_reclaimin_jiffies) >= 50) {
			avail = get_high_avail_buffers_value();
			curr_buffers = system_cur_avail_buffers();

			if (curr_buffers < avail) {
				size = (avail - curr_buffers) * SZ_1M;
				size = min_t(u64, size, max_reclaimin_size);
#ifdef CONFIG_HYBRIDSWAP_CORE
				swapout_size = hybridswap_reclaim_in(size);
				count_swapd_event(SWAPD_SWAPOUT);
				last_reclaimin_jiffies = jiffies;
#endif
				hybp(HYB_DEBUG, "SWAPD_SWAPOUT curr %u avail %lu size %lu maybe swapout %lu\n",
						curr_buffers, avail,
						size / SZ_1M, swapout_size / SZ_1M);
			} else  {
				hybp(HYB_INFO, "SWAPD_SKIP_SWAPOUT curr %u avail %lu\n",
						curr_buffers, avail);
				count_swapd_event(SWAPD_SKIP_SWAPOUT);
			}
		}

		if (!buffer_is_suitable()) {
			if (free_swap_is_low() || zram_watermark_exceed()) {
				swapd_pressure_report(LEVEL_CRITICAL);
				count_swapd_event(SWAPD_CRITICAL_PRESS);
			}
		}
	}

	return 0;
}

/*
 * This swapd start function will be called by init and node-hot-add.
 */
int swapd_run(int nid)
{
	pg_data_t *pgdat = NODE_DATA(nid);
	struct sched_param param = {
		.sched_priority = DEFAULT_PRIO,
	};
	struct hybridswapd_task* hyb_task = PGDAT_ITEM_DATA(pgdat);
	int ret;

	if (!hyb_task || hyb_task->swapd)
		return 0;

	atomic_set(&hyb_task->swapd_wait_flag, 0);
	hyb_task->swapd = kthread_create(swapd, pgdat, "hybridswapd:%d", nid);
	if (IS_ERR(hyb_task->swapd)) {
		hybp(HYB_ERR, "Failed to start swapd on node %d\n", nid);
		ret = PTR_ERR(hyb_task->swapd);
		hyb_task->swapd = NULL;
		return ret;
	}

	sched_setscheduler_nocheck(hyb_task->swapd, SCHED_NORMAL, &param);
	set_user_nice(hyb_task->swapd, PRIO_TO_NICE(param.sched_priority));
	wake_up_process(hyb_task->swapd);

	return 0;
}

/*
 * Called by memory hotplug when all memory in a node is offlined.  Caller must
 * hold mem_hotplug_begin/end().
 */
void swapd_stop(int nid)
{
	struct pglist_data *pgdata = NODE_DATA(nid);
	struct task_struct *swapd;
	struct hybridswapd_task* hyb_task;

	if (unlikely(!PGDAT_ITEM_DATA(pgdata))) {
		hybp(HYB_ERR, "nid %d pgdata %p PGDAT_ITEM_DATA is NULL\n",
				nid, pgdata);
		return;
	}

	hyb_task = PGDAT_ITEM_DATA(pgdata);
	swapd = hyb_task->swapd;
	if (swapd) {
		atomic_set(&hyb_task->swapd_wait_flag, 1);
		kthread_stop(swapd);
		hyb_task->swapd = NULL;
	}

	swapd_pid = -1;
}

static int mem_hotplug_swapd_notifier(struct notifier_block *nb,
		unsigned long action, void *data)
{
	struct memory_notify *arg = (struct memory_notify*)data;
	int nid = arg->status_change_nid;

	if (action == MEM_ONLINE)
		swapd_run(nid);
	else if (action == MEM_OFFLINE)
		swapd_stop(nid);

	return NOTIFY_OK;
}

static struct notifier_block swapd_notifier_nb = {
	.notifier_call = mem_hotplug_swapd_notifier,
};

static int swapd_cpu_online(unsigned int cpu)
{
	int nid;

	for_each_node_state(nid, N_MEMORY) {
		pg_data_t *pgdat = NODE_DATA(nid);
		struct hybridswapd_task* hyb_task;
		struct cpumask *mask;

		hyb_task = PGDAT_ITEM_DATA(pgdat);
		mask = &hyb_task->swapd_bind_cpumask;

		if (cpumask_any_and(cpu_online_mask, mask) < nr_cpu_ids)
			/* One of our CPUs online: restore mask */
			set_cpus_allowed_ptr(PGDAT_ITEM(pgdat, swapd), mask);
	}
	return 0;
}

void alloc_pages_slowpath_hook(void *data, gfp_t gfp_flags,
		unsigned int order, unsigned long delta)
{
	if (gfp_flags & __GFP_KSWAPD_RECLAIM)
		wake_all_swapd();
}

void rmqueue_hook(void *data, struct zone *preferred_zone,
		struct zone *zone, unsigned int order, gfp_t gfp_flags,
		unsigned int alloc_flags, int migratetype)
{
	if (gfp_flags & __GFP_KSWAPD_RECLAIM)
		wake_all_swapd();
}

static int create_swapd_thread(struct zram *zram)
{
	int nid;
	int ret;
	struct pglist_data *pgdat;
	struct hybridswapd_task *tsk_info;

	for_each_node(nid) {
		pgdat = NODE_DATA(nid);
		if (!PGDAT_ITEM_DATA(pgdat)) {
			tsk_info = kzalloc(sizeof(struct hybridswapd_task),
					GFP_KERNEL);
			if (!tsk_info) {
				hybp(HYB_ERR, "kmalloc tsk_info failed node %d\n", nid);
				goto error_out;
			}

			pgdat->android_oem_data1 = (u64)tsk_info;
		}

		init_waitqueue_head(&PGDAT_ITEM(pgdat, swapd_wait));
	}

	for_each_node_state(nid, N_MEMORY) {
		if (swapd_run(nid))
			goto error_out;
	}

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
			"mm/swapd:online", swapd_cpu_online, NULL);
	if (ret < 0) {
		hybp(HYB_ERR, "swapd: failed to register hotplug callbacks.\n");
		goto error_out;
	}
	swapd_online = ret;

	return 0;

error_out:
	for_each_node(nid) {
		pgdat = NODE_DATA(node);

		if (!PGDAT_ITEM_DATA(pgdat))
			continue;

		if (PGDAT_ITEM(pgdat, swapd)) {
			kthread_stop(PGDAT_ITEM(pgdat, swapd));
			PGDAT_ITEM(pgdat, swapd) = NULL;
		}

		kfree((void*)PGDAT_ITEM_DATA(pgdat));
		pgdat->android_oem_data1 = 0;
	}

	return -ENOMEM;
}

static void destroy_swapd_thread(void)
{
	int nid;
	struct pglist_data *pgdat;

	cpuhp_remove_state_nocalls(swapd_online);
	for_each_node(nid) {
		pgdat = NODE_DATA(node);
		if (!PGDAT_ITEM_DATA(pgdat))
			continue;

		swapd_stop(nid);
		kfree((void*)PGDAT_ITEM_DATA(pgdat));
		pgdat->android_oem_data1 = 0;
	}
}

ssize_t hybridswap_swapd_pause_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	char *type_buf = NULL;
	bool val;

	type_buf = strstrip((char *)buf);
	if (kstrtobool(type_buf, &val))
		return -EINVAL;
	atomic_set(&swapd_pause, val);

	return len;
}

ssize_t hybridswap_swapd_pause_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t size = 0;

	size += scnprintf(buf + size, PAGE_SIZE - size,
			"%d\n", atomic_read(&swapd_pause));

	return size;
}

#if IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
static int bright_fb_notifier_callback(struct notifier_block *self,
		unsigned long event, void *data)
{
	struct msm_drm_notifier *evdata = data;
	int *blank;

	if (evdata && evdata->data) {
		blank = evdata->data;

		if (*blank ==  MSM_DRM_BLANK_POWERDOWN)
			atomic_set(&display_off, 1);
		else if (*blank == MSM_DRM_BLANK_UNBLANK)
			atomic_set(&display_off, 0);
	}

	return NOTIFY_OK;
}
#endif

void __init swapd_pre_init(void)
{
	all_totalreserve_pages = get_totalreserve_pages();
}

void swapd_pre_deinit(void)
{
	all_totalreserve_pages = 0;
}

int swapd_init(struct zram *zram)
{
	int ret;

	ret = register_memory_notifier(&swapd_notifier_nb);
	if (ret) {
		hybp(HYB_ERR, "register_memory_notifier failed, ret = %d\n", ret);
		return ret;
	}

#if IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
	fb_notif.notifier_call = bright_fb_notifier_callback;
	ret = msm_drm_register_client(&fb_notif);
	if (ret) {
		hybp(HYB_ERR, "msm_drm_register_client failed, ret=%d\n", ret);
		goto msm_drm_register_fail;
	}
#endif

	ret = snapshotd_run();
	if (ret) {
		hybp(HYB_ERR, "snapshotd_run failed, ret=%d\n", ret);
		goto snapshotd_fail;
	}

	ret = create_swapd_thread(zram);
	if (ret) {
		hybp(HYB_ERR, "create_swapd_thread failed, ret=%d\n", ret);
		goto create_swapd_fail;
	}

	swapd_zram = zram;
	atomic_set(&swapd_enabled, 1);
	return 0;

create_swapd_fail:
	snapshotd_exit();
snapshotd_fail:
#if IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
	msm_drm_unregister_client(&fb_notif);
msm_drm_register_fail:
#endif
	unregister_memory_notifier(&swapd_notifier_nb);
	return ret;
}

void swapd_exit(void)
{
	destroy_swapd_thread();
	snapshotd_exit();
#if IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
	msm_drm_unregister_client(&fb_notif);
#endif
	unregister_memory_notifier(&swapd_notifier_nb);
	atomic_set(&swapd_enabled, 0);
}

bool hybridswap_swapd_enabled(void)
{
	return !!atomic_read(&swapd_enabled);
}
