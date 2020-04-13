#ifndef _DSI_IRIS5_API_H_
#define _DSI_IRIS5_API_H_

/*
 * These files include source code for the Licensed Software that is provided under the terms and
 * conditions of a Master Software License Agreement and its applicable schedules.
 */

// Use Iris5 Analog bypass mode to light up panel
// Note: input timing should be same with output timing
//#define IRIS5_ABYP_LIGHTUP
//#define IRIS5_MIPI_TEST

#include "dsi_display.h"
#include "dsi_iris5_def.h"

void iris_set_cfg_index(int index);

int iris5_parse_params(struct dsi_display *display);
void iris5_init(struct dsi_display *display, struct dsi_panel *panel);
void iris5_deinit(struct dsi_display *display);
int iris5_power_on(struct dsi_panel *panel);
void iris5_reset(struct dsi_panel *panel);
void iris5_power_off(struct dsi_panel *panel);
void iris5_gpio_parse(struct dsi_panel *panel);
void iris5_gpio_request(struct dsi_panel *panel);
void iris5_gpio_free(struct dsi_panel *panel);
int iris5_lightup(struct dsi_panel *panel, struct dsi_panel_cmd_set *on_cmds);
int iris5_lightoff(struct dsi_panel *panel, struct dsi_panel_cmd_set *off_cmds);
int iris5_panel_cmd_passthrough(struct dsi_panel *panel, struct dsi_panel_cmd_set *cmdset);
void iris_register_osd_irq(void *disp);
int iris_panel_enable(struct dsi_panel *panel, struct dsi_panel_cmd_set *on_cmds);
int iris_panel_post_switch(struct dsi_panel *panel,
		      struct dsi_panel_cmd_set *switch_cmds,
		      struct dsi_mode_info *mode_info);
int iris_panel_switch(struct dsi_panel *panel,
		      struct dsi_panel_cmd_set *switch_cmds,
		      struct dsi_mode_info *mode_info);
int iris_read_status(struct dsi_display_ctrl *ctrl, struct dsi_panel *panel);
int get_iris_status(void);
int iris5_aod_set(struct dsi_panel *panel, bool aod);
bool iris5_aod_get(struct dsi_panel *panel);
int iris5_fod_set(struct dsi_panel *panel, bool fod);
int iris5_fod_post(struct dsi_panel *panel);

/*
* @Description: send continuous splash commands
* @param type IRIS_CONT_SPLASH_LK/IRIS_CONT_SPLASH_KERNEL
*/
void iris_send_cont_splash(struct dsi_display *display);

int iris5_operate_conf(struct msm_iris_operate_value *argp);
int iris5_operate_tool(struct msm_iris_operate_value *argp);

int iris5_hdr_enable_get(void);
bool iris5_dspp_dirty(void);
int iris5_abypass_mode_get(struct dsi_panel *panel);

void iris5_display_prepare(struct dsi_display *display);

int iris5_update_backlight(u8 PkgMode, u32 bl_lvl);
int iris5_prepare_for_kickoff(void *phys_enc);
int iris5_kickoff(void *phys_enc);
bool iris_secondary_display_autorefresh(void *phys_enc);
bool iris_virtual_encoder_phys(void *phys_enc);
void iris_second_channel_pre(bool dsc_enabled);
void iris_osd_irq_cnt_clean(void);
void iris_osd_irq_cnt_inc(void);
int iris5_control_pwr_regulator(bool on);
int iris_panel_ctrl_read_reg(struct dsi_display_ctrl *ctrl, struct dsi_panel *panel,
			u8 *rx_buf, int rlen, struct dsi_cmd_desc *cmd);
int iris5_sync_panel_brightness(int32_t step, void *phys_enc);
#endif // _DSI_IRIS5_API_H_
