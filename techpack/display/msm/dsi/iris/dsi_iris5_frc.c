// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#include <linux/types.h>
#include <dsi_drm.h>
#include <sde_encoder_phys.h>
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_iris5.h"
#include "dsi_iris5_api.h"
#include "dsi_iris5_lightup.h"
#include "dsi_iris5_lightup_ocp.h"
#include "dsi_iris5_mode_switch.h"
#include "dsi_iris5_lp.h"
#include "dsi_iris5_pq.h"
#include "dsi_iris5_log.h"


enum {
	MIPI2_LP_PRE = 0,
	MIPI2_LP_SWRST,
	MIPI2_LP_POST,
	MIPI2_LP_FINISH,
	MIPI2_LP_INVALID,
};

struct osd_blending_st {
	atomic_t compression_mode;
	uint32_t enter_lp_st;
};

static int32_t iris_disable_osd_autorefresh;
static struct osd_blending_st osd_blending_work;

static void __iris_update_vfr_work(struct work_struct *work)
{
	struct iris_cfg *pcfg = container_of(work, struct iris_cfg, vfr_update_work);

	if (atomic_read(&pcfg->video_update_wo_osd) >= 4) {
		if (iris_update_vfr(pcfg, true))
			IRIS_LOGI("enable vfr");
	} else {
		if (iris_update_vfr(pcfg, false))
			IRIS_LOGI("disable vfr");
	}
}

void iris_init_vfr_work(struct iris_cfg *pcfg)
{
	INIT_WORK(&pcfg->vfr_update_work, __iris_update_vfr_work);
}

void iris_frc_low_latency(bool low_latency)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	pcfg->frc_low_latency = low_latency;

	IRIS_LOGI("%s(%d) low latency: %d.", __func__, __LINE__, low_latency);
}

void iris_set_panel_te(u8 panel_te)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	pcfg->panel_te = panel_te;
}

void iris_set_n2m_enable(bool enable)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	pcfg->n2m_enable = enable;
}

void iris_frc_parameter_init(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	struct dsi_mode_info *timing;
	u32 refresh_rate;

	osd_blending_work.enter_lp_st = MIPI2_LP_FINISH;

	if (!pcfg->panel->cur_mode)
		return;

	timing = &pcfg->panel->cur_mode->timing;
	refresh_rate = timing->refresh_rate;
	IRIS_LOGI("refresh_rate: %d!", refresh_rate);
	if ((refresh_rate % 10) != 0) {
		refresh_rate = ((refresh_rate + 5) / 10) * 10;
		IRIS_LOGW("change refresh_rate from %d to %d!", timing->refresh_rate, refresh_rate);
	}
	pcfg->frc_setting.out_fps = refresh_rate;
	pcfg->frc_setting.default_out_fps = refresh_rate;
	pcfg->panel_te = timing->refresh_rate;
	pcfg->ap_te = timing->refresh_rate;
	pcfg->frc_setting.input_vtotal = DSI_V_TOTAL(timing);
	// temp treat display timing same as input timing
	pcfg->frc_setting.disp_hres = timing->h_active;
	pcfg->frc_setting.disp_vres = timing->v_active;
	pcfg->frc_setting.disp_htotal = DSI_H_TOTAL(timing);
	pcfg->frc_setting.disp_vtotal = DSI_V_TOTAL(timing);
}

int32_t iris_parse_frc_setting(struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t rc = 0;

	pcfg->frc_enable = false;
	pcfg->frc_setting.memc_level = 3;
	pcfg->frc_setting.memc_osd = 0;
	pcfg->frc_setting.mv_buf_num = 7;
	pcfg->frc_setting.in_fps = 24;
	pcfg->frc_setting.mv_baseaddr = 0x002c0000; // 7 MV buffers

	// Move panel timing parse to iris_pq_parameter_init().
	/* panel_te, ap_te get too late in IrisService, add pxlw,panel-te and pxlw,ap-te in dtsi */
	pcfg->panel_te = 60;
	of_property_read_u32(np, "pxlw,panel-te", &(pcfg->panel_te));
	pcfg->ap_te = 60;
	of_property_read_u32(np, "pxlw,ap-te", &(pcfg->ap_te));

	pcfg->frc_enable = true;

	return rc;
}

bool iris_get_dual2single_status(void)
{
	u32 rc = 0;

	rc = iris_ocp_read(IRIS_PWIL_CUR_META0, DSI_CMD_SET_STATE_HS);
	if ((rc != 0 && (rc & BIT(10)) == 0))
		return true;

	return false;
}

static void _iris_second_channel_pre(bool dsc_enabled)
{
	iris_send_ipopt_cmds(IRIS_IP_DMA, 0xe7);
}

int32_t iris_set_second_channel_power(bool pwr)
{
	bool compression_mode;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_cfg *pcfg2 = iris_get_cfg_by_index(DSI_SECONDARY);
	struct iris_update_regval regval;

	if (unlikely(!pcfg2->panel)) {
		IRIS_LOGE("%s(), no secondary panel configured!", __func__);
		return -EFAULT;
	}

	if (pwr) {	//on
		regval.ip = IRIS_IP_SYS;
		regval.opt_id = 0x06;
		regval.mask = 0x2000;
		regval.value = 0x2000;
		iris_update_bitmask_regval_nonread(&regval, true);
		if (osd_blending_work.enter_lp_st != MIPI2_LP_FINISH) {
			IRIS_LOGE("osd_disable_work is still in queue entry");
			osd_blending_work.enter_lp_st = MIPI2_LP_FINISH;
		}

		if (pcfg2->iris_osd_autorefresh_enabled)
			IRIS_LOGW("osd_autorefresh is not disable before enable osd!");
		else
			IRIS_LOGI("osd_autorefresh is disable before enable osd!");

		if (pcfg2->panel->power_info.refcount == 0) {
			IRIS_LOGW("%s(), AP mipi2 tx hasn't been power on.", __func__);
			pcfg2->osd_switch_on_pending = true;
		} else {
			if (pcfg->panel->cur_mode && pcfg->panel->cur_mode->priv_info && pcfg->panel->cur_mode->priv_info->dsc_enabled)
				compression_mode = true;
			else
				compression_mode = false;

			IRIS_LOGI("%s(), iris_pmu_mipi2 on.", __func__);

			/*Power up BSRAM domain if need*/
			iris_pmu_bsram_set(true);
			/* power up & config mipi2 domain */
			iris_pmu_mipi2_set(true);
			udelay(300);

			iris_set_cfg_index(DSI_PRIMARY);
			iris_ulps_source_sel(ULPS_NONE);
			_iris_second_channel_pre(compression_mode);

			IRIS_LOGI("%s(), mipi pwr st is true", __func__);
			pcfg2->mipi_pwr_st = true;
		}
	} else {	//off
		/* power down mipi2 domain */
		regval.ip = IRIS_IP_SYS;
		regval.opt_id = 0x06;
		regval.mask = 0x2000;
		regval.value = 0x0;
		iris_update_bitmask_regval_nonread(&regval,true);

		iris_pmu_mipi2_set(false);
		IRIS_LOGI("%s(), iris_pmu_mipi2 off.", __func__);
		pcfg2->mipi_pwr_st = false;
	}

	return 0;
}

void iris_second_channel_post(u32 val)
{
        int i = 0;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	//struct iris_cfg *pcfg2 = iris_get_cfg_by_index(DSI_SECONDARY);
	osd_blending_work.enter_lp_st = MIPI2_LP_SWRST;
	iris_send_ipopt_cmds(IRIS_IP_PWIL_2, 0xf1);

	for(i = 0; i < 10; i++)
	    iris_send_ipopt_cmds(IRIS_IP_PWIL_2, 0xf1);

	IRIS_LOGD("%s(), MIPI2_LP_SWRST", __func__);
	/*wait a frame to ensure the pwil_v6 SW_RST is sent*/
	msleep(20);
	osd_blending_work.enter_lp_st = MIPI2_LP_POST;
	IRIS_LOGD("%s(), MIPI2_LP_POST", __func__);
	/* power down mipi2 domain */
	//iris_pmu_mipi2_set(false);
	/* bulksram retain on when FRC power on */
	if (pcfg->pwil_mode == PT_MODE) {
		if (iris_pmu_frc_get())
			IRIS_LOGI("FRC power on, can't power off bulksram");
		else {
			iris_pmu_bsram_set(false);
			if (iris_i3c_status_get() == false)
				iris_ulps_source_sel(ULPS_MAIN);
		}
	}
	//pcfg->osd_enable = false;
	osd_blending_work.enter_lp_st = MIPI2_LP_FINISH;
	IRIS_LOGD("%s(), MIPI2_LP_FINISH", __func__);
}

static void _iris_osd_blending_off(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	IRIS_LOGD("%s, ", __func__);

	iris_set_pwil_mode(pcfg->panel, pcfg->pwil_mode, false, DSI_CMD_SET_STATE_HS);
	iris_set_pwil_disp_ctrl();
	iris_psf_mif_efifo_set(pcfg->pwil_mode, false);
}

static void _iris_osd_blending_on(void)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	IRIS_LOGD("%s(), ", __func__);
	if (pcfg->pwil_mode == PT_MODE)
		iris_psf_mif_efifo_set(pcfg->pwil_mode, true);

	iris_set_pwil_mode(pcfg->panel, pcfg->pwil_mode, true, DSI_CMD_SET_STATE_HS);
	iris_set_pwil_disp_ctrl();
}

int32_t iris_switch_osd_blending(u32 val)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_cfg *pcfg2 = iris_get_cfg_by_index(DSI_SECONDARY);

	if (unlikely(!pcfg2->panel)) {
		IRIS_LOGE("%s(), No secondary panel configured!", __func__);
		return -EFAULT;
	}

	if (val) {
		if (pcfg->dual_test & 0x100) {
			// check MIPI_RX AUX 2b_page register
			uint32_t *payload;
			u32 cmd_2b_page;
			int count = 0;

			payload = iris_get_ipopt_payload_data(IRIS_IP_RX_2, 0xF0, 2);
			while (count < 20) {
				cmd_2b_page = iris_ocp_read(0xf1840304, DSI_CMD_SET_STATE_HS);
				if (cmd_2b_page != payload[2]) {
					count++;
					IRIS_LOGW("Warning: cmd_2b_page: %x not right, %d!", cmd_2b_page, count);
					usleep_range(2000, 2100);
					_iris_second_channel_pre(true);
				} else
					break;
			}
		}
		pcfg->osd_enable = true;
		if (osd_blending_work.enter_lp_st != MIPI2_LP_FINISH) {
			//	IRIS_LOGE("osd_disable_work is still in queue entry");
			osd_blending_work.enter_lp_st = MIPI2_LP_FINISH;
		}
		//if (pcfg2->panel->power_info.refcount == 0) {
		//	IRIS_LOGW("%s: AP mipi2 tx hasn't been power on.", __func__);
		//	pcfg2->osd_switch_on_pending = true;
		//} else
		_iris_osd_blending_on();
		pcfg->osd_on = true;
	} else {
		osd_blending_work.enter_lp_st = MIPI2_LP_PRE;
		pcfg->osd_enable = false;
		_iris_osd_blending_off();
		pcfg->osd_on = false;
	}

	return 0;
}

int32_t iris_osd_autorefresh(u32 val)
{
	int osd_gpio = -1;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_SECONDARY);

	if (iris_disable_osd_autorefresh) {
		pcfg->iris_osd_autorefresh = false;
		IRIS_LOGI("%s(), osd autofresh is disable.", __func__);
		return 0;
	}

	IRIS_LOGI("%s(%d), value: %d", __func__, __LINE__, val);
	if (pcfg == NULL) {
		IRIS_LOGE("%s(), no secondary display.", __func__);
		return -EINVAL;
	}

	osd_gpio = pcfg->iris_osd_gpio;
	if (!gpio_is_valid(osd_gpio)) {
		IRIS_LOGE("%s(), invalid GPIO %d", __func__, osd_gpio);
		return -EINVAL;
	}

	if (val) {
		IRIS_LOGI("%s(), enable osd auto refresh", __func__);
		enable_irq(gpio_to_irq(osd_gpio));
		pcfg->iris_osd_autorefresh = true;
	} else {
		IRIS_LOGI("%s(), disable osd auto refresh", __func__);
		disable_irq(gpio_to_irq(osd_gpio));
		pcfg->iris_osd_autorefresh = false;
	}

	return 0;
}

int32_t iris_get_osd_overflow_st(void)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (atomic_read(&pcfg->osd_irq_cnt) >= 2)
		return 1;

	return 0;	// overflow, old define
}

irqreturn_t iris_osd_handler(int irq, void *data)
{
	struct dsi_display *display = data;
	struct drm_encoder *enc = NULL;

	if (display == NULL) {
		IRIS_LOGE("%s(), invalid display.", __func__);
		return IRQ_NONE;
	}

	IRIS_LOGV("%s(), irq: %d, display: %s", __func__, irq, display->name);
	if (display && display->bridge)
		enc = display->bridge->base.encoder;

	if (enc)
		sde_encoder_disable_autorefresh_handler(enc);
	else
		IRIS_LOGW("%s(), no encoder.", __func__);

	return IRQ_HANDLED;
}

void iris_register_osd_irq(void *disp)
{
	int rc = 0;
	int osd_gpio = -1;
	struct dsi_display *display = NULL;
	struct platform_device *pdev = NULL;
	struct iris_cfg *pcfg = NULL;

	if (!iris_is_dual_supported())
		return;

	if (!disp) {
		IRIS_LOGE("%s(), invalid display.", __func__);
		return;
	}

	display = (struct dsi_display *)disp;
	if (!iris_virtual_display(display))
		return;

	pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	osd_gpio = pcfg->iris_osd_gpio;
	IRIS_LOGI("%s(), for display %s, osd status gpio is %d",
			__func__,
			display->name, osd_gpio);
	if (!gpio_is_valid(osd_gpio)) {
		IRIS_LOGE("%s(%d), osd status gpio not specified",
				__func__, __LINE__);
		return;
	}

	pdev = display->pdev;
	IRIS_LOGI("%s, display: %s, irq: %d", __func__, display->name, gpio_to_irq(osd_gpio));
	rc = devm_request_irq(&pdev->dev, gpio_to_irq(osd_gpio), iris_osd_handler,
			IRQF_TRIGGER_RISING, "OSD_GPIO", display);
	if (rc) {
		IRIS_LOGE("%s(), IRIS OSD request irq failed", __func__);
		return;
	}

	disable_irq(gpio_to_irq(osd_gpio));
}

static void _iris_osd_irq_cnt_clean(void)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	atomic_set(&pcfg->osd_irq_cnt, 0);
}

bool iris_secondary_display_autorefresh(void *phys_enc)
{
	struct sde_encoder_phys *phys_encoder = phys_enc;
	struct sde_connector *c_conn = NULL;
	struct dsi_display *display = NULL;
	struct iris_cfg *pcfg = NULL;

	if (phys_encoder == NULL)
		return false;

	if (phys_encoder->connector == NULL)
		return false;

	c_conn = to_sde_connector(phys_encoder->connector);
	if (c_conn == NULL)
		return false;

	display = c_conn->display;
	if (display == NULL)
		return false;

	if (!iris_virtual_display(display))
		return false;

	pcfg = iris_get_cfg_by_index(DSI_SECONDARY);
	IRIS_LOGV("%s(), auto refresh: %s", __func__,
			pcfg->iris_osd_autorefresh ? "true" : "false");
	if (!pcfg->iris_osd_autorefresh) {
		pcfg->iris_osd_autorefresh_enabled = false;
		return false;
	}

	pcfg->iris_osd_autorefresh_enabled = true;
	_iris_osd_irq_cnt_clean();

	return true;
}

void iris_inc_osd_irq_cnt(void)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	atomic_inc(&pcfg->osd_irq_cnt);
	IRIS_LOGD("osd_irq: %d", atomic_read(&pcfg->osd_irq_cnt));
}

void iris_frc_prepare(struct iris_cfg *pcfg)
{
	if (pcfg->osd_switch_on_pending) {
                struct iris_cfg *pcfg0 = iris_get_cfg_by_index(DSI_PRIMARY);
		pcfg->osd_switch_on_pending = false;
		//schedule_work(&osd_blending_work.osd_enable_work);
                IRIS_LOGI("%s(), set secondary channel power for osd pending", __func__);
                mutex_lock(&pcfg0->panel->panel_lock);
		iris_set_second_channel_power(true);
		mutex_unlock(&pcfg0->panel->panel_lock);
		IRIS_LOGI("%s(), finish setting secondary channel power", __func__);
	}
}

void iris_clean_frc_status(struct iris_cfg *pcfg)
{
	if (pcfg->rx_mode == pcfg->tx_mode)
		pcfg->pwil_mode = PT_MODE;
	else
		pcfg->pwil_mode = RFB_MODE;
	IRIS_LOGI("%s(), pwil_mode: %d", __func__, pcfg->pwil_mode);

	pcfg->switch_mode = IRIS_MODE_RFB;
	pcfg->osd_enable = false;
	pcfg->osd_on = false;
	pcfg->frc_setting.in_fps_configured = 0;
	pcfg->mcu_code_downloaded = false;
	if (pcfg->tx_mode == 0) // video mode
		iris_set_frc_var_display(0);
	pcfg->dynamic_vfr = false;
	atomic_set(&pcfg->video_update_wo_osd, 0);
	cancel_work_sync(&pcfg->vfr_update_work);
	atomic_set(&pcfg->osd_irq_cnt, 0);
}

int32_t iris_dbgfs_frc_init(void)
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

	debugfs_create_u32("disable_osd_autorefresh", 0644, pcfg->dbg_root,
			(u32 *)&iris_disable_osd_autorefresh);

	return 0;
}

static void iris_frc_setting_switch(bool dual)
{
	uint32_t *payload = NULL;
	uint32_t process_hres[2];	// 0: single, 1: dual
	uint32_t process_vres[2];
	uint32_t mv_buf_number[2];
	uint32_t video_baseaddr[2];
	uint8_t opt_id[2] = {0xb0, 0xb1};
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	int i;
	for (i=0; i<2; i++) {
		payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, opt_id[i], 2);
		mv_buf_number[i] = (payload[2] >> 16) & 0x7;
		video_baseaddr[i] = payload[7];
		process_hres[i] = payload[10] & 0xffff;
		process_vres[i] = (payload[10] >> 16) & 0xffff;
	}
	i = dual ? 1 : 0;
	pcfg->frc_setting.memc_hres = process_hres[i];
	pcfg->frc_setting.memc_vres = process_vres[i];
	pcfg->frc_setting.mv_buf_num = mv_buf_number[i];
	pcfg->frc_setting.video_baseaddr = video_baseaddr[i];

	if (process_hres[0] != process_hres[1]) {
		uint32_t add_last_flag = pcfg->add_last_flag;

		pcfg->add_last_flag = pcfg->add_on_last_flag;
		iris_scaler_filter_ratio_get();
		pcfg->add_last_flag = add_last_flag;
		IRIS_LOGI("update scaler filter");
	}
	payload = iris_get_ipopt_payload_data(IRIS_IP_DSC_ENC_2, dual ? 0xf2 : 0xf1, 2);
	pcfg->frc_setting.memc_dsc_bpp = (payload[42] >> 8) & 0xff;
	IRIS_LOGD("memc_dsc_bpp: %x", pcfg->frc_setting.memc_dsc_bpp);
}

void iris_dual_setting_switch(bool dual)
{
	struct iris_ctrl_opt arr_single[] = {
		{0x03, 0xa0, 0x01},	// IRIS_IP_PWIL, pwil: ctrl graphic
		{0x03, 0xb0, 0x01},	// IRIS_IP_PWIL, pwil: video
		{0x03, 0x80, 0x01},	// IRIS_IP_PWIL, pwil: update
		{0x0b, 0xf0, 0x01},	// IRIS_IP_SCALER1D, scaler1d
		{0x0b, 0xa0, 0x01},	// IRIS_IP_SCALER1D, scaler1d: gc
		{0x2f, 0xf0, 0x01},	// IRIS_IP_SCALER1D_2, scaler_pp: init
		{0x2d, 0xf0, 0x01},	// IRIS_IP_PSR_MIF, psr_mif: init
		{0x11, 0xe2, 0x00},	// IRIS_IP_DMA
	};
	struct iris_ctrl_opt arr_dual[] = {
		{0x03, 0xa1, 0x01},	// IRIS_IP_PWIL, pwil: ctrl graphic
		{0x03, 0xb1, 0x01},	// IRIS_IP_PWIL, pwil: video
		{0x03, 0x80, 0x01},	// IRIS_IP_PWIL, pwil: update
		{0x0b, 0xf1, 0x01},	// IRIS_IP_SCALER1D, scaler1d
		{0x0b, 0xa0, 0x01},	// IRIS_IP_SCALER1D, scaler1d: gc
		{0x2f, 0xf1, 0x01},	// IRIS_IP_SCALER1D_2, scaler_pp: init
		{0x2d, 0xf1, 0x01},	// IRIS_IP_PSR_MIF, psr_mif: init
		{0x11, 0xe2, 0x00},	// IRIS_IP_DMA
	};
	struct iris_ctrl_opt *opt_arr = dual ? arr_dual : arr_single;
	int len = sizeof(arr_single)/sizeof(struct iris_ctrl_opt);
	iris_send_assembled_pkt(opt_arr, len);
	IRIS_LOGI("iris_dual_setting_switch, dual: %d, len: %d", dual, len);
	iris_frc_setting_switch(dual);
	iris_cm_setting_switch(dual);
}

void iris_frc_dsc_setting(bool dual)
{
	struct iris_ctrl_opt arr_single[] = {
		{0x25, 0xf1, 0x01},	// IRIS_IP_DSC_DEN_2, dsc_encoder_frc: init
		{0x24, 0xf1, 0x01},	// IRIS_IP_DSC_ENC_2, dsc_encoder_frc: init
		{0x11, 0xe8, 0x00},	// IRIS_IP_DMA
	};
	struct iris_ctrl_opt arr_dual[] = {
		{0x25, 0xf2, 0x01},	// IRIS_IP_DSC_DEN_2
		{0x24, 0xf2, 0x01},	// IRIS_IP_DSC_ENC_2
		{0x11, 0xe8, 0x00},	// IRIS_IP_DMA
	};
	struct iris_ctrl_opt *opt_arr = dual ? arr_dual : arr_single;
	int len = sizeof(arr_single)/sizeof(struct iris_ctrl_opt);
	iris_send_assembled_pkt(opt_arr, len);
	IRIS_LOGI("iris_frc_dsc_setting, dual: %d, len: %d", dual, len);
}
