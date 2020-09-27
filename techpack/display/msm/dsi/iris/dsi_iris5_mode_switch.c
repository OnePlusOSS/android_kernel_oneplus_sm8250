// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */

#include <linux/gcd.h>

#include <video/mipi_display.h>
#include <drm/drm_mipi_dsi.h>
#include "dsi_iris5_api.h"
#include "dsi_iris5_lightup.h"
#include "dsi_iris5_lightup_ocp.h"
#include "dsi_iris5.h"
#include "dsi_iris5_pq.h"
#include "dsi_iris5_lp.h"
#include "dsi_iris5_mode_switch.h"
#include "dsi_iris5_frc.h"
#include "dsi_iris5_log.h"

struct iris_frc_setting *frc_setting;
struct iris_mspwil_parameter mspwil_par;
static int iris_frc_label = 0;
static int iris_frc_var_disp = 0;   //0: disable, 1: set before FRC, 2: set after FRC
// bit[4], enable sw configure, bit[3:0], sw motion level, 0-4
static int iris_frc_mnt_level = 0;
static int iris_frc_dynamic_off = 1;
static int iris_frc_pt_switch_on = 1;
//mcu is enabled in prelightup stage, and iris_mcu_enable is no need to enabled.
static int iris_mcu_enable = 1; // 0: disable, 1: dynamic enable , 2: always enable
static int iris_mcu_stop_check = 1;
int iris_w_path_select = 1; //0: i2c, 1: dsi
int iris_r_path_select = 1; //0: i2c, 1: dsi
int iris_frc_dma_disable = 0;
#define IRIS_FRC_CMD_PAYLOAD_LENGTH	300	// 128 registers
static u32 *iris_frc_cmd_payload;
static int iris_frc_cmd_payload_count;
static int iris_frc_demo_window;
static int iris_dport_output_toggle = 1;
static int iris_frc_fallback_disable = 0;
static int iris_dynamic_sadgain_mid = 8;
static int iris_dynamic_sadgain_low = 16;
static int iris_dynamic_sadcore_mid = 10;
static int iris_dynamic_sadcore_low = 4;
static int iris_dynamic_outgain_mid = 8;
static int iris_dynamic_outgain_low = 16;
static int iris_dynamic_outcore_mid = 10;
static int iris_dynamic_outcore_low = 6;
static int iris_frc_osd_protection = 0;
static int iris_mvc_tuning = 1;
static int iris_mtnsw_low_th = 8;
static int iris_mtnsw_high_th = 3;
static int iris_frcpt_switch_th = 0x3564;
static int iris_te_dly_frc = -1;
static int iris_display_vtotal = -1;
static u32 iris_val_frcc_reg8;
static int iris_frc_var_disp_dbg = 0;
static int iris_frc_var_disp_dual = 2;
static int iris_frc_pt_switch_on_dbg = 1;

static void iris_frc_cmd_payload_init(void)
{
	iris_frc_cmd_payload_count = 0;
	if (iris_frc_cmd_payload == NULL)
		iris_frc_cmd_payload = vmalloc(IRIS_FRC_CMD_PAYLOAD_LENGTH*sizeof(u32));
	if (iris_frc_cmd_payload == NULL)
		IRIS_LOGE("can not vmalloc size = %d in %s", IRIS_FRC_CMD_PAYLOAD_LENGTH, __func__);
}

static void iris_frc_cmd_payload_release(void)
{
	if (iris_frc_cmd_payload != NULL && iris_frc_cmd_payload_count != 0) {
		IRIS_LOGI("iris_frc_cmd_payload_count: %d", iris_frc_cmd_payload_count);
		iris_ocp_write_mult_vals(iris_frc_cmd_payload_count, iris_frc_cmd_payload);
		vfree(iris_frc_cmd_payload);
		iris_frc_cmd_payload = NULL;
	}
}

void iris_frc_reg_add(u32 addr, u32 val, bool last)
{
	u32 *payload = &iris_frc_cmd_payload[iris_frc_cmd_payload_count];

	if (iris_frc_cmd_payload_count < IRIS_FRC_CMD_PAYLOAD_LENGTH) {
		*payload = addr;
		payload++;
		*payload = val;
		iris_frc_cmd_payload_count += 2;
	} else {
		IRIS_LOGE("payload buffer lenght is not enough! %s", __func__);
	}
}

void iris_rfb_mode_enter(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	if (pcfg->rx_mode == pcfg->tx_mode)
		pcfg->pwil_mode = PT_MODE;
	else
		pcfg->pwil_mode = RFB_MODE;
	IRIS_LOGI("rx_mode: %d, tx_mode: %d", pcfg->rx_mode, pcfg->tx_mode);
	if (pcfg->osd_enable)
		iris_psf_mif_efifo_set(pcfg->pwil_mode, pcfg->osd_enable);
	iris_set_pwil_mode(pcfg->panel, pcfg->pwil_mode, pcfg->osd_enable, DSI_CMD_SET_STATE_HS);
}

void iris_frc_mode_enter(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	pcfg->pwil_mode = FRC_MODE;
	iris_set_pwil_mode(pcfg->panel, pcfg->pwil_mode, pcfg->osd_enable, DSI_CMD_SET_STATE_HS);
}

u32 iris_frc_video_hstride_calc(u16 hres, u16 bpp, u16 slice_num)
{
	u32 hstride;

	hstride = bpp * ((hres + slice_num - 1) / slice_num);
	hstride = (hstride + 15) >> 4;
	hstride = ((hstride  + 7) / 8) * 8;
	hstride = (hstride + 63) / 64;

	return hstride;
}

static bool iris_frc_setting_check(void)
{
#define IRIS_FRC_V2_LUT_TABLE_NUM 19
	struct frc_lut_config {
		int index;
		int in_out_ratio;
		int period_phasenum;
		int idx_max;
	};
	//int v2_table[IRIS_FRC_LUT_TABLE_NUM];
	int inout_ratio = frc_setting->in_fps | frc_setting->out_fps<<8;
	int i;
	struct iris_cfg *pcfg = iris_get_cfg();
	//int ppn_table[IRIS_FRC_LUT_TABLE_NUM];	// period, phasenum
	//int idx_max_table[IRIS_FRC_LUT_TABLE_NUM]; //phaselut index max
	struct frc_lut_config config_table[IRIS_FRC_V2_LUT_TABLE_NUM+8] = {
		{ 1, 12 | 60 << 8,	1 | 5 << 11,  0},
		{ 2, 12 | 90 << 8,	2 | 15 << 11, 1},
		{ 3, 12 | 120 << 8, 1 | 10 << 11, 0},
		{ 4, 15 | 60 << 8,	1 | 4 << 11,  1},
		{ 5, 15 | 90 << 8,	1 | 6 << 11,  0},
		{ 6, 15 | 120 << 8, 1 | 8 << 11,  0},
		{ 7, 24 | 60 << 8,	2 | 5 << 11,  1},
		{ 8, 24 | 90 << 8,	4 | 15 << 11, 2},
		{ 9, 24 | 120 << 8, 2 | 10 << 11, 1},
		{10, 25 | 60 << 8,	5 | 12 << 11, 2},
		{11, 25 | 90 << 8,	5 | 18 << 11, 2},
		{12, 25 | 120 << 8, 5 | 24 << 11, 2},
		{13, 30 | 60 << 8,	2 | 4 << 11,  1},
		{14, 30 | 90 << 8,	2 | 6 << 11,  1},
		{15, 30 | 120 << 8, 2 | 8 << 11,  1},
		{16, 60 | 90 << 8,	2 | 3 << 11,  1},
		{17, 60 | 120 << 8, 2 | 4 << 11,  1},
		{18, 27 | 60 << 8,	4 | 9 << 11,  2},
		{ 3,  6 | 60 << 8,  1 | 10 << 11, 0},
		{ 2,  8 | 60 << 8,	2 | 15 << 11, 1},
		{ 5, 10 | 60 << 8,	1 | 6 << 11,  0},
		{ 8, 16 | 60 << 8,	4 | 15 << 11, 2},
		{14, 20 | 60 << 8,	2 | 6 << 11,  1},
		{16, 40 | 60 << 8,  2 | 3 << 11,  1},
		{18, 40 | 90 << 8,  4 | 9 << 11,  2},
		{13, 45 | 90 << 8,  2 | 4 << 11,  1},
	};

	struct frc_lut_config video_config_table[IRIS_FRC_V2_LUT_TABLE_NUM+8] = {
		{ 1, 12 | 60 << 8,  1 | 5 << 11,  0},
		{ 2, 12 | 90 << 8,  2 | 15 << 11, 1},
		{ 3, 12 | 120 << 8, 1 | 10 << 11, 0},
		{ 4, 15 | 60 << 8,  1 | 4 << 11,  1},
		{ 5, 15 | 90 << 8,  1 | 6 << 11,  0},
		{ 6, 15 | 120 << 8, 1 | 8 << 11,  0},
		{ 19, 24 | 60 << 8, 4 | 10 << 11, 1},
		{ 8, 24 | 90 << 8,  4 | 15 << 11, 2},
		{ 9, 24 | 120 << 8, 2 | 10 << 11, 1},
		{10, 25 | 60 << 8,  5 | 12 << 11, 2},
		{11, 25 | 90 << 8,  5 | 18 << 11, 2},
		{12, 25 | 120 << 8, 5 | 24 << 11, 2},
		{13, 30 | 60 << 8,  2 | 4 << 11,  1},
		{14, 30 | 90 << 8,  2 | 6 << 11,  1},
		{15, 30 | 120 << 8, 2 | 8 << 11,  1},
		{16, 60 | 90 << 8,  2 | 3 << 11,  1},
		{17, 60 | 120 << 8, 2 | 4 << 11,  1},
		{18, 27 | 60 << 8,	4 | 9 << 11,  2},
		{ 3,  6 | 60 << 8,	1 | 10 << 11, 0},
		{ 2,  8 | 60 << 8,  2 | 15 << 11, 1},
		{ 5, 10 | 60 << 8,  1 | 6 << 11,  0},
		{ 8, 16 | 60 << 8,  4 | 15 << 11, 2},
		{14, 20 | 60 << 8,  2 | 6 << 11,  1},
		{16, 40 | 60 << 8,  2 | 3 << 11,  1},
		{18, 40 | 90 << 8,  4 | 9 << 11,  2},
		{13, 45 | 90 << 8,  2 | 4 << 11,  1},
	};

	if (pcfg->rx_mode == DSI_OP_CMD_MODE) {
		for (i = 0; i < IRIS_FRC_V2_LUT_TABLE_NUM+8; i++) {
			if (inout_ratio == config_table[i].in_out_ratio) {
				frc_setting->v2_lut_index = config_table[i].index;
				frc_setting->v2_period_phasenum = config_table[i].period_phasenum;
				frc_setting->v2_phaselux_idx_max = config_table[i].idx_max;
				IRIS_LOGI("inout_ratio: %d", inout_ratio);
				IRIS_LOGI("select frc lut index: %d", frc_setting->v2_lut_index);
				IRIS_LOGI("select frc inout ratio: %d", frc_setting->v2_period_phasenum);
				return true;
			}
		}
	} else {
		for (i = 0; i < IRIS_FRC_V2_LUT_TABLE_NUM+8; i++) {
			if (inout_ratio == video_config_table[i].in_out_ratio) {
				frc_setting->v2_lut_index = video_config_table[i].index;
				frc_setting->v2_period_phasenum = video_config_table[i].period_phasenum;
				frc_setting->v2_phaselux_idx_max = video_config_table[i].idx_max;
				IRIS_LOGI("inout_ratio: %d", inout_ratio);
				IRIS_LOGI("select frc lut index: %d", frc_setting->v2_lut_index);
				IRIS_LOGI("select frc inout ratio: %d", frc_setting->v2_period_phasenum);
				return true;
			}
		}
	}

	return false;
}

static u32 iris_get_vtotal(void)
{
	uint32_t *payload;
	uint32_t vtotal;

	if (frc_setting->out_fps == HIGH_FREQ)
		payload = iris_get_ipopt_payload_data(IRIS_IP_DTG, 0x01, 2);
	else
		payload = iris_get_ipopt_payload_data(IRIS_IP_DTG, 0x00, 2);
	vtotal = payload[4] + payload[5] + payload[6]  + payload[7];
	if (iris_display_vtotal != -1)
		vtotal = iris_display_vtotal;
	return vtotal;
}

void iris_frc_mif_reg_set(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 val_frcc_reg5 = 0x3c000000, val_frcc_reg8 = 0x10000000, val_frcc_reg16 = 0x413120c8, val_frcc_reg17 = 0x8000;
	u32 val_frcc_reg18 = 0x80000000, val_frcc_cmd_th = 0x8000, val_frcc_dtg_sync = 0;
	u8 keep_th = 0, carry_th = 0, phase_map_en = 0;
	u8 repeatp1_th = 0, repeatcf_th = 0;
	u8 ts_frc_en = 0, three_mvc_en = 0;
	u16 input_record_thr = 0, start_line;
	u32 hsync_freq_out, hsync_freq_in;
	u16 mvc_metarec_thr1 = 0, mvc_metarec_thr2 = 0;
	u16 vfr_mvc_metagen_th1 = 0, vfr_mvc_metagen_th2 = 0;
	u16 fi_repeatcf_th = 0;
	u16 fmif_vd_hstride, fmif_dsy_hstride, fmif_mv_hstride;
	u32 fmif_vd_offset, fmif_dsy_frm_offset, fmif_mv_frm_offset;
	u16 mv_hres, mv_vres;
	u8 max_fi_meta_fifo = 4;
	u32 vtotal_frcc = iris_get_vtotal();
	u32 mvc_01phase_vcnt, dedicate_01phase_en = 0, mvc_01phase_metagen2, mvc_01phase_metagen, mvc_01phase_metarec;
	u32 mvc_metagen_thr3, mvc_metarec_thr3, three_metagen_en = 1, three_metagen_fi_en = 0;
	u32 frcc_teadj_th = 0x00282080;
	u32 temp, temp0, m, n;
	u32 frc_ratio = frc_setting->v2_period_phasenum;
	u32 phase_teadj_en = 1;
	u32 input_vtotal = iris_get_vtotal();
	u32 disp_vtotal = iris_get_vtotal();
	u32 memc_level = frc_setting->memc_level;

	IRIS_LOGI("out_fps: %d, vtotal: %d", frc_setting->out_fps, disp_vtotal);

	fmif_vd_hstride = iris_frc_video_hstride_calc(frc_setting->memc_hres, frc_setting->memc_dsc_bpp, 1);
	fmif_vd_offset = fmif_vd_hstride * frc_setting->memc_vres * 8;
	fmif_dsy_hstride = (((frc_setting->memc_hres + 1) / 2) * 8 + 63) / 64;
	fmif_dsy_frm_offset = fmif_dsy_hstride * (frc_setting->memc_vres * 5 / 10) * 8;
	fmif_mv_hstride = ((frc_setting->memc_hres + 15) / 16 * 32 + 63) / 64;
	fmif_mv_frm_offset = fmif_mv_hstride * CEILING(frc_setting->memc_vres, 16) * 8;
	mv_hres = CEILING(frc_setting->memc_hres, 16);
	mv_vres = CEILING(frc_setting->memc_vres, 16);

	if (!(pcfg->dual_test & 0x2))
		frc_setting->mv_baseaddr = 0x300000 - (4+frc_setting->mv_buf_num) * fmif_mv_frm_offset;
	IRIS_LOGI("mv_baseaddr: %x, offset: %x", frc_setting->mv_baseaddr, fmif_mv_frm_offset);
	if ((frc_setting->video_baseaddr + 3 * fmif_vd_offset) > frc_setting->mv_baseaddr) {
		IRIS_LOGE("buffer check failed!");
		IRIS_LOGE("video_baseaddr: %x, offset: %x", frc_setting->video_baseaddr, fmif_vd_offset);
		IRIS_LOGE("mv_baseaddr: %x, offset: %x", frc_setting->mv_baseaddr, fmif_mv_frm_offset);
	}

	if (frc_setting->disp_hres != frc_setting->memc_hres)
		start_line = 5;
	else
		start_line = 0;

	hsync_freq_in = frc_setting->out_fps * input_vtotal;
	hsync_freq_out = frc_setting->out_fps * disp_vtotal;

	IRIS_LOGI(" print video enter fps = %d, output fps = %d.", frc_setting->in_fps, frc_setting->out_fps);

	carry_th = 5;
	keep_th = 252;
	repeatp1_th = 5;
	repeatcf_th = 252;
	fi_repeatcf_th = 252;
	input_record_thr = (input_vtotal / 8 + start_line) * hsync_freq_out / hsync_freq_in;
	mvc_metarec_thr1 = (input_vtotal * 5 / 8 + start_line) * hsync_freq_out / hsync_freq_in;
	mvc_metarec_thr2 = (input_vtotal * 7 / 8 + start_line) * hsync_freq_out / hsync_freq_in;
	vfr_mvc_metagen_th1 = (input_vtotal / 8 + start_line) * hsync_freq_out / hsync_freq_in;
	vfr_mvc_metagen_th2 = (input_vtotal * 6 / 8 + start_line) * hsync_freq_out / hsync_freq_in;

	temp = frc_setting->out_fps;
	temp0 = frc_setting->in_fps;
	while (temp%temp0 != 0) {
		m = temp%temp0;
		temp = temp0;
		temp0 = m;
	}

	m = frc_setting->out_fps/temp0;
	n = frc_setting->in_fps/temp0;

	if ((m/n) == 4) {
		vtotal_frcc = (disp_vtotal * 2)/3;
		IRIS_LOGI(" print m = %d, n = %d, vtotal_frcc = %d.", m, n, vtotal_frcc);
	} else if ((m/n) == 5 || (m/n) == 6) {
		vtotal_frcc = (disp_vtotal * (m - 1))/m;
		IRIS_LOGI(" print m = %d, n = %d, vtotal_frcc = %d.", m, n, vtotal_frcc);
	} else if ((m/n) > 6) {
		temp = (m + n - 1)/n;
		temp = (temp)/2;
		temp0 = (m/n)-1;
		temp0 = temp0 * disp_vtotal;
		vtotal_frcc = (2*temp0)/(2*temp + 3);
		IRIS_LOGI(" print m = %d, n = %d, vtotal_frcc = %d.", m, n, vtotal_frcc);
	}

	mvc_01phase_vcnt = disp_vtotal;
	mvc_01phase_metagen = 20;
	mvc_01phase_metarec = mvc_01phase_metagen + disp_vtotal/4;
	mvc_01phase_metagen2 = mvc_01phase_metarec + 10;

	vfr_mvc_metagen_th1 = 50;
	vfr_mvc_metagen_th2 = vfr_mvc_metagen_th1 + disp_vtotal/4;
	mvc_metagen_thr3 = vfr_mvc_metagen_th2 + disp_vtotal/4;

	mvc_metarec_thr1 = vfr_mvc_metagen_th1 + disp_vtotal/5;
	mvc_metarec_thr2 = vfr_mvc_metagen_th2 + disp_vtotal/5;
	mvc_metarec_thr3 = mvc_metagen_thr3 + disp_vtotal/5;

	switch (frc_setting->in_fps) {
	case 24:
		three_mvc_en = 1;
		if (frc_setting->out_fps == 90)
			max_fi_meta_fifo = 6;
		else
			max_fi_meta_fifo = 4;

		if (frc_setting->out_fps == 120) {
			three_mvc_en = 0;
			max_fi_meta_fifo = 6;
			three_metagen_en = 0;
			dedicate_01phase_en = 1;
		}
		break;
	case 30:
		if (pcfg->frc_low_latency) {
			three_mvc_en = 0;
			max_fi_meta_fifo = 1;
			phase_teadj_en = 0;
			three_metagen_en = 0;
			frc_ratio = 0x801;
		} else if (frc_setting->out_fps == 120) {
			three_mvc_en = 0;
			max_fi_meta_fifo = 6;
			three_metagen_en = 0;
			dedicate_01phase_en = 1;
		} else {
			three_mvc_en = 1;
			max_fi_meta_fifo = 3;
			frcc_teadj_th = 0x280880;
			if (frc_setting->out_fps == 90)
				max_fi_meta_fifo = 5;
			else
				max_fi_meta_fifo = 3;
		}
		break;
	case 25:
		three_mvc_en = 1;
		max_fi_meta_fifo = 5;
		if (frc_setting->out_fps == 120) {
			three_mvc_en = 0;
			max_fi_meta_fifo = 6;
			three_metagen_en = 0;
			dedicate_01phase_en = 1;
		}
		break;
	case 15:
		three_mvc_en = 0;
		max_fi_meta_fifo = 6;
		three_metagen_en = 0;
		dedicate_01phase_en = 1;
		//three_metagen_fi_en = 1;
		break;
	case 6:
	case 8:
	case 10:
	case 12:
		three_mvc_en = 0;
		max_fi_meta_fifo = 6;
		dedicate_01phase_en = 1;
		three_metagen_en = 0;
		break;
	case 16:
		three_mvc_en = 1;
		max_fi_meta_fifo = 6;
		break;
	case 20:
		if (pcfg->frc_low_latency) {
			three_mvc_en = 0;
			max_fi_meta_fifo = 1;
			phase_teadj_en = 0;
			three_metagen_en = 0;
			frc_ratio = 0x801;
		} else {
			three_mvc_en = 1;
			max_fi_meta_fifo = 4;
		}
		break;
	case 40:
		if (pcfg->frc_low_latency) {
			max_fi_meta_fifo = 4;
			phase_teadj_en = 1;
			three_mvc_en = 1;
			three_metagen_en = 0;
		}
		break;
	case 45:
		if (pcfg->frc_low_latency) {
			three_mvc_en = 0;
			max_fi_meta_fifo = 1;
			phase_teadj_en = 0;
			three_metagen_en = 0;
			frc_ratio = 0x801;
		}
		break;
	case 60:
		if (pcfg->frc_low_latency) {
			max_fi_meta_fifo = 2;
			frc_ratio = 0x801;
			phase_teadj_en = 0;
			three_mvc_en = 0;
			three_metagen_en = 0;
		}
		break;
	default:
		three_mvc_en = 1;
		max_fi_meta_fifo = 4;
		break;
	}

	memc_level = frc_setting->memc_level;
	if (frc_setting->memc_osd && (memc_level > 2))
		memc_level = 2;
	val_frcc_reg5 += carry_th + (keep_th << 8) + (phase_map_en << 16) + (memc_level << 17);
	val_frcc_reg8 += repeatcf_th + (repeatp1_th << 8);
	iris_val_frcc_reg8 = val_frcc_reg8;
	val_frcc_reg8 |= 0x40000000;
	val_frcc_reg16 += (three_mvc_en << 15) + (ts_frc_en << 31);
	val_frcc_reg17 += input_record_thr + (vtotal_frcc << 16);
	val_frcc_reg18 += mvc_metarec_thr1 + (mvc_metarec_thr2 << 16);
	val_frcc_cmd_th += vfr_mvc_metagen_th1 + (vfr_mvc_metagen_th2 << 16);
	val_frcc_dtg_sync += fi_repeatcf_th << 16;


	//iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG0, 0x02122369, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG1, 0x80280014 + (memc_level << 25), 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG2, (0x96100043 | (iris_dynamic_sadgain_mid << 10) | (iris_dynamic_sadgain_low << 16)), 0);
	if (pcfg->rx_mode == DSI_OP_CMD_MODE)
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG3, (0x18002100 | iris_dynamic_sadcore_mid | (iris_dynamic_sadcore_low << 6)
					| (iris_dynamic_outgain_mid << 18) | (iris_dynamic_outgain_low << 24)) + (1 << 30), 0);
	else
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG3, 0x18602104, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG4, 0x000C2020 | (iris_dynamic_outcore_mid << 6) | (iris_dynamic_outcore_low << 12), 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG5, val_frcc_reg5, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG6, 0x028003C0, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG7, 0x20000000 | frc_ratio, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG8, val_frcc_reg8, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG9, 0x3FFF3FFF, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG10, fmif_vd_offset, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG11, 0x380000, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG12, fmif_dsy_frm_offset, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG13, fmif_dsy_hstride + (fmif_mv_hstride << 16), 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG14, frc_setting->mv_baseaddr, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG15, fmif_mv_frm_offset, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG16, val_frcc_reg16, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG17, val_frcc_reg17, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG18, val_frcc_reg18, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CMD_MOD_TH, val_frcc_cmd_th, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_DTG_SYNC, val_frcc_dtg_sync, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_SWITCH_CTRL, 0x8000000a, 0);
	if (pcfg->frc_setting.short_video == 2)
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_SWFBK_CTRL, 0x00000080, 0);
	else if (pcfg->frc_setting.short_video == 1)
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_SWFBK_CTRL, 0x00000040, 0);
	else
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_SWFBK_CTRL, 0x00000000, 0);
#if 0
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_TEOFFSET_CTRL, 0x00000000, 0);
#else
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_01PHASE_CTRL0, mvc_01phase_vcnt | dedicate_01phase_en << 15 | mvc_01phase_metagen2 << 16, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_01PHASE_CTRL1, mvc_01phase_metagen | mvc_01phase_metarec << 16, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_TEOFFSET_CTRL, 0x00000060 | phase_teadj_en, 0);
	// enable frame drop threshold 4
	if (iris_frc_var_disp && !pcfg->osd_enable) {
		if (iris_frc_var_disp == 1) {
			if (pcfg->tx_mode == DSI_OP_CMD_MODE)
				frc_setting->frcc_pref_ctrl = 0x00018012;
			else
				frc_setting->frcc_pref_ctrl = 0x00018912;
		} else {
			frc_setting->frcc_pref_ctrl = 0x00018010;
		}
	} else {
		if (iris_frc_pt_switch_on)
			frc_setting->frcc_pref_ctrl = 0x0001a011;
		else
			frc_setting->frcc_pref_ctrl = 0x00018010;
	}
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_PERF_CTRL, frc_setting->frcc_pref_ctrl, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_TEOFFSET_ADJ, 0x00000000, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_TEADJ_TH, frcc_teadj_th, 0);
	if (iris_frc_pt_switch_on) {
		u32 mntswitch_th = (iris_mtnsw_low_th & 0xf) | ((iris_mtnsw_high_th & 0xf) << 4);

		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_MNTSWITCH_TH, mntswitch_th, 0);
	} else
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_MNTSWITCH_TH, 0x00000000, 0);
	if (iris_frc_fallback_disable)
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_EXT_MOTION,  0x10, 0);
	else
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_EXT_MOTION, iris_frc_mnt_level, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_EXT_TE_OFFSET, 0x00000000, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_PHASELUT_CLIP, 0x0000fc05 | frc_setting->v2_phaselux_idx_max<<16, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_RW_PROT_TH, 0x00000080, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_REC_META_CTRL, max_fi_meta_fifo, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_METAGEN3, mvc_metagen_thr3 | mvc_metarec_thr3 << 16 |
			three_metagen_fi_en << 30 | three_metagen_en << 31, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_FRCPT_SWITCH_CTRL, iris_frcpt_switch_th, 0);
#endif
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_REG_SHDW, 0x00000002, 0);

	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + IMIF_MODE_CTRL, 0x4 + ((frc_setting->memc_vres * 3 / 4) << 16), 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + IMIF_DW_PER_LINE, fmif_vd_hstride, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + IMIF_VSIZE, (frc_setting->memc_vres), 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + IMIF_SW_UPDATE_EN, 0x00000001, 0);

	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + MMIF_CTRL1, 0x00402000, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + MMIF_CTRL2, 0x8 + (disp_vtotal << 16), 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + MMIF_PHASE1_BA,
			frc_setting->mv_baseaddr + fmif_mv_frm_offset * (2 + frc_setting->mv_buf_num), 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + MMIF_PHASE0_BA, frc_setting->mv_baseaddr + fmif_mv_frm_offset * frc_setting->mv_buf_num, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + MMIF_UPDATE, 0x00000001, 0);

	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FMIF_CTRL, 0xFF004034, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FMIF_VD_FRM_ATTRIBUTE0, (0x10 << 16) + fmif_vd_hstride, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FMIF_VD_FRM_ATTRIBUTE1, (0x40 << 16) + frc_setting->memc_vres, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FMIF_MV_FRM_ATTRIBUTE0, (mv_hres << 16) + mv_vres, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FMIF_MV_FRM_ATTRIBUTE1, (0x4 << 16) + fmif_mv_hstride, 0);
	iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FMIF_REG_SHDW, 0x00000002, 0);

}

void iris_gmd_reg_set(void)
{
	iris_frc_reg_add(IRIS_GMD_ADDR + GMD_GAIN, 0x0000C488, 0);
	iris_frc_reg_add(IRIS_GMD_ADDR + GMD_FILT, 0x00000000, 0);
	iris_frc_reg_add(IRIS_GMD_ADDR + GMD_ACCUM, 0x00000659, 0);
	iris_frc_reg_add(IRIS_GMD_ADDR + GMD_SHIFT, 0x00000070, 0);
	iris_frc_reg_add(IRIS_GMD_ADDR + GMD_START, 0x00000000, 0);
	iris_frc_reg_add(IRIS_GMD_ADDR + GMD_STOP, frc_setting->memc_hres + (frc_setting->memc_vres << 16), 0);
	iris_frc_reg_add(IRIS_GMD_ADDR + GMD_CTRL, 0x00000011, 0);
}

void iris_fbd_reg_set(void)
{
	iris_frc_reg_add(IRIS_FBD_ADDR + FILMBD_RESOLUTION, (frc_setting->memc_hres / 2) + ((frc_setting->memc_vres / 2) << 16), 0);
	iris_frc_reg_add(IRIS_FBD_ADDR + FILMBD_WIN_STOP_SET, frc_setting->memc_hres + (frc_setting->memc_vres << 16), 0);
	iris_frc_reg_add(IRIS_FBD_ADDR + FILMBD_TOP_CTRL, 0x00010025, 0);

}

void iris_cad_reg_set(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	iris_frc_reg_add(IRIS_CAD_ADDR + NEW_FRM_FLG_DET_1, 0x06920311, 0);
	iris_frc_reg_add(IRIS_CAD_ADDR + CAD_DET_BASIC_CAD, 0x0000010c, 0);
	iris_frc_reg_add(IRIS_CAD_ADDR + CAD_DET_STVT, 0x00004083, 0);
	iris_frc_reg_add(IRIS_CAD_ADDR + CAD_DET_BAD_EDIT, 0x00011255, 0);
	iris_frc_reg_add(IRIS_CAD_ADDR + CAD_DET_VOF_0, 0x096823C1, 0);
	iris_frc_reg_add(IRIS_CAD_ADDR + DUMMY, 0x000000F1, 0);
	if (pcfg->rx_mode == DSI_OP_VIDEO_MODE) {
		switch (frc_setting->in_fps) {
		case 24:
		case 30:
			iris_frc_reg_add(IRIS_CAD_ADDR + COMMON, 0x00000079, 0);
			break;
		default:
			iris_frc_reg_add(IRIS_CAD_ADDR + COMMON, 0x00000078, 0);
			break;
		}
	}

	iris_frc_reg_add(IRIS_CAD_ADDR + SW_DONE, 0x00000001, 1);

}

void iris_mvc_reg_set(void)
{
	iris_frc_reg_add(IRIS_MVC_ADDR + MVC_CTRL_0, 0x00524480, 0);
	iris_frc_reg_add(IRIS_MVC_ADDR + MVC_TOP_CTRL_0, 0x00000012, 0);
	iris_frc_reg_add(IRIS_MVC_ADDR + GLB_MVSELECT_1, 0x9C1F7F20, 0);
	iris_frc_reg_add(IRIS_MVC_ADDR + GLB_MVSELECT_2, 0x08448082, 0);
	if (iris_mvc_tuning & 1)
		iris_frc_reg_add(IRIS_MVC_ADDR + HALORED_2, 0x2148091c, 0);
	else
		iris_frc_reg_add(IRIS_MVC_ADDR + HALORED_2, 0x2148091f, 0);
	iris_frc_reg_add(IRIS_MVC_ADDR + MVC_POSTFILT_1, 0x208F0012, 0);
	iris_frc_reg_add(IRIS_MVC_ADDR + MVC_SAD_2, 0x622FFFFF, 0); //todo
	if (frc_setting->memc_hres < 300) {	// FRC resolution 270x480 in FPGA verfication
		iris_frc_reg_add(IRIS_MVC_ADDR + HISTMV_CTRL_3, 0x00640000, 0);
		iris_frc_reg_add(IRIS_MVC_ADDR + HISTMV_CTRL_4, 0x00000064, 0);
	} else {
		iris_frc_reg_add(IRIS_MVC_ADDR + HISTMV_CTRL_1, 0x00000103, 0);
		iris_frc_reg_add(IRIS_MVC_ADDR + HISTMV_CTRL_2, 0x00410204, 0);
		iris_frc_reg_add(IRIS_MVC_ADDR + HISTMV_CTRL_3, 0x00640000, 0);
		iris_frc_reg_add(IRIS_MVC_ADDR + HISTMV_CTRL_4, 0x00000320, 0);
		iris_frc_reg_add(IRIS_MVC_ADDR + HISTMV_STEP, 0x01020082, 0);
		iris_frc_reg_add(IRIS_MVC_ADDR + HISTMV_BASE, 0x24121005, 0);
	}

	iris_frc_reg_add(IRIS_MVC_ADDR + MVC_POSTGLBDC_0, 0x00541204, 0);
	iris_frc_reg_add(IRIS_MVC_ADDR + MVC_OSDDET_0, 0x40642408, 0);
	iris_frc_reg_add(IRIS_MVC_ADDR + MVC_OSDDET_1, 0x20880320, 0);
	iris_frc_reg_add(IRIS_MVC_ADDR + HLMD_CTRL, 0xB98C820B, 0);
	iris_frc_reg_add(IRIS_MVC_ADDR + MVC_SW_UPDATE, 0x00000001, 0);
}

void iris_fi_reg_set(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 max_search_range, val_range_ctrl;
	u32 vrange_top, vrange_bot;
	u32 hres = (frc_setting->memc_hres / 4) * 4;

	if (frc_setting->memc_hres % 4)
		hres += 4;
	max_search_range = 0x20000 / hres - 4;
	if (max_search_range > 510)
		max_search_range = 510;
	vrange_top = max_search_range / 2 - 1;
	vrange_bot = max_search_range / 2;
	// vrange_bot should be odd number
	if (vrange_bot%2 == 0) {
		vrange_bot -= 1;
		vrange_top -= 1;
	}
	val_range_ctrl = vrange_top + (vrange_bot << 9) + (max_search_range << 18);
	iris_frc_reg_add(IRIS_FI_ADDR + FI_RANGE_CTRL, val_range_ctrl, 0);
	iris_frc_reg_add(IRIS_FI_ADDR + FI_CLOCK_GATING, 0xff2c0, 0);
	iris_frc_reg_add(IRIS_FI_ADDR + FI_DEMO_MODE_CTRL, 0x00004022 | iris_frc_label<<16
			| (iris_frc_demo_window ? 1:0), 0);// set flim label
	if (pcfg->osd_on) {
		iris_frc_reg_add(IRIS_FI_ADDR + FI_DEMO_MODE_RING, 0x780152, 0);
		IRIS_LOGI("Dual channel!");
	} else {
		iris_frc_reg_add(IRIS_FI_ADDR + FI_DEMO_MODE_RING, 0xf05a52, 0);
		IRIS_LOGI("singl channel!");
	}

	if (iris_frc_demo_window == 1) {
		iris_frc_reg_add(IRIS_FI_ADDR + FI_DEMO_COL_SIZE, frc_setting->memc_hres << 16, 0);
		iris_frc_reg_add(IRIS_FI_ADDR + FI_DEMO_ROW_SIZE, frc_setting->memc_vres << 15, 0);
	} else if (iris_frc_demo_window == 2) {
		iris_frc_reg_add(IRIS_FI_ADDR + FI_DEMO_COL_SIZE, frc_setting->memc_hres << 16, 0);
		iris_frc_reg_add(IRIS_FI_ADDR + FI_DEMO_ROW_SIZE, frc_setting->memc_vres >> 1
				| frc_setting->memc_vres << 16, 0);
	} else if (iris_frc_demo_window == 3) {
		iris_frc_reg_add(IRIS_FI_ADDR + FI_DEMO_COL_SIZE, frc_setting->memc_hres << 15, 0);
		iris_frc_reg_add(IRIS_FI_ADDR + FI_DEMO_ROW_SIZE, frc_setting->memc_vres << 16, 0);
	} else if (iris_frc_demo_window == 4) {
		iris_frc_reg_add(IRIS_FI_ADDR + FI_DEMO_COL_SIZE, frc_setting->memc_hres >> 1
				| frc_setting->memc_hres << 16, 0);
		iris_frc_reg_add(IRIS_FI_ADDR + FI_DEMO_ROW_SIZE, frc_setting->memc_vres << 16, 0);
	} else if (iris_frc_demo_window == 5) {
		iris_frc_reg_add(IRIS_FI_ADDR + FI_DEMO_COL_SIZE, frc_setting->memc_hres << 16, 0);
		iris_frc_reg_add(IRIS_FI_ADDR + FI_DEMO_ROW_SIZE, frc_setting->memc_vres << 16, 0);
	}
	iris_fi_demo_window_cal();
	iris_frc_reg_add(IRIS_FI_ADDR + FI_VIDEO_BUF_CTRL, 0x00002000, 0);
	iris_frc_reg_add(IRIS_FI_ADDR + FI_V9_GENERIC_CTRL, 0xffffffff, 0);
	iris_frc_reg_add(IRIS_FI_ADDR + FI_MISC_CTRL, 0x00000008, 0);
	iris_frc_reg_add(IRIS_FI_ADDR + FI_CSC_CTRL, 0x00000019, 0);
	iris_frc_reg_add(IRIS_FI_ADDR + FI_CSC_COEF0, 0x08000800, 0);
	iris_frc_reg_add(IRIS_FI_ADDR + FI_CSC_COEF1, 0x00000800, 0);
	iris_frc_reg_add(IRIS_FI_ADDR + FI_CSC_COEF2, 0x0e307d40, 0);
	iris_frc_reg_add(IRIS_FI_ADDR + FI_CSC_COEF3, 0x7a480b38, 0);
	iris_frc_reg_add(IRIS_FI_ADDR + FI_CSC_COEF4, 0x00000000, 0);
	iris_frc_reg_add(IRIS_FI_ADDR + FI_CSC_OFFSET0, 0x00000000, 0);
	iris_frc_reg_add(IRIS_FI_ADDR + FI_CSC_OFFSET1, 0x00000000, 0);
	iris_frc_reg_add(IRIS_FI_ADDR + FI_CSC_OFFSET2, 0x00000000, 0);
	iris_frc_reg_add(IRIS_FI_ADDR + FI_SHDW_CTRL, 0x00000001, 1);	// set last flag
}

void iris_pwil_frc_ratio_and_01phase_set(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 temp, temp0, m, n;
	u32 mvc_01phase = 1;

	temp = frc_setting->out_fps;
	temp0 = frc_setting->in_fps;
	while (temp % temp0 != 0) {
		m = temp % temp0;
		temp = temp0;
		temp0 = m;
	}

	m = frc_setting->out_fps/temp0;
	n = frc_setting->in_fps/temp0;
	switch (frc_setting->in_fps) {
	case 30:
	case 45:
	case 60:
		if (!pcfg->frc_low_latency)
			mvc_01phase = 1;
		else
			mvc_01phase = 0;
		break;
	default:
		break;
	}
	mspwil_par.out_fps_ratio = m;
	mspwil_par.in_fps_ratio = n;
	mspwil_par.mvc_01phase = mvc_01phase;
	mspwil_par.ratio_update = 1;
	mspwil_par.mvc_01phase_update = 1;
}

void iris_pwil_frc_ratio_and_01phase_ocp_set(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 temp, temp0, m, n;
	u32 mvc_01phase = 1;

	temp = frc_setting->out_fps;
	temp0 = frc_setting->in_fps;
	while (temp % temp0 != 0) {
		m = temp % temp0;
		temp = temp0;
		temp0 = m;
	}

	m = frc_setting->out_fps/temp0;
	n = frc_setting->in_fps/temp0;
	switch (frc_setting->in_fps) {
	case 30:
	case 45:
	case 60:
		if (!pcfg->frc_low_latency)
			mvc_01phase = 1;
		else
			mvc_01phase = 0;
		break;
	default:
		break;
	}
	iris_frc_reg_add(IRIS_PWIL_ADDR + PWIL_DPCD_CTRL, m<<24, 0);
	iris_frc_reg_add(IRIS_PWIL_ADDR + PWIL_FBO_CTRL, 0x800 | (n<<8), 0);
	if (pcfg->rx_mode == DSI_OP_CMD_MODE)
		iris_frc_reg_add(IRIS_PWIL_ADDR + PWIL_VIDEO_CTRL2, 0x12061b09 | (mvc_01phase<<31), 0);
	else
		iris_frc_reg_add(IRIS_PWIL_ADDR + PWIL_VIDEO_CTRL2, 0x12061b0d | (mvc_01phase<<31), 0);
	iris_frc_reg_add(IRIS_PWIL_ADDR + PWIL_REG_UPDATE, 0x100, 1);
}

void iris_frc_mode_prepare(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	pcfg->frc_setting_ready = iris_frc_setting_check();
	if (!pcfg->frc_setting_ready) {
		IRIS_LOGE("Can't find correct frc setting!");
		return;
	}

	if (!iris_frc_dynamic_off) {
		if (frc_setting->in_fps_configured == frc_setting->in_fps) {
			IRIS_LOGI("FRC setting has been configured: %d", frc_setting->in_fps);
			return;
		}
	}
	iris_frc_reg_add(IRIS_PROXY_MB5, 0x00000000, 0);
	/* FRC_MIF */
	iris_frc_mif_reg_set();

	/* GMD */
	iris_gmd_reg_set();

	/* FBD */
	iris_fbd_reg_set();

	/* CAD */
	iris_cad_reg_set();

	/* MVC */
	iris_mvc_reg_set();

	/* FI */
	iris_fi_reg_set();

	/* FI_DS to improve 444 to 422 down-sample quality*/
	iris_frc_reg_add(0xf2080000, 0x000a003e, 1);

	/* send frc lut */
	iris_send_ipopt_cmds(FRC_PHASE_LUT, 0);
	iris_send_ipopt_cmds(FRC_PHASE_LUT, frc_setting->v2_lut_index);

	frc_setting->in_fps_configured = frc_setting->in_fps;
}

static uint32_t iris_pwil_disp_ctrl0(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	struct dsi_display_mode *display_mode;
	u8 mode = pcfg->pwil_mode;
	bool osd_enable = pcfg->osd_enable;
	u32 disp_ctrl0 = 0;

	if (mode == PT_MODE) {
		disp_ctrl0 = 0;
		disp_ctrl0 |= 0x10000;
	} else if (mode == RFB_MODE) {
		disp_ctrl0 = 3;
		disp_ctrl0 |= 0x10000;
	} else if (mode == FRC_MODE) {
		disp_ctrl0 = 1;
		disp_ctrl0 |= 0x20000;
	}
	if (osd_enable)
		disp_ctrl0 |= 0x10000000;

	if (pcfg->panel && pcfg->panel->cur_mode) {
		display_mode = pcfg->panel->cur_mode;
		if (display_mode->priv_info && display_mode->priv_info->dsc_enabled)
			disp_ctrl0 |= 0x100;
	}
	return disp_ctrl0;
}

static bool iris_get_tx_reserve_0(uint32_t *tx_reserve_0)
{
	uint32_t *payload = NULL;
	struct iris_ip_opt  *popt = iris_find_ip_opt(IRIS_IP_TX, 0x00);

	if (popt != NULL) {
		if (popt->cmd->msg.tx_len >= 19) {	// length
			payload = iris_get_ipopt_payload_data(IRIS_IP_TX, 0x00, 2);
			if (payload) {
				if (payload[15] == IRIS_TX_RESERVE_0) {
					*tx_reserve_0 = payload[16];
					return true;
				}
				IRIS_LOGE("cannot find IRIS_TX_RESERVE_0, [15]: %x, [16]: %x",
						payload[15], payload[16]);
			} else {
				IRIS_LOGE("cannot find IRIS_TX_RESERVE_0, payload NULL");
			}
		} else {
			IRIS_LOGE("TX cmd length: %zu is less than 19.", popt->cmd->msg.tx_len);
		}
	}
	return false;
}

static void iris_ms_pwil_ocp_update(bool force_repeat, bool var_disp_in_frc_post)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	uint32_t pwil_ad_frc_info;
	uint32_t pwil_datapath;
	uint32_t pwil_ctrl;
	uint32_t disp_ctrl0 = iris_pwil_disp_ctrl0();
	uint32_t tx_reserve_0 = 0x00c840c6;
	int temp = 0;

	uint32_t  *payload = NULL;

	payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0x23, 2);
	pwil_ad_frc_info = payload[0];
	payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0xF1, 2);
	pwil_ctrl = payload[0];
	pwil_datapath = payload[2];
	IRIS_LOGI("%s, pwil_ctrl=%x, pwil_datapath: %x", __func__, pwil_ctrl, pwil_datapath);
	IRIS_LOGI("%s, pwil_ad_frc_info=%x", __func__, pwil_ad_frc_info);
	iris_frc_reg_add(IRIS_PWIL_ADDR + PWIL_CTRL, pwil_ctrl, 0);
	iris_frc_reg_add(IRIS_PWIL_ADDR + DATA_PATH_CTRL, pwil_datapath, 0);
	if (iris_get_tx_reserve_0(&tx_reserve_0)) {
		if (pwil_ad_frc_info & (1<<8))	// frc_var_disp flag
			tx_reserve_0 |= 1<<28;
		else
			tx_reserve_0 &= ~(1<<28);
		IRIS_LOGI("%s, tx_reserve_0=%x", __func__, tx_reserve_0);
		iris_frc_reg_add(IRIS_TX_RESERVE_0, tx_reserve_0, 0);
	}
	iris_frc_reg_add(IRIS_PWIL_ADDR + PWIL_PIAD_FRC_INFO, pwil_ad_frc_info, 0);
	if (var_disp_in_frc_post && !pcfg->osd_enable) {
		if (pcfg->tx_mode == DSI_OP_CMD_MODE)
			frc_setting->frcc_pref_ctrl = 0x00018012;
		else
			frc_setting->frcc_pref_ctrl = 0x00018912;
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_PERF_CTRL, frc_setting->frcc_pref_ctrl, 0);
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_REG_SHDW, 0x00000002, 0);
	}
	if (force_repeat && (pcfg->rx_mode == pcfg->tx_mode))
		disp_ctrl0 |= 0x40;
	iris_frc_reg_add(IRIS_PWIL_ADDR + PWIL_DISP_CTRL0, disp_ctrl0, 1);

	if (mspwil_par.ratio_update) {
		payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0xF4, 2);
		temp = payload[9];
		iris_frc_reg_add(IRIS_PWIL_ADDR + PWIL_DPCD_CTRL, temp, 0);

		payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0xD0, 2);
		temp = payload[5];
		iris_frc_reg_add(IRIS_PWIL_ADDR + PWIL_FBO_CTRL, temp, 0);
	}

	if (mspwil_par.mvc_01phase_update) {
		payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, 0xB0, 2);
		temp = payload[2];
		iris_frc_reg_add(IRIS_PWIL_ADDR + PWIL_VIDEO_CTRL2, temp, 0);
	}
	iris_frc_reg_add(IRIS_PWIL_ADDR + PWIL_REG_UPDATE, 0x100, 1);
}

static void iris_dtg_te_n2m_mode(int frc)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 temp, temp0, m, n;
	u32 in_fps, out_fps;

	if (frc && pcfg->n2m_enable) {
		out_fps = frc_setting->out_fps;
		in_fps = frc_setting->in_fps;
	} else {
		out_fps = pcfg->panel_te;
		in_fps = pcfg->ap_te;
	}

	temp = out_fps;
	temp0 = in_fps;
	while (temp % temp0 != 0) {
		m = temp % temp0;
		temp = temp0;
		temp0 = m;
	}
	m = out_fps/temp0;
	n = in_fps/temp0;

	if (iris_te_dly_frc != -1)
		iris_frc_reg_add(IRIS_DTG_ADDR + DTG_REG_26, iris_te_dly_frc, 0);
	iris_frc_reg_add(IRIS_DTG_ADDR + DTG_REG_41, m | n<<8 | 0x80000000, 0);
	if (frc && in_fps == 40 && out_fps == 90)	// keep previouse value
		iris_frc_reg_add(IRIS_DTG_ADDR + DTG_REG_45, 0x80000004, 0);
	else
		iris_frc_reg_add(IRIS_DTG_ADDR + DTG_REG_45, 0x80000005, 0);
	iris_frc_reg_add(IRIS_DTG_ADDR + DTG_UPDATE, 0x0000000e, 1);

	// update DTG dtsi
	if (frc == 0) {
		if (pcfg->panel_te == 90)
			iris_set_ipopt_payload_data(IRIS_IP_DTG, 0x00, 2+41, m | n<<8 | 0x80000000);
		else
			iris_set_ipopt_payload_data(IRIS_IP_DTG, 0x01, 2+41, m | n<<8 | 0x80000000);
	}
}

static void iris_rfb_mode_prepare(void)
{
	//struct iris_cfg *pcfg = iris_get_cfg();
	// disable variable display
	iris_frc_reg_add(IRIS_PROXY_MB5, 0x00000001, 0);
	if (iris_frc_var_disp) {
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_PERF_CTRL, 0x00019f10, 0);
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_REG_SHDW, 0x00000002, 1);
	} else if (iris_frc_pt_switch_on) {
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_MNTSWITCH_TH, 0x00000000, 0);
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_FRCPT_SWITCH_CTRL, iris_frcpt_switch_th & ~0x20, 0);
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_REG_SHDW, 0x00000003, 1);
		//iris_pwil_frc_pt_set(0);
	}
}

static void iris_download_mcu_code(void)
{
	iris_send_ipopt_cmds(APP_CODE_LUT, 0);
	IRIS_LOGI("%s", __func__);
}

static void iris_mcu_sw_reset(u32 reset)
{
	iris_frc_reg_add(IRIS_SYS_ADDR + MCU_SW_RESET, reset, 1);
}

static void iris_proxy_mcu_stop(void)
{
	iris_frc_reg_add(IRIS_UNIT_CTRL_INTEN, 0x0, 0x0);
	iris_frc_reg_add(IRIS_PROXY_MB1, 0x1, 0x1);
}

static bool iris_mcu_is_stop(void)
{
	if (iris_mcu_stop_check) {
		// if stress test passed, no need check mcu stop status
		// u32 proxy_mb1 = iris_ocp_read(IRIS_PROXY_MB1, DSI_CMD_SET_STATE_HS);
		// u32 mcu_info_1 = iris_ocp_read(IRIS_MCU_INFO_1, DSI_CMD_SET_STATE_HS);
		u32 mcu_info_2 = iris_ocp_read(IRIS_MCU_INFO_2, DSI_CMD_SET_STATE_HS);

		IRIS_LOGI("mcu_info_2: %x", mcu_info_2);
		if (((mcu_info_2>>8)&0x3) == 3)
			return true;
		else
			return false;
	} else {
		return true;
	}
}

static void iris_blending_timeout_set(int frc) {
	struct iris_cfg *pcfg = iris_get_cfg();
	bool high;
	u32 framerate = pcfg->panel->cur_mode->timing.refresh_rate;
	if ((framerate == HIGH_FREQ) && (pcfg->panel->cur_mode->timing.v_active == FHD_H))
		high = true;
	else
		high = false;
	if (frc) {
		iris_send_ipopt_cmds(IRIS_IP_BLEND, high? 0x31 : 0x11);	// blending: frc ocp
		iris_send_ipopt_cmds(IRIS_IP_BLEND, high? 0x30 : 0x10);	// blending: frc
	} else {
		iris_send_ipopt_cmds(IRIS_IP_BLEND, 0x21);	// blending: pt ocp
		iris_send_ipopt_cmds(IRIS_IP_BLEND, 0x20);	// blending: pt
	}
}

static void iris_dport_output_mode(int mode)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	uint32_t *payload = NULL;
	uint32_t dport_ctrl0;
	static u32 write_data[4];

	payload = iris_get_ipopt_payload_data(IRIS_IP_DPORT, 0xF0, 2);
	dport_ctrl0 = payload[0];
	dport_ctrl0 &= ~0xc000;
	dport_ctrl0 |= (mode & 0x3) << 14;
	// disable dport output before FRC enter command
	write_data[0] = IRIS_DPORT_CTRL0;
	write_data[1] = dport_ctrl0;
	write_data[2] = IRIS_DPORT_REGSEL;
	write_data[3] = 0x1;
	iris_ocp_write_mult_vals(4, write_data);
	pcfg->dport_output_mode = mode;
}

static void iris_mspwil_par_clean(void)
{
	mspwil_par.frc_var_disp = -1;
	mspwil_par.frc_pt_switch_on = -1;
	mspwil_par.cmd_disp_on = -1;
	mspwil_par.ratio_update = 0;
	mspwil_par.mvc_01phase_update = 0;
}

void iris_mode_switch_proc(u32 mode)
{
	bool force_repeat;
	struct iris_cfg *pcfg = iris_get_cfg();

	if (!pcfg->frc_enable)
		return;

	frc_setting = &pcfg->frc_setting;
	iris_frc_cmd_payload_init();
	iris_mspwil_par_clean();

	if (mode == IRIS_MODE_FRC_PREPARE) {
		if(pcfg->dual_setting) {
			iris_frc_var_disp = iris_frc_var_disp_dual;		// enable vfr in dual case by default
			iris_frc_pt_switch_on = 0;	// disable frc+pt in dual case
		} else {
			iris_frc_var_disp = iris_frc_var_disp_dbg;
			iris_frc_pt_switch_on = iris_frc_pt_switch_on_dbg;
		}

		iris_ulps_source_sel(ULPS_NONE);
		/*Power up FRC domain*/
		iris_pmu_frc_set(true);
		/*dma trigger frc dsc_unit, for FPGA only */
		//iris_send_ipopt_cmds(IRIS_IP_DMA, 0xe6);

		/*Power up BSRAM domain if need*/
		iris_pmu_bsram_set(true);

		if (pcfg->pwil_mode == PT_MODE) {
			/* power up DSC_UNIT */
			iris_pmu_dscu_set(true);
			if (!(pcfg->dual_test & 0x1))
				iris_frc_dsc_setting(pcfg->dual_setting);
			else
				iris_send_ipopt_cmds(IRIS_IP_DMA, 0xe8);
		}
		mspwil_par.frc_var_disp = iris_frc_var_disp == 1;
		mspwil_par.frc_pt_switch_on = iris_frc_pt_switch_on;
		mspwil_par.cmd_disp_on = 0;
		iris_pwil_frc_ratio_and_01phase_set();
		iris_ms_pwil_dma_update(&mspwil_par);
		iris_ms_pwil_ocp_update(false, false);
		iris_dtg_te_n2m_mode(1);
		iris_frc_mode_prepare();
		if (iris_mcu_enable != 0) {
			if (!pcfg->mcu_code_downloaded) {
				iris_download_mcu_code();
				pcfg->mcu_code_downloaded = true;
			}
		}
		if (iris_frc_fallback_disable) {	// disable mcu during mcu
			iris_mcu_sw_reset(1);
		}
		iris_blending_timeout_set(1);
	} else if (mode == IRIS_MODE_FRC2RFB) {
		iris_rfb_mode_enter();
		iris_pwil_sdr2hdr_resolution_set(false);
		if (iris_mcu_enable)
			iris_proxy_mcu_stop();
	} else if (mode == IRIS_MODE_RFB2FRC) {
		if (pcfg->frc_setting_ready) {
			if (iris_dport_output_toggle)
				iris_dport_output_mode(0);
			iris_frc_mode_enter();
			iris_pwil_sdr2hdr_resolution_set(true);
		} else
			IRIS_LOGE("frc setting not ready!");
	} else if (mode == IRIS_MODE_RFB_PREPARE) {
		iris_rfb_mode_prepare();
		mspwil_par.cmd_disp_on = 0;
		iris_ms_pwil_dma_update(&mspwil_par);
		// no need set force repeat in video mode, single channel only in video mode
		force_repeat = pcfg->rx_mode ? true : false;
		if (iris_frc_pt_switch_on)
			force_repeat = false;
		iris_ms_pwil_ocp_update(force_repeat, false);
	} else if (mode == IRIS_MODE_FRC_POST) {
		if (iris_frc_var_disp == 2) {
			mspwil_par.frc_var_disp = 1;
			iris_ms_pwil_dma_update(&mspwil_par);
			iris_ms_pwil_ocp_update(false, true);
		}
		if (pcfg->osd_enable)
			iris_psf_mif_efifo_set(pcfg->pwil_mode, pcfg->osd_enable);
		if (iris_mcu_enable)
			iris_mcu_sw_reset(0);
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_CTRL_REG8, iris_val_frcc_reg8, 0);
		iris_frc_reg_add(IRIS_FRC_MIF_ADDR + FRCC_REG_SHDW, 0x00000002, 0);
	} else if (mode == IRIS_MODE_RFB_PREPARE_DELAY) {
		if (iris_frc_var_disp) {
			mspwil_par.frc_var_disp = 0;
			iris_ms_pwil_dma_update(&mspwil_par);
			force_repeat = pcfg->rx_mode ? true : false;
			iris_ms_pwil_ocp_update(force_repeat, false);
		}
		iris_blending_timeout_set(0);
	} else if (mode == IRIS_MODE_RFB_POST) {
		if (iris_mcu_enable == 1) {
			if (pcfg->rx_mode == 1) {// command mode
				if (iris_mcu_is_stop())
					iris_mcu_sw_reset(1);
				else
					IRIS_LOGI("iris mcu not in stop, can't reset mcu");
			} else {
				iris_mcu_sw_reset(1);
			}
		}
		if (iris_frc_fallback_disable) // disable mcu during mcu
			iris_mcu_sw_reset(0);
		iris_dtg_te_n2m_mode(0);
		/*Power down FRC domain*/
		iris_pmu_frc_set(false);
		if (pcfg->pwil_mode == RFB_MODE) {
			mspwil_par.cmd_disp_on = 1;
			iris_pwil_cmd_disp_mode_set(true);
		} else if (pcfg->pwil_mode == PT_MODE) {
			/* power down DSC_UNIT */
			iris_pmu_dscu_set(false);
		}

		/*Power down BSRAM domain if in PT single channel*/
		if ((pcfg->pwil_mode == PT_MODE) && (pcfg->osd_enable == false))
			iris_pmu_bsram_set(false);

		if ((pcfg->osd_enable == false) && (iris_i3c_status_get() == false))
			iris_ulps_source_sel(ULPS_MAIN);
	}

	if ((mode == IRIS_MODE_FRC_POST) ||
			(mode == IRIS_MODE_RFB_PREPARE_DELAY) ||
			(mode == IRIS_MODE_RFB_POST)) {
		// just for set parameters
	} else
		pcfg->switch_mode = mode;

	if (mode == IRIS_MODE_FRC_POST) {
		if (iris_frc_var_disp)
			pcfg->dynamic_vfr = true;
		atomic_set(&pcfg->video_update_wo_osd, 0);
	} else if (mode == IRIS_MODE_RFB_PREPARE)
		pcfg->dynamic_vfr = false;
	iris_frc_cmd_payload_release();
}

static int iris_get_mode(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	int switch_mode;
	int pwil_mode = -1;
	u32 pwil_status = iris_ocp_read(IRIS_PWIL_ADDR + PWIL_STATUS, DSI_CMD_SET_STATE_HS);
	u32 pwil_mode_state = pwil_status;

	pwil_mode_state >>= 5;
	pwil_mode_state &= 0x3f;
	if (pwil_mode_state == 2)
		pwil_mode = PT_MODE;
	else if (pwil_mode_state == 4)
		pwil_mode = RFB_MODE;
	else if (pwil_mode_state == 8)
		pwil_mode = FRC_MODE;

	if (pwil_mode == PT_MODE)
		switch_mode = IRIS_MODE_PT;
	else if (pwil_mode == RFB_MODE)
		switch_mode = IRIS_MODE_RFB;
	else if (pwil_mode == FRC_MODE)
		switch_mode = IRIS_MODE_FRC;
	else
		switch_mode = pcfg->switch_mode;

	if ((pcfg->dport_output_mode == 0) && (pwil_mode == FRC_MODE))
		iris_dport_output_mode(2);

	IRIS_LOGW("switch_mode: %d, pwil_mode: %d,  pwil_status: %x",
			switch_mode, pwil_mode, pwil_status);

	return switch_mode;
}

void iris_fi_demo_window(u32 DemoWinMode)
{
	iris_frc_demo_window = DemoWinMode;
	IRIS_LOGI("MEMC demo window mode !");
}

int32_t iris_fi_osd_protect_window(u32 Top_left_position, u32 bottom_right_position, u32 osd_window_ctrl, u32 Enable, u32 DynCompensate)
{
	int32_t rc = 0;
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 temp0, temp1, temp2, temp3;

	if (osd_window_ctrl > 4) {
		IRIS_LOGE("OSD protect window number only have 5.");
		return -EINVAL;
	}

	temp0 = Top_left_position & 0xffff;
	temp1 = (Top_left_position >> 16) & 0xffff;
	temp2 = bottom_right_position & 0xffff;
	temp3 = (bottom_right_position >> 16) & 0xffff;

	if ((temp0 > (frc_setting->disp_hres - 1)) || (temp1 > (frc_setting->disp_vres - 1)) || (temp2 > (frc_setting->disp_hres - 1)) || (temp3 > (frc_setting->disp_vres - 1))) {
		IRIS_LOGE("OSD protect window size error.");
		return -EINVAL;
	}

	if ((temp0 > temp2) || (temp1 > temp3)) {
		IRIS_LOGE("OSD protect window begin point position is bigger than end point position.");
		return -EINVAL;
	}

	if (!DynCompensate)
		pcfg->frc_setting.iris_osd_win_dynCompensate &= (~(1 << osd_window_ctrl));
	else
		pcfg->frc_setting.iris_osd_win_dynCompensate |= (1 << osd_window_ctrl);

	if (!Enable) {
		pcfg->frc_setting.iris_osd_window_ctrl &= (~(7 << (osd_window_ctrl * 3)));
	} else {
		pcfg->frc_setting.iris_osd_window_ctrl |= (1 << (osd_window_ctrl * 3));
		switch (osd_window_ctrl) {
		case 0:
			pcfg->frc_setting.iris_osd0_tl = Top_left_position;
			pcfg->frc_setting.iris_osd0_br = bottom_right_position;
			break;
		case 1:
			pcfg->frc_setting.iris_osd1_tl = Top_left_position;
			pcfg->frc_setting.iris_osd1_br = bottom_right_position;
			break;
		case 2:
			pcfg->frc_setting.iris_osd2_tl = Top_left_position;
			pcfg->frc_setting.iris_osd2_br = bottom_right_position;
			break;
		case 3:
			pcfg->frc_setting.iris_osd3_tl = Top_left_position;
			pcfg->frc_setting.iris_osd3_br = bottom_right_position;
			break;
		case 4:
			pcfg->frc_setting.iris_osd4_tl = Top_left_position;
			pcfg->frc_setting.iris_osd4_br = bottom_right_position;
			break;
		default:
			break;
		}
	}
	return rc;
}

void iris_fi_demo_window_cal(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	u32 osd0_tl, osd0_br, osd1_tl, osd1_br, osd2_tl, osd2_br;
	u32 osd3_tl, osd3_br, osd4_tl, osd4_br;
	u32 temp0, temp1, temp2, temp3;

	//osd window 0
	temp0 = pcfg->frc_setting.iris_osd0_tl & 0xfff;
	temp0 = (temp0*frc_setting->memc_hres)/frc_setting->disp_hres;
	temp1 = (pcfg->frc_setting.iris_osd0_tl >> 16) & 0xfff;
	temp1 = (temp1*frc_setting->memc_vres)/frc_setting->disp_vres;

	temp2 = pcfg->frc_setting.iris_osd0_br & 0xfff;
	temp2 = (temp2*frc_setting->memc_hres)/frc_setting->disp_hres;
	temp3 = (pcfg->frc_setting.iris_osd0_br >> 16) & 0xfff;
	temp3 = (temp3*frc_setting->memc_vres)/frc_setting->disp_vres;
	osd0_tl = (temp1 << 16) | temp0;
	osd0_br = (temp3 << 16) | temp2;
	//osd window 1
	temp0 = pcfg->frc_setting.iris_osd1_tl & 0xfff;
	temp0 = (temp0*frc_setting->memc_hres)/frc_setting->disp_hres;
	temp1 = (pcfg->frc_setting.iris_osd1_tl >> 16) & 0xfff;
	temp1 = (temp1*frc_setting->memc_vres)/frc_setting->disp_vres;

	temp2 = pcfg->frc_setting.iris_osd1_br & 0xfff;
	temp2 = (temp2*frc_setting->memc_hres)/frc_setting->disp_hres;
	temp3 = (pcfg->frc_setting.iris_osd1_br >> 16) & 0xfff;
	temp3 = (temp3*frc_setting->memc_vres)/frc_setting->disp_vres;
	osd1_tl = (temp1 << 16) | temp0;
	osd1_br = (temp3 << 16) | temp2;
	//osd window 2
	temp0 = pcfg->frc_setting.iris_osd2_tl & 0xfff;
	temp0 = (temp0*frc_setting->memc_hres)/frc_setting->disp_hres;
	temp1 = (pcfg->frc_setting.iris_osd2_tl >> 16) & 0xfff;
	temp1 = (temp1*frc_setting->memc_vres)/frc_setting->disp_vres;

	temp2 = pcfg->frc_setting.iris_osd2_br & 0xfff;
	temp2 = (temp2*frc_setting->memc_hres)/frc_setting->disp_hres;
	temp3 = (pcfg->frc_setting.iris_osd2_br >> 16) & 0xfff;
	temp3 = (temp3*frc_setting->memc_vres)/frc_setting->disp_vres;
	osd2_tl = (temp1 << 16) | temp0;
	osd2_br = (temp3 << 16) | temp2;
	//osd window 3
	temp0 = pcfg->frc_setting.iris_osd3_tl & 0xfff;
	temp0 = (temp0*frc_setting->memc_hres)/frc_setting->disp_hres;
	temp1 = (pcfg->frc_setting.iris_osd3_tl >> 16) & 0xfff;
	temp1 = (temp1*frc_setting->memc_vres)/frc_setting->disp_vres;

	temp2 = pcfg->frc_setting.iris_osd3_br & 0xfff;
	temp2 = (temp2*frc_setting->memc_hres)/frc_setting->disp_hres;
	temp3 = (pcfg->frc_setting.iris_osd3_br >> 16) & 0xfff;
	temp3 = (temp3*frc_setting->memc_vres)/frc_setting->disp_vres;
	osd3_tl = (temp1 << 16) | temp0;
	osd3_br = (temp3 << 16) | temp2;
	//osd window 4
	temp0 = pcfg->frc_setting.iris_osd4_tl & 0xfff;
	temp0 = (temp0*frc_setting->memc_hres)/frc_setting->disp_hres;
	temp1 = (pcfg->frc_setting.iris_osd4_tl >> 16) & 0xfff;
	temp1 = (temp1*frc_setting->memc_vres)/frc_setting->disp_vres;

	temp2 = pcfg->frc_setting.iris_osd4_br & 0xfff;
	temp2 = (temp2*frc_setting->memc_hres)/frc_setting->disp_hres;
	temp3 = (pcfg->frc_setting.iris_osd4_br >> 16) & 0xfff;
	temp3 = (temp3*frc_setting->memc_vres)/frc_setting->disp_vres;
	osd4_tl = (temp1 << 16) | temp0;
	osd4_br = (temp3 << 16) | temp2;

	IRIS_LOGD("%s, input osd protect area: osd0_tl = 0x%x, osd0_br = 0x%x, osd1_tl = 0x%x, osd1_br = 0x%x, osd2_tl = 0x%x, osd2_br = 0x%x", __func__,
			pcfg->frc_setting.iris_osd0_tl, pcfg->frc_setting.iris_osd0_br,
			pcfg->frc_setting.iris_osd1_tl, pcfg->frc_setting.iris_osd1_br,
			pcfg->frc_setting.iris_osd2_tl, pcfg->frc_setting.iris_osd2_br);
	IRIS_LOGD("%s, input osd protect area: osd3_tl = 0x%x, osd3_br = 0x%x, osd4_tl = 0x%x, osd4_br = 0x%x", __func__,
			pcfg->frc_setting.iris_osd3_tl, pcfg->frc_setting.iris_osd3_br,
			pcfg->frc_setting.iris_osd4_tl, pcfg->frc_setting.iris_osd4_br);
	IRIS_LOGD("%s, real osd protect area: osd0_tl = 0x%x, osd0_br = 0x%x, osd1_tl = 0x%x, osd1_br = 0x%x, osd2_tl = 0x%x, osd2_br = 0x%x", __func__,
			osd0_tl, osd0_br,
			osd1_tl, osd1_br,
			osd2_tl, osd2_br);
	IRIS_LOGD("%s, real osd protect area: osd3_tl = 0x%x, osd3_br = 0x%x, osd4_tl = 0x%x, osd4_br = 0x%x", __func__,
			osd3_tl, osd3_br,
			osd4_tl, osd4_br);
	IRIS_LOGD("%s, osd window ctrl:  = 0x%x, iris_osd_win_dynCompensate = 0x%x", __func__,
			pcfg->frc_setting.iris_osd_window_ctrl, pcfg->frc_setting.iris_osd_win_dynCompensate);

	if (pcfg->frc_setting.iris_osd_window_ctrl & 0x1249) { //osd protection
		iris_frc_reg_add(IRIS_FI_ADDR + FI_OSD0_TL, osd0_tl, 0);
		iris_frc_reg_add(IRIS_FI_ADDR + FI_OSD0_BR, osd0_br, 0);
		iris_frc_reg_add(IRIS_FI_ADDR + FI_OSD1_TL, osd1_tl, 0);
		iris_frc_reg_add(IRIS_FI_ADDR + FI_OSD1_BR, osd1_br, 0);
		iris_frc_reg_add(IRIS_FI_ADDR + FI_OSD2_TL, osd2_tl, 0);
		iris_frc_reg_add(IRIS_FI_ADDR + FI_OSD2_BR, osd2_br, 0);
		iris_frc_reg_add(IRIS_FI_ADDR + FI_OSD3_TL, osd3_tl, 0);
		iris_frc_reg_add(IRIS_FI_ADDR + FI_OSD3_BR, osd3_br, 0);
		iris_frc_reg_add(IRIS_FI_ADDR + FI_OSD4_TL, osd4_tl, 0);
		iris_frc_reg_add(IRIS_FI_ADDR + FI_OSD4_BR, osd4_br, 0);
		iris_frc_reg_add(IRIS_FI_ADDR + FI_OSD_WINDOW_CTRL, pcfg->frc_setting.iris_osd_window_ctrl, 0);
		//iris_frc_reg_add(IRIS_PROXY_MB0, pcfg->frc_setting.iris_osd_win_dynCompensate, 0);
	} else {
		iris_frc_reg_add(IRIS_FI_ADDR + FI_OSD_WINDOW_CTRL, 0x00000000, 0);
	}
}
int iris_mode_switch_update(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 mode =  pcfg->switch_mode;

	if (pcfg->abypss_ctrl.abypass_mode == ANALOG_BYPASS_MODE) {
		IRIS_LOGD("under abyp: switch_mode: %d", pcfg->switch_mode);
		return IRIS_MODE_BYPASS;
	}

	if (mode == IRIS_MODE_FRC_PREPARE)
		mode = IRIS_MODE_FRC_PREPARE_DONE;
	else if (mode == IRIS_MODE_RFB_PREPARE)
		mode = IRIS_MODE_RFB_PREPARE_DONE;
	else if (mode == IRIS_MODE_PT_PREPARE || mode == IRIS_MODE_PTLOW_PREPARE)
		mode = IRIS_MODE_PT_PREPARE_DONE;
	else if (mode == IRIS_MODE_RFB2FRC)
		if (pcfg->rx_mode == 1) { // command mode
			mode = iris_get_mode();
			if (mode != IRIS_MODE_FRC)
				mode = pcfg->switch_mode;	// keep original
		} else
			mode = IRIS_MODE_FRC;
		else if (mode == IRIS_MODE_FRC2RFB) {
			if (pcfg->rx_mode == 1) { // command mode
				mode = iris_get_mode();
				if (mode == IRIS_MODE_RFB || mode == IRIS_MODE_PT)
					mode = IRIS_MODE_RFB;
				else
					mode = pcfg->switch_mode;   // keep original
			} else
				mode = IRIS_MODE_RFB;
		} else if (mode == IRIS_MODE_RFB2PT)
			mode = IRIS_MODE_PT;
		else if (mode == IRIS_MODE_PT2RFB)
			mode = IRIS_MODE_RFB;
		else if (mode == IRIS_MODE_PT2BYPASS)
			mode = IRIS_MODE_BYPASS;
		else if (mode == IRIS_MODE_BYPASS2PT)
			mode = IRIS_MODE_PT;
		else if (mode == IRIS_MODE_FRC_CANCEL)
			mode = IRIS_MODE_RFB;
	pcfg->switch_mode = mode;
	return mode;
}

void iris_set_video_frame_rate_ms(u32 framerate)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	pcfg->frc_setting.in_fps = framerate/1000;
}

void iris_set_out_frame_rate(u32 framerate)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	IRIS_LOGI("%s, default out framerate: %u, set framerate: %u",
			__func__, pcfg->frc_setting.out_fps, framerate);

	//if (pcfg->frc_setting.default_out_fps == HIGH_FREQ) {
	//	//TODO: always set vtotal or not?
	//if ((pcfg->frc_setting.out_fps != framerate) &&
	//		(framerate == HIGH_FREQ || framerate == LOW_FREQ)) {
	pcfg->frc_setting.out_fps = framerate;

	//IRIS_LOGI("%s, change framerate to: %d", __func__, framerate);
	iris_dtg_frame_rate_set(framerate);
	//}
	//}
	pcfg->cur_fps_in_iris = framerate;
}

void iris_set_frc_var_display(int var_disp)
{
	iris_frc_var_disp = var_disp;
}

void iris_set_ap_te(u8 ap_te)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	if (ap_te > pcfg->panel_te) {
		IRIS_LOGI("%s, ap_te[%d] > panel_te[%d]", __func__, ap_te, pcfg->panel_te);
		ap_te = pcfg->panel_te;
	}

	if (ap_te != pcfg->ap_te) {
		pcfg->ap_te = ap_te;
		iris_frc_cmd_payload_init();
		iris_dtg_te_n2m_mode(0);
		iris_frc_cmd_payload_release();
	}
}

bool iris_update_vfr(struct iris_cfg *pcfg, bool enable)
{
	u32 frcc_pref_ctrl = pcfg->frc_setting.frcc_pref_ctrl;
	static u32 write_data[2];

	if (!mutex_trylock(&pcfg->panel->panel_lock)) {
		IRIS_LOGI("%s:%d panel_lock is locked!", __func__, __LINE__);
		mutex_lock(&pcfg->panel->panel_lock);
	}
	if (!pcfg->dynamic_vfr) {
		mutex_unlock(&pcfg->panel->panel_lock);
		IRIS_LOGI("dynamic_vfr is disable, return");
		return false;
	}

	if (iris_frc_var_disp) {
		if (enable)
			frcc_pref_ctrl |= 0x2;
		else
			frcc_pref_ctrl &= ~0x2;
		if (frcc_pref_ctrl == pcfg->frc_setting.frcc_pref_ctrl) {
			mutex_unlock(&pcfg->panel->panel_lock);
			IRIS_LOGI("same frcc_pref_ctrl value, return");
			return false;
		}
		pcfg->frc_setting.frcc_pref_ctrl = frcc_pref_ctrl;
		write_data[0] = IRIS_FRC_MIF_ADDR + FRCC_PERF_CTRL;
		write_data[1] = frcc_pref_ctrl;
		iris_ocp_write_mult_vals(2, write_data);
	}
	mutex_unlock(&pcfg->panel->panel_lock);
	return true;
}

void iris_update_frc_fps(u8 iris_fps)
{
	iris_get_cfg()->frc_setting.out_fps = iris_fps;
}

void iris_set_pwil_disp_ctrl(void)
{
	u32 write_data[4];
	uint32_t disp_ctrl0 = iris_pwil_disp_ctrl0();

	write_data[0] = IRIS_PWIL_ADDR + PWIL_DISP_CTRL0;
	write_data[1] = IRIS_PWIL_ADDR + disp_ctrl0;
	write_data[2] = IRIS_PWIL_ADDR + PWIL_REG_UPDATE;
	write_data[3] = 0x300;

	iris_ocp_write_mult_vals(4, write_data);
}

int iris_dbgfs_ms_init(struct dsi_display *display)
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

	debugfs_create_u32("frc_label", 0644, pcfg->dbg_root,
			(u32 *)&iris_frc_label);

	debugfs_create_u32("frc_var_disp_dual", 0644, pcfg->dbg_root,
		(u32 *)&iris_frc_var_disp_dual);
	debugfs_create_u32("frc_var_disp", 0644, pcfg->dbg_root,
		(u32 *)&iris_frc_var_disp_dbg);

	debugfs_create_u32("frc_mnt_level", 0644, pcfg->dbg_root,
			(u32 *)&iris_frc_mnt_level);
	debugfs_create_u32("frc_dynamic_off", 0644, pcfg->dbg_root,
			(u32 *)&iris_frc_dynamic_off);

	debugfs_create_u32("frc_pt_switch_on", 0644, pcfg->dbg_root,
		(u32 *)&iris_frc_pt_switch_on_dbg);

	debugfs_create_u32("mcu_enable", 0644, pcfg->dbg_root,
			(u32 *)&iris_mcu_enable);
	debugfs_create_u32("mcu_stop_check", 0644, pcfg->dbg_root,
			(u32 *)&iris_mcu_stop_check);
	debugfs_create_u32("frc_dma_disable", 0644, pcfg->dbg_root,
			(u32 *)&iris_frc_dma_disable);
	debugfs_create_u32("dsi_write", 0644, pcfg->dbg_root,
			(u32 *)&iris_w_path_select);
	debugfs_create_u32("dsi_read", 0644, pcfg->dbg_root,
			(u32 *)&iris_r_path_select);
	debugfs_create_u32("frc_demo_window", 0644, pcfg->dbg_root,
			(u32 *)&iris_frc_demo_window);
	debugfs_create_u32("dport_output_toggle", 0644, pcfg->dbg_root,
			(u32 *)&iris_dport_output_toggle);
	debugfs_create_u32("frc_fallback_disable", 0644, pcfg->dbg_root,
			(u32 *)&iris_frc_fallback_disable);
	debugfs_create_u32("dynamic_sadgain_mid", 0644, pcfg->dbg_root,
			(u32 *)&iris_dynamic_sadgain_mid);
	debugfs_create_u32("dynamic_sadcore_mid", 0644, pcfg->dbg_root,
			(u32 *)&iris_dynamic_sadcore_mid);
	debugfs_create_u32("dynamic_outgain_mid", 0644, pcfg->dbg_root,
			(u32 *)&iris_dynamic_outgain_mid);
	debugfs_create_u32("dynamic_outcore_mid", 0644, pcfg->dbg_root,
			(u32 *)&iris_dynamic_outcore_mid);
	debugfs_create_u32("frc_osd_protection", 0644, pcfg->dbg_root,
			(u32 *)&iris_frc_osd_protection);
	debugfs_create_u32("mvc_tuning", 0644, pcfg->dbg_root,
			(u32 *)&iris_mvc_tuning);
	debugfs_create_u32("mtnsw_low_th", 0644, pcfg->dbg_root,
			(u32 *)&iris_mtnsw_low_th);
	debugfs_create_u32("mtnsw_high_th", 0644, pcfg->dbg_root,
			(u32 *)&iris_mtnsw_high_th);
	debugfs_create_u32("frcpt_switch_th", 0644, pcfg->dbg_root,
			(u32 *)&iris_frcpt_switch_th);
	debugfs_create_u32("te_dly_frc", 0644, pcfg->dbg_root,
			(u32 *)&iris_te_dly_frc);
	debugfs_create_u32("display_vtotal", 0644, pcfg->dbg_root,
			(u32 *)&iris_display_vtotal);
	return 0;
}
