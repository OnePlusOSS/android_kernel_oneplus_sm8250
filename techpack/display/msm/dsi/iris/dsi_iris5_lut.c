// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#include <linux/firmware.h>
#include <linux/debugfs.h>
#include "dsi_iris5_api.h"
#include "dsi_iris5_lightup.h"
#include "dsi_iris5_pq.h"
#include "dsi_iris5_log.h"

static u8 payload_size;

static uint32_t lut_lut2[LUT_LEN] = {};
static uint32_t LUT2_fw[LUT_LEN+LUT_LEN+LUT_LEN] = {};

static struct msmfb_iris_ambient_info iris_ambient_lut;
static struct msmfb_iris_maxcll_info iris_maxcll_lut;

static u8 *iris_ambient_lut_buf;
/* SDR2HDR_UVYGAIN_BLOCK_CNT > SDR2HDR_LUT2_BLOCK_CNT */
static struct dsi_cmd_desc *dynamic_lut_send_cmd;

static uint32_t lut_luty[LUT_LEN] = {};
static uint32_t lut_lutuv[LUT_LEN] = {};
static uint32_t LUTUVY_fw[LUT_LEN+LUT_LEN+LUT_LEN+LUT_LEN+LUT_LEN+LUT_LEN] = {};

static u8 *iris_maxcll_lut_buf;
static struct dsi_cmd_desc *dynamic_lutuvy_send_cmd;

struct lut_node {
	u32 lut_cmd_cnts_max;
	u32 hdr_lut2_pkt_cnt;
	u32 hdr_lutuvy_pkt_cnt;
};

static struct lut_node iris_lut_param;
static u8 firmware_loaded = FIRMWARE_LOAD_FAIL;
static u16 firmware_calibrate_status;

static u8 cm_lut_opt_cnt = 21;
static u8 gamma_lut_opt_cnt = 8;

u8 iris_get_fw_status(void)
{
	return firmware_loaded;
}

void iris_update_fw_status(u8 value)
{
	firmware_loaded = value;
}

struct msmfb_iris_ambient_info *iris_get_ambient_lut(void)
{
	return &iris_ambient_lut;
}

struct msmfb_iris_maxcll_info *iris_get_maxcll_info(void)
{
	return &iris_maxcll_lut;
}

static void _iris_init_ambient_lut(void)
{
	iris_ambient_lut.ambient_lux = 0;
	iris_ambient_lut.ambient_bl_ratio = 0;
	iris_ambient_lut.lut_lut2_payload = &lut_lut2;

	if (iris_ambient_lut_buf != NULL) {
		vfree(iris_ambient_lut_buf);
		iris_ambient_lut_buf = NULL;
	}

	dynamic_lut_send_cmd = NULL;
}

static void _iris_init_maxcll_lut(void)
{
	iris_maxcll_lut.mMAXCLL = 2200;
	iris_maxcll_lut.lut_luty_payload = &lut_luty;
	iris_maxcll_lut.lut_lutuv_payload = &lut_lutuv;

	if (iris_maxcll_lut_buf != NULL) {
		vfree(iris_maxcll_lut_buf);
		iris_maxcll_lut_buf = NULL;
	}

	dynamic_lutuvy_send_cmd = NULL;
}

static void _iris_init_lut_buf(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	payload_size = pcfg->split_pkt_size;
	memset(&iris_lut_param, 0x00, sizeof(iris_lut_param));

	/* for HDR ambient light */
	_iris_init_ambient_lut();

	/* for HDR maxcll */
	_iris_init_maxcll_lut();
}

static int32_t _iris_request_firmware(const struct firmware **fw,
		const uint8_t *name)
{
	int32_t rc = 0;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct device *dev = &pcfg->display->pdev->dev;

	if (name == NULL) {
		IRIS_LOGE("%s(), firmware is null", __func__);
		return -EINVAL;
	}

	rc = request_firmware(fw, name, dev);
	if (rc) {
		IRIS_LOGE("%s(), failed to request firmware: %s, return: %d",
				__func__, name, rc);
		return rc;
	}

	IRIS_LOGE("%s(), request firmware [Success], name: %s, size: %zu bytes",
			__func__, name, (*fw)->size);

	return rc;
}

static void _iris_release_firmware(const struct firmware **fw)
{
	if (*fw) {
		release_firmware(*fw);
		*fw = NULL;
	}
}

static int _iris_change_lut_type_addr(
		struct iris_ip_opt *dest, struct iris_ip_opt *src)
{
	int rc = -EINVAL;
	struct dsi_cmd_desc *desc = NULL;

	if (!src || !dest) {
		IRIS_LOGE("%s(), src or dest is null", __func__);
		return rc;
	}

	desc = src->cmd;
	if (!desc) {
		IRIS_LOGE("%s(), invalid desc.", __func__);
		return rc;
	}

	IRIS_LOGD("%s(), desc len: %zu", __func__, desc->msg.tx_len);
	iris_change_type_addr(dest, src);

	return 0;
}

static int _iris_change_dbc_type_addr(void)
{
	int i = 0;
	int rc = -EINVAL;
	u8 ip = IRIS_IP_DBC;
	u8 opt_id = 0xFE;
	u8 lut_opt_id = CABC_DLV_OFF;
	struct iris_ip_opt *lut_popt = NULL;
	struct iris_ip_opt *popt = NULL;

	/*DBC change*/
	IRIS_LOGD("%s(%d)", __func__, __LINE__);
	popt = iris_find_ip_opt(ip, opt_id);
	if (!popt)
		return rc;

	for (i = 0; i < DBC_HIGH; i++) {
		lut_popt = iris_find_ip_opt(DBC_LUT, lut_opt_id + i);
		if (!lut_popt)
			return rc;

		rc = _iris_change_lut_type_addr(lut_popt, popt);
	}
	return rc;
}

static int _iris_change_gamma_type_addr(void)
{
	u8 i = 0;
	int rc = -EINVAL;
	u8 ip = IRIS_IP_DPP;
	u8 opt_id = 0xFE;
	u8 lut_opt_id = 0;
	struct iris_ip_opt *lut_popt = NULL;
	struct iris_ip_opt *popt = NULL;

	IRIS_LOGD("%s(%d)", __func__, __LINE__);
	popt = iris_find_ip_opt(ip, opt_id);
	if (!popt) {
		IRIS_LOGE("%s(), cann't find valid option, input ip: %#x, opt: %#x.",
				__func__, ip, opt_id);
		return rc;
	}

	for (i = 0; i < gamma_lut_opt_cnt; i++) {
		lut_popt = iris_find_ip_opt(GAMMA_LUT, lut_opt_id + i);
		if (!lut_popt) {
			IRIS_LOGE("%s(), cann't find valid lut option, input ip: %#x, opt: %#x.",
					__func__, GAMMA_LUT, lut_opt_id + i);
			return rc;
		}

		rc = _iris_change_lut_type_addr(lut_popt, popt);
	}

	return rc;
}


static int _iris_change_sdr2hdr_type_addr(void)
{
	int i = 0;
	int j = 0;
	int rc = -EINVAL;
	u8 ip = IRIS_IP_SDR2HDR;
	u8 opt_id = 0xFE;
	u8 lut_opt_id = SDR2HDR_INV_UV0;
	struct iris_ip_opt *lut_popt = NULL;
	struct iris_ip_opt *popt = NULL;

	IRIS_LOGD("%s(%d)", __func__, __LINE__);
	for (j = SDR2HDR_INV_UV0; j <= SDR2HDR_INV_UV1; j++) {
		opt_id -= j - SDR2HDR_INV_UV0;
		popt = iris_find_ip_opt(ip, opt_id);
		if (!popt) {
			IRIS_LOGE("%s(), cann't find valid option, input ip: %#x, opt: %#x.",
					__func__, ip, opt_id);
			return rc;
		}

		for (i = 0; i < SDR2HDR_LEVEL_CNT; i++) {
			lut_opt_id = j << 4 | i;
			lut_popt = iris_find_ip_opt(SDR2HDR_LUT, lut_opt_id);
			if (!lut_popt) {
				IRIS_LOGE("%s(), cann't find valid lut option, input ip: %#x, opt: %#x.",
						__func__, SDR2HDR_LUT, lut_opt_id);
				return rc;
			}

			rc = _iris_change_lut_type_addr(lut_popt, popt);
		}
	}
	return rc;
}

static int _iris_change_dither_type_addr(void)
{
	int rc = -EINVAL;
	u8 ip = IRIS_IP_DPP;
	u8 opt_id = 0xF8;
	u8 lut_opt_id = 0;
	struct iris_ip_opt *lut_popt = NULL;
	struct iris_ip_opt *popt = NULL;

	IRIS_LOGD("%s(%d)", __func__, __LINE__);
	popt = iris_find_ip_opt(ip, opt_id);
	if (!popt) {
		IRIS_LOGE("%s(), cann't find valid option, input ip: %#x, opt: %#x.",
				__func__, ip, opt_id);
		return rc;
	}

	lut_popt = iris_find_ip_opt(DPP_DITHER_LUT, lut_opt_id);
	if (!lut_popt) {
		IRIS_LOGE("%s(), cann't find valid lut option, input ip: %#x, opt: %#x.",
				__func__, DPP_DITHER_LUT, lut_opt_id);
		return rc;
	}

	rc = _iris_change_lut_type_addr(lut_popt, popt);

	return rc;
}

static int _iris_send_lut_for_dma(void)
{
	int rc = 0;

	/*register level*/
	rc = _iris_change_dbc_type_addr();
	rc = _iris_change_gamma_type_addr();
	rc = _iris_change_sdr2hdr_type_addr();
	rc = _iris_change_dither_type_addr();

	return rc;
}

static void _iris_parse_panel_nits(const struct firmware *fw)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	pcfg->panel_nits = fw->data[fw->size-2]<<8 | fw->data[fw->size-3];
	IRIS_LOGI("%s(), panel nits: 0x%04x.", __func__, pcfg->panel_nits);
}

static u8 _iris_parse_calibrate_status(const struct firmware *fw)
{
	u8 calibrate_status = 0;

	calibrate_status = fw->data[fw->size-1];
	IRIS_LOGI("%s(), panel calibrate status: 0x%02x.",
			__func__, calibrate_status);

	return calibrate_status;
}

int iris_parse_lut_cmds(u8 flag)
{
	int ret = 0;
	struct iris_data data[3] = {{NULL, 0}, {NULL, 0}, {NULL, 0} };
	const struct firmware *fw = NULL;
	const struct firmware *ccf1_fw = NULL;
	const struct firmware *ccf2_fw = NULL;
	struct iris_ip_index *pip_index = NULL;
	struct iris_cfg *pcfg = NULL;
	u8 firmware_state = 0;
	char ccf1_name[256] = {};
	char ccf2_name[256] = {};

	firmware_calibrate_status = 0;
	pip_index = iris_get_ip_idx(IRIS_LUT_PIP_IDX);
	_iris_init_lut_buf();

	// Load "iris5.fw".
	ret = _iris_request_firmware(&fw, IRIS_FIRMWARE_NAME);
	if (!ret) {
		firmware_state |= (1<<0);
		data[0].buf = fw->data;
		data[0].size = fw->size;
		IRIS_LOGI("%s(%d), request name: %s, size: %u.",
				__func__, __LINE__, IRIS_FIRMWARE_NAME, data[0].size);
	} else {
		IRIS_LOGE("%s(), failed to request: %s", __func__, IRIS_FIRMWARE_NAME);
	}

	// Load "iris5_ccf1.fw".
	if (flag == 0 || flag == 2) {
		// Load calibrated firmware.
		strlcpy(ccf1_name, IRIS_CCF1_CALIBRATED_FIRMWARE_NAME, 256);
		ret = _iris_request_firmware(&ccf1_fw, ccf1_name);
		if (ret && flag == 0) {
			// Load golden firmware.
			strlcpy(ccf1_name, IRIS_CCF1_FIRMWARE_NAME, 256);
			ret = _iris_request_firmware(&ccf1_fw, ccf1_name);
		}
	} else {
		// Load golden firmware.
		strlcpy(ccf1_name, IRIS_CCF1_FIRMWARE_NAME, 256);
		ret = _iris_request_firmware(&ccf1_fw, ccf1_name);
	}
	if (!ret) {
		const uint32_t ccf1_per_pkg_size = 23584;
		const uint32_t ccf1_tail_size = 3;

		if ((ccf1_fw->size - ccf1_tail_size) % ccf1_per_pkg_size == 0
				&& ccf1_fw->size > ccf1_per_pkg_size) {
			u8 fw_calibrate_st = 0;

			firmware_state |= (1<<1);
			data[1].buf = ccf1_fw->data;

			// ommit the last 3 bytes for parsing firmware
			// panel nits(2 bytes) and panel calibration status(1 byte)
			data[1].size = ccf1_fw->size - 3;
			_iris_parse_panel_nits(ccf1_fw);
			fw_calibrate_st = _iris_parse_calibrate_status(ccf1_fw);
			firmware_calibrate_status |= fw_calibrate_st;

			cm_lut_opt_cnt = (ccf1_fw->size - ccf1_tail_size) / ccf1_per_pkg_size;
			IRIS_LOGI("%s(%d), request name: %s, size: %u, option count: %u.",
					__func__, __LINE__,
					ccf1_name, data[1].size, cm_lut_opt_cnt);
		} else {
			IRIS_LOGE("%s(), invalid format for firmware: %s",
					__func__, ccf1_name);
		}
	} else {
		IRIS_LOGE("%s(), failed to request: %s", __func__, ccf1_name);
	}

	// Load "iris5_ccf2.fw".
	if (flag == 0 || flag == 2) {
		// Load calibrated firmware.
		strlcpy(ccf2_name, IRIS_CCF2_CALIBRATED_FIRMWARE_NAME, 256);
		ret = _iris_request_firmware(&ccf2_fw, ccf2_name);
		if (ret && flag == 0) {
			// Load golden firmware.
			strlcpy(ccf2_name, IRIS_CCF2_FIRMWARE_NAME, 256);
			ret = _iris_request_firmware(&ccf2_fw, ccf2_name);
		}
	} else {
		// Load golden firmware.
		strlcpy(ccf2_name, IRIS_CCF2_FIRMWARE_NAME, 256);
		ret = _iris_request_firmware(&ccf2_fw, ccf2_name);
	}
	if (!ret) {
		const uint32_t ccf2_per_pkg_size = 428;
		const uint32_t ccf2_tail_size = 1;

		if ((ccf2_fw->size - ccf2_tail_size) % ccf2_per_pkg_size == 0
				&& ccf2_fw->size > ccf2_per_pkg_size) {
			u8 fw_calibrate_st = 0;

			firmware_state |= (1<<2);
			data[2].buf = ccf2_fw->data;

			// ommit the last 1 byte for parsing firmware
			// it's panel calibration status
			data[2].size = ccf2_fw->size - 1;
			fw_calibrate_st = _iris_parse_calibrate_status(ccf2_fw);
			firmware_calibrate_status |= fw_calibrate_st<<8;

			gamma_lut_opt_cnt = (ccf2_fw->size - ccf2_tail_size) / ccf2_per_pkg_size;
			IRIS_LOGI("%s(%d), request name: %s, size: %u, option count: %u.",
					__func__, __LINE__, ccf2_name, data[2].size, gamma_lut_opt_cnt);
		} else {
			IRIS_LOGE("%s(), invalid format for firmware: %s",
					__func__, ccf2_name);
		}
	} else {
		IRIS_LOGE("%s(), failed to request: %s", __func__, ccf2_name);
	}

	firmware_loaded = (firmware_state == 0x07 ? FIRMWARE_LOAD_SUCCESS : FIRMWARE_LOAD_FAIL);
	IRIS_LOGI("%s(), load firmware: %s, state: %#x",
			__func__,
			firmware_loaded == FIRMWARE_LOAD_SUCCESS ? "success" : "fail",
			firmware_state);
	if (firmware_state != 0) {
		pcfg = iris_get_cfg();
		ret = iris_attach_cmd_to_ipidx(data, (sizeof(data))/(sizeof(data[0])),
				pip_index);
		if (ret)
			IRIS_LOGE("%s(), failed to load iris fw", __func__);

		_iris_send_lut_for_dma();
	}

	_iris_release_firmware(&fw);
	_iris_release_firmware(&ccf1_fw);
	_iris_release_firmware(&ccf2_fw);

	return ret;
}

/*add lut cmds to bufs for sending*/
static void _iris_prepare_lut_cmds(struct iris_ip_opt *popt)
{
	int pos = 0;
	struct iris_cfg *pcfg = NULL;
	struct dsi_cmd_desc *pdesc = NULL;

	pcfg = iris_get_cfg();

	pdesc = pcfg->iris_cmds.iris_cmds_buf;
	pos = pcfg->iris_cmds.cmds_index;

	IRIS_LOGD("%s(), %p %p len: %d",
			__func__, &pdesc[pos], popt, popt->cmd_cnt);
	memcpy(&pdesc[pos], popt->cmd, sizeof(*pdesc) * popt->cmd_cnt);
	pos += popt->cmd_cnt;
	pcfg->iris_cmds.cmds_index = pos;
}

static void _iris_fomat_lut_cmds(u8 lut_type, u8 opt_id)
{
	struct iris_ip_opt *popt = NULL;

	popt = iris_find_ip_opt(lut_type, opt_id);
	if (!popt) {
		IRIS_LOGW("%s(%d), invalid opt id: %#x.",
				__func__, __LINE__, opt_id);
		return;
	}
	_iris_prepare_lut_cmds(popt);
}

int iris_send_lut(u8 lut_type, u8 lut_table_index, u32 lut_abtable_index)
{
	int i = 0;
	u8 lut_opt_id = 0xfe;
	int len = 0;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct dsi_cmd_desc *pdesc = pcfg->iris_cmds.iris_cmds_buf;

	switch (lut_type) {
	case DBC_LUT:
		if (!((lut_table_index <= DBC_HIGH /*&& lut_table_index >= DBC_INIT*/) ||
					(lut_table_index <= CABC_DLV_HIGH && lut_table_index >= CABC_DLV_OFF)))
			break;

		/*lut_abtable_index will be used at AB_table index here.*/
		if (lut_abtable_index > 0)
			lut_abtable_index = 1;

		len = 1;
		if (lut_table_index < CABC_DLV_OFF) {
			/*find lut table ip and opt id*/
			lut_opt_id = (lut_table_index & 0x3f) | (lut_abtable_index << 7);
			/*even and odd*/
			if (lut_table_index != DBC_INIT)
				len = 2;
		} else
			lut_opt_id = lut_table_index;

		for (i = 0; i < len; i++) {
			/*dbc level as to a table for example:odd 0x1 and even 0x41*/
			lut_opt_id = (i << 6 | lut_opt_id);
			_iris_fomat_lut_cmds(lut_type, lut_opt_id);
		}
		IRIS_LOGI("%s(): call DBC_LUT, index: %#x.", __func__, lut_table_index);
		break;

	case CM_LUT:
		if (lut_table_index >= cm_lut_opt_cnt) {
			IRIS_LOGW("%s(%d), invalid table index %d of type: %#x.",
					__func__, __LINE__, lut_table_index, lut_type);
			break;
		}

		lut_opt_id = lut_table_index & 0xff;
		_iris_fomat_lut_cmds(lut_type, lut_opt_id);
		IRIS_LOGI("%s(): call CM_LUT, index: %#x.", __func__, lut_table_index);
		break;

	case SDR2HDR_LUT:
		if (lut_table_index > (SDR2HDR_LEVEL5 | (SDR2HDR_INV_UV1<<4))) {
			IRIS_LOGW("%s(%d), invalid table index %d of type: %#x.",
					__func__, __LINE__, lut_table_index, lut_type);
			break;
		}

		lut_opt_id = lut_table_index & 0xff;
		_iris_fomat_lut_cmds(lut_type, lut_opt_id);
		IRIS_LOGI("%s(), call SDR2HDR_LUT, index: %#x.", __func__, lut_table_index);
		break;

	case SCALER1D_LUT:
	case SCALER1D_PP_LUT:
		if (lut_table_index >= SCALER1D_LUT_NUMBER) {
			IRIS_LOGW("%s(%d), invalid table index %d of type: %#x.",
					__func__, __LINE__, lut_table_index, lut_type);
			break;
		}

		lut_opt_id = lut_table_index & 0xff;
		_iris_fomat_lut_cmds(lut_type, lut_opt_id);
		IRIS_LOGI("%s(), call SCALER1D, ip: %#x, index: %d.", __func__,
				lut_type, lut_table_index);
		break;

	case AMBINET_HDR_GAIN:
		if (!iris_maxcll_lut_buf || !dynamic_lutuvy_send_cmd)
			break;

		memcpy(&pdesc[pcfg->iris_cmds.cmds_index], &dynamic_lutuvy_send_cmd[0],
				sizeof(struct dsi_cmd_desc)*iris_lut_param.hdr_lutuvy_pkt_cnt);

		pcfg->iris_cmds.cmds_index += iris_lut_param.hdr_lutuvy_pkt_cnt;
		IRIS_LOGI("%s(), ambinet hdr gain.", __func__);
		break;

	case AMBINET_SDR2HDR_LUT:
		if (!iris_ambient_lut_buf || !dynamic_lut_send_cmd)
			break;

		memcpy(&pdesc[pcfg->iris_cmds.cmds_index],
				&dynamic_lut_send_cmd[0],
				sizeof(struct dsi_cmd_desc)
				* iris_lut_param.hdr_lut2_pkt_cnt);

		pcfg->iris_cmds.cmds_index +=
			iris_lut_param.hdr_lut2_pkt_cnt;
		break;

	case GAMMA_LUT:
		if (lut_table_index >= gamma_lut_opt_cnt) {
			IRIS_LOGW("%s(%d), invalid table index %d of type: %#x.",
					__func__, __LINE__, lut_table_index, lut_type);
			break;
		}

		lut_opt_id = lut_table_index & 0xff;
		_iris_fomat_lut_cmds(lut_type, lut_opt_id);
		IRIS_LOGI("%s(), call GAMMA_LUT, index: %d.", __func__, lut_table_index);
		break;

	case FRC_PHASE_LUT:
		if (lut_table_index >= FRC_PHASE_TYPE_CNT) {
			IRIS_LOGW("%s(%d), invalid table index %d of type: %#x.",
					__func__, __LINE__, lut_table_index, lut_type);
			break;
		}

		lut_opt_id = lut_table_index & 0xff;
		_iris_fomat_lut_cmds(lut_type, lut_opt_id);
		IRIS_LOGI("%s(), call FRC_PHASE_LUT, index: %d.", __func__, lut_table_index);
		break;

	case APP_CODE_LUT:
		if (lut_table_index != 0) {
			IRIS_LOGW("%s(%d), invalid table index %d of type: %#x.",
					__func__, __LINE__, lut_table_index, lut_type);
			break;
		}

		lut_opt_id = lut_table_index & 0xff;
		_iris_fomat_lut_cmds(lut_type, lut_opt_id);
		IRIS_LOGI("%s(), call APP_CODE_LUT, index: %d.", __func__, lut_table_index);
		break;

	case DPP_DITHER_LUT:
		if (lut_table_index != 0) {
			IRIS_LOGW("%s(%d), invalid table index %d of type: %#x.",
					__func__, __LINE__, lut_table_index, lut_type);
			break;
		}

		lut_opt_id = lut_table_index & 0xff;
		_iris_fomat_lut_cmds(lut_type, lut_opt_id);
		IRIS_LOGI("%s(), call DPP_DITHER_LUT, index: %d.", __func__, lut_table_index);
		break;

	case DTG_PHASE_LUT:
		if (lut_table_index != 0) {
			IRIS_LOGW("%s(%d), invalid table index %d of type: %#x.",
					__func__, __LINE__, lut_table_index, lut_type);
			break;
		}

		lut_opt_id = lut_table_index & 0xff;
		_iris_fomat_lut_cmds(lut_type, lut_opt_id);
		IRIS_LOGI("%s(), call DTG_PHASE_LUT, index: %d.", __func__, lut_table_index);
		break;

	default:
		IRIS_LOGW("%s(), type of %d have no cmd.", __func__, lut_type);
		break;
	}

	IRIS_LOGD("%s(), lut type: %#x, lut index: %#x, ab index: %#x, cmd count: %d, max count: %d",
			__func__,
			lut_type, lut_table_index, lut_abtable_index,
			pcfg->iris_cmds.cmds_index, iris_lut_param.lut_cmd_cnts_max);

	return IRIS_SUCCESS;
}

void iris_update_ambient_lut(enum LUT_TYPE lutType, u32 lutpos)
{
	u32 len = 0;
	u32 hdr_payload_size = payload_size;
	u32 hdr_pkt_size = hdr_payload_size + DIRECT_BUS_HEADER_SIZE;
	u32 hdr_block_pkt_cnt =
		(SDR2HDR_LUT_BLOCK_SIZE/2 + hdr_payload_size - 1)
		/ hdr_payload_size;
	u32 iris_lut_buf_index, lut_block_index, lut_block_cnt;
	u32 lut_pkt_cmd_index;
	u32 temp_index, index_i;
	u32 dbus_addr_start;
	u32 lut_fw_index;
	u32 cmd_payload_len;
	struct ocp_header ocp_dbus_header;

	memset(&ocp_dbus_header, 0, sizeof(ocp_dbus_header));
	ocp_dbus_header.header = 0x0004000C;
	ocp_dbus_header.address = SDR2HDR_LUT2_ADDRESS;

	if (lutpos == 0xFFE00000)
		hdr_block_pkt_cnt =
			(SDR2HDR_LUT_BLOCK_SIZE + hdr_payload_size - 1)
			/ hdr_payload_size;

	if (lutType != AMBINET_SDR2HDR_LUT) {
		IRIS_LOGE("%s input lutType error %d", __func__, lutType);
		return;
	}

	if (lutpos == 0xFFE00000)
		dbus_addr_start = SDR2HDR_LUT2_ADDRESS;
	else
		dbus_addr_start = SDR2HDR_LUT2_ADDRESS + lutpos * SDR2HDR_LUT_BLOCK_SIZE / 2;
	lut_block_cnt = SDR2HDR_LUT2_BLOCK_CNT;

	// copy lut2 to the firmware format.
	//  lut2 is EVEN+ODD,
	//  LUT2_fw is  EVEN ODD EVEN ODD EVEN ODD
	for (index_i = 0; index_i < LUT_LEN; index_i++) {
		if (lutpos == 0xFFE00000) {
			lut_fw_index = index_i / 2;
			if (index_i % 2 != 0)  // ODD
				lut_fw_index += LUT_LEN / 2;
			LUT2_fw[lut_fw_index] = lut_lut2[index_i];
			LUT2_fw[lut_fw_index + LUT_LEN] = lut_lut2[index_i];
			LUT2_fw[lut_fw_index + LUT_LEN + LUT_LEN] = lut_lut2[index_i];
		} else {
			if (index_i % 2 == 0) {
				lut_fw_index = index_i / 4;
				if (index_i % 4 != 0) /* ODD */
					lut_fw_index += LUT_LEN / 4;
				LUT2_fw[lut_fw_index] = lut_lut2[index_i];
				LUT2_fw[lut_fw_index + LUT_LEN / 2] = lut_lut2[index_i];
				LUT2_fw[lut_fw_index + LUT_LEN / 2 + LUT_LEN / 2] =
					lut_lut2[index_i];
			}
		}
	}

	if (dynamic_lut_send_cmd == NULL) {
		len = sizeof(struct dsi_cmd_desc)
			* hdr_pkt_size * hdr_block_pkt_cnt
			* SDR2HDR_LUT2_BLOCK_NUMBER;
		dynamic_lut_send_cmd = vzalloc(len);
		if (dynamic_lut_send_cmd == NULL) {
			IRIS_LOGE("%s(), failed to alloc mem", __func__);
			return;
		}
		iris_lut_param.lut_cmd_cnts_max +=
			hdr_block_pkt_cnt * SDR2HDR_LUT2_BLOCK_NUMBER;
		iris_lut_param.hdr_lut2_pkt_cnt =
			hdr_block_pkt_cnt * SDR2HDR_LUT2_BLOCK_NUMBER;
	}

	if (iris_ambient_lut_buf)
		memset(iris_ambient_lut_buf, 0,
				hdr_pkt_size * iris_lut_param.hdr_lut2_pkt_cnt);

	if (!iris_ambient_lut_buf) {
		len = hdr_pkt_size * iris_lut_param.hdr_lut2_pkt_cnt;
		iris_ambient_lut_buf = vzalloc(len);
	}
	if (!iris_ambient_lut_buf)
		return;

	lut_fw_index = 0;
	/*parse LUT2*/
	for (lut_block_index = 0;
			lut_block_index < lut_block_cnt;
			lut_block_index++){

		ocp_dbus_header.address = dbus_addr_start
			+ lut_block_index
			* SDR2HDR_LUT_BLOCK_ADDRESS_INC;

		for (lut_pkt_cmd_index = 0;
				lut_pkt_cmd_index < hdr_block_pkt_cnt;
				lut_pkt_cmd_index++) {

			iris_lut_buf_index =
				lut_block_index * hdr_pkt_size
				* hdr_block_pkt_cnt
				+ lut_pkt_cmd_index * hdr_pkt_size;

			if (lut_pkt_cmd_index == hdr_block_pkt_cnt-1) {
				if (lutpos == 0xFFE00000)
					cmd_payload_len = SDR2HDR_LUT_BLOCK_SIZE
						- (hdr_block_pkt_cnt-1) * hdr_payload_size;
				else
					cmd_payload_len = SDR2HDR_LUT_BLOCK_SIZE/2
						- (hdr_block_pkt_cnt-1) * hdr_payload_size;
			} else
				cmd_payload_len = hdr_payload_size;

			temp_index = lut_pkt_cmd_index
				+ hdr_block_pkt_cnt * lut_block_index;
			dynamic_lut_send_cmd[temp_index].msg.type = 0x29;
			dynamic_lut_send_cmd[temp_index].msg.tx_len =
				cmd_payload_len + DIRECT_BUS_HEADER_SIZE;
			dynamic_lut_send_cmd[temp_index].post_wait_ms = 0;
			dynamic_lut_send_cmd[temp_index].msg.tx_buf =
				iris_ambient_lut_buf + iris_lut_buf_index;

			memcpy(&iris_ambient_lut_buf[iris_lut_buf_index],
					&ocp_dbus_header, DIRECT_BUS_HEADER_SIZE);
			iris_lut_buf_index += DIRECT_BUS_HEADER_SIZE;

			memcpy(&iris_ambient_lut_buf[iris_lut_buf_index],
					&LUT2_fw[lut_fw_index], cmd_payload_len);

			lut_fw_index += cmd_payload_len / 4;
			ocp_dbus_header.address += cmd_payload_len;
		}
	}
}

void iris_update_maxcll_lut(enum LUT_TYPE lutType, u32 lutpos)
{
	u32 hdr_payload_size = payload_size;
	u32 hdr_pkt_size = hdr_payload_size + DIRECT_BUS_HEADER_SIZE;
	u32 hdr_block_pkt_cnt = (SDR2HDR_LUT_BLOCK_SIZE / 2 + hdr_payload_size - 1) / hdr_payload_size;
	u32 iris_lut_buf_index, lut_block_index, lut_block_cnt, lut_pkt_cmd_index;
	u32 temp_index, index_i;
	u32 dbus_addr_start;
	u32 lut_fw_index;
	u32 cmd_payload_len;
	struct ocp_header ocp_dbus_header;

	memset(&ocp_dbus_header, 0, sizeof(ocp_dbus_header));
	ocp_dbus_header.header = 0x0004000C;
	ocp_dbus_header.address = SDR2HDR_LUTUVY_ADDRESS;

	if (lutpos == 0xFFFF0000)
		hdr_block_pkt_cnt = (SDR2HDR_LUT_BLOCK_SIZE + hdr_payload_size - 1) / hdr_payload_size;

	if (lutType != AMBINET_HDR_GAIN) {
		IRIS_LOGE("%s input lutType error %d", __func__, lutType);
		return;
	}

	dbus_addr_start = SDR2HDR_LUTUVY_ADDRESS;
	lut_block_cnt = SDR2HDR_LUTUVY_BLOCK_CNT;

	// copy lutuvy to the firmware format.
	// lutuvy is EVEN+ODD, LUT2_fw is  EVEN ODD EVEN ODD EVEN ODD
	for (index_i = 0; index_i < LUT_LEN; index_i++) {
		if (lutpos == 0xFFFF0000) {
			lut_fw_index = index_i / 2;
			if (index_i % 2 == 0) // ODD
				lut_fw_index += LUT_LEN / 2;
			LUTUVY_fw[lut_fw_index] = lut_lutuv[index_i];
			LUTUVY_fw[lut_fw_index + LUT_LEN] = lut_lutuv[index_i];
			LUTUVY_fw[lut_fw_index + 2 * LUT_LEN] = lut_lutuv[index_i];
			LUTUVY_fw[lut_fw_index + 3 * LUT_LEN] = lut_lutuv[index_i];
			LUTUVY_fw[lut_fw_index + 4 * LUT_LEN] = lut_luty[index_i];
			LUTUVY_fw[lut_fw_index + 5 * LUT_LEN] = lut_luty[index_i];
		} else {
			if (index_i % 2 == 0) {
				lut_fw_index = index_i / 4;
				if (index_i % 4 != 0) // ODD
					lut_fw_index += LUT_LEN / 4;
				LUTUVY_fw[lut_fw_index] = lut_lutuv[index_i];
				LUTUVY_fw[lut_fw_index + LUT_LEN / 2] = lut_lutuv[index_i];
				LUTUVY_fw[lut_fw_index + LUT_LEN] = lut_lutuv[index_i];
				LUTUVY_fw[lut_fw_index + 3 * LUT_LEN / 2] = lut_lutuv[index_i];
				LUTUVY_fw[lut_fw_index + 2 * LUT_LEN] = lut_luty[index_i];
				LUTUVY_fw[lut_fw_index + 5 * LUT_LEN / 2] = lut_luty[index_i];
			}
		}
	}

	if (dynamic_lutuvy_send_cmd == NULL) {
		dynamic_lutuvy_send_cmd = vzalloc(sizeof(struct dsi_cmd_desc) * hdr_pkt_size * hdr_block_pkt_cnt * SDR2HDR_LUTUVY_BLOCK_NUMBER);
		if (dynamic_lutuvy_send_cmd == NULL) {
			IRIS_LOGE("%s: failed to alloc mem", __func__);
			return;
		}
		iris_lut_param.lut_cmd_cnts_max += hdr_block_pkt_cnt * SDR2HDR_LUTUVY_BLOCK_NUMBER;
		iris_lut_param.hdr_lutuvy_pkt_cnt = hdr_block_pkt_cnt * SDR2HDR_LUTUVY_BLOCK_NUMBER;
	}

	if (iris_maxcll_lut_buf)
		memset(iris_maxcll_lut_buf, 0, hdr_pkt_size * iris_lut_param.hdr_lutuvy_pkt_cnt);

	if (!iris_maxcll_lut_buf)
		iris_maxcll_lut_buf = vzalloc(hdr_pkt_size * iris_lut_param.hdr_lutuvy_pkt_cnt);

	if (!iris_maxcll_lut_buf) {
		IRIS_LOGE("%s: failed to alloc mem", __func__);
		return;
	}

	lut_fw_index = 0;
	//parse LUTUVY
	for (lut_block_index = 0; lut_block_index < lut_block_cnt; lut_block_index++) {
		ocp_dbus_header.address = dbus_addr_start + lut_block_index*SDR2HDR_LUT_BLOCK_ADDRESS_INC;
		if (lutpos != 0xFFFF0000)
			ocp_dbus_header.address += lutpos * SDR2HDR_LUT_BLOCK_SIZE/2;
		for (lut_pkt_cmd_index = 0; lut_pkt_cmd_index < hdr_block_pkt_cnt; lut_pkt_cmd_index++) {
			iris_lut_buf_index = lut_block_index*hdr_pkt_size*hdr_block_pkt_cnt + lut_pkt_cmd_index*hdr_pkt_size;
			if (lut_pkt_cmd_index == hdr_block_pkt_cnt-1) {
				cmd_payload_len = SDR2HDR_LUT_BLOCK_SIZE/2 - (hdr_block_pkt_cnt-1) * hdr_payload_size;
				if (lutpos == 0xFFFF0000)
					cmd_payload_len += SDR2HDR_LUT_BLOCK_SIZE/2;
			} else
				cmd_payload_len = hdr_payload_size;

			temp_index = lut_pkt_cmd_index + hdr_block_pkt_cnt * lut_block_index;
			dynamic_lutuvy_send_cmd[temp_index].msg.type = 0x29;
			dynamic_lutuvy_send_cmd[temp_index].msg.tx_len = cmd_payload_len + DIRECT_BUS_HEADER_SIZE;
			dynamic_lutuvy_send_cmd[temp_index].post_wait_ms = 0;
			dynamic_lutuvy_send_cmd[temp_index].msg.tx_buf = iris_maxcll_lut_buf + iris_lut_buf_index;

			memcpy(&iris_maxcll_lut_buf[iris_lut_buf_index], &ocp_dbus_header, DIRECT_BUS_HEADER_SIZE);
			iris_lut_buf_index += DIRECT_BUS_HEADER_SIZE;

			memcpy(&iris_maxcll_lut_buf[iris_lut_buf_index], &LUTUVY_fw[lut_fw_index], cmd_payload_len);
			lut_fw_index += cmd_payload_len/4;
			ocp_dbus_header.address += cmd_payload_len;
		}
	}
}

void iris_update_gamma(void)
{
	if (iris_get_fw_status() != FIRMWARE_LOAD_SUCCESS)
		return;

	iris_scaler_gamma_enable(true, 1);
	iris_update_fw_status(FIRMWARE_IN_USING);
}

int iris_dbgfs_fw_calibrate_status_init(void)
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

	debugfs_create_u16("fw_calibrate_status", 0644, pcfg->dbg_root,
			&firmware_calibrate_status);

	return 0;
}
