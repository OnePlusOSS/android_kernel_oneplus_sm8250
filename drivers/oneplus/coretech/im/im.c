#include <linux/oem/im.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include "../kernel/sched/sched.h"
#include "../kernel/sched/walt.h"

struct task_list_entry {
	int pid;
	struct list_head node;
};

static int tb_rdg_enable = 1;
static DEFINE_SPINLOCK(tb_render_group_lock);
static struct list_head task_list_head = LIST_HEAD_INIT(task_list_head);

/* default list, empty string means no need to check */
static struct im_target {
	char val[64];
	const char* desc;
} im_target [IM_ID_MAX] = {
	{"surfaceflinger", "sf "},
	{"", "kworker "},
	{"logd", "logd "},
	{"logcat", "logcat "},
	{"", "main "},
	{"", "enqueue "},
	{"", "gl "},
	{"", "vk "},
	{"composer-servic", "hwc "},
	{"HwBinder:", "hwbinder "},
	{"Binder:", "binder "},
	{"hwuiTask", "hwui "},
	{"", "render "},
	{"", "unity_wk"},
	{"UnityMain", "unityM"},
	{"neplus.launcher", "launcher "},
	{"HwuiTask", "HwuiEx "},
	{"CrRendererMain", "crender "},
};

/* ignore list, not set any im_flag */
static char target_ignore_prefix[IM_IG_MAX][64] = {
	"Prober_",
	"DispSync",
	"app",
	"sf",
	"ScreenShotThrea",
	"DPPS_THREAD",
	"LTM_THREAD",
};

void im_to_str(int flag, char* desc, int size)
{
	char *base = desc;
	int i;

	for (i = 0; i < IM_ID_MAX; ++i) {
		if (flag & (1 << i)) {
			size_t len = strlen(im_target[i].desc);

			if (len) {
				if (size <= base - desc + len) {
					pr_warn("im tag desc too long\n");
					return;
				}
				strncpy(base, im_target[i].desc, len);
				base += len;
			}
		}
	}
}

static inline bool im_ignore(struct task_struct *task, int idx)
{
	size_t tlen = 0, len = 0;

	tlen = strlen(target_ignore_prefix[idx]);
	if (tlen == 0)
		return false;

	/* NOTE: task->comm has only 16 bytes */
	len = strlen(task->comm);
	if (len < tlen)
		return false;

	if (!strncmp(task->comm, target_ignore_prefix[idx], tlen)) {
		task->im_flag = 0;
		return true;
	}
	return false;
}

static inline void im_tagging(struct task_struct *task, int idx)
{
	size_t tlen = 0, len = 0;

	tlen = strlen(im_target[idx].val);
	if (tlen == 0)
		return;

	/* NOTE: task->comm has only 16 bytes */
	len = strlen(task->comm);

	/* non restrict tagging for some prefixed tasks*/
	if (len < tlen)
		return;

	/* prefix cases */
	if (!strncmp(task->comm, im_target[idx].val, tlen)) {
		switch (idx) {
		case IM_ID_HWBINDER:
			task->im_flag |= IM_HWBINDER;
			break;
		case IM_ID_BINDER:
			task->im_flag |= IM_BINDER;
			break;
		case IM_ID_HWUI:
			task->im_flag |= IM_HWUI;
			break;
		case IM_ID_HWUI_EX:
			task->im_flag |= IM_HWUI_EX;
			break;
		}
	}

	/* restrict tagging for specific identical tasks */
	if (len != tlen)
		return;

	if (!strncmp(task->comm, im_target[idx].val, len)) {
		switch (idx) {
		case IM_ID_SURFACEFLINGER:
			task->im_flag |= IM_SURFACEFLINGER;
			break;
		case IM_ID_LOGD:
			task->im_flag |= IM_LOGD;
			break;
		case IM_ID_LOGCAT:
			task->im_flag |= IM_LOGCAT;
			break;
		case IM_ID_HWC:
			task->im_flag |= IM_HWC;
			break;
		case IM_ID_LAUNCHER:
			task->im_flag |= IM_LAUNCHER;
			break;
		case IM_ID_RENDER:
			task->im_flag |= IM_RENDER;
			break;
		case IM_ID_UNITY_MAIN:
			task->im_flag |= IM_UNITY_MAIN;
			break;
		}
	}
}

void im_wmi(struct task_struct *task)
{
	int i = 0;
	struct task_struct *leader = task->group_leader;

	/* check for ignore */
	for (i = 0; i < IM_IG_MAX; ++i)
		if (im_ignore(task, i))
			return;

	/* do the check and initial */
	task->im_flag = 0;
	for (i = 0; i < IM_ID_MAX; ++i)
		im_tagging(task, i);

	/* for hwc cases */
	if (im_hwc(leader)) {
		struct task_struct *p;
		rcu_read_lock();
		for_each_thread(task, p) {
			if (im_binder_related(p))
				p->im_flag |= IM_HWC;
		}
		rcu_read_unlock();
	}

	/* for sf cases */
	if (im_sf(leader) && im_binder_related(task))
		task->im_flag |= IM_SURFACEFLINGER;
}

void im_wmi_current(void)
{
	im_wmi(current);
}

void im_set_flag(struct task_struct *task, int flag)
{
	struct task_struct *leader = task->group_leader;

	task->im_flag |= flag;
	if (flag == IM_ENQUEUE) {
		if (leader)
			im_set_flag(leader, IM_MAIN);
	}
}

void im_set_flag_current(int flag)
{

	struct task_struct *leader = current->group_leader;

	im_tagging(current, IM_ID_HWUI);
	if (current->im_flag & IM_HWUI) {
		return;
	}
	im_tagging(current, IM_ID_HWUI_EX);
	if (current->im_flag & IM_HWUI_EX) {
		return;
	}

	current->im_flag |= flag;
	if (flag == IM_ENQUEUE) {
		if (leader)
			im_set_flag(leader, IM_MAIN);
	}
}

void im_unset_flag(struct task_struct *task, int flag)
{
	task->im_flag &= ~flag;
}

void im_unset_flag_current(int flag)
{
	current->im_flag &= ~flag;
}

void im_reset_flag(struct task_struct *task)
{
	task->im_flag = 0;
}

void im_reset_flag_current(void)
{
	current->im_flag = 0;
}

static void change_grp_from_pool(bool grouping_enable)
{
	struct task_list_entry *p;
	struct task_struct *task;

	if (list_empty(&task_list_head))
		return;

	spin_lock(&tb_render_group_lock);
	list_for_each_entry(p, &task_list_head, node) {

		rcu_read_lock();
		task = find_task_by_vpid(p->pid);
		if (task)
			/* group_id of surfaceflinger is 0 by default,
			 * because sf is not top-app
			 */
			im_set_op_group(task, IM_SURFACEFLINGER,
				grouping_enable);

		rcu_read_unlock();
	}
	spin_unlock(&tb_render_group_lock);
}

static int tb_rdg_enable_store(const char *buf, const struct kernel_param *kp)
{
	int val;

	if (sscanf(buf, "%d\n", &val) <= 0)
		return 0;

	if (tb_rdg_enable == val)
		return 0;

	tb_rdg_enable = val;

	change_grp_from_pool(!(tb_rdg_enable == 0));

	// disable will change group 2 to 0
	if(!tb_rdg_enable)
		group_remove();

	return 0;
}

static int tb_rdg_enable_show(char *buf, const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", tb_rdg_enable);
}

static struct kernel_param_ops tb_rdg_enable_ops = {
	.set = tb_rdg_enable_store,
	.get = tb_rdg_enable_show,
};

module_param_cb(tb_rdg_enable, &tb_rdg_enable_ops, NULL, 0664);

static int tb_rdg_list_store(const char *buf, const struct kernel_param *kp)
{
	int val;
	struct task_list_entry *p, *next;

	if (sscanf(buf, "%d\n", &val) <= 0)
		return 0;

	if (list_empty(&task_list_head))
		return 0;

	spin_lock(&tb_render_group_lock);
	list_for_each_entry_safe(p, next, &task_list_head, node) {

		pr_debug("rm task pid=%d\n", p->pid);
		list_del_init(&p->node);
		kfree(p);
		val--;
		if (val == 0)
			break;
	}
	spin_unlock(&tb_render_group_lock);
	return 0;
}

static int tb_rdg_list_show(char *buf, const struct kernel_param *kp)
{
	struct task_list_entry *p;
	struct task_struct *task;
	int cnt = 0;

	if (list_empty(&task_list_head))
		return cnt;

	spin_lock(&tb_render_group_lock);
	list_for_each_entry(p, &task_list_head, node) {

		pr_info("show task pid=%d\n", p->pid);
		rcu_read_lock();
		task = find_task_by_vpid(p->pid);
		if (task) {
			cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "%s %d\n",
				task->comm, task->pid);
		} else {
			pr_warn("cannot find task pid=%d\n", p->pid);
		}

		rcu_read_unlock();
	}
	spin_unlock(&tb_render_group_lock);

	return cnt;
}

static struct kernel_param_ops tb_rdg_list_ops = {
	.set = tb_rdg_list_store,
	.get = tb_rdg_list_show,
};
module_param_cb(tb_rdg_list, &tb_rdg_list_ops, NULL, 0664);

int im_render_grouping_enable(void)
{
	return tb_rdg_enable;
}

void im_set_op_group(struct task_struct *task, int flag, bool insert)
{
	if (insert) {
		if (flag != IM_KWORKER && flag != IM_LOGD &&
				flag != IM_LOGCAT) {
			if (task->grp && task->grp->id == DEFAULT_CGROUP_COLOC_ID)
				return;

			sched_set_group_id(task, DEFAULT_CGROUP_COLOC_ID);
		}
	} else {
		struct related_thread_group *grp = task->grp;

		if (grp && grp->id == DEFAULT_CGROUP_COLOC_ID)
			sched_set_group_id(task, 0);
	}
}

void im_list_add_task(struct task_struct *task)
{

	struct task_list_entry *p;

	if (!im_sf(task))
		return;

	p = kmalloc(sizeof(struct task_list_entry), GFP_KERNEL);
	if (p == NULL)
		return;

	p->pid = task->pid;
	INIT_LIST_HEAD(&p->node);

	pr_info("add task pid=%d comm=%s\n",
		task->pid, task->comm);

	spin_lock(&tb_render_group_lock);
	list_add_tail(&p->node, &task_list_head);
	spin_unlock(&tb_render_group_lock);
}

void im_list_del_task(struct task_struct *task)
{

	struct task_list_entry *p, *next;

	if (!task->pid)
		return;

	if (!im_sf(task))
		return;

	if (list_empty(&task_list_head))
		return;

	pr_info("rm task pid=%d\n", task->pid);

	spin_lock(&tb_render_group_lock);
	list_for_each_entry_safe(p, next, &task_list_head, node) {

		if (p->pid == task->pid) {
			list_del_init(&p->node);
			kfree(p);
			break;
		}
	}
	spin_unlock(&tb_render_group_lock);
}

void im_tsk_init_flag(void *ptr)
{
	struct task_struct *task = (struct task_struct*) ptr;
	task->im_flag &= ~IM_HWUI;
	task->im_flag &= ~IM_HWUI_EX;
}
