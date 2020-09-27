// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#include "dsi_iris5_api.h"
#include "dsi_iris5_lightup.h"
#include "dsi_iris5_lightup_ocp.h"
#include "dsi_iris5_pq.h"
#include "dsi_iris5_ioctl.h"
#include "dsi_iris5_lut.h"
#include "dsi_iris5_mode_switch.h"
#include "dsi_iris5_log.h"

#define IRIS_DBG_TOP_DIR "iris"
#define IRIS_DBG_FUNCSTATUS_FILE "iris_func_status"

int iris_dbg_fstatus_open(struct inode *inode, struct file *file)
{
	if (inode->i_private)
		file->private_data = inode->i_private;
	return 0;
}

/**
 * read module's status
 */
static ssize_t iris_dbg_fstatus_read(struct file *file, char __user *ubuf,
		size_t count, loff_t *ppos)
{
	char *kbuf = NULL;
	int size = count < PAGE_SIZE ? PAGE_SIZE : (int)count;
	int len = 0;
	struct iris_setting_info *iris_setting = iris_get_setting();
	struct quality_setting *pqlt_cur_setting = &iris_setting->quality_cur;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_cfg *pcfg1 = iris_get_cfg_by_index(DSI_PRIMARY);

	if (*ppos)
		return 0;

	kbuf = vzalloc(size);
	if (kbuf == NULL) {
		IRIS_LOGE("Fatal erorr: No mem!\n");
		return -ENOMEM;
	}

	len += snprintf(kbuf, size,
			"iris function parameter info\n"
			"***peaking_setting***\n"
			"%-20s:\t%d\n",
			"peaking", pqlt_cur_setting->pq_setting.peaking);

	len += snprintf(kbuf+len, size - len,
			"***system_brightness***\n"
			"%-20s:\t%d\n",
			"system_brightness",
			pqlt_cur_setting->system_brightness);

	len += snprintf(kbuf+len, size - len,
			"***dspp_dirty***\n"
			"%-20s:\t%d\n",
			"dspp_dirty", pqlt_cur_setting->dspp_dirty);

	len += snprintf(kbuf+len, size - len,
			"***dbc_setting***\n"
			"%-20s:\t%d\n",
			"dbc", pqlt_cur_setting->pq_setting.dbc);

	len += snprintf(kbuf+len, size - len,
			"***lce_setting***\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n",
			"mode", pqlt_cur_setting->pq_setting.lcemode,
			"level", pqlt_cur_setting->pq_setting.lcelevel,
			"graphics_detection",
			pqlt_cur_setting->pq_setting.graphicdet);

	len += snprintf(kbuf+len, size - len,
			"***cm_setting***\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n",
			"cm6axis", pqlt_cur_setting->pq_setting.cm6axis,
			"cmftc", pqlt_cur_setting->cmftc,
			"cmcolortempmode",
			pqlt_cur_setting->pq_setting.cmcolortempmode,
			"colortempvalue", pqlt_cur_setting->colortempvalue,
			"min_colortempvalue",
			pqlt_cur_setting->min_colortempvalue,
			"max_colortempvalue",
			pqlt_cur_setting->max_colortempvalue,
			"cmcolorgamut",
			pqlt_cur_setting->pq_setting.cmcolorgamut,
			"demomode", pqlt_cur_setting->pq_setting.demomode,
			"source_switch", pqlt_cur_setting->source_switch);

	len += snprintf(kbuf+len, size - len,
			"***lux_value***\n"
			"%-20s:\t%d\n",
			"luxvalue", pqlt_cur_setting->luxvalue);

	len += snprintf(kbuf+len, size - len,
			"***cct_value***\n"
			"%-20s:\t%d\n",
			"cctvalue", pqlt_cur_setting->cctvalue);

	len += snprintf(kbuf+len, size - len,
			"***reading_mode***\n"
			"%-20s:\t%d\n",
			"readingmode", pqlt_cur_setting->pq_setting.readingmode);

	len += snprintf(kbuf+len, size - len,
			"***ambient_lut***\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n",
			"al_en", pqlt_cur_setting->pq_setting.alenable,
			"al_luxvalue", pqlt_cur_setting->luxvalue,
			"al_bl_ratio", pqlt_cur_setting->al_bl_ratio);

	len += snprintf(kbuf+len, size - len,
			"***sdr2hdr***\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n",
			"sdr2hdr", pqlt_cur_setting->pq_setting.sdr2hdr,
			"maxcll", pqlt_cur_setting->maxcll);

	len += snprintf(kbuf+len, size - len,
			"***frc_setting***\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n"
			"%-20s:\t%d\n",
			"memc_level", pcfg1->frc_setting.memc_level,
			"memc_osd", pcfg1->frc_setting.memc_osd,
			"in_fps", pcfg->frc_setting.in_fps,
			"out_fps", pcfg->frc_setting.out_fps,
			"in_fps_configured", pcfg->frc_setting.in_fps_configured,
			"low_latency", pcfg->frc_low_latency);

	len += snprintf(kbuf+len, size - len,
			"***osd***\n"
			"%-20s:\t%d\n",
			"osd_en", pcfg->osd_enable);

	len += snprintf(kbuf+len, size - len,
			"***analog_abypass***\n"
			"%-20s:\t%d\n",
			"abyp_mode", pcfg->abypss_ctrl.abypass_mode);

	len += snprintf(kbuf+len, size - len,
			"***n2m***\n"
			"%-20s:\t%d\n",
			"n2m_en", pcfg1->n2m_enable);

	len += snprintf(kbuf+len, size - len,
			"***osd protect window***\n"
			"%-20s:\t0x%x\n"
			"%-20s:\t0x%x\n"
			"%-20s:\t0x%x\n"
			"%-20s:\t0x%x\n"
			"%-20s:\t0x%x\n"
			"%-20s:\t0x%x\n"
			"%-20s:\t0x%x\n"
			"%-20s:\t0x%x\n"
			"%-20s:\t0x%x\n"
			"%-20s:\t0x%x\n"
			"%-20s:\t0x%x\n"
			"%-20s:\t0x%x\n",
			"osd0_tl", pcfg1->frc_setting.iris_osd0_tl,
			"osd0_br", pcfg1->frc_setting.iris_osd0_br,
			"osd1_tl", pcfg1->frc_setting.iris_osd1_tl,
			"osd1_br", pcfg1->frc_setting.iris_osd1_br,
			"osd2_tl", pcfg1->frc_setting.iris_osd2_tl,
			"osd2_br", pcfg1->frc_setting.iris_osd2_br,
			"osd3_tl", pcfg1->frc_setting.iris_osd3_tl,
			"osd3_br", pcfg1->frc_setting.iris_osd3_br,
			"osd4_tl", pcfg1->frc_setting.iris_osd4_tl,
			"osd4_br", pcfg1->frc_setting.iris_osd4_br,
			"osd_win_ctrl", pcfg1->frc_setting.iris_osd_window_ctrl,
			"osd_win_ctrl", pcfg1->frc_setting.iris_osd_win_dynCompensate);

	len += snprintf(kbuf+len, size - len,
			"***firmware_version***\n"
			"%-20s:\t%d\n"  //CID101064
			"%-20s:\t%d%d/%d/%d\n",
			"version", pcfg1->app_version,
			"date", pcfg1->app_date[3], pcfg1->app_date[2], pcfg1->app_date[1], pcfg1->app_date[0]);

	size = len;
	if (len >= count)
		size = count - 1;

	if (copy_to_user(ubuf, kbuf, size)) {
		vfree(kbuf);
		return -EFAULT;
	}

	vfree(kbuf);

	*ppos += size;

	return size;
}


static const struct file_operations iris_dbg_fstatus_fops = {
	.open = iris_dbg_fstatus_open,
	.read = iris_dbg_fstatus_read,
};

void iris_display_mode_name_update(void)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (pcfg == NULL)
		return;

	strlcpy(pcfg->display_mode_name, "Not Set", sizeof(pcfg->display_mode_name));

	if (pcfg->abypss_ctrl.abypass_mode == ANALOG_BYPASS_MODE) {
		strlcpy(pcfg->display_mode_name, "ABYPASS", sizeof(pcfg->display_mode_name));
	} else {
		if (pcfg->osd_enable) {
			if (pcfg->pwil_mode == FRC_MODE)
				strncpy(pcfg->display_mode_name, "DUAL-MEMC", sizeof(pcfg->display_mode_name));
			else
				strncpy(pcfg->display_mode_name, "DUAL-PT", sizeof(pcfg->display_mode_name));
			return;
		}
		strncpy(pcfg->display_mode_name, "PT", sizeof(pcfg->display_mode_name));
		if (pcfg->pwil_mode == FRC_MODE) {
			strlcpy(pcfg->display_mode_name, "MEMC", sizeof(pcfg->display_mode_name));
		} else if (pcfg->pwil_mode == RFB_MODE) {
			strlcpy(pcfg->display_mode_name, "RFB", sizeof(pcfg->display_mode_name));
		}
	}
}

static ssize_t iris_dbg_display_mode_show(struct file *file, char __user *ubuf,
		size_t count, loff_t *ppos)
{
	char *kbuf = NULL;
	int size = count < PAGE_SIZE ? PAGE_SIZE : count;
	struct iris_cfg *pcfg = iris_get_cfg();

	if (*ppos)
		return 0;

	kbuf = vzalloc(size);
	if (kbuf == NULL) {
		IRIS_LOGE("Fatal erorr: No mem!\n");
		return -ENOMEM;
	}

	iris_display_mode_name_update();

	snprintf(kbuf, size,
			"%s\n", pcfg->display_mode_name);

	size = strlen(kbuf);
	if (size >= count)
		size = count - 1;

	if (copy_to_user(ubuf, kbuf, size)) {
		vfree(kbuf);
		return -EFAULT;
	}

	vfree(kbuf);

	*ppos += size;

	return size;
}

static const struct file_operations iris_dbg_dislay_mode_fops = {
	.open = simple_open,
	.read = iris_dbg_display_mode_show,
};

int iris_dbgfs_status_init(struct dsi_display *display)
{
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();

	if (pcfg->dbg_root == NULL) {
		pcfg->dbg_root = debugfs_create_dir(IRIS_DBG_TOP_DIR, NULL);
		if (IS_ERR_OR_NULL(pcfg->dbg_root)) {
			IRIS_LOGE("create dir for iris failed, error %ld",
					PTR_ERR(pcfg->dbg_root));
			return -ENODEV;
		}
	}

	if (debugfs_create_file(IRIS_DBG_FUNCSTATUS_FILE, 0644,
				pcfg->dbg_root, display,
				&iris_dbg_fstatus_fops) == NULL)
		IRIS_LOGE("create file func_status failed\n");

	if (debugfs_create_file("display_mode", 0644,
				pcfg->dbg_root, display,
				&iris_dbg_dislay_mode_fops) == NULL)
		IRIS_LOGE("create file display_mode failed\n");

	return 0;
}
