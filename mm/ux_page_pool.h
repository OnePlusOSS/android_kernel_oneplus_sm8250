#ifndef _UX_PAGE_POOL_H
#define _UX_PAGE_POOL_H

#include <linux/kref.h>
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/types.h>
/*
#ifdef CONFIG_OPLUS_UXMEM_OPT
#define UXPAGEPOOL_DEBUG 0
#endif
*/

#define UXMEM_POOL_MAX_PAGES 2

#define HIGHATOMIC_MIN_RESERVED_PAGES (SZ_16M >> PAGE_SHIFT)
extern int ux_page_pool_enable;
enum UX_POOL_MIGRATETYPE{
	UX_POOL_MIGRATETYPE_UNMOVABLE,
	UX_POOL_MIGRATETYPE_MOVABLE,
	UX_POOL_MIGRATETYPE_TYPES_SIZE
};
struct ux_page_pool {
    int low[UX_POOL_MIGRATETYPE_TYPES_SIZE];
    int high[UX_POOL_MIGRATETYPE_TYPES_SIZE];
    int count[UX_POOL_MIGRATETYPE_TYPES_SIZE];
    struct list_head items[UX_POOL_MIGRATETYPE_TYPES_SIZE];
    spinlock_t lock;
    unsigned int order;
    gfp_t gfp_mask;
};

struct page *ux_page_pool_alloc_pages(unsigned int order, int migratetype, bool may_retry);
int ux_page_pool_refill(struct page *page, unsigned int order, int migratetype);
#endif /* _UX_PAGE_POOL_H */
