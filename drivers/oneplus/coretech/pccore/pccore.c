#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cpufreq.h>
#include <oneplus/pccore/pccore_helper.h>
#include <linux/oem/pccore.h>

static int pcclog_lv = 1;
module_param_named(pcclog_lv, pcclog_lv, int, 0664);

// param@1: enable or not, param@2: select_fd_mode, param@3: depress mode, param@4: depress level
static unsigned int params[PCC_PARAMS] = { 0, 0, 0, 0 };
module_param_array_named(params, params, uint, NULL, 0664);

bool get_op_select_freq_enable(void)
{
	return params[0];
}
EXPORT_SYMBOL(get_op_select_freq_enable);

static unsigned int op_cross_limit = 99;
module_param_named(op_cross_limit, op_cross_limit, uint, 0664);
unsigned int get_op_limit(void)
{
	return op_cross_limit;
}
EXPORT_SYMBOL(get_op_limit);

unsigned int get_op_level(void)
{
	return params[3];
}
EXPORT_SYMBOL(get_op_level);

unsigned int get_op_fd_mode(void)
{
	return params[2];
}
EXPORT_SYMBOL(get_op_fd_mode);

unsigned int get_op_mode(void)
{
	return params[1];
}
EXPORT_SYMBOL(get_op_mode);

static unsigned int *get_cluster_arr(int cpu)
{

	switch (cpu) {
	case 0:
	case 1:
	case 2:
	case 3:
		return cpufreq_pd_0;
	case 4:
	case 5:
	case 6:
		return cpufreq_pd_1;
	case 7:
		return cpufreq_pd_2;
	}
	return NULL;
}

static int get_cluster(int cpu)
{
	int err = -1;

	switch (cpu) {
	case 0:
	case 1:
	case 2:
	case 3:
		return 0;
	case 4:
	case 5:
	case 6:
		return 1;
	case 7:
		return 2;
	default:
		return err;
	}
}

int cross_pd(int cpu, int prefer_idx, int target_idx, bool ascending)
{
	unsigned int *arr = get_cluster_arr(cpu);
	unsigned int idx_max;
	int cluster;

	cluster = get_cluster(cpu);
	if (cluster < 0)
		return target_idx;

	idx_max = cluster_pd[cluster]-1;

	if (ascending && (target_idx == 0 || target_idx > idx_max))
		return target_idx;

	if (!ascending && target_idx >= idx_max)
		return target_idx;

	if (target_idx == prefer_idx)
		return target_idx;

	if (arr == NULL) {
		pcc_loge("can't get pd\n");
		return target_idx;
	}

	if (ascending) {
		if (arr[target_idx] == arr[prefer_idx])
			return target_idx;
	} else {
		if (idx_max < prefer_idx)
			return target_idx;

		if (arr[idx_max - target_idx] == arr[idx_max - prefer_idx])
			return target_idx;
	}

	return prefer_idx;
}
EXPORT_SYMBOL(cross_pd);

int find_prefer_pd(int cpu, int target_idx, bool ascending, int lv_cnt)
{
	unsigned int *arr = get_cluster_arr(cpu);
	unsigned int val;
	int pre_idx;
	int prefer_idx = target_idx;
	unsigned int idx_max;
	int cluster;

	cluster = get_cluster(cpu);
	if (cluster < 0)
		return target_idx;

	idx_max = cluster_pd[cluster]-1;


	if (arr == NULL) {
		pcc_loge("can't get pd\n");
		return target_idx;
	}

	if (target_idx < 0 || target_idx > idx_max) {
		pcc_loge("idx oob, idx=%d, max=%d\n", target_idx, idx_max);
		return target_idx;
	}

	if (ascending) {

		if (target_idx == 0 || lv_cnt <= 0)
			return target_idx;

		pre_idx = target_idx - 1;
		val = arr[target_idx];

		while (lv_cnt > 0) {

			if (pre_idx == 0) {
				if (val == arr[pre_idx])
					return prefer_idx;
				else
					return pre_idx;
			}

			if (val != arr[pre_idx]) {
				val = arr[pre_idx];
				prefer_idx = pre_idx;
				lv_cnt--;
			}

			pre_idx--;
		}

	} else {

		if (target_idx == idx_max || lv_cnt <= 0)
			return target_idx;

		pre_idx = target_idx + 1;
		val = arr[target_idx];

		while (lv_cnt > 0) {

			if (pre_idx == idx_max) {
				if (val == arr[pre_idx])
					return prefer_idx;
				else
					return pre_idx;
			}

			if (val != arr[pre_idx]) {
				val = arr[pre_idx];
				prefer_idx = pre_idx;
				lv_cnt--;
			}

			pre_idx++;
		}

	}

	return prefer_idx;
}
EXPORT_SYMBOL(find_prefer_pd);

static int pccore_init(void)
{
	pcc_logi("pccore init\n");
	return 0;
}

pure_initcall(pccore_init);
