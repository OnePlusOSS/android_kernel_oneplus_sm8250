#ifndef MDSS_DSI_IRIS_MODE_SWITCH
#define MDSS_DSI_IRIS_MODE_SWITCH

/*
 * These files include source code for the Licensed Software that is provided under the terms and
 * conditions of a Master Software License Agreement and its applicable schedules.
 */

#define GRCP_HEADER 4

#define CEILING(x,y) (((x)+((y)-1))/(y))

#define IRIS_FRC_MIF_ADDR	0xf2020000
#define IRIS_GMD_ADDR		0xf20a0000
#define IRIS_FBD_ADDR		0xf20c0000
#define IRIS_CAD_ADDR		0xf20e0000
#define IRIS_MVC_ADDR		0xf2100000
#define IRIS_FI_ADDR		0xf2160000
#define IRIS_PWIL_ADDR		0xf1240000
#define IRIS_PSR_ADDR		0xf1400000
#define IRIS_SRAM_CTRL_ADDR	0xf1040000
#define IRIS_SCALER_IN_ADDR	0xf1a20000
#define IRIS_SCALER_PP_ADDR	0xf1a40000
#define IRIS_BLENDING_ADDR	0xf1540000
#define IRIS_CM_ADDR		0xf1560000
#define IRIS_DTG_ADDR		0xf1200000
#define IRIS_SYS_ADDR		0xf0000000

/*FRC_MIF*/
#define FRCC_CTRL_REG0		0x10000
#define FRCC_CTRL_REG1		0x10004
#define FRCC_CTRL_REG2		0x10008
#define FRCC_CTRL_REG3		0x1000c
#define FRCC_CTRL_REG4		0x10010
#define FRCC_CTRL_REG5		0x10014
#define FRCC_CTRL_REG6		0x10018
#define FRCC_CTRL_REG7		0x1001c
#define FRCC_CTRL_REG8		0x10020
#define FRCC_CTRL_REG9		0x10024
#define FRCC_CTRL_REG10		0x10028
#define FRCC_CTRL_REG11		0x1002c
#define FRCC_CTRL_REG12		0x10030
#define FRCC_CTRL_REG13		0x10034
#define FRCC_CTRL_REG14		0x10038
#define FRCC_CTRL_REG15		0x1003c
#define FRCC_CTRL_REG16		0x10040
#define FRCC_CTRL_REG17		0x10044
#define FRCC_CTRL_REG18		0x10048
#define FRCC_CMD_MOD_TH		0x1004c
#define FRCC_DTG_SYNC		0x10060
#define FRCC_SWITCH_CTRL	0x10074
#define FRCC_SWFBK_CTRL		0x10078
#define FRCC_01PHASE_CTRL0	0x1007c
#define FRCC_01PHASE_CTRL1      0x10080
#define FRCC_TEOFFSET_CTRL	0x10090
#define FRCC_PERF_CTRL		0x10094
#define FRCC_TEOFFSET_ADJ	0x10098
#define FRCC_TEADJ_TH		0x1009c
#define FRCC_MNTSWITCH_TH	0x100a0
#define FRCC_EXT_MOTION		0x100a4
#define FRCC_EXT_TE_OFFSET	0x100a8
#define FRCC_PHASELUT_CLIP	0x100ac
#define FRCC_RW_PROT_TH		0x100b0
#define FRCC_REC_META_CTRL	0x100b4
#define FRCC_METAGEN3		0x100b8
#define FRCC_FRCPT_SWITCH_CTRL	0x100c0
#define FRCC_REG_SHDW		0x11198
#define IMIF_MODE_CTRL		0x12000
#define IMIF_DW_PER_LINE	0x12008
#define IMIF_VSIZE			0x1200c
#define IMIF_SW_UPDATE_EN	0x13400
#define MMIF_CTRL1			0x14000
#define MMIF_CTRL2			0x14004
#define MMIF_PHASE1_BA		0x140f0
#define MMIF_PHASE0_BA		0x140f4
#define MMIF_UPDATE			0x15020
#define FMIF_CTRL			0x16000
#define FMIF_VD_FRM_ATTRIBUTE0	0x16004
#define FMIF_VD_FRM_ATTRIBUTE1	0x16008
#define FMIF_MV_FRM_ATTRIBUTE0	0x16014
#define FMIF_MV_FRM_ATTRIBUTE1	0x16020
#define FMIF_REG_SHDW		0x17000

/* GMD */
#define GMD_GAIN		0x00000
#define GMD_FILT		0x00004
#define GMD_ACCUM		0x00008
#define GMD_SHIFT		0x0000c
#define GMD_START		0x00010
#define GMD_STOP		0x00014
#define GMD_CTRL		0x00020

/* FBD */
#define FILMBD_RESOLUTION		0x00018
#define FILMBD_WIN_STOP_SET		0x00020
#define FILMBD_TOP_CTRL			0x00024

/* CAD */
#define NEW_FRM_FLG_DET_1		0x00004
#define CAD_DET_BASIC_CAD		0x0000c
#define CAD_DET_STVT			0x00010
#define CAD_DET_BAD_EDIT		0x00014
#define CAD_DET_VOF_0			0x00018
#define COMMON				0x00030
#define DUMMY				0x00040
#define SW_DONE				0x1ffd4

/* MVC */
#define MVC_CTRL_0				0x00000
#define MVC_TOP_CTRL_0			0x0000c
#define GLB_MVSELECT_1			0x00080
#define GLB_MVSELECT_2			0x00084
#define HALORED_2			0x00094
#define MVC_OSDDET_0			0x000b8
#define MVC_OSDDET_1			0x000bc
#define MVC_POSTFILT_1			0x000dc
#define MVC_POSTGLBDC_0			0x00118
#define MVC_SAD_2			0x00180
#define HISTMV_CTRL_1			0x00190
#define HISTMV_CTRL_2			0x00194
#define HISTMV_CTRL_3			0x00198
#define HISTMV_CTRL_4			0x0019c
#define HISTMV_STEP			0x001a0
#define HISTMV_BASE			0x001a4
#define HLMD_CTRL				0x001d4
#define MVC_SW_UPDATE			0x1ff00

/* FI */
#define FI_CLOCK_GATING			0x0000c
#define FI_RANGE_CTRL			0x00014
#define FI_DEMO_COL_SIZE		0x00018
#define FI_DEMO_MODE_CTRL		0x0001c
#define FI_DEMO_MODE_RING		0x00020
#define FI_DEMO_ROW_SIZE		0x00024
#define FI_OSD0_TL			0x00038
#define FI_OSD1_TL			0x0003c
#define FI_OSD2_TL			0x00040
#define FI_OSD3_TL			0x00044
#define FI_OSD4_TL			0x00048
#define FI_OSD0_BR			0x0004c
#define FI_OSD1_BR			0x00050
#define FI_OSD2_BR			0x00054
#define FI_OSD3_BR			0x00058
#define FI_OSD4_BR			0x0005c
#define FI_OSD_WINDOW_CTRL		0x00060
#define FI_VIDEO_BUF_CTRL		0x00064
#define FI_V9_GENERIC_CTRL		0x00078
#define FI_MISC_CTRL			0x0007c
#define FI_CSC_CTRL				0x00100
#define FI_CSC_COEF0			0x00104
#define FI_CSC_COEF1			0x00108
#define FI_CSC_COEF2			0x0010c
#define FI_CSC_COEF3			0x00110
#define FI_CSC_COEF4			0x00114
#define FI_CSC_OFFSET0			0x00118
#define FI_CSC_OFFSET1			0x0011c
#define FI_CSC_OFFSET2			0x00120
#define FI_SHDW_CTRL			0x1ff00

/* PWIL */
#define PWIL_CTRL				0x00000
#define PWIL_CTRL1				0x00004
#define DATA_PATH_CTRL			0x00008
#define DATA_PATH_CTRL1			0x0000c
#define PWIL_STATUS				0x00030
#define PWIL_VIDEO_CTRL0		0x01028
#define PWIL_VIDEO_CTRL1		0x0102c
#define PWIL_VIDEO_CTRL2		0x01030
#define PWIL_VIDEO_CTRL3		0x01034
#define PWIL_VIDEO_CTRL4		0x01038
#define PWIL_VIDEO_CTRL5		0x0103c
#define PWIL_VIDEO_CTRL6		0x01040
#define PWIL_VIDEO_CTRL7		0x01044
#define PWIL_VIDEO_CTRL8		0x01048
#define PWIL_VIDEO_CTRL11		0x0104c
#define PWIL_VIDEO_CTRL12		0x01050
#define PWIL_CSC_CTRL			0x00078
#define PWIL_CSC_COEF0			0x0007c
#define PWIL_CSC_COEF1			0x00080
#define PWIL_CSC_COEF2			0x00084
#define PWIL_CSC_COEF3			0x00088
#define PWIL_CSC_COEF4			0x0008c
#define PWIL_CSC_OFFSET0		0x00090
#define PWIL_CSC_OFFSET1		0x00094
#define PWIL_CSC_OFFSET2		0x00098
#define PWIL_PIAD_FRC_INFO		0x00108
#define PWIL_DISP_CTRL0 		0x010b0
#define PWIL_FBO_CTRL			0x010c8
#define PWIL_CMD_CTRL0			0x010f0
#define PWIL_CMD_CTRL1			0x010f4
#define PWIL_DPCD_CTRL			0x000c4
#define PWIL_REG_UPDATE			0x10000

/* PSR_MIF */
#define PSR_MIF_CTRL			0x00000
#define PSR_ELFIFO_CTRL			0x00004
#define PSR_RW_CTRL			0x00010
#define PSR_ELFIFO_STRIDE		0x0002c
#define PSR_WR_FIFO_DEPTH1		0x00054
#define PSR_SLICE_RAW_HSIZE		0x00104
#define PSR_SLICE_SIZE0			0x0010c
#define PSR_SLICE_SIZE1			0x00110
#define PSR_SLICE_SIZE2			0x00114
#define PSR_SLICE_SIZE3			0x00118
#define PSR_SW_CONTROL			0x1ff00

/* SRAM_CTRL */
#define RAMCTRL_PWRCTRL			0x00000

/* SCALER */
#define SCALER_TOP_CTRL			0x00000
#define VS_CTRL					0x00004
#define VS_VINC_0				0x00008
#define VS_VINC_1				0x0000c
#define VS_ALGPARM				0x00010
#define VS_ALGPARM_MAXLINE		0x00014
#define VS_OFFSET_0				0x00018
#define VS_OFFSET_1				0x0001c
#define VS_BGR					0x00020
#define HS_CTRL					0x00024
#define HS_PIXELBOUNDARY		0x00028
#define HS_HINC_0				0x0002c
#define HS_HINC_1				0x00030
#define HS_OFFSET_0				0x00034
#define HS_OFFSET_1				0x00038
#define HS_ALGPARM				0x0003c
#define HS_BGR					0x00040
#define SCALE_V3_GENERIC_CTRL	0x00044


/* BLENDING */
#define BLENDING_CTRL			0x00000
#define BLENDING_VIDEO_CTRL		0x00030

/* CM */
#define CM_CNTL_1				0x00004

/* DTG */
#define DTG_REG_8				0x00020
#define DTG_REG_9				0x00024
#define DTG_REG_10				0x00028
#define DTG_REG_11				0x0002c
#define DTG_REG_12				0x00030
#define DTG_REG_14				0x00038
#define DTG_REG_19				0x0004c
#define DTG_REG_23				0x0005c
#define DTG_REG_26				0x00068
#define DTG_REG_27				0x0006c
#define DTG_REG_41				0x000A4
#define DTG_REG_45				0x000B4
#define DTG_UPDATE				0x10000

#define MCU_SW_RESET			0x000c8
#define IRIS_PROXY_MB0			0xf0040000 //OSD protect window use
#define IRIS_PROXY_MB1			0xf0040008
#define IRIS_PROXY_MB5			0xf0040028
#define IRIS_MCU_INFO_1 		0xf0fe0000
#define IRIS_MCU_INFO_2 		0xf0fe0004
#define IRIS_UNIT_CTRL_INTEN		0xf0060008
#define IRIS_TX_RESERVE_0		0xf1880038
#define IRIS_DPORT_CTRL0		0xf1220000
#define IRIS_DPORT_REGSEL		0xf1220064

struct iris_grcp_cmd {
	char cmd[CMD_PKT_SIZE];
	int cmd_len;
};

enum iris_frc_lut {
	IRIS_FRC_PHASE_TALBE_V1,
	IRIS_FRC_PHASE_TABLE_V2_12to60,
	IRIS_FRC_PHASE_TABLE_V2_12to90,
	IRIS_FRC_PHASE_TABLE_V2_12to120,
	IRIS_FRC_PHASE_TABLE_V2_15to60,
	IRIS_FRC_PHASE_TABLE_V2_15to90,
	IRIS_FRC_PHASE_TABLE_V2_15to120,
	IRIS_FRC_PHASE_TABLE_V2_24to60,
	IRIS_FRC_PHASE_TABLE_V2_24to90,
	IRIS_FRC_PHASE_TABLE_V2_24to120,
	IRIS_FRC_PHASE_TABLE_V2_25to60,
	IRIS_FRC_PHASE_TABLE_V2_25to90,
	IRIS_FRC_PHASE_TABLE_V2_25to120,
	IRIS_FRC_PHASE_TABLE_V2_30to60,
	IRIS_FRC_PHASE_TABLE_V2_30to90,
	IRIS_FRC_PHASE_TABLE_V2_30to120,
	IRIS_FRC_PHASE_TABLE_V2_60to90,
	IRIS_FRC_PHASE_TABLE_V2_60to120
};

void iris_mode_switch_proc(u32 mode);
void iris_set_video_frame_rate_ms(u32 framerate);
void iris_set_out_frame_rate(u32 framerate);
void iris_set_frc_var_display(int var_disp);
int iris_mode_switch_update(void);
int iris_ms_debugfs_init(struct dsi_display *display);
void iris_set_ap_te(u8 ap_te);
void iris_vfr_update(struct iris_cfg *pcfg, bool enable);
int32_t iris_fi_osd_protect_window(u32 Top_left_position, u32 bottom_right_position, u32 osd_window_ctrl, u32 Enable, u32 DynCompensate);
void iris_fi_demo_window(u32 DemoWinMode);
void iris_fi_demo_window_cal(void);
#endif
