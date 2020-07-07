#ifndef _DSI_IRIS5_LIGHTUP_H_
#define _DSI_IRIS5_LIGHTUP_H_

#include <linux/completion.h>
#include <linux/err.h>
#include <linux/clk.h>
#include "dsi_pwr.h"
#include "dsi_iris5.h"

/*
 * These files include source code for the Licensed Software that is provided under the terms and
 * conditions of a Master Software License Agreement and its applicable schedules.
 */

#define IRIS_CHIP_CNT   2
#define IRIS_PWIL_CUR_META0	0xf12400c8

#define MDSS_MAX_PANEL_LEN      256

#define HIGH_FREQ 120
#define LOW_FREQ 60
#define FHD_H 2376
#define QHD_H 3168

/*use to parse dtsi cmd list*/
struct iris_parsed_hdr {
	uint32_t dtype;  /* dsi command type 0x23 0x29*/
	//uint32_t lwtype; /* 8bit burst single */
	uint32_t last; /*last in chain*/
	uint32_t wait; /*wait time*/
	uint32_t ip; /*ip type*/
	uint32_t opt; /*ip option and lp or hs*/
	uint32_t dlen; /*payload len*/
};


/* iris ip option, it will create according to opt_id.
*  link_state will be create according to the last cmds
*/
struct iris_ip_opt {
	uint8_t opt_id; /*option identifier*/
	uint32_t len; /*option length*/
	uint8_t link_state; /*high speed or low power*/
	struct dsi_cmd_desc *cmd; /*the first cmd of desc*/
};

/*ip search index*/
struct iris_ip_index {
	//char ip; /*IP index*/
	//char enable; /*1-open 0-close*/
	int32_t opt_cnt; /*ip option number*/
	//char opt_cur; /*current use option*/
	struct iris_ip_opt *opt; /*option array*/
};

struct iris_pq_ipopt_val {
	int32_t opt_cnt;
	uint8_t ip;
	uint8_t *popt;
};

struct iris_pq_init_val {
	int32_t ip_cnt;
	struct iris_pq_ipopt_val  *val;
};

struct iris_cmd_statics {
	int cnt;
	int len;
};

/*used to control iris_ctrl opt sequence*/
struct iris_ctrl_opt {
	uint8_t ip;
	uint8_t opt_id;
	uint8_t skip_last;
	uint8_t reserved;
	//struct iris_ip_opt *opt;
};

struct iris_ctrl_seq {
	int32_t cnt;
	struct iris_ctrl_opt *ctrl_opt;
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
	uint16_t pending_mode;		// pending_mode is accessed by SDEEncoder and HWBinder.
	int abyp_switch_state;
	int frame_delay;
	struct mutex abypass_mutex;
};

//will pack all the commands here
struct iris_out_cmds{
	/* will be used before cmds sent out */
	struct dsi_cmd_desc *iris_cmds_buf;
	u32 cmds_index;
};


typedef int (*iris5_i2c_read_cb)(u32 reg_addr,
			      u32 *reg_val);

typedef int (*iris5_i2c_write_cb)(u32 reg_addr,
			      u32 reg_val);

typedef int (*iris5_i2c_burst_write_cb)(u32 start_addr,
			      u32 *lut_buffer,
			      u16 reg_num);

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
#if defined(PXLW_IRIS_DUAL)
	u32 video_baseaddr;
#endif
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

/*iris lightup configure commands*/
struct iris_cfg {
	struct iris_lp_ctrl lp_ctrl;
	uint8_t panel_type;
	uint16_t panel_nits;
	uint8_t chip_id;
	uint8_t power_mode;
	uint8_t valid;			/* 0: none, 1: empty, 2: parse ok, 3: minimum light up, 4. full light up */
	uint8_t lut_mode;
	uint32_t chip_ver;
	uint32_t add_last_flag;
	uint32_t add_on_last_flag;
	uint32_t add_pt_last_flag;
	uint32_t split_pkt_size;
	uint32_t lut_cmds_cnt;
	uint32_t none_lut_cmds_cnt;
	uint32_t panel_dimming_brightness;
	uint8_t panel_hbm[2];
	uint32_t loop_back_mode;
	uint32_t loop_back_mode_res;
	char display_mode_name[16];
	spinlock_t iris_lock;
	struct mutex mutex;
	struct mutex lb_mutex;
	struct dsi_display *display;
	struct dsi_panel *panel;
	/*one for lut and none_lut ip index arr*/
	struct iris_ip_index  ip_index_arr[IRIS_PIP_IDX_CNT][IRIS_IP_CNT];
	struct dsi_panel_cmd_set  cmds;
	struct iris_ctrl_seq   ctrl_seq[IRIS_CHIP_CNT];
	struct iris_ctrl_seq   ctrl_seq_cs[IRIS_CHIP_CNT];
	struct iris_pq_init_val  pq_init_val;
	struct dentry *dbg_root;
	struct iris_abypass_ctrl abypss_ctrl;
	struct iris_out_cmds iris_cmds;
	uint32_t min_color_temp;
	uint32_t max_color_temp;
	bool frc_enable;
	bool frc_setting_ready;
	struct iris_frc_setting frc_setting;
	u8 rx_mode; /* 0: DSI_VIDEO_MODE, 1: DSI_CMD_MODE */
	u8 tx_mode;
	int pwil_mode;
	bool osd_enable;
	bool osd_on;
	bool mcu_code_downloaded;
	bool frc_low_latency;
	u32 panel_te;
	u32 ap_te;
	u32 switch_mode;
	bool n2m_enable;
	struct work_struct cont_splash_work;
	struct work_struct lut_update_work;
	struct completion frame_ready_completion;
	iris5_i2c_read_cb iris5_i2c_read;
	iris5_i2c_write_cb iris5_i2c_write;
	iris5_i2c_burst_write_cb iris5_i2c_burst_write;
	struct mutex gs_mutex;
	struct mutex lock_send_pkt;
	bool osd_switch_on_pending;
	bool mipi_pwr_st;
	int dport_output_mode;
	bool iris_initialized;
	uint32_t cmd_list_index;
	uint32_t cur_h_active;
	uint32_t cur_v_active;
	uint32_t switch_case;
	int cur_fps_in_iris; //for dynamic fps switch in bypass mode then do switch to pt mode
	int next_fps_for_iris;
	int cur_vres_in_iris;
	struct iris_ctrl_seq mode_switch_seq;
	struct iris_ctrl_seq mode_switch_seq_1;
	struct work_struct vfr_update_work;
	atomic_t video_update_wo_osd;
	bool dynamic_vfr;
	atomic_t osd_irq_cnt;
	struct clk *ext_clk;
	bool aod;
	uint8_t abyp_prev_mode;
	bool fod;
	bool fod_pending;
	atomic_t fod_cnt;
	int32_t panel_pending;
	int32_t panel_delay;
	int32_t panel_level;

	/*iris pmic power*/
	struct dsi_regulator_info iris_power_info;
#if defined(PXLW_IRIS_DUAL)
	bool dual_setting;
	uint32_t dual_test;
	atomic_t dom_cnt_in_frc;
	atomic_t dom_cnt_in_ioctl;
#endif
};

struct iris_cmd_comp {
	int32_t link_state;
	int32_t cnt;
	struct dsi_cmd_desc *cmd;
};

struct iris_buf_len{
	const uint8_t *buf;
	uint32_t len;
};

struct iris_cfg * iris_get_cfg(void);
struct iris_cfg * iris_get_cfg_by_index(int index);

void iris_out_cmds_buf_reset(void);
int32_t iris_send_ipopt_cmds(int32_t ip, int32_t opt_id);
void iris_update_pq_opt(struct iris_update_ipopt *popt, int len, uint8_t path);
void iris_update_bitmask_regval(
				struct iris_update_regval *pregval, bool is_commit);
void iris_update_bitmask_regval_nonread(
				struct iris_update_regval *pregval, bool is_commit);

void iris_alloc_seq_space(void);

void iris_init_update_ipopt(struct iris_update_ipopt *popt,
		uint8_t ip, uint8_t opt_old, uint8_t opt_new, uint8_t skip_last);
struct iris_pq_ipopt_val  *  iris_get_cur_ipopt_val(uint8_t ip);

int iris_init_update_ipopt_t(struct iris_update_ipopt *popt,  int len,
						uint8_t ip, uint8_t opt_old, uint8_t opt_new, uint8_t skip_last);
struct iris_ip_opt * iris_find_ip_opt(uint8_t ip, uint8_t opt_id);
/*
* @description  get assigned position data of ip opt
* @param ip       ip sign
* @param opt_id   option id of ip
* @param pos      the position of option payload
* @return   fail NULL/success payload data of position
*/
uint32_t  * iris_get_ipopt_payload_data(uint8_t ip, uint8_t opt_id, int32_t pos);
void iris_set_ipopt_payload_data(uint8_t ip, uint8_t opt_id, int32_t pos,uint32_t value);

/*
*@Description: get current continue splash stage
				first light up panel only
				second pq effect
*/
uint8_t iris_get_cont_splash_type(void);

void iris_dump_packet(u8 *data, int size);
/*
*@Description: print continuous splash commands for bootloader
*@param: pcmd: cmds array  cnt: cmds cound
*/
void  iris_print_cmds(struct dsi_cmd_desc *pcmd, int cnt, int state);

int iris_read_chip_id(void);
void iris_read_power_mode(struct dsi_panel *panel);

int iris5_cmds_init(void);
void iris5_get_cmds(struct dsi_panel_cmd_set * cmds , char ** ls_arr);
void iris5_get_lightoff_cmds(struct dsi_panel_cmd_set * cmds , char ** ls_arr);

int32_t iris_attach_cmd_to_ipidx(struct iris_buf_len *data,
		int32_t cnt, struct iris_ip_index *pip_index,
		uint32_t *cmd_cnt);

struct iris_ip_index *iris_get_ip_idx(int32_t type);

void iris_change_type_addr(struct iris_ip_opt *dest, struct iris_ip_opt *src);

struct iris_ip_opt *iris_find_ip_opt(uint8_t ip, uint8_t opt_id);

int iris_osd_blending_switch(u32 val);
int iris_osd_autorefresh(u32 val);
int iris_second_channel_power(bool pwr);
void iris_second_channel_post(u32 val);
int iris_get_osd_overflow_st(void);
bool iris_get_dual2single_status(void);
void iris_frc_low_latency(bool low_latency);
void iris_set_panel_te(u8 panel_te);
void iris_set_n2m_enable(bool bEn);
int iris_wait_vsync(void); //CID89779
int iris_set_pending_panel_brightness(int32_t pending, int32_t delay, int32_t level);

bool iris_virtual_display(const struct dsi_display *display);
void iris_free_ipopt_buf(uint32_t ip_type);
void iris_free_seq_space(void);

/* API for debug begin */
void iris_print_desc_info(struct dsi_cmd_desc *cmds, int32_t cnt);

void iris_print_none_lut_cmds_for_lk(struct dsi_cmd_desc *cmds,
			 int32_t cnt, int32_t wait, int32_t link_state);
void iris_send_mode_switch_pkt(void);
/* API for debug end */

#if defined(PXLW_IRIS_DUAL)
void iris_dual_setting_switch(bool dual);
void iris_frc_dsc_setting(bool dual);
#endif
#endif // _DSI_IRIS5_LIGHTUP_H_
