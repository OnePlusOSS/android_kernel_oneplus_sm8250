/*
 * oem_force_dump.c
 *
 * drivers supporting debug functions for Oneplus device.
 *
 * hefaxi@filesystems, 2015/07/03.
 */
#include <linux/reboot.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/types.h>
#include <net/sock.h>
#include <net/netlink.h>
#include <linux/string.h>
#include <linux/task_work.h>
#include <linux/oem/oem_force_dump.h>
#include <linux/sched/signal.h>
#include <linux/syscalls.h>
#include <linux/sched/debug.h>
#include <linux/cred.h>
#include "../../../kernel/sched/sched.h"

struct sock *nl_sk;
static int fd = -1;
static struct workqueue_struct *smg_workwq;
static struct work_struct smg_work;

#define MAX_MSGSIZE 1024
#define SIGNAL_DEBUGGER	(SIGRTMIN + 3)
static int message_state = -1;
static int selinux_switch;
enum key_stat_item pwr_status, vol_up_status;

static bool find_task_by_name(struct task_struct *t, char *name)
{
	const struct cred *tcred = __task_cred(t);

	if (!strncmp(t->comm, name, TASK_COMM_LEN))
		return true;
	if (!strncmp(t->comm, "Binder:", 7) && (t->group_leader->pid == t->pid)
			&& (tcred->uid.val == 1000) && (t->parent != 0 && !strcmp(t->parent->comm, "main")))
		return true;

	return false;
}

void send_sig_to_get_trace(char *name)
{
	struct task_struct *g, *t;

	rcu_read_lock();
	for_each_process_thread(g, t) {
		if (find_task_by_name(t, name)) {
			do_send_sig_info(SIGQUIT, SEND_SIG_FORCED, t, PIDTYPE_TGID);
			msleep(500);
			goto out;
		}
	}
out:
	rcu_read_unlock();
}


void send_sig_to_get_tombstone(char *name)
{
	struct task_struct *p;

	rcu_read_lock();
	for_each_process(p) {
		if (!strncmp(p->comm, name, TASK_COMM_LEN)) {
			do_send_sig_info(SIGNAL_DEBUGGER, SEND_SIG_FORCED, p, PIDTYPE_TGID);
			msleep(500);
			break;
		}
	}
	rcu_read_unlock();
}

void get_init_sched_info(void)
{
	struct task_struct *p, *t;

	for_each_process(p) {
		if (p->pid == 1)
			break;
	}

	for_each_thread(p, t)
		sched_show_task(t);

}

static void dump_task_info(char *status, struct task_struct *p,
				bool dump_sched_info, bool dump_call_stack)
{
	if (p) {
		pr_info("%s: %s(pid: %d)\n", status, p->comm, p->pid);
#ifdef CONFIG_SCHED_INFO
		if (dump_sched_info)
			pr_info("Exec_Started_at: %llu nsec, Last_Queued_at: %llu nsec, Prio: %d Preempt_count: %#x\n",
			p->sched_info.last_arrival,
			p->sched_info.last_queued,
			p->prio, p->thread_info.preempt_count);
#else
		if (dump_sched_info)
			pr_info(" vrun: %lu arr: %lu sum_ex: %lu\n",
					   (unsigned long)p->se.vruntime,
					   (unsigned long)p->se.exec_start,
					   (unsigned long)p->se.sum_exec_runtime);

#endif
		if (dump_call_stack)
			sched_show_task(p);
	} else
		pr_info("%s: None\n", status);
}

static void dump_rq(struct rq *rq)
{
	dump_task_info("curr", rq->curr, false, false);
	dump_task_info("idle", rq->idle, false, false);
	dump_task_info("stop", rq->stop, false, false);
}

static void dump_cfs_rq(struct cfs_rq *cfs_rq);

static void dump_cgroup_state(char *status, struct sched_entity *se_p)
{
	struct task_struct *task;
	struct cfs_rq *my_q = NULL;
	unsigned int nr_running;

	if (!se_p) {
		dump_task_info(status, NULL, false, false);
		return;
	}
#ifdef CONFIG_FAIR_GROUP_SCHED
	my_q = se_p->my_q;
#endif
	if (!my_q) {
		task = container_of(se_p, struct task_struct, se);
		dump_task_info(status, task, true, true);
		return;
	}
	nr_running = my_q->nr_running;
	pr_info("%s: %d process is grouping\n",
				   status, nr_running);
	dump_cfs_rq(my_q);
}

static void dump_cfs_node_func(struct rb_node *node)
{
	struct sched_entity *se_p = container_of(node, struct sched_entity,
						 run_node);

	dump_cgroup_state("pend", se_p);
}

static void dump_rb_walk_cfs(struct rb_root_cached *rb_root_cached_p)
{
	int max_walk = 200;
	struct rb_node *leftmost = rb_root_cached_p->rb_leftmost;
	struct rb_root *root = &rb_root_cached_p->rb_root;
	struct rb_node *rb_node = rb_first(root);

	if (!leftmost)
		return;
	while (rb_node && max_walk--) {
		dump_cfs_node_func(rb_node);
		rb_node = rb_next(rb_node);
	}
}

static void dump_rt_rq(struct rt_rq *rt_rq)
{
	struct rt_prio_array *array = &rt_rq->active;
	struct sched_rt_entity *rt_se;
	int idx;

	pr_info("RT %d process is pending\n", rt_rq->rt_nr_running);

	if (bitmap_empty(array->bitmap, MAX_RT_PRIO))
		return;

	idx = sched_find_first_bit(array->bitmap);
	while (idx < MAX_RT_PRIO) {
		list_for_each_entry(rt_se, array->queue + idx, run_list) {
			struct task_struct *p;

#ifdef CONFIG_RT_GROUP_SCHED
			if (rt_se->my_q)
				continue;
#endif

			p = container_of(rt_se, struct task_struct, rt);
			dump_task_info("pend", p, true, true);
		}
		idx = find_next_bit(array->bitmap, MAX_RT_PRIO, idx + 1);
	}
}

static void dump_cfs_rq(struct cfs_rq *cfs_rq)
{
	struct rb_root_cached *rb_root_cached_p = &cfs_rq->tasks_timeline;

	pr_info("CFS %d process is pending\n", cfs_rq->nr_running);

	dump_cgroup_state("curr", cfs_rq->curr);
	dump_cgroup_state("next", cfs_rq->next);
	dump_cgroup_state("last", cfs_rq->last);
	dump_cgroup_state("skip", cfs_rq->skip);
	dump_rb_walk_cfs(rb_root_cached_p);
}

void dump_runqueue(void)
{
	int cpu;
	struct rq *rq;
	struct rt_rq  *rt;
	struct cfs_rq *cfs;

	pr_info("==================== RUNQUEUE STATE ====================\n");
	for_each_possible_cpu(cpu) {
		rq = cpu_rq(cpu);
		rt = &rq->rt;
		cfs = &rq->cfs;
		pr_info("CPU%d %d process is running\n", cpu, rq->nr_running);
		dump_rq(rq);
		dump_cfs_rq(cfs);
		dump_rt_rq(rt);
	}
}

void compound_key_to_get_trace(char *name)
{
	if (pwr_status == KEY_PRESSED && vol_up_status == KEY_PRESSED)
		send_sig_to_get_trace(name);
}

void compound_key_to_get_tombstone(char *name)
{
	if (pwr_status == KEY_PRESSED && vol_up_status == KEY_PRESSED)
		send_sig_to_get_tombstone(name);
}


/*
 * the way goto force dump:
 * 1. press the voluemup key and then relase it.
 * 2. press the volumedown key and then relase it.
 * 3. long press volumeup key, without release it.
 * 4. press twice power key, and release it.
 * 5. release the volumeup key.
 * 6. presss the volumeup key, without release it.
 * 7. press the power key.
 * after those step, the device will goto the force dump.
 */
void oem_check_force_dump_key(unsigned int code, int value)
{
	static enum { NONE, STEP1, STEP2, STEP3, STEP4, STEP5,
	STEP6, STEP7, STEP8, STEP9, STEP10, STEP11, STEP_DEBUG1} state = NONE;

	switch (state) {
	case NONE:
		if (code == KEY_VOLUMEUP && value)
			state = STEP1;
		else
			state = NONE;
		break;
	case STEP1:
		if (code == KEY_VOLUMEUP && !value)
			state = STEP2;
		else
			state = NONE;
		break;
	case STEP2:
		if (code == KEY_VOLUMEDOWN && value)
			state = STEP3;
		else
			state = NONE;
		break;
	case STEP3:
		if (code == KEY_VOLUMEDOWN && !value)
			state = STEP4;
		else
			state = NONE;
		break;
	case STEP4:
		if (code == KEY_VOLUMEUP && value)
			state = STEP5;
		else
			state = NONE;
		break;
	case STEP5:
		if (code == KEY_POWER && value)
			state = STEP6;
		else
			state = NONE;
		break;
	case STEP6:
		if (code == KEY_POWER && !value)
			state = STEP7;
		else
			state = NONE;
		break;
	case STEP7:
		if (code == KEY_POWER && value)
			state = STEP8;
		else
			state = NONE;
		break;
	case STEP8:
		if (code == KEY_POWER && !value)
			state = STEP9;
		else
			state = NONE;
		break;
	case STEP9:
		if (code == KEY_VOLUMEUP && !value)
			state = STEP10;
		else
			state = NONE;
		break;
	case STEP10:
		if (code == KEY_VOLUMEUP && value)
			state = STEP11;
		else if (code == KEY_VOLUMEDOWN && value)
			state = STEP_DEBUG1;
		else
			state = NONE;
		break;
	case STEP11:
		if (code == KEY_POWER && value) {
			if (oem_get_download_mode())
				panic("Force Dump");
		} else
			state = NONE;
		break;

	case STEP_DEBUG1:
		if (code == KEY_POWER && value) {
			set_oem_selinux_state(1);
			message_state = 1;
			queue_work(smg_workwq, &smg_work);
			state = NONE;
		} else if (code == KEY_VOLUMEDOWN && !value) {
			message_state = 2;
			queue_work(smg_workwq, &smg_work);
			state = NONE;
		} else
			state = NONE;
		break;
	}
}
int  set_oem_selinux_state(int state)
{
	selinux_switch = state;
	return 0;
}
int get_oem_selinux_state(void)
{
	return selinux_switch;
}

static void send_msg_worker(struct work_struct *work)
{
	if (message_state == 1)
		send_msg("Enable DEBUG!");
	else if (message_state == 2) {
		pr_info("force oem serial\n");
		msm_serial_oem_init();
		send_msg("ENABLE_OEM_FORCE_SERIAL");
	}
	message_state = 0;
}

void send_msg_sync_mdm_dump(void)
{
	send_msg("FORCE_MDM_DUMP_SYNC");
}

void send_msg(char *message)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	int len = NLMSG_SPACE(MAX_MSGSIZE);

	pr_info("%s,%s\n",__func__,message);

	if (!message || !nl_sk)
		return;

	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb)
		return;
	nlh = nlmsg_put(skb, 0, 0, 0, MAX_MSGSIZE, 0);
	NETLINK_CB(skb).portid = 0;
	NETLINK_CB(skb).dst_group = 0;
	strlcpy(NLMSG_DATA(nlh), message, MAX_MSGSIZE);
	netlink_unicast(nl_sk, skb, fd, MSG_DONTWAIT);
}

void recv_nlmsg(struct sk_buff *skb)
{
	struct nlmsghdr *nlh = nlmsg_hdr(skb);

	if (nlh->nlmsg_len < NLMSG_HDRLEN || skb->len < nlh->nlmsg_len)
		return;
	fd = nlh->nlmsg_pid;
	pr_err("received:%s %d\n", (char *)NLMSG_DATA(nlh), fd);
}

struct netlink_kernel_cfg nl_kernel_cfg = {
	.groups = 0,
	.flags = 0,
	.input = recv_nlmsg,
	.cb_mutex = NULL,
	.bind = NULL,
	.compare = NULL,
};

int op_netlink_init(void)
{
	nl_sk = netlink_kernel_create(&init_net, NETLINK_ADB, &nl_kernel_cfg);
	if (!nl_sk) {
		pr_err("%s: Create netlink socket error.\n", __func__);
		return 1;
	}
	smg_workwq = create_singlethread_workqueue("oem_key_dump");
	if (!smg_workwq) {
		pr_err("%s: Create oem_key_dump error.\n", __func__);
		return 1;
	}
	INIT_WORK(&smg_work, send_msg_worker);
	pr_err("%s\n", __func__);
	return 0;
}

static void op_netlink_exit(void)
{
	if (nl_sk != NULL)
		sock_release(nl_sk->sk_socket);
	if (smg_workwq != NULL)
		destroy_workqueue(smg_workwq);
	pr_err("%s\n", __func__);
}

module_init(op_netlink_init);
module_exit(op_netlink_exit);
MODULE_LICENSE("GPL v2");
