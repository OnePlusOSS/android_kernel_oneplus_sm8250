#ifndef _DSI_IRIS5_DEF_H_
#define _DSI_IRIS5_DEF_H_

/*
 * These files include source code for the Licensed Software that is provided under the terms and
 * conditions of a Master Software License Agreement and its applicable schedules.
 */

enum {
	IRIS_DTSI0_PIP_IDX = 0,
	IRIS_DTSI1_PIP_IDX,
	IRIS_LUT_PIP_IDX,
	IRIS_PIP_IDX_CNT
};

enum {
	IRIS_IP_SYS = 0x00,
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

	IRIS_IP_CNT
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

enum {
	IRIS_CONT_SPLASH_LK = 1,
	IRIS_CONT_SPLASH_KERNEL,
	IRIS_CONT_SPLASH_NONE,
	IRIS_CONT_SPLASH_BYPASS,
	IRIS_CONT_SPLASH_BYPASS_PRELOAD,
};

enum iris_abypass_status {
	PASS_THROUGH_MODE = 0,
	ANALOG_BYPASS_MODE,
	ABP2PT_SWITCHING,
	MAX_MODE = 255,
};

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
	IRIS_DBC_LCE_POWER= 52,
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
	IRIS_MIPI2RX_PWRST= 133,
	IRIS_DUAL2SINGLE_ST = 134,
	IRIS_MEMC_OSD = 135,
	IRIS_MEMC_OSD_PROTECT = 136,
	IRIS_LCE_DEMO_WINDOW = 137,
	IRIS_CONFIG_TYPE_MAX
};

enum SDR2HDR_CASE{
	SDR2HDR_Bypass = 0,
	HDR10In_ICtCp,
	HDR10In_YCbCr,
	ICtCpIn_YCbCr,
	SDR709_2_709,
	SDR709_2_p3,
	SDR709_2_2020,
};

enum SDR2HDR_LUT_GAMMA_INDEX{
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
	UNKOWN_VER
};

#endif // _DSI_IRIS5_DEF_H_
