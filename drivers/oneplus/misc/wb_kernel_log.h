/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _WB_KERNEL_LOG_H
#define _WB_KERNEL_LOG_H

#define LOG_PAGE_SIZE 4096
#define LOG_SECTOR_SIZE 4096
#define SEGMENT_SIZE 256 //256 * LOG_PAGE_SIZE
#define LOG_TIME 120 //LOG_TINE * 500ms
#define HEADER_SHIFT 1 //LOG_TINE * 500ms
#define RECORD_MAXIMUM 7
#define HEADER_SIZE 7
#define SECTOR_OFFSET_MAXIMUM 16192
#define EOL_SIZE 1
#define NEED_EOL 0xffffffff

struct log_segment_status {
	char OPlogheader[HEADER_SIZE];
	char klog_boot_count;
};

struct printk_log_dup {
	u64 ts_nsec;		/* timestamp in nanoseconds */
	u16 len;		/* length of entire record */
	u16 text_len;		/* length of text buffer */
	u16 dict_len;		/* length of dictionary buffer */
	u8 facility;		/* syslog facility */
	u8 flags:5;		/* internal record flags */
	u8 level:3;		/* syslog level */
};

#endif
