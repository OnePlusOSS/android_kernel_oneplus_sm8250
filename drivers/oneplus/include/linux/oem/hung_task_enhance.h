#ifndef __HUNG_TASK_ENHANCE_H
#define __HUNG_TASK_ENHANCE_H
#include <linux/sched/clock.h>
#include <linux/signal.h>
#include <linux/sched.h>

/* format: task_name,reason. e.g. system_server,uninterruptible for 60 secs */
extern char sysctl_hung_task_kill[];
/* Foreground background optimization,change max io count */
extern int sysctl_hung_task_maxiowait_count;
static int five = 5;

#ifdef CONFIG_HUNG_TASK_ENHANCE
void io_check_hung_detection(struct task_struct *t, unsigned long timeout, unsigned int *iowait_count, bool *show_lock, bool *call_panic);
void io_block_panic(unsigned int *iowait_count, unsigned int sys_mamxiowait_count);
#else
void io_check_hung_detection(struct task_struct *t, unsigned long timeout, unsigned int *iowait_count, bool *show_lock, bool *call_panic) {}
static void io_block_panic(unsigned int *iowait_count, unsigned int sys_mamxiowait_count) {}
#endif

#endif