#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
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
#include <linux/psi.h>
#include <linux/swapops.h>
#include <linux/cpufreq.h>
#include <linux/proc_fs.h>

#if defined(OPLUS_FEATURE_PROCESS_RECLAIM) && defined(CONFIG_PROCESS_RECLAIM_ENHANCE)
#include <linux/process_mm_reclaim.h>
#endif

#include "internal.h"

#define SHRINK_LRUVECD_HIGH (0x1000)  //16Mbytes

#define PG_nolockdelay (__NR_PAGEFLAGS + 2)
#define SetPageNoLockDelay(page) set_bit(PG_nolockdelay, &(page)->flags)
#define TestPageNoLockDelay(page) test_bit(PG_nolockdelay, &(page)->flags)
#define TestClearPageNoLockDelay(page) test_and_clear_bit(PG_nolockdelay, &(page)->flags)
#define ClearPageNoLockDelay(page) clear_bit(PG_nolockdelay, &(page)->flags)

#define PG_skiped_lock (__NR_PAGEFLAGS + 3)
#define SetPageSkipedLock(page) set_bit(PG_skiped_lock, &(page)->flags)
#define ClearPageSkipedLock(page) clear_bit(PG_skiped_lock, &(page)->flags)
#define PageSkipedLock(page) test_bit(PG_skiped_lock, &(page)->flags)
#define TestClearPageSkipedLock(page) test_and_clear_bit(PG_skiped_lock, &(page)->flags)

extern int unforce_reclaim_pages_from_list(struct list_head *page_list);
bool async_shrink_lruvec_setup = false;
static struct task_struct *shrink_lruvec_tsk = NULL;
static atomic_t shrink_lruvec_runnable = ATOMIC_INIT(0);
unsigned long shrink_lruvec_pages = 0;
unsigned long shrink_lruvec_pages_max = 0;
unsigned long shrink_lruvec_handle_pages = 0;
wait_queue_head_t shrink_lruvec_wait;
spinlock_t l_inactive_lock;
LIST_HEAD(lru_inactive);

static void add_to_lruvecd_inactive_list(struct page *page)
{
	list_move(&page->lru, &lru_inactive);

	/* account how much pages in lru_inactive */
	shrink_lruvec_pages += hpage_nr_pages(page);
	if (shrink_lruvec_pages > shrink_lruvec_pages_max)
		shrink_lruvec_pages_max = shrink_lruvec_pages;
}

bool wakeup_kshrink_lruvecd(struct list_head *page_list)
{
	struct page *page, *next;
	bool shrink_lruvecd_is_full = false;
	bool pages_should_be_reclaim = false;
	LIST_HEAD(tmp_lru_inactive);

	if (unlikely(!async_shrink_lruvec_setup))
		return true;

	if (list_empty(page_list))
		return true;

	if (unlikely(shrink_lruvec_pages > SHRINK_LRUVECD_HIGH))
		shrink_lruvecd_is_full = true;

	list_for_each_entry_safe(page, next, page_list, lru) {
		ClearPageNoLockDelay(page);
		if (unlikely(TestClearPageSkipedLock(page))) {
			/* trylock failed and been skiped  */
			ClearPageActive(page);
			if (!shrink_lruvecd_is_full)
				list_move(&page->lru, &tmp_lru_inactive);
		}
	}

	if (unlikely(!list_empty(&tmp_lru_inactive))) {
		spin_lock_irq(&l_inactive_lock);
		list_for_each_entry_safe(page, next, &tmp_lru_inactive, lru) {
			if (likely(!shrink_lruvecd_is_full)) {
				pages_should_be_reclaim = true;
				add_to_lruvecd_inactive_list(page);
			}
		}
		spin_unlock_irq(&l_inactive_lock);
	}

	if (shrink_lruvecd_is_full || !pages_should_be_reclaim)
		return true;

	if (atomic_read(&shrink_lruvec_runnable) == 1)
		return true;

	atomic_set(&shrink_lruvec_runnable, 1);
	wake_up_interruptible(&shrink_lruvec_wait);

	return true;
}

void set_shrink_lruvecd_cpus(void)
{
	struct cpumask mask;
	struct cpumask *cpumask = &mask;
	pg_data_t *pgdat = NODE_DATA(0);
	unsigned int cpu = 0, cpufreq_max_tmp = 0;
	struct cpufreq_policy *policy_max;
	static bool set_slabd_cpus_success = false;

	if (unlikely(!async_shrink_lruvec_setup))
		return;

	if (likely(set_slabd_cpus_success))
		return;

	for_each_possible_cpu(cpu) {
		struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

		if (policy == NULL)
			continue;

		if (policy->cpuinfo.max_freq >= cpufreq_max_tmp) {
			cpufreq_max_tmp = policy->cpuinfo.max_freq;
			policy_max = policy;
		}
	}

	cpumask_copy(cpumask, cpumask_of_node(pgdat->node_id));
	cpumask_andnot(cpumask, cpumask, policy_max->related_cpus);

	if (!cpumask_empty(cpumask)) {
		set_cpus_allowed_ptr(shrink_lruvec_tsk, cpumask);
		set_slabd_cpus_success = true;
	}
}

static int shrink_lruvecd(void *p)
{
	pg_data_t *pgdat;
	LIST_HEAD(tmp_lru_inactive);
	struct page *page, *next;
	struct list_head;

	/*
	 * Tell the memory management that we're a "memory allocator",
	 * and that if we need more memory we should get access to it
	 * regardless (see "__alloc_pages()"). "kswapd" should
	 * never get caught in the normal page freeing logic.
	 *
	 * (Kswapd normally doesn't need memory anyway, but sometimes
	 * you need a small amount of memory in order to be able to
	 * page out something else, and this flag essentially protects
	 * us from recursively trying to free more memory as we're
	 * trying to free the first piece of memory in the first place).
	 */
	pgdat = (pg_data_t *)p;

	current->flags |= PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD | PF_SHRNIK_LRUVECD;
	set_freezable();

	while (!kthread_should_stop()) {
		wait_event_freezable(shrink_lruvec_wait,
			(atomic_read(&shrink_lruvec_runnable) == 1));

		set_shrink_lruvecd_cpus();
retry_reclaim:

		spin_lock_irq(&l_inactive_lock);
		if (list_empty(&lru_inactive)) {
			spin_unlock_irq(&l_inactive_lock);
			atomic_set(&shrink_lruvec_runnable, 0);
			continue;
		}
		list_for_each_entry_safe(page, next, &lru_inactive, lru) {
			list_move(&page->lru, &tmp_lru_inactive);
			shrink_lruvec_pages -= hpage_nr_pages(page);
			shrink_lruvec_handle_pages += hpage_nr_pages(page);
		}
		spin_unlock_irq(&l_inactive_lock);

		unforce_reclaim_pages_from_list(&tmp_lru_inactive);
		goto retry_reclaim;
	}
	current->flags &= ~(PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD | PF_SHRNIK_LRUVECD);

	return 0;
}

void setpage_reclaim_trylock(struct page *page)
{
	if (unlikely(!async_shrink_lruvec_setup))
		return;
	ClearPageSkipedLock(page);

	if (unlikely(current->flags & PF_SHRNIK_LRUVECD)) {
		ClearPageNoLockDelay(page);
		return;
	}

	SetPageNoLockDelay(page);
}

bool page_reclaim_trylock_fail(struct page *page)
{
	ClearPageNoLockDelay(page);  /* trylock sucessfully */
	if (unlikely(!async_shrink_lruvec_setup))
		return false;

	if (unlikely(current->flags & PF_SHRNIK_LRUVECD)) {
		ClearPageNoLockDelay(page);
		return false;
	}

	if (PageSkipedLock(page))
		return true; /*page trylock failed and been skipped*/

	return false; /*page trylock successfully and  clear bit by should_page_skip_lock*/
}

void clearpage_reclaim_trylock(struct page *page, bool clear_skipped)
{
	ClearPageNoLockDelay(page);

	if (clear_skipped)
		ClearPageSkipedLock(page);
}

bool reclaim_page_trylock(struct page *page, struct rw_semaphore *sem,
				bool *got_lock)
{
	bool ret = false;

	if (unlikely(!async_shrink_lruvec_setup))
		return ret;

	if (TestClearPageNoLockDelay(page)) {
		ret = true;

		if (sem == NULL)
			return ret;

		if (down_read_trylock(sem)) {   /* return 1 successful */
			*got_lock = true;

		} else {
			SetPageSkipedLock(page);  /* trylock failed and skipped */
			*got_lock = false;
		}

		return ret;
	}
	return ret;
}

static int kshrink_lruvecd_show(struct seq_file *m, void *arg)
{
	seq_printf(m,
		  "10async_shrink_lruvec_setup:     %s\n"
		   "shrink_lruvec_pages:     %lu\n"
		   "shrink_lruvec_handle_pages:     %lu\n"
		   "shrink_lruvec_pages_max:     %lu\n",
		   async_shrink_lruvec_setup ? "enable" : "disable",
		   shrink_lruvec_pages,
		   shrink_lruvec_handle_pages,
		   shrink_lruvec_pages_max);
	seq_putc(m, '\n');

	return 0;
}

static int kshrink_lruvecd_open(struct inode *inode, struct file *file)
{
    return single_open(file, kshrink_lruvecd_show, NULL);
}

static const struct file_operations kshrink_lruvecd_operations = {
	.open		= kshrink_lruvecd_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int async_shrink_lruvec_init(void)
{
    	struct proc_dir_entry *pentry;
	pg_data_t *pgdat = NODE_DATA(0);
	int ret;

	init_waitqueue_head(&shrink_lruvec_wait);
	spin_lock_init(&l_inactive_lock);

	shrink_lruvec_tsk = kthread_run(shrink_lruvecd, pgdat, "kshrink_lruvecd");
	if (IS_ERR_OR_NULL(shrink_lruvec_tsk)) {
		pr_err("Failed to start shrink_lruvec on node 0\n");
		ret = PTR_ERR(shrink_lruvec_tsk);
		shrink_lruvec_tsk = NULL;
		return ret;
	}

	pentry = proc_create("kshrink_lruvecd_status", 0400, NULL, &kshrink_lruvecd_operations);
	async_shrink_lruvec_setup = true;
	return 0;
}
module_init(async_shrink_lruvec_init);
