// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[HYBRIDSWAP]" fmt

#include <linux/slab.h>
#include <linux/cpu.h>
#include <trace/hooks/vh_vmscan.h>
#include <linux/genhd.h>
#include <linux/proc_fs.h>
#include <linux/version.h>

#include "../zram_drv.h"
#include "../zram_drv_internal.h"
#include "hybridswap_internal.h"
#include "hybridswap.h"

static const char *swapd_text[NR_EVENT_ITEMS] = {
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	"swapd_wakeup",
	"swapd_hit_pagefaults",
	"swapd_memcg_scale_skip",
	"swapd_memcg_pagefault_skip",
	"swapd_shrink_anon",
	"swapd_swapout",
	"swapd_skip_swapout",
	"swapd_nothing_ignore",
	"swapd_over_min_buffer_skip_times",
	"swapd_nothing_ignore_skip_times",
	"swapd_snapshot_times",
	"swapd_skip_shrink_of_window",
	"swapd_manual_pause",
#ifdef CONFIG_OPLUS_JANK
	"swapd_cpu_busy_skip_times",
	"swapd_cpu_busy_break_times",
#endif
#endif
#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
	"akcompressd_running",
#endif
};

enum scan_balance {
	SCAN_EQUAL,
	SCAN_FRACT,
	SCAN_ANON,
	SCAN_FILE,
};

static int log_level = HYB_MAX;
static struct kmem_cache *hybridswap_cache;
static struct list_head grade_head;
static DEFINE_SPINLOCK(grade_list_lock);
static DEFINE_MUTEX(hybridswap_enable_lock);
static bool hybridswap_enabled = false;

DEFINE_MUTEX(reclaim_para_lock);
DEFINE_PER_CPU(struct swapd_event_state, swapd_event_states);

extern unsigned long try_to_free_mem_cgroup_pages(struct mem_cgroup *memcg,
		unsigned long nr_pages,
		gfp_t gfp_mask,
		bool may_swap);

void hybridswap_loglevel_set(int level)
{
	log_level = level;
}

int hybridswap_loglevel(void)
{
	return log_level;
}

void __put_memcg_cache(memcg_hybs_t *hybs)
{
#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
	if (hybs->cache.id > 0) {
		spin_lock(&cached_idr_lock);
		idr_replace(&cached_idr, NULL, hybs->cache.id);
		idr_remove(&cached_idr, hybs->cache.id);
		spin_unlock(&cached_idr_lock);
	}

	spin_lock(&hybs->cache.lock);
	if (hybs->cache.dead != 1)
		BUG();
	spin_unlock(&hybs->cache.lock);
#endif
	kmem_cache_free(hybridswap_cache, (void *)hybs);
}

static inline void sum_hybridswap_vm_events(unsigned long *ret)
{
	int cpu;
	int i;

	memset(ret, 0, NR_EVENT_ITEMS * sizeof(unsigned long));

	for_each_online_cpu(cpu) {
		struct swapd_event_state *this =
			&per_cpu(swapd_event_states, cpu);

		for (i = 0; i < NR_EVENT_ITEMS; i++)
			ret[i] += this->event[i];
	}
}

static inline void all_hybridswap_vm_events(unsigned long *ret)
{
	get_online_cpus();
	sum_hybridswap_vm_events(ret);
	put_online_cpus();
}

ssize_t hybridswap_vmstat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned long *vm_buf = NULL;
	int len = 0;
	int i = 0;

	vm_buf = kzalloc(sizeof(struct swapd_event_state), GFP_KERNEL);
	if (!vm_buf)
		return -ENOMEM;
	all_hybridswap_vm_events(vm_buf);

#ifdef CONFIG_HYBRIDSWAP_SWAPD
	len += snprintf(buf + len, PAGE_SIZE - len, "%-32s %12lu\n",
			"page_fault_pause", atomic_long_read(&page_fault_pause));
	len += snprintf(buf + len, PAGE_SIZE - len, "%-32s %12lu\n",
			"page_fault_pause_cnt", atomic_long_read(&page_fault_pause_cnt));
#endif

	for (;i < NR_EVENT_ITEMS; i++) {
		len += snprintf(buf + len, PAGE_SIZE - len, "%-32s %12lu\n",
				swapd_text[i], vm_buf[i]);
		if (len == PAGE_SIZE)
			break;
	}
	kfree(vm_buf);

	return len;
}

ssize_t hybridswap_loglevel_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	char *type_buf = NULL;
	unsigned long val;

	type_buf = strstrip((char *)buf);
	if (kstrtoul(type_buf, 0, &val))
		return -EINVAL;

	if (val >= HYB_MAX) {
		hybp(HYB_ERR, "val %lu is not valid\n", val);
		return -EINVAL;
	}
	hybridswap_loglevel_set((int)val);

	return len;
}

ssize_t hybridswap_loglevel_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t size = 0;

	size += scnprintf(buf + size, PAGE_SIZE - size,
			"Hybridswap log level: %d\n", hybridswap_loglevel());

	return size;
}

/* Make sure the memcg is not NULL in caller */
memcg_hybs_t *hybridswap_cache_alloc(struct mem_cgroup *memcg, bool atomic)
{
	memcg_hybs_t *hybs;
	u64 ret;
	gfp_t flags = GFP_KERNEL;

	if (memcg->android_oem_data1)
		BUG();

	if (atomic)
		flags &= ~__GFP_DIRECT_RECLAIM;

	hybs = (memcg_hybs_t *)kmem_cache_zalloc(hybridswap_cache, flags);
	if (!hybs)
		return NULL;

#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
	spin_lock_init(&hybs->cache.lock);
	INIT_LIST_HEAD(&hybs->cache.head);
	hybs->cache.cnt = 0;
	hybs->cache.compressing = 0;
	hybs->cache.dead = 0;
	spin_lock(&cached_idr_lock);
	hybs->cache.id = idr_alloc(&cached_idr, NULL, 1, MEM_CGROUP_ID_MAX,
			GFP_KERNEL);
	if (hybs->cache.id < 0) {
		spin_unlock(&cached_idr_lock);
		kmem_cache_free(hybridswap_cache, (void*)hybs);
		return NULL;
	}
	idr_replace(&cached_idr, &hybs->cache, hybs->cache.id);
	spin_unlock(&cached_idr_lock);
#endif
	INIT_LIST_HEAD(&hybs->grade_node);
#ifdef CONFIG_HYBRIDSWAP_CORE
	spin_lock_init(&hybs->zram_init_lock);
#endif
	atomic64_set(&hybs->app_grade, 300);
	atomic64_set(&hybs->ufs2zram_scale, 100);
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	atomic_set(&hybs->mem2zram_scale, 80);
	atomic_set(&hybs->zram2ufs_scale, 50);
	atomic_set(&hybs->pagefault_level, 50);
#endif
	hybs->memcg = memcg;
	refcount_set(&hybs->usage, 1);

	ret = atomic64_cmpxchg((atomic64_t *)&memcg->android_oem_data1, 0, (u64)hybs);
	if (ret != 0) {
#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
		hybs->cache.dead = 1;
#endif
		put_memcg_cache(hybs);
		return (memcg_hybs_t *)ret;
	}

	return hybs;
}

#ifdef CONFIG_HYBRIDSWAP_SWAPD
static void tune_scan_type_hook(void *data, char *scan_balance)
{
	/*hybrid swapd,scan anon only*/
	if (current_is_swapd()) {
		*scan_balance = SCAN_ANON;
		return;
	}

#ifdef CONFIG_HYBRIDSWAP_CORE
	if (unlikely(!hybridswap_core_enabled()))
		return;

	/*real zram full, scan file only*/
	if (!free_zram_is_ok()) {
		*scan_balance = SCAN_FILE;
		return;
	}
#endif
}
#endif

static void mem_cgroup_alloc_hook(void *data, struct mem_cgroup *memcg)
{
	if (memcg->android_oem_data1)
		BUG();

	hybridswap_cache_alloc(memcg, true);
}

static void mem_cgroup_free_hook(void *data, struct mem_cgroup *memcg)
{
	memcg_hybs_t *hybs;

	if (!memcg->android_oem_data1)
		return;

	hybs = (memcg_hybs_t *)memcg->android_oem_data1;
	memcg->android_oem_data1 = 0;
#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
	clear_page_memcg(&hybs->cache);
#endif
	put_memcg_cache(hybs);
}

void memcg_app_grade_update(struct mem_cgroup *tarfetch)
{
	struct list_head *pos = NULL;
	unsigned long flags;

#ifdef CONFIG_HYBRIDSWAP_SWAPD
	update_swapd_mcg_setup(tarfetch);
#endif
	spin_lock_irqsave(&grade_list_lock, flags);
	list_for_each(pos, &grade_head) {
		memcg_hybs_t *hybs = list_entry(pos, memcg_hybs_t, grade_node);
		if (atomic64_read(&hybs->app_grade) <
				atomic64_read(&MEMCGRP_ITEM(tarfetch, app_grade)))
			break;
	}
	list_move_tail(&MEMCGRP_ITEM(tarfetch, grade_node), pos);
	spin_unlock_irqrestore(&grade_list_lock, flags);
}

static void mem_cgroup_css_online_hook(void *data,
		struct cgroup_subsys_state *css, struct mem_cgroup *memcg)
{
	if (memcg->android_oem_data1)
		memcg_app_grade_update(memcg);

	css_get(css);
}

static void mem_cgroup_css_offline_hook(void *data,
		struct cgroup_subsys_state *css, struct mem_cgroup *memcg)
{
	unsigned long flags;

	if (memcg->android_oem_data1) {
		spin_lock_irqsave(&grade_list_lock, flags);
		list_del_init(&MEMCGRP_ITEM(memcg, grade_node));
		spin_unlock_irqrestore(&grade_list_lock, flags);
	}

	css_put(css);
}

#define REGISTER_HOOK(name) do {\
	rc = register_trace_android_vh_##name(name##_hook, NULL);\
	if (rc) {\
		hybp(HYB_ERR, "%s:%d register hook %s failed", __FILE__, __LINE__, #name);\
		goto err_out_##name;\
	}\
} while (0)

#define UNREGISTER_HOOK(name) do {\
	unregister_trace_android_vh_##name(name##_hook, NULL);\
} while (0)

#define ERROR_OUT(name) err_out_##name

static int register_all_hooks(void)
{
	int rc;

	/* mem_cgroup_alloc_hook */
	REGISTER_HOOK(mem_cgroup_alloc);
	/* mem_cgroup_free_hook */
	REGISTER_HOOK(mem_cgroup_free);
	/* mem_cgroup_css_online_hook */
	REGISTER_HOOK(mem_cgroup_css_online);
	/* mem_cgroup_css_offline_hook */
	REGISTER_HOOK(mem_cgroup_css_offline);
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	/* rmqueue_hook */
	REGISTER_HOOK(rmqueue);
	/* tune_scan_type_hook */
	REGISTER_HOOK(tune_scan_type);
#endif
#ifdef CONFIG_HYBRIDSWAP_CORE
	/* mem_cgroup_id_remove_hook */
	REGISTER_HOOK(mem_cgroup_id_remove);
#endif
	return 0;

#ifdef CONFIG_HYBRIDSWAP_CORE
	UNREGISTER_HOOK(mem_cgroup_id_remove);
ERROR_OUT(mem_cgroup_id_remove):
#endif
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	UNREGISTER_HOOK(tune_scan_type);
ERROR_OUT(tune_scan_type):
	UNREGISTER_HOOK(rmqueue);
ERROR_OUT(rmqueue):
#endif
	UNREGISTER_HOOK(mem_cgroup_css_offline);
ERROR_OUT(mem_cgroup_css_offline):
	UNREGISTER_HOOK(mem_cgroup_css_online);
ERROR_OUT(mem_cgroup_css_online):
	UNREGISTER_HOOK(mem_cgroup_free);
ERROR_OUT(mem_cgroup_free):
	UNREGISTER_HOOK(mem_cgroup_alloc);
ERROR_OUT(mem_cgroup_alloc):
	return rc;
}

static void unregister_all_hook(void)
{
	UNREGISTER_HOOK(mem_cgroup_alloc);
	UNREGISTER_HOOK(mem_cgroup_free);
	UNREGISTER_HOOK(mem_cgroup_css_offline);
	UNREGISTER_HOOK(mem_cgroup_css_online);
#ifdef CONFIG_HYBRIDSWAP_CORE
	UNREGISTER_HOOK(mem_cgroup_id_remove);
#endif
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	UNREGISTER_HOOK(rmqueue);
	UNREGISTER_HOOK(tune_scan_type);
#endif
}

unsigned long memcg_anon_pages(struct mem_cgroup *memcg)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	struct lruvec *lruvec = NULL;
	struct mem_cgroup_per_node *mz = NULL;
#endif
	if (!memcg)
		return 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	mz = mem_cgroup_nodeinfo(memcg, 0);
	if (!mz) {
		fetch_next_memcg_break(memcg);
		return 0;
	}

	lruvec = &mz->lruvec;
	if (!lruvec) {
		fetch_next_memcg_break(memcg);
		return 0;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
	return (mem_cgroup_get_lru_size(lruvec, LRU_ACTIVE_ANON) +
			mem_cgroup_get_lru_size(lruvec, LRU_INACTIVE_ANON));
#else
	return (lruvec_page_state(lruvec, NR_ACTIVE_ANON) +
			lruvec_page_state(lruvec, NR_INACTIVE_ANON));
#endif
#else
	return (memcg_page_state_local(memcg, NR_ACTIVE_ANON) +
			memcg_page_state_local(memcg, NR_INACTIVE_ANON));
#endif
}

static unsigned long memcg_inactive_anon_pages(struct mem_cgroup *memcg)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	struct lruvec *lruvec = NULL;
	struct mem_cgroup_per_node *mz = NULL;
#endif

	if (!memcg)
		return 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	mz = mem_cgroup_nodeinfo(memcg, 0);
	if (!mz) {
		fetch_next_memcg_break(memcg);
		return 0;
	}

	lruvec = &mz->lruvec;
	if (!lruvec) {
		fetch_next_memcg_break(memcg);
		return 0;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
	return mem_cgroup_get_lru_size(lruvec, LRU_INACTIVE_ANON);
#else
	return lruvec_page_state(lruvec, NR_INACTIVE_ANON);
#endif
#else
	return memcg_page_state_local(memcg, NR_INACTIVE_ANON);
#endif
}

static ssize_t mem_cgroup_force_shrink_anon(struct kernfs_open_file *of,
		char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg;
	unsigned long nr_need_reclaim, reclaim_total, nr_reclaimed;
	int ret;

	buf = strstrip(buf);
	ret = kstrtoul(buf, 0, &reclaim_total);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "reclaim_total %s value is error!\n",  buf);
		return -EINVAL;
	}

	memcg = mem_cgroup_from_css(of_css(of));

	if (reclaim_total)
		nr_need_reclaim = memcg_anon_pages(memcg);
	else
		nr_need_reclaim = memcg_inactive_anon_pages(memcg);

	nr_reclaimed = try_to_free_mem_cgroup_pages(memcg, nr_need_reclaim,
			GFP_KERNEL, true);

	return nbytes;
}

static int memcg_total_info_per_app_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = NULL;
	unsigned long anon_size;
	unsigned long zram_compress_size;
	unsigned long eswap_compress_size;
	unsigned long zram_page_size;
	unsigned long eswap_page_size;

	seq_printf(m, "%-8s %-8s %-8s %-8s %-8s %s \n",
			"anon", "zram_c", "zram_p", "eswap_c", "eswap_p",
			"memcg_n");
	while ((memcg = fetch_next_memcg(memcg))) {
		if (!MEMCGRP_ITEM_DATA(memcg))
			continue;

		anon_size = memcg_anon_pages(memcg);
		zram_compress_size = hybridswap_read_mcg_stats(memcg,
				MCG_ZRAM_STORED_SZ);
		eswap_compress_size = hybridswap_read_mcg_stats(memcg,
				MCG_DISK_STORED_SZ);
		zram_page_size = hybridswap_read_mcg_stats(memcg,
				MCG_ZRAM_STORED_PG_SZ);
		eswap_page_size = hybridswap_read_mcg_stats(memcg,
				MCG_DISK_STORED_PG_SZ);

		anon_size *= PAGE_SIZE / SZ_1K;
		zram_compress_size /= SZ_1K;
		eswap_compress_size /= SZ_1K;
		zram_page_size *= PAGE_SIZE / SZ_1K;
		eswap_page_size *= PAGE_SIZE / SZ_1K;

		seq_printf(m, "%-8lu %-8lu %-8lu %-8lu %-8lu %s \n",
				anon_size, zram_compress_size, zram_page_size,
				eswap_compress_size, eswap_page_size,
				MEMCGRP_ITEM(memcg, name));
	}

	return 0;
}

static int memcg_swap_stat_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = NULL;
	unsigned long eswap_out_cnt;
	unsigned long eswap_out_size;
	unsigned long eswap_in_size;
	unsigned long eswap_in_cnt;
	unsigned long page_fault_cnt;
	unsigned long cur_eswap_size;
	unsigned long max_eswap_size;
	unsigned long zram_compress_size, zram_page_size;
	unsigned long eswap_compress_size, eswap_page_size;

	memcg = mem_cgroup_from_css(seq_css(m));

	zram_compress_size = hybridswap_read_mcg_stats(memcg, MCG_ZRAM_STORED_SZ);
	zram_page_size = hybridswap_read_mcg_stats(memcg, MCG_ZRAM_STORED_PG_SZ);
	eswap_compress_size = hybridswap_read_mcg_stats(memcg, MCG_DISK_STORED_SZ);
	eswap_page_size = hybridswap_read_mcg_stats(memcg, MCG_DISK_STORED_PG_SZ);

	eswap_out_cnt = hybridswap_read_mcg_stats(memcg, MCG_ESWAPOUT_CNT);
	eswap_out_size = hybridswap_read_mcg_stats(memcg, MCG_ESWAPOUT_SZ);
	eswap_in_size = hybridswap_read_mcg_stats(memcg, MCG_ESWAPIN_SZ);
	eswap_in_cnt = hybridswap_read_mcg_stats(memcg, MCG_ESWAPIN_CNT);
	page_fault_cnt = hybridswap_read_mcg_stats(memcg, MCG_DISK_FAULT_CNT);
	cur_eswap_size = hybridswap_read_mcg_stats(memcg, MCG_DISK_SPACE);
	max_eswap_size = hybridswap_read_mcg_stats(memcg, MCG_DISK_SPACE_PEAK);

	seq_printf(m, "%-32s %12lu KB\n", "zramCompressedSize:",
			zram_compress_size / SZ_1K);
	seq_printf(m, "%-32s %12lu KB\n", "zramOrignalSize:",
			zram_page_size << (PAGE_SHIFT - 10));
	seq_printf(m, "%-32s %12lu KB\n", "eswapCompressedSize:",
			eswap_compress_size / SZ_1K);
	seq_printf(m, "%-32s %12lu KB\n", "eswapOrignalSize:",
			eswap_page_size << (PAGE_SHIFT - 10));
	seq_printf(m, "%-32s %12lu \n", "eswapOutTotal:", eswap_out_cnt);
	seq_printf(m, "%-32s %12lu KB\n", "eswapOutSize:", eswap_out_size / SZ_1K);
	seq_printf(m, "%-32s %12lu\n", "eswapInTotal:", eswap_in_cnt);
	seq_printf(m, "%-32s %12lu KB\n", "eswapInSize:", eswap_in_size / SZ_1K);
	seq_printf(m, "%-32s %12lu\n", "pageInTotal:", page_fault_cnt);
	seq_printf(m, "%-32s %12lu KB\n", "eswapSizeCur:", cur_eswap_size / SZ_1K);
	seq_printf(m, "%-32s %12lu KB\n", "eswapSizeMax:", max_eswap_size / SZ_1K);

	return 0;
}

static ssize_t mem_cgroup_name_write(struct kernfs_open_file *of, char *buf,
		size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	memcg_hybs_t *hybp = MEMCGRP_ITEM_DATA(memcg);
	int len, w_len;

	if (!hybp)
		return -EINVAL;

	buf = strstrip(buf);
	len = strlen(buf) + 1;
	if (len > MEM_CGROUP_NAME_MAX_LEN)
		len = MEM_CGROUP_NAME_MAX_LEN;

	w_len = snprintf(hybp->name, len, "%s", buf);
	if (w_len > len)
		hybp->name[len - 1] = '\0';

	return nbytes;
}

static int mem_cgroup_name_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(m));

	if (!MEMCGRP_ITEM_DATA(memcg))
		return -EPERM;

	seq_printf(m, "%s\n", MEMCGRP_ITEM(memcg, name));

	return 0;
}

static int mem_cgroup_app_grade_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	struct mem_cgroup *memcg;
	memcg_hybs_t *hybs;

	if (val > MAX_APP_GRADE || val < 0)
		return -EINVAL;

	memcg = mem_cgroup_from_css(css);
	hybs = MEMCGRP_ITEM_DATA(memcg);
	if (!hybs) {
		hybs = hybridswap_cache_alloc(memcg, false);
		if (!hybs)
			return -EINVAL;
	}

	if (atomic64_read(&MEMCGRP_ITEM(memcg, app_grade)) != val)
		atomic64_set(&MEMCGRP_ITEM(memcg, app_grade), val);
	memcg_app_grade_update(memcg);

	return 0;
}

static s64 mem_cgroup_app_grade_read(struct cgroup_subsys_state *css,
		struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (!MEMCGRP_ITEM_DATA(memcg))
		return -EPERM;

	return atomic64_read(&MEMCGRP_ITEM(memcg, app_grade));
}

int mem_cgroup_app_uid_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	struct mem_cgroup *memcg;
	memcg_hybs_t *hybs;

	if (val < 0)
		return -EINVAL;

	memcg = mem_cgroup_from_css(css);
	hybs = MEMCGRP_ITEM_DATA(memcg);
	if (!hybs) {
		return -EINVAL;
	}

	if (atomic64_read(&MEMCGRP_ITEM(memcg, app_uid)) != val)
		atomic64_set(&MEMCGRP_ITEM(memcg, app_uid), val);

	return 0;
}

static s64 mem_cgroup_app_uid_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (!MEMCGRP_ITEM_DATA(memcg))
		return -EPERM;

	return atomic64_read(&MEMCGRP_ITEM(memcg, app_uid));
}

static int mem_cgroup_ufs2zram_scale_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (!MEMCGRP_ITEM_DATA(memcg))
		return -EPERM;

	if (val > MAX_RATIO || val < MIN_RATIO)
		return -EINVAL;

	atomic64_set(&MEMCGRP_ITEM(memcg, ufs2zram_scale), val);

	return 0;
}

static s64 mem_cgroup_ufs2zram_scale_read(struct cgroup_subsys_state *css,
		struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (!MEMCGRP_ITEM_DATA(memcg))
		return -EPERM;

	return atomic64_read(&MEMCGRP_ITEM(memcg, ufs2zram_scale));
}

static int mem_cgroup_force_swapin_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	memcg_hybs_t *hybs;
	unsigned long size = 0;
	const unsigned int scale = 100;

	hybs = MEMCGRP_ITEM_DATA(memcg);
	if (!hybs)
		return -EPERM;

#ifdef	CONFIG_HYBRIDSWAP_CORE
	size = atomic64_read(&hybs->hybridswap_stored_size);
#endif
	size = atomic64_read(&hybs->ufs2zram_scale) * size / scale;
	size = ESWAP_ALIGN_UP(size);

#ifdef CONFIG_HYBRIDSWAP_CORE
	hybridswap_batches(memcg, size, val ? true : false);
#endif

	return 0;
}

static int mem_cgroup_force_swapout_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
#ifdef CONFIG_HYBRIDSWAP_CORE
	hybridswap_force_reclaim(mem_cgroup_from_css(css));
#endif
	return 0;
}

struct mem_cgroup *fetch_next_memcg(struct mem_cgroup *prev)
{
	memcg_hybs_t *hybs = NULL;
	struct mem_cgroup *memcg = NULL;
	struct list_head *pos = NULL;
	unsigned long flags;
	bool prev_got = true;

	spin_lock_irqsave(&grade_list_lock, flags);
find_again:
	if (unlikely(!prev))
		pos = &grade_head;
	else
		pos = &MEMCGRP_ITEM(prev, grade_node);

	if (list_empty(pos)) /* deleted node */
		goto unlock;

	if (pos->next == &grade_head)
		goto unlock;

	hybs = list_entry(pos->next, struct mem_cgroup_hybridswap, grade_node);
	memcg = hybs->memcg;
	if (unlikely(!memcg))
		goto unlock;

	if (!css_tryget(&memcg->css)) {
		if (prev && prev_got)
			css_put(&prev->css);
		prev = memcg;
		prev_got = false;
		goto find_again;
	}

unlock:
	spin_unlock_irqrestore(&grade_list_lock, flags);
	if (prev && prev_got)
		css_put(&prev->css);

	return memcg;
}

void fetch_next_memcg_break(struct mem_cgroup *memcg)
{
	if (memcg)
		css_put(&memcg->css);
}

static struct cftype mem_cgroup_hybridswap_legacy_files[] = {
	{
		.name = "force_shrink_anon",
		.write = mem_cgroup_force_shrink_anon,
	},
	{
		.name = "total_info_per_app",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.seq_show = memcg_total_info_per_app_show,
	},
	{
		.name = "swap_stat",
		.seq_show = memcg_swap_stat_show,
	},
	{
		.name = "name",
		.write = mem_cgroup_name_write,
		.seq_show = mem_cgroup_name_show,
	},
	{
		.name = "app_score",
		.write_s64 = mem_cgroup_app_grade_write,
		.read_s64 = mem_cgroup_app_grade_read,
	},
	{
		.name = "app_uid",
		.write_s64 = mem_cgroup_app_uid_write,
		.read_s64 = mem_cgroup_app_uid_read,
	},
	{
		.name = "ub_ufs2zram_ratio",
		.write_s64 = mem_cgroup_ufs2zram_scale_write,
		.read_s64 = mem_cgroup_ufs2zram_scale_read,
	},
	{
		.name = "force_swapin",
		.write_s64 = mem_cgroup_force_swapin_write,
	},
	{
		.name = "force_swapout",
		.write_s64 = mem_cgroup_force_swapout_write,
	},
#ifdef CONFIG_HYBRIDSWAP_CORE
	{
		.name = "psi",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.seq_show = hybridswap_psi_show,
	},
	{
		.name = "stored_wm_ratio",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.write_s64 = mem_cgroup_stored_wm_scale_write,
		.read_s64 = mem_cgroup_stored_wm_scale_read,
	},
#endif
	{ }, /* terminate */
};

static int hybridswap_enable(struct zram *zram)
{
	int ret = 0;

	if (hybridswap_enabled) {
		hybp(HYB_WARN, "hybridswap_enabled is true\n");
		return ret;
	}

#ifdef CONFIG_HYBRIDSWAP_SWAPD
	ret = swapd_init(zram);
	if (ret)
		return ret;
#endif

#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
	ret = create_akcompressd_task(zram);
	if (ret)
		goto create_akcompressd_task_fail;
#endif

#ifdef CONFIG_HYBRIDSWAP_CORE
	ret = hybridswap_core_enable();
	if (ret)
		goto hybridswap_core_enable_fail;
#endif
	hybridswap_enabled = true;

	return 0;

#ifdef CONFIG_HYBRIDSWAP_CORE
hybridswap_core_enable_fail:
#endif
#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
	destroy_akcompressd_task(zram);
create_akcompressd_task_fail:
#endif
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	swapd_exit();
#endif
	return ret;
}

static void hybridswap_disable(struct zram * zram)
{
	if (!hybridswap_enabled) {
		hybp(HYB_WARN, "hybridswap_enabled is false\n");
		return;
	}

#ifdef CONFIG_HYBRIDSWAP_CORE
	hybridswap_core_disable();
#endif

#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
	destroy_akcompressd_task(zram);
#endif

#ifdef CONFIG_HYBRIDSWAP_SWAPD
	swapd_exit();
#endif
	hybridswap_enabled = false;
}

ssize_t hybridswap_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int len = snprintf(buf, PAGE_SIZE, "hybridswap %s out_to_eswap %s swapd %s\n",
			hybridswap_core_enabled() ? "enable" : "disable",
			hybridswap_out_to_eswap_enable() ? "enable" : "disable",
			hybridswap_swapd_enabled() ? "enable" : "disable");

	return len;
}

ssize_t hybridswap_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret;
	unsigned long val;
	char *kbuf;
	struct zram *zram;

	kbuf = strstrip((char *)buf);
	ret = kstrtoul(kbuf, 0, &val);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "val %s is invalid!\n", kbuf);

		return -EINVAL;
	}

	mutex_lock(&hybridswap_enable_lock);
	zram = dev_to_zram(dev);
	if (val == 0)
		hybridswap_disable(zram);
	else
		ret = hybridswap_enable(zram);
	mutex_unlock(&hybridswap_enable_lock);

	if (ret == 0)
		ret = len;
	return ret;
}

int __init hybridswap_pre_init(void)
{
	int ret;

	INIT_LIST_HEAD(&grade_head);
	log_level = HYB_INFO;

	hybridswap_cache = kmem_cache_create("mem_cgroup_hybridswap",
			sizeof(struct mem_cgroup_hybridswap),
			0, SLAB_PANIC, NULL);
	if (!hybridswap_cache) {
		hybp(HYB_ERR, "create hybridswap_cache failed\n");
		ret = -ENOMEM;
		return ret;
	}

	ret = cgroup_add_legacy_cftypes(&memory_cgrp_subsys,
			mem_cgroup_hybridswap_legacy_files);
	if (ret) {
		hybp(HYB_INFO, "add mem_cgroup_hybridswap_legacy_files failed\n");
		goto error_out;
	}

#ifdef CONFIG_HYBRIDSWAP_SWAPD
	ret = cgroup_add_legacy_cftypes(&memory_cgrp_subsys,
			mem_cgroup_swapd_legacy_files);
	if (ret) {
		hybp(HYB_INFO, "add mem_cgroup_swapd_legacy_files failed!\n");
		goto error_out;
	}
#endif

#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
	akcompressd_pre_init();
#endif

#ifdef CONFIG_HYBRIDSWAP_SWAPD
	swapd_pre_init();
#endif
	ret = register_all_hooks();
	if (ret)
		goto fail_out;

	hybp(HYB_INFO, "hybridswap inited success!\n");
	return 0;

fail_out:
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	swapd_pre_deinit();
#endif
#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
	akcompressd_pre_deinit();
#endif
error_out:
	if (hybridswap_cache) {
		kmem_cache_destroy(hybridswap_cache);
		hybridswap_cache = NULL;
	}
	return ret;
}

void __exit hybridswap_exit(void)
{
	unregister_all_hook();

#ifdef CONFIG_HYBRIDSWAP_SWAPD
	swapd_pre_deinit();
#endif
#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
	akcompressd_pre_deinit();
#endif

	if (hybridswap_cache) {
		kmem_cache_destroy(hybridswap_cache);
		hybridswap_cache = NULL;
	}
}
