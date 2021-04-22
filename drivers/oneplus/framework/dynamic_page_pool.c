// SPDX-License-Identifier: GPL-2.0+
//
/*
 * drivers/oneplus/framework/dynamic_page_pool.c
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/freezer.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/huge_mm.h>
#include <linux/freezer.h>
#include <linux/notifier.h>
#include <linux/vmpressure.h>

#include <oneplus/dynamic_page_pool.h>

#define GRACE_PERIOD		2000	/* 2 Sec */
#define HUGEPAGE_ORDER		9	/* 2^9 pages = 2 MB */

#define DPP_ANON_MIN		256
#define DPP_ANON_MAX		25600
#define	DPP_ANON_SLEEP_MS	1000

#define DPP_ANON_HUGEPAGE_MIN		0
#define DPP_ANON_HUGEPAGE_MAX		20
#define	DPP_ANON_HUGEPAGE_SLEEP_MS	1000

#define PAGES_TO_FREE		512
#define HIGH_PRESSURE		95
#define MID_PRESSURE		75

/*
 * ANON-ZEROED pages pool. This pool is first use-case of this framework.
 */
struct dynamic_page_pool *anon_pool;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
struct dynamic_page_pool *anon_hugepage_pool;
#endif

static void *dynamic_page_pool_alloc_pages(struct dynamic_page_pool *pool)
{
	struct page *page;

	page = alloc_pages(pool->gfp_mask & ~__GFP_ZERO, pool->order);
	if (!page)
		return NULL;

	if (pool->gfp_mask & __GFP_ZERO) {
		if (pool->order == 0)
			clear_user_highpage(page, 0);
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		else if (pool->order == HUGEPAGE_ORDER)
			clear_huge_page(page, 0, HPAGE_PMD_NR);
#endif
	}

	return page;
}

static void dynamic_page_pool_free_pages(struct dynamic_page_pool *pool,
				     struct page *page)
{
	__free_pages(page, pool->order);
}

static int dynamic_page_pool_add(struct dynamic_page_pool *pool,
							struct page *page)
{
	mutex_lock(&pool->mutex);
	list_add_tail(&page->lru, &pool->items);
	pool->count++;
	mutex_unlock(&pool->mutex);

	mod_node_page_state(page_pgdat(page), NR_INDIRECTLY_RECLAIMABLE_BYTES,
			    (1 << (PAGE_SHIFT + pool->order)));
	return 0;
}

static struct page *dynamic_page_pool_remove(struct dynamic_page_pool *pool)
{
	struct page *page;

	if (!mutex_trylock(&pool->mutex))
		return NULL;

	if (!pool->count) {
		mutex_unlock(&pool->mutex);
		return NULL;
	}

	page = list_first_entry(&pool->items, struct page, lru);
	pool->count--;
	list_del(&page->lru);
	mutex_unlock(&pool->mutex);
	mod_node_page_state(page_pgdat(page), NR_INDIRECTLY_RECLAIMABLE_BYTES,
			    -(1 << (PAGE_SHIFT + pool->order)));
	return page;
}

void *dynamic_page_pool_alloc(struct dynamic_page_pool *pool)
{
	struct page *page = NULL;

	BUG_ON(!pool);
	page = dynamic_page_pool_remove(pool);

	return page;
}

void dynamic_page_pool_free(struct dynamic_page_pool *pool, struct page *page)
{
	/* TODO: IMPROVEMENT:
	 * These are free pages. We can directly add to the pool. Not doing
	 * that now as we want to rely on one source for now, which is
	 * dynamic_page_pool_grow().
	 */
	dynamic_page_pool_free_pages(pool, page);
}

/*
 * @pool: The pool which we want to shrink
 * @nr_to_scan:  Number of 4KB pages we want to free
 *
 * Shrinker policy is, just do the minimum required free up as asked by
 * kswapd() or someone else.
 *
 * Ideally, we don't wish to do much of a shrink as it is a waste of
 * pooling and zeroing. So, we won't be much aggressive here and just
 * do the thing as asked.
 */
static unsigned int dynamic_page_pool_shrink(struct dynamic_page_pool *pool,
						int nr_to_scan, bool force)
{
	int count;
	unsigned int freed = 0;

	/*
	 * For nr_to_scan = 1 or 2, it is not worth freeing any single page
	 * with very high order (such as 9, means we free 512 pages)
	 */
	if (nr_to_scan == 0 || (nr_to_scan >> pool->order) == 0)
		return freed;

	/*
	 * We are interested to know when lastly shrinker has been called.
	 * Mainly for 2 reasons. First, we don't want to run refill funciton
	 * soon after shrinker has been called. Secondly, we also don't want
	 * to call shrinkers back to back.
	 */
	mutex_lock(&pool->mutex);
	if (!force && (pool->last_shrink_time + pool->sleep_millisecs >
			jiffies_to_msecs(jiffies))) {
		mutex_unlock(&pool->mutex);
		return freed;
	}

	pool->last_shrink_time = jiffies_to_msecs(jiffies);
	mutex_unlock(&pool->mutex);

	while (freed < nr_to_scan) {
		struct page *page;

		mutex_lock(&pool->mutex);
		count = pool->count;
		mutex_unlock(&pool->mutex);

		if (count > pool->min_reserve_pages) {
			page = dynamic_page_pool_remove(pool);
			if (!page)
				continue;
		} else {
			break;
		}

		dynamic_page_pool_free_pages(pool, page);
		freed += (1 << pool->order);
	}

	return freed;
}

/*
 * Policy to grow pool size is simple, just reach to the max limit. The max
 * limit is typically one big app's anon requirement. And we wish to be ready
 * when someone requests for anon pages.
 */
static int dynamic_page_pool_grow(struct dynamic_page_pool *pool)
{
	struct page *page;
	int refill_pages;

	mutex_lock(&pool->mutex);
	refill_pages = (pool->max_reserve_pages > pool->count) ?
				pool->max_reserve_pages - pool->count :
				0;
	mutex_unlock(&pool->mutex);

	while (refill_pages) {
		page = dynamic_page_pool_alloc_pages(pool);

		if (!page)
			break;

		dynamic_page_pool_add(pool, page);
		refill_pages--;
	}

	return refill_pages;
}

void dynamic_page_pool_set_max_pool_size(struct dynamic_page_pool *pool,
					unsigned long max_reserve_pages)
{
	mutex_lock(&pool->mutex);
	pool->max_reserve_pages = max_reserve_pages;

	if (pool->count - max_reserve_pages > 0) {
		mutex_unlock(&pool->mutex);
		/*
		 * While degrading pool size, we wish to have pool->count be
		 * operating in the same range as newly set.
		 */
		dynamic_page_pool_shrink(pool, (pool->count - max_reserve_pages) << pool->order, true);
		return;
	}

	mutex_unlock(&pool->mutex);
}

unsigned long dynamic_page_pool_get_max_pool_size(struct dynamic_page_pool *pool)
{
	unsigned long count;

	mutex_lock(&pool->mutex);
	count = pool->max_reserve_pages;
	mutex_unlock(&pool->mutex);
	return count;
}

void dynamic_page_pool_set_current_size(struct dynamic_page_pool *pool,
					unsigned int pages)
{
}

unsigned long dynamic_page_pool_get_current_size(struct dynamic_page_pool *pool)
{
	unsigned long count;

	mutex_lock(&pool->mutex);
	count = pool->count;
	mutex_unlock(&pool->mutex);
	return count;
}

void dynamic_page_pool_set_sleep_millisecs(struct dynamic_page_pool *pool,
					unsigned int sleep_millisecs)
{
	mutex_lock(&pool->mutex);
	pool->sleep_millisecs = sleep_millisecs;
	mutex_unlock(&pool->mutex);
}

unsigned long dynamic_page_pool_get_sleep_millisecs(
					struct dynamic_page_pool *pool)
{
	unsigned long sleep_millisecs;

	mutex_lock(&pool->mutex);
	sleep_millisecs = pool->sleep_millisecs;
	mutex_unlock(&pool->mutex);
	return sleep_millisecs;
}

static int dynamic_page_pool_thread(void *nothing)
{
	struct dynamic_page_pool *pool;
	unsigned long last_shrink_time;
	unsigned int sleep_millisecs;
	long diff;
	int short_of;

	pool = (struct dynamic_page_pool *) nothing;
	set_freezable();

	while (1) {
		mutex_lock(&pool->mutex);
		last_shrink_time = pool->last_shrink_time;
		sleep_millisecs = pool->sleep_millisecs;
		mutex_unlock(&pool->mutex);

		/*
		 * As per the grow policy, we want to rest for some
		 * GRACE_PERIOD time since the last shrinker executed,
		 * before we start new grow procedure. The reason here
		 * is obvious, we don't want to grow the pool and at
		 * the same time perform shrink operation on it.
		 */
		diff = last_shrink_time + GRACE_PERIOD - jiffies_to_msecs(jiffies);
		if (diff > 10 && diff < GRACE_PERIOD)
			freezable_schedule_timeout_interruptible(
					msecs_to_jiffies(diff));

		if (!pm_freezing) {
			short_of = dynamic_page_pool_grow(pool);
		} else {
			set_current_state(TASK_INTERRUPTIBLE);
			freezable_schedule();
		}

		freezable_schedule_timeout_interruptible(
					msecs_to_jiffies(sleep_millisecs));
	}

	return 0;
}

struct dynamic_page_pool *dynamic_page_pool_create(const char *name,
						gfp_t gfp_mask,
						unsigned long min_reserve_pages,
						unsigned long max_reserve_pages,
						unsigned int order,
						unsigned int sleep_millisecs)
{
	struct dynamic_page_pool *pool;

	pool = kmalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return NULL;

	pool->name = name;
	pool->count = 0;
	pool->gfp_mask = gfp_mask;
	pool->order = order;
	pool->min_reserve_pages = min_reserve_pages;
	pool->max_reserve_pages = max_reserve_pages;
	pool->sleep_millisecs = sleep_millisecs;
	pool->last_shrink_time = 0;
	mutex_init(&pool->mutex);
	INIT_LIST_HEAD(&pool->items);

	pool->thread = kthread_run(dynamic_page_pool_thread, pool, pool->name);
	if (IS_ERR(pool->thread)) {
		pr_err("%s thread couldn't start\n", pool->name);
		return NULL;
	}

	return pool;
}

void dynamic_page_pool_destroy(struct dynamic_page_pool *pool)
{
	kfree(pool);
}

static int vmpressure_notifier(struct notifier_block *nb,
			unsigned long action, void *data)
{
	unsigned long pressure = action;
	unsigned long freed = 0;

	if (!current_is_kswapd())
		return 0;

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	/*
	 * High pressure event needs at least 2 MB chunk at that time.
	 */
	if (pressure >= HIGH_PRESSURE) {
		freed = dynamic_page_pool_shrink(anon_hugepage_pool,
							PAGES_TO_FREE,
							false);
		return 0;
	}
#endif

	/*
	 * We don't want to free too much on medium pressure
	 */
	if (pressure >= MID_PRESSURE)
		freed = dynamic_page_pool_shrink(anon_pool,
							PAGES_TO_FREE/4,
							false);

	return 0;
}

static struct notifier_block vmpr_nb = {
	.notifier_call = vmpressure_notifier,
};

static int __init dynamic_page_pool_init(void)
{
	gfp_t pool_flags;

	/*
	 * Currently, we have an idea of only 2 pools. So, we define them
	 * explicitly. In case if it happens to add any new pool, it should
	 * be better to access each pool through some connecting data
	 * structure.
	 */
	pool_flags = __GFP_MOVABLE | GFP_HIGHUSER | __GFP_ZERO;
	anon_pool = dynamic_page_pool_create("anon_pool",
					pool_flags,
					DPP_ANON_MIN,
					DPP_ANON_MAX,
					0,
					DPP_ANON_SLEEP_MS);
	if (!anon_pool)
		pr_err("There is some error in creating anon_pool\n");

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	pool_flags = (GFP_TRANSHUGE | __GFP_ZERO);
	anon_hugepage_pool = dynamic_page_pool_create("anon_hugepage_pool",
					pool_flags,
					DPP_ANON_HUGEPAGE_MIN,
					DPP_ANON_HUGEPAGE_MAX,
					HUGEPAGE_ORDER,
					DPP_ANON_HUGEPAGE_SLEEP_MS);

	if (!anon_hugepage_pool)
		pr_err("There is some error in creating anon_hugepage_pool\n");
#endif

	vmpressure_notifier_register(&vmpr_nb);
	return 0;
}
device_initcall(dynamic_page_pool_init);
