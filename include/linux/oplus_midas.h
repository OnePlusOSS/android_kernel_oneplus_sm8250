/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2019-2020 Oplus. All rights reserved.
 */

#ifndef __OPLUS_MIDAS_H__
#define __OPLUS_MIDAS_H__

#include <linux/sched.h>

#ifdef CONFIG_OPLUS_FEATURE_MIDAS
void midas_record_task_times(uid_t uid, u64 cputime,
                    struct task_struct *p, unsigned int state);
#else
static inline void midas_record_task_times(uid_t uid, u64 cputime,
                    struct task_struct *p, unsigned int state) { }
#endif

#endif /* __OPLUS_MIDAS_H__ */
