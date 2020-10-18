/***************************************************************
** File : hung_task_enhance.c
** Description : detect hung task in D state
** Version : 1.0
** Date : 2020/08/18
******************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cred.h>
#include <linux/nmi.h>
#include <linux/utsname.h>
#include <trace/events/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/signal.h>

#include <linux/oem/hung_task_enhance.h>
#include <linux/oem/oem_force_dump.h>

/*
 * format: task_name,reason. e.g. system_server,uninterruptible for 60 secs
 */
#define HUNG_TASK_KILL_LEN	128
char __read_mostly sysctl_hung_task_kill[HUNG_TASK_KILL_LEN];
#define TWICE_DEATH_PERIOD	300000000000ULL	 /* 300s */
#define MAX_DEATH_COUNT	3
#define DISP_TASK_COMM_LEN_MASK 10

/* Foreground background optimization,change max io count */
#define MAX_IO_WAIT_HUNG 5
int __read_mostly sysctl_hung_task_maxiowait_count = MAX_IO_WAIT_HUNG;

/* key process:zygote system_server surfaceflinger */
static bool is_userspace_key_process(struct task_struct *t)
{
	const struct cred *tcred = __task_cred(t);

	if(!strcmp(t->comm, "main") && (tcred->uid.val == 0) &&
			(t->parent != 0 && !strcmp(t->parent->comm, "init")))
		return true;
	if(!strncmp(t->comm, "system_server", TASK_COMM_LEN) ||
			!strncmp(t->comm, "surfaceflinger", TASK_COMM_LEN))
		return true;
	if (!strncmp(t->comm, "Binder:", 7) && (t->group_leader->pid == t->pid)
			&& (tcred->uid.val == 1000) && (t->parent != 0 && !strcmp(t->parent->comm, "main")))
		return true;

	return false;
}

static void oplus_check_hung_task(struct task_struct *t, unsigned long timeout,
				unsigned int *iowait_count, bool *show_lock, bool *call_panic)
{
	unsigned long switch_count = t->nvcsw + t->nivcsw;
	static unsigned long long last_death_time = 0;
	unsigned long long cur_death_time = 0;
	static int death_count = 0;
	unsigned int local_iowait = 0;

	/*
	 * Ensure the task is not frozen.
	 * Also, skip vfork and any other user process that freezer should skip.
	 */
	if (unlikely(t->flags & (PF_FROZEN | PF_FREEZER_SKIP)))
		return;

	/*
	 * When a freshly created task is scheduled once, changes its state to
	 * TASK_UNINTERRUPTIBLE without having ever been switched out once, it
	 * musn't be checked.
	 */
	if (unlikely(!switch_count))
		return;

	if (switch_count != t->last_switch_count) {
		t->last_switch_count = switch_count;
		t->last_switch_time = jiffies;
		return;
	}
	if (time_is_after_jiffies(t->last_switch_time + timeout * HZ))
		return;

	trace_sched_process_hang(t);

	/* kill D/T/t state tasks ,if this task blocked at iowait. so maybe we should reboot system first */
	if(t->in_iowait) {
		printk("hung_task_enhance: io wait too long time\n");
        if(t->mm != NULL && t == t->group_leader) { // only work on user main thread
			*iowait_count = *iowait_count + 1;
			local_iowait = 1;
        }
	}
	if (is_userspace_key_process(t)) {
		if (t->state == TASK_UNINTERRUPTIBLE)
			snprintf(sysctl_hung_task_kill, HUNG_TASK_KILL_LEN,
				"%s,uninterruptible for %ld seconds",
				t->comm, timeout);
		else if (t->state == TASK_STOPPED)
			snprintf(sysctl_hung_task_kill, HUNG_TASK_KILL_LEN,
				"%s,stopped for %ld seconds",
				t->comm, timeout);
		else if (t->state == TASK_TRACED)
			snprintf(sysctl_hung_task_kill, HUNG_TASK_KILL_LEN,
				"%s,traced for %ld seconds",
				t->comm, timeout);
		else
			snprintf(sysctl_hung_task_kill, HUNG_TASK_KILL_LEN,
				"%s,unknown hung for %ld seconds",
				t->comm, timeout);

		death_count++;
		printk("hung_task_enhance: task %s:%d blocked for more than %ld seconds in state 0x%lx. Count:%d\n",
			t->comm, t->pid, timeout, t->state, death_count);

		sched_show_task(t);
		debug_show_held_locks(t);
		trigger_all_cpu_backtrace();

		cur_death_time = local_clock();
		if (death_count >= MAX_DEATH_COUNT) {
			if (cur_death_time - last_death_time < TWICE_DEATH_PERIOD) {
				printk("hung_task_enhance: has been triggered %d times, \
					last time at: %llu\n", death_count, last_death_time);
				panic("hung_task_enhance: key process recovery has been triggered more than 3 times");
			}
		}
		last_death_time = cur_death_time;

		if (oem_get_download_mode())
			panic("hung_task_enhance: %s hung in D state", t->comm);

		do_send_sig_info(SIGKILL, SEND_SIG_FORCED, t, PIDTYPE_TGID);
		wake_up_process(t);
	}

	if (sysctl_hung_task_panic) {
		console_verbose();
		*show_lock = true;
		*call_panic = true;

		/* Panic on critical process D-state */
		if (is_userspace_key_process(t)) {
			trigger_all_cpu_backtrace();
			panic("hung_task_enhance: blocked tasks");
		}
	}

	/*
	 * Ok, the task did not get scheduled for more than 2 minutes,
	 * complain:
	 */

    /* Modify for make sure we could print the stack of iowait thread before panic */
	if (local_iowait) {
		pr_err("INFO: task %s:%d blocked for more than %ld seconds.\n",
			t->comm, t->pid, timeout);
		pr_err("      %s %s %.*s\n",
			print_tainted(), init_utsname()->release,
			(int)strcspn(init_utsname()->version, " "),
			init_utsname()->version);
		pr_err("\"echo 0 > /proc/sys/kernel/hung_task_timeout_secs\""
			" disables this message.\n");
		sched_show_task(t);
		*show_lock = true;
	}
	touch_nmi_watchdog();
}

void io_check_hung_detection(struct task_struct *t, unsigned long timeout,
				unsigned int *iowait_count, bool *show_lock, bool *call_panic)
{
	/* add io wait monitor */
	if (t->state == TASK_UNINTERRUPTIBLE || t->state == TASK_STOPPED || t->state == TASK_TRACED)
		/* Check for selective monitoring */
		if (!sysctl_hung_task_selective_monitoring ||
			t->hang_detection_enabled)
			oplus_check_hung_task(t, timeout, iowait_count, show_lock, call_panic);
}
EXPORT_SYMBOL(io_check_hung_detection);

void io_block_panic(unsigned int *iowait_count, unsigned int sys_mamxiowait_count)
{
/* Foreground background optimization,change max io count */
	if(*iowait_count >= sysctl_hung_task_maxiowait_count)
		panic("hung_task_enhance: [%u]IO blocked too long time", *iowait_count);
}
EXPORT_SYMBOL(io_block_panic);
