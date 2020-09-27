#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/oem/tpd.h>
#include <linux/cred.h>

/*
 * Task Placement Decision
 */

static int tpd_log_lv = 2;
module_param_named(log_lv, tpd_log_lv, int, 0664);

static int tpd_enable = 0;
static int tpd_enable_store(const char *buf, const struct kernel_param *kp)
{
	int val;

	if (sscanf(buf, "%d\n", &val) <= 0)
		return 0;

	tpd_enable = val;

	return 0;
}

static int tpd_enable_show(char *buf, const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", tpd_enable);
}

static struct kernel_param_ops tpd_enable_ops = {
	.set = tpd_enable_store,
	.get = tpd_enable_show,
};

module_param_cb(tpd_enable, &tpd_enable_ops, NULL, 0664);

bool is_tpd_enable(void)
{
	return tpd_enable;
}

static inline void tagging(struct task_struct *tsk, int decision)
{
	if (tsk == NULL) {
		tpd_loge("task cannot set");
		return;
	}

	tpd_logv("%s task: %s pid:%d decision:%d\n", __func__, tsk->comm, tsk->pid, decision);

	tsk->tpd = decision;
}

static inline void tagging_by_name(struct task_struct *tsk, char* name, int decision, int *cnt)
{
	size_t tlen = 0, len = 0;

	tlen = strlen(name);
	if (tlen == 0)
		return;

	len = strlen(tsk->comm);

	if (len != tlen)
		return;

	if (!strncmp(tsk->comm, name, tlen)) {
		tpd_logi("%s task: %s pid:%d decision:%d name=%s\n", __func__, tsk->comm, tsk->pid, decision, name);
		tsk->tpd = decision;
		*cnt = *cnt + 1;
	}
}

static void tag_from_tgid(unsigned tgid, int decision, char* thread_name, int *cnt)
{
	struct task_struct *p, *t;

	rcu_read_lock();
	p = find_task_by_vpid(tgid);
	if (p) {
		for_each_thread(p, t)
			tagging_by_name(t, thread_name, decision, cnt);
	}
	rcu_read_unlock();

}

static int tpd_cmd_store(const char *buf, const struct kernel_param *kp)
{
	unsigned int tgid = 0;
	int tp_decision = -1;
	char threads[MAX_THREAD_INPUT][TASK_COMM_LEN] = {{0}, {0}, {0}, {0}, {0}, {0}};
	int ret, i, cnt = 0;

	ret = sscanf(buf, "%u %d %s %s %s %s %s %s\n",
		&tgid, &tp_decision,
		threads[0], threads[1], threads[2], threads[3], threads[4], threads[5]);

        tpd_logi("tpd params: %u %d %s %s %s %s %s %s, from %s %d, total=%d\n",
              tgid, tp_decision, threads[0], threads[1], threads[2], threads[3], threads[4], threads[5],
              current->comm, current->pid, ret);

	for (i = 0; i < MAX_THREAD_INPUT; i++) {
		if (strlen(threads[i]) > 0)
			tag_from_tgid(tgid, tp_decision, threads[i], &cnt);
	}

	tpd_logv("tpd tagging count = %d\n", cnt);

	return 0;
}

static struct kernel_param_ops tpd_cmd_ops = {
	.set = tpd_cmd_store,
};
module_param_cb(tpd_cmds, &tpd_cmd_ops, NULL, 0664);

static void tag_from_tid(unsigned int pid, unsigned int tid, int decision)
{
	struct task_struct *p;

	rcu_read_lock();
	p = find_task_by_vpid(tid);
	if (p) {
		if (p->group_leader && (p->group_leader->pid == pid)) {
			tpd_logi("tpd tagging task pid= %d\n", pid);
			tagging(p, decision);
		}
	} else {
		tpd_loge("cannot find task!!! pid = %d", tid);
	}
	rcu_read_unlock();
}

static int tpd_store(const char *buf, const struct kernel_param *kp)
{
	unsigned int pid = 0;
	unsigned int tid = 0;
	int tp_decision = -1;
	int ret;

	ret = sscanf(buf, "%u,%u,%d\n",
		&pid, &tid, &tp_decision);

	tpd_logi("tpd param pid:%u tid:%u, decision:%d from %s %d\n",
		pid, tid, tp_decision, current->comm, current->pid);

	if (ret != 3) {
		tpd_loge("Invalid params!!!!!!");
		return 0;
	}

	tag_from_tid(pid, tid, tp_decision);

	return 0;
}

static struct kernel_param_ops tpd_ops = {
	.set = tpd_store,
};
module_param_cb(tpd_id, &tpd_ops, NULL, 0664);

int tpd_suggested(struct task_struct* tsk, int min_idx, int mid_idx, int max_idx, int request_cpu)
{
	int suggest_cpu = request_cpu;

	if (!task_is_fg(tsk))
		goto out;

	switch (tsk->tpd) {
	case TPD_TYPE_S:
	case TPD_TYPE_GS:
	case TPD_TYPE_PS:
	case TPD_TYPE_PGS:
		suggest_cpu = min_idx;
		break;
	case TPD_TYPE_G:
	case TPD_TYPE_PG:
		suggest_cpu = mid_idx;
		break;
	case TPD_TYPE_P:
		suggest_cpu = max_idx;
		break;
	default:
		break;
	}
out:
	tpd_logi("pid = %d: comm = %s, tpd = %d, suggest_cpu = %d, task is fg? %d\n", tsk->pid, tsk->comm,
		tsk->tpd, suggest_cpu, task_is_fg(tsk));
	return suggest_cpu;
}

void tpd_mask(struct task_struct* tsk, int min_idx, int mid_idx, int max_idx, cpumask_t *request, int nrcpu)
{
	int start_idx = nrcpu, end_idx = -1, i, next_start_idx = nrcpu;
	bool second_round = false;

	if (!task_is_fg(tsk)) {
		tpd_loge("task is not fg!!!\n");
		return;
	}

	switch (tsk->tpd) {
	case TPD_TYPE_S:
		start_idx = mid_idx;
		break;
	case TPD_TYPE_G:
		start_idx = min_idx;
		end_idx = mid_idx;
		second_round = true;
		next_start_idx = max_idx;
		break;
	case TPD_TYPE_GS:
		start_idx = max_idx;
		break;
	case TPD_TYPE_P:
		start_idx = min_idx;
		end_idx = max_idx;
		break;
	case TPD_TYPE_PS:
		start_idx = mid_idx;
		end_idx = max_idx;
		break;
	case TPD_TYPE_PG:
		start_idx = min_idx;
		end_idx = mid_idx;
		break;
	default:
		break;
	}

redo:
	for (i = start_idx; i < nrcpu; ++i) {

		if (i == end_idx)
			break;

		tpd_logv("task: %d, cpu clear bit = %d\n", (tsk) ? tsk->pid : -1, i);

		cpumask_clear_cpu(i, request);
	}

	if (second_round) {
		start_idx = next_start_idx;
		second_round = false;
		goto redo;
	}
}

bool tpd_check(struct task_struct *tsk, int dest_cpu, int min_idx, int mid_idx, int max_idx)
{
	bool mismatch = false;

	if (!task_is_fg(tsk)) {
		goto out;
	}

	switch (tsk->tpd) {
	case TPD_TYPE_S:
		if (dest_cpu >= mid_idx)
			mismatch = true;
		break;
	case TPD_TYPE_G:
		if ((mid_idx != max_idx) &&
				(dest_cpu < mid_idx || dest_cpu >= max_idx))
			mismatch = true;
		break;
	case TPD_TYPE_GS:
		/* if no gold plus cores, mid = max*/
		if (dest_cpu >= max_idx)
			mismatch = true;
		break;
	case TPD_TYPE_P:
		if (dest_cpu < max_idx)
			mismatch = true;
		break;
	case TPD_TYPE_PS:
		if (dest_cpu < max_idx && dest_cpu >= mid_idx)
			mismatch = true;
		break;
	case TPD_TYPE_PG:
		if (dest_cpu < mid_idx)
			mismatch = true;
		break;
	default:
		break;
	}

out:
	tpd_logi("task:%d comm:%s dst: %d should migrate = %d, task is fg? %d\n", tsk->pid, tsk->comm, dest_cpu, !mismatch, task_is_fg(tsk));

	return mismatch;
}
