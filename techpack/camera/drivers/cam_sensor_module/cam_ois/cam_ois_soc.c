// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 */

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <cam_sensor_cmn_header.h>
#include <cam_sensor_util.h>
#include <cam_sensor_io.h>
#include <cam_req_mgr_util.h>

#include "cam_ois_soc.h"
#include "cam_debug_util.h"

/**
 * @e_ctrl: ctrl structure
 *
 * Parses ois dt
 */
static int cam_ois_get_dt_data(struct cam_ois_ctrl_t *o_ctrl)
{
	int                             i, rc = 0;
	struct cam_hw_soc_info         *soc_info = &o_ctrl->soc_info;
	struct cam_ois_soc_private     *soc_private =
		(struct cam_ois_soc_private *)o_ctrl->soc_info.soc_private;
	struct cam_sensor_power_ctrl_t *power_info = &soc_private->power_info;
	struct device_node             *of_node = NULL;

	of_node = soc_info->dev->of_node;

	if (!of_node) {
		CAM_ERR(CAM_OIS, "of_node is NULL, device type %d",
			o_ctrl->ois_device_type);
		return -EINVAL;
	}
	rc = cam_soc_util_get_dt_properties(soc_info);
	if (rc < 0) {
		CAM_ERR(CAM_OIS, "cam_soc_util_get_dt_properties rc %d",
			rc);
		return rc;
	}

	if (!soc_info->gpio_data) {
		CAM_INFO(CAM_OIS, "No GPIO found");
		return 0;
	}

	if (!soc_info->gpio_data->cam_gpio_common_tbl_size) {
		CAM_INFO(CAM_OIS, "No GPIO found");
		return -EINVAL;
	}

	rc = cam_sensor_util_init_gpio_pin_tbl(soc_info,
		&power_info->gpio_num_info);
	if ((rc < 0) || (!power_info->gpio_num_info)) {
		CAM_ERR(CAM_OIS, "No/Error OIS GPIOs");
		return -EINVAL;
	}

	for (i = 0; i < soc_info->num_clk; i++) {
		soc_info->clk[i] = devm_clk_get(soc_info->dev,
			soc_info->clk_name[i]);
		if (!soc_info->clk[i]) {
			CAM_ERR(CAM_SENSOR, "get failed for %s",
				soc_info->clk_name[i]);
			rc = -ENOENT;
			return rc;
		}
	}

	return rc;
}
/**
 * @o_ctrl: ctrl structure
 *
 * This function is called from cam_ois_platform/i2c_driver_probe, it parses
 * the ois dt node.
 */
int cam_ois_driver_soc_init(struct cam_ois_ctrl_t *o_ctrl)
{
	int                            rc = 0;
	const char                     *p = NULL;
	struct cam_hw_soc_info         *soc_info = &o_ctrl->soc_info;
	struct device_node             *of_node = NULL;
	struct device_node             *of_parent = NULL;
	int                             ret = 0;
	int                             id;

	if (!soc_info->dev) {
		CAM_ERR(CAM_OIS, "soc_info is not initialized");
		return -EINVAL;
	}

	of_node = soc_info->dev->of_node;
	if (!of_node) {
		CAM_ERR(CAM_OIS, "dev.of_node NULL");
		return -EINVAL;
	}

	if (o_ctrl->ois_device_type == MSM_CAMERA_PLATFORM_DEVICE) {
		rc = of_property_read_u32(of_node, "cci-master",
			&o_ctrl->cci_i2c_master);
		if (rc < 0) {
			CAM_DBG(CAM_OIS, "failed rc %d", rc);
			return rc;
		}

		of_parent = of_get_parent(of_node);
		if (of_property_read_u32(of_parent, "cell-index",
				&o_ctrl->cci_num) < 0)
			/* Set default master 0 */
			o_ctrl->cci_num = CCI_DEVICE_0;

		o_ctrl->io_master_info.cci_client->cci_device = o_ctrl->cci_num;
		CAM_DBG(CAM_OIS, "cci-device %d", o_ctrl->cci_num);

	}

	rc = cam_ois_get_dt_data(o_ctrl);
	if (rc < 0)
		CAM_DBG(CAM_OIS, "failed: ois get dt data rc %d", rc);

	ret = of_property_read_u32(of_node, "ois_gyro,position", &id);
	if (ret) {
	    o_ctrl->ois_gyro_position = 1;
		CAM_ERR(CAM_OIS, "get ois_gyro,position failed rc:%d, set default value to %d", ret, o_ctrl->ois_gyro_position);
	} else {
	    o_ctrl->ois_gyro_position = (uint8_t)id;
		CAM_INFO(CAM_OIS, "read ois_gyro,position success, value:%d", o_ctrl->ois_gyro_position);
	}

	ret = of_property_read_u32(of_node, "ois,type", &id);
	if (ret) {
	    o_ctrl->ois_type = CAM_OIS_MASTER;
		CAM_ERR(CAM_OIS, "get ois,type failed rc:%d, default %d", ret, o_ctrl->ois_type);
	} else {
	    o_ctrl->ois_type = (uint8_t)id;
		CAM_INFO(CAM_OIS, "read ois,type success, value:%d", o_ctrl->ois_type);
	}

	ret = of_property_read_u32(of_node, "ois_gyro,type", &id);
	if (ret) {
	    o_ctrl->ois_gyro_vendor = 0x02;
		CAM_ERR(CAM_OIS, "get ois_gyro,type failed rc:%d, default %d", ret, o_ctrl->ois_gyro_vendor);
	} else {
	    o_ctrl->ois_gyro_vendor = (uint8_t)id;
		CAM_INFO(CAM_OIS, "read ois_gyro,type success, value:%d", o_ctrl->ois_gyro_vendor);
	}

	ret = of_property_read_string_index(of_node, "ois,name", 0, (const char **)&p);
	if (ret) {
		CAM_ERR(CAM_OIS, "get ois,name failed rc:%d, set default value to %s", ret, o_ctrl->ois_name);
	} else {
	    memcpy(o_ctrl->ois_name, p, sizeof(o_ctrl->ois_name));
		CAM_INFO(CAM_OIS, "read ois,name success, value:%s", o_ctrl->ois_name);
	}

	ret = of_property_read_u32(of_node, "ois_module,vendor", &id);
	if (ret) {
	    o_ctrl->ois_module_vendor = 0x01;
		CAM_ERR(CAM_OIS, "get ois_module,vendor failed rc:%d, default %d", ret, o_ctrl->ois_module_vendor);
	} else {
	    o_ctrl->ois_module_vendor = (uint8_t)id;
		CAM_INFO(CAM_OIS, "read ois_module,vendor success, value:%d", o_ctrl->ois_module_vendor);
	}

	ret = of_property_read_u32(of_node, "ois_actuator,vednor", &id);
	if (ret) {
	    o_ctrl->ois_actuator_vendor = 0x01;
		CAM_ERR(CAM_OIS, "get ois_actuator,vednor failed rc:%d, default %d", ret, o_ctrl->ois_actuator_vendor);
	} else {
	    o_ctrl->ois_actuator_vendor = (uint8_t)id;
		CAM_INFO(CAM_OIS, "read ois_actuator,vednor success, value:%d", o_ctrl->ois_actuator_vendor);
	}

	ret = of_property_read_u32(of_node, "ois,fw", &id);
	if (ret) {
	    o_ctrl->ois_fw_flag = 0x01;
		CAM_ERR(CAM_OIS, "get ois,fw failed rc:%d, default %d", ret, o_ctrl->ois_fw_flag);
	} else {
	    o_ctrl->ois_fw_flag = (uint8_t)id;
		CAM_INFO(CAM_OIS, "read ois,fw success, value:%d", o_ctrl->ois_fw_flag);
	}

	ret = of_property_read_u32(of_node, "download,fw", &id);
	if (ret) {
	    o_ctrl->cam_ois_download_fw_in_advance = 0;
		CAM_ERR(CAM_OIS, "get download,fw failed rc:%d, default %d", ret, o_ctrl->cam_ois_download_fw_in_advance);
	} else {
	    o_ctrl->cam_ois_download_fw_in_advance = (uint8_t)id;
		CAM_INFO(CAM_OIS, "read download,fw success, value:%d", o_ctrl->cam_ois_download_fw_in_advance);
	}

	return rc;
}
