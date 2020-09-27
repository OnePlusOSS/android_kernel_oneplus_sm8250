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
#include <linux/oem/op_rf_cable_monitor.h>
#include <linux/oem/boot_mode.h>
#include <linux/oem/project_info.h>
#include <linux/platform_device.h>
#include <linux/kdev_t.h>

#define RF_CABLE_STATE_ACTIVE   "oem_rf_cable_active"

static struct class *rf_uevent_class;
static struct device *rf_uevent_device;
static struct project_info *project_info_desc;
#define CABLE_GPIO_NUM 4
struct cable_data {
	int irq[CABLE_GPIO_NUM];
	int cable_gpio[CABLE_GPIO_NUM];
	int support_timer;
	struct delayed_work work;
	struct workqueue_struct *wqueue;
	struct device *dev;
	struct wakeup_source *wl;
	int rf_v2;
	int rf_v3;
	int rf_v3_pre;
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

static char *cmdline_find_option(char *str)
{
    return strnstr(saved_command_line, str, strlen(saved_command_line));
}

int modify_rf_cable_smem_info(uint32 status)
{
    size_t size;

    project_info_desc = qcom_smem_get(QCOM_SMEM_HOST_ANY,SMEM_PROJECT_INFO, &size);

    if (IS_ERR_OR_NULL(project_info_desc))
        pr_err("%s: get project_info failure\n", __func__);
    else {
        project_info_desc->rf_v3 = status;
        pr_err("%s: rf_cable: %d\n",
            __func__, project_info_desc->rf_v3);
    }
    return 0;
}

int modify_rf_v2_info(uint32 status)
{
    size_t size;

    project_info_desc = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_PROJECT_INFO, &size);

    if (IS_ERR_OR_NULL(project_info_desc))
        pr_err("%s: get project_info failure\n", __func__);
    else {
        project_info_desc->rf_v2 = status;
        pr_err("%s: rf_cable: %d\n",
                __func__, project_info_desc->rf_v3);
    }
    return 0;
}
int local_pow(int x,int y)
{
	int i = 0;
	int val = 1;
	for(i = 0; i< y; i++)
		val =  val*x;

	return val;
}

int get_all_gpio_val(void)
{
	int i = 0;
	int gpiostate = 0;
	for(i = 0; i< CABLE_GPIO_NUM; i++)
		gpiostate = gpiostate + (gpio_get_value(rf_cable_data->cable_gpio[i]) * local_pow(10, CABLE_GPIO_NUM - i - 1));
	/*only 19811 china and 19821 china use ANT6(gpio109)*/
	if((get_prj_version() == 12 && get_rf_version() == 11) 
		||(get_prj_version() == 11 && get_rf_version() == 11)
		||(get_prj_version() == 14))
		return gpiostate;
	else
		return gpiostate - gpiostate % 10;
}

static void irq_cable_enable(int enable)
{
	unsigned long flags;
	int i = 0;
	if (!rf_cable_data->support_timer) {

		spin_lock_irqsave(&rf_cable_data->lock, flags);
		if (enable) {
			for(i = 0; i< CABLE_GPIO_NUM; i++)
				enable_irq(rf_cable_data->irq[i]);
		} else {
			for(i = 0; i< CABLE_GPIO_NUM; i++)
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
			kobject_uevent_env(&rf_uevent_device->kobj,
					KOBJ_CHANGE, connected);
			rf_cable_data->connected = true;
			pr_err("%s: sent uevent %s\n", __func__,
					connected[0]);
		} else {
			kobject_uevent_env(&rf_uevent_device->kobj,
					KOBJ_CHANGE, disconnected);
			pr_err("%s: sent uevent %s\n", __func__,
					disconnected[0]);
			rf_cable_data->connected = false;
		}
	}
}
static void rc_cable_state_change(int state,int restart)
{
	
	modify_rf_cable_smem_info(state);
	cable_connect_state(state);

	if (restart && !rf_cable_data->uevent_feature)
       op_restart_modem();
}

static void rf_cable_work(struct work_struct *work)
{
	int current_gpio_state = 0;

	irq_cable_enable(0);
	current_gpio_state = get_all_gpio_val();

	pr_err("%s rf_v3_pre=%d, rf_v3=%d gpio=%2d,\n",
			__func__, rf_cable_data->rf_v3_pre,
			rf_cable_data->rf_v3,current_gpio_state);

	if (rf_cable_data->gpio_state != current_gpio_state) {
		pr_err("%s gpio_state=%d, current_gpio_state=%d ignore\n",
		__func__, rf_cable_data->gpio_state, current_gpio_state);
		goto out;
	}

	rf_cable_data->rf_v3 = current_gpio_state;

	if (rf_cable_data->rf_v3 != rf_cable_data->rf_v3_pre) {
        rc_cable_state_change(rf_cable_data->rf_v3,1);
	}
	rf_cable_data->rf_v3_pre =current_gpio_state;

out:
	irq_cable_enable(1);

	if (rf_cable_data->support_timer) {
		queue_delayed_work(rf_cable_data->wqueue, &rf_cable_data->work,
				msecs_to_jiffies(2*HZ));
	}
}

irqreturn_t cable_interrupt(int irq, void *_dev)
{

	rf_cable_data->gpio_state = get_all_gpio_val();

	__pm_wakeup_event(rf_cable_data->wl,
		msecs_to_jiffies(CABLE_WAKELOCK_HOLD_TIME));
	queue_delayed_work(rf_cable_data->wqueue,
		&rf_cable_data->work, msecs_to_jiffies(500));
	return IRQ_HANDLED;
}
static void factory_mode_state_change(int state)
{
	modify_rf_v2_info(state);
}

static ssize_t rf_factory_mode_proc_read_func(struct file *file,
        char __user *user_buf, size_t count, loff_t *ppos)
{
    char page[PAGESIZE];
    int len;

    len = scnprintf(page, sizeof(page), "%d\n", rf_cable_data->is_rf_factory_mode);

    return simple_read_from_buffer(user_buf,
            count, ppos, page, len);
}

static ssize_t rf_factory_mode_proc_write_func(struct file *file,
        const char __user *buffer, size_t count, loff_t *ppos)
{

	int enable = 0;
	char buf[10] = {0};
	int ret = 0;

    if (copy_from_user(buf, buffer, count))  {
        pr_err("%s: read proc input error.\n", __func__);
        return count;
    }

    ret = kstrtoint(buf, 0, &enable);
    if (ret < 0)
        return ret;

    pr_err("%s: input : %d\n", enable, __func__);
    irq_cable_enable(0);
    rf_cable_data->is_rf_factory_mode = enable;
    if (!rf_cable_data->is_rf_factory_mode) {
        factory_mode_state_change(0);
    } else {
        factory_mode_state_change(2);
    }
    irq_cable_enable(1);

    return count;
}

static const struct file_operations rf_factory_mode_proc_fops = {
    .write = rf_factory_mode_proc_write_func,
    .read =  rf_factory_mode_proc_read_func,
    .open = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t rf_cable_proc_read_func(struct file *file,
    char __user *user_buf, size_t count, loff_t *ppos)
{
    char page[PAGESIZE];
    int len;

    len = scnprintf(page, sizeof(page), "%d\n", rf_cable_data->enable);

    return simple_read_from_buffer(user_buf,
        count, ppos, page, len);
}

static ssize_t rf_cable_proc_write_func(struct file *file,
    const char __user *buffer, size_t count, loff_t *ppos)
{
    int enable = 0;
    char buf[10];
    int ret;

    if (copy_from_user(buf, buffer, count))  {
        pr_err("%s: read proc input error.\n", __func__);
        return count;
    }

    ret = kstrtoint(buf, 0, &enable);
    if (ret < 0)
        return ret;

    irq_cable_enable(0);

    if (enable != rf_cable_data->enable) {
        rf_cable_data->enable = enable;
		rc_cable_state_change(rf_cable_data->enable,1);
    }
    irq_cable_enable(1);

    return count;
}

static const struct file_operations rf_enable_proc_fops = {
    .write = rf_cable_proc_write_func,
    .read =  rf_cable_proc_read_func,
    .open = simple_open,
    .owner = THIS_MODULE,
};

int create_rf_cable_procfs(void)
{
    int ret = 0;

    if (!proc_create("rf_cable_config",
        0644, NULL, &rf_enable_proc_fops)) {
        pr_err("%s: proc_create enable fail!\n", __func__);
        ret = -1;
    }
    rf_cable_data->enable = 1;

    if (!proc_create("rf_factory_mode",
                0644, NULL, &rf_factory_mode_proc_fops)) {
        pr_err("%s: proc_create re_factory_mode fail!\n", __func__);
        ret = -1;
    }
    rf_cable_data->is_rf_factory_mode = 0;
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
        dev_err(dev, "failed to request gpio %d\n", *gpio);
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

	rf_cable_data->gpio_pinctrl_active
		= pinctrl_lookup_state(rf_cable_data->gpio_pinctrl,
				RF_CABLE_STATE_ACTIVE);
	if (IS_ERR_OR_NULL(rf_cable_data->gpio_pinctrl_active)) {
		retval = PTR_ERR(rf_cable_data->gpio_pinctrl_active);
		dev_err(&pdev->dev,
				"Can not lookup %s pinstate %d\n",
				RF_CABLE_STATE_ACTIVE, retval);
		goto err_pinctrl_lookup;
	}

	if (rf_cable_data->gpio_pinctrl) {

		retval = pinctrl_select_state(rf_cable_data->gpio_pinctrl,
				rf_cable_data->gpio_pinctrl_active);
		if (retval < 0) {
			dev_err(&pdev->dev,
					"failed to select pin to active state");
		}
	}

	return 0;

err_pinctrl_lookup:
	devm_pinctrl_put(rf_cable_data->gpio_pinctrl);
err_pinctrl_get:
	rf_cable_data->gpio_pinctrl = NULL;
	return retval;
}

static bool is_oem_dump = true;
static ssize_t oem_dump_show(struct device *pdev, struct device_attribute *attr,
		char *buf)
{
	char *state = "DISABLE";

	if (is_oem_dump == true)
		state = "ENABLE";

	return snprintf(buf, sizeof(state), "%s\n", state);
}
static ssize_t oem_dump_store(struct device *dev, struct device_attribute *attr,
		const char *buffer, size_t size)
{
	int val;
	int ret = 0;

	char *oem_dump_on[2] = { "OEM_DUMP=ENABLE", NULL };
	char *oem_dump_off[2] = { "OEM_DUMP=DISABLE", NULL };

	ret = kstrtoint(buffer, 10, &val);
	if (ret != 0) {
		pr_err("%s: invalid content: '%s', length = %zd\n", __func__,
				buffer, size);
		return ret;
	}

	if (val) {
		kobject_uevent_env(&rf_uevent_device->kobj,
				KOBJ_CHANGE, oem_dump_on);
		is_oem_dump = true;
		pr_err("%s: sent uevent %s\n", __func__,
				oem_dump_on[0]);
	} else {
		kobject_uevent_env(&rf_uevent_device->kobj,
				KOBJ_CHANGE, oem_dump_off);
		pr_err("%s: sent uevent %s\n", __func__,
				oem_dump_off[0]);
		is_oem_dump = false;
	}
	return size;

}
static DEVICE_ATTR(oem_dump, 0644, oem_dump_show, oem_dump_store);
static ssize_t state_show(struct device *pdev, struct device_attribute *attr,
		char *buf)
{
	char *state = "DISCONNECTED";

	if (rf_cable_data->connected == true)
		state = "CONNECTED";
	return snprintf(buf, sizeof(state), "%s\n", state);
}
static ssize_t state_store(struct device *dev, struct device_attribute *attr,
		const char *buffer, size_t size)
{
	int val;
	int ret = 0;

	ret = kstrtoint(buffer, 10, &val);
	if (ret != 0) {
		pr_err("%s: invalid content: '%s', length = %zd\n", __func__,
				buffer, size);
		return ret;
	}

	cable_connect_state(val);
	return size;

}
static DEVICE_ATTR(state, 0644, state_show, state_store);

static struct device_attribute *rf_uevent_attributes[] = {
	&dev_attr_state,
	&dev_attr_oem_dump,
	NULL
};

static int op_rf_cable_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct device *dev = &pdev->dev;
	int cable_state = 0;
	struct device_attribute **attrs;
	struct device_attribute *attr;
	int err,i;

	rf_cable_data = kzalloc(sizeof(struct cable_data), GFP_KERNEL);
	if (!rf_cable_data)
		goto exit;

	rf_cable_data->dev = dev;
	dev_set_drvdata(dev, rf_cable_data);

	rf_cable_data->uevent_feature = of_property_read_bool(pdev->dev.of_node,
					"oem,rf_uevent_feature_enable");

	rc = of_property_read_u32(pdev->dev.of_node, "rf,cable-support-timer",
					&rf_cable_data->support_timer);
	if (rc) {
		pr_err("%s: cable-support-timer fail\n",__func__);
		goto exit_gpio;
	}

	rf_cable_gpio_pinctrl_init(pdev);

	rf_cable_data->wqueue = create_singlethread_workqueue(
							"op_rf_cable_wqueue");
	INIT_DELAYED_WORK(&rf_cable_data->work, rf_cable_work);

	if (rf_cable_data->support_timer)
		queue_delayed_work(rf_cable_data->wqueue, &rf_cable_data->work, msecs_to_jiffies(HZ));

	pr_err("cable uevent init\n");
	rf_uevent_class = class_create(THIS_MODULE, "sdx5x_rf_cable");
	if (IS_ERR(rf_uevent_class)) {
		pr_err("%s: class_create fail - %d!\n", __func__,
				PTR_ERR(rf_uevent_class));
		return PTR_ERR(rf_uevent_class);
	}

	rf_uevent_device = device_create(rf_uevent_class, rf_cable_data->dev,
			MKDEV(0, 0), NULL, "rf_cable");
	if (IS_ERR(rf_uevent_device)) {
		pr_err("%s: rf_uevent_device fail - %d!\n", __func__,
				PTR_ERR(rf_uevent_device));
		return PTR_ERR(rf_uevent_device);
	}

	attrs = rf_uevent_attributes;
	while ((attr = *attrs++)) {
		err = device_create_file(rf_uevent_device, attr);
		if (err) {
			device_destroy(rf_uevent_device->class,
					rf_uevent_device->devt);
			return err;
		}
	}

	rf_cable_data->wl = wakeup_source_register(rf_cable_data->dev,"rf_cable_wake_lock");
	spin_lock_init(&rf_cable_data->lock);

	for(i = 0; i < CABLE_GPIO_NUM; i++) {

		char cable[PAGESIZE];
		memset(cable, 0, sizeof(cable));
		scnprintf(cable, sizeof(cable), "rf,cable-gpio-%d", i);

		rc = op_rf_request_named_gpio(cable, &rf_cable_data->cable_gpio[i]);
		if (rc) {
			pr_err("%s: op_rf_request_named_gpio gpio-%d fail\n",__func__,i);
			goto exit_gpio;
		}
		gpio_direction_input(rf_cable_data->cable_gpio[i]);

		rf_cable_data->irq[i] = gpio_to_irq(rf_cable_data->cable_gpio[i]);
		if (rf_cable_data->irq[i] < 0) {
			pr_err("Unable to get irq number for GPIO %d, error %d\n",
			rf_cable_data->cable_gpio[i], rf_cable_data->irq[i]);
			rc = rf_cable_data->irq[i];
			goto exit_gpio;
		}
		rc = request_irq(rf_cable_data->irq[i], cable_interrupt,
			        IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			        "rf,cable-gpio", rf_cable_data);
		if (rc) {
			pr_err("could not request irq %d\n", rf_cable_data->irq[i]);
			goto exit_gpio;
		}
		pr_err("requested irq %d\n", rf_cable_data->irq[i]);
		enable_irq_wake(rf_cable_data->irq[i]);
	}
	create_rf_cable_procfs();

	cable_state = get_all_gpio_val();
	pr_err("%s gpio=%d ,\n", __func__, cable_state);

	if (cmdline_find_option("ftm_mode=ftm_rf")) {
		pr_err("%s: ftm_mode FOUND! use 1 always\n", __func__);
		rc_cable_state_change(1,0);
	} else {
        rc_cable_state_change(cable_state,0);
	}
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

    ret = platform_driver_register(&op_rf_cable_driver);
    if (ret)
        pr_err("rf_cable_driver register failed: %d\n", ret);

    return ret;
}

MODULE_LICENSE("GPL v2");
late_initcall(op_rf_cable_init);

