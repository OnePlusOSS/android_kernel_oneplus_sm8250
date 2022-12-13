/**
 * Copyright 2008-2013 OPLUS Mobile Comm Corp., Ltd, All rights reserved.
 * FileName:devinfo.h
 * ModuleName:devinfo
 * Create Date: 2013-10-23
 * Description:add interface to get device information.
*/

#ifndef _DEVICE_INFO_H
#define _DEVICE_INFO_H

#include <linux/list.h>

#define INFO_LEN  24

typedef enum
{
	DDR_TYPE_LPDDR1 = 0, /**< Low power DDR1. */
	DDR_TYPE_LPDDR2 = 2, /**< Low power DDR2  set to 2 for compatibility*/
	DDR_TYPE_PCDDR2 = 3, /**< Personal computer DDR2. */
	DDR_TYPE_PCDDR3 = 4, /**< Personal computer DDR3. */

	DDR_TYPE_LPDDR3 = 5, /**< Low power DDR3. */
	DDR_TYPE_LPDDR4 = 6, /**< Low power DDR4. */
	DDR_TYPE_LPDDR4X = 7, /**< Low power DDR4x. */

	DDR_TYPE_LPDDR5 = 8, /**< Low power DDR5. */
	DDR_TYPE_LPDDR5X = 9, /**< Low power DDR5x. */
	DDR_TYPE_UNUSED = 0x7FFFFFFF /**< For compatibility with deviceprogrammer(features not using DDR). */
} DDR_TYPE;

struct manufacture_info {
	char name[INFO_LEN];
	char *version;
	char *manufacture;
	char *fw_path;
};

struct o_hw_id {
	const char *label;
	const char *match;
	int id;
	struct list_head list;
};

struct o_ufsplus_status {
	int *hpb_status;
	int *tw_status;
};


int register_device_proc(char *name, char *version, char *vendor);
int register_device_proc_for_ufsplus(char *name, int *hpb_status,int *tw_status);
int register_devinfo(char *name, struct manufacture_info *info);
bool check_id_match(const char *label, const char *id_match, int id);

#endif
