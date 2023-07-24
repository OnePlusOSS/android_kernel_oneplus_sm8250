/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM damon

#if !defined(_TRACE_DAMON_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DAMON_H

#include <linux/damon.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

TRACE_EVENT(damon_aggregated,

	TP_PROTO(struct damon_target *t, unsigned int target_id,
		struct damon_region *r, unsigned int nr_regions),
	TP_ARGS(t, target_id, r, nr_regions),

	TP_STRUCT__entry(
		__field(unsigned long, target_id)
		__field(unsigned int, nr_regions)
		__field(unsigned long, start)
		__field(unsigned long, end)
		__field(unsigned int, nr_accesses)
		__field(unsigned int, age)
	),

	TP_fast_assign(
		__entry->target_id = target_id;
		__entry->nr_regions = nr_regions;
		__entry->start = r->ar.start;
		__entry->end = r->ar.end;
		__entry->nr_accesses = r->nr_accesses;
		__entry->age = r->age;
	),

	TP_printk("target_id=%lu nr_regions=%u %lu-%lu: %u %u",
			__entry->target_id, __entry->nr_regions,
			__entry->start, __entry->end,
			__entry->nr_accesses, __entry->age)
);

TRACE_EVENT(damon_reclaim_statistics,

	TP_PROTO(unsigned long nr_reclaim_tried_regions,
		unsigned long bytes_reclaim_tried_regions,
		unsigned long nr_reclaimed_regions,
		unsigned long bytes_reclaimed_regions,
		unsigned long nr_quota_exceeds),
	TP_ARGS(nr_reclaim_tried_regions,
		bytes_reclaim_tried_regions,
		nr_reclaimed_regions,
		bytes_reclaimed_regions,
		nr_quota_exceeds),

	TP_STRUCT__entry(
		__field(unsigned long, nr_reclaim_tried_regions)
		__field(unsigned long, bytes_reclaim_tried_regions)
		__field(unsigned long, nr_reclaimed_regions)
		__field(unsigned long, bytes_reclaimed_regions)
		__field(unsigned long, nr_quota_exceeds)
	),

	TP_fast_assign(
		__entry->nr_reclaim_tried_regions = nr_reclaim_tried_regions;
		__entry->bytes_reclaim_tried_regions = bytes_reclaim_tried_regions;
		__entry->nr_reclaimed_regions = nr_reclaimed_regions;
		__entry->bytes_reclaimed_regions = bytes_reclaimed_regions;
		__entry->nr_quota_exceeds = nr_quota_exceeds;
	),

	TP_printk("nr_reclaim_tried_regions=%lu bytes_reclaim_tried_regions=%lu nr_reclaimed_regions=%lu, bytes_reclaimed_regions=%lu, nr_quota_exceeds=%lu",
		__entry->nr_reclaim_tried_regions,
		__entry->bytes_reclaim_tried_regions,
		__entry->nr_reclaimed_regions,
		__entry->bytes_reclaimed_regions,
		__entry->nr_quota_exceeds)
);

#endif /* _TRACE_DAMON_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
