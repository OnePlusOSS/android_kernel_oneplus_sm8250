// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/regulator/consumer.h>
#include "oplus_touchscreen/tp_devices.h"
#include "oplus_touchscreen/touchpanel_common.h"
#include <soc/oplus/system/oplus_project.h>
#include <soc/oplus/device_info.h>
#include "touch.h"

#define MAX_LIMIT_DATA_LENGTH         100
extern char *saved_command_line;
/*if can not compile success, please update vendor/oplus_touchsreen*/
struct tp_dev_name tp_dev_names[] = {
	{TP_OFILM, "OFILM"},
	{TP_BIEL, "BIEL"},
	{TP_TRULY, "TRULY"},
	{TP_BOE, "BOE"},
	{TP_G2Y, "G2Y"},
	{TP_TPK, "TPK"},
	{TP_JDI, "JDI"},
	{TP_TIANMA, "TIANMA"},
	{TP_SAMSUNG, "SAMSUNG"},
	{TP_DSJM, "DSJM"},
	{TP_BOE_B8, "BOEB8"},
	{TP_UNKNOWN, "UNKNOWN"},
};
int g_tp_prj_id = 0;
int g_tp_dev_vendor = TP_UNKNOWN;
char *g_tp_ext_prj_name = NULL;
typedef enum {
	TP_INDEX_NULL,
	SAMSUNG_Y791,
	BOE_S3908,
	SAMSUNG_Y771,
	ili7807s_boe
} TP_USED_INDEX;
TP_USED_INDEX tp_used_index  = TP_INDEX_NULL;



#define GET_TP_DEV_NAME(tp_type) ((tp_dev_names[tp_type].type == (tp_type))?tp_dev_names[tp_type].name:"UNMATCH")

bool __init tp_judge_ic_match(char *tp_ic_name)
{
	return true;
}

bool  tp_judge_ic_match_commandline(struct panel_info *panel_data)
{
	int prj_id = 0;
	int i = 0;
	prj_id = get_project();
	pr_err("[TP] boot_command_line = %s \n", saved_command_line);
	for(i = 0; i < panel_data->project_num; i++) {
		if(prj_id == panel_data->platform_support_project[i]) {
			g_tp_prj_id = panel_data->platform_support_project_dir[i];
			g_tp_ext_prj_name = panel_data->platform_support_external_name[i];
			if(strstr(saved_command_line, panel_data->platform_support_commandline[i]) || strstr("default_commandline", panel_data->platform_support_commandline[i])) {
				pr_err("[TP] Driver match the project\n");
				return true;
			}
			else {
				break;
			}
		}
	}
	pr_err("[TP] Driver does not match the project\n");
	pr_err("Lcd module not found\n");
	return false;
}


int tp_util_get_vendor(struct hw_resource *hw_res, struct panel_info *panel_data)
{
	char *vendor;
	int prj_id = 0;

	panel_data->test_limit_name = kzalloc(MAX_LIMIT_DATA_LENGTH, GFP_KERNEL);
	if (panel_data->test_limit_name == NULL) {
		pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
	}

	prj_id = g_tp_prj_id;
	if (panel_data->tp_type == TP_SAMSUNG) {
		memcpy(panel_data->manufacture_info.version, "SL", 2);
	} else if (panel_data->tp_type == TP_BOE) {
		memcpy(panel_data->manufacture_info.version, "BS", 2);
	} else {
		memcpy(panel_data->manufacture_info.version, "0x", 2);
	}
	if (prj_id == 19795) {
		memcpy(panel_data->manufacture_info.version, "goodix_", 7);
	}
	if (prj_id == 19015 || prj_id == 19016) {
		memcpy(panel_data->manufacture_info.version, "0xbd3180000", 11);
	}
	if (prj_id == 19125) {
		memcpy(panel_data->manufacture_info.version, "0xbd2830000", 11);
	}
	if (prj_id == 20801) {
		memcpy(panel_data->manufacture_info.version, "0x504000000", 11);
	}
	if (prj_id == 21623) {
		memcpy(panel_data->manufacture_info.version, "focalt_", sizeof("focalt_"));
	}
	if (g_tp_ext_prj_name) {
		if (NULL != panel_data->manufacture_info.version) {
			strncpy(panel_data->manufacture_info.version + strlen(panel_data->manufacture_info.version),
			g_tp_ext_prj_name, 7);
			panel_data->manufacture_info.version[strlen(panel_data->manufacture_info.version)] = '\0';
		}
	}
	if (panel_data->tp_type == TP_UNKNOWN) {
		pr_err("[TP]%s type is unknown\n", __func__);
		return 0;
	}

	vendor = GET_TP_DEV_NAME(panel_data->tp_type);

	strcpy(panel_data->manufacture_info.manufacture, vendor);
	snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH,
		"tp/%d/FW_%s_%s.img",
		prj_id, panel_data->chip_name, vendor);

	if (panel_data->test_limit_name) {
		snprintf(panel_data->test_limit_name, MAX_LIMIT_DATA_LENGTH,
		"tp/%d/LIMIT_%s_%s.img",
		prj_id, panel_data->chip_name, vendor);
	}

	panel_data->manufacture_info.fw_path = panel_data->fw_name;

	if (prj_id == 20669 || prj_id == 20751) {
		snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH,
			"tp/20669/FW_%s_%s.img", panel_data->chip_name, vendor);

		if (panel_data->test_limit_name) {
			snprintf(panel_data->test_limit_name, MAX_LIMIT_DATA_LENGTH,
				"tp/20669/LIMIT_%s_%s.img", panel_data->chip_name, vendor);
		}
		pr_info("panel_data->tp_type = %d\n", panel_data->tp_type);
		if (panel_data->tp_type == TP_JDI) {
			memcpy(panel_data->manufacture_info.version, "AA869_DS_NT_", 12);
			panel_data->firmware_headfile.firmware_data = FW_17951_NT36672C_JDI;
			panel_data->firmware_headfile.firmware_size = sizeof(FW_17951_NT36672C_JDI);
		} else {
			memcpy(panel_data->manufacture_info.version, "AA869_BOE_ILI_", 14);
			panel_data->firmware_headfile.firmware_data = FW_20669_ILI7807S;
			panel_data->firmware_headfile.firmware_size = sizeof(FW_20669_ILI7807S);
		}
	}
	if (prj_id == 21027) {
		strcpy(panel_data->manufacture_info.manufacture, "BOE");
		memcpy(panel_data->manufacture_info.version, "BSFA26105", 9);
		panel_data->firmware_headfile.firmware_data = FW_21027_NT36523_BOE;
		panel_data->firmware_headfile.firmware_size = sizeof(FW_21027_NT36523_BOE);
		snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH,
		"tp/%d/FW_%s_%s.bin",
		prj_id, panel_data->chip_name, vendor);

		if (panel_data->test_limit_name) {
			snprintf(panel_data->test_limit_name, MAX_LIMIT_DATA_LENGTH,
			"tp/%d/LIMIT_%s_%s.img",
			prj_id, panel_data->chip_name, vendor);
		}
	}
	if (prj_id == 0x2065C) {
	    snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH,
            "tp/%s/FW_%s_%s.img",
            "2065C", panel_data->chip_name, vendor);

		if (panel_data->test_limit_name) {
			snprintf(panel_data->test_limit_name, MAX_LIMIT_DATA_LENGTH,
				"tp/%s/LIMIT_%s_%s.img",
				"2065C", panel_data->chip_name, vendor);
		}
		memcpy(panel_data->manufacture_info.version, "focalt_0000", 11);
		panel_data->manufacture_info.fw_path = panel_data->fw_name;
	}

	pr_info("[TP]vendor:%s fw:%s limit:%s\n",
		vendor,
		panel_data->fw_name,
		panel_data->test_limit_name == NULL?"NO Limit":panel_data->test_limit_name);
	return 0;
}

int preconfig_power_control(struct touchpanel_data *ts)
{
	return 0;
}
EXPORT_SYMBOL(preconfig_power_control);

int reconfig_power_control(struct touchpanel_data *ts)
{
	int ret = 0;
	int prj_id = 0;
	prj_id = get_project();

	if ((prj_id == 20135 || prj_id == 20137 || prj_id == 20139 || prj_id == 20235) && !strstr(saved_command_line, "20135samsung_amb655xl08_1080_2400_cmd_dvt")) {
		pr_err("[TP]pcb is old version, need to reconfig the regulator.\n");
		if (!IS_ERR_OR_NULL(ts->hw_res.vdd_2v8)) {
			regulator_put(ts->hw_res.vdd_2v8);
			ts->hw_res.vdd_2v8 = NULL;
		}
		ts->hw_res.vdd_2v8 = regulator_get(ts->dev, "vdd_dvt_2v8");
		if (IS_ERR_OR_NULL(ts->hw_res.vdd_2v8)) {
			pr_err("[TP]Regulator vdd2v8 get failed, ret = %d\n", ret);
		} else {
			if (regulator_count_voltages(ts->hw_res.vdd_2v8) > 0) {
				pr_err("[TP]set avdd voltage to %d uV\n", ts->hw_res.vdd_volt);
				if (ts->hw_res.vdd_volt) {
					ret = regulator_set_voltage(ts->hw_res.vdd_2v8, ts->hw_res.vdd_volt, ts->hw_res.vdd_volt);
				} else {
					ret = regulator_set_voltage(ts->hw_res.vdd_2v8, 3100000, 3100000);
				}
				if (ret) {
					dev_err(ts->dev, "Regulator set_vtg failed vdd rc = %d\n", ret);
				}
				ret = regulator_set_load(ts->hw_res.vdd_2v8, 200000);
				if (ret < 0) {
					dev_err(ts->dev, "Failed to set vdd_2v8 mode(rc:%d)\n", ret);
				}
			}
        	}
	}

	return 0;
}
EXPORT_SYMBOL(reconfig_power_control);
