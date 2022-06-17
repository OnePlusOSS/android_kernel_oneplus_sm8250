/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _OF2FS_BIGDATA_H
#define _OF2FS_BIGDATA_H
/* unit for time: ns */
enum {
	HC_UNSET,
	HC_HOT_DATA,
	HC_WARM_DATA,
	HC_COLD_DATA,
	HC_HOT_NODE,
	HC_WARM_NODE,
	HC_COLD_NODE,
	NR_CURSEG,
	HC_META = NR_CURSEG,
	HC_META_SB,
	HC_META_CP,
	HC_META_SIT,
	HC_META_NAT,
	HC_META_SSA,
	HC_DIRECT_IO,
	HC_GC_COLD_DATA,
	HC_REWRITE_HOT_DATA,
	HC_REWRITE_WARM_DATA,
	NR_HOTCOLD_TYPE,
};

struct f2fs_bigdata_info {
	/* CP info
	 * avg_cp_time = cp_time / cp_count
	 */
	unsigned int cp_count, cp_success_count;
	u64 cp_time, max_cp_time, max_cp_submit_time,
	    max_cp_flush_meta_time, max_cp_discard_time;


	/* Discard info
	 * avg_discard_time = discard_time / discard_count
	 */
	unsigned int discard_count, discard_blocks,
                undiscard_count, undiscard_blocks;
	u64 discard_time, max_discard_time;

	/* GC info: BG_GC = 0, FG_GC = 1
	 * avg_[bg|fg]gc_data_segments = [bg|fg]gc_data_segments / [bg|fg]gc_data_count
	 * avg_[bg|fg]gc_data_blocks = [bg|fg]gc_data_blocks / [bg|fg]gc_data_count
	 * avg_fggc_time = fggc_time / fggc_count
	 *
	 */
	unsigned int gc_count[2], gc_fail_count[2],
		     gc_data_count[2], gc_node_count[2];
	unsigned int gc_data_segments[2], gc_data_blocks[2],
		     gc_node_segments[2], gc_node_blocks[2];
	u64 fggc_time;


	/* Node alloc info: LFS = 0, SSR = 1 */
	unsigned int node_alloc_count[2], data_alloc_count[2], data_ipu_count;

	unsigned long last_node_alloc_count, last_data_alloc_count;
	unsigned long curr_node_alloc_count, curr_data_alloc_count;
	unsigned long ssr_last_jiffies;

	/* Fsync info */
	unsigned int fsync_reg_file_count, fsync_dir_count;
	u64 fsync_time, max_fsync_time, fsync_cp_time, max_fsync_cp_time,
	    fsync_wr_file_time, max_fsync_wr_file_time, fsync_sync_node_time,
	    max_fsync_sync_node_time, fsync_flush_time, max_fsync_flush_time;

	/* Hot cold info */
	unsigned long hotcold_count[NR_HOTCOLD_TYPE];
	unsigned long hotcold_gc_segments[NR_CURSEG];
	unsigned long hotcold_gc_blocks[NR_CURSEG];
};

static inline struct f2fs_bigdata_info *F2FS_BD_STAT(struct f2fs_sb_info *sbi)
{
	return (struct f2fs_bigdata_info *)sbi->bd_info;
}

#define bd_inc_val(sbi, member, val) do {			\
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);	\
	if (bd)							\
		bd->member += (val);				\
} while (0)
#define bd_inc_array_val(sbi, member, idx, val) do {		\
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);	\
	if (bd)							\
		bd->member[(idx)] += (val);			\
} while (0)

#define bd_set_val(sbi, member, val) do {			\
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);	\
	if (bd)							\
		bd->member = (val);				\
} while (0)
#define bd_set_array_val(sbi, member, idx, val) do {		\
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);	\
	if (bd)							\
		bd->member[(idx)] = (val);			\
} while (0)

#define bd_max_val(sbi, member, val) do {			\
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);	\
	if (bd) {						\
		if (bd->member < (val))				\
			bd->member = (val);			\
	}							\
} while (0)

#define bd_lock_init(sbi) spin_lock_init(&(sbi)->bd_lock)
#define bd_lock(sbi) spin_lock(&(sbi)->bd_lock)
#define bd_unlock(sbi) spin_unlock(&(sbi)->bd_lock)
#endif
