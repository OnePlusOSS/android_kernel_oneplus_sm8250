// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#include <video/mipi_display.h>
#include <sde_encoder_phys.h>
#include "dsi_iris5_api.h"
#include "dsi_iris5_lightup.h"
#include "dsi_iris5_lightup_ocp.h"
#include "dsi_iris5_lp.h"
#include "dsi_iris5_pq.h"
#include "dsi_iris5_ioctl.h"
#include "dsi_iris5_log.h"


extern uint8_t iris_pq_update_path;

static bool iris_require_yuv_input;
static bool iris_HDR10;
static bool iris_HDR10_YCoCg;
static bool shadow_iris_require_yuv_input;
static bool shadow_iris_HDR10;
static bool shadow_iris_HDR10_YCoCg;
static bool iris_yuv_datapath;
static bool iris_capture_ctrl_en;
static bool iris_debug_cap;
static u8 iris_dbc_lut_index;
static u8 iris_sdr2hdr_mode;
static struct iris_setting_info iris_setting;
static bool iris_skip_dma;
extern int iris_frc_dma_disable;
static u32 iris_min_color_temp;
static u32 iris_max_color_temp;
static u32 iris_min_x_value;
static u32 iris_max_x_value;
static u32 iris_sdr2hdr_lut2ctl = 0xFFE00000;
static u32 iris_sdr2hdr_lutyctl = 0xFFFF0000;

#ifndef MIN
#define  MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

#define IRIS_CCT_MIN_VALUE		2500
#define IRIS_CCT_MAX_VALUE		7500
#define IRIS_CCT_STEP			25
#define IRIS_X_6500K			3128
#define IRIS_X_7500K			2991
#define IRIS_X_7700K			2969
#define IRIS_X_2500K			4637
#define IRIS_LAST_BIT_CTRL	1

/*range is 2500~10000*/
static u32 iris_color_x_buf[] = {
	4637, 4626, 4615, 4603,
	4591, 4578, 4565, 4552,
	4538, 4524, 4510, 4496,
	4481, 4467, 4452, 4437,
	4422, 4407, 4392, 4377,
	4362, 4347, 4332, 4317,
	4302, 4287, 4272, 4257,
	4243, 4228, 4213, 4199,
	4184, 4170, 4156, 4141,
	4127, 4113, 4099, 4086,
	4072, 4058, 4045, 4032,
	4018, 4005, 3992, 3980,
	3967, 3954, 3942, 3929,
	3917, 3905, 3893, 3881,
	3869, 3858, 3846, 3835,
	3823, 3812, 3801, 3790,
	3779, 3769, 3758, 3748,
	3737, 3727, 3717, 3707,
	3697, 3687, 3677, 3668,
	3658, 3649, 3639, 3630,
	3621, 3612, 3603, 3594,
	3585, 3577, 3568, 3560,
	3551, 3543, 3535, 3527,
	3519, 3511, 3503, 3495,
	3487, 3480, 3472, 3465,
	3457, 3450, 3443, 3436,
	3429, 3422, 3415, 3408,
	3401, 3394, 3388, 3381,
	3375, 3368, 3362, 3356,
	3349, 3343, 3337, 3331,
	3325, 3319, 3313, 3307,
	3302, 3296, 3290, 3285,
	3279, 3274, 3268, 3263,
	3258, 3252, 3247, 3242,
	3237, 3232, 3227, 3222,
	3217, 3212, 3207, 3202,
	3198, 3193, 3188, 3184,
	3179, 3175, 3170, 3166,
	3161, 3157, 3153, 3149,
	3144, 3140, 3136, 3132,
	3128, 3124, 3120, 3116,
	3112, 3108, 3104, 3100,
	3097, 3093, 3089, 3085,
	3082, 3078, 3074, 3071,
	3067, 3064, 3060, 3057,
	3054, 3050, 3047, 3043,
	3040, 3037, 3034, 3030,
	3027, 3024, 3021, 3018,
	3015, 3012, 3009, 3006,
	3003, 3000, 2997, 2994,
	2991, 2988, 2985, 2982,
	2980, 2977, 2974, 2971,
	2969, 2966, 2963, 2961,
	2958, 2955, 2953, 2950,
	2948, 2945, 2943, 2940,
	2938, 2935, 2933, 2930,
	2928, 2926, 2923, 2921,
	2919, 2916, 2914, 2912,
	2910, 2907, 2905, 2903,
	2901, 2899, 2896, 2894,
	2892, 2890, 2888, 2886,
	2884, 2882, 2880, 2878,
	2876, 2874, 2872, 2870,
	2868, 2866, 2864, 2862,
	2860, 2858, 2856, 2854,
	2853, 2851, 2849, 2847,
	2845, 2844, 2842, 2840,
	2838, 2837, 2835, 2833,
	2831, 2830, 2828, 2826,
	2825, 2823, 2821, 2820,
	2818, 2817, 2815, 2813,
	2812, 2810, 2809, 2807,
	2806, 2804, 2803, 2801,
	2800, 2798, 2797, 2795,
	2794, 2792, 2791, 2789,
	2788,
};

/*G0,G1,G2,G3,G4,G5*/
static u32 m_dwGLuxBuffer[] = {210, 1024, 1096, 1600, 2000, 2400};
/*K0,K1,K2,K3,K4,K5*/
static u32 m_dwKLuxBuffer[] = {511, 51, 46, 30, 30, 30};
/*X1,X2,X3,X4,X5*/
static u32 m_dwXLuxBuffer[] = {20, 1200, 1300, 1536, 3584};
/*org=320; Joey modify 20160712*/
static u32 m_dwBLux = 50;
/*org=128; Joey modify 20160712*/
static u32 m_dwTH_LuxAdj = 150;

typedef enum {
	eScalerDownNom = 0,
	eScalerDown1_6,	//shrink ratio >=1.6;
	eScalerDown1_8,	//shrink ratio >=1.8;
	eScalerDown2_0,	//shrink ratio >=2;
	eScalerDown2_2,
	eScalerDown2_4,
	eScalerDown2_6,
	eScalerDown2_8,
	eScaleDownInvalid,
} eScalerDownSoftRatio;

u8 iris_get_dbc_lut_index(void)
{
	return iris_dbc_lut_index;
}

struct iris_setting_info *iris_get_setting(void)
{
	return &iris_setting;
}

void iris_set_yuv_input(bool val)
{
	shadow_iris_require_yuv_input = val;
}

void iris_set_HDR10_YCoCg(bool val)
{
	shadow_iris_HDR10_YCoCg = val;
}

void iris_set_sdr2hdr_mode(u8 val)
{
	iris_sdr2hdr_mode = val;
}

int iris_get_hdr_enable(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	if (pcfg->valid < PARAM_PARSED)
		return 0;
	else if (iris_HDR10_YCoCg)
		return 2;
	else if (iris_HDR10)
		return 1;
	else if (iris_setting.quality_cur.pq_setting.sdr2hdr != SDR2HDR_Bypass)
		return 100;
	else
		return 0;
}

bool iris_dspp_dirty(void)
{
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;

	if (pqlt_cur_setting->dspp_dirty > 0) {
		IRIS_LOGI("DSPP is dirty");
		pqlt_cur_setting->dspp_dirty--;
		return true;
	}

	return false;
}

void iris_quality_setting_off(void)
{
	iris_setting.quality_cur.al_bl_ratio = 0;
	if (iris_setting.quality_cur.pq_setting.sdr2hdr != SDR2HDR_Bypass) {
		iris_setting.quality_cur.pq_setting.sdr2hdr = SDR2HDR_Bypass;
		iris_sdr2hdr_level_set(SDR2HDR_Bypass);
		iris_setting.quality_cur.pq_setting.cmcolorgamut = 0;
		iris_cm_color_gamut_set(
				iris_setting.quality_cur.pq_setting.cmcolorgamut);
	} else {
		iris_cm_color_gamut_pre_clear();
	}

	iris_capture_ctrl_en = false;
	iris_skip_dma = false;
	iris_sdr2hdr_mode = 0;
}

bool iris_get_debug_cap(void)
{
	return iris_debug_cap;
}

void iris_set_debug_cap(bool val)
{
	iris_debug_cap = val;
}

void iris_set_skip_dma(bool skip)
{
	IRIS_LOGD("skip_dma=%d", skip);
	iris_skip_dma = skip;
}

static int iris_capture_disable_pq(struct iris_update_ipopt *popt, bool *skiplast)
{
	int len = 0;

	*skiplast = 0;
	if ((!iris_capture_ctrl_en) && (!iris_debug_cap)) {
		if (!iris_dynamic_power_get())
			len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_PWIL,
					0x50, 0x50, IRIS_LAST_BIT_CTRL);
		else
			len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_PWIL,
					0x52, 0x52, IRIS_LAST_BIT_CTRL);

		*skiplast = 1;
	}
	if (!iris_dynamic_power_get() && !iris_skip_dma)
		*skiplast = 1;
	return len;
}

static int iris_capture_enable_pq(struct iris_update_ipopt *popt, int oldlen)
{
	int len = oldlen;

	if ((!iris_capture_ctrl_en) && (!iris_debug_cap))
		len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_PWIL,
				0x51, 0x51,
				(!iris_dynamic_power_get()) ? 0x01 : 0x0);

	if (!iris_dynamic_power_get() && !iris_skip_dma)
		len = iris_init_update_ipopt_t(
				popt, IP_OPT_MAX, IRIS_IP_DMA, 0xe2, 0xe2, 0);
	return len;
}

static int iris_capture_disable_lce(struct iris_update_ipopt *popt, bool *skiplast)
{
	int len = 0;

	*skiplast = 0;
	if ((!iris_capture_ctrl_en)
			&& (iris_lce_power_status_get())
			&& (!iris_debug_cap)) {
		if (!iris_dynamic_power_get())
			iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_PWIL,
					0x50, 0x50, IRIS_LAST_BIT_CTRL);
		else
			iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_PWIL,
					0x52, 0x52, IRIS_LAST_BIT_CTRL);

		*skiplast = 1;
	}
	if (!iris_dynamic_power_get() && !iris_skip_dma)
		*skiplast = 1;
	return len;
}

static int iris_capture_enable_lce(struct iris_update_ipopt *popt, int oldlen)
{
	int len = oldlen;

	if ((!iris_capture_ctrl_en)
			&& (iris_lce_power_status_get())
			&& (!iris_debug_cap))
		len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_PWIL,
				0x51, 0x51, (!iris_dynamic_power_get())?0x01:0x0);

	if (!iris_dynamic_power_get() && !iris_skip_dma) {
		len = iris_init_update_ipopt_t(
				popt, IP_OPT_MAX, IRIS_IP_DMA, 0xe3, 0xe3, 0x01);
		len = iris_init_update_ipopt_t(
				popt, IP_OPT_MAX, IRIS_IP_DMA, 0xe2, 0xe2, 0);
	}
	return len;
}

void iris_init_ipopt_ip(struct iris_update_ipopt *ipopt,  int len)
{
	int i = 0;

	for (i = 0; i < len; i++)
		ipopt[i].ip = 0xff;
}

static u32 iris_color_temp_x_get(u32 index)
{
	return iris_color_x_buf[index];
}

void iris_scaler_filter_ratio_get(void)
{
	u32 dwRatioDiff = 0;
	struct iris_cfg *pcfg = NULL;
	eScalerDownSoftRatio dwScaleDownRatio = eScalerDownNom;

	pcfg = iris_get_cfg();
	dwRatioDiff = pcfg->frc_setting.disp_hres * 10 / pcfg->frc_setting.memc_hres;

	if ((dwRatioDiff < 20) && (dwRatioDiff >= 18))
		dwScaleDownRatio = eScalerDown1_8;
	else if ((dwRatioDiff < 22) && (dwRatioDiff >= 20))
		dwScaleDownRatio = eScalerDown2_0;
	else if ((dwRatioDiff < 24) && (dwRatioDiff >= 22))
		dwScaleDownRatio = eScalerDown2_2;
	else if ((dwRatioDiff < 26) && (dwRatioDiff >= 24))
		dwScaleDownRatio = eScalerDown2_4;
	else if ((dwRatioDiff < 28) && (dwRatioDiff >= 26))
		dwScaleDownRatio = eScalerDown2_6;
	else if (dwRatioDiff >= 28)
		dwScaleDownRatio = eScalerDown2_8;

	if (dwScaleDownRatio < eScalerDown1_6)
		iris_scaler_filter_update(SCALER_PP, 0);
	else
		iris_scaler_filter_update(SCALER_PP, 1);

	IRIS_LOGI("%s, scaler ratio=%d", __func__, dwScaleDownRatio);
	iris_scaler_filter_update(SCALER_INPUT, (u32)dwScaleDownRatio);
}

void iris_pq_parameter_init(void)
{
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 index;

	if (pqlt_cur_setting->pq_setting.sdr2hdr
			== SDR2HDR_Bypass)
		iris_yuv_datapath = false;
	else
		iris_yuv_datapath = true;

	/* no pxlw node */
	if (pcfg->valid <= PARAM_EMPTY) {
		IRIS_LOGW("no pxlw node");
		return;
	}

	iris_dbc_lut_index = 0;
	if (pcfg->panel->panel_mode == DSI_OP_VIDEO_MODE)
		iris_debug_cap = true;

	iris_min_color_temp = pcfg->min_color_temp;
	iris_max_color_temp = pcfg->max_color_temp;

	index = (iris_min_color_temp-IRIS_CCT_MIN_VALUE)/IRIS_CCT_STEP;
	iris_min_x_value = iris_color_temp_x_get(index);

	index = (iris_max_color_temp-IRIS_CCT_MIN_VALUE)/IRIS_CCT_STEP;
	iris_max_x_value = iris_color_temp_x_get(index);

	pqlt_cur_setting->colortempvalue = 6500;

	IRIS_LOGI("%s, iris_min_x_value=%d, iris_max_x_value = %d", __func__, iris_min_x_value, iris_max_x_value);
}

void iris_peaking_level_set(u32 level)
{
	u32 csc;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool skiplast = 0;
	int len;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;


	iris_init_ipopt_ip(popt,  IP_OPT_MAX);

	csc = (level == 0) ? 0x11 : 0x10;
	if (iris_yuv_datapath == true)
		csc = 0x11;

	len = iris_capture_disable_pq(popt, &skiplast);
	iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PEAKING,
			level, 0x01);
	len  = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PEAKING,
			csc, skiplast);

	len = iris_capture_enable_pq(popt, len);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);

	IRIS_LOGI("peaking level=%d, len=%d", level, len);
}

static int iris_cm_csc_para_set(struct iris_update_ipopt *popt, uint8_t skip_last, uint32_t csc_ip, uint32_t *csc_value)
{
	int32_t ip = 0;
	int32_t opt_id = 0;
	uint32_t *data = NULL;
	uint32_t val = 0;
	int len = 0;
	struct iris_ip_opt *psopt;

	if (csc_value == NULL) {
		IRIS_LOGE("csc value is empty");
		return 0;
	}

	ip = csc_ip;
	opt_id = 0x40;
	psopt = iris_find_ip_opt(ip, opt_id);
	if (psopt == NULL) {
		IRIS_LOGE("can not find ip = %02x opt_id = %02x", ip, opt_id);
		return 1;
	}

	data = (uint32_t *)psopt->cmd[0].msg.tx_buf;
	IRIS_LOGD("csc: csc0=0x%x, csc1=0x%x, csc2=0x%x, csc3=0x%x, csc4=0x%x",
			csc_value[0], csc_value[1], csc_value[2], csc_value[3], csc_value[4]);
	val = csc_value[0];
	val &= 0x7fff7fff;
	data[3] = val;
	val = csc_value[1];
	val &= 0x7fff7fff;
	data[4] = val;
	val = csc_value[2];
	val &= 0x7fff7fff;
	data[5] = val;
	val = csc_value[3];
	val &= 0x7fff7fff;
	data[6] = val;
	val = csc_value[4];
	val &= 0x00007fff;
	data[7] = val;

	iris_send_ipopt_cmds(ip, opt_id);

	return len;
}

void iris_cm_csc_level_set(u32 csc_ip, u32 *csc_value)
{
	bool skiplast = 0;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;


	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);
	len = iris_cm_csc_para_set(popt, skiplast, csc_ip, csc_value);
	len = iris_capture_enable_pq(popt, len);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
	IRIS_LOGD("%s csc len=%d", (csc_ip == IRIS_IP_DPP) ? "dpp" : "cm", len);
}

void iris_cm_6axis_level_set(u32 level)
{
	bool skiplast = 0;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;
	//bool cm_csc_enable = true;
	//struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);
	//f ((level == 0)
	//		&& (pqlt_cur_setting->pq_setting.cm6axis == 0)
	//		&& (iris_yuv_datapath == false))
	//	cm_csc_enable = false;

	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_CM, level, skiplast);

	//len = iris_cm_csc_set(popt, skiplast, cm_csc_enable);

	len = iris_capture_enable_pq(popt, len);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
	IRIS_LOGI("cm 6axis level=%d, len=%d", level, len);
}

void iris_cm_ftc_enable_set(u32 level)
{
	u32 locallevel;
	bool skiplast = 0;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	locallevel = 0x10 | (u8)level;
	len = iris_capture_disable_pq(popt, &skiplast);

	len = iris_update_ip_opt(
			popt, IP_OPT_MAX, IRIS_IP_CM, locallevel, skiplast);

	len = iris_capture_enable_pq(popt, len);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
	IRIS_LOGI("cm ftc enable=%d, len=%d", level, len);
}


void iris_scurve_enable_set(u32 level)
{
	u32 locallevel;
	u32 scurvelevel;
	bool skiplast = 0;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;
	u8 enable = 0;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);

	if (level > 0) {
		enable = 1;
		scurvelevel = (0x70 + (level - 1));
		len = iris_update_ip_opt(
				popt, IP_OPT_MAX, IRIS_IP_DPP, scurvelevel, 0x01);
	}

	locallevel = 0x50 | enable;
	len = iris_update_ip_opt(
			popt, IP_OPT_MAX, IRIS_IP_DPP, locallevel, skiplast);

	len = iris_capture_enable_pq(popt, len);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
	IRIS_LOGI("scurve level=%d, len=%d", level, len);
}

int iris_cm_ratio_set(struct iris_update_ipopt *popt, uint8_t skip_last)
{
	u32 tablesel;
	u32 index;
	u32 xvalue;
	u32 ratio;
	u32 value;
	u32 regvalue = 0;
	struct iris_update_regval regval;
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;
	int len;

	if (pqlt_cur_setting->pq_setting.cmcolortempmode == IRIS_COLOR_TEMP_MANUL)
		value = pqlt_cur_setting->colortempvalue;
	else if (pqlt_cur_setting->pq_setting.cmcolortempmode == IRIS_COLOR_TEMP_AUTO)
		value = pqlt_cur_setting->cctvalue;
	else
		value = IRIS_CCT_MIN_VALUE;


	if (value > iris_max_color_temp)
		value = iris_max_color_temp;
	else if (value < iris_min_color_temp)
		value = iris_min_color_temp;
	index = (value - IRIS_CCT_MIN_VALUE) / 25;
	xvalue = iris_color_temp_x_get(index);

	if (xvalue == iris_min_x_value) {
		tablesel = 0;
		regvalue = tablesel | 0x02;
	} else if ((xvalue < iris_min_x_value) && (xvalue >= IRIS_X_6500K)) {
		tablesel = 0;
		ratio = ((xvalue - IRIS_X_6500K) * 16383) / (iris_min_x_value - IRIS_X_6500K);
		regvalue = tablesel | (ratio << 16);
	} else if ((xvalue >= iris_max_x_value) && (xvalue < IRIS_X_6500K)) {
		tablesel = 1;
		ratio = ((xvalue - iris_max_x_value) * 16383) / (IRIS_X_6500K - iris_max_x_value);
		regvalue = tablesel | (ratio << 16);
	}

	regval.ip = IRIS_IP_DPP;
	regval.opt_id = 0xfd;
	regval.mask = 0x3fff0003;
	regval.value = regvalue;

	iris_update_bitmask_regval_nonread(&regval, false);
	len = iris_init_update_ipopt_t(
			popt, IP_OPT_MAX, IRIS_IP_DPP, 0xfd, 0xfd, skip_last);
	IRIS_LOGI("cm color temperature value=%d", value);
	return len;
}

u32 iris_cm_ratio_set_for_iic(void)
{
	u32 tablesel;
	u32 index;
	u32 xvalue;
	u32 ratio;
	u32 value;
	u32 regvalue = 0;
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;

	value = pqlt_cur_setting->colortempvalue;

	if (value > iris_max_color_temp)
		value = iris_max_color_temp;
	else if (value < iris_min_color_temp)
		value = iris_min_color_temp;
	index = (value - IRIS_CCT_MIN_VALUE) / 25;
	xvalue = iris_color_temp_x_get(index);

	if (xvalue == iris_min_x_value) {
		tablesel = 0;
		regvalue = tablesel | 0x02;
	} else if ((xvalue < iris_min_x_value) && (xvalue >= IRIS_X_7700K)) {
		tablesel = 0;
		ratio = ((xvalue - IRIS_X_7700K) * 16383) / (iris_min_x_value - IRIS_X_7700K);
		regvalue = tablesel | (ratio << 16);
	} else if ((xvalue >= iris_max_x_value) && (xvalue < IRIS_X_7700K)) {
		tablesel = 1;
		ratio = ((xvalue - iris_max_x_value) * 16383) / (IRIS_X_7700K - iris_max_x_value);
		regvalue = tablesel | (ratio << 16);
	}

	IRIS_LOGD("cm color temperature value=%d", value);

	return regvalue;
}

void iris_cm_colortemp_mode_set(u32 mode)
{
	struct iris_update_regval regval;
	bool skiplast = 0;
	int len = 0;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;


	/*do not generate lut table during mode switch.*/
	if (pqlt_cur_setting->source_switch != 1) {
		iris_init_ipopt_ip(popt,  IP_OPT_MAX);
		regval.ip = IRIS_IP_DPP;
		regval.opt_id = 0xfc;
		regval.mask = 0x00000031;
		regval.value = (mode == 0)?0x00000020:0x00000011;
		len = iris_capture_disable_pq(popt, &skiplast);
		if (mode == IRIS_COLOR_TEMP_OFF)
			iris_update_ip_opt(popt, IP_OPT_MAX, GAMMA_LUT,
					0x00, 0x01);
		else
			iris_update_ip_opt(popt, IP_OPT_MAX, GAMMA_LUT,
					pqlt_cur_setting->pq_setting.cmcolorgamut + 0x01, 0x01);

		if (mode > IRIS_COLOR_TEMP_OFF)
			len = iris_cm_ratio_set(popt, 0x01);

		if (pqlt_cur_setting->source_switch == 2)
			pqlt_cur_setting->source_switch = 0;

		iris_update_bitmask_regval_nonread(&regval, false);
		len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_DPP,
				0xfc, 0xfc, skiplast);

		len = iris_capture_enable_pq(popt, len);

		is_ulps_enable = iris_disable_ulps(path);
		iris_update_pq_opt(popt, len, path);
		iris_enable_ulps(path, is_ulps_enable);
	}
	IRIS_LOGI("cm color temperature mode=%d, len=%d", mode, len);
}

void iris_cm_color_temp_set(void)
{
	bool skiplast = 0;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;
	/*struct quality_setting *pqlt_cur_setting = & iris_setting.quality_cur;*/

	/*if(pqlt_cur_setting->pq_setting.cmcolorgamut == 0) {*/
	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);
	len = iris_cm_ratio_set(popt, skiplast);

	len = iris_capture_enable_pq(popt, len);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
	/*}*/
	IRIS_LOGI("%s, len = %d",  __func__, len);
}

void iris_cm_color_gamut_pre_set(u32 source_switch)
{
	struct iris_update_regval regval;
	bool skiplast = 0;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	/*add protection for source and scene switch at the same time*/
	if (source_switch == 3)
		source_switch = 1;
	pqlt_cur_setting->source_switch = source_switch;
	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);

	regval.ip = IRIS_IP_DPP;
	regval.opt_id = 0xfc;
	regval.mask = 0x00000011;
	regval.value = 0x00000000;

	iris_update_bitmask_regval_nonread(&regval, false);
	len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_DPP,
			0xfc, 0xfc, skiplast);
	len = iris_capture_enable_pq(popt, len);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);

	IRIS_LOGI("source switch = %d, len=%d", source_switch, len);
}

void iris_cm_color_gamut_set(u32 level)
{
	struct iris_update_regval regval;
	bool skiplast = 0;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;
	u32 gammalevel;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);

	regval.ip = IRIS_IP_DPP;
	regval.opt_id = 0xfc;
	regval.mask = 0x00000011;
	regval.value = 0x00000011;

	/*use liner gamma if cm lut disable*/
	if (pqlt_cur_setting->pq_setting.cmcolortempmode ==
			IRIS_COLOR_TEMP_OFF)
		gammalevel = 0;
	else
		gammalevel = level + 1;


	iris_update_ip_opt(popt, IP_OPT_MAX, GAMMA_LUT, gammalevel, 0x01);
	iris_update_ip_opt(popt, IP_OPT_MAX, CM_LUT, level * 3 + 0, 0x01);
	iris_update_ip_opt(popt, IP_OPT_MAX, CM_LUT, level * 3 + 1, 0x01);
	len = iris_update_ip_opt(
			popt, IP_OPT_MAX, CM_LUT,
			level*3 + 2, (pqlt_cur_setting->source_switch == 0) ? 0x01 : skiplast);

	/*do not generate lut table for source switch.*/
	if (pqlt_cur_setting->source_switch == 0) {
		iris_update_bitmask_regval_nonread(&regval, false);
		len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_DPP,
				0xfc, 0xfc, skiplast);
	}
	len = iris_capture_enable_pq(popt, len);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
	IRIS_LOGI("cm color gamut=%d, len=%d", level, len);
}

void iris_cm_color_gamut_pre_clear(void)
{
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;

	if (pqlt_cur_setting->source_switch != 0) {
		pqlt_cur_setting->source_switch = 0;
		iris_cm_color_gamut_set(
				iris_setting.quality_cur.pq_setting.cmcolorgamut);
		iris_cm_colortemp_mode_set(
				iris_setting.quality_cur.pq_setting.cmcolortempmode);
	}
}

void iris_dpp_gamma_set(void)
{
	bool skiplast = 0;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;
	u32 gammalevel;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);

	if (pqlt_cur_setting->pq_setting.cmcolortempmode
			== IRIS_COLOR_TEMP_OFF)
		gammalevel = 0; /*use liner gamma if cm lut disable*/
	else
		gammalevel = pqlt_cur_setting->pq_setting.cmcolorgamut + 1;

	len = iris_update_ip_opt(popt, IP_OPT_MAX, GAMMA_LUT,
			gammalevel, skiplast);

	len = iris_capture_enable_pq(popt, len);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
}

static int iris_lce_gamm1k_set(
		struct iris_update_ipopt *popt, uint8_t skip_last)
{
	u32 dwLux_i, dwnBL_AL, dwGain, dwAHEGAMMA1K;
	u32 level;
	struct iris_update_regval regval;
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;
	int len = 0;
	uint32_t  *payload = NULL;

	level = (pqlt_cur_setting->pq_setting.lcemode) * 6
		+ pqlt_cur_setting->pq_setting.lcelevel;
	payload = iris_get_ipopt_payload_data(IRIS_IP_LCE, level, 5);

	if (pqlt_cur_setting->pq_setting.alenable == true) {
		if (pqlt_cur_setting->luxvalue > 20000)
			pqlt_cur_setting->luxvalue = 20000;

		if (pqlt_cur_setting->luxvalue >= (m_dwTH_LuxAdj * 8))
			dwLux_i = MIN(8191,
					(8 * m_dwTH_LuxAdj + pqlt_cur_setting->luxvalue / 8 - m_dwTH_LuxAdj));
		else
			dwLux_i = pqlt_cur_setting->luxvalue;

		if (dwLux_i <= m_dwXLuxBuffer[0])
			dwnBL_AL = MIN(m_dwGLuxBuffer[0],
					m_dwBLux + (m_dwKLuxBuffer[0] * dwLux_i) / 64);
		else if (dwLux_i <= m_dwXLuxBuffer[1])
			dwnBL_AL = MIN(m_dwGLuxBuffer[1],
					m_dwGLuxBuffer[0] + (m_dwKLuxBuffer[1] * (dwLux_i - m_dwXLuxBuffer[0])) / 64);
		else if (dwLux_i <= m_dwXLuxBuffer[2])
			dwnBL_AL = MIN(m_dwGLuxBuffer[2],
					m_dwGLuxBuffer[1] + (m_dwKLuxBuffer[2] * (dwLux_i - m_dwXLuxBuffer[1])) / 64);
		else if (dwLux_i <= m_dwXLuxBuffer[3])
			dwnBL_AL = MIN(m_dwGLuxBuffer[3],
					m_dwGLuxBuffer[2] + (m_dwKLuxBuffer[3] * (dwLux_i - m_dwXLuxBuffer[2])) / 64);
		else if (dwLux_i <= m_dwXLuxBuffer[4])
			dwnBL_AL = MIN(m_dwGLuxBuffer[4],
					m_dwGLuxBuffer[3] + (m_dwKLuxBuffer[4] * (dwLux_i - m_dwXLuxBuffer[3])) / 64);
		else
			dwnBL_AL = MIN(m_dwGLuxBuffer[5],
					m_dwGLuxBuffer[4] + (m_dwKLuxBuffer[5] * (dwLux_i - m_dwXLuxBuffer[4])) / 64);

		if (dwnBL_AL < 1024)
			dwGain = 0;
		else
			dwGain = dwnBL_AL - 1024;

		//max lux = 10000 ->  974, max lux = 20000 -> 976,
		//max lux = 30000 -> 976,
		dwAHEGAMMA1K = (dwGain * 90) / 976;
		regval.ip = IRIS_IP_LCE;
		regval.opt_id = 0xfd;
		regval.mask = 0xffffffff;
		regval.value = ((*payload) & 0x00ffffff) | (dwAHEGAMMA1K << 24);
		iris_update_bitmask_regval_nonread(&regval, false);
		len = iris_init_update_ipopt_t(
				popt, IP_OPT_MAX, IRIS_IP_LCE,
				0xfd, 0xfd, skip_last);
	}
	IRIS_LOGI("lux value=%d", pqlt_cur_setting->luxvalue);
	return len;
}

static int iris_lce_gamm1k_restore(
		struct iris_update_ipopt *popt, uint8_t skip_last)
{
	u32 level;
	struct iris_update_regval regval;
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;
	int len = 0;
	uint32_t  *payload = NULL;


	level = (pqlt_cur_setting->pq_setting.lcemode) * 6
		+ pqlt_cur_setting->pq_setting.lcelevel;
	payload = iris_get_ipopt_payload_data(IRIS_IP_LCE, level, 5);

	regval.ip = IRIS_IP_LCE;
	regval.opt_id = 0xfd;
	regval.mask = 0xffffffff;
	regval.value = *payload;
	iris_update_bitmask_regval_nonread(&regval, false);
	len = iris_init_update_ipopt_t(
			popt, IP_OPT_MAX, IRIS_IP_LCE,
			0xfd, 0xfd, skip_last);

	IRIS_LOGI("%s, len = %d", __func__, len);
	return len;
}

void iris_lce_mode_set(u32 mode)
{
	u32 locallevel;
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;
	bool skiplast = 0;
	int len = 0;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_lce(popt, &skiplast);

	if (mode == IRIS_LCE_GRAPHIC)
		locallevel = pqlt_cur_setting->pq_setting.lcelevel;
	else
		locallevel = pqlt_cur_setting->pq_setting.lcelevel + 6;

	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_LCE, locallevel, 0x01);

	if (pqlt_cur_setting->pq_setting.alenable == true
			&& pqlt_cur_setting->pq_setting.sdr2hdr == SDR2HDR_Bypass)
		len = iris_lce_gamm1k_set(popt, skiplast);
	else
		len = iris_lce_gamm1k_restore(popt, skiplast);

	len = iris_capture_enable_lce(popt, len);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);

	IRIS_LOGI("lce mode=%d, len=%d", mode, len);
}

void iris_lce_level_set(u32 level)
{
	u32 locallevel;
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;
	bool skiplast = 0;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_lce(popt, &skiplast);

	if (pqlt_cur_setting->pq_setting.lcemode
			== IRIS_LCE_GRAPHIC)
		locallevel = level;
	else
		locallevel = level + 6;

	len = iris_update_ip_opt(
			popt, IP_OPT_MAX, IRIS_IP_LCE, locallevel, 0x01);

	/* hdr's al may use lce */
	if (pqlt_cur_setting->pq_setting.alenable == true
			&& pqlt_cur_setting->pq_setting.sdr2hdr == SDR2HDR_Bypass)
		len = iris_lce_gamm1k_set(popt, skiplast);
	else
		len = iris_lce_gamm1k_restore(popt, skiplast);

	len = iris_capture_enable_lce(popt, len);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);

	IRIS_LOGI("lce level=%d, len=%d", level, len);
}

void iris_lce_graphic_det_set(bool enable)
{
	u32 locallevel;
	bool skiplast = 0;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_lce(popt, &skiplast);

	locallevel = 0x10 | (u8)enable;

	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_LCE,
			locallevel, skiplast);

	len = iris_capture_enable_lce(popt, len);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
	IRIS_LOGI("lce graphic det=%d, len=%d", enable, len);
}

void iris_lce_al_set(bool enable)
{
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	if (enable == true)
		iris_lce_lux_set();
	else {
		bool skiplast = 0;
		int len;
		struct iris_update_ipopt popt[IP_OPT_MAX];

		iris_init_ipopt_ip(popt,  IP_OPT_MAX);
		len = iris_capture_disable_lce(popt, &skiplast);
		len = iris_lce_gamm1k_restore(popt, skiplast);

		len = iris_capture_enable_lce(popt, len);
		is_ulps_enable = iris_disable_ulps(path);
		iris_update_pq_opt(popt, len, path);
		iris_enable_ulps(path, is_ulps_enable);
		IRIS_LOGI("%s, len = %d", __func__, len);
	}
	IRIS_LOGI("lce al enable=%d", enable);
}

void iris_lce_demo_window_set(u32 vsize, u32 hsize, u8 inwnd)
{
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;
	bool skiplast = 0;
	int len = 0;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	uint32_t  *payload = NULL;
	bool is_ulps_enable = 0;
	u32 level;
	int data;
	uint8_t path = iris_pq_update_path;

	IRIS_LOGI("%s E %d %d %d", __func__, vsize, hsize, inwnd);

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_lce(popt, &skiplast);

	// RESERVED
	payload = iris_get_ipopt_payload_data(IRIS_IP_LCE, 0xF0, 2);
	// payload[0]: RESERVED,
	// [11:0] = DEMO_HSIZE, 0x00000FFF
	// [27:16] = DEMO_VSIZE, 0x0FFF0000
	data = (payload[0] & (~0x0FFF0FFF)) | ((vsize & 0x0FFF) << 16) | (hsize & 0x0FFF);
	iris_set_ipopt_payload_data(IRIS_IP_LCE, 0xF0, 2, data);
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_LCE, 0xF0, 0x01);

	// NR_CTRL
	level = (pqlt_cur_setting->pq_setting.lcemode) * 6
		+ pqlt_cur_setting->pq_setting.lcelevel;
	payload = iris_get_ipopt_payload_data(IRIS_IP_LCE, level, 2);
	// BFRINC[17:10]
	// bit14 = DEMO_H_EN
	// bit15 = DEMO_V_EN
	// bit17 = WND_CHANGE, 0: in window, 1: out of window
	data = (payload[12] & (~0x0002C000)) |
		((hsize != 0 ? 1:0) << 14) |
		((vsize != 0 ? 1:0) << 15) |
		((inwnd & 0x01) << 17);
	iris_set_ipopt_payload_data(IRIS_IP_LCE, level, 14, data);
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_LCE, level, skiplast);

	len = iris_capture_enable_lce(popt, len);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);

	IRIS_LOGI("%s X %d %d %d, len=%d", __func__, vsize, hsize, inwnd, len);
}


void iris_dbc_level_set(u32 level)
{
	u32 locallevel;
	u32 dbcenable;
	bool skiplast = 0;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	u8 localindex;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_lce(popt, &skiplast);

	dbcenable = (level > 0) ? 0x01 : 0x00;
	locallevel = 0x10 | level;

	iris_dbc_lut_index ^= 1;

	IRIS_LOGI("send A/B  %d", iris_dbc_lut_index);

	localindex = 0x20 | iris_dbc_lut_index;
	iris_update_ip_opt(popt, IP_OPT_MAX, DBC_LUT,
			DBC_OFF + level, 0x01);
	iris_init_update_ipopt_t(popt, IP_OPT_MAX, DBC_LUT,
			CABC_DLV_OFF + level, CABC_DLV_OFF + level, 0x01);

	iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_DBC,
			localindex, localindex, 0x01);
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_DBC,
			dbcenable, skiplast);
	/*len = iris_update_ip_opt(popt,  IP_OPT_MAX, IRIS_IP_DBC, locallevel, skiplast);*/
	len = iris_capture_enable_lce(popt, len);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);

	IRIS_LOGI("dbc level=%d, len=%d", level, len);
}

void iris_reading_mode_set(u32 level)
{
	u32 locallevel, locallevel_cm;
	bool skiplast = 0;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;
	// struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	//bool cm_csc_enable = true;

	/*only take affect when sdr2hdr bypass */
	if (pqlt_cur_setting->pq_setting.sdr2hdr == SDR2HDR_Bypass) {
		iris_init_ipopt_ip(popt,  IP_OPT_MAX);
		len = iris_capture_disable_pq(popt, &skiplast);

		locallevel = 0x40 | level;
		// FIXME: WA for old pxlw-dtsi
		// locallevel_cm = (pcfg->dual_setting ? 0x40 : 0x48) | level;
		locallevel_cm = 0x40 | level;

		//if ((level == 0) &&
		//	(pqlt_cur_setting->pq_setting.cm6axis == 0))
		//	cm_csc_enable = false;

		//len = iris_cm_csc_set(popt, 0x01, cm_csc_enable);

		iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_CM,
				locallevel_cm, 0x01);
		len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_DPP,
				locallevel, skiplast);

		len = iris_capture_enable_pq(popt, len);
		is_ulps_enable = iris_disable_ulps(path);
		iris_update_pq_opt(popt, len, path);
		iris_enable_ulps(path, is_ulps_enable);
	}
	IRIS_LOGI("reading mode=%d", level);
}

void iris_lce_lux_set(void)
{
	bool skiplast = 0;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_lce(popt, &skiplast);
	len = iris_lce_gamm1k_set(popt, skiplast);

	len = iris_capture_enable_lce(popt, len);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
	IRIS_LOGD("%s, len = %d", __func__, len);
}

void iris_ambient_light_lut_set(uint32_t lut_offset)
{
#define AL_SDR2HDR_INDEX_OFFSET 7
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;
	bool skiplast = 0;
	uint32_t  *payload = NULL;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);
	if (pqlt_cur_setting->pq_setting.alenable == true) {

		if (pqlt_cur_setting->pq_setting.sdr2hdr >= HDR10In_ICtCp &&
				pqlt_cur_setting->pq_setting.sdr2hdr <= ICtCpIn_YCbCr) {

			if (!(iris_sdr2hdr_lut2ctl & 0xFFE00000)) {
				payload = iris_get_ipopt_payload_data(IRIS_IP_SDR2HDR, (AL_SDR2HDR_INDEX_OFFSET + 2), 83);
				iris_sdr2hdr_lut2ctl = payload[0];
				iris_sdr2hdr_lut2ctl = ((lut_offset<<19) | (0x17FFFF & iris_sdr2hdr_lut2ctl));
				iris_set_ipopt_payload_data(IRIS_IP_SDR2HDR, (AL_SDR2HDR_INDEX_OFFSET + 2), 83, iris_sdr2hdr_lut2ctl);
			}

			if (!(iris_sdr2hdr_lutyctl & 0xFFFF0000)) {
				payload = iris_get_ipopt_payload_data(IRIS_IP_SDR2HDR, (AL_SDR2HDR_INDEX_OFFSET + 2), 23);
				iris_sdr2hdr_lutyctl = payload[0];
			}

			iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_SDR2HDR,
					(AL_SDR2HDR_INDEX_OFFSET + 2), 0x01);

			len = iris_update_ip_opt(popt, IP_OPT_MAX, AMBINET_SDR2HDR_LUT,
					0x0, skiplast);
			payload = iris_get_ipopt_payload_data(IRIS_IP_SDR2HDR, (AL_SDR2HDR_INDEX_OFFSET + 2), 83);
		} else if (pqlt_cur_setting->pq_setting.sdr2hdr >= SDR709_2_709 &&
				pqlt_cur_setting->pq_setting.sdr2hdr <= SDR709_2_2020) {

			if (!(iris_sdr2hdr_lut2ctl & 0xFFE00000)) {
				payload = iris_get_ipopt_payload_data(IRIS_IP_SDR2HDR, (AL_SDR2HDR_INDEX_OFFSET + 5), 83);
				iris_sdr2hdr_lut2ctl = payload[0];
				iris_sdr2hdr_lut2ctl = ((lut_offset<<19) | (0x17FFFF & iris_sdr2hdr_lut2ctl));
				iris_set_ipopt_payload_data(IRIS_IP_SDR2HDR, (AL_SDR2HDR_INDEX_OFFSET + 5), 83, iris_sdr2hdr_lut2ctl);
			}
			if (!(iris_sdr2hdr_lutyctl & 0xFFFF0000)) {
				payload = iris_get_ipopt_payload_data(IRIS_IP_SDR2HDR, (AL_SDR2HDR_INDEX_OFFSET + 5), 23);
				iris_sdr2hdr_lutyctl = payload[0];
			}
			iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_SDR2HDR,
					(AL_SDR2HDR_INDEX_OFFSET + 5), 0x01);
			//(AL_SDR2HDR_INDEX_OFFSET +
			//	pqlt_cur_setting->pq_setting.sdr2hdr), 0x01);
			len = iris_update_ip_opt(popt, IP_OPT_MAX, AMBINET_SDR2HDR_LUT,
					0x0, skiplast);
			payload = iris_get_ipopt_payload_data(IRIS_IP_SDR2HDR, (AL_SDR2HDR_INDEX_OFFSET + 5), 83);
		}
	} else {
		u32 hdr_lut_idx;

		iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_SDR2HDR,
				pqlt_cur_setting->pq_setting.sdr2hdr, 0x01);

		if (!(iris_sdr2hdr_lut2ctl&0xFFE00000)) {
			payload = iris_get_ipopt_payload_data(IRIS_IP_SDR2HDR, pqlt_cur_setting->pq_setting.sdr2hdr, 83);
			iris_sdr2hdr_lut2ctl = payload[0];
		}
		if (!(iris_sdr2hdr_lutyctl&0xFFFF0000)) {
			payload = iris_get_ipopt_payload_data(IRIS_IP_SDR2HDR, pqlt_cur_setting->pq_setting.sdr2hdr, 23);
			iris_sdr2hdr_lutyctl = payload[0];
		}

		if (pqlt_cur_setting->pq_setting.sdr2hdr >= HDR10In_ICtCp && pqlt_cur_setting->pq_setting.sdr2hdr <= ICtCpIn_YCbCr) {
			hdr_lut_idx = ((pqlt_cur_setting->pq_setting.sdr2hdr-1) & 0xf) | ((SDR2HDR_LUT0 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((pqlt_cur_setting->pq_setting.sdr2hdr-1) & 0xf) | ((SDR2HDR_UVY0 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((pqlt_cur_setting->pq_setting.sdr2hdr-1) & 0xf) | ((SDR2HDR_UVY1 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((pqlt_cur_setting->pq_setting.sdr2hdr-1) & 0xf) | ((SDR2HDR_UVY2 << 4) & 0xf0);
			len = iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, skiplast);
		} else if (pqlt_cur_setting->pq_setting.sdr2hdr >= SDR709_2_709 && pqlt_cur_setting->pq_setting.sdr2hdr <= SDR709_2_2020) {
			hdr_lut_idx = ((pqlt_cur_setting->pq_setting.sdr2hdr-1) & 0xf) | ((SDR2HDR_LUT0 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((pqlt_cur_setting->pq_setting.sdr2hdr-1) & 0xf) | ((SDR2HDR_LUT1 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((pqlt_cur_setting->pq_setting.sdr2hdr-1) & 0xf) | ((SDR2HDR_UVY0 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((pqlt_cur_setting->pq_setting.sdr2hdr-1) & 0xf) | ((SDR2HDR_UVY1 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((pqlt_cur_setting->pq_setting.sdr2hdr-1) & 0xf) | ((SDR2HDR_UVY2 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((pqlt_cur_setting->pq_setting.sdr2hdr-1) & 0xf) | ((SDR2HDR_INV_UV0 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((pqlt_cur_setting->pq_setting.sdr2hdr-1) & 0xf) | ((SDR2HDR_INV_UV1 << 4) & 0xf0);
			len = iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, skiplast);
		} else {
			hdr_lut_idx = ((pqlt_cur_setting->pq_setting.sdr2hdr-1) & 0xf) | ((SDR2HDR_LUT0 << 4) & 0xf0);
			len = iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, skiplast);
		}
	}

	len = iris_capture_enable_pq(popt, len);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
	IRIS_LOGD("al=%d hdr level=%d",
			pqlt_cur_setting->pq_setting.alenable,
			pqlt_cur_setting->pq_setting.sdr2hdr);
}

void iris_maxcll_lut_set(u32 lutpos)
{
	int len;
	struct iris_update_regval regval;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool skiplast = 0;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);

	if (!(iris_sdr2hdr_lutyctl & 0xFFFF0000)) {
		len = iris_update_ip_opt(popt, IP_OPT_MAX, AMBINET_HDR_GAIN, 0x0, 1);

		regval.ip = IRIS_IP_SDR2HDR;
		regval.opt_id = 0x92;
		regval.mask = 0x0000FFFF;
		regval.value = ((lutpos << 6) & 0xc0) | (iris_sdr2hdr_lutyctl & 0xFF3F);
		iris_update_bitmask_regval_nonread(&regval, false);

		iris_sdr2hdr_lutyctl_set(lutpos);
		len += iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_SDR2HDR,
				0x92, 0x92, skiplast);
	} else
		len = iris_update_ip_opt(popt, IP_OPT_MAX, AMBINET_HDR_GAIN, 0x0, skiplast);

	len = iris_capture_enable_pq(popt, len);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
	IRIS_LOGD("%s, len=%d", __func__, len);
}


void iris_dbclce_datapath_set(bool bEn)
{
	bool skiplast = 0;
	int len;
	u32 pwil_datapath;
	uint32_t  *payload = NULL;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);

	if (!iris_lce_power_status_get())
		bEn = 0;/*cannot enable lce dbc data path if power off*/

	payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 2);
	pwil_datapath = (payload[2] & (~0x1)) | ((bEn == true) ? 0x00000001 : 0x00000000);
	iris_set_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 4, pwil_datapath);
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL,
			0xF1, skiplast);

	if ((!iris_capture_ctrl_en)
			&& (!iris_dynamic_power_get())
			&& (!iris_debug_cap))
		len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_PWIL,
				0x30, 0x30, 1);
	len = iris_capture_enable_pq(popt, len);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
	IRIS_LOGI("dbclce_en=%d, len=%d", bEn, len);
}


void iris_dbclce_power_set(bool bEn)
{
	iris_lce_power_status_set(bEn);
	iris_pmu_lce_set(bEn);
}

void iris_sdr2hdr_level_set(u32 level)
{
	struct iris_update_regval regval;
	u32 pwil_channel_order;
	u32 pwil_csc;
	u32 pwil_ctrl1, pwil_datapath, pwil_datapath1;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	u32 cm_csc = 0x40;
	bool cm_csc_enable = true;
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;
	u32 peaking_csc;
	bool skiplast = 0;
	u32 hdr_lut_idx = 0x01;
	uint32_t  *payload = NULL;
	u32 gammalevel;
	bool dma_sent = false;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;
	// struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	if ((!iris_dynamic_power_get()) && (iris_capture_ctrl_en == true))
		skiplast = 1;

	if (pqlt_cur_setting->pq_setting.readingmode != 0)
		cm_csc = 0x41;

	if ((level <= ICtCpIn_YCbCr) && (level >= HDR10In_ICtCp)) {
		/*Not change iris_require_yuv_input due to magic code in ioctl.*/
		shadow_iris_HDR10 = true;
	} else if (level == SDR709_2_709) {
		shadow_iris_require_yuv_input = true;
		shadow_iris_HDR10 = false;
	} else {
		shadow_iris_require_yuv_input = false;
		shadow_iris_HDR10 = false;
		shadow_iris_HDR10_YCoCg = false;
	}

	if (level >= HDR10In_ICtCp && level <= SDR709_2_2020) {
		iris_yuv_datapath = true;
		//sdr2hdr level in irisconfigure start from 1,
		//but lut table start from 0, so need -1.
		//iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_EXT,
		//		0x60 + level - 1, 0x01);
		hdr_lut_idx = ((level-1) & 0xf) | ((SDR2HDR_LUT0 << 4) & 0xf0);
		iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
		if (level >= HDR10In_ICtCp && level <= ICtCpIn_YCbCr) {
			hdr_lut_idx = ((level-1) & 0xf) | ((SDR2HDR_UVY0 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((level-1) & 0xf) | ((SDR2HDR_UVY1 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((level-1) & 0xf) | ((SDR2HDR_UVY2 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((level-1) & 0xf) | ((SDR2HDR_INV_UV0 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((level-1)&0xf) | ((SDR2HDR_INV_UV1 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
		} else if (level >= SDR709_2_709 && level <= SDR709_2_2020) {
			hdr_lut_idx = ((level-1) & 0xf) | ((SDR2HDR_LUT1 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((level-1) & 0xf) | ((SDR2HDR_UVY0 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((level-1) & 0xf) | ((SDR2HDR_UVY1 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((level-1) & 0xf) | ((SDR2HDR_INV_UV0 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
			hdr_lut_idx = ((level-1) & 0xf) | ((SDR2HDR_INV_UV1 << 4) & 0xf0);
			iris_update_ip_opt(popt, IP_OPT_MAX, SDR2HDR_LUT, hdr_lut_idx, 0x01);
		}
	} else if (level == SDR2HDR_Bypass) {
		iris_yuv_datapath = false;
	} else {
		IRIS_LOGE("%s something is  wrong level=%d", __func__, level);
		return;
	}

	if ((level <= ICtCpIn_YCbCr)
			&& (level >= HDR10In_ICtCp)) {
		/*channel order YUV, ICtCp*/
		pwil_channel_order = 0x60;
		if (level == ICtCpIn_YCbCr)
			pwil_csc = 0x91;/*pwil csc ICtCp to RGB. */
		else
			pwil_csc = 0x90;/*pwil csc YUV to RGB*/
		pwil_datapath = 0x4008000;
		pwil_datapath1 = 0;
		if (shadow_iris_HDR10_YCoCg) {/*RGBlike solution*/
			pwil_channel_order = 0x62;
			pwil_csc = 0x93;
			pwil_datapath1 = 0x8000;
		}
	} else if (level == SDR709_2_709) {
		pwil_channel_order = 0x60;/*channel order YUV*/
		pwil_csc = 0x92;/*pwil csc YUV to YUVFull */
		pwil_datapath = 0x4010000;
		pwil_datapath1 = 0;
	} else {
		pwil_channel_order = 0x61;/*channel order RGB*/
		pwil_csc = 0x93;/*pwil csc default*/
		if (level == SDR2HDR_Bypass) {
			pwil_csc = 0x95;
			pwil_datapath = 0;
		} else {
			pwil_datapath = 0x4008000;
		}
		pwil_datapath1 = 0x8000;
	}
	switch (level) {
	case HDR10In_ICtCp:
	case HDR10In_YCbCr:
	case ICtCpIn_YCbCr:
		cm_csc = 0x45;
		break;
	case SDR709_2_709:
		/*TODOS*/
		break;
	case SDR709_2_p3:
		cm_csc = 0x46;
		break;
	case SDR709_2_2020:
		/*TODOS*/
		break;
	default:
		break;
	}

	iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL,
			pwil_channel_order, 0x01);
	iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL,
			pwil_csc, 0x01);

	payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 2);
	IRIS_LOGI("pwil ctrl1 %x", payload[0]);
	pwil_datapath = (payload[2] & (~0x4018000)) | pwil_datapath;  //CSC_MODE 18000  POST_PQ_PROCESS_EN 4000000
	pwil_datapath1 = (payload[3] & (~0x8020)) | pwil_datapath1;   //UNPACK_BYPASS_EN
	pwil_ctrl1 = (payload[1] & (~0x40)) | (iris_yuv_datapath ? 0x40 : 0x00);
	iris_set_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 3, pwil_ctrl1);
	iris_set_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 4, pwil_datapath);
	iris_set_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 5, pwil_datapath1);
	iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL,
			0xF1, 0x01);

	iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_SDR2HDR,
			level, 0x01);

	if (!(iris_sdr2hdr_lut2ctl & 0xFFE00000)) {
		payload = iris_get_ipopt_payload_data(IRIS_IP_SDR2HDR, level, 83);
		iris_sdr2hdr_lut2ctl = payload[0];
	}
	if (!(iris_sdr2hdr_lutyctl & 0xFFFF0000)) {
		payload = iris_get_ipopt_payload_data(IRIS_IP_SDR2HDR, level, 23);
		iris_sdr2hdr_lutyctl = payload[0];
	}

	// FIXME: WA for old pxlw-dtsi
	//if (!pcfg->dual_setting)
	//	cm_csc += 8;
	iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_CM,
			cm_csc, 0x01);

	/*add the workaround to let hdr and cm lut */
	/*table take affect at the same time*/
	if (pqlt_cur_setting->pq_setting.cmcolortempmode > IRIS_COLOR_TEMP_OFF)
		/*change ratio when source switch*/
		len = iris_cm_ratio_set(popt, 0x01);

	regval.ip = IRIS_IP_DPP;
	regval.opt_id = 0xfc;
	regval.mask = 0x00000031;
	regval.value = (pqlt_cur_setting->pq_setting.cmcolortempmode == 0)
		? 0x00000020 : 0x00000011;

	iris_update_bitmask_regval_nonread(&regval, false);
	len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_DPP,
			0xfc, 0xfc, 0x01);

	if (pqlt_cur_setting->source_switch == 1) {
		/*use liner gamma if cm lut disable*/
		if (pqlt_cur_setting->pq_setting.cmcolortempmode ==
				IRIS_COLOR_TEMP_OFF)
			gammalevel = 0;
		else
			gammalevel = pqlt_cur_setting->pq_setting.cmcolorgamut + 1;

		len = iris_update_ip_opt(popt, IP_OPT_MAX, GAMMA_LUT,
				gammalevel, 0x01);
	}

	if (iris_yuv_datapath == true)
		peaking_csc = 0x11;
	else {
		if ((pqlt_cur_setting->pq_setting.readingmode == 0)
				&& (pqlt_cur_setting->pq_setting.cm6axis == 0))
			cm_csc_enable = false;

		if (pqlt_cur_setting->pq_setting.peaking == 0)
			peaking_csc = 0x11;
		else
			peaking_csc = 0x10;
	}

	//len = iris_cm_csc_set(popt, 0x01, cm_csc_enable);
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PEAKING,
			peaking_csc, skiplast);

	if (!iris_dynamic_power_get() && (iris_capture_ctrl_en == true)) {
		len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_DMA,
				0xe2, 0xe2, 0);
		dma_sent = true;
	}

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);

	if (dma_sent) {
		IRIS_LOGD("AP csc prepare.");
		iris_require_yuv_input = shadow_iris_require_yuv_input;
		iris_HDR10 = shadow_iris_HDR10;
		iris_HDR10_YCoCg = shadow_iris_HDR10_YCoCg;
		pqlt_cur_setting->dspp_dirty = 1;
	}

	pqlt_cur_setting->source_switch = 0;
	IRIS_LOGI("sdr2hdr level =%d, len = %d", level, len);
}

u32 iris_sdr2hdr_lut2ctl_get(void)
{
	if (iris_sdr2hdr_lut2ctl & 0xFFE00000)
		return 0xFFE00000;
	else if (iris_sdr2hdr_lut2ctl)
		return (iris_sdr2hdr_lut2ctl & 0x80000)>>19;
	else
		return 15;
}

void iris_sdr2hdr_lut2ctl_set(u32 value)
{
	IRIS_LOGI("Iris LUT2 Pos %d", value);

	if (value == 0xFFE00000) {
		iris_sdr2hdr_lut2ctl = 0xFFE00000;
		return;
	} else if (value)
		value = 1;

	iris_sdr2hdr_lut2ctl = ((value << 19) | (0x17FFFF & iris_sdr2hdr_lut2ctl));
}

u32 iris_sdr2hdr_lutyctl_get(void)
{
	if (iris_sdr2hdr_lutyctl & 0xFFFF0000)
		return 0xFFFF0000;
	else if (iris_sdr2hdr_lutyctl)
		return (iris_sdr2hdr_lutyctl & 0xC0)>>6;
	else
		return 15;
}

void iris_sdr2hdr_lutyctl_set(u32 value)
{
	if (value == 0xFFFF0000)
		return;
	else if (value)
		value = 1;

	iris_sdr2hdr_lutyctl = ((value << 6) | (0xFF3F & iris_sdr2hdr_lutyctl));
	IRIS_LOGI("iris_sdr2hdr_lutyctl %x", iris_sdr2hdr_lutyctl);
}

void iris_peaking_idle_clk_enable(bool enable)
{
	u32 locallevel;
	bool skiplast = 0;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	locallevel = 0xa0 | (u8)enable;
	len = iris_capture_disable_pq(popt, &skiplast);

	len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_PEAKING,
			locallevel, locallevel, skiplast);

	len = iris_capture_enable_pq(popt, len);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
	IRIS_LOGD("peaking idle clk enable=%d", enable);
}

void iris_cm_6axis_seperate_gain(u8 gain_type, u32 value)
{
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool skiplast = 0;
	int len;
	struct iris_update_regval regval;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);

	len = iris_capture_disable_pq(popt, &skiplast);

	regval.ip = IRIS_IP_CM;
	/*6 axis separate gain id start from 0xb0*/
	regval.opt_id = 0xb0 + gain_type;
	regval.mask = 0x00fc0000;
	regval.value = value << 18;

	iris_update_bitmask_regval_nonread(&regval, false);
	len  = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_CM,
			regval.opt_id, regval.opt_id, skiplast);

	len = iris_capture_enable_pq(popt, len);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);

	IRIS_LOGI("cm gain type = %d, value = %d, len = %d",
			gain_type, value, len);
}

void iris_pwm_freq_set(u32 value)
{
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	struct iris_update_regval regval;
	u32 regvalue = 0;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt, IP_OPT_MAX);

	regvalue = 1000000 / value;
	regvalue = (27000000 / 1024) / regvalue;

	regval.ip = IRIS_IP_PWM;
	regval.opt_id = 0xfd;
	regval.mask = 0xffffffff;
	regval.value = regvalue;

	iris_update_bitmask_regval_nonread(&regval, false);
	len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_PWM,
			0xfd, 0xfd, 0);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);

	IRIS_LOGI("%s, blc_pwm freq=%d", __func__, regvalue);
}

void iris_pwm_enable_set(bool enable)
{
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);

	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWM, enable, 0);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
	IRIS_LOGI("%s, blc_pwm enable=%d", __func__, enable);
}

void iris_dbc_bl_user_set(u32 value)
{
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	struct iris_update_regval regval;
	u32 regvalue = 0;
	bool skiplast = 0;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	if (iris_setting.quality_cur.pq_setting.dbc == 0)
		return;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	/*if ((!iris_dynamic_power_get()) */
	/*&& (iris_lce_power_status_get()))*/
	len = iris_capture_disable_lce(popt, &skiplast);

	regvalue = (value * 0xfff) / 0xff;

	regval.ip = IRIS_IP_DBC;
	regval.opt_id = 0xfd;
	regval.mask = 0xffffffff;
	regval.value = regvalue;

	iris_update_bitmask_regval_nonread(&regval, false);
	len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_DBC,
			0xfd, 0xfd, skiplast);

	len = iris_capture_enable_lce(popt, len);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);

	IRIS_LOGI("%s,bl_user value=%d", __func__, regvalue);
}

void iris_dbc_led0d_gain_set(u32 value)
{
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	struct iris_update_regval regval;
	bool skiplast = 0;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_lce(popt, &skiplast);

	regval.ip = IRIS_IP_DBC;
	regval.opt_id = 0xfc;
	regval.mask = 0xffffffff;
	regval.value = value;

	iris_update_bitmask_regval_nonread(&regval, false);
	len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_DBC,
			regval.opt_id, regval.opt_id, skiplast);

	len = iris_capture_enable_lce(popt, len);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);

	IRIS_LOGI("%s,dbc_led0d value=%d", __func__, value);
}

void iris_panel_nits_set(u32 bl_ratio, bool bSystemRestore, int level)
{
#if 0
	char led_pwm1[3] = {MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x0, 0x0};
	char hbm_data[2] = {MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x0};
	struct dsi_cmd_desc backlight_cmd = {
		{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, sizeof(led_pwm1), led_pwm1, 0, NULL}, 1, 1};
	struct dsi_cmd_desc hbm_cmd = {
		{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, 0, 0, 0, sizeof(hbm_data), hbm_data, 0, NULL}, 1, 1};

	struct dsi_panel_cmd_set cmdset = {
		.state = DSI_CMD_SET_STATE_HS,
		.count = 1,
		.cmds = &backlight_cmd,
	};
	u32 bl_lvl;
	struct iris_cfg *pcfg = iris_get_cfg();

	/* Don't control panel's brightness when sdr2hdr mode is 3 */
	if (iris_sdr2hdr_mode == 3)
		return;

	if (bSystemRestore)
		bl_lvl = iris_setting.quality_cur.system_brightness;
	else
		bl_lvl = bl_ratio * pcfg->panel_dimming_brightness / PANEL_BL_MAX_RATIO;

	if (pcfg->panel->bl_config.bl_max_level > 255) {
		led_pwm1[1] = (unsigned char)(bl_lvl >> 8);
		led_pwm1[2] = (unsigned char)(bl_lvl & 0xff);
	} else {
		led_pwm1[1] = (unsigned char)(bl_lvl & 0xff);
		backlight_cmd.msg.tx_len = 2;
	}
	iris_pt_send_panel_cmd(pcfg->panel, &cmdset);

	// Support HBM for different panels.
	hbm_data[1] = (bSystemRestore) ? pcfg->panel_hbm[0] : pcfg->panel_hbm[1];
	cmdset.cmds = &hbm_cmd;
	if (pcfg->panel_hbm[0] != pcfg->panel_hbm[1])
		iris_pt_send_panel_cmd(pcfg->panel, &cmdset);
	IRIS_LOGD("panel_nits: bl_lvl=0x%x, hbm=0x%x, restore=%d", bl_lvl, hbm_data[1], bSystemRestore);
#endif
}


void iris_scaler_filter_update(u8 scaler_type, u32 level)
{
	struct iris_update_ipopt popt[IP_OPT_MAX];
	int len;
	uint8_t ip;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	if (scaler_type == SCALER_INPUT)
		ip = SCALER1D_LUT;
	else
		ip = SCALER1D_PP_LUT;
	iris_init_ipopt_ip(popt, IP_OPT_MAX);

	len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, ip,
			level, level, 0);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);

	IRIS_LOGI("scaler filter level=%d", level);
}


void iris_scaler_gamma_enable(bool lightup_en, u32 level)
{
	struct iris_update_ipopt popt[IP_OPT_MAX];
	int len;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;
	bool skiplast = 0;

	iris_init_ipopt_ip(popt, IP_OPT_MAX);
	if (lightup_en == false)
		len = iris_capture_disable_pq(popt, &skiplast);
	else {
		if (!iris_dynamic_power_get())
			skiplast = 1;
	}

	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_DPP,
			level, skiplast);

	if (lightup_en == false)
		len = iris_capture_enable_pq(popt, len);
	else {
		if (!iris_dynamic_power_get())
			len = iris_init_update_ipopt_t(
					popt, IP_OPT_MAX, IRIS_IP_DMA, 0xe2, 0xe2, 0);
	}

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);

	IRIS_LOGI("gamma enable=%d", level);
}


void iris_hdr_csc_prepare(void)
{
	struct iris_update_ipopt popt[IP_OPT_MAX];
	int len;
	bool skiplast = 0;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	if (iris_capture_ctrl_en == false) {
		IRIS_LOGD("iris csc prepare.");
		iris_capture_ctrl_en = true;
		iris_init_ipopt_ip(popt, IP_OPT_MAX);
		if (!iris_dynamic_power_get() && !iris_skip_dma)
			skiplast = 1;

		len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_PWIL,
				0x52, 0x52, skiplast);
		if (!iris_dynamic_power_get() && !iris_skip_dma)
			len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_DMA,
					0xe2, 0xe2, 0);

		is_ulps_enable = iris_disable_ulps(path);
		iris_update_pq_opt(popt, len, path);
		iris_enable_ulps(path, is_ulps_enable);
	}
}

static void iris_dynamic_vfr(struct iris_cfg *pcfg, struct dsi_display *display)
{
	if (iris_virtual_display(display)) {
		int video_update_wo_osd = atomic_read(&pcfg->video_update_wo_osd);

		IRIS_LOGV("clean video_update_wo_osd");
		atomic_set(&pcfg->video_update_wo_osd, 0);
		if (video_update_wo_osd >= 4) {
			cancel_work_sync(&pcfg->vfr_update_work);
			schedule_work(&pcfg->vfr_update_work);
		}
	} else {
		IRIS_LOGV("video_update_wo_osd: %d", atomic_read(&pcfg->video_update_wo_osd));
		atomic_inc(&pcfg->video_update_wo_osd);
		if (atomic_read(&pcfg->video_update_wo_osd) == 4) {
			cancel_work_sync(&pcfg->vfr_update_work);
			schedule_work(&pcfg->vfr_update_work);
		}
	}
}

int iris_kickoff(void *phys_enc)
{
	struct sde_encoder_phys *phys_encoder = phys_enc;
	struct sde_connector *c_conn = NULL;
	struct dsi_display *display = NULL;
	struct iris_cfg *pcfg;

	if (phys_encoder == NULL)
		return -EFAULT;
	if (phys_encoder->connector == NULL)
		return -EFAULT;

	c_conn = to_sde_connector(phys_encoder->connector);
	if (c_conn == NULL)
		return -EFAULT;

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI)
		return 0;

	display = c_conn->display;
	if (display == NULL)
		return -EFAULT;

	pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	if (pcfg->fod_pending)
		iris_post_fod(pcfg->panel);
	if (pcfg->dynamic_vfr)
		iris_dynamic_vfr(pcfg, display);
	if (iris_virtual_display(display) || pcfg->valid < PARAM_PARSED)
		return 0;

	complete(&pcfg->frame_ready_completion);
	return 0;
}

void iris_hdr_csc_complete(int step)
{
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool skiplast = 0;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	if (step == 0 || step == 1 || step == 3 || step == 4) {
		IRIS_LOGD("AP csc prepare.");
		iris_require_yuv_input = shadow_iris_require_yuv_input;
		iris_HDR10 = shadow_iris_HDR10;
		iris_HDR10_YCoCg = shadow_iris_HDR10_YCoCg;
		iris_setting.quality_cur.dspp_dirty = 1;
		if (step == 4)
			return;
	} else if (step == 5 || step == 6) {
		struct iris_cfg *pcfg = iris_get_cfg();

		IRIS_LOGD("Wait frame ready.");
		reinit_completion(&pcfg->frame_ready_completion);
		if (!wait_for_completion_timeout(&pcfg->frame_ready_completion,
					msecs_to_jiffies(50)))
			IRIS_LOGE("%s: timeout waiting for frame ready", __func__);
	}

	IRIS_LOGD("AP csc complete.");
	iris_init_ipopt_ip(popt, IP_OPT_MAX);
	if (iris_capture_ctrl_en == true) {
		if (!iris_dynamic_power_get()) {
			skiplast = 1;
			len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_PWIL,
					0x30, 0x30, 1);
		}

		len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_PWIL,
				0x51, 0x51, skiplast);

		if (!iris_dynamic_power_get())
			len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_DMA,
					0xe2, 0xe2, 0);
		is_ulps_enable = iris_disable_ulps(path);
		iris_update_pq_opt(popt, len, path);
		iris_enable_ulps(path, is_ulps_enable);
		IRIS_LOGD("iris csc complete.");
		iris_capture_ctrl_en = false;
	} else {
		if (!iris_dynamic_power_get()) {
			len = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_DMA, 0xe2, 0xe2, 0);
			is_ulps_enable = iris_disable_ulps(path);
			iris_update_pq_opt(popt, len, path);
			iris_enable_ulps(path, is_ulps_enable);
			IRIS_LOGD("iris csc complete.");
		}
	}
}


int32_t  iris_update_ip_opt(
		struct iris_update_ipopt *popt, int len,
		uint8_t ip, uint8_t opt_id, uint8_t skip_last)
{
	int i = 0;
	uint8_t old_opt;
	int32_t cnt = 0;

	struct iris_pq_ipopt_val *pq_ipopt_val = iris_get_cur_ipopt_val(ip);

	if (pq_ipopt_val == NULL) {
		IRIS_LOGI("can not get pq ipot val ip = %02x, opt_id = %02x",
				ip, opt_id);
		return 1;
	}

	if (0 /*ip == IRIS_IP_EXT*/)	{
		if ((opt_id & 0xe0) == 0x40) {  /*CM LUT table*/
			for (i = 0; i < pq_ipopt_val->opt_cnt; i++) {
				if (((opt_id & 0x1f)%CM_LUT_GROUP)
						== (((pq_ipopt_val->popt[i]) & 0x1f)
							% CM_LUT_GROUP)) {
					old_opt = pq_ipopt_val->popt[i];
					pq_ipopt_val->popt[i] = opt_id;
					cnt = iris_init_update_ipopt_t(popt, len, ip,
							old_opt, opt_id, skip_last);
					return cnt;
				}
			}
		} else if (((opt_id & 0xe0) == 0x60)
				|| ((opt_id & 0xe0) == 0xa0)
				|| ((opt_id & 0xe0) == 0xe0)) {
			/*SDR2HDR LUT table and gamma table*/
			for (i = 0; i < pq_ipopt_val->opt_cnt; i++) {
				if ((opt_id & 0xe0)
						== ((pq_ipopt_val->popt[i]) & 0xe0)) {
					old_opt = pq_ipopt_val->popt[i];
					pq_ipopt_val->popt[i] = opt_id;
					cnt = iris_init_update_ipopt_t(popt, len, ip,
							old_opt, opt_id, skip_last);
					return cnt;
				}
			}
		} else { /*DBC LUT table*/
			for (i = 0; i < pq_ipopt_val->opt_cnt; i++) {
				if (((opt_id & 0xe0)
							== ((pq_ipopt_val->popt[i]) & 0xe0))
						&& (((pq_ipopt_val->popt[i]) & 0x1f) != 0)) {

					old_opt = pq_ipopt_val->popt[i];
					pq_ipopt_val->popt[i] = opt_id;

					cnt = iris_init_update_ipopt_t(popt, len, ip,
							old_opt, opt_id, skip_last);
					return cnt;
				}
			}
		}
	}

	if (ip == DBC_LUT) {
		for (i = 0; i < pq_ipopt_val->opt_cnt; i++) {
			old_opt = pq_ipopt_val->popt[i];
			/*init lut can not change*/
			if (old_opt < CABC_DLV_OFF && (old_opt & 0x7f) != 0) {
				pq_ipopt_val->popt[i] = opt_id;
				cnt = iris_init_update_ipopt_t(popt, len, ip,
						old_opt, opt_id, skip_last);
				return cnt;
			}
		}
	}

	if (ip == CM_LUT) {
		for (i = 0; i < pq_ipopt_val->opt_cnt; i++) {
			if ((opt_id % CM_LUT_GROUP)
					== (pq_ipopt_val->popt[i]
						% CM_LUT_GROUP)) {
				old_opt = pq_ipopt_val->popt[i];
				pq_ipopt_val->popt[i] = opt_id;
				cnt = iris_init_update_ipopt_t(popt, len, ip,
						old_opt, opt_id, skip_last);
				return cnt;
			}
		}
	}

	for (i = 0; i < pq_ipopt_val->opt_cnt; i++) {
		if ((opt_id & 0xf0) == ((pq_ipopt_val->popt[i]) & 0xf0)) {
			old_opt  = pq_ipopt_val->popt[i];
			pq_ipopt_val->popt[i] = opt_id;
			cnt = iris_init_update_ipopt_t(popt, len, ip,
					old_opt, opt_id, skip_last);
			return cnt;
		}
	}
	return 1;
}

uint32_t iris_frc_variable_set(int frc_var_disp)
{
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool skiplast = 0;
	int len;
	struct iris_update_regval regval;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);
	regval.ip = IRIS_IP_PWIL;
	regval.opt_id = 0x23;
	regval.mask = 0x00000100;
	regval.value = frc_var_disp << 8;

	iris_update_bitmask_regval_nonread(&regval, false);
	len  = iris_init_update_ipopt_t(popt, IP_OPT_MAX, regval.ip,
			regval.opt_id, regval.opt_id, skiplast);
	len = iris_capture_enable_pq(popt, len);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);

	IRIS_LOGI("%s, regval.value=%x", __func__, regval.value);
	return regval.value;
}

void iris_frc_force_repeat(bool enable)
{
	struct iris_update_regval regval;

	regval.ip = IRIS_IP_PWIL;
	regval.opt_id = 0xD1;
	regval.mask = 0x00000040;
	regval.value = (enable ? 0x40 : 0x00);
	iris_update_bitmask_regval(&regval, true);
}

void iris_pwil_frc_pt_set(int frc_pt_switch_on)
{
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool skiplast = 0;
	int len;
	uint32_t  *payload = NULL;
	u32 pwil_datapath = frc_pt_switch_on ? 0x80000000 : 0x0;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);
	payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 2);
	pwil_datapath = (payload[2] & (~0x80000000)) | pwil_datapath;
	iris_set_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 4, pwil_datapath);
	iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL, 0xF1, 0x01);

	len = iris_capture_enable_pq(popt, len);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
}

void iris_pwil_frc_ratio_set(int out_fps_ratio, int in_fps_ratio)
{
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool skiplast = 0;
	int len;
	uint32_t  *payload = NULL;
	int temp = 0;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);

	payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0xF4, 2);
	temp = (payload[9] & (~0xff000000)) | (out_fps_ratio<<24);
	iris_set_ipopt_payload_data(IRIS_IP_PWIL, 0xF4, 11, temp);
	iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL, 0xF4, 0x01);

	payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0xD0, 2);
	temp = (payload[5] & (~0x00003F00)) | (in_fps_ratio<<8);
	iris_set_ipopt_payload_data(IRIS_IP_PWIL, 0xd0, 7, temp);
	iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL, 0xD0, 0x01);

	len = iris_capture_enable_pq(popt, len);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
}

void iris_pwil_frc_video_ctrl2_set(int mvc_0_1_phase)
{
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool skiplast = 0;
	int len;
	uint32_t  *payload = NULL;
	int temp = 0;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);
	payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0xB0, 2);
	temp = (payload[2] & (~0x80000000)) | (mvc_0_1_phase<<31);
	iris_set_ipopt_payload_data(IRIS_IP_PWIL, 0xb0, 4, temp);
	iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL, 0xB0, 0x01);

	len = iris_capture_enable_pq(popt, len);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
}

void iris_pwil_cmd_disp_mode_set(bool cmd_disp_on)
{
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool skiplast = 0;
	int len = 0;
	uint32_t  *payload = NULL;
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 pwil_ctrl = (cmd_disp_on && pcfg->tx_mode) ? 0x10 : 0x0;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	if (!iris_dynamic_power_get())
		skiplast = 1;
	payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 2);
	pwil_ctrl = (payload[0] & (~0x10)) | pwil_ctrl;
	iris_set_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 2, pwil_ctrl);
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL, 0xF1, skiplast);

	if (!iris_dynamic_power_get())
		len = iris_init_update_ipopt_t(
				popt, IP_OPT_MAX, IRIS_IP_DMA, 0xe2, 0xe2, 0);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
}

void iris_psf_mif_efifo_set(u8 mode, bool osd_enable)
{
	struct iris_update_ipopt popt[IP_OPT_MAX];
	struct iris_update_regval regval;
	bool enable = false;
	bool skiplast = 0;
	int len;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	/* disable efifo */
	if (true)
		return;
	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	if (!iris_dynamic_power_get())
		skiplast = 1;
	if ((mode == PT_MODE) && (osd_enable == true))
		enable = true;

	regval.ip = IRIS_IP_PSR_MIF;
	regval.opt_id = pcfg->dual_setting ? 0xfe : 0xfd;
	regval.mask = 0x1000;
	regval.value = ((enable == true) ? 0 : 0x1000);
	iris_update_bitmask_regval_nonread(&regval, false);
	len  = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_PSR_MIF,
			regval.opt_id, regval.opt_id, skiplast);
	if (!iris_dynamic_power_get())
		len = iris_init_update_ipopt_t(
				popt, IP_OPT_MAX, IRIS_IP_DMA, 0xe2, 0xe2, 0);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
	IRIS_LOGI("mode = %d, regval.value = 0x%x, len = %d", mode, regval.value, len);
}

void iris_psf_mif_dyn_addr_set(bool dyn_addr_enable)
{
	struct iris_update_ipopt popt[IP_OPT_MAX];
	struct iris_update_regval regval;
	bool skiplast = 0;
	int len;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);

	regval.ip = IRIS_IP_PSR_MIF;
	regval.opt_id = pcfg->dual_setting ? 0xfe : 0xfd;
	regval.mask = 0x2000;
	regval.value = ((dyn_addr_enable == false) ? 0 : 0x2000);
	iris_update_bitmask_regval_nonread(&regval, false);
	len  = iris_init_update_ipopt_t(popt, IP_OPT_MAX, IRIS_IP_PSR_MIF,
			regval.opt_id, regval.opt_id, skiplast);
	if (!dyn_addr_enable)
		len = iris_init_update_ipopt_t(
				popt, IP_OPT_MAX, IRIS_IP_DMA, 0xe2, 0xe2, 0);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
	IRIS_LOGI("regval.value = 0x%x, len = %d", regval.value, len);
}

// -1: no update, 0: disable, 1: enable
void iris_ms_pwil_dma_update(struct iris_mspwil_parameter *par)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool skiplast = 0;
	int len;
	struct iris_update_regval regval;
	uint32_t pwil_ad_frc_info;
	uint32_t  *payload = NULL;
	u32 pwil_datapath = par->frc_pt_switch_on ? 0x80000000 : 0x0;
	u32 pwil_ctrl = (par->cmd_disp_on && pcfg->tx_mode) ? 0x10 : 0x0;
	int temp = 0;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;
	//	uint32_t tx_reserve_0;

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);

	// frc_var_disp, 0xf1240108
	if (par->frc_var_disp != -1) {
		// MIPI_TX ECO
		//		payload = iris_get_ipopt_payload_data(IRIS_IP_TX, 0x00, 2);
		//		if (payload && payload[15] == 0xf1880038) {
		//			tx_reserve_0 = payload[16];
		//			tx_reserve_0 &= ~(1<<28);
		//			tx_reserve_0 |= par->frc_var_disp << 28;
		//			iris_set_ipopt_payload_data(IRIS_IP_TX, 0x00, 2+16, tx_reserve_0);
		//			len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_TX, 0x00, 0x01);
		//		} else {
		//			pr_err("cannot find IRIS_TX_RESERVE_0, payload[15]: %x\n", payload[15]);
		//		}

		regval.ip = IRIS_IP_PWIL;
		regval.opt_id = 0x23;
		regval.mask = 0x00000100;
		regval.value = par->frc_var_disp << 8;
		iris_update_bitmask_regval_nonread(&regval, false);
		len  = iris_init_update_ipopt_t(popt, IP_OPT_MAX, regval.ip,
				regval.opt_id, regval.opt_id, 1);
		pwil_ad_frc_info = regval.value;
		IRIS_LOGI("%s, pwil_ad_frc_info=%x", __func__, pwil_ad_frc_info);
	}

	// type: 0x0001000C
	// frc_pt_switch_on, 0xf1240008
	if (par->frc_pt_switch_on != -1) {
		payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 2);
		pwil_datapath = (payload[2] & (~0x80000000)) | pwil_datapath;
		pwil_datapath &= ~0x00800000;		// blending before scale(PP)
		if (pcfg->dual_setting && par->frc_pt_switch_on == 0)
			pwil_datapath |= 0x00800000;	// blending after scale(PP)
		iris_set_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 4, pwil_datapath);
		//		len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL, 0xF1, 0x01);
	}
	// cmd_disp_on, 0xf1240000
	if (par->cmd_disp_on != -1) {
		payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 2);
		pwil_ctrl = (payload[0] & (~0x10)) | pwil_ctrl;
		iris_set_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 2, pwil_ctrl);
		//		len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL, 0xF1, 0x01);
	}

	if (par->frc_pt_switch_on != -1 || par->cmd_disp_on != -1)
		len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL, 0xF1, 0x01);

	if (par->ratio_update) {
		payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0xF4, 2);
		temp = (payload[9] & (~0xff000000)) | (par->out_fps_ratio<<24);
		iris_set_ipopt_payload_data(IRIS_IP_PWIL, 0xF4, 11, temp);
		iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL, 0xF4, 0x01);

		payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0xD0, 2);
		temp = (payload[5] & (~0x00003F00)) | (par->in_fps_ratio<<8);
		iris_set_ipopt_payload_data(IRIS_IP_PWIL, 0xd0, 7, temp);
		iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL, 0xD0, 0x01);
	}

	if (par->mvc_01phase_update) {
		int pwil_video_opt = 0xB0;
		if (pcfg->dual_setting)
			pwil_video_opt = 0xB1;
		payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, pwil_video_opt, 2);
		temp = (payload[2] & (~0x80000000)) | (par->mvc_01phase<<31);
		iris_set_ipopt_payload_data(IRIS_IP_PWIL, pwil_video_opt, 4, temp);
		iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL, pwil_video_opt, 0x01);
	}

	len = iris_capture_enable_pq(popt, len);
	if (!iris_frc_dma_disable) {
		is_ulps_enable = iris_disable_ulps(path);
		iris_update_pq_opt(popt, len, path);
		iris_enable_ulps(path, is_ulps_enable);
	}

}

void iris_dtg_frame_rate_set(u32 framerate)
{
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool skiplast = 0;
	int len;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt, IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);
	if (framerate == HIGH_FREQ)
		len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_DTG, 0x1, 0x0);
	else
		len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_DTG, 0x0, 0x0);
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_DTG, 0xF0, 0x0);
	len = iris_capture_enable_pq(popt, len);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);

}

static ssize_t iris_pq_config_write(struct file *file, const char __user *buff,
		size_t count, loff_t *ppos)
{
	unsigned long val;
	struct iris_cfg_log {
		uint8_t type;
		char *str;
	};

	struct iris_cfg_log arr[] = {
		{IRIS_PEAKING, "IRIS_PEAKING"},
		{IRIS_DBC_LEVEL, "IRIS_DBC_LEVEL"},
		{IRIS_LCE_LEVEL, "IRIS_LCE_LEVEL"},
		{IRIS_DEMO_MODE, "IRIS_DEMO_MODE"},
		{IRIS_SDR2HDR, "IRIS_SDR2HDR"},
		{IRIS_DYNAMIC_POWER_CTRL, "IRIS_DYNAMIC_POWER_CTRL"},
		{IRIS_LCE_MODE, "IRIS_LCE_MODE"},
		{IRIS_GRAPHIC_DET_ENABLE, "IRIS_GRAPHIC_DET_ENABLE"},
		{IRIS_HDR_MAXCLL, "IRIS_HDR_MAXCLL"},
		{IRIS_ANALOG_BYPASS_MODE, "IRIS_ANALOG_BYPASS_MODE"},
		{IRIS_CM_6AXES, "IRIS_CM_6AXES"},
		{IRIS_CM_FTC_ENABLE, "IRIS_CM_FTC_ENABLE"},
		{IRIS_S_CURVE, "IRIS_S_CURVE"},
		{IRIS_CM_COLOR_TEMP_MODE, "IRIS_CM_COLOR_TEMP_MODE"},
		{IRIS_CM_COLOR_GAMUT, "IRIS_CM_COLOR_GAMUT"},
		{IRIS_CM_COLOR_GAMUT_PRE, "IRIS_CM_COLOR_GAMUT_PRE"},
		{IRIS_CCT_VALUE, "IRIS_CCT_VALUE"},
		{IRIS_COLOR_TEMP_VALUE, "IRIS_COLOR_TEMP_VALUE"},
		{IRIS_AL_ENABLE, "IRIS_AL_ENABLE"},
		{IRIS_LUX_VALUE, "IRIS_LUX_VALUE"},
		{IRIS_READING_MODE, "IRIS_READING_MODE"},
		{IRIS_CHIP_VERSION, "IRIS_CHIP_VERSION"},
		{IRIS_PANEL_TYPE, "IRIS_PANEL_TYPE"},
		{IRIS_LCE_DEMO_WINDOW, "IRIS_LCE_DEMO_WINDOW"},
	};
	u32 type;
	u32 value;
	int i = 0;
	uint32_t cfg_val = 0;
	int len = (sizeof(arr))/(sizeof(arr[0]));

	if (kstrtoul_from_user(buff, count, 0, &val))
		return -EFAULT;

	type = (val & 0xffff0000) >> 16;
	value = (val & 0xffff);
	iris_configure(DSI_PRIMARY, type, value);

	for (i = 0; i < len; i++) {
		iris_configure_get(DSI_PRIMARY, arr[i].type, 1, &cfg_val);
		IRIS_LOGI("%s: %d", arr[i].str, cfg_val);
	}

	return count;
}

static const struct file_operations iris_pq_config_fops = {
	.open = simple_open,
	.write = iris_pq_config_write,
};


int iris_dbgfs_pq_init(struct dsi_display *display)
{
	struct iris_cfg *pcfg;

	pcfg = iris_get_cfg();

	if (pcfg->dbg_root == NULL) {
		pcfg->dbg_root = debugfs_create_dir("iris", NULL);
		if (IS_ERR_OR_NULL(pcfg->dbg_root)) {
			IRIS_LOGE("debugfs_create_dir for iris_debug failed, error %ld",
					PTR_ERR(pcfg->dbg_root));
			return -ENODEV;
		}
	}
	if (debugfs_create_file("iris_pq_config", 0644, pcfg->dbg_root, display,
				&iris_pq_config_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
				__FILE__, __LINE__);
		return -EFAULT;
	}
	return 0;
}

int iris_update_backlight(u8 pkg_mode, u32 bl_lvl)
{
	int rc = 0;
	struct iris_cfg *pcfg = NULL;
	struct mipi_dsi_device *dsi;
	struct dsi_panel *panel;
	char led_pwm1[3] = {MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x0, 0x0};
	struct dsi_cmd_desc backlight_cmd = {
		{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, sizeof(led_pwm1), led_pwm1, 0, NULL}, 1, 1};

	struct dsi_panel_cmd_set cmdset = {
		.state = DSI_CMD_SET_STATE_HS,
		.count = 1,
		.cmds = &backlight_cmd,
	};

	pcfg = iris_get_cfg();
	panel = pcfg->panel;
	dsi = &panel->mipi_device;

	iris_setting.quality_cur.system_brightness = bl_lvl;
	/* Don't set panel's brightness during HDR/SDR2HDR */
	/* Set panel's brightness when sdr2hdr mode is 3 */
	if (iris_setting.quality_cur.pq_setting.sdr2hdr != SDR2HDR_Bypass && iris_sdr2hdr_mode != 3)
		return rc;

	if (panel->bl_config.bl_max_level > 255) {
		if (pkg_mode) {
			led_pwm1[1] = (unsigned char)(bl_lvl >> 8);
			led_pwm1[2] = (unsigned char)(bl_lvl & 0xff);
		} else {
			led_pwm1[1] = (unsigned char)(bl_lvl & 0xff);
			led_pwm1[2] = (unsigned char)(bl_lvl >> 8);
		}
	} else {
		led_pwm1[1] = (unsigned char)(bl_lvl & 0xff);
		backlight_cmd.msg.tx_len = 2;
	}

	if (pcfg->abypss_ctrl.abypass_mode == PASS_THROUGH_MODE)
		rc = iris_pt_send_panel_cmd(panel, &cmdset);
	else
		rc = iris_dsi_send_cmds(panel, cmdset.cmds, cmdset.count, cmdset.state);

	return rc;
}

void iris_pwil_sdr2hdr_resolution_set(bool enter_frc_mode)
{
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;
	struct iris_cfg *pcfg = NULL;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool skiplast = 0;
	int len = 0;
	uint32_t  *payload = NULL;
	u32 resolution = 0, num_ratio = 0;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	if (pqlt_cur_setting->pq_setting.sdr2hdr >= SDR709_2_p3 && pqlt_cur_setting->pq_setting.sdr2hdr <= SDR709_2_2020) {
		pcfg = iris_get_cfg();
		iris_init_ipopt_ip(popt,  IP_OPT_MAX);
		if (enter_frc_mode) {
			num_ratio = (1<<28) / (pcfg->frc_setting.memc_hres * pcfg->frc_setting.memc_vres);
			resolution = (pcfg->frc_setting.memc_vres & 0xFFF) | (pcfg->frc_setting.memc_hres & 0xFFF)<<16;
		} else {
			num_ratio = (1<<28) / (pcfg->frc_setting.disp_vres * pcfg->frc_setting.disp_hres);
			resolution = (pcfg->frc_setting.disp_vres & 0xFFF) | (pcfg->frc_setting.disp_hres & 0xFFF)<<16;
		}

		if (!iris_dynamic_power_get())
			skiplast = 1;

		payload = iris_get_ipopt_payload_data(IRIS_IP_SDR2HDR, 0x93, 2);
		iris_set_ipopt_payload_data(IRIS_IP_SDR2HDR, 0x93, 2, resolution);
		iris_set_ipopt_payload_data(IRIS_IP_SDR2HDR, 0x93, 3, num_ratio);
		len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_SDR2HDR, 0x93, skiplast);

		if (!iris_dynamic_power_get())
			len = iris_init_update_ipopt_t(
					popt, IP_OPT_MAX, IRIS_IP_DMA, 0xe2, 0xe2, 0);
		is_ulps_enable = iris_disable_ulps(path);
		iris_update_pq_opt(popt, len, path);
		iris_enable_ulps(path, is_ulps_enable);
	}
}

void iris_dom_set(int mode)
{
	// struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool skiplast = 0;
	int len;
	bool is_ulps_enable = 0;
	uint32_t  *payload = NULL;
	uint8_t path = iris_pq_update_path;
	uint32_t dport_ctrl0;

	// IRIS_LOGD("%s, mode: %d, DOM cnt: %d-%d", __func__, mode, pcfg->dom_cnt_in_ioctl, pcfg->dom_cnt_in_frc);
	// if (mode != 0) {
	// 	if (atomic_read(&pcfg->dom_cnt_in_ioctl) && atomic_read(&pcfg->dom_cnt_in_frc)) {
	// 		IRIS_LOGI("%s, both set dom in ioctl and frc", __func__);
	// 		atomic_set(&pcfg->dom_cnt_in_frc, 0);
	// 		return;
	// 	}
	// }

	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);

	payload = iris_get_ipopt_payload_data(IRIS_IP_DPORT, 0xF0, 2);
	dport_ctrl0 = payload[0];
	dport_ctrl0 &= ~0xc000;
	dport_ctrl0 |= (mode & 0x3) << 14;
	iris_set_ipopt_payload_data(IRIS_IP_DPORT, 0xF0, 2, dport_ctrl0);
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_DPORT, 0xF0, skiplast);
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_DPORT, 0x80, skiplast);

	len = iris_capture_enable_pq(popt, len);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
	// if (mode == 0)
	// 	atomic_set(&pcfg->dom_cnt_in_ioctl, 1);
	// else
	// 	atomic_set(&pcfg->dom_cnt_in_ioctl, 0);
}

static int iris_brightness_para_set(struct iris_update_ipopt *popt, uint8_t skip_last, uint32_t *value)
{
	int32_t ip = 0;
	int32_t opt_id = 0;
	uint32_t *data = NULL;
	uint32_t val = 0;
	int len = 0;
	struct iris_ip_opt *psopt;

	if (value == NULL) {
		IRIS_LOGE("brightness value is empty");
		return 0;
	}

	ip = IRIS_IP_DPP;
	opt_id = 0x40;
	psopt = iris_find_ip_opt(ip, opt_id);
	if (psopt == NULL) {
		IRIS_LOGE("can not find ip = %02x opt_id = %02x", ip, opt_id);
		return 1;
	}

	data = (uint32_t *)psopt->cmd[0].msg.tx_buf;
	val = value[0];
	val &= 0x7fff7fff;
	data[3] = val;
	val = value[1];
	val &= 0x7fff7fff;
	data[4] = val;
	val = value[2];
	val &= 0x7fff7fff;
	data[5] = val;
	val = value[3];
	val &= 0x7fff7fff;
	data[6] = val;
	val = value[4];
	val &= 0x00007fff;
	data[7] = val;

	len = iris_update_ip_opt(popt, IP_OPT_MAX, ip, opt_id, skip_last);

	return len;
}

void iris_brightness_level_set(u32 *value)
{
	bool skiplast = 0;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;


	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);
	len = iris_brightness_para_set(popt, skiplast, value);
	len = iris_capture_enable_pq(popt, len);

	// TODO: improve performance
	// iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	// len = iris_brightness_para_set(popt, skiplast, value);
	// if (!iris_dynamic_power_get() && !iris_skip_dma) {
	//     len = iris_init_update_ipopt_t(
	//         popt, IP_OPT_MAX, IRIS_IP_DMA, 0xe2, 0xe2, 0);
	// }

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
}

int32_t iris_parse_color_temp_range(struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t rc = 0;

	/* 2500K~7500K for default */
	pcfg->min_color_temp = 2500;
	pcfg->max_color_temp = 7500;

	rc = of_property_read_u32(np, "pxlw,min-color-temp", &(pcfg->min_color_temp));
	if (rc) {
		IRIS_LOGE("can not get property: pxlw,min-color-temp");
		return rc;
	}
	IRIS_LOGI("pxlw,min-color-temp: %d", pcfg->min_color_temp);

	rc = of_property_read_u32(np, "pxlw,max-color-temp", &(pcfg->max_color_temp));
	if (rc) {
		IRIS_LOGE("can not get property:pxlw,max-color-temp");
		return rc;
	}
	IRIS_LOGI("pxlw,max-color-temp: %d", pcfg->max_color_temp);

	return rc;
}

static int32_t _iris_count_ip(const uint8_t *data, int32_t len, int32_t *pval)
{
	int tmp = 0;
	int i = 0;
	int j = 0;

	if (data == NULL || len == 0 || pval == NULL) {
		IRIS_LOGE("%s(), invalid data or pval or len", __func__);
		return -EINVAL;
	}

	tmp = data[0];
	len = len >> 1;

	for (i = 0; i < len; i++) {
		if (tmp == data[2 * i]) {
			pval[j]++;
		} else {
			tmp = data[2 * i];
			j++;
			pval[j]++;
		}
	}

	/*j begin from 0*/
	return j + 1;
}

static int32_t _iris_alloc_pq_init_space(struct iris_cfg *pcfg,
		const uint8_t *pdata, int32_t item_cnt)
{
	int32_t i = 0;
	int32_t size = 0;
	int32_t ip_cnt = 0;
	int32_t rc = 0;
	int32_t *ptr = NULL;
	struct iris_pq_init_val *pinit_val = &pcfg->pq_init_val;

	if (pdata == NULL || item_cnt == 0) {
		IRIS_LOGE("%s(), invalide input, data: %p, size: %d", pdata, item_cnt);
		return -EINVAL;
	}

	size = sizeof(*ptr) * (item_cnt >> 1);
	ptr = vmalloc(size);
	if (ptr == NULL) {
		IRIS_LOGE("can not malloc space for ptr");
		return -EINVAL;
	}
	memset(ptr, 0x00, size);

	ip_cnt = _iris_count_ip(pdata, item_cnt, ptr);
	if (ip_cnt <= 0) {
		IRIS_LOGE("can not static ip option");
		rc = -EINVAL;
		goto EXIT_FREE;
	}

	pinit_val->ip_cnt = ip_cnt;
	size = sizeof(struct iris_pq_ipopt_val) * ip_cnt;
	pinit_val->val = vmalloc(size);
	if (pinit_val->val == NULL) {
		IRIS_LOGE("can not malloc pinit_val->val");
		rc = -EINVAL;
		goto EXIT_FREE;
	}

	for (i = 0; i < ip_cnt; i++) {
		pinit_val->val[i].opt_cnt = ptr[i];
		size = sizeof(uint8_t) * ptr[i];
		pinit_val->val[i].popt = vmalloc(size);
	}

EXIT_FREE:
	vfree(ptr);
	ptr = NULL;

	return rc;
}
int32_t iris_parse_default_pq_param(struct device_node *np,
		struct iris_cfg *pcfg)
{
	int32_t i = 0;
	int32_t j = 0;
	int32_t k = 0;
	int32_t item_cnt = 0;
	int32_t rc = 0;
	const uint8_t *key = "pxlw,iris-pq-default-val";
	const uint8_t *pdata = NULL;
	struct iris_pq_init_val *pinit_val = &pcfg->pq_init_val;

	pdata = of_get_property(np, key, &item_cnt);
	if (!pdata) {
		IRIS_LOGE("%s pxlw,iris-pq-default-val fail", __func__);
		return -EINVAL;
	}

	rc = _iris_alloc_pq_init_space(pcfg, pdata, item_cnt);
	if (rc) {
		IRIS_LOGE("malloc error");
		return rc;
	}

	for (i = 0; i < pinit_val->ip_cnt; i++) {
		struct iris_pq_ipopt_val *pval = &(pinit_val->val[i]);

		pval->ip = pdata[k++];
		for (j = 0; j < pval->opt_cnt; j++) {
			pval->popt[j] = pdata[k];
			k += 2;
		}
		/*need to skip one*/
		k -= 1;
	}

	if (IRIS_IF_LOGV()) {
		IRIS_LOGE("ip_cnt = %0x", pinit_val->ip_cnt);
		for (i = 0; i < pinit_val->ip_cnt; i++) {
			char ptr[256];
			int32_t len = 0;
			int32_t sum = 256;
			struct iris_pq_ipopt_val *pval = &(pinit_val->val[i]);

			snprintf(ptr, sum, "ip is %0x opt is ", pval->ip);
			for (j = 0; j < pval->opt_cnt; j++) {
				len = strlen(ptr);
				sum -= len;
				snprintf(ptr + len, sum, "%0x ", pval->popt[j]);
			}
			IRIS_LOGE("%s", ptr);
		}
	}

	return rc;
}

void iris_cm_setting_switch(bool dual)
{
	bool skiplast = 0;
	int len;
	struct iris_update_ipopt popt[IP_OPT_MAX];
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;
	struct quality_setting *pqlt_cur_setting = &iris_setting.quality_cur;
	uint32_t  *payload = NULL;
	u32 pwil_datapath;
	u32 cm_csc = 0x40;
	// FIXME: WA for old pxlw-dtsi
	// u32 pwil_csc = dual ? 0x95 : 0x96;
	u32 pwil_csc = 0x95;

	if (pqlt_cur_setting->pq_setting.readingmode != 0)
		cm_csc = 0x41;
	switch (pqlt_cur_setting->pq_setting.sdr2hdr) {
	case HDR10In_ICtCp:
	case HDR10In_YCbCr:
	case ICtCpIn_YCbCr:
		cm_csc = 0x45;
		break;
	case SDR709_2_709:
		/*TODOS*/
		break;
	case SDR709_2_p3:
		cm_csc = 0x46;
		break;
	case SDR709_2_2020:
		/*TODOS*/
		break;
	default:
		break;
	}
	// FIXME: WA for old pxlw-dtsi
	// if (!dual)
	// 	cm_csc += 8;
	iris_init_ipopt_ip(popt,  IP_OPT_MAX);
	len = iris_capture_disable_pq(popt, &skiplast);

	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_CM, cm_csc, skiplast);
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL, pwil_csc, skiplast);

	payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 2);
	pwil_datapath = payload[2] & ~0x00800000;// blending before PP
	if (dual)
		pwil_datapath |= 0x00800000;	// blending after PP
	iris_set_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 4, pwil_datapath);
	iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_PWIL, 0xF1, 0x01);

	len = iris_capture_enable_pq(popt, len);

	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);
}
