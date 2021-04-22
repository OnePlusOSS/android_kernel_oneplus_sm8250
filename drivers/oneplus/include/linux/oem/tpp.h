#ifndef __INCLUDE_TPP__
#define __INCLUDE_TPP__

#ifndef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#endif

#include <linux/sched.h>

#define TPP_CTL_NODE "tpp_ctl"
#define TPP_TAG "tpp_monitor: "

/* log info */
#define tpp_logv(fmt...) \
	do { \
		if (tpp_log_lv < 1) \
			pr_info(TPP_TAG fmt); \
	} while (0)

#define tpp_logi(fmt...) \
	do { \
		if (tpp_log_lv < 2) \
			pr_info(TPP_TAG fmt); \
	} while (0)

#define tpp_logw(fmt...) \
	do { \
		if (tpp_log_lv < 3) \
			pr_warn(TPP_TAG fmt); \
	} while (0)

#define tpp_loge(fmt...) pr_err(TPP_TAG fmt)
#define tpp_logd(fmt...) pr_debug(TPP_TAG fmt)

#define TPP_NR_CPUS (8)
#define TPP_TASK_REPORT_SIZE 360000
#define TPP_CPU_SELECT_MAX_REPORT_SIZE 3600000

/* tpp monitor log */
enum tpp_cpu_select_tags {
	TPP_CPU_SELECT_TPP_TASK_ID,
	TPP_CPU_SELECT_CPU,
	TPP_CPU_SELECT_MONITOR_SIZE,
};

enum tpp_tasks {
	TPP_OTHER_THREAD_ID = 0,
	TPP_UNITY_WORKER_THREAD_ID = 1,
	TPP_TASK_MONITOR_SIZE,
};

enum tpp_strategy {
	TPP_STRATEGY_ORIG,
	TPP_UNITY_WORKER_THREAD_MIDDLE_CORE,
	TPP_STRATEGY_SIZE,
};

enum tpp_cpu_selection_state {
	TPP_CPU_NOT_SELECT_YET = -1,
};

enum tpp_flags {
	TPP_ID_CFS_RQ,
	TPP_ID_UNITY_WORKER_THREAD,
};

#define TPP_CFS_RQ 	(1 << TPP_ID_CFS_RQ)
#define TPP_UNITY_WORKER_THREAD (1 << TPP_ID_UNITY_WORKER_THREAD)

struct tpp_cpu_select_sample {
	int data[TPP_CPU_SELECT_MAX_REPORT_SIZE][TPP_CPU_SELECT_MONITOR_SIZE];
};

struct tpp_cpu_select_monitor_struct {
	struct tpp_cpu_select_sample *buf;
	atomic_t index;
};

enum tpp_task_tags_enum {
	TPP_TASK_TAG_UTIL,
	TPP_TASK_TAG_CPU_ORIG,
	TPP_TASK_TAG_CPU,
	TPP_TASK_TAG_WORK_THREAD,
	TPP_TASK_COLUMN,
};

struct tpp_task_sample {
	long long data[TPP_TASK_REPORT_SIZE][TPP_TASK_MONITOR_SIZE * TPP_TASK_COLUMN];
};

struct tpp_task_monitor_struct {
	struct tpp_task_sample *buf;
	atomic_t index[TPP_TASK_MONITOR_SIZE];
};
extern void tpp_tagging(struct task_struct *p);
extern void tpp_dequeue(int cpu, struct task_struct *p);
extern void tpp_enqueue(int cpu, struct task_struct *p);
extern int tpp_find_cpu(struct task_struct *p, int cpu_orig);
#endif // __INCLUDE_TPP__
