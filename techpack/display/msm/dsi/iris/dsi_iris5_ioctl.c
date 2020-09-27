// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#include "dsi_iris5_api.h"
#include "dsi_iris5.h"
#include "dsi_iris5_lightup.h"
#include "dsi_iris5_lightup_ocp.h"
#include "dsi_iris5_lp.h"
#include "dsi_iris5_lut.h"
#include "dsi_iris5_pq.h"
#include "dsi_iris5_mode_switch.h"
#include "dsi_iris5_ioctl.h"
#include "dsi_iris5_i3c.h"
#include "dsi_iris5_loop_back.h"
#include "dsi_iris5_frc.h"
#include "dsi_iris5_log.h"

// for game station settings via i2c
uint32_t CM_CNTL[14] = {
	0xf0560000, 0x8020e000,
	0xf0560000, 0x820e000,
	0xf0560008, 0x00000000,
	0xf056000c, 0x6e,
	0xf056000c, 0x5f,
	0xf0560110, 0x00000000,
	0xf0560140, 0x00000100
};

// 0: mipi, 1: i2c
static int adb_type;
static int iris_i2c_ver = 1;

static int mdss_mipi_dsi_command(void __user *values)
{
	struct msmfb_mipi_dsi_cmd cmd;

	char read_response_buf[16] = {0};
	struct dsi_cmd_desc desc = {
		.msg.rx_buf = &read_response_buf,
		.msg.rx_len = 16
	};
	struct dsi_panel_cmd_set cmdset = {
		.count = 1,
		.cmds = &desc
	};
	int ret;
	struct iris_cfg *pcfg = iris_get_cfg();

	struct iris_ocp_dsi_tool_input iris_ocp_input = {0, 0, 0, 0, 0};

	ret = copy_from_user(&cmd, values, sizeof(cmd));
	if (ret) {
		IRIS_LOGE("can not copy from user");
		return -EPERM;
	}

	IRIS_LOGD("#### %s:%d vc=%u d=%02x f=%hu l=%hu", __func__, __LINE__,
			cmd.vc, cmd.dtype, cmd.flags, cmd.length);

	IRIS_LOGD("#### %s:%d %x, %x, %x", __func__, __LINE__,
			cmd.iris_ocp_type, cmd.iris_ocp_addr, cmd.iris_ocp_size);

	if (cmd.length < SZ_4K && cmd.payload) {
		desc.msg.tx_buf = vmalloc(cmd.length);
		if (!desc.msg.tx_buf)
			return -ENOMEM;
		ret = copy_from_user((char *)desc.msg.tx_buf, cmd.payload, cmd.length);
		if (ret) {
			ret = -EPERM;
			goto err;
		}
	} else
		return -EINVAL;

	desc.msg.type = cmd.dtype;
	desc.msg.channel = cmd.vc;
	desc.last_command = (cmd.flags & MSMFB_MIPI_DSI_COMMAND_LAST) > 0;
	desc.msg.flags |= ((cmd.flags & MSMFB_MIPI_DSI_COMMAND_ACK) > 0 ? MIPI_DSI_MSG_REQ_ACK : 0);
	desc.msg.tx_len = cmd.length;
	desc.post_wait_ms = 0;
	desc.msg.ctrl = 0;

	if (cmd.flags & MSMFB_MIPI_DSI_COMMAND_ACK)
		desc.msg.flags = desc.msg.flags | DSI_CTRL_CMD_READ;

	if (cmd.flags & MSMFB_MIPI_DSI_COMMAND_HS)
		cmdset.state = DSI_CMD_SET_STATE_HS;

	mutex_lock(&pcfg->panel->panel_lock);

	if (cmd.flags & MSMFB_MIPI_DSI_COMMAND_TO_PANEL) {
		if (iris_get_abyp_mode(pcfg->panel) == PASS_THROUGH_MODE)
			iris_pt_send_panel_cmd(pcfg->panel, &cmdset);
		else
			iris_dsi_send_cmds(pcfg->panel, cmdset.cmds, cmdset.count, cmdset.state);
	} else if (cmd.flags & MSMFB_MIPI_DSI_COMMAND_T) {
		u32 pktCnt = (cmd.iris_ocp_type >> 8) & 0xFF;

		//only test LUT send command
		if ((cmd.iris_ocp_type & 0xF) == PXLW_DIRECTBUS_WRITE) {
			u8 lut_type = (cmd.iris_ocp_type >> 8) & 0xFF;
			u8 lut_index = (cmd.iris_ocp_type >> 16) & 0xFF;
			u8 lut_parse = (cmd.iris_ocp_type >> 24) & 0xFF;
			u32 lut_pkt_index = cmd.iris_ocp_addr;

			if (lut_parse) // only parse firmware when value is not zero
				iris_parse_lut_cmds(0);
			iris_send_lut(lut_type, lut_index, lut_pkt_index);
		} else { // test ocp wirte
			if (pktCnt > DSI_CMD_CNT)
				pktCnt = DSI_CMD_CNT;

			if (cmd.iris_ocp_size < OCP_MIN_LEN)
				cmd.iris_ocp_size = OCP_MIN_LEN;

			if (cmd.iris_ocp_size > 4096) //CID89803
				cmd.iris_ocp_size = 4096;

			iris_ocp_input.iris_ocp_type = cmd.iris_ocp_type & 0xF;
			iris_ocp_input.iris_ocp_cnt = pktCnt;
			iris_ocp_input.iris_ocp_addr = cmd.iris_ocp_addr;
			iris_ocp_input.iris_ocp_value = cmd.iris_ocp_value;
			iris_ocp_input.iris_ocp_size = cmd.iris_ocp_size;

			if (pktCnt)
				iris_write_test_muti_pkt(pcfg->panel, &iris_ocp_input);
			else
				iris_write_test(pcfg->panel, cmd.iris_ocp_addr, cmd.iris_ocp_type & 0xF, cmd.iris_ocp_size);
			//iris_ocp_bitmask_write(ctrl,cmd.iris_ocp_addr,cmd.iris_ocp_size,cmd.iris_ocp_value);
		}
	} else
		iris_dsi_send_cmds(pcfg->panel, cmdset.cmds, cmdset.count, cmdset.state);

	mutex_unlock(&pcfg->panel->panel_lock);

	if (cmd.flags & MSMFB_MIPI_DSI_COMMAND_ACK) {
		// Both length of cmd.response and read_response_buf are 16.
		memcpy(cmd.response, read_response_buf, sizeof(cmd.response));
	}
	ret = copy_to_user(values, &cmd, sizeof(cmd));
	if (ret)
		ret = -EPERM;
err:
	vfree(desc.msg.tx_buf);
	return ret;
}


int iris_operate_tool(struct msm_iris_operate_value *argp)
{
	int ret = -1;
	uint32_t parent_type = 0;
	uint32_t display_type = 0;
	struct iris_cfg *pcfg = NULL;

	// FIXME: copy_from_user() is failed.
	// ret = copy_from_user(&configure, argp, sizeof(configure));
	// if (ret) {
	//	pr_err("1st %s type = %d, value = %d\n",
	//		__func__, configure.type, configure.count);
	//	return -EPERM;
	// }
	IRIS_LOGI("%s type = %d, value = %d", __func__, argp->type, argp->count);

	display_type = (argp->type >> 16) & 0xff;
	pcfg = iris_get_cfg_by_index(display_type);
	if (pcfg == NULL || pcfg->valid < PARAM_PARSED) {
		IRIS_LOGE("Target display does not exist!");
		return -EPERM;
	}

	parent_type = argp->type & 0xff;
	switch (parent_type) {
	case IRIS_OPRT_TOOL_DSI:
		ret = mdss_mipi_dsi_command(argp->values);
		break;
	default:
		IRIS_LOGE("could not find right opertat type = %d", argp->type);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static bool iris_special_config(u32 type)
{
	switch (type) {
	case IRIS_OSD_ENABLE:
	case IRIS_OSD_AUTOREFRESH:
	case IRIS_OSD_OVERFLOW_ST:
	case IRIS_DBG_KERNEL_LOG_LEVEL:
	case USER_DEMO_WND:
	case IRIS_MEMC_LEVEL:
	case IRIS_WAIT_VSYNC:
	case IRIS_CHIP_VERSION:
	case IRIS_FW_UPDATE:
		return true;
	}

	return false;
}

static bool _iris_is_valid_type(u32 display, u32 type)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(display);

	if (type >= IRIS_CONFIG_TYPE_MAX)
		return false;

	if (!iris_special_config(type)
			&& type != IRIS_ANALOG_BYPASS_MODE
			&& pcfg->abypss_ctrl.abypass_mode == ANALOG_BYPASS_MODE)
		return false;

	if (type != IRIS_DBG_KERNEL_LOG_LEVEL
			&& pcfg->chip_ver == IRIS3_CHIP_VERSION)
		return false;

	return true;
}

static int _iris_configure(u32 display, u32 type, u32 value)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(display);
	struct iris_cfg *pcfg1 = iris_get_cfg_by_index(DSI_PRIMARY);
	struct iris_setting_info *iris_setting = iris_get_setting();
	struct quality_setting *pqlt_cur_setting = &iris_setting->quality_cur;

	// Always use primary display.
	iris_set_cfg_index(DSI_PRIMARY);
	switch (type) {
	case IRIS_PEAKING:
		pqlt_cur_setting->pq_setting.peaking = value & 0xf;
		if (pqlt_cur_setting->pq_setting.peaking > 4)
			goto error;

		iris_peaking_level_set(pqlt_cur_setting->pq_setting.peaking);
		break;
	case IRIS_CM_6AXES:
		pqlt_cur_setting->pq_setting.cm6axis = value & 0x3;
		iris_cm_6axis_level_set(pqlt_cur_setting->pq_setting.cm6axis);
		break;
	case IRIS_CM_FTC_ENABLE:
		pqlt_cur_setting->cmftc = value;
		iris_cm_ftc_enable_set(pqlt_cur_setting->cmftc);
		break;
	case IRIS_S_CURVE:
		pqlt_cur_setting->scurvelevel = value & 0x3;
		if (pqlt_cur_setting->scurvelevel > 2)
			goto error;

		iris_scurve_enable_set(pqlt_cur_setting->scurvelevel);
		break;
	case IRIS_CM_COLOR_TEMP_MODE:
		pqlt_cur_setting->pq_setting.cmcolortempmode = value & 0x3;
		if (pqlt_cur_setting->pq_setting.cmcolortempmode > 2)
			goto error;

		iris_cm_colortemp_mode_set(pqlt_cur_setting->pq_setting.cmcolortempmode);
		break;
	case IRIS_CM_COLOR_GAMUT_PRE:
		iris_cm_color_gamut_pre_set(value & 0x03);
		break;
	case IRIS_CM_COLOR_GAMUT:
		pqlt_cur_setting->pq_setting.cmcolorgamut = value;
		if (pqlt_cur_setting->pq_setting.cmcolorgamut > 6)
			goto error;

		iris_cm_color_gamut_set(pqlt_cur_setting->pq_setting.cmcolorgamut);
		break;
	case IRIS_DBC_LCE_POWER:
		if (value == 0)
			iris_dbclce_power_set(false);
		else if (value == 1)
			iris_dbclce_power_set(true);
		else if (value == 2)
			iris_lce_dynamic_pmu_mask_set(false);
		else if (value == 3)
			iris_lce_dynamic_pmu_mask_set(true);

		break;
	case IRIS_DBC_LCE_DATA_PATH:
		iris_dbclce_datapath_set(value & 0x01);
		break;
	case IRIS_LCE_MODE:
		if (pqlt_cur_setting->pq_setting.lcemode != (value & 0x1)) {
			pqlt_cur_setting->pq_setting.lcemode = value & 0x1;
			iris_lce_mode_set(pqlt_cur_setting->pq_setting.lcemode);
		}
		break;
	case IRIS_LCE_LEVEL:
		if (pqlt_cur_setting->pq_setting.lcelevel != (value & 0x7)) {
			pqlt_cur_setting->pq_setting.lcelevel = value & 0x7;
			if (pqlt_cur_setting->pq_setting.lcelevel > 5)
				goto error;

			iris_lce_level_set(pqlt_cur_setting->pq_setting.lcelevel);
		}
		break;
	case IRIS_GRAPHIC_DET_ENABLE:
		pqlt_cur_setting->pq_setting.graphicdet = value & 0x1;
		iris_lce_graphic_det_set(pqlt_cur_setting->pq_setting.graphicdet);
		break;
	case IRIS_AL_ENABLE:

		if (pqlt_cur_setting->pq_setting.alenable != (value & 0x1)) {
			pqlt_cur_setting->pq_setting.alenable = value & 0x1;

			/*check the case here*/
			if (pqlt_cur_setting->pq_setting.sdr2hdr == SDR2HDR_Bypass)
				iris_lce_al_set(pqlt_cur_setting->pq_setting.alenable);
			else
				iris_ambient_light_lut_set(iris_sdr2hdr_lut2ctl_get());
		}
		break;
	case IRIS_DBC_LEVEL:
		pqlt_cur_setting->pq_setting.dbc = value & 0x3;
		iris_dbc_level_set(pqlt_cur_setting->pq_setting.dbc);
		break;
	case IRIS_BLC_PWM_ENABLE:
		iris_pwm_enable_set(value & 0x1);
		break;
	case IRIS_DEMO_MODE:
		pqlt_cur_setting->pq_setting.demomode = value & 0x3;
		break;
	case IRIS_DYNAMIC_POWER_CTRL:
		if (value & 0x01) {
			IRIS_LOGI(" [%s, %d] open psr_mif osd first address eco.", __func__, __LINE__);
			iris_psf_mif_dyn_addr_set(true);
			iris_dynamic_power_set(value & 0x01);
		} else {
			IRIS_LOGI(" [%s, %d] close psr_mif osd first address eco.", __func__, __LINE__);
			iris_dynamic_power_set(value & 0x01);
			iris_psf_mif_dyn_addr_set(false);
		}
		break;
	case IRIS_DMA_LOAD:
		iris_dma_trigger_load();
		break;
	case IRIS_SDR2HDR:
		iris_set_sdr2hdr_mode((value & 0xf00) >> 8);
		value = value & 0xff;
		if (value/10 == 4) {/*magic code to enable YUV input.*/
			iris_set_yuv_input(true);
			value -= 40;
		} else if (value/10 == 6) {
			iris_set_HDR10_YCoCg(true);
			value -= 60;
		} else if (value == 55) {
			iris_set_yuv_input(true);
			return 0;
		} else if (value == 56) {
			iris_set_yuv_input(false);
			return 0;
		} else {
			iris_set_yuv_input(false);
			iris_set_HDR10_YCoCg(false);
		}

		if (pqlt_cur_setting->pq_setting.sdr2hdr > SDR709_2_2020 || value > SDR709_2_2020)
			goto error;
		if (pqlt_cur_setting->pq_setting.sdr2hdr != value) {
			pqlt_cur_setting->pq_setting.sdr2hdr = value;

			iris_sdr2hdr_level_set(pqlt_cur_setting->pq_setting.sdr2hdr);
		}
		break;
	case IRIS_READING_MODE:
		pqlt_cur_setting->pq_setting.readingmode = value & 0x1;
		iris_reading_mode_set(pqlt_cur_setting->pq_setting.readingmode);
		break;
	case IRIS_COLOR_TEMP_VALUE:
		pqlt_cur_setting->colortempvalue = value;
		if (pqlt_cur_setting->pq_setting.cmcolortempmode == IRIS_COLOR_TEMP_MANUL)
			iris_cm_color_temp_set();
		break;
	case IRIS_CCT_VALUE:
		pqlt_cur_setting->cctvalue = value;
		if (pqlt_cur_setting->pq_setting.cmcolortempmode == IRIS_COLOR_TEMP_AUTO)
			iris_cm_color_temp_set();
		break;
	case IRIS_LUX_VALUE:
		/* move to iris_configure_ex*/
		pqlt_cur_setting->luxvalue = value;
		if (pqlt_cur_setting->pq_setting.alenable == 1) {

			if (pqlt_cur_setting->pq_setting.sdr2hdr == SDR2HDR_Bypass)
				iris_lce_lux_set();
			else
				iris_ambient_light_lut_set(iris_sdr2hdr_lut2ctl_get());
		}
		break;
	case IRIS_HDR_MAXCLL:
		pqlt_cur_setting->maxcll = value;
		break;
	case IRIS_ANALOG_BYPASS_MODE:
		if (value == pcfg->abypss_ctrl.abypass_mode) {
			IRIS_LOGD("Same bypass mode");
			break;
		}
		IRIS_LOGI("%s(), switch Iris mode to: %u", __func__, value);
		if (value == ANALOG_BYPASS_MODE) {
			iris_panel_nits_set(0, true, value);
			iris_quality_setting_off();
			iris_abypass_switch_proc(pcfg->display, value, true, true);
		} else
			iris_abypass_switch_proc(pcfg->display, value, false, true);
		break;
	case IRIS_DBG_LOOP_BACK_MODE:
		pcfg->loop_back_mode = value;
		break;
	case IRIS_HDR_PANEL_NITES_SET:
		if (pqlt_cur_setting->al_bl_ratio != value) {
			pqlt_cur_setting->al_bl_ratio = value;
			iris_panel_nits_set(value, false, pqlt_cur_setting->pq_setting.sdr2hdr);
		}
		break;
	case IRIS_PEAKING_IDLE_CLK_ENABLE:
		iris_peaking_idle_clk_enable(value & 0x01);
		break;
	case IRIS_CM_MAGENTA_GAIN:
		iris_cm_6axis_seperate_gain(IRIS_MAGENTA_GAIN_TYPE, value & 0x3f);
		break;
	case IRIS_CM_RED_GAIN:
		iris_cm_6axis_seperate_gain(IRIS_RED_GAIN_TYPE, value & 0x3f);
		break;
	case IRIS_CM_YELLOW_GAIN:
		iris_cm_6axis_seperate_gain(IRIS_YELLOW_GAIN_TYPE, value & 0x3f);
		break;
	case IRIS_CM_GREEN_GAIN:
		iris_cm_6axis_seperate_gain(IRIS_GREEN_GAIN_TYPE, value & 0x3f);
		break;
	case IRIS_CM_BLUE_GAIN:
		iris_cm_6axis_seperate_gain(IRIS_BLUE_GAIN_TYPE, value & 0x3f);
		break;
	case IRIS_CM_CYAN_GAIN:
		iris_cm_6axis_seperate_gain(IRIS_CYAN_GAIN_TYPE, value & 0x3f);
		break;
	case IRIS_DBC_LED_GAIN:
		iris_dbc_led0d_gain_set(value & 0x3f);
		break;
	case IRIS_SCALER_FILTER_LEVEL:
		iris_scaler_filter_update(SCALER_INPUT, value & 0x7);
		break;
	case IRIS_SCALER_PP_FILTER_LEVEL:
		iris_scaler_filter_update(SCALER_PP, value & 0x1);
		break;
	case IRIS_HDR_PREPARE:
		if ((value == 0) || ((value == 1) && !iris_get_debug_cap()) || (value == 2))
			iris_hdr_csc_prepare();
		else if (value == 3)
			iris_set_skip_dma(true);
		break;
	case IRIS_HDR_COMPLETE:
		if ((value == 3) || (value == 5))
			iris_set_skip_dma(false);
		if ((value == 0) || ((value == 1) && !iris_get_debug_cap()))
			iris_hdr_csc_complete(value);
		else if (value >= 2)
			iris_hdr_csc_complete(value);

		if (value != 2 && value != 4) {
			if (pqlt_cur_setting->pq_setting.sdr2hdr == SDR2HDR_Bypass)
				iris_panel_nits_set(0, true, value);
			else
				iris_panel_nits_set(PANEL_BL_MAX_RATIO, false, value);
		}
		break;
	case IRIS_DEBUG_CAP:
		iris_set_debug_cap(value & 0x01);
		break;
	case IRIS_FW_UPDATE:
		// Need do multi-thread protection.
		if (value <= 2) {//CID100675
			/* before parsing firmware, free ip & opt buffer which alloc for LUT,
			 * if loading firmware failed before, need realloc seq space after
			 * updating firmware
			 */
			u8 firmware_status = iris_get_fw_status();

			iris_free_ipopt_buf(IRIS_LUT_PIP_IDX);
			iris_parse_lut_cmds(value);
			if (firmware_status == FIRMWARE_LOAD_FAIL) {
				iris_free_seq_space();
				iris_alloc_seq_space();
			}
			if (iris_get_fw_status() == FIRMWARE_LOAD_SUCCESS) {
				if (pcfg->abypss_ctrl.abypass_mode == PASS_THROUGH_MODE) {
					iris_cm_color_gamut_set(pqlt_cur_setting->pq_setting.cmcolorgamut);
					iris_scaler_gamma_enable(false, 1);
				}
				iris_update_fw_status(FIRMWARE_IN_USING);
			}
		}
		break;
	case IRIS_DBG_KERNEL_LOG_LEVEL:
		iris_set_loglevel(value);
		break;
	case IRIS_MODE_SET:
		iris_mode_switch_proc(value);
		break;
	case IRIS_VIDEO_FRAME_RATE_SET:
		iris_set_video_frame_rate_ms(value);
		break;
	case IRIS_OUT_FRAME_RATE_SET:
		iris_set_out_frame_rate(value);
		break;
	case IRIS_OSD_ENABLE:
		if (pcfg1->iris_initialized == false) {
			IRIS_LOGI("iris not initialized");
			break;
		}
		if ((value == 1) || (value == 0)) {
			IRIS_LOGI("call iris_switch_osd_blending(%d)", value);
			iris_switch_osd_blending(value);
		} else if (value == 0x10) {
			IRIS_LOGI("power on iris mipi2 rx");
			iris_set_second_channel_power(true);
		} else if (value == 0x20) {
			IRIS_LOGI("reset pwil_v6, power off bulksram");
			iris_second_channel_post(value);
		} else if (value == 0x21) {
			IRIS_LOGI("power off iris mipi2 rx");
			iris_set_second_channel_power(false);
		} else if (value == 0x40) {
			iris_dom_set(0);
		} else if (value == 0x41) {
			iris_dom_set(2);
		} else if (value == 0x80 || value == 0x81) {
			pcfg1->dual_setting = value == 0x81;
			if (pcfg->dual_test & 0x10)
				pcfg1->dual_setting = true;

			if (!(pcfg->dual_test & 0x4))
				iris_dual_setting_switch(pcfg1->dual_setting);
		} else
			IRIS_LOGE("IRIS_OSD_ENABLE, invalid val=%d", value);
		break;
	case IRIS_OSD_AUTOREFRESH:
		// Always use secondary display.
		iris_osd_autorefresh(value);
		break;
	case IRIS_FRC_LOW_LATENCY:
		iris_frc_low_latency(value);
		break;
	case IRIS_PANEL_TE:
		iris_set_panel_te(value);
		break;
	case IRIS_AP_TE:
		iris_set_ap_te(value);
		break;
	case IRIS_N2M_ENABLE:
		iris_set_n2m_enable(value);
		break;
	case IRIS_WAIT_VSYNC:
		return iris_wait_vsync();
	case IRIS_MEMC_LEVEL:
		if (value <= 3) {
			pcfg1->frc_setting.memc_level = value;
			pcfg1->frc_setting.short_video = 0;
		} else if (value >= 4 && value <= 7) {
			pcfg1->frc_setting.memc_level = value - 4;
			pcfg1->frc_setting.short_video = 1;
		} else if (value >= 8 && value <= 11) {
			pcfg1->frc_setting.memc_level = value - 8;
			pcfg1->frc_setting.short_video = 2;
		}
		break;
	case IRIS_MEMC_OSD:
		pcfg1->frc_setting.memc_osd = value;
		break;
	case USER_DEMO_WND:
		if (value > 5)
			goto error;
		iris_fi_demo_window(value);
		break;
	case IRIS_CHIP_VERSION:
		pcfg1->chip_value[0] = value;
		break;
	default:
		goto error;
	}

	return 0;

error:
	return -EINVAL;

}

int iris_configure(u32 display, u32 type, u32 value)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	int rc = 0;

	IRIS_LOGI("%s(), display: %u, type: 0x%04x(%u), value: %#x(%u), current Iris mode: %d",
			__func__,
			display, type, type, value, value, pcfg->abypss_ctrl.abypass_mode);
	if (!_iris_is_valid_type(display, type))
		return -EPERM;

	switch (type) {
	case IRIS_DEMO_MODE:
	case IRIS_HDR_MAXCLL:
	case IRIS_DEBUG_CAP:
	case IRIS_VIDEO_FRAME_RATE_SET:
	case IRIS_OUT_FRAME_RATE_SET:
	case IRIS_OSD_AUTOREFRESH:
	case IRIS_DBG_KERNEL_LOG_LEVEL:
	case IRIS_FRC_LOW_LATENCY:
	case IRIS_N2M_ENABLE:
	case IRIS_WAIT_VSYNC:
	case IRIS_MEMC_LEVEL:
	case IRIS_MEMC_OSD:
	case USER_DEMO_WND:
	case IRIS_CHIP_VERSION:
		/* don't lock panel_lock */
		return _iris_configure(display, type, value);
	}

	mutex_lock(&pcfg->panel->panel_lock);
	rc = _iris_configure(display, type, value);
	mutex_unlock(&pcfg->panel->panel_lock);
	return rc;
}

int iris_configure_t(uint32_t display, u32 type, void __user *argp)
{
	int ret = -1;
	uint32_t value = 0;

	ret = copy_from_user(&value, argp, sizeof(uint32_t));
	if (ret) {
		IRIS_LOGE("can not copy from user");
		return -EPERM;
	}

	ret = iris_configure(display, type, value);
	return ret;
}

static int _iris_configure_ex(u32 display, u32 type, u32 count, u32 *values)
{
	int ret = -1;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(display);
	struct iris_setting_info *iris_setting = iris_get_setting();
	struct quality_setting *pqlt_cur_setting = &iris_setting->quality_cur;
	struct msmfb_iris_ambient_info iris_ambient;
	struct msmfb_iris_maxcll_info iris_maxcll;
	struct msmfb_iris_ambient_info *iris_ambient_lut = NULL;
	struct msmfb_iris_maxcll_info *iris_maxcll_lut = NULL;
	uint8_t i = 0;
	u32 TempValue = 0;
	bool is_phone;
	u32 LutPos = 0;

	if (!_iris_is_valid_type(display, type))
		return -EPERM;

	// Always use primary display.
	iris_set_cfg_index(DSI_PRIMARY);
	switch (type) {
	case IRIS_LUX_VALUE:
		iris_ambient = *(struct msmfb_iris_ambient_info *)(values);
		iris_ambient_lut = iris_get_ambient_lut();
		iris_ambient_lut->ambient_lux = iris_ambient.ambient_lux;
		pqlt_cur_setting->luxvalue = iris_ambient_lut->ambient_lux;

		if (iris_ambient.lut_lut2_payload != NULL) {
			ret = copy_from_user(iris_ambient_lut->lut_lut2_payload, iris_ambient.lut_lut2_payload, sizeof(uint32_t)*LUT_LEN);
			if (ret) {
				IRIS_LOGE("can not copy from user sdr2hdr");
				goto error1;
			}
			LutPos = iris_sdr2hdr_lut2ctl_get();
			if (LutPos == 15) {
				LutPos = 0;
				iris_update_ambient_lut(AMBINET_SDR2HDR_LUT, 1);
			} else if (LutPos == 1)
				LutPos = 0;
			else if (!LutPos)
				LutPos = 1;
			iris_update_ambient_lut(AMBINET_SDR2HDR_LUT, LutPos);
		} else if (iris_sdr2hdr_lut2ctl_get() == 0xFFE00000)
			LutPos = 0xFFE00000;
		else
			LutPos = 0;

		if (pqlt_cur_setting->pq_setting.alenable == 1) {
			if (pqlt_cur_setting->pq_setting.sdr2hdr == SDR2HDR_Bypass) {
				iris_sdr2hdr_lut2ctl_set(LutPos);
				iris_lce_lux_set();
			} else
				iris_ambient_light_lut_set(LutPos);
		} else
			iris_sdr2hdr_lut2ctl_set(LutPos);
		break;
	case IRIS_HDR_MAXCLL:
		iris_maxcll_lut = iris_get_maxcll_info();
		iris_maxcll = *(struct msmfb_iris_maxcll_info *)(values);
		iris_maxcll_lut->mMAXCLL = iris_maxcll.mMAXCLL;

		if (iris_maxcll.lut_luty_payload != NULL) {
			ret = copy_from_user(iris_maxcll_lut->lut_luty_payload, iris_maxcll.lut_luty_payload, sizeof(uint32_t)*LUT_LEN);
			if (ret) {
				IRIS_LOGE("can not copy lut y from user sdr2hdr");
				goto error1;
			}
		}
		if (iris_maxcll.lut_lutuv_payload != NULL) {
			ret = copy_from_user(iris_maxcll_lut->lut_lutuv_payload, iris_maxcll.lut_lutuv_payload, sizeof(uint32_t)*LUT_LEN);
			if (ret) {
				IRIS_LOGE("can not copy lut uv from user sdr2hdr");
				goto error1;
			}
		}
		LutPos = iris_sdr2hdr_lutyctl_get();
		if (!LutPos)
			LutPos = 1;
		else if ((LutPos == 15) || (LutPos == 1))
			LutPos = 0;
		iris_update_maxcll_lut(AMBINET_HDR_GAIN, LutPos);
		iris_maxcll_lut_set(LutPos);
		break;
	case IRIS_CCF1_UPDATE:
		/* Nothing to do for Iirs5*/
		break;
	case IRIS_CCF2_UPDATE:
		/* Nothing to do for Iirs5*/
		break;
	case IRIS_HUE_SAT_ADJ:
		IRIS_LOGD("cm csc value: csc0 = 0x%x, csc1 = 0x%x, csc2 = 0x%x, csc3 = 0x%x, csc4 = 0x%x", values[0], values[1], values[2], values[3], values[4]);
		IRIS_LOGD("game mode %d", values[5]);
		if (values[5] == 1) {
			for (i = 0; i <= 4; i++) {
				if (pcfg->iris_i2c_write) {
					if (pcfg->iris_i2c_write(CM_CNTL[10] + i*4, values[i]) < 0)
						IRIS_LOGE("i2c set reg fails, reg=0x%x, val=0x%x", CM_CNTL[10] + i*4, values[i]);
				} else {
					IRIS_LOGE("Game Station is not connected");
				}
			}
		} else {
			iris_cm_csc_level_set(IRIS_IP_CM, &values[0]);
		}
		break;
	case IRIS_CONTRAST_DIMMING:
		IRIS_LOGI("dpp csc value: csc0 = 0x%x, csc1 = 0x%x, csc2 = 0x%x, csc3 = 0x%x, csc4 = 0x%x",
				values[0], values[1], values[2], values[3], values[4]);
		iris_cm_csc_level_set(IRIS_IP_DPP, &values[0]);
		break;
	case IRIS_COLOR_TEMP_VALUE:
		is_phone = (count > 1) ? (values[1] == 0) : true;
		pqlt_cur_setting->colortempvalue = values[0];

		if (is_phone) {
			if (count > 3) {
				pqlt_cur_setting->min_colortempvalue = values[2];
				pqlt_cur_setting->max_colortempvalue = values[3];
			} else {
				pqlt_cur_setting->min_colortempvalue = 0;
				pqlt_cur_setting->max_colortempvalue = 0;
			}
			if (pqlt_cur_setting->pq_setting.cmcolortempmode == IRIS_COLOR_TEMP_MANUL)
				iris_cm_color_temp_set();
		} else {
			TempValue = iris_cm_ratio_set_for_iic();
			IRIS_LOGD("set reg=0x%x, val=0x%x", CM_CNTL[4], TempValue);
			if (pcfg->iris_i2c_write) {
				if (pcfg->iris_i2c_write(CM_CNTL[4], TempValue) < 0)
					IRIS_LOGE("i2c set reg fails, reg=0x%x, val=0x%x", CM_CNTL[4], TempValue);
			} else {
				IRIS_LOGE("Game Station is not connected");
			}
		}
		break;
	case IRIS_DBG_TARGET_REGADDR_VALUE_SET:
		if (adb_type == 0) {
			iris_ocp_write_val(values[0], values[1]);
		} else if (adb_type == 1) {
			if (iris_i2c_ver == 0) {
				if (pcfg->iris_i2c_write) {
					if (pcfg->iris_i2c_write(values[0], values[1]) < 0)
						IRIS_LOGE("i2c set reg fails, reg=0x%x, val=0x%x", values[0], values[1]);
				} else {
					IRIS_LOGE("Game Station is not connected");
				}
			} else {
				IRIS_LOGD("addr = %x, value = %x\n", values[0], values[1]);
				iris_i2c_ocp_single_write(values, 1);
			}
		}
		break;
	case IRIS_DBG_TARGET_REGADDR_VALUE_SET2:
		iris_ocp_write_vals(values[0], values[1], count-2, values+2);
		break;
	case IRIS_CM_6AXES:
		// phone
		pqlt_cur_setting->pq_setting.cm6axis = values[0] & 0x3;
		iris_cm_6axis_level_set(pqlt_cur_setting->pq_setting.cm6axis);

		// game station
		if (pcfg->iris_i2c_write) {
			if (pcfg->iris_i2c_write(CM_CNTL[0], values[1] ? CM_CNTL[3] : CM_CNTL[1]) < 0)
				IRIS_LOGE("i2c set reg fails, reg=0x%x", CM_CNTL[0]);
			else if (pcfg->iris_i2c_write(CM_CNTL[12], CM_CNTL[13]) < 0)
				IRIS_LOGE("i2c set reg fails, reg=0x%x", CM_CNTL[12]);
		} else {
			IRIS_LOGE("Game Station is not connected");
		}
		break;
	case IRIS_CM_COLOR_TEMP_MODE:
		// phone
		pqlt_cur_setting->pq_setting.cmcolortempmode = values[0] & 0x3;
		if (pqlt_cur_setting->pq_setting.cmcolortempmode > 2)
			goto error;

		iris_cm_colortemp_mode_set(pqlt_cur_setting->pq_setting.cmcolortempmode);

		// game station
		if (pcfg->iris_i2c_write) {
			if (pcfg->iris_i2c_write(CM_CNTL[6], values[1] ? CM_CNTL[9] : CM_CNTL[7]) < 0)
				IRIS_LOGE("i2c set reg fails, reg=0x%x", CM_CNTL[6]);
			else if (pcfg->iris_i2c_write(CM_CNTL[12], CM_CNTL[13]) < 0)
				IRIS_LOGE("i2c set reg fails, reg=0x%x", CM_CNTL[12]);
		} else {
			IRIS_LOGE("Game Station is not connected");
		}
		break;
	case IRIS_CSC_MATRIX:
		if (count > 9) {
			if (values[0] == 1)
				iris_cm_csc_level_set(IRIS_IP_CM, &values[2]);
			else if (values[0] == 2)
				iris_cm_csc_level_set(IRIS_IP_DPP, &values[2]);
			else
				return -EPERM;
		} else
			return -EPERM;
		break;
	case IRIS_DBG_SEND_PACKAGE:
		ret = iris_send_ipopt_cmds(values[0], values[1]);
		IRIS_LOGD("iris config sends package: ip: %#x, opt: %#x, send: %d.",
				values[0], values[1], ret);
		break;
	case IRIS_MEMC_OSD_PROTECT:
		IRIS_LOGD("OSD protect setting: Top_left_pos = 0x%x, bot_right_pos = 0x%x, OSDwinID = 0x%x, OSDwinIDEn = 0x%x, DynCompensate = 0x%x",
				values[0], values[1], values[2], values[3], values[4]);
		ret = iris_fi_osd_protect_window(values[0], values[1], values[2], values[3], values[4]);
		if (ret)
			goto error;
		break;
	case IRIS_BRIGHTNESS_CHIP:
		iris_brightness_level_set(&values[0]);
		break;
	case IRIS_LCE_DEMO_WINDOW:
		iris_lce_demo_window_set(values[0], values[1], values[2]);
		break;
	case IRIS_WAIT_VSYNC:
		if (count > 2)
			iris_set_pending_panel_brightness(values[0], values[1], values[2]);
		break;
	default:
		goto error;
	}

	return 0;

error:
	return -EINVAL;
error1:
	return -EPERM;
}

int iris_configure_ex(u32 display, u32 type, u32 count, u32 *values)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(display);
	struct iris_cfg *pcfg1 = iris_get_cfg_by_index(DSI_PRIMARY);
	int rc = 0;

	IRIS_LOGI("%s(), type: 0x%04x(%d), value: %#x(%d), count: %d, abyp mode: %d",
			__func__,
			type, type, values[0], values[0], count, pcfg->abypss_ctrl.abypass_mode);
	if (!_iris_is_valid_type(display, type))
		return -EPERM;

	switch (type) {
	case IRIS_WAIT_VSYNC:
		/* don't lock panel_lock */
		return _iris_configure_ex(display, type, count, values);
	}

	mutex_lock(&pcfg1->panel->panel_lock);
	rc = _iris_configure_ex(display, type, count, values);
	mutex_unlock(&pcfg1->panel->panel_lock);
	return rc;
}

static int iris_configure_ex_t(uint32_t display, uint32_t type,
		uint32_t count, void __user *values)
{
	int ret = -1;
	uint32_t *val = NULL;

	val = vmalloc(sizeof(uint32_t) * count);
	if (!val) {
		IRIS_LOGE("can not vmalloc space");
		return -ENOSPC;
	}
	ret = copy_from_user(val, values, sizeof(uint32_t) * count);
	if (ret) {
		IRIS_LOGE("can not copy from user");
		vfree(val);
		return -EPERM;
	}
	ret = iris_configure_ex(display, type, count, val);
	vfree(val);
	return ret;
}

int iris_configure_get(u32 display, u32 type, u32 count, u32 *values)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(display);
	struct iris_cfg *pcfg1 = iris_get_cfg_by_index(DSI_PRIMARY);
	struct iris_cfg *pcfg2 = iris_get_cfg_by_index(DSI_SECONDARY);
	struct iris_setting_info *iris_setting = iris_get_setting();
	struct quality_setting *pqlt_cur_setting = &iris_setting->quality_cur;
	u32 reg_addr, reg_val;

	if (type >= IRIS_CONFIG_TYPE_MAX)
		return -EINVAL;

	switch (type) {
	case IRIS_PEAKING:
		*values = pqlt_cur_setting->pq_setting.peaking;
		break;
	case IRIS_CM_6AXES:
		*values = pqlt_cur_setting->pq_setting.cm6axis;
		break;
	case IRIS_CM_FTC_ENABLE:
		*values = pqlt_cur_setting->cmftc;
		break;
	case IRIS_S_CURVE:
		*values = pqlt_cur_setting->scurvelevel;
		break;
	case IRIS_CM_COLOR_TEMP_MODE:
		*values = pqlt_cur_setting->pq_setting.cmcolortempmode;
		break;
	case IRIS_CM_COLOR_GAMUT:
		*values = pqlt_cur_setting->pq_setting.cmcolorgamut;
		break;
	case IRIS_LCE_MODE:
		*values = pqlt_cur_setting->pq_setting.lcemode;
		break;
	case IRIS_LCE_LEVEL:
		*values = pqlt_cur_setting->pq_setting.lcelevel;
		break;
	case IRIS_GRAPHIC_DET_ENABLE:
		*values = pqlt_cur_setting->pq_setting.graphicdet;
		break;
	case IRIS_AL_ENABLE:
		*values = pqlt_cur_setting->pq_setting.alenable;
		break;
	case IRIS_DBC_LEVEL:
		*values = pqlt_cur_setting->pq_setting.dbc;
		break;
	case IRIS_DEMO_MODE:
		*values = pqlt_cur_setting->pq_setting.demomode;
		break;
	case IRIS_SDR2HDR:
		*values = pqlt_cur_setting->pq_setting.sdr2hdr;
		break;
	case IRIS_LUX_VALUE:
		*values = pqlt_cur_setting->luxvalue;
		break;
	case IRIS_READING_MODE:
		*values = pqlt_cur_setting->pq_setting.readingmode;
		break;
	case IRIS_DYNAMIC_POWER_CTRL:
		*values = iris_dynamic_power_get();
		break;
	case IRIS_HDR_MAXCLL:
		*values = pqlt_cur_setting->maxcll;
		break;
	case IRIS_ANALOG_BYPASS_MODE:
		*values = pcfg->abypss_ctrl.abypass_mode;
		break;
	case IRIS_DBG_LOOP_BACK_MODE:
		*values = pcfg->loop_back_mode;
		break;
	case IRIS_DBG_LOOP_BACK_MODE_RES:
		*values = pcfg->loop_back_mode_res;
		break;
	case IRIS_CM_COLOR_GAMUT_PRE:
		*values = pqlt_cur_setting->source_switch;
		break;
	case IRIS_CCT_VALUE:
		*values = pqlt_cur_setting->cctvalue;
		break;
	case IRIS_COLOR_TEMP_VALUE:
		*values = pqlt_cur_setting->colortempvalue;
		break;
	case IRIS_CHIP_VERSION:
		if (*values == 1)
			*values = pcfg1->chip_value[1];
		else {
			*values = 0;
			if (iris_is_chip_supported())
				*values |= (1 << IRIS5_VER);
			if (iris_is_softiris_supported())
				*values |= (1 << IRISSOFT_VER);
			if (iris_is_dual_supported())
				*values |= (1 << IRIS5DUAL_VER);
			if (*values == 0)
				return -EFAULT;
		}
		break;
	case IRIS_PANEL_TYPE:
		*values = pcfg->panel_type;
		break;
	case IRIS_PANEL_NITS:
		*values = pcfg->panel_nits;
		break;
	case IRIS_MCF_DATA:
		mutex_lock(&pcfg1->panel->panel_lock);
		/* get MCF from panel */
		mutex_unlock(&pcfg1->panel->panel_lock);
		break;
	case IRIS_DBG_TARGET_REGADDR_VALUE_GET:
		IRIS_LOGI("%s:%d, pcfg->abypss_ctrl.abypass_mode = %d",
				__func__, __LINE__,
				pcfg->abypss_ctrl.abypass_mode);
		if ((pcfg->abypss_ctrl.abypass_mode == ANALOG_BYPASS_MODE) && (adb_type == 0))
			return -ENOTCONN;

		if (adb_type == 0) {
			mutex_lock(&pcfg1->panel->panel_lock);
			iris_set_cfg_index(DSI_PRIMARY);
			*values = iris_ocp_read(*values, DSI_CMD_SET_STATE_HS);
			mutex_unlock(&pcfg1->panel->panel_lock);
		} else if (adb_type == 1) {
			reg_addr = *values;
			if (iris_i2c_ver == 0) {
				if (pcfg->iris_i2c_read) {
					if (pcfg->iris_i2c_read(reg_addr, &reg_val) < 0)
						IRIS_LOGE("i2c read reg fails, reg=0x%x", reg_addr);
					else
						*values = reg_val;
				} else {
					IRIS_LOGE("Game Station is not connected");
				}
			} else {
				iris_i2c_ocp_read(values, 1, 0);
				IRIS_LOGD("addr = %x, value = %x\n", reg_addr, *values);
			}
		}
		break;
	case IRIS_DBG_KERNEL_LOG_LEVEL:
		*values = iris_get_loglevel();
		break;
	case IRIS_VIDEO_FRAME_RATE_SET:
		*values = (u32)pcfg->frc_setting.in_fps * 1000;
		break;
	case IRIS_OUT_FRAME_RATE_SET:
		*values = pcfg->frc_setting.out_fps;
		break;
	case IRIS_OSD_ENABLE:
		*values = pcfg->osd_on ? 1 : 0;
		break;
	case IRIS_OSD_AUTOREFRESH:
		// Always use secondary display.
		pcfg = iris_get_cfg_by_index(DSI_SECONDARY);
		*values = pcfg->iris_osd_autorefresh ? 1 : 0;
		break;
	case IRIS_OSD_OVERFLOW_ST:
		// Always use secondary display.
		*values = iris_get_osd_overflow_st();
		break;
	case IRIS_MIPI2RX_PWRST:
		pcfg = iris_get_cfg_by_index(DSI_SECONDARY);
		*values = pcfg->mipi_pwr_st;
		break;
	case IRIS_DUAL2SINGLE_ST:
		mutex_lock(&pcfg1->panel->panel_lock);
		if (pcfg2->mipi_pwr_st == false) {
			IRIS_LOGI("mipi2 rx has been power off");
			*values = 1;
		} else
			*values = iris_get_dual2single_status();
		mutex_unlock(&pcfg1->panel->panel_lock);
		break;
	case IRIS_WORK_MODE:
		*values = ((int)pcfg->pwil_mode<<16) | ((int)pcfg->tx_mode<<8) | ((int)pcfg->rx_mode);
		break;
	case IRIS_PANEL_TE:
		*values = pcfg1->panel_te;
		break;
	case IRIS_AP_TE:
		*values = pcfg1->ap_te;
		IRIS_LOGI("get IRIS_AP_TE: %d", pcfg1->ap_te);
		break;
	case IRIS_MODE_SET:
		mutex_lock(&pcfg1->panel->panel_lock);
		iris_set_cfg_index(DSI_PRIMARY);
		*values = iris_mode_switch_update();
		mutex_unlock(&pcfg1->panel->panel_lock);
		break;
	case IRIS_N2M_ENABLE:
		*values = pcfg1->n2m_enable;
		break;
	case IRIS_MEMC_LEVEL:
		*values = pcfg1->frc_setting.memc_level;
		break;
	case IRIS_MEMC_OSD:
		*values = pcfg1->frc_setting.memc_osd;
		break;
	case IRIS_PARAM_VALID:
		*values = pcfg1->valid;
		break;
	default:
		return -EFAULT;
	}

	IRIS_LOGI("%s(), type: 0x%04x(%d), value: %d",
			__func__,
			type, type, *values);
	return 0;
}

int iris_configure_get_t(uint32_t display, uint32_t type,
		uint32_t count, void __user *values)
{
	int ret = -1;
	uint32_t *val = NULL;

	val = vmalloc(count * sizeof(uint32_t));
	if (val == NULL) {
		IRIS_LOGE("could not vmalloc space for func = %s", __func__);
		return -ENOSPC;
	}
	ret = copy_from_user(val, values, sizeof(uint32_t) * count);
	if (ret) {
		IRIS_LOGE("can not copy from user");
		vfree(val);
		return -EPERM;
	}
	ret = iris_configure_get(display, type, count, val);
	if (ret) {
		IRIS_LOGE("get error");
		vfree(val);
		return ret;
	}
	ret = copy_to_user(values, val, sizeof(uint32_t) * count);
	if (ret) {
		IRIS_LOGE("copy to user error");
		vfree(val);
		return -EPERM;
	}
	vfree(val);
	return ret;
}

int iris_operate_conf(struct msm_iris_operate_value *argp)
{
	int ret = -1;
	uint32_t parent_type = 0;
	uint32_t child_type = 0;
	uint32_t display_type = 0;
	struct iris_cfg *pcfg = NULL;

	IRIS_LOGD("%s type=0x%04x", __func__, argp->type);

	parent_type = argp->type & 0xff;
	child_type = (argp->type >> 8) & 0xff;
	display_type = (argp->type >> 16) & 0xff;
	pcfg = iris_get_cfg_by_index(display_type);
	if (pcfg == NULL || pcfg->valid < PARAM_PARSED) {
		if (child_type == IRIS_WAIT_VSYNC || child_type == IRIS_CHIP_VERSION)
			IRIS_LOGV("Allow type 0x%04x(%u) for Soft Iris", child_type, child_type);
		else {
			IRIS_LOGE("Target display does not exist!");
			return -EPERM;
		}
	}

	switch (parent_type) {
	case IRIS_OPRT_CONFIGURE:
		ret = iris_configure_t(display_type, child_type, argp->values);
		break;
	case IRIS_OPRT_CONFIGURE_NEW:
		ret = iris_configure_ex_t(display_type, child_type, argp->count, argp->values);
		break;
	case IRIS_OPRT_CONFIGURE_NEW_GET:
		ret = iris_configure_get_t(display_type, child_type, argp->count, argp->values);
		break;
	default:
		IRIS_LOGE("could not find right operate type = %d", argp->type);
		break;
	}

	return ret;
}

static ssize_t iris_adb_type_read(struct file *file, char __user *buff,
		size_t count, loff_t *ppos)
{
	int tot = 0;
	char bp[512];

	if (*ppos)
		return 0;

	tot = scnprintf(bp, sizeof(bp), "%d\n", adb_type);
	if (copy_to_user(buff, bp, tot))
		return -EFAULT;
	*ppos += tot;

	return tot;
}

static ssize_t iris_adb_type_write(struct file *file,
		const char __user *buff, size_t count, loff_t *ppos)
{
	unsigned long val;

	if (kstrtoul_from_user(buff, count, 0, &val))
		return -EFAULT;

	adb_type = val;

	return count;
}

static const struct file_operations iris_adb_type_write_fops = {
	.open = simple_open,
	.write = iris_adb_type_write,
	.read = iris_adb_type_read,
};

int iris_dbgfs_adb_type_init(struct dsi_display *display)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	if (debugfs_create_file("adb_type", 0644, pcfg->dbg_root, display,
				&iris_adb_type_write_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
				__FILE__, __LINE__);
		return -EFAULT;
	}

	return 0;
}

/* Iris log level definition, for 'dsi_iris5_log.h' */
static int iris_log_level = 2;

void iris_set_loglevel(int level)
{
	iris_log_level = level;
}

inline int iris_get_loglevel(void)
{
	return iris_log_level;
}
