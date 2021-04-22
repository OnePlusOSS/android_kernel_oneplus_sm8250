/* decision maker info */

#define CCDM_CLUS_SIZE 3
#define CCDM_CPU_SIZE 8
#define CCDM_ULLONG_MAX (0xffffffffffffffffULL)
struct ccdm_info {
	long long c_min[CCDM_CLUS_SIZE];
	long long c_max[CCDM_CLUS_SIZE];
	long long c_fps_boost[CCDM_CLUS_SIZE];
	long long fps_boost_hint;
	long long trust[CCDM_CLUS_SIZE];
	long long weight[CCDM_CLUS_SIZE];

	long long c_fps_boost_ddrfreq;
	long long ddrfreq;
	/* Turbo Boost */
	long long tb_freq_boost[CCDM_CLUS_SIZE];
	long long tb_place_boost_hint;
	long long tb_idle_block_hint[CCDM_CPU_SIZE];
	long long tb_cctl_boost_hint;
};

/* expected to public */
enum {
	CCDM_DEFAULT = 0,
	CCDM_CLUS_0_CPUFREQ,
	CCDM_CLUS_1_CPUFREQ,
	CCDM_CLUS_2_CPUFREQ,
	CCDM_FPS_BOOST,
	CCDM_VOTING_DDRFREQ,
	CCDM_FPS_BOOST_HINT,

	/* Turbo boost */
	CCDM_TB_CLUS_0_FREQ_BOOST,
	CCDM_TB_CLUS_1_FREQ_BOOST,
	CCDM_TB_CLUS_2_FREQ_BOOST,
	CCDM_TB_FREQ_BOOST,
	CCDM_TB_PLACE_BOOST,

	CCDM_TB_CPU_0_IDLE_BLOCK,
	CCDM_TB_CPU_1_IDLE_BLOCK,
	CCDM_TB_CPU_2_IDLE_BLOCK,
	CCDM_TB_CPU_3_IDLE_BLOCK,
	CCDM_TB_CPU_4_IDLE_BLOCK,
	CCDM_TB_CPU_5_IDLE_BLOCK,
	CCDM_TB_CPU_6_IDLE_BLOCK,
	CCDM_TB_CPU_7_IDLE_BLOCK,
	CCDM_TB_IDLE_BLOCK,
	CCDM_TB_CCTL_BOOST,
};

static struct ccdm_info ginfo = {
	{ 0, 0, 0}, // c_min
	{ 0, 0, 0}, // c_max
	{ 0, 0, 0}, // fps_boost
	0, // fps_boost_hint
	{ 100, 100, 100}, // trust
	{ 100, 100, 100}, // weight
	0,          // fps_boost_ddr
	0,          // ddr
	{ 0, 0, 0}, // freq boost
	0,          // place boost hint
	{ CCDM_ULLONG_MAX, CCDM_ULLONG_MAX,
		CCDM_ULLONG_MAX, CCDM_ULLONG_MAX,
		CCDM_ULLONG_MAX, CCDM_ULLONG_MAX,
		CCDM_ULLONG_MAX, CCDM_ULLONG_MAX
	}, // idle block
	0,


};

/* helper */
static inline clamp(long long val, long long lo, long long hi)
{
	val = val >= lo ? val : lo;
	val = val <= hi ? val : hi;
	return val;
}

/* update info part */
void ccdm_update_hint_1(int type, long long arg1)
{
	switch (type) {
	case CCDM_VOTING_DDRFREQ:
		ginfo.ddrfreq = arg1;
		break;
	case CCDM_TB_CLUS_0_FREQ_BOOST:
		ginfo.tb_freq_boost[0] = arg1;
		break;
	case CCDM_TB_CLUS_1_FREQ_BOOST:
		ginfo.tb_freq_boost[1] = arg1;
		break;
	case CCDM_TB_CLUS_2_FREQ_BOOST:
		ginfo.tb_freq_boost[2] = arg1;
		break;
	case CCDM_TB_PLACE_BOOST:
		ginfo.tb_place_boost_hint = arg1;
		break;
	case CCDM_TB_CPU_0_IDLE_BLOCK:
		ginfo.tb_idle_block_hint[0] = arg1;
		break;
	case CCDM_TB_CPU_1_IDLE_BLOCK:
		ginfo.tb_idle_block_hint[1] = arg1;
		break;
	case CCDM_TB_CPU_2_IDLE_BLOCK:
		ginfo.tb_idle_block_hint[2] = arg1;
		break;
	case CCDM_TB_CPU_3_IDLE_BLOCK:
		ginfo.tb_idle_block_hint[3] = arg1;
		break;
	case CCDM_TB_CPU_4_IDLE_BLOCK:
		ginfo.tb_idle_block_hint[4] = arg1;
		break;
	case CCDM_TB_CPU_5_IDLE_BLOCK:
		ginfo.tb_idle_block_hint[5] = arg1;
		break;
	case CCDM_TB_CPU_6_IDLE_BLOCK:
		ginfo.tb_idle_block_hint[6] = arg1;
		break;
	case CCDM_TB_CPU_7_IDLE_BLOCK:
		ginfo.tb_idle_block_hint[7] = arg1;
		break;
	case CCDM_TB_CCTL_BOOST:
		ginfo.tb_cctl_boost_hint = arg1;
		break;
	}
}

void ccdm_update_hint_2(int type, long long arg1, long long arg2)
{
	switch (type) {
	case CCDM_CLUS_0_CPUFREQ:
		ginfo.c_min[0] = arg1;
		ginfo.c_max[0] = arg2;
		break;
	case CCDM_CLUS_1_CPUFREQ:
		ginfo.c_min[1] = arg1;
		ginfo.c_max[1] = arg2;
		break;
	case CCDM_CLUS_2_CPUFREQ:
		ginfo.c_min[2] = arg1;
		ginfo.c_max[2] = arg2;
		break;
	}
}

void ccdm_update_hint_3(
	int type,
	long long arg1,
	long long arg2,
	long long arg3)
{
	switch (type) {
	case CCDM_TB_FREQ_BOOST:
		ginfo.tb_freq_boost[0] = arg1;
		ginfo.tb_freq_boost[1] = arg2;
		ginfo.tb_freq_boost[2] = arg3;
		break;
	}
}

void ccdm_update_hint_4(
	int type,
	long long arg1,
	long long arg2,
	long long arg3,
	long long arg4)
{
	switch (type) {
	case CCDM_FPS_BOOST:
		ginfo.c_fps_boost[0] = arg1;
		ginfo.c_fps_boost[1] = arg2;
		ginfo.c_fps_boost[2] = arg3;
		ginfo.c_fps_boost_ddrfreq = arg4;
		break;
	}
}

long long ccdm_get_hint(int type)
{
	switch (type) {
	case CCDM_FPS_BOOST_HINT:
		return ginfo.fps_boost_hint;
	case CCDM_TB_PLACE_BOOST:
		return ginfo.tb_place_boost_hint;
	case CCDM_TB_CLUS_0_FREQ_BOOST:
		return ginfo.tb_freq_boost[0];
	case CCDM_TB_CLUS_1_FREQ_BOOST:
		return ginfo.tb_freq_boost[1];
	case CCDM_TB_CLUS_2_FREQ_BOOST:
		return ginfo.tb_freq_boost[2];
	case CCDM_TB_CPU_0_IDLE_BLOCK:
		return ginfo.tb_idle_block_hint[0];
	case CCDM_TB_CPU_1_IDLE_BLOCK:
		return ginfo.tb_idle_block_hint[1];
	case CCDM_TB_CPU_2_IDLE_BLOCK:
		return ginfo.tb_idle_block_hint[2];
	case CCDM_TB_CPU_3_IDLE_BLOCK:
		return ginfo.tb_idle_block_hint[3];
	case CCDM_TB_CPU_4_IDLE_BLOCK:
		return ginfo.tb_idle_block_hint[4];
	case CCDM_TB_CPU_5_IDLE_BLOCK:
		return ginfo.tb_idle_block_hint[5];
	case CCDM_TB_CPU_6_IDLE_BLOCK:
		return ginfo.tb_idle_block_hint[6];
	case CCDM_TB_CPU_7_IDLE_BLOCK:
		return ginfo.tb_idle_block_hint[7];
	case CCDM_TB_CCTL_BOOST:
		return ginfo.tb_cctl_boost_hint;
	}
	return 0;
}

int ccdm_any_hint(void)
{
	int i;

	for (i = CCDM_TB_CLUS_0_FREQ_BOOST; i < CCDM_TB_CPU_0_IDLE_BLOCK; ++i) {
		if (ccdm_get_hint(i))
			return i;
	}

	return 0;
}

/* output util */
static long long cpufreq_decision(int type,
	long long util,
	long long min_freq,
	long long max_freq,
	long long max_util) // TBD
{
	long long final_util, final_freq, extra_util;
	long long w, t;
	int clus_idx = 0;
	long long target;

	switch (type) {
	case CCDM_CLUS_0_CPUFREQ:
		clus_idx = 0;
		break;
	case CCDM_CLUS_1_CPUFREQ:
		clus_idx = 1;
		break;
	case CCDM_CLUS_2_CPUFREQ:
		clus_idx = 2;
		break;
	default:
		/* should not happen */
		return util;
	}

	extra_util = (ginfo.c_fps_boost[clus_idx] * max_util / 10) * 4 / 5;

	/* read out in case changed */
	target = ginfo.c_max[clus_idx];

	if (target == 0 || target == 2147483647 /* INT_MAX */) {
		/* no cpufreq adjust, only take care of fps boost case */
		util += extra_util;
		util = util > max_util ? max_util : util;
		return util;
	} else {
		final_freq = target;
	}

	if (!max_freq)
		max_freq = 1;

	final_util = max_util * final_freq / max_freq;
	w = ginfo.weight[clus_idx];
	t = ginfo.trust[clus_idx];

	if (w)
		final_util =
			(util * (100 - w) / 100 + final_util * w / 100) * 4 / 5;

	/* pick lower one */
	final_util = final_util > util ? util : final_util;

	/* add extra boost */
	final_util += extra_util;

	/* cap with ceil */
	final_util = final_util > max_util ? max_util : final_util;

	return final_util;
}

static long long ddrfreq_voting_decision(int type,
	long long arg1,
	long long arg2,
	long long arg3,
	long long arg4)
{
	/* TODO add mapping table */
	return ginfo.ddrfreq;
}

static inline long long decision(int type,
	long long arg1,
	long long arg2,
	long long arg3,
	long long arg4)
{
	long long ret = 0;

	switch (type) {
	case CCDM_CLUS_0_CPUFREQ:
	case CCDM_CLUS_1_CPUFREQ:
	case CCDM_CLUS_2_CPUFREQ:
		ret = cpufreq_decision(type, arg1, arg2, arg3, arg4);
		break;
	case CCDM_VOTING_DDRFREQ:
		ret = ddrfreq_voting_decision(type, arg1, arg2, arg3, arg4);
		break;
	}
	return ret;
}

/* decision part */
long long ccdm_decision_1(int type, long long arg1)
{
	return decision(type, arg1, 0, 0, 0);
}

long long ccdm_decision_2(int type, long long arg1, long long arg2)
{
	return decision(type, arg1, arg2, 0, 0);
}

long long ccdm_decision_3(
	int type,
	long long arg1,
	long long arg2,
	long long arg3)
{
	return decision(type, arg1, arg2, arg3, 0);
}

long long ccdm_decision_4(
	int type,
	long long arg1,
	long long arg2,
	long long arg3,
	long long arg4)
{
	return decision(type, arg1, arg2, arg3, arg4);
}

long long ccdm_decision(
	int type,
	long long arg1,
	long long arg2,
	long long arg3,
	long long arg4)
{
	return decision(type, arg1, arg2, arg3, arg4);
}

/* debug */
void ccdm_get_status(void *ptr)
{
	struct ccdm_info *ccdm = (struct ccdm_info *) ptr;
	int i;

	if (!ccdm)
		return;

	for (i = 0; i < CCDM_CLUS_SIZE; ++i) {
		ccdm->c_min[i] = ginfo.c_min[i];
		ccdm->c_max[i] = ginfo.c_max[i];
		ccdm->c_fps_boost[i] = ginfo.c_fps_boost[i];
		ccdm->trust[i] = ginfo.trust[i];
		ccdm->weight[i] = ginfo.weight[i];
		ccdm->tb_freq_boost[i] = ginfo.tb_freq_boost[i];
	}
	ccdm->fps_boost_hint = ginfo.fps_boost_hint;
	ccdm->ddrfreq = ginfo.ddrfreq;
	ccdm->c_fps_boost_ddrfreq = ginfo.c_fps_boost_ddrfreq;
	ccdm->tb_place_boost_hint = ginfo.tb_place_boost_hint;
	ccdm->tb_cctl_boost_hint = ginfo.tb_cctl_boost_hint;

	for (i = 0; i < CCDM_CPU_SIZE; ++i)
		ccdm->tb_idle_block_hint[i] = ginfo.tb_idle_block_hint[i];
};

void ccdm_reset(void)
{
	int i = 0;

	for (i = 0; i < CCDM_CLUS_SIZE; ++i) {
		ginfo.c_min[i] = 0;
		ginfo.c_max[i] = 0;
		ginfo.c_fps_boost[i] = 0;
		/* leave default config not change */
		//ginfo.trust[i] = 100;
		//ginfo.weight[i] = 100;
		ginfo.tb_freq_boost[i] = 0;
	}
	ginfo.fps_boost_hint = 0;
	ginfo.ddrfreq = 0;
	ginfo.c_fps_boost_ddrfreq = 0;
	ginfo.tb_place_boost_hint = 0;
	ginfo.tb_cctl_boost_hint = 0;

	for (i = 0; i < CCDM_CPU_SIZE; ++i)
		ginfo.tb_idle_block_hint[i] = CCDM_ULLONG_MAX;
}
