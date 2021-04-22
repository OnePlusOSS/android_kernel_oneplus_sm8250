// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 , Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/thermal.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/spinlock.h>

#ifdef CONFIG_HOUSTON
#include <oneplus/houston/houston_helper.h>
#endif

enum {
	SHELL_FRONT = 0,
	SHELL_FRAME,
	SHELL_BACK,
	SHELL_MAX,
};

static DEFINE_IDA(shell_temp_ida);
static DEFINE_SPINLOCK(horae_lock);
static int shell_temp[SHELL_MAX];

struct horae_shell_temp {
	struct thermal_zone_device *tzd;
	int shell_id;
};

static const struct of_device_id horae_shell_of_match[] = {
	{ .compatible = "oneplus,shell-temp" },
	{},
};

static int horae_get_shell_temp(struct thermal_zone_device *tz,
					int *temp)
{
	struct horae_shell_temp *hst;

	if (!temp || !tz)
		return -EINVAL;

	hst = tz->devdata;
	*temp = shell_temp[hst->shell_id];
	return 0;
}

struct thermal_zone_device_ops shell_thermal_zone_ops = {
	.get_temp = horae_get_shell_temp,
};

static int horae_shell_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *dev_node = dev->of_node;
	struct thermal_zone_device *tz_dev;
	struct horae_shell_temp *hst;
	int ret = 0;
	int result;

	if (!of_device_is_available(dev_node)) {
		pr_err("shell-temp dev not found\n");
		return -ENODEV;
	}

	hst = kzalloc(sizeof(struct horae_shell_temp), GFP_KERNEL);
	if (!hst)
		return -ENOMEM;

	result = ida_simple_get(&shell_temp_ida, 0, 0, GFP_KERNEL);
	if (result < 0) {
		pr_err("genernal horae id failed\n");
		ret = -EINVAL;
		goto err_free_mem;
	}

	hst->shell_id = result;

	tz_dev = thermal_zone_device_register(dev_node->name,
			0, 0, hst, &shell_thermal_zone_ops, NULL, 0, 0);
	if (IS_ERR_OR_NULL(tz_dev)) {
		pr_err("register thermal zone for shell failed\n");
		ret = -ENODEV;
		goto err_remove_id;
	}
#ifdef CONFIG_HOUSTON
	ht_register_thermal_zone_device(tz_dev);
#endif
	hst->tzd = tz_dev;

	platform_set_drvdata(pdev, hst);

	return 0;

err_remove_id:
	ida_simple_remove(&shell_temp_ida, result);
err_free_mem:
	kfree(hst);
	return ret;
}

static int horae_shell_remove(struct platform_device *pdev)
{
	struct horae_shell_temp *hst = platform_get_drvdata(pdev);

	if (hst) {
		platform_set_drvdata(pdev, NULL);
		thermal_zone_device_unregister(hst->tzd);
		kfree(hst);
	}

	return 0;
}

static struct platform_driver horae_shell_platdrv = {
	.driver = {
		.name		= "horae-shell-temp",
		.owner		= THIS_MODULE,
		.of_match_table = horae_shell_of_match,
	},
	.probe	= horae_shell_probe,
	.remove = horae_shell_remove,
};

#define BUF_LEN		256
static ssize_t proc_shell_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *pos)
{
	int ret, temp, len;
	unsigned int index = 0;
	char tmp[BUF_LEN + 1];
	unsigned long flags;


	if (count == 0)
		return 0;

	len = count > BUF_LEN ? BUF_LEN : count;

	ret = copy_from_user(tmp, buf, len);
	if (ret) {
		pr_err("copy_from_user failed, ret=%d\n", ret);
		return count;
	}

	if (tmp[len - 1] == '\n')
		tmp[len - 1] = '\0';
	else
		tmp[len] = '\0';

	ret = sscanf(tmp, "%d %d", &index, &temp);
	if (ret < 2) {
		pr_err("write failed, ret=%d\n", ret);
		return count;
	}

	if (index >= SHELL_MAX) {
		pr_err("write invalid para\n");
		return count;
	}

	spin_lock_irqsave(&horae_lock, flags);
	shell_temp[index] = temp;
	spin_unlock_irqrestore(&horae_lock, flags);

	return count;
}

static const struct file_operations proc_shell_fops = {
	.write = proc_shell_write,
};

static int __init horae_shell_init(void)
{
	struct proc_dir_entry *shell_proc_entry;

	shell_proc_entry = proc_create("shell-temp", 0664, NULL, &proc_shell_fops);
	if (!shell_proc_entry) {
		pr_err("shell-temp proc create failed\n");
		return -EINVAL;
	}

	spin_lock_init(&horae_lock);

	return platform_driver_register(&horae_shell_platdrv);
}

static void __exit horae_shell_exit(void)
{
	platform_driver_unregister(&horae_shell_platdrv);
}


module_init(horae_shell_init);
module_exit(horae_shell_exit);
