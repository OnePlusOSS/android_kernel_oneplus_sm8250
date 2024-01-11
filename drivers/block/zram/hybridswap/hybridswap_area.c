// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[HYBRIDSWAP]" fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/zsmalloc.h>
#include <linux/memcontrol.h>

#include "hybridswap_area.h"
#include "hybridswap_list.h"
#include "hybridswap_internal.h"

struct mem_cgroup *get_mem_cgroup(unsigned short mcg_id)
{
	struct mem_cgroup *mcg = NULL;

	rcu_read_lock();
	mcg = mem_cgroup_from_id(mcg_id);
	rcu_read_unlock();

	return mcg;
}

static bool fragment_dec(bool prev_flag, bool next_flag,
			 struct hybridswap_stat *stat)
{
	if (prev_flag && next_flag) {
		atomic64_inc(&stat->frag_cnt);
		return false;
	}

	if (prev_flag || next_flag)
		return false;

	return true;
}

static bool fragment_inc(bool prev_flag, bool next_flag,
			 struct hybridswap_stat *stat)
{
	if (prev_flag && next_flag) {
		atomic64_dec(&stat->frag_cnt);
		return false;
	}

	if (prev_flag || next_flag)
		return false;

	return true;
}

static bool prev_is_cont(struct hybridswap_area *area, int ext_id, int mcg_id)
{
	int prev;

	if (is_first_idx(ext_idx(area, ext_id), mcg_idx(area, mcg_id),
			 area->ext_table))
		return false;
	prev = prev_idx(ext_idx(area, ext_id), area->ext_table);

	return (prev >= 0) && (ext_idx(area, ext_id) == prev + 1);
}

static bool next_is_cont(struct hybridswap_area *area, int ext_id, int mcg_id)
{
	int next;

	if (is_last_idx(ext_idx(area, ext_id), mcg_idx(area, mcg_id),
			area->ext_table))
		return false;
	next = next_idx(ext_idx(area, ext_id), area->ext_table);

	return (next >= 0) && (ext_idx(area, ext_id) + 1 == next);
}

static void ext_fragment_sub(struct hybridswap_area *area, int ext_id)
{
	bool prev_flag = false;
	bool next_flag = false;
	int mcg_id;
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}

	if (!area->ext_table) {
		hybp(HYB_ERR, "NULL table\n");
		return;
	}
	if (ext_id < 0 || ext_id >= area->nr_exts) {
		hybp(HYB_ERR, "ext = %d invalid\n", ext_id);
		return;
	}

	mcg_id = hyb_list_get_mcgid(ext_idx(area, ext_id), area->ext_table);
	if (mcg_id <= 0 || mcg_id >= area->nr_mcgs) {
		hybp(HYB_ERR, "mcg_id = %d invalid\n", mcg_id);
		return;
	}

	atomic64_dec(&stat->ext_cnt);
	area->mcg_id_cnt[mcg_id]--;
	if (area->mcg_id_cnt[mcg_id] == 0) {
		atomic64_dec(&stat->mcg_cnt);
		atomic64_dec(&stat->frag_cnt);
		return;
	}

	prev_flag = prev_is_cont(area, ext_id, mcg_id);
	next_flag = next_is_cont(area, ext_id, mcg_id);

	if (fragment_dec(prev_flag, next_flag, stat))
		atomic64_dec(&stat->frag_cnt);
}

static void ext_fragment_add(struct hybridswap_area *area, int ext_id)
{
	bool prev_flag = false;
	bool next_flag = false;
	int mcg_id;
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}

	if (!area->ext_table) {
		hybp(HYB_ERR, "NULL table\n");
		return;
	}
	if (ext_id < 0 || ext_id >= area->nr_exts) {
		hybp(HYB_ERR, "ext = %d invalid\n", ext_id);
		return;
	}

	mcg_id = hyb_list_get_mcgid(ext_idx(area, ext_id), area->ext_table);
	if (mcg_id <= 0 || mcg_id >= area->nr_mcgs) {
		hybp(HYB_ERR, "mcg_id = %d invalid\n", mcg_id);
		return;
	}

	atomic64_inc(&stat->ext_cnt);
	if (area->mcg_id_cnt[mcg_id] == 0) {
		area->mcg_id_cnt[mcg_id]++;
		atomic64_inc(&stat->frag_cnt);
		atomic64_inc(&stat->mcg_cnt);
		return;
	}
	area->mcg_id_cnt[mcg_id]++;

	prev_flag = prev_is_cont(area, ext_id, mcg_id);
	next_flag = next_is_cont(area, ext_id, mcg_id);

	if (fragment_inc(prev_flag, next_flag, stat))
		atomic64_inc(&stat->frag_cnt);
}

static int extent_bit2id(struct hybridswap_area *area, int bit)
{
	if (bit < 0 || bit >= area->nr_exts) {
		hybp(HYB_ERR, "bit = %d invalid\n", bit);
		return -EINVAL;
	}

	return area->nr_exts - bit - 1;
}

static int extent_id2bit(struct hybridswap_area *area, int id)
{
	if (id < 0 || id >= area->nr_exts) {
		hybp(HYB_ERR, "id = %d invalid\n", id);
		return -EINVAL;
	}

	return area->nr_exts - id - 1;
}

int obj_idx(struct hybridswap_area *area, int idx)
{
	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return -EINVAL;
	}
	if (idx < 0 || idx >= area->nr_objs) {
		hybp(HYB_ERR, "idx = %d invalid\n", idx);
		return -EINVAL;
	}

	return idx;
}

int ext_idx(struct hybridswap_area *area, int idx)
{
	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return -EINVAL;
	}
	if (idx < 0 || idx >= area->nr_exts) {
		hybp(HYB_ERR, "idx = %d invalid\n", idx);
		return -EINVAL;
	}

	return idx + area->nr_objs;
}

int mcg_idx(struct hybridswap_area *area, int idx)
{
	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return -EINVAL;
	}
	if (idx <= 0 || idx >= area->nr_mcgs) {
		hybp(HYB_ERR, "idx = %d invalid, nr_mcgs %d\n", idx,
			area->nr_mcgs);
		return -EINVAL;
	}

	return idx + area->nr_objs + area->nr_exts;
}

static struct hyb_list_head *get_obj_table_node(int idx, void *private)
{
	struct hybridswap_area *area = private;

	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return NULL;
	}
	if (idx < 0) {
		hybp(HYB_ERR, "idx = %d invalid\n", idx);
		return NULL;
	}
	if (idx < area->nr_objs)
		return &area->lru[idx];
	idx -= area->nr_objs;
	if (idx < area->nr_exts)
		return &area->rmap[idx];
	idx -= area->nr_exts;
	if (idx > 0 && idx < area->nr_mcgs) {
		struct mem_cgroup *mcg = get_mem_cgroup(idx);

		if (!mcg)
			goto err_out;
		return (struct hyb_list_head *)(&MEMCGRP_ITEM(mcg, zram_lru));
	}
err_out:
	hybp(HYB_ERR, "idx = %d invalid, mcg is NULL\n", idx);

	return NULL;
}

static void free_obj_list_table(struct hybridswap_area *area)
{
	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return;
	}

	if (area->lru) {
		vfree(area->lru);
		area->lru = NULL;
	}
	if (area->rmap) {
		vfree(area->rmap);
		area->rmap = NULL;
	}

	kfree(area->obj_table);
	area->obj_table = NULL;
}

static int init_obj_list_table(struct hybridswap_area *area)
{
	int i;

	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return -EINVAL;
	}

	area->lru = vzalloc(sizeof(struct hyb_list_head) * area->nr_objs);
	if (!area->lru) {
		hybp(HYB_ERR, "area->lru alloc failed\n");
		goto err_out;
	}
	area->rmap = vzalloc(sizeof(struct hyb_list_head) * area->nr_exts);
	if (!area->rmap) {
		hybp(HYB_ERR, "area->rmap alloc failed\n");
		goto err_out;
	}
	area->obj_table = alloc_table(get_obj_table_node, area, GFP_KERNEL);
	if (!area->obj_table) {
		hybp(HYB_ERR, "area->obj_table alloc failed\n");
		goto err_out;
	}
	for (i = 0; i < area->nr_objs; i++)
		hyb_list_init(obj_idx(area, i), area->obj_table);
	for (i = 0; i < area->nr_exts; i++)
		hyb_list_init(ext_idx(area, i), area->obj_table);

	hybp(HYB_INFO, "hybridswap obj list table init OK.\n");
	return 0;
err_out:
	free_obj_list_table(area);
	hybp(HYB_ERR, "hybridswap obj list table init failed.\n");

	return -ENOMEM;
}

static struct hyb_list_head *get_ext_table_node(int idx, void *private)
{
	struct hybridswap_area *area = private;

	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return NULL;
	}

	if (idx < area->nr_objs)
		goto err_out;
	idx -= area->nr_objs;
	if (idx < area->nr_exts)
		return &area->ext[idx];
	idx -= area->nr_exts;
	if (idx > 0 && idx < area->nr_mcgs) {
		struct mem_cgroup *mcg = get_mem_cgroup(idx);

		if (!mcg)
			return NULL;
		return (struct hyb_list_head *)(&MEMCGRP_ITEM(mcg, ext_lru));
	}
err_out:
	hybp(HYB_ERR, "idx = %d invalid\n", idx);

	return NULL;
}

static void free_ext_list_table(struct hybridswap_area *area)
{
	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return;
	}

	if (area->ext)
		vfree(area->ext);

	kfree(area->ext_table);
}

static int init_ext_list_table(struct hybridswap_area *area)
{
	int i;

	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return -EINVAL;
	}
	area->ext = vzalloc(sizeof(struct hyb_list_head) * area->nr_exts);
	if (!area->ext)
		goto err_out;
	area->ext_table = alloc_table(get_ext_table_node, area, GFP_KERNEL);
	if (!area->ext_table)
		goto err_out;
	for (i = 0; i < area->nr_exts; i++)
		hyb_list_init(ext_idx(area, i), area->ext_table);
	hybp(HYB_INFO, "hybridswap ext list table init OK.\n");
	return 0;
err_out:
	free_ext_list_table(area);
	hybp(HYB_ERR, "hybridswap ext list table init failed.\n");

	return -ENOMEM;
}

void free_hybridswap_area(struct hybridswap_area *area)
{
	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return;
	}

	vfree(area->bitmap);
	vfree(area->ext_stored_pages);
	free_obj_list_table(area);
	free_ext_list_table(area);
	vfree(area);
}

struct hybridswap_area *alloc_hybridswap_area(unsigned long ori_size,
					    unsigned long comp_size)
{
	struct hybridswap_area *area = vzalloc(sizeof(struct hybridswap_area));

	if (!area) {
		hybp(HYB_ERR, "area alloc failed\n");
		goto err_out;
	}
	if (comp_size & (EXTENT_SIZE - 1)) {
		hybp(HYB_ERR, "disksize = %ld align invalid (32K align needed)\n",
			 comp_size);
		goto err_out;
	}
	area->size = comp_size;
	area->nr_exts = comp_size >> EXTENT_SHIFT;
	area->nr_mcgs = MEM_CGROUP_ID_MAX;
	area->nr_objs = ori_size >> PAGE_SHIFT;
	area->bitmap = vzalloc(BITS_TO_LONGS(area->nr_exts) * sizeof(long));
	if (!area->bitmap) {
		hybp(HYB_ERR, "area->bitmap alloc failed, %lu\n",
				BITS_TO_LONGS(area->nr_exts) * sizeof(long));
		goto err_out;
	}
	area->ext_stored_pages = vzalloc(sizeof(atomic_t) * area->nr_exts);
	if (!area->ext_stored_pages) {
		hybp(HYB_ERR, "area->ext_stored_pages alloc failed\n");
		goto err_out;
	}
	if (init_obj_list_table(area)) {
		hybp(HYB_ERR, "init obj list table failed\n");
		goto err_out;
	}
	if (init_ext_list_table(area)) {
		hybp(HYB_ERR, "init ext list table failed\n");
		goto err_out;
	}
	hybp(HYB_INFO, "area %p size %lu nr_exts %lu nr_mcgs %lu nr_objs %lu\n",
			area, area->size, area->nr_exts, area->nr_mcgs,
			area->nr_objs);
	hybp(HYB_INFO, "hybridswap_area init OK.\n");
	return area;
err_out:
	free_hybridswap_area(area);
	hybp(HYB_ERR, "hybridswap_area init failed.\n");

	return NULL;
}

void hybridswap_check_area_extent(struct hybridswap_area *area)
{
	int i;

	if (!area)
		return;

	for (i = 0; i < area->nr_exts; i++) {
		int cnt = atomic_read(&area->ext_stored_pages[i]);
		int ext_id = ext_idx(area, i);
		bool priv = hyb_list_test_priv(ext_id, area->ext_table);
		int mcg_id = hyb_list_get_mcgid(ext_id, area->ext_table);

		if (cnt < 0 || (cnt > 0 && mcg_id == 0))
			hybp(HYB_ERR, "%8d %8d %8d %8d %4d\n", i, cnt, ext_id,
				mcg_id, priv);
	}
}

void hybridswap_free_extent(struct hybridswap_area *area, int ext_id)
{
	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return;
	}
	if (ext_id < 0 || ext_id >= area->nr_exts) {
		hybp(HYB_ERR, "INVALID ext %d\n", ext_id);
		return;
	}
	hybp(HYB_DEBUG, "free ext id = %d.\n", ext_id);

	hyb_list_set_mcgid(ext_idx(area, ext_id), area->ext_table, 0);
	if (!test_and_clear_bit(extent_id2bit(area, ext_id), area->bitmap)) {
		hybp(HYB_ERR, "bit not set, ext = %d\n", ext_id);
		WARN_ON_ONCE(1);
	}
	atomic_dec(&area->stored_exts);
}

static int alloc_bitmap(unsigned long *bitmap, int max, int last_bit)
{
	int bit;

	if (!bitmap) {
		hybp(HYB_ERR, "NULL bitmap.\n");
		return -EINVAL;
	}
retry:
	bit = find_next_zero_bit(bitmap, max, last_bit);
	if (bit == max) {
		if (last_bit == 0) {
			hybp(HYB_ERR, "alloc bitmap failed.\n");
			return -ENOSPC;
		}
		last_bit = 0;
		goto retry;
	}
	if (test_and_set_bit(bit, bitmap))
		goto retry;

	return bit;
}

int hybridswap_alloc_extent(struct hybridswap_area *area, struct mem_cgroup *mcg)
{
	int last_bit;
	int bit;
	int ext_id;
	int mcg_id;

	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return -EINVAL;
	}
	if (!mcg) {
		hybp(HYB_ERR, "NULL mcg\n");
		return -EINVAL;
	}

	last_bit = atomic_read(&area->last_alloc_bit);
	hybp(HYB_DEBUG, "last_bit = %d.\n", last_bit);
	bit = alloc_bitmap(area->bitmap, area->nr_exts, last_bit);
	if (bit < 0) {
		hybp(HYB_ERR, "alloc bitmap failed.\n");
		return bit;
	}
	ext_id = extent_bit2id(area, bit);
	mcg_id = hyb_list_get_mcgid(ext_idx(area, ext_id), area->ext_table);
	if (mcg_id) {
		hybp(HYB_ERR, "already has mcg %d, ext %d\n",
				mcg_id, ext_id);
		goto err_out;
	}
	hyb_list_set_mcgid(ext_idx(area, ext_id), area->ext_table, mcg->id.id);

	atomic_set(&area->last_alloc_bit, bit);
	atomic_inc(&area->stored_exts);
	hybp(HYB_DEBUG, "extent %d init OK.\n", ext_id);
	hybp(HYB_DEBUG, "mcg_id = %d, ext id = %d\n", mcg->id.id, ext_id);

	return ext_id;
err_out:
	clear_bit(bit, area->bitmap);
	WARN_ON_ONCE(1);
	return -EBUSY;
}

int get_extent(struct hybridswap_area *area, int ext_id)
{
	int mcg_id;

	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return -EINVAL;
	}
	if (ext_id < 0 || ext_id >= area->nr_exts) {
		hybp(HYB_ERR, "ext = %d invalid\n", ext_id);
		return -EINVAL;
	}

	if (!hyb_list_clear_priv(ext_idx(area, ext_id), area->ext_table))
		return -EBUSY;
	mcg_id = hyb_list_get_mcgid(ext_idx(area, ext_id), area->ext_table);
	if (mcg_id) {
		ext_fragment_sub(area, ext_id);
		hyb_list_del(ext_idx(area, ext_id), mcg_idx(area, mcg_id),
			    area->ext_table);
	}
	hybp(HYB_DEBUG, "ext id = %d\n", ext_id);

	return ext_id;
}

void put_extent(struct hybridswap_area *area, int ext_id)
{
	int mcg_id;

	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return;
	}
	if (ext_id < 0 || ext_id >= area->nr_exts) {
		hybp(HYB_ERR, "ext = %d invalid\n", ext_id);
		return;
	}

	mcg_id = hyb_list_get_mcgid(ext_idx(area, ext_id), area->ext_table);
	if (mcg_id) {
		hh_lock_list(mcg_idx(area, mcg_id), area->ext_table);
		hyb_list_add_nolock(ext_idx(area, ext_id), mcg_idx(area, mcg_id),
			area->ext_table);
		ext_fragment_add(area, ext_id);
		hh_unlock_list(mcg_idx(area, mcg_id), area->ext_table);
	}
	if (!hyb_list_set_priv(ext_idx(area, ext_id), area->ext_table)) {
		hybp(HYB_ERR, "private not set, ext = %d\n", ext_id);
		WARN_ON_ONCE(1);
		return;
	}
	hybp(HYB_DEBUG, "put extent %d.\n", ext_id);
}

int get_memcg_extent(struct hybridswap_area *area, struct mem_cgroup *mcg)
{
	int mcg_id;
	int ext_id = -ENOENT;
	int idx;

	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return -EINVAL;
	}
	if (!area->ext_table) {
		hybp(HYB_ERR, "NULL table\n");
		return -EINVAL;
	}
	if (!mcg) {
		hybp(HYB_ERR, "NULL mcg\n");
		return -EINVAL;
	}

	mcg_id = mcg->id.id;
	hh_lock_list(mcg_idx(area, mcg_id), area->ext_table);
	hyb_list_for_each_entry(idx, mcg_idx(area, mcg_id), area->ext_table)
		if (hyb_list_clear_priv(idx, area->ext_table)) {
			ext_id = idx - area->nr_objs;
			break;
		}
	if (ext_id >= 0 && ext_id < area->nr_exts) {
		ext_fragment_sub(area, ext_id);
		hyb_list_del_nolock(idx, mcg_idx(area, mcg_id), area->ext_table);
		hybp(HYB_DEBUG, "ext id = %d\n", ext_id);
	}
	hh_unlock_list(mcg_idx(area, mcg_id), area->ext_table);

	return ext_id;
}

int get_memcg_zram_entry(struct hybridswap_area *area, struct mem_cgroup *mcg)
{
	int mcg_id, idx;
	int index = -ENOENT;

	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return -EINVAL;
	}
	if (!area->obj_table) {
		hybp(HYB_ERR, "NULL table\n");
		return -EINVAL;
	}
	if (!mcg) {
		hybp(HYB_ERR, "NULL mcg\n");
		return -EINVAL;
	}

	mcg_id = mcg->id.id;
	hh_lock_list(mcg_idx(area, mcg_id), area->obj_table);
	hyb_list_for_each_entry(idx, mcg_idx(area, mcg_id), area->obj_table) {
		index = idx;
		break;
	}
	hh_unlock_list(mcg_idx(area, mcg_id), area->obj_table);

	return index;
}

int get_extent_zram_entry(struct hybridswap_area *area, int ext_id)
{
	int index = -ENOENT;
	int idx;

	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return -EINVAL;
	}
	if (!area->obj_table) {
		hybp(HYB_ERR, "NULL table\n");
		return -EINVAL;
	}
	if (ext_id < 0 || ext_id >= area->nr_exts) {
		hybp(HYB_ERR, "ext = %d invalid\n", ext_id);
		return -EINVAL;
	}

	hh_lock_list(ext_idx(area, ext_id), area->obj_table);
	hyb_list_for_each_entry(idx, ext_idx(area, ext_id), area->obj_table) {
		index = idx;
		break;
	}
	hh_unlock_list(ext_idx(area, ext_id), area->obj_table);

	return index;
}
