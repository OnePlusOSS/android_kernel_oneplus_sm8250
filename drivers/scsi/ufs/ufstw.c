/*
 * Universal Flash Storage Turbo Write
 *
 * Copyright (C) 2017-2018 Samsung Electronics Co., Ltd.
 * Copyright (C) 2020 Oplus. All rights reserved.
 *
 * Authors:
 *	Yongmyung Lee <ymhungry.lee@samsung.com>
 *	Jinyoung Choi <j-young.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * See the COPYING file in the top-level directory or visit
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This program is provided "AS IS" and "WITH ALL FAULTS" and
 * without warranty of any kind. You are solely responsible for
 * determining the appropriateness of using and distributing
 * the program and assume all risks associated with your exercise
 * of rights with respect to the program, including but not limited
 * to infringement of third party rights, the risks and costs of
 * program errors, damage to or loss of data, programs or equipment,
 * and unavailability or interruption of operations. Under no
 * circumstances will the contributor of this Program be liable for
 * any damages of any kind arising from your use or distribution of
 * this program.
 *
 * The Linux Foundation chooses to take subject only to the GPLv2
 * license terms, and distributes only under these terms.
 */

#include <uapi/scsi/ufs/ufs.h>

#include "ufshcd.h"
#include "ufstw.h"

static int ufstw_create_sysfs(struct ufsf_feature *ufsf, struct ufstw_lu *tw);
static int create_wbfn_enable(void);
static void remove_wbfn_enable(void);
static int create_wbfn_dynamic_tw_enable(void);
static void remove_wbfn_dynamic_tw_enable(void);
inline int ufstw_get_state(struct ufsf_feature *ufsf)
{
	return atomic_read(&ufsf->tw_state);
}

inline void ufstw_set_state(struct ufsf_feature *ufsf, int state)
{
	atomic_set(&ufsf->tw_state, state);
}

static int ufstw_is_not_present(struct ufsf_feature *ufsf)
{
	enum UFSTW_STATE cur_state = ufstw_get_state(ufsf);

	if (cur_state != TW_PRESENT) {
		INFO_MSG("tw_state != TW_PRESENT (%d)", cur_state);
		return -ENODEV;
	}
	return 0;
}

#define FLAG_IDN_NAME(idn)						\
	(idn == QUERY_FLAG_IDN_WB_EN ? "tw_enable" :			\
	 idn == QUERY_FLAG_IDN_WB_BUFF_FLUSH_EN ? "flush_enable" :	\
	 idn == QUERY_FLAG_IDN_WB_BUFF_FLUSH_DURING_HIBERN8 ?			\
	 "flush_hibern" : "unknown")

#define ATTR_IDN_NAME(idn)						\
	(idn == QUERY_ATTR_IDN_WB_FLUSH_STATUS ? "flush_status" :	\
	 idn == QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE ? "avail_buffer_size" :\
	 idn == QUERY_ATTR_IDN_WB_BUFF_LIFE_TIME_EST ? "lifetime_est" :	\
	 idn == QUERY_ATTR_IDN_CURR_WB_BUFF_SIZE ? "current_buf_size" :	\
	 "unknown")

static int ufstw_read_lu_attr(struct ufstw_lu *tw, u8 idn, u32 *attr_val)
{
	struct ufs_hba *hba = tw->ufsf->hba;
	int err = 0, lun;
	u32 val;

	lun = (tw->lun == TW_LU_SHARED) ? 0 : tw->lun;
	err = ufsf_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR, idn,
				    (u8)lun, &val);
	if (err) {
		ERR_MSG("read attr [0x%.2X](%s) failed. (%d)", idn,
			ATTR_IDN_NAME(idn), err);
		goto out;
	}

	*attr_val = val;

	INFO_MSG("read attr LUN(%d) [0x%.2X](%s) success (%u)",
		 lun, idn, ATTR_IDN_NAME(idn), *attr_val);
out:
	return err;
}

static int ufstw_set_lu_flag_dynamic_tw(struct ufstw_lu *tw, u8 idn, bool *flag_res)
{
	struct ufs_hba *hba = tw->ufsf->hba;
	int err = 0, lun;

	/* ufstw_lu_get(tw); */
	lun = (tw->lun == TW_LU_SHARED) ? 0 : tw->lun;
	err = ufsf_query_flag_retry(hba, UPIU_QUERY_OPCODE_SET_FLAG, idn,
				    (u8)lun, NULL);
	if (err) {
		ERR_MSG("set flag [0x%.2X] failed...err %d", idn, err);
		/* ufstw_lu_put(tw); */
		return err;
	}

	*flag_res = true;
	blk_add_trace_msg(tw->ufsf->sdev_ufs_lu[lun]->request_queue,
			  "%s:%d IDN %s (%d)", __func__, __LINE__,
			  idn == QUERY_FLAG_IDN_WB_EN ? "TW_EN" :
			  idn == QUERY_FLAG_IDN_WB_BUFF_FLUSH_EN ? "FLUSH_EN" :
			  idn == QUERY_FLAG_IDN_WB_BUFF_FLUSH_DURING_HIBERN8 ?
			  "HIBERN_EN" : "UNKNOWN", idn);

	/*INFO_MSG("tw_flag LUN(%d) [0x%.2X] %u", lun, idn,*flag_res);*/

	switch(idn) {
	case QUERY_FLAG_IDN_WB_EN:
		ufsf_para.tw_enable = true;
		break;
	default:
		break;
	}

	/* ufstw_lu_put(tw); */

	return 0;
}

static int ufstw_clear_lu_flag_dynamic_tw(struct ufstw_lu *tw, u8 idn, bool *flag_res)
{
	struct ufs_hba *hba = tw->ufsf->hba;
	int err = 0, lun;

	/* ufstw_lu_get(tw); */
	lun = (tw->lun == TW_LU_SHARED) ? 0 : tw->lun;
	err = ufsf_query_flag_retry(hba, UPIU_QUERY_OPCODE_CLEAR_FLAG, idn,
				    (u8)lun, NULL);
	if (err) {
		ERR_MSG("clear flag [0x%.2X] failed...err%d", idn, err);
		/* ufstw_lu_put(tw); */
		return err;
	}

	*flag_res = false;

	blk_add_trace_msg(tw->ufsf->sdev_ufs_lu[lun]->request_queue,
			  "%s:%d IDN %s (%d)", __func__, __LINE__,
			  idn == QUERY_FLAG_IDN_WB_EN ? "TW_EN" :
			  idn == QUERY_FLAG_IDN_WB_BUFF_FLUSH_EN ? "FLUSH_EN" :
			  idn == QUERY_FLAG_IDN_WB_BUFF_FLUSH_DURING_HIBERN8 ? "HIBERN_EN" :
			  "UNKNOWN", idn);
	INFO_MSG("tw_flag LUN(%d) [0x%.2X] %u", lun, idn, *flag_res);

	switch(idn) {
	case QUERY_FLAG_IDN_WB_EN:
		ufsf_para.tw_enable = false;
		break;
	default:
		break;
	}

	/* ufstw_lu_put(tw); */

	return 0;
}

static int ufstw_set_lu_flag(struct ufstw_lu *tw, u8 idn, bool *flag_res)
{
	struct ufs_hba *hba = tw->ufsf->hba;
	int err = 0, lun;

	lun = (tw->lun == TW_LU_SHARED) ? 0 : tw->lun;
	err = ufsf_query_flag_retry(hba, UPIU_QUERY_OPCODE_SET_FLAG, idn,
				    (u8)lun, NULL);
	if (err) {
		ERR_MSG("set flag [0x%.2X](%s) failed. (%d)", idn,
			FLAG_IDN_NAME(idn), err);
		goto out;
	}

	*flag_res = true;

	INFO_MSG("set flag LUN(%d) [0x%.2X](%s) success. (%u)",
		 lun, idn, FLAG_IDN_NAME(idn), *flag_res);
out:
	return err;
}

static int ufstw_clear_lu_flag(struct ufstw_lu *tw, u8 idn, bool *flag_res)
{
	struct ufs_hba *hba = tw->ufsf->hba;
	int err = 0, lun;

	lun = (tw->lun == TW_LU_SHARED) ? 0 : tw->lun;
	err = ufsf_query_flag_retry(hba, UPIU_QUERY_OPCODE_CLEAR_FLAG, idn,
				    (u8)lun, NULL);
	if (err) {
		ERR_MSG("clear flag [0x%.2X](%s) failed. (%d)", idn,
			FLAG_IDN_NAME(idn), err);
		goto out;
	}

	*flag_res = false;

	INFO_MSG("clear flag LUN(%d) [0x%.2X](%s) success. (%u)",
		 lun, idn, FLAG_IDN_NAME(idn), *flag_res);
out:
	return err;
}

static int ufstw_read_lu_flag(struct ufstw_lu *tw, u8 idn, bool *flag_res)
{
	struct ufs_hba *hba = tw->ufsf->hba;
	int err = 0, lun;
	bool val;

	lun = (tw->lun == TW_LU_SHARED) ? 0 : tw->lun;
	err = ufsf_query_flag_retry(hba, UPIU_QUERY_OPCODE_READ_FLAG, idn,
				    (u8)lun, &val);
	if (err) {
		ERR_MSG("read flag [0x%.2X](%s) failed. (%d)", idn,
			FLAG_IDN_NAME(idn), err);
		goto out;
	}

	*flag_res = val;

	INFO_MSG("read flag LUN(%d) [0x%.2X](%s) success. (%u)",
		 lun, idn, FLAG_IDN_NAME(idn), *flag_res);
out:
	return err;
}

static inline bool ufstw_is_write_lrbp(struct ufshcd_lrb *lrbp)
{
	if (lrbp->cmd->cmnd[0] == WRITE_10 || lrbp->cmd->cmnd[0] == WRITE_16)
		return true;

	return false;
}

static void ufstw_switch_disable_state(struct ufstw_lu *tw)
{
	int err = 0;

	WARN_MSG("dTurboWriteBUfferLifeTImeEst (0x%.2X)", tw->lifetime_est);
	WARN_MSG("tw-mode will change to disable-mode");

	mutex_lock(&tw->sysfs_lock);
	ufstw_set_state(tw->ufsf, TW_FAILED);
	mutex_unlock(&tw->sysfs_lock);

	if (tw->tw_enable) {
		pm_runtime_get_sync(tw->ufsf->hba->dev);
		err = ufstw_clear_lu_flag(tw, QUERY_FLAG_IDN_WB_EN,
					  &tw->tw_enable);
		pm_runtime_put_sync(tw->ufsf->hba->dev);
		if (err)
			WARN_MSG("tw_enable flag clear failed");
	}
}

static int ufstw_check_lifetime_not_guarantee(struct ufstw_lu *tw)
{
	bool disable_flag = false;
	unsigned int lifetime_guarantee = MASK_UFSTW_LIFETIME_NOT_GUARANTEE_1_0_1;

	if(tw->ufsf->tw_dev_info.tw_ver == UFSTW_VER_1_1_0)
		lifetime_guarantee = MASK_UFSTW_LIFETIME_NOT_GUARANTEE_1_1_0;
	WARN_MSG("dTurboWriteBUfferLifeTImeEst (0x%.2X),lifetime_guarantee=0x%x", tw->lifetime_est, lifetime_guarantee);
	if (tw->lifetime_est & lifetime_guarantee) {
		if (tw->lun == TW_LU_SHARED)
			WARN_MSG("lun-shared lifetime_est[31] (1)");
		else
			WARN_MSG("lun %d lifetime_est[31] (1)",
				    tw->lun);

		WARN_MSG("Device not guarantee the lifetime of TW Buffer");
#if defined(CONFIG_UFSTW_IGNORE_GUARANTEE_BIT)
		WARN_MSG("but we will ignore them for PoC");
#else
		disable_flag = true;
#endif
	}

	if (disable_flag ||
	    (tw->lifetime_est & ~lifetime_guarantee) >=
	    UFSTW_MAX_LIFETIME_VALUE) {
		ufstw_switch_disable_state(tw);
		return -ENODEV;
	}

	return 0;
}

static void ufstw_lifetime_work_fn(struct work_struct *work)
{
	struct ufstw_lu *tw;
	int ret;

	tw = container_of(work, struct ufstw_lu, tw_lifetime_work);

	if (ufstw_is_not_present(tw->ufsf))
		return;

	pm_runtime_get_sync(tw->ufsf->hba->dev);
	ret = ufstw_read_lu_attr(tw, QUERY_ATTR_IDN_WB_BUFF_LIFE_TIME_EST,
				 &tw->lifetime_est);
	pm_runtime_put_sync(tw->ufsf->hba->dev);
	if (ret)
		return;

	ufstw_check_lifetime_not_guarantee(tw);
}

void ufstw_prep_fn(struct ufsf_feature *ufsf, struct ufshcd_lrb *lrbp)
{
	struct ufstw_lu *tw;

	if (!lrbp || !ufsf_is_valid_lun(lrbp->lun))
		return;

	if (!ufstw_is_write_lrbp(lrbp))
		return;

	ufsf_para.total_write_secs += blk_rq_sectors(lrbp->cmd->request);

	tw = ufsf->tw_lup[lrbp->lun];
	if (!tw)
		return;

	if (!tw->tw_enable)
		return;

	spin_lock_bh(&tw->lifetime_lock);
	tw->stat_write_sec += blk_rq_sectors(lrbp->cmd->request);

	if (tw->stat_write_sec > UFSTW_LIFETIME_SECT) {
		tw->stat_write_sec = 0;
		spin_unlock_bh(&tw->lifetime_lock);
		schedule_work(&tw->tw_lifetime_work);
		return;
	}
	spin_unlock_bh(&tw->lifetime_lock);

	TMSG(tw->ufsf, lrbp->lun, "%s:%d tw_lifetime_work %u",
	     __func__, __LINE__, tw->stat_write_sec);
}

static inline void ufstw_init_lu_jobs(struct ufstw_lu *tw)
{
	INIT_WORK(&tw->tw_lifetime_work, ufstw_lifetime_work_fn);
}

static inline void ufstw_cancel_lu_jobs(struct ufstw_lu *tw)
{
	int ret;

	ret = cancel_work_sync(&tw->tw_lifetime_work);
	INFO_MSG("cancel_work_sync(tw_lifetime_work) ufstw_lu[%d] (%d)",
		 tw->lun, ret);
}

static inline int ufstw_version_mismatched(struct ufstw_dev_info *tw_dev_info)
{
	INFO_MSG("Support TW Spec : UFSTW_VER_1_0_1 = %.4X, UFSTW_VER_1_1_0 = %.4X, Device = %.4X",
		 UFSTW_VER_1_0_1 , UFSTW_VER_1_1_0, tw_dev_info->tw_ver);

	INFO_MSG("TW Driver Version : %.6X%s", UFSTW_DD_VER,
		 UFSTW_DD_VER_POST);

	if (tw_dev_info->tw_ver != UFSTW_VER_1_0_1 &&
		tw_dev_info->tw_ver != UFSTW_VER_1_1_0)
			return -ENODEV;


	return 0;
}

void ufstw_get_dev_info(struct ufsf_feature *ufsf, u8 *desc_buf)
{
	struct ufstw_dev_info *tw_dev_info = &ufsf->tw_dev_info;
	u16 wspecversion;
	u16 w_manufacturer_id;
	if (LI_EN_32(&desc_buf[DEVICE_DESC_PARAM_EXT_UFS_FEATURE_SUP]) &
		     UFS_FEATURE_SUPPORT_TW_BIT) {
		INFO_MSG("bUFSExFeaturesSupport: TW is set");
	} else {
		ERR_MSG("bUFSExFeaturesSupport: TW not support");
		ufstw_set_state(ufsf, TW_FAILED);
		return;
	}
	tw_dev_info->tw_buf_no_reduct =
		desc_buf[DEVICE_DESC_PARAM_WB_US_RED_EN];
	tw_dev_info->tw_buf_type = desc_buf[DEVICE_DESC_PARAM_WB_TYPE];
	tw_dev_info->tw_shared_buf_alloc_units =
		LI_EN_32(&desc_buf[DEVICE_DESC_PARAM_WB_SHARED_ALLOC_UNITS]);
	wspecversion = desc_buf[DEVICE_DESC_PARAM_SPEC_VER] << 8 |
				  desc_buf[DEVICE_DESC_PARAM_SPEC_VER + 1];

	w_manufacturer_id =	desc_buf[DEVICE_DESC_PARAM_MANF_ID] << 8 |
				desc_buf[DEVICE_DESC_PARAM_MANF_ID + 1];
	INFO_MSG("dev_desc wspecversion 0x%x\n", wspecversion);
	if(wspecversion == 0x310 || wspecversion == 0x220)
		tw_dev_info->tw_ver = LI_EN_16(&desc_buf[DEVICE_DESC_PARAM_TW_VER]);
	else
		tw_dev_info->tw_ver = LI_EN_16(&desc_buf[DEVICE_DESC_PARAM_TW_VER_3_0]);
	/*temporary for hynix 2.2 tw function*/
	if (wspecversion == 0x220 && w_manufacturer_id == 0x1AD)
		tw_dev_info->tw_ver = UFSTW_VER_1_1_0;
        /*temporary for micron 3.1 tw function*/
        if (wspecversion == 0x310 && w_manufacturer_id == 0x12C)
                tw_dev_info->tw_ver = UFSTW_VER_1_1_0;

	if (ufstw_version_mismatched(tw_dev_info)) {
		ERR_MSG("TW Spec Version mismatch. TW disabled");
		ufstw_set_state(ufsf, TW_FAILED);
		return;
	}

	INFO_MSG("tw_dev [53] bTurboWriteBufferNoUserSpaceReductionEn (%u)",
		 tw_dev_info->tw_buf_no_reduct);
	INFO_MSG("tw_dev [54] bTurboWriteBufferType (%u)",
		 tw_dev_info->tw_buf_type);
	INFO_MSG("tw_dev [55] dNumSharedTUrboWriteBufferAllocUnits (%u)",
		 tw_dev_info->tw_shared_buf_alloc_units);

	if (tw_dev_info->tw_buf_type == TW_BUF_TYPE_SHARED &&
	    tw_dev_info->tw_shared_buf_alloc_units == 0) {
		ERR_MSG("TW use shared buffer. But alloc unit is (0)");
		ufstw_set_state(ufsf, TW_FAILED);
		return;
	}
}

void ufstw_get_geo_info(struct ufsf_feature *ufsf, u8 *geo_buf)
{
	struct ufstw_dev_info *tw_dev_info = &ufsf->tw_dev_info;

	tw_dev_info->tw_number_lu = geo_buf[GEOMETRY_DESC_PARAM_WB_MAX_WB_LUNS];
	if (tw_dev_info->tw_number_lu == 0) {
		ERR_MSG("Turbo Write is not supported");
		ufstw_set_state(ufsf, TW_FAILED);
		return;
	}

	INFO_MSG("tw_geo [4F:52] dTurboWriteBufferMaxNAllocUnits (%u)",
		 LI_EN_32(&geo_buf[GEOMETRY_DESC_PARAM_WB_MAX_ALLOC_UNITS]));
	INFO_MSG("tw_geo [53] bDeviceMaxTurboWriteLUs (%u)",
		 tw_dev_info->tw_number_lu);
	INFO_MSG("tw_geo [54] bTurboWriteBufferCapAdjFac (%u)",
		 geo_buf[GEOMETRY_DESC_PARAM_WB_BUFF_CAP_ADJ]);
	INFO_MSG("tw_geo [55] bSupportedTWBufferUserSpaceReductionTypes (%u)",
		 geo_buf[GEOMETRY_DESC_PARAM_WB_SUP_RED_TYPE]);
	INFO_MSG("tw_geo [56] bSupportedTurboWriteBufferTypes (%u)",
		 geo_buf[GEOMETRY_DESC_PARAM_WB_SUP_WB_TYPE]);
}

static void ufstw_alloc_shared_lu(struct ufsf_feature *ufsf)
{
	struct ufstw_lu *tw;

	tw = kzalloc(sizeof(struct ufstw_lu), GFP_KERNEL);
	if (!tw) {
		ERR_MSG("ufstw_lu[shared] memory alloc failed");
		return;
	}

	tw->lun = TW_LU_SHARED;
	tw->ufsf = ufsf;
	ufsf->tw_lup[0] = tw;

	INFO_MSG("ufstw_lu[shared] is TurboWrite-Enabled");
}

static void ufstw_get_lu_info(struct ufsf_feature *ufsf, int lun, u8 *lu_buf)
{
	struct ufsf_lu_desc lu_desc;
	struct ufstw_lu *tw;

	lu_desc.tw_lu_buf_size =
		LI_EN_32(&lu_buf[UNIT_DESC_PARAM_WB_BUF_ALLOC_UNITS]);

	ufsf->tw_lup[lun] = NULL;

	if (lu_desc.tw_lu_buf_size) {
		ufsf->tw_lup[lun] =
			kzalloc(sizeof(struct ufstw_lu), GFP_KERNEL);
		if (!ufsf->tw_lup[lun]) {
			ERR_MSG("ufstw_lu[%d] memory alloc faield", lun);
			return;
		}

		tw = ufsf->tw_lup[lun];
		tw->ufsf = ufsf;
		tw->lun = lun;
		INFO_MSG("ufstw_lu[%d] [29:2C] dLUNumTWBufferAllocUnits (%u)",
			 lun, lu_desc.tw_lu_buf_size);
		INFO_MSG("ufstw_lu[%d] is TurboWrite-Enabled.", lun);
	} else {
		INFO_MSG("ufstw_lu[%d] [29:2C] dLUNumTWBufferAllocUnits (%u)",
			 lun, lu_desc.tw_lu_buf_size);
		INFO_MSG("ufstw_lu[%d] is TurboWrite-disabled", lun);
	}
}

inline void ufstw_alloc_lu(struct ufsf_feature *ufsf,
				  int lun, u8 *lu_buf)
{
	if (ufsf->tw_dev_info.tw_buf_type == TW_BUF_TYPE_SHARED &&
	    !ufsf->tw_lup[0])
		ufstw_alloc_shared_lu(ufsf);
	else if (ufsf->tw_dev_info.tw_buf_type == TW_BUF_TYPE_LU)
		ufstw_get_lu_info(ufsf, lun, lu_buf);
}

static inline void ufstw_print_lu_flag_attr(struct ufstw_lu *tw)
{
	char lun_str[20] = { 0 };

	if (tw->lun == TW_LU_SHARED)
		snprintf(lun_str, 7, "shared");
	else
		snprintf(lun_str, 2, "%d", tw->lun);

	INFO_MSG("tw_flag ufstw_lu[%s] IDN (0x%.2X) tw_enable (%d)",
		 lun_str, QUERY_FLAG_IDN_WB_EN, tw->tw_enable);
	INFO_MSG("tw_flag ufstw_lu[%s] IDN (0x%.2X) flush_enable (%d)",
		 lun_str, QUERY_FLAG_IDN_WB_BUFF_FLUSH_EN,
		 tw->flush_enable);
	INFO_MSG("tw_flag ufstw_lu[%s] IDN (0x%.2X) flush_hibern (%d)",
		 lun_str, QUERY_FLAG_IDN_WB_BUFF_FLUSH_DURING_HIBERN8,
		 tw->flush_during_hibern_enter);

	INFO_MSG("tw_attr ufstw_lu[%s] IDN (0x%.2X) flush_status (%u)",
		 lun_str, QUERY_ATTR_IDN_WB_FLUSH_STATUS, tw->flush_status);
	INFO_MSG("tw_attr ufstw_lu[%s] IDN (0x%.2X) buffer_size (%u)",
		 lun_str, QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE,
		 tw->available_buffer_size);
	INFO_MSG("tw_attr ufstw_lu[%s] IDN (0x%.2X) buffer_lifetime (0x%.2X)",
		 lun_str, QUERY_ATTR_IDN_WB_BUFF_LIFE_TIME_EST,
		 tw->lifetime_est);
}

static inline void ufstw_lu_update(struct ufstw_lu *tw)
{
	/* Flag */
	pm_runtime_get_sync(tw->ufsf->hba->dev);
	if (ufstw_read_lu_flag(tw, QUERY_FLAG_IDN_WB_EN, &tw->tw_enable))
		goto error_put;

	if (ufstw_read_lu_flag(tw, QUERY_FLAG_IDN_WB_BUFF_FLUSH_EN,
			       &tw->flush_enable))
		goto error_put;

	if (ufstw_read_lu_flag(tw, QUERY_FLAG_IDN_WB_BUFF_FLUSH_DURING_HIBERN8,
			       &tw->flush_during_hibern_enter))
		goto error_put;

	/* Attribute */
	if (ufstw_read_lu_attr(tw, QUERY_ATTR_IDN_WB_FLUSH_STATUS,
			       &tw->flush_status))
		goto error_put;

	if (ufstw_read_lu_attr(tw, QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE,
			       &tw->available_buffer_size))
		goto error_put;

	ufstw_read_lu_attr(tw, QUERY_ATTR_IDN_WB_BUFF_LIFE_TIME_EST,
			       &tw->lifetime_est);
error_put:
	pm_runtime_put_sync(tw->ufsf->hba->dev);
}

int ufstw_enable_tw_lun(struct ufstw_lu *tw, bool enable)
{
	ssize_t ret = 0;

	if (!tw->dynamic_tw_enable) {
		return 0;
	}

	/* mutex_lock(&tw->mode_lock); */
	if (enable) {
		if (ufstw_set_lu_flag_dynamic_tw(tw, QUERY_FLAG_IDN_WB_EN,
				      &tw->tw_enable)) {
			ret = -EINVAL;
			goto failed;
		}
	} else {
		if (ufstw_clear_lu_flag_dynamic_tw(tw, QUERY_FLAG_IDN_WB_EN,
					&tw->tw_enable)) {
			ret = -EINVAL;
			goto failed;
		}
	}

failed:
	/* mutex_unlock(&tw->mode_lock); */

	return ret;
}

void ufstw_enable_tw(struct ufsf_feature *ufsf, bool enable)
{
	int ret;
	int lun;

	if (!ufsf) {
		ERR_MSG("ERROR: ufsf is NULL!");
		return;
	}

	seq_scan_lu(lun) {
		if (!ufsf->tw_lup[lun])
			continue;

		ret = ufstw_enable_tw_lun(ufsf->tw_lup[lun], enable);
		if (ret) {
			ERR_MSG("ERROR: UFSTW LU %d enable failed!", lun);
			continue;
		}
	}
}


static int ufstw_lu_init(struct ufsf_feature *ufsf, int lun)
{
	struct ufstw_lu *tw;
	int ret = 0;

	if (lun == TW_LU_SHARED)
		tw = ufsf->tw_lup[0];
	else
		tw = ufsf->tw_lup[lun];

	tw->ufsf = ufsf;
	spin_lock_init(&tw->lifetime_lock);

	ufstw_lu_update(tw);

	ret = ufstw_check_lifetime_not_guarantee(tw);
	if (ret)
		goto err_out;

	ufstw_print_lu_flag_attr(tw);

	tw->stat_write_sec = 0;

	ufstw_init_lu_jobs(tw);

#if defined(CONFIG_UFSTW_BOOT_ENABLED)
	pm_runtime_get_sync(ufsf->hba->dev);
	ufstw_set_lu_flag(tw, QUERY_FLAG_IDN_WB_EN, &tw->tw_enable);
	ufstw_set_lu_flag(tw, QUERY_FLAG_IDN_WB_BUFF_FLUSH_DURING_HIBERN8,
			  &tw->flush_during_hibern_enter);
	pm_runtime_put_sync(ufsf->hba->dev);
#endif
	ret = ufstw_create_sysfs(ufsf, tw);
	if (ret)
		ERR_MSG("create sysfs failed");
err_out:
	return ret;
}

extern int ufsplus_tw_status;
void ufstw_init(struct ufsf_feature *ufsf)
{
	int lun, ret = 0;
	unsigned int tw_enabled_lun = 0;

	INFO_MSG("init start.. tw_state (%d)", ufstw_get_state(ufsf));

	if (ufsf->tw_dev_info.tw_buf_type == TW_BUF_TYPE_SHARED) {
		if (!ufsf->tw_lup[0]) {
			ERR_MSG("tw_lup memory allocation failed");
			goto out;
		}
		BUG_ON(ufsf->tw_lup[0]->lun != TW_LU_SHARED);

		ret = ufstw_lu_init(ufsf, TW_LU_SHARED);
		if (ret)
			goto out_free_mem;
		ufsf->tw_lup[0]->dynamic_tw_enable = true;
		INFO_MSG("ufstw_lu[shared] working");
		tw_enabled_lun++;
	} else {
		seq_scan_lu(lun) {
			if (!ufsf->tw_lup[lun])
				continue;

			ret = ufstw_lu_init(ufsf, lun);
			if (ret)
				goto out_free_mem;
			ufsf->tw_lup[lun]->dynamic_tw_enable = true;
			INFO_MSG("ufstw_lu[%d] working", lun);
			tw_enabled_lun++;
		}
		if (tw_enabled_lun > ufsf->tw_dev_info.tw_number_lu) {
			ERR_MSG("lu count mismatched");
			goto out_free_mem;
		}
	}

	if (tw_enabled_lun) {
		ufsplus_tw_status = 1;
	}

	if (tw_enabled_lun == 0) {
		ERR_MSG("tw_enabled_lun count zero");
		goto out_free_mem;
	}

	ufstw_set_state(ufsf, TW_PRESENT);
	create_wbfn_enable();
	create_wbfn_dynamic_tw_enable();
	return;
out_free_mem:
	seq_scan_lu(lun) {
		kfree(ufsf->tw_lup[lun]);
		ufsf->tw_lup[lun] = NULL;
	}
out:
	ERR_MSG("Turbo write intialization failed");

	ufstw_set_state(ufsf, TW_FAILED);
}

static inline void ufstw_remove_sysfs(struct ufstw_lu *tw)
{
	int ret;

	ret = kobject_uevent(&tw->kobj, KOBJ_REMOVE);
	INFO_MSG("kobject removed (%d)", ret);
	kobject_del(&tw->kobj);
}

void ufstw_remove(struct ufsf_feature *ufsf)
{
	struct ufstw_lu *tw;
	int lun;

	dump_stack();
	INFO_MSG("start release");

	ufstw_set_state(ufsf, TW_FAILED);

	if (ufsf->tw_dev_info.tw_buf_type == TW_BUF_TYPE_SHARED) {
		tw = ufsf->tw_lup[0];
		INFO_MSG("ufstw_lu[shared] %p", tw);
		ufsf->tw_lup[0] = NULL;
		ufstw_cancel_lu_jobs(tw);
		ufstw_remove_sysfs(tw);
		kfree(tw);
	} else {
	remove_wbfn_enable();
	remove_wbfn_dynamic_tw_enable();
		seq_scan_lu(lun) {
			tw = ufsf->tw_lup[lun];
			INFO_MSG("ufstw_lu[%d] %p", lun, tw);

			if (!tw)
				continue;
			ufsf->tw_lup[lun] = NULL;
			ufstw_cancel_lu_jobs(tw);
			ufstw_remove_sysfs(tw);
			kfree(tw);
		}
	}

	INFO_MSG("end release");
}

static void ufstw_reset_query_handling(struct ufstw_lu *tw)
{
	int ret;

	if (tw->tw_enable) {
		ret = ufstw_set_lu_flag(tw, QUERY_FLAG_IDN_WB_EN,
					&tw->tw_enable);
		if (ret)
			tw->tw_enable = false;
	}

	if (tw->flush_enable) {
		ret = ufstw_set_lu_flag(tw, QUERY_FLAG_IDN_WB_BUFF_FLUSH_EN,
					&tw->flush_enable);
		if (ret)
			tw->flush_enable = false;
	}

	if (tw->flush_during_hibern_enter) {
		ret = ufstw_set_lu_flag(tw,
					QUERY_FLAG_IDN_WB_BUFF_FLUSH_DURING_HIBERN8,
					&tw->flush_during_hibern_enter);
		if (ret)
			tw->flush_during_hibern_enter = false;
	}
}

void ufstw_reset_host(struct ufsf_feature *ufsf)
{
	struct ufstw_lu *tw;
	int lun;

	if (ufstw_is_not_present(ufsf))
		return;

	ufstw_set_state(ufsf, TW_RESET);
	if (ufsf->tw_dev_info.tw_buf_type == TW_BUF_TYPE_SHARED) {
		tw = ufsf->tw_lup[0];

		INFO_MSG("ufstw_lu[shared] cancel jobs");
		ufstw_cancel_lu_jobs(tw);
	} else {
		seq_scan_lu(lun) {
			tw = ufsf->tw_lup[lun];
			if (!tw)
				continue;

			INFO_MSG("ufstw_lu[%d] cancel jobs", lun);
			ufstw_cancel_lu_jobs(tw);
		}
	}
}

void ufstw_reset(struct ufsf_feature *ufsf, bool resume)
{
	struct ufstw_lu *tw;
	int lun;

	INFO_MSG("ufstw reset start. reason: %s",
		 resume ? "resume" : "reset");
	if (ufstw_get_state(ufsf) != TW_RESET) {
		ERR_MSG("tw_state error (%d)", ufstw_get_state(ufsf));
		return;
	}

	if (ufsf->tw_dev_info.tw_buf_type == TW_BUF_TYPE_SHARED) {
		tw = ufsf->tw_lup[0];

		INFO_MSG("ufstw_lu[shared] reset");
		ufstw_reset_query_handling(tw);
	} else {
		seq_scan_lu(lun) {
			tw = ufsf->tw_lup[lun];
			if (!tw)
				continue;

			INFO_MSG("ufstw_lu[%d] reset", lun);
			ufstw_reset_query_handling(tw);
		}
	}

	ufstw_set_state(ufsf, TW_PRESENT);
	INFO_MSG("ufstw reset finish");
}

#define ufstw_sysfs_attr_show_func(_query, _name, _IDN, hex)		\
static ssize_t ufstw_sysfs_show_##_name(struct ufstw_lu *tw, char *buf)	\
{									\
	int ret;							\
									\
	pm_runtime_get_sync(tw->ufsf->hba->dev);			\
	if (ufstw_is_not_present(tw->ufsf)) {				\
		pm_runtime_put_sync(tw->ufsf->hba->dev);		\
		return -ENODEV;						\
	}								\
									\
	ret = ufstw_read_lu_##_query(tw, _IDN, &tw->_name);		\
	pm_runtime_put_sync(tw->ufsf->hba->dev);			\
	if (ret)							\
		return -ENODEV;						\
									\
	INFO_MSG("read "#_query" "#_name" %u (0x%X)",			\
		 tw->_name, tw->_name);					\
	if (hex)							\
		return snprintf(buf, PAGE_SIZE, "0x%.2X\n", tw->_name);	\
	return snprintf(buf, PAGE_SIZE, "%u\n", tw->_name);		\
}

#define ufstw_sysfs_attr_store_func(_name, _IDN)			\
static ssize_t ufstw_sysfs_store_##_name(struct ufstw_lu *tw,		\
					 const char *buf,		\
					 size_t count)			\
{									\
	unsigned long val;						\
	ssize_t ret =  count;						\
									\
	if (kstrtoul(buf, 0, &val))					\
		return -EINVAL;						\
									\
	if (!(val == 0  || val == 1))					\
		return -EINVAL;						\
									\
	INFO_MSG("val %lu", val);					\
	pm_runtime_get_sync(tw->ufsf->hba->dev);			\
	if (ufstw_is_not_present(tw->ufsf)) {				\
		pm_runtime_put_sync(tw->ufsf->hba->dev);		\
		return -ENODEV;						\
	}								\
									\
	if (val) {							\
		if (ufstw_set_lu_flag(tw, _IDN, &tw->_name))		\
			ret = -ENODEV;					\
	} else {							\
		if (ufstw_clear_lu_flag(tw, _IDN, &tw->_name))		\
			ret = -ENODEV;					\
	}								\
	pm_runtime_put_sync(tw->ufsf->hba->dev);			\
									\
	INFO_MSG(#_name " query success");				\
	return ret;							\
}

ufstw_sysfs_attr_show_func(flag, tw_enable, QUERY_FLAG_IDN_WB_EN, 0);
ufstw_sysfs_attr_store_func(tw_enable, QUERY_FLAG_IDN_WB_EN);
ufstw_sysfs_attr_show_func(flag, flush_enable,
			   QUERY_FLAG_IDN_WB_BUFF_FLUSH_EN, 0);
ufstw_sysfs_attr_store_func(flush_enable, QUERY_FLAG_IDN_WB_BUFF_FLUSH_EN);
ufstw_sysfs_attr_show_func(flag, flush_during_hibern_enter,
			   QUERY_FLAG_IDN_WB_BUFF_FLUSH_DURING_HIBERN8, 0);
ufstw_sysfs_attr_store_func(flush_during_hibern_enter,
			    QUERY_FLAG_IDN_WB_BUFF_FLUSH_DURING_HIBERN8);

ufstw_sysfs_attr_show_func(attr, flush_status,
			   QUERY_ATTR_IDN_WB_FLUSH_STATUS, 0);
ufstw_sysfs_attr_show_func(attr, available_buffer_size,
			   QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE, 0);
ufstw_sysfs_attr_show_func(attr, lifetime_est,
			   QUERY_ATTR_IDN_WB_BUFF_LIFE_TIME_EST, 1);
ufstw_sysfs_attr_show_func(attr, curr_buffer_size,
			   QUERY_ATTR_IDN_CURR_WB_BUFF_SIZE, 0);

#define ufstw_sysfs_attr_ro(_name) __ATTR(_name, 0444, \
				      ufstw_sysfs_show_##_name, NULL)
#define ufstw_sysfs_attr_rw(_name) __ATTR(_name, 0644, \
				      ufstw_sysfs_show_##_name, \
				      ufstw_sysfs_store_##_name)

static struct ufstw_sysfs_entry ufstw_sysfs_entries[] = {
	/* Flag */
	ufstw_sysfs_attr_rw(tw_enable),
	ufstw_sysfs_attr_rw(flush_enable),
	ufstw_sysfs_attr_rw(flush_during_hibern_enter),
	/* Attribute */
	ufstw_sysfs_attr_ro(flush_status),
	ufstw_sysfs_attr_ro(available_buffer_size),
	ufstw_sysfs_attr_ro(lifetime_est),
	ufstw_sysfs_attr_ro(curr_buffer_size),
	__ATTR_NULL
};

static ssize_t ufstw_attr_show(struct kobject *kobj, struct attribute *attr,
			       char *page)
{
	struct ufstw_sysfs_entry *entry;
	struct ufstw_lu *tw;
	ssize_t error;

	entry = container_of(attr, struct ufstw_sysfs_entry, attr);
	tw = container_of(kobj, struct ufstw_lu, kobj);
	if (!entry->show)
		return -EIO;

	mutex_lock(&tw->sysfs_lock);
	error = entry->show(tw, page);
	mutex_unlock(&tw->sysfs_lock);
	return error;
}

static ssize_t ufstw_attr_store(struct kobject *kobj, struct attribute *attr,
				const char *page, size_t length)
{
	struct ufstw_sysfs_entry *entry;
	struct ufstw_lu *tw;
	ssize_t error;

	entry = container_of(attr, struct ufstw_sysfs_entry, attr);
	tw = container_of(kobj, struct ufstw_lu, kobj);

	if (!entry->store)
		return -EIO;

	mutex_lock(&tw->sysfs_lock);
	error = entry->store(tw, page, length);
	mutex_unlock(&tw->sysfs_lock);
	return error;
}

static const struct sysfs_ops ufstw_sysfs_ops = {
	.show = ufstw_attr_show,
	.store = ufstw_attr_store,
};

static struct kobj_type ufstw_ktype = {
	.sysfs_ops = &ufstw_sysfs_ops,
	.release = NULL,
};

static int ufstw_create_sysfs(struct ufsf_feature *ufsf, struct ufstw_lu *tw)
{
	struct device *dev = ufsf->hba->dev;
	struct ufstw_sysfs_entry *entry;
	int err;
	char lun_str[20] = { 0 };

	tw->sysfs_entries = ufstw_sysfs_entries;

	kobject_init(&tw->kobj, &ufstw_ktype);
	mutex_init(&tw->sysfs_lock);

	if (tw->lun == TW_LU_SHARED) {
		snprintf(lun_str, 6, "ufstw");
		INFO_MSG("ufstw creates sysfs ufstw-shared");
	} else {
		snprintf(lun_str, 10, "ufstw_lu%d", tw->lun);
		INFO_MSG("ufstw creates sysfs ufstw_lu%d", tw->lun);
	}

	err = kobject_add(&tw->kobj, kobject_get(&dev->kobj), lun_str);
	if (!err) {
		for (entry = tw->sysfs_entries; entry->attr.name != NULL;
		     entry++) {
			if (tw->lun == TW_LU_SHARED)
				INFO_MSG("ufstw-shared sysfs attr creates: %s",
					 entry->attr.name);
			else
				INFO_MSG("ufstw_lu(%d) sysfs attr creates: %s",
					 tw->lun, entry->attr.name);

			err = sysfs_create_file(&tw->kobj, &entry->attr);
			if (err) {
				ERR_MSG("create entry(%s) failed",
					entry->attr.name);
				goto kobj_del;
			}
		}
		kobject_uevent(&tw->kobj, KOBJ_ADD);
	} else {
		ERR_MSG("kobject_add failed");
	}

	return err;
kobj_del:
	err = kobject_uevent(&tw->kobj, KOBJ_REMOVE);
	INFO_MSG("kobject removed (%d)", err);
	kobject_del(&tw->kobj);
	return -EINVAL;
}

static inline void wbfn_enable_ctrl(struct ufstw_lu *tw, long val)
{
	switch (val) {
	case 0:
		ufstw_clear_lu_flag(tw, QUERY_FLAG_IDN_WB_EN,
				&tw->tw_enable);
		break;
	case 1:
		ufstw_set_lu_flag(tw, QUERY_FLAG_IDN_WB_EN,
				&tw->tw_enable);
		break;
	default:
		break;
	}
	return;
}

static ssize_t wbfn_enable_write(struct file *filp, const char *ubuf,
				 size_t cnt, loff_t *data)
{
	char buf[64] = {0};
	long val = 64;
	int lun;

	if (!ufsf_para.ufsf)
		return -EFAULT;
	if (cnt >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	if (buf[0] == '0')
		val = 0;
	else if (buf[0] == '1')
		val = 1;
	else
		val = 64;

	seq_scan_lu(lun) {
		if (ufsf_para.ufsf->tw_lup[lun])
			wbfn_enable_ctrl(ufsf_para.ufsf->tw_lup[lun], val);
	}

	return cnt;
}

static const struct file_operations wbfn_enable_fops = {
	.write = wbfn_enable_write,
};

static int create_wbfn_enable(void)
{
	struct proc_dir_entry *d_entry;

	if (!ufsf_para.ctrl_dir)
		return -EFAULT;

	d_entry = proc_create("wbfn_enable", S_IWUGO, ufsf_para.ctrl_dir,
			      &wbfn_enable_fops);
	if(!d_entry)
		return -ENOMEM;

	return 0;
}

static void remove_wbfn_enable(void)
{
	if (ufsf_para.ctrl_dir)
		remove_proc_entry("wbfn_enable", ufsf_para.ctrl_dir);

	return;
}

/* For the RUS gate of dynamic tw */
static inline void wbfn_dynamic_tw_enable_ctrl(struct ufstw_lu *tw, long val)
{
	int ret = 0;

	if (atomic_read(&tw->ufsf->tw_state) == TW_PRESENT) {
		INFO_MSG("val: %lu\n", val);
		switch (val) {
		case 0:
			tw->dynamic_tw_enable = false;
			/* mutex_lock(&tw->mode_lock); */
			ret = ufstw_set_lu_flag(tw, QUERY_FLAG_IDN_WB_EN, &tw->tw_enable);
			if(ret == 0) {
				INFO_MSG("ufstw_set_lu_flag success");
			}
			/* utex_unlock(&tw->mode_lock); */
			break;
		case 1:
			tw->dynamic_tw_enable = true;
			break;
		default:
			break;
		}
	} else {
		INFO_MSG("tw_state != TW_PRESENT (%d)\n", atomic_read(&tw->ufsf->tw_state));
	}

	return;
}

static ssize_t wbfn_dynamic_tw_enable_write(struct file *filp, const char *ubuf,
				 size_t cnt, loff_t *data)
{
	char buf[64] = {0};
	long val = 64;
	int lun;

	if (!ufsf_para.ufsf)
		return -EFAULT;
	if (cnt >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	if (buf[0] == '0')
		val = 0;
	else if (buf[0] == '1')
		val = 1;
	else
		val = 64;

	seq_scan_lu(lun) {
		if (ufsf_para.ufsf->tw_lup[lun])
			wbfn_dynamic_tw_enable_ctrl(ufsf_para.ufsf->tw_lup[lun], val);
	}

	return cnt;
}

static const struct file_operations wbfn_dynamic_tw_enable_fops = {
	.write = wbfn_dynamic_tw_enable_write,
};

static int create_wbfn_dynamic_tw_enable(void)
{
	struct proc_dir_entry *d_entry;

	if (!ufsf_para.ctrl_dir)
		return -EFAULT;

	d_entry = proc_create("wbfn_dynamic_tw_enable", S_IWUGO, ufsf_para.ctrl_dir,
			      &wbfn_dynamic_tw_enable_fops);
	if(!d_entry)
		return -ENOMEM;

	return 0;
}

static void remove_wbfn_dynamic_tw_enable(void)
{
	if (ufsf_para.ctrl_dir)
		remove_proc_entry("wbfn_dynamic_tw_enable", ufsf_para.ctrl_dir);

	return;
}
