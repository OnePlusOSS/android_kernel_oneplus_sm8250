/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

/****************************************************************
 ** File: - ufs_signal_quality.h
 ** Description: record ufs error interrupt information
 ** Version: 1.0
 ** Date : 2020-08-27
 **
 ** ----------------------Revision History: --------------------
 **  <author>      <date>      <version>      <desc>
 **  jason.wu      2020-08-27   1.0            add this module
 ****************************************************************/
#ifndef __UFS_SIGNAL_QUALITY_H__
#define __UFS_SIGNAL_QUALITY_H__

#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/ktime.h>

enum unipro_pa_errCode {
	UNIPRO_PA_LANE0_ERR_CNT,
	UNIPRO_PA_LANE1_ERR_CNT,
	UNIPRO_PA_LANE2_ERR_CNT,
	UNIPRO_PA_LANE3_ERR_CNT,
	UNIPRO_PA_LINE_RESET,
	UNIPRO_PA_ERR_MAX
};

enum unipro_dl_errCode {
	UNIPRO_DL_NAC_RECEIVED,
	UNIPRO_DL_TCX_REPLAY_TIMER_EXPIRED,
	UNIPRO_DL_AFCX_REQUEST_TIMER_EXPIRED,
	UNIPRO_DL_FCX_PROTECTION_TIMER_EXPIRED,
	UNIPRO_DL_CRC_ERROR,
	UNIPRO_DL_RX_BUFFER_OVERFLOW,
	UNIPRO_DL_MAX_FRAME_LENGTH_EXCEEDED,
	UNIPRO_DL_WRONG_SEQUENCE_NUMBER,
	UNIPRO_DL_AFC_FRAME_SYNTAX_ERROR,
	UNIPRO_DL_NAC_FRAME_SYNTAX_ERROR,
	UNIPRO_DL_EOF_SYNTAX_ERROR,
	UNIPRO_DL_FRAME_SYNTAX_ERROR,
	UNIPRO_DL_BAD_CTRL_SYMBOL_TYPE,
	UNIPRO_DL_PA_INIT_ERROR,
	UNIPRO_DL_PA_ERROR_IND_RECEIVED,
	UNIPRO_DL_PA_INIT,
	UNIPRO_DL_ERR_MAX
};

enum unipro_nl_errCode {
	UNIPRO_NL_UNSUPPORTED_HEADER_TYPE,
	UNIPRO_NL_BAD_DEVICEID_ENC,
	UNIPRO_NL_LHDR_TRAP_PACKET_DROPPING,
	UNIPRO_NL_ERR_MAX
};

enum unipro_tl_errCode {
	UNIPRO_TL_UNSUPPORTED_HEADER_TYPE,
	UNIPRO_TL_UNKNOWN_CPORTID,
	UNIPRO_TL_NO_CONNECTION_RX,
	UNIPRO_TL_CONTROLLED_SEGMENT_DROPPING,
	UNIPRO_TL_BAD_TC,
	UNIPRO_TL_E2E_CREDIT_OVERFLOW,
	UNIPRO_TL_SAFETY_VALVE_DROPPING,
	UNIPRO_TL_ERR_MAX
};

enum unipro_dme_errCode {
	UNIPRO_DME_GENERIC,
	UNIPRO_DME_TX_QOS,
	UNIPRO_DME_RX_QOS,
	UNIPRO_DME_PA_INIT_QOS,
	UNIPRO_DME_ERR_MAX
};

enum unipro_err_type {
	UNIPRO_ERR_FATAL,
	UNIPRO_ERR_LINK,
	UNIPRO_ERR_PA,
	UNIPRO_ERR_DL,
	UNIPRO_ERR_NL,
	UNIPRO_ERR_TL,
	UNIPRO_ERR_DME
};

enum unipro_err_time_stamp {
	UNIPRO_0_STAMP,
	UNIPRO_1_STAMP,
	UNIPRO_2_STAMP,
	UNIPRO_3_STAMP,
	UNIPRO_4_STAMP,
	UNIPRO_5_STAMP,
	UNIPRO_6_STAMP,
	UNIPRO_7_STAMP,
	UNIPRO_8_STAMP,
	UNIPRO_9_STAMP,
	STAMP_RECORD_MAX
};
#define STAMP_MIN_INTERVAL ((ktime_t)600000000000) /*ns, 10min*/

struct signal_quality {
	u32 ufs_device_err_cnt;
	u32 ufs_host_err_cnt;
	u32 ufs_bus_err_cnt;
	u32 ufs_crypto_err_cnt;
	u32 ufs_link_lost_cnt;
	u32 unipro_PA_err_total_cnt;
	u32 unipro_PA_err_cnt[UNIPRO_PA_ERR_MAX];
	u32 unipro_DL_err_total_cnt;
	u32 unipro_DL_err_cnt[UNIPRO_DL_ERR_MAX];
	u32 unipro_NL_err_total_cnt;
	u32 unipro_NL_err_cnt[UNIPRO_NL_ERR_MAX];
	u32 unipro_TL_err_total_cnt;
	u32 unipro_TL_err_cnt[UNIPRO_TL_ERR_MAX];
	u32 unipro_DME_err_total_cnt;
	u32 unipro_DME_err_cnt[UNIPRO_DME_ERR_MAX];
	u64 request_cnt;
/* first 10 error cnt, interval is 10min at least */
	ktime_t stamp[STAMP_RECORD_MAX];
	int stamp_pos;
};

struct unipro_signal_quality_ctrl {
	struct proc_dir_entry *ctrl_dir;
	struct signal_quality record;
	struct signal_quality record_upload;
};

void recordUniproErr(
	struct unipro_signal_quality_ctrl *signalCtrl,
	u32 reg,
	enum unipro_err_type type
);
static inline void recordRequestCnt(struct unipro_signal_quality_ctrl *signalCtrl)
{
	signalCtrl->record.request_cnt++;
}

int create_signal_quality_proc(struct unipro_signal_quality_ctrl *signalCtrl);
void remove_signal_quality_proc(struct unipro_signal_quality_ctrl *signalCtrl);

#endif /* __UFS_SIGNAL_QUALITY_H__ */

