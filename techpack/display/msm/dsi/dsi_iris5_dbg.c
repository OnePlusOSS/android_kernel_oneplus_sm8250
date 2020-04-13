#include "dsi_iris5_api.h"
#include "dsi_iris5_lightup.h"
#include "dsi_iris5_lightup_ocp.h"
#include "dsi_iris5_pq.h"
#include "dsi_iris5_ioctl.h"
#include "dsi_iris5_lut.h"
#include "dsi_iris5_mode_switch.h"
#include "dsi_iris5_osd.h"
#include "iris_log.h"

/*
 * These files include source code for the Licensed Software that is provided under the terms and
 * conditions of a Master Software License Agreement and its applicable schedules.
 */

#define IRIS_DBG_TOP_DIR "iris"
#define IRIS_DBG_FUNCSTATUS_FILE "iris5_func_status"

int iris5_dbg_fstatus_open(struct inode *inode, struct file *file)
{
	if (inode->i_private)
		file->private_data = inode->i_private;
	return 0;
}

/**
 * read module's status
 */
static ssize_t iris5_dbg_fstatus_read(struct file *file, char __user *ubuf,
				    size_t count, loff_t *ppos)
{
	char *kbuf = NULL;
	int size = PAGE_SIZE > count ? PAGE_SIZE : (int)count; //CID799203
	int len = 0;
	struct iris_setting_info *iris_setting = iris_get_setting();
	struct quality_setting *pqlt_cur_setting = &iris_setting->quality_cur;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_cfg *pcfg1 = iris_get_cfg_by_index(DSI_PRIMARY);

	if (*ppos)
		return 0;

	kbuf = kzalloc(size, GFP_KERNEL); //CID799203
	if (NULL == kbuf) { //CID89877
		IRIS_LOGE("Fatal erorr: No mem!\n");
		return -ENOMEM; //CID799203
	}

	len += snprintf(kbuf, size, //CID799203
		"iris function parameter info\n"
		"***peaking_setting***\n"
		"%-20s:\t%d\n",
		"peaking", pqlt_cur_setting->pq_setting.peaking);

	len += snprintf(kbuf+len, size - len, //CID799203
		"***system_brightness***\n"
		"%-20s:\t%d\n",
		"system_brightness",
		pqlt_cur_setting->system_brightness);

	len += snprintf(kbuf+len, size - len, //CID799203
		"***dspp_dirty***\n"
		"%-20s:\t%d\n",
		"dspp_dirty", pqlt_cur_setting->dspp_dirty);

	len += snprintf(kbuf+len, size - len, //CID799203
		"***dbc_setting***\n"
		"%-20s:\t%d\n",
		"dbc", pqlt_cur_setting->pq_setting.dbc);

	len += snprintf(kbuf+len, size - len, //CID799203
		"***lce_setting***\n"
		"%-20s:\t%d\n"
		"%-20s:\t%d\n"
		"%-20s:\t%d\n",
		"mode", pqlt_cur_setting->pq_setting.lcemode,
		"level", pqlt_cur_setting->pq_setting.lcelevel,
		"graphics_detection",
		pqlt_cur_setting->pq_setting.graphicdet);

	len += snprintf(kbuf+len, size - len, //CID799203
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

	len += snprintf(kbuf+len, size - len, //CID799203
		"***lux_value***\n"
		"%-20s:\t%d\n",
		"luxvalue", pqlt_cur_setting->luxvalue);

	len += snprintf(kbuf+len, size - len, //CID799203
		"***cct_value***\n"
		"%-20s:\t%d\n",
		"cctvalue", pqlt_cur_setting->cctvalue);

	len += snprintf(kbuf+len, size - len, //CID799203
		"***reading_mode***\n"
		"%-20s:\t%d\n",
		"readingmode", pqlt_cur_setting->pq_setting.readingmode);

	len += snprintf(kbuf+len, size - len, //CID799203
		"***ambient_lut***\n"
		"%-20s:\t%d\n"
		"%-20s:\t%d\n"
		"%-20s:\t%d\n",
		"al_en", pqlt_cur_setting->pq_setting.alenable,
		"al_luxvalue", pqlt_cur_setting->luxvalue,
		"al_bl_ratio", pqlt_cur_setting->al_bl_ratio);

	len += snprintf(kbuf+len, size - len, //CID799203
		"***sdr2hdr***\n"
		"%-20s:\t%d\n"
		"%-20s:\t%d\n",
		"sdr2hdr", pqlt_cur_setting->pq_setting.sdr2hdr,
		"maxcll", pqlt_cur_setting->maxcll);

	len += snprintf(kbuf+len, size - len, //CID799203
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

	len += snprintf(kbuf+len, size - len, //CID799203
		"***osd***\n"
		"%-20s:\t%d\n",
		"osd_en", pcfg->osd_enable);

	len += snprintf(kbuf+len, size - len, //CID799203
		"***analog_abypass***\n"
		"%-20s:\t%d\n",
		"abyp_mode", pcfg->abypss_ctrl.abypass_mode);

	len += snprintf(kbuf+len, size - len, //CID799203
		"***n2m***\n"
		"%-20s:\t%d\n",
		"n2m_en", pcfg1->n2m_enable);

	len += snprintf(kbuf+len, size - len, //CID799203
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

	size = len; //CID799203
	if ( len >= count ) { //CID799203
		size = count - 1;
	}
	if (copy_to_user(ubuf, kbuf, size)) {
		kfree(kbuf);
		return -EFAULT;
	}

	kfree(kbuf);

//CID799203 exit:
	*ppos += size;

	return size;
}


static const struct file_operations iris5_dbg_fstatus_fops = {
	.open = iris5_dbg_fstatus_open,
	.read = iris5_dbg_fstatus_read,
};

void iris_display_mode_name_update(void)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (NULL == pcfg)
		return;

	//CID799179
	strncpy(pcfg->display_mode_name, "Not Set", sizeof(pcfg->display_mode_name));

	if (ANALOG_BYPASS_MODE == pcfg->abypss_ctrl.abypass_mode) {
		strncpy(pcfg->display_mode_name, "ABYPASS", sizeof(pcfg->display_mode_name));
	} else {
		strncpy(pcfg->display_mode_name, "PT", sizeof(pcfg->display_mode_name));
		if (pcfg->pwil_mode == FRC_MODE) {
			strncpy(pcfg->display_mode_name, "MEMC", sizeof(pcfg->display_mode_name));
		} else if (pcfg->pwil_mode == RFB_MODE) {
			strncpy(pcfg->display_mode_name, "RFB", sizeof(pcfg->display_mode_name));
		}
	}
}

static ssize_t iris5_dbg_display_mode_show(struct file *file, char __user *ubuf,
					size_t count, loff_t *ppos)
{
	char *kbuf = NULL;
	int size = PAGE_SIZE > count ? PAGE_SIZE : count; //CID799222
	struct iris_cfg *pcfg = iris_get_cfg();

	if (*ppos)
		return 0;

	kbuf = kzalloc(size, GFP_KERNEL); //CID799222
	if (NULL == kbuf) { //CID89984
		IRIS_LOGE("Fatal erorr: No mem!\n");
		return -ENOMEM;	//CID799222
	}

	iris_display_mode_name_update();

	snprintf(kbuf, size,
		"%s\n", pcfg->display_mode_name);

	size = strlen(kbuf);
	if (size >= count) { //CID799222
		size = count - 1;
	}

	if (copy_to_user(ubuf, kbuf, size)) {
		kfree(kbuf);
		return -EFAULT;
	}

	kfree(kbuf);

//CID799222 exit:
	*ppos += size;

	return size;
}

static const struct file_operations iris5_dbg_dislay_mode_fops = {
	.open = simple_open,
	.read = iris5_dbg_display_mode_show,
};

int iris_fstatus_debugfs_init(struct dsi_display *display)
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
				&iris5_dbg_fstatus_fops) == NULL)
		IRIS_LOGE("create file func_status failed\n");

	if (debugfs_create_file("display_mode", 0644,
				pcfg->dbg_root, display,
				&iris5_dbg_dislay_mode_fops) == NULL)
		IRIS_LOGE("create file display_mode failed\n");

	return 0;
}
