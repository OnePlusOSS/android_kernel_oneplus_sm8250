// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[HYBRIDSWAP]" fmt

#include <linux/kernel.h>
#include <linux/swap.h>

#include "../zram_drv.h"
#include "../zram_drv_internal.h"
#include "hybridswap.h"
#include "hybridswap_internal.h"

#include "hybridswap_list.h"
#include "hybridswap_area.h"
#include "hybridswap_lru_rmap.h"

#define esentry_extid(e) ((e) >> EXTENT_SHIFT)

void zram_set_memcg(struct zram *zram, u32 index, int mcg_id)
{
	hyb_list_set_mcgid(obj_idx(zram->area, index),
				zram->area->obj_table, mcg_id);
}

struct mem_cgroup *zram_get_memcg(struct zram *zram, u32 index)
{
	unsigned short mcg_id;

	mcg_id = hyb_list_get_mcgid(obj_idx(zram->area, index),
				zram->area->obj_table);

	return get_mem_cgroup(mcg_id);
}

int zram_get_memcg_coldest_index(struct hybridswap_area *area,
				 struct mem_cgroup *mcg,
				 int *index, int max_cnt)
{
	int cnt = 0;
	u32 i, tmp;

	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return 0;
	}
	if (!area->obj_table) {
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

	hh_lock_list(mcg_idx(area, mcg->id.id), area->obj_table);
	hyb_list_for_each_entry_reverse_safe(i, tmp,
		mcg_idx(area, mcg->id.id), area->obj_table) {
		if (i >= (u32)area->nr_objs) {
			hybp(HYB_ERR, "index = %d invalid\n", i);
			continue;
		}
		index[cnt++] = i;
		if (cnt >= max_cnt)
			break;
	}
	hh_unlock_list(mcg_idx(area, mcg->id.id), area->obj_table);

	return cnt;
}

int zram_rmap_get_extent_index(struct hybridswap_area *area,
			       int ext_id, int *index)
{
	int cnt = 0;
	u32 i;

	if (!area) {
		hybp(HYB_ERR, "NULL area\n");
		return 0;
	}
	if (!area->obj_table) {
		hybp(HYB_ERR, "NULL table\n");
		return 0;
	}
	if (!index) {
		hybp(HYB_ERR, "NULL index\n");
		return 0;
	}
	if (ext_id < 0 || ext_id >= area->nr_exts) {
		hybp(HYB_ERR, "ext = %d invalid\n", ext_id);
		return 0;
	}

	hh_lock_list(ext_idx(area, ext_id), area->obj_table);
	hyb_list_for_each_entry(i, ext_idx(area, ext_id), area->obj_table) {
		if (cnt >= (int)EXTENT_MAX_OBJ_CNT) {
			WARN_ON_ONCE(1);
			break;
		}
		index[cnt++] = i;
	}
	hh_unlock_list(ext_idx(area, ext_id), area->obj_table);

	return cnt;
}

void zram_lru_add(struct zram *zram, u32 index, struct mem_cgroup *memcg)
{
	unsigned long size;
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

	zram_set_memcg(zram, index, memcg->id.id);
	hyb_list_add(obj_idx(zram->area, index),
			mcg_idx(zram->area, memcg->id.id),
			zram->area->obj_table);

	size = zram_get_obj_size(zram, index);

	atomic64_add(size, &MEMCGRP_ITEM(memcg, zram_stored_size));
	atomic64_inc(&MEMCGRP_ITEM(memcg, zram_page_size));
	atomic64_add(size, &stat->zram_stored_size);
	atomic64_inc(&stat->zram_stored_pages);
}

void zram_lru_add_tail(struct zram *zram, u32 index, struct mem_cgroup *mcg)
{
	unsigned long size;
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();

	if (!stat) {
		hybp(HYB_ERR, "NULL stat\n");
		return;
	}
	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}
	if (!mcg || !MEMCGRP_ITEM(mcg, zram) || !MEMCGRP_ITEM(mcg, zram)->area) {
		hybp(HYB_ERR, "invalid mcg\n");
		return;
	}
	if (index >= (u32)zram->area->nr_objs) {
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

	zram_set_memcg(zram, index, mcg->id.id);
	hyb_list_add_tail(obj_idx(zram->area, index),
			mcg_idx(zram->area, mcg->id.id),
			zram->area->obj_table);

	size = zram_get_obj_size(zram, index);

	atomic64_add(size, &MEMCGRP_ITEM(mcg, zram_stored_size));
	atomic64_inc(&MEMCGRP_ITEM(mcg, zram_page_size));
	atomic64_add(size, &stat->zram_stored_size);
	atomic64_inc(&stat->zram_stored_pages);
}

void zram_lru_del(struct zram *zram, u32 index)
{
	struct mem_cgroup *mcg = NULL;
	unsigned long size;
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
	if (zram_test_flag(zram, index, ZRAM_WB)) {
		hybp(HYB_ERR, "WB object, index = %d\n", index);
		return;
	}

	mcg = zram_get_memcg(zram, index);
	if (!mcg || !MEMCGRP_ITEM(mcg, zram) || !MEMCGRP_ITEM(mcg, zram)->area)
		return;
	if (zram_test_flag(zram, index, ZRAM_SAME))
		return;

	size = zram_get_obj_size(zram, index);
	hyb_list_del(obj_idx(zram->area, index),
			mcg_idx(zram->area, mcg->id.id),
			zram->area->obj_table);
	zram_set_memcg(zram, index, 0);

	atomic64_sub(size, &MEMCGRP_ITEM(mcg, zram_stored_size));
	atomic64_dec(&MEMCGRP_ITEM(mcg, zram_page_size));
	atomic64_sub(size, &stat->zram_stored_size);
	atomic64_dec(&stat->zram_stored_pages);
}

void zram_rmap_insert(struct zram *zram, u32 index)
{
	unsigned long eswpentry;
	u32 ext_id;

	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}
	if (index >= (u32)zram->area->nr_objs) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return;
	}

	eswpentry = zram_get_handle(zram, index);
	ext_id = esentry_extid(eswpentry);
	hyb_list_add_tail(obj_idx(zram->area, index),
			ext_idx(zram->area, ext_id),
			zram->area->obj_table);
}

void zram_rmap_erase(struct zram *zram, u32 index)
{
	unsigned long eswpentry;
	u32 ext_id;

	if (!zram) {
		hybp(HYB_ERR, "NULL zram\n");
		return;
	}
	if (index >= (u32)zram->area->nr_objs) {
		hybp(HYB_ERR, "index = %d invalid\n", index);
		return;
	}

	eswpentry = zram_get_handle(zram, index);
	ext_id = esentry_extid(eswpentry);
	hyb_list_del(obj_idx(zram->area, index),
		ext_idx(zram->area, ext_id),
		zram->area->obj_table);
}
