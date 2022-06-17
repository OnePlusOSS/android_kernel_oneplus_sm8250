// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#ifndef _HYBRIDSWAP_AREA_H
#define _HYBRIDSWAP_AREA_H

#include <linux/memcontrol.h>

struct hybridswap_area {
	unsigned long size;
	int nr_objs;
	int nr_exts;
	int nr_mcgs;

	unsigned long *bitmap;
	atomic_t last_alloc_bit;

	struct hyb_list_table *ext_table;
	struct hyb_list_head *ext;

	struct hyb_list_table *obj_table;
	struct hyb_list_head *rmap;
	struct hyb_list_head *lru;

	atomic_t stored_exts;
	atomic_t *ext_stored_pages;

	unsigned int mcg_id_cnt[MEM_CGROUP_ID_MAX + 1];
};

struct mem_cgroup *get_mem_cgroup(unsigned short mcg_id);

int obj_idx(struct hybridswap_area *area, int idx);
int ext_idx(struct hybridswap_area *area, int idx);
int mcg_idx(struct hybridswap_area *area, int idx);

void free_hybridswap_area(struct hybridswap_area *area);
struct hybridswap_area *alloc_hybridswap_area(unsigned long ori_size,
					    unsigned long comp_size);
void hybridswap_check_area_extent(struct hybridswap_area *area);
void hybridswap_free_extent(struct hybridswap_area *area, int ext_id);
int hybridswap_alloc_extent(struct hybridswap_area *area, struct mem_cgroup *mcg);
int get_extent(struct hybridswap_area *area, int ext_id);
void put_extent(struct hybridswap_area *area, int ext_id);
int get_memcg_extent(struct hybridswap_area *area, struct mem_cgroup *mcg);
int get_memcg_zram_entry(struct hybridswap_area *area, struct mem_cgroup *mcg);
int get_extent_zram_entry(struct hybridswap_area *area, int ext_id);
#endif /* _HYBRIDSWAP_AREA_H */
