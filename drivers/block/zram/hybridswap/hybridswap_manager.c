// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[HYBRIDSWAP]" fmt

#include <linux/kernel.h>
#include <linux/swap.h>
#include <linux/sched/debug.h>

#include "../zram_drv.h"
#include "../zram_drv_internal.h"
#include "hybridswap.h"

#include "hybridswap_list.h"
#include "hybridswap_area.h"
#include "hybridswap_internal.h"
#include "hybridswap_lru_rmap.h"

#define esentry_extid(e) ((e) >> EXTENT_SHIFT)
#define esentry_pgid(e) (((e) & ((1 << EXTENT_SHIFT) - 1)) >> PAGE_SHIFT)
#define esentry_pgoff(e) ((e) & (PAGE_SIZE - 1))

static struct io_extent *alloc_io_extent(struct hybridswap_page_pool *pool,
				  bool fast, bool nofail)
{
	int i;
	struct io_extent *io_ext = hybridswap_malloc(sizeof(struct io_extent),
						     fast, nofail);

	if (!io_ext) {
		hybp(HYB_ERR, "alloc io_ext failed\n");
		return NULL;
	}

	io_ext->ext_id = -EINVAL;
	io_ext->pool = pool;
	for (i = 0; i < (int)EXTENT_PG_CNT; i++) {
		io_ext->pages[i] = hybridswap_alloc_page(pool, GFP_ATOMIC,
							fast, nofail);
		if (!io_ext->pages[i]) {
			hybp(HYB_ERR, "alloc page[%d] failed\n", i);
			goto page_free;
		}
	}
	return io_ext;
page_free:
	for (i = 0; i < (int)EXTENT_PG_CNT; i++)
		if (io_ext->pages[i])
			hybridswap_page_recycle(io_ext->pages[i], pool);
	hybridswap_free(io_ext);

	return NULL;
}

static void discard_io_extent(struct io_extent *io_ext, unsigned int op)
{
	struct zram *zram = NULL;
	int i;

	if (!io_ext) {
		hybp(HYB_ERR, "NULL io_ext\n");
		return;
	}
	if (!io_ext->mcg)
		zram = io_ext->zram;
	else
		zram = MEMCGRP_ITEM(io_ext->mcg, zram);
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		goto out;
	}
	for (i = 0; i < (int)EXTENT_PG_CNT; i++)
		if (io_ext->pages[i])
			hybridswap_page_recycle(io_ext->pages[i], io_ext->pool);
	if (io_ext->ext_id < 0)
		goto out;
	hybp(HYB_DEBUG, "ext = %d, op = %d\n", io_ext->ext_id, op);
	if (op == REQ_OP_READ) {
		put_extent(zram->area, io_ext->ext_id);
		goto out;
	}
	for (i = 0; i < io_ext->cnt; i++) {
		u32 index = io_ext->index[i];

		zram_slot_lock(zram, index);
		if (io_ext->mcg)
			zram_lru_add_tail(zram, index, io_ext->mcg);
		zram_clear_flag(zram, index, ZRAM_UNDER_WB);
		zram_slot_unlock(zram, index);
	}
	hybridswap_free_extent(zram->area, io_ext->ext_id);
out:
	hybridswap_free(io_ext);
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
	if (pg_id == EXTENT_PG_CNT - 1) {
		hybp(HYB_ERR, "ext overflow, addr = %lx, size = %d\n",
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
	if (pg_id == EXTENT_PG_CNT - 1) {
		hybp(HYB_ERR, "ext overflow, addr = %lx, size = %d\n",
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
	if (mcg != zram_get_memcg(zram, index))
		return true;
	if (!zram_get_obj_size(zram, index))
		return true;

	return false;
}

static bool zram_test_overwrite(struct zram *zram, u32 index, int ext_id)
{
	if (!zram_test_flag(zram, index, ZRAM_WB))
		return true;
	if (ext_id != esentry_extid(zram_get_handle(zram, index)))
		return true;

	return false;
}

static void update_size_info(struct zram *zram, u32 index)
{
	struct hybridswap_stat *stat;
	int size = zram_get_obj_size(zram, index);
	struct mem_cgroup *mcg;
	memcg_hybs_t *hybs;
	int ext_id;

	if (!zram_test_flag(zram, index, ZRAM_IN_BD))
		return;

	ext_id = esentry_extid(zram_get_handle(zram, index));
	hybp(HYB_INFO, "ext_id %d index %d\n", ext_id, index);

	if (ext_id >= 0 && ext_id < zram->area->nr_exts)
		atomic_dec(&zram->area->ext_stored_pages[ext_id]);
	else {
		hybp(HYB_ERR, "ext = %d invalid\n", ext_id);
		ext_id = -1;
	}

	stat = hybridswap_get_stat_obj();
	if (stat) {
		atomic64_add(size, &stat->dropped_ext_size);
		atomic64_sub(size, &stat->stored_size);
		atomic64_dec(&stat->stored_pages);
	} else
		hybp(HYB_ERR, "NULL stat\n");

	mcg = zram_get_memcg(zram, index);
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
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}
	if (index >= (u32)zram->area->nr_objs) {
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

	zram_set_memcg(zram, index, mcg->id.id);
	zram_set_flag(zram, index, ZRAM_IN_BD);
	zram_set_flag(zram, index, ZRAM_WB);
	zram_set_obj_size(zram, index, size);
	if (size == PAGE_SIZE)
		zram_set_flag(zram, index, ZRAM_HUGE);
	zram_set_handle(zram, index, eswpentry);
	zram_rmap_insert(zram, index);

	atomic64_add(size, &stat->stored_size);
	atomic64_add(size, &MEMCGRP_ITEM(mcg, hybridswap_stored_size));
	atomic64_inc(&stat->stored_pages);
	atomic_inc(&zram->area->ext_stored_pages[esentry_extid(eswpentry)]);
	atomic64_inc(&MEMCGRP_ITEM(mcg, hybridswap_stored_pages));
}

static void __move_to_zram(struct zram *zram, u32 index, unsigned long handle,
			struct io_extent *io_ext)
{
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();
	struct mem_cgroup *mcg = io_ext->mcg;
	int size = zram_get_obj_size(zram, index);

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}

	zram_slot_lock(zram, index);
	if (zram_test_overwrite(zram, index, io_ext->ext_id)) {
		zram_slot_unlock(zram, index);
		zs_free(zram->mem_pool, handle);
		return;
	}
	zram_rmap_erase(zram, index);
	zram_set_handle(zram, index, handle);
	zram_clear_flag(zram, index, ZRAM_WB);
	if (mcg)
		zram_lru_add_tail(zram, index, mcg);
	zram_set_flag(zram, index, ZRAM_FROM_HYBRIDSWAP);
	atomic64_add(size, &zram->stats.compr_data_size);
	atomic64_inc(&zram->stats.pages_stored);
	zram_clear_flag(zram, index, ZRAM_IN_BD);
	zram_slot_unlock(zram, index);

	atomic64_inc(&stat->batchout_pages);
	atomic64_sub(size, &stat->stored_size);
	atomic64_dec(&stat->stored_pages);
	atomic64_add(size, &stat->batchout_real_load);
	atomic_dec(&zram->area->ext_stored_pages[io_ext->ext_id]);
	if (mcg) {
		atomic64_sub(size, &MEMCGRP_ITEM(mcg, hybridswap_stored_size));
		atomic64_dec(&MEMCGRP_ITEM(mcg, hybridswap_stored_pages));
	}
}

static int move_to_zram(struct zram *zram, u32 index, struct io_extent *io_ext)
{
	unsigned long handle, eswpentry;
	struct mem_cgroup *mcg = NULL;
	int size, i;
	u8 *dst = NULL;

	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return -EINVAL;
	}
	if (index >= (u32)zram->area->nr_objs) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return -EINVAL;
	}
	if (!io_ext) {
		hybp(HYB_ERR, "NULL io_ext\n");
		return -EINVAL;
	}

	mcg = io_ext->mcg;
	zram_slot_lock(zram, index);
	eswpentry = zram_get_handle(zram, index);
	if (zram_test_overwrite(zram, index, io_ext->ext_id)) {
		zram_slot_unlock(zram, index);
		return 0;
	}
	size = zram_get_obj_size(zram, index);
	zram_slot_unlock(zram, index);

	for (i = esentry_pgid(eswpentry) - 1; i >= 0 && io_ext->pages[i]; i--) {
		hybridswap_page_recycle(io_ext->pages[i], io_ext->pool);
		io_ext->pages[i] = NULL;
	}
	handle = hybridswap_zsmalloc(zram->mem_pool, size, io_ext->pool);
	if (!handle) {
		hybp(HYB_ERR, "alloc handle failed, size = %d\n", size);
		return -ENOMEM;
	}
	dst = zs_map_object(zram->mem_pool, handle, ZS_MM_WO);
	copy_from_pages(dst, io_ext->pages, eswpentry, size);
	zs_unmap_object(zram->mem_pool, handle);
	__move_to_zram(zram, index, handle, io_ext);

	return 0;
}

static int extent_unlock(struct io_extent *io_ext)
{
	int ext_id;
	struct mem_cgroup *mcg = NULL;
	struct zram *zram = NULL;
	int k;
	unsigned long eswpentry;
	int real_load = 0, size;

	if (!io_ext) {
		hybp(HYB_ERR, "NULL io_ext\n");
		goto out;
	}

	mcg = io_ext->mcg;
	if (!mcg) {
		hybp(HYB_ERR, "NULL mcg\n");
		goto out;
	}
	zram = MEMCGRP_ITEM(mcg, zram);
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		goto out;
	}
	ext_id = io_ext->ext_id;
	if (ext_id < 0)
		goto out;

	ext_id = io_ext->ext_id;
	if (MEMCGRP_ITEM(mcg, in_swapin))
		goto out;
	hybp(HYB_DEBUG, "add ext_id = %d, cnt = %d.\n",
			ext_id, io_ext->cnt);
	eswpentry = ((unsigned long)ext_id) << EXTENT_SHIFT;
	for (k = 0; k < io_ext->cnt; k++)
		zram_slot_lock(zram, io_ext->index[k]);
	for (k = 0; k < io_ext->cnt; k++) {
		move_to_hybridswap(zram, io_ext->index[k], eswpentry, mcg);
		size = zram_get_obj_size(zram, io_ext->index[k]);
		eswpentry += size;
		real_load += size;
	}
	put_extent(zram->area, ext_id);
	io_ext->ext_id = -EINVAL;
	for (k = 0; k < io_ext->cnt; k++)
		zram_slot_unlock(zram, io_ext->index[k]);
	hybp(HYB_DEBUG, "add extent OK.\n");
out:
	discard_io_extent(io_ext, REQ_OP_WRITE);
	if (mcg)
		css_put(&mcg->css);

	return real_load;
}

static void extent_add(struct io_extent *io_ext,
		       enum hybridswap_scenario scenario)
{
	struct mem_cgroup *mcg = NULL;
	struct zram *zram = NULL;
	int ext_id;
	int k;

	if (!io_ext) {
		hybp(HYB_ERR, "NULL io_ext\n");
		return;
	}

	mcg = io_ext->mcg;
	if (!mcg)
		zram = io_ext->zram;
	else
		zram = MEMCGRP_ITEM(mcg, zram);
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		goto out;
	}

	ext_id = io_ext->ext_id;
	if (ext_id < 0)
		goto out;

	io_ext->cnt = zram_rmap_get_extent_index(zram->area,
						 ext_id,
						 io_ext->index);
	hybp(HYB_DEBUG, "ext_id = %d, cnt = %d.\n", ext_id, io_ext->cnt);
	for (k = 0; k < io_ext->cnt; k++) {
		int ret = move_to_zram(zram, io_ext->index[k], io_ext);

		if (ret < 0)
			goto out;
	}
	hybp(HYB_DEBUG, "extent add OK, free ext_id = %d.\n", ext_id);
	hybridswap_free_extent(zram->area, io_ext->ext_id);
	io_ext->ext_id = -EINVAL;
	if (mcg) {
		atomic64_inc(&MEMCGRP_ITEM(mcg, hybridswap_inextcnt));
		atomic_dec(&MEMCGRP_ITEM(mcg, hybridswap_extcnt));
	}
out:
	discard_io_extent(io_ext, REQ_OP_READ);
	if (mcg)
		css_put(&mcg->css);
}

static void extent_clear(struct zram *zram, int ext_id)
{
	int *index = NULL;
	int cnt;
	int k;
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}

	index = kzalloc(sizeof(int) * EXTENT_MAX_OBJ_CNT, GFP_NOIO);
	if (!index)
		index = kzalloc(sizeof(int) * EXTENT_MAX_OBJ_CNT,
				GFP_NOIO | __GFP_NOFAIL);

	cnt = zram_rmap_get_extent_index(zram->area, ext_id, index);

	for (k = 0; k < cnt; k++) {
		zram_slot_lock(zram, index[k]);
		if (zram_test_overwrite(zram, index[k], ext_id)) {
			zram_slot_unlock(zram, index[k]);
			continue;
		}
		zram_set_memcg(zram, index[k], 0);
		zram_set_flag(zram, index[k], ZRAM_MCGID_CLEAR);
		atomic64_inc(&stat->mcgid_clear);
		zram_slot_unlock(zram, index[k]);
	}

	kfree(index);
}

static int shrink_entry(struct zram *zram, u32 index, struct io_extent *io_ext,
		 unsigned long ext_off)
{
	unsigned long handle;
	int size;
	u8 *src = NULL;
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return -EINVAL;
	}
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return -EINVAL;
	}
	if (index >= (u32)zram->area->nr_objs) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return -EINVAL;
	}

	zram_slot_lock(zram, index);
	handle = zram_get_handle(zram, index);
	if (!handle || zram_test_skip(zram, index, io_ext->mcg)) {
		zram_slot_unlock(zram, index);
		return 0;
	}
	size = zram_get_obj_size(zram, index);
	if (ext_off + size > EXTENT_SIZE) {
		zram_slot_unlock(zram, index);
		return -ENOSPC;
	}

	src = zs_map_object(zram->mem_pool, handle, ZS_MM_RO);
	copy_to_pages(src, io_ext->pages, ext_off, size);
	zs_unmap_object(zram->mem_pool, handle);
	io_ext->index[io_ext->cnt++] = index;

	zram_lru_del(zram, index);
	zram_set_flag(zram, index, ZRAM_UNDER_WB);
	if (zram_test_flag(zram, index, ZRAM_FROM_HYBRIDSWAP)) {
		atomic64_inc(&stat->reout_pages);
		atomic64_add(size, &stat->reout_bytes);
	}
	zram_slot_unlock(zram, index);
	atomic64_inc(&stat->reclaimin_pages);

	return size;
}

static int shrink_entry_list(struct io_extent *io_ext)
{
	struct mem_cgroup *mcg = NULL;
	struct zram *zram = NULL;
	unsigned long stored_size;
	int *swap_index = NULL;
	int swap_cnt, k;
	int swap_size = 0;

	if (!io_ext) {
		hybp(HYB_ERR, "NULL io_ext\n");
		return -EINVAL;
	}

	mcg = io_ext->mcg;
	zram = MEMCGRP_ITEM(mcg, zram);
	hybp(HYB_DEBUG, "mcg = %d\n", mcg->id.id);
	stored_size = atomic64_read(&MEMCGRP_ITEM(mcg, zram_stored_size));
	hybp(HYB_DEBUG, "zram_stored_size = %ld\n", stored_size);
	if (stored_size < EXTENT_SIZE) {
		hybp(HYB_INFO, "%lu is smaller than EXTENT_SIZE\n", stored_size);
		return -ENOENT;
	}

	swap_index = kzalloc(sizeof(int) * EXTENT_MAX_OBJ_CNT, GFP_NOIO);
	if (!swap_index)
		return -ENOMEM;
	io_ext->ext_id = hybridswap_alloc_extent(zram->area, mcg);
	if (io_ext->ext_id < 0) {
		kfree(swap_index);
		return io_ext->ext_id;
	}
	swap_cnt = zram_get_memcg_coldest_index(zram->area, mcg, swap_index,
						EXTENT_MAX_OBJ_CNT);
	io_ext->cnt = 0;
	for (k = 0; k < swap_cnt && swap_size < (int)EXTENT_SIZE; k++) {
		int size = shrink_entry(zram, swap_index[k], io_ext, swap_size);

		if (size < 0)
			break;
		swap_size += size;
	}
	kfree(swap_index);
	hybp(HYB_DEBUG, "fill extent = %d, cnt = %d, overhead = %ld.\n",
		 io_ext->ext_id, io_ext->cnt, EXTENT_SIZE - swap_size);
	if (swap_size == 0) {
		hybp(HYB_ERR, "swap_size = 0, free ext_id = %d.\n",
			io_ext->ext_id);
		hybridswap_free_extent(zram->area, io_ext->ext_id);
		io_ext->ext_id = -EINVAL;
		return -ENOENT;
	}

	return swap_size;
}

/*
 * The function is used to initialize global settings
 */
void hybridswap_manager_deinit(struct zram *zram)
{
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}

	free_hybridswap_area(zram->area);
	zram->area = NULL;
}

int hybridswap_manager_init(struct zram *zram)
{
	int ret;

	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		ret = -EINVAL;
		goto out;
	}

	zram->area = alloc_hybridswap_area(zram->disksize,
					  zram->nr_pages << PAGE_SHIFT);
	if (!zram->area) {
		ret = -ENOMEM;
		goto out;
	}
	return 0;
out:
	hybridswap_manager_deinit(zram);

	return ret;
}

/*
 * The function is used to initialize private member in memcg
 */
void hybridswap_manager_memcg_init(struct zram *zram,
						struct mem_cgroup *memcg)
{
	memcg_hybs_t *hybs;

	if (!memcg || !zram || !zram->area) {
		hybp(HYB_ERR, "invalid zram or mcg_hyb\n");
		return;
	}

	hyb_list_init(mcg_idx(zram->area, memcg->id.id), zram->area->obj_table);
	hyb_list_init(mcg_idx(zram->area, memcg->id.id), zram->area->ext_table);

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
	struct hybridswap_area *area = NULL;
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();
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
	if (!zram->area) {
		hybp(HYB_WARN, "mcg %p name %s id %d zram %p area is NULL\n",
			mcg, hybs->name,   mcg->id.id, zram);
		return;
	}

	hybp(HYB_DEBUG, "deinit mcg %d %s\n", mcg->id.id, hybs->name);
	if (mcg->id.id == 0)
		return;

	area = zram->area;
	while (1) {
		int index = get_memcg_zram_entry(area, mcg);

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
		if (index == last_index || mcg == zram_get_memcg(zram, index)) {
			hyb_list_del(obj_idx(zram->area, index),
					mcg_idx(zram->area, mcg->id.id),
					zram->area->obj_table);
			zram_set_memcg(zram, index, 0);
			zram_set_flag(zram, index, ZRAM_MCGID_CLEAR);
			atomic64_inc(&stat->mcgid_clear);
		}
		zram_slot_unlock(zram, index);
		last_index = index;
	}

	hybp(HYB_DEBUG, "deinit mcg %d %s, entry done\n", mcg->id.id, hybs->name);
	while (1) {
		int ext_id = get_memcg_extent(area, mcg);

		if (ext_id == -ENOENT)
			break;

		extent_clear(zram, ext_id);
		hyb_list_set_mcgid(ext_idx(area, ext_id), area->ext_table, 0);
		put_extent(area, ext_id);
	}
	hybp(HYB_DEBUG, "deinit mcg %d %s, extent done\n", mcg->id.id, hybs->name);
	hybs->zram = NULL;
}
/*
 * The function is used to add ZS object to ZRAM LRU list
 */
void hybridswap_zram_lru_add(struct zram *zram,
			    u32 index, struct mem_cgroup *memcg)
{
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}
	if (index >= (u32)zram->area->nr_objs) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return;
	}

	zram_lru_add(zram, index, memcg);
}

/*
 * The function is used to del ZS object from ZRAM LRU list
 */
void hybridswap_zram_lru_del(struct zram *zram, u32 index)
{
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}
	if (index >= (u32)zram->area->nr_objs) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return;
	}

	zram_clear_flag(zram, index, ZRAM_FROM_HYBRIDSWAP);
	if (zram_test_flag(zram, index, ZRAM_MCGID_CLEAR)) {
		zram_clear_flag(zram, index, ZRAM_MCGID_CLEAR);
		atomic64_dec(&stat->mcgid_clear);
	}

	if (zram_test_flag(zram, index, ZRAM_WB)) {
		update_size_info(zram, index);
		zram_rmap_erase(zram, index);
		zram_clear_flag(zram, index, ZRAM_WB);
		zram_set_memcg(zram, index, 0);
		zram_set_handle(zram, index, 0);
	} else {
		zram_lru_del(zram, index);
	}
}

/*
 * The function is used to alloc a new extent,
 * then reclaim ZRAM by LRU list to the new extent.
 */
unsigned long hybridswap_extent_create(struct mem_cgroup *mcg,
				      int *ext_id,
				      struct hybridswap_buffer *buf,
				      void **private)
{
	struct io_extent *io_ext = NULL;
	int reclaim_size;

	if (!mcg) {
		hybp(HYB_ERR, "NULL mcg\n");
		return 0;
	}
	if (!ext_id) {
		hybp(HYB_ERR, "NULL ext_id\n");
		return 0;
	}
	(*ext_id) = -EINVAL;
	if (!buf) {
		hybp(HYB_ERR, "NULL buf\n");
		return 0;
	}
	if (!private) {
		hybp(HYB_ERR, "NULL private\n");
		return 0;
	}

	io_ext = alloc_io_extent(buf->pool, false, true);
	if (!io_ext)
		return 0;
	io_ext->mcg = mcg;
	reclaim_size = shrink_entry_list(io_ext);
	if (reclaim_size < 0) {
		discard_io_extent(io_ext, REQ_OP_WRITE);
		(*ext_id) = reclaim_size;
		return 0;
	}
	io_ext->real_load = reclaim_size;
	css_get(&mcg->css);
	(*ext_id) = io_ext->ext_id;
	buf->dest_pages = io_ext->pages;
	(*private) = io_ext;
	hybp(HYB_DEBUG, "mcg = %d, ext_id = %d\n", mcg->id.id, io_ext->ext_id);

	return reclaim_size;
}

/*
 * The function will be called when hybridswap write done.
 * The function should register extent to hybridswap manager.
 */
void hybridswap_extent_register(void *private, struct hybridswap_io_req *req)
{
	struct io_extent *io_ext = private;

	if (!io_ext) {
		hybp(HYB_ERR, "NULL io_ext\n");
		return;
	}
	hybp(HYB_DEBUG, "ext_id = %d\n", io_ext->ext_id);
	atomic64_add(extent_unlock(io_ext), &req->real_load);
}

/*
 * If extent empty, return false, otherise return true.
 */
void hybridswap_extent_objs_del(struct zram *zram, u32 index)
{
	int ext_id;
	struct mem_cgroup *mcg = NULL;
	unsigned long eswpentry;
	int size;
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}
	if (!zram || !zram->area) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}
	if (index >= (u32)zram->area->nr_objs) {
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
	atomic64_add(size, &stat->dropped_ext_size);
	mcg = zram_get_memcg(zram, index);
	if (mcg) {
		atomic64_sub(size, &MEMCGRP_ITEM(mcg, hybridswap_stored_size));
		atomic64_dec(&MEMCGRP_ITEM(mcg, hybridswap_stored_pages));
	}

	zram_clear_flag(zram, index, ZRAM_IN_BD);
	if (!atomic_dec_and_test(
			&zram->area->ext_stored_pages[esentry_extid(eswpentry)]))
		return;
	ext_id = get_extent(zram->area, esentry_extid(eswpentry));
	if (ext_id < 0)
		return;

	atomic64_inc(&stat->notify_free);
	if (mcg)
		atomic64_inc(&MEMCGRP_ITEM(mcg, hybridswap_ext_notify_free));
	hybp(HYB_DEBUG, "free ext_id = %d\n", ext_id);
	hybridswap_free_extent(zram->area, ext_id);
}

/*
 * The function will be called when hybridswap execute fault out.
 */
int hybridswap_find_extent_by_idx(unsigned long eswpentry,
				 struct hybridswap_buffer *buf,
				 void **private)
{
	int ext_id;
	struct io_extent *io_ext = NULL;
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
	ext_id = get_extent(zram->area, esentry_extid(eswpentry));
	if (ext_id < 0)
		return ext_id;
	io_ext = alloc_io_extent(buf->pool, true, true);
	if (!io_ext) {
		hybp(HYB_ERR, "io_ext alloc failed\n");
		put_extent(zram->area, ext_id);
		return -ENOMEM;
	}

	io_ext->ext_id = ext_id;
	io_ext->zram = zram;
	io_ext->mcg = get_mem_cgroup(
				hyb_list_get_mcgid(ext_idx(zram->area, ext_id),
						  zram->area->ext_table));
	if (io_ext->mcg)
		css_get(&io_ext->mcg->css);
	buf->dest_pages = io_ext->pages;
	(*private) = io_ext;
	hybp(HYB_DEBUG, "get entry = %lx ext = %d\n", eswpentry, ext_id);

	return ext_id;
}

/*
 * The function will be called when hybridswap executes batch out.
 */
int hybridswap_find_extent_by_memcg(struct mem_cgroup *mcg,
		struct hybridswap_buffer *buf,
		void **private)
{
	int ext_id;
	struct io_extent *io_ext = NULL;

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

	ext_id = get_memcg_extent(MEMCGRP_ITEM(mcg, zram)->area, mcg);
	if (ext_id < 0)
		return ext_id;
	io_ext = alloc_io_extent(buf->pool, true, false);
	if (!io_ext) {
		hybp(HYB_ERR, "io_ext alloc failed\n");
		put_extent(MEMCGRP_ITEM(mcg, zram)->area, ext_id);
		return -ENOMEM;
	}
	io_ext->ext_id = ext_id;
	io_ext->mcg = mcg;
	css_get(&mcg->css);
	buf->dest_pages = io_ext->pages;
	(*private) = io_ext;
	hybp(HYB_DEBUG, "get mcg = %d, ext = %d\n", mcg->id.id, ext_id);

	return ext_id;
}

/*
 * The function will be called when hybridswap read done.
 * The function should extra the extent to ZRAM, then destroy
 */
void hybridswap_extent_destroy(void *private, enum hybridswap_scenario scenario)
{
	struct io_extent *io_ext = private;

	if (!io_ext) {
		hybp(HYB_ERR, "NULL io_ext\n");
		return;
	}

	hybp(HYB_DEBUG, "ext_id = %d\n", io_ext->ext_id);
	extent_add(io_ext, scenario);
}

/*
 * The function will be called
 * when schedule meets exception proceeding extent
 */
void hybridswap_extent_exception(enum hybridswap_scenario scenario,
			       void *private)
{
	struct io_extent *io_ext = private;
	struct mem_cgroup *mcg = NULL;
	unsigned int op = (scenario == HYBRIDSWAP_RECLAIM_IN) ?
			  REQ_OP_WRITE : REQ_OP_READ;

	if (!io_ext) {
		hybp(HYB_ERR, "NULL io_ext\n");
		return;
	}

	hybp(HYB_DEBUG, "ext_id = %d, op = %d\n", io_ext->ext_id, op);
	mcg = io_ext->mcg;
	discard_io_extent(io_ext, op);
	if (mcg)
		css_put(&mcg->css);
}

struct mem_cgroup *hybridswap_zram_get_memcg(struct zram *zram, u32 index)
{
	return zram_get_memcg(zram, index);
}
