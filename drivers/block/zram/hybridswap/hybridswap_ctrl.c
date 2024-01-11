// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[HYBRIDSWAP]" fmt

#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/atomic.h>
#include <linux/memcontrol.h>
#include <linux/swap.h>

#include "../zram_drv.h"
#include "../zram_drv_internal.h"
#include "hybridswap_internal.h"
#include "hybridswap.h"

#define PRE_EOL_INFO_OVER_VAL 2
#define LIFE_TIME_EST_OVER_VAL 8
#define DEFAULT_STORED_WM_RATIO 90

struct zs_ext_para {
	struct hybridswap_page_pool *pool;
	size_t alloc_size;
	bool fast;
	bool nofail;
};

struct hybridswap_cfg {
	atomic_t enable;
	atomic_t reclaim_in_enable;
	struct hybridswap_stat *stat;
	struct workqueue_struct *reclaim_wq;
	struct zram *zram;

	atomic_t dev_life;
	unsigned long quota_day;
	struct timer_list lpc_timer;
	struct work_struct lpc_work;
};

struct hybridswap_cfg global_settings;

#define DEVICE_NAME_LEN 64
static char loop_device[DEVICE_NAME_LEN];

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
	struct zs_ext_para *ext_para = (struct zs_ext_para *)data;

	if (ext_para->pool) {
		spin_lock(&ext_para->pool->page_pool_lock);
		if (!list_empty(&ext_para->pool->page_pool_list)) {
			page = list_first_entry(
					&ext_para->pool->page_pool_list,
					struct page, lru);
			list_del(&page->lru);
		}
		spin_unlock(&ext_para->pool->page_pool_lock);
	}

	if (!page) {
		if (ext_para->fast) {
			page = alloc_page(GFP_ATOMIC);
			if (likely(page))
				goto out;
		}
		if (ext_para->nofail)
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
	struct zs_ext_para ext_para;

	ext_para.pool = pool;
	ext_para.fast = fast;
	ext_para.nofail = nofail;

	return hybridswap_alloc_page_common((void *)&ext_para, gfp);
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

bool hybridswap_reclaim_in_enable(void)
{
	return !!atomic_read(&global_settings.reclaim_in_enable);
}

void hybridswap_set_reclaim_in_disable(void)
{
	atomic_set(&global_settings.reclaim_in_enable, false);
}

void hybridswap_set_reclaim_in_enable(bool en)
{
	atomic_set(&global_settings.reclaim_in_enable, en ? 1 : 0);
}

bool hybridswap_core_enabled(void)
{
	return !!atomic_read(&global_settings.enable);
}

void hybridswap_set_enable(bool en)
{
	hybridswap_set_reclaim_in_enable(en);

	if (!hybridswap_core_enabled())
		atomic_set(&global_settings.enable, en ? 1 : 0);
}

struct hybridswap_stat *hybridswap_get_stat_obj(void)
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
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();
	unsigned long quota = hybridswap_quota_day();

	if (hybridswap_dev_life())
		quota /= 10;
	return atomic64_read(&stat->reclaimin_bytes_daily) > quota;
}

static void hybridswap_life_protect_ctrl_work(struct work_struct *work)
{
	struct tm tm;
	struct timespec64 ts;
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();

	ktime_get_real_ts64(&ts);
	time64_to_tm(ts.tv_sec - sys_tz.tz_minuteswest * 60, 0, &tm);

	if (tm.tm_hour > 2)
		atomic64_set(&stat->reclaimin_bytes_daily, 0);
}

static void hybridswap_life_protect_ctrl_timer(struct timer_list *t)
{
	schedule_work(&global_settings.lpc_work);
	mod_timer(&global_settings.lpc_timer,
		  jiffies + HYBRIDSWAP_CHECK_INTERVAL * HZ);
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
		hybp(HYB_ERR, "%s blkdev_get failed!\n", file_name);
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

static inline unsigned long get_original_used_swap(void)
{
	struct sysinfo val;

	si_swapinfo(&val);

	return val.totalswap - val.freeswap;
}

void hybridswap_stat_init(struct hybridswap_stat *stat)
{
	int i;

	atomic64_set(&stat->reclaimin_cnt, 0);
	atomic64_set(&stat->reclaimin_bytes, 0);
	atomic64_set(&stat->reclaimin_real_load, 0);
	atomic64_set(&stat->dropped_ext_size, 0);
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
	atomic64_set(&stat->ext_cnt, 0);
	atomic64_set(&stat->miss_free, 0);
	atomic64_set(&stat->mcgid_clear, 0);
	atomic64_set(&stat->skip_track_cnt, 0);
	atomic64_set(&stat->null_memcg_skip_track_cnt, 0);
	atomic64_set(&stat->used_swap_pages, get_original_used_swap());
	atomic64_set(&stat->stored_wm_ratio, DEFAULT_STORED_WM_RATIO);

	for (i = 0; i < HYBRIDSWAP_SCENARIO_BUTT; ++i) {
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
			sizeof(struct hybridswap_stat), false, true);
	if (unlikely(!global_settings.stat)) {
		hybp(HYB_ERR, "global stat allocation failed!\n");
		return false;
	}

	hybridswap_stat_init(global_settings.stat);
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
	global_settings.lpc_timer.expires = jiffies + HYBRIDSWAP_CHECK_INTERVAL * HZ;
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

struct workqueue_struct *hybridswap_get_reclaim_workqueue(void)
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

	ret = hybridswap_schedule_init();
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
	int len = snprintf(buf, PAGE_SIZE, "hybridswap %s reclaim_in %s\n",
			hybridswap_core_enabled() ? "enable" : "disable",
			hybridswap_reclaim_in_enable() ? "enable" : "disable");

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

int mem_cgroup_stored_wm_ratio_write(
		struct cgroup_subsys_state *css, struct cftype *cft, s64 val)
{
	if (val > MAX_RATIO || val < MIN_RATIO)
		return -EINVAL;

	if (!global_settings.stat)
		return -EINVAL;

	atomic64_set(&global_settings.stat->stored_wm_ratio, val);

	return 0;
}

s64 mem_cgroup_stored_wm_ratio_read(
		struct cgroup_subsys_state *css, struct cftype *cft)
{
	if (!global_settings.stat)
		return -EINVAL;

	return atomic64_read(&global_settings.stat->stored_wm_ratio);
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

	*used = atomic64_read(&global_settings.stat->ext_cnt) * EXTENT_PG_CNT;
	*total = global_settings.zram->nr_pages;

	return 0;
}

bool hybridswap_stored_wm_ok(void)
{
	unsigned long ratio, stored_pages, total_pages, wm_ratio;
	int ret;

	if (!hybridswap_core_enabled())
		return false;

	ret = hybridswap_stored_info(&total_pages, &stored_pages);
	if (ret)
		return false;

	ratio = (stored_pages * 100) / (total_pages + 1);
	wm_ratio = atomic64_read(&global_settings.stat->stored_wm_ratio);

	return ratio <= wm_ratio;
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
