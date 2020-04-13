/* SPDX-License-Identifier: GPL-2.0 */

/*
 * include/oneplus/dynamic_page_pool.h
 *
 * Copyright (C) 2019 OnePlus.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _DYNAMIC_PAGE_POOL_PRIV_H
#define _DYNAMIC_PAGE_POOL_PRIV_H

#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/kref.h>
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/device.h>

/**
 * struct dynamic_page_pool - pagepool struct
 * @name:		name of the pool/thread
 * @count:		number of items in the pool
 * @items:		list of lowmem items
 * @mutex:		lock protecting this struct and especially the count
 *			item list
 * @gfp_mask:		gfp_mask to use from alloc
 * @order:		order of pages in the pool
 * @min_reserve_pages:  minimum guranteed pages in the pool
 * @max_reserve_pages:  max limit on number of pages in the pool
 * @thread:		Dedicated thread for the pool which does refilling
 * @last_shrink_time:	stores last time when shrinker started (called)
 *
 * Allows you to keep a pool of pre allocated pages to use from your heap.
 * Keeping a pool of pages that is ready for uses avoids allocation & zeroing
 * (if applicable) latencies & provides a significant performance benefit
 * on many systems
 */
struct dynamic_page_pool {
	const char *name;
	int count;
	struct list_head items;
	struct mutex mutex;
	gfp_t gfp_mask;
	unsigned int order;
	unsigned long min_reserve_pages;
	unsigned long max_reserve_pages;
	unsigned long sleep_millisecs;
	struct task_struct *thread;
	unsigned long last_shrink_time;
};

extern struct dynamic_page_pool *anon_pool;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
extern struct dynamic_page_pool *anon_hugepage_pool;
#endif

/* APIs */
#ifdef CONFIG_DYNAMIC_PAGE_POOL
struct dynamic_page_pool *dynamic_page_pool_create(const char *name,
					gfp_t gfp_mask,
					unsigned long min_reserve_pages,
					unsigned long max_reserve_pages,
					unsigned int order,
					unsigned int sleep_millisecs);
void dynamic_page_pool_destroy(struct dynamic_page_pool *pool);

void *dynamic_page_pool_alloc(struct dynamic_page_pool *pool);
void dynamic_page_pool_free(struct dynamic_page_pool *pool,
					struct page *page);

void dynamic_page_pool_set_max_pool_size(struct dynamic_page_pool *pool,
					unsigned long max_reserve_pages);
unsigned long dynamic_page_pool_get_max_pool_size(
					struct dynamic_page_pool *pool);

void dynamic_page_pool_set_current_size(struct dynamic_page_pool *pool,
					unsigned int pages);
unsigned long dynamic_page_pool_get_current_size(
					struct dynamic_page_pool *pool);

void dynamic_page_pool_set_sleep_millisecs(struct dynamic_page_pool *pool,
					unsigned int sleep_millisecs);
unsigned long dynamic_page_pool_get_sleep_millisecs(
					struct dynamic_page_pool *pool);
#else
static inline void *dynamic_page_pool_alloc(struct dynamic_page_pool *pool)
{
	return NULL;
}

static inline void dynamic_page_pool_free(struct dynamic_page_pool *pool,
					struct page *page) {}

static inline void dynamic_page_pool_set_max_pool_size(
					struct dynamic_page_pool *pool,
					unsigned long max_reserve_pages) {}
static inline unsigned long dynamic_page_pool_get_max_pool_size(
					struct dynamic_page_pool *pool)
{
	return 0;
}

static inline void dynamic_page_pool_set_current_size(struct dynamic_page_pool *pool,
					unsigned int pages) {}
static inline unsigned long dynamic_page_pool_get_current_size(
					struct dynamic_page_pool *pool)
{
	return 0;
}

static inline void dynamic_page_pool_set_sleep_millisecs(
					struct dynamic_page_pool *pool,
					unsigned int sleep_millisecs) {}
static inline unsigned long dynamic_page_pool_get_sleep_millisecs(
					struct dynamic_page_pool *pool)
{
	return 0;
}
#endif	/* CONFIG_DYNAMIC_PAGE_POOL */
#endif /* _DYNAMIC_PAGE_POOL_PRIV_H */
