// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

/****************************************************************
 ** File: - ufs_signal_quality.c
 ** Description: record ufs error interrupt information
 ** Version: 1.0
 ** Date : 2020-08-27
 **
 ** ----------------------Revision History: --------------------
 **  <author>      <date>      <version>      <desc>
 **  jason.wu      2020-08-27   1.0            add this module
 ****************************************************************/
#include <linux/bitops.h>
#include "ufs_signal_quality.h"
#include "ufshci.h"

static void recordTimeStamp(
	struct signal_quality *record,
	enum unipro_err_type type
) {
	ktime_t cur_time = ktime_get();
	switch (type) {
	case UNIPRO_ERR_PA:
	case UNIPRO_ERR_DL:
	case UNIPRO_ERR_NL:
	case UNIPRO_ERR_TL:
	case UNIPRO_ERR_DME:
		if (STAMP_RECORD_MAX <= record->stamp_pos) {
			return;
		}
		if (0 == record->stamp_pos) {
			record->stamp[0] = cur_time;
		} else if (cur_time > (record->stamp[record->stamp_pos - 1] +
		        STAMP_MIN_INTERVAL)) {
			record->stamp[record->stamp_pos++] = cur_time;
		}
		return;
	default:
		return;
	}
}

void recordUniproErr(
	struct unipro_signal_quality_ctrl *signalCtrl,
	u32 reg,
	enum unipro_err_type type
) {
	unsigned long err_bits;
	int ec;
	struct signal_quality *rec = &signalCtrl->record;
	recordTimeStamp(rec, type);
	switch (type) {
	case UNIPRO_ERR_FATAL:
		if (DEVICE_FATAL_ERROR & reg) {
			rec->ufs_device_err_cnt++;
		}
		if (CONTROLLER_FATAL_ERROR & reg) {
			rec->ufs_host_err_cnt++;
		}
		if (SYSTEM_BUS_FATAL_ERROR & reg) {
			rec->ufs_bus_err_cnt++;
		}
		if (CRYPTO_ENGINE_FATAL_ERROR & reg) {
			rec->ufs_crypto_err_cnt++;
		}
		break;
	case UNIPRO_ERR_LINK:
		if (UIC_LINK_LOST & reg) {
			rec->ufs_link_lost_cnt++;
		}
		break;
	case UNIPRO_ERR_PA:
		err_bits = reg & UIC_PHY_ADAPTER_LAYER_ERROR_CODE_MASK;
		for_each_set_bit(ec, &err_bits, UNIPRO_PA_ERR_MAX) {
			rec->unipro_PA_err_total_cnt++;
			rec->unipro_PA_err_cnt[ec]++;
		}
		break;
	case UNIPRO_ERR_DL:
		err_bits = reg & UIC_DATA_LINK_LAYER_ERROR_CODE_MASK;
		for_each_set_bit(ec, &err_bits, UNIPRO_DL_ERR_MAX) {
			rec->unipro_DL_err_total_cnt++;
			rec->unipro_DL_err_cnt[ec]++;
		}
		break;
	case UNIPRO_ERR_NL:
		err_bits = reg & UIC_NETWORK_LAYER_ERROR_CODE_MASK;
		for_each_set_bit(ec, &err_bits, UNIPRO_NL_ERR_MAX) {
			rec->unipro_NL_err_total_cnt++;
			rec->unipro_NL_err_cnt[ec]++;
		}
		break;
	case UNIPRO_ERR_TL:
		err_bits = reg & UIC_TRANSPORT_LAYER_ERROR_CODE_MASK;
		for_each_set_bit(ec, &err_bits, UNIPRO_TL_ERR_MAX) {
			rec->unipro_TL_err_total_cnt++;
			rec->unipro_TL_err_cnt[ec]++;
		}
		break;
	case UNIPRO_ERR_DME:
		err_bits = reg & UIC_DME_ERROR_CODE_MASK;
		for_each_set_bit(ec, &err_bits, UNIPRO_DME_ERR_MAX) {
			rec->unipro_DME_err_total_cnt++;
			rec->unipro_DME_err_cnt[ec]++;
		}
		break;
	default:
		break;
	}
}

#define SEQ_EASY_PRINT(x)   seq_printf(s, #x"\t%d\n", signalCtrl->record.x)
#define SEQ_PA_PRINT(x)     \
	seq_printf(s, #x"\t%d\n", signalCtrl->record.unipro_PA_err_cnt[x])
#define SEQ_DL_PRINT(x)     \
	seq_printf(s, #x"\t%d\n", signalCtrl->record.unipro_DL_err_cnt[x])
#define SEQ_NL_PRINT(x)     \
	seq_printf(s, #x"\t%d\n", signalCtrl->record.unipro_NL_err_cnt[x])
#define SEQ_TL_PRINT(x)     \
	seq_printf(s, #x"\t%d\n", signalCtrl->record.unipro_TL_err_cnt[x])
#define SEQ_DME_PRINT(x)    \
	seq_printf(s, #x"\t%d\n", signalCtrl->record.unipro_DME_err_cnt[x])
#define SEQ_STAMP_PRINT(x)  \
	seq_printf(s, #x"\t%lld\n", signalCtrl->record.stamp[x])
#define SEQ_U64_PRINT(x)   seq_printf(s, #x"\t%lld\n", signalCtrl->record.x)

static int record_read_func(struct seq_file *s, void *v)
{
	struct unipro_signal_quality_ctrl *signalCtrl =
	    (struct unipro_signal_quality_ctrl *)(s->private);
	if (!signalCtrl) {
		return -EINVAL;
	}
	SEQ_EASY_PRINT(ufs_device_err_cnt);
	SEQ_EASY_PRINT(ufs_host_err_cnt);
	SEQ_EASY_PRINT(ufs_bus_err_cnt);
	SEQ_EASY_PRINT(ufs_crypto_err_cnt);
	SEQ_EASY_PRINT(ufs_link_lost_cnt);
	SEQ_EASY_PRINT(unipro_PA_err_total_cnt);
	SEQ_PA_PRINT(UNIPRO_PA_LANE0_ERR_CNT);
	SEQ_PA_PRINT(UNIPRO_PA_LANE1_ERR_CNT);
	SEQ_PA_PRINT(UNIPRO_PA_LANE2_ERR_CNT);
	SEQ_PA_PRINT(UNIPRO_PA_LANE3_ERR_CNT);
	SEQ_PA_PRINT(UNIPRO_PA_LINE_RESET);
	SEQ_EASY_PRINT(unipro_DL_err_total_cnt);
	SEQ_DL_PRINT(UNIPRO_DL_NAC_RECEIVED);
	SEQ_DL_PRINT(UNIPRO_DL_TCX_REPLAY_TIMER_EXPIRED);
	SEQ_DL_PRINT(UNIPRO_DL_AFCX_REQUEST_TIMER_EXPIRED);
	SEQ_DL_PRINT(UNIPRO_DL_FCX_PROTECTION_TIMER_EXPIRED);
	SEQ_DL_PRINT(UNIPRO_DL_CRC_ERROR);
	SEQ_DL_PRINT(UNIPRO_DL_RX_BUFFER_OVERFLOW);
	SEQ_DL_PRINT(UNIPRO_DL_MAX_FRAME_LENGTH_EXCEEDED);
	SEQ_DL_PRINT(UNIPRO_DL_WRONG_SEQUENCE_NUMBER);
	SEQ_DL_PRINT(UNIPRO_DL_AFC_FRAME_SYNTAX_ERROR);
	SEQ_DL_PRINT(UNIPRO_DL_NAC_FRAME_SYNTAX_ERROR);
	SEQ_DL_PRINT(UNIPRO_DL_EOF_SYNTAX_ERROR);
	SEQ_DL_PRINT(UNIPRO_DL_FRAME_SYNTAX_ERROR);
	SEQ_DL_PRINT(UNIPRO_DL_BAD_CTRL_SYMBOL_TYPE);
	SEQ_DL_PRINT(UNIPRO_DL_PA_INIT_ERROR);
	SEQ_DL_PRINT(UNIPRO_DL_PA_ERROR_IND_RECEIVED);
	SEQ_DL_PRINT(UNIPRO_DL_PA_INIT);
	SEQ_EASY_PRINT(unipro_NL_err_total_cnt);
	SEQ_NL_PRINT(UNIPRO_NL_UNSUPPORTED_HEADER_TYPE);
	SEQ_NL_PRINT(UNIPRO_NL_BAD_DEVICEID_ENC);
	SEQ_NL_PRINT(UNIPRO_NL_LHDR_TRAP_PACKET_DROPPING);
	SEQ_EASY_PRINT(unipro_TL_err_total_cnt);
	SEQ_TL_PRINT(UNIPRO_TL_UNSUPPORTED_HEADER_TYPE);
	SEQ_TL_PRINT(UNIPRO_TL_UNKNOWN_CPORTID);
	SEQ_TL_PRINT(UNIPRO_TL_NO_CONNECTION_RX);
	SEQ_TL_PRINT(UNIPRO_TL_CONTROLLED_SEGMENT_DROPPING);
	SEQ_TL_PRINT(UNIPRO_TL_BAD_TC);
	SEQ_TL_PRINT(UNIPRO_TL_E2E_CREDIT_OVERFLOW);
	SEQ_TL_PRINT(UNIPRO_TL_SAFETY_VALVE_DROPPING);
	SEQ_EASY_PRINT(unipro_DME_err_total_cnt);
	SEQ_DME_PRINT(UNIPRO_DME_GENERIC);
	SEQ_DME_PRINT(UNIPRO_DME_TX_QOS);
	SEQ_DME_PRINT(UNIPRO_DME_RX_QOS);
	SEQ_DME_PRINT(UNIPRO_DME_PA_INIT_QOS);
	SEQ_STAMP_PRINT(UNIPRO_0_STAMP);
	SEQ_STAMP_PRINT(UNIPRO_1_STAMP);
	SEQ_STAMP_PRINT(UNIPRO_2_STAMP);
	SEQ_STAMP_PRINT(UNIPRO_3_STAMP);
	SEQ_STAMP_PRINT(UNIPRO_4_STAMP);
	SEQ_STAMP_PRINT(UNIPRO_5_STAMP);
	SEQ_STAMP_PRINT(UNIPRO_6_STAMP);
	SEQ_STAMP_PRINT(UNIPRO_7_STAMP);
	SEQ_STAMP_PRINT(UNIPRO_8_STAMP);
	SEQ_STAMP_PRINT(UNIPRO_9_STAMP);
	SEQ_U64_PRINT(request_cnt);
	return 0;
}

static int record_open(struct inode *inode, struct file *file)
{
	return single_open(file, record_read_func, PDE_DATA(inode));
}

static const struct file_operations record_fops = {
	.owner = THIS_MODULE,
	.open = record_open,
	.read = seq_read,
	.release = single_release,
};

#define SEQ_UPLOAD_PRINT(x) \
	seq_printf(s, #x": %d\n", signalCtrl->record.x \
		-signalCtrl->record_upload.x);\
	signalCtrl->record_upload.x = signalCtrl->record.x;
#define SEQ_UPLOAD_STAMP_PRINT(x) \
	seq_printf(s, #x": %lld\n", signalCtrl->record.stamp[x] \
		-signalCtrl->record_upload.stamp[x]);\
	signalCtrl->record_upload.stamp[x] = signalCtrl->record.stamp[x];
#define SEQ_UPLOAD_U64_PRINT(x) \
	seq_printf(s, #x": %lld\n", signalCtrl->record.x \
		-signalCtrl->record_upload.x);\
	signalCtrl->record_upload.x = signalCtrl->record.x;
static int record_upload_read_func(struct seq_file *s, void *v)
{
	struct unipro_signal_quality_ctrl *signalCtrl =
	    (struct unipro_signal_quality_ctrl *)(s->private);
	if (!signalCtrl) {
		return -EINVAL;
	}
	SEQ_UPLOAD_PRINT(ufs_device_err_cnt);
	SEQ_UPLOAD_PRINT(ufs_host_err_cnt);
	SEQ_UPLOAD_PRINT(ufs_bus_err_cnt);
	SEQ_UPLOAD_PRINT(ufs_crypto_err_cnt);
	SEQ_UPLOAD_PRINT(ufs_link_lost_cnt);
	SEQ_UPLOAD_PRINT(unipro_PA_err_total_cnt);
	SEQ_UPLOAD_PRINT(unipro_DL_err_total_cnt);
	SEQ_UPLOAD_PRINT(unipro_NL_err_total_cnt);
	SEQ_UPLOAD_PRINT(unipro_TL_err_total_cnt);
	SEQ_UPLOAD_PRINT(unipro_DME_err_total_cnt);
	SEQ_UPLOAD_STAMP_PRINT(UNIPRO_0_STAMP);
	SEQ_UPLOAD_U64_PRINT(request_cnt);
	return 0;
}

static int record_upload_open(struct inode *inode, struct file *file)
{
	return single_open(file, record_upload_read_func, PDE_DATA(inode));
}

static const struct file_operations record_upload_fops = {
	.owner = THIS_MODULE,
	.open = record_upload_open,
	.read = seq_read,
	.release = single_release,
};

int create_signal_quality_proc(struct unipro_signal_quality_ctrl *signalCtrl)
{
	struct proc_dir_entry *d_entry;
	signalCtrl->ctrl_dir = proc_mkdir("ufs_signalShow", NULL);
	if (!signalCtrl->ctrl_dir) {
		return -ENOMEM;
	}
	d_entry = proc_create_data("record", S_IRUGO, signalCtrl->ctrl_dir,
	        &record_fops, signalCtrl);
	if (!d_entry) {
		return -ENOMEM;
	}
	d_entry = proc_create_data("record_upload", S_IRUGO, signalCtrl->ctrl_dir,
	        &record_upload_fops, signalCtrl);
	if (!d_entry) {
		return -ENOMEM;
	}
	return 0;
}

void remove_signal_quality_proc(struct unipro_signal_quality_ctrl *signalCtrl)
{
	if (signalCtrl->ctrl_dir) {
		remove_proc_entry("record", signalCtrl->ctrl_dir);
		remove_proc_entry("record_upload", signalCtrl->ctrl_dir);
	}
	return;
}


