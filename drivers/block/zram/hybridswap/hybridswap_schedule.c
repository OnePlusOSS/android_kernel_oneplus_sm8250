// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[HYBRIDSWAP]" fmt

#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/bio.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/sched/task.h>
#include <linux/list.h>
#include <linux/random.h>
#include <linux/zsmalloc.h>
#include <linux/delay.h>

#include "hybridswap_internal.h"

/* default key index is zero */
#define HYBRIDSWAP_KEY_INDEX 0
#define HYBRIDSWAP_KEY_SIZE 64
#define HYBRIDSWAP_KEY_INDEX_SHIFT 3

#define HYBRIDSWAP_MAX_INFILGHT_NUM 256

#define HYBRIDSWAP_SECTOR_SHIFT 9
#define HYBRIDSWAP_PAGE_SIZE_SECTOR (PAGE_SIZE >> HYBRIDSWAP_SECTOR_SHIFT)

#define HYBRIDSWAP_READ_TIME 10
#define HYBRIDSWAP_WRITE_TIME 100
#define HYBRIDSWAP_FAULT_OUT_TIME 10

struct hybridswap_segment_time {
	ktime_t submit_bio;
	ktime_t end_io;
};

struct hybridswap_segment {
	sector_t segment_sector;
	int extent_cnt;
	int page_cnt;
	struct list_head io_entries;
	struct hybridswap_entry *io_entries_fifo[BIO_MAX_PAGES];
	struct work_struct endio_work;
	struct hybridswap_io_req *req;
	struct hybridswap_segment_time time;
	u32 bio_result;
};

static u8 hybridswap_io_key[HYBRIDSWAP_KEY_SIZE];
#ifdef CONFIG_SCSI_UFS_ENHANCED_INLINE_CRYPTO_V3
static u8 hybridswap_io_metadata[METADATA_BYTE_IN_KDF];
#endif
static struct workqueue_struct *hybridswap_proc_read_workqueue;
static struct workqueue_struct *hybridswap_proc_write_workqueue;
bool hybridswap_schedule_init_flag;

static void hybridswap_stat_io_bytes(struct hybridswap_io_req *req)
{
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();

	if (!stat || !req->page_cnt)
		return;

	if (req->io_para.scenario == HYBRIDSWAP_RECLAIM_IN) {
		atomic64_add(req->page_cnt * PAGE_SIZE, &stat->reclaimin_bytes);
		atomic64_add(req->page_cnt * PAGE_SIZE, &stat->reclaimin_bytes_daily);
		atomic64_add(atomic64_read(&req->real_load), &stat->reclaimin_real_load);
		atomic64_inc(&stat->reclaimin_cnt);
	} else {
		atomic64_add(req->page_cnt * PAGE_SIZE, &stat->batchout_bytes);
		atomic64_inc(&stat->batchout_cnt);
	}
}

static void hybridswap_key_init(void)
{
	get_random_bytes(hybridswap_io_key, HYBRIDSWAP_KEY_SIZE);
#ifdef CONFIG_SCSI_UFS_ENHANCED_INLINE_CRYPTO_V3
	get_random_bytes(hybridswap_io_metadata, METADATA_BYTE_IN_KDF);
#endif
}

static void hybridswap_io_req_release(struct kref *ref)
{
	struct hybridswap_io_req *req =
		container_of(ref, struct hybridswap_io_req, refcount);

	if (req->io_para.complete_notify && req->io_para.private)
		req->io_para.complete_notify(req->io_para.private);

	kfree(req);
}

static void hybridswap_segment_free(struct hybridswap_io_req *req,
	struct hybridswap_segment *segment)
{
	int i;

	for (i = 0; i < segment->extent_cnt; ++i) {
		INIT_LIST_HEAD(&segment->io_entries_fifo[i]->list);
		req->io_para.done_callback(segment->io_entries_fifo[i], -EIO, req);
	}
	kfree(segment);
}

static void hybridswap_limit_inflight(struct hybridswap_io_req *req)
{
	int ret;

	if (!req->limit_inflight_flag)
		return;

	if (atomic_read(&req->extent_inflight) >= HYBRIDSWAP_MAX_INFILGHT_NUM) {
		do {
			hybp(HYB_DEBUG, "wait inflight start\n");
			ret = wait_event_timeout(req->io_wait,
					atomic_read(&req->extent_inflight) <
					HYBRIDSWAP_MAX_INFILGHT_NUM,
					msecs_to_jiffies(100));
		} while (!ret);
	}
}

static void hybridswap_wait_io_finish(struct hybridswap_io_req *req)
{
	int ret;
	unsigned int wait_time;

	if (!req->wait_io_finish_flag || !req->page_cnt)
		return;

	if (req->io_para.scenario == HYBRIDSWAP_FAULT_OUT) {
		hybp(HYB_DEBUG, "fault out wait finish start\n");
		wait_for_completion_io_timeout(&req->io_end_flag,
				MAX_SCHEDULE_TIMEOUT);

		return;
	}

	wait_time = (req->io_para.scenario == HYBRIDSWAP_RECLAIM_IN) ?
		HYBRIDSWAP_WRITE_TIME : HYBRIDSWAP_READ_TIME;

	do {
		hybp(HYB_DEBUG, "wait finish start\n");
		ret = wait_event_timeout(req->io_wait,
			(!atomic_read(&req->extent_inflight)),
			msecs_to_jiffies(wait_time));
	} while (!ret);
}

static void hybridswap_inflight_inc(struct hybridswap_segment *segment)
{
	mutex_lock(&segment->req->refmutex);
	kref_get(&segment->req->refcount);
	mutex_unlock(&segment->req->refmutex);
	atomic_add(segment->page_cnt, &segment->req->extent_inflight);
}

static void hybridswap_inflight_dec(struct hybridswap_io_req *req,
	int num)
{
	if ((atomic_sub_return(num, &req->extent_inflight) <
		HYBRIDSWAP_MAX_INFILGHT_NUM) && req->limit_inflight_flag &&
		wq_has_sleeper(&req->io_wait))
		wake_up(&req->io_wait);
}

static void hybridswap_io_end_wake_up(struct hybridswap_io_req *req)
{
	if (req->io_para.scenario == HYBRIDSWAP_FAULT_OUT) {
		complete(&req->io_end_flag);
		return;
	}

	if (wq_has_sleeper(&req->io_wait))
		wake_up(&req->io_wait);
}

static void hybridswap_io_entry_proc(struct hybridswap_segment *segment)
{
	int i;
	struct hybridswap_io_req *req = segment->req;
	struct hybridswap_key_point_record *record = req->io_para.record;
	int page_num;
	ktime_t callback_start;
	unsigned long long callback_start_ravg_sum;

	for (i = 0; i < segment->extent_cnt; ++i) {
		INIT_LIST_HEAD(&segment->io_entries_fifo[i]->list);
		page_num = segment->io_entries_fifo[i]->pages_sz;
		hybp(HYB_DEBUG, "extent_id[%d] %d page_num %d\n",
			i, segment->io_entries_fifo[i]->ext_id, page_num);
		callback_start = ktime_get();
		callback_start_ravg_sum = hybridswap_get_ravg_sum();
		if (req->io_para.done_callback)
			req->io_para.done_callback(segment->io_entries_fifo[i],
				0, req);
		hybridswap_perf_async_perf(record, HYBRIDSWAP_CALL_BACK,
			callback_start, callback_start_ravg_sum);
		hybridswap_inflight_dec(req, page_num);
	}
}

static void hybridswap_io_err_record(enum hybridswap_fail_point point,
	struct hybridswap_io_req *req, int ext_id)
{
	if (req->io_para.scenario == HYBRIDSWAP_FAULT_OUT)
		hybridswap_fail_record(point, 0, ext_id,
			req->io_para.record->task_comm);
}

static void hybridswap_stat_io_fail(enum hybridswap_scenario scenario)
{
	struct hybridswap_stat *stat = hybridswap_get_stat_obj();

	if (!stat || (scenario >= HYBRIDSWAP_SCENARIO_BUTT))
		return;

	atomic64_inc(&stat->io_fail_cnt[scenario]);
}

static void hybridswap_io_err_proc(struct hybridswap_io_req *req,
	struct hybridswap_segment *segment)
{
	hybp(HYB_ERR, "segment sector 0x%llx, extent_cnt %d\n",
		segment->segment_sector, segment->extent_cnt);
	hybp(HYB_ERR, "scenario %u, bio_result %u\n",
		req->io_para.scenario, segment->bio_result);
	hybridswap_stat_io_fail(req->io_para.scenario);
	hybridswap_io_err_record(HYBRIDSWAP_FAULT_OUT_IO_FAIL, req,
		segment->io_entries_fifo[0]->ext_id);
	hybridswap_inflight_dec(req, segment->page_cnt);
	hybridswap_io_end_wake_up(req);
	hybridswap_segment_free(req, segment);
	kref_put_mutex(&req->refcount, hybridswap_io_req_release,
		&req->refmutex);
}

static void hybridswap_io_end_work(struct work_struct *work)
{
	struct hybridswap_segment *segment =
		container_of(work, struct hybridswap_segment, endio_work);
	struct hybridswap_io_req *req = segment->req;
	struct hybridswap_key_point_record *record = req->io_para.record;
	int old_nice = task_nice(current);
	ktime_t work_start;
	unsigned long long work_start_ravg_sum;

	if (unlikely(segment->bio_result)) {
		hybridswap_io_err_proc(req, segment);
		return;
	}

	hybp(HYB_DEBUG, "segment sector 0x%llx, extent_cnt %d passed\n",
		segment->segment_sector, segment->extent_cnt);
	hybp(HYB_DEBUG, "scenario %u, bio_result %u passed\n",
		req->io_para.scenario, segment->bio_result);

	set_user_nice(current, req->nice);

	hybridswap_perf_async_perf(record, HYBRIDSWAP_SCHED_WORK,
		segment->time.end_io, 0);
	work_start = ktime_get();
	work_start_ravg_sum = hybridswap_get_ravg_sum();

	hybridswap_io_entry_proc(segment);

	hybridswap_perf_async_perf(record, HYBRIDSWAP_END_WORK, work_start,
		work_start_ravg_sum);

	hybridswap_io_end_wake_up(req);

	kref_put_mutex(&req->refcount, hybridswap_io_req_release,
		&req->refmutex);
	kfree(segment);

	set_user_nice(current, old_nice);
}

static void hybridswap_end_io(struct bio *bio)
{
	struct hybridswap_segment *segment = bio->bi_private;
	struct hybridswap_io_req *req = NULL;
	struct workqueue_struct *workqueue = NULL;
	struct hybridswap_key_point_record *record = NULL;

	if (unlikely(!segment || !(segment->req))) {
		hybp(HYB_ERR, "segment or req null\n");
		bio_put(bio);

		return;
	}

	req = segment->req;
	record = req->io_para.record;

	hybridswap_perf_async_perf(record, HYBRIDSWAP_END_IO,
		segment->time.submit_bio, 0);

	workqueue = (req->io_para.scenario == HYBRIDSWAP_RECLAIM_IN) ?
		hybridswap_proc_write_workqueue : hybridswap_proc_read_workqueue;
	segment->time.end_io = ktime_get();
	segment->bio_result = bio->bi_status;

	queue_work(workqueue, &segment->endio_work);
	bio_put(bio);
}

static bool hybridswap_ext_merge_back(
	struct hybridswap_segment *segment,
	struct hybridswap_entry *io_entry)
{
	struct hybridswap_entry *tail_io_entry =
		list_last_entry(&segment->io_entries,
			struct hybridswap_entry, list);

	return ((tail_io_entry->addr +
		tail_io_entry->pages_sz * HYBRIDSWAP_PAGE_SIZE_SECTOR) ==
		io_entry->addr);
}

static bool hybridswap_ext_merge_front(
	struct hybridswap_segment *segment,
	struct hybridswap_entry *io_entry)
{
	struct hybridswap_entry *head_io_entry =
		list_first_entry(&segment->io_entries,
			struct hybridswap_entry, list);

	return (head_io_entry->addr ==
		(io_entry->addr +
		io_entry->pages_sz * HYBRIDSWAP_PAGE_SIZE_SECTOR));
}

static bool hybridswap_ext_merge(struct hybridswap_io_req *req,
	struct hybridswap_entry *io_entry)
{
	struct hybridswap_segment *segment = req->segment;

	if (segment == NULL)
		return false;

	if ((segment->page_cnt + io_entry->pages_sz) > BIO_MAX_PAGES)
		return false;

	if (hybridswap_ext_merge_front(segment, io_entry)) {
		list_add(&io_entry->list, &segment->io_entries);
		segment->io_entries_fifo[segment->extent_cnt++] = io_entry;
		segment->segment_sector = io_entry->addr;
		segment->page_cnt += io_entry->pages_sz;
		return true;
	}

	if (hybridswap_ext_merge_back(segment, io_entry)) {
		list_add_tail(&io_entry->list, &segment->io_entries);
		segment->io_entries_fifo[segment->extent_cnt++] = io_entry;
		segment->page_cnt += io_entry->pages_sz;
		return true;
	}

	return false;
}

static struct bio *hybridswap_bio_alloc(enum hybridswap_scenario scenario)
{
	gfp_t gfp = (scenario != HYBRIDSWAP_RECLAIM_IN) ? GFP_ATOMIC : GFP_NOIO;
	struct bio *bio = bio_alloc(gfp, BIO_MAX_PAGES);

	if (!bio && (scenario == HYBRIDSWAP_FAULT_OUT))
		bio = bio_alloc(GFP_NOIO, BIO_MAX_PAGES);

	return bio;
}

static int hybridswap_bio_add_page(struct bio *bio,
	struct hybridswap_segment *segment)
{
	int i;
	int k = 0;
	struct hybridswap_entry *io_entry = NULL;
	struct hybridswap_entry *tmp = NULL;

	list_for_each_entry_safe(io_entry, tmp, &segment->io_entries, list)  {
		for (i = 0; i < io_entry->pages_sz; i++) {
			io_entry->dest_pages[i]->index =
				bio->bi_iter.bi_sector + k;
			if (unlikely(!bio_add_page(bio,
				io_entry->dest_pages[i], PAGE_SIZE, 0))) {
				return -EIO;
			}
			k += HYBRIDSWAP_PAGE_SIZE_SECTOR;
		}
	}

	return 0;
}

static void hybridswap_set_bio_opf(struct bio *bio,
	struct hybridswap_segment *segment)
{
	if (segment->req->io_para.scenario == HYBRIDSWAP_RECLAIM_IN) {
		bio->bi_opf |= REQ_BACKGROUND;
		return;
	}

	bio->bi_opf |= REQ_SYNC;
}

int hybridswap_submit_bio(struct hybridswap_segment *segment)
{
	unsigned int op =
		(segment->req->io_para.scenario == HYBRIDSWAP_RECLAIM_IN) ?
		REQ_OP_WRITE : REQ_OP_READ;
	struct hybridswap_entry *head_io_entry =
		list_first_entry(&segment->io_entries,
			struct hybridswap_entry, list);
	struct hybridswap_key_point_record *record =
		segment->req->io_para.record;
	struct bio *bio = NULL;

	hybridswap_perf_lat_start(record, HYBRIDSWAP_BIO_ALLOC);
	bio = hybridswap_bio_alloc(segment->req->io_para.scenario);
	hybridswap_perf_lat_end(record, HYBRIDSWAP_BIO_ALLOC);
	if (unlikely(!bio)) {
		hybp(HYB_ERR, "bio is null.\n");
		hybridswap_io_err_record(HYBRIDSWAP_FAULT_OUT_BIO_ALLOC_FAIL,
			segment->req, segment->io_entries_fifo[0]->ext_id);

		return -ENOMEM;
	}

	bio->bi_iter.bi_sector = segment->segment_sector;
	bio_set_dev(bio, segment->req->io_para.bdev);
	bio->bi_private = segment;
	bio_set_op_attrs(bio, op, 0);
	bio->bi_end_io = hybridswap_end_io;
	hybridswap_set_bio_opf(bio, segment);

	if (unlikely(hybridswap_bio_add_page(bio, segment))) {
		bio_put(bio);
		hybp(HYB_ERR, "bio_add_page fail\n");
		hybridswap_io_err_record(HYBRIDSWAP_FAULT_OUT_BIO_ADD_FAIL,
			segment->req, segment->io_entries_fifo[0]->ext_id);

		return -EIO;
	}

	hybridswap_inflight_inc(segment);
	hybp(HYB_DEBUG, "submit bio sector %llu ext_id %d\n",
		segment->segment_sector, head_io_entry->ext_id);
	hybp(HYB_DEBUG, "extent_cnt %d scenario %u\n",
		segment->extent_cnt, segment->req->io_para.scenario);

	segment->req->page_cnt += segment->page_cnt;
	segment->req->segment_cnt++;
	segment->time.submit_bio = ktime_get();

	hybridswap_perf_lat_start(record, HYBRIDSWAP_SUBMIT_BIO);
	submit_bio(bio);
	hybridswap_perf_lat_end(record, HYBRIDSWAP_SUBMIT_BIO);

	return 0;
}

static int hybridswap_new_segment_init(struct hybridswap_io_req *req,
	struct hybridswap_entry *io_entry)
{
	gfp_t gfp = (req->io_para.scenario != HYBRIDSWAP_RECLAIM_IN) ?
		GFP_ATOMIC : GFP_NOIO;
	struct hybridswap_segment *segment = NULL;
	struct hybridswap_key_point_record *record = req->io_para.record;

	hybridswap_perf_lat_start(record, HYBRIDSWAP_SEGMENT_ALLOC);
	segment = kzalloc(sizeof(struct hybridswap_segment), gfp);
	if (!segment && (req->io_para.scenario == HYBRIDSWAP_FAULT_OUT))
		segment = kzalloc(sizeof(struct hybridswap_segment), GFP_NOIO);
	hybridswap_perf_lat_end(record, HYBRIDSWAP_SEGMENT_ALLOC);
	if (unlikely(!segment)) {
		hybridswap_io_err_record(HYBRIDSWAP_FAULT_OUT_SEGMENT_ALLOC_FAIL,
			req, io_entry->ext_id);

		return -ENOMEM;
	}

	segment->req = req;
	INIT_LIST_HEAD(&segment->io_entries);
	list_add_tail(&io_entry->list, &segment->io_entries);
	segment->io_entries_fifo[segment->extent_cnt++] = io_entry;
	segment->page_cnt = io_entry->pages_sz;
	INIT_WORK(&segment->endio_work, hybridswap_io_end_work);
	segment->segment_sector = io_entry->addr;
	req->segment = segment;

	return 0;
}

static int hybridswap_io_submit(struct hybridswap_io_req *req,
	bool merge_flag)
{
	int ret;
	struct hybridswap_segment *segment = req->segment;

	if (!segment || ((merge_flag) && (segment->page_cnt < BIO_MAX_PAGES)))
		return 0;

	hybridswap_limit_inflight(req);

	ret = hybridswap_submit_bio(segment);
	if (unlikely(ret)) {
		hybp(HYB_WARN, "submit bio failed, ret %d\n", ret);
		hybridswap_segment_free(req, segment);
	}
	req->segment = NULL;

	return ret;
}

static bool hybridswap_check_io_para_err(struct hybridswap_io *io_para)
{
	if (unlikely(!io_para)) {
		hybp(HYB_ERR, "io_para null\n");

		return true;
	}

	if (unlikely(!io_para->bdev ||
		(io_para->scenario >= HYBRIDSWAP_SCENARIO_BUTT))) {
		hybp(HYB_ERR, "io_para err, scenario %u\n",
			io_para->scenario);

		return true;
	}

	if (unlikely(!io_para->done_callback)) {
		hybp(HYB_ERR, "done_callback err\n");

		return true;
	}

	return false;
}

static bool hybridswap_check_entry_err(
	struct hybridswap_entry *io_entry)
{
	int i;

	if (unlikely(!io_entry)) {
		hybp(HYB_ERR, "io_entry null\n");

		return true;
	}

	if (unlikely((!io_entry->dest_pages) ||
		(io_entry->ext_id < 0) ||
		(io_entry->pages_sz > BIO_MAX_PAGES) ||
		(io_entry->pages_sz <= 0))) {
		hybp(HYB_ERR, "ext_id %d, page_sz %d\n", io_entry->ext_id,
			io_entry->pages_sz);

		return true;
	}

	for (i = 0; i < io_entry->pages_sz; ++i) {
		if (!io_entry->dest_pages[i]) {
			hybp(HYB_ERR, "dest_pages[%d] is null\n", i);
			return true;
		}
	}

	return false;
}

static int hybridswap_io_extent(void *io_handler,
	struct hybridswap_entry *io_entry)
{
	int ret;
	struct hybridswap_io_req *req = (struct hybridswap_io_req *)io_handler;

	if (unlikely(hybridswap_check_entry_err(io_entry))) {
		hybridswap_io_err_record(HYBRIDSWAP_FAULT_OUT_IO_ENTRY_PARA_FAIL,
			req, io_entry ? io_entry->ext_id : -EINVAL);
		req->io_para.done_callback(io_entry, -EIO, req);

		return -EFAULT;
	}

	hybp(HYB_DEBUG, "ext id %d, pages_sz %d, addr %llx\n",
		io_entry->ext_id, io_entry->pages_sz,
		io_entry->addr);

	if (hybridswap_ext_merge(req, io_entry))
		return hybridswap_io_submit(req, true);

	ret = hybridswap_io_submit(req, false);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "submit fail %d\n", ret);
		req->io_para.done_callback(io_entry, -EIO, req);

		return ret;
	}

	ret = hybridswap_new_segment_init(req, io_entry);
	if (unlikely(ret)) {
		hybp(HYB_ERR, "hybridswap_new_segment_init fail %d\n", ret);
		req->io_para.done_callback(io_entry, -EIO, req);

		return ret;
	}

	return 0;
}

int hybridswap_schedule_init(void)
{
	if (hybridswap_schedule_init_flag)
		return 0;

	hybridswap_proc_read_workqueue = alloc_workqueue("proc_hybridswap_read",
		WQ_HIGHPRI | WQ_UNBOUND, 0);
	if (unlikely(!hybridswap_proc_read_workqueue))
		return -EFAULT;

	hybridswap_proc_write_workqueue = alloc_workqueue("proc_hybridswap_write",
		WQ_CPU_INTENSIVE, 0);
	if (unlikely(!hybridswap_proc_write_workqueue)) {
		destroy_workqueue(hybridswap_proc_read_workqueue);

		return -EFAULT;
	}

	hybridswap_key_init();

	hybridswap_schedule_init_flag = true;

	return 0;
}

void *hybridswap_plug_start(struct hybridswap_io *io_para)
{
	gfp_t gfp;
	struct hybridswap_io_req *req = NULL;

	if (unlikely(hybridswap_check_io_para_err(io_para)))
		return NULL;

	gfp = (io_para->scenario != HYBRIDSWAP_RECLAIM_IN) ?
		GFP_ATOMIC : GFP_NOIO;
	req = kzalloc(sizeof(struct hybridswap_io_req), gfp);
	if (!req && (io_para->scenario == HYBRIDSWAP_FAULT_OUT))
		req = kzalloc(sizeof(struct hybridswap_io_req), GFP_NOIO);

	if (unlikely(!req)) {
		hybp(HYB_ERR, "io_req null\n");

		return NULL;
	}

	kref_init(&req->refcount);
	mutex_init(&req->refmutex);
	atomic_set(&req->extent_inflight, 0);
	init_waitqueue_head(&req->io_wait);
	req->io_para.bdev = io_para->bdev;
	req->io_para.scenario = io_para->scenario;
	req->io_para.done_callback = io_para->done_callback;
	req->io_para.complete_notify = io_para->complete_notify;
	req->io_para.private = io_para->private;
	req->io_para.record = io_para->record;
	req->limit_inflight_flag =
		(io_para->scenario == HYBRIDSWAP_RECLAIM_IN) ||
		(io_para->scenario == HYBRIDSWAP_PRE_OUT);
	req->wait_io_finish_flag =
		(io_para->scenario == HYBRIDSWAP_RECLAIM_IN) ||
		(io_para->scenario == HYBRIDSWAP_FAULT_OUT);
	req->nice = task_nice(current);
	init_completion(&req->io_end_flag);

	return (void *)req;
}

/* io_handler validity guaranteed by the caller */
int hybridswap_read_extent(void *io_handler,
	struct hybridswap_entry *io_entry)
{
	return hybridswap_io_extent(io_handler, io_entry);
}

/* io_handler validity guaranteed by the caller */
int hybridswap_write_extent(void *io_handler,
	struct hybridswap_entry *io_entry)
{
	return hybridswap_io_extent(io_handler, io_entry);
}

/* io_handler validity guaranteed by the caller */
int hybridswap_plug_finish(void *io_handler)
{
	int ret;
	struct hybridswap_io_req *req = (struct hybridswap_io_req *)io_handler;

	hybridswap_perf_lat_start(req->io_para.record, HYBRIDSWAP_IO_EXTENT);
	ret = hybridswap_io_submit(req, false);
	if (unlikely(ret))
		hybp(HYB_ERR, "submit fail %d\n", ret);

	hybridswap_perf_lat_end(req->io_para.record, HYBRIDSWAP_IO_EXTENT);
	hybridswap_wait_io_finish(req);
	hybridswap_perf_lat_point(req->io_para.record, HYBRIDSWAP_WAKE_UP);

	hybridswap_stat_io_bytes(req);
	hybridswap_perf_io_stat(req->io_para.record, req->page_cnt,
		req->segment_cnt);

	kref_put_mutex(&req->refcount, hybridswap_io_req_release,
		&req->refmutex);

	hybp(HYB_DEBUG, "io schedule finish succ\n");

	return ret;
}

