/*
 *
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/gpio.h>
#include <linux/notifier.h>

#include "sensors_simulated_hall.h"

#define HALL_DEV_NAME "oplus,simulated_hall"
#define MHALL_DEBUG
#define MHALL_TAG "[MHALL]"
#if defined(MHALL_DEBUG)
#define MHALL_ERR(fmt, args...) printk(KERN_ERR  MHALL_TAG"%s %d : "fmt, __FUNCTION__, __LINE__, ##args)
#define MHALL_LOG(fmt, args...) printk(KERN_INFO MHALL_TAG"%s(%d):" fmt, __FUNCTION__, __LINE__, ##args)
#else
#define MHALL_CHG_REAR_ERR(fmt, args...)
#define MHALL_CHG_REAR_LOG(fmt, args...)
#endif

struct blocking_notifier_head hall_notifier = BLOCKING_NOTIFIER_INIT(hall_notifier);

static void hall_optional_handle(struct hall_simulated_data *hall_data, hall_status status)
{
	static struct hall_simulated_data *last_hall = NULL;
	bool  need_report = false;
	if (!hall_data) {
		return;
	}
	if (last_hall != NULL && last_hall != hall_data) {
		MHALL_ERR("%d--anather hall near now ,return , status = %d" ,hall_data->id, TYPE_HALL_NEAR == status ? 1 : 0);
		return;
	} else if (last_hall == NULL && status == TYPE_HALL_NEAR) {
		last_hall = hall_data;
		need_report = true;
	} else if (last_hall != NULL && status == TYPE_HALL_FAR && last_hall == hall_data) {
		last_hall = NULL;
		need_report = true;
	}
	if (need_report) {
		if (TYPE_HANDLE_NOTIFY_WIRELESS == hall_data->handle_option) {
		blocking_notifier_call_chain(&hall_notifier, TYPE_HALL_NEAR == status ? 1 : 0, NULL);
		MHALL_ERR("%d--TYPE_HANDLE_NOTIFY_WIRELESS , %d" ,hall_data->id, TYPE_HALL_NEAR == status ? 1 : 0);

	} else if (TYPE_HANDLE_REPORT_KEYS == hall_data->handle_option) {
		input_report_switch(hall_data->hall_input_dev, SW_PEN_INSERTED, TYPE_HALL_NEAR == status ? 1 : 0);
		input_sync(hall_data->hall_input_dev);
		MHALL_ERR("%d--TYPE_HANDLE_REPORT_KEYS , %d" ,hall_data->id, TYPE_HALL_NEAR == status ? 1 : 0);

		} else {
		MHALL_ERR("handle_option=0 noritify charge and android");
		// blocking_notifier_call_chain(&hall_notifier, TYPE_HALL_NEAR == status ? 1 : 0, NULL);
		input_report_switch(hall_data->hall_input_dev, SW_PEN_INSERTED, TYPE_HALL_NEAR == status ? 1 : 0);
		input_sync(hall_data->hall_input_dev);
		MHALL_ERR("%d--double report, %d" ,hall_data->id, TYPE_HALL_NEAR == status ? 1 : 0);
		}
	}

	return;
}

static irqreturn_t hall_interrupt_handler_func(int irq, void *dev)
{
	int val = 0;
	struct hall_simulated_data *hall_data = dev;

	mutex_lock(&hall_data->report_mutex);

	val = gpio_get_value(hall_data->irq_gpio);
	if (hall_data->active_low) {
		if ((TYPE_HALL_NEAR == hall_data->hall_status) && val) {
			hall_data->hall_status = TYPE_HALL_FAR;
			hall_optional_handle(hall_data, TYPE_HALL_FAR);
		} else if ((TYPE_HALL_FAR == hall_data->hall_status) && !val) {
			hall_data->hall_status = TYPE_HALL_NEAR;
			hall_optional_handle(hall_data, TYPE_HALL_NEAR);
		} else {
			hall_data->hall_status = (0 == val ? TYPE_HALL_NEAR : TYPE_HALL_FAR);
			MHALL_ERR("[logic wrong]%s hall_status:%d, gpio_val:%d.",hall_data->DEV_NAME, hall_data->hall_status, val);
			if (hall_data->hall_status == TYPE_HALL_NEAR) {
				hall_optional_handle(hall_data, hall_data->hall_status);
			}
		}
	} else {
		MHALL_ERR("[%s]not support non active low handle yet.", hall_data->DEV_NAME);
	}

	mutex_unlock(&hall_data->report_mutex);

	return IRQ_HANDLED;
}

static int hall_int_gpio_init(struct device *dev, struct hall_simulated_data *hall_data)
{
    char pinctrl_status[64] = {0};

	hall_data->hall_pinctrl = devm_pinctrl_get(dev);
	if (hall_data->hall_pinctrl == NULL) {
		MHALL_ERR("get pinctrl fail\n");
		return -EINVAL;
	}

	sprintf(pinctrl_status , "wireless_chg_hall%d_active" , hall_data->id);
	MHALL_ERR("pinctrl_status:%s\n" , pinctrl_status);

	hall_data->hall_int_active =
				pinctrl_lookup_state(hall_data->hall_pinctrl, pinctrl_status);
	if (hall_data->hall_int_active == NULL) {
		MHALL_ERR("get hall_int_active fail\n");
		return -EINVAL;
	}
	MHALL_ERR("get %s success!\n" , pinctrl_status);
	sprintf(pinctrl_status , "wireless_chg_hall%d_sleep" , hall_data->id);
	MHALL_ERR("pinctrl_status:%s\n" , pinctrl_status);
	hall_data->hall_int_sleep =
				pinctrl_lookup_state(hall_data->hall_pinctrl, pinctrl_status);
	if (hall_data->hall_int_sleep == NULL) {
		MHALL_ERR("get hall_int_sleep fail\n");
		return -EINVAL;
	}
	MHALL_ERR("get %s success!\n" , pinctrl_status);

	if (hall_data->irq_gpio > 0) {
		gpio_direction_input(hall_data->irq_gpio);
	}

	MHALL_ERR("pinctrl_select_state wireless_chg_hall1_active !\n");
	pinctrl_select_state(hall_data->hall_pinctrl, hall_data->hall_int_active);

	return 0;
}

static int hall_parse_dt(struct device *dev, struct hall_simulated_data *hall_data)
{
	int rc = 0;
	struct device_node *np = dev->of_node;

	hall_data->irq_gpio = of_get_named_gpio_flags(np, "irq-gpio", 0, &(hall_data->irq_flags));

	hall_data->active_low = of_property_read_bool(np, "irq_active_low");
	rc = of_property_read_u32(np, "optional-handle-type", &hall_data->handle_option);
	if (rc) {
		MHALL_ERR("handle option not specified.\n");
		return rc;
	}

	rc = of_property_read_u32(np, "hall-id", &hall_data->id);
	if (rc) {
		MHALL_ERR("handle id not specified.\n");
		return rc;
	}
	sprintf(hall_data->DEV_NAME , "%s_%d" , HALL_DEV_NAME , hall_data->id);
	rc = hall_int_gpio_init(dev, hall_data);
	if (rc) {
		MHALL_ERR("gpio init fail.\n");
		return rc;
	}
	hall_data->irq_number = gpio_to_irq(hall_data->irq_gpio);
	MHALL_LOG("gpio %d hall_irq %d, flags = %d\n",hall_data->irq_gpio , hall_data->irq_number, hall_data->irq_flags);
	return 0;
}

static struct platform_driver simulated_hall_driver;
static int hall_simulated_driver_probe(struct platform_device *dev)
{
	int err = 0;
	struct hall_simulated_data *hall_data = NULL;

	MHALL_LOG("simulated_hall_driver probe\n");
	hall_data = devm_kzalloc(&dev->dev, sizeof(*hall_data), GFP_KERNEL);
	if (hall_data == NULL) {
		MHALL_ERR("failed to allocate memory %d\n", err);
		err = -ENOMEM;
		goto return_err;
	}
	dev_set_drvdata(&dev->dev, hall_data);

	if (dev->dev.of_node) {
		err = hall_parse_dt(&dev->dev, hall_data);
		if (err) {
			dev_err(&dev->dev, "Failed to parse device tree\n");
			goto free_hall_data;
		}
	} else if (dev->dev.platform_data != NULL) {
		memcpy(hall_data, dev->dev.platform_data, sizeof(*hall_data));
	} else {
		MHALL_ERR("No valid platform data, probe failed.\n");
		err = -ENODEV;
		goto free_hall_data;
	}

	/*** creat mhall input_dev ***/
	hall_data->hall_input_dev = input_allocate_device();
	if (NULL == hall_data->hall_input_dev) {
		err = -ENOMEM;
		MHALL_ERR(" failed to allocate input device for hall\n");
		goto free_hall_data;
	}
	hall_data->hall_input_dev->name = hall_data->DEV_NAME;
	__set_bit(EV_SW,  hall_data->hall_input_dev->evbit);
	input_set_capability(hall_data->hall_input_dev, EV_SW, SW_PEN_INSERTED);
	err = input_register_device(hall_data->hall_input_dev);
	if(err){
		MHALL_ERR("failed to register mhall input device\n");
		err = -ENODEV;
		goto free_input_device;
	}

	hall_data->hall_status = TYPE_HALL_FAR;
	mutex_init(&hall_data->report_mutex);

	/*** register irq handler ***/
	err = request_threaded_irq(hall_data->irq_number, NULL, hall_interrupt_handler_func, hall_data->irq_flags, hall_data->DEV_NAME, hall_data);
	if (err < 0) {
		MHALL_ERR("request irq handler failed.\n");
		err = -ENODEV;
		goto exit;
	}
	enable_irq_wake(hall_data->irq_number);

	MHALL_LOG("simulated mhall probe ok\n");
	hall_interrupt_handler_func(hall_data->irq_number , hall_data);
	MHALL_LOG("simulated mhall_%d update first status ok\n" , hall_data->id);
	return 0;
exit:
free_input_device:
	input_free_device(hall_data->hall_input_dev);
free_hall_data:
	kfree(hall_data);
return_err:
	return err;
}

static int hall_simulated_driver_remove(struct platform_device *dev)
{
	struct hall_simulated_data *hall_data = dev_get_drvdata(&dev->dev);

	input_unregister_device(hall_data->hall_input_dev);
	kfree(hall_data);

	return 0;
}

static struct platform_device_id hall_simulated_id[] = {
	{HALL_DEV_NAME, 0 },
	{ },
};

static struct of_device_id hall_sensor_match_table[] = {
	{.compatible = HALL_DEV_NAME, },
	{ },
};

static struct platform_driver simulated_hall_driver = {
	.driver = {
		.name = HALL_DEV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = hall_sensor_match_table,
	},
	.probe = hall_simulated_driver_probe,
	.remove = hall_simulated_driver_remove,
	.id_table = hall_simulated_id,
};

static int __init simulated_hall_init(void)
{
	MHALL_LOG("simulated hall sensor start init.\n");
	platform_driver_register(&simulated_hall_driver);
	return 0;
}

static void __exit simulated_hall_exit(void)
{
	platform_driver_unregister(&simulated_hall_driver);
}

//module_init(simulated_hall_init);
late_initcall(simulated_hall_init);
module_exit(simulated_hall_exit);
MODULE_DESCRIPTION("Simulated Hall sensor driver");
MODULE_LICENSE("GPL v2");
