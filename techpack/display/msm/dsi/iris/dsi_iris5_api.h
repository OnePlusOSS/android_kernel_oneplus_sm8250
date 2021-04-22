// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#ifndef _DSI_IRIS_API_H_
#define _DSI_IRIS_API_H_

#include "dsi_display.h"

void iris_deinit(struct dsi_display *display);
void iris_power_on(struct dsi_panel *panel);
void iris_reset(void);
void iris_power_off(struct dsi_panel *panel);
int iris_pt_send_panel_cmd(struct dsi_panel *panel,
		struct dsi_panel_cmd_set *cmdset);
int iris_enable(struct dsi_panel *panel, struct dsi_panel_cmd_set *on_cmds);
int iris_disable(struct dsi_panel *panel, struct dsi_panel_cmd_set *off_cmds);
int iris_post_switch(struct dsi_panel *panel,
		      struct dsi_panel_cmd_set *switch_cmds,
		      struct dsi_mode_info *mode_info);
int iris_switch(struct dsi_panel *panel,
		      struct dsi_panel_cmd_set *switch_cmds,
		      struct dsi_mode_info *mode_info);
int iris_read_status(struct dsi_display_ctrl *ctrl, struct dsi_panel *panel);
int iris_get_status(void);
int iris_set_aod(struct dsi_panel *panel, bool aod);
bool iris_get_aod(struct dsi_panel *panel);
int iris_set_fod(struct dsi_panel *panel, bool fod);
int iris_post_fod(struct dsi_panel *panel);

void iris_send_cont_splash(struct dsi_display *display);
bool iris_is_pt_mode(struct dsi_panel *panel);
void iris_prepare(struct dsi_display *display);

int iris_update_backlight(u8 pkg_mode, u32 bl_lvl);
void iris_control_pwr_regulator(bool on);
int iris_panel_ctrl_read_reg(struct dsi_display_ctrl *ctrl,
		struct dsi_panel *panel, u8 *rx_buf, int rlen,
		struct dsi_cmd_desc *cmd);

bool iris_secondary_display_autorefresh(void *phys_enc);
bool iris_is_virtual_encoder_phys(void *phys_enc);
void iris_register_osd_irq(void *disp);
void iris_inc_osd_irq_cnt(void);

void iris_query_capability(struct dsi_panel *panel);
bool iris_is_chip_supported(void);
bool iris_is_softiris_supported(void);
bool iris_is_dual_supported(void);

void iris_sde_plane_setup_csc(void *csc_ptr);
int iris_sde_kms_iris_operate(struct msm_kms *kms,
		u32 operate_type, struct msm_iris_operate_value *operate_value);
void iris_sde_update_dither_depth_map(uint32_t *map);
void iris_sde_prepare_for_kickoff(uint32_t num_phys_encs, void *phys_enc);
void iris_sde_encoder_sync_panel_brightness(uint32_t num_phys_encs,
		void *phys_enc);
void iris_sde_encoder_kickoff(uint32_t num_phys_encs, void *phys_enc);
void iris_sde_encoder_wait_for_event(uint32_t num_phys_encs,
		void *phys_enc, uint32_t event);

int msm_ioctl_iris_operate_conf(struct drm_device *dev, void *data,
		struct drm_file *file);
int msm_ioctl_iris_operate_tool(struct drm_device *dev, void *data,
		struct drm_file *file);

void iris_dsi_display_res_init(struct dsi_display *display);
void iris_dsi_display_debugfs_init(struct dsi_display *display,
		struct dentry *dir, struct dentry *dump_file);
void iris_dsi_panel_dump_pps(struct dsi_panel_cmd_set *set);
void iris_dsi_ctrl_dump_desc_cmd(struct dsi_ctrl *dsi_ctrl,
		const struct mipi_dsi_msg *msg);

void iris_sde_hw_sspp_setup_csc_v2(void *pctx, const void *pfmt, void *pdata);

#endif // _DSI_IRIS_API_H_
