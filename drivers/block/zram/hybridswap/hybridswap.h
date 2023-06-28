// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#ifndef HYBRIDSWAP_H
#define HYBRIDSWAP_H
extern int __init hybridswap_pre_init(void);
extern ssize_t hybridswap_vmstat_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t hybridswap_loglevel_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t hybridswap_loglevel_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t hybridswap_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t hybridswap_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf);
#ifdef CONFIG_HYBRIDSWAP_CORE
extern void hybridswap_record(struct zram *zram, u32 index, struct mem_cgroup *memcg);
extern void hybridswap_untrack(struct zram *zram, u32 index);
extern int hybridswap_page_fault(struct zram *zram, u32 index);
extern bool hybridswap_delete(struct zram *zram, u32 index);

extern ssize_t hybridswap_report_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t hybridswap_stat_snap_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t hybridswap_meminfo_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t hybridswap_core_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t hybridswap_core_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t hybridswap_loop_device_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t hybridswap_loop_device_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t hybridswap_dev_life_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t hybridswap_dev_life_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t hybridswap_quota_day_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t hybridswap_quota_day_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t hybridswap_zram_increase_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t hybridswap_zram_increase_show(struct device *dev,
		struct device_attribute *attr, char *buf);
#endif

#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
/* 63---48,47--32,31-0 : cgroup id, thread_index, index*/
#define ZRAM_INDEX_SHIFT	32
#define CACHE_INDEX_SHIFT	32
#define CACHE_INDEX_MASK	((1llu << CACHE_INDEX_SHIFT) - 1)
#define ZRAM_INDEX_MASK		((1llu << ZRAM_INDEX_SHIFT) - 1)

#define cache_index_val(index) (((unsigned long)index & CACHE_INDEX_MASK) << ZRAM_INDEX_SHIFT)
#define zram_index_val(id) ((unsigned long)id & ZRAM_INDEX_MASK)
#define mk_page_val(cache_index, index) (cache_index_val(cache_index) | zram_index_val(index))

#define fetch_cache_id(page) ((page->private >> 32) & CACHE_INDEX_MASK)
#define fetch_zram_index(page) (page->private & ZRAM_INDEX_MASK)

#define zram_set_page(zram, index, page) (zram->table[index].page = page)
#define zram_fetch_page(zram, index) (zram->table[index].page)

extern void del_page_from_cache(struct page *page);
extern int add_anon_page2cache(struct zram * zram, u32 index,
		struct page *page);
extern ssize_t hybridswap_akcompress_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t hybridswap_akcompress_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern void put_free_page(struct page *page);
extern void put_anon_pages(struct page *page);
extern int akcompress_cache_page_fault(struct zram *zram,
		struct page *page, u32 index);
extern void destroy_akcompressd_task(struct zram *zram);
#endif

#ifdef CONFIG_HYBRIDSWAP_SWAPD
extern ssize_t hybridswap_swapd_pause_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t hybridswap_swapd_pause_show(struct device *dev,
		struct device_attribute *attr, char *buf);
#endif
static inline bool current_is_swapd(void)
{
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	return (strncmp(current->comm, "hybridswapd:", sizeof("hybridswapd:") - 1) == 0);
#else
	return false;
#endif
}
#endif /* HYBRIDSWAP_H */
