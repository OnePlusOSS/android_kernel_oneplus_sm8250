/*For OEM project monitor RF cable connection status,
 * and config different RF configuration
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/sys_soc.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/soc/qcom/smem.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <soc/qcom/subsystem_restart.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/kdev_t.h>
#include <soc/oplus/system/oplus_project.h>

#define RF_CABLE_STATE_ACTIVE   "oem_rf_cable_active"

static struct class *rf_uevent_class;
static struct device *rf_uevent_device;

#define CABLE_GPIO_NUM 5
#define PAGESIZE 512
struct cable_data {
    int irq[CABLE_GPIO_NUM];
    int cable_gpio[CABLE_GPIO_NUM];
    int support_timer;
    int gpio_num;
    struct delayed_work work;
    struct workqueue_struct *wqueue;
    struct device *dev;
    int rf_state;
    int rf_state_pre;
    spinlock_t lock;
    int enable;
    int is_rf_factory_mode;
    int gpio_state;
    struct pinctrl *gpio_pinctrl;
    struct pinctrl_state *gpio_pinctrl_active;
    struct pinctrl_state *gpio_pinctrl_suspend;
    bool connected;
    bool uevent_feature;
};
static struct cable_data *rf_cable_data;

int local_pow(int x,int y)
{
    int i = 0;
    int val = 1;
    for(i = 0; i< y; i++) {
        val =  val*x;
    }
    return val;
}

int get_all_gpio_val(void)
{
    int i = 0;
    int gpiostate = 0;
    for (i = 0; i < rf_cable_data->gpio_num; i++) {
        gpiostate = gpiostate + (gpio_get_value(rf_cable_data->cable_gpio[i]) *local_pow(10, rf_cable_data->gpio_num - i - 1));
    }
    return gpiostate;
}

static void irq_cable_enable(int enable)
{
    unsigned long flags;
    int i = 0;
    if (!rf_cable_data->support_timer) {
        spin_lock_irqsave(&rf_cable_data->lock, flags);
        if (enable) {
            for (i = 0; i < rf_cable_data->gpio_num; i++)
                enable_irq(rf_cable_data->irq[i]);
        } else {
            for (i = 0; i < rf_cable_data->gpio_num; i++)
                disable_irq_nosync(rf_cable_data->irq[i]);
        }
        spin_unlock_irqrestore(&rf_cable_data->lock, flags);
    }
}

static void cable_connect_state(int enable)
{
    char *connected[2]    = { "CABLE_STATE=CONNECTED", NULL };
    char *disconnected[2] = { "CABLE_STATE=DISCONNECTED", NULL };

    if (rf_cable_data->uevent_feature) {
        if (enable) {
            kobject_uevent_env(&rf_uevent_device->kobj, KOBJ_CHANGE, connected);
            rf_cable_data->connected = true;
            pr_err("%s: sent uevent %s\n", __func__, connected[0]);
        } else {
            kobject_uevent_env(&rf_uevent_device->kobj, KOBJ_CHANGE, disconnected);
            pr_err("%s: sent uevent %s\n", __func__, disconnected[0]);
            rf_cable_data->connected = false;
        }
    }
}
static void rc_cable_state_change(int state,int restart)
{
    cable_connect_state(state);
}

static void rf_cable_work(struct work_struct *work)
{
    int current_gpio_state = 0;

    irq_cable_enable(0);
    current_gpio_state = get_all_gpio_val();

    pr_err("%s rf_state_pre=%d, rf_state=%d gpio=%2d,\n", __func__, rf_cable_data->rf_state_pre, rf_cable_data->rf_state, current_gpio_state);

    if (rf_cable_data->gpio_state != current_gpio_state) {
        pr_err("%s gpio_state=%d, current_gpio_state=%d ignore\n", __func__, rf_cable_data->gpio_state, current_gpio_state);
        goto out;
    }

    rf_cable_data->rf_state = current_gpio_state;

    if (rf_cable_data->rf_state != rf_cable_data->rf_state_pre) {
        rc_cable_state_change(rf_cable_data->rf_state,1);
    }
    rf_cable_data->rf_state_pre =current_gpio_state;

out:
    irq_cable_enable(1);

    if (rf_cable_data->support_timer) {
        queue_delayed_work(rf_cable_data->wqueue, &rf_cable_data->work, msecs_to_jiffies(2*HZ));
    }
}

irqreturn_t cable_interrupt(int irq, void *_dev)
{
    rf_cable_data->gpio_state = get_all_gpio_val();

    queue_delayed_work(rf_cable_data->wqueue, &rf_cable_data->work, msecs_to_jiffies(500));
    return IRQ_HANDLED;
}

static ssize_t cable_read_proc(struct file *file, char __user *buf, size_t count, loff_t *off)
{
    char page[PAGESIZE];
    int len;

    len = scnprintf(page, sizeof(page), "%d\n", rf_cable_data->rf_state);
    return simple_read_from_buffer(buf, count, off, page, len);
}


static struct file_operations cable_proc_fops_cable = {
    .read = cable_read_proc,
};

int create_rf_cable_procfs(void)
{
    int ret = 0;
    struct proc_dir_entry *oplus_rf = NULL;

    oplus_rf = proc_mkdir("oplus_rf", NULL);
    if (!oplus_rf) {
        pr_err("can't create oplus_rf proc.\n");
        ret = -1;
    }

    proc_create_data("rf_cable", S_IRUGO, oplus_rf, &cable_proc_fops_cable, rf_cable_data);

    return ret;
}

static int op_rf_request_named_gpio(const char *label, int *gpio)
{
    struct device *dev = rf_cable_data->dev;
    struct device_node *np = dev->of_node;
    int rc = of_get_named_gpio(np, label, 0);

    if (rc < 0) {
        dev_err(dev, "failed to get '%s'\n", label);
        *gpio = rc;
        return rc;
    }
    *gpio = rc;
    rc = devm_gpio_request(dev, *gpio, label);
    if (rc) {
        dev_err(dev, "failed to request gpio %d, rc=%d\n", *gpio, rc);
        return rc;
    }
    dev_info(dev, "%s - gpio: %d\n", label, *gpio);
    return 0;
}
static int rf_cable_gpio_pinctrl_init(struct platform_device *pdev)
{
    int retval;

    dev_dbg(&pdev->dev, "%s\n", __func__);

    rf_cable_data->gpio_pinctrl = devm_pinctrl_get(&(pdev->dev));
    if (IS_ERR_OR_NULL(rf_cable_data->gpio_pinctrl)) {
        retval = PTR_ERR(rf_cable_data->gpio_pinctrl);
        dev_dbg(&pdev->dev, "Target does not use pinctrl %d\n", retval);
        goto err_pinctrl_get;
    }

    rf_cable_data->gpio_pinctrl_active = pinctrl_lookup_state(rf_cable_data->gpio_pinctrl, RF_CABLE_STATE_ACTIVE);
    if (IS_ERR_OR_NULL(rf_cable_data->gpio_pinctrl_active)) {
        retval = PTR_ERR(rf_cable_data->gpio_pinctrl_active);
        dev_err(&pdev->dev, "Can not lookup %s pinstate %d\n", RF_CABLE_STATE_ACTIVE, retval);
        goto err_pinctrl_lookup;
    }

    if (rf_cable_data->gpio_pinctrl) {
        retval = pinctrl_select_state(rf_cable_data->gpio_pinctrl, rf_cable_data->gpio_pinctrl_active);
        if (retval < 0) {
            dev_err(&pdev->dev, "failed to select pin to active state");
        }
    }

    return 0;

    err_pinctrl_lookup:devm_pinctrl_put(rf_cable_data->gpio_pinctrl);
    err_pinctrl_get:rf_cable_data->gpio_pinctrl = NULL;
    return retval;
}

static int op_rf_cable_probe(struct platform_device *pdev)
{
    int rc = 0;
    struct device *dev = &pdev->dev;
    int cable_state = 0;
    int i;

    rf_cable_data = kzalloc(sizeof(struct cable_data), GFP_KERNEL);
    if (!rf_cable_data)
        goto exit;

    rf_cable_data->dev = dev;
    dev_set_drvdata(dev, rf_cable_data);

    rf_cable_data->uevent_feature = of_property_read_bool(pdev->dev.of_node, "oem,rf_uevent_feature_enable");

    rc = of_property_read_u32(pdev->dev.of_node, "rf,cable-support-timer", &rf_cable_data->support_timer);
    if (rc) {
        pr_err("%s: cable-support-timer fail\n", __func__);
        goto exit_gpio;
    }

    rc = of_property_read_u32(pdev->dev.of_node, "rf,cable-gpio-num", &rf_cable_data->gpio_num);
    if (rc) {
        pr_err("%s: cable-gpio-num\n", __func__);
        goto exit_gpio;
    }

    rf_cable_gpio_pinctrl_init(pdev);

    rf_cable_data->wqueue = create_singlethread_workqueue("op_rf_cable_wqueue");
    INIT_DELAYED_WORK(&rf_cable_data->work, rf_cable_work);

    if (rf_cable_data->support_timer)
        queue_delayed_work(rf_cable_data->wqueue, &rf_cable_data->work, msecs_to_jiffies(HZ));

    pr_err("cable uevent init\n");
    rf_uevent_class = class_create(THIS_MODULE, "sdx5x_rf_cable");
    if (IS_ERR(rf_uevent_class)) {
        pr_err("%s: class_create fail - %d!\n", __func__, PTR_ERR(rf_uevent_class));
        return PTR_ERR(rf_uevent_class);
    }

    rf_uevent_device = device_create(rf_uevent_class, rf_cable_data->dev, MKDEV(0, 0), NULL, "rf_cable");
    if (IS_ERR(rf_uevent_device)) {
        pr_err("%s: rf_uevent_device fail - %d!\n", __func__, PTR_ERR(rf_uevent_device));
        return PTR_ERR(rf_uevent_device);
    }

    spin_lock_init(&rf_cable_data->lock);

    for (i = 0; i < rf_cable_data->gpio_num; i++) {

        char cable[PAGESIZE];
        memset(cable, 0, sizeof(cable));
        scnprintf(cable, sizeof(cable), "rf,cable-gpio-%d", i);

        rc = op_rf_request_named_gpio(cable, &rf_cable_data->cable_gpio[i]);
        if (rc) {
            pr_err("%s: op_rf_request_named_gpio gpio-%d fail\n", __func__, i);
            goto exit_gpio;
        }
        gpio_direction_input(rf_cable_data->cable_gpio[i]);

        rf_cable_data->irq[i] = gpio_to_irq(rf_cable_data->cable_gpio[i]);
        if (rf_cable_data->irq[i] < 0) {
            pr_err("Unable to get irq number for GPIO %d, error %d\n", rf_cable_data->cable_gpio[i], rf_cable_data->irq[i]);
            rc = rf_cable_data->irq[i];
            goto exit_gpio;
        }
        rc = request_irq(rf_cable_data->irq[i], cable_interrupt, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "rf,cable-gpio", rf_cable_data);
        if (rc) {
            pr_err("could not request irq %d\n", rf_cable_data->irq[i]);
            goto exit_gpio;
        }
        pr_err("requested irq %d\n", rf_cable_data->irq[i]);
        enable_irq_wake(rf_cable_data->irq[i]);
    }
    create_rf_cable_procfs();
    cable_state = get_all_gpio_val();
    rf_cable_data->rf_state = cable_state;
    pr_err("%s gpio=%d ,\n", __func__, cable_state);
    rc_cable_state_change(cable_state,0);
    pr_err("%s: probe success!\n", __func__);
    return 0;

exit_gpio:
    kfree(rf_cable_data);
exit:
    pr_err("%s: probe Fail!\n", __func__);

    return rc;
}

static const struct of_device_id rf_of_match[] = {
    { .compatible = "oem,rf_cable", },
    {}
};
MODULE_DEVICE_TABLE(of, rf_of_match);

static struct platform_driver op_rf_cable_driver = {
    .driver = {
        .name       = "op_rf_cable",
        .owner      = THIS_MODULE,
        .of_match_table = rf_of_match,
    },
    .probe = op_rf_cable_probe,
};

static int __init op_rf_cable_init(void)
{
    int ret;

    pr_info("%s: enter\n", __func__);
    ret = platform_driver_register(&op_rf_cable_driver);
    if (ret)
        pr_err("rf_cable_driver register failed: %d\n", ret);

    return ret;
}

MODULE_LICENSE("GPL v2");
late_initcall(op_rf_cable_init);

