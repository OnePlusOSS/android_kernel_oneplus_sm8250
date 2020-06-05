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

#include "memplus.h"
#include <linux/cpumask.h>
#include <linux/sched.h>

#define PF_NO_TAIL(page, enforce) ({					\
		VM_BUG_ON_PGFLAGS(enforce && PageTail(page), page);	\
		compound_head(page);})

#define MEMEX_SIZE 512
#define MEMEX_DEBUG 0
#define GC_SIZE 1024
#define DEBUG_GCD 0 /*print gcd info */
#define GCD_SST 0 /* stress test */
#define RD_SIZE 128
#define DEBUG_TIME_INFO 0
#define FEAT_RECLAIM_LIMIT 0
#define	MEMPLUS_GET_ANON_MEMORY _IOWR('a',  1, unsigned long)
#define	MEMPLUS_RECLAIM_ANON_MEMORY _IOWR('a',  2, unsigned long)
#define	MEMPLUS_SWAPIN_ANON_MEMORY _IOWR('a',  3, unsigned long)
#define	MEMPLUS_GET_AVAILABLE_SWAP_SPACE _IOWR('a',  4, unsigned long)

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

struct reclaim_data {
	pid_t pid;
	int prev_adj;
};

struct reclaim_info {
	struct reclaim_data rd[RD_SIZE];
	int i_idx;
	int o_idx;
	int count;
};

struct reclaim_info ri = { {{0}}, 0, 0, 0 };
struct reclaim_info si = { {{0}}, 0, 0, 0 };
static struct task_struct *reclaimd_tsk = NULL;
static struct task_struct *swapind_tsk = NULL;
static DEFINE_SPINLOCK(rd_lock);

static atomic64_t accu_display_on_jiffies = ATOMIC64_INIT(0);
static struct notifier_block memplus_notify;
static unsigned long display_on_jiffies;
static pid_t proc[GC_SIZE] = { 0 };
static struct task_struct *gc_tsk;
static bool display_on = true;

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
unsigned long swapout_to_disk(struct list_head *page_list, struct vm_area_struct *vma);

static inline bool current_is_gcd(void)
{
	return current == gc_tsk;
}

bool ctech_current_is_swapind() {
	return current == swapind_tsk;
}

bool ctech_memplus_check_isolate_page(struct page*page)
{
	return (memplus_enabled() && (!PageSwapCache(page) || PageWillneed(page)));
}

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

__always_inline void ctech_memplus_move_swapcache_to_anon_lru(struct page *page)
{
	struct zone *zone = page_zone(page);
	unsigned long flag;

	if (memplus_enabled()) {
		spin_lock_irqsave(zone_lru_lock(zone), flag);
		if (PageLRU(page)) {
			struct lruvec *lruvec;
			enum lru_list lru, oldlru = page_lru(page);

			clear_bit(PG_swapcache, &(PF_NO_TAIL(page, 1))->flags);
			lru = page_lru(page);
			lruvec = mem_cgroup_page_lruvec(page, zone->zone_pgdat);
			list_move(&page->lru, &lruvec->lists[lru]);
			update_lru_size(lruvec, oldlru, page_zonenum(page), -hpage_nr_pages(page));
			update_lru_size(lruvec, lru, page_zonenum(page), hpage_nr_pages(page));
		} else
			clear_bit(PG_swapcache, &(PF_NO_TAIL(page, 1))->flags);
		spin_unlock_irqrestore(zone_lru_lock(zone), flag);
	}else
		clear_bit(PG_swapcache, &(PF_NO_TAIL(page, 1))->flags);
}

__always_inline void ctech_memplus_move_anon_to_swapcache_lru(struct page *page)
{
	struct zone *zone = page_zone(page);
	unsigned long flag;

	if (memplus_enabled()) {
		spin_lock_irqsave(zone_lru_lock(zone), flag);
		if (likely(!PageLRU(page)))
			set_bit(PG_swapcache, &(PF_NO_TAIL(page, 1))->flags);
		else {
			struct lruvec *lruvec;
			enum lru_list lru, oldlru = page_lru(page);

			set_bit(PG_swapcache, &(PF_NO_TAIL(page, 1))->flags);
			lru = page_lru(page);
			lruvec = mem_cgroup_page_lruvec(page, zone->zone_pgdat);
			list_move(&page->lru, &lruvec->lists[lru]);
			update_lru_size(lruvec, oldlru, page_zonenum(page), -hpage_nr_pages(page));
			update_lru_size(lruvec, lru, page_zonenum(page), hpage_nr_pages(page));
		}
		spin_unlock_irqrestore(zone_lru_lock(zone), flag);
	} else
		set_bit(PG_swapcache, &(PF_NO_TAIL(page, 1))->flags);
}
extern int do_swap_page(struct vm_fault *vmf);
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

static void enqueue_reclaim_data(pid_t nr, int prev_adj, struct reclaim_info *info)
{
	int idx;
	struct task_struct *waken_task;

	waken_task = (info == &ri? reclaimd_tsk : swapind_tsk);
	if (!waken_task)
		return;

	spin_lock(&rd_lock);
	if (info->count < RD_SIZE) {
		info->count++;
		idx = info->i_idx++ % RD_SIZE;
		info->rd[idx].pid = nr;
		info->rd[idx].prev_adj = prev_adj;
	}
	spin_unlock(&rd_lock);
	BUG_ON(info->count > RD_SIZE || info->count < 0);

	if (waken_task->state == TASK_INTERRUPTIBLE)
		wake_up_process(waken_task);
}

bool ctech_memplus_enabled(void)
{
	return vm_memory_plus > 0 && total_swap_pages;
}

__always_inline bool ctech__memplus_enabled(void)
{
	return vm_memory_plus > 0;
}

static void __memplus_state_check(int cur_adj, int prev_adj, struct task_struct* task)
{
	int uid = task_uid(task).val;

	if (!memplus_enabled())
		return;

	/* special case that SmartBoost toggle disables this feature */
	if (vm_memory_plus == 2)
		goto queue_swapin;

	if (task->signal->memplus_type == TYPE_SYS_IGNORE)
		return;

	if (task->signal->memplus_type == TYPE_WILL_NEED)
		goto queue_swapin;

	if (unlikely((prev_adj == -1) || (cur_adj == prev_adj)))
		return;

	if (uid < AID_APP) {
		//trace_printk("QUIT-reclaim %s (pid %d) (adj %d -> %d) (uid %d)\n", task->comm, task->pid, prev_adj, cur_adj, uid);
		return;
	}

	if (cur_adj >= 800 && time_after_eq(jiffies, task->signal->reclaim_timeout)) {
		spin_lock(&task->signal->reclaim_state_lock);
		/* reclaim should not kick-in within 2 secs */
		task->signal->reclaim_timeout = jiffies + 2*HZ;

		if (task->signal->swapin_should_readahead_m == RECLAIM_STANDBY) {
			task->signal->swapin_should_readahead_m = RECLAIM_QUEUE;
			//trace_printk("Q-reclaim %s (pid %d) (adj %d -> %d) (uid %d)\n", task->comm, task->pid, prev_adj, cur_adj, uid);
			enqueue_reclaim_data(task->pid, prev_adj, &ri);
		}
		spin_unlock(&task->signal->reclaim_state_lock);
	} else if (cur_adj == 0) {
queue_swapin:
		spin_lock(&task->signal->reclaim_state_lock);
		if (task->signal->swapin_should_readahead_m == RECLAIM_QUEUE) {
			task->signal->reclaim_timeout = jiffies + 2*HZ;
			task->signal->swapin_should_readahead_m = RECLAIM_STANDBY;
		} else if (task->signal->swapin_should_readahead_m == RECLAIM_DONE) {
			task->signal->reclaim_timeout = jiffies + 2*HZ;
			task->signal->swapin_should_readahead_m = SWAPIN_QUEUE;
			//trace_printk("Q-swapin %s (pid %d) (adj %d -> %d) (uid %d)\n", task->comm, task->pid, prev_adj, cur_adj, uid);
			enqueue_reclaim_data(task->pid, prev_adj, &si);
		}
		spin_unlock(&task->signal->reclaim_state_lock);
	}
}

void ctech_memplus_state_check(bool legacy, int oom_adj, struct task_struct *task, int type, int update)
{
	int oldadj = task->signal->oom_score_adj;

	if (update) {
		if (type >= TYPE_END || type < 0)
			return;
		task->signal->memplus_type = type;

		if (type == TYPE_WILL_NEED)
			oom_adj = 0;
		else if ((type & MEMPLUS_TYPE_MASK) < TYPE_SYS_IGNORE)
			oom_adj = oldadj;
		else
			return;
	}

	if (!legacy && (oom_adj >= 800 || oom_adj == 0))
		__memplus_state_check(oom_adj, oldadj, task);
}

static bool dequeue_reclaim_data(struct reclaim_data *data, struct reclaim_info *info)
{
	int idx;
	bool has_data = false;

	spin_lock(&rd_lock);
	if (info->count > 0) {
		has_data = true;
		info->count--;
		idx = info->o_idx++ % RD_SIZE;
		*data = info->rd[idx];
	}
	spin_unlock(&rd_lock);
	BUG_ON(info->count > RD_SIZE || info->count < 0);

	return has_data;
}

static ssize_t swapin_anon(struct task_struct *task, int prev_adj)
{
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	struct mm_walk walk = {};
	int task_anon = 0, task_swap = 0, err = 0;
	//u64 time_ns = 0;
#if DEBUG_TIME_INFO
	struct timespec ts1, ts2;
	getnstimeofday(&ts1);
#endif

retry:
	/* TODO: do we need to use p = find_lock_task_mm(tsk); in case main thread got killed */
	mm = get_task_mm(task);
	if (!mm)
		goto out;

	/* system pid may reach its max value and this pid was reused by other process */
	if (unlikely(task->signal->swapin_should_readahead_m != SWAPIN_QUEUE)) {
		mmput(mm);
		return 0;
	}

	task_anon = get_mm_counter(mm, MM_ANONPAGES);
	task_swap = get_mm_counter(mm, MM_SWAPENTS);

	/* swapin only for large APP, flip 33000, bow 60000, eightpoll 16000 */
	if (task_swap <= 10000) {
		mmput(mm);
		//trace_printk("SMALL swapin: this task is too small\n");
		goto out;
	}

	walk.mm = mm;
	walk.pmd_entry = memplus_swapin_walk_pmd_entry;

	down_read(&mm->mmap_sem);

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (vma->memplus_flags)
			continue;

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
		vma->memplus_flags = 1;
	}

	flush_tlb_mm(mm);
	up_read(&mm->mmap_sem);
	mmput(mm);
	if (err) {
		err = 0;
		//schedule();
		goto retry;
	}
out:
		/* TODO */
	lru_add_drain();	/* Push any new pages onto the LRU now */
#if DEBUG_TIME_INFO
	getnstimeofday(&ts2);
	ts2 = timespec_sub(ts2, ts1);
	time_ns = timespec_to_ns(&ts2);
#endif
	//trace_printk("%s (pid %d)(size %d-%d) (adj %d -> %d) consumed %llu ms %llu us\n", task->comm, task->pid, task_anon, task_swap, prev_adj, task->signal->oom_score_adj, (time_ns/1000000), (time_ns/1000)%1000);

	spin_lock(&task->signal->reclaim_state_lock);
	task->signal->swapin_should_readahead_m = RECLAIM_STANDBY;
	spin_unlock(&task->signal->reclaim_state_lock);

	return 0;
}

//TODO: blk_plug don't seem to work
static int swapind_fn(void *p)
{
	struct reclaim_data data;
	struct task_struct *tsk;

	set_freezable();
	for ( ; ; ) {
		while (!pm_freezing && dequeue_reclaim_data(&data, &si)) {
			rcu_read_lock();
			tsk = find_task_by_vpid(data.pid);

			/* KTHREAD is almost impossible to hit this */
			//if (tsk->flags & PF_KTHREAD) {
			//  rcu_read_unlock();
			//	continue;
			//}

			if (!tsk) {
				rcu_read_unlock();
				continue;
			}

			get_task_struct(tsk);
			rcu_read_unlock();

			swapin_anon(tsk, data.prev_adj);
			put_task_struct(tsk);
		}

		set_current_state(TASK_INTERRUPTIBLE);
		freezable_schedule();

		if (kthread_should_stop())
			break;
	}

	return 0;
}

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
	bool check_event = current_is_gcd();
#if defined(CONFIG_PAGE_EXTENSION) && defined(CONFIG_PAGE_OWNER_ENABLE_DEFAULT)
	struct page_ext *page_ext;
#endif

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

		if (check_event) {
#if defined(CONFIG_PAGE_EXTENSION) && defined(CONFIG_PAGE_OWNER_ENABLE_DEFAULT)
			page_ext = lookup_page_ext(page);
			if (unlikely(!page_ext))
				continue;

			/* gc_tsk should respect countdown event */
			if ((page_ext->next_event > 0) && (--(page_ext->next_event) > 0))
				continue;
#else
			/* gc_tsk should respect countdown event */
			if ((page->next_event > 0) && (--(page->next_event) > 0))
				continue;
#endif
		}

		ClearPageWillneed(page);

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

	if (reclaim_type == TYPE_NORMAL && !enough_swap_size(isolated, TYPE_NORMAL))
		reclaim_type = TYPE_FREQUENT;

	if (reclaim_type == TYPE_NORMAL)
		reclaimed = swapout_to_disk(&page_list, vma);
	else if (reclaim_type == TYPE_FREQUENT)
		reclaimed = swapout_to_zram(&page_list, vma);
	else {
		if (!current_is_gcd())
			pr_info_ratelimited("!! %s(%d) is reclaiming unexpected task type (%d)\n"
					, current->comm, current->pid, reclaim_type);
		reclaimed = swapout_to_zram(&page_list, vma);
	}

	rp->nr_reclaimed += reclaimed;
	rp->nr_to_reclaim -= reclaimed;
	if (rp->nr_to_reclaim < 0)
		rp->nr_to_reclaim = 0;

#if FEAT_RECLAIM_LIMIT
	/* TODO: early quit */
	/* timeout (range from 10~20ms), emergency quit back to reclaim_anon() */
	/* statistics shows 90% of reclaim finish within 60ms, should be a good timeout value */
	/* statistics shows 80% of reclaim finish within 26ms, should be a good timeout value */
	/* statistics shows 77% of reclaim finish within 20ms, should be a good timeout value */
	/* statistics shows 68% of reclaim finish within 10ms, should be a good timeout value */
	//if (time_after_eq(jiffies, rp->start_jiffies + 2)) {
	//	rp->nr_to_reclaim = 0;
	//	return 1;
	//}

	/* this will make black screen shorter */
	//if (rp->nr_reclaimed > 2000) {
	//	rp->nr_to_reclaim = 0;
	//	return 1;
	//}
#endif

	if (rp->nr_to_reclaim && (addr != end))
		goto cont;

	/* TODO: is there other reschedule point we can add */
	cond_resched();

	return 0;
}

/* get_task_struct before using this function */
static ssize_t reclaim_anon(struct task_struct *task, int prev_adj)
{
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	struct mm_walk reclaim_walk = {};
	struct mp_reclaim_param rp;
	int task_anon = 0, task_swap = 0;
	int a_task_anon = 0, a_task_swap = 0;

	//u64 time_ns = 0;
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

	spin_lock(&task->signal->reclaim_state_lock);
	if (task->signal->swapin_should_readahead_m == GC_RECLAIM_QUEUE) {
		spin_unlock(&task->signal->reclaim_state_lock);
		goto gc_proceed;
	}
	/*TODO: additional handle for PF_EXITING do_exit()->exit_signal()*/
	if (task->signal->swapin_should_readahead_m != RECLAIM_QUEUE) {
		//trace_printk("EXIT reclaim: this task is either (reclaimed) or (adj 0 swapin)\n");
		spin_unlock(&task->signal->reclaim_state_lock);
		goto out;
	}
	task->signal->swapin_should_readahead_m = RECLAIM_DONE;
	spin_unlock(&task->signal->reclaim_state_lock);

gc_proceed:
	/* TODO: do we need to use p = find_lock_task_mm(tsk); in case main thread got killed */
	mm = get_task_mm(task);
	if (!mm)
		goto out;

	task_anon = get_mm_counter(mm, MM_ANONPAGES);
	task_swap = get_mm_counter(mm, MM_SWAPENTS);

	reclaim_walk.mm = mm;
	reclaim_walk.pmd_entry = memplus_reclaim_pte;

	/* if app is larger than 200MB, override its property to frequent */
	if (task_anon + task_swap > 51200) {
		rp.type = TYPE_FREQUENT;
	} else
		rp.type = task->signal->memplus_type;

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

		if (!current_is_gcd() && task->signal->swapin_should_readahead_m != RECLAIM_DONE)
			break;

		rp.vma = vma;
		walk_page_range(vma->vm_start, vma->vm_end,
				&reclaim_walk);

		vma->memplus_flags = 0;
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
	/* it's possible that rp data isn't initialized because mm don't exist */
	//trace_printk("%s (pid %d)(size %d-%d to %d-%d) (adj %d -> %d) reclaimed %d scan %d consumed %llu ms %llu us\n"
	//		, task->comm, task->pid, task_anon, task_swap, a_task_anon, a_task_swap
	//		, prev_adj, task->signal->oom_score_adj, rp.nr_reclaimed, rp.nr_scanned
	//		, (time_ns/1000000), (time_ns/1000)%1000);

	/* TODO : return proper value */
	return rp.nr_reclaimed;
}

//TODO: should we mark reclaimd/swapind freezable?
static int reclaimd_fn(void *p)
{
	struct reclaim_data data;
	struct task_struct *tsk;

	set_freezable();
	for ( ; ; ) {
		while (!pm_freezing && dequeue_reclaim_data(&data, &ri)) {
			rcu_read_lock();
			tsk = find_task_by_vpid(data.pid);

			/* KTHREAD is almost impossible to hit this */
			//if (tsk->flags & PF_KTHREAD) {
			//  rcu_read_unlock();
			//	continue;
			//}

			if (!tsk) {
				rcu_read_unlock();
				continue;
			}

			get_task_struct(tsk);
			rcu_read_unlock();

			do {
				msleep(30);
			} while (swapind_tsk && (swapind_tsk->state == TASK_RUNNING));

			reclaim_anon(tsk, data.prev_adj);
			put_task_struct(tsk);
		}

		set_current_state(TASK_INTERRUPTIBLE);
		freezable_schedule();

		if (kthread_should_stop())
			break;
	}
	return 0;
}

/* do_swap_page() hook */
static void ctech_memplus_next_event(struct page *page)
{
	unsigned long ret;
#if defined(CONFIG_PAGE_EXTENSION) && defined(CONFIG_PAGE_OWNER_ENABLE_DEFAULT)
	struct page_ext *page_ext;
#endif

	/* skip if handled by reclaimd or current is swapind */
	if (current->signal->reclaim_timeout)
		return;

#if defined(CONFIG_PAGE_EXTENSION) && defined(CONFIG_PAGE_OWNER_ENABLE_DEFAULT)
	page_ext = lookup_page_ext(page);
	if (unlikely(!page_ext))
		return;
#endif

	/* next_event value
	 *  0 : gc always reclaim / default
	 *  1 : gc at next event
	 *  N : N <= 7, countdown N to do gc
	 */
	ret = (atomic64_read(&accu_display_on_jiffies)
			+ (display_on ? (jiffies - display_on_jiffies) : 0)) / (3600 * HZ);
	ret = ret >= 6 ? 1 : (7 - ret);

#if defined(CONFIG_PAGE_EXTENSION) && defined(CONFIG_PAGE_OWNER_ENABLE_DEFAULT)
	page_ext->next_event = ret;
#else
	page->next_event = ret;
#endif
}

static noinline void wait_for_suspend(void)
{
#if GCD_SST
	return;
#endif
	/* wait until user-space process all freezed */
	while (!pm_nosig_freezing) {
#if DEBUG_GCD
		pr_info("gc wait for suspend\n");
#endif
		/* suspend freezer only wake TASK_INTERRUPTIBLE */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		atomic64_set(&accu_display_on_jiffies, 0);
#if DEBUG_GCD
		pr_info("gc finish waiting suspend\n");
#endif
	}
}

#if defined(CONFIG_DRM_PANEL)
static int memplus_notifier_callback(struct notifier_block *nb, unsigned long event, void *data)
{
	int blank;
	struct drm_panel_notifier *evdata = data;
	static int old_status = -1;

	if (evdata && evdata->data) {
		blank = *(int *)(evdata->data);
#if DEBUG_GCD
		pr_info("event = %d blank = %d", event, blank);
#endif
		if (event == DRM_PANEL_EARLY_EVENT_BLANK) {
			if (old_status == blank)
				return 0;
			switch (blank) {
			case DRM_PANEL_BLANK_UNBLANK:
				old_status = blank;
				display_on = true;
				display_on_jiffies = jiffies;
#if DEBUG_GCD
				pr_info("display ON\n");
#endif
				break;
			case DRM_PANEL_BLANK_POWERDOWN:
				old_status = blank;
				atomic64_add((jiffies - display_on_jiffies), &accu_display_on_jiffies);
#if DEBUG_GCD
				pr_info("display OFF\n");
#endif
#if GCD_SST
				wake_up_process(gc_tsk);
#endif
				display_on = false;
				break;
			default:
				break;
			}
		}
	}
	return 0;
}
#endif

/* return current status */
static inline bool register_notifier(void)
{
	static bool initialized;
#if defined(CONFIG_DRM_PANEL)
	int status;
#endif

	if (likely(initialized))
		goto out;
#if defined(CONFIG_DRM_PANEL)
	if (!lcd_active_panel) {
		pr_err("register drm panel notifier - lcd_active_panel not present\n");
		goto out;
	}
	memplus_notify.notifier_call = memplus_notifier_callback;
	status = drm_panel_notifier_register(lcd_active_panel, &memplus_notify);
	if (status) {
		pr_err("Unable to register notifier: %d\n", status);
	} else {
		initialized = true;
		pr_err("register drm panel notifier - success!");
	}
#else
	pr_err("cannot register display notifier, please FIX IT!!!!!!\n");
#endif

out:
	return initialized;
}

static int gc_fn(void *p)
{
	struct task_struct *tsk;
	int idx = 0, i;

	/* register display notifier */
	while (idx++ < 10) {
		if (register_notifier())
			break;
		msleep(2000);
	}

	set_freezable();
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		freezable_schedule();

		idx = 0;
		memset(proc, 0, sizeof(proc));

		rcu_read_lock();
		for_each_process(tsk) {
			if (tsk->flags & PF_KTHREAD)
				continue;
#if DEBUG_GCD
			if (tsk->signal->reclaim_timeout)
				pr_info("gc skip %s (pid %d uid %d, adj %d reclaim_time %ds before %llu %llu)\n"
						, tsk->comm, tsk->pid, task_uid(tsk).val, tsk->signal->oom_score_adj
						, (2*HZ + jiffies - tsk->signal->reclaim_timeout) / HZ, 2*HZ + jiffies
						, tsk->signal->reclaim_timeout);
#endif
			/* skip if being handled by reclaimd */
			if (tsk->signal->reclaim_timeout)
				continue;

			proc[idx] = tsk->pid;

			if (++idx == GC_SIZE)
				break;
		}
		rcu_read_unlock();

		atomic64_set(&accu_display_on_jiffies, 0);
		for (i = 0; i < idx; i++) {
			int pid = proc[i];

			if (pid == 0)
				break;

			wait_for_suspend();

			rcu_read_lock();
			tsk = find_task_by_vpid(pid);

			if (!tsk) {
				rcu_read_unlock();
				continue;
			}

			get_task_struct(tsk);
			rcu_read_unlock();
#if DEBUG_GCD
			if (task_uid(tsk).val >= AID_APP)
				pr_info("gc processing %s (pid %d uid %d, adj %d)\n"
						, tsk->comm, tsk->pid, task_uid(tsk).val, tsk->signal->oom_score_adj);
#endif
			spin_lock(&tsk->signal->reclaim_state_lock);
			/* final check if handled by reclaimd */
			if (tsk->signal->reclaim_timeout) {
				spin_unlock(&tsk->signal->reclaim_state_lock);
				put_task_struct(tsk);
				continue;
			}
			/* change to special state GC_RECLAIM_QUEUE */
			if (likely(tsk->signal->swapin_should_readahead_m == RECLAIM_STANDBY))
				tsk->signal->swapin_should_readahead_m = GC_RECLAIM_QUEUE;
			else
				pr_info("pre-check task %s(%d) unexpected status %d"
						, tsk->comm, tsk->pid, tsk->signal->swapin_should_readahead_m);
			spin_unlock(&tsk->signal->reclaim_state_lock);

			reclaim_anon(tsk, 0);

			spin_lock(&tsk->signal->reclaim_state_lock);
			if (unlikely(tsk->signal->swapin_should_readahead_m != GC_RECLAIM_QUEUE))
				pr_info("post-check task %s(%d) unexpected status %d"
						, tsk->comm, tsk->pid, tsk->signal->swapin_should_readahead_m);
			tsk->signal->swapin_should_readahead_m = RECLAIM_STANDBY;
			spin_unlock(&tsk->signal->reclaim_state_lock);

			put_task_struct(tsk);
		}

		if (kthread_should_stop())
			break;
	}
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
		//schedule();
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

	//u64 time_ns = 0;
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

	/* setup nice: 130 cpumask: 0x7f */
	cpumask_parse("7f", &tmask);
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

int get_anon_memory(struct task_struct *task, unsigned long __user *buf)
{
	unsigned long size = 0;
	struct mm_struct *mm = get_task_mm(task);
	if (mm) {
		size = get_mm_counter(mm, MM_ANONPAGES);
		mmput(mm);
	}
	if (copy_to_user(buf, &size, sizeof(unsigned long)))
		return -EFAULT;
	return 0;
}
/* caller must hold spin lock before calling */
static bool check_can_reclaimd(struct task_struct * task) {
	bool ret = false;
	if (task->signal->memplus_type != TYPE_WILL_NEED
		&& task->signal->oom_score_adj >= 800
		&& time_after_eq(jiffies, task->signal->reclaim_timeout)) {
		task->signal->reclaim_timeout = jiffies + 2*HZ;
		ret = true;
	}
	return ret;
}

static long memplus_sub_ioctl(unsigned int cmd, void __user *parg)
{
	long ret = 0;
	unsigned long pid;
	struct task_struct *task;
	unsigned long size = 0;
	int uid;
	bool can_reclaim = false;

	if (copy_from_user(&pid, parg, sizeof(unsigned long)))
		return -EFAULT;

	//printk("memplus_ioctl: pid = %lu\n", pid);
	rcu_read_lock();
	task = find_task_by_vpid((int)pid);

	if (!task) {
		rcu_read_unlock();
		return -ESRCH;
	}

	get_task_struct(task);
	rcu_read_unlock();
	uid = task_uid(task).val;

	switch (cmd) {
		case MEMPLUS_GET_ANON_MEMORY:
			if (get_anon_memory(task, parg))
				ret = -EFAULT;
			break;
		case MEMPLUS_RECLAIM_ANON_MEMORY:
			/* TODO: reclaim directly, the reclaimd thread move to userspace */
			if (is_fg(uid)) {
				pr_err("task %s(pid:%d uid:%d) is top app\n", task->comm, pid, uid);
				if (copy_to_user(parg, &size, sizeof(unsigned long)))
					ret = -EFAULT;
				break;
			}
			spin_lock(&task->signal->reclaim_state_lock);
			if (task->signal->swapin_should_readahead_m == RECLAIM_STANDBY) {
				task->signal->swapin_should_readahead_m = RECLAIM_QUEUE;
				can_reclaim = check_can_reclaimd(task);
				spin_unlock(&task->signal->reclaim_state_lock);
				if (can_reclaim && uid > AID_APP) {
					size = reclaim_anon(task, 900);
				}
			} else {
				spin_unlock(&task->signal->reclaim_state_lock);
				pr_err("task %s(pid:%d) is doing swapin, top app?\n",task->comm, pid);
			}

			if (copy_to_user(parg, &size, sizeof(unsigned long)))
				ret = -EFAULT;
			break;
		case MEMPLUS_SWAPIN_ANON_MEMORY:
			/* TODO: swapin directly, the swapind thread move to userspace,
			 * if the task's MM_SWAPENTS is zero, no need to swapin its memory.
			 * mark the task as swaping, let the task is doing reclaim stop immediately
			 */
			spin_lock(&task->signal->reclaim_state_lock);
			task->signal->swapin_should_readahead_m = SWAPIN_QUEUE;
			spin_unlock(&task->signal->reclaim_state_lock);
			swapin_anon(task, 0);
			break;
	}
	put_task_struct(task);

	return ret;
}

long memplus_ioctl(struct file *file, unsigned int cmd, unsigned long args)
{
	long ret = 0;
	void __user *parg = (void __user *)args;
	unsigned long size = 0;

	if (cmd == MEMPLUS_GET_AVAILABLE_SWAP_SPACE) {
		size = atomic_long_read(&nr_swap_pages);
		if (copy_to_user(parg, &size, sizeof(unsigned long)))
			return -EFAULT;
	} else
		ret = memplus_sub_ioctl(cmd, parg);

	return ret;
}

static const struct file_operations memplus_ops = {
	.unlocked_ioctl = memplus_ioctl,
};

void memplus_stop(void)
{
	if (reclaimd_tsk) {
		kthread_stop(reclaimd_tsk);
		reclaimd_tsk = NULL;
	}
	if (swapind_tsk) {
		kthread_stop(swapind_tsk);
		swapind_tsk = NULL;
	}
	if (gc_tsk) {
		kthread_stop(gc_tsk);
		gc_tsk = NULL;
	}
}

static int __init memplus_init(void)
{
	//TODO: priority tuning for reclaimd/swapind
	//struct sched_param param = { .sched_priority = MAX_USER_RT_PRIO -1 };
	//struct sched_param param = { .sched_priority = 1 };
	struct memplus_cb_set set;
	struct miscdevice *misc = NULL;

	reclaimd_tsk = kthread_run(reclaimd_fn, 0, "reclaimd");
	if (IS_ERR(reclaimd_tsk)) {
		pr_err("Failed to start reclaimd\n");
		reclaimd_tsk = NULL;
	}

	swapind_tsk = kthread_run(swapind_fn, 0, "swapind");
	if (IS_ERR(swapind_tsk)) {
		pr_err("Failed to start swapind\n");
		swapind_tsk = NULL;
	} else {
		/* if do_swap_page by swapind, don't calculate next event */
		swapind_tsk->signal->reclaim_timeout = 1;
		//if (sched_setscheduler_nocheck(swapind_tsk, SCHED_FIFO, &param)) {
		//	pr_warn("%s: failed to set SCHED_FIFO\n", __func__);
		//}
	}
	gc_tsk = kthread_run(gc_fn, 0, "system_gcd");
	if (IS_ERR(gc_tsk)) {
		pr_err("Failed to start system_gcd\n");
		gc_tsk = NULL;
	}

	memex_tsk = kthread_run(memex_fn, 0, "memex");
	if (IS_ERR(memex_tsk)) {
		pr_err("Failed to start memex_task\n");
		memex_tsk = NULL;
	}

#if defined(CONFIG_PAGE_EXTENSION) && defined(CONFIG_PAGE_OWNER_ENABLE_DEFAULT)
	pr_info("ext mode\n");
#else
	pr_info("normal mode\n");
#endif

	misc = kzalloc(sizeof(struct miscdevice), GFP_KERNEL);
	if (!misc) {
		pr_err("Failed alloc memplus miscdevice\n");
		return -1;
	}
	misc->fops = &memplus_ops;
	misc->name = "memplus";
	misc->minor = MISC_DYNAMIC_MINOR;
	if (misc_register(misc)) {
		pr_err("Failed to create dev/memplus\n");
		return -1;
	}
	set.current_is_swapind_cb = ctech_current_is_swapind;
	set.memplus_check_isolate_page_cb = ctech_memplus_check_isolate_page;
	set.memplus_enabled_cb = ctech_memplus_enabled;
	set.memplus_move_anon_to_swapcache_lru_cb = ctech_memplus_move_anon_to_swapcache_lru;
	set.memplus_move_swapcache_to_anon_lru_cb = ctech_memplus_move_swapcache_to_anon_lru;
	set.memplus_state_check_cb = ctech_memplus_state_check;
	set.__memplus_enabled_cb = ctech__memplus_enabled;
	set.memplus_next_event_cb = ctech_memplus_next_event;

	register_cb_set(&set);
	return 0;
}

unsigned long memplus_scan(void)
{
	struct pagevec pvec;
	unsigned nr_space = 0;
	pgoff_t index = 0, indices[PAGEVEC_SIZE];
	int i, j, iso_count = 0;
	struct address_space *spaces;
	struct swap_info_struct *sis, *next;
	unsigned int node = numa_node_id();
	unsigned int total_swapcache = total_swapcache_pages();
	LIST_HEAD(page_list);

	if (!total_swapcache)
		return 0;

	spin_lock(&swap_lock);
	plist_for_each_entry_safe(sis, next, &swap_avail_heads[node], avail_lists[node]) {
		nr_space = DIV_ROUND_UP(sis->max, SWAP_ADDRESS_SPACE_PAGES);
		spaces = rcu_dereference(swapper_spaces[sis->type]);
		if (!nr_space || !spaces)
			continue;
		for (j = 0; j < nr_space; j++) {
			index = 0;
			pagevec_init(&pvec);
			while (pagevec_lookup_entries(&pvec, &spaces[j], index, (pgoff_t)PAGEVEC_SIZE, indices)) {
				for (i = 0; i < pagevec_count(&pvec); i++) {
					struct page *page = pvec.pages[i];

					index = indices[i];

					if (radix_tree_exceptional_entry(page)) {
						continue;
					}

					if (!PageSwapCache(page))
						continue;

					if (PageWriteback(page))
						continue;

					if (isolate_lru_page(page))
						continue;

					if (PageAnon(page) && !PageSwapBacked(page)) {
						putback_lru_page(page);
						continue;
					}

					ClearPageWillneed(page);
					list_add(&page->lru, &page_list);
					inc_node_page_state(page, NR_ISOLATED_ANON +
							page_is_file_cache(page));
					iso_count ++;
				}
				pagevec_remove_exceptionals(&pvec);
				pagevec_release(&pvec);
				index++;
			}
		}
	}
	spin_unlock(&swap_lock);
	return coretech_reclaim_pagelist(&page_list, NULL, NULL);
}

static int memory_plus_test_worstcase_store(const char *buf, const struct kernel_param *kp)
{
	unsigned int val;
	unsigned long freed = 0;

	if (sscanf(buf, "%u\n", &val) <= 0)
		return -EINVAL;

	if(val == 1)
		freed = memplus_scan();
	printk("memory_plus_test_worstcase_store: freed = %ld\n", freed);

	return 0;
}

/* return value mapping:
 * 0     - success
 * ESRCH - no GC daemon
 * EPERM - memplus is diabled by user
 * EINVAL- invalid input
 * EBUSY - triggered too frequently
 */
static int memory_plus_wake_gcd_store(const char *buf, const struct kernel_param *kp)
{
	static ktime_t last_wake;
	unsigned int val;
	ktime_t cur_ktime;
	s64 elapsed_hr;

	if (!gc_tsk)
		return -ESRCH;
	if (vm_memory_plus == 2 || vm_memory_plus == 0)
		return -EPERM;
	if (sscanf(buf, "%u\n", &val) <= 0)
		return -EINVAL;

	cur_ktime = ktime_get_boottime();
	elapsed_hr = ktime_to_ms(ktime_sub(cur_ktime, last_wake)) / (MSEC_PER_SEC * 3600);

#if DEBUG_GCD
	pr_info("elapsed bootime %d sec, hr %d\n"
			, ktime_to_ms(ktime_sub(cur_ktime, last_wake)) / MSEC_PER_SEC, elapsed_hr);
	pr_info("elapsed display on jiffies %d sec\n"
			, (atomic64_read(&accu_display_on_jiffies)
				+ (display_on ? (jiffies - display_on_jiffies) : 0)) / HZ);
#endif
#if GCD_SST
	wake_up_process(gc_tsk);
#endif
	/* 24hr control */
	if (last_wake && elapsed_hr < 24)
		return -EBUSY;

	wake_up_process(gc_tsk);
	last_wake = cur_ktime;

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

static struct kernel_param_ops memory_plus_test_worstcase_ops = {
	.set = memory_plus_test_worstcase_store,
};

static struct kernel_param_ops memory_plus_wake_gcd_ops = {
	.set = memory_plus_wake_gcd_store,
};

static struct kernel_param_ops memory_plus_wake_memex_ops = {
	.set = memory_plus_wake_memex_store,
	.get = memory_plus_wake_memex_show,
};

module_param_cb(memory_plus_test_worstcase, &memory_plus_test_worstcase_ops, NULL, 0200);
module_param_cb(memory_plus_wake_gcd, &memory_plus_wake_gcd_ops, NULL, 0644);
module_param_cb(memory_plus_wake_memex, &memory_plus_wake_memex_ops, NULL, 0644);

module_param_named(memory_plus_enabled, vm_memory_plus, uint, S_IRUGO | S_IWUSR);
module_param_named(memplus_add_to_swap, memplus_add_to_swap, ulong, S_IRUGO | S_IWUSR);
module_param_named(memory_plus_cam_aware, vm_cam_aware, uint, S_IRUGO | S_IWUSR);

module_init(memplus_init)
