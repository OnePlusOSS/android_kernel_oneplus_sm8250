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

#define DRV_NAME    "gpio_switch"
#define GPIO_STATE_ACTIVE   "gpio_switch_pin_active"
#define GPIO_STATE_SUSPEND  "gpio_switch_pin_suspend"

#define MAX_GPIO_ID_LEN 24

enum GPIO_GPIO{
    GPIO_MIN_NUM,
    GPIO_065=1,
    GPIO_066,
    GPIO_073,
    GPIO_074,
    GPIO_077,
    GPIO_078,
    PM8009_01,
    PM8009_02,
    PM8009_04,
    GPIO_MAX_NUM,
};

struct gpio_list {
    int gpio;
    char name[MAX_GPIO_ID_LEN];
};

static struct gpio_list gpio_lists[]=
{
    {GPIO_MIN_NUM,"GPIO_MIN_NUM"},
    {GPIO_065,    "GPIO_065"},
    {GPIO_066,    "GPIO_066"},
    {GPIO_073,    "GPIO_073"},
    {GPIO_074,    "GPIO_074"},
    {GPIO_077,    "GPIO_077"},
    {GPIO_078,    "GPIO_078"},
    {PM8009_01,   "PM8009_01"},
    {PM8009_02,   "PM8009_02"},
    {PM8009_04,   "PM8009_04"},
    {GPIO_MAX_NUM,"GPIO_MAX_NUM"}
};

struct gpio_dev_data {
    int gpios[GPIO_MAX_NUM];
    struct device *dev;
    int gpio_state;
    struct pinctrl *gpio_pinctrl;
    struct pinctrl_state *gpio_pinctrl_active;
    struct pinctrl_state *gpio_pinctrl_suspend;
};

static struct gpio_dev_data *gpio_data;

static DEFINE_MUTEX(sem);
static int gpio_state_show(struct seq_file *seq, void *offset)
{
    seq_printf(seq, "%d\n", gpio_data->gpio_state);
    return 0;
}

static ssize_t gpio_state_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos)
{

    int gpio_state;
    int gpio;
    int enable;
    char buf[10]={0};

    if (!gpio_data) {
        return -EFAULT;
    }
    mutex_lock(&sem);

    if( copy_from_user(buf, buffer, sizeof (buf)) ){
        return count;
    }

    sscanf(buf, "%d", &gpio_state);

    gpio = gpio_state/10;
    enable = gpio_state%10;
    printk("gpio_state before(%d),current(%d) gpio_index=(%d),gpionum=%d enable=(%d)\n",gpio_data->gpio_state,gpio_state,gpio,gpio_data->gpios[gpio],!!enable);

    gpio_data->gpio_state=gpio_state;

    if(gpio > GPIO_MIN_NUM && gpio < GPIO_MAX_NUM)
    {
        if(gpio_is_valid(gpio_data->gpios[gpio]))
        {
            gpio_direction_output(gpio_data->gpios[gpio],!!enable);
            gpio_state = gpio_get_value(gpio_data->gpios[gpio]);
            printk("gpio_name (%d) enable=(%d) ,get_gpio=%d \n",gpio_data->gpios[gpio],!!enable,gpio_state);
        }
    }

    mutex_unlock(&sem);
    return count;
}

static int gpio_state_open(struct inode *inode, struct file *file)
{
    return single_open(file, gpio_state_show, gpio_data);
}

static const struct file_operations gpio_state_proc_fops = {
    .owner      = THIS_MODULE,
    .open       = gpio_state_open,
    .read       = seq_read,
    .llseek     = seq_lseek,
    .release    = single_release,
    .write      = gpio_state_write,
};

static int gpio_dev_get_devtree_pdata(struct device *dev)
{
    struct device_node *node;
    int i=0;
    node = dev->of_node;
    if (!node)
        return -EINVAL;

    for (i = 1; i < GPIO_MAX_NUM; i++) {
        gpio_data->gpios[i] = of_get_gpio(node, i-1);
        pr_err("i= %d gpio=: %d\n", i,gpio_data->gpios[i]);
        if (gpio_data->gpios[i] < 0)
            pr_warn("%s: Fail to get 5wire gpio: %d\n",__func__, i);
    }
    return 0;
}

static int gpio_pinctrl_init(struct platform_device *pdev)
{
    int retval;
    printk("%s \n",__func__);
    //Get pinctrl if target uses pinctrl
    gpio_data->gpio_pinctrl = devm_pinctrl_get(&(pdev->dev));
    if (IS_ERR_OR_NULL(gpio_data->gpio_pinctrl)) {
        retval = PTR_ERR(gpio_data->gpio_pinctrl);
        dev_dbg(&pdev->dev,"Target does not use pinctrl %d\n", retval);
        goto err_pinctrl_get;
    }

    gpio_data->gpio_pinctrl_active
        = pinctrl_lookup_state(gpio_data->gpio_pinctrl,
                GPIO_STATE_ACTIVE);
    if (IS_ERR_OR_NULL(gpio_data->gpio_pinctrl_active)) {
        retval = PTR_ERR(gpio_data->gpio_pinctrl_active);
        dev_err(&pdev->dev,
            "Can not lookup %s pinstate %d\n",
            GPIO_STATE_ACTIVE, retval);
        goto err_pinctrl_lookup;
    }

    gpio_data->gpio_pinctrl_suspend
        = pinctrl_lookup_state(gpio_data->gpio_pinctrl,
            GPIO_STATE_SUSPEND);
    if (IS_ERR_OR_NULL(gpio_data->gpio_pinctrl_suspend)) {
        retval = PTR_ERR(gpio_data->gpio_pinctrl_suspend);
        dev_err(&pdev->dev,
            "Can not lookup %s pinstate %d\n",
            GPIO_STATE_SUSPEND, retval);
        goto err_pinctrl_lookup;
    }

    if ( gpio_data->gpio_pinctrl) {

        retval = pinctrl_select_state(gpio_data->gpio_pinctrl,
                    gpio_data->gpio_pinctrl_active);
        if (retval < 0) {
            dev_err(&pdev->dev,
                "failed to select pin to active state");
        }
    }

    return 0;

err_pinctrl_lookup:
    devm_pinctrl_put(gpio_data->gpio_pinctrl);
err_pinctrl_get:
    gpio_data->gpio_pinctrl = NULL;
    return retval;
}


static int gpio_dev_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    int error=0;
    int i=0,j=0;
    printk(KERN_ERR"%s\n",__func__);

    gpio_data = kzalloc(sizeof(struct gpio_dev_data), GFP_KERNEL);
    gpio_data->dev = dev;

    //parse device tree node
    error = gpio_dev_get_devtree_pdata(dev);

    if (error) {
        dev_err(dev, "parse device tree fail!!!\n");
        goto err_gpio_dev_register;
    }

    if (!proc_create_data("demogpio", 0666, NULL, &gpio_state_proc_fops, gpio_data)) {
        error = -ENOMEM;
        goto err_gpio_dev_register;
    }

    error= gpio_pinctrl_init(pdev);
    if(error < 0)
    {
        printk(KERN_ERR "%s: gpio_pinctrl_init, err=%d", __func__, error);
        goto err_set_gpio;
    }

    for (i = 1; i < GPIO_MAX_NUM; i++) {

        error =gpio_request(gpio_data->gpios[i],gpio_lists[i].name);
        if(error < 0)
        {
            printk(KERN_ERR "%s: gpio_request, err=%d", __func__, error);
            goto err_set_gpio;
        }
        error = gpio_direction_output(gpio_data->gpios[i],0);
        if(error < 0)
        {
            printk(KERN_ERR "%s: gpio_direction_output, err=%d", __func__, error);
            goto err_set_gpio;
        }
    }

    printk("%s  ok!\n",__func__);
    return 0;

err_set_gpio:
    for (j = i-1; i > GPIO_MIN_NUM; i--) {
      if (gpio_is_valid(gpio_data->gpios[i]))
         gpio_free(gpio_data->gpios[i]);
    }
err_gpio_dev_register:
    kfree(gpio_data);
    return error;
}

static int gpio_dev_remove(struct platform_device *pdev)
{
    int j=0;
    printk("%s\n",__func__);
    for (j = GPIO_MAX_NUM-1; j > GPIO_MIN_NUM; j--) {
      if (gpio_is_valid(gpio_data->gpios[j]))
         gpio_free(gpio_data->gpios[j]);
    }

    kfree(gpio_data);

    return 0;
}

static struct of_device_id gpio_of_match[] = {
    { .compatible = "oneplus,gpio_switch", },
    { },
};
MODULE_DEVICE_TABLE(of, gpio_of_match);


static struct platform_driver gpio_dev_driver = {
    .probe  = gpio_dev_probe,
    .remove = gpio_dev_remove,
    .driver = {
        .name   = DRV_NAME,
        .owner  = THIS_MODULE,
        .of_match_table = of_match_ptr(gpio_of_match),
    },
};
module_platform_driver(gpio_dev_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("gpio switch by gpio's driver");

