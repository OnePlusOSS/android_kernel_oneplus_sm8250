// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#ifndef __DSI_IRIS_FRC__
#define __DSI_IRIS_FRC__

struct iris_cfg;
struct device_node;
void iris_init_vfr_work(struct iris_cfg *pcfg);
void iris_frc_low_latency(bool low_latency);
void iris_set_panel_te(u8 panel_te);
void iris_set_n2m_enable(bool enable);
void iris_frc_parameter_init(void);
int32_t iris_parse_frc_setting(struct device_node *np, struct iris_cfg *pcfg);
bool iris_get_dual2single_status(void);
int32_t iris_set_second_channel_power(bool pwr);
void iris_second_channel_post(u32 val);
int32_t iris_switch_osd_blending(u32 val);
int32_t iris_osd_autorefresh(u32 val);
int32_t iris_get_osd_overflow_st(void);
irqreturn_t iris_osd_handler(int irq, void *data);
void iris_frc_prepare(struct iris_cfg *pcfg);
void iris_clean_frc_status(struct iris_cfg *pcfg);
int32_t iris_dbgfs_frc_init(void);
void iris_dual_setting_switch(bool dual);
void iris_frc_dsc_setting(bool dual);

#endif //__DSI_IRIS_FRC__
