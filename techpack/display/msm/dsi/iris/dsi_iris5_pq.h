// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#ifndef _DSI_IRIS_PQ_H_
#define _DSI_IRIS_PQ_H_


enum {
	IRIS_LCE_GRAPHIC = 0x00,
	IRIS_LCE_VIDEO,
};

enum {
	IRIS_COLOR_TEMP_OFF = 0x00,
	IRIS_COLOR_TEMP_MANUL,
	IRIS_COLOR_TEMP_AUTO,
};

enum {
	IRIS_MAGENTA_GAIN_TYPE = 0,
	IRIS_RED_GAIN_TYPE,
	IRIS_YELLOW_GAIN_TYPE,
	IRIS_GREEN_GAIN_TYPE,
	IRIS_BLUE_GAIN_TYPE,
	IRIS_CYAN_GAIN_TYPE,
};

#define IP_OPT_MAX				20

void iris_set_skip_dma(bool skip);

void iris_pq_parameter_init(void);

void iris_peaking_level_set(u32 level);

void iris_cm_6axis_level_set(u32 level);

void iris_cm_csc_level_set(u32 csc_ip, u32 *csc_value);

void iris_cm_ftc_enable_set(u32 level);

void iris_scurve_enable_set(u32 level);

void iris_cm_colortemp_mode_set(u32 mode);

void iris_cm_color_temp_set(void);

u32 iris_cm_ratio_set_for_iic(void);

void iris_cm_color_gamut_pre_set(u32 source_switch);

void iris_cm_color_gamut_pre_clear(void);

void iris_cm_color_gamut_set(u32 level);

void iris_dpp_gamma_set(void);

void iris_lce_mode_set(u32 mode);

void iris_lce_level_set(u32 level);

void iris_lce_graphic_det_set(bool enable);

void iris_lce_al_set(bool enable);

void iris_lce_demo_window_set(u32 vsize, u32 hsize, u8 inwnd);

void iris_dbc_level_set(u32 level);

void iris_pwm_freq_set(u32 value);

void iris_pwm_enable_set(bool enable);

void iris_dbc_bl_user_set(u32 value);

void iris_dbc_led0d_gain_set(u32 value);

void iris_reading_mode_set(u32 level);

void iris_lce_lux_set(void);
void iris_ambient_light_lut_set(u32 lut_offset);
u32 iris_sdr2hdr_lut2ctl_get(void);
void iris_sdr2hdr_lut2ctl_set(u32 value);
void iris_maxcll_lut_set(u32 lutpos);
u32 iris_sdr2hdr_lutyctl_get(void);
void iris_sdr2hdr_lutyctl_set(u32 value);

void iris_dbclce_datapath_set(bool bEn);

void iris_dbclce_power_set(bool bEn);

void iris_dbc_compenk_set(u8 lut_table_index);
void iris_sdr2hdr_level_set(u32 level);
void iris_pwil_sdr2hdr_resolution_set(bool enter_frc_mode);
void iris_panel_nits_set(u32 bl_ratio, bool bSystemRestore, int level);
void iris_scaler_filter_update(u8 scaler_type, u32 level);

void iris_peaking_idle_clk_enable(bool enable);

void iris_cm_6axis_seperate_gain(u8 gain_type, u32 value);

void iris_init_ipopt_ip(struct iris_update_ipopt *ipopt,  int len);
void iris_hdr_csc_prepare(void);
void iris_hdr_csc_complete(int step);
void iris_hdr_csc_frame_ready(void);
uint32_t iris_frc_variable_set(int frc_var_disp);
void iris_frc_force_repeat(bool enable);
void iris_pwil_frc_pt_set(int frc_pt_switch_on);
void iris_pwil_frc_ratio_set(int out_fps_ratio, int in_fps_ratio);
void iris_pwil_frc_video_ctrl2_set(int mvc_0_1_phase);
void iris_scaler_filter_ratio_get(void);
void iris_pwil_cmd_disp_mode_set(bool cmd_disp_on);
void iris_psf_mif_efifo_set(u8 mode, bool osd_enable);
void iris_psf_mif_dyn_addr_set(bool dyn_adrr_enable);
void iris_ms_pwil_dma_update(struct iris_mspwil_parameter *par);
void iris_dtg_frame_rate_set(u32 framerate);

int32_t iris_update_ip_opt(
		struct iris_update_ipopt *popt, int len, uint8_t ip,
		uint8_t opt_id, uint8_t skip_last);

int iris_dbgfs_pq_init(struct dsi_display *display);

u8 iris_get_dbc_lut_index(void);

struct iris_setting_info *iris_get_setting(void);

void iris_set_yuv_input(bool val);

void iris_set_HDR10_YCoCg(bool val);

bool iris_get_debug_cap(void);

void iris_set_debug_cap(bool val);

void iris_set_sdr2hdr_mode(u8 val);

void iris_quality_setting_off(void);

struct msmfb_iris_ambient_info *iris_get_ambient_lut(void);

struct msmfb_iris_maxcll_info *iris_get_maxcll_info(void);

void iris_scaler_gamma_enable(bool lightup_en, u32 level);

void iris_dom_set(int mode);

void iris_brightness_level_set(u32 *value);

int32_t iris_parse_color_temp_range(struct device_node *np, struct iris_cfg *pcfg);

int32_t iris_parse_default_pq_param(struct device_node *np, struct iris_cfg *pcfg);

void iris_cm_setting_switch(bool dual);
#endif // _DSI_IRIS_PQ_H_
