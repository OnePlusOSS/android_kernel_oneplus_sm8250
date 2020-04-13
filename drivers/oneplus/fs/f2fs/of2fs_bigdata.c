/***********************************************************
** File: - of2fs_sysfs.c
** Description:  f2fs bigdata statistics
**
** Version: 1.0
** Date: 2019/08/14
** Activity: [ASTI-147]
**
** ------------------ Revision History:------------------------
** <date>	<version >	<desc>
** 2019/08/14	1.0		add code for f2fs bigdata statistics
****************************************************************/

#include <linux/proc_fs.h>
#include <linux/f2fs_fs.h>
#include <linux/seq_file.h>

#include "../../../../fs/f2fs/f2fs.h"
#include "../../../../fs/f2fs/segment.h"
#include "../../../../fs/f2fs/gc.h"

#include "of2fs_bigdata.h"

/* f2fs big-data statistics */
#define OF2FS_PROC_DEF(_name)					\
static int of2fs_##_name##_open(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, of2fs_##_name##_show, PDE_DATA(inode));	\
}									\
									\
static const struct file_operations of2fs_##_name##_fops = {		\
	.owner = THIS_MODULE,						\
	.open = of2fs_##_name##_open,					\
	.read = seq_read,						\
	.write = of2fs_##_name##_write,					\
	.llseek = seq_lseek,						\
	.release = single_release,					\
};

static int of2fs_base_info_show(struct seq_file *seq, void *p)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	/*
	 * each column indicates: block_count fs_block_count
	 * free_segment_count reserved_segment_count
	 * valid_user_blocks
	 */
	seq_printf(seq, "%llu %llu %u %u %u\n",
		   le64_to_cpu(sbi->raw_super->block_count),
		   le64_to_cpu(sbi->raw_super->block_count) - le32_to_cpu(sbi->raw_super->main_blkaddr),
		   free_segments(sbi), reserved_segments(sbi),
		   valid_user_blocks(sbi));
	return 0;
}

static ssize_t of2fs_base_info_write(struct file *file,
				       const char __user *buf,
				       size_t length, loff_t *ppos)
{
	return length;
}

static int of2fs_discard_info_show(struct seq_file *seq, void *p)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);

	/*
	 * each colum indicates: discard_count discard_blocks undiscard_count
	 * undiscard_blocks discard_time max_discard_time
	 */
	bd_lock(sbi);
	if (SM_I(sbi)->dcc_info) {
		bd->undiscard_count = atomic_read(&SM_I(sbi)->dcc_info->discard_cmd_cnt);
		bd->undiscard_blocks = SM_I(sbi)->dcc_info->undiscard_blks;
	}
	seq_printf(seq, "%u %u %u %u %llu %llu\n", bd->discard_count,
		   bd->discard_blocks, bd->undiscard_count,
		   bd->undiscard_blocks, bd->discard_time,
		   bd->max_discard_time);
	bd_unlock(sbi);
	return 0;
}

static ssize_t of2fs_discard_info_write(struct file *file,
					  const char __user *buf,
					  size_t length, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);
	char buffer[3] = {0};

	if (!buf || length > 2 || length <= 0)
		return -EINVAL;

	if (copy_from_user(&buffer, buf, length))
		return -EFAULT;

	if (buffer[0] != '0')
		return -EINVAL;

	bd_lock(sbi);
	bd->discard_count = 0;
	bd->discard_blocks = 0;
	bd->undiscard_count = 0;
	bd->undiscard_blocks = 0;
	bd->discard_time = 0;
	bd->max_discard_time = 0;
	bd_unlock(sbi);

	return length;
}

static int of2fs_cp_info_show(struct seq_file *seq, void *p)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);

	/*
	 * each column indicates: cp_count cp_success_count cp_time max_cp_time
	 * max_cp_submit_time max_cp_flush_meta_time max_cp_discard_time
	 */
	bd_lock(sbi);
	bd->cp_count = sbi->stat_info->cp_count;
	seq_printf(seq, "%u %u %llu %llu %llu %llu %llu\n", bd->cp_count,
		   bd->cp_success_count, bd->cp_time, bd->max_cp_time,
		   bd->max_cp_submit_time, bd->max_cp_flush_meta_time,
		   bd->max_cp_discard_time);
	bd_unlock(sbi);
	return 0;
}

static ssize_t of2fs_cp_info_write(struct file *file,
				     const char __user *buf,
				     size_t length, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);
	char buffer[3] = {0};

	if (!buf || length > 2 || length <= 0)
		return -EINVAL;

	if (copy_from_user(&buffer, buf, length))
		return -EFAULT;

	if (buffer[0] != '0')
		return -EINVAL;

	bd_lock(sbi);
	bd->cp_count = 0;
	bd->cp_success_count = 0;
	bd->cp_time = 0;
	bd->max_cp_time = 0;
	bd->max_cp_submit_time = 0;
	bd->max_cp_flush_meta_time = 0;
	bd->max_cp_discard_time = 0;
	bd_unlock(sbi);

	return length;
}

static int of2fs_gc_info_show(struct seq_file *seq, void *p)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);

	/*
	 * each column indicates: bggc_cnt bggc_fail_cnt fggc_cnt fggc_fail_cnt
	 * bggc_data_seg_cnt bggc_data_blk_cnt bggc_node_seg_cnt bggc_node_blk_cnt
	 * fggc_data_seg_cnt fggc_data_blk_cnt fggc_node_seg_cnt fggc_node_blk_cnt
	 * node_ssr_cnt data_ssr_cnt node_lfs_cnt data_lfs_cnt data_ipu_cnt
	 * fggc_time
	 */
	bd_lock(sbi);
	seq_printf(seq, "%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %llu\n",
		   bd->gc_count[BG_GC], bd->gc_fail_count[BG_GC],
		   bd->gc_count[FG_GC], bd->gc_fail_count[FG_GC],
		   bd->gc_data_segments[BG_GC], bd->gc_data_blocks[BG_GC],
		   bd->gc_node_segments[BG_GC], bd->gc_node_blocks[BG_GC],
		   bd->gc_data_segments[FG_GC], bd->gc_data_blocks[FG_GC],
		   bd->gc_node_segments[FG_GC], bd->gc_node_blocks[FG_GC],
		   bd->data_alloc_count[SSR], bd->node_alloc_count[SSR],
		   bd->data_alloc_count[LFS], bd->node_alloc_count[LFS],
		   bd->data_ipu_count, bd->fggc_time);
	bd_unlock(sbi);
	return 0;
}

static ssize_t of2fs_gc_info_write(struct file *file,
				     const char __user *buf,
				     size_t length, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);
	int i;
	char buffer[3] = {0};

	if (!buf || length > 2 || length <= 0)
		return -EINVAL;

	if (copy_from_user(&buffer, buf, length))
		return -EFAULT;

	if (buffer[0] != '0')
		return -EINVAL;

	bd_lock(sbi);
	for (i = BG_GC; i <= FG_GC; i++) {
		bd->gc_count[i] = 0;
		bd->gc_fail_count[i] = 0;
		bd->gc_data_count[i] = 0;
		bd->gc_node_count[i] = 0;
		bd->gc_data_segments[i] = 0;
		bd->gc_data_blocks[i] = 0;
		bd->gc_node_segments[i] = 0;
		bd->gc_node_blocks[i] = 0;
	}
	bd->fggc_time = 0;
	for (i = LFS; i <= SSR; i++) {
		bd->node_alloc_count[i] = 0;
		bd->data_alloc_count[i] = 0;
	}
	bd->data_ipu_count = 0;
	bd_unlock(sbi);

	return length;
}

static int of2fs_fsync_info_show(struct seq_file *seq, void *p)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);

	/*
	 * eacho column indicates: fsync_reg_file_cnt fsync_dir_cnt fsync_time
	 * max_fsync_time fsync_wr_file_time max_fsync_wr_file_time
	 * fsync_cp_time max_fsync_cp_time fsync_sync_node_time
	 * max_fsync_sync_node_time fsync_flush_time max_fsync_flush_time
	 */
	bd_lock(sbi);
	seq_printf(seq, "%u %u %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
		   bd->fsync_reg_file_count, bd->fsync_dir_count, bd->fsync_time,
		   bd->max_fsync_time, bd->fsync_wr_file_time,
		   bd->max_fsync_wr_file_time, bd->fsync_cp_time,
		   bd->max_fsync_cp_time, bd->fsync_sync_node_time,
		   bd->max_fsync_sync_node_time, bd->fsync_flush_time,
		   bd->max_fsync_flush_time);
	bd_unlock(sbi);

	return 0;
}

static ssize_t of2fs_fsync_info_write(struct file *file,
					const char __user *buf,
					size_t length, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);
	char buffer[3] = {0};

	if (!buf || length > 2 || length <= 0)
		return -EINVAL;

	if (copy_from_user(&buffer, buf, length))
		return -EFAULT;

	if (buffer[0] != '0')
		return -EINVAL;

	bd_lock(sbi);
	bd->fsync_reg_file_count = 0;
	bd->fsync_dir_count = 0;
	bd->fsync_time = 0;
	bd->max_fsync_time = 0;
	bd->fsync_cp_time = 0;
	bd->max_fsync_cp_time = 0;
	bd->fsync_wr_file_time = 0;
	bd->max_fsync_wr_file_time = 0;
	bd->fsync_sync_node_time = 0;
	bd->max_fsync_sync_node_time = 0;
	bd->fsync_flush_time = 0;
	bd->max_fsync_flush_time = 0;
	bd_unlock(sbi);

	return length;
}

static int of2fs_hotcold_info_show(struct seq_file *seq, void *p)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);

	bd_lock(sbi);
	/*
	 * each colum indicates: hot_data_cnt, warm_data_cnt, cold_data_cnt, hot_node_cnt,
	 * warm_node_cnt, cold_node_cnt, meta_cp_cnt, meta_sit_cnt, meta_nat_cnt, meta_ssa_cnt,
	 * directio_cnt, gc_cold_data_cnt, rewrite_hot_data_cnt, rewrite_warm_data_cnt,
	 * gc_segment_hot_data_cnt, gc_segment_warm_data_cnt, gc_segment_cold_data_cnt,
	 * gc_segment_hot_node_cnt, gc_segment_warm_node_cnt, gc_segment_cold_node_cnt,
	 * gc_block_hot_data_cnt, gc_block_warm_data_cnt, gc_block_cold_data_cnt,
	 * gc_block_hot_node_cnt, gc_block_warm_node_cnt, gc_block_cold_node_cnt
	 */
	seq_printf(seq, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu "
		   "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
		   bd->hotcold_count[HC_HOT_DATA], bd->hotcold_count[HC_WARM_DATA],
		   bd->hotcold_count[HC_COLD_DATA], bd->hotcold_count[HC_HOT_NODE],
		   bd->hotcold_count[HC_WARM_NODE], bd->hotcold_count[HC_COLD_NODE],
		   bd->hotcold_count[HC_META], bd->hotcold_count[HC_META_SB],
		   bd->hotcold_count[HC_META_CP], bd->hotcold_count[HC_META_SIT],
		   bd->hotcold_count[HC_META_NAT], bd->hotcold_count[HC_META_SSA],
		   bd->hotcold_count[HC_DIRECT_IO], bd->hotcold_count[HC_GC_COLD_DATA],
		   bd->hotcold_count[HC_REWRITE_HOT_DATA],
		   bd->hotcold_count[HC_REWRITE_WARM_DATA],
		   bd->hotcold_gc_segments[HC_HOT_DATA],
		   bd->hotcold_gc_segments[HC_WARM_DATA],
		   bd->hotcold_gc_segments[HC_COLD_DATA],
		   bd->hotcold_gc_segments[HC_HOT_NODE],
		   bd->hotcold_gc_segments[HC_WARM_NODE],
		   bd->hotcold_gc_segments[HC_COLD_NODE],
		   bd->hotcold_gc_blocks[HC_HOT_DATA],
		   bd->hotcold_gc_blocks[HC_WARM_DATA],
		   bd->hotcold_gc_blocks[HC_COLD_DATA],
		   bd->hotcold_gc_blocks[HC_HOT_NODE],
		   bd->hotcold_gc_blocks[HC_WARM_NODE],
		   bd->hotcold_gc_blocks[HC_COLD_NODE]);
	bd_unlock(sbi);
	return 0;
}

static ssize_t of2fs_hotcold_info_write(struct file *file,
					  const char __user *buf,
					  size_t length, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);
	char buffer[3] = {0};
	int i;

	if (!buf || length > 2 || length <= 0)
		return -EINVAL;

	if (copy_from_user(&buffer, buf, length))
		return -EFAULT;

	if (buffer[0] != '0')
		return -EINVAL;

	bd_lock(sbi);
	for (i = 0; i < NR_HOTCOLD_TYPE; i++)
		bd->hotcold_count[i] = 0;
	for (i = 0; i < NR_CURSEG; i++) {
		bd->hotcold_gc_segments[i] = 0;
		bd->hotcold_gc_blocks[i] = 0;
	}
	bd_unlock(sbi);

	return length;
}

OF2FS_PROC_DEF(base_info);
OF2FS_PROC_DEF(discard_info);
OF2FS_PROC_DEF(gc_info);
OF2FS_PROC_DEF(cp_info);
OF2FS_PROC_DEF(fsync_info);
OF2FS_PROC_DEF(hotcold_info);

void f2fs_build_bd_stat(struct f2fs_sb_info *sbi)
{
	struct super_block *sb = sbi->sb;

	proc_create_data("base_info", S_IRUGO | S_IWUGO, sbi->s_proc,
				&of2fs_base_info_fops, sb);
	proc_create_data("discard_info", S_IRUGO | S_IWUGO, sbi->s_proc,
				&of2fs_discard_info_fops, sb);
	proc_create_data("cp_info", S_IRUGO | S_IWUGO, sbi->s_proc,
				&of2fs_cp_info_fops, sb);
	proc_create_data("gc_info", S_IRUGO | S_IWUGO, sbi->s_proc,
				&of2fs_gc_info_fops, sb);
	proc_create_data("fsync_info", S_IRUGO | S_IWUGO, sbi->s_proc,
				&of2fs_fsync_info_fops, sb);
	proc_create_data("hotcold_info", S_IRUGO | S_IWUGO, sbi->s_proc,
				&of2fs_hotcold_info_fops, sb);
}

void f2fs_destroy_bd_stat(struct f2fs_sb_info *sbi)
{
	remove_proc_entry("base_info", sbi->s_proc);
	remove_proc_entry("discard_info", sbi->s_proc);
	remove_proc_entry("cp_info", sbi->s_proc);
	remove_proc_entry("gc_info", sbi->s_proc);
	remove_proc_entry("fsync_info", sbi->s_proc);
	remove_proc_entry("hotcold_info", sbi->s_proc);

	if (sbi->bd_info) {
		kfree(sbi->bd_info);
		sbi->bd_info = NULL;
	}
}
