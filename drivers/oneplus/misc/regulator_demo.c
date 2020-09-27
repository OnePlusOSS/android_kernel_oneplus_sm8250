/*
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>

#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#include <linux/sort.h>
#include <linux/seq_file.h>
#include "../../../fs/proc/internal.h"
#include <linux/regulator/consumer.h>

#define DRV_NAME    "regulator_demo"

struct regulator_demo_dev_data {
	struct device *dev;
	int regulator_demo_value;
	struct regulator *regulator_demo;
};

static struct regulator_demo_dev_data *regulator_demo_data;

static int regulator_demo_state_show(struct seq_file *seq, void *offset)
{
	seq_printf(seq, "%d\n", regulator_demo_data->regulator_demo_value);
	return 0;
}

static ssize_t regulator_demo_state_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos)
{
	int regulator_demo_value = 0;
	char buf[10]={0};
	int ret;

	if (!regulator_demo_data) {
		return -EFAULT;
	}

	if( copy_from_user(buf, buffer, sizeof (buf)) ){
		return count;
	}

	snprintf(buf, sizeof(buf), "%d", &regulator_demo_value);
	printk("%s before(%d),current(%d) \n",__func__,regulator_demo_data->regulator_demo_value,regulator_demo_value);
	regulator_demo_data->regulator_demo_value=regulator_demo_value;

	if (regulator_count_voltages(regulator_demo_data->regulator_demo) > 0) {
		ret = regulator_set_voltage(regulator_demo_data->regulator_demo, regulator_demo_value, regulator_demo_value);
		if (ret) {
			pr_err( "Regulator set demo fail rc = %d\n", ret);
			goto regulator_demo_put;
		}

		ret = regulator_set_load(regulator_demo_data->regulator_demo, 200000);
		if (ret < 0) {
			pr_err( "Failed to set demo mode(rc:%d)\n", ret);
			goto regulator_demo_put;
		}
	}
regulator_demo_put:
	return count;
}

static int regulator_demo_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, regulator_demo_state_show, regulator_demo_data);
}

static const struct file_operations regulator_demo_state_proc_fops = {
	.owner      = THIS_MODULE,
	.open       = regulator_demo_state_open,
	.read       = seq_read,
	.llseek     = seq_lseek,
	.release    = single_release,
	.write      = regulator_demo_state_write,
};

static int regulator_demo_init(struct platform_device *pdev)
{
	int ret = 0;
	printk("%s \n",__func__);

	regulator_demo_data->regulator_demo = regulator_get(&pdev->dev, "regulator_demo");
	if (IS_ERR_OR_NULL(regulator_demo_data->regulator_demo)) {
		pr_err("Regulator get failed vcc_1v8, ret = %d\n", ret);
	} else {
		if (regulator_count_voltages(regulator_demo_data->regulator_demo) > 0) {
			ret = regulator_set_voltage(regulator_demo_data->regulator_demo, 2000000, 2000000);
			if (ret) {
				pr_err( "Regulator set_vtg failed vcc_i2c rc = %d\n", ret);
				goto regulator_demo_put;
			}

			ret = regulator_set_load(regulator_demo_data->regulator_demo, 200000);
			if (ret < 0) {
				pr_err( "Failed to set vcc_1v8 mode(rc:%d)\n", ret);
				goto regulator_demo_put;
			}
		}
	}

	return 0;

regulator_demo_put:
	regulator_put(regulator_demo_data->regulator_demo);
	regulator_demo_data->regulator_demo = NULL;
	return ret;
}


static int regulator_demo_dev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int error=0;

	printk(KERN_ERR"%s\n",__func__);

	regulator_demo_data = kzalloc(sizeof(struct regulator_demo_dev_data), GFP_KERNEL);
	regulator_demo_data->dev = dev;

	if (!proc_create_data("regulator_demo", 0666, NULL, &regulator_demo_state_proc_fops, regulator_demo_data)) {
		error = -ENOMEM;
		goto err_set_regulator_demo;
	}

	error= regulator_demo_init(pdev);
	if (error < 0) {
		printk(KERN_ERR "%s: regulator_demo_pinctrl_init, err=%d", __func__, error);
		goto err_set_regulator_demo;
	}

	printk("%s  ok!\n",__func__);
	return 0;

	err_set_regulator_demo:
	kfree(regulator_demo_data);
	return error;
}

static int regulator_demo_dev_remove(struct platform_device *pdev)
{
	printk("%s\n",__func__);
	return 0;
}

static struct of_device_id regulator_demo_of_match[] = {
	{ .compatible = "oneplus,regulator_demo", },
	{ },
};
MODULE_DEVICE_TABLE(of, regulator_demo_of_match);

static struct platform_driver regulator_demo_dev_driver = {
	.probe  = regulator_demo_dev_probe,
	.remove = regulator_demo_dev_remove,
	.driver = {
		.name   = DRV_NAME,
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(regulator_demo_of_match),
	},
};
module_platform_driver(regulator_demo_dev_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("regulator_demo switch by  driver");

