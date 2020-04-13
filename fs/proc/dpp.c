// SPDX-License-Identifier: GPL-2.0

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/mmzone.h>
#include <linux/proc_fs.h>
#include <linux/quicklist.h>
#include <linux/seq_file.h>
#include <linux/swap.h>
#include <linux/vmstat.h>
#include <linux/atomic.h>
#include <linux/vmalloc.h>
#ifdef CONFIG_CMA
#include <linux/cma.h>
#endif
#include <asm/page.h>
#include <asm/pgtable.h>
#include "internal.h"

#include <linux/highmem.h>
#include <oneplus/dynamic_page_pool.h>

static int get_pool_max_size = 1;
static int get_pool_current_size = 2;
static int get_pool_sleep_millisecs = 3;

#define DPP_PROC_FOPS_DEF(pool, name)					\
static int __maybe_unused pool##_##name##_seq_show(			\
						struct seq_file *seq,	\
						void *offset)		\
{									\
	seq_printf(seq, "%ld\n",					\
			dynamic_page_pool_get_##name(pool));		\
									\
	return 0;							\
}									\
									\
static int pool##_##name##_open(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, pool##_##name##_seq_show,		\
			PDE_DATA(inode));				\
}									\
									\
static ssize_t pool##_##name##_write(struct file *file,			\
			const char __user *buf,				\
			size_t count, loff_t *ppos)			\
{									\
	int ret;							\
	unsigned int c = 0;						\
									\
	if (count) {							\
		ret = kstrtouint_from_user(buf, count, 0, &c);		\
		if (ret < 0)						\
			return ret;					\
									\
		dynamic_page_pool_set_##name(pool, c);			\
		return count;						\
	}								\
									\
	return count;							\
}									\
									\
static const struct file_operations					\
			dpp_proc_##pool##_##name##_fops = {		\
	.open		= pool##_##name##_open,				\
	.read		= seq_read,					\
	.llseek		= seq_lseek,					\
	.release	= single_release,				\
	.write		= pool##_##name##_write,			\
}

#define DPP_PROC_FILE_CREATE(pool, dir)					\
	do {								\
		proc_create_data(#pool "_max_pool_size", 0644, dir,	\
			&dpp_proc_##pool##_max_pool_size_fops,		\
			&get_pool_max_size);				\
		proc_create_data(#pool "_current_size", 0644, dir,	\
			&dpp_proc_##pool##_current_size_fops,		\
			&get_pool_current_size);			\
		proc_create_data(#pool "_sleep_millisecs", 0644, dir,	\
			&dpp_proc_##pool##_sleep_millisecs_fops,	\
			&get_pool_sleep_millisecs);			\
	} while (0)

DPP_PROC_FOPS_DEF(anon_pool, max_pool_size);
DPP_PROC_FOPS_DEF(anon_pool, current_size);
DPP_PROC_FOPS_DEF(anon_pool, sleep_millisecs);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
DPP_PROC_FOPS_DEF(anon_hugepage_pool, max_pool_size);
DPP_PROC_FOPS_DEF(anon_hugepage_pool, current_size);
DPP_PROC_FOPS_DEF(anon_hugepage_pool, sleep_millisecs);
#endif

static int __init dpp_proc_init(void)
{
	struct proc_dir_entry *dir;

	dir = proc_mkdir("dpp", NULL);
	if (!dir) {
		pr_err("/proc/dpp/ creation failed\n");
		return -ENOMEM;
	}

	DPP_PROC_FILE_CREATE(anon_pool, dir);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	DPP_PROC_FILE_CREATE(anon_hugepage_pool, dir);
#endif

	return 0;
}
fs_initcall(dpp_proc_init);
