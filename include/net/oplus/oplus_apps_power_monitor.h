/* SPDX-License-Identifier: GPL-2.0-only */
/*
* Copyright (C) 2018-2020 Oplus. All rights reserved.
*/

#ifndef _OPLUS_APPS_POWER_MONITOR_H
#define _OPLUS_APPS_POWER_MONITOR_H

#include <linux/types.h>
#include <linux/err.h>
#include <linux/netlink.h>

int app_monitor_dl_ctl_msg_handle(struct nlmsghdr *nlh);
int app_monitor_dl_report_msg_handle(struct nlmsghdr *nlh);
void oplus_app_power_monitor_init(void);
void oplus_app_power_monitor_fini(void);

#endif /* _OPLUS_APPS_POWER_MONITOR_H */
