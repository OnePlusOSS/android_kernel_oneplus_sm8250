// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#ifndef __DSI_IRIS_GPIO__
#define __DSI_IRIS_GPIO__


typedef enum {
	IRIS_POWER_UP_SYS,
	IRIS_ENTER_ANALOG_BYPASS,
	IRIS_EXIT_ANALOG_BYPASS,
	IRIS_POWER_DOWN_SYS,
	IRIS_RESET_SYS,
	IRIS_FORCE_ENTER_ANALOG_BYPASS,
	IRIS_FORCE_EXIT_ANALOG_BYPASS,
	IRIS_POWER_UP_MIPI,
	IRIS_POWER_DOWN_MIPI,
	IRIS_ONE_WIRE_CMD_CNT
} IRIS_ONE_WIRE_TYPE;

int iris_init_one_wired(void);
void iris_send_one_wired_cmd(IRIS_ONE_WIRE_TYPE type);
int iris_enable_pinctrl(void *dev, void *cfg);
int iris_set_pinctrl_state(void *cfg, bool enable);
int iris_parse_gpio(void *dev, void *cfg);
void iris_request_gpio(void);
void iris_release_gpio(void *cfg);
int iris_check_abyp_ready(void);
bool iris_vdd_valid(void);
void iris_enable_vdd(void);
void iris_disable_vdd(void);
void iris_reset_chip(void);
void iris_reset_off(void);
int iris_dbg_gpio_init(void);

#endif //__DSI_IRIS_GPIO__
