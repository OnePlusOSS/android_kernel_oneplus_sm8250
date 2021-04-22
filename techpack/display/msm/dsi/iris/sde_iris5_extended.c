// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#include <sde_hw_mdss.h>
#include <sde_hw_sspp.h>
#include "dsi_iris5_api.h"
#include "dsi_iris5.h"
#include "dsi_iris5_log.h"


void iris_sde_plane_setup_csc(void *csc_ptr)
{
	static const struct sde_csc_cfg hdrYUV = {
		{
			0x00010000, 0x00000000, 0x00000000,
			0x00000000, 0x00010000, 0x00000000,
			0x00000000, 0x00000000, 0x00010000,
		},
		{ 0x0, 0x0, 0x0,},
		{ 0x0, 0x0, 0x0,},
		{ 0x0, 0x3ff, 0x0, 0x3ff, 0x0, 0x3ff,},
		{ 0x0, 0x3ff, 0x0, 0x3ff, 0x0, 0x3ff,},
	};
	static const struct sde_csc_cfg hdrRGB10 = {
		/* S15.16 format */
		{
			0x00012A15, 0x00000000, 0x0001ADBE,
			0x00012A15, 0xFFFFD00B, 0xFFFF597E,
			0x00012A15, 0x0002244B, 0x00000000,
		},
		/* signed bias */
		{ 0xffc0, 0xfe00, 0xfe00,},
		{ 0x0, 0x0, 0x0,},
		/* unsigned clamp */
		{ 0x40, 0x3ac, 0x40, 0x3c0, 0x40, 0x3c0,},
		{ 0x00, 0x3ff, 0x00, 0x3ff, 0x00, 0x3ff,},
	};

	if (!iris_is_chip_supported())
		return;

	if (iris_get_hdr_enable() == 1)
		csc_ptr = (void *)&hdrYUV;
	else if (iris_get_hdr_enable() == 2)
		csc_ptr = (void *)&hdrRGB10;

	return;
}


int iris_sde_kms_iris_operate(struct msm_kms *kms,
		u32 operate_type, struct msm_iris_operate_value *operate_value)
{
	int ret = -EINVAL;

	if (!iris_is_chip_supported() && !iris_is_softiris_supported())
		return 0;

	if (operate_type == DRM_MSM_IRIS_OPERATE_CONF) {
		ret = iris_operate_conf(operate_value);
	} else if (operate_type == DRM_MSM_IRIS_OPERATE_TOOL) {
		ret = iris_operate_tool(operate_value);
	}

	return ret;
}


void iris_sde_update_dither_depth_map(uint32_t *map)
{
	if (!iris_is_chip_supported())
		return;

	map[5] = 1;
	map[6] = 2;
	map[7] = 3;
	map[8] = 2;
}


void iris_sde_prepare_for_kickoff(uint32_t num_phys_encs, void *phys_enc)
{
	if (!iris_is_chip_supported() && !iris_is_softiris_supported())
		return;

	if (num_phys_encs == 0)
		return;

	if (iris_is_chip_supported())
		iris_prepare_for_kickoff(phys_enc);
	iris_sync_panel_brightness(1, phys_enc);
}

void iris_sde_encoder_kickoff(uint32_t num_phys_encs, void *phys_enc)
{
	if (!iris_is_chip_supported() && !iris_is_softiris_supported())
		return;

	if (num_phys_encs == 0)
		return;

	if (iris_is_chip_supported())
		iris_kickoff(phys_enc);
	iris_sync_panel_brightness(2, phys_enc);
}

void iris_sde_encoder_sync_panel_brightness(uint32_t num_phys_encs, void *phys_enc)
{
	if (!iris_is_chip_supported() && !iris_is_softiris_supported())
		return;

	if (num_phys_encs == 0)
		return;

	iris_sync_panel_brightness(3, phys_enc);
}

void iris_sde_encoder_wait_for_event(uint32_t num_phys_encs, void *phys_enc,
		uint32_t event)
{
	if (!iris_is_chip_supported() && !iris_is_softiris_supported())
		return;

	if (num_phys_encs == 0)
		return;

	if (event != MSM_ENC_COMMIT_DONE)
		return;

	iris_sync_panel_brightness(4, phys_enc);
}

#if defined(PXLW_IRIS_DUAL)
#define CSC_10BIT_OFFSET       4
#define DGM_CSC_MATRIX_SHIFT       0

extern int iris_sspp_subblk_offset(struct sde_hw_pipe *ctx, int s_id, u32 *idx);

void iris_sde_hw_sspp_setup_csc_v2(void *pctx, const void *pfmt, void *pdata)
{
	u32 idx = 0;
	u32 op_mode = 0;
	u32 clamp_shift = 0;
	u32 val;
	u32 op_mode_off = 0;
	bool csc10 = false;
	const struct sde_sspp_sub_blks *sblk;
	struct sde_hw_pipe *ctx = pctx;
	const struct sde_format *fmt = pfmt;
	struct sde_csc_cfg *data = pdata;

	if (!iris_is_dual_supported())
		return;

	if (!ctx || !ctx->cap || !ctx->cap->sblk)
		return;

	if (SDE_FORMAT_IS_YUV(fmt))
		return;

	if (!iris_is_chip_supported())
		return;

	sblk = ctx->cap->sblk;
	if (iris_sspp_subblk_offset(ctx, SDE_SSPP_CSC_10BIT, &idx))
		return;

	op_mode_off = idx;
	if (test_bit(SDE_SSPP_CSC_10BIT, &ctx->cap->features)) {
		idx += CSC_10BIT_OFFSET;
		csc10 = true;
	}
	clamp_shift = csc10 ? 16 : 8;
	if (data && !SDE_FORMAT_IS_YUV(fmt)) {
		op_mode |= BIT(0);
		sde_hw_csc_matrix_coeff_setup(&ctx->hw,
				idx, data, DGM_CSC_MATRIX_SHIFT);
		/* Pre clamp */
		val = (data->csc_pre_lv[0] << clamp_shift) | data->csc_pre_lv[1];
		SDE_REG_WRITE(&ctx->hw, idx + 0x14, val);
		val = (data->csc_pre_lv[2] << clamp_shift) | data->csc_pre_lv[3];
		SDE_REG_WRITE(&ctx->hw, idx + 0x18, val);
		val = (data->csc_pre_lv[4] << clamp_shift) | data->csc_pre_lv[5];
		SDE_REG_WRITE(&ctx->hw, idx + 0x1c, val);

		/* Post clamp */
		val = (data->csc_post_lv[0] << clamp_shift) | data->csc_post_lv[1];
		SDE_REG_WRITE(&ctx->hw, idx + 0x20, val);
		val = (data->csc_post_lv[2] << clamp_shift) | data->csc_post_lv[3];
		SDE_REG_WRITE(&ctx->hw, idx + 0x24, val);
		val = (data->csc_post_lv[4] << clamp_shift) | data->csc_post_lv[5];
		SDE_REG_WRITE(&ctx->hw, idx + 0x28, val);

		/* Pre-Bias */
		SDE_REG_WRITE(&ctx->hw, idx + 0x2c, data->csc_pre_bv[0]);
		SDE_REG_WRITE(&ctx->hw, idx + 0x30, data->csc_pre_bv[1]);
		SDE_REG_WRITE(&ctx->hw, idx + 0x34, data->csc_pre_bv[2]);

		/* Post-Bias */
		SDE_REG_WRITE(&ctx->hw, idx + 0x38, data->csc_post_bv[0]);
		SDE_REG_WRITE(&ctx->hw, idx + 0x3c, data->csc_post_bv[1]);
		SDE_REG_WRITE(&ctx->hw, idx + 0x40, data->csc_post_bv[2]);
	}
	IRIS_LOGD("%s(), name:%s offset:%x ctx->idx:%x op_mode:%x",
			__func__, sblk->csc_blk.name, idx, ctx->idx, op_mode);
	SDE_REG_WRITE(&ctx->hw, op_mode_off, op_mode);
	wmb();
}
#endif
