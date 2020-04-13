/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#if defined(CONFIG_QTI_RPM_STATS_LOG)

void msm_rpmh_master_stats_update(void);

#else

static inline void msm_rpmh_master_stats_update(void) {}

#endif
extern int rpmh_master_stats_open(struct inode *inode, struct file *file);
