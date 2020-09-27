#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/pm.h>
#include <linux/delay.h>
#include <linux/input/qpnp-power-on.h>
#include <linux/of_address.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>
#include <asm/system_misc.h>
#include <asm/memory.h>
#include <linux/random.h>
#include <soc/qcom/scm.h>


// martin.li@coreBsp,2020.1.25 add for power cut testing 
int power_cut_mode, power_cut_delay;
struct delayed_work power_cut_delayed_work;
static void __iomem *op_msm_ps_hold;
#define SCM_IO_DEASSERT_PS_HOLD		2

static int __init power_cut_test_param(char *str)
{
	int value = simple_strtol(str, NULL, 0);
	int min, max, tmp;

	power_cut_mode =  value/1000000;
	min  = (value%1000000)/1000 ;
	max  = value%1000;

	if(min > max) {
		tmp = min;
		min = max;
		max = tmp;
	}

	pr_info("power cut test mode:%d, mindelay:%d, maxdelay:%d, value:%d\n",
		power_cut_mode, min, max, value);

	if(min == max)
		power_cut_delay = min;
	else
		power_cut_delay = get_random_u32()%(max - min + 1) + min;

	pr_info("power cut test delay %d\n", power_cut_delay);
	return 0;
}
__setup("androidboot.power_cut_test=", power_cut_test_param);

static void deassert_ps_hold(void)
{
	struct scm_desc desc = {
		.args[0] = 0,
		.arginfo = SCM_ARGS(1),
	};

	if (scm_is_call_available(SCM_SVC_PWR, SCM_IO_DEASSERT_PS_HOLD) > 0) {
		/* This call will be available on ARMv8 only */
		scm_call2_atomic(SCM_SIP_FNID(SCM_SVC_PWR,
				 SCM_IO_DEASSERT_PS_HOLD), &desc);
	}
	printk("%s:%d\n",__func__,__LINE__);
	/* Fall-through to the direct write in case the scm_call "returns" */
	__raw_writel(0, op_msm_ps_hold);
}

void drop_pshold_work_func (struct work_struct *p_work)
{
	qpnp_pon_system_pwr_off(PON_POWER_OFF_HARD_RESET);
	deassert_ps_hold();
}
void batfet_work_func (struct work_struct *p_work)
{
	return;
}

static ssize_t write_deassert_ps_hold(struct file *file, const char __user *buf,
                                  size_t count, loff_t *ppos)
{
	if (count) {
		char c;

		if (get_user(c, buf))
			return -EFAULT;

		if (c == 'c') {
			qpnp_pon_system_pwr_off(PON_POWER_OFF_HARD_RESET);
			deassert_ps_hold();
		}
	}

	return count;
}

static const struct file_operations deassert_ps_hold_operations = {
       .write          = write_deassert_ps_hold,
       .llseek         = noop_llseek,
};

static int init_power_cut_test(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL,"qcom,pshold");
	if (!np) {
		pr_err("unable to find pshold-base node\n");
	} else {
		op_msm_ps_hold = of_iomap(np, 0);
		if (!op_msm_ps_hold) {
			pr_err("unable to map pshold-base offset\n");
			return -ENOMEM;
		}

		if (!proc_create("deassert_ps_hold", S_IWUSR, NULL,
				 &deassert_ps_hold_operations))
			pr_err("Failed to register proc interface\n");
	}

	switch (power_cut_mode) {
	case 1:// drop ps hold
		INIT_DELAYED_WORK(&power_cut_delayed_work, drop_pshold_work_func);
		schedule_delayed_work(&power_cut_delayed_work, msecs_to_jiffies(power_cut_delay*1000));
		break;
	case 2:// force all power off
		INIT_DELAYED_WORK(&power_cut_delayed_work, batfet_work_func);
		schedule_delayed_work(&power_cut_delayed_work, msecs_to_jiffies(power_cut_delay*1000));
		break;
	default:
		return 0;
	}
	return 0;
}
core_initcall(init_power_cut_test);
