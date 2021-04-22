/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#if defined(CONFIG_QTI_RPM_STATS_LOG)

void msm_rpmh_master_stats_update(void);

#else

static inline void msm_rpmh_master_stats_update(void) {}

#endif
extern bool get_apps_stats(bool start);
extern bool get_subsystem_stats(bool start);
extern void rpmhstats_statistics(void);
extern int rpmh_master_stats_open(struct inode *inode, struct file *file);
extern ssize_t master_stats_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
