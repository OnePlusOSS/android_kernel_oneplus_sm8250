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
#include <linux/buffer_head.h>	/* for try_to_release_page(),
					buffer_heads_over_limit */
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
#include <linux/proc_fs.h>

#define MAPCOUNT_PROTECT_THRESHOLD (20)
/*
 * [0]: nr_anon_mapped_multiple
 * [1]: nr_file_mapped_multiple
 *
 * */
static atomic_long_t nr_mapped_multiple[2] = {
	ATOMIC_INIT(0)
};
long  max_nr_mapped_multiple[2] = { 0 };
long  mapcount_protected_high[2] = { 0 };

unsigned long  memavail_noprotected = 0;
static bool mapcount_protect_setup = false;
static bool mapped_protect_debug = false;

long multi_mapped(int file)
{
	return atomic_long_read(&nr_mapped_multiple[file]);
}

long multi_mapped_max(int file)
{
	return max_nr_mapped_multiple[file];
}
static bool mapped_protected_is_full(int file)
{
	if (atomic_long_read(&nr_mapped_multiple[file])
			> mapcount_protected_high[file])
		return true;
	else
		return false;
}

static bool mem_available_is_low(void)
{
	long available = si_mem_available();

	if (available < memavail_noprotected)
		return true;

	return false;
}

void mapped_page_try_sorthead(struct page *page)
{
	struct pglist_data *pgdat = page_pgdat(page);
	struct lruvec *lruvec;
	int file;
	int lru;

	if (likely(page_mapcount(page) < MAPCOUNT_PROTECT_THRESHOLD))
		return;

	if (!PageLRU(page) || PageUnevictable(page))
		return;

	if (!PageActive(page) && !PageUnevictable(page) &&
			(PageReferenced(page) || page_mapcount(page) > 10))
		return;

	lru = page_lru(page);
	if (lru == LRU_UNEVICTABLE)
		return;

	file = is_file_lru(page_lru(page));
	if (unlikely(mapped_protected_is_full(file)))
		return;

	if (spin_trylock_irq(&pgdat->lru_lock)) {
		lruvec = mem_cgroup_page_lruvec(page, pgdat);
		if (PageLRU(page) && !PageUnevictable(page))
			list_move(&page->lru, &lruvec->lists[lru]);
		spin_unlock_irq(&pgdat->lru_lock);
	}

}
unsigned long page_should_be_protect(struct page *page)
{
	int file;

	if (unlikely(!mapcount_protect_setup))
		return 0;

	if (likely(page_mapcount(page) < MAPCOUNT_PROTECT_THRESHOLD))
		return 0;

	if (unlikely(!page_evictable(page) || PageUnevictable(page)))
		return 0;

	file = is_file_lru(page_lru(page));
	if (unlikely(mapped_protected_is_full(file)))
		return 0;

	if (unlikely(mem_available_is_low()))
		return 0;

	return hpage_nr_pages(page);
}

bool update_mapped_mul(struct page *page, bool inc_size)
{
	unsigned long nr_mapped_multi_pages;
	int file, mapcount;
	bool ret;

	if (inc_size) {
		mapcount = atomic_inc_return(&page->_mapcount);
		ret = ((mapcount == 0) ? true : false);
	} else {
		mapcount = atomic_add_return(-1, &page->_mapcount);
		ret = ((mapcount < 0) ? true : false);
	}
	/* we update multi-mapped counts when page_mapcount(page) changing:
	 * - 19->20
	 * - 20->19
	 * Because we judge mapcount by page_mapcount(page) which return
	 * page->_mapcount + 1, variable "mapcount" + 1 = page_mapcount(page).
	 * If inc_size is equal to true, "mapcount" + 1 = MAPCOUNT_PROTECT_THRESHOLD
	 * which means 19->20.
	 * If inc_size is equal to false, "mapcount" + 2 = MAPCOUNT_PROTECT_THRESHOLD
	 * which means 20->19.
	 * */
	if (likely((mapcount + (inc_size ? 1 : 2)) !=
					MAPCOUNT_PROTECT_THRESHOLD))
		return ret;

	if (unlikely(!mapcount_protect_setup))
		return ret;

	if (!PageLRU(page) || PageUnevictable(page))
		return ret;

	file = is_file_lru(page_lru(page));

	if (inc_size) {
		atomic_long_add(hpage_nr_pages(page), &nr_mapped_multiple[file]);
		nr_mapped_multi_pages = atomic_long_read(&nr_mapped_multiple[file]);
		if (max_nr_mapped_multiple[file] < nr_mapped_multi_pages)
			max_nr_mapped_multiple[file] = nr_mapped_multi_pages;
	} else {
		atomic_long_sub(hpage_nr_pages(page), &nr_mapped_multiple[file]);
	}

	return ret;
}

void add_mapped_mul_op_lrulist(struct page *page, enum lru_list lru)
{
	unsigned long nr_mapped_multi_pages;
	int file;

	if (unlikely(!mapcount_protect_setup))
		return;

	if (likely(page_mapcount(page) < MAPCOUNT_PROTECT_THRESHOLD))
		return;

	if (lru == LRU_UNEVICTABLE)
		return;

	file = is_file_lru(lru);

	atomic_long_add(hpage_nr_pages(page), &nr_mapped_multiple[file]);
	nr_mapped_multi_pages = atomic_long_read(&nr_mapped_multiple[file]);
	if (max_nr_mapped_multiple[file] < nr_mapped_multi_pages)
		max_nr_mapped_multiple[file] = nr_mapped_multi_pages;
}

void dec_mapped_mul_op_lrulist(struct page *page, enum lru_list lru)
{
	int file;

	if (unlikely(!mapcount_protect_setup))
		return;

	if (likely(page_mapcount(page) < MAPCOUNT_PROTECT_THRESHOLD))
		return;

	if (lru == LRU_UNEVICTABLE)
		return;

	file = is_file_lru(lru);

	atomic_long_sub(hpage_nr_pages(page), &nr_mapped_multiple[file]);
}

static int mapped_protect_show(struct seq_file *m, void *arg)
{
	pg_data_t *pgdat;
	unsigned long now_nr_mapped[2] = {0};

	if (!mapped_protect_debug)
		return 0;

	for_each_online_pgdat(pgdat) {
		struct mem_cgroup *memcg = NULL;
		struct page *page;
		int lru;

		spin_lock_irq(&pgdat->lru_lock);
		memcg = mem_cgroup_iter(NULL, NULL, NULL);
		do {
			struct lruvec *lruvec = mem_cgroup_lruvec(pgdat, memcg);

			for_each_evictable_lru(lru) {
				int file = is_file_lru(lru);
                    		list_for_each_entry(page, &lruvec->lists[lru], lru) {
					if (!page)
						continue;
					if (page_mapcount(page) >= MAPCOUNT_PROTECT_THRESHOLD) {
						now_nr_mapped[file] += hpage_nr_pages(page);
					}
				}
			}
			memcg = mem_cgroup_iter(NULL, memcg, NULL);
		} while (memcg);

		spin_unlock_irq(&pgdat->lru_lock);
	}

	seq_printf(m,
		   "now_anon_nr_mapped:     %ld\n"
		   "now_file_nr_mapped:     %ld\n"
		   "nr_anon_mapped_multiple:     %ld\n"
		   "nr_file_mapped_multiple:     %ld\n"
		   "max_nr_anon_mapped_multiple:     %ld\n"
		   "max_nr_file_mapped_multiple:     %ld\n"
		   "nr_anon_mapped_high:     %lu\n"
		   "nr_file_mapped_high:     %lu\n"
		   "memavail_noprotected:     %lu\n",
		   now_nr_mapped[0],
		   now_nr_mapped[1],
		   nr_mapped_multiple[0],
		   nr_mapped_multiple[1],
		   max_nr_mapped_multiple[0],
		   max_nr_mapped_multiple[1],
		   mapcount_protected_high[0],
		   mapcount_protected_high[1],
		   memavail_noprotected);
	seq_putc(m, '\n');

	return 0;
}

static int mapped_protect_open(struct inode *inode, struct file *file)
{
    return single_open(file, mapped_protect_show, NULL);
}

static const struct file_operations mapped_protect_file_operations = {
	.open		= mapped_protect_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};


static int __init mapped_protect_init(void)
{
    	struct proc_dir_entry *pentry;
	pg_data_t *pgdat;

	memavail_noprotected = totalram_pages / 10;
	mapcount_protected_high[0] = totalram_pages / 20;
	mapcount_protected_high[1] = totalram_pages / 20;

	for_each_online_pgdat(pgdat) {
		struct mem_cgroup *memcg = NULL;
		struct page *page;
		int lru;

		spin_lock_irq(&pgdat->lru_lock);
		memcg = mem_cgroup_iter(NULL, NULL, NULL);
		do {
			struct lruvec *lruvec = mem_cgroup_lruvec(pgdat, memcg);

			for_each_evictable_lru(lru) {
				int file = is_file_lru(lru);
                    		list_for_each_entry(page, &lruvec->lists[lru], lru) {
					if (!page)
						continue;
					if (page_mapcount(page) >= MAPCOUNT_PROTECT_THRESHOLD) {
						atomic_long_add(hpage_nr_pages(page), &nr_mapped_multiple[file]);
					}
				}
			}
			memcg = mem_cgroup_iter(NULL, memcg, NULL);
		} while (memcg);

		spin_unlock_irq(&pgdat->lru_lock);
	}
	mapcount_protect_setup = true;
	pentry = proc_create("mapped_protect_show", 0400, NULL, &mapped_protect_file_operations);

	if (!pentry)
		return -ENOMEM;

	return 0;
}

module_init(mapped_protect_init);

