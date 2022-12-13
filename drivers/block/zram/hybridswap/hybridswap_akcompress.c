// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[HYBRIDSWAP]" fmt

#include <linux/types.h>
#include <linux/spinlock_types.h>
#include <linux/atomic.h>
#include <linux/idr.h>
#include <linux/freezer.h>

#include "../zram_drv.h"
#include "../zram_drv_internal.h"
#include "hybridswap_internal.h"
#include "hybridswap.h"

struct compress_info_s {
	struct list_head free_page_head;
	spinlock_t free_lock;
	unsigned int free_cnt;

	unsigned int max_cnt;
} compress_info;

#define MAX_AKCOMPRESSD_THREADS 4
#define DEFAULT_CACHE_SIZE_MB 64
#define DEFAULT_COMPRESS_BATCH_MB 1
#define DEFAULT_CACHE_COUNT  ((DEFAULT_CACHE_SIZE_MB << 20) >> PAGE_SHIFT)
#define WAKEUP_AKCOMPRESSD_WATERMARK ((DEFAULT_COMPRESS_BATCH_MB << 20) >> PAGE_SHIFT)

static wait_queue_head_t akcompressd_wait;
static struct task_struct *akc_task[MAX_AKCOMPRESSD_THREADS];
static atomic64_t akc_cnt[MAX_AKCOMPRESSD_THREADS];
static int akcompressd_threads = 0;
static atomic64_t cached_cnt;
static struct zram *zram_info;
static DEFINE_MUTEX(akcompress_init_lock);

struct idr cached_idr = IDR_INIT(cached_idr);
DEFINE_SPINLOCK(cached_idr_lock);

static void wake_all_akcompressd(void);

void clear_page_memcg(struct cgroup_cache_page *cache)
{
	struct list_head *pos;
	struct page *page;

	spin_lock(&cache->lock);
	if (list_empty(&cache->head))
		goto out;

	list_for_each(pos, &cache->head) {
		page = list_entry(pos, struct page, lru);
		if (!page->mem_cgroup)
			BUG();
		page->mem_cgroup = NULL;
	}

out:
	cache->dead = 1;
	spin_unlock(&cache->lock);
}

static inline struct page * fetch_free_page(void)
{
	struct page *page = NULL;

	spin_lock(&compress_info.free_lock);
	if (compress_info.free_cnt > 0) {
		if (list_empty(&compress_info.free_page_head))
			BUG();
		page = lru_to_page(&compress_info.free_page_head);
		list_del(&page->lru);
		compress_info.free_cnt--;
	}
	spin_unlock(&compress_info.free_lock);

	return page;
}

void put_free_page(struct page *page)
{
	set_page_private(page, 0);
	spin_lock(&compress_info.free_lock);
	list_add_tail(&page->lru, &compress_info.free_page_head);
	compress_info.free_cnt++;
	spin_unlock(&compress_info.free_lock);
}

static inline struct cgroup_cache_page *find_and_fetch_memcg_cache(int cache_id)
{
	struct cgroup_cache_page *cache;

	spin_lock(&cached_idr_lock);
	cache = (struct cgroup_cache_page *)idr_find(&cached_idr, cache_id);
	if (unlikely(!cache)) {
		spin_unlock(&cached_idr_lock);
		pr_err("cache_id %d cache not find.\n", cache_id);

		return NULL;
	}
	fetch_memcg_cache(container_of(cache, memcg_hybs_t, cache));
	spin_unlock(&cached_idr_lock);

	return cache;
}

void del_page_from_cache(struct page *page)
{
	int cache_id;
	struct cgroup_cache_page *cache;

	if (!page)
		return;

	cache_id = fetch_cache_id(page);
	if (unlikely(cache_id < 0 || cache_id > MEM_CGROUP_ID_MAX)) {
		hybp(HYB_ERR, "page %p cache_id %d index %u is invalid.\n",
			page, cache_id, fetch_zram_index(page));
		return;
	}

	cache = find_and_fetch_memcg_cache(cache_id);
	if (!cache)
		return;

	spin_lock(&cache->lock);
	list_del(&page->lru);
	cache->cnt--;
	spin_unlock(&cache->lock);
	put_memcg_cache(container_of(cache, memcg_hybs_t, cache));
	atomic64_dec(&cached_cnt);
}

void del_page_from_cache_with_cache(struct page *page,
					struct cgroup_cache_page *cache)
{
	spin_lock(&cache->lock);
	list_del(&page->lru);
	cache->cnt--;
	spin_unlock(&cache->lock);
	atomic64_dec(&cached_cnt);
}

void put_anon_pages(struct page *page)
{
	memcg_hybs_t *hybs = MEMCGRP_ITEM_DATA(page->mem_cgroup);

	spin_lock(&hybs->cache.lock);
	list_add(&page->lru, &hybs->cache.head);
	hybs->cache.cnt++;
	spin_unlock(&hybs->cache.lock);
}

static inline bool can_stop_working(struct cgroup_cache_page *cache, int index)
{
	spin_lock(&cache->lock);
	if (unlikely(!list_empty(&cache->head))) {
		spin_unlock(&cache->lock);
		return false;
	}
	spin_unlock(&cache->lock);
	return 1;
}

static int check_cache_state(struct cgroup_cache_page *cache)
{
	if (cache->cnt == 0 || cache->compressing == 1)
		return 0;

	spin_lock(&cache->lock);
	if (cache->cnt == 0 || cache->compressing) {
		spin_unlock(&cache->lock);
		return 0;
	}
	cache->compressing = 1;
	spin_unlock(&cache->lock);
	fetch_memcg_cache(container_of(cache, memcg_hybs_t, cache));
	return 1;
}

struct cgroup_cache_page *fetch_one_cache(void)
{
	struct cgroup_cache_page *cache = NULL;
	int id;

	spin_lock(&cached_idr_lock);
	idr_for_each_entry(&cached_idr, cache, id) {
		if (check_cache_state(cache))
			break;
	}
	spin_unlock(&cached_idr_lock);

	return cache;
}

void mark_compressing_stop(struct cgroup_cache_page *cache)
{
	spin_lock(&cache->lock);
	if (cache->dead)
		hybp(HYB_WARN, "stop compressing, may be cgroup is delelted\n");
	cache->compressing = 0;
	spin_unlock(&cache->lock);
	put_memcg_cache(container_of(cache, memcg_hybs_t, cache));
}

static inline struct page *fetch_anon_page(struct zram *zram,
					struct cgroup_cache_page *cache)
{
	struct page *page, *prev_page;
	int index;

	if (compress_info.free_cnt == 0)
		return NULL;

	prev_page = NULL;
try_again:
	page = NULL;

	spin_lock(&cache->lock);
	if (!list_empty(&cache->head)) {
		page = lru_to_page(&cache->head);
		index = fetch_zram_index(page);
	}
	spin_unlock(&cache->lock);

	if (page) {
		if (prev_page && (page == prev_page)) {
			hybp(HYB_ERR, "zram %p index %d page %p\n",
				zram, index, page);
			BUG();
		}

		zram_slot_lock(zram, index);
		if (!zram_test_flag(zram, index, ZRAM_CACHED)) {
			zram_slot_unlock(zram, index);
			prev_page = page;
			goto try_again;
		}

		prev_page = NULL;
		zram_clear_flag(zram, index, ZRAM_CACHED);
		del_page_from_cache_with_cache(page, cache);
		zram_set_flag(zram, index, ZRAM_CACHED_COMPRESS);
		zram_slot_unlock(zram, index);
	}

	return page;
}

int add_anon_page2cache(struct zram * zram, u32 index, struct page *page)
{
	struct page *dst_page;
	void *src, *dst;
	struct mem_cgroup *memcg;
	struct cgroup_cache_page *cache;
	memcg_hybs_t *hybs;

	if (akcompressd_threads == 0)
		return 0;

	memcg = page->mem_cgroup;
	if (!memcg || !MEMCGRP_ITEM_DATA(memcg))
		return 0;

	hybs = MEMCGRP_ITEM_DATA(memcg);
	cache = &hybs->cache;
	if (find_and_fetch_memcg_cache(cache->id) != cache)
		return 0;

	spin_lock(&cache->lock);
	if (cache->dead == 1) {
		spin_unlock(&cache->lock);
		return 0;
	}
	spin_unlock(&cache->lock);

	dst_page = fetch_free_page();
	if (!dst_page)
		return 0;

	src = kmap_atomic(page);
	dst = kmap_atomic(dst_page);
	memcpy(dst, src, PAGE_SIZE);
	kunmap_atomic(src);
	kunmap_atomic(dst);

	dst_page->mem_cgroup = memcg;
	set_page_private(dst_page, mk_page_val(cache->id, index));
	update_zram_index(zram, index, (unsigned long)dst_page);
	atomic64_inc(&cached_cnt);
	wake_all_akcompressd();
	hybp(HYB_DEBUG, "add_anon_page2cache index %u page %p passed\n",
		index, dst_page);
	return 1;
}

static inline void akcompressd_try_to_sleep(wait_queue_head_t *waitq)
{
	DEFINE_WAIT(wait);

	prepare_to_wait(waitq, &wait, TASK_INTERRUPTIBLE);
	freezable_schedule();
	finish_wait(waitq, &wait);
}

static int akcompressd_func(void *data)
{
	struct page *page;
	int ret, thread_index;
	struct list_head compress_fail_list;
	struct cgroup_cache_page *cache = NULL;

	thread_index = (int)data;
	if (thread_index < 0 || thread_index >= MAX_AKCOMPRESSD_THREADS) {
		hybp(HYB_ERR, "akcompress task index %d is invalid.\n", thread_index);
		return -EINVAL;
	}

	set_freezable();
	while (!kthread_should_stop()) {
		akcompressd_try_to_sleep(&akcompressd_wait);
		count_swapd_event(AKCOMPRESSD_WAKEUP);

		cache = fetch_one_cache();
		if (!cache)
			continue;

finish_last_jobs:
		INIT_LIST_HEAD(&compress_fail_list);
		page = fetch_anon_page(zram_info, cache);
		while (page) {
			ret = async_compress_page(zram_info, page);
			put_memcg_cache(container_of(cache, memcg_hybs_t, cache));

			if (ret)
				list_add(&page->lru, &compress_fail_list);
			else {
				atomic64_inc(&akc_cnt[thread_index]);
				page->mem_cgroup = NULL;
				put_free_page(page);
			}
			page = fetch_anon_page(zram_info, cache);
		}

		if (!list_empty(&compress_fail_list))
			hybp(HYB_ERR, "have some compress failed pages.\n");

		if (kthread_should_stop()) {
			if (!can_stop_working(cache, thread_index))
				goto finish_last_jobs;
		}
		mark_compressing_stop(cache);
	}

	return 0;
}

static int update_akcompressd_threads(int thread_count, struct zram *zram)
{
        int drop, increase;
	int last_index, start_index, hid;
	static DEFINE_MUTEX(update_lock);

	if (thread_count < 0 || thread_count > MAX_AKCOMPRESSD_THREADS) {
		hybp(HYB_ERR, "thread_count %d is invalid\n", thread_count);
                return -EINVAL;
        }

	mutex_lock(&update_lock);
	if (!zram_info || zram_info != zram)
		zram_info = zram;

	if (thread_count == akcompressd_threads) {
		mutex_unlock(&update_lock);
                return thread_count;
	}

	last_index = akcompressd_threads - 1;
	if (thread_count < akcompressd_threads) {
		drop = akcompressd_threads - thread_count;
		for (hid = last_index; hid > (last_index - drop); hid--) {
			if (akc_task[hid]) {
				kthread_stop(akc_task[hid]);
				akc_task[hid] = NULL;
			}
		}
	} else {
		increase = thread_count - akcompressd_threads;
		start_index = last_index + 1;
		for (hid = start_index; hid < (start_index + increase); hid++) {
			if (unlikely(akc_task[hid]))
				BUG();
			akc_task[hid]= kthread_run(akcompressd_func,
				(void*)(unsigned long)hid, "akcompressd:%d", hid);
			if (IS_ERR(akc_task[hid])) {
				pr_err("Failed to start akcompressd%d\n", hid);
				akc_task[hid] = NULL;
				break;
			}
		}
	}

        hybp(HYB_INFO, "akcompressd_threads count changed, old:%d new:%d\n",
                akcompressd_threads, thread_count);
        akcompressd_threads = thread_count;
	mutex_unlock(&update_lock);

	return thread_count;
}

static void wake_all_akcompressd(void)
{
	if (atomic64_read(&cached_cnt) < WAKEUP_AKCOMPRESSD_WATERMARK)
		return;

	if (!waitqueue_active(&akcompressd_wait))
		return;

	wake_up_interruptible(&akcompressd_wait);
}

int create_akcompressd_task(struct zram *zram)
{
	return update_akcompressd_threads(1, zram) != 1;
}

void destroy_akcompressd_task(struct zram *zram)
{
	(void)update_akcompressd_threads(0, zram);
}

ssize_t hybridswap_akcompress_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	int ret;
	unsigned long val;
	struct zram *zram = dev_to_zram(dev);

	ret = kstrtoul(buf, 0, &val);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "val is error!\n");
		return -EINVAL;
	}

	ret = update_akcompressd_threads(val, zram);
	if (ret < 0) {
		hybp(HYB_ERR, "create task failed, val %d\n", val);
		return ret;
	}

	return len;
}

ssize_t hybridswap_akcompress_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int len = 0, id, i;
	struct cgroup_cache_page *cache = NULL;
	unsigned long cnt = atomic64_read(&cached_cnt);
	memcg_hybs_t *hybs;

	len += sprintf(buf + len, "akcompressd_threads: %d\n", akcompressd_threads);
	len += sprintf(buf + len, "cached page cnt: %lu\n", cnt);
	len += sprintf(buf + len, "free page cnt: %u\n", compress_info.free_cnt);

	for (i = 0; i < MAX_AKCOMPRESSD_THREADS; i++)
		len += sprintf(buf + len, "%-d %-d\n",	i, atomic64_read(&akc_cnt[i]));

	if (cnt == 0)
		return len;

	spin_lock(&cached_idr_lock);
	idr_for_each_entry(&cached_idr, cache, id) {
		hybs = container_of(cache, memcg_hybs_t, cache);
		if (cache->cnt == 0)
			continue;

		len += scnprintf(buf + len, PAGE_SIZE - len, "%s %d\n",
					hybs->name, cache->cnt);

		if (len >= PAGE_SIZE)
			break;
	}
	spin_unlock(&cached_idr_lock);

	return len;
}

void __init akcompressd_pre_init(void)
{
	int i;
	struct page *page;

	mutex_lock(&akcompress_init_lock);
	INIT_LIST_HEAD(&compress_info.free_page_head);
	spin_lock_init(&compress_info.free_lock);
	compress_info.free_cnt = 0;

	init_waitqueue_head(&akcompressd_wait);

	atomic64_set(&cached_cnt, 0);
	for (i = 0; i < MAX_AKCOMPRESSD_THREADS; i++)
		atomic64_set(&akc_cnt[i], 0);

	for (i = 0; i < DEFAULT_CACHE_COUNT; i ++) {
		page = alloc_page(GFP_KERNEL);

		if (page) {
			list_add_tail(&page->lru, &compress_info.free_page_head);
		} else
			break;
	}
	compress_info.free_cnt = i;
	mutex_unlock(&akcompress_init_lock);
}

void __exit akcompressd_pre_deinit(void)
{
	int i;
	struct page *page, *tmp;

	mutex_lock(&akcompress_init_lock);
	if (list_empty(&compress_info.free_page_head))
		goto out;
	list_for_each_entry_safe(page, tmp, &compress_info.free_page_head , lru) {
		list_del(&page->lru);
		free_page(page);
	}

out:
	compress_info.free_cnt = 0;
	mutex_unlock(&akcompress_init_lock);
}

int akcompress_cache_page_fault(struct zram *zram,
					struct page *page, u32 index)
{
	void *src, *dst;

	if (zram_test_flag(zram, index, ZRAM_CACHED)) {
		struct page *src_page = (struct page *)zram_fetch_page(zram, index);

		src = kmap_atomic(src_page);
		dst = kmap_atomic(page);
		memcpy(dst, src, PAGE_SIZE);
		kunmap_atomic(src);
		kunmap_atomic(dst);
		zram_slot_unlock(zram, index);

		hybp(HYB_DEBUG, "read_anon_page_from_cache index %u page %p passed, ZRAM_CACHED\n",
			index, src_page);
		return 1;
	}

	if  (zram_test_flag(zram, index, ZRAM_CACHED_COMPRESS)) {
		struct page *src_page = (struct page *)zram_fetch_page(zram, index);

		src = kmap_atomic(src_page);
		dst = kmap_atomic(page);
		memcpy(dst, src, PAGE_SIZE);
		kunmap_atomic(src);
		kunmap_atomic(dst);
		zram_slot_unlock(zram, index);

		hybp(HYB_DEBUG, "read_anon_page_from_cache index %u page %p passed, ZRAM_CACHED_COMPRESS\n",
			index, src_page);
		return 1;
	}

	return 0;
}
