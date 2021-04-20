/****************************************************
 **Description:fastchg update firmware and driver
 *****************************************************/
#define pr_fmt(fmt) "FASTCHG: %s: " fmt, __func__

#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/oem/project_info.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/power_supply.h>
#include <linux/interrupt.h>
#include <linux/oem/power/oem_external_fg.h>
#include <linux/pm_qos.h>
#include <linux/proc_fs.h>
#include <linux/moduleparam.h>

#define BYTE_OFFSET			2
#define BYTES_TO_WRITE		16

#define READ_COUNT			192
#define	FW_CHECK_FAIL		0
#define	FW_CHECK_SUCCESS	1

#define SHOW_FW_VERSION_DELAY_MS 18000

enum dpdm_mode {
	DPDM_MODE_NORMAL,
	DPDM_MODE_WARP,
};

struct fastchg_device_info {
	struct i2c_client		*client;
	struct miscdevice   dash_device;
	struct mutex        read_mutex;
	struct mutex        gpio_mutex;
	wait_queue_head_t   read_wq;

	struct pinctrl_state *pinctrl_state_active;
	struct pinctrl_state *pinctrl_state_suspended;
	struct pinctrl_state *pinctrl_mcu_data_read;
	struct pinctrl_state *pinctrl_mcu_data_write;
	struct pinctrl *pinctrl;
	bool fast_chg_started;
	bool fast_low_temp_full;
	bool fast_chg_ing;
	bool fast_switch_to_normal;
	bool fast_normal_to_warm;
	bool fast_chg_error;
	bool irq_enabled;
	bool fast_chg_allow;
	bool firmware_already_updated;
	bool n76e_present;
	bool is_mcl_verion;
#ifdef OP_SWARP_SUPPORTED
	struct pinctrl_state *pinctrl_mcu_id_hiz;
	struct pinctrl_state *pinctrl_mcu_id_pull_up;
	struct pinctrl_state *pinctrl_mcu_id_pull_down;
	bool is_swarp_supported;
	bool warp_normal_path_need_config;
	int asic_hw_id;
#endif
	bool is_4300mAh_4p45_support;
	bool is_4320mAh_4p45_support;
	bool is_4510mAh_4p45_support;
	int skin_hi_curr_max;
	int skin_wrm_curr_max;
	int skin_med_curr_max;
	int skin_hi_lcdoff_curr_max;
	int skin_med_lcdoff_curr_max;
	int dash_firmware_ok;
	int mcu_reset_ahead;
	int erase_count;
	int addr_low;
	int addr_high;
	int adapter_update_report;
	int adapter_update_real;
	int battery_type;
	int irq;
	int mcu_en_gpio;
	int usb_sw_1_gpio;
	int usb_sw_2_gpio;
	int usb_on_gpio;
	int usb_on_gpio_1;
	int ap_clk;
	int ap_data;
	int dash_enhance;
	int dashchg_fw_ver_count;
	unsigned int sid;

	struct power_supply		*batt_psy;
	struct work_struct fastcg_work;
	struct work_struct charger_present_status_work;
	struct timer_list watchdog;
	struct wakeup_source *fastchg_wake_lock;
	struct wakeup_source *fastchg_update_fireware_lock;

	struct delayed_work		update_firmware;
	struct delayed_work update_fireware_version_work;
	struct delayed_work adapter_update_work;
#if (defined(OP_SWARP_SUPPORTED) && !defined(GET_HWID_BY_GPIO))
	struct delayed_work get_asic_hwid_work;
#endif
	struct delayed_work disable_mcu_work;
	char fw_id[255];
	char manu_name[255];

	enum dpdm_mode dpdm_mode;
};

struct fastchg_device_info *fastchg_di;

static unsigned char *dashchg_firmware_data;
static struct i2c_client *mcu_client;

#ifdef OP_SWARP_SUPPORTED
static const char * const mcu_id_text[] = {
"Silergy", "Rockchip", "Richtek"
};
static const unsigned short i2c_addr[] = {0x06, 0x0a, 0x0e};
enum swarp_asic_hw_id {
	SILERGY_SY6610 = 0,
	ROCKCHIP_RK826,
	RICHTEK_RT5125,
};

static void oneplus_notify_dash_charger_type(enum fast_charger_type type);
#endif
static void oneplus_notify_adapter_sid(unsigned int sid);
void switch_mode_to_normal(void);

static int is_usb_pluged(void)
{
	static struct power_supply *psy;
	union power_supply_propval ret = {0,};
	int usb_present, rc;

	if (!psy) {
		psy = power_supply_get_by_name("usb");
		if (!psy) {
			pr_err("fastchg failed to get ps usb\n");
			return -EINVAL;
		}
	}

	rc = power_supply_get_property(psy, POWER_SUPPLY_PROP_PRESENT, &ret);
	if (rc) {
		pr_err("fastchg failed to get  POWER_SUPPLY_PROP_PRESENT\n");
		return -EINVAL;
	}

	if (ret.intval < 0) {
		pr_err("fastchg get POWER_SUPPLY_PROP_PRESENT EINVAL \n");
		return -EINVAL;
	}

	usb_present = ret.intval;
	return usb_present;
}

static ssize_t warp_exist_read(struct file *p_file,
	char __user *puser_buf, size_t count, loff_t *p_offset)
{
	return 0;
}

static ssize_t warp_exist_write(struct file *p_file,
	const char __user *puser_buf,
	size_t count, loff_t *p_offset)
{
	return 0;
}

static const struct file_operations warp_chg_exist_operations = {
	.read = warp_exist_read,
	.write = warp_exist_write,
};
static void init_warp_chg_exist_node(void)
{
	if (!proc_create("warp_chg_exit", 0644, NULL,
			 &warp_chg_exist_operations)){
		pr_info("Failed to register n76e node\n");
	}
}

#ifdef OP_SWARP_SUPPORTED
static ssize_t swarp_exist_read(struct file *p_file,
	char __user *puser_buf, size_t count, loff_t *p_offset)
{
	char buffer[7];
	int len = 0;
	len = snprintf(buffer, 7, "%d\n", fastchg_di->asic_hw_id);
	return simple_read_from_buffer(puser_buf, count, p_offset, buffer, len);;
}

static ssize_t swarp_exist_write(struct file *p_file,
	const char __user *puser_buf,
	size_t count, loff_t *p_offset)
{
	return 0;
}

static const struct file_operations swarp_chg_exist_operations = {
	.read = swarp_exist_read,
	.write = swarp_exist_write,
};
static void init_swarp_chg_exist_node(void)
{
	if (!proc_create("swarp_chg_exist", 0644, NULL,
			 &swarp_chg_exist_operations)){
		pr_info("Failed to register swarp node\n");
	}
}
#endif

static ssize_t dash_4300mAh_4p45_exist_read(struct file *p_file,
	char __user *puser_buf, size_t count, loff_t *p_offset)
{
	return 0;
}

static ssize_t dash_4300mAh_4p45_exist_write(struct file *p_file,
	const char __user *puser_buf,
	size_t count, loff_t *p_offset)
{
	return 0;
}

static const struct file_operations dash_4300mAh_4p45_exist_operations = {
	.read = dash_4300mAh_4p45_exist_read,
	.write = dash_4300mAh_4p45_exist_write,
};

static void init_dash_4300mAh_4p45_exist_node(void)
{
	if (!proc_create("dash_4300_4p45_exit", 0644, NULL,
			 &dash_4300mAh_4p45_exist_operations)){
		pr_info("Failed to register dash_4300mAh_4p45 node\n");
	}
}

static ssize_t dash_4320mAh_4p45_exist_read(struct file *p_file,
	char __user *puser_buf, size_t count, loff_t *p_offset)
{
	return 0;
}

static ssize_t dash_4320mAh_4p45_exist_write(struct file *p_file,
	const char __user *puser_buf,
	size_t count, loff_t *p_offset)
{
	return 0;
}

static const struct file_operations dash_4320mAh_4p45_exist_operations = {
	.read = dash_4320mAh_4p45_exist_read,
	.write = dash_4320mAh_4p45_exist_write,
};

static void init_dash_4320mAh_4p45_exist_node(void)
{
	if (!proc_create("dash_4320_4p45_exit", 0644, NULL,
			 &dash_4320mAh_4p45_exist_operations)){
		pr_info("Failed to register dash_4320mAh_4p45 node\n");
	}
}

static ssize_t dash_4510mAh_4p45_exist_read(struct file *p_file,
	char __user *puser_buf, size_t count, loff_t *p_offset)
{
	return 0;
}

static ssize_t dash_4510mAh_4p45_exist_write(struct file *p_file,
	const char __user *puser_buf,
	size_t count, loff_t *p_offset)
{
	return 0;
}

static const struct file_operations dash_4510mAh_4p45_exist_operations = {
	.read = dash_4510mAh_4p45_exist_read,
	.write = dash_4510mAh_4p45_exist_write,
};

static void init_dash_4510mAh_4p45_exist_node(void)
{
	if (!proc_create("dash_4510_4p45_exit", 0644, NULL,
			 &dash_4510mAh_4p45_exist_operations)){
		pr_info("Failed to register dash_4510mAh_4p45 node\n");
	}
}

static ssize_t n76e_exist_read(struct file *p_file,
	char __user *puser_buf, size_t count, loff_t *p_offset)
{
	return 0;
}

static ssize_t n76e_exist_write(struct file *p_file,
	const char __user *puser_buf,
	size_t count, loff_t *p_offset)
{
	return 0;
}

static const struct file_operations n76e_exist_operations = {
	.read = n76e_exist_read,
	.write = n76e_exist_write,
};

static void init_n76e_exist_node(void)
{
	if (!proc_create("n76e_exit", 0644, NULL,
			 &n76e_exist_operations)){
		pr_info("Failed to register n76e node\n");
	}
}
#define PAGESIZE 512

static ssize_t enhance_exist_read(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE];
	struct fastchg_device_info *di = fastchg_di;

	if (!di)
		return ret;
	ret = snprintf(page, 255, "%d", di->dash_enhance);
	ret = simple_read_from_buffer(user_buf,
			count, ppos, page, strlen(page));
	return ret;
}

static ssize_t enhance_exist_write(struct file *file,
			const char __user *buffer, size_t count, loff_t *ppos)
{
	struct fastchg_device_info *di = fastchg_di;
	int ret = 0;
	char buf[4] = {0};

	if (count > 2)
		return count;

	if (copy_from_user(buf, buffer, count)) {
		pr_err("%s: write proc dash error.\n", __func__);
		return count;
	}

	if (-1 == sscanf(buf, "%d", &ret)) {
		pr_err("%s sscanf error\n", __func__);
		return count;
	}
	if (!di)
		return count;
	if ((ret == 0) || (ret == 1))
		di->dash_enhance = ret;
	pr_info("%s:the dash enhance is = %d\n",
			__func__, di->dash_enhance);
	return count;
}

static const struct file_operations enhance_exist_operations = {
	.read = enhance_exist_read,
	.write = enhance_exist_write,
};

static void init_enhance_dash_exist_node(void)
{
	if (!proc_create("enhance_dash", 0644, NULL,
			 &enhance_exist_operations))
		pr_err("Failed to register enhance dash node\n");
}

static ssize_t dash_firmware_exist_read(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE];
	struct fastchg_device_info *di = fastchg_di;

	if (!di)
		return ret;
	ret = snprintf(page, 255, "%d", di->dash_firmware_ok);
	ret = simple_read_from_buffer(user_buf,
			count, ppos, page, strlen(page));
	return ret;
}

static ssize_t dash_firmware_exist_write(struct file *file,
			const char __user *buffer, size_t count, loff_t *ppos)
{
	return count;
}

static const struct file_operations dash_frimware_done_operations = {
	.read = dash_firmware_exist_read,
	.write = dash_firmware_exist_write,
};

static void init_dash_firmware_done_node(void)
{
	if (!proc_create("dash_firmware_ok", 0644, NULL,
			 &dash_frimware_done_operations))
		pr_err("Failed to register dash_frimware_done_operations node\n");
}

void opchg_set_mcu_data_read(struct fastchg_device_info *chip)
{
	gpio_direction_input(chip->ap_data);
	if (chip->pinctrl &&
		!IS_ERR_OR_NULL(chip->pinctrl_mcu_data_read))
		pinctrl_select_state(chip->pinctrl,
			chip->pinctrl_mcu_data_read);
}

void set_mcu_active(int value)
{
	if (gpio_is_valid(fastchg_di->mcu_en_gpio)) {
#ifdef OP_SWARP_SUPPORTED
		if (fastchg_di->is_swarp_supported)
			gpio_direction_output(fastchg_di->mcu_en_gpio, value);
		else
#endif
			gpio_direction_output(fastchg_di->mcu_en_gpio, !(value & 0x01));
	}
}

void mcu_en_reset(void)
{
	if (gpio_is_valid(fastchg_di->mcu_en_gpio)) {
#ifdef OP_SWARP_SUPPORTED
		if (fastchg_di->is_swarp_supported) {
			gpio_direction_output(fastchg_di->mcu_en_gpio, 0);
			usleep_range(2000, 2001);
			gpio_direction_output(fastchg_di->mcu_en_gpio, 1);
		} else
#endif
			gpio_direction_output(fastchg_di->mcu_en_gpio, 1);
	}
}

void mcu_en_gpio_set(int value)
{
	if (value) {
		if (gpio_is_valid(fastchg_di->mcu_en_gpio)) {
			if (fastchg_di->is_swarp_supported)
				gpio_direction_output(fastchg_di->mcu_en_gpio, 1);
			else
				gpio_direction_output(fastchg_di->mcu_en_gpio, 0);
		}
	} else {
		if (gpio_is_valid(fastchg_di->mcu_en_gpio)) {
#ifdef OP_SWARP_SUPPORTED
			if (fastchg_di->is_swarp_supported) {
				gpio_direction_output(fastchg_di->mcu_en_gpio, 0);
				usleep_range(10000, 10001);
				gpio_direction_output(fastchg_di->mcu_en_gpio, 1);
			} else {
#endif
				gpio_direction_output(fastchg_di->mcu_en_gpio, 1);
				usleep_range(10000, 10001);
				gpio_direction_output(fastchg_di->mcu_en_gpio, 0);
#ifdef OP_SWARP_SUPPORTED
			}
#endif
		}
	}
}
#define ADAPTER_UPDATE_DELAY              1400

void usb_sw_gpio_set(int value)
{
	pr_info("set usb_sw_gpio=%d\n", value);
	if (!gpio_is_valid(fastchg_di->usb_sw_1_gpio)
		&& !gpio_is_valid(fastchg_di->usb_sw_2_gpio)) {
		pr_err("gpio is invalid\n");
		return;
	}

	if (value) {
		if (gpio_is_valid(fastchg_di->usb_on_gpio)) {
			gpio_direction_output(fastchg_di->usb_on_gpio, 1);
			gpio_direction_output(fastchg_di->usb_on_gpio_1, 1);
			gpio_direction_output(fastchg_di->usb_sw_1_gpio, 1);
			gpio_direction_output(fastchg_di->usb_sw_2_gpio, 0);
		} else {
			gpio_direction_output(fastchg_di->usb_sw_1_gpio, 1);
			gpio_direction_output(fastchg_di->usb_sw_2_gpio, 1);
		}
	} else {
		if (gpio_is_valid(fastchg_di->usb_on_gpio)) {
			gpio_direction_output(fastchg_di->usb_on_gpio, 0);
			gpio_direction_output(fastchg_di->usb_on_gpio_1, 0);
		}
		gpio_direction_output(fastchg_di->usb_sw_1_gpio, 0);
		gpio_direction_output(fastchg_di->usb_sw_2_gpio, 0);
	}
	fastchg_di->fast_chg_allow = value;
	/* david@bsp add log */
	pr_info("get usb_sw_gpio=%d&%d\n"
		, gpio_get_value(fastchg_di->usb_sw_1_gpio)
		, gpio_get_value(fastchg_di->usb_sw_2_gpio));
}

void opchg_set_mcu_data_write(struct fastchg_device_info *chip)
{
	gpio_direction_output(chip->ap_data, 0);
	if (chip->pinctrl &&
		!IS_ERR_OR_NULL(chip->pinctrl_mcu_data_write))
		pinctrl_select_state(chip->pinctrl,
			chip->pinctrl_mcu_data_write);
}

static inline int opchg_mcu_enable(struct fastchg_device_info *di, bool en)
{
	int rc = 0;

	if (!gpio_is_valid(di->mcu_en_gpio)) {
		pr_err("mcu en gpio is invalid\n");
		return -ENODEV;
	}

	if (di->is_swarp_supported)
		rc = gpio_direction_output(di->mcu_en_gpio, en);
	else
		rc = gpio_direction_output(di->mcu_en_gpio, !en);
	if (rc < 0) {
		pr_err("%s mcu failed, rc=%d\n", en ? "enbale" : "disable", rc);
		return rc;
	}
	return 0;
}

static inline bool opchg_mcu_is_enabled(struct fastchg_device_info *di)
{
	if (!gpio_is_valid(di->mcu_en_gpio)) {
		pr_err("mcu en gpio is invalid\n");
		return false;
	}

	if (di->is_swarp_supported)
		return !!gpio_get_value(di->mcu_en_gpio);
	else
		return !gpio_get_value(di->mcu_en_gpio);
}

static inline int opchg_switch_dmdm(struct fastchg_device_info *di,
				    enum dpdm_mode mode)
{
	int rc = 0;

	if (!gpio_is_valid(di->usb_sw_1_gpio) &&
	    !gpio_is_valid(di->usb_sw_2_gpio)) {
		pr_err("sw gpio is invalid\n");
		return -ENODEV;
	}

	if (mode == DPDM_MODE_WARP) {
		if (gpio_is_valid(di->usb_on_gpio)) {
			rc = gpio_direction_output(di->usb_on_gpio, 1);
			if (rc < 0) {
				pr_err("switch usb on(=1) err, rc=%d\n", rc);
				goto error;
			}
			rc = gpio_direction_output(di->usb_on_gpio_1, 1);
			if (rc < 0) {
				pr_err("switch usb on1(=1) err, rc=%d\n", rc);
				goto error;
			}
			rc = gpio_direction_output(di->usb_sw_1_gpio, 1);
			if (rc < 0) {
				pr_err("switch sw1(=1) err, rc=%d\n", rc);
				goto error;
			}
			rc = gpio_direction_output(di->usb_sw_2_gpio, 0);
			if (rc < 0) {
				pr_err("switch sw2(=0) err, rc=%d\n", rc);
				goto error;
			}
		} else {
			rc = gpio_direction_output(di->usb_sw_1_gpio, 1);
			if (rc < 0) {
				pr_err("switch sw1(=1) err, rc=%d\n", rc);
				goto error;
			}
			rc = gpio_direction_output(di->usb_sw_2_gpio, 1);
			if (rc < 0) {
				pr_err("switch sw2(=1) err, rc=%d\n", rc);
				goto error;
			}
		}
		di->fast_chg_allow = true;
	} else {
		if (gpio_is_valid(di->usb_on_gpio)) {
			rc = gpio_direction_output(di->usb_on_gpio, 0);
			if (rc < 0) {
				pr_err("switch usb on(=0) err, rc=%d\n", rc);
				goto error;
			}
			rc = gpio_direction_output(di->usb_on_gpio_1, 0);
			if (rc < 0) {
				pr_err("switch usb on1(=0) err, rc=%d\n", rc);
				goto error;
			}
		}
		rc = gpio_direction_output(di->usb_sw_1_gpio, 1);
		if (rc < 0) {
			pr_err("switch sw1(=1) err, rc=%d\n", rc);
			goto error;
		}
		rc = gpio_direction_output(di->usb_sw_2_gpio, 0);
		if (rc < 0) {
			pr_err("switch sw2(=0) err, rc=%d\n", rc);
			goto error;
		}
		di->fast_chg_allow = false;
	}

	pr_info("get usb_sw_gpio=%d&%d\n",
		gpio_get_value(di->usb_sw_1_gpio),
		gpio_get_value(di->usb_sw_2_gpio));

	return 0;
error:
	di->fast_chg_allow = false;
	if (gpio_is_valid(di->usb_on_gpio)) {
		gpio_direction_output(di->usb_on_gpio, 0);
		gpio_direction_output(di->usb_on_gpio_1, 0);
	}
	gpio_direction_output(di->usb_sw_1_gpio, 1);
	gpio_direction_output(di->usb_sw_2_gpio, 0);
	pr_info("get usb_sw_gpio=%d&%d\n",
		gpio_get_value(di->usb_sw_1_gpio),
		gpio_get_value(di->usb_sw_2_gpio));
	return rc;
}

static inline int opchg_mcu_switch_to_upgrade_mode(struct fastchg_device_info *di)
{
	int rc = 0;

	if (!gpio_is_valid(di->ap_clk)) {
		pr_err("ap clk gpio is invalid\n");
		return -ENODEV;
	}

	rc = gpio_direction_output(di->ap_clk, 0);
	if (rc < 0) {
		pr_err("set ap_clk(=0) error, rc=%d\n", rc);
		return rc;
	}
	usleep_range(10000, 10001);
	if (di->dpdm_mode != DPDM_MODE_NORMAL) {
		(void)opchg_switch_dmdm(di, DPDM_MODE_NORMAL);
		di->dpdm_mode = DPDM_MODE_NORMAL;
	}
	rc = opchg_mcu_enable(di, true);
	if (rc < 0) {
		pr_err("%d: set mcu enable error, rc=%d\n", __LINE__);
		goto error;
	}
	usleep_range(5000, 5001);
	rc = opchg_mcu_enable(di, false);
	if (rc < 0) {
		pr_err("%d: set mcu disable error, rc=%d\n", __LINE__);
		goto error;
	}
	usleep_range(10000, 10001);
	rc = opchg_mcu_enable(di, true);
	if (rc < 0) {
		pr_err("%d: set mcu enable error, rc=%d\n", __LINE__);
		goto error;
	}
	msleep(2500);
	rc = gpio_direction_output(di->ap_clk, 1);
	if (rc < 0) {
		pr_err("set ap_clk(=1) error, rc=%d\n", rc);
		goto error;
	}
	usleep_range(10000, 10001);

	return 0;

error: // disable mcu
	(void) opchg_mcu_enable(di, false);
	return rc;
}

int opchg_mcu_action(enum mcu_action_mode mode)
{
	int rc = 0;
	int count;

	if (fastchg_di == NULL) {
		pr_err("warp device not found\n");
		return -ENODEV;
	}

	pr_info("mcu action %d\n", mode);
	mutex_lock(&fastchg_di->gpio_mutex);
	switch (mode) {
	case ACTION_MODE_ENABLE:
		rc = opchg_mcu_enable(fastchg_di, true);
		if (rc < 0) {
			pr_err("%d: set mcu enable error, rc=%d\n", __LINE__);
			goto error;
		}
		break;
	case ACTION_MODE_RESET_ACTIVE:
		if (fastchg_di->dpdm_mode != DPDM_MODE_NORMAL)
			(void)opchg_switch_dmdm(fastchg_di, DPDM_MODE_NORMAL);
		rc = opchg_mcu_enable(fastchg_di, true);
		if (rc < 0) {
			pr_err("%d: set mcu enable error, rc=%d\n", __LINE__);
			goto error;
		}
		usleep_range(5000, 5001);
		rc = opchg_mcu_enable(fastchg_di, false);
		if (rc < 0) {
			pr_err("%d: set mcu disable error, rc=%d\n", __LINE__);
			goto error;
		}
		usleep_range(10000, 10001);
		rc = opchg_mcu_enable(fastchg_di, true);
		if (rc < 0) {
			pr_err("%d: set mcu enable error, rc=%d\n", __LINE__);
			goto error;
		}
		usleep_range(2500, 2501);
		if (fastchg_di->dpdm_mode == DPDM_MODE_WARP) {
			count = 0;
			while (!opchg_mcu_is_enabled(fastchg_di) && count < 50) {
				count++;
				usleep_range(1000, 1001);
			}
			if (count >= 50) {
				pr_err("can't enable mcu\n");
				goto error;
			}
			rc = opchg_switch_dmdm(fastchg_di, DPDM_MODE_WARP);
			if (rc < 0) {
				pr_err("%d: switch to warp mode error, rc=%d\n",
					__LINE__);
				goto error;
			}
		}
		break;
	case ACTION_MODE_RESET_SLEEP:
		if (fastchg_di->dpdm_mode != DPDM_MODE_NORMAL)
			(void)opchg_switch_dmdm(fastchg_di, DPDM_MODE_NORMAL);
		fastchg_di->dpdm_mode = DPDM_MODE_NORMAL;
		rc = opchg_mcu_enable(fastchg_di, false);
		if (rc < 0) {
			pr_err("%d: set mcu disable error, rc=%d\n", __LINE__);
			goto error;
		}
		usleep_range(1000, 1001);
		rc = opchg_mcu_enable(fastchg_di, true);
		if (rc < 0) {
			pr_err("%d: set mcu enable error, rc=%d\n", __LINE__);
			goto error;
		}
		usleep_range(5000, 5001);
		rc = opchg_mcu_enable(fastchg_di, false);
		if (rc < 0) {
			pr_err("%d: set mcu disable error, rc=%d\n", __LINE__);
			goto error;
		}
		usleep_range(1000, 1001);
		break;
	case ACTION_MODE_SWITCH_UPGRADE:
		rc = opchg_mcu_switch_to_upgrade_mode(fastchg_di);
		if (rc < 0)
			goto error;
		break;
	case ACTION_MODE_SWITCH_NORMAL:
		if (fastchg_di->dpdm_mode != DPDM_MODE_NORMAL)
			(void)opchg_switch_dmdm(fastchg_di, DPDM_MODE_NORMAL);
		fastchg_di->dpdm_mode = DPDM_MODE_NORMAL;
		rc = opchg_mcu_enable(fastchg_di, true);
		if (rc < 0) {
			pr_err("%d: set mcu enable error, rc=%d\n", __LINE__);
			goto error;
		}
		usleep_range(5000, 5001);
		break;
	case ACTION_MODE_SWITCH_WARP:
		if (fastchg_di->dpdm_mode != DPDM_MODE_NORMAL)
			(void)opchg_switch_dmdm(fastchg_di, DPDM_MODE_NORMAL);
		rc = opchg_mcu_enable(fastchg_di, true);
		if (rc < 0) {
			pr_err("%d: set mcu enable error, rc=%d\n", __LINE__);
			goto error;
		}
		usleep_range(5000, 5001);
		rc = opchg_mcu_enable(fastchg_di, false);
		if (rc < 0) {
			pr_err("%d: set mcu disable error, rc=%d\n", __LINE__);
			goto error;
		}
		usleep_range(10000, 10001);
		rc = opchg_mcu_enable(fastchg_di, true);
		if (rc < 0) {
			pr_err("%d: set mcu enable error, rc=%d\n", __LINE__);
			goto error;
		}
		usleep_range(2500, 2501);
		count = 0;
		while (!opchg_mcu_is_enabled(fastchg_di) && count < 50) {
			count++;
			usleep_range(1000, 1001);
		}
		if (count >= 50) {
			pr_err("can't enable mcu\n");
			goto error;
		}
		rc = opchg_switch_dmdm(fastchg_di, DPDM_MODE_WARP);
		if (rc < 0) {
			pr_err("%d: switch to warp mode error, rc=%d\n",
				__LINE__);
			goto error;
		}
		fastchg_di->dpdm_mode = DPDM_MODE_WARP;
		break;
	default:
		rc = -EINVAL;
		pr_err("unknown active mode=(=%d)\n", mode);
		goto error;
	}
	mutex_unlock(&fastchg_di->gpio_mutex);

	return 0;

error:
	if (fastchg_di->dpdm_mode != DPDM_MODE_NORMAL)
		(void)opchg_switch_dmdm(fastchg_di, DPDM_MODE_NORMAL);
	fastchg_di->dpdm_mode = DPDM_MODE_NORMAL;
	(void)opchg_mcu_enable(fastchg_di, false);
	usleep_range(1000, 1001);
	(void)opchg_mcu_enable(fastchg_di, true);
	usleep_range(5000, 5001);
	(void)opchg_mcu_enable(fastchg_di, false);
	usleep_range(1000, 1001);
	mutex_unlock(&fastchg_di->gpio_mutex);
	return rc;
}

#define ADAPTER_UPDATE_DELAY              1400

static int set_property_on_smbcharger(
	enum power_supply_property prop, bool data)
{
	static struct power_supply *psy;
	union power_supply_propval value = {data, };
	int ret;

	if (!psy) {
		psy = power_supply_get_by_name("battery");
		if (!psy) {
			pr_err("failed to get ps battery\n");
			return -EINVAL;
		}
	}
	ret = power_supply_set_property(psy, prop, &value);
	/* david@bsp modified */
	if (ret)
		return -EINVAL;

	return 0;
}

#ifdef OP_SWARP_SUPPORTED
#define ASIC_ADD_COUNT 2
#define p9415_MAX_I2C_READ_CNT 10
static int oneplus_u16_i2c_read(struct i2c_client *client, u16 reg, int count,
				u8 *data)
{
	int ret = -1;
	struct i2c_msg i2c_msg[2];
	u8 reg_buf[2] = {reg & 0xff, reg >> 8};

	//write msg
	i2c_msg[0].addr = client->addr;
	i2c_msg[0].flags = 0;
	i2c_msg[0].len = 2;
	i2c_msg[0].buf = reg_buf;

	//read msg
	i2c_msg[1].addr = client->addr;
	i2c_msg[1].flags = I2C_M_RD;
	i2c_msg[1].len = count;
	i2c_msg[1].buf = data;

	ret = i2c_transfer(client->adapter, i2c_msg, 2);
	if (ret != 2) {
		pr_err("read reg(=0x%04x) error, ret=%d\n", reg, ret);
		return ret;
	}

	return count;
}

static int oneplus_u16_i2c_write(struct i2c_client *client, int reg,
				 int count, u8 *data)
{
	int ret;
	struct i2c_msg i2c_msg;
	u8 *buf;
	buf = kzalloc(count + 2, GFP_KERNEL);
	if (!buf) {
		pr_err("can't alloc memory!\n");
		return -ENOMEM;
	}

	buf[0] = reg & 0xff;
	buf[1] = reg >> 8;

	memcpy(buf + 2, data, count);

	//write msg
	i2c_msg.addr = client->addr;
	i2c_msg.flags = 0;
	i2c_msg.len = count + 2;
	i2c_msg.buf = buf;

	ret = i2c_transfer(client->adapter, &i2c_msg, 1);
	if (ret != 1) {
		pr_err("write reg(=0x%04x) error, ret=%d\n", reg, ret);
		kfree(buf);
		return ret;
	}

	kfree(buf);
	return count;
}
#endif

static int oneplus_dash_i2c_read(
	struct i2c_client *client, u8 addr, s32 len, u8 *rxbuf)
{
	return i2c_smbus_read_i2c_block_data(client, addr, len, rxbuf);
}

static int oneplus_dash_i2c_write(
	struct i2c_client *client, u8 addr, s32 len, u8 *txbuf)
{
	return i2c_smbus_write_i2c_block_data(client, addr, len, txbuf);
}

static unsigned char addr_buf[2];
static bool n76e_fw_check(struct fastchg_device_info *chip)
{
	unsigned char data_buf[16] = {0x0};
	int rc = 0;
	int j = 0, i;
	int fw_line = 0;
	int total_line = 0;

	total_line = chip->dashchg_fw_ver_count / 18;

	for (fw_line = 0; fw_line < total_line; fw_line++) {
		addr_buf[0] = dashchg_firmware_data[fw_line * 18 + 1];
		addr_buf[1] = dashchg_firmware_data[fw_line * 18];
		rc = oneplus_dash_i2c_write(chip->client,
				0x01, 2, &addr_buf[0]);
		if (rc < 0) {
			pr_err("i2c_write 0x01 error\n");
			return FW_CHECK_FAIL;
		}

		data_buf[0] = 0;
		oneplus_dash_i2c_write(chip->client, 0x03, 1, &data_buf[0]);
		usleep_range(2000, 2100);
		oneplus_dash_i2c_read(chip->client, 0x03, 16, &data_buf[0]);

		for (j = 0; j < 16; j++) {
			if (data_buf[j] != dashchg_firmware_data[fw_line * 18 + 2 + j]) {
				pr_err("fail, data_buf[%d]:0x%x != n76e_firmware_data[%d]:0x%x\n",
						j, data_buf[j], (fw_line * 18 + 2 + j),
						dashchg_firmware_data[fw_line * 18 + 2 + j]);
				for (i = 0; i < 16; i++)
					pr_info("data_buf[%d]:0x%x\n", i, data_buf[i]);
				pr_info("fail line=%d\n", fw_line);
				return FW_CHECK_FAIL;
			}
		}
	}
		return FW_CHECK_SUCCESS;
}


static bool dashchg_fw_check(void)
{
	unsigned char addr_buf[2] = {0x88, 0x00};
	unsigned char data_buf[32] = {0x0};
	int rc, i, j, addr;
	int fw_line = 0;

	addr_buf[0] = fastchg_di->addr_low;
	addr_buf[1] = fastchg_di->addr_high;
	rc = oneplus_dash_i2c_write(mcu_client, 0x01, 2, &addr_buf[0]);
	if (rc < 0) {
		pr_err("%s i2c_write 0x01 error\n", __func__);
		goto i2c_err;
	}

	usleep_range(2000, 2001);
	for (i = 0; i < READ_COUNT; i++) {
		oneplus_dash_i2c_read(mcu_client, 0x03, 16, &data_buf[0]);
		usleep_range(2000, 2001);
		oneplus_dash_i2c_read(mcu_client, 0x03, 16, &data_buf[16]);
		addr = 0x8800 + i * 32;

		/* compare recv_buf with dashchg_firmware_data[] begin */
		if (addr == ((dashchg_firmware_data[fw_line * 34 + 1] << 8)
			| dashchg_firmware_data[fw_line * 34])) {
			for (j = 0; j < 32; j++) {
			if (data_buf[j] != dashchg_firmware_data
					[fw_line * 34 + 2 + j]) {
				pr_info("%s fail,data_buf[%d]:0x%x!=dashchg_firmware_data[%d]:0x%x\n",
				__func__, j, data_buf[j],
				(fw_line * 34 + 2 + j),
				dashchg_firmware_data[fw_line * 34 + 2 + j]);
				pr_info("%s addr = 0x%x", __func__, addr);
				for (j = 0; j <= 31; j++)
					pr_info("%x\n", data_buf[j]);
				return FW_CHECK_FAIL;
				}
			}
			fw_line++;
		} else {
	/*pr_err("%s addr dismatch,addr:0x%x,stm_data:0x%x\n",__func__,*/
	/*addr,(dashchg_firmware_data[fw_line * 34 + 1] << 8) | */
	/*dashchg_firmware_data[fw_line * 34]);*/
		}
		/* compare recv_buf with dashchg_firmware_data[] end */
	}
	pr_info("result=success\n");
	return FW_CHECK_SUCCESS;
i2c_err:
	pr_err("result=fail\n");
	return FW_CHECK_FAIL;
}

static int dashchg_fw_write(
	unsigned char *data_buf,
	unsigned int offset, unsigned int length)
{
	unsigned int count = 0;
	unsigned char zero_buf[1] = {0};
	unsigned char temp_buf[1] = {0};
	unsigned char addr_buf[2] = {0x88, 0x00};
	int rc;

	addr_buf[0] = fastchg_di->addr_low;
	addr_buf[1] = fastchg_di->addr_high;
	count = offset;
	/* write data begin */
	while (count < (offset + length)) {
		addr_buf[0] = data_buf[count + 1];
		addr_buf[1] = data_buf[count];

		rc = oneplus_dash_i2c_write(mcu_client, 0x01, 2, &addr_buf[0]);
		if (rc < 0) {
			pr_err("i2c_write 0x01 error\n");
			return -EFAULT;
		}

		/* write 16 bytes data to dashchg */
		oneplus_dash_i2c_write(mcu_client,
		0x02, BYTES_TO_WRITE, &data_buf[count+BYTE_OFFSET]);
		oneplus_dash_i2c_write(mcu_client, 0x05, 1, &zero_buf[0]);
		oneplus_dash_i2c_read(mcu_client, 0x05, 1, &temp_buf[0]);

		/* write 16 bytes data to dashchg again */
		if (!fastchg_di->n76e_present) {
			oneplus_dash_i2c_write(mcu_client,
			0x02, BYTES_TO_WRITE,
			&data_buf[count+BYTE_OFFSET+BYTES_TO_WRITE]);
			oneplus_dash_i2c_write(mcu_client,
					0x05, 1, &zero_buf[0]);
			oneplus_dash_i2c_read(mcu_client,
					0x05, 1, &temp_buf[0]);
			count = count + BYTE_OFFSET + 2 * BYTES_TO_WRITE;
		} else
			count = count + BYTE_OFFSET +  BYTES_TO_WRITE;

		usleep_range(2000, 2001);
		if (count > (offset + length - 1))
			break;
	}
	return 0;
}

#ifdef OP_SWARP_SUPPORTED
/*RockChip RK826 firmware upgrade start*/
#define ERASE_COUNT			959 /*0x0000-0x3BFF*/
#define BYTE_OFFSET			2
#define BYTES_TO_WRITE		16
#define FW_CHECK_FAIL		0
#define FW_CHECK_SUCCESS	1
#define PAGE_UNIT			128
#define TRANSFER_LIMIT		72
#define I2C_ADDR			0x14
#define REG_RESET			0x5140
#define REG_SYS0			0x52C0
#define REG_HOST			0x52C8
#define	REG_SLAVE			0x52CC
#define REG_STATE			0x52C4
#define REG_MTP_SELECT		0x4308
#define REG_MTP_ADDR		0x4300
#define REG_MTP_DATA		0x4304
#define REG_SRAM_BEGIN		0x2000
#define SYNC_FLAG			0x53594E43
#define NOT_SYNC_FLAG		(~SYNC_FLAG)
#define REC_01_FLAG			0x52454301
#define REC_0O_FLAG			0x52454300
#define RESTART_FLAG		0x52455354
#define MTP_SELECT_FLAG	0x000f0001
#define MTP_ADDR_FLAG		0xffff8000
#define SLAVE_IDLE			0x49444C45
#define SLAVE_BUSY			0x42555359
#define SLAVE_ACK			0x41434B00
#define SLAVE_ACK_01		0x41434B01
#define FORCE_UPDATE_FLAG	0xaf1c0b76
#define SW_RESET_FLAG		0X0000fdb9
#define STATE_READY			0x0
#define STATE_SYNC			0x1
#define STATE_REQUEST		0x2
#define STATE_FIRMWARE		0x3
#define STATE_FINISH			0x4

typedef struct {
	u32 tag;
	u32 length;
	u32 timeout;
	u32 ram_offset;
	u32 fw_crc;
	u32 header_crc;
} struct_req, *pstruct_req;

static bool rk826_fw_check(struct fastchg_device_info *chip)
{
	int ret = 0;
	u8 data_buf[4]= {0};
	u32 mtp_select_flag = cpu_to_le32(MTP_SELECT_FLAG);
	u32 mtp_addr_flag = cpu_to_le32(MTP_ADDR_FLAG);
	u32 i = 0;

	pr_err("wkcs: fw check\n");
	ret = oneplus_u16_i2c_write(chip->client, REG_MTP_SELECT, 4, (u8 *)(&mtp_select_flag));
	if (ret < 0) {
		pr_err("write mtp select reg error\n");
		goto fw_update_check_err;
	}

	for (i = chip->dashchg_fw_ver_count - 11; i <= chip->dashchg_fw_ver_count - 4; i++) {
		mtp_addr_flag = (MTP_ADDR_FLAG | i);
		ret = oneplus_u16_i2c_write(chip->client, REG_MTP_ADDR, 4, (u8 *)(&mtp_addr_flag));
		if (ret < 0) {
			pr_err("write mtp addr error\n");
			goto fw_update_check_err;
		}

		ret = oneplus_u16_i2c_read(chip->client, REG_MTP_ADDR, 4, data_buf);
		if (ret < 0) {
			pr_err("read mtp addr error\n");
			goto fw_update_check_err;
		}

		do {
			ret  = oneplus_u16_i2c_read(chip->client, REG_MTP_SELECT, 4, data_buf);
			if (ret < 0) {
				pr_err("read mtp select reg error\n");
				goto fw_update_check_err;
			}
		} while (!(data_buf[1] & 0x01));

		ret = oneplus_u16_i2c_read(chip->client, REG_MTP_DATA, 4, data_buf);
		if (ret < 0) {
			pr_err("read mtp data error\n");
			goto fw_update_check_err;
		}
		pr_info("the read compare data: %02x, target:%02x\n", data_buf[0], dashchg_firmware_data[i]);
		if (data_buf[0] != dashchg_firmware_data[i]) {
			//pr_err("rk826_fw_data check fail\n");
			goto fw_update_check_err;
		}
	}

	return FW_CHECK_SUCCESS;

fw_update_check_err:
	pr_err("rk826_fw_data check fail\n");
	return FW_CHECK_FAIL;
}

static u32 js_hash_en(u32 hash, const u8 *buf, u32 len)
{
	u32 i;

	for (i = 0; i < len; i++)
		hash ^= ((hash << 5) + buf[i] + (hash >> 2));
	return hash;
}

static u32 js_hash(const u8 *buf, u32 len)
{
	return js_hash_en(0x47C6A7E6, buf, len);
}

int WriteSram(struct fastchg_device_info *chip, const u8 *buf, u32 size)
{
	u8 offset = 0;
	u16 reg_addr;
	int ret = 0;
	int i = 0;
	int cur_size = 0;
	int try_count = 5;
	u8 readbuf[4] = {0};
	u8 tx_buff[4] = {0};
	u8 TEST[72] = {0};
	u32 rec_0O_flag = cpu_to_le32(REC_0O_FLAG);
	u32 rec_01_flag = cpu_to_le32(REC_01_FLAG);

	while (size) {
		if (size >= TRANSFER_LIMIT) {
			cur_size = TRANSFER_LIMIT;
		} else
			cur_size = size;
		memcpy(TEST, buf, 72);
		for (i = 0; i < cur_size / 4; i++) {
			reg_addr = REG_SRAM_BEGIN + i * 4;
			memcpy(tx_buff, buf + offset + i * 4, 4);
			ret = oneplus_u16_i2c_write(chip->client, reg_addr, 4, tx_buff);
			if (ret < 0) {
				pr_err("write SRAM fail");
				return -1;
			}
		}
		//ret = oneplus_u16_i2c_read(chip->client, REG_STATE, 4, readbuf);
		//pr_err("teh REG_STATE1: %d", *(u32*)readbuf);
		ret = oneplus_u16_i2c_write(chip->client, REG_HOST, 4, (u8 *)(&rec_0O_flag));
		//mdelay(3);
		//write rec_00 into host
		if (ret < 0) {
			pr_err("write rec_00 into host");
			return -1;
		}
		//read slave
		do {
			ret = oneplus_u16_i2c_read(chip->client, REG_STATE, 4, readbuf);
			pr_err(" the try_count: %d, the REG_STATE: %d", try_count, *(u32*)readbuf);
			//msleep(10);
			ret = oneplus_u16_i2c_read(chip->client, REG_SLAVE, 4, readbuf);
			if (ret < 0) {
				pr_err("read slave ack fail");
				return -1;
			}
			try_count--;
		} while (*(u32 *)readbuf == SLAVE_BUSY);
		pr_info("the try_count: %d, the readbuf: %x\n", try_count, *(u32 *)readbuf);
		if ((*(u32 *)readbuf != SLAVE_ACK) && (*(u32 *)readbuf != SLAVE_ACK_01)) {
			pr_err(" slave ack fail");
			return -1;
		}

		//write rec_01 into host
		ret = oneplus_u16_i2c_write(chip->client, REG_HOST, 4, (u8 *)(&rec_01_flag));
		//write rec_00 into host
		if (ret < 0) {
			pr_err("write rec_00 into host");
			return -1;
		}

		//msleep(50);
		offset += cur_size;
		size -= cur_size;
		try_count = 5;
	}
	return 0;
}

int DownloadFirmware(struct fastchg_device_info *chip, const u8 *buf, u32 size)
{
	u8 transfer_buf[TRANSFER_LIMIT];
	u32 onetime_size = TRANSFER_LIMIT - 8;
	u32 index = 0;
	u32 offset = 0;
	int ret = 0;

	pr_info("size: %d\n", size);
	do {
		memset(transfer_buf, 0, TRANSFER_LIMIT);
		if (size >= onetime_size) {
			memcpy(transfer_buf, buf + offset, onetime_size);
			size-= onetime_size;
			offset += onetime_size;
		} else {
			memcpy(transfer_buf, buf + offset, size);
			offset += size;
			size = 0;
		}
		*((u32 *)(transfer_buf + onetime_size)) = index;
		*((u32 *)(transfer_buf + onetime_size + 4)) = js_hash(transfer_buf, onetime_size + 4);
		ret = WriteSram(chip, transfer_buf, TRANSFER_LIMIT);
		if (ret != 0) {
			return ret;
		}
		pr_info("index: %d\n", index);
		index++;
	} while (size);
	return 0;
}

static int rk826_fw_write(struct fastchg_device_info *chip,
			  const unsigned char *data_buf,
			  unsigned int offset, unsigned int length)
{
	int ret = 0;
	int iTryCount = 3;
	struct_req req = {0};
	u32 sync_flag = cpu_to_le32(SYNC_FLAG);
	u32 force_update_flag = cpu_to_le32(FORCE_UPDATE_FLAG);
	u32 sw_reset_flag = cpu_to_le32(SW_RESET_FLAG);
	u32 rec_01_flag = cpu_to_le32(REC_01_FLAG);
	u8 read_buf[4] = {0};

	oneplus_u16_i2c_write(chip->client, REG_SYS0, 4, (u8 *)(&force_update_flag));
	msleep(10);
	oneplus_u16_i2c_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	while (iTryCount) {
		msleep(10);
		ret = oneplus_u16_i2c_write(chip->client, REG_HOST, 4, (u8 *)(&sync_flag));
		if (ret < 0) {
			pr_err("write sync failed!");
			goto update_fw_err;
		}

		//2.check ~sync
		msleep(10);
		ret = oneplus_u16_i2c_read(chip->client, REG_HOST, 4, read_buf);
		pr_info("the data: %x, %x, %x, %x\n", read_buf[0], read_buf[1], read_buf[2], read_buf[3]);
		pr_info("the data: %x, %x, %x, %x\n", *(u8 *)(&sync_flag), *((u8 *)(&sync_flag) + 1), *((u8 *)(&sync_flag) + 2), *((u8 *)(&sync_flag) + 3));

		if (ret < 0) {
			pr_err("read sync failed!");
			goto update_fw_err;
		}
		if (*(u32 *)read_buf != NOT_SYNC_FLAG) {
			pr_err("check ~sync failed!, data=0x%x\n", *(u32 *)read_buf);
			iTryCount--;
			msleep(50);
			continue;
		}
		break;
	}

	if (iTryCount == 0) {
		pr_err("Failed to sync!");
		goto update_fw_err;
	}

	// write rec_01
	ret = oneplus_u16_i2c_write(chip->client, REG_HOST, 4, (u8 *)(&rec_01_flag));
	if (ret < 0) {
		pr_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	msleep(10);

	// read reg_state
	ret = oneplus_u16_i2c_read(chip->client, REG_STATE, 4, read_buf);
	if (ret<0) {
		pr_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_REQUEST) {
		pr_err("Failed to go into request_state!");
		goto update_fw_err;
	}

	// send req
	req.tag = 0x51455220;
	req.ram_offset = 0;
	req.length =  chip->dashchg_fw_ver_count;
	req.timeout = 0;
	req.fw_crc = js_hash(data_buf, req.length);
	req.header_crc = js_hash((const u8*)&req, sizeof(req) - 4);
	if ((ret = WriteSram(chip, (const u8* )&req, sizeof(req))) != 0) {
		pr_err("failed to send request!err=%d\n", ret);
		goto update_fw_err;
	}
	msleep(10);

	// read state firwware
	ret = oneplus_u16_i2c_read(chip->client, REG_STATE, 4, read_buf);
	pr_info("read state firwware: %x\n", *(u32 *)read_buf);
	if (ret < 0) {
		pr_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FIRMWARE) {
		pr_err("Failed to go into firmware_state");
		goto update_fw_err;
	}

	// send fw
	if ((ret = DownloadFirmware(chip, data_buf, chip->dashchg_fw_ver_count)) != 0) {
		pr_err("failed to send firmware");
		goto update_fw_err;
	}

	ret = oneplus_u16_i2c_read(chip->client, REG_STATE, 4, read_buf);
	if (ret < 0) {
		pr_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FINISH) {
		pr_err("Failed to go into finish_state");
		goto update_fw_err;
	}
	oneplus_u16_i2c_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	pr_info("success\n");
	return 0;

update_fw_err:
	pr_err("fail\n");
	return 1;
}
/*RockChip RK826 firmware upgrade end*/

/*RichTek RT5125 firmware upgrade start*/
#define DEFAULT_MAX_BINSIZE	(16 * 1024)
#define DEFAULT_MAX_DATALEN	(128)
#define DEFAULT_MAX_PAGELEN	(128)
#define DEFAULT_MAX_PAGEIDX	(DEFAULT_MAX_BINSIZE / DEFAULT_MAX_PAGELEN)
#define DEFAULT_VERINFO_LEN	(10)
#define DEFAULT_PAGEWR_RETRY	(110)
#define DEFAULT_I2C_RETRY	(5)
/* cmd 1 + data 128 + crc8 */
#define DEFAULT_MAX_BUFFLEN	(1 + DEFAULT_MAX_DATALEN + 1)
#define RT5125_CRC16_INIT	(0xffff)
#define RT5125_CID		(0x5125)

#define RT5125_CHIP_ID		(0x00)
#define RT5125_MTP_INFO_0	(0x01)
#define RT5125_PAGE_IDX		(0x10)
#define RT5125_ACCESS_CTL	(0x12)
#define RT5125_STATUS		(0x13)
#define RT5125_DATA_BUF		(0x80)
#define RT5125_FW_CRC16_INFO	(0x90)
#define RT5125_CMD_CRC8_INFO	(0x91)

#define RT5125_MTP_MODE		BIT(0)
#define RT5125_FW_CRC16_RSLT	BIT(3)
#define RT5125_OPSTATUS_MASK	(0x7)
#define RT5125_OPSTATUS_DFAIL	(0x7)
#define RT5125_OPSTATUS_CFAIL	(0x6)
#define RT5125_OPSTATUS_PFAIL	(0x5)
#define RT5125_OPSTATUS_FAIL	(0x4)
#define RT5125_OPSTATUS_SUCCESS	(0x2)
#define RT5125_OPSTATUS_ONGOING	(0x1)
#define RT5125_OPSTATUS_IDLE	(0x0)
#define RT5125_OPSTATUS_FAILMSK	BIT(2)
#define RT5125_WT_PAGE		BIT(7)
#define RT5125_RD_PAGE		BIT(6)
#define RT5125_WT_FW_CRC16	BIT(3)
#define RT5125_FW_CRC16_VRFY	BIT(2)
#define RT5125_WT_KEY		BIT(1)
#define CRC8_TABLE_SIZE	256

static u8 crc8_table[CRC8_TABLE_SIZE];
static DEFINE_MUTEX(data_lock);

static u16 const crc16_table[256] = {
0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
};

static u8 crc8(const u8 table[CRC8_TABLE_SIZE], u8 *pdata, size_t nbytes, u8 crc)
{
	/* loop over the buffer data */
	while (nbytes-- > 0)
		crc = table[(crc ^ *pdata++) & 0xff];

	return crc;
}

static void crc8_populate_msb(u8 table[CRC8_TABLE_SIZE], u8 polynomial)
{
	int i, j;
	const u8 msbit = 0x80;
	u8 t = msbit;

	table[0] = 0;
	for (i = 1; i < CRC8_TABLE_SIZE; i *= 2) {
		t = (t << 1) ^ (t & msbit ? polynomial : 0);
		for (j = 0; j < i; j++)
			table[i+j] = table[j] ^ t;
	}
}

static inline u16 crc16_byte(u16 crc, const u8 data)
{
	return (crc >> 8) ^ crc16_table[(crc ^ data) & 0xff];
}

static u16 crc16(u16 crc, u8 const *buffer, size_t len)
{
	while (len--)
		crc = crc16_byte(crc, *buffer++);
	return crc;
}

DEFINE_MUTEX(dma_wr_access_rt5125);
#define I2C_MASK_FLAG	(0x00ff)
#define GTP_DMA_MAX_TRANSACTION_LENGTH	255 /* for DMA mode */
static int op_i2c_dma_read(struct i2c_client *client, u8 addr, s32 len, u8 *rxbuf)
{
    int ret;
    s32 retry = 0;
    u8 buffer[1] = {0};
	char gpDMABuf_pa[GTP_DMA_MAX_TRANSACTION_LENGTH] = {0};
    struct i2c_msg msg[2] = {
        {
            .addr = (client->addr & I2C_MASK_FLAG),
            .flags = 0,
            .buf = buffer,
            .len = 1,
        },
        {
            .addr = (client->addr & I2C_MASK_FLAG),
            .flags = I2C_M_RD,
            .buf = (__u8 *)gpDMABuf_pa,   /*modified by PengNan*/
            .len = len,
        },
    };

    mutex_lock(&dma_wr_access_rt5125);
    buffer[0] = (u8)(addr & 0xFF);

    if (rxbuf == NULL) {
        mutex_unlock(&dma_wr_access_rt5125);
        return -1;
    }
	//chg_debug("rk826 dma i2c read: 0x%x, %d bytes(s)\n", addr, len);
    for (retry = 0; retry < 5; ++retry) {
        ret = i2c_transfer(client->adapter, &msg[0], 2);
        if (ret < 0) {
            continue;
        }
        memcpy(rxbuf, gpDMABuf_pa, len);
        mutex_unlock(&dma_wr_access_rt5125);
        return 0;
    }
    pr_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);
    mutex_unlock(&dma_wr_access_rt5125);

    return ret;
}

static int op_i2c_dma_write(struct i2c_client *client, u8 addr, s32 len, u8 const *txbuf)
{
    int ret = 0;
    s32 retry = 0;
	char gpDMABuf_pa[GTP_DMA_MAX_TRANSACTION_LENGTH] = {0};
    u8 *wr_buf = gpDMABuf_pa;
    struct i2c_msg msg = {
        .addr = (client->addr & I2C_MASK_FLAG),
        .flags = 0,
        .buf = (__u8 *)gpDMABuf_pa, /*modified by PengNan*/
        .len = 1 + len,
    };

    mutex_lock(&dma_wr_access_rt5125);
    wr_buf[0] = (u8)(addr & 0xFF);
    if (txbuf == NULL) {
        mutex_unlock(&dma_wr_access_rt5125);
        return -1;
    }
    memcpy(wr_buf + 1, txbuf, len);
	//chg_debug("rk826 dma i2c write: 0x%x, %d bytes(s)\n", addr, len);
    for (retry = 0; retry < 5; ++retry) {
        ret = i2c_transfer(client->adapter, &msg, 1);
        if (ret < 0) {
            continue;
        }
        mutex_unlock(&dma_wr_access_rt5125);
        return 0;
    }
    pr_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);
    mutex_unlock(&dma_wr_access_rt5125);

    return ret;
}

static int rt5125_i2c_block_read(struct fastchg_device_info *chip,
				 u8 cmd, u8 *data, s32 len)
{
	u8 crc;
	int retry = 0, ret;
	static u8 data_buff[DEFAULT_MAX_BUFFLEN];
//	chip->client = the_chip->client;

	if (len > DEFAULT_MAX_DATALEN || len <= 0)
		return -EINVAL;

	mutex_lock(&data_lock);
retry_read:
	if (retry++ >= DEFAULT_I2C_RETRY) {
		ret = -EIO;
		goto out_read;
	}

	ret = op_i2c_dma_read(chip->client, cmd, len + 1, data_buff + 1);
	if (ret < 0)
		goto out_read;
	data_buff[0] = cmd;
	/* verify crc 8 : cmd + data */
	crc = crc8(crc8_table, data_buff, len + 1, 0);
	if (crc != data_buff[len + 1])
		goto retry_read;
	memcpy(data, data_buff + 1, len);
out_read:
	mutex_unlock(&data_lock);
	return ret;
}

static int rt5125_i2c_block_write(struct fastchg_device_info *chip,
				  u8 cmd, const u8 *data, s32 len)
{
	int retry = 0, ret;
	static u8 data_buff[DEFAULT_MAX_BUFFLEN];

	if (len > DEFAULT_MAX_DATALEN || len <= 0)
		return -EINVAL;

	mutex_lock(&data_lock);
retry_write:
	if (retry++ >= DEFAULT_I2C_RETRY) {
		ret = -EIO;
		goto out_write;
	}

	data_buff[0] = cmd;
	memcpy(data_buff + 1, data, len);
	data_buff[len + 1] = crc8(crc8_table, data_buff, len + 1, 0);
	ret = op_i2c_dma_write(chip->client, cmd, len + 1, data_buff + 1);
	if (ret < 0)
		goto out_write;
	ret = op_i2c_dma_read(chip->client, RT5125_CMD_CRC8_INFO, 1, data_buff);
	if (ret < 0)
		goto out_write;
	if (data_buff[0] != (~data_buff[len +1] & 0xff))
		goto retry_write;

out_write:
	mutex_unlock(&data_lock);
	return ret;
}

static bool rt5125_fw_check(struct fastchg_device_info *chip)
{
	const u8 *data = dashchg_firmware_data;
	s32 len = chip->dashchg_fw_ver_count;
	u8 rwdata = 0, status, fwdata[DEFAULT_MAX_PAGELEN * 2];
	u8 last_page[DEFAULT_MAX_PAGELEN];
	u8 verinfo[DEFAULT_VERINFO_LEN];
	u32 fw_info = 0;
	int i, idx, ret, retry = 0;

	if (!data) {
		pr_err("rt5125_fw_data Null, Return\n");
		return FW_CHECK_FAIL;
	}

	/* poly = x^8 + x^2 + x^1 + 1 */
	crc8_populate_msb(crc8_table, 0x7);

	/* write access for crc16 verify */
	rwdata = (u8)RT5125_FW_CRC16_VRFY;
	ret = rt5125_i2c_block_write(chip, RT5125_ACCESS_CTL,
				   &rwdata, sizeof(rwdata));
	if (ret < 0) {
		pr_err("fw_crc_vrfy access fail\n");
		goto fw_update_check_err;
	}

busy_retry:
	/* wait 200ms for CRC verify */
	msleep(200);

	/* check fw_crc_vrfy status */
	ret = rt5125_i2c_block_read(chip, RT5125_STATUS,
				  &rwdata, sizeof(rwdata));
	if (ret < 0) {
		pr_err("fw_crc_vrfy read status fail\n");
		goto fw_update_check_err;
	}
	status = rwdata & RT5125_OPSTATUS_MASK;
	if (status & RT5125_OPSTATUS_ONGOING) {
		pr_err("fw_crc_vrfy ongoing\n");
		if (++retry < 10)
			goto busy_retry;
		else {
			pr_err("fw_crc_vrfy busy retry fail\n");
			goto fw_update_check_err;
		}
	} else if (status & RT5125_OPSTATUS_FAILMSK) {
		pr_err("fw_crc_vrfy status fail\n");
		goto fw_update_check_err;
	} else if (status == RT5125_OPSTATUS_SUCCESS) {
		pr_err("fw_crc_vrfy success\n");
	} else {
		pr_err("fw_crc_vrfy unknown 0x%02x\n", status);
		goto fw_update_check_err;
	}

	if (!(rwdata & RT5125_FW_CRC16_RSLT)) {
		pr_err("fw_crc_vrfy crc result fail\n");
		goto fw_update_check_err;
	}
	pr_info("fw_crc_vrfy OK\n");

	/* read orig fw info */
	idx = DEFAULT_MAX_PAGEIDX - 1;
	rwdata = (u8)idx;
	ret = rt5125_i2c_block_write(chip, RT5125_PAGE_IDX,
					 &rwdata, sizeof(rwdata));
	if (ret < 0) {
		pr_err("[%d] rdpage idx fail\n", idx);
		goto fw_update_check_err;
	}

	/* access to read page from mtp to buffer */
	rwdata = (u8)RT5125_RD_PAGE;
	ret = rt5125_i2c_block_write(chip, RT5125_ACCESS_CTL,
				   &rwdata, sizeof(rwdata));
	if (ret < 0) {
		pr_err("[%d]rdpage access fail\n", idx);
		goto fw_update_check_err;
	}

	/* wait 5ms for mtp read */
	msleep(5);

	/* check page rd status */
	ret = rt5125_i2c_block_read(chip, RT5125_STATUS,
					&rwdata, sizeof(rwdata));
	if (ret < 0) {
		pr_err("[%d] read status fail\n", idx);
		goto fw_update_check_err;
	}
	status = rwdata & RT5125_OPSTATUS_MASK;
	if (status & RT5125_OPSTATUS_FAILMSK) {
		pr_err("[%d] rdpage fail 0x%02x\n", idx, status);
		goto fw_update_check_err;
	} else if (status == RT5125_OPSTATUS_SUCCESS) {
		pr_err("[%d] rdpage success\n", idx);
	} else {
		pr_err("[%d]rdpage unknown 0x%02x\n", idx, status);
		goto fw_update_check_err;
	}

	/* page data */
	ret = rt5125_i2c_block_read(chip, RT5125_DATA_BUF,
					last_page, DEFAULT_MAX_PAGELEN);
	if (ret < 0) {
		pr_err("[%d] rdpage data fail\n", idx);
		goto fw_update_check_err;
	}
	/* get fw size */
	fw_info = (last_page[123] << 8) + last_page[122];
	if (fw_info < DEFAULT_VERINFO_LEN) {
		pr_err("size [%d] smaller than verinfo\n", fw_info);
		goto fw_update_check_err;
	}

	/* always read last two single page */
	idx = fw_info / DEFAULT_MAX_PAGELEN - 1;
	for (i = 0; i < 2; i++) {
		/* page idx */
		if ((idx + i) < 0 || (idx + i) >= DEFAULT_MAX_PAGEIDX)
			continue;
		rwdata = (u8)(idx + i);
		ret = rt5125_i2c_block_write(chip, RT5125_PAGE_IDX,
						 &rwdata, sizeof(rwdata));
		if (ret < 0) {
			pr_err("[%d] rdpage idx fail\n", idx + i);
			goto fw_update_check_err;
		}

		/* access to read page from mtp to buffer */
		rwdata = (u8)RT5125_RD_PAGE;
		ret = rt5125_i2c_block_write(chip, RT5125_ACCESS_CTL,
						 &rwdata, sizeof(rwdata));
		if (ret < 0) {
			pr_err("[%d]rdpage access fail\n", idx + i);
			goto fw_update_check_err;
		}

		/* wait 5ms for mtp read */
		msleep(5);

		/* check page rd status */
		ret = rt5125_i2c_block_read(chip, RT5125_STATUS,
						&rwdata, sizeof(rwdata));
		if (ret < 0) {
			pr_err("[%d] read status fail\n", idx + i);
			goto fw_update_check_err;
		}
		status = rwdata & RT5125_OPSTATUS_MASK;
		if (status & RT5125_OPSTATUS_FAILMSK) {
			pr_err("[%d] rdpage fail 0x%02x\n", idx + i, status);
			goto fw_update_check_err;
		} else if (status == RT5125_OPSTATUS_SUCCESS) {
			pr_err("[%d] rdpage success\n", idx + i);
		} else {
			pr_err("[%d]rdpage unknown 0x%02x\n", idx + i, status);
			goto fw_update_check_err;
		}

		/* page data */
		ret = rt5125_i2c_block_read(chip, RT5125_DATA_BUF,
						fwdata + i * DEFAULT_MAX_PAGELEN,
						DEFAULT_MAX_PAGELEN);
		if (ret < 0) {
			pr_err("[%d] rdpage data fail\n", idx + i);
			goto fw_update_check_err;
		}
	}

	idx = DEFAULT_MAX_PAGELEN +
		fw_info % DEFAULT_MAX_PAGELEN - DEFAULT_VERINFO_LEN;
	memcpy(verinfo, fwdata + idx, DEFAULT_VERINFO_LEN);

	idx = len - DEFAULT_VERINFO_LEN;
	ret = memcmp(verinfo, data + idx, DEFAULT_VERINFO_LEN);
	if (ret != 0) {
		pr_err("verinfo not equal\n");
		goto fw_update_check_err;
	}

	return FW_CHECK_SUCCESS;
fw_update_check_err:
	pr_err("rt5125_fw_data check fail\n");
	return FW_CHECK_FAIL;
}

static int rt5125_fw_update(struct fastchg_device_info *chip)
{
	const u8 *data = dashchg_firmware_data;
	s32 len = chip->dashchg_fw_ver_count;
	u8 rwdata = 0, status, fwdata[DEFAULT_MAX_PAGELEN];
	s32 elapsed, wr_len;
	u32 fw_info;
	int i, idx, retry, ret;

	/* poly = x^8 + x^2 + x^1 + 1 */
	crc8_populate_msb(crc8_table, 0x7);

	ret = rt5125_i2c_block_read(chip, RT5125_MTP_INFO_0,
						&status, sizeof(status));
	pr_err("MTP status=0x%02x", status);
	/* wirte every single page */
	for (i = 0; i < len ; i += DEFAULT_MAX_PAGELEN) {
		idx = i / DEFAULT_MAX_PAGELEN;
		elapsed = len - i;
		wr_len = (elapsed > DEFAULT_MAX_PAGELEN)
				? DEFAULT_MAX_PAGELEN : elapsed;
		memset(fwdata, 0xff, DEFAULT_MAX_PAGELEN);
		memcpy(fwdata, data + i, wr_len);
		/* page idx */
		rwdata = (u8)idx;
		ret = rt5125_i2c_block_write(chip, RT5125_PAGE_IDX,
					&rwdata, sizeof(rwdata));
		if (ret < 0) {
			pr_err("[%d] wrpage idx fail, ret=%d\n", idx, ret);
			goto update_fw_err;
		}

		/* page data */
		ret = rt5125_i2c_block_write(chip, RT5125_DATA_BUF,
					fwdata, DEFAULT_MAX_PAGELEN);
		if (ret < 0) {
			pr_err("[%d] wrpage data fail\n", idx);
			goto update_fw_err;
		}

		/* access to write page from buffer to mtp */
		rwdata = (u8)RT5125_WT_PAGE;
		ret = rt5125_i2c_block_write(chip, RT5125_ACCESS_CTL,
					&rwdata, sizeof(rwdata));
		if (ret < 0) {
			pr_err("[%d] wrpage access fail\n", idx);
			goto update_fw_err;
		}

		/* wait 128ms for mtp write */
		msleep(128);
		retry = 0;

busy_check:
		if (retry++ > DEFAULT_PAGEWR_RETRY) {
			pr_err("[%d] wrpage over retrycnt\n", idx);
			goto update_fw_err;
		}

		msleep(5);
		/* check page wr status */
		ret = rt5125_i2c_block_read(chip, RT5125_STATUS,
					    &rwdata, sizeof(rwdata));
		if (ret < 0) {
			pr_err("[%d] read status fail\n", idx);
			goto update_fw_err;
		}
		status = rwdata & RT5125_OPSTATUS_MASK;
		if (status & RT5125_OPSTATUS_FAILMSK) {
			pr_err("[%d] wrpage fail 0x%02x\n", idx, status);
			goto update_fw_err;
		} else if (status == RT5125_OPSTATUS_ONGOING) {
			pr_err("[%d] wrpage ongoing\n", idx);
			goto busy_check;
		} else if (status == RT5125_OPSTATUS_SUCCESS) {
			pr_err("[%d] wrpage success\n", idx);
		} else {
			pr_err("[%d] wrpage unknown 0x%02x\n", idx, status);
			goto update_fw_err;
		}
	}

	/* FWINFO[31:16] = CRC16_H:CRC16_L */
	fw_info = crc16(RT5125_CRC16_INIT, data, len) << 16;
	/* FWINFO[15:0] = FWSIZE_H:FWSIZE_L */
	fw_info |= (u16)len;
	ret = rt5125_i2c_block_write(chip, RT5125_FW_CRC16_INFO,
				(void *)&fw_info, sizeof(fw_info));
	if (ret < 0) {
		pr_err("write fw info fail\n");
		goto update_fw_err;
	}

	/* write access for crc16 write */
	rwdata = (u8)RT5125_WT_FW_CRC16;;
	ret = rt5125_i2c_block_write(chip, RT5125_ACCESS_CTL,
				&rwdata, sizeof(rwdata));
	if (ret < 0) {
		pr_err("wr_fw_crc access fail\n");
		goto update_fw_err;
	}

	/* wait 20ms for CRC write */
	msleep(20);

	/* check wr_fw_crc status */
	ret = rt5125_i2c_block_read(chip, RT5125_STATUS,
				    &rwdata, sizeof(rwdata));
	if (ret < 0) {
		pr_err("wr_fw_crc read status fail\n");
		goto update_fw_err;
	}
	status = rwdata & RT5125_OPSTATUS_MASK;
	if (status & RT5125_OPSTATUS_FAILMSK) {
		pr_err("wr_fw_crc status fail\n");
			goto update_fw_err;
	} else if (status == RT5125_OPSTATUS_SUCCESS) {
		pr_err("wr_fw_crc success\n");
	} else {
		pr_err("wr_fw_crc unknown 0x%02x\n", status);
		goto update_fw_err;
	}


	/* write access for crc16 verify */
	rwdata = (u8)RT5125_FW_CRC16_VRFY;;
	ret = rt5125_i2c_block_write(chip, RT5125_ACCESS_CTL,
				&rwdata, sizeof(rwdata));
	if (ret < 0) {
		pr_err("fw_crc_vrfy access fail\n");
		goto update_fw_err;
	}

	/* wait 200ms for CRC verify */
	msleep(200);

	/* check fw_crc_vrfy status */
	ret = rt5125_i2c_block_read(chip, RT5125_STATUS,
				    &rwdata, sizeof(rwdata));
	if (ret < 0) {
		pr_err("fw_crc_vrfy read status fail\n");
		goto update_fw_err;
	}
	status = rwdata & RT5125_OPSTATUS_MASK;
	if (status & RT5125_OPSTATUS_FAILMSK) {
		pr_err("fw_crc_vrfy status fail\n");
		goto update_fw_err;
	} else if (status == RT5125_OPSTATUS_SUCCESS) {
		pr_err("fw_crc_vrfy success\n");
	} else {
		pr_err("fw_crc_vrfy unknown 0x%02x\n", status);
		goto update_fw_err;
	}

	if (!(rwdata & RT5125_FW_CRC16_RSLT)) {
		pr_err("fw_crc_vrfy crc result fail\n");
		goto update_fw_err;
	}
	pr_info("fw_crc_vrfy OK\n");

	/* write access for keyword */
	rwdata = (u8)RT5125_WT_KEY;
	ret = rt5125_i2c_block_write(chip, RT5125_ACCESS_CTL,
				&rwdata, sizeof(rwdata));
	if (ret < 0) {
		pr_err("key_write access fail\n");
		goto update_fw_err;
	}

	/* wait keyword write success */
	msleep(10);

	/* check keyword write status */
	ret = rt5125_i2c_block_read(chip, RT5125_STATUS,
				    &rwdata, sizeof(rwdata));
	if (ret < 0) {
		pr_err("key_write read status fail\n");
		goto update_fw_err;
	}
	status = rwdata & RT5125_OPSTATUS_MASK;
	if (status & RT5125_OPSTATUS_FAILMSK) {
		pr_err("key_write status fail\n");
		goto update_fw_err;
	} else if (status == RT5125_OPSTATUS_SUCCESS) {
		pr_err("key_write success\n");
	} else {
		pr_err("key_write unknown 0x%02x\n", status);
		goto update_fw_err;
	}

	pr_info("success\n");
	return 0;
update_fw_err:
	pr_err("fail\n");
	return 1;
}
#endif

static irqreturn_t irq_rx_handler(int irq, void *dev_id);
static void reset_mcu_and_request_irq(struct fastchg_device_info *di)
{
	int ret;

	pr_info("\n");
	gpio_direction_output(di->ap_clk, 1);
	usleep_range(10000, 10001);
	if (di->is_swarp_supported) {
		(void)opchg_mcu_action(ACTION_MODE_RESET_ACTIVE);
	} else {
		gpio_direction_output(di->mcu_en_gpio, 1);
		usleep_range(10000, 10001);
		gpio_direction_output(di->mcu_en_gpio, 0);
		usleep_range(5000, 5001);
	}
	opchg_set_mcu_data_read(di);
	di->irq = gpio_to_irq(di->ap_data);

	/* 0x01:rising edge, 0x02:falling edge */
	ret = request_irq(di->irq, irq_rx_handler,
			IRQF_TRIGGER_RISING, "mcu_data", di);
	if (ret < 0)
		pr_err("request ap rx irq failed.\n");
	else
		di->irq_enabled = true;
	irq_set_status_flags(di->irq, IRQ_DISABLE_UNLAZY);
}


static void dashchg_fw_update(struct work_struct *work)
{
	unsigned char zero_buf[1] = {0};
	unsigned char addr_buf[2] = {0x88, 0x00};
	unsigned char temp_buf[1] = {0};
	int i, rc = 0;
	unsigned int addr;
	int download_again = 0;
	struct fastchg_device_info *di = container_of(work,
			struct fastchg_device_info,
			update_firmware.work);

	addr_buf[0] = fastchg_di->addr_low;
	addr_buf[1] = fastchg_di->addr_high;
	addr = (addr_buf[0] <<  8)  +  (addr_buf[1] & 0xFF);
	__pm_stay_awake(di->fastchg_update_fireware_lock);
	if (di->n76e_present) {
		rc = n76e_fw_check(di);
#ifdef OP_SWARP_SUPPORTED
	} else if (di->is_swarp_supported) {
		(void)opchg_mcu_action(ACTION_MODE_SWITCH_UPGRADE);
		if (di->asic_hw_id == ROCKCHIP_RK826) {
			rc = rk826_fw_check(di);
		} else if (di->asic_hw_id == RICHTEK_RT5125) {
			rc = rt5125_fw_check(di);
		}
#endif
	} else {
		rc = dashchg_fw_check();
	}
	if (rc == FW_CHECK_SUCCESS) {
		di->firmware_already_updated = true;
		reset_mcu_and_request_irq(di);
#ifdef OP_SWARP_SUPPORTED
		if (di->is_swarp_supported)
			(void)opchg_mcu_action(ACTION_MODE_RESET_SLEEP);
#endif
		__pm_relax(di->fastchg_update_fireware_lock);
		set_property_on_smbcharger(POWER_SUPPLY_PROP_SWITCH_DASH, true);
		di->dash_firmware_ok = 1;
		pr_info("FW check success\n"); /* david@bsp add log */
		return;
	}
#ifdef OP_SWARP_SUPPORTED
update_asic_fw:
	if (di->is_swarp_supported) {
		if (di->asic_hw_id == ROCKCHIP_RK826) {
			rc = rk826_fw_write(di, dashchg_firmware_data, 0, di->dashchg_fw_ver_count);
		} else if (di->asic_hw_id == RICHTEK_RT5125) {
			rc = rt5125_fw_update(di);
		}
		if (rc) {
			download_again++;
			if (download_again > 3)
				goto update_fw_err;
			(void)opchg_mcu_action(ACTION_MODE_SWITCH_UPGRADE);
			msleep(1000);
			pr_err("fw download fail, download fw again\n");
			goto update_asic_fw;
		}
		goto update_done;
	}
#endif
	pr_info("start erasing data.......\n");
update_fw:
	/* erase address 0x200-0x7FF */
	for (i = 0; i < di->erase_count; i++) {
		/* first:set address */
		rc = oneplus_dash_i2c_write(mcu_client, 0x01, 2, &addr_buf[0]);
		if (rc < 0) {
			pr_err("dashchg_update_fw, i2c_write 0x01 error\n");
			goto update_fw_err;
		}

		/* erase data:0x10 words once */
		if (!di->n76e_present)
			oneplus_dash_i2c_write(mcu_client,
					0x04, 1, &zero_buf[0]);
		usleep_range(1000, 1001);
		oneplus_dash_i2c_read(mcu_client, 0x04, 1, &temp_buf[0]);
		if (di->n76e_present)
			usleep_range(7000, 7100);
		/* erase data:0x10 words once */
		addr = addr + 0x10;
		addr_buf[0] = addr >> 8;
		addr_buf[1] = addr & 0xFF;
	}
	usleep_range(10000, 10001);
	dashchg_fw_write(dashchg_firmware_data, 0, di->dashchg_fw_ver_count);

	/* fw check begin:read data from mcu and compare*/
	/*it with dashchg_firmware_data[] */
	if (di->n76e_present)
		rc = n76e_fw_check(di);
	else
		rc = dashchg_fw_check();
	if (rc == FW_CHECK_FAIL) {
		download_again++;
		if (download_again > 3)
			goto update_fw_err;
		if (di->is_swarp_supported)
			(void)opchg_mcu_action(ACTION_MODE_RESET_ACTIVE);
		else
			mcu_en_gpio_set(0);
		msleep(1000);
		pr_err("fw check fail, download fw again\n");
		goto update_fw;
	}
	/* fw check end */

	usleep_range(2000, 2001);
	/* jump to app code begin */
	oneplus_dash_i2c_write(mcu_client, 0x06, 1, &zero_buf[0]);
	oneplus_dash_i2c_read(mcu_client, 0x06, 1, &temp_buf[0]);
	/* jump to app code end */
#ifdef OP_SWARP_SUPPORTED
update_done:
#endif
	di->firmware_already_updated = true;
	reset_mcu_and_request_irq(di);
#ifdef OP_SWARP_SUPPORTED
	if (di->is_swarp_supported)
		(void)opchg_mcu_action(ACTION_MODE_RESET_SLEEP);
#endif
	__pm_relax(di->fastchg_update_fireware_lock);
	set_property_on_smbcharger(POWER_SUPPLY_PROP_SWITCH_DASH, true);
	di->dash_firmware_ok = 1;
	pr_info("result=success\n");
	return;

update_fw_err:
	di->firmware_already_updated = true;
	reset_mcu_and_request_irq(di);
#ifdef OP_SWARP_SUPPORTED
	if (di->is_swarp_supported)
		(void)opchg_mcu_action(ACTION_MODE_ENABLE);
#endif
	__pm_relax(di->fastchg_update_fireware_lock);
	set_property_on_smbcharger(POWER_SUPPLY_PROP_SWITCH_DASH, true);
	pr_err("result=fail\n");
}

static struct external_battery_gauge *bq27541_data;
void bq27541_information_register(
	struct external_battery_gauge *fast_chg)
{
	if (bq27541_data) {
		bq27541_data = fast_chg;
		pr_err("multiple battery gauge called\n");
	} else {
		bq27541_data = fast_chg;
	}
}
EXPORT_SYMBOL(bq27541_information_register);

static void update_fast_chg_started(void)
{
	if (bq27541_data && bq27541_data->fast_chg_started_status)
		bq27541_data->fast_chg_started_status(
		fastchg_di->fast_chg_started);
}

void bq27541_information_unregister(struct external_battery_gauge *batt_gauge)
{
	bq27541_data = NULL;
}

static bool bq27541_fast_chg_started(void)
{
	if (fastchg_di)
		return fastchg_di->fast_chg_started;

	return false;
}

static bool get_fastchg_status(void)
{
	if (fastchg_di)
		return !fastchg_di->fast_chg_error;
	return true;
}

static bool bq27541_get_fast_low_temp_full(void)
{
	if (fastchg_di)
		return fastchg_di->fast_low_temp_full;

	return false;
}

static int bq27541_set_fast_chg_allow(bool enable)
{
	if (fastchg_di)
		fastchg_di->fast_chg_allow = enable;

	return 0;
}

static void clean_status(void)
{
	if (fastchg_di) {
		fastchg_di->dash_enhance = 0;
		fastchg_di->sid = 0;
#ifdef OP_SWARP_SUPPORTED
		oneplus_notify_dash_charger_type(CHARGER_DEFAULT);
#endif
		oneplus_notify_adapter_sid(fastchg_di->sid);
		schedule_delayed_work(&fastchg_di->disable_mcu_work,
					msecs_to_jiffies(2000));
		fastchg_di->fast_chg_started = false;
		fastchg_di->fast_chg_ing = false;
		update_fast_chg_started();
	}
}
static void disable_mcu_work_func(struct work_struct *work)
{
	if (!is_usb_pluged() && fastchg_di->is_swarp_supported
		&& fastchg_di->firmware_already_updated) {
		pr_err("usb unpluged, disable mcu.");
		if (fastchg_di->is_swarp_supported)
			(void)opchg_mcu_action(ACTION_MODE_RESET_SLEEP);
		else
			set_mcu_active(0);
	}
}

#ifdef OP_SWARP_SUPPORTED
static int dash_get_fastchg_warm(void)
{
	if (fastchg_di)
		return fastchg_di->fast_normal_to_warm ? 1 : 0;

	return 0;
}
#endif
static bool bq27541_get_fast_chg_allow(void)
{
	if (fastchg_di)
		return fastchg_di->fast_chg_allow;

	return false;
}

static bool bq27541_fast_switch_to_normal(void)
{
	if (fastchg_di)
		return fastchg_di->fast_switch_to_normal;

	return false;
}

static bool bq27541_get_fast_chg_ing(void)
{
	if (fastchg_di)
		return fastchg_di->fast_chg_ing;

	return false;
}


static int bq27541_set_switch_to_noraml_false(void)
{
	if (fastchg_di)
		fastchg_di->fast_switch_to_normal = false;

	return 0;
}

static bool get_fastchg_firmware_already_updated(void)
{
	if (fastchg_di)
		return fastchg_di->firmware_already_updated;

	return false;
}

static bool fastchg_is_usb_switch_on(void)
{
	if (fastchg_di) {
		if (gpio_is_valid(fastchg_di->usb_on_gpio))
			return gpio_get_value(fastchg_di->usb_on_gpio);
		else
			return gpio_get_value(fastchg_di->usb_sw_1_gpio);
	}

	return false;
}

static bool enhance_dash_on(void)
{
	if (fastchg_di)
		return fastchg_di->dash_enhance;

	return false;
}

void enhance_dash_type_set(int type)
{
	if (fastchg_di) {
		if (type >= 0 && type <= 6)
		fastchg_di->dash_enhance = type;
		pr_info("set dash enhance %d.", type);
	}
}

int dash_get_adapter_update_status(void)
{
	if (!fastchg_di)
		return ADAPTER_FW_UPDATE_NONE;
	else
		return fastchg_di->adapter_update_report;
}
static struct external_battery_gauge fastcharge_information  = {
	.fast_chg_status_is_ok =
		get_fastchg_status,
	.fast_chg_started =
		bq27541_fast_chg_started,
	.get_fast_low_temp_full =
		bq27541_get_fast_low_temp_full,
	.fast_switch_to_normal =
		bq27541_fast_switch_to_normal,
	.get_fast_chg_ing =
		bq27541_get_fast_chg_ing,
	.set_fast_chg_allow =
		bq27541_set_fast_chg_allow,
	.get_fast_chg_allow =
		bq27541_get_fast_chg_allow,
	.set_switch_to_noraml_false =
		bq27541_set_switch_to_noraml_false,
	.get_fastchg_firmware_already_updated =
		get_fastchg_firmware_already_updated,
	.is_usb_switch_on = fastchg_is_usb_switch_on,
	.get_adapter_update = dash_get_adapter_update_status,
	.is_enhance_dash = enhance_dash_on,
	.clean = clean_status,
	.fast_normal_to_warm = dash_get_fastchg_warm,
};

static struct notify_dash_event *notify_event;

void notify_dash_unplug_register(struct notify_dash_event *event)
{
	if (notify_event) {
		notify_event = event;
		pr_err("multiple battery gauge called\n");
	} else {
		notify_event = event;
	}
}
EXPORT_SYMBOL(notify_dash_unplug_register);

void notify_dash_unplug_unregister(struct notify_dash_event *notify_event)
{
	notify_event = NULL;
}
EXPORT_SYMBOL(notify_dash_unplug_unregister);

static void mcu_init(struct fastchg_device_info *di)
{
	gpio_direction_output(di->ap_clk, 0);
	if (di->is_swarp_supported) {
		(void)opchg_mcu_action(ACTION_MODE_RESET_ACTIVE);
	} else {
		usleep_range(1000, 1001);
		gpio_direction_output(di->mcu_en_gpio, 1);
		usleep_range(1000, 1001);
		gpio_direction_output(di->mcu_en_gpio, 0);
	}
}

static irqreturn_t irq_rx_handler(int irq, void *dev_id)
{
	struct fastchg_device_info *di = dev_id;

	pr_debug("triggered\n");
	schedule_work(&di->fastcg_work);
	return IRQ_HANDLED;
}

static void oneplus_notify_dash_charger_present(bool status)
{
	if (notify_event && notify_event->notify_dash_charger_present)
		notify_event->notify_dash_charger_present(status);
}

#ifdef OP_SWARP_SUPPORTED
static void oneplus_notify_dash_charger_type(enum fast_charger_type type)
{
	if (notify_event && notify_event->update_dash_type)
		notify_event->update_dash_type(type);
}
#endif

static void oneplus_notify_adapter_sid(unsigned int sid)
{
	if (notify_event && notify_event->update_adapter_sid)
		(void)notify_event->update_adapter_sid(sid);
}

static void oneplus_notify_pmic_check_charger_present(void)
{
	if (notify_event && notify_event->notify_event)
		notify_event->notify_event();
}

static void notify_check_usb_suspend(bool status, bool check_power_ok)
{
	if (notify_event && notify_event->op_contrl
		&& !fastchg_di->is_swarp_supported)
		notify_event->op_contrl(status, check_power_ok);
}

static void oneplus_notify_suspend_normalchg(bool en)
{
	if (notify_event && notify_event->suspend_disable_nor_charge)
		notify_event->suspend_disable_nor_charge(en);
}

static void update_charger_present_status(struct work_struct *work)
{
	/* switch off fast chg */
	switch_mode_to_normal();
	notify_check_usb_suspend(true, true);
	oneplus_notify_dash_charger_present(false);
	oneplus_notify_pmic_check_charger_present();
}

static int op_get_device_type(void)
{
	if (bq27541_data && bq27541_data->get_device_type)
		return bq27541_data->get_device_type();
	else
		return 0;
}

static int onplus_get_battery_mvolts(void)
{
	if (bq27541_data && bq27541_data->get_battery_mvolts)
		return bq27541_data->get_battery_mvolts();
	else
		return 4010 * 1000; /* retrun 4.01v for default */
}

static int onplus_get_battery_temperature(void)
{
	if (bq27541_data && bq27541_data->get_battery_temperature)
		return bq27541_data->get_battery_temperature();
	else
		return 255; /* retrun 25.5 for default temp */
}

static int onplus_get_batt_remaining_capacity(void)
{
	if (bq27541_data && bq27541_data->get_batt_remaining_capacity)
		return bq27541_data->get_batt_remaining_capacity();
	else
		return 5; /* retrun 5 for default remaining_capacity */
}

static int onplus_get_battery_soc(void)
{
	if (bq27541_data && bq27541_data->get_battery_soc)
		return bq27541_data->get_battery_soc();
	else
		return 50; /* retrun 50 for default soc */
}

static int onplus_get_average_current(void)
{
	if (bq27541_data && bq27541_data->get_average_current)
		return bq27541_data->get_average_current();
	else
		return 666 * 1000; /* retrun 666ma for default current */
}

void op_check_charger_collapse_rerun_aicl(void);

void switch_mode_to_normal(void)
{
	if (fastchg_di->is_swarp_supported) {
		(void)opchg_mcu_action(ACTION_MODE_SWITCH_NORMAL);
	} else {
		usb_sw_gpio_set(0);
		mcu_en_gpio_set(1);
		op_check_charger_collapse_rerun_aicl();
	}
	//update_disconnect_pd_status(false);
}

static void request_mcu_irq(struct fastchg_device_info *di)
{
	int retval;

	opchg_set_mcu_data_read(di);
	gpio_set_value(di->ap_clk, 0);
	usleep_range(10000, 10001);
	gpio_set_value(di->ap_clk, 1);
	if (di->adapter_update_real
		!= ADAPTER_FW_NEED_UPDATE) {
		pr_info("%s\n", __func__);
	if (!di->irq_enabled) {
		retval = request_irq(di->irq, irq_rx_handler,
				IRQF_TRIGGER_RISING, "mcu_data", di);
		if (retval < 0)
			pr_err("request ap rx irq failed.\n");
		else
			di->irq_enabled = true;
		irq_set_status_flags(di->irq, IRQ_DISABLE_UNLAZY);
		}
	} else {
			di->irq_enabled = true;
	}
}

static void fastcg_work_func(struct work_struct *work)
{
	struct fastchg_device_info *di = container_of(work,
			struct fastchg_device_info,
			fastcg_work);
	pr_info("\n");
	if (di->irq_enabled) {
		free_irq(di->irq, di);
		msleep(25);
		di->irq_enabled = false;
		wake_up(&di->read_wq);
	}
}

static void update_fireware_version_func(struct work_struct *work)
{
	struct fastchg_device_info *di = container_of(work,
			struct fastchg_device_info,
			update_fireware_version_work.work);

	if (!dashchg_firmware_data || di->dashchg_fw_ver_count == 0)
		return;

	snprintf(di->fw_id, 255, "0x%x",
	dashchg_firmware_data[di->dashchg_fw_ver_count - 4]);
	if (di->is_swarp_supported)
		snprintf(di->manu_name, 255, "%s", mcu_id_text[di->asic_hw_id]);
	else
		snprintf(di->manu_name, 255, "%s", "ONEPLUS");
	push_component_info(FAST_CHARGE, di->fw_id, di->manu_name);
}
void di_watchdog(struct timer_list *t)
{
	struct fastchg_device_info *di = fastchg_di;

	pr_err("di_watchdog can't receive mcu data\n");
	bq27541_data->set_allow_reading(true);
	di->fast_chg_started = false;
	di->fast_switch_to_normal = false;
	di->fast_low_temp_full = false;
	di->fast_chg_allow = false;
	di->fast_normal_to_warm = false;
	di->fast_chg_ing = false;
	di->fast_chg_error = false;
	schedule_work(&di->charger_present_status_work);
	pr_err("switch off fastchg\n");

	__pm_relax(di->fastchg_wake_lock);
}

#define MAX_BUFFER_SIZE 1024
#define ALLOW_DATA 0x2
                            /* warp */ /* swarp */
#define CURRENT_LIMIT_1 0x1 /* 3.6A */ /* 2.5A */
#define CURRENT_LIMIT_2 0x2 /* 2.5A */ /* 2.0A */
#define CURRENT_LIMIT_3 0x3 /* 3.0A */ /* 3.0A */
#define CURRENT_LIMIT_4 0x4 /* 4.0A */ /* 4.0A */
#define CURRENT_LIMIT_5 0x5 /* 5.0A */ /* 5.0A */
#define CURRENT_LIMIT_6 0x6 /* 6.0A */ /* 6.5A */
#define REJECT_DATA 0x11

/* Legacy write function */
/*
static void dash_write(struct fastchg_device_info *di, int data)
{
	int i;
	int device_type = op_get_device_type();

	usleep_range(2000, 2001);
	gpio_direction_output(di->ap_data, 0);
	if (di->pinctrl &&
		!IS_ERR_OR_NULL(di->pinctrl_mcu_data_state_suspended))
		pinctrl_select_state(di->pinctrl,
			di->pinctrl_mcu_data_state_suspended);
	for (i = 0; i < 3; i++) {
		if (i == 0)
			gpio_set_value(di->ap_data, data >> 1);
		else if (i == 1)
			gpio_set_value(di->ap_data, data & 0x1);
		else
			gpio_set_value(di->ap_data, device_type);
		gpio_set_value(di->ap_clk, 0);
		usleep_range(1000, 1001);
		gpio_set_value(di->ap_clk, 1);
		usleep_range(19000, 19001);
	}
}
*/

static void dash_write_4bits(struct fastchg_device_info *di, int data)
{
	int i = 0;
	int device_type = op_get_device_type();

	usleep_range(2000, 2001);
	opchg_set_mcu_data_write(di);
	for (i = 0; i < 4; i++) {
		if (i == 0) {
			gpio_set_value(di->ap_data, (data & BIT(2)) >> 2);
		} else if (i == 1) {
			gpio_set_value(di->ap_data, (data & BIT(1)) >> 1);
		} else if (i == 2) {
			gpio_set_value(di->ap_data, data & BIT(0));
		} else {
			gpio_set_value(di->ap_data, device_type);
		}
		gpio_set_value(di->ap_clk, 0);
		usleep_range(1000, 1001);
		gpio_set_value(di->ap_clk, 1);
		usleep_range(19000, 19001);
	}
}

static int dash_read(struct fastchg_device_info *di)
{
	int i;
	int bit = 0;
	int data = 0;

	for (i = 0; i < 7; i++) {
		gpio_set_value(di->ap_clk, 0);
		usleep_range(1000, 1001);
		gpio_set_value(di->ap_clk, 1);
		usleep_range(19000, 19001);
		bit = gpio_get_value(di->ap_data);
		data |= bit<<(6-i);
	}
	pr_err("recv data:0x%x\n", data);
	return data;
}

static int dash_dev_open(struct inode *inode, struct file *filp)
{
	struct fastchg_device_info *dash_dev = container_of(filp->private_data,
			struct fastchg_device_info, dash_device);

	filp->private_data = dash_dev;
	pr_debug("%d,%d\n", imajor(inode), iminor(inode));
	return 0;
}

static ssize_t dash_dev_read(struct file *filp, char __user *buf,
		size_t count, loff_t *offset)
{
	struct fastchg_device_info *di = filp->private_data;

	int data = 0;
	int ret = 0;

	mutex_lock(&di->read_mutex);
	while (1) {
		ret = wait_event_interruptible(di->read_wq,
				(!di->irq_enabled));
		if (ret)
			goto fail;
		if (di->irq_enabled)
			pr_err("dash false wakeup,ret=%d\n", ret);
		data = dash_read(di);
		mutex_unlock(&di->read_mutex);
		if (copy_to_user(buf, &data, 1)) {
			pr_err("failed to copy to user space\n");
			return -EFAULT;
		}
		break;
	}
	return ret;
fail:
	mutex_unlock(&di->read_mutex);
	return ret;
}
static struct op_adapter_chip *g_adapter_chip;

static void adapter_update_work_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct fastchg_device_info *chip =
		container_of(dwork,
		struct fastchg_device_info, adapter_update_work);
	bool update_result = false;
	int i = 0;

	if (!g_adapter_chip) {
		pr_info("%s g_adapter_chip NULL\n", __func__);
		return;
	}
	pr_info("%s begin\n", __func__);
	opchg_set_mcu_data_read(chip);
	/*pm_qos_update_request(&big_cpu_update_freq, MAX_CPUFREQ);*/
	op_bus_vote(false);
	msleep(1000);
	for (i = 0; i < 3; i++) {
		update_result =
			g_adapter_chip->vops->adapter_update(g_adapter_chip,
			chip->ap_clk, chip->ap_data);
		if (update_result == true)
			break;
		if (i < 1)
			msleep(1650);
	}
	msleep(5000);
	if (update_result) {
		chip->adapter_update_real = ADAPTER_FW_UPDATE_SUCCESS;
	} else {
		chip->adapter_update_real = ADAPTER_FW_UPDATE_FAIL;
		chip->adapter_update_report = chip->adapter_update_real;
	}
	msleep(20);
	if (chip->is_swarp_supported)
		(void)opchg_mcu_action(ACTION_MODE_ENABLE);
	else
		mcu_en_gpio_set(1);
	chip->fast_chg_started = false;
	chip->fast_chg_allow = false;
	chip->fast_chg_ing = false;
	msleep(1000);
	if (update_result) {
		msleep(2000);
		chip->adapter_update_report = ADAPTER_FW_UPDATE_SUCCESS;
	}
	notify_check_usb_suspend(true, false);
	oneplus_notify_pmic_check_charger_present();
	oneplus_notify_dash_charger_present(false);
	reset_mcu_and_request_irq(chip);

	pr_info("%s end update_result:%d\n",
		__func__, update_result);
	__pm_relax(chip->fastchg_wake_lock);
	op_bus_vote(true);

}

#ifdef SUPPORT_ADAPTER_FW_UPDATE
static void dash_adapter_update(struct fastchg_device_info *chip)
{
	pr_err("%s\n", __func__);
	/*schedule_delayed_work_on(5,*/
	/*&chip->adapter_update_work,*/
	/*round_jiffies_relative(*/
	/*msecs_to_jiffies(ADAPTER_UPDATE_DELAY)));*/
	schedule_delayed_work(&chip->adapter_update_work,
			msecs_to_jiffies(ADAPTER_UPDATE_DELAY));
}
#endif

void op_adapter_init(struct op_adapter_chip *chip)
{
	g_adapter_chip = chip;
}

#define DASH_IOC_MAGIC					0xff
#define DASH_NOTIFY_FIRMWARE_UPDATE		_IO(DASH_IOC_MAGIC, 1)
#define DASH_NOTIFY_FAST_PRESENT		_IOW(DASH_IOC_MAGIC, 2, int)
#define DASH_NOTIFY_FAST_ABSENT			_IOW(DASH_IOC_MAGIC, 3, int)
#define DASH_NOTIFY_NORMAL_TEMP_FULL	_IOW(DASH_IOC_MAGIC, 4, int)
#define DASH_NOTIFY_LOW_TEMP_FULL		_IOW(DASH_IOC_MAGIC, 5, int)
#define DASH_NOTIFY_BAD_CONNECTED		_IOW(DASH_IOC_MAGIC, 6, int)
#define DASH_NOTIFY_TEMP_OVER			_IOW(DASH_IOC_MAGIC, 7, int)
#define DASH_NOTIFY_ADAPTER_FW_UPDATE	_IOW(DASH_IOC_MAGIC, 8, int)
#define DASH_NOTIFY_BTB_TEMP_OVER		_IOW(DASH_IOC_MAGIC, 9, int)
#define DASH_NOTIFY_ALLOW_READING_IIC	_IOW(DASH_IOC_MAGIC, 10, int)
#define DASH_NOTIFY_UNDEFINED_CMD		_IO(DASH_IOC_MAGIC, 11)
#define DASH_NOTIFY_INVALID_DATA_CMD	_IO(DASH_IOC_MAGIC, 12)
#define DASH_NOTIFY_REQUEST_IRQ			_IO(DASH_IOC_MAGIC, 13)
#define DASH_NOTIFY_UPDATE_DASH_PRESENT	_IOW(DASH_IOC_MAGIC, 14, int)
#define DASH_NOTIFY_UPDATE_ADAPTER_INFO	_IOW(DASH_IOC_MAGIC, 15, int)
#ifdef OP_SWARP_SUPPORTED
#define DASH_NOTIFY_ADAPTER_NOT_MATCH   _IOW(DASH_IOC_MAGIC, 16, int)
#endif
#define DASH_NOTIFY_UPDATE_ADAPTER_SID _IOW(DASH_IOC_MAGIC, 17, int)

static long  dash_dev_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	struct fastchg_device_info *di = filp->private_data;
	int volt = 0;
	int temp = 0;
	int soc = 0;
	int current_now = 0;
	int remain_cap = 0;
	int need_send_msg = 0;
	bool is_skin_temp_high = false;
	bool is_skin_temp_warm = false;
	bool is_skin_thermal_medium = false;
	bool is_call_on = false;
	bool is_video_call_on = false;
	bool is_lcd_on = false;
	static int lcd_on_msg;

		switch (cmd) {
		case DASH_NOTIFY_FIRMWARE_UPDATE:
			schedule_delayed_work(&di->update_firmware,
					msecs_to_jiffies(2200));
			break;
		case DASH_NOTIFY_FAST_PRESENT:
			oneplus_notify_dash_charger_present(true);
			if (arg == DASH_NOTIFY_FAST_PRESENT + 1) {
				__pm_stay_awake(di->fastchg_wake_lock);
				bq27541_data->set_allow_reading(false);
				di->fast_chg_allow = false;
				di->fast_normal_to_warm = false;
				di->warp_normal_path_need_config = false;
				mod_timer(&di->watchdog,
				jiffies + msecs_to_jiffies(15000));
			} else if (arg == DASH_NOTIFY_FAST_PRESENT + 2) {
				pr_err("REJECT_DATA\n");
				dash_write_4bits(di, REJECT_DATA);
				di->warp_normal_path_need_config = false;
			} else if (arg == DASH_NOTIFY_FAST_PRESENT + 3) {
				notify_check_usb_suspend(false, false);
				di->fast_chg_error = false;
				dash_write_4bits(di, ALLOW_DATA);
				di->fast_chg_started = true;
			}
			break;
		case DASH_NOTIFY_FAST_ABSENT:
			if (arg == DASH_NOTIFY_FAST_ABSENT + 1) {
				bq27541_data->set_allow_reading(true);
				di->fast_chg_started = false;
				di->fast_chg_allow = false;
				di->fast_switch_to_normal = false;
				di->fast_normal_to_warm = false;
				di->fast_chg_ing = false;
				di->dash_enhance = 0;
				di->sid = 0;
#ifdef OP_SWARP_SUPPORTED
				oneplus_notify_dash_charger_type(di->dash_enhance);
#endif
				oneplus_notify_adapter_sid(di->sid);
				pr_err("fastchg stop unexpectly, switch off fastchg\n");
				switch_mode_to_normal();
				update_fast_switch_off_status();
				del_timer(&di->watchdog);
				dash_write_4bits(di, REJECT_DATA);
				if (di->is_swarp_supported)
					(void)opchg_mcu_action(ACTION_MODE_RESET_SLEEP);
			} else if (arg == DASH_NOTIFY_FAST_ABSENT + 2) {
				notify_check_usb_suspend(true, true);
				oneplus_notify_dash_charger_present(false);
				oneplus_notify_pmic_check_charger_present();
				__pm_relax(di->fastchg_wake_lock);
			}
			break;
		case DASH_NOTIFY_ALLOW_READING_IIC:
			if (arg == DASH_NOTIFY_ALLOW_READING_IIC + 1) {
				bq27541_data->set_allow_reading(true);
				di->fast_chg_started = true;
				di->fast_chg_ing = true;
				volt = onplus_get_battery_mvolts();
				temp = onplus_get_battery_temperature();
				remain_cap =
				onplus_get_batt_remaining_capacity();
				soc = onplus_get_battery_soc();
				current_now = onplus_get_average_current();
				pr_err("volt:%d,temp:%d,remain_cap:%d,soc:%d,current:%d\n",
				volt, temp, remain_cap, soc, current_now);
				if (!di->batt_psy)
					di->batt_psy =
					power_supply_get_by_name("battery");
				if (di->batt_psy)
					power_supply_changed(di->batt_psy);
				mod_timer(&di->watchdog,
				jiffies + msecs_to_jiffies(15000));
				if (di->is_swarp_supported) {
					oneplus_notify_suspend_normalchg(true);
				}
				bq27541_data->set_allow_reading(false);

				is_skin_thermal_medium = check_skin_thermal_medium();
				is_skin_temp_warm = check_skin_thermal_warm();
				is_skin_temp_high = check_skin_thermal_high();
				is_call_on = check_call_on_status();
				is_video_call_on = check_video_call_on_status();
				is_lcd_on = check_lcd_on_status();

				if (is_call_on || is_video_call_on)
					lcd_on_msg = CURRENT_LIMIT_2;
				else {
					if (is_skin_temp_high)
						lcd_on_msg = is_lcd_on ? di->skin_hi_curr_max : di->skin_hi_lcdoff_curr_max;
					else if (is_skin_temp_warm)
						lcd_on_msg = is_lcd_on ? di->skin_wrm_curr_max : CURRENT_LIMIT_6;
					else if (is_skin_thermal_medium)
						lcd_on_msg = is_lcd_on ? di->skin_med_curr_max : CURRENT_LIMIT_6;
					else
						lcd_on_msg = CURRENT_LIMIT_6;
				}
				if (!di->is_swarp_supported)
					dash_write_4bits(di, lcd_on_msg);
#ifdef OP_SWARP_SUPPORTED
			} else if (di->is_swarp_supported) {
				need_send_msg = arg & 0xff;
				if (need_send_msg < CURRENT_LIMIT_1 || need_send_msg > CURRENT_LIMIT_6) {
					pr_err("dashd give an invalid msg(0x%02x).", need_send_msg);
					need_send_msg = CURRENT_LIMIT_6;
				}
				need_send_msg = need_send_msg <= lcd_on_msg ? need_send_msg : lcd_on_msg;
				pr_info("send msg(0x%02x) to asic.", need_send_msg);
				dash_write_4bits(di, need_send_msg);
				lcd_on_msg = CURRENT_LIMIT_6; // reset lcd_on_msg
#endif
			}
			break;
		case DASH_NOTIFY_BTB_TEMP_OVER:
			if (di->fast_chg_ing)
				mod_timer(&di->watchdog,
					jiffies + msecs_to_jiffies(15000));
			dash_write_4bits(di, ALLOW_DATA);
			break;
		case DASH_NOTIFY_UPDATE_ADAPTER_INFO:
			if (is_usb_pluged()) {
				di->dash_enhance = arg;
#ifdef OP_SWARP_SUPPORTED
				oneplus_notify_dash_charger_type(di->dash_enhance);
#endif
			}
			if (!di->batt_psy)
				di->batt_psy =
					power_supply_get_by_name("battery");
			if (di->batt_psy)
				power_supply_changed(di->batt_psy);
			break;
		case DASH_NOTIFY_UPDATE_ADAPTER_SID:
			if (is_usb_pluged())
				di->sid = arg;
			else
				di->sid = 0;
			oneplus_notify_adapter_sid(di->sid);
			if (!di->batt_psy)
				di->batt_psy =
					power_supply_get_by_name("battery");
			if (di->batt_psy)
				power_supply_changed(di->batt_psy);
			break;
		case DASH_NOTIFY_BAD_CONNECTED:
		case DASH_NOTIFY_NORMAL_TEMP_FULL:
			if (arg == DASH_NOTIFY_NORMAL_TEMP_FULL + 1) {
				pr_err("fastchg full, switch off fastchg, set usb_sw_gpio 0\n");
				di->fast_switch_to_normal = true;
				switch_mode_to_normal();
				if (di->is_swarp_supported)
					(void)opchg_mcu_action(ACTION_MODE_RESET_SLEEP);
				del_timer(&di->watchdog);
			} else if (arg == DASH_NOTIFY_NORMAL_TEMP_FULL + 2) {
				bq27541_data->set_allow_reading(true);
				di->fast_chg_started = false;
				di->fast_chg_allow = false;
				di->fast_chg_ing = false;
				di->fast_chg_error = false;
				notify_check_usb_suspend(true, false);
				oneplus_notify_pmic_check_charger_present();
				__pm_relax(di->fastchg_wake_lock);
			} else if (arg == DASH_NOTIFY_NORMAL_TEMP_FULL + 3) {
				op_switch_normal_set();
			}
			break;
		case DASH_NOTIFY_TEMP_OVER:
			if (arg == DASH_NOTIFY_TEMP_OVER + 1) {
				pr_err("fastchg temp over\n");
				switch_mode_to_normal();
				if (di->is_swarp_supported)
					(void)opchg_mcu_action(ACTION_MODE_RESET_SLEEP);
				del_timer(&di->watchdog);
			} else if (arg == DASH_NOTIFY_TEMP_OVER + 2) {
				di->fast_normal_to_warm = true;
				bq27541_data->set_allow_reading(true);
				di->fast_chg_started = false;
				di->fast_chg_allow = false;
				di->fast_chg_ing = false;
				di->fast_chg_error = true;
				notify_check_usb_suspend(true, false);
				oneplus_notify_pmic_check_charger_present();
				oneplus_notify_dash_charger_present(false);
				if (di->is_swarp_supported)
					op_pd_config_switch_normal();
				__pm_relax(di->fastchg_wake_lock);
			}
			break;
		case DASH_NOTIFY_ADAPTER_FW_UPDATE:
			if (arg == DASH_NOTIFY_ADAPTER_FW_UPDATE + 1) {
#ifdef SUPPORT_ADAPTER_FW_UPDATE
				di->adapter_update_real
					= ADAPTER_FW_NEED_UPDATE;
				di->adapter_update_report
					= di->adapter_update_real;
#else
				di->adapter_update_real
					= ADAPTER_FW_UPDATE_NONE;
				di->adapter_update_report
					= di->adapter_update_real;
#endif
			} else if (arg == DASH_NOTIFY_ADAPTER_FW_UPDATE + 2) {
				bq27541_data->set_allow_reading(true);
				di->fast_chg_started = false;
#ifdef SUPPORT_ADAPTER_FW_UPDATE
				oneplus_notify_dash_charger_present(true);
				dash_write_4bits(di, ALLOW_DATA);
				__pm_stay_awake(di->fastchg_wake_lock);
				dash_adapter_update(di);
#else
				dash_write_4bits(di, 0x01);
				mod_timer(&di->watchdog,
					jiffies + msecs_to_jiffies(25000));
#endif
			}
			break;
		case DASH_NOTIFY_UNDEFINED_CMD:
			if (di->fast_chg_started) {
				pr_err("UNDEFINED_CMD, switch off fastchg\n");
				switch_mode_to_normal();
				if (di->is_swarp_supported)
					(void)opchg_mcu_action(ACTION_MODE_RESET_SLEEP);
				msleep(500); /* avoid i2c conflict */
				/* data err */
				bq27541_data->set_allow_reading(true);
				di->fast_chg_started = false;
			__pm_relax(di->fastchg_wake_lock);
				di->fast_chg_allow = false;
				di->fast_switch_to_normal = false;
				di->fast_normal_to_warm = false;
				di->fast_chg_ing = false;
				di->fast_chg_error = true;
				notify_check_usb_suspend(true, false);
			}
			break;
		case DASH_NOTIFY_INVALID_DATA_CMD:
			if (di->fast_chg_started) {
				bq27541_data->set_allow_reading(true);
				di->fast_chg_started = false;
				di->fast_chg_allow = false;
				di->fast_switch_to_normal = false;
				di->fast_normal_to_warm = false;
				di->fast_chg_ing = false;
				di->fast_chg_error = true;
				pr_err("DASH_NOTIFY_INVALID_DATA_CMD, switch off fastchg\n");
				switch_mode_to_normal();
				if (di->is_swarp_supported)
					(void)opchg_mcu_action(ACTION_MODE_RESET_SLEEP);
				del_timer(&di->watchdog);
			__pm_relax(di->fastchg_wake_lock);
				notify_check_usb_suspend(true, true);
				oneplus_notify_pmic_check_charger_present();
			}
			break;
		case DASH_NOTIFY_REQUEST_IRQ:
			request_mcu_irq(di);
			break;
		case DASH_NOTIFY_UPDATE_DASH_PRESENT:
			if (arg == DASH_NOTIFY_UPDATE_DASH_PRESENT+1)
				update_fast_chg_started();
			break;
#ifdef OP_SWARP_SUPPORTED
		case DASH_NOTIFY_ADAPTER_NOT_MATCH:
			if (arg == DASH_NOTIFY_ADAPTER_NOT_MATCH + 1) {
				// not allow switch to dash again.
				di->fast_chg_allow = false;
				di->fast_switch_to_normal = true;
				bq27541_data->set_allow_reading(true);
				di->warp_normal_path_need_config = true;
				di->fast_chg_started = false;
				update_fast_chg_started();
			} else if (arg == DASH_NOTIFY_ADAPTER_NOT_MATCH + 2) {
				volt = onplus_get_battery_mvolts();
				temp = onplus_get_battery_temperature();
				remain_cap =
				onplus_get_batt_remaining_capacity();
				soc = onplus_get_battery_soc();
				current_now = onplus_get_average_current();
				pr_err("volt:%d,temp:%d,remain_cap:%d,soc:%d,current:%d\n",
				volt, temp, remain_cap, soc, current_now);
				if (!di->batt_psy)
					di->batt_psy =
					power_supply_get_by_name("battery");
				if (di->batt_psy)
					power_supply_changed(di->batt_psy);
				mod_timer(&di->watchdog,
					jiffies + msecs_to_jiffies(15000));
				dash_write_4bits(di, ALLOW_DATA);
				if (di->warp_normal_path_need_config) {
					op_warp_config_for_swarp();
					di->warp_normal_path_need_config = false;
				}
			} else if (arg == DASH_NOTIFY_ADAPTER_NOT_MATCH + 3) {
				if (is_usb_pluged())
					fastchg_di->fast_chg_started = true;
				di->warp_normal_path_need_config = false;
				dash_write_4bits(di, 0x01);
				switch_mode_to_normal();
				del_timer(&di->watchdog);
			} else if (arg == DASH_NOTIFY_ADAPTER_NOT_MATCH + 4) {
				fastchg_di->fast_chg_started = false;
				__pm_relax(di->fastchg_wake_lock);
			}
			break;
#endif
		default:
			pr_err("bad ioctl %u\n", cmd);
	}
	return 0;
}

static ssize_t dash_dev_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offset)
{
	struct fastchg_device_info *di = filp->private_data;

	/*malloc for firmware, do not free*/
	if (di->firmware_already_updated)
		return 0;

	dashchg_firmware_data = kmalloc(count, GFP_ATOMIC);
	di->dashchg_fw_ver_count = count;
	if (copy_from_user(dashchg_firmware_data, buf, count)) {
		pr_err("failed to copy from user space\n");
		kfree(dashchg_firmware_data);
		return -EFAULT;
	}
	schedule_delayed_work(&di->update_fireware_version_work,
			msecs_to_jiffies(SHOW_FW_VERSION_DELAY_MS));
	pr_info("fw_ver_count=%d\n", di->dashchg_fw_ver_count);
	return count;
}

static const struct file_operations dash_dev_fops = {
	.owner			= THIS_MODULE,
	.llseek			= no_llseek,
	.write			= dash_dev_write,
	.read			= dash_dev_read,
	.open			= dash_dev_open,
	.unlocked_ioctl	= dash_dev_ioctl,
};

static int dash_parse_dt(struct fastchg_device_info *di)
{
	u32 flags;
	int rc;
	struct device_node *dev_node = di->client->dev.of_node;

	if (!dev_node) {
		pr_err("device tree info. missing\n");
		return -EINVAL;
	}

	di->usb_sw_1_gpio = of_get_named_gpio_flags(dev_node,
			"microchip,usb-sw-1-gpio", 0, &flags);
	di->usb_sw_2_gpio = of_get_named_gpio_flags(dev_node,
			"microchip,usb-sw-2-gpio", 0, &flags);
	di->usb_on_gpio = of_get_named_gpio_flags(dev_node,
			"microchip,usb-on", 0, &flags);
	di->usb_on_gpio_1 = of_get_named_gpio_flags(dev_node,
			"microchip,usb-on_46", 0, &flags);
	di->ap_clk = of_get_named_gpio_flags(dev_node,
			"microchip,ap-clk", 0, &flags);
	di->ap_data = of_get_named_gpio_flags(dev_node,
			"microchip,ap-data", 0, &flags);
	di->mcu_en_gpio = of_get_named_gpio_flags(dev_node,
			"microchip,mcu-en-gpio", 0, &flags);
	di->n76e_present = of_property_read_bool(dev_node,
			"op,n76e_support");
	di->is_mcl_verion = of_property_read_bool(dev_node,
		"op,mcl_verion");
#ifdef OP_SWARP_SUPPORTED
	di->is_swarp_supported = of_property_read_bool(dev_node,
		"op,swarp_supported");
#endif
	di->is_4300mAh_4p45_support = of_property_read_bool(dev_node,
		"op,4300mAh_4p45_support");
	di->is_4320mAh_4p45_support = of_property_read_bool(dev_node,
		"op,4320mAh_4p45_support");
	di->is_4510mAh_4p45_support = of_property_read_bool(dev_node,
		"op,4510mAh_4p45_support");
	rc = of_property_read_u32(dev_node,
			"op,fw-erase-count", &di->erase_count);
	if (rc < 0)
		di->erase_count = 384;
	rc = of_property_read_u32(dev_node,
			"op,fw-addr-low", &di->addr_low);
	if (rc < 0)
		di->addr_low = 0x88;
	rc = of_property_read_u32(dev_node,
			"op,fw-addr-high", &di->addr_high);
	if (rc < 0)
		di->addr_high = 0;
	return 0;
}

static int request_dash_gpios(struct fastchg_device_info *di)
{
	int ret;

	if (gpio_is_valid(di->usb_sw_1_gpio)
		&& gpio_is_valid(di->usb_sw_2_gpio)) {
		ret = gpio_request(di->usb_sw_1_gpio, "usb_sw_1_gpio");
		if (ret) {
			pr_err("gpio_request failed for %d ret=%d\n",
			di->usb_sw_1_gpio, ret);
			return -EINVAL;
		}
		gpio_direction_output(di->usb_sw_1_gpio, 0);

		ret = gpio_request(di->usb_sw_2_gpio, "usb_sw_2_gpio");
		if (ret) {
			pr_err("gpio_request failed for %d ret=%d\n",
			di->usb_sw_2_gpio, ret);
			return -EINVAL;
		}
		gpio_direction_output(di->usb_sw_2_gpio, 0);
	} else
		return -EINVAL;

	if (gpio_is_valid(di->usb_on_gpio)) {
		ret = gpio_request(di->usb_on_gpio, "usb_on_gpio");
		if (ret)
			pr_err("gpio_request failed for %d ret=%d\n",
				di->usb_on_gpio, ret);
		gpio_direction_output(di->usb_on_gpio, 0);
	}

	di->dpdm_mode = DPDM_MODE_NORMAL;

	if (gpio_is_valid(di->usb_on_gpio_1)) {
		ret = gpio_request(di->usb_on_gpio_1, "usb_on_gpio");
		if (ret)
			pr_err("gpio_request failed for %d ret=%d\n",
				di->usb_on_gpio_1, ret);
		gpio_direction_output(di->usb_on_gpio_1, 0);
	}

	if (gpio_is_valid(di->ap_clk)) {
		ret = gpio_request(di->ap_clk, "ap_clk");
		if (ret)
			pr_err("gpio_request failed for %d ret=%d\n",
			di->ap_clk, ret);
	}

	if (gpio_is_valid(di->mcu_en_gpio)) {
		ret = gpio_request(di->mcu_en_gpio, "mcu_en_gpio");
		if (ret)
			pr_err("gpio_request failed for %d ret=%d\n",
			di->mcu_en_gpio, ret);
		else {
			gpio_direction_output(di->mcu_en_gpio, 0);
		}
	}

	if (gpio_is_valid(di->ap_data)) {
		ret = gpio_request(di->ap_data, "mcu_data");
		if (ret)
			pr_err("gpio_request failed for %d ret=%d\n",
			di->ap_data, ret);
	}

	return 0;
}

static int dash_pinctrl_init(struct fastchg_device_info *di)
{
	di->pinctrl = devm_pinctrl_get(&di->client->dev);
	if (IS_ERR_OR_NULL(di->pinctrl)) {
		dev_err(&di->client->dev,
				"Unable to acquire pinctrl\n");
		di->pinctrl = NULL;
		return 0;
	} else {
		di->pinctrl_state_active =
			pinctrl_lookup_state(di->pinctrl, "mux_fastchg_active");
		if (IS_ERR_OR_NULL(di->pinctrl_state_active)) {
			dev_err(&di->client->dev,
					"Can not fastchg_active state\n");
			devm_pinctrl_put(di->pinctrl);
			di->pinctrl = NULL;
			return PTR_ERR(di->pinctrl_state_active);
		}
		di->pinctrl_state_suspended =
			pinctrl_lookup_state(di->pinctrl,
					"mux_fastchg_suspend");
		if (IS_ERR_OR_NULL(di->pinctrl_state_suspended)) {
			dev_err(&di->client->dev,
					"Can not fastchg_suspend state\n");
			devm_pinctrl_put(di->pinctrl);
			di->pinctrl = NULL;
			return PTR_ERR(di->pinctrl_state_suspended);
		}

		di->pinctrl_mcu_data_read =
			pinctrl_lookup_state(di->pinctrl,
					"mcu_data_read");
		if (IS_ERR_OR_NULL(di->pinctrl_mcu_data_read)) {
			dev_err(&di->client->dev,
					"Can not mcu_data_read state\n");
			devm_pinctrl_put(di->pinctrl);
			di->pinctrl = NULL;
			return PTR_ERR(di->pinctrl_mcu_data_read);
		}
		di->pinctrl_mcu_data_write =
					pinctrl_lookup_state(di->pinctrl,
							"mcu_data_write");
		if (IS_ERR_OR_NULL(di->pinctrl_mcu_data_write)) {
			dev_err(&di->client->dev,
					"Can not fastchg_suspend state\n");
			devm_pinctrl_put(di->pinctrl);
			di->pinctrl = NULL;
			return PTR_ERR(di->pinctrl_mcu_data_write);
		}
#ifdef OP_SWARP_SUPPORTED
		if (di->is_swarp_supported) {
			di->pinctrl_mcu_id_hiz =
				pinctrl_lookup_state(di->pinctrl, "mcu_id_hiz");
			if (IS_ERR_OR_NULL(di->pinctrl_mcu_id_hiz)) {
				dev_err(&di->client->dev,
						"Can not mcu_id_hiz\n");
				devm_pinctrl_put(di->pinctrl);
				di->pinctrl = NULL;
				return PTR_ERR(di->pinctrl_mcu_id_hiz);
			}
			di->pinctrl_mcu_id_pull_up =
				pinctrl_lookup_state(di->pinctrl, "mcu_id_pull_up");
			if (IS_ERR_OR_NULL(di->pinctrl_mcu_id_pull_up)) {
				dev_err(&di->client->dev,
						"Can not mcu_id_pull_up\n");
				devm_pinctrl_put(di->pinctrl);
				di->pinctrl = NULL;
				return PTR_ERR(di->pinctrl_mcu_id_pull_up);
			}
			di->pinctrl_mcu_id_pull_down =
				pinctrl_lookup_state(di->pinctrl, "mcu_id_pull_down");
			if (IS_ERR_OR_NULL(di->pinctrl_mcu_id_pull_down)) {
				dev_err(&di->client->dev,
						"Can not mcu_id_pull_down\n");
				devm_pinctrl_put(di->pinctrl);
				di->pinctrl = NULL;
				return PTR_ERR(di->pinctrl_mcu_id_pull_down);
			}
		}
	}
#endif
	if (pinctrl_select_state(di->pinctrl,
				di->pinctrl_state_active) < 0)
		pr_err("pinctrl set active fail\n");

	if (pinctrl_select_state(di->pinctrl,
				di->pinctrl_mcu_data_read) < 0)
		pr_err("pinctrl set pinctrl_mcu_data_read fail\n");
#ifdef OP_SWARP_SUPPORTED
	if (di->is_swarp_supported && pinctrl_select_state(di->pinctrl,
				di->pinctrl_mcu_id_hiz) < 0)
		pr_err("pinctrl set mcu_id_hiz fail\n");
#endif
	return 0;

}

static void check_n76e_support(struct fastchg_device_info *di)
{
	if (di->n76e_present) {
		init_n76e_exist_node();
		pr_info("n76e 4p45 exist\n");
	} else {
		pr_info("n76e 4p45 not exist\n");
	}

}

static void check_enhance_support(struct fastchg_device_info *di)
{
	if (di->is_mcl_verion) {
		init_warp_chg_exist_node();
		pr_info("warp dash exist\n");
	} else {
		pr_info("warp dash not exist\n");
	}

}

#ifdef OP_SWARP_SUPPORTED
static void check_swarp_support(struct fastchg_device_info *di)
{
	if (di->is_swarp_supported) {
		init_swarp_chg_exist_node();
		pr_info("swarp dash exist\n");
	} else {
		pr_info("swarp dash not exist\n");
	}

}
#ifdef GET_HWID_BY_GPIO
static int dash_get_asic_hw_id(struct fastchg_device_info *di)
{
	int hw_id_gpio = 0;
	int id_val[3] = { 0 };
	int rc = 0;
	struct device_node *dev_node = di->client->dev.of_node;

	hw_id_gpio = of_get_named_gpio(dev_node, "microchip,id-m0", 0);
	if (hw_id_gpio < 0) {
		pr_err("hw_id_m0 not specified\n");
		di->asic_hw_id = 1; //default rk826
		return -EINVAL;
	}
	if (gpio_is_valid(hw_id_gpio)) {
		rc = gpio_request(hw_id_gpio, "asic-id-gpio");
		if (rc) {
			pr_err("unable to request asic_hw_id gpio [%d], rc=%d\n", hw_id_gpio, rc);
			return rc;
		}
		if (di->pinctrl != NULL
			&& !IS_ERR_OR_NULL(di->pinctrl_mcu_id_hiz)
			&& !IS_ERR_OR_NULL(di->pinctrl_mcu_id_pull_up)
			&& !IS_ERR_OR_NULL(di->pinctrl_mcu_id_pull_down)){
			id_val[0] = gpio_get_value(hw_id_gpio);
			usleep_range(10000, 10001);
			if (pinctrl_select_state(di->pinctrl,
				di->pinctrl_mcu_id_pull_up) < 0)
				pr_err("pinctrl set mcu_id_pull_up fail\n");
			usleep_range(10000, 10001);
			id_val[1] = gpio_get_value(hw_id_gpio);
			usleep_range(10000, 10001);
			if (pinctrl_select_state(di->pinctrl,
				di->pinctrl_mcu_id_pull_down) < 0)
				pr_err("pinctrl set mcu_id_pull_down fail\n");
			usleep_range(10000, 10001);
			id_val[2] = gpio_get_value(hw_id_gpio);
			pr_err("id gpio val[0-2]=[%d,%d,%d]", id_val[0], id_val[1],id_val[2]);
			if (pinctrl_select_state(di->pinctrl,
				di->pinctrl_mcu_id_hiz) < 0)
				pr_err("pinctrl set mcu_id_hiz fail\n");
		}
		gpio_free(hw_id_gpio);
		di->asic_hw_id = id_val[1]^id_val[2] ? 2 : id_val[0];
	} else
		di->asic_hw_id = 1; //default rk826

	return 0;
}
#endif
#endif

static void check_4p45_support(struct fastchg_device_info *di)
{
	if (di->is_4300mAh_4p45_support) {
		init_dash_4300mAh_4p45_exist_node();
		pr_info("4300mAh_4p45 dash exist\n");
	} else if (di->is_4320mAh_4p45_support) {
		init_dash_4320mAh_4p45_exist_node();
		pr_info("4320mAh_4p45 dash exist\n");
	} else if (di->is_4510mAh_4p45_support) {
		init_dash_4510mAh_4p45_exist_node();
		pr_info("4510mAh_4p45 dash exist\n");
	} else {
		pr_info("ST 4p45 dash not exist\n");
	}

}

#if (defined(OP_SWARP_SUPPORTED) && !defined(GET_HWID_BY_GPIO))
static bool is_sy6610(struct i2c_client *client)
{
	u8 value = 0;
	int rc = 0;

	client->addr = 0x06;

	rc = op_i2c_dma_read(client, 0x08, 1, &value);
	if (rc < 0) {
        pr_err("op10 read register 0x08 fail, rc = %d\n", rc);
        return false;
	} else {
		if (value == 0x02) {
			pr_err("op10 detected, register 0x08: 0x %x\n", value);
			return true;
		}
	}

	return false;
}
static bool is_rk826(struct i2c_client *client)
{
    u8 value_buf[2] = {0};
	u32 value = 0;
    int rc = 0;

	client->addr = 0x0a;
	rc = oneplus_u16_i2c_read(client, 0x52f8, 2, value_buf);
	if (rc < 0) {
        pr_err("rk826 read register 0x52f8 fail, rc = %d\n", rc);
        return false;
	} else {
		pr_err("register 0x52f8: 0x%x, 0x%x\n", value_buf[0], value_buf[1]);
		value = value_buf[0] | (value_buf[1] << 8);
		pr_err("register 0x52f8: 0x%x\n", value);
		if (value == 0x826A) {
			pr_err("rk826 detected, register 0x52f8: 0x%x\n", value);
			return true;
		}
	}
	return false;
}
static bool is_rt5125(struct i2c_client *client)
{
	u8 value = 0;
	int rc = 0;

	client->addr = 0x0e;

	rc = op_i2c_dma_read(client, 0x02, 1, &value);
	if (rc < 0) {
		pr_err("rt5125 read register 0x02 fail, rc = %d\n", rc);
		return false;
	} else {
		if (value == 0x80) {
			pr_err("rt5125 detected, register 0x02: 0x%x\n", value);
			return true;
		}
	}
	return false;
}
static void get_asic_hw_id_by_i2c_work(struct work_struct *work)
{
	struct fastchg_device_info *di = container_of(work,
				struct fastchg_device_info,
				get_asic_hwid_work.work);
	bool is_online = false;
	int i = 0;

	(void)opchg_mcu_action(ACTION_MODE_SWITCH_UPGRADE);
	for (i = 0; i < 3; i++) {
		if (i == 0)
			is_online = is_sy6610(di->client);
		else if (i == 1)
			is_online = is_rk826(di->client);
		else if (i == 2)
			is_online = is_rt5125(di->client);
		if (is_online)
			break;
	}
	if (i < 3)
		di->asic_hw_id = i;
	else
		di->asic_hw_id = 1;
	pr_err("asic_hw_id=%d", di->asic_hw_id);
	di->client->addr = i2c_addr[di->asic_hw_id];
	(void)opchg_mcu_action(ACTION_MODE_RESET_SLEEP);
}
#endif

static int dash_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct fastchg_device_info *di;
	int ret;

	pr_info("dash_probe\n");
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("i2c_func error\n");
		goto err_check_functionality_failed;
	}

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di) {
		ret = -ENOMEM;
		goto err_check_functionality_failed;
	}
	di->client = mcu_client = client;
	di->firmware_already_updated = false;
	di->irq_enabled = true;
	di->fast_chg_ing = false;
	di->fast_low_temp_full = false;
	di->fast_chg_started = false;

	fastchg_di = di;
	dev_set_drvdata(&client->dev, di);

	ret = dash_parse_dt(di);
	if (ret == -EINVAL)
		goto err_read_dt;

	ret = request_dash_gpios(di);
    /*
	if (ret < 0)
		goto err_read_dt;
    */
	dash_pinctrl_init(di);
	mutex_init(&di->read_mutex);
	mutex_init(&di->gpio_mutex);

	init_waitqueue_head(&di->read_wq);
	di->fastchg_wake_lock = wakeup_source_register(&client->dev, "fastcg_wake_lock");
	di->fastchg_update_fireware_lock = wakeup_source_register(&client->dev,
		"fastchg_fireware_lock");

	INIT_WORK(&di->fastcg_work, fastcg_work_func);
	INIT_WORK(&di->charger_present_status_work,
		update_charger_present_status);
	INIT_DELAYED_WORK(&di->update_fireware_version_work,
		update_fireware_version_func);
	INIT_DELAYED_WORK(&di->update_firmware, dashchg_fw_update);
	INIT_DELAYED_WORK(&di->adapter_update_work, adapter_update_work_func);
#if (defined(OP_SWARP_SUPPORTED) && !defined(GET_HWID_BY_GPIO))
	INIT_DELAYED_WORK(&di->get_asic_hwid_work, get_asic_hw_id_by_i2c_work);
#endif
	INIT_DELAYED_WORK(&di->disable_mcu_work, disable_mcu_work_func);

	__init_timer(&di->watchdog, di_watchdog, TIMER_IRQSAFE);
	//di->watchdog.data = (unsigned long)di;//20190707
	//di->watchdog.function = di_watchdog;//20190707

	di->dash_device.minor = MISC_DYNAMIC_MINOR;
	di->dash_device.name = "dash";
	di->dash_device.fops = &dash_dev_fops;
	ret = misc_register(&di->dash_device);
	if (ret) {
		pr_err("%s : misc_register failed\n", __FILE__);
		goto err_misc_register_failed;
	}

	mcu_init(di);
	check_n76e_support(di);
	check_enhance_support(di);
#ifdef OP_SWARP_SUPPORTED
	check_swarp_support(di);
	if (di->is_swarp_supported) {
#ifdef GET_HWID_BY_GPIO
		dash_get_asic_hw_id(di);
		client->addr = i2c_addr[di->asic_hw_id];
#else
		di->asic_hw_id = -1;
		schedule_delayed_work(&di->get_asic_hwid_work,
					msecs_to_jiffies(1000));
		di->skin_hi_curr_max = CURRENT_LIMIT_2;
		di->skin_wrm_curr_max = CURRENT_LIMIT_1;
		di->skin_med_curr_max = CURRENT_LIMIT_3;
		di->skin_hi_lcdoff_curr_max = CURRENT_LIMIT_2;
		di->skin_med_lcdoff_curr_max = CURRENT_LIMIT_6;
#endif
	} else {
		di->skin_hi_curr_max = CURRENT_LIMIT_2;
		di->skin_wrm_curr_max = CURRENT_LIMIT_1;
		di->skin_med_curr_max = CURRENT_LIMIT_1;
		di->skin_hi_lcdoff_curr_max = CURRENT_LIMIT_1;
		di->skin_med_lcdoff_curr_max = CURRENT_LIMIT_6;
	}
#endif
	check_4p45_support(di);
	init_enhance_dash_exist_node();
	init_dash_firmware_done_node();
	fastcharge_information_register(&fastcharge_information);
	pr_info("dash_probe success\n");

	return 0;

err_misc_register_failed:
err_read_dt:
	kfree(di);
err_check_functionality_failed:
	pr_err("dash_probe fail\n");
	return 0;
}

static int dash_remove(struct i2c_client *client)
{
	struct fastchg_device_info *di = dev_get_drvdata(&client->dev);

	fastcharge_information_unregister(&fastcharge_information);
	if (gpio_is_valid(di->mcu_en_gpio))
		gpio_free(di->mcu_en_gpio);
	if (gpio_is_valid(di->usb_sw_1_gpio))
		gpio_free(di->usb_sw_1_gpio);
	if (gpio_is_valid(di->usb_sw_2_gpio))
		gpio_free(di->usb_sw_2_gpio);
	if (gpio_is_valid(di->usb_on_gpio))
		gpio_free(di->usb_on_gpio);
	if (gpio_is_valid(di->usb_on_gpio_1))
		gpio_free(di->usb_on_gpio_1);
	if (gpio_is_valid(di->ap_clk))
		gpio_free(di->ap_clk);
	if (gpio_is_valid(di->ap_data))
		gpio_free(di->ap_data);

	dev_set_drvdata(&client->dev, NULL);
	return 0;
}

static void dash_shutdown(struct i2c_client *client)
{
	struct fastchg_device_info *di = dev_get_drvdata(&client->dev);

	if (di->is_swarp_supported) {
		(void)opchg_mcu_action(ACTION_MODE_RESET_SLEEP);
		msleep(10);
		if (di->fast_chg_started) {
			gpio_direction_output(di->ap_clk, 1);
			msleep(10);
			(void)opchg_mcu_action(ACTION_MODE_RESET_ACTIVE);
		}
		msleep(80);
	} else {
		usb_sw_gpio_set(0);
		mcu_en_reset();
		usleep_range(2000, 2001);
		set_mcu_active(0);
	}
}

static const struct of_device_id dash_match[] = {
	{ .compatible = "microchip,oneplus_fastchg" },
	{ },
};

static const struct i2c_device_id dash_id[] = {
	{ "dash_fastchg", 1 },
	{},
};
MODULE_DEVICE_TABLE(i2c, dash_id);

static struct i2c_driver dash_fastcg_driver = {
	.driver		= {
		.name = "dash_fastchg",
		.owner	= THIS_MODULE,
		.of_match_table = dash_match,
	},
	.probe		= dash_probe,
	.remove		= dash_remove,
	.shutdown	= dash_shutdown,
	.id_table	= dash_id,
};

static int __init dash_fastcg_init(void)
{
	return i2c_add_driver(&dash_fastcg_driver);
}
module_init(dash_fastcg_init);

static void __exit dash_fastcg_exit(void)
{
	i2c_del_driver(&dash_fastcg_driver);
}
module_exit(dash_fastcg_exit);
