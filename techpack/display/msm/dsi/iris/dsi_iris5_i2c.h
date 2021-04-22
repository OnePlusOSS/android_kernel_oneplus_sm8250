// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2015-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2015-2020.
 */
#ifndef DSI_IRIS_I2C_H
#define DSI_IRIS_I2C_H

#include <linux/i2c.h>
#include <linux/of.h>

struct addr_val_i2c {
	uint32_t addr;
	uint32_t data;
};
struct iris_i2c_msg {
	uint32_t *payload;
	uint32_t   len;
	uint32_t   base_addr;
};


int iris_i2c_bus_init(void);
void iris_i2c_bus_exit(void);
int iris_i2c_conver_ocp_read(uint32_t *ptr, uint32_t len, bool is_burst);
int iris_i2c_conver_ocp_write(uint32_t base_addr, uint32_t *ptr, uint32_t len, bool is_burst);
int iris_i2c_burst_conver_ocp_write(uint32_t base_addr, uint32_t *arr, uint32_t dlen);
int iris_i2c_single_conver_ocp_write(uint32_t *arr, uint32_t dlen);
int iris_i2c_group_write(struct iris_i2c_msg *iris_i2c_msg, uint32_t iris_i2c_msg_num);

#endif
