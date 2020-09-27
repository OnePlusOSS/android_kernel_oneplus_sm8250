// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2015-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2015-2020.
 */
#ifndef DSI_IRIS_I3C_H
#define DSI_IRIS_I3C_H

#include <linux/i2c.h>
#include <linux/of.h>
#include "dsi_iris5_i2c.h"

#define I2C_LEGACY_CMD_ENABLE 0

#define IRIS_CMD_I2C_SR    0x09
#define IRIS_CMD_I2C_BR    0x08
#define IRIS_CMD_I2C_SW    0x04
#define IRIS_CMD_I2C_BW    0x00
#define IRIS_CMD_I2C_DW    0x0C
#define IRIS_CMD_I2C_BSW   0x05
#define IRIS_CMD_I2C_LUT   0x0F

struct addr_val {
	uint32_t addr;
	uint32_t data;
};

enum PATH_TYPE {
	PATH_I2C = 0,
	PATH_DSI,
};

int iris_i2c_byte_read(uint32_t reg_offset, uint32_t *read_buf);
int iris_i2c_byte_write(uint32_t reg_offset,  uint32_t value);
int iris_i2c_read(struct addr_val *val, int len, bool is_burst);
int iris_i2c_write(uint32_t *arr, int len, bool is_burst);
int iris_i2c_bus_init(void);
void iris_i2c_bus_exit(void);
int iris_i2c_ocp_burst_write(uint32_t *arr, uint32_t dlen);
int iris_i2c_ocp_single_write(uint32_t *arr, uint32_t dlen);
int iris_i2c_ocp_write(uint32_t *ptr, uint32_t len, bool is_burst);
int iris_i2c_ocp_read(uint32_t *ptr, uint32_t len, bool is_burst);
int iris_i2c_direct_write(uint32_t *arr, uint32_t dlen, uint32_t type);
int iris_i2c_burst_write(struct iris_i2c_msg *iris_i2c_msg, uint32_t iris_i2c_msg_num);
void iris_i3c_status_set(bool enable);
bool iris_i3c_status_get(void);


#endif
