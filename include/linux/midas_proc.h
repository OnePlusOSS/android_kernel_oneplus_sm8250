/*
 *  SPDX-License-Identifier: GPL-2.0-only
 *
 *  Copyright (C) 2019-2020 Oplus. All rights reserved.
 */

#ifndef __MIDAS_PROC_H__
#define __MIDAS_PROC_H__

#include <linux/sched.h>

#define STATE_MAX    60
#define CNT_MAX      1024

enum {
	TYPE_UID = 0,
	TYPE_RPID,
	TYPE_SPID,
	TYPE_TOTAL,
};

enum {
    ID_PID = 0,
    ID_TGID,
    ID_UID,
    ID_TOTAL,
};

struct state_inst {
    unsigned int id[ID_TOTAL];
    char name[TASK_COMM_LEN];
    unsigned long long time_in_state[STATE_MAX];
};

struct midas_id_state {
    unsigned int cnt;
    struct state_inst insts[CNT_MAX];
};

#endif
