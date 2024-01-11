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

#include "../zram_drv.h"
#include "../zram_drv_internal.h"
#include "hybridswap_area.h"
#include "hybridswap_list.h"
#include "hybridswap_internal.h"
#include "hybridswap.h"

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
	enum hybridswap_scenario scenario;
	struct hybridswap_page_pool page_pool;
};

struct schedule_para {
	void *io_handler;
	struct hybridswap_entry *io_entry;
	struct hybridswap_buffer io_buf;
	struct io_priv priv;
	struct hybridswap_key_point_record record;
};

#define MIN_RECLAIM_ZRAM_SZ	(1024 * 1024)

static void hybridswap_memcg_iter(
		int (*iter)(struct mem_cgroup *, void *), void *data)
{
	struct mem_cgroup *mcg = get_next_memcg(NULL);
	int ret;

	while (mcg) {
		ret = iter(mcg, data);
		hybp(HYB_DEBUG, "%pS mcg %d %s %s, ret %d\n",
					iter, mcg->id.id,
					MEMCGRP_ITEM(mcg, name),
					ret ? "failed" : "pass",
					ret);
		if (ret) {
			get_next_memcg_break(mcg);
			return;
		}
		mcg = get_next_memcg(mcg);
	}
}

/*
 * This interface will be called when anon page is added to ZRAM.
 * Hybridswap should trace this ZRAM in memcg LRU list.
 */
void hybridswap_track(struct zram *zram, u32 index,
				struct mem_cgroup *memcg)
{
	memcg_hybs_t *hybs;
	struct hybridswap_stat *stat;

	if (!hybridswap_core_enabled())
		return;

	if (!memcg || !memcg->id.id) {
		stat = hybridswap_get_stat_obj();
		if (stat)
			atomic64_inc(&stat->null_memcg_skip_track_cnt);
		return;
	}

	hybs = MEMCGRP_ITEM_DATA(memcg);
	if (!hybs) {
		hybs = hybridswap_cache_alloc(memcg, false);
		if (!hybs) {
			stat = hybridswap_get_stat_obj();
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

	hybridswap_zram_lru_add(zram, index, memcg);

#ifdef CONFIG_HYBRIDSWAP_SWAPD
	zram_slot_unlock(zram, index);
	if (!zram_watermark_ok())
		wake_all_swapd();
	zram_slot_lock(zram, index);
#endif
}

void hybridswap_untrack(struct zram *zram, u32 index)
{
	/*
	 * 1. When the ZS object in the writeback or swapin,
	 * it can't be untrack.
	 * 2. Updata the stored size in memcg and ZRAM.
	 * 3. Remove ZRAM obj from LRU.
	 *
	 */
	if (!hybridswap_core_enabled())
		return;

	while (zram_test_flag(zram, index, ZRAM_UNDER_WB) ||
			zram_test_flag(zram, index, ZRAM_BATCHING_OUT)) {
		zram_slot_unlock(zram, index);
		udelay(50);
		zram_slot_lock(zram, index);
	}

	hybridswap_zram_lru_del(zram, index);
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
			atomic_read(&hybs->ub_zram2ufs_ratio) / 100;

	hybs->can_eswaped = (new_size > cur_size) ? (new_size - cur_size) : 0;
	return hybs->can_eswaped;
}

static int hybridswap_permcg_sz(struct mem_cgroup *memcg, void *data)
{
	unsigned long *out_size = (unsigned long *)data;

	*out_size += memcg_reclaim_size(memcg);
	return 0;
}

static void hybridswap_flush_cb(enum hybridswap_scenario scenario,
		void *pri, struct hybridswap_io_req *req)
{
	switch (scenario) {
	case HYBRIDSWAP_FAULT_OUT:
	case HYBRIDSWAP_PRE_OUT:
	case HYBRIDSWAP_BATCH_OUT:
		hybridswap_extent_destroy(pri, scenario);
		break;
	case HYBRIDSWAP_RECLAIM_IN:
		hybridswap_extent_register(pri, req);
		break;
	default:
		break;
	}
}

static void hybridswap_flush_done(struct hybridswap_entry *io_entry,
		int err, struct hybridswap_io_req *req)
{
	struct io_priv *priv;

	if (unlikely(!io_entry))
		return;

	priv = (struct io_priv *)(io_entry->private);
	if (likely(!err)) {
		hybridswap_flush_cb(priv->scenario,
				io_entry->manager_private, req);
#ifdef CONFIG_HYBRIDSWAP_SWAPD
		if (!zram_watermark_ok())
			wake_all_swapd();
#endif
	} else {
		hybridswap_extent_exception(priv->scenario,
				io_entry->manager_private);
	}
	hybridswap_free(io_entry);
}

static void hybridswap_free_pagepool(struct schedule_para *sched)
{
	struct page *free_page = NULL;

	spin_lock(&sched->priv.page_pool.page_pool_lock);
	while (!list_empty(&sched->priv.page_pool.page_pool_list)) {
		free_page = list_first_entry(
				&sched->priv.page_pool.page_pool_list,
				struct page, lru);
		list_del_init(&free_page->lru);
		__free_page(free_page);
	}
	spin_unlock(&sched->priv.page_pool.page_pool_lock);
}

static void hybridswap_plug_complete(void *data)
{
	struct schedule_para *sched  = (struct schedule_para *)data;

	hybridswap_free_pagepool(sched);

	hybridswap_perf_end(&sched->record);

	hybridswap_free(sched);
}

static void *hybridswap_init_plug(struct zram *zram,
		enum hybridswap_scenario scenario,
		struct schedule_para *sched)
{
	struct hybridswap_io io_para;

	io_para.bdev = zram->bdev;
	io_para.scenario = scenario;
	io_para.private = (void *)sched;
	io_para.record = &sched->record;
	INIT_LIST_HEAD(&sched->priv.page_pool.page_pool_list);
	spin_lock_init(&sched->priv.page_pool.page_pool_lock);
	io_para.done_callback = hybridswap_flush_done;
	switch (io_para.scenario) {
	case HYBRIDSWAP_RECLAIM_IN:
		io_para.complete_notify = hybridswap_plug_complete;
		sched->io_buf.pool = NULL;
		break;
	case HYBRIDSWAP_BATCH_OUT:
	case HYBRIDSWAP_PRE_OUT:
		io_para.complete_notify = hybridswap_plug_complete;
		sched->io_buf.pool = &sched->priv.page_pool;
		break;
	case HYBRIDSWAP_FAULT_OUT:
		io_para.complete_notify = NULL;
		sched->io_buf.pool = NULL;
		break;
	default:
		break;
	}
	sched->io_buf.zram = zram;
	sched->priv.zram = zram;
	sched->priv.scenario = io_para.scenario;
	return hybridswap_plug_start(&io_para);
}

static void hybridswap_fill_entry(struct hybridswap_entry *io_entry,
		struct hybridswap_buffer *io_buf,
		void *private)
{
	io_entry->addr = io_entry->ext_id * EXTENT_SECTOR_SIZE;
	io_entry->dest_pages = io_buf->dest_pages;
	io_entry->pages_sz = EXTENT_PG_CNT;
	io_entry->private = private;
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

static void hybridswap_stat_alloc_fail(enum hybridswap_scenario scenario,
		int err)
{
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();

	if (!stat || (err != -ENOMEM) || (scenario >= HYBRIDSWAP_SCENARIO_BUTT))
		return;

	atomic64_inc(&stat->alloc_fail_cnt[scenario]);
}

static int hybridswap_reclaim_extent(struct mem_cgroup *memcg,
		struct schedule_para *sched,
		unsigned long *require_size,
		unsigned long *mcg_reclaimed_sz,
		int *io_err)
{
	int ret;
	unsigned long reclaim_size;

	hybridswap_perf_lat_start(&sched->record, HYBRIDSWAP_IOENTRY_ALLOC);
	sched->io_entry = hybridswap_malloc(
			sizeof(struct hybridswap_entry), false, true);
	hybridswap_perf_lat_end(&sched->record, HYBRIDSWAP_IOENTRY_ALLOC);
	if (unlikely(!sched->io_entry)) {
		hybp(HYB_ERR, "alloc io entry failed!\n");
		*require_size = 0;
		*io_err = -ENOMEM;
		hybridswap_stat_alloc_fail(HYBRIDSWAP_RECLAIM_IN, -ENOMEM);

		return *io_err;
	}

	hybridswap_perf_lat_start(&sched->record, HYBRIDSWAP_FIND_EXTENT);
	reclaim_size = hybridswap_extent_create(
			memcg, &sched->io_entry->ext_id,
			&sched->io_buf, &sched->io_entry->manager_private);
	hybridswap_perf_lat_end(&sched->record, HYBRIDSWAP_FIND_EXTENT);
	if (unlikely(!reclaim_size)) {
		if (sched->io_entry->ext_id != -ENOENT)
			*require_size = 0;
		hybridswap_free(sched->io_entry);
		return -EAGAIN;
	}

	hybridswap_fill_entry(sched->io_entry, &sched->io_buf,
			(void *)(&sched->priv));

	hybridswap_perf_lat_start(&sched->record, HYBRIDSWAP_IO_EXTENT);
	ret = hybridswap_write_extent(sched->io_handler, sched->io_entry);
	hybridswap_perf_lat_end(&sched->record, HYBRIDSWAP_IO_EXTENT);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "hybridswap write failed! %d\n", ret);
		*require_size = 0;
		*io_err = ret;
		hybridswap_stat_alloc_fail(HYBRIDSWAP_RECLAIM_IN, ret);

		return *io_err;
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
	int io_err = 0;
	unsigned long require_size_before = 0;
	struct schedule_para *sched = NULL;
	ktime_t start = ktime_get();
	unsigned long long start_ravg_sum = hybridswap_get_ravg_sum();
	memcg_hybs_t *hybs = MEMCGRP_ITEM_DATA(memcg);

	ret = hybridswap_reclaim_check(memcg, &require_size);
	if (ret)
		return ret == -EAGAIN ? 0 : ret;

	sched = hybridswap_malloc(sizeof(struct schedule_para), false, true);
	if (unlikely(!sched)) {
		hybp(HYB_ERR, "alloc sched failed!\n");
		hybridswap_stat_alloc_fail(HYBRIDSWAP_RECLAIM_IN, -ENOMEM);

		return -ENOMEM;
	}

	hybridswap_perf_start(&sched->record, start, start_ravg_sum,
			HYBRIDSWAP_RECLAIM_IN);
	hybridswap_perf_lat_start(&sched->record, HYBRIDSWAP_INIT);
	sched->io_handler = hybridswap_init_plug(hybs->zram,
			HYBRIDSWAP_RECLAIM_IN, sched);
	hybridswap_perf_lat_end(&sched->record, HYBRIDSWAP_INIT);
	if (unlikely(!sched->io_handler)) {
		hybp(HYB_ERR, "plug start failed!\n");
		hybridswap_perf_end(&sched->record);
		hybridswap_free(sched);
		hybridswap_stat_alloc_fail(HYBRIDSWAP_RECLAIM_IN, -ENOMEM);
		ret = -EIO;
		goto out;
	}

	require_size_before = require_size;
	while (require_size) {
		if (hybridswap_reclaim_extent(memcg, sched,
					&require_size, mcg_reclaimed_sz, &io_err))
			break;

		atomic64_inc(&hybs->hybridswap_outextcnt);
		extcnt = atomic_inc_return(&hybs->hybridswap_extcnt);
		if (extcnt > atomic_read(&hybs->hybridswap_peakextcnt))
			atomic_set(&hybs->hybridswap_peakextcnt, extcnt);
	}

	ret = hybridswap_plug_finish(sched->io_handler);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "hybridswap write flush failed! %d\n", ret);
		hybridswap_stat_alloc_fail(HYBRIDSWAP_RECLAIM_IN, ret);
		require_size = 0;
	} else {
		ret = io_err;
	}
	atomic64_inc(&hybs->hybridswap_outcnt);

out:
	hybp(HYB_INFO, "memcg %s %lu %lu reclaim_in %lu KB eswap %lu zram %lu %d\n",
		hybs->name, require_size_before, require_size,
		(require_size_before - require_size) >> 10,
		atomic64_read(&hybs->hybridswap_stored_size),
		atomic64_read(&hybs->zram_stored_size), ret);
	return ret;
}

static void hybridswap_reclaimin_inc(void)
{
	struct hybridswap_stat *stat;

	stat = hybridswap_get_stat_obj();
	if (unlikely(!stat))
		return;
	atomic64_inc(&stat->reclaimin_infight);
}

static void hybridswap_reclaimin_dec(void)
{
	struct hybridswap_stat *stat;

	stat = hybridswap_get_stat_obj();
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

unsigned long hybridswap_reclaim_in(unsigned long size)
{
	struct async_req *rq = NULL;
	unsigned long out_size = 0;

	if (!hybridswap_core_enabled() || !hybridswap_reclaim_in_enable()
	    || hybridswap_reach_life_protect() || !size)
		return 0;

	hybridswap_memcg_iter(hybridswap_permcg_sz, &out_size);
	if (!out_size)
		return 0;

	rq = hybridswap_malloc(sizeof(struct async_req), false, true);
	if (unlikely(!rq)) {
		hybp(HYB_ERR, "alloc async req fail!\n");
		hybridswap_stat_alloc_fail(HYBRIDSWAP_RECLAIM_IN, -ENOMEM);
		return 0;
	}

	if (out_size < size)
		size = out_size;
	rq->size = size;
	rq->out_size = out_size;
	rq->reclaimined_sz = 0;
	rq->nice = task_nice(current);
	INIT_WORK(&rq->work, hybridswap_reclaim_work);
	queue_work(hybridswap_get_reclaim_workqueue(), &rq->work);

	return out_size > size ? size : out_size;
}

static int hybridswap_batch_out_extent(struct schedule_para *sched,
		struct mem_cgroup *mcg,
		bool preload,
		int *io_err)
{
	int ret;

	hybridswap_perf_lat_start(&sched->record, HYBRIDSWAP_IOENTRY_ALLOC);
	sched->io_entry = hybridswap_malloc(
			sizeof(struct hybridswap_entry), !preload, preload);
	hybridswap_perf_lat_end(&sched->record, HYBRIDSWAP_IOENTRY_ALLOC);
	if (unlikely(!sched->io_entry)) {
		hybp(HYB_ERR, "alloc io entry failed!\n");
		*io_err = -ENOMEM;
		hybridswap_stat_alloc_fail(HYBRIDSWAP_BATCH_OUT, -ENOMEM);

		return *io_err;
	}

	hybridswap_perf_lat_start(&sched->record, HYBRIDSWAP_FIND_EXTENT);
	sched->io_entry->ext_id = hybridswap_find_extent_by_memcg(
			mcg, &sched->io_buf,
			&sched->io_entry->manager_private);
	hybridswap_perf_lat_end(&sched->record, HYBRIDSWAP_FIND_EXTENT);
	if (sched->io_entry->ext_id < 0) {
		hybridswap_stat_alloc_fail(HYBRIDSWAP_BATCH_OUT,
				sched->io_entry->ext_id);
		hybridswap_free(sched->io_entry);
		return -EAGAIN;
	}

	hybridswap_fill_entry(sched->io_entry, &sched->io_buf,
			(void *)(&sched->priv));

	hybridswap_perf_lat_start(&sched->record, HYBRIDSWAP_IO_EXTENT);
	ret = hybridswap_read_extent(sched->io_handler, sched->io_entry);
	hybridswap_perf_lat_end(&sched->record, HYBRIDSWAP_IO_EXTENT);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "hybridswap read failed! %d\n", ret);
		hybridswap_stat_alloc_fail(HYBRIDSWAP_BATCH_OUT, ret);
		*io_err = ret;

		return *io_err;
	}

	return 0;
}

static int hybridswap_do_batch_out_init(struct schedule_para **out_sched,
		struct mem_cgroup *mcg, bool preload)
{
	struct schedule_para *sched = NULL;
	ktime_t start = ktime_get();
	unsigned long long start_ravg_sum = hybridswap_get_ravg_sum();

	sched = hybridswap_malloc(sizeof(struct schedule_para),
			!preload, preload);
	if (unlikely(!sched)) {
		hybp(HYB_ERR, "alloc sched failed!\n");
		hybridswap_stat_alloc_fail(HYBRIDSWAP_BATCH_OUT, -ENOMEM);

		return -ENOMEM;
	}

	hybridswap_perf_start(&sched->record, start, start_ravg_sum,
			preload ? HYBRIDSWAP_PRE_OUT : HYBRIDSWAP_BATCH_OUT);

	hybridswap_perf_lat_start(&sched->record, HYBRIDSWAP_INIT);
	sched->io_handler = hybridswap_init_plug(MEMCGRP_ITEM(mcg, zram),
			preload ? HYBRIDSWAP_PRE_OUT : HYBRIDSWAP_BATCH_OUT,
			sched);
	hybridswap_perf_lat_end(&sched->record, HYBRIDSWAP_INIT);
	if (unlikely(!sched->io_handler)) {
		hybp(HYB_ERR, "plug start failed!\n");
		hybridswap_perf_end(&sched->record);
		hybridswap_free(sched);
		hybridswap_stat_alloc_fail(HYBRIDSWAP_BATCH_OUT, -ENOMEM);

		return -EIO;
	}

	*out_sched = sched;

	return 0;
}

static int hybridswap_do_batch_out(struct mem_cgroup *mcg,
		unsigned long size, bool preload)
{
	int ret = 0;
	int io_err = 0;
	struct schedule_para *sched = NULL;

	if (unlikely(!mcg || !MEMCGRP_ITEM(mcg, zram))) {
		hybp(HYB_WARN, "no zram in mcg!\n");
		ret = -EINVAL;
		goto out;
	}

	ret = hybridswap_do_batch_out_init(&sched, mcg, preload);
	if (unlikely(ret))
		goto out;

	MEMCGRP_ITEM(mcg, in_swapin) = true;
	while (size) {
		if (hybridswap_batch_out_extent(sched, mcg, preload, &io_err))
			break;
		size -= EXTENT_SIZE;
	}

	ret = hybridswap_plug_finish(sched->io_handler);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "hybridswap read flush failed! %d\n", ret);
		hybridswap_stat_alloc_fail(HYBRIDSWAP_BATCH_OUT, ret);
	} else {
		ret = io_err;
	}

	if (atomic64_read(&MEMCGRP_ITEM(mcg, hybridswap_stored_size)) &&
			hybridswap_loglevel() >= HYB_INFO)
		hybridswap_check_area_extent((MEMCGRP_ITEM(mcg, zram)->area));

	atomic64_inc(&MEMCGRP_ITEM(mcg, hybridswap_incnt));
	MEMCGRP_ITEM(mcg, in_swapin) = false;
out:
	return ret;
}

static void hybridswap_batchout_inc(void)
{
	struct hybridswap_stat *stat;

	stat = hybridswap_get_stat_obj();
	if (unlikely(!stat))
		return;
	atomic64_inc(&stat->batchout_inflight);
}

static void hybridswap_batchout_dec(void)
{
	struct hybridswap_stat *stat;

	stat = hybridswap_get_stat_obj();
	if (unlikely(!stat))
		return;
	atomic64_dec(&stat->batchout_inflight);
}

int hybridswap_batch_out(struct mem_cgroup *mcg,
		unsigned long size, bool preload)
{
	int ret;

	if (!hybridswap_core_enabled())
		return 0;

	hybridswap_batchout_inc();
	ret = hybridswap_do_batch_out(mcg, size, preload);
	hybridswap_batchout_dec();

	return ret;
}

static void hybridswap_fault_stat(struct zram *zram, u32 index)
{
	struct mem_cgroup *mcg = NULL;
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();

	if (unlikely(!stat))
		return;

	atomic64_inc(&stat->fault_cnt);

	mcg = hybridswap_zram_get_memcg(zram, index);
	if (mcg)
		atomic64_inc(&MEMCGRP_ITEM(mcg, hybridswap_allfaultcnt));
}

static void hybridswap_fault2_stat(struct zram *zram, u32 index)
{
	struct mem_cgroup *mcg = NULL;
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();

	if (unlikely(!stat))
		return;

	atomic64_inc(&stat->hybridswap_fault_cnt);

	mcg = hybridswap_zram_get_memcg(zram, index);
	if (mcg)
		atomic64_inc(&MEMCGRP_ITEM(mcg, hybridswap_faultcnt));
}

static bool hybridswap_fault_out_check(struct zram *zram,
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

static int hybridswap_fault_out_get_extent(struct zram *zram,
		struct schedule_para *sched,
		unsigned long zentry,
		u32 index)
{
	int wait_cycle = 0;

	sched->io_buf.zram = zram;
	sched->priv.zram = zram;
	sched->io_buf.pool = NULL;
	hybridswap_perf_lat_start(&sched->record, HYBRIDSWAP_FIND_EXTENT);
	sched->io_entry->ext_id = hybridswap_find_extent_by_idx(zentry,
			&sched->io_buf, &sched->io_entry->manager_private);
	hybridswap_perf_lat_end(&sched->record, HYBRIDSWAP_FIND_EXTENT);
	if (unlikely(sched->io_entry->ext_id == -EBUSY)) {
		/* The extent maybe in unexpected case, wait here */
		while (1) {
			/* The extent doesn't exist in hybridswap */
			zram_slot_lock(zram, index);
			if (!zram_test_flag(zram, index, ZRAM_WB)) {
				zram_slot_unlock(zram, index);
				hybridswap_free(sched->io_entry);
#ifdef CONFIG_HYBRIDSWAP_SWAPD
				if (wait_cycle >= 1000)
					atomic_long_dec(&fault_out_pause);
#endif
				return -EAGAIN;
			}
			zram_slot_unlock(zram, index);

			/* Get extent again */
			hybridswap_perf_lat_start(&sched->record,
					HYBRIDSWAP_FIND_EXTENT);
			sched->io_entry->ext_id =
				hybridswap_find_extent_by_idx(zentry,
						&sched->io_buf,
						&sched->io_entry->manager_private);
			hybridswap_perf_lat_end(&sched->record,
					HYBRIDSWAP_FIND_EXTENT);
			if (likely(sched->io_entry->ext_id != -EBUSY))
				break;

			if (wait_cycle < 100)
				udelay(50);
			else
				usleep_range(50, 100);
			wait_cycle++;
#ifdef CONFIG_HYBRIDSWAP_SWAPD
			if (wait_cycle == 1000) {
				atomic_long_inc(&fault_out_pause);
				atomic_long_inc(&fault_out_pause_cnt);
			}
#endif
		}
	}
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	if (wait_cycle >= 1000)
		atomic_long_dec(&fault_out_pause);
#endif
	if (sched->io_entry->ext_id < 0) {
		hybridswap_stat_alloc_fail(HYBRIDSWAP_FAULT_OUT,
				sched->io_entry->ext_id);

		return sched->io_entry->ext_id;
	}
	hybridswap_fault2_stat(zram, index);
	hybridswap_fill_entry(sched->io_entry, &sched->io_buf,
			(void *)(&sched->priv));
	return 0;
}

static int hybridswap_fault_out_exit_check(struct zram *zram,
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

static int hybridswap_fault_out_extent(struct zram *zram, u32 index,
		struct schedule_para *sched, unsigned long zentry)
{
	int ret;

	hybridswap_perf_lat_start(&sched->record, HYBRIDSWAP_IOENTRY_ALLOC);
	sched->io_entry = hybridswap_malloc(sizeof(struct hybridswap_entry),
			true, true);
	hybridswap_perf_lat_end(&sched->record, HYBRIDSWAP_IOENTRY_ALLOC);
	if (unlikely(!sched->io_entry)) {
		hybp(HYB_ERR, "alloc io entry failed!\n");
		hybridswap_stat_alloc_fail(HYBRIDSWAP_FAULT_OUT, -ENOMEM);
		hybridswap_fail_record(HYBRIDSWAP_FAULT_OUT_ENTRY_ALLOC_FAIL,
				index, 0, sched->record.task_comm);
		return -ENOMEM;
	}

	ret = hybridswap_fault_out_get_extent(zram, sched, zentry, index);
	if (ret)
		return ret;

	hybridswap_perf_lat_start(&sched->record, HYBRIDSWAP_IO_EXTENT);
	ret = hybridswap_read_extent(sched->io_handler, sched->io_entry);
	hybridswap_perf_lat_end(&sched->record, HYBRIDSWAP_IO_EXTENT);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "hybridswap read failed! %d\n", ret);
		hybridswap_stat_alloc_fail(HYBRIDSWAP_FAULT_OUT, ret);
	}

	return ret;
}

/*
 * This interface will be called when ZRAM is read.
 * Hybridswap should be searched before ZRAM is read.
 * This function require ZRAM slot lock being held.
 *
 */
int hybridswap_fault_out(struct zram *zram, u32 index)
{
	/*
	 * 1. Find the extent in ZRAM by the index.
	 * 2. if extent exist, dispatch it to UFS.
	 *
	 */
	int ret = 0;
	int io_err;
	struct schedule_para sched;
	unsigned long zentry;
	ktime_t start = ktime_get();
	unsigned long long start_ravg_sum = hybridswap_get_ravg_sum();

	if (!hybridswap_fault_out_check(zram, index, &zentry))
		return ret;

	memset(&sched.record, 0, sizeof(struct hybridswap_key_point_record));
	hybridswap_perf_start(&sched.record, start, start_ravg_sum,
			HYBRIDSWAP_FAULT_OUT);

	hybridswap_perf_lat_start(&sched.record, HYBRIDSWAP_INIT);
	sched.io_handler = hybridswap_init_plug(zram,
			HYBRIDSWAP_FAULT_OUT, &sched);
	hybridswap_perf_lat_end(&sched.record, HYBRIDSWAP_INIT);
	if (unlikely(!sched.io_handler)) {
		hybp(HYB_ERR, "plug start failed!\n");
		hybridswap_stat_alloc_fail(HYBRIDSWAP_FAULT_OUT, -ENOMEM);
		ret = -EIO;
		hybridswap_fail_record(HYBRIDSWAP_FAULT_OUT_INIT_FAIL,
				index, 0, sched.record.task_comm);

		goto out;
	}

	io_err = hybridswap_fault_out_extent(zram, index, &sched, zentry);
	ret = hybridswap_plug_finish(sched.io_handler);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "hybridswap flush failed! %d\n", ret);
		hybridswap_stat_alloc_fail(HYBRIDSWAP_FAULT_OUT, ret);
	} else {
		ret = (io_err != -EAGAIN) ? io_err : 0;
	}
out:
	hybridswap_perf_lat_start(&sched.record, HYBRIDSWAP_ZRAM_LOCK);
	ret = hybridswap_fault_out_exit_check(zram, index, ret);
	hybridswap_perf_lat_end(&sched.record, HYBRIDSWAP_ZRAM_LOCK);
	hybridswap_perf_end(&sched.record);

	return ret;
}

/*
 * This interface will be called when ZRAM is freed.
 * Hybridswap should be deleted before ZRAM is freed.
 * If obj can be deleted from ZRAM,
 * return true, otherwise return false.
 *
 */
bool hybridswap_delete(struct zram *zram, u32 index)
{
	/*
	 * 1. Find the extent in ZRAM by the index.
	 * 2. Delete the zs Object in the extent.
	 * 3. If extent is empty, free the extent.
	 *
	 */

	if (!hybridswap_core_enabled())
		return true;

	if (zram_test_flag(zram, index, ZRAM_UNDER_WB)
			|| zram_test_flag(zram, index, ZRAM_BATCHING_OUT)) {
		struct hybridswap_stat *stat = hybridswap_get_stat_obj();

		if (stat)
			atomic64_inc(&stat->miss_free);
		return false;
	}

	if (!zram_test_flag(zram, index, ZRAM_WB))
		return true;

	hybridswap_extent_objs_del(zram, index);

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

	if (!hybridswap_core_enabled() || !hybridswap_reclaim_in_enable()
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
