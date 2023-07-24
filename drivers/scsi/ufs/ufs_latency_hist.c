// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "ufs_latency_hist.h"
#include <linux/math64.h>
/* add latency_hist node for ufs latency calculate in sysfs */
/*
 * Blk IO latency support. We want this to be as cheap as possible, so doing
 * this lockless (and avoiding atomics), a few off by a few errors in this
 * code is not harmful, and we don't want to do anything that is
 * perf-impactful.
 * TODO : If necessary, we can make the histograms per-cpu and aggregate
 * them when printing them out.
 */
ssize_t
io_latency_hist_show(char *name, struct io_latency_state *s, char *buf,
		int buf_size)
{
	int i;
	int bytes_written = 0;
	u_int64_t num_elem, elem;
	int pct;
	u_int64_t average;

	num_elem = s->latency_elems;
	if (num_elem > 0) {
		average = div64_u64(s->latency_sum, s->latency_elems);
		bytes_written += scnprintf(buf + bytes_written,
	    	buf_size - bytes_written,
			"IO %s(count = %llu,"
			" average = %lluus)\t0\t4k\t8k\t8-32k\t32-64k\t64-128k\t128-256k\t>256k:\n", name, num_elem, average);
		for (i = 0;i < ARRAY_SIZE(latency_x_axis_us);i++) {
			elem = s->latency_y_axis[i].latency_axis;
		    pct = div64_u64(elem * 100, num_elem);
		    bytes_written += scnprintf(buf + bytes_written,
			    8192 - bytes_written,
			    "\t< %6lluus\t%llu\t%d%%\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t\t%llu\n",
				latency_x_axis_us[i],
			    elem, pct,
			    s->latency_y_axis[i].chunk_0_count,
			    s->latency_y_axis[i].chunk_4k_count,
			    s->latency_y_axis[i].chunk_8k_count,
			    s->latency_y_axis[i].chunk_8_32k_count,
			    s->latency_y_axis[i].chunk_32_64k_count,
			    s->latency_y_axis[i].chunk_64_128k_count,
			    s->latency_y_axis[i].chunk_128_256k_count,
			    s->latency_y_axis[i].chunk_above_256k_count);
		}
	    /* Last element in y-axis table is overflow */
		elem = s->latency_y_axis[i].latency_axis;
	    pct = div64_u64(elem * 100, num_elem);
	    bytes_written += scnprintf(buf + bytes_written,
				8192 - bytes_written,
			    "\t>=%6lluus\t%llu\t%d%%\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t\t%llu\n",
			    latency_x_axis_us[i - 1],
			    elem, pct,
			    s->latency_y_axis[i].chunk_0_count,
			    s->latency_y_axis[i].chunk_4k_count,
			    s->latency_y_axis[i].chunk_8k_count,
			    s->latency_y_axis[i].chunk_8_32k_count,
			    s->latency_y_axis[i].chunk_32_64k_count,
			    s->latency_y_axis[i].chunk_64_128k_count,
			    s->latency_y_axis[i].chunk_128_256k_count,
			    s->latency_y_axis[i].chunk_above_256k_count);
	}

	return bytes_written;
}

