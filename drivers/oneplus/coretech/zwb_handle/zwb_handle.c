#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/kernel_stat.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/vmpressure.h>
#include <linux/vmstat.h>
#include <linux/file.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/mm_inline.h>
#include <linux/backing-dev.h>
#include <linux/rmap.h>
#include <linux/topology.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/compaction.h>
#include <linux/notifier.h>
#include <linux/rwsem.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/memcontrol.h>
#include <linux/delayacct.h>
#include <linux/sysctl.h>
#include <linux/oom.h>
#include <linux/prefetch.h>
#include <linux/printk.h>
#include <linux/dax.h>

#include <asm/tlbflush.h>
#include <asm/div64.h>

#include <linux/swapops.h>
#include <linux/balloon_compaction.h>
#include <linux/swap.h>
#include <linux/sched/mm.h>
#include <linux/page-flags.h>
#include <linux/swapfile.h>
#include <linux/pagevec.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/cred.h>

#include <linux/msm_drm_notify.h>
#include <linux/fb.h>
#include <drm/drm_panel.h>

#include <linux/cpumask.h>
#include <linux/sched.h>

#include "../../../block/zram/zram_drv.h"

#define PF_NO_TAIL(page, enforce) ({					\
		VM_BUG_ON_PGFLAGS(enforce && PageTail(page), page);	\
		compound_head(page);})

#define CLEAR_DEBUG 1
#define ZWB_SIZE 16
static pid_t clear_proc[ZWB_SIZE];

struct task_struct *zwb_clear_tsk;

static bool is_fast_entry(swp_entry_t entry)
{
	struct swap_info_struct *p;
	bool ret = false;
	unsigned long offset, type;

	if (!entry.val)
		goto out;
	type = swp_type(entry);
	p = swap_info[type];
	if (!(p->flags & SWP_USED))
		goto out;
	offset = swp_offset(entry);
	if (offset >= p->max)
		goto out;

	spin_lock(&p->lock);
	if (p->flags & SWP_SYNCHRONOUS_IO)
		ret = true;
	spin_unlock(&p->lock);
out:
	return ret;
}

static int zwb_clear_walk_pmd_entry(pmd_t *pmd, unsigned long start,
	unsigned long end, struct mm_walk *walk)
{
	pte_t *orig_pte;
	struct vm_area_struct *vma = walk->private;
	unsigned long index;

	if (pmd_none_or_trans_huge_or_clear_bad(pmd))
		return 0;

	for (index = start; index != end; index += PAGE_SIZE) {
		pte_t pte;
		swp_entry_t entry;
		spinlock_t *ptl;

		if (!list_empty(&vma->vm_mm->mmap_sem.wait_list))
			return -1;

		orig_pte = pte_offset_map_lock(vma->vm_mm, pmd, start, &ptl);
		pte = *(orig_pte + ((index - start) / PAGE_SIZE));
		pte_unmap_unlock(orig_pte, ptl);

		if (pte_present(pte) || pte_none(pte))
			continue;
		entry = pte_to_swp_entry(pte);
		if (unlikely(non_swap_entry(entry)))
			continue;

		if (!is_fast_entry(entry)) {
			struct swap_info_struct *sis = swp_swap_info(entry);
			struct block_device *bdev = sis->bdev;
			sector_t sector = ((sector_t)swp_offset(entry) << (PAGE_SHIFT - 9)) + get_start_sect(bdev);
			struct zram *zram;
			u32 index;
			int result;

			result = blk_queue_enter(bdev->bd_queue, 0);
			if (result)
				continue;
			zram = bdev->bd_disk->private_data;
			index = sector >> SECTORS_PER_PAGE_SHIFT;
			bit_spin_lock(ZRAM_LOCK, &zram->table[index].flags);
			zram->table[index].flags &= ~BIT(ZRAM_IDLE);
			bit_spin_unlock(ZRAM_LOCK, &zram->table[index].flags);
			blk_queue_exit(bdev->bd_queue);
		}
	}

	return 0;
}

static ssize_t zwb_clear_idle_flag(struct task_struct *task)
{
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	struct mm_walk walk = {};
	int task_anon = 0, task_swap = 0, err = 0;

retry:
	mm = get_task_mm(task);
	if (!mm)
		goto out;

	task_anon = get_mm_counter(mm, MM_ANONPAGES);
	task_swap = get_mm_counter(mm, MM_SWAPENTS);

	walk.mm = mm;
	walk.pmd_entry = zwb_clear_walk_pmd_entry;

	down_read(&mm->mmap_sem);

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (is_vm_hugetlb_page(vma))
			continue;

		if (vma->vm_file)
			continue;

		/* if mlocked, don't reclaim it */
		if (vma->vm_flags & VM_LOCKED)
			continue;

		walk.private = vma;
		err = walk_page_range(vma->vm_start, vma->vm_end, &walk);
		if (err == -1)
			break;
	}

	flush_tlb_mm(mm);
	up_read(&mm->mmap_sem);
	mmput(mm);
	if (err) {
		err = 0;
		goto retry;
	}
out:
	return 0;
}


static inline bool should_skip(const char *comm)
{
	/* only A or B can go to processing */
	return strncmp("eplus.wallpaper", comm, TASK_COMM_LEN) && strncmp("ndroid.systemui", comm, TASK_COMM_LEN);
}

/* clear ZRAM_IDLE flag for specific processes. */
static int clear_fn(void *p)
{
	struct task_struct *tsk;
	int i, count;

	set_freezable();
	while (1) {
		count = 0;
		set_current_state(TASK_INTERRUPTIBLE);
		freezable_schedule();

		rcu_read_lock();
		for_each_process(tsk) {
			if (tsk->flags & PF_KTHREAD)
				continue;
			/* TODO: do we need this check? */
			if (should_skip(tsk->comm))
				continue;

			clear_proc[count] = tsk->pid;
			count++;
#if CLEAR_DEBUG
			pr_info("clear_idle_flag processing %s (%d) \n", tsk->comm, tsk->pid);
#endif

			if (count == 2)
				break;
		}
		rcu_read_unlock();

		for (i = 0; i < count; i++) {
			int pid = clear_proc[i];
			rcu_read_lock();
			tsk = find_task_by_vpid(pid);
			if (!tsk) {
				rcu_read_unlock();
				continue;
			}
			get_task_struct(tsk);
			rcu_read_unlock();
			zwb_clear_idle_flag(tsk);
			put_task_struct(tsk);
		}

#if CLEAR_DEBUG
		pr_info("clear_idle_flag finish");
#endif
		if (kthread_should_stop())
			break;
	}
	return 0;
}

static int __init zwb_handle_init(void)
{
	//TODO: priority tuning for reclaimd/swapind

	zwb_clear_tsk = kthread_run(clear_fn, 0, "zwb_clear_flag");
	if (IS_ERR(zwb_clear_tsk)) {
		pr_err("Failed to start zwb_clear_flag_task\n");
		zwb_clear_tsk = NULL;
	}

	return 0;
}

/* return value mapping:
 * 0     - success
 * ESRCH - no clear daemon
 * EINVAL- invalid input
 */
static int zwb_handle_wake_clear_store(const char *buf, const struct kernel_param *kp)
{
	unsigned int val;

	if (!zwb_clear_tsk)
		return -ESRCH;
	if (sscanf(buf, "%u\n", &val) <= 0)
		return -EINVAL;

	if (val)
		wake_up_process(zwb_clear_tsk);

	return 0;
}

static struct kernel_param_ops zwb_handle_wake_clear_ops = {
	.set = zwb_handle_wake_clear_store,
};

module_param_cb(zwb_handle_wake_clear, &zwb_handle_wake_clear_ops, NULL, 0644);

module_init(zwb_handle_init);
