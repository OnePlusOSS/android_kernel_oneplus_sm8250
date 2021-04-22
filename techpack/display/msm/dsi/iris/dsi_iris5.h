// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#ifndef _DSI_IRIS_H_
#define _DSI_IRIS_H_

#include "dsi_iris5_def.h"


void iris_set_cfg_index(int index);
int iris_parse_param(struct dsi_display *display);
void iris_init(struct dsi_display *display, struct dsi_panel *panel);

int iris_get_abyp_mode(struct dsi_panel *panel);

int iris_operate_conf(struct msm_iris_operate_value *argp);
int iris_operate_tool(struct msm_iris_operate_value *argp);

int iris_get_hdr_enable(void);
bool iris_dspp_dirty(void);

int iris_prepare_for_kickoff(void *phys_enc);
int iris_kickoff(void *phys_enc);

int iris_sync_panel_brightness(int32_t step, void *phys_enc);

#endif // _DSI_IRIS_H_
