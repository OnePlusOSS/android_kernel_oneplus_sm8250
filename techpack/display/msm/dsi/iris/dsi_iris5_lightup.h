// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#ifndef _DSI_IRIS_LIGHTUP_H_
#define _DSI_IRIS_LIGHTUP_H_

#include <linux/completion.h>
#include <linux/err.h>
#include <linux/clk.h>
#include "dsi_pwr.h"
#include "dsi_iris5.h"

#define IRIS_CHIP_CNT   2
#define IRIS_PWIL_CUR_META0	0xf12400c8

#define MDSS_MAX_PANEL_LEN      256

#define HIGH_FREQ 120
#define LOW_FREQ 60
#define FHD_H 2376
#define QHD_H 3168


/* iris ip option, it will create according to opt_id.
 *  link_state will be create according to the last cmds
 */
struct iris_ip_opt {
	uint8_t opt_id; /*option identifier*/
	uint32_t cmd_cnt; /*option length*/
	uint8_t link_state; /*high speed or low power*/
	struct dsi_cmd_desc *cmd; /*the first cmd of desc*/
};

/*ip search index*/
struct iris_ip_index {
	int32_t opt_cnt; /*ip option number*/
	struct iris_ip_opt *opt; /*option array*/
};

struct iris_pq_ipopt_val {
	int32_t opt_cnt;
	uint8_t ip;
	uint8_t *popt;
};

struct iris_pq_init_val {
	int32_t ip_cnt;
	struct iris_pq_ipopt_val *val;
};

/*used to control iris_ctrl opt sequence*/
struct iris_ctrl_opt {
	uint8_t ip;
	uint8_t opt_id;
	uint8_t skip_last;
};

struct iris_ctrl_seq {
	int32_t cnt;
	struct iris_ctrl_opt *ctrl_opt;
};

//will pack all the commands here
struct iris_out_cmds {
	/* will be used before cmds sent out */
	struct dsi_cmd_desc *iris_cmds_buf;
	u32 cmds_index;
};

typedef int (*iris_i2c_read_cb)(u32 reg_addr, u32 *reg_val);
typedef int (*iris_i2c_write_cb)(u32 reg_addr, u32 reg_val);
typedef int (*iris_i2c_burst_write_cb)(u32 start_addr, u32 *lut_buffer, u16 reg_num);

enum IRIS_PARAM_VALID {
	PARAM_NONE = 0,
	PARAM_EMPTY,
	PARAM_PARSED,
	PARAM_PREPARED,
};

/* iris lightup configure commands */
struct iris_cfg {
	struct dsi_display *display;
	struct dsi_panel *panel;

	struct platform_device *pdev;
	struct {
		struct pinctrl *pinctrl;
		struct pinctrl_state *active;
		struct pinctrl_state *suspend;
	} pinctrl;
	int iris_reset_gpio;
	int iris_wakeup_gpio;
	int iris_abyp_ready_gpio;
	int iris_osd_gpio;
	int iris_vdd_gpio;
	bool iris_osd_autorefresh;
	bool iris_osd_autorefresh_enabled;

	/* hardware version and initialization status */
	uint8_t chip_id;
	uint32_t chip_ver;
	uint32_t chip_value[2];
	uint8_t valid; /* 0: none, 1: empty, 2: parse ok, 3: minimum light up, 4. full light up */
	bool iris_initialized;
	bool mcu_code_downloaded;

	/* static configuration */
	uint8_t panel_type;
	uint8_t lut_mode;
	uint32_t add_last_flag;
	uint32_t add_on_last_flag;
	uint32_t add_pt_last_flag;
	uint32_t split_pkt_size;
	uint32_t loop_back_mode;
	uint32_t loop_back_mode_res;
    struct mutex lb_mutex;
	uint32_t min_color_temp;
	uint32_t max_color_temp;
	uint8_t rx_mode; /* 0: DSI_VIDEO_MODE, 1: DSI_CMD_MODE */
	uint8_t tx_mode;

	/* current state */
	struct iris_lp_ctrl lp_ctrl;
	struct iris_abypass_ctrl abypss_ctrl;
	uint16_t panel_nits;
	uint32_t panel_dimming_brightness;
	uint8_t panel_hbm[2];
	bool frc_enable;
	bool frc_setting_ready;
	struct iris_frc_setting frc_setting;
	bool frc_low_latency;
	int pwil_mode;
	bool osd_enable;
	bool osd_on;
	bool osd_switch_on_pending;
	atomic_t osd_irq_cnt;
	uint32_t panel_te;
	uint32_t ap_te;
	uint32_t switch_mode;
	uint8_t power_mode;
	bool n2m_enable;
	bool mipi_pwr_st;
	int dport_output_mode;
	bool dynamic_vfr;
	atomic_t video_update_wo_osd;

	char display_mode_name[16];
	uint32_t app_version;
	uint8_t app_date[4];
	uint8_t abyp_prev_mode;
	struct clk *ext_clk;

	uint32_t cmd_list_index;
	uint32_t cur_h_active;
	uint32_t cur_v_active;
	uint32_t switch_case;
	int cur_fps_in_iris; //for dynamic fps switch in bypass mode then do switch to pt mode
	int next_fps_for_iris;
	int cur_vres_in_iris;

	int32_t panel_pending;
	int32_t panel_delay;
	int32_t panel_level;

	bool aod;
	bool fod;
	bool fod_pending;
	atomic_t fod_cnt;

	struct dsi_regulator_info iris_power_info; // iris pmic power

	/* configuration commands, parsed from dt, dynamically modified
	 * panel->panel_lock must be locked before access and for DSI command send
	 */
	uint32_t lut_cmds_cnt;
	uint32_t dtsi_cmds_cnt;
	struct iris_ip_index ip_index_arr[IRIS_PIP_IDX_CNT][IRIS_IP_CNT];
	struct iris_ctrl_seq ctrl_seq[IRIS_CHIP_CNT];
	struct iris_ctrl_seq ctrl_seq_cs[IRIS_CHIP_CNT];
	struct iris_pq_init_val pq_init_val;
	struct iris_out_cmds iris_cmds;

	struct iris_ctrl_seq timing_switch_seq;
	struct iris_ctrl_seq timing_switch_seq_1;

	/* one wire gpio lock */
	spinlock_t iris_1w_lock;
	struct dentry *dbg_root;
	struct work_struct cont_splash_work;
	struct work_struct lut_update_work;
	struct work_struct vfr_update_work;
	struct completion frame_ready_completion;

	/* hook for i2c extension */
	struct mutex gs_mutex;
	iris_i2c_read_cb iris_i2c_read;
	iris_i2c_write_cb iris_i2c_write;
	iris_i2c_burst_write_cb iris_i2c_burst_write;
	bool dual_setting;
	uint32_t dual_test;
};

struct iris_data {
	const uint8_t *buf;
	uint32_t size;
};

struct iris_cfg *iris_get_cfg(void);
struct iris_cfg *iris_get_cfg_by_index(int index);

int iris_lightup(struct dsi_panel *panel, struct dsi_panel_cmd_set *on_cmds);
int iris_lightoff(struct dsi_panel *panel, struct dsi_panel_cmd_set *off_cmds);
int32_t iris_send_ipopt_cmds(int32_t ip, int32_t opt_id);
void iris_update_pq_opt(struct iris_update_ipopt *popt, int len, uint8_t path);
void iris_update_bitmask_regval(
		struct iris_update_regval *pregval, bool is_commit);
void iris_update_bitmask_regval_nonread(
		struct iris_update_regval *pregval, bool is_commit);

void iris_alloc_seq_space(void);

void iris_init_update_ipopt(struct iris_update_ipopt *popt,
		uint8_t ip, uint8_t opt_old, uint8_t opt_new, uint8_t skip_last);
struct iris_pq_ipopt_val  *iris_get_cur_ipopt_val(uint8_t ip);

int iris_init_update_ipopt_t(struct iris_update_ipopt *popt,  int len,
		uint8_t ip, uint8_t opt_old, uint8_t opt_new, uint8_t skip_last);
struct iris_ip_opt *iris_find_ip_opt(uint8_t ip, uint8_t opt_id);
/*
 * @description  get assigned position data of ip opt
 * @param ip       ip sign
 * @param opt_id   option id of ip
 * @param pos      the position of option payload
 * @return   fail NULL/success payload data of position
 */
uint32_t  *iris_get_ipopt_payload_data(uint8_t ip, uint8_t opt_id, int32_t pos);
void iris_set_ipopt_payload_data(uint8_t ip, uint8_t opt_id, int32_t pos, uint32_t value);

/*
 *@Description: get current continue splash stage
 first light up panel only
 second pq effect
 */
uint8_t iris_get_cont_splash_type(void);

/*
 *@Description: print continuous splash commands for bootloader
 *@param: pcmd: cmds array  cnt: cmds cound
 */
void iris_print_desc_cmds(struct dsi_cmd_desc *pcmd, int cmd_cnt, int state);

int iris_init_cmds(void);
void iris_get_cmds(struct dsi_panel_cmd_set *cmds, char **ls_arr);
void iris_get_lightoff_cmds(struct dsi_panel_cmd_set *cmds, char **ls_arr);

int32_t iris_attach_cmd_to_ipidx(const struct iris_data *data,
		int32_t data_cnt, struct iris_ip_index *pip_index);

struct iris_ip_index *iris_get_ip_idx(int32_t type);

void iris_change_type_addr(struct iris_ip_opt *dest, struct iris_ip_opt *src);

struct iris_ip_opt *iris_find_ip_opt(uint8_t ip, uint8_t opt_id);

int iris_wait_vsync(void);
int iris_set_pending_panel_brightness(int32_t pending, int32_t delay, int32_t level);

bool iris_virtual_display(const struct dsi_display *display);
void iris_free_ipopt_buf(uint32_t ip_type);
void iris_free_seq_space(void);

void iris_send_assembled_pkt(struct iris_ctrl_opt *arr, int seq_cnt);
int32_t iris_parse_dtsi_cmd(const struct device_node *lightup_node,
		uint32_t cmd_index);
int32_t iris_parse_optional_seq(struct device_node *np, const uint8_t *key,
		struct iris_ctrl_seq *pseq);

int iris_display_cmd_engine_enable(struct dsi_display *display);
int iris_display_cmd_engine_disable(struct dsi_display *display);

#endif // _DSI_IRIS_LIGHTUP_H_
