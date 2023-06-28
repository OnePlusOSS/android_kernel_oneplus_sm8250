// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>

#include "game_ctrl.h"

struct proc_dir_entry *game_opt_dir = NULL;

static int __init game_ctrl_init(void)
{
	game_opt_dir = proc_mkdir("game_opt", NULL);
	if (!game_opt_dir) {
		pr_err("fail to mkdir /proc/game_opt\n");
		return -ENOMEM;
	}

	cpu_load_init();
	cpufreq_limits_init();
	task_util_init();
	rt_info_init();
	dstate_dump_init();

	return 0;
}

static void __exit game_ctrl_exit(void)
{
}

module_init(game_ctrl_init);
module_exit(game_ctrl_exit);
MODULE_LICENSE("GPL v2");
