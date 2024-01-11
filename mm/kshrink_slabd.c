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

#if defined(OPLUS_FEATURE_PROCESS_RECLAIM) && defined(CONFIG_PROCESS_RECLAIM_ENHANCE)
#include <linux/process_mm_reclaim.h>
#endif

#include "internal.h"

extern unsigned long shrink_slab(gfp_t gfp_mask, int nid,
				 struct mem_cgroup *memcg,
				 int priority);

static bool kshrink_slabd_setup = false;
wait_queue_head_t shrink_slabd_wait;
struct async_slabd_parameter {
	gfp_t shrink_slabd_gfp_mask;
	int shrink_slabd_nid;
	struct mem_cgroup *shrink_slabd_memcg;
	int shrink_slabd_priority;
	int shrink_slabd_runnable;
} asp;

static struct task_struct *shrink_slabd_tsk = NULL;
static struct reclaim_state async_reclaim_state = {
	.reclaimed_slab = 0,
};

bool wakeup_shrink_slabd(gfp_t gfp_mask, int nid,
				 struct mem_cgroup *memcg,
				 int priority, struct reclaim_state *reclaim_state)
{
	if (unlikely(!kshrink_slabd_setup))
		return false;

	if (asp.shrink_slabd_runnable == 1)
		return true;

	async_reclaim_state.reclaimed_slab = 0;
	current->reclaim_state = reclaim_state = &async_reclaim_state;

	asp.shrink_slabd_gfp_mask = gfp_mask;
	asp.shrink_slabd_nid = nid;
	asp.shrink_slabd_memcg = memcg;
	asp.shrink_slabd_priority = priority;

	asp.shrink_slabd_runnable = 1;
	wake_up_interruptible(&shrink_slabd_wait);

	return true;
}

void set_kshrink_slabd_cpus(void)
{
	struct cpumask mask;
	struct cpumask *cpumask = &mask;
	pg_data_t *pgdat = NODE_DATA(0);
	unsigned int cpu = 0, cpufreq_max_tmp = 0;
	struct cpufreq_policy *policy_max;
	static bool set_slabd_cpus_success = false;

	if (unlikely(!kshrink_slabd_setup))
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
		set_cpus_allowed_ptr(shrink_slabd_tsk, cpumask);
		set_slabd_cpus_success = true;
	}
}

static int kshrink_slabd_func(void *p)
{
	int nid, priority;
	struct mem_cgroup *memcg;
	gfp_t gfp_mask;
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
	current->flags |= PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD;
	set_freezable();

	current->reclaim_state = &async_reclaim_state;
	asp.shrink_slabd_gfp_mask = 0;
	asp.shrink_slabd_nid = 0;
	asp.shrink_slabd_memcg = NULL;
	asp.shrink_slabd_priority = 0;
	asp.shrink_slabd_runnable = 0;

	while (!kthread_should_stop()) {
		wait_event_freezable(shrink_slabd_wait,
					(asp.shrink_slabd_runnable == 1));

		set_kshrink_slabd_cpus();

		gfp_mask = asp.shrink_slabd_gfp_mask;
		nid = asp.shrink_slabd_nid;
		priority = asp.shrink_slabd_priority;
		memcg = asp.shrink_slabd_memcg;

		shrink_slab(gfp_mask, nid, memcg, priority);

		asp.shrink_slabd_runnable = 0;
	}
	current->flags &= ~(PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD);
	current->reclaim_state = NULL;

	return 0;
}

static int kshrink_slabd_init(void)
{
	int ret;

	init_waitqueue_head(&shrink_slabd_wait);

	shrink_slabd_tsk = kthread_run(kshrink_slabd_func, NULL, "kshrink_slabd");
	if (IS_ERR_OR_NULL(shrink_slabd_tsk)) {
		pr_err("Failed to start shrink_slabd on node 0\n");
		ret = PTR_ERR(shrink_slabd_tsk);
		shrink_slabd_tsk = NULL;
		return ret;
	}

	kshrink_slabd_setup = true;
	return 0;
}
module_init(kshrink_slabd_init);
