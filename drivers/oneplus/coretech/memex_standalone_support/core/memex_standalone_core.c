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

#include "memex_standalone_core.h"
#include <linux/cpumask.h>
#include <linux/sched.h>

#define PF_NO_TAIL(page, enforce) ({					\
		VM_BUG_ON_PGFLAGS(enforce && PageTail(page), page);	\
		compound_head(page);})

#define MEMEX_SIZE 512
#define MEMEX_DEBUG 0
#define RD_SIZE 128
#define DEBUG_TIME_INFO 0
#define FEAT_RECLAIM_LIMIT 0

#if defined(CONFIG_DRM_PANEL)
extern struct drm_panel *lcd_active_panel;
#endif

struct mp_reclaim_param {
	struct vm_area_struct *vma;
	/* Number of pages scanned */
	int nr_scanned;
#if FEAT_RECLAIM_LIMIT
	unsigned long start_jiffies;
#endif
	/* max pages to reclaim */
	int nr_to_reclaim;
	/* pages reclaimed */
	int nr_reclaimed;
	int type;
};

/* MemEx mode */
static struct task_struct *memex_tsk;
static unsigned int memex_threshold __read_mostly;
static pid_t memex_proc[MEMEX_SIZE];
static unsigned int vm_cam_aware __read_mostly = 1;
#if MEMEX_DEBUG
static unsigned int vm_swapin __read_mostly = 0;
module_param_named(memory_plus_swapin, vm_swapin, uint, S_IRUGO | S_IWUSR);
#endif

/* -1 = system free to use swap, 0 = disable retention, swap not available, 1 = enable retention */
static int vm_memory_plus __read_mostly = 0;
static unsigned long memplus_add_to_swap = 0;
unsigned long coretech_reclaim_pagelist(struct list_head *page_list, struct vm_area_struct *vma, void *sc);
unsigned long swapout_to_zram(struct list_head *page_list, struct vm_area_struct *vma);

#if MEMEX_DEBUG
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
#endif

static bool enough_swap_size(unsigned long req_size, int swap_bdv)
{
	bool ret = false;
	unsigned int n = 0;
	struct swap_info_struct *sis, *next;
	unsigned int node = numa_node_id();

	if (swap_bdv > 1)
		return ret;

	spin_lock(&swap_lock);
	plist_for_each_entry_safe(sis, next, &swap_avail_heads[node], avail_lists[node]) {
		int fast_i = (sis->flags & SWP_SYNCHRONOUS_IO)? 1:0;

		if (fast_i == swap_bdv) {
			spin_lock(&sis->lock);
			if (sis->flags & SWP_WRITEOK) {
				n += sis->pages - sis->inuse_pages;
				if (n > req_size) {
					ret = true;
					spin_unlock(&sis->lock);
					goto unlock;
				}
			}
			spin_unlock(&sis->lock);
		}
	}

unlock:
	spin_unlock(&swap_lock);
	return ret;
}

extern int do_swap_page(struct vm_fault *vmf);
#if MEMEX_DEBUG
static int memplus_swapin_walk_pmd_entry(pmd_t *pmd, unsigned long start,
	unsigned long end, struct mm_walk *walk)
{
	pte_t *orig_pte;
	struct vm_area_struct *vma = walk->private;
	unsigned long index;
	int ret = 0;

	if (pmd_none_or_trans_huge_or_clear_bad(pmd))
		return 0;

	for (index = start; index != end; index += PAGE_SIZE) {
		pte_t pte;
		swp_entry_t entry;
		struct page *page;
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

		if (is_fast_entry(entry)) {
			struct vm_fault fe = {
				.vma = vma,
				.pte = &pte,
				.orig_pte = pte,
				.address = index,
				.flags = FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_RETRY_NOWAIT,
				.pmd = pmd,
				.vma_flags = vma->vm_flags,
				.vma_page_prot = vma->vm_page_prot,
			};

			ret = do_swap_page(&fe);
			if (ret & VM_FAULT_ERROR) {
				printk(KERN_ERR "%s: do_swap_page ERROR\n", __func__);
				return -1;
			}
			continue;
		} else
			page = read_swap_cache_async(entry, GFP_HIGHUSER_MOVABLE,
								vma, index, false);
		if (page)
			put_page(page);
	}

	return 0;
}
#endif

static int memplus_reclaim_pte(pmd_t *pmd, unsigned long addr,
				unsigned long end, struct mm_walk *walk)
{
	struct mp_reclaim_param *rp = walk->private;
	struct vm_area_struct *vma = rp->vma;
	pte_t *pte, ptent;
	spinlock_t *ptl;
	struct page *page;
	LIST_HEAD(page_list);
	int isolated;
	int reclaimed = 0;
	int reclaim_type = rp->type;

	split_huge_pmd(vma, addr, pmd);
	if (pmd_trans_unstable(pmd) || !rp->nr_to_reclaim)
		return 0;
cont:
	isolated = 0;
	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	for (; addr != end; pte++, addr += PAGE_SIZE) {
		ptent = *pte;
		if (!pte_present(ptent))
			continue;

		page = vm_normal_page(vma, addr, ptent);
		if (!page)
			continue;

		if ((reclaim_type == TYPE_NORMAL) && PageSwapCache(page))
			continue;

		/* About 11% of pages have more than 1 map_count
		 * only take care mapcount == 1 is good enough */
		if (page_mapcount(page) != 1)
			continue;

		if (isolate_lru_page(page))
			continue;

		if (PageAnon(page) && !PageSwapBacked(page)) {
			putback_lru_page(page);
			continue;
		}

		list_add(&page->lru, &page_list);
		inc_node_page_state(page, NR_ISOLATED_ANON +
				page_is_file_cache(page));
		isolated++;
		rp->nr_scanned++;

		if ((isolated >= SWAP_CLUSTER_MAX) || !rp->nr_to_reclaim)
			break;
	}
	pte_unmap_unlock(pte - 1, ptl);

	memplus_add_to_swap += isolated;

	reclaimed = swapout_to_zram(&page_list, vma);

	rp->nr_reclaimed += reclaimed;
	rp->nr_to_reclaim -= reclaimed;
	if (rp->nr_to_reclaim < 0)
		rp->nr_to_reclaim = 0;

	if (rp->nr_to_reclaim && (addr != end))
		goto cont;

	/* TODO: is there other reschedule point we can add */
	cond_resched();

	return 0;
}

#if MEMEX_DEBUG
static ssize_t memex_do_swapin_anon(struct task_struct *task)
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
	walk.pmd_entry = memplus_swapin_walk_pmd_entry;

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
#if MEMEX_DEBUG
		/* TODO: it's possible to loop forever here
		 * if we're swapin camera which is foreground actively used */
#endif
		goto retry;
	}
out:
	lru_add_drain();	/* Push any new pages onto the LRU now */
	return 0;
}
#endif

/* get_task_struct before using this function */
static ssize_t memex_do_reclaim_anon(struct task_struct *task, int prev_adj)
{
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	struct mm_walk reclaim_walk = {};
	struct mp_reclaim_param rp;
	int task_anon = 0, task_swap = 0;
	int a_task_anon = 0, a_task_swap = 0;

#if DEBUG_TIME_INFO
	struct timespec ts1, ts2;
	getnstimeofday(&ts1);
#endif
#if FEAT_RECLAIM_LIMIT
	rp.start_jiffies = jiffies;
#endif
	rp.nr_to_reclaim = INT_MAX;
	rp.nr_reclaimed = 0;
	rp.nr_scanned = 0;
	/* memex currently use zram by default */
	rp.type = TYPE_FREQUENT;

	/* if available zram is less than 32MB, quit early */
	if (!enough_swap_size(8192, TYPE_FREQUENT))
		goto out;

	/* TODO: do we need to use p = find_lock_task_mm(tsk); in case main thread got killed */
	mm = get_task_mm(task);
	if (!mm)
		goto out;

	task_anon = get_mm_counter(mm, MM_ANONPAGES);
	task_swap = get_mm_counter(mm, MM_SWAPENTS);

	reclaim_walk.mm = mm;
	reclaim_walk.pmd_entry = memplus_reclaim_pte;
	reclaim_walk.private = &rp;

	down_read(&mm->mmap_sem);
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (is_vm_hugetlb_page(vma))
			continue;

		if (vma->vm_file)
			continue;

		/* if mlocked, don't reclaim it */
		if (vma->vm_flags & VM_LOCKED)
			continue;

		rp.vma = vma;

		/* TODO: do we need this check? */
		if (!list_empty(&vma->vm_mm->mmap_sem.wait_list)) {
#if MEMEX_DEBUG
			pr_info("MemEX mmap_sem waiting %s(%d)\n", task->comm, task->pid);
#endif
			break;
		}

		walk_page_range(vma->vm_start, vma->vm_end,
				&reclaim_walk);

		if (!rp.nr_to_reclaim)
			break;
	}

	flush_tlb_mm(mm);
	up_read(&mm->mmap_sem);
	a_task_anon = get_mm_counter(mm, MM_ANONPAGES);
	a_task_swap = get_mm_counter(mm, MM_SWAPENTS);
	mmput(mm);
out:
#if DEBUG_TIME_INFO
	getnstimeofday(&ts2);
	ts2 = timespec_sub(ts2, ts1);
	time_ns = timespec_to_ns(&ts2);
#endif
	/* TODO : return proper value */
	return rp.nr_reclaimed;
}

static inline bool should_skip(const char *comm)
{
	if (!vm_cam_aware)
		return false;
	/* provider@2.4-se, camera@1.0-serv, cameraserver, .camera.service, ctureprocessing, .oneplus.camera */
	return strnstr(comm, "camera", TASK_COMM_LEN)
		|| !strncmp("provider@2.4-se", comm, TASK_COMM_LEN)
		|| !strncmp("ctureprocessing", comm, TASK_COMM_LEN);
}

static int memex_fn(void *p)
{
	struct task_struct *tsk;
	int i, idx_sys, idx_app;
	cpumask_t tmask;

	/* setup nice: 130 cpumask: 0x3f */
	cpumask_parse("3f", &tmask);
	set_cpus_allowed_ptr(current, &tmask);
	set_user_nice(current, 10);

	set_freezable();
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		freezable_schedule();

		idx_sys = 0;
		idx_app = MEMEX_SIZE - 1;
		memset(memex_proc, 0, sizeof(memex_proc));

		rcu_read_lock();
		for_each_process(tsk) {
			if (tsk->flags & PF_KTHREAD)
				continue;

			/* TODO: do we need this check? */
			if (should_skip(tsk->comm))
				continue;

			if (task_uid(tsk).val >= AID_APP) {
				memex_proc[idx_app--] = tsk->pid;
			} else {
				memex_proc[idx_sys++] = tsk->pid;
			}

			if (unlikely(idx_sys > idx_app))
				break;
		}
#if MEMEX_DEBUG
		pr_info("MemEX sys=%d app=%d\n", idx_sys, idx_app);
#endif
		rcu_read_unlock();

		for (i = 0; i < MEMEX_SIZE; i++) {
			int pid = memex_proc[i];

			while (memex_threshold && memex_threshold <= (si_mem_available() * PAGE_SIZE / (1024 * 1024))) {
#if MEMEX_DEBUG
				pr_info("MemEX thresh = %u, MemAvail = %u\n"
						, memex_threshold, (si_mem_available() * PAGE_SIZE / (1024 * 1024)));
#endif
				freezable_schedule_timeout_interruptible(HZ / 30);
			}

			/* monitor current mode change */
			if (unlikely(!memex_threshold))
				break;

			if (pid == 0)
				continue;

			rcu_read_lock();
			tsk = find_task_by_vpid(pid);
			if (!tsk) {
				rcu_read_unlock();
				continue;
			}
			get_task_struct(tsk);
			rcu_read_unlock();
#if MEMEX_DEBUG
			pr_info("MemEX processing %s (%d) \n", tsk->comm, tsk->pid);
			if (vm_swapin)
				memex_do_swapin_anon(tsk);
			else
#endif
				memex_do_reclaim_anon(tsk, 0);
			put_task_struct(tsk);
		}

		if (kthread_should_stop())
			break;
	}
	return 0;
}

static int __init memplus_init(void)
{
	//TODO: priority tuning for reclaimd/swapind

	memex_tsk = kthread_run(memex_fn, 0, "memex");
	if (IS_ERR(memex_tsk)) {
		pr_err("Failed to start memex_task\n");
		memex_tsk = NULL;
	}

	return 0;
}

/* return value mapping:
 * 0     - success
 * ESRCH - no MemEx daemon
 * EINVAL- invalid input
 */
static int memory_plus_wake_memex_store(const char *buf, const struct kernel_param *kp)
{
	unsigned int val;

	if (!memex_tsk)
		return -ESRCH;
	if (sscanf(buf, "%u\n", &val) <= 0)
		return -EINVAL;

	memex_threshold = val;
	if (val)
		wake_up_process(memex_tsk);

	return 0;
}

static int memory_plus_wake_memex_show(char *buf, const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", memex_threshold);
}

static struct kernel_param_ops memory_plus_wake_memex_ops = {
	.set = memory_plus_wake_memex_store,
	.get = memory_plus_wake_memex_show,
};

module_param_cb(memory_plus_wake_memex, &memory_plus_wake_memex_ops, NULL, 0644);

module_param_named(memory_plus_enabled, vm_memory_plus, uint, S_IRUGO | S_IWUSR);
module_param_named(memplus_add_to_swap, memplus_add_to_swap, ulong, S_IRUGO | S_IWUSR);
module_param_named(memory_plus_cam_aware, vm_cam_aware, uint, S_IRUGO | S_IWUSR);

module_init(memplus_init)
