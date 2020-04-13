#ifndef _DSI_IRIS5_LUT_H_
#define _DSI_IRIS5_LUT_H_

/*
 * These files include source code for the Licensed Software that is provided under the terms and
 * conditions of a Master Software License Agreement and its applicable schedules.
 */

int iris5_parse_lut_cmds(void);
int iris_lut_send(u8 lut_type, u8 lut_table_index, u32 lut_abtable_index);
void iris_ambient_lut_update(enum LUT_TYPE lutType, u32 lutPos);
void iris_maxcll_lut_update(enum LUT_TYPE lutType, u32 lutpos);
u8 iris_get_firmware_status(void);
void iris_set_firmware_status(u8 value);
int iris_fw_calibrate_status_debugfs_init(void);

#endif // _DSI_IRIS5_LUT_H_
