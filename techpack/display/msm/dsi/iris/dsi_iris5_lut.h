// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#ifndef _DSI_IRIS_LUT_H_
#define _DSI_IRIS_LUT_H_

int iris_parse_lut_cmds(u8 flag);
int iris_send_lut(u8 lut_type, u8 lut_table_index, u32 lut_abtable_index);
void iris_update_ambient_lut(enum LUT_TYPE lutType, u32 lutPos);
void iris_update_maxcll_lut(enum LUT_TYPE lutType, u32 lutpos);
u8 iris_get_fw_status(void);
void iris_update_fw_status(u8 value);
void iris_update_gamma(void);
int iris_dbgfs_fw_calibrate_status_init(void);

#endif // _DSI_IRIS_LUT_H_
