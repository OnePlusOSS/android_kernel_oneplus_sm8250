// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#include <drm/drm_mipi_dsi.h>
#include <video/mipi_display.h>
#include <dsi_drm.h>
#include <sde_encoder_phys.h>
#include <sde_trace.h>
#include "dsi_parser.h"
#include "dsi_iris5_api.h"
#include "dsi_iris5_lightup.h"
#include "dsi_iris5_lightup_ocp.h"
#include "dsi_iris5_lp.h"
#include "dsi_iris5_pq.h"
#include "dsi_iris5_ioctl.h"
#include "dsi_iris5_lut.h"
#include "dsi_iris5_mode_switch.h"
#include "dsi_iris5_loop_back.h"
#include "dsi_iris5_gpio.h"
#include "dsi_iris5_frc.h"
#include "dsi_iris5_timing_switch.h"
#include "dsi_iris5_log.h"

//#define IRIS_HDK_DEV
#define IRIS_CHIP_VER_0   0
#define IRIS_CHIP_VER_1   1
#define IRIS_OCP_HEADER_ADDR_LEN  8

#define to_dsi_display(x) container_of(x, struct dsi_display, host)

enum {
	DSI_CMD_ONE_LAST_FOR_MULT_IPOPT = 0,
	DSI_CMD_ONE_LAST_FOR_ONE_IPOPT,
	DSI_CMD_ONE_LAST_FOR_ONE_PKT,
};

/*use to parse dtsi cmd list*/
struct iris_cmd_header {
	uint32_t dsi_type;  /* dsi command type 0x23 0x29*/
	uint32_t last_pkt; /*last in chain*/
	uint32_t wait_us; /*wait time*/
	uint32_t ip_type; /*ip type*/
	uint32_t opt_and_link; /*ip option and lp or hs*/
	uint32_t payload_len; /*payload len*/
};

struct iris_cmd_comp {
	int32_t link_state;
	int32_t cnt;
	struct dsi_cmd_desc *cmd;
};

static int gcfg_index;
static struct iris_cfg gcfg[IRIS_CFG_NUM] = {};
static uint8_t g_cont_splash_type = IRIS_CONT_SPLASH_NONE;
uint8_t iris_pq_update_path = PATH_DSI;

int iris_dbgfs_status_init(struct dsi_display *display);
static int _iris_dbgfs_cont_splash_init(struct dsi_display *display);
static void _iris_send_cont_splash_pkt(uint32_t type);
static int _iris_update_pq_seq(struct iris_update_ipopt *popt, int len);

static int _iris_get_vreg(void)
{
	int rc = 0;
	int i;
	struct regulator *vreg = NULL;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct dsi_panel *panel = pcfg->panel;

	for (i = 0; i < pcfg->iris_power_info.count; i++) {
		vreg = devm_regulator_get(panel->parent,
				pcfg->iris_power_info.vregs[i].vreg_name);
		rc = PTR_RET(vreg);
		if (rc) {
			IRIS_LOGE("failed to get %s regulator",
					pcfg->iris_power_info.vregs[i].vreg_name);
			goto error_put;
		}
		pcfg->iris_power_info.vregs[i].vreg = vreg;
	}

	return rc;
error_put:
	for (i = i - 1; i >= 0; i--) {
		devm_regulator_put(pcfg->iris_power_info.vregs[i].vreg);
		pcfg->iris_power_info.vregs[i].vreg = NULL;
	}
	return rc;
}

static int _iris_put_vreg(void)
{
	int rc = 0;
	int i;
	struct iris_cfg *pcfg = iris_get_cfg();

	for (i = pcfg->iris_power_info.count - 1; i >= 0; i--)
		devm_regulator_put(pcfg->iris_power_info.vregs[i].vreg);

	return rc;
}

void iris_init(struct dsi_display *display, struct dsi_panel *panel)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	IRIS_LOGI("%s(), for dispaly: %s, panel: %s, cfg index: %d",
			__func__,
			display->display_type, panel->name, gcfg_index);
	pcfg->display = display;
	pcfg->panel = panel;
	pcfg->iris_i2c_read = NULL;
	pcfg->iris_i2c_write = NULL;
	pcfg->lp_ctrl.esd_enable = true;
	pcfg->aod = false;
	pcfg->fod = false;
	pcfg->fod_pending = false;
	atomic_set(&pcfg->fod_cnt, 0);
	pcfg->iris_initialized = false;
	iris_init_timing_switch();
#ifdef IRIS_ABYP_LIGHTUP
	pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE;
#else
	// UEFI is running bypass mode.
	pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE;

	if (iris_virtual_display(display))
		pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE;
	else if (pcfg->valid < PARAM_PARSED)
		pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE;
	else
		iris_init_vfr_work(pcfg);
#endif
	pcfg->abypss_ctrl.pending_mode = MAX_MODE;
	mutex_init(&pcfg->abypss_ctrl.abypass_mutex);

#ifndef IRIS_HDK_DEV // skip ext clk
	pcfg->ext_clk = devm_clk_get(&display->pdev->dev, "pw_bb_clk2");
#endif

	if (!iris_virtual_display(display) && pcfg->valid >= PARAM_PARSED) {
		pcfg->cmd_list_index = IRIS_DTSI0_PIP_IDX;
		pcfg->cur_fps_in_iris = LOW_FREQ;
		pcfg->next_fps_for_iris = LOW_FREQ;
		pcfg->cur_vres_in_iris = FHD_H;
		iris_dbgfs_lp_init(display);
		iris_dbgfs_pq_init(display);
		_iris_dbgfs_cont_splash_init(display);
		iris_loop_back_init(display);
		iris_dbgfs_ms_init(display);
		iris_dbgfs_adb_type_init(display);
		iris_dbgfs_fw_calibrate_status_init();
		iris_dbgfs_frc_init();
		iris_dbgfs_status_init(display);
		iris_dbg_gpio_init();
	}
	_iris_get_vreg();
	//pcfg->mipi_pwr_st = true; //force to true for aux ch at boot up to workaround
	mutex_init(&pcfg->gs_mutex);
}

void iris_deinit(struct dsi_display *display)
{
	struct iris_cfg *pcfg;
	int i;

	if (!iris_is_chip_supported())
		return;

	if (iris_virtual_display(display))
		pcfg = iris_get_cfg_by_index(DSI_SECONDARY);
	else
		pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

#ifndef IRIS_HDK_DEV // skip ext clk
	if (pcfg->ext_clk) {
		devm_clk_put(&display->pdev->dev, pcfg->ext_clk);
		pcfg->ext_clk = NULL;
	}
#endif

	for (i = 0; i < IRIS_PIP_IDX_CNT; i++)
		iris_free_ipopt_buf(i);
	_iris_put_vreg();
}

void iris_control_pwr_regulator(bool on)
{
	int rc = 0;
	struct iris_cfg *pcfg = NULL;

	if (!iris_is_chip_supported())
		return;

	pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	rc = dsi_pwr_enable_regulator(&pcfg->iris_power_info, on);
	if (rc)
		IRIS_LOGE("failed to power %s iris", on ? "on" : "off");
}

void iris_power_on(struct dsi_panel *panel)
{
	if (!iris_is_chip_supported())
		return;

	IRIS_LOGI("%s(), for [%s] %s, cfg index: %i, secondary: %s",
			__func__,
			panel->name, panel->type, gcfg_index,
			panel->is_secondary ? "true" : "false");

	if (panel->is_secondary)
		return;

#ifdef IRIS_HDK_DEV // skip power control
	return;
#endif

	if (iris_vdd_valid()) {
		iris_enable_vdd();
	} else { // No need to control vdd and clk
		IRIS_LOGW("%s(), vdd does not valid, use pmic", __func__);
		iris_control_pwr_regulator(true);
	}

	usleep_range(5000, 5000);
}

void iris_reset(void)
{
#ifndef IRIS_HDK_DEV // skip ext clk
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
#endif

	if (!iris_is_chip_supported())
		return;

	IRIS_LOGI("%s()", __func__);

#ifndef IRIS_HDK_DEV // skip ext clk
	if (pcfg->ext_clk) {
		IRIS_LOGI("%s(), enable ext clk", __func__);
		clk_prepare_enable(pcfg->ext_clk);
		usleep_range(5000, 5001);
	} else { // No need to control vdd and clk
		IRIS_LOGV("%s(), invalid ext clk", __func__);
		goto ERROR_CLK;
	}
#endif

	iris_reset_chip();
	return;

#ifndef IRIS_HDK_DEV // skip ext clk
ERROR_CLK:
	iris_control_pwr_regulator(false);
#endif
}

void iris_power_off(struct dsi_panel *panel)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (!iris_is_chip_supported())
		return;

	IRIS_LOGI("%s(), for [%s] %s, cfg index: %i, secondary: %s",
			__func__,
			panel->name, panel->type, gcfg_index,
			panel->is_secondary ? "true" : "false");

	if (panel->is_secondary)
		return;

	iris_reset_off();
#ifdef IRIS_HDK_DEV // skip power control
	return;
#endif

	if (iris_vdd_valid())
		iris_disable_vdd();
	else
		iris_control_pwr_regulator(false);

	if (pcfg->ext_clk) {
		IRIS_LOGI("%s(), disable ext clk", __func__);
		clk_disable_unprepare(pcfg->ext_clk);
	}
	iris_reset_off();
}

void iris_set_cfg_index(int index)
{
	if (index >= IRIS_CFG_NUM) {
		IRIS_LOGE("%s(), index: %d exceed %d", __func__, index, IRIS_CFG_NUM);
		return;
	}

	IRIS_LOGD("%s(), index: %d", __func__, index);
	gcfg_index = index;
}

bool iris_virtual_display(const struct dsi_display *display)
{
	if (display && display->panel && display->panel->is_secondary)
		return true;

	return false;
}

bool iris_is_virtual_encoder_phys(void *phys_enc)
{
	struct sde_encoder_phys *phys_encoder = phys_enc;
	struct sde_connector *c_conn = NULL;
	struct dsi_display *display = NULL;

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

	return true;
}

struct iris_cfg *iris_get_cfg(void)
{
	return &gcfg[gcfg_index];
}

struct iris_cfg *iris_get_cfg_by_index(int index)
{
	if (index < IRIS_CFG_NUM)
		return &gcfg[index];

	return NULL;
}

uint8_t iris_get_cont_splash_type(void)
{
	return g_cont_splash_type;
}

static struct iris_ctrl_seq *_iris_get_ctrl_seq_addr(
		struct iris_ctrl_seq *base, uint8_t chip_id)
{
	struct iris_ctrl_seq *pseq = NULL;

	switch (chip_id) {
	case IRIS_CHIP_VER_0:
		pseq = base;
		break;
	case IRIS_CHIP_VER_1:
		pseq = base + 1;
		break;
	default:
		IRIS_LOGE("unknown chip id: %d", chip_id);
		break;
	}
	return pseq;
}

static bool _iris_is_valid_ip(uint32_t ip)
{
	if (ip >= LUT_IP_START && ip < LUT_IP_END)
		return true;

	if (ip < IRIS_IP_END) //CID99843
		return true;

	return false;
}

static struct iris_ctrl_seq *_iris_get_ctrl_seq_common(
		struct iris_cfg *pcfg, int32_t type)
{
	struct iris_ctrl_seq *pseq = NULL;

	if (type == IRIS_CONT_SPLASH_NONE)
		pseq = _iris_get_ctrl_seq_addr(pcfg->ctrl_seq, pcfg->chip_id);
	else if (type == IRIS_CONT_SPLASH_LK)
		pseq = _iris_get_ctrl_seq_addr(pcfg->ctrl_seq_cs, pcfg->chip_id);

	return pseq;
}

static struct iris_ctrl_seq *_iris_get_ctrl_seq(struct iris_cfg *pcfg)
{
	return _iris_get_ctrl_seq_common(pcfg, IRIS_CONT_SPLASH_NONE);
}

static struct iris_ctrl_seq *_iris_get_ctrl_seq_cs(struct iris_cfg *pcfg)
{
	return _iris_get_ctrl_seq_common(pcfg, IRIS_CONT_SPLASH_LK);
}

static bool _iris_is_lut(uint8_t ip)
{
	return ip >= LUT_IP_START ? true : false;
}

static uint32_t _iris_get_ocp_type(const uint8_t *payload)
{
	uint32_t *pval = (uint32_t *)payload;

	return cpu_to_le32(pval[0]);
}

static uint32_t _iris_get_ocp_base_addr(const uint8_t *payload)
{
	uint32_t *pval = (uint32_t *)payload;

	return cpu_to_le32(pval[1]);
}

static void _iris_set_ocp_type(const uint8_t *payload, uint32_t val)
{
	uint32_t *pval = (uint32_t *)payload;

	IRIS_LOGV("%s(), change addr from %#x to %#x.", __func__, pval[0], val);
	pval[0] = val;
}

static void _iris_set_ocp_base_addr(const uint8_t *payload, uint32_t val)
{
	uint32_t *pval = (uint32_t *)payload;

	IRIS_LOGV("%s(), change addr from %#x to %#x.", __func__, pval[1], val);
	pval[1] = val;
}

static bool _iris_is_direct_bus(const uint8_t *payload)
{
	uint8_t val = _iris_get_ocp_type(payload) & 0x0f;

	//the last 4bit will show the ocp type
	if (val == 0x00 || val == 0x0c)
		return true;

	return false;
}

static int _iris_split_mult_pkt(const uint8_t *payload, int payload_size)
{
	uint32_t pkt_size = 0;
	int pkt_cnt = 1;
	struct iris_cfg *pcfg = iris_get_cfg();

	if (!_iris_is_direct_bus(payload))
		return pkt_cnt;

	pkt_size = pcfg->split_pkt_size;
	if (payload_size > pkt_size + IRIS_OCP_HEADER_ADDR_LEN)
		pkt_cnt =  (payload_size - IRIS_OCP_HEADER_ADDR_LEN + pkt_size - 1) / pkt_size;

	return pkt_cnt;
}

static void _iris_set_cont_splash_type(uint8_t type)
{
	g_cont_splash_type = type;
}

struct iris_ip_index *iris_get_ip_idx(int32_t type)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	if (unlikely(type < IRIS_DTSI0_PIP_IDX || type >= IRIS_PIP_IDX_CNT)) {
		IRIS_LOGE("%s, can not get pip idx %u", __func__, type);
		return NULL;
	}

	return pcfg->ip_index_arr[type];
}

static int32_t _iris_get_ip_idx_type(const struct iris_ip_index *pip_index)
{
	int32_t type = -1;
	struct iris_cfg *pcfg = iris_get_cfg();

	if (pip_index == pcfg->ip_index_arr[IRIS_DTSI0_PIP_IDX])
		type = IRIS_DTSI0_PIP_IDX;
	else if (pip_index == pcfg->ip_index_arr[IRIS_DTSI1_PIP_IDX])
		type = IRIS_DTSI1_PIP_IDX;
	else if (pip_index == pcfg->ip_index_arr[IRIS_LUT_PIP_IDX])
		type = IRIS_LUT_PIP_IDX;

	return type;
}

static void _iris_init_ip_index(struct iris_ip_index *pip_index)
{
	int32_t i = 0;
	int32_t j = 0;
	int32_t cnt = 0;
	int32_t ip_cnt = IRIS_IP_CNT;

	if (_iris_get_ip_idx_type(pip_index) == IRIS_LUT_PIP_IDX)
		ip_cnt = LUT_IP_END - LUT_IP_START;

	for (i = 0; i < ip_cnt; i++) {
		cnt = pip_index[i].opt_cnt;
		for (j = 0; j < cnt; j++) {
			pip_index[i].opt[j].cmd = NULL;
			pip_index[i].opt[j].link_state = 0xff;
		}
	}
}

static int32_t _iris_alloc_pip_buf(struct iris_ip_index *pip_index)
{
	int i = 0;
	int j = 0;
	int opt_cnt = 0;
	int ip_cnt = IRIS_IP_CNT;

	if (_iris_get_ip_idx_type(pip_index) == IRIS_LUT_PIP_IDX)
		ip_cnt = LUT_IP_END - LUT_IP_START;

	for (i = 0; i < ip_cnt; i++) {
		opt_cnt = pip_index[i].opt_cnt;
		if (opt_cnt != 0) {
			pip_index[i].opt =
				kvzalloc(opt_cnt * sizeof(struct iris_ip_opt),
						GFP_KERNEL);
			if (!pip_index[i].opt) {
				IRIS_LOGE("%s:%d no space\n", __func__, __LINE__);
				/*free already malloc space*/
				for (j = 0; j < i; j++) {
					kvfree(pip_index[j].opt);
					pip_index[j].opt = NULL;
				}
				return -ENOMEM;
			}
		}
	}

	return 0;
}

static int32_t _iris_alloc_desc_buf(struct dsi_cmd_desc **cmds, int cmd_cnt)
{
	int cmd_size = 0;

	/*create dsi cmds*/
	if (cmd_cnt == 0) {
		IRIS_LOGE("%s(), invalid statics count", __func__);
		return -EINVAL;
	}

	cmd_size = cmd_cnt * sizeof(struct dsi_cmd_desc);
	*cmds = vzalloc(cmd_size);
	if (!(*cmds)) {
		IRIS_LOGE("%s(), failed to malloc space for dsi", __func__);
		return -ENOMEM;
	}
	IRIS_LOGI("%s(), alloc %p, count %d", __func__, *cmds, cmd_cnt);

	return 0;
}

static int32_t _iris_alloc_cmd_buf(struct dsi_cmd_desc **cmds,
		struct iris_ip_index *pip_index, int cmd_cnt)
{
	int32_t rc = 0;

	rc = _iris_alloc_desc_buf(cmds, cmd_cnt);
	if (rc)
		return rc;

	rc = _iris_alloc_pip_buf(pip_index);
	if (rc) {
		vfree(*cmds);
		*cmds = NULL;
	}

	return rc;
}

static int32_t _iris_write_ip_opt(struct dsi_cmd_desc *cmd,
		const struct iris_cmd_header *hdr, int32_t pkt_cnt,
		struct iris_ip_index *pip_index)
{
	uint8_t i = 0;
	uint8_t ip = 0;
	uint8_t opt_id = 0;
	uint8_t cnt = 0;

	if (!hdr || !cmd || !pip_index) {
		IRIS_LOGE("%s(), invalid input parameter.", __func__);
		return -EINVAL;
	}

	ip = hdr->ip_type & 0xff;
	opt_id = hdr->opt_and_link & 0xff;

	if (ip >= LUT_IP_START)
		ip -= LUT_IP_START;

	cnt = pip_index[ip].opt_cnt;

	for (i = 0; i < cnt; i++) {
		if (pip_index[ip].opt[i].cmd == NULL) {
			pip_index[ip].opt[i].cmd = cmd;
			pip_index[ip].opt[i].cmd_cnt = pkt_cnt;
			pip_index[ip].opt[i].opt_id = opt_id;
			break;
		} else if (pip_index[ip].opt[i].opt_id == opt_id) {
			/*find the right opt_id*/
			pip_index[ip].opt[i].cmd_cnt += pkt_cnt;
			break;
		}
	}

	if (i == cnt) {
		IRIS_LOGE("%s(), find ip opt fail, ip = 0x%02x opt = 0x%02x.",
				__func__, ip, opt_id);
		return -EINVAL;
	}

	/*to set link state*/
	if (pip_index[ip].opt[i].link_state == 0xff
			&& pip_index[ip].opt[i].opt_id == opt_id) {
		uint8_t link_state = 0;

		link_state = (hdr->opt_and_link >> 8) & 0xff;
		pip_index[ip].opt[i].link_state =
			link_state ? DSI_CMD_SET_STATE_LP : DSI_CMD_SET_STATE_HS;
	}

	return 0;
}

static int32_t _iris_trans_section_hdr_to_desc(
		struct dsi_cmd_desc *cmd, const struct iris_cmd_header *hdr)
{
	memset(cmd, 0, sizeof(struct dsi_cmd_desc));

	cmd->msg.type = (hdr->dsi_type & 0xff);
	cmd->post_wait_ms = (hdr->wait_us & 0xff);
	cmd->last_command = ((hdr->last_pkt & 0xff) != 0);
	cmd->msg.tx_len = hdr->payload_len;

	IRIS_LOGV("%s(), type: %#x, wait: %#x, last: %s, len: %zu",
			__func__,
			cmd->msg.type, cmd->post_wait_ms,
			cmd->last_command ? "true" : "false", cmd->msg.tx_len);

	return cmd->msg.tx_len;
}

static void _iris_change_last_and_size(struct iris_cmd_header *dest,
		const struct iris_cmd_header *src, int index, const int pkt_cnt)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	int pkt_size = pcfg->split_pkt_size;

	memcpy(dest, src, sizeof(*src));
	if (index == pkt_cnt - 1) {
		dest->payload_len = src->payload_len * sizeof(uint32_t) - (pkt_cnt - 1) * pkt_size;
		return;
	}

	dest->last_pkt = (pcfg->add_last_flag) ? 1 : 0;
	dest->payload_len = (pkt_size + IRIS_OCP_HEADER_ADDR_LEN);
}

static int _iris_write_cmd_hdr(struct dsi_cmd_desc *cmd,
		const struct iris_cmd_header *phdr, int pkt_cnt)
{
	int i = 0;
	struct iris_cmd_header tmp_hdr;

	for (i = 0; i < pkt_cnt; i++) {
		_iris_change_last_and_size(&tmp_hdr, phdr, i, pkt_cnt);

		/*add cmds hdr information*/
		_iris_trans_section_hdr_to_desc(cmd + i, &tmp_hdr);
	}

	return 0;
}

static bool _iris_need_direct_send(const struct iris_cmd_header *hdr)
{
	if (hdr == NULL) {
		IRIS_LOGE("%s(), invalid input!", __func__);
		return false;
	}

	if (hdr->ip_type == APP_CODE_LUT)
		return true;

	return false;
}

static void _iris_create_cmd_payload(const struct iris_cmd_header *hdr,
		const uint8_t *payload, uint8_t *msg_buf, int32_t buf_size)
{
	int32_t i = 0;
	uint32_t *pval = NULL;
	uint32_t cnt = 0;

	if (_iris_need_direct_send(hdr)) {
		memcpy(msg_buf, payload, buf_size);
		return;
	}

	pval = (uint32_t *)payload;
	cnt = buf_size >> 2;
	for (i = 0; i < cnt; i++)
		*(uint32_t *)(msg_buf + (i << 2)) = cpu_to_le32(pval[i]);
}

static int _iris_write_cmd_payload(struct dsi_cmd_desc *pdesc,
		const struct iris_cmd_header *hdr, const char *payload, int pkt_cnt)
{
	int i = 0;
	uint32_t dlen = 0;
	uint8_t *ptr = NULL;
	struct iris_cfg *pcfg = iris_get_cfg();
	uint32_t pkt_size = pcfg->split_pkt_size;
	uint32_t ocp_type = _iris_get_ocp_type(payload);
	uint32_t base_addr = _iris_get_ocp_base_addr(payload);

	if (pkt_cnt == 1) {
		dlen = pdesc->msg.tx_len;
		ptr = vzalloc(dlen);
		if (!ptr) {
			IRIS_LOGE("%s Failed to allocate memory", __func__);
			return -ENOMEM;
		}

		_iris_create_cmd_payload(hdr, payload, ptr, dlen);
		pdesc->msg.tx_buf = ptr;
	} else {
		/*remove header and base address*/
		payload += IRIS_OCP_HEADER_ADDR_LEN;
		for (i = 0; i < pkt_cnt; i++) {
			dlen = pdesc[i].msg.tx_len;

			ptr = vzalloc(dlen);
			if (!ptr) {
				IRIS_LOGE("can not allocate space");
				return -ENOMEM;
			}

			_iris_set_ocp_base_addr(ptr, base_addr + i * pkt_size);
			_iris_set_ocp_type(ptr, ocp_type);
			_iris_create_cmd_payload(hdr, payload,
					ptr + IRIS_OCP_HEADER_ADDR_LEN,
					dlen - IRIS_OCP_HEADER_ADDR_LEN);

			/* add payload */
			payload += (dlen - IRIS_OCP_HEADER_ADDR_LEN);
			pdesc[i].msg.tx_buf = ptr;
		}
	}

	if (IRIS_IF_LOGVV()) {
		int len = 0;

		for (i = 0; i < pkt_cnt; i++) {
			len = (pdesc[i].msg.tx_len > 16) ? 16 : pdesc[i].msg.tx_len;
			print_hex_dump(KERN_ERR, "", DUMP_PREFIX_NONE, 16, 4,
					pdesc[i].msg.tx_buf, len, false);
		}
	}

	return 0;
}

void iris_change_type_addr(struct iris_ip_opt *dest, struct iris_ip_opt *src)
{
	int i = 0;
	struct iris_cfg *pcfg = iris_get_cfg();
	uint32_t pkt_size = pcfg->split_pkt_size;
	const void *buf = src->cmd->msg.tx_buf;
	int pkt_cnt = dest->cmd_cnt;
	uint32_t ocp_type = _iris_get_ocp_type(buf);
	uint32_t base_addr = _iris_get_ocp_base_addr(buf);

	for (i = 0; i < pkt_cnt; i++) {
		buf = dest->cmd[i].msg.tx_buf;
		_iris_set_ocp_base_addr(buf, base_addr + i * pkt_size);
		_iris_set_ocp_type(buf, ocp_type);
		if (IRIS_LOGD_IF(i == 0)) {
			IRIS_LOGD("%s(), change ocp type 0x%08x, change base addr to 0x%08x.",
					__func__, ocp_type, base_addr);
		}
	}
}

struct iris_ip_opt *iris_find_ip_opt(uint8_t ip, uint8_t opt_id)
{
	int32_t i = 0;
	struct iris_ip_opt *popt = NULL;
	struct iris_ip_index *pip_index = NULL;
	struct iris_cfg *pcfg = iris_get_cfg();
	int32_t type = pcfg->cmd_list_index;

	IRIS_LOGV("%s(), ip: %#x, opt: %#x", __func__, ip, opt_id);
	if (!_iris_is_valid_ip(ip)) {
		IRIS_LOGE("%s(), ip %d is out of range", __func__, ip);
		return NULL;
	}

	if (ip >= LUT_IP_START) {
		type = IRIS_LUT_PIP_IDX;
		ip -= LUT_IP_START;
	}

	pip_index = iris_get_ip_idx(type) + ip;

	for (i = 0; i < pip_index->opt_cnt; i++) {
		popt = pip_index->opt + i;
		if (popt->opt_id == opt_id)
			return popt;
	}

	return NULL;
}

static void _iris_print_ipopt(struct iris_ip_index  *pip_index)
{
	int32_t i = 0;
	int32_t j = 0;
	int32_t ip_cnt = IRIS_IP_CNT;

	if (_iris_get_ip_idx_type(pip_index) == IRIS_LUT_PIP_IDX)
		ip_cnt = LUT_IP_END - LUT_IP_START;

	for (i = 0; i < ip_cnt; i++) {
		for (j = 0; j < pip_index[i].opt_cnt; j++) {
			struct iris_ip_opt *popt = &(pip_index[i].opt[j]);

			IRIS_LOGI("%s(%d), ip: %02x, opt: %02x, cmd: %p, len: %d, link state: %#x",
					__func__, __LINE__,
					i, popt->opt_id, popt->cmd, popt->cmd_cnt, popt->link_state);
		}
	}
}

static void _iris_parse_appversion(
		const uint8_t *payload, const struct iris_cmd_header *phdr)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	uint32_t *pval = NULL;
	uint32_t app_version = 0x0;
	uint32_t app_date;

	if (phdr->ip_type != APP_VERSION_LUT)
		return;

	pval = (uint32_t *)payload;

	app_version = *pval;
	app_date = *(pval + 1);
	pcfg->app_version = app_version;
	pcfg->app_date[0] = app_date & 0xff;
	pcfg->app_date[1] = (app_date >> 8) & 0xff;
	pcfg->app_date[2] = (app_date >> 16) & 0xff;
	pcfg->app_date[3] = (app_date >> 24) & 0xff;
	IRIS_LOGI("%s(), iris fw version: %d, [date]%d:%d:%d:%d",
			__func__,
			pcfg->app_version,
			pcfg->app_date[3],
			pcfg->app_date[2],
			pcfg->app_date[1],
			pcfg->app_date[0]);
}

static int32_t _iris_add_cmd_to_ipidx(const struct iris_data *data,
		struct dsi_cmd_desc *cmds, int cmd_pos, struct iris_ip_index *pip_index)
{
	int32_t span = 0;
	int32_t pkt_cnt = 0;
	int32_t total_size = 0;
	int32_t payload_size = 0;
	struct dsi_cmd_desc *pdesc = NULL;
	const uint8_t *payload = NULL;
	const struct iris_cmd_header *hdr = NULL;
	const uint8_t *buf_ptr = (uint8_t *)data->buf;
	int32_t data_size = data->size;
	int32_t cmd_index = cmd_pos;

	while (total_size < data_size) {
		hdr = (const struct iris_cmd_header *)buf_ptr;
		pdesc = &cmds[cmd_index];
		payload = buf_ptr + sizeof(struct iris_cmd_header);
		payload_size = hdr->payload_len * sizeof(uint32_t);
		total_size += sizeof(struct iris_cmd_header) + payload_size;

		_iris_parse_appversion(payload, hdr);

		pkt_cnt = _iris_split_mult_pkt(payload, payload_size);
		if (IRIS_LOGV_IF(pkt_cnt > 1))
			IRIS_LOGV("%s(), pkt_cnt is: %d.", __func__, pkt_cnt);

		/*need to first write desc header and then write payload*/
		_iris_write_cmd_hdr(pdesc, hdr, pkt_cnt);
		_iris_write_cmd_payload(pdesc, hdr, payload, pkt_cnt);

		/*write cmd link information*/
		_iris_write_ip_opt(pdesc, hdr, pkt_cnt, pip_index);

		buf_ptr += sizeof(struct iris_cmd_header) + payload_size;
		cmd_index += pkt_cnt;
	}
	span = cmd_index - cmd_pos;

	if (IRIS_IF_LOGVV())
		_iris_print_ipopt(pip_index);

	return span;
}

static int32_t _iris_create_ipidx(const struct iris_data *data, int32_t data_cnt,
		struct iris_ip_index *pip_index, int32_t cmd_cnt)
{
	int32_t i = 0;
	int32_t rc = 0;
	int32_t cmd_pos = 0;
	struct dsi_cmd_desc *cmds = NULL;

	/*create dsi cmd list*/
	rc = _iris_alloc_cmd_buf(&cmds, pip_index, cmd_cnt);
	if (rc) {
		IRIS_LOGE("create dsi memory failed!");
		return -ENOMEM;
	}

	_iris_init_ip_index(pip_index);

	for (i = 0; i < data_cnt; i++) {
		if (data[i].size == 0) {
			IRIS_LOGW("data[%d] length is %d.", i, data[i].size);
			continue;
		}
		cmd_pos += _iris_add_cmd_to_ipidx(&data[i], cmds, cmd_pos, pip_index);
	}

	if (cmd_cnt != cmd_pos) {
		IRIS_LOGE("%s(), invalid desc, cmd count: %d, cmd pos: %d.",
				__func__, cmd_cnt, cmd_pos);
	}

	return 0;
}

static int32_t _iris_accum_section_desc_cnt(const struct iris_cmd_header *hdr,
		const uint8_t *payload, int32_t *pcmd_cnt)
{
	int pkt_cnt = 1;
	int32_t payload_size = 0;

	if (!hdr || !pcmd_cnt || !payload) {
		IRIS_LOGE("%s(%d), invalid input parameter!", __func__, __LINE__);
		return -EINVAL;
	}

	payload_size = hdr->payload_len * sizeof(uint32_t);
	pkt_cnt = _iris_split_mult_pkt(payload, payload_size);

	/* it will split to pkt_cnt dsi cmds
	 * add (pkt_cnt-1) ocp_header(4 bytes) and ocp_type(4 bytes)
	 */
	*pcmd_cnt += pkt_cnt;

	IRIS_LOGV("%s(), dsi cmd count: %d", __func__, *pcmd_cnt);

	return 0;
}

static int32_t _iris_accum_section_opt_cnt(const struct iris_cmd_header *hdr,
		struct iris_ip_index *pip_index)
{
	uint8_t last = 0;
	uint8_t ip = 0;

	if (!hdr || !pip_index) {
		IRIS_LOGE("%s(%d), invalid input parameter.", __func__, __LINE__);
		return -EINVAL;
	}

	last = hdr->last_pkt & 0xff;
	ip = hdr->ip_type & 0xff;

	if (last == 1) {
		if (ip >= LUT_IP_START)
			ip -= LUT_IP_START;
		pip_index[ip].opt_cnt++;
	}

	return 0;
}

static int32_t _iris_poll_each_section(const struct iris_cmd_header *hdr,
		const char *payload, struct iris_ip_index *pip_index, int32_t *pcmd_cnt)
{
	int32_t rc = 0;

	rc = _iris_accum_section_desc_cnt(hdr, payload, pcmd_cnt);
	if (rc)
		goto EXIT_VAL;

	rc = _iris_accum_section_opt_cnt(hdr, pip_index);
	if (rc)
		goto EXIT_VAL;

	return 0;

EXIT_VAL:

	IRIS_LOGE("%s(), cmd static is error!", __func__);
	return rc;
}

static int32_t _iris_verify_dtsi(const struct iris_cmd_header *hdr,
		struct iris_ip_index *pip_index)
{
	uint32_t *pval = NULL;
	uint8_t tmp = 0;
	int32_t rc = 0;
	int32_t type = _iris_get_ip_idx_type(pip_index);

	if (type >= IRIS_DTSI0_PIP_IDX && type <= IRIS_DTSI1_PIP_IDX) {
		if (hdr->ip_type >= IRIS_IP_CNT) {
			IRIS_LOGE("hdr->ip_type is  0x%0x out of max ip", hdr->ip_type);
			rc = -EINVAL;
		} else if (((hdr->opt_and_link >> 8) & 0xff)  > 1) {
			IRIS_LOGE("hdr->opt link state not right 0x%0x", hdr->opt_and_link);
			rc = -EINVAL;
		}
	} else {
		if (hdr->ip_type >= LUT_IP_END || hdr->ip_type < LUT_IP_START) {
			IRIS_LOGE("hdr->ip_type is  0x%0x out of ip range", hdr->ip_type);
			rc = -EINVAL;
		}
	}

	switch (hdr->dsi_type) {
	case MIPI_DSI_DCS_SHORT_WRITE:
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
	case MIPI_DSI_DCS_READ:
	case MIPI_DSI_DCS_LONG_WRITE:
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
		break;
	case MIPI_DSI_GENERIC_LONG_WRITE:
		/*judge payload0 for iris header*/
		pval = (uint32_t *)hdr + (sizeof(*hdr) >> 2);
		tmp = *pval & 0x0f;
		if (tmp == 0x00 || tmp == 0x05 || tmp == 0x0c || tmp == 0x08) {
			break;
		} else if (tmp == 0x04) {
			if ((hdr->payload_len - 1) % 2 != 0) {
				IRIS_LOGE("dlen is not right = %d", hdr->payload_len);
				rc = -EINVAL;
			}
		} else {
			IRIS_LOGE("payload hdr is not right = %0x", *pval);
			rc = -EINVAL;
		}
		break;
	default:
		IRIS_LOGE("dsi type is not right %0x", hdr->dsi_type);
		rc = -EINVAL;
	}

	if (rc) {
		IRIS_LOGE("hdr info: %#x %#x %#x %#x %#x %#x",
				hdr->dsi_type, hdr->last_pkt,
				hdr->wait_us, hdr->ip_type,
				hdr->opt_and_link, hdr->payload_len);
	}

	return rc;
}

static int32_t _iris_parse_panel_type(
		struct device_node *np, struct iris_cfg *pcfg)
{
	const char *data = NULL;
	u32 value = 0;
	u8 values[2] = {};
	int32_t rc = 0;

	data = of_get_property(np, "pxlw,panel-type", NULL);
	if (data) {
		if (!strcmp(data, "PANEL_LCD_SRGB"))
			pcfg->panel_type = PANEL_LCD_SRGB;
		else if (!strcmp(data, "PANEL_LCD_P3"))
			pcfg->panel_type = PANEL_LCD_P3;
		else if (!strcmp(data, "PANEL_OLED"))
			pcfg->panel_type = PANEL_OLED;
		else/*default value is 0*/
			pcfg->panel_type = PANEL_LCD_SRGB;
	} else { /*default value is 0*/
		pcfg->panel_type = PANEL_LCD_SRGB;
		IRIS_LOGW("parse panel type failed!");
	}

	rc = of_property_read_u32(np, "pxlw,panel-dimming-brightness", &value);
	if (rc == 0) {
		pcfg->panel_dimming_brightness = value;
	} else {
		/* for V30 panel, 255 may cause panel during exit HDR, and lost TE.*/
		pcfg->panel_dimming_brightness = 250;
		IRIS_LOGW("parse panel dimming brightness failed!");
	}

	rc = of_property_read_u8_array(np, "pxlw,panel-hbm", values, 2);
	if (rc == 0) {
		pcfg->panel_hbm[0] = values[0];
		pcfg->panel_hbm[1] = values[1];
	} else {
		pcfg->panel_hbm[0] = 0xff;
		pcfg->panel_hbm[1] = 0xff;
	}

	return 0;
}

static int32_t _iris_parse_chip_ver(
		struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t rc = 0;

	rc = of_property_read_u32(np, "pxlw,chip-ver",
			&(pcfg->chip_ver));
	if (rc) {
		IRIS_LOGE("can not get property: pxlw, chip-ver");
		return rc;
	}
	IRIS_LOGE("pxlw,chip-version: %#x", pcfg->chip_ver);

	return rc;
}

static int32_t _iris_parse_lut_mode(
		struct device_node *np, struct iris_cfg *pcfg)
{
	const char *data;

	data = of_get_property(np, "pxlw,lut-mode", NULL);
	if (data) {
		if (!strcmp(data, "single"))
			pcfg->lut_mode = SINGLE_MODE;
		else if (!strcmp(data, "interpolation"))
			pcfg->lut_mode = INTERPOLATION_MODE;
		else/*default value is 0*/
			pcfg->lut_mode = INTERPOLATION_MODE;
	} else { /*default value is 0*/
		pcfg->lut_mode = INTERPOLATION_MODE;
		IRIS_LOGI("no lut mode set, use default");
	}

	IRIS_LOGI("pxlw,lut-mode: %d", pcfg->lut_mode);
	return 0;
}

static int32_t _iris_parse_split_pkt_info(
		struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t rc = 0;

	rc = of_property_read_u32(np, "pxlw,pkt-payload-size",
			&(pcfg->split_pkt_size));
	if (rc) {
		IRIS_LOGE("can not get property: pxlw,pkt-payload-size");
		return rc;
	}
	IRIS_LOGI("pxlw,split-pkt-payload-size: %d", pcfg->split_pkt_size);

	rc = of_property_read_u32(np, "pxlw,last-for-per-pkt",
			&(pcfg->add_on_last_flag));
	if (rc) {
		IRIS_LOGE("can not get property:pxlw,last-for-per-pkt");
		pcfg->add_on_last_flag = DSI_CMD_ONE_LAST_FOR_ONE_PKT;
	}
	rc = of_property_read_u32(np, "pxlw,pt-last-for-per-pkt",
			&(pcfg->add_pt_last_flag));
	if (rc) {
		IRIS_LOGE("can not get property:pxlw,pt-last-for-per-pkt");
		pcfg->add_pt_last_flag = DSI_CMD_ONE_LAST_FOR_MULT_IPOPT;
	}
	IRIS_LOGE("pxlw,add-last-for-split-pkt: %d, pxlw,add-pt-last-for-split-pkt: %d",
			pcfg->add_on_last_flag, pcfg->add_pt_last_flag);

	pcfg->add_last_flag = pcfg->add_on_last_flag;

	return rc;
}

static int32_t _iris_poll_cmd_lists(const struct iris_data *data, int32_t data_cnt,
		struct iris_ip_index *pip_index, int32_t *pcmd_cnt)
{
	int32_t rc = 0;
	int32_t i = 0;
	int32_t payload_size = 0;
	int32_t data_len = 0;
	const uint8_t *buf_ptr = NULL;
	const struct iris_cmd_header *hdr = NULL;

	if (data == NULL || pip_index == NULL || pcmd_cnt == NULL) {
		IRIS_LOGE("%s(), invalid input!", __func__);
		return -EINVAL;
	}

	for (i = 0; i < data_cnt; i++) {
		if (data[i].size == 0) {
			IRIS_LOGW("data length is = %d", data[i].size);
			continue;
		}

		buf_ptr = data[i].buf;
		data_len = data[i].size;
		while (data_len >= sizeof(struct iris_cmd_header)) {
			hdr = (const struct iris_cmd_header *)buf_ptr;
			data_len -= sizeof(struct iris_cmd_header);
			if (hdr->payload_len > (data_len >> 2)) {
				IRIS_LOGE("%s: length error, ip = 0x%02x opt=0x%02x, len=%d",
						__func__, hdr->ip_type, hdr->opt_and_link, hdr->payload_len);
				return -EINVAL;
			}

			if (IRIS_IF_LOGVV()) {
				rc = _iris_verify_dtsi(hdr, pip_index);
				if (rc) {
					IRIS_LOGE("%s(%d), verify dtis return: %d", __func__, __LINE__, rc);
					return rc;
				}
			}

			IRIS_LOGV("hdr info, type: 0x%02x, last: 0x%02x, wait: 0x%02x, ip: 0x%02x, opt: 0x%02x, len: %d.",
					hdr->dsi_type, hdr->last_pkt, hdr->wait_us,
					hdr->ip_type, hdr->opt_and_link, hdr->payload_len);

			//payload
			buf_ptr += sizeof(struct iris_cmd_header);

			/*change to uint8_t length*/
			//hdr->payload_len *= sizeof(uint32_t);
			payload_size = hdr->payload_len * sizeof(uint32_t);

			rc = _iris_poll_each_section(hdr, buf_ptr, pip_index, pcmd_cnt);
			if (rc) {
				IRIS_LOGE("%s(), failed to poll section: %d, return: %d",
						__func__, hdr->ip_type, rc);
				return rc;
			}

			buf_ptr += payload_size;
			data_len -= payload_size;
		}
	}

	return rc;
}

static int32_t _iris_alloc_dtsi_cmd_buf(
		const struct device_node *np, const uint8_t *key, uint8_t **buf)
{
	int32_t cmd_size = 0;
	int32_t cmd_len = 0;
	const void *ret = NULL;

	ret = of_get_property(np, key, &cmd_len);
	if (!ret) {
		IRIS_LOGE("%s(), failed for parsing %s", __func__, key);
		return -EINVAL;
	}

	if (cmd_len % 4 != 0) {
		IRIS_LOGE("lenght = %d is not multpile of 4", cmd_len);
		return -EINVAL;
	}

	cmd_size = sizeof(char) * cmd_len;
	*buf = vzalloc(cmd_size);
	if (!*buf) {
		IRIS_LOGE("can not vzalloc memory");
		return  -ENOMEM;
	}

	return cmd_size;
}

static int32_t _iris_write_dtsi_cmd_to_buf(const struct device_node *np,
		const uint8_t *key, uint8_t **buf, int size)
{
	int32_t rc = 0;

	rc = of_property_read_u32_array(np, key,
			(uint32_t *)(*buf), size >> 2);
	if (rc != 0) {
		IRIS_LOGE("%s(%d), read array is not right", __func__, __LINE__);
		return -EINVAL;
	}

	return rc;
}

static void _iris_free_dtsi_cmd_buf(uint8_t **buf)
{
	if (*buf) {
		vfree(*buf);
		*buf = NULL;
	}
}

static void _iris_save_cmd_count(const struct iris_ip_index *pip_index,
		const int cmd_cnt)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	int32_t idx_type = _iris_get_ip_idx_type(pip_index);

	if (idx_type == IRIS_DTSI0_PIP_IDX
			|| idx_type == IRIS_DTSI1_PIP_IDX) { //CID101339
		if (cmd_cnt > pcfg->dtsi_cmds_cnt)
			pcfg->dtsi_cmds_cnt = cmd_cnt;
		return;
	}

	if (idx_type == IRIS_LUT_PIP_IDX) {
		pcfg->lut_cmds_cnt = cmd_cnt;
		return;
	}

	IRIS_LOGI("%s(), doesn't save count for type %#x pip index %p",
			__func__, idx_type, pip_index);
}

int32_t iris_attach_cmd_to_ipidx(const struct iris_data *data,
		int32_t data_cnt, struct iris_ip_index *pip_index)
{
	int32_t rc = 0;
	int32_t cmd_cnt = 0;

	rc = _iris_poll_cmd_lists(data, data_cnt, pip_index, &cmd_cnt);
	if (rc) {
		IRIS_LOGE("fail to parse dtsi/lut cmd list!");
		return rc;
	}

	_iris_save_cmd_count(pip_index, cmd_cnt);

	rc = _iris_create_ipidx(data, data_cnt, pip_index, cmd_cnt);

	return rc;
}

int32_t iris_parse_dtsi_cmd(const struct device_node *lightup_node, uint32_t cmd_index)
{
	int32_t rc = 0;
	int32_t cmd_size = 0;
	int32_t data_cnt = 0;
	uint8_t *dtsi_buf = NULL;
	struct iris_ip_index *pip_index = NULL;
	struct iris_data data[1];
	const uint8_t *key = "pxlw,iris-cmd-list";

	if (cmd_index == IRIS_DTSI1_PIP_IDX)
		key = "pxlw,iris-cmd-list-1";
	memset(data, 0x00, sizeof(data));

	// need to keep dtsi buf and release after used
	cmd_size = _iris_alloc_dtsi_cmd_buf(lightup_node, key, &dtsi_buf);
	if (cmd_size <= 0) {
		IRIS_LOGE("can not malloc space for dtsi cmd");
		return -ENOMEM;
	}

	rc = _iris_write_dtsi_cmd_to_buf(lightup_node, key, &dtsi_buf, cmd_size);
	if (rc) {
		IRIS_LOGE("cant not write dtsi cmd to buf");
		goto FREE_DTSI_BUF;
	}
	data[0].buf = dtsi_buf;
	data[0].size = cmd_size;

	pip_index = iris_get_ip_idx(cmd_index);
	data_cnt = ARRAY_SIZE(data);
	rc = iris_attach_cmd_to_ipidx(data, data_cnt, pip_index);

FREE_DTSI_BUF:
	_iris_free_dtsi_cmd_buf(&dtsi_buf);

	return rc;
}

static void _iris_add_cmd_seq(struct iris_ctrl_opt *ctrl_opt,
		int item_cnt, const uint8_t *pdata)
{
	int32_t i = 0;
	uint8_t ip = 0;
	uint8_t opt_id = 0;
	uint8_t skip_last = 0;
	const int32_t span = 3;

	for (i = 0; i < item_cnt; i++) {
		ip = pdata[span * i];
		opt_id = pdata[span * i + 1];
		skip_last = pdata[span * i + 2];

		ctrl_opt[i].ip = ip & 0xff;
		ctrl_opt[i].opt_id = opt_id & 0xff;
		ctrl_opt[i].skip_last = skip_last & 0xff;

		if (IRIS_IF_LOGV()) {
			IRIS_LOGE("ip = %d opt = %d  skip=%d",
					ip, opt_id, skip_last);
		}
	}
}

static int32_t _iris_alloc_cmd_seq(
		struct iris_ctrl_seq  *pctrl_seq, int32_t seq_cnt)
{
	pctrl_seq->ctrl_opt = vmalloc(seq_cnt * sizeof(struct iris_ctrl_seq));
	if (pctrl_seq->ctrl_opt == NULL) {
		IRIS_LOGE("can not malloc space for pctrl opt");
		return -ENOMEM;
	}
	pctrl_seq->cnt = seq_cnt;

	return 0;
}

static int32_t _iris_parse_cmd_seq_data(struct device_node *np,
		const uint8_t *key, const uint8_t **pval)
{
	const uint8_t *pdata = NULL;
	int32_t item_cnt = 0;
	int32_t seq_cnt = 0;
	int32_t span = 3;

	pdata = of_get_property(np, key, &item_cnt);
	if (!pdata) {
		IRIS_LOGE("%s %s is error", __func__, key);
		return -EINVAL;
	}

	seq_cnt =  (item_cnt / span);
	if (item_cnt == 0 || item_cnt != span * seq_cnt) {
		IRIS_LOGE("parse %s len is not right = %d", key, item_cnt);
		return -EINVAL;
	}

	*pval = pdata;

	return seq_cnt;
}

static int32_t _iris_parse_cmd_seq_common(
		struct device_node *np, const uint8_t *pre_key,
		const uint8_t *key, struct iris_ctrl_seq *pctrl_seq)
{
	int32_t pre_seq_cnt = 0;
	int32_t seq_cnt = 0;
	int32_t sum = 0;
	int32_t rc = 0;
	const uint8_t *pdata = NULL;
	const uint8_t *pre_pdata = NULL;

	pre_seq_cnt = _iris_parse_cmd_seq_data(np, pre_key, &pre_pdata);
	if (pre_seq_cnt <= 0)
		return -EINVAL;

	seq_cnt = _iris_parse_cmd_seq_data(np, key, &pdata);
	if (seq_cnt <= 0)
		return -EINVAL;

	sum = pre_seq_cnt + seq_cnt;

	rc = _iris_alloc_cmd_seq(pctrl_seq, sum);
	if (rc != 0)
		return rc;

	_iris_add_cmd_seq(pctrl_seq->ctrl_opt, pre_seq_cnt, pre_pdata);
	_iris_add_cmd_seq(&pctrl_seq->ctrl_opt[pre_seq_cnt], seq_cnt, pdata);

	return rc;
}

static int32_t _iris_parse_cmd_seq(
		struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t rc = 0;
	uint8_t *pre0_key = "pxlw,iris-lightup-sequence-pre0";
	uint8_t *pre1_key = "pxlw,iris-lightup-sequence-pre1";
	uint8_t *key = "pxlw,iris-lightup-sequence";

	rc = _iris_parse_cmd_seq_common(np, pre0_key, key, pcfg->ctrl_seq);
	if (rc != 0)
		return rc;

	return _iris_parse_cmd_seq_common(np, pre1_key, key, pcfg->ctrl_seq + 1);
}

int32_t iris_parse_optional_seq(struct device_node *np, const uint8_t *key,
		struct iris_ctrl_seq *pseq)
{
	int32_t rc = 0;
	int32_t seq_cnt = 0;
	const uint8_t *pdata = NULL;

	seq_cnt = _iris_parse_cmd_seq_data(np, key, &pdata);
	if (seq_cnt <= 0) {
		IRIS_LOGI("%s(), [optional] without sequence for %s, seq_cnt %d",
				__func__, key, seq_cnt);
		return 0;
	}

	rc = _iris_alloc_cmd_seq(pseq, seq_cnt);
	if (rc != 0) {
		IRIS_LOGE("%s(), failed to alloc for %s seq, return %d",
				__func__, key, rc);
		return rc;
	}

	_iris_add_cmd_seq(pseq->ctrl_opt, seq_cnt, pdata);

	return rc;
}

/*use for debug cont-splash lk part*/
static int32_t _iris_parse_cont_splash_cmd_seq(
		struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t rc = 0;
	uint8_t *pre0_key = "pxlw,iris-lightup-sequence-pre0";
	uint8_t *pre1_key = "pxlw,iris-lightup-sequence-pre1";
	uint8_t *key = "pxlw,iris-lightup-sequence-cont-splash";

	rc = _iris_parse_cmd_seq_common(np, pre0_key, key, pcfg->ctrl_seq_cs);
	if (rc != 0)
		return rc;

	return _iris_parse_cmd_seq_common(np, pre1_key,
			key, pcfg->ctrl_seq_cs + 1);
}

static int32_t _iris_parse_tx_mode(
		struct device_node *np,
		struct dsi_panel *panel, struct iris_cfg *pcfg)
{
	int32_t rc = 0;
	u8 tx_mode;

	pcfg->rx_mode = panel->panel_mode;
	pcfg->tx_mode = panel->panel_mode;
	IRIS_LOGE("%s, panel_mode = %d", __func__, panel->panel_mode);
	rc = of_property_read_u8(np, "pxlw,iris-tx-mode", &tx_mode);
	if (!rc) {
		IRIS_LOGE("get property: pxlw, iris-tx-mode: %d", tx_mode);
		//pcfg->tx_mode = tx_mode;
	}
	if (pcfg->rx_mode == pcfg->tx_mode)
		pcfg->pwil_mode = PT_MODE;
	else
		pcfg->pwil_mode = RFB_MODE;

	IRIS_LOGI("%s(), pwil mode: %d", __func__, pcfg->pwil_mode);
	return 0;
}

static int _iris_parse_pwr_entries(struct dsi_display *display)
{
	int32_t rc = 0;
	char *supply_name = NULL;
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (!display || !display->panel)
		return -EINVAL;

	if (!strcmp(display->panel->type, "primary")) {
		supply_name = "qcom,iris-supply-entries";

		rc = dsi_pwr_of_get_vreg_data(&display->panel->utils,
				&pcfg->iris_power_info, supply_name);
		if (rc) {
			rc = -EINVAL;
			IRIS_LOGE("%s pwr enters error", __func__);
		}
	}
	return rc;
}

static void __cont_splash_work_handler(struct work_struct *work)
{
	// struct iris_cfg *pcfg = iris_get_cfg();
	// bool done = false;

	// do {
	//	done = iris_abypass_switch_proc(pcfg->display, ANALOG_BYPASS_MODE, false, true);
	//	if (done)
	//		done = iris_abypass_switch_proc(pcfg->display, PASS_THROUGH_MODE, false, true);
	// } while (!done);
}

int iris_parse_param(struct dsi_display *display)
{
	int32_t rc = 0;
	struct device_node *lightup_node = NULL;
	struct iris_cfg *pcfg = iris_get_cfg();

	pcfg->valid = PARAM_EMPTY;	/* empty */

	IRIS_LOGI("%s(%d), enter.", __func__, __LINE__);
	if (!display || !display->pdev->dev.of_node || !display->panel_node) {
		IRIS_LOGE("the param is null");
		return -EINVAL;
	}

	if (display->panel->is_secondary) {
		// Copy valid flag to 2nd iris cfg.
		struct iris_cfg *pcfg1 = iris_get_cfg_by_index(DSI_PRIMARY);

		pcfg->valid = pcfg1->valid;
		return 0;
	}

	spin_lock_init(&pcfg->iris_1w_lock);
	mutex_init(&pcfg->lb_mutex);
	init_completion(&pcfg->frame_ready_completion);

	lightup_node = of_parse_phandle(display->pdev->dev.of_node, "pxlw,iris-lightup-config", 0);
	if (!lightup_node) {
		IRIS_LOGE("%s(), failed to find lightup node", __func__);
		return -EINVAL;
	}
	IRIS_LOGI("%s(), lightup node: %s", __func__, lightup_node->name);

	rc = _iris_parse_split_pkt_info(lightup_node, pcfg);
	if (rc) {
		/*use 64 split packet and do not add last for every packet.*/
		pcfg->split_pkt_size = 64;
		pcfg->add_last_flag = 0;
	}

	iris_parse_color_temp_range(lightup_node, pcfg);

	rc = iris_parse_dtsi_cmd(lightup_node, IRIS_DTSI0_PIP_IDX);
	if (rc) {
		IRIS_LOGE("%s, parse cmds list failed", __func__);
		return -EINVAL;
	}

	rc = _iris_parse_cmd_seq(lightup_node, pcfg);
	if (rc) {
		IRIS_LOGE("parse cmd seq error");
		return -EINVAL;
	}

	rc = iris_parse_timing_switch_info(lightup_node, pcfg);
	if (rc)
		IRIS_LOGI("%s, [optional] have not timing switch info", __func__);

	rc = iris_parse_default_pq_param(lightup_node, pcfg);
	if (rc) {
		IRIS_LOGE("parse pq init error");
		return -EINVAL;
	}

	rc = _iris_parse_panel_type(lightup_node, pcfg);
	if (rc) {
		IRIS_LOGE("parse panel type error");
		return -EINVAL;
	}

	_iris_parse_lut_mode(lightup_node, pcfg);

	rc = _iris_parse_chip_ver(lightup_node, pcfg);
	if (rc) {
		IRIS_LOGE("parse chip ver error");
		return -EINVAL;
	}

	rc = iris_parse_lp_ctrl(lightup_node, pcfg);
	if (rc) {
		IRIS_LOGE("parse low power control error");
		return -EINVAL;
	}

	rc = _iris_parse_cont_splash_cmd_seq(lightup_node, pcfg);
	if (rc) {
		IRIS_LOGE("parse cont splash cmd seq error");
		return -EINVAL;
	}

	rc = iris_parse_frc_setting(lightup_node, pcfg);
	if (rc)
		IRIS_LOGE("FRC not ready!");

	rc = _iris_parse_tx_mode(lightup_node, display->panel, pcfg);
	if (rc)
		IRIS_LOGE("no set iris tx mode!");

	rc = _iris_parse_pwr_entries(display);
	if (rc)
		IRIS_LOGE("pwr entries error\n");

	iris_parse_loopback_info(lightup_node, pcfg);

	INIT_WORK(&pcfg->cont_splash_work, __cont_splash_work_handler);

	pcfg->valid = PARAM_PARSED;	/* parse ok */
	IRIS_LOGI("%s(%d), exit.", __func__, __LINE__);

	return 0;
}

struct iris_pq_ipopt_val *iris_get_cur_ipopt_val(uint8_t ip)
{
	int i = 0;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_pq_init_val *pinit_val = &(pcfg->pq_init_val);

	for (i = 0; i < pinit_val->ip_cnt; i++) {
		struct iris_pq_ipopt_val *pq_ipopt_val = pinit_val->val + i;

		if (ip == pq_ipopt_val->ip)
			return pq_ipopt_val;
	}

	return NULL;
}

static void _iris_reset_out_cmds(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	int sum = pcfg->dtsi_cmds_cnt + pcfg->lut_cmds_cnt;

	memset(pcfg->iris_cmds.iris_cmds_buf, 0x00,
			sum * sizeof(struct dsi_cmd_desc));
	pcfg->iris_cmds.cmds_index = 0;
}

static int32_t _iris_init_cmd_comp(int32_t ip,
		int32_t opt_index, struct iris_cmd_comp *pcmd_comp)
{
	struct iris_ip_opt *opt = NULL;

	if (!_iris_is_valid_ip(ip)) {
		IRIS_LOGE("%s(), invalid ip: %#x", __func__, ip);
		return -EINVAL;
	}

	opt = iris_find_ip_opt(ip, opt_index);
	if (!opt) {
		IRIS_LOGE("%s(), can not find popt, ip: %#x, opt: %#x",
				__func__, ip, opt_index);
		return -EINVAL;
	}

	pcmd_comp->cmd = opt->cmd;
	pcmd_comp->cnt = opt->cmd_cnt;
	pcmd_comp->link_state = opt->link_state;
	IRIS_LOGV("%s(), opt count: %d, link state: %#x",
			__func__, pcmd_comp->cnt, pcmd_comp->link_state);

	return 0;
}

void iris_print_desc_cmds(struct dsi_cmd_desc *p, int cmd_cnt, int state)
{
	int i = 0;
	int j = 0;
	int msg_len = 0;
	int dlen = 0;
	uint8_t *arr = NULL;
	uint8_t *ptr = NULL;
	uint8_t *ptr_tx = NULL;
	struct dsi_cmd_desc *pcmd = NULL;
	int str_len = 0;//CID99296

	IRIS_LOGI("%s(), cmd len: %d, state: %s", __func__, cmd_cnt,
			(state == DSI_CMD_SET_STATE_HS) ? "high speed" : "low power");

	for (i = 0; i < cmd_cnt; i++) {
		pcmd = p + i;
		dlen = pcmd->msg.tx_len;
		msg_len = 3 * dlen + 23; //3* 7(dchdr) + 1(\n) + 1 (0)
		arr = vmalloc(msg_len * sizeof(uint8_t));
		if (!arr) {
			IRIS_LOGE("%s(), fail to malloc space", __func__);
			return;
		}
		memset(arr, 0x00, sizeof(uint8_t) * msg_len);

		ptr = arr;
		ptr_tx = (uint8_t *) pcmd->msg.tx_buf;
		str_len = snprintf(ptr, msg_len, "\" %02X", pcmd->msg.type);
		ptr += str_len;
		for (j = 0; j < dlen; j++) {
			str_len = snprintf(ptr, msg_len - (ptr - arr), " %02X", ptr_tx[j]);
			ptr += str_len;
		}
		snprintf(ptr, msg_len - (ptr - arr), "\\n\"");
		IRIS_LOGE("%s", arr);

		if (pcmd->post_wait_ms > 0)
			IRIS_LOGE("\" FF %02X\\n\"", pcmd->post_wait_ms);

		vfree(arr);
		arr = NULL;
	}
}

static void _iris_print_spec_cmds(struct dsi_cmd_desc *p, int cmd_cnt)
{
	int i = 0;
	int j = 0;
	int value_count = 0;
	int print_count = 0;
	struct dsi_cmd_desc *pcmd = NULL;
	uint32_t *pval = NULL;

	if (IRIS_IF_NOT_LOGD())
		return;

	IRIS_LOGD("%s(), package count in cmd list: %d", __func__, cmd_cnt);
	for (i = 0; i < cmd_cnt; i++) {
		pcmd = p + i;
		value_count = pcmd->msg.tx_len/sizeof(uint32_t);
		print_count = value_count;
		if (value_count > 16)
			print_count = 16;
		pval = (uint32_t *)pcmd->msg.tx_buf;
		if (i == 0 || i == cmd_cnt-1) {
			IRIS_LOGD("%s(), package: %d, type: 0x%02x, last: %s, channel: 0x%02x, flags: 0x%04x, wait: 0x%02x, send size: %zu.",
					__func__, i,
					pcmd->msg.type, pcmd->last_command?"true":"false", pcmd->msg.channel,
					pcmd->msg.flags, pcmd->post_wait_ms, pcmd->msg.tx_len);

			if (IRIS_IF_NOT_LOGV())
				continue;

			IRIS_LOGV("%s(), payload value count: %d, print count: %d, ocp type: 0x%08x, addr: 0x%08x",
					__func__, value_count, print_count, pval[0], pval[1]);
			for (j = 2; j < print_count; j++)
				IRIS_LOGV("0x%08x", pval[j]);

			if (i == cmd_cnt-1 && value_count > 4 && print_count != value_count) {
				IRIS_LOGV("%s(), payload tail: 0x%08x, 0x%08x, 0x%08x, 0x%08x.", __func__,
						pval[value_count-4], pval[value_count-3],
						pval[value_count-2], pval[value_count-1]);
			}
		}
	}
}

static void _iris_print_dtsi_cmds_for_lk(struct dsi_cmd_desc *cmds,
		int32_t cnt, int32_t wait, int32_t link_state)
{
	if (iris_get_cont_splash_type() != IRIS_CONT_SPLASH_LK)
		return;

	//restore the last cmd wait time
	if (wait != 0)
		cmds[cnt-1].post_wait_ms = 1;

	iris_print_desc_cmds(cmds, cnt, link_state);
}

static int32_t _iris_i2c_send_ocp_cmds(struct dsi_panel *panel,
		struct iris_cmd_comp *pcmd_comp)
{
	int i = 0;
	int ret = 0;
	bool is_burst;
	bool is_allburst = true;
	int len = 0;
	uint32_t *payload = NULL;
	uint32_t header = 0;
	struct iris_i2c_msg *msg = NULL;
	uint32_t iris_i2c_msg_num = 0;

	for (i = 0; i < pcmd_comp->cnt; i++) {
		is_burst = _iris_is_direct_bus(pcmd_comp->cmd[i].msg.tx_buf);
		if (is_burst == false) {
			is_allburst = false;
			break;
		}
	}

	if (!is_allburst) {
		for (i = 0; i < pcmd_comp->cnt; i++) {
			header = *(uint32_t *)(pcmd_comp->cmd[i].msg.tx_buf);
			payload = (uint32_t *)(pcmd_comp->cmd[i].msg.tx_buf) + 1;
			len = (pcmd_comp->cmd[i].msg.tx_len >> 2) - 1;
			is_burst = _iris_is_direct_bus(pcmd_comp->cmd[i].msg.tx_buf);
			if (is_burst) {
				if ((header & 0x0f) == 0x0c)
					iris_i2c_direct_write(payload, len-1, header);
				else
					iris_i2c_ocp_burst_write(payload, len-1);
			} else {
				iris_i2c_ocp_single_write(payload, len/2);
			}
		}

		return 0;
	}

	iris_i2c_msg_num = pcmd_comp->cnt;
	msg = vmalloc(sizeof(struct iris_i2c_msg) * iris_i2c_msg_num + 1);
	if (msg == NULL) {
		IRIS_LOGE("[iris] %s: allocate memory fails", __func__);
		return -EINVAL;
	}
	for (i = 0; i < iris_i2c_msg_num; i++) {
		msg[i].payload = (uint32_t *)(pcmd_comp->cmd[i].msg.tx_buf) + 1;
		msg[i].len = (pcmd_comp->cmd[i].msg.tx_len >> 2) - 1;
		msg[i].base_addr = *(uint32_t *)(pcmd_comp->cmd[i].msg.tx_buf);
	}
	ret = iris_i2c_burst_write(msg, iris_i2c_msg_num);
	vfree(msg);

	return ret;
}


static int32_t _iris_dsi_send_ocp_cmds(struct dsi_panel *panel, struct iris_cmd_comp *pcmd_comp)
{
	int ret;
	uint32_t wait = 0;
	struct dsi_cmd_desc *cmd = NULL;

	if (!pcmd_comp) {
		IRIS_LOGE("%s(), cmd list is null.", __func__);
		return -EINVAL;
	}

	/*use us than ms*/
	cmd = pcmd_comp->cmd + pcmd_comp->cnt - 1;
	wait = cmd->post_wait_ms;
	if (wait)
		cmd->post_wait_ms = 0;

	ret = iris_dsi_send_cmds(panel, pcmd_comp->cmd,
			pcmd_comp->cnt, pcmd_comp->link_state);
	if (wait)
		udelay(wait);

	_iris_print_spec_cmds(pcmd_comp->cmd, pcmd_comp->cnt);
	_iris_print_dtsi_cmds_for_lk(pcmd_comp->cmd, pcmd_comp->cnt, wait, pcmd_comp->link_state);

	return ret;
}

int32_t _iris_send_cmds(struct dsi_panel *panel,
		struct iris_cmd_comp *pcmd_comp, uint8_t path)
{
	int32_t ret = 0;

	IRIS_LOGD("%s,%d: path = %d", __func__, __LINE__, path);

	if (!pcmd_comp) {
		IRIS_LOGE("cmd list is null");
		return -EINVAL;
	}

	if (path == PATH_DSI)
		ret = _iris_dsi_send_ocp_cmds(panel, pcmd_comp);
	else if (path == PATH_I2C)
		ret = _iris_i2c_send_ocp_cmds(panel, pcmd_comp);
	else
		ret = -EINVAL;

	return ret;
}

static void _iris_send_panel_cmd(struct dsi_panel *panel,
		struct dsi_panel_cmd_set *cmds)
{
	if (!cmds || !cmds->count) {
		IRIS_LOGE("cmds = %p or cmd_cnt = 0", cmds);
		return;
	}

	iris_pt_send_panel_cmd(panel, cmds);
}

int32_t iris_send_ipopt_cmds(int32_t ip, int32_t opt_id)
{
	int32_t rc = 0;
	struct iris_cmd_comp cmd_comp;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	IRIS_LOGD("%s(), ip: %#x, opt: %#x.", __func__, ip, opt_id);
	rc = _iris_init_cmd_comp(ip, opt_id, &cmd_comp);
	if (rc) {
		IRIS_LOGE("%s(), can not find in seq for ip: 0x%02x opt: 0x%02x.",
				__func__, ip, opt_id);
		return rc;
	}

	return _iris_send_cmds(pcfg->panel, &cmd_comp, PATH_DSI);
}

/**********************************************
 * the API will only be called when suspend/resume and boot up.
 *
 ***********************************************/
static void _iris_send_spec_lut(uint8_t lut_table, uint8_t lut_idx)
{
	if (lut_table == AMBINET_HDR_GAIN
			|| lut_table == AMBINET_SDR2HDR_LUT)
		return;

	if (lut_table == DBC_LUT && lut_idx < CABC_DLV_OFF)
		iris_send_lut(lut_table, lut_idx, 1);

	iris_send_lut(lut_table, lut_idx, 0);
}

static void _iris_send_new_lut(uint8_t lut_table, uint8_t lut_idx)
{
	uint8_t dbc_lut_index = 0;

	if (lut_table == DBC_LUT)
		dbc_lut_index = iris_get_dbc_lut_index();

	iris_send_lut(lut_table, lut_idx, dbc_lut_index);
}

static void _iris_update_cmds(struct iris_cmd_comp *pcmd_comp, int32_t link_state)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	_iris_reset_out_cmds();

	memset(pcmd_comp, 0x00, sizeof(*pcmd_comp));
	pcmd_comp->cmd = pcfg->iris_cmds.iris_cmds_buf;
	pcmd_comp->link_state = link_state;
	pcmd_comp->cnt = pcfg->iris_cmds.cmds_index;
}

static void _iris_update_desc_last(struct dsi_cmd_desc *pcmd,
		int count, bool last_cmd)
{
	int i = 0;

	for (i = 0; i < count; i++)
		pcmd[i].last_command = last_cmd;
}

static void _iris_add_last_pkt(struct dsi_cmd_desc *cmd, int cmd_cnt)
{
	_iris_update_desc_last(cmd, cmd_cnt, false);
	_iris_update_desc_last(cmd + cmd_cnt - 1, 1, true);
}

static void _iris_update_mult_pkt_last(struct dsi_cmd_desc *cmd,
		int cmd_cnt, int skip_last)
{
	int i = 0;
	int pos = 0;
	int num = 0;
	int surplus = 0;
	int span = 0;
	static int sum;
	struct iris_cfg *pcfg = iris_get_cfg();
	int prev = sum;

	sum += cmd_cnt;

	span = pcfg->add_last_flag;

	num = sum / span;
	surplus = sum  - num * span;

	for (i = 0; i < num; i++) {
		if (i == 0) {
			_iris_add_last_pkt(cmd, span - prev);
		} else {
			pos = i * span - prev;
			_iris_add_last_pkt(cmd + pos, span);
		}
	}
	pos = cmd_cnt - surplus;

	if (skip_last) {
		_iris_update_desc_last(cmd + pos, surplus, false);
		sum = surplus;
	} else {
		_iris_add_last_pkt(cmd + pos, surplus);
		sum = 0;
	}
}

static int _iris_set_pkt_last(struct dsi_cmd_desc *cmd,
		int cmd_cnt, int skip_last)
{
	int32_t ret = 0;
	struct iris_cfg *pcfg = iris_get_cfg();
	int32_t add_last_flag = pcfg->add_last_flag;

	switch (add_last_flag) {
	case DSI_CMD_ONE_LAST_FOR_MULT_IPOPT:
		_iris_update_desc_last(cmd, cmd_cnt, false);
		_iris_update_desc_last(cmd + cmd_cnt - 1, 1, true);
		break;
	case DSI_CMD_ONE_LAST_FOR_ONE_IPOPT:
		/*only add the last packet*/
		_iris_update_desc_last(cmd, cmd_cnt - 1, false);
		_iris_update_desc_last(cmd + cmd_cnt - 1, 1, true);

		break;
	case DSI_CMD_ONE_LAST_FOR_ONE_PKT:
		/*add all packets*/
		_iris_update_desc_last(cmd, cmd_cnt, true);
		break;
	default:
		_iris_update_mult_pkt_last(cmd, cmd_cnt, skip_last);
		break;
	}

	return ret;
}

static int _iris_send_lut_pkt(
		struct iris_ctrl_opt *popt, struct iris_cmd_comp *pcomp,
		bool is_update, uint8_t path)
{
	int32_t cur = 0;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	uint8_t ip = popt->ip;
	uint8_t opt_id = popt->opt_id;
	int32_t skip_last = popt->skip_last;
	int32_t prev = pcomp->cnt;

	IRIS_LOGD("%s(), ip: %#x opt: %#x, skip last: %d, update: %s",
			__func__, ip, opt_id, skip_last, is_update ? "true" : "false");

	pcfg->iris_cmds.cmds_index = prev;
	if (is_update)
		_iris_send_new_lut(ip, opt_id);
	else
		_iris_send_spec_lut(ip, opt_id);

	cur = pcfg->iris_cmds.cmds_index;
	if (cur == prev) {
		IRIS_LOGD("lut table is empty for ip: %02x opt: %02x",
				popt->ip, opt_id);
		return 0;
	}

	pcomp->cnt = cur;

	_iris_set_pkt_last(pcomp->cmd + prev, cur - prev, skip_last);
	if (!skip_last) {
		_iris_send_cmds(pcfg->panel, pcomp, path);
		_iris_update_cmds(pcomp, pcomp->link_state);
	}

	return 0;
}

static int _iris_send_dtsi_pkt(
		struct iris_ctrl_opt *pip_opt, struct iris_cmd_comp *pcomp, uint8_t path)
{
	uint8_t ip = 0;
	uint8_t opt_id = 0;
	int32_t flag = 0;
	int32_t prev = 0;
	int32_t cur = 0;
	int32_t skip_last = 0;
	int32_t add_last_flag = 0;
	int32_t rc = 0;
	struct iris_cfg *pcfg = NULL;
	struct iris_cmd_comp comp_priv;

	pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	ip = pip_opt->ip;
	opt_id = pip_opt->opt_id;
	skip_last = pip_opt->skip_last;
	add_last_flag = pcfg->add_last_flag;

	IRIS_LOGD("%s(), ip: %#x opt: %#x, skip last: %d.",
			__func__, ip, opt_id, skip_last);

	/*get single/multiple selection(s) according to option of ip*/
	rc = _iris_init_cmd_comp(ip, opt_id, &comp_priv);
	if (rc) {
		IRIS_LOGE("%s(), invalid ip: %#x opt: %#x.", __func__, ip, opt_id);
		return -EINVAL;
	}

	if (pcomp->cnt == 0)
		pcomp->link_state = comp_priv.link_state;
	else if (comp_priv.link_state != pcomp->link_state)
		flag = 1; /*send link state different packet.*/

	if (flag == 0) {
		prev = pcomp->cnt;
		/*move single/multiples selection to one command*/

		memcpy(pcomp->cmd + pcomp->cnt, comp_priv.cmd,
				comp_priv.cnt * sizeof(*comp_priv.cmd));
		pcomp->cnt += comp_priv.cnt;

		cur = pcomp->cnt;
		_iris_set_pkt_last(pcomp->cmd + prev, cur - prev, skip_last);
	}

	/* if need to send or the last packet of sequence,
	 * it should send out to the MIPI
	 */
	if (!skip_last || flag == 1) {
		_iris_send_cmds(pcfg->panel, pcomp, path);
		_iris_update_cmds(pcomp, pcomp->link_state);
	}

	return 0;
}

void iris_send_pkt(struct iris_ctrl_opt *arr, int seq_cnt,
		struct iris_cmd_comp *pcmd_comp)
{
	int i = 0;
	uint8_t ip = 0;
	uint8_t opt_id = 0;
	int32_t rc = -1;

	for (i = 0; i < seq_cnt; i++) {
		ip = arr[i].ip;
		opt_id = arr[i].opt_id;
		IRIS_LOGV("%s(), ip:%0x opt:%0x", __func__, ip, opt_id);

		/*lut table*/
		if (_iris_is_lut(ip))
			rc = _iris_send_lut_pkt(arr + i, pcmd_comp, false, PATH_DSI);
		else
			rc = _iris_send_dtsi_pkt(arr + i, pcmd_comp, PATH_DSI);

		if (rc)
			IRIS_LOGE("%s(), [FATAL ERROR] invalid ip: %0x opt: %0x", __func__, ip, opt_id);
	}
}

void iris_send_assembled_pkt(struct iris_ctrl_opt *arr, int seq_cnt)
{
	struct iris_cmd_comp cmd_comp;

	_iris_update_cmds(&cmd_comp, DSI_CMD_SET_STATE_HS);
	iris_send_pkt(arr, seq_cnt, &cmd_comp);
}

static void _iris_send_lightup_pkt(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_ctrl_seq *pseq = _iris_get_ctrl_seq(pcfg);

	iris_send_assembled_pkt(pseq->ctrl_opt, pseq->cnt);
}

void iris_init_update_ipopt(
		struct iris_update_ipopt *popt, uint8_t ip,
		uint8_t opt_old, uint8_t opt_new, uint8_t skip_last)
{
	popt->ip = ip;
	popt->opt_old = opt_old;
	popt->opt_new = opt_new;
	popt->skip_last = skip_last;
}

int iris_init_update_ipopt_t(struct iris_update_ipopt *popt, int max_cnt,
		uint8_t ip, uint8_t opt_old, uint8_t opt_new, uint8_t skip_last)
{
	int i  = 0;
	int cnt = 0;

	for (i = 0; i < max_cnt; i++) {
		if (popt[i].ip == 0xff)
			break;
	}

	if (i >= max_cnt) {
		IRIS_LOGE("%s(), no empty space to install ip: %#x, opt old: %#x, opt new: %#x",
				__func__, ip, opt_old, opt_new);
		return -EINVAL;
	}

	iris_init_update_ipopt(&popt[i],  ip, opt_old, opt_new, skip_last);
	cnt = i + 1;

	return cnt;
}

static int _iris_read_chip_id(void)
{
	// uint32_t sys_pll_ro_status = 0xf0000010;
	struct iris_cfg *pcfg = iris_get_cfg();

	if (pcfg->chip_value[0] != 0)
		pcfg->chip_value[1] = iris_ocp_read(pcfg->chip_value[0], DSI_CMD_SET_STATE_HS);

	// FIXME: if chip version is set by sw, skip hw read chip id.
	// if (pcfg->chip_ver == IRIS3_CHIP_VERSION)
	// 	pcfg->chip_id = (iris_ocp_read(sys_pll_ro_status, DSI_CMD_SET_STATE_HS)) & 0xFF;
	// else
	pcfg->chip_id = 0;

	IRIS_LOGI("%s(), chip version: %#x, chip id: %#x",
			__func__, pcfg->chip_ver, pcfg->chip_id);

	return pcfg->chip_id;
}

static void _iris_clean_status(void)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	iris_clean_frc_status(pcfg);
}

void iris_free_ipopt_buf(uint32_t ip_type)
{
	int ip_index = 0;
	int opt_index = 0;
	uint32_t desc_index = 0;
	int ip_cnt = IRIS_IP_CNT;
	struct dsi_cmd_desc *pdesc_addr = NULL;
	struct iris_ip_index *pip_index = iris_get_ip_idx(ip_type);

	if (ip_type == IRIS_LUT_PIP_IDX)
		ip_cnt = LUT_IP_END - LUT_IP_START;

	for (ip_index = 0; ip_index < ip_cnt; ip_index++) {
		if (pip_index[ip_index].opt_cnt == 0 || pip_index[ip_index].opt == NULL)
			continue;

		for (opt_index = 0; opt_index < pip_index[ip_index].opt_cnt; opt_index++) {
			if (pip_index[ip_index].opt[opt_index].cmd_cnt == 0
					|| pip_index[ip_index].opt[opt_index].cmd == NULL)
				continue;

			/* get desc cmd start address */
			if (pdesc_addr == NULL || pip_index[ip_index].opt[opt_index].cmd < pdesc_addr)
				pdesc_addr = pip_index[ip_index].opt[opt_index].cmd;

			for (desc_index = 0; desc_index < pip_index[ip_index].opt[opt_index].cmd_cnt; desc_index++) {
				if (pip_index[ip_index].opt[opt_index].cmd[desc_index].msg.tx_buf == NULL
						|| pip_index[ip_index].opt[opt_index].cmd[desc_index].msg.tx_len == 0)
					continue;

				/* free cmd payload, which alloc in "_iris_write_cmd_payload()" */
				vfree(pip_index[ip_index].opt[opt_index].cmd[desc_index].msg.tx_buf);
				pip_index[ip_index].opt[opt_index].cmd[desc_index].msg.tx_buf = NULL;
			}

			/* set each desc cmd to NULL first */
			pip_index[ip_index].opt[opt_index].cmd = NULL;
		}

		/* free opt buffer for each ip, which alloc in "_iris_alloc_pip_buf()" */
		kvfree(pip_index[ip_index].opt);
		pip_index[ip_index].opt = NULL;
		pip_index[ip_index].opt_cnt = 0;
	}

	/* free desc cmd buffer, which alloc in "_iris_alloc_desc_buf()", desc
	 * cmd buffer is continus memory, so only free once on start address
	 */
	if (pdesc_addr != NULL) {
		IRIS_LOGI("%s(), free desc cmd buffer %p, type %#x", __func__, pdesc_addr, ip_type);
		vfree(pdesc_addr);
		pdesc_addr = NULL;
	}
}

void iris_free_seq_space(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	/* free cmd to sent buffer, which alloc in "iris_alloc_seq_space()" */
	if (pcfg->iris_cmds.iris_cmds_buf != NULL) {
		IRIS_LOGI("%s(), free %p", __func__, pcfg->iris_cmds.iris_cmds_buf);
		vfree(pcfg->iris_cmds.iris_cmds_buf);
		pcfg->iris_cmds.iris_cmds_buf = NULL;
	}

}

void iris_alloc_seq_space(void)
{
	struct dsi_cmd_desc *pdesc = NULL;
	struct iris_cfg *pcfg = iris_get_cfg();
	int sum = pcfg->dtsi_cmds_cnt + pcfg->lut_cmds_cnt;

	IRIS_LOGI("%s(), seq = %u, lut = %u", __func__,
			pcfg->dtsi_cmds_cnt, pcfg->lut_cmds_cnt);

	sum *= sizeof(struct dsi_cmd_desc);
	pdesc = vmalloc(sum);
	if (!pdesc) {
		IRIS_LOGE("%s(), failed to alloc buffer", __func__);
		return;
	}
	pcfg->iris_cmds.iris_cmds_buf = pdesc;

	IRIS_LOGI("%s(), alloc %p", __func__, pcfg->iris_cmds.iris_cmds_buf);
	_iris_reset_out_cmds();

	// Need to init PQ parameters here for video panel.
	iris_pq_parameter_init();
}

static void _iris_load_mcu(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 values[2] = {0xF00000C8, 0x1};
	struct iris_ctrl_opt ctrl_opt;

	IRIS_LOGI("%s(%d): load and run mcu", __func__, __LINE__);
	ctrl_opt.ip = APP_CODE_LUT;
	ctrl_opt.opt_id = 0;
	ctrl_opt.skip_last = 0;

	iris_send_assembled_pkt(&ctrl_opt, 1);
	iris_ocp_write_mult_vals(2, values);
	pcfg->mcu_code_downloaded = true;
}

static void _iris_pre_lightup(struct dsi_panel *panel)
{
	static int num;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_update_ipopt popt[IP_OPT_MAX];
	int len = 0;
	bool high = false;

	if ((panel->cur_mode->timing.refresh_rate == HIGH_FREQ)
			&& (pcfg->panel->cur_mode->timing.v_active == FHD_H))
		high = true;

	iris_init_ipopt_ip(popt, IP_OPT_MAX);
	//sys pll
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_SYS, high ? 0xA1:0xA0, 0x1);
	//dtg
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_DTG, high ? 0x1:0x0, 0x1);
	//mipi tx
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_TX, high ? 0x4:0x0, 0x1);
	//mipi abp
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_RX, high ? 0xE1:0xE0, 0x0);
	_iris_update_pq_seq(popt, len);

	/*send rx cmds first with low power*/
	iris_send_ipopt_cmds(IRIS_IP_RX, high ? 0xF2 : 0xF1);

	//if (num == 0) {
	if (1) {
		/*read chip_id*/
		_iris_read_chip_id();
		num++;
	}

	iris_pq_parameter_init();
	iris_frc_parameter_init();
	_iris_clean_status();
}

void _iris_read_power_mode(struct dsi_panel *panel)
{
	char get_power_mode[1] = {0x0a};
	char read_cmd_rbuf[16] = {0};
	struct dsi_cmd_desc cmds = {
		{0, MIPI_DSI_DCS_READ, MIPI_DSI_MSG_REQ_ACK, 0, 0, sizeof(get_power_mode), get_power_mode, 1, read_cmd_rbuf}, 1, 0};
	struct dsi_panel_cmd_set cmdset = {
		.state = DSI_CMD_SET_STATE_HS,
		.count = 1,
		.cmds = &cmds,
	};
	struct iris_cfg *pcfg = iris_get_cfg();

	IRIS_LOGI("%s(%d), abyp mode: %d", __func__, __LINE__,
			pcfg->abypss_ctrl.abypass_mode);
	if (pcfg->abypss_ctrl.abypass_mode == ANALOG_BYPASS_MODE) {
		iris_dsi_send_cmds(panel, cmdset.cmds, cmdset.count, cmdset.state);
	} else {
		iris_dsi_send_cmds(panel, cmdset.cmds, cmdset.count, cmdset.state);
		IRIS_LOGE("[a]power mode: 0x%02x", read_cmd_rbuf[0]);
		read_cmd_rbuf[0] = 0;
		_iris_send_panel_cmd(panel, &cmdset);
	}
	pcfg->power_mode = read_cmd_rbuf[0];

	IRIS_LOGI("%s(), power mode: 0x%02x", __func__, pcfg->power_mode);
}

int iris_lightup(struct dsi_panel *panel,
		struct dsi_panel_cmd_set *on_cmds)
{
	ktime_t ktime0;
	ktime_t ktime1;
	uint32_t timeus0 = 0;
	uint32_t timeus1 = 0;
	uint8_t type = 0;
	struct dsi_display *display = to_dsi_display(panel->host);
	int rc;
	struct iris_cfg *pcfg = iris_get_cfg();

	IRIS_LOGI("%s(%d), mode: %s(%d) +++", __func__, __LINE__,
			pcfg->abypss_ctrl.abypass_mode == PASS_THROUGH_MODE ? "PT" : "ABYP",
			pcfg->abypss_ctrl.abypass_mode);

	ktime0 = ktime_get();
	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_ON);
	if (rc) {
		IRIS_LOGE("%s(), failed to enable all DSI clocks for display: %s, return: %d",
				__func__, display->name, rc);
	}

	rc = iris_display_cmd_engine_enable(display);
	if (rc) {
		IRIS_LOGE("%s(), failed to enable cmd engine for display: %s, return: %d",
				__func__, display->name, rc);
	}

	pcfg->add_last_flag = pcfg->add_on_last_flag;

	iris_set_pinctrl_state(pcfg, true);
	_iris_pre_lightup(panel);

	type = iris_get_cont_splash_type();

	/*use to debug cont splash*/
	if (type == IRIS_CONT_SPLASH_LK) {
		IRIS_LOGI("%s(%d), enter cont splash", __func__, __LINE__);
		_iris_send_cont_splash_pkt(IRIS_CONT_SPLASH_LK);
	} else {
		_iris_send_lightup_pkt();
		iris_scaler_filter_ratio_get();
		iris_update_gamma();
		if (!(pcfg->dual_test & 0x20))
			iris_dual_setting_switch(pcfg->dual_setting);
	}

	_iris_load_mcu();

	if (panel->bl_config.type == DSI_BACKLIGHT_PWM)
		iris_pwm_freq_set(panel->bl_config.pwm_period_usecs);

	ktime1 = ktime_get();
	if (on_cmds)
		_iris_send_panel_cmd(panel, on_cmds);

	if (type == IRIS_CONT_SPLASH_LK)
		IRIS_LOGI("%s(), exit cont splash", __func__);
	else
		/*continuous splahs should not use dma setting low power*/
		iris_lp_init();

	pcfg->add_last_flag = pcfg->add_pt_last_flag;

	rc = iris_display_cmd_engine_disable(display);
	if (rc) {
		IRIS_LOGE("%s(), failed to disable cmd engine for display: %s, return: %d",
				__func__, display->name, rc);
	}
	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_OFF);
	if (rc) {
		IRIS_LOGE("%s(), failed to disable all DSI clocks for display: %s, return: %d",
				__func__, display->name, rc);
	}

	pcfg->cur_fps_in_iris = panel->cur_mode->timing.refresh_rate;
	pcfg->cur_vres_in_iris = panel->cur_mode->timing.v_active;
	iris_update_frc_fps(pcfg->cur_fps_in_iris & 0xFF);

	timeus0 = (u32) ktime_to_us(ktime1) - (u32)ktime_to_us(ktime0);
	timeus1 = (u32) ktime_to_us(ktime_get()) - (u32)ktime_to_us(ktime1);
	IRIS_LOGI("%s() spend time0 %d us, time1 %d us.",
			__func__, timeus0, timeus1);

#ifdef IRIS_MIPI_TEST
	_iris_read_power_mode(panel);
#endif
	IRIS_LOGI("%s(), end +++", __func__);

	return 0;
}

int iris_enable(struct dsi_panel *panel, struct dsi_panel_cmd_set *on_cmds)
{
	int rc = 0;
	struct iris_cfg *pcfg = NULL;
#ifndef IRIS_HDK_DEV // skip preload
	int abyp_status_gpio;
	int prev_mode;
#endif
	int lightup_opt = iris_lightup_opt_get();

    pcfg = iris_get_cfg_by_index(panel->is_secondary ? DSI_SECONDARY : DSI_PRIMARY);

	if (panel->is_secondary) {
		if (pcfg->iris_osd_autorefresh) {
			IRIS_LOGI("reset iris_osd_autorefresh");
			iris_osd_autorefresh(0);
		}
		return rc;
	}

	iris_lp_preinit();

	pcfg->iris_initialized = false;

#ifndef IRIS_HDK_DEV // skip preload
	pcfg->next_fps_for_iris = panel->cur_mode->timing.refresh_rate;

	/* Special process for WQHD@120Hz */
	if (panel->cur_mode->timing.refresh_rate == HIGH_FREQ
			&& panel->cur_mode->timing.v_active == QHD_H) {
		/* Force Iris work in ABYP mode */
		pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE;
	}
#endif

	IRIS_LOGI("%s(), mode:%d, rate: %d, v: %d, on_opt:0x%x",
			__func__,
			pcfg->abypss_ctrl.abypass_mode,
			panel->cur_mode->timing.refresh_rate,
			panel->cur_mode->timing.v_active,
			lightup_opt);

	// if (pcfg->fod == true && pcfg->fod_pending) {
	//	iris_abyp_lp(1);
	//	pcfg->abyp_prev_mode = pcfg->abypss_ctrl.abypass_mode;
	//	pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE;
	//	pcfg->fod_pending = false;
	//	pcfg->initialized = false;
	//	IRIS_LOGD("[%s:%d] fod = 1 in init, ABYP prev mode: %d ABYP mode: %d",
	//		 __func__, __LINE__, pcfg->abyp_prev_mode, pcfg->abypss_ctrl.abypass_mode);
	//	return rc;
	// }

	/* support lightup_opt */
	if (lightup_opt & 0x1) {
		if (on_cmds != NULL)
			rc = iris_dsi_send_cmds(panel, on_cmds->cmds, on_cmds->count, on_cmds->state);
		IRIS_LOGI("%s(), force ABYP lightup.", __func__);
		return rc;
	}

#ifndef IRIS_HDK_DEV // skip preload
	prev_mode = pcfg->abypss_ctrl.abypass_mode;

	abyp_status_gpio = iris_exit_abyp(false);
	if (abyp_status_gpio == 1) {
		IRIS_LOGE("%s(), failed to exit abyp!", __func__);
		return rc;
	}
#endif

	if (pcfg->loop_back_mode == 1) {
		pcfg->loop_back_mode_res = iris_loop_back_verify();
		return rc;
	}

#ifndef IRIS_HDK_DEV // skip preload
	rc = iris_lightup(panel, NULL);
	pcfg->iris_initialized = true;
	if (on_cmds != NULL)
		rc = iris_pt_send_panel_cmd(panel, on_cmds);
	if (pcfg->lp_ctrl.esd_cnt > 0) /* restore brightness setting for esd */
		iris_panel_nits_set(0, true, 0);
	//iris_set_out_frame_rate(panel->cur_mode->timing.refresh_rate);
	pcfg->abypss_ctrl.abypass_mode = PASS_THROUGH_MODE;
#else
	if (on_cmds != NULL)
		rc = iris_dsi_send_cmds(panel, on_cmds->cmds, on_cmds->count, on_cmds->state);
#endif

#ifndef IRIS_HDK_DEV // skip preload
	//Switch back to ABYP mode if need
	if (prev_mode == ANALOG_BYPASS_MODE)
		iris_abypass_switch_proc(pcfg->display, ANALOG_BYPASS_MODE, false, true);
#endif

	return rc;
}

int iris_set_aod(struct dsi_panel *panel, bool aod)
{
	int rc = 0;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (!pcfg)
		return rc;

	if (panel->is_secondary)
		return rc;

	IRIS_LOGI("%s(%d), aod: %d", __func__, __LINE__, aod);
	if (pcfg->aod == aod) {
		IRIS_LOGI("[%s:%d] aod: %d no change", __func__, __LINE__, aod);
		return rc;
	}

	if (aod) {
		if (!pcfg->fod) {
			pcfg->abyp_prev_mode = pcfg->abypss_ctrl.abypass_mode;
			if (iris_get_abyp_mode(panel) == PASS_THROUGH_MODE)
				iris_abypass_switch_proc(pcfg->display, ANALOG_BYPASS_MODE, false, true);
		}
	} else {
		if (!pcfg->fod) {
			if (iris_get_abyp_mode(panel) == ANALOG_BYPASS_MODE &&
					pcfg->abyp_prev_mode == PASS_THROUGH_MODE &&
					!pcfg->fod) {
				iris_abypass_switch_proc(pcfg->display, PASS_THROUGH_MODE, false, true);
			}
		}
	}

	if (pcfg->fod_pending)
		pcfg->fod_pending = false;
	pcfg->aod = aod;

	return rc;
}

int iris_set_fod(struct dsi_panel *panel, bool fod)
{
	int rc = 0;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (!pcfg)
		return rc;

	if (panel->is_secondary)
		return rc;

	IRIS_LOGD("%s(%d), fod: %d", __func__, __LINE__, fod);
	if (pcfg->fod == fod) {
		IRIS_LOGD("%s(%d), fod: %d no change", __func__, __LINE__, fod);
		return rc;
	}

	if (!dsi_panel_initialized(panel)) {
		IRIS_LOGD("%s(%d), panel is not initialized fod: %d", __func__, __LINE__, fod);
		pcfg->fod_pending = true;
		atomic_set(&pcfg->fod_cnt, 1);
		pcfg->fod = fod;
		return rc;
	}

	if (fod) {
		if (!pcfg->aod) {
			pcfg->abyp_prev_mode = pcfg->abypss_ctrl.abypass_mode;
			if (iris_get_abyp_mode(panel) == PASS_THROUGH_MODE)
				iris_abypass_switch_proc(pcfg->display, ANALOG_BYPASS_MODE, false, true);
		}
	} else {
		/* pending until hbm off cmds sent in update_hbm 1->0 */
		pcfg->fod_pending = true;
		atomic_set(&pcfg->fod_cnt, 1);
	}

	pcfg->fod = fod;

	return rc;
}

int iris_post_fod(struct dsi_panel *panel)
{
	int rc = 0;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (!pcfg)
		return rc;

	if (panel->is_secondary)
		return rc;

	if (atomic_read(&pcfg->fod_cnt) > 0) {
		IRIS_LOGD("%s(%d), fod delay %d", __func__, __LINE__, atomic_read(&pcfg->fod_cnt));
		atomic_dec(&pcfg->fod_cnt);
		return rc;
	}

	IRIS_LOGD("%s(%d), fod: %d", __func__, __LINE__, pcfg->fod);

	if (pcfg->fod) {
		if (!pcfg->aod) {
			pcfg->abyp_prev_mode = pcfg->abypss_ctrl.abypass_mode;
			if (iris_get_abyp_mode(panel) == PASS_THROUGH_MODE)
				iris_abypass_switch_proc(pcfg->display, ANALOG_BYPASS_MODE, false, true);
		}
	} else {
		if (!pcfg->aod) {
			if (iris_get_abyp_mode(panel) == ANALOG_BYPASS_MODE &&
					pcfg->abyp_prev_mode == PASS_THROUGH_MODE) {
				iris_abypass_switch_proc(pcfg->display, PASS_THROUGH_MODE, false, true);
			}
		}
	}

	pcfg->fod_pending = false;

	return rc;
}

bool iris_get_aod(struct dsi_panel *panel)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (!panel || !pcfg)
		return false;

	return pcfg->aod;
}

static void _iris_clear_aod_state(void)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (pcfg->aod) {
		pcfg->aod = false;
		pcfg->abypss_ctrl.abypass_mode = pcfg->abyp_prev_mode;
	}
}

/*check whether it is in initial cont-splash packet*/
static bool _iris_check_cont_splash_ipopt(uint8_t ip, uint8_t opt_id)
{
	int i = 0;
	uint8_t cs_ip = 0;
	uint8_t cs_opt_id = 0;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_ctrl_seq *pseq_cs = _iris_get_ctrl_seq_cs(pcfg);

	for (i = 0; i < pseq_cs->cnt; i++) {
		cs_ip = pseq_cs->ctrl_opt[i].ip;
		cs_opt_id = pseq_cs->ctrl_opt[i].opt_id;

		if (ip == cs_ip && opt_id == cs_opt_id)
			return true;
	}

	return false;
}

/*select ip/opt to the opt_arr according to lightup stage type*/
static int _iris_select_cont_splash_ipopt(
		int type, struct iris_ctrl_opt *opt_arr)
{
	uint32_t i = 0;
	uint32_t j = 0;
	uint8_t ip = 0;
	uint8_t opt_id = 0;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_ctrl_seq *pseq = _iris_get_ctrl_seq(pcfg);
	struct iris_ctrl_opt *pctrl_opt = NULL;

	for (i = 0; i < pseq->cnt; i++) {
		pctrl_opt = pseq->ctrl_opt + i;
		ip = pctrl_opt->ip;
		opt_id = pctrl_opt->opt_id;

		if (_iris_check_cont_splash_ipopt(ip, opt_id))
			continue;

		memcpy(opt_arr + j, pctrl_opt, sizeof(*pctrl_opt));
		j++;
	}

	IRIS_LOGD("%s(), real len: %d", __func__, j);
	return j;
}

static void _iris_send_cont_splash_pkt(uint32_t type)
{
	int seq_cnt = 0;
	uint32_t size = 0;
	const int iris_max_opt_cnt = 30;
	struct iris_ctrl_opt *opt_arr = NULL;
	struct iris_cfg *pcfg = NULL;
	struct iris_ctrl_seq *pseq_cs = NULL;
	bool pt_mode;

	size = IRIS_IP_CNT * iris_max_opt_cnt * sizeof(struct iris_ctrl_opt);
	opt_arr = vmalloc(size);
	if (opt_arr == NULL) {
		IRIS_LOGE("%s(), failed to malloc buffer!", __func__);
		return;
	}

	pcfg = iris_get_cfg();
	memset(opt_arr, 0xff, size);

	if (type == IRIS_CONT_SPLASH_LK) {
		pseq_cs = _iris_get_ctrl_seq_cs(pcfg);
		iris_send_assembled_pkt(pseq_cs->ctrl_opt, pseq_cs->cnt);
		pcfg->iris_initialized = true;
	} else if (type == IRIS_CONT_SPLASH_KERNEL) {
		seq_cnt = _iris_select_cont_splash_ipopt(type, opt_arr);
		/*stop video -->set pq --> start video*/
		iris_sde_encoder_rc_lock();
		msleep(20);
		iris_send_assembled_pkt(opt_arr, seq_cnt);
		iris_sde_encoder_rc_unlock();
		iris_lp_init();
		_iris_read_chip_id();
		pcfg->iris_initialized = true;
	} else if (type == IRIS_CONT_SPLASH_BYPASS) {
		iris_lp_preinit();
		pcfg->iris_initialized = false;
		pt_mode = iris_abypass_switch_proc(pcfg->display, PASS_THROUGH_MODE, false, true);
		if (pt_mode)
			iris_set_out_frame_rate(pcfg->panel->cur_mode->timing.refresh_rate);
	} else if (type == IRIS_CONT_SPLASH_BYPASS_PRELOAD) {
		iris_reset_mipi();
		iris_enable(pcfg->panel, NULL);
	}

	vfree(opt_arr);
}

void iris_send_cont_splash(struct dsi_display *display)
{
	struct dsi_panel *panel = display->panel;
	struct iris_cfg *pcfg = iris_get_cfg();
	int lightup_opt = iris_lightup_opt_get();
	uint32_t type;

	if (!iris_is_chip_supported())
		return;

	if (panel->is_secondary)
		return;

	if (pcfg->ext_clk) {
		IRIS_LOGI("%s(), enable ext clk", __func__);
		clk_prepare_enable(pcfg->ext_clk);
	}

	// Update panel timing from UEFI.
	iris_is_resolution_switched(&panel->cur_mode->timing);
	type = iris_get_cont_type_with_timing_switch(panel);

	if (lightup_opt & 0x1)
		type = IRIS_CONT_SPLASH_NONE;

	mutex_lock(&pcfg->panel->panel_lock);
	_iris_send_cont_splash_pkt(type);
	mutex_unlock(&pcfg->panel->panel_lock);
}

int iris_lightoff(struct dsi_panel *panel,
		struct dsi_panel_cmd_set *off_cmds)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	struct iris_cfg *pcfg2 = iris_get_cfg_by_index(DSI_SECONDARY);
	int lightup_opt = iris_lightup_opt_get();

	pcfg2->mipi_pwr_st = false;

	if (!panel || panel->is_secondary) {
		IRIS_LOGD("no need to light off for 2nd panel.");
		return 0;
	}

	if ((lightup_opt & 0x10) == 0)
		pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE; //clear to ABYP mode

	iris_set_cfg_index(DSI_PRIMARY);

	IRIS_LOGI("%s(%d), mode: %s(%d) ---", __func__, __LINE__,
			pcfg->abypss_ctrl.abypass_mode == PASS_THROUGH_MODE ? "PT" : "ABYP",
			pcfg->abypss_ctrl.abypass_mode);
	if (off_cmds) {
		if (pcfg->abypss_ctrl.abypass_mode == PASS_THROUGH_MODE)
			iris_pt_send_panel_cmd(panel, off_cmds);
		else
			iris_dsi_send_cmds(panel, off_cmds->cmds, off_cmds->count, off_cmds->state);
	}
	iris_quality_setting_off();
	iris_lp_setting_off();
	_iris_clear_aod_state();
	pcfg->panel_pending = 0;
	pcfg->iris_initialized = false;
	iris_set_pinctrl_state(pcfg, false);

	IRIS_LOGI("%s(%d) ---", __func__, __LINE__);

	return 0;
}

int iris_disable(struct dsi_panel *panel, struct dsi_panel_cmd_set *off_cmds)
{
    return iris_lightoff(panel, off_cmds);
}

static void _iris_send_update_opt(
		struct iris_update_ipopt *popt,
		struct iris_cmd_comp *pasm_comp, uint8_t path)
{
	int32_t ip = 0;
	int32_t rc = 0;
	struct iris_ctrl_opt ctrl_opt;

	ip = popt->ip;
	ctrl_opt.ip = popt->ip;
	ctrl_opt.opt_id = popt->opt_new;
	ctrl_opt.skip_last = popt->skip_last;

	/*speical deal with lut table*/
	if (_iris_is_lut(ip))
		rc = _iris_send_lut_pkt(&ctrl_opt, pasm_comp, true, path);
	else
		rc = _iris_send_dtsi_pkt(&ctrl_opt, pasm_comp, path);

	if (rc) {
		IRIS_LOGE("%s(), [FATAL ERROR] invalid ip: %#x, opt: %#x",
				__func__,
				ip, ctrl_opt.opt_id);
	}
}

static void _iris_send_pq_cmds(struct iris_update_ipopt *popt, int ipopt_cnt, uint8_t path)
{
	int32_t i = 0;
	struct iris_cmd_comp cmd_comp;
	struct iris_cfg *pcfg = iris_get_cfg();

	if (!popt || !ipopt_cnt) {
		IRIS_LOGE("%s(), invalid popt or ipopt_cnt", __func__);
		return;
	}

	memset(&cmd_comp, 0x00, sizeof(cmd_comp));
	cmd_comp.cmd =  pcfg->iris_cmds.iris_cmds_buf;
	cmd_comp.link_state = DSI_CMD_SET_STATE_HS;
	cmd_comp.cnt = pcfg->iris_cmds.cmds_index;

	for (i = 0; i < ipopt_cnt; i++)
		_iris_send_update_opt(&popt[i], &cmd_comp, path);
}

static int _iris_update_pq_seq(struct iris_update_ipopt *popt, int ipopt_cnt)
{
	int32_t i = 0;
	int32_t j = 0;
	int32_t ip = 0;
	int32_t opt_id = 0;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_ctrl_seq *pseq = _iris_get_ctrl_seq(pcfg);

	for (i = 0; i < ipopt_cnt; i++) {
		/*need to update sequence*/
		if (popt[i].opt_new != popt[i].opt_old) {
			for (j = 0; j < pseq->cnt; j++) {
				ip = pseq->ctrl_opt[j].ip;
				opt_id = pseq->ctrl_opt[j].opt_id;

				if (ip == popt[i].ip &&
						opt_id == popt[i].opt_old)
					break;
			}

			if (j == pseq->cnt) {
				IRIS_LOGE("%s(), failed to find ip: %#x opt: %d",
						__func__,
						popt[i].ip, popt[i].opt_old);
				return -EINVAL;
			}

			pseq->ctrl_opt[j].opt_id = popt[i].opt_new;
		}
	}

	return 0;
}

void iris_update_pq_opt(struct iris_update_ipopt *popt, int ipopt_cnt, uint8_t path)
{
	int32_t rc = 0;

	if (!popt || !ipopt_cnt) {
		IRIS_LOGE("%s(), invalid popt or ipopt_cnt", __func__);
		return;
	}

	rc = _iris_update_pq_seq(popt, ipopt_cnt);
	if (!rc)
		_iris_send_pq_cmds(popt, ipopt_cnt, path);
}

static struct dsi_cmd_desc *_iris_get_desc_from_ipopt(uint8_t ip, uint8_t opt_id, int32_t pos)
{
	struct iris_ip_opt *popt = NULL;
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();
	popt = iris_find_ip_opt(ip, opt_id);
	if (popt == NULL) {
		IRIS_LOGE("%s(), can't find ip opt for ip: 0x%02x, opt: 0x%02x.",
				__func__, ip, opt_id);
		return NULL;
	}

	if (pos < 2) {
		IRIS_LOGE("%s(), invalid pos: %d", __func__, pos);
		return NULL;
	}

	return popt->cmd + (pos * 4 - IRIS_OCP_HEADER_ADDR_LEN) / pcfg->split_pkt_size;
}

uint32_t *iris_get_ipopt_payload_data(
		uint8_t ip, uint8_t opt_id, int32_t pos)
{
	struct dsi_cmd_desc *pdesc = NULL;
	struct iris_cfg *pcfg = iris_get_cfg();

	pdesc = _iris_get_desc_from_ipopt(ip, opt_id, pos);
	if (!pdesc) {
		IRIS_LOGE("%s(), failed to find desc!", __func__);
		return NULL;
	} else if (pos > pdesc->msg.tx_len) {
		IRIS_LOGE("%s(), pos %d is out of paload length %zu",
				__func__,
				pos, pdesc->msg.tx_len);
		return NULL;
	}

	return (uint32_t *)((uint8_t *)pdesc->msg.tx_buf + (pos * 4) % pcfg->split_pkt_size);
}

void iris_set_ipopt_payload_data(
		uint8_t ip, uint8_t opt_id, int32_t pos, uint32_t value)
{
	struct dsi_cmd_desc *pdesc = NULL;
	struct iris_cfg *pcfg = NULL;
	uint32_t *pvalue = NULL;

	pcfg = iris_get_cfg();
	pdesc = _iris_get_desc_from_ipopt(ip, opt_id, pos);
	if (!pdesc) {
		IRIS_LOGE("%s(), failed to find right desc.", __func__);
		return;
	}

	pvalue = (uint32_t *)((uint8_t *)pdesc->msg.tx_buf + (pos * 4) % pcfg->split_pkt_size);
	pvalue[0] = value;
}

void iris_update_bitmask_regval_nonread(
		struct iris_update_regval *pregval, bool is_commit)
{
	int32_t ip = 0;
	int32_t opt_id = 0;
	uint32_t orig_val = 0;
	uint32_t *data = NULL;
	uint32_t val = 0;
	struct iris_ip_opt *popt = NULL;

	if (!pregval) {
		IRIS_LOGE("%s(), invalid input", __func__);
		return;
	}

	ip = pregval->ip;
	opt_id = pregval->opt_id;

	popt = iris_find_ip_opt(ip, opt_id);
	if (popt == NULL) {
		IRIS_LOGE("%s(), can't find ip: 0x%02x opt: 0x%02x",
				__func__, ip, opt_id);
		return;
	} else if (popt->cmd_cnt != 1) {
		IRIS_LOGE("%s(), invalid bitmask, popt len: %d",
				__func__, popt->cmd_cnt);
		return;
	}

	data = (uint32_t *)popt->cmd[0].msg.tx_buf;

	orig_val = cpu_to_le32(data[2]);
	val = orig_val & (~pregval->mask);
	val |= (pregval->value  & pregval->mask);
	data[2] = val;
	pregval->value = val;

	if (is_commit)
		iris_send_ipopt_cmds(ip, opt_id);
}

void iris_update_bitmask_regval(
		struct iris_update_regval *pregval, bool is_commit)
{
	int32_t ip = 0;
	int32_t opt_id = 0;
	uint32_t *data = NULL;
	struct iris_ip_opt *popt = NULL;

	if (!pregval) {
		IRIS_LOGE("%s(), invalid input", __func__);
		return;
	}

	ip = pregval->ip;
	opt_id = pregval->opt_id;

	popt = iris_find_ip_opt(ip, opt_id);
	if (popt == NULL) {
		IRIS_LOGE("%s(), can't find ip: 0x%02x opt: 0x%02x",
				__func__, ip, opt_id);
		return;
	} else if (popt->cmd_cnt != 2) {
		IRIS_LOGE("%s(), invalid bitmask, popt len: %d",
				__func__, popt->cmd_cnt);
		return;
	}

	data = (uint32_t *)popt->cmd[1].msg.tx_buf;
	data[2] = cpu_to_le32(pregval->mask);
	data[3] = cpu_to_le32(pregval->value);

	if (is_commit)
		iris_send_ipopt_cmds(ip, opt_id);
}

static ssize_t _iris_cont_splash_write(
		struct file *file, const char __user *buff,
		size_t count, loff_t *ppos)
{
	unsigned long val;

	if (kstrtoul_from_user(buff, count, 0, &val))
		return -EFAULT;

	_iris_set_cont_splash_type(val);

	if (val == IRIS_CONT_SPLASH_KERNEL) {
		struct iris_cfg *pcfg = iris_get_cfg();
		mutex_lock(&pcfg->panel->panel_lock);
		_iris_send_cont_splash_pkt(val);
		mutex_unlock(&pcfg->panel->panel_lock);
	} else if (val != IRIS_CONT_SPLASH_LK &&
			val != IRIS_CONT_SPLASH_NONE) {
		IRIS_LOGE("the value is %zu, need to be 1 or 2 3", val);
	}

	return count;
}

static ssize_t _iris_cont_splash_read(
		struct file *file, char __user *buff,
		size_t count, loff_t *ppos)
{
	uint8_t type;
	int len, tot = 0;
	char bp[512];

	if (*ppos)
		return 0;

	type = iris_get_cont_splash_type();
	len = sizeof(bp);
	tot = scnprintf(bp, len, "%u\n", type);

	if (copy_to_user(buff, bp, tot))
		return -EFAULT;

	*ppos += tot;

	return tot;
}

static const struct file_operations iris_cont_splash_fops = {
	.open = simple_open,
	.write = _iris_cont_splash_write,
	.read = _iris_cont_splash_read,
};

static ssize_t _iris_split_pkt_write(
		struct file *file, const char __user *buff,
		size_t count, loff_t *ppos)
{
	unsigned long val;
	struct iris_cfg *pcfg = NULL;

	if (kstrtoul_from_user(buff, count, 0, &val))
		return -EFAULT;

	pcfg = iris_get_cfg();
	pcfg->add_last_flag = val;

	return count;
}

static ssize_t _iris_split_pkt_read(
		struct file *file, char __user *buff,
		size_t count, loff_t *ppos)
{
	uint8_t type;
	int len, tot = 0;
	struct iris_cfg *pcfg = NULL;
	char bp[512];

	if (*ppos)
		return 0;

	pcfg = iris_get_cfg();
	type = pcfg->add_last_flag;

	len = sizeof(bp);
	tot = scnprintf(bp, len, "%u\n", type);

	if (copy_to_user(buff, bp, tot))
		return -EFAULT;

	*ppos += tot;

	return tot;
}

int iris_wait_vsync(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	struct drm_encoder *drm_enc;

	if (pcfg->display == NULL || pcfg->display->bridge == NULL)//CID107520
		return -ENOLINK;
	drm_enc = pcfg->display->bridge->base.encoder;
	if (!drm_enc || !drm_enc->crtc)
		return -ENOLINK;
	if (sde_encoder_is_disabled(drm_enc))
		return -EIO;

	sde_encoder_wait_for_event(drm_enc, MSM_ENC_VBLANK);

	return 0;
}

int iris_set_pending_panel_brightness(int32_t pending, int32_t delay, int32_t level)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (pcfg) {
		IRIS_LOGI("set pending panel %d, %d, %d", pending, delay, level);
		pcfg->panel_pending = pending;
		pcfg->panel_delay = delay;
		pcfg->panel_level = level;
	}

	return 0;
}

int iris_sync_panel_brightness(int32_t step, void *phys_enc)
{
	struct sde_encoder_phys *phys_encoder = phys_enc;
	struct sde_connector *c_conn = NULL;
	struct dsi_display *display = NULL;
	struct iris_cfg *pcfg;
	int rc = 0;

	if (phys_encoder == NULL)
		return -EFAULT;
	if (phys_encoder->connector == NULL)
		return -EFAULT;

	c_conn = to_sde_connector(phys_encoder->connector);
	if (c_conn == NULL)
		return -EFAULT;

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI)
		return 0;

	display = c_conn->display;
	if (display == NULL)
		return -EFAULT;

	pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (pcfg->panel_pending == step) {
		IRIS_LOGI("sync pending panel %d %d,%d,%d",
				step, pcfg->panel_pending, pcfg->panel_delay,
				pcfg->panel_level);
		SDE_ATRACE_BEGIN("sync_panel_brightness");
		if (step <= 2) {
			rc = c_conn->ops.set_backlight(&c_conn->base,
					display, pcfg->panel_level);
			usleep_range(pcfg->panel_delay, pcfg->panel_delay + 1);
		} else {
			usleep_range(pcfg->panel_delay, pcfg->panel_delay + 1);
			rc = c_conn->ops.set_backlight(&c_conn->base,
					display, pcfg->panel_level);
		}
		if (c_conn->bl_device)
			c_conn->bl_device->props.brightness = pcfg->panel_level;
		pcfg->panel_pending = 0;
		SDE_ATRACE_END("sync_panel_brightness");
	}

	return rc;
}

static const struct file_operations iris_split_pkt_fops = {
	.open = simple_open,
	.write = _iris_split_pkt_write,
	.read = _iris_split_pkt_read,
};

static ssize_t _iris_chip_id_read(struct file *file, char __user *buff,
		size_t count, loff_t *ppos)
{
	int tot = 0;
	struct iris_cfg *pcfg = NULL;
	char bp[512];

	if (*ppos)
		return 0;

	pcfg = iris_get_cfg();

	tot = scnprintf(bp, sizeof(bp), "%u\n", pcfg->chip_id);
	if (copy_to_user(buff, bp, tot))
		return -EFAULT;
	*ppos += tot;

	return tot;
}

static const struct file_operations iris_chip_id_fops = {
	.open = simple_open,
	.read = _iris_chip_id_read,
};

static ssize_t _iris_power_mode_read(struct file *file, char __user *buff,
		size_t count, loff_t *ppos)
{
	int tot = 0;
	struct iris_cfg *pcfg = NULL;
	char bp[512];

	if (*ppos)
		return 0;

	pcfg = iris_get_cfg();

	tot = scnprintf(bp, sizeof(bp), "0x%02x\n", pcfg->power_mode);
	if (copy_to_user(buff, bp, tot))
		return -EFAULT;
	*ppos += tot;

	return tot;
}

static const struct file_operations iris_power_mode_fops = {
	.open = simple_open,
	.read = _iris_power_mode_read,
};

static ssize_t _iris_dbg_i2c_write(struct file *file, const char __user *buff,
		size_t count, loff_t *ppos)
{

	unsigned long val;
	int ret = 0;
	bool is_ulps_enable = 0;
	uint32_t header = 0;
	uint32_t arr[100] = {0};


	if (kstrtoul_from_user(buff, count, 0, &val))
		return -EFAULT;
	IRIS_LOGI("%s(%d)", __func__, __LINE__);

	//single write
	header = 0xFFFFFFF4;
	arr[0] = 0xf0000000;
	arr[1] = 0x12345678;

	is_ulps_enable = iris_ulps_enable_get();
	IRIS_LOGI("%s(%d), is_ulps_enable = %d", __func__, __LINE__, is_ulps_enable);
	if (is_ulps_enable)
		iris_ulps_source_sel(ULPS_NONE);
	ret = iris_i2c_ocp_write(arr, 1, 0);
	if (is_ulps_enable)
		iris_ulps_source_sel(ULPS_MAIN);
	if (ret)
		IRIS_LOGE("%s(%d), ret = %d", __func__, __LINE__, ret);

	return count;

}

static ssize_t _iris_dbg_i2c_read(struct file *file, char __user *buff,
		size_t count, loff_t *ppos)
{
	int ret = 0;
	int i = 0;
	const int cnt = 5;
	const bool is_burst = true;
	bool is_ulps_enable = 0;
	uint32_t arr[100] = {0};

	arr[0] = 0xf0000000;

	is_ulps_enable = iris_ulps_enable_get();
	IRIS_LOGI("%s(%d), is_ulps_enable = %d", __func__, __LINE__, is_ulps_enable);
	if (is_ulps_enable)
		iris_ulps_source_sel(ULPS_NONE);
	ret = iris_i2c_ocp_read(arr, cnt, is_burst);
	if (is_ulps_enable)
		iris_ulps_source_sel(ULPS_MAIN);

	if (ret) {
		IRIS_LOGE("%s(%d), ret = %d", __func__, __LINE__, ret);
	} else {
		for (i = 0; i < cnt; i++)
			IRIS_LOGI("%s(%d), arr[%d] = %x", __func__, __LINE__, i, arr[i]);
	}
	return 0;
}

static const struct file_operations iris_i2c_srw_fops = {
	.open = simple_open,
	.write = _iris_dbg_i2c_write,
	.read = _iris_dbg_i2c_read,
};

static ssize_t _iris_list_debug(struct file *file,
		const char __user *user_buf, size_t count, loff_t *ppos)
{
	uint8_t ip;
	uint8_t opt_id;
	int32_t pos;
	uint32_t value;
	char buf[64];
	uint32_t *payload = NULL;

	if (count > sizeof(buf))
		return -EINVAL;

	memset(buf, 0, sizeof(buf));//CID98777

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (sscanf(buf, "%x %x %x %x", &ip, &opt_id, &pos, &value) != 4)
		return -EINVAL;

	payload = iris_get_ipopt_payload_data(ip, opt_id, 2);
	if (payload == NULL)
		return -EFAULT;

	IRIS_LOGI("%s: %x %x %x %x", __func__, ip, opt_id, pos, value);

	iris_set_ipopt_payload_data(ip, opt_id, pos, value);

	return count;
}

static const struct file_operations _iris_list_debug_fops = {
	.open = simple_open,
	.write = _iris_list_debug,
};

static int _iris_dbgfs_cont_splash_init(struct dsi_display *display)
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
	if (debugfs_create_file("iris_cont_splash", 0644, pcfg->dbg_root, display,
				&iris_cont_splash_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
				__FILE__, __LINE__);
		return -EFAULT;
	}

	if (debugfs_create_file("iris_split_pkt", 0644, pcfg->dbg_root, display,
				&iris_split_pkt_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
				__FILE__, __LINE__);
		return -EFAULT;
	}

	if (debugfs_create_file("chip_id", 0644, pcfg->dbg_root, display,
				&iris_chip_id_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
				__FILE__, __LINE__);
		return -EFAULT;
	}

	if (debugfs_create_file("power_mode", 0644, pcfg->dbg_root, display,
				&iris_power_mode_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
				__FILE__, __LINE__);
		return -EFAULT;
	}

	debugfs_create_u8("iris_pq_update_path", 0644, pcfg->dbg_root,
			(uint8_t *)&iris_pq_update_path);

	if (debugfs_create_file("iris_i2c_srw",	0644, pcfg->dbg_root, display,
				&iris_i2c_srw_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
				__FILE__, __LINE__);
		return -EFAULT;
	}

	if (debugfs_create_file("iris_list_debug",	0644, pcfg->dbg_root, display,
				&_iris_list_debug_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
				__FILE__, __LINE__);
		return -EFAULT;
	}

	return 0;
}

void iris_prepare(struct dsi_display *display)
{
	int index = display->panel->is_secondary ? DSI_SECONDARY : DSI_PRIMARY;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(index);

	if (!iris_is_chip_supported())
		return;

	if (pcfg->valid < PARAM_PARSED)
		return;

	iris_frc_prepare(pcfg);

	if (display->panel->is_secondary)
		return;

	if (pcfg->valid < PARAM_PREPARED) {
		iris_set_cfg_index(index);
		iris_parse_lut_cmds(1);
		iris_alloc_seq_space();
		pcfg->valid = PARAM_PREPARED;
	}
}

static int _iris_dev_probe(struct platform_device *pdev)
{
	struct iris_cfg *pcfg;
	u32 index = 0;
	int rc;

	if (!iris_is_chip_supported())
		return 0;

	IRIS_LOGI("%s()", __func__);
	if (!pdev || !pdev->dev.of_node) {
		IRIS_LOGE("%s(), pdev not found", __func__);
		return -ENODEV;
	}

	of_property_read_u32_index(pdev->dev.of_node, "index", 0, &index);
	pcfg = iris_get_cfg_by_index(index);
	pcfg->pdev = pdev;
	dev_set_drvdata(&pdev->dev, pcfg);

	rc = iris_enable_pinctrl(pdev, pcfg);
	if (rc) {
		IRIS_LOGE("%s(), failed to enable pinctrl, return: %d",
			__func__, rc);
	}

	rc = iris_parse_gpio(pdev, pcfg);
	if (rc) {
		IRIS_LOGE("%s(), failed to parse gpio, return: %d",
				__func__, rc);
		return rc;
	}

	iris_request_gpio();

	return 0;
}

static int _iris_dev_remove(struct platform_device *pdev)
{
	struct iris_cfg *pcfg = dev_get_drvdata(&pdev->dev);

	IRIS_LOGI("%s()", __func__);
	if (!iris_is_chip_supported())
		return 0;

	iris_release_gpio(pcfg);

	return 0;
}

static const struct of_device_id iris_dt_match[] = {
	{.compatible = "pxlw,iris"},
	{}
};

static struct platform_driver iris_driver = {
	.probe = _iris_dev_probe,
	.remove = _iris_dev_remove,
	.driver = {
		.name = "pxlw-iris",
		.of_match_table = iris_dt_match,
	},
};

module_platform_driver(iris_driver);
