// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#ifndef _DSI_IRIS_BACK_H_
#define _DSI_IRIS_BACK_H_

int32_t iris_parse_loopback_info(struct device_node *np, struct iris_cfg *pcfg);

u32 iris_loop_back_verify(void);

/* API in kernel for recovery mode */
int iris_loop_back_validate(void);

int iris_loop_back_init(struct dsi_display *display);

#endif // _DSI_IRIS_BACK_H_
