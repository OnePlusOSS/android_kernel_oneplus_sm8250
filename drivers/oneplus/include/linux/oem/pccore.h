#ifndef __INCLUDE_PCCORE__
#define __INCLUDE_PCCORE__

#ifndef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#endif

#define PCC_TAG "pccore:"

#define PCC_PARAMS 4
#define NR_CLU 3

#define pcc_logv(fmt...) \
	do { \
		if (pcclog_lv < 1) \
			pr_info(PCC_TAG fmt); \
	} while (0)

#define pcc_logi(fmt...) \
	do { \
		if (pcclog_lv < 2) \
			pr_info(PCC_TAG fmt); \
	} while (0)

#define pcc_logw(fmt...) \
	do { \
		if (pcclog_lv < 3) \
			pr_warn(PCC_TAG fmt); \
	} while (0)

#define pcc_loge(fmt...) pr_err(PCC_TAG fmt)
#define pcc_logd(fmt...) pr_debug(PCC_TAG fmt)

static unsigned int cluster_pd[NR_CLU] = {17, 18, 21};
static unsigned int cpufreq_pd_0[17] = {
	0,//300000
	0,//403200
	0,//518400
	0,//614400
	0,//691200
	1,//787200
	1,//883200
	2,//979200
	2,//1075200
	2,//1171200
	3,//1248800
	3,//1344000
	3,//1420800
	3,//1516800
	4,//1612800
	5,//1708800
	5//1804800
};

static unsigned int cpufreq_pd_1[18] = {
	0,//710400
	1,//825600
	1,//940800
	2,//1056000
	2,//1171200
	3,//1286400
	3,//1382400
	3,//1478400
	4,//1574400
	4,//1670400
	4,//1766400
	5,//1862400
	6,//1958400
	6,//2054400
	7,//2150400
	8,//2246400
	8,//2342400
	8 //2419200
};

static unsigned int cpufreq_pd_2[21] = {
	0,// 844800
	1,// 960000
	1,//1075200
	2,//1190400
	2,//1305600
	2,//1401600
	3,//1516800
	3,//1632000
	3,//1747200
	4,//1862400
	4,//1977600
	4,//2073600
	5,//2169600
	6,//2265600
	6,//2361600
	7,//2457600
	8,//2553600
	8,//2649600
	8,//2745600
	8,//2841600
	8//3187200 (20828)
};

#endif // __INCLUDE_PCCORE__
