/***********************************************************
** File: - panic_flush.c
** Description:  code to flush device cache in panic
**
** Version: 1.0
** Date: 2019/08/27
** Activity: [ITN-14106]
****************************************************************/

#define DEBUG
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/kthread.h>

#define PANIC_FLUSH_POLL_MS (10)
struct panic_flush_control {
	struct task_struct *flush_thread;
	wait_queue_head_t flush_wq;
	atomic_t flush_issuing;
	atomic_t flush_issued;
	atomic_t flush_circled;
};

static struct panic_flush_control *pfc;
static void panic_issue_flush(struct super_block *sb ,void *arg)
{
	int ret = -1;
	int *flush_count = (int *)arg;
	if (!(sb->s_flags & MS_RDONLY) && NULL != sb->s_bdev) {
		ret = blkdev_issue_flush(sb->s_bdev, GFP_KERNEL, NULL);
	}
	if (!ret) {
		(*flush_count)++;
		pr_emerg("blkdev_issue_flush before panic return %d\n", *flush_count);
	}
}

static int panic_flush_thread(void *data)
{
	int flush_count = 0;
	int flush_count_circle = 0;
repeat:
	if (kthread_should_stop())
		return 0;

	if (atomic_read(&pfc->flush_circled) > 0)
		wait_event_timeout(pfc->flush_wq, kthread_should_stop() || atomic_read(&pfc->flush_issuing) > 0, msecs_to_jiffies(500));
	else
		wait_event(pfc->flush_wq, kthread_should_stop() || atomic_read(&pfc->flush_issuing) > 0 || atomic_read(&pfc->flush_circled) > 0);

	if (atomic_read(&pfc->flush_issuing) > 0) {
		iterate_supers(panic_issue_flush, &flush_count);
		pr_emerg("Up to now, total %d panic_issue_flush_count\n", flush_count);
		atomic_inc(&pfc->flush_issued);
		atomic_dec(&pfc->flush_issuing);
	} else if (atomic_read(&pfc->flush_circled) > 0) {
		iterate_supers(panic_issue_flush, &flush_count_circle);
		pr_emerg("Up to now, total %d panic_issue_flush_count_circled\n", flush_count_circle);
	}

	goto repeat;
}

int panic_flush_device_cache(int timeout)
{
	pr_emerg("%s\n", __func__);
	if (!pfc) {
		pr_emerg("%s: skip flush device cache\n", __func__);
		return timeout;
	}

	if (atomic_inc_return(&pfc->flush_issuing) == 1 &&
		waitqueue_active(&pfc->flush_wq)) {
		pr_emerg("%s: flush device cache\n", __func__);
		atomic_set(&pfc->flush_issued, 0);
		wake_up(&pfc->flush_wq);
		while (timeout > 0 && atomic_read(&pfc->flush_issued) == 0) {
			mdelay(PANIC_FLUSH_POLL_MS);
			timeout -= PANIC_FLUSH_POLL_MS;
		}
		pr_emerg("%s: remaining timeout = %d\n", __func__, timeout);
	}
	return timeout;
}
EXPORT_SYMBOL(panic_flush_device_cache);

void panic_flush_device_cache_circled_on(void)
{
	pr_emerg("%s\n", __func__);
	if (!pfc) {
		pr_emerg("%s: skip flush device cache\n", __func__);
		return;
	}

	if (atomic_inc_return(&pfc->flush_circled) == 1 &&
		waitqueue_active(&pfc->flush_wq)) {
		pr_emerg("%s: flush device cache circle on\n", __func__);
		wake_up(&pfc->flush_wq);
	}
}
EXPORT_SYMBOL(panic_flush_device_cache_circled_on);

void panic_flush_device_cache_circled_off(void)
{
	pr_emerg("%s\n", __func__);
	if (!pfc) {
		pr_emerg("%s: skip flush device cache\n", __func__);
		return;
	}
	atomic_set(&pfc->flush_circled, 0);
	pr_emerg("%s: flush device cache circle off\n", __func__);
}
EXPORT_SYMBOL(panic_flush_device_cache_circled_off);

static int __init create_panic_flush_control(void)
{
	int err = 0;
	pr_debug("%s\n", __func__);
	pfc = kzalloc(sizeof(*pfc), GFP_KERNEL);
	if (!pfc) {
		pr_err("%s: fail to allocate memory\n", __func__);
		return -ENOMEM;
	}

	init_waitqueue_head(&pfc->flush_wq);
	atomic_set(&pfc->flush_issuing, 0);
	atomic_set(&pfc->flush_issued, 0);
	atomic_set(&pfc->flush_circled, 0);
	pfc->flush_thread = kthread_run(panic_flush_thread, pfc, "panic_flush");
	if (IS_ERR(pfc->flush_thread)) {
		err = PTR_ERR(pfc->flush_thread);
		kfree(pfc);
		pfc = NULL;
	}
	return err;
}

static void __exit destroy_panic_flush_control(void)
{
	pr_debug("%s\n", __func__);
	if (pfc && pfc->flush_thread) {
		pr_debug("%s: stop panic_flush thread\n", __func__);
		kthread_stop(pfc->flush_thread);
		kfree(pfc);
		pfc = NULL;
	}
}
module_init(create_panic_flush_control);
module_exit(destroy_panic_flush_control);
MODULE_DESCRIPTION("ONEPLUS panic flush control");
MODULE_LICENSE("GPL v2");

