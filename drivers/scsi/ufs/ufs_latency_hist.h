/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _UFS_LATENCY_HIST_
#define _UFS_LATENCY_HIST_

#include <linux/types.h>
#include <linux/kernel.h>

/* add latency_hist node for ufs latency calculate in sysfs */
/*
 * X-axis for IO latency histogram support.
 */
static const u_int64_t latency_x_axis_us[] = {
	100,
    200,
    300,
    400,
    500,
    600,
    700,
    800,
    900,
    1000,
    2000,
    3000,
    4000,
    5000,
    6000,
    7000,
    8000,
    9000,
    10000,
    20000,
    30000,
    40000,
    50000,
    100000,
    200000,
    300000,
    400000,
    500000,
    600000,
    700000,
    800000,
    900000,
    1000000,
    1500000,
    2000000
};

#define IO_LAT_HIST_DISABLE         0
#define IO_LAT_HIST_ENABLE          1
#define IO_LAT_HIST_ZERO            2
#define CHUNCK_SIZE_4K                  8
#define CHUNCK_SIZE_8K                  16
#define CHUNCK_SIZE_32K                 64
#define CHUNCK_SIZE_64K                 128
#define CHUNCK_SIZE_128K                256
#define CHUNCK_SIZE_256K                512

struct io_latency_chunck_size {
	u_int64_t   chunk_0_count;
	u_int64_t	chunk_4k_count;
	u_int64_t   chunk_8k_count;
	u_int64_t   chunk_8_32k_count;
	u_int64_t   chunk_32_64k_count;
	u_int64_t   chunk_64_128k_count;
	u_int64_t   chunk_128_256k_count;
	u_int64_t   chunk_above_256k_count;
	u_int64_t   latency_axis;
};

struct io_latency_state {
	struct io_latency_chunck_size latency_y_axis[ARRAY_SIZE(latency_x_axis_us) + 1];
	u_int64_t	latency_elems;
	u_int64_t	latency_sum;
};

static inline void
io_update_latency_hist(struct io_latency_state *s, u_int64_t delta_us, unsigned int length)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(latency_x_axis_us); i++)
		if (delta_us < (u_int64_t)latency_x_axis_us[i])
			break;
	s->latency_y_axis[i].latency_axis++;
	if(!length) {
		s->latency_y_axis[i].chunk_0_count++;
	} else if (length > CHUNCK_SIZE_256K) {
		s->latency_y_axis[i].chunk_above_256k_count++;
	} else if (length > CHUNCK_SIZE_128K) {
		s->latency_y_axis[i].chunk_128_256k_count++;
	} else if (length > CHUNCK_SIZE_64K) {
		s->latency_y_axis[i].chunk_64_128k_count++;
	} else if (length > CHUNCK_SIZE_32K) {
	    s->latency_y_axis[i].chunk_32_64k_count++;
	} else if (length > CHUNCK_SIZE_8K) {
        s->latency_y_axis[i].chunk_8_32k_count++;
	} else if (length == CHUNCK_SIZE_8K) {
        s->latency_y_axis[i].chunk_8k_count++;
	} else if (length == CHUNCK_SIZE_4K) {
        s->latency_y_axis[i].chunk_4k_count++;
	}
	s->latency_elems++;
	s->latency_sum += delta_us;
}

ssize_t io_latency_hist_show(char *name, struct io_latency_state *s,
		char *buf, int buf_size);

#endif /* _UFS_LATENCY_HIST_ */
