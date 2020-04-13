// SPDX-License-Identifier: GPL-2.0
/*
 * Power trace points
 *
 * Copyright (C) 2009 Arjan van de Ven <arjan@linux.intel.com>
 */

#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/module.h>

#define CREATE_TRACE_POINTS
#include <trace/events/power.h>

EXPORT_TRACEPOINT_SYMBOL_GPL(suspend_resume);
EXPORT_TRACEPOINT_SYMBOL_GPL(cpu_idle);
EXPORT_TRACEPOINT_SYMBOL_GPL(cpu_frequency);
EXPORT_TRACEPOINT_SYMBOL_GPL(powernv_throttle);
// rock.lin@ASTI, 2019/12/12, add for pccore CONFIG_PCCORE
EXPORT_TRACEPOINT_SYMBOL_GPL(cpu_frequency_select);

