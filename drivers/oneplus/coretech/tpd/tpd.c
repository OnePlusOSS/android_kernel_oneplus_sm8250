#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/oem/tpd.h>
#include <linux/cred.h>
#include <linux/oem/im.h>

/*
 * Task Placement Decision
 *
 * two main rules as below:
 *  1. trigger tpd with tgid and thread's name which will be limited
 *     through online config, framework can tag threads and limit cpu placement
 *     of tagged threads that are also foreground task.
 *     tpd_cmds
 *  2. trigger tpd with tgid and thread_id
 *     control the placement limitation by itself, set tpd_ctl = 1 will limit
 *     cpu placement of tagged threads, but need release by itself to set tpd_enable = 0,
 *     tpd_ctl = 0 and task->tpd = 0
 *     tpd_id
 *  3. control the placement limitation by itself, set tpd_ctl = 1 will limit
 *     cpu placement of tagged threads, but need release by itself to set tpd_enable = 0,
 *     tpd_ctl = 0 and task->tpd = 0
 *     tpd_dynamic
 */

struct monitor_gp {
	int tgid;
	int decision;
};

static struct monitor_gp mgp[TPD_GROUP_MAX];

static atomic_t tpd_enable_rc = ATOMIC_INIT(0);
static int tpd_enable_rc_show(char *buf, const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&tpd_enable_rc));
}

static struct kernel_param_ops tpd_enable_rc_ops = {
	.get = tpd_enable_rc_show,
};

module_param_cb(tpd_en_rc, &tpd_enable_rc_ops, NULL, 0664);

static int tpd_log_lv = 2;
module_param_named(log_lv, tpd_log_lv, int, 0664);

static bool should_update_tpd_enable(int enable) {

	bool ret = true;

	if (enable) {
		if (atomic_read(&tpd_enable_rc) > 0) {
			ret = false;
		}
		atomic_inc(&tpd_enable_rc);
	} else {
		atomic_dec(&tpd_enable_rc);
		if (atomic_read(&tpd_enable_rc) > 0) {
			tpd_loge("tpd can't disable");
			ret = false;
		}
	}

	tpd_logi("should_update_tpd_enable? %d", ret);

	return ret;
}

static int tpd_enable = 0;
static int tpd_enable_store(const char *buf, const struct kernel_param *kp)
{
	int val;

	if (sscanf(buf, "%d\n", &val) <= 0)
		return 0;

	if (should_update_tpd_enable(val))
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

bool is_dynamic_tpd_task(struct task_struct *tsk)
{
	int i;
	struct task_struct *leader;
	struct monitor_gp *group;

	for (i = TPD_GROUP_MEDIAPROVIDER; i < TPD_GROUP_MAX; ++i) {

		group = &mgp[i];

		if (group->tgid == -2)
			continue;

		switch (i) {
		case TPD_GROUP_MEDIAPROVIDER:

			leader = tsk ? tsk->group_leader : NULL;

			if (leader == NULL)
				return false;

			if (group->tgid == -1) {
				if (tsk->dtpd) {
					tsk->tpd = 0;
					tsk->dtpd = 0;
				}
				return false;
			}

#ifdef CONFIG_IM
			/*binder thread of media provider */
			if (leader->pid == group->tgid && im_binder(tsk)) {
				tsk->dtpd = 1; /* dynamic tpd */
				tsk->tpd = group->decision;
				return true;
			}
#endif

#ifdef CONFIG_ONEPLUS_FG_OPT
			/* fuse related thread of media provider */
			if (tsk->fuse_boost) {
				tsk->dtpd = 1; /* dynamic tpd */
				tsk->tpd = group->decision;
				return true;
			}
#endif

			break;
		default:
			break;
		}
	}

	return false;
}

static int tpd_ctl = 0; /*used to ignore fg task checking*/

static void set_tpd_ctl(int force)
{
	tpd_ctl = force;
}

static int tpd_ctl_store(const char *buf, const struct kernel_param *kp)
{
	int val;

	if (sscanf(buf, "%d\n", &val) <= 0)
		return 0;

	set_tpd_ctl(val);

	return 0;
}

static int tpd_ctl_show(char *buf, const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", tpd_ctl);
}

static struct kernel_param_ops tpd_ctl_ops = {
	.set = tpd_ctl_store,
	.get = tpd_ctl_show,
};

module_param_cb(tpd_ctl, &tpd_ctl_ops, NULL, 0664);

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
	int tpdenable = 0;
	int tp_decision = -1;
	int ret;

	ret = sscanf(buf, "%u,%u,%d,%d\n",
		&pid, &tid, &tpdenable, &tp_decision);

	tpd_logi("tpd param pid:%u tid:%u, tpd_enable:%d decision:%d from %s %d\n",
		pid, tid, tpdenable, tp_decision, current->comm, current->pid);

	if (ret != 4) {
		tpd_loge("Invalid params!!!!!!");
		return 0;
	}

	tag_from_tid(pid, tid, tpdenable ? tp_decision : 0);

	set_tpd_ctl(tpdenable);

	/* update tpd_enable ref cnt*/
	if (should_update_tpd_enable(tpdenable))
		tpd_enable = tpdenable;

	return 0;
}

static struct kernel_param_ops tpd_ops = {
	.set = tpd_store,
};
module_param_cb(tpd_id, &tpd_ops, NULL, 0664);

#define MONITOR_THREAD_NUM 1
static int tpd_process_trigger_store(const char *buf, const struct kernel_param *kp)
{
	unsigned int tgid = 0;
	int tpdenable = 0;
	int tp_decision = -1;
	int tpd_group = -1;
	int ret, i, cnt=0;
	char *threads[MONITOR_THREAD_NUM] = {"bg"};

	ret = sscanf(buf, "%d,%u,%d,%d\n",
		&tpd_group, &tgid, &tpdenable, &tp_decision);

	tpd_logi("tpd param group:%d pid:%u tpd_enable:%d decision:%d from %s %d\n",
		tpd_group, tgid, tpdenable, tp_decision, current->comm, current->pid);

	if (ret != 4) {
		tpd_loge("Invalid params!!!!!!");
		return 0;
	}

	if (tpd_group >= TPD_GROUP_MAX || tpd_group < 0) {
		tpd_loge("Invalid group!!!!!!");
		return 0;
	}

	if (!tpdenable) {
		mgp[tpd_group].tgid = -1;
		mgp[tpd_group].decision = 0;
	} else {
		mgp[tpd_group].tgid = tgid;
		mgp[tpd_group].decision = tp_decision;
	}

	set_tpd_ctl(tpdenable);

	for(i = 0; i < MONITOR_THREAD_NUM; i++)
		tag_from_tgid(tgid, tpdenable ? tp_decision : 0, threads[i], &cnt);

	tpd_logi("tagging count = %d, tpd enable set:%d", 2, tpdenable);

	/* update tpd_enable ref cnt*/
	if (should_update_tpd_enable(tpdenable))
		tpd_enable = tpdenable;

	return 0;
}

static struct kernel_param_ops tpd_pt_ops = {
	.set = tpd_process_trigger_store,
};
module_param_cb(tpd_dynamic, &tpd_pt_ops, NULL, 0664);

int tpd_suggested(struct task_struct* tsk, int min_idx, int mid_idx, int max_idx, int request_cpu)
{
	int suggest_cpu = request_cpu;

	if (!(task_is_fg(tsk) || tpd_ctl))
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
	tpd_logi("pid = %d: comm = %s, tpd = %d, suggest_cpu = %d, task is fg? %d, tpd_ctl = %d\n", tsk->pid, tsk->comm,
		tsk->tpd, suggest_cpu, task_is_fg(tsk), tpd_ctl);
	return suggest_cpu;
}

void tpd_mask(struct task_struct* tsk, int min_idx, int mid_idx, int max_idx, cpumask_t *request, int nrcpu)
{
	int start_idx = nrcpu, end_idx = -1, i, next_start_idx = nrcpu;
	bool second_round = false;

	if (!(task_is_fg(tsk) || tpd_ctl)) {
		if (!task_is_fg(tsk))
			tpd_logi("task is not fg!!!\n");
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

	tpd_logi("pid = %d: comm = %s, tpd = %d, min_idx = %d, mid_idx = %d, max_idx = %d, task is fg? %d, tpd_ctl = %d\n", tsk->pid, tsk->comm,
		tsk->tpd, min_idx, mid_idx, max_idx, task_is_fg(tsk), tpd_ctl);
}

bool tpd_check(struct task_struct *tsk, int dest_cpu, int min_idx, int mid_idx, int max_idx)
{
	bool mismatch = false;

	if (!(task_is_fg(tsk) || tpd_ctl))
		goto out;

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

static void tpd_mgp_init()
{
	int i;

	for (i = 0; i < TPD_GROUP_MAX; ++i) {
		mgp[i].tgid = -2;
		mgp[i].decision = 0;
	}
}

static int tpd_init(void)
{
        tpd_mgp_init();
        tpd_logi("tpd init\n");
        return 0;
}

pure_initcall(tpd_init);
