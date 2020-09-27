#ifdef CONFIG_UXCHAIN

#include <linux/version.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include "../../../../kernel/sched/sched.h"
#define TID_MAGIC 0x57590000

struct task_struct *get_futex_owner(u32 __user *uaddr2) 
{
	int owner_tid = -1;
	struct task_struct *futex_owner = NULL;

	if (uaddr2 != NULL) {
		if (copy_from_user(&owner_tid, uaddr2, sizeof(int))) {
		} else if (owner_tid != 0) {
			int tmp = owner_tid & 0xffff0000;

			if (tmp == TID_MAGIC)
				owner_tid &= 0xffff;
			else
				return NULL;
			rcu_read_lock();
			futex_owner = find_task_by_vpid(owner_tid);
			if (futex_owner)
				get_task_struct(futex_owner);
			rcu_read_unlock();
		}
	}
	return futex_owner;
}

static void uxchain_resched_task(struct task_struct *task)
{
	struct rq *rq;
	struct rq_flags rf;

	rq = task_rq_lock(task, &rf);
	if (task->state != TASK_RUNNING || rq->curr == task) {
		task_rq_unlock(rq, task, &rf);
		return;
	}
	update_rq_clock(rq);
	deactivate_task(rq, task, DEQUEUE_NOCLOCK);
	activate_task(rq, task, ENQUEUE_NOCLOCK);
	task_rq_unlock(rq, task, &rf);
	resched_cpu(task_cpu(task));
}

static void uxchain_list_add_ux(struct list_head *entry, struct list_head *head)
{
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct mutex_waiter *waiter = NULL;
	list_for_each_safe(pos, n, head) {
		waiter = list_entry(pos, struct mutex_waiter, list);
		if (!waiter->task->static_ux) {
			list_add(entry, waiter->list.prev);
			return;
		}
	}
	if (pos == head) {
		list_add_tail(entry, head);
	}
}

void uxchain_mutex_list_add(struct task_struct *task, struct list_head *entry, struct list_head *head, struct mutex *lock)
{
	struct task_struct *owner;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
	owner = __mutex_owner(lock);
#else
	owner = lock->owner;
#endif
	if (!entry || !head || !lock) {
		return;
	}
	if (task->static_ux) {
		uxchain_list_add_ux(entry, head);
	} else {
		list_add_tail(entry, head);
	}
}

void uxchain_dynamic_ux_boost(struct task_struct *owner, struct task_struct *task)
{
	if (task->static_ux && owner && !owner->dynamic_ux) {
		owner->dynamic_ux = 1;
		owner->ux_depth = task->ux_depth + 1;
		uxchain_resched_task(owner);
	}
	if (task->dynamic_ux && owner && !owner->dynamic_ux /*&& task->ux_depth < 2*/) {
		owner->dynamic_ux = 1;
		owner->ux_depth = task->ux_depth + 1;
		uxchain_resched_task(owner);
	}
}

void uxchain_dynamic_ux_reset(struct task_struct *task)
{
	task->dynamic_ux = 0;
	task->ux_depth = 0;
}

int ux_thread(struct task_struct *task)
{
	return task->dynamic_ux || task->static_ux;
}

#endif
