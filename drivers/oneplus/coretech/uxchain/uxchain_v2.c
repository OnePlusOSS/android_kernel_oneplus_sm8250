#ifdef CONFIG_UXCHAIN_V2
#include "../../../../kernel/sched/sched.h"
#include <linux/mm.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/rwsem.h>



void uxchain_rwsem_wake(struct task_struct *tsk, struct rw_semaphore *sem)
{
	if (current->mm && sem == &(current->mm->mmap_sem) && sysctl_uxchain_v2)
		tsk->ux_once = 1;
}

void uxchain_rwsem_down(struct rw_semaphore *sem)
{
	if (current->mm && sem == &(current->mm->mmap_sem) && sysctl_uxchain_v2) {
		current->get_mmlock = 1;
		current->get_mmlock_ts = sched_ktime_clock();
	}
}

void uxchain_rwsem_up(struct rw_semaphore *sem)
{
	if (current->mm && sem == &(current->mm->mmap_sem) &&
		current->get_mmlock == 1 && sysctl_uxchain_v2)
		current->get_mmlock = 0;
}

#endif
