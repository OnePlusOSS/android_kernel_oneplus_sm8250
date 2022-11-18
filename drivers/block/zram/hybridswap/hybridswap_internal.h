// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#ifndef HYBRIDSWAP_INTERNAL_H
#define HYBRIDSWAP_INTERNAL_H

#include <linux/sched.h>
#include <linux/zsmalloc.h>
#include <linux/timer.h>
#include <linux/device.h>
#include <linux/memcontrol.h>

#define ESWAP_SHIFT        15
#define ESWAP_SIZE         (1UL << ESWAP_SHIFT)
#define ESWAP_PG_CNT		(ESWAP_SIZE >> PAGE_SHIFT)
#define ESWAP_SECTOR_SIZE	(ESWAP_PG_CNT << 3)
#define ESWAP_MAX_OBJ_CNT  (30 * ESWAP_PG_CNT)
#define ESWAP_MASK (~(ESWAP_SIZE - 1))
#define ESWAP_ALIGN_UP(size)	((size + ESWAP_SIZE - 1) & ESWAP_MASK)

#define MAX_FAIL_RECORD_NUM 4
#define MAX_APP_GRADE 600

#define HYBRIDSWAP_QUOTA_DAY		0x280000000	/* 10G bytes */
#define HYBRIDSWAP_CHECK_GAP	86400		/* 24 hour */
#define MEM_CGROUP_NAME_MAX_LEN 32

#define MAX_RATIO 100
#define MIN_RATIO 0

enum {
	HYB_ERR = 0,
	HYB_WARN,
	HYB_INFO,
	HYB_DEBUG,
	HYB_MAX
};

void hybridswap_loglevel_set(int level);
int hybridswap_loglevel(void);

#define DUMP_STACK_ON_ERR 0
#define pt(l, f, ...)	pr_err("[%s]<%d:%s>:"f, #l, __LINE__, __func__, ##__VA_ARGS__)
static inline void pr_none(void) {}
#define hybp(l, f, ...) do {\
	(l <= hybridswap_loglevel()) ? pt(l, f, ##__VA_ARGS__) : pr_none();\
	if (DUMP_STACK_ON_ERR && l == HYB_ERR) dump_stack();\
} while (0)

enum hybridswap_class {
	HYB_RECLAIM_IN = 0,
	HYB_FAULT_OUT,
	HYB_BATCH_OUT,
	HYB_PRE_OUT,
	HYB_CLASS_BUTT
};

enum hybridswap_key_point {
	HYB_START = 0,
	HYB_INIT,
	HYB_IOENTRY_ALLOC,
	HYB_FIND_ESWAP,
	HYB_IO_ESWAP,
	HYB_SEGMENT_ALLOC,
	HYB_BIO_ALLOC,
	HYB_SUBMIT_BIO,
	HYB_END_IO,
	HYB_SCHED_WORK,
	HYB_END_WORK,
	HYB_CALL_BACK,
	HYB_WAKE_UP,
	HYB_ZRAM_LOCK,
	HYB_DONE,
	HYB_KYE_POINT_BUTT
};

enum hybridswap_mcg_member {
	MCG_ZRAM_STORED_SZ = 0,
	MCG_ZRAM_STORED_PG_SZ,
	MCG_DISK_STORED_SZ,
	MCG_DISK_STORED_PG_SZ,
	MCG_ANON_FAULT_CNT,
	MCG_DISK_FAULT_CNT,
	MCG_ESWAPOUT_CNT,
	MCG_ESWAPOUT_SZ,
	MCG_ESWAPIN_CNT,
	MCG_ESWAPIN_SZ,
	MCG_DISK_SPACE,
	MCG_DISK_SPACE_PEAK,
};

enum hybridswap_fail_point {
	HYB_FAULT_OUT_INIT_FAIL = 0,
	HYB_FAULT_OUT_ENTRY_ALLOC_FAIL,
	HYB_FAULT_OUT_IO_ENTRY_PARA_FAIL,
	HYB_FAULT_OUT_SEGMENT_ALLOC_FAIL,
	HYB_FAULT_OUT_BIO_ALLOC_FAIL,
	HYB_FAULT_OUT_BIO_ADD_FAIL,
	HYB_FAULT_OUT_IO_FAIL,
	HYBRIDSWAP_FAIL_POINT_BUTT
};

struct hybridswap_fail_record {
	unsigned char task_comm[TASK_COMM_LEN];
	enum hybridswap_fail_point point;
	ktime_t time;
	u32 index;
	int eswapid;
};

struct hybridswap_fail_record_info {
	int num;
	spinlock_t lock;
	struct hybridswap_fail_record record[MAX_FAIL_RECORD_NUM];
};

struct hybridswap_key_point_info {
	unsigned int record_cnt;
	unsigned int end_cnt;
	ktime_t first_time;
	ktime_t last_time;
	s64 proc_total_time;
	s64 proc_max_time;
	unsigned long long last_ravg_sum;
	unsigned long long proc_ravg_sum;
	spinlock_t time_lock;
};

struct hybridswap_key_point_record {
	struct timer_list lat_monitor;
	unsigned long warn_level;
	int page_cnt;
	int segment_cnt;
	int nice;
	bool timeout_flag;
	unsigned char task_comm[TASK_COMM_LEN];
	struct task_struct *task;
	enum hybridswap_class class;
	struct hybridswap_key_point_info key_point[HYB_KYE_POINT_BUTT];
};

struct hybridswapiowrkstat {
	atomic64_t total_lat;
	atomic64_t max_lat;
	atomic64_t timeout_cnt;
};

struct hybridswap_fault_timeout_cnt{
	atomic64_t timeout_100ms_cnt;
	atomic64_t timeout_500ms_cnt;
};

struct hybstatus {
	atomic64_t reclaimin_cnt;
	atomic64_t reclaimin_bytes;
	atomic64_t reclaimin_real_load;
	atomic64_t reclaimin_bytes_daily;
	atomic64_t reclaimin_pages;
	atomic64_t reclaimin_infight;
	atomic64_t batchout_cnt;
	atomic64_t batchout_bytes;
	atomic64_t batchout_real_load;
	atomic64_t batchout_pages;
	atomic64_t batchout_inflight;
	atomic64_t fault_cnt;
	atomic64_t hybridswap_fault_cnt;
	atomic64_t reout_pages;
	atomic64_t reout_bytes;
	atomic64_t zram_stored_pages;
	atomic64_t zram_stored_size;
	atomic64_t stored_pages;
	atomic64_t stored_size;
	atomic64_t notify_free;
	atomic64_t frag_cnt;
	atomic64_t mcg_cnt;
	atomic64_t eswap_cnt;
	atomic64_t miss_free;
	atomic64_t memcgid_clear;
	atomic64_t skip_track_cnt;
	atomic64_t used_swap_pages;
	atomic64_t null_memcg_skip_track_cnt;
	atomic64_t stored_wm_scale;
	atomic64_t dropped_eswap_size;
	atomic64_t io_fail_cnt[HYB_CLASS_BUTT];
	atomic64_t alloc_fail_cnt[HYB_CLASS_BUTT];
	struct hybridswapiowrkstat lat[HYB_CLASS_BUTT];
	struct hybridswap_fault_timeout_cnt fault_stat[2]; /* 0:bg 1:fg */
	struct hybridswap_fail_record_info record;
};

struct hybridswap_page_pool {
	struct list_head page_pool_list;
	spinlock_t page_pool_lock;
};

struct io_eswapent {
	int eswapid;
	struct zram *zram;
	struct mem_cgroup *mcg;
	struct page *pages[ESWAP_PG_CNT];
	u32 index[ESWAP_MAX_OBJ_CNT];
	int cnt;
	int real_load;

	struct hybridswap_page_pool *pool;
};

struct hybridswap_buffer {
	struct zram *zram;
	struct hybridswap_page_pool *pool;
	struct page **dest_pages;
};

struct hybridswap_entry {
	int eswapid;
	sector_t addr;
	struct page **dest_pages;
	int pages_sz;
	struct list_head list;
	void *private;
	void *manager_private;
};

struct hybridswap_io_req;
struct hybridswap_io {
	struct block_device *bdev;
	enum hybridswap_class class;
	void (*done_callback)(struct hybridswap_entry *, int, struct hybridswap_io_req *);
	void (*complete_notify)(void *);
	void *private;
	struct hybridswap_key_point_record *record;
};

struct hybridswap_io_req {
	struct hybridswap_io io_para;
	struct kref refcount;
	struct mutex refmutex;
	struct wait_queue_head io_wait;
	atomic_t eswap_doing;
	struct completion io_end_flag;
	struct hyb_sgm *segment;
	bool limit_doing_flag;
	bool wait_io_finish_flag;
	int page_cnt;
	int segment_cnt;
	int nice;
	atomic64_t real_load;
};

/* Change hybridswap_event_item, you should change swapd_text togather*/
enum hybridswap_event_item {
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	SWAPD_WAKEUP,
	SWAPD_REFAULT,
	SWAPD_MEMCG_RATIO_SKIP,
	SWAPD_MEMCG_REFAULT_SKIP,
	SWAPD_SHRINK_ANON,
	SWAPD_SWAPOUT,
	SWAPD_SKIP_SWAPOUT,
	SWAPD_EMPTY_ROUND,
	SWAPD_OVER_MIN_BUFFER_SKIP_TIMES,
	SWAPD_EMPTY_ROUND_SKIP_TIMES,
	SWAPD_SNAPSHOT_TIMES,
	SWAPD_SKIP_SHRINK_OF_WINDOW,
	SWAPD_MANUAL_PAUSE,
#ifdef CONFIG_OPLUS_JANK
	SWAPD_CPU_BUSY_SKIP_TIMES,
	SWAPD_CPU_BUSY_BREAK_TIMES,
#endif
#endif
#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
	AKCOMPRESSD_WAKEUP,
#endif
	NR_EVENT_ITEMS
};

struct swapd_event_state {
	unsigned long event[NR_EVENT_ITEMS];
};

#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
struct cgroup_cache_page {
	spinlock_t lock;
	struct list_head head;
	unsigned int cnt;
	int id;
	char compressing;
	char dead;
};
#endif

typedef struct mem_cgroup_hybridswap {
#ifdef CONFIG_HYBRIDSWAP
	atomic64_t ufs2zram_scale;
	atomic_t zram2ufs_scale;
	atomic64_t app_grade;
	atomic64_t app_uid;
	struct list_head grade_node;
	char name[MEM_CGROUP_NAME_MAX_LEN];
	struct zram *zram;
	struct mem_cgroup *memcg;
	refcount_t usage;
#endif
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	atomic_t mem2zram_scale;
	atomic_t pagefault_level;
	unsigned long long reclaimed_pagefault;
	long long can_reclaimed;
#endif
#ifdef CONFIG_HYBRIDSWAP_CORE
	unsigned long swap_sorted_list;
	unsigned long eswap_lru;
	struct list_head link_list;
	spinlock_t zram_init_lock;
	long long can_eswaped;

	atomic64_t zram_stored_size;
	atomic64_t zram_page_size;
	unsigned long zram_watermark;

	atomic_t hybridswap_extcnt;
	atomic_t hybridswap_peakextcnt;

	atomic64_t hybridswap_stored_pages;
	atomic64_t hybridswap_stored_size;
	atomic64_t hybridswap_eswap_notify_free;

	atomic64_t hybridswap_outcnt;
	atomic64_t hybridswap_incnt;
	atomic64_t hybridswap_allfaultcnt;
	atomic64_t hybridswap_faultcnt;

	atomic64_t hybridswap_outextcnt;
	atomic64_t hybridswap_inextcnt;

	struct mutex swap_lock;
	bool in_swapin;
	bool force_swapout;
#endif
#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
	struct cgroup_cache_page cache;
#endif
}memcg_hybs_t;

#define MEMCGRP_ITEM_DATA(memcg) ((memcg_hybs_t *)(memcg)->android_oem_data1)
#define MEMCGRP_ITEM(memcg, item) (MEMCGRP_ITEM_DATA(memcg)->item)

extern void __put_memcg_cache(memcg_hybs_t *hybs);

static inline memcg_hybs_t *fetch_memcg_cache(memcg_hybs_t *hybs)
{
	refcount_inc(&hybs->usage);
	return hybs;
}

static inline void put_memcg_cache(memcg_hybs_t *hybs)
{
	if (refcount_dec_and_test(&hybs->usage))
		__put_memcg_cache(hybs);
}

DECLARE_PER_CPU(struct swapd_event_state, swapd_event_states);
extern struct mutex reclaim_para_lock;

static inline void __count_swapd_event(enum hybridswap_event_item item)
{
	raw_cpu_inc(swapd_event_states.event[item]);
}

static inline void count_swapd_event(enum hybridswap_event_item item)
{
	this_cpu_inc(swapd_event_states.event[item]);
}

static inline void __count_swapd_events(enum hybridswap_event_item item, long delta)
{
	raw_cpu_add(swapd_event_states.event[item], delta);
}

static inline void count_swapd_events(enum hybridswap_event_item item, long delta)
{
	this_cpu_add(swapd_event_states.event[item], delta);
}

void *hybridswap_malloc(size_t size, bool fast, bool nofail);
void hybridswap_free(const void *mem);
unsigned long hybridswap_zsmalloc(struct zs_pool *zs_pool,
		size_t size, struct hybridswap_page_pool *pool);
struct page *hybridswap_alloc_page(
		struct hybridswap_page_pool *pool, gfp_t gfp,
		bool fast, bool nofail);
void hybridswap_page_recycle(struct page *page,
		struct hybridswap_page_pool *pool);
struct hybstatus *hybridswap_fetch_stat_obj(void);
int hybridswap_manager_init(struct zram *zram);
void hybridswap_manager_memcg_init(struct zram *zram,
		struct mem_cgroup *memcg);
void hybridswap_manager_memcg_deinit(struct mem_cgroup *mcg);
void hybridswap_swap_sorted_list_add(struct zram *zram, u32 index,
		struct mem_cgroup *memcg);
void hybridswap_swap_sorted_list_del(struct zram *zram, u32 index);
unsigned long hybridswap_eswap_create(struct mem_cgroup *memcg,
		int *eswapid,
		struct hybridswap_buffer *dest_buf,
		void **private);
void hybridswap_eswap_register(void *private, struct hybridswap_io_req *req);
void hybridswap_eswap_objs_del(struct zram *zram, u32 index);
int hybridswap_find_eswap_by_index(
		unsigned long eswpentry, struct hybridswap_buffer *buf, void **private);
int hybridswap_find_eswap_by_memcg(
		struct mem_cgroup *mcg,
		struct hybridswap_buffer *dest_buf, void **private);
void hybridswap_eswap_destroy(void *private, enum hybridswap_class class);
void hybridswap_eswap_exception(enum hybridswap_class class,
		void *private);
void hybridswap_manager_deinit(struct zram *zram);
struct mem_cgroup *hybridswap_zram_fetch_mcg(struct zram *zram, u32 index);
int hyb_io_work_begin(void);
void *hybridswap_plug_start(struct hybridswap_io *io_para);
int hybridswap_read_eswap(void *iohandle,
		struct hybridswap_entry *ioentry);
int hybridswap_write_eswap(void *iohandle,
		struct hybridswap_entry *ioentry);

int hybridswap_plug_finish(void *iohandle);

void hybperf_start(
		struct hybridswap_key_point_record *record,
		ktime_t stsrt, unsigned long long start_ravg_sum,
		enum hybridswap_class class);

void hybperf_end(struct hybridswap_key_point_record *record);

void hybperfiowrkstart(
		struct hybridswap_key_point_record *record,
		enum hybridswap_key_point type);

void hybperfiowrkend(
		struct hybridswap_key_point_record *record,
		enum hybridswap_key_point type);

void hybperfiowrkpoint(
		struct hybridswap_key_point_record *record,
		enum hybridswap_key_point type);

void hybperf_async_perf(
		struct hybridswap_key_point_record *record,
		enum hybridswap_key_point type, ktime_t start,
		unsigned long long start_ravg_sum);

void hybperf_io_stat(
		struct hybridswap_key_point_record *record, int page_cnt,
		int segment_cnt);

static inline unsigned long long hybridswap_fetch_ravg_sum(void)
{
	return 0;
}

void hybridswap_fail_record(enum hybridswap_fail_point point,
		u32 index, int eswapid, unsigned char *task_comm);
bool hybridswap_reach_life_protect(void);
struct workqueue_struct *hybridswap_fetch_reclaim_workqueue(void);
extern struct mem_cgroup *fetch_next_memcg(struct mem_cgroup *prev);
extern void fetch_next_memcg_break(struct mem_cgroup *prev);
extern memcg_hybs_t *hybridswap_cache_alloc(struct mem_cgroup *memcg, bool atomic);
extern void memcg_app_grade_resort(void);
extern unsigned long memcg_anon_pages(struct mem_cgroup *memcg);

#ifdef CONFIG_HYBRIDSWAP_CORE
extern bool hybridswap_core_enabled(void);
extern bool hybridswap_out_to_eswap_enable(void);
extern void hybridswap_mem_cgroup_deinit(struct mem_cgroup *memcg);
extern unsigned long hybridswap_out_to_eswap(unsigned long size);
extern int hybridswap_batches(struct mem_cgroup *mcg,
		unsigned long size, bool preload);
extern unsigned long zram_zsmalloc(struct zs_pool *zs_pool,
		size_t size, gfp_t gfp);
extern struct task_struct *fetch_task_from_proc(struct inode *inode);
extern unsigned long long hybridswap_fetch_zram_pagefault(void);
extern bool hybridswap_reclaim_work_running(void);
extern void hybridswap_force_reclaim(struct mem_cgroup *mcg);
extern bool hybridswap_stored_wm_ok(void);
extern void mem_cgroup_id_remove_hook(void *data, struct mem_cgroup *memcg);
extern int mem_cgroup_stored_wm_scale_write(
	struct cgroup_subsys_state *css, struct cftype *cft, s64 val);
extern s64 mem_cgroup_stored_wm_scale_read(
	struct cgroup_subsys_state *css, struct cftype *cft);
extern bool hybridswap_delete(struct zram *zram, u32 index);
extern int hybridswap_stored_info(unsigned long *total, unsigned long *used);

extern unsigned long long hybridswap_read_mcg_stats(
	struct mem_cgroup *mcg, enum hybridswap_mcg_member mcg_member);
extern int hybridswap_core_enable(void);
extern void hybridswap_core_disable(void);
extern int hybridswap_psi_show(struct seq_file *m, void *v);
#else
static inline unsigned long long hybridswap_read_mcg_stats(
        struct mem_cgroup *mcg, enum hybridswap_mcg_member mcg_member)
{
	return 0;
}

static inline unsigned long long hybridswap_fetch_zram_pagefault(void)
{
	return 0;
}

static inline bool hybridswap_reclaim_work_running(void)
{
	return false;
}

static inline bool hybridswap_core_enabled(void) { return false; }
static inline bool hybridswap_out_to_eswap_enable(void) { return false; }
#endif

#ifdef CONFIG_HYBRIDSWAP_SWAPD
extern atomic_long_t page_fault_pause;
extern atomic_long_t page_fault_pause_cnt;
extern struct cftype mem_cgroup_swapd_legacy_files[];
extern bool zram_watermark_ok(void);
extern void wake_all_swapd(void);
extern void alloc_pages_slowpath_hook(void *data, gfp_t gfp_mask,
        unsigned int order, unsigned long delta);
extern void rmqueue_hook(void *data, struct zone *preferred_zone,
	struct zone *zone, unsigned int order, gfp_t gfp_flags,
	unsigned int alloc_flags, int migratetype);
extern void __init swapd_pre_init(void);
extern void swapd_pre_deinit(void);
extern void update_swapd_mcg_setup(struct mem_cgroup *memcg);
extern bool free_zram_is_ok(void);
extern unsigned long fetch_nr_zram_total(void);
extern int swapd_init(struct zram *zram);
extern void swapd_exit(void);
extern bool hybridswap_swapd_enabled(void);
#else
static inline bool hybridswap_swapd_enabled(void) { return false; }
#endif

#ifdef CONFIG_HYBRIDSWAP_ASYNC_COMPRESS
extern spinlock_t cached_idr_lock;
extern struct idr cached_idr;

extern void __init akcompressd_pre_init(void);
extern void __exit akcompressd_pre_deinit(void);
extern int create_akcompressd_task(struct zram *zram);
extern void clear_page_memcg(struct cgroup_cache_page *cache);
#endif

#endif /* end of HYBRIDSWAP_INTERNAL_H */
