
#ifdef CONFIG_SCHED_TUNE

#include <linux/reciprocal_div.h>

/*
 * System energy normalization constants
 */
struct target_nrg {
	unsigned long min_power;
	unsigned long max_power;
	struct reciprocal_value rdiv;
};

int schedtune_cpu_boost_with(int cpu, struct task_struct *p);
int schedtune_task_boost(struct task_struct *tsk);

int schedtune_prefer_idle(struct task_struct *tsk);

void schedtune_enqueue_task(struct task_struct *p, int cpu);
void schedtune_dequeue_task(struct task_struct *p, int cpu);

#ifdef OPLUS_FEATURE_POWER_CPUFREQ
unsigned int schedtune_window_policy(struct task_struct *p);
unsigned int uclamp_discount_wait_time(struct task_struct *p);
unsigned int uclamp_top_task_filter(struct task_struct *p);
unsigned int uclamp_ed_task_filter(struct task_struct *p);
#endif

#else /* CONFIG_SCHED_TUNE */

#define schedtune_cpu_boost_with(cpu, p)  0
#define schedtune_task_boost(tsk) 0

#define schedtune_prefer_idle(tsk) 0

#define schedtune_enqueue_task(task, cpu) do { } while (0)
#define schedtune_dequeue_task(task, cpu) do { } while (0)

#define stune_util(cpu, other_util, walt_load) cpu_util_cfs(cpu_rq(cpu))

#ifdef OPLUS_FEATURE_POWER_CPUFREQ
#define schedtune_window_policy(tsk) 0
#endif

#endif /* CONFIG_SCHED_TUNE */
