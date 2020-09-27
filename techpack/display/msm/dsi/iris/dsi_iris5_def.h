// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#ifndef _DSI_IRIS_DEF_H_
#define _DSI_IRIS_DEF_H_


// Use Iris Analog bypass mode to light up panel
// Note: input timing should be same with output timing
//#define IRIS_ABYP_LIGHTUP
//#define IRIS_MIPI_TEST
#define IRIS_CFG_NUM	2

#define IRIS_FIRMWARE_NAME	"iris5.fw"
#define IRIS_CCF1_FIRMWARE_NAME "iris5_ccf1.fw"
#define IRIS_CCF2_FIRMWARE_NAME "iris5_ccf2.fw"
#define IRIS_CCF1_CALIBRATED_FIRMWARE_NAME "iris5_ccf1b.fw"
#define IRIS_CCF2_CALIBRATED_FIRMWARE_NAME "iris5_ccf2b.fw"
#define IRIS3_CHIP_VERSION	0x6933
#define IRIS5_CHIP_VERSION	0x6935

#define DIRECT_BUS_HEADER_SIZE 8

#define LUT_LEN 256
#define CM_LUT_GROUP 3 // table 0,3,6 should store at the same address in iris
#define SCALER1D_LUT_NUMBER 9
#define SDR2HDR_LUT_BLOCK_SIZE (128*4)
#define SDR2HDR_LUT2_BLOCK_NUMBER (6)
#define SDR2HDR_LUTUVY_BLOCK_NUMBER (12)
#define SDR2HDR_LUT2_ADDRESS 0x3000
#define SDR2HDR_LUTUVY_ADDRESS 0x6000
#define SDR2HDR_LUT_BLOCK_ADDRESS_INC 0x400
#define SDR2HDR_LUT2_BLOCK_CNT (6)  //for ambient light lut
#define SDR2HDR_LUTUVY_BLOCK_CNT (12)  // for maxcll lut

#define PANEL_BL_MAX_RATIO 10000
#define IRIS_MODE_RFB                   0x0
#define IRIS_MODE_FRC_PREPARE           0x1
#define IRIS_MODE_FRC_PREPARE_DONE      0x2
#define IRIS_MODE_FRC                   0x3
#define IRIS_MODE_FRC_CANCEL            0x4
#define IRIS_MODE_FRC_PREPARE_RFB       0x5
#define IRIS_MODE_FRC_PREPARE_TIMEOUT   0x6
#define IRIS_MODE_RFB2FRC               0x7
#define IRIS_MODE_RFB_PREPARE           0x8
#define IRIS_MODE_RFB_PREPARE_DONE      0x9
#define IRIS_MODE_RFB_PREPARE_TIMEOUT   0xa
#define IRIS_MODE_FRC2RFB               0xb
#define IRIS_MODE_PT_PREPARE            0xc
#define IRIS_MODE_PT_PREPARE_DONE       0xd
#define IRIS_MODE_PT_PREPARE_TIMEOUT    0xe
#define IRIS_MODE_RFB2PT                0xf
#define IRIS_MODE_PT2RFB                0x10
#define IRIS_MODE_PT                    0x11
#define IRIS_MODE_KICKOFF60_ENABLE      0x12
#define IRIS_MODE_KICKOFF60_DISABLE     0x13
#define IRIS_MODE_PT2BYPASS             0x14
#define IRIS_MODE_BYPASS                0x15
#define IRIS_MODE_BYPASS2PT             0x16
#define IRIS_MODE_PTLOW_PREPARE         0x17
#define IRIS_MODE_DSI_SWITCH_2PT        0x18    // dsi mode switch during RFB->PT
#define IRIS_MODE_DSI_SWITCH_2RFB       0x19    // dsi mode switch during PT->RFB
#define IRIS_MODE_FRC_POST              0x1a    // for set parameters after FRC
#define IRIS_MODE_RFB_PREPARE_DELAY     0x1b    // for set parameters before RFB
#define IRIS_MODE_RFB_POST              0x1c    // for set parameters after RFB
#define IRIS_MODE_INITING               0xff
#define IRIS_MODE_OFF                   0xf0
#define IRIS_MODE_HDR_EN                0x20

enum DBC_LEVEL {
	DBC_INIT = 0,
	DBC_OFF,
	DBC_LOW,
	DBC_MIDDLE,
	DBC_HIGH,
	CABC_DLV_OFF = 0xF1,
	CABC_DLV_LOW,
	CABC_DLV_MIDDLE,
	CABC_DLV_HIGH,
};

enum SDR2HDR_LEVEL {
	SDR2HDR_LEVEL0 = 0,
	SDR2HDR_LEVEL1,
	SDR2HDR_LEVEL2,
	SDR2HDR_LEVEL3,
	SDR2HDR_LEVEL4,
	SDR2HDR_LEVEL5,
	SDR2HDR_LEVEL_CNT
};

enum SDR2HDR_TABLE_TYPE {
	SDR2HDR_LUT0 = 0,
	SDR2HDR_LUT1,
	SDR2HDR_LUT2,
	SDR2HDR_LUT3,
	SDR2HDR_UVY0,
	SDR2HDR_UVY1,
	SDR2HDR_UVY2,
	SDR2HDR_INV_UV0,
	SDR2HDR_INV_UV1,
};

enum FRC_PHASE_TYPE {
	FRC_PHASE_V1_128 = 0,
	FRC_PHASE_V2_12TO60_25,
	FRC_PHASE_V2_12TO90_75,
	FRC_PHASE_V2_12TO120_50,
	FRC_PHASE_V2_15TO60_20,
	FRC_PHASE_V2_15TO90_30,
	FRC_PHASE_V2_15TO120_40,
	FRC_PHASE_V2_24TO60_25,
	FRC_PHASE_V2_24TO90_75,
	FRC_PHASE_V2_24TO120_50,
	FRC_PHASE_V2_25TO60_60,
	FRC_PHASE_V2_25TO90_90,
	FRC_PHASE_V2_25TO120_120,
	FRC_PHASE_V2_30TO60_20,
	FRC_PHASE_V2_30TO90_30,
	FRC_PHASE_V2_30TO120_40,
	FRC_PHASE_V2_60TO90_15,
	FRC_PHASE_V2_60TO120_20,
	FRC_PHASE_TYPE_CNT
};

enum {
	IRIS_CONT_SPLASH_LK = 1,
	IRIS_CONT_SPLASH_KERNEL,
	IRIS_CONT_SPLASH_NONE,
	IRIS_CONT_SPLASH_BYPASS,
	IRIS_CONT_SPLASH_BYPASS_PRELOAD,
};

enum {
	IRIS_DTSI0_PIP_IDX = 0,
	IRIS_DTSI1_PIP_IDX,
	IRIS_LUT_PIP_IDX,
	IRIS_PIP_IDX_CNT,

	IRIS_DTSI_NONE = 0xFF,
};

enum {
	IRIS_IP_START = 0x00,
	IRIS_IP_SYS = IRIS_IP_START,
	IRIS_IP_RX = 0x01,
	IRIS_IP_TX = 0x02,
	IRIS_IP_PWIL = 0x03,
	IRIS_IP_DPORT = 0x04,
	IRIS_IP_DTG = 0x05,
	IRIS_IP_PWM = 0x06,
	IRIS_IP_DSC_DEN = 0x07,
	IRIS_IP_DSC_ENC = 0x08,
	IRIS_IP_SDR2HDR = 0x09,
	IRIS_IP_CM = 0x0a,
	IRIS_IP_SCALER1D = 0x0b,
	IRIS_IP_PEAKING = 0x0c,
	IRIS_IP_LCE = 0x0d,
	IRIS_IP_DPP = 0x0e,
	IRIS_IP_DBC = 0x0f,
	IRIS_IP_EXT = 0x10,
	IRIS_IP_DMA = 0x11,

	IRIS_IP_RX_2 = 0x021,
	IRIS_IP_SRAM = 0x022,
	IRIS_IP_PWIL_2 = 0x023,
	IRIS_IP_DSC_ENC_2 = 0x24,
	IRIS_IP_DSC_DEN_2 = 0x25,
	IRIS_IP_PBSEL_1 = 0x26,
	IRIS_IP_PBSEL_2 = 0x27,
	IRIS_IP_PBSEL_3 = 0x28,
	IRIS_IP_PBSEL_4 = 0x29,
	IRIS_IP_OSD_COMP = 0x2a,
	IRIS_IP_OSD_DECOMP = 0x2b,
	IRIS_IP_OSD_BW = 0x2c,
	IRIS_IP_PSR_MIF = 0x2d,
	IRIS_IP_BLEND = 0x2e,
	IRIS_IP_SCALER1D_2 = 0x2f,
	IRIS_IP_FRC_MIF = 0x30,
	IRIS_IP_FRC_DS = 0x31,
	IRIS_IP_GMD = 0x32,
	IRIS_IP_FBD = 0x33,
	IRIS_IP_CADDET = 0x34,
	IRIS_IP_MVC = 0x35,
	IRIS_IP_FI = 0x36,
	IRIS_IP_DSC_DEC_AUX = 0x37,

	IRIS_IP_END,
	IRIS_IP_CNT = IRIS_IP_END
};

enum LUT_TYPE {
	LUT_IP_START = 128, /*0x80*/
	DBC_LUT = LUT_IP_START,
	CM_LUT,
	SDR2HDR_LUT,
	SCALER1D_LUT,
	AMBINET_HDR_GAIN, /*HDR case*/
	AMBINET_SDR2HDR_LUT, /*SDR2HDR case;*/
	GAMMA_LUT,
	FRC_PHASE_LUT,
	APP_CODE_LUT,
	DPP_DITHER_LUT,
	SCALER1D_PP_LUT,
	DTG_PHASE_LUT,
	APP_VERSION_LUT,
	LUT_IP_END
};

enum FIRMWARE_STATUS {
	FIRMWARE_LOAD_FAIL,
	FIRMWARE_LOAD_SUCCESS,
	FIRMWARE_IN_USING,
};

enum result {
	IRIS_FAILED = -1,
	IRIS_SUCCESS = 0,
};

enum PANEL_TYPE {
	PANEL_LCD_SRGB = 0,
	PANEL_LCD_P3,
	PANEL_OLED,
};

enum LUT_MODE {
	INTERPOLATION_MODE = 0,
	SINGLE_MODE,
};

enum SCALER_IP_TYPE {
	SCALER_INPUT = 0,
	SCALER_PP,
};

struct iris_pq_setting {
	u32 peaking:4;
	u32 cm6axis:2;
	u32 cmcolortempmode:2;
	u32 cmcolorgamut:4;
	u32 lcemode:2;
	u32 lcelevel:3;
	u32 graphicdet:1;
	u32 alenable:1;
	u32 dbc:2;
	u32 demomode:3;
	u32 sdr2hdr:4;
	u32 readingmode:4;
};

struct quality_setting {
	struct iris_pq_setting pq_setting;
	u32 cctvalue;
	u32 colortempvalue;
	u32 luxvalue;
	u32 maxcll;
	u32 source_switch;
	u32 al_bl_ratio;
	u32 system_brightness;
	u32 min_colortempvalue;
	u32 max_colortempvalue;
	u32 dspp_dirty;
	u32 scurvelevel;
	u32 cmftc;
};

struct iris_setting_info {
	struct quality_setting quality_cur;
	struct quality_setting quality_def;
};
struct ocp_header {
	u32 header;
	u32 address;
};

struct iris_update_ipopt {
	uint8_t ip;
	uint8_t opt_old;
	uint8_t opt_new;
	uint8_t skip_last;
};

struct iris_update_regval {
	uint8_t ip;
	uint8_t opt_id;
	uint16_t reserved;
	uint32_t mask;
	//uint32_t addr;
	uint32_t value;
};

struct iris_lp_ctrl {
	bool dynamic_power;
	bool ulps_lp;
	bool abyp_enable;
	bool esd_enable;
	int esd_cnt;
};

struct iris_abypass_ctrl {
	bool analog_bypass_disable;
	uint8_t abypass_mode;
	uint16_t pending_mode;	// pending_mode is accessed by SDEEncoder and HWBinder
	int abyp_switch_state;
	int frame_delay;
	struct mutex abypass_mutex;
};

struct iris_frc_setting {
	u8 memc_level;
	u8 mv_buf_num;
	u8 in_fps;
	u8 out_fps;
	u16 disp_hres;
	u16 disp_vres;
	u16 input_vtotal;
	u16 disp_htotal;
	u16 disp_vtotal;
	uint32_t memc_hres;
	uint32_t memc_vres;
	uint32_t memc_dsc_bpp;
	u32 video_baseaddr;
	u32 mv_baseaddr;
	u8 v2_lut_index;
	u8 v2_phaselux_idx_max;
	u32 v2_period_phasenum;
	u8 in_fps_configured;
	u8 default_out_fps;
	u8 memc_osd;
	u32 frcc_pref_ctrl;
	uint32_t iris_osd0_tl;
	uint32_t iris_osd1_tl;
	uint32_t iris_osd2_tl;
	uint32_t iris_osd3_tl;
	uint32_t iris_osd4_tl;
	uint32_t iris_osd0_br;
	uint32_t iris_osd1_br;
	uint32_t iris_osd2_br;
	uint32_t iris_osd3_br;
	uint32_t iris_osd4_br;
	uint32_t iris_osd_window_ctrl;
	uint32_t iris_osd_win_dynCompensate;
	uint32_t short_video;
};

struct iris_mspwil_parameter {
	int frc_var_disp;	// -1: mean no update
	int frc_pt_switch_on;	// -1: mean no update
	int cmd_disp_on;	// -1: mean no update
	int ratio_update;
	int out_fps_ratio;
	int in_fps_ratio;
	int mvc_01phase_update;
	int mvc_01phase;
};

enum pwil_mode {
	PT_MODE,
	RFB_MODE,
	FRC_MODE,
};

enum iris_config_type {
	IRIS_PEAKING = 0,
	IRIS_MEMC_LEVEL = 5,
	USER_DEMO_WND = 17,
	IRIS_CHIP_VERSION = 33,      // 0x0 : IRIS2, 0x1 : IRIS2-plus, 0x2 : IRIS3-lite
	IRIS_LUX_VALUE = 34,
	IRIS_CCT_VALUE = 35,
	IRIS_READING_MODE = 36,

	IRIS_CM_6AXES = 37,
	IRIS_CM_FTC_ENABLE = 38,
	IRIS_CM_COLOR_TEMP_MODE = 39,
	IRIS_CM_COLOR_GAMUT = 40,
	IRIS_LCE_MODE = 41,
	IRIS_LCE_LEVEL = 42,
	IRIS_GRAPHIC_DET_ENABLE = 43,
	IRIS_AL_ENABLE = 44,			//AL means ambient light
	IRIS_DBC_LEVEL = 45,
	IRIS_DEMO_MODE = 46,
	IRIS_SDR2HDR = 47,
	IRIS_COLOR_TEMP_VALUE = 48,
	IRIS_HDR_MAXCLL = 49,
	IRIS_CM_COLOR_GAMUT_PRE = 51,
	IRIS_DBC_LCE_POWER = 52,
	IRIS_DBC_LCE_DATA_PATH = 53,
	IRIS_DYNAMIC_POWER_CTRL = 54,
	IRIS_DMA_LOAD = 55,
	IRIS_ANALOG_BYPASS_MODE = 56,
	IRIS_PANEL_TYPE = 57,
	IRIS_HDR_PANEL_NITES_SET = 60,
	IRIS_PEAKING_IDLE_CLK_ENABLE = 61,
	IRIS_CM_MAGENTA_GAIN = 62,
	IRIS_CM_RED_GAIN = 63,
	IRIS_CM_YELLOW_GAIN = 64,
	IRIS_CM_GREEN_GAIN = 65,
	IRIS_CM_BLUE_GAIN = 66,
	IRIS_CM_CYAN_GAIN = 67,
	IRIS_BLC_PWM_ENABLE = 68,
	IRIS_DBC_LED_GAIN = 69,
	IRIS_SCALER_FILTER_LEVEL = 70,
	IRIS_CCF1_UPDATE = 71,
	IRIS_CCF2_UPDATE = 72,
	IRIS_FW_UPDATE = 73,
	IRIS_HUE_SAT_ADJ = 74,
	IRIS_SCALER_PP_FILTER_LEVEL = 76,
	IRIS_CSC_MATRIX = 75,
	IRIS_CONTRAST_DIMMING = 80,
	IRIS_S_CURVE = 81,
	IRIS_BRIGHTNESS_CHIP = 82,
	IRIS_HDR_PREPARE = 90,
	IRIS_HDR_COMPLETE = 91,
	IRIS_MCF_DATA = 92,
	IRIS_Y5P = 93,
	IRIS_PANEL_NITS = 99,

	IRIS_DBG_TARGET_REGADDR_VALUE_GET = 103,
	IRIS_DBG_TARGET_REGADDR_VALUE_SET = 105,
	IRIS_DBG_KERNEL_LOG_LEVEL = 106,
	IRIS_DBG_SEND_PACKAGE = 107,
	IRIS_DBG_LOOP_BACK_MODE = 108,
	IRIS_DBG_LOOP_BACK_MODE_RES = 109,
	IRIS_DBG_TARGET_REGADDR_VALUE_SET2 = 112,
	IRIS_DEBUG_CAP = 113,

	IRIS_MODE_SET = 120,
	IRIS_VIDEO_FRAME_RATE_SET = 121,
	IRIS_OUT_FRAME_RATE_SET = 122,	// debug only
	IRIS_OSD_ENABLE = 123,
	IRIS_OSD_AUTOREFRESH = 124,
	IRIS_OSD_OVERFLOW_ST = 125,
	// [23-16]: pwil mode, [15-8]: tx mode, [7-0]: rx mode
	IRIS_WORK_MODE = 126,
	IRIS_FRC_LOW_LATENCY = 127,
	IRIS_PANEL_TE = 128,
	IRIS_AP_TE = 129,
	IRIS_N2M_ENABLE = 130,
	IRIS_WAIT_VSYNC = 132,
	IRIS_MIPI2RX_PWRST = 133,
	IRIS_DUAL2SINGLE_ST = 134,
	IRIS_MEMC_OSD = 135,
	IRIS_MEMC_OSD_PROTECT = 136,
	IRIS_LCE_DEMO_WINDOW = 137,
	IRIS_CM_PARA_SET = 138,
	IRIS_PARAM_VALID = 144,
	IRIS_CONFIG_TYPE_MAX
};

enum SDR2HDR_CASE {
	SDR2HDR_Bypass = 0,
	HDR10In_ICtCp,
	HDR10In_YCbCr,
	ICtCpIn_YCbCr,
	SDR709_2_709,
	SDR709_2_p3,
	SDR709_2_2020,
};

enum SDR2HDR_LUT_GAMMA_INDEX {
	SDR2HDR_LUT_GAMMA_120 = 0,
	SDR2HDR_LUT_GAMMA_106 = 1,
	SDR2HDR_LUT_GAMMA_102 = 2,
	SDR2HDR_LUT_GAMMA_100 = 3,
	SDR2HDR_LUT_GAMMA_MAX
};

enum iris_chip_version {
	IRIS2_VER = 0,
	IRIS2PLUS_VER,
	IRIS3LITE_VER,
	IRIS5_VER,
	IRIS6_VER,
	IRISSOFT_VER,
	IRIS5DUAL_VER,
	UNKOWN_VER
};

enum iris_abypass_status {
	PASS_THROUGH_MODE = 0,
	ANALOG_BYPASS_MODE,
	ABP2PT_SWITCHING,
	MAX_MODE = 255,
};

#endif // _DSI_IRIS_DEF_H_
