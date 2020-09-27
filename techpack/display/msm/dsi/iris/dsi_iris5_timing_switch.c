// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#include <dsi_drm.h>
#include "dsi_panel.h"
#include "dsi_iris5.h"
#include "dsi_iris5_api.h"
#include "dsi_iris5_lightup.h"
#include "dsi_iris5_lightup_ocp.h"
#include "dsi_iris5_mode_switch.h"
#include "dsi_iris5_lp.h"
#include "dsi_iris5_log.h"


enum {
	SWITCH_ABYP_TO_ABYP = 0,
	SWITCH_ABYP_TO_PT,
	SWITCH_PT_TO_ABYP,
	SWITCH_PT_TO_PT,
	SWITCH_NONE,
};

#define SWITCH_CASE(case)[SWITCH_##case] = #case
static const char * const switch_case_name[] = {
	SWITCH_CASE(ABYP_TO_ABYP),
	SWITCH_CASE(ABYP_TO_PT),
	SWITCH_CASE(PT_TO_ABYP),
	SWITCH_CASE(PT_TO_PT),
	SWITCH_CASE(NONE),
};
#undef SWITCH_CASE

void iris_init_timing_switch(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	pcfg->switch_case = SWITCH_ABYP_TO_ABYP;
}

static int32_t _iris_parse_timing_switch_seq(
		struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t rc = 0;
	const uint8_t *key = "pxlw,iris-timing-switch-sequence";

	rc = iris_parse_optional_seq(np, key, &pcfg->timing_switch_seq);
	if (rc != 0) {
		IRIS_LOGE("%s(), failed to parse %s seq", __func__, key);
		return rc;
	}

	key = "pxlw,iris-timing-switch-sequence-1";
	rc = iris_parse_optional_seq(np, key, &pcfg->timing_switch_seq_1);
	if (rc != 0) {
		IRIS_LOGE("%s(), failed to parse %s seq", __func__, key);
		return rc;
	}

	return rc;
}

int32_t iris_parse_timing_switch_info(
		struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t rc = 0;

	rc = iris_parse_dtsi_cmd(np, IRIS_DTSI1_PIP_IDX);
	if (rc)
		IRIS_LOGI("%s, [optional] have not cmds list 1", __func__);

	rc = _iris_parse_timing_switch_seq(np, pcfg);
	if (rc)
		IRIS_LOGI("%s, [optional] have not timing switch sequence", __func__);

	return 0;
}

void iris_send_timing_switch_pkt(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_ctrl_seq *pseq = &pcfg->timing_switch_seq;
	struct iris_ctrl_opt *arr = NULL;
	int refresh_rate = pcfg->panel->cur_mode->timing.refresh_rate;
	int v_active = pcfg->panel->cur_mode->timing.v_active;

	if ((refresh_rate == HIGH_FREQ) && (v_active == FHD_H))
		pseq = &pcfg->timing_switch_seq_1;

	IRIS_LOGI("%s, cmd list index: %d, v res: %d, fps: %d",
			__func__, pcfg->cmd_list_index, v_active, refresh_rate);

	if (pseq == NULL) {
		IRIS_LOGE("%s(), seq is NULL", __func__);
		return;
	}
	arr = pseq->ctrl_opt;

	iris_send_assembled_pkt(arr, pseq->cnt);
	udelay(100);
}

static uint32_t _iris_switch_case(const u32 refresh_rate, const u32 frame_height)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	bool cur_pt_mode = (pcfg->abypss_ctrl.abypass_mode == PASS_THROUGH_MODE);
	u32 switch_mode = SWITCH_ABYP_TO_ABYP;

	IRIS_LOGD("%s(), refersh rate %u, frame height %u, iris current mode '%s'",
			__func__, refresh_rate, frame_height, cur_pt_mode ? "PT" : "ABYP");

	if (frame_height == QHD_H) {
		pcfg->cmd_list_index = IRIS_DTSI0_PIP_IDX;
		if (cur_pt_mode)
			switch_mode = SWITCH_PT_TO_ABYP;
	}

	if (frame_height == FHD_H) {
		pcfg->cmd_list_index = IRIS_DTSI1_PIP_IDX;
		if (cur_pt_mode) {
			if (pcfg->cur_v_active == QHD_H)
				switch_mode = SWITCH_PT_TO_ABYP;
			else
				switch_mode = SWITCH_PT_TO_PT;
		}
	}

	return switch_mode;
}

bool iris_is_resolution_switched(struct dsi_mode_info *mode_info)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	IRIS_LOGD("%s(), switch resolution from %ux%u to %ux%u", __func__,
			pcfg->cur_h_active, pcfg->cur_v_active,
			mode_info->h_active, mode_info->v_active);

	if (pcfg->cur_h_active != mode_info->h_active
			|| pcfg->cur_v_active != mode_info->v_active) {
		pcfg->cur_h_active = mode_info->h_active;
		pcfg->cur_v_active = mode_info->v_active;

		return true;
	}

	return false;
}

static void _iris_switch_framerate(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 framerate = pcfg->panel->cur_mode->timing.refresh_rate;
	bool high = false;

	if ((framerate == HIGH_FREQ)
			&& (pcfg->panel->cur_mode->timing.v_active == FHD_H))
		high = true;

	iris_send_ipopt_cmds(IRIS_IP_SYS, high ? 0xA1:0xA0);
	udelay(100);
	iris_send_ipopt_cmds(IRIS_IP_RX, high ? 0xE1:0xE0);
	iris_send_ipopt_cmds(IRIS_IP_TX, high ? 0x4:0x0);
	iris_set_out_frame_rate(framerate);
	iris_send_ipopt_cmds(IRIS_IP_RX, high ? 0xF2:0xF1);
#if defined(PXLW_IRIS_DUAL)
	iris_send_ipopt_cmds(IRIS_IP_RX_2, high ? 0xF2:0xF1);
	iris_send_ipopt_cmds(IRIS_IP_BLEND, high ? 0xF1:0xF0);
#endif
	udelay(2000); //delay 2ms
}

int iris_post_switch(struct dsi_panel *panel,
		struct dsi_panel_cmd_set *switch_cmds,
		struct dsi_mode_info *mode_info)
{
	int rc = 0;
	struct iris_cfg *pcfg = iris_get_cfg();
	int lightup_opt = iris_lightup_opt_get();
	u32 refresh_rate = mode_info->refresh_rate;
	u32 frame_width = mode_info->h_active;
	u32 frame_height = mode_info->v_active;
	u32 switch_case = _iris_switch_case(refresh_rate, frame_height);

	IRIS_LOGI("%s(), post switch to %ux%u@%uHz, cmd list %u, switch case %s",
			__func__, frame_width, frame_height, refresh_rate,
			pcfg->cmd_list_index, switch_case_name[switch_case]);

	pcfg->switch_case = switch_case;

	if (switch_cmds == NULL)
		return 0;

	if (lightup_opt & 0x8) {
		rc = iris_dsi_send_cmds(panel, switch_cmds->cmds, switch_cmds->count, switch_cmds->state);
		IRIS_LOGI("%s(), post switch Force ABYP", __func__);
		return 0;
	}

	if (iris_get_abyp_mode(panel) == PASS_THROUGH_MODE)
		rc = iris_pt_send_panel_cmd(panel, &(panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_POST_TIMING_SWITCH]));
	else
		rc = iris_dsi_send_cmds(panel, switch_cmds->cmds, switch_cmds->count, switch_cmds->state);

	IRIS_LOGD("%s(), return %d", __func__, rc);
	return 0;
}

int iris_switch(struct dsi_panel *panel,
		struct dsi_panel_cmd_set *switch_cmds,
		struct dsi_mode_info *mode_info)
{
	int rc = 0;
	struct iris_cfg *pcfg = iris_get_cfg();
	int lightup_opt = iris_lightup_opt_get();
	u32 refresh_rate = mode_info->refresh_rate;
	u32 switch_case = pcfg->switch_case;
	ktime_t ktime = 0;

	IRIS_LOGD("%s(%d)", __func__, __LINE__);

	pcfg->panel_te = refresh_rate;
	pcfg->ap_te = refresh_rate;
	pcfg->next_fps_for_iris = refresh_rate;

	if (IRIS_IF_LOGI())
		ktime = ktime_get();

	if (lightup_opt & 0x8) {
		rc = iris_dsi_send_cmds(panel, switch_cmds->cmds, switch_cmds->count, switch_cmds->state);
		IRIS_LOGI("%s(), switch between ABYP and ABYP, total cost '%d us'",
				__func__,
				(u32)(ktime_to_us(ktime_get()) - ktime_to_us(ktime)));
		IRIS_LOGW("%s(), force ABYP switch.", __func__);
		return rc;
	}

	if (switch_case == SWITCH_ABYP_TO_ABYP)
		rc = iris_dsi_send_cmds(panel, switch_cmds->cmds, switch_cmds->count, switch_cmds->state);

	if (switch_case == SWITCH_PT_TO_PT) {
		rc = iris_pt_send_panel_cmd(panel, switch_cmds);
		_iris_switch_framerate();
	}

	if (switch_case == SWITCH_PT_TO_ABYP) {
		iris_abypass_switch_proc(pcfg->display, ANALOG_BYPASS_MODE, false, true);
		rc = iris_dsi_send_cmds(panel, switch_cmds->cmds, switch_cmds->count, switch_cmds->state);
	}

	// Update panel timing
	iris_is_resolution_switched(mode_info);

	IRIS_LOGI("%s(), return %d, total cost '%d us'",
			__func__,
			rc, (u32)(ktime_to_us(ktime_get()) - ktime_to_us(ktime)));

	return 0;
}

uint32_t iris_get_cont_type_with_timing_switch(struct dsi_panel *panel)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	uint32_t type = IRIS_CONT_SPLASH_NONE;
	uint32_t switch_case = SWITCH_NONE;

	if (pcfg->valid >= PARAM_PARSED)
		switch_case = _iris_switch_case(
				panel->cur_mode->timing.refresh_rate,
				panel->cur_mode->timing.v_active);

	IRIS_LOGI("%s(), switch case: %s, rate: %d, v: %d",
			__func__,
			switch_case_name[switch_case],
			panel->cur_mode->timing.refresh_rate,
			panel->cur_mode->timing.v_active);

	switch (switch_case) {
	case SWITCH_PT_TO_PT:
		type = IRIS_CONT_SPLASH_LK;
		break;
	case SWITCH_ABYP_TO_ABYP:
	case SWITCH_ABYP_TO_PT:
		type = IRIS_CONT_SPLASH_BYPASS_PRELOAD;
		break;
	case SWITCH_PT_TO_ABYP:
		// This case does not happen
	default:
		type = IRIS_CONT_SPLASH_NONE;
		break;
	}

	return type;
}
