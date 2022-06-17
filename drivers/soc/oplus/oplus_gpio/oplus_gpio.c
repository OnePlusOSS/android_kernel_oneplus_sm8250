#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/platform_device.h>

#include <soc/oplus/system/oplus_project.h>

#define TAG	"oplus_gpio:"

#define __OF_GPIO_MATCH(p) \
	struct of_device_id p[] = {\
		{ .compatible = #p},\
		{},\
	}

#define __OPLUS_PLATEFORM_DRIVER(p) \
	{ .probe = oplus_gpio_probe, \
	  .remove = oplus_gpio_remove \
	}

#define __OPLUS_GPIO_DRIVER(p) \
	static struct platform_driver p = __OPLUS_PLATEFORM_DRIVER(p)

#define OPLUS_GPIO_DRIVER(d) \
	__OF_GPIO_MATCH(oplus_##d);\
	__OPLUS_GPIO_DRIVER(d);

#define GPIO_DO_DRIVER_INIT(d) \
	do {\
		d.driver.name = #d;\
		d.driver.of_match_table = oplus_##d;\
	} while(0)

static int oplus_gpio_probe(struct platform_device *pdev)
{
	pr_info(TAG "oplus_gpio_probe call %d %s\n",get_PCB_Version(),pdev->name);
	return 0;
}

static int oplus_gpio_remove(struct platform_device *pdev)
{
	pr_info(TAG "oplus_gpio_remove call \n");
	return 0;
}

OPLUS_GPIO_DRIVER(gpio_evt);
OPLUS_GPIO_DRIVER(gpio_dvt);
OPLUS_GPIO_DRIVER(gpio_pvt);

static int __init oplus_gpio_init(void)
{
	pr_info(TAG "oplus_gpio_init call \n");

	GPIO_DO_DRIVER_INIT(gpio_evt);
	GPIO_DO_DRIVER_INIT(gpio_dvt);
	GPIO_DO_DRIVER_INIT(gpio_pvt);

	if (get_PCB_Version() <= EVT6) {
		platform_driver_register(&gpio_evt);
	}
	else if ((get_PCB_Version() > EVT6) && (get_PCB_Version() <= DVT6)) {
		platform_driver_register(&gpio_dvt);
	}
	else {
		platform_driver_register(&gpio_pvt);
	}

	return 0;
}

late_initcall_sync(oplus_gpio_init);
MODULE_DESCRIPTION("nc gpio config");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("mofei@oplus.com");
