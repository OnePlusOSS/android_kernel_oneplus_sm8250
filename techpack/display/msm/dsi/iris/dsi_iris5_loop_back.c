// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#include <drm/drm_mipi_dsi.h>
#include <video/mipi_display.h>
#include <linux/of_gpio.h>
#include <dsi_drm.h>
#include <sde_encoder_phys.h>
#include "dsi_iris5_api.h"
#include "dsi_iris5_lightup.h"
#include "dsi_iris5_lightup_ocp.h"
#include "dsi_iris5_lp.h"
#include "dsi_iris5_pq.h"
#include "dsi_iris5_ioctl.h"
#include "dsi_iris5_lut.h"
#include "dsi_iris5_mode_switch.h"
#include "dsi_iris5_gpio.h"
#include "dsi_iris5_frc.h"
#include "dsi_iris5_log.h"

static int iris_i2c_test_verify(void);

int32_t iris_parse_loopback_info(struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t rc = 0;
	u32 loop_back_mode = 0;
	u32 loop_back_mode_res = 0;

	rc = of_property_read_u32(np, "pxlw,loop-back-mode", &loop_back_mode);
	if (!rc)
		IRIS_LOGE("get property: pxlw, loop-back-mode: %d", loop_back_mode);
	pcfg->loop_back_mode = loop_back_mode;

	rc = of_property_read_u32(np, "pxlw,loop-back-mode-res", &loop_back_mode_res);
	if (!rc)
		IRIS_LOGE("get property: pxlw, loop-back-mode-res: %d", loop_back_mode_res);
	pcfg->loop_back_mode_res = loop_back_mode_res;

	return 0;
}


int iris_loop_back_reset(void)
{
	iris_send_one_wired_cmd(IRIS_RESET_SYS);

	return 0;
}

static u32 addrs[66] = {
	0xf000004c, 0xf000004c, 0xf0000048, 0xf1680008, 0xf16e0008, 0xf1a20044,
	0xf1a40044, 0xf158000c, 0xf1580290, 0xf1560118, 0xf1a00060, 0xf1520058,
	0xf10c0000, 0xf1500404, 0xf12c0000, 0xf12d0000, 0xf1640054, 0xf1200020,
	0xf120002c, 0xf120009c, 0xf1210000, 0xf1240004, 0xf1240008, 0xf124000c,
	0xf1240018, 0xf124003c, 0xf1240074, 0xf1240150, 0xf1240170, 0xf1241004,
	0xf1241084, 0xf1241098, 0xf124109c, 0xf12410b0, 0xf12410e8, 0xf1240000,
	0xf1250000, 0xf1280008, 0xf1280038, 0xf12800c4, 0xf1281004, 0xf1281014,
	0xf1281028, 0xf1290000, 0xf1220000, 0xf1220004, 0xf1220008, 0xf1220014,
	0xf122001c, 0xf1220064, 0xf16400b8, 0xf1a40000, 0xf1a40008, 0xf1a40018,
	0xf1a4001c, 0xf1a40024, 0xf1a40028, 0xf1a4002c, 0xf1500098, 0xf1500000,
	0xf1580000, 0xf1580014, 0xf1580290, 0xf1400024, 0xf140002c, 0xf141ff00
};

static u32 values[66] = {
	0x0c011800, 0x0e019c00, 0x000026a0, 0x00000800, 0x00000800, 0x00001FFF,
	0x00001FFF, 0x00000800, 0x00000001, 0x00003FFF, 0x00010800, 0x00003FFF,
	0x00001484, 0x00000800, 0x0000d04d, 0x00000000, 0x000013ff, 0x020002c3,
	0x0000000a, 0x0000000c, 0x0000000f, 0x00401384, 0x30800065, 0x50208800,
	0x04380438, 0x00000020, 0xffffffff, 0x00000545, 0x00000003, 0x00020888,
	0xe4100010, 0x0000005a, 0x00040000, 0x11210100, 0x0000005a, 0xa0e8000c,
	0x00000100, 0x00000001, 0x04380438, 0x00000003, 0x00020888, 0x0000005a,
	0x00000001, 0x00000004, 0xe0008007, 0x21008801, 0x4780010e, 0x00044100,
	0x20000186, 0x00000002, 0xb46c343c, 0x00037762, 0x00080000, 0x00020003,
	0x00020003, 0x00000019, 0x09800438, 0x00080000, 0x00000000, 0xd0840421,
	0x00010040, 0x00000010, 0x00000002, 0x00020000, 0x00200195, 0x00000101
};

void iris_ocp_i3c_write(u32 addr, u32 value)
{
	u32 values[2];

	values[0] = addr;
	values[1] = value;

	IRIS_LOGD("i3c write, addr = %x, value = %x", addr, value);
	iris_i2c_ocp_single_write(values, 1);
}

u32 iris_ocp_i3c_read(u32 addr, u32 mode)
{
	u32 values[2];
	u32 ret = 0;

	values[0] = addr;
	values[1] = mode;

	ret = iris_i2c_ocp_read(values, 1, 0);
	if (ret)
		pr_err("%s error!\n", __func__);

	return values[0];
}

static int iris_i2c_test_verify(void)
{
	int rc = 0;
	uint32_t val[10] = {10};

	val[0] = 0xf001fff8;
	rc = iris_i2c_conver_ocp_read(val, 1, false);
	IRIS_LOGD("%s,%d: value = 0x%x", __func__, __LINE__, val[0]);
	val[0] = 0xf1800000;
	iris_i2c_conver_ocp_read(val, 1, false);
	val[1] = val[0] | 0x1;
	val[0] = 0xf1800000;
	iris_i2c_single_conver_ocp_write(val, 1);
	rc = iris_i2c_conver_ocp_read(val, 1, false);
	IRIS_LOGD("%s,%d: value = 0x%x", __func__, __LINE__, val[0]);

	return rc;

}

u32 iris_loop_back_verify(void)
{

	u32 i, r, g, b;
	struct iris_cfg *pcfg;
	u32 ret = 0;
	u32 standard_rgbsum[3] = {0x40d1a890, 0x318c343c, 0x37839da4};

	pcfg = iris_get_cfg();

	IRIS_LOGD("loop back verify!\n");

	iris_ocp_i3c_write(0xf0000044, 0x00000400);
	iris_ocp_i3c_write(0xf00000c0, 0x00000055);
	iris_ocp_i3c_write(0xf0000060, 0x0f0303fe);
	iris_ocp_i3c_write(0xf0000060, 0x0f0303f6);
	iris_ocp_i3c_write(0xf0000060, 0x0f0303fe);
	msleep(100);

	//iris_ocp_i3c_write(0xf0000050, 0x00003f00);
	iris_ocp_i3c_write(0xf0000004, 0x002a80a9);
	iris_ocp_i3c_write(0xf0000008, 0x0010f018);
	iris_ocp_i3c_write(0xf0000000, 0x00000081);
	iris_ocp_i3c_write(0xf0000000, 0x00000083);
	iris_ocp_i3c_write(0xf0000000, 0x00000081);
	msleep(10);
	iris_ocp_i3c_write(0xf0000050, 0x00000000);

	iris_ocp_i3c_write(0xf120005c, 0x00fffffe);

	for (i = 0; i < 66; i++)
		iris_ocp_i3c_write(addrs[i], values[i]);

	msleep(1);
	iris_ocp_i3c_write(0xf1200020, 0x02000ac3);
	iris_ocp_i3c_write(0xf1210000, 0x3);
	msleep(20);
	iris_ocp_i3c_write(0xf1200020, 0x020002c3);

	r = iris_ocp_i3c_read(0xf12401a8, DSI_CMD_SET_STATE_HS);
	g = iris_ocp_i3c_read(0xf12401ac, DSI_CMD_SET_STATE_HS);
	b = iris_ocp_i3c_read(0xf12401b0, DSI_CMD_SET_STATE_HS);
	IRIS_LOGD("r = 0x%08x, g = 0x%08x, b = 0x%08x\n", r, g, b);

	if ((r == standard_rgbsum[0]) && (g == standard_rgbsum[1]) && (b == standard_rgbsum[2]))
		ret = 0;
	else
		ret = 3;

	return ret;

}

int iris_loop_back_validate(void)
{

	int rc = 0;
	int temp = 0;
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();

	IRIS_LOGI("[%s:%d] loop back test.", __func__, __LINE__);

	rc = iris_loop_back_reset();
	if (rc) {
		IRIS_LOGW("[%s:%d] loop back iris reset rc = %d", __func__, __LINE__, rc);
		return rc;
	}

	msleep(10);
	temp = iris_ocp_i3c_read(0xf00000d0, DSI_CMD_SET_STATE_HS);
	IRIS_LOGD("%s,%d: value = 0x%x", __func__, __LINE__, temp);

	msleep(10);

	temp = iris_ocp_i3c_read(0xf1800000, DSI_CMD_SET_STATE_HS);
	IRIS_LOGD("%s,%d: value = 0x%x", __func__, __LINE__, temp);
	temp &= (~0x1);
	IRIS_LOGD("%s,%d: value = 0x%x", __func__, __LINE__, temp);
	iris_ocp_i3c_write(0xf1800000, temp);
	temp = iris_ocp_i3c_read(0xf1800000, DSI_CMD_SET_STATE_HS);
	IRIS_LOGD("%s,%d: value = 0x%x", __func__, __LINE__, temp);

	rc = iris_loop_back_verify();
	if (rc) {
		IRIS_LOGE("[%s:%d] rc = %d", __func__, __LINE__, rc);
		return rc;
	}

	rc = iris_i2c_test_verify();
	if (rc) {
		IRIS_LOGE("[%s:%d] i2c read rc = %d", __func__, __LINE__, rc);
		return rc;
	}

	rc = iris_loop_back_reset();
	if (rc) {
		IRIS_LOGW("[%s:%d] loop back iris reset rc = %d", __func__, __LINE__, rc);
		return rc;
	}

	iris_abyp_lp(ABYP_POWER_DOWN_PLL);

	return rc;
}

static ssize_t _iris_dbg_loop_back_ops(struct file *file,
		const char __user *buff, size_t count, loff_t *ppos)
{
	unsigned long val;
	uint32_t temp, values[2];

	if (kstrtoul_from_user(buff, count, 0, &val))
		return -EFAULT;

	if (val == 0) {
		iris_reset();
		IRIS_LOGE("iris reset.");
	} else if (val == 1) {
		iris_exit_abyp(true);
		IRIS_LOGE("iris exit abyp.");
	} else if (val == 2) {
		iris_ocp_write_val(0xf00000c0, 0x0);
		IRIS_LOGE("enable analog bypass.");
	} else if (val == 3) {
		values[0] = 0xf1800000;
		values[1] = 0;
		iris_i2c_ocp_read(values, 1, 0);
		temp = values[0];
		IRIS_LOGD("%s(%d), value = 0x%x", __func__, __LINE__, temp);
		temp &= (~0x1);
		IRIS_LOGD("%s(%d), value = 0x%x", __func__, __LINE__, temp);
		values[0] = 0xf1800000;
		values[1] = temp;
		iris_i2c_ocp_single_write(values, 1);
		values[0] = 0xf1800000;
		values[1] = 0;
		iris_i2c_ocp_read(values, 1, 0);
		temp = values[0];
		IRIS_LOGD("%s(%d), value = 0x%x", __func__, __LINE__, temp);
		IRIS_LOGE("%s(%d), disable mipi rx", __func__, __LINE__);
	} else if (val == 4) {
		iris_loop_back_verify();
	} else if (val == 5) {
		temp = 0x400;
		values[0] = 0xf0000044;
		values[1] = temp;
		iris_i2c_ocp_single_write(values, 1);
		IRIS_LOGE("%s(%d), rst dtg!", __func__, __LINE__);
	} else if (val == 6) {
		temp = 0x55;
		IRIS_LOGD("%s(%d), value = 0x%x", __func__, __LINE__, temp);
		values[0] = 0xf00000c0;
		values[1] = temp;
		iris_i2c_ocp_single_write(values, 1);
		IRIS_LOGE("%s(%d), disable ulps!", __func__, __LINE__);
	} else if (val == 7) {
		iris_send_ipopt_cmds(IRIS_IP_SYS, 0x26);
	} else {
		pr_err("%s(%d), parameter error!", __func__, __LINE__);
	}

	return count;
}

static ssize_t _iris_dbg_loop_back_test(struct file *file, char __user *buff,
		size_t count, loff_t *ppos)
{
	int ret = 0;
	ktime_t ktime0;
	ktime_t ktime1;
	uint32_t timeus = 0;
	struct iris_cfg *pcfg;
	int tot = 0;
	char bp[512];

	if (*ppos)
		return 0;

	pcfg = iris_get_cfg();

	mutex_lock(&pcfg->panel->panel_lock);
	ktime0 = ktime_get();
	ret = iris_loop_back_validate();
	ktime1 = ktime_get();
	timeus = (u32) ktime_to_us(ktime1) - (u32)ktime_to_us(ktime0);
	mutex_unlock(&pcfg->panel->panel_lock);
	IRIS_LOGI("%s(), spend time %d us, return: %d", __func__, timeus, ret);


	tot = scnprintf(bp, sizeof(bp), "0x%02x\n", ret);
	if (copy_to_user(buff, bp, tot))
		return -EFAULT;
	*ppos += tot;

	return tot;

}

static const struct file_operations iris_loop_back_fops = {
	.open = simple_open,
	.write = _iris_dbg_loop_back_ops,
	.read = _iris_dbg_loop_back_test,
};

int iris_loop_back_init(struct dsi_display *display)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	if (pcfg->dbg_root == NULL) {
		pcfg->dbg_root = debugfs_create_dir("iris", NULL);
		if (IS_ERR_OR_NULL(pcfg->dbg_root)) {
			IRIS_LOGE("debugfs_create_dir for iris_debug failed, error %ld",
					PTR_ERR(pcfg->dbg_root));
			return -ENODEV;
		}
	}

	if (debugfs_create_file("iris_loop_back",	0644, pcfg->dbg_root, display,
				&iris_loop_back_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
				__FILE__, __LINE__);
		return -EFAULT;
	}

	return 0;
}
