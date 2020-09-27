// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#ifndef __DSI_IRIS_TIMING_SWITCH__
#define __DSI_IRIS_TIMING_SWITCH__


void iris_init_timing_switch(void);
int32_t iris_parse_timing_switch_info(struct device_node *np,
		struct iris_cfg *pcfg);
void iris_send_timing_switch_pkt(void);
bool iris_is_resolution_switched(struct dsi_mode_info *mode_info);
uint32_t iris_get_cont_type_with_timing_switch(struct dsi_panel *panel);

#endif //__DSI_IRIS_TIMING_SWITCH__
