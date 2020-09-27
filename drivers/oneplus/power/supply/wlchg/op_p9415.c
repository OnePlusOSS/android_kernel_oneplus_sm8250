#define pr_fmt(fmt) "WLCHG: %s: " fmt, __func__

#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/proc_fs.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/rtc.h>
#include <linux/oem/boot_mode.h>
#include <linux/pmic-voter.h>
#include <linux/oem/power/op_wlc_helper.h>
#include "op_chargepump.h"
#include "bq2597x_charger.h"
#include "op_wlchg_rx.h"
#include "op_p9415.h"
#include "op_p9415_fw.h"
#include "op_wlchg_policy.h"
#include "smb5-lib.h"
#include "linux/oem/project_info.h"

extern struct smb_charger *normal_charger;

static bool charge_mode_startup;
static struct op_p9415_ic *g_p9415_chip;

void op_set_wrx_en_value(int value);
void op_set_wrx_otg_value(int value);
extern int wlchg_get_usbin_val(void);
extern int wlchg_send_msg(enum WLCHG_MSG_TYPE type, char data, char remark);

static DEFINE_MUTEX(p9415_i2c_access);

#define P22X_ADD_COUNT 2
#define p9415_MAX_I2C_READ_CNT 10
static int __p9415_read_reg(struct op_p9415_ic *chip, int reg, char *returnData,
			    int count)
{
	/* We have 16-bit i2c addresses - care for endianness */
	char cmd_buf[2] = { reg >> 8, reg & 0xff };
	int ret = 0;
	int i;
	char val_buf[p9415_MAX_I2C_READ_CNT];

	for (i = 0; i < count; i++) {
		val_buf[i] = 0;
	}

	ret = i2c_master_send(chip->client, cmd_buf, P22X_ADD_COUNT);
	if (ret < P22X_ADD_COUNT) {
		pr_err("i2c read error, reg: %x\n", reg);
		return ret < 0 ? ret : -EIO;
	}

	ret = i2c_master_recv(chip->client, val_buf, count);
	if (ret < count) {
		pr_err("i2c read error, reg: %x\n", reg);
		return ret < 0 ? ret : -EIO;
	}

	for (i = 0; i < count; i++) {
		*(returnData + i) = val_buf[i];
	}

	return 0;
}

static int __p9415_write_reg(struct op_p9415_ic *chip, int reg, int val)
{
	int ret;
	unsigned char data[3] = { reg >> 8, reg & 0xff, val };

	ret = i2c_master_send(chip->client, data, 3);
	if (ret < 3) {
		pr_err("%s: i2c write error, reg: %x\n", __func__, reg);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int p9415_write_reg_multi_byte(struct op_p9415_ic *chip, int reg,
				      const char *cbuf, int length)
{
	int ret;
	int send_length;
	unsigned char *data_w;

	send_length = length + 2;
	data_w = kzalloc(send_length, GFP_KERNEL);
	if (!data_w) {
		pr_err("can't alloc memory!\n");
		return -EINVAL;
	}

	data_w[0] = reg >> 8;
	data_w[1] = reg & 0xff;

	memcpy(data_w + 2, cbuf, length);

	mutex_lock(&p9415_i2c_access);

	ret = i2c_master_send(chip->client, data_w, send_length);
	if (ret < send_length) {
		pr_err("%s: i2c write error, reg: %x\n", __func__, reg);
		kfree(data_w);
		mutex_unlock(&p9415_i2c_access);
		return ret < 0 ? ret : -EIO;
	}

	mutex_unlock(&p9415_i2c_access);

	kfree(data_w);
	return 0;
}

static int p9415_read_reg(struct op_p9415_ic *chip, int reg, char *returnData,
			  int count)
{
	int ret = 0;

	mutex_lock(&p9415_i2c_access);
	ret = __p9415_read_reg(chip, reg, returnData, count);
	mutex_unlock(&p9415_i2c_access);
	return ret;
}

int p9415_config_interface(struct op_p9415_ic *chip, int RegNum, int val,
				  int MASK)
{
	char p9415_reg = 0;
	int ret = 0;
	if (!chip) {
		pr_err("op_p9415_ic not ready!\n");
		return -EINVAL;
	}

	mutex_lock(&p9415_i2c_access);
	ret = __p9415_read_reg(chip, RegNum, &p9415_reg, 1);

	p9415_reg &= ~MASK;
	p9415_reg |= val;

	ret = __p9415_write_reg(chip, RegNum, p9415_reg);

	mutex_unlock(&p9415_i2c_access);

	return ret;
}

static int p9415_get_idt_con_val(struct op_p9415_ic *chip)
{
	if (chip->idt_con_gpio <= 0) {
		pr_err("idt_con_gpio not exist, return\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->idt_con_active) ||
	    IS_ERR_OR_NULL(chip->idt_con_sleep)) {
		pr_err("pinctrl null, return\n");
		return -EINVAL;
	}

	return gpio_get_value(chip->idt_con_gpio);
}

int p9415_get_idt_int_val(struct op_p9415_ic *chip)
{
	if (chip->idt_int_gpio <= 0) {
		pr_err("idt_int_gpio not exist, return\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->idt_int_active) ||
	    IS_ERR_OR_NULL(chip->idt_int_sleep)) {
		pr_err("pinctrl null, return\n");
		return -EINVAL;
	}

	return gpio_get_value(chip->idt_int_gpio);
}

static int p9415_set_vbat_en_val(struct op_p9415_ic *chip, int value)
{

	if (chip->vbat_en_gpio <= 0) {
		pr_err("vbat_en_gpio not exist, return\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->vbat_en_active) ||
	    IS_ERR_OR_NULL(chip->vbat_en_sleep) ||
	    IS_ERR_OR_NULL(chip->vbat_en_default)) {
		pr_err("pinctrl null, return\n");
		return -EINVAL;
	}

	if (value) {
		gpio_direction_output(chip->vbat_en_gpio, 1);
		pinctrl_select_state(chip->pinctrl, chip->vbat_en_default);
	} else {
		gpio_direction_output(chip->vbat_en_gpio, 0);
		pinctrl_select_state(chip->pinctrl, chip->vbat_en_sleep);
	}

	pr_info("set value:%d, gpio_val:%d\n", value,
		gpio_get_value(chip->vbat_en_gpio));
	return 0;
}

static int p9415_get_vbat_en_val(struct op_p9415_ic *chip)
{
	if (chip->vbat_en_gpio <= 0) {
		pr_err("vbat_en_gpio not exist, return\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->vbat_en_active) ||
	    IS_ERR_OR_NULL(chip->vbat_en_sleep) ||
	    IS_ERR_OR_NULL(chip->vbat_en_default)) {
		pr_err("pinctrl null, return\n");
		return -EINVAL;
	}

	return gpio_get_value(chip->vbat_en_gpio);
}

static int p9415_booster_en_gpio_init(struct op_p9415_ic *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OP_CHG][%s]: op_p9415_ic not ready!\n",
		       __func__);
		return -EINVAL;
	}

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		pr_err("get pinctrl fail\n");
		return -EINVAL;
	}

	//booster_en
	chip->booster_en_active =
		pinctrl_lookup_state(chip->pinctrl, "booster_en_active");
	if (IS_ERR_OR_NULL(chip->booster_en_active)) {
		pr_err("get booster_en_active fail\n");
		return -EINVAL;
	}

	chip->booster_en_sleep =
		pinctrl_lookup_state(chip->pinctrl, "booster_en_sleep");
	if (IS_ERR_OR_NULL(chip->booster_en_sleep)) {
		pr_err("get booster_en_sleep fail\n");
		return -EINVAL;
	}

	chip->booster_en_default =
		pinctrl_lookup_state(chip->pinctrl, "booster_en_default");
	if (IS_ERR_OR_NULL(chip->booster_en_default)) {
		pr_err("get booster_en_default fail\n");
		return -EINVAL;
	}

	gpio_direction_output(chip->booster_en_gpio, 0);
	pinctrl_select_state(chip->pinctrl, chip->booster_en_sleep);

	pr_err("gpio_val:%d\n", gpio_get_value(chip->booster_en_gpio));

	return 0;
}

void p9415_set_booster_en_val(int value)
{
	struct op_p9415_ic *chip = g_p9415_chip;

	if (!chip) {
		printk(KERN_ERR "[OP_CHG][%s]: op_p9415_ic not ready!\n",
		       __func__);
		return;
	}

	if (chip->booster_en_gpio <= 0) {
		pr_err("booster_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->booster_en_active) ||
	    IS_ERR_OR_NULL(chip->booster_en_sleep) ||
	    IS_ERR_OR_NULL(chip->booster_en_default)) {
		pr_err("pinctrl null, return\n");
		return;
	}

	if (value) {
		gpio_direction_output(chip->booster_en_gpio, 1);
		pinctrl_select_state(chip->pinctrl, chip->booster_en_active);
	} else {
		gpio_direction_output(chip->booster_en_gpio, 0);
		pinctrl_select_state(chip->pinctrl, chip->booster_en_sleep);
	}

	pr_err("set value:%d, gpio_val:%d\n", value,
		gpio_get_value(chip->booster_en_gpio));
}

int p9415_get_booster_en_val(struct op_p9415_ic *chip)
{
	if (chip->booster_en_gpio <= 0) {
		pr_err("booster_en_gpio not exist, return\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->booster_en_active) ||
	    IS_ERR_OR_NULL(chip->booster_en_sleep) ||
	    IS_ERR_OR_NULL(chip->booster_en_default)) {
		pr_err("pinctrl null, return\n");
		return -EINVAL;
	}

	return gpio_get_value(chip->booster_en_gpio);
}

static int p9415_idt_en_gpio_init(struct op_p9415_ic *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OP_CHG][%s]: op_p9415_ic not ready!\n",
		       __func__);
		return -EINVAL;
	}

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		pr_err("get pinctrl fail\n");
		return -EINVAL;
	}

	//idt_en
	chip->idt_en_active =
		pinctrl_lookup_state(chip->pinctrl, "idt_en_active");
	if (IS_ERR_OR_NULL(chip->idt_en_active)) {
		pr_err("get idt_en_active fail\n");
		return -EINVAL;
	}

	chip->idt_en_sleep =
		pinctrl_lookup_state(chip->pinctrl, "idt_en_sleep");
	if (IS_ERR_OR_NULL(chip->idt_en_sleep)) {
		pr_err("get idt_en_sleep fail\n");
		return -EINVAL;
	}

	chip->idt_en_default =
		pinctrl_lookup_state(chip->pinctrl, "idt_en_default");
	if (IS_ERR_OR_NULL(chip->idt_en_default)) {
		pr_err("get idt_en_default fail\n");
		return -EINVAL;
	}

	gpio_direction_output(chip->idt_en_gpio, 0);
	pinctrl_select_state(chip->pinctrl, chip->idt_en_sleep);

	pr_err("gpio_val:%d\n", gpio_get_value(chip->idt_en_gpio));

	return 0;
}

static int p9415_get_idt_en_val(struct op_p9415_ic *chip)
{
	if (chip->idt_en_gpio <= 0) {
		pr_err("idt_en_gpio not exist, return\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->idt_en_active) ||
	    IS_ERR_OR_NULL(chip->idt_en_sleep) ||
	    IS_ERR_OR_NULL(chip->idt_en_default)) {
		pr_err("pinctrl null, return\n");
		return -EINVAL;
	}

	return gpio_get_value(chip->idt_en_gpio);
}

static void p9415_set_idt_int_active(struct op_p9415_ic *chip)
{
	gpio_direction_input(chip->idt_int_gpio); // in
	pinctrl_select_state(chip->pinctrl, chip->idt_int_active); // no_PULL
}

static void p9415_set_idt_con_active(struct op_p9415_ic *chip)
{
	gpio_direction_input(chip->idt_con_gpio); // in
	pinctrl_select_state(chip->pinctrl, chip->idt_con_active); // no_PULL
}

static void p9415_idt_int_irq_init(struct op_p9415_ic *chip)
{
	chip->idt_int_irq = gpio_to_irq(chip->idt_int_gpio);

	pr_err("op-wlchg test %s chip->idt_int_irq[%d]\n", __func__,
	       chip->idt_int_irq);
}

static void p9415_idt_con_irq_init(struct op_p9415_ic *chip)
{
	chip->idt_con_irq = gpio_to_irq(chip->idt_con_gpio);
	pr_err("op-wlchg test %s chip->idt_con_irq[%d]\n", __func__,
	       chip->idt_con_irq);
}

static int p9415_idt_con_gpio_init(struct op_p9415_ic *chip)
{
	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		pr_err("get pinctrl fail\n");
		return -EINVAL;
	}

	//idt_con
	chip->idt_con_active =
		pinctrl_lookup_state(chip->pinctrl, "idt_connect_active");
	if (IS_ERR_OR_NULL(chip->idt_con_active)) {
		pr_err("get idt_con_active fail\n");
		return -EINVAL;
	}

	chip->idt_con_sleep =
		pinctrl_lookup_state(chip->pinctrl, "idt_connect_sleep");
	if (IS_ERR_OR_NULL(chip->idt_con_sleep)) {
		pr_err("get idt_con_sleep fail\n");
		return -EINVAL;
	}

	chip->idt_con_default =
		pinctrl_lookup_state(chip->pinctrl, "idt_connect_default");
	if (IS_ERR_OR_NULL(chip->idt_con_default)) {
		pr_err("get idt_con_default fail\n");
		return -EINVAL;
	}

	if (chip->idt_con_gpio > 0) {
		gpio_direction_input(chip->idt_con_gpio);
	}

	pinctrl_select_state(chip->pinctrl, chip->idt_con_active);

	return 0;
}

static int p9415_idt_int_gpio_init(struct op_p9415_ic *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OP_CHG][%s]: op_p9415_ic not ready!\n",
		       __func__);
		return -EINVAL;
	}

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		pr_err("get pinctrl fail\n");
		return -EINVAL;
	}

	//idt_int
	chip->idt_int_active =
		pinctrl_lookup_state(chip->pinctrl, "idt_int_active");
	if (IS_ERR_OR_NULL(chip->idt_int_active)) {
		pr_err("get idt_int_active fail\n");
		return -EINVAL;
	}

	chip->idt_int_sleep =
		pinctrl_lookup_state(chip->pinctrl, "idt_int_sleep");
	if (IS_ERR_OR_NULL(chip->idt_int_sleep)) {
		pr_err("get idt_int_sleep fail\n");
		return -EINVAL;
	}

	chip->idt_int_default =
		pinctrl_lookup_state(chip->pinctrl, "idt_int_default");
	if (IS_ERR_OR_NULL(chip->idt_int_default)) {
		pr_err("get idt_int_default fail\n");
		return -EINVAL;
	}

	if (chip->idt_int_gpio > 0) {
		gpio_direction_input(chip->idt_int_gpio);
	}

	pinctrl_select_state(chip->pinctrl, chip->idt_int_active);

	return 0;
}

static int p9415_vbat_en_gpio_init(struct op_p9415_ic *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OP_CHG][%s]: op_p9415_ic not ready!\n",
		       __func__);
		return -EINVAL;
	}

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		pr_err("get pinctrl fail\n");
		return -EINVAL;
	}

	//vbat_en
	chip->vbat_en_active =
		pinctrl_lookup_state(chip->pinctrl, "vbat_en_active");
	if (IS_ERR_OR_NULL(chip->vbat_en_active)) {
		pr_err("get vbat_en_active fail\n");
		return -EINVAL;
	}

	chip->vbat_en_sleep =
		pinctrl_lookup_state(chip->pinctrl, "vbat_en_sleep");
	if (IS_ERR_OR_NULL(chip->vbat_en_sleep)) {
		pr_err("get vbat_en_sleep fail\n");
		return -EINVAL;
	}

	chip->vbat_en_default =
		pinctrl_lookup_state(chip->pinctrl, "vbat_en_default");
	if (IS_ERR_OR_NULL(chip->vbat_en_default)) {
		pr_err("get vbat_en_default fail\n");
		return -EINVAL;
	}

	gpio_direction_output(chip->vbat_en_gpio, 0);
	pinctrl_select_state(chip->pinctrl, chip->vbat_en_sleep);

	return 0;
}

static int p9415_set_idt_en_val(struct op_p9415_ic *chip, int value) // 0 active, 1 inactive
{

	if (chip->idt_en_gpio <= 0) {
		pr_err("idt_en_gpio not exist, return\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->idt_en_active) ||
	    IS_ERR_OR_NULL(chip->idt_en_sleep) ||
	    IS_ERR_OR_NULL(chip->idt_en_default)) {
		pr_err("pinctrl null, return\n");
		return -EINVAL;
	}

	if (value) {
		gpio_direction_output(chip->idt_en_gpio, 1);
		pinctrl_select_state(chip->pinctrl, chip->idt_en_active);
	} else {
		gpio_direction_output(chip->idt_en_gpio, 0);
		pinctrl_select_state(chip->pinctrl, chip->idt_en_default);
	}
	pr_info("set value:%d, gpio_val:%d\n", value,
		gpio_get_value(chip->idt_en_gpio));
	return 0;
}

static int p9415_get_vout(struct op_p9415_ic *chip, int *vout)
{
	char val_buf[2] = { 0, 0 };
	int temp;
	int rc;

	rc = p9415_read_reg(chip, 0x003C, val_buf, 2);
	if (rc) {
		pr_err("read vout err, rc=%d\n", rc);
		return rc;
	}
	temp = val_buf[0] | val_buf[1] << 8;
	*vout = temp * 21000 / 4095;

	return 0;
}

static int p9415_set_vout(struct op_p9415_ic *chip, int vout)
{
	char val_buf[2];
	int rc;

	val_buf[0] = vout & 0x00FF;
	val_buf[1] = (vout & 0xFF00) >> 8;

	rc = p9415_write_reg_multi_byte(chip, 0x003E, val_buf, 2);
	if (rc) {
		pr_err("set vout err, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int p9415_get_vrect(struct op_p9415_ic *chip, int *vrect)
{
	char val_buf[2] = { 0, 0 };
	int temp;
	int rc;

	rc = p9415_read_reg(chip, 0x0040, val_buf, 2);
	pr_debug("raw data:0x%02x, 0x%02x\n", val_buf[0], val_buf[1]);
	if (rc) {
		pr_err("read vrect err, rc=%d\n", rc);
		return rc;
	}
	temp = val_buf[0] | val_buf[1] << 8;
	*vrect = temp * 26250 / 4095;

	return 0;
}

static int p9415_get_iout(struct op_p9415_ic *chip, int *iout)
{
	char val_buf[2] = { 0, 0 };
	int rc;

	rc = p9415_read_reg(chip, 0x0044, val_buf, 2);
	pr_debug("raw data:0x%02x, 0x%02x\n", val_buf[0], val_buf[1]);
	if (rc) {
		pr_err("read iout err, rc=%d\n", rc);
		return rc;
	}
	*iout = val_buf[0] | val_buf[1] << 8;

	return 0;
}

static int p9415_get_trx_vol(struct op_p9415_ic *chip, int *vol)
{
	char val_buf[2] = { 0, 0 };
	int rc;

	rc = p9415_read_reg(chip, 0x0070, val_buf, 2);
	pr_debug("raw data:0x%02x, 0x%02x\n", val_buf[0], val_buf[1]);
	if (rc) {
		pr_err("read trx vol err, rc=%d\n", rc);
		return rc;
	}
	*vol = val_buf[0] | val_buf[1] << 8;

	return 0;
}

static int p9415_get_trx_curr(struct op_p9415_ic *chip, int *curr)
{
	char val_buf[2] = { 0, 0 };
	int rc;

	rc = p9415_read_reg(chip, 0x006e, val_buf, 2);
	if (rc) {
		pr_err("read trx current err, rc=%d\n", rc);
		return rc;
	}
	*curr = val_buf[0] | val_buf[1] << 8;

	return 0;
}

static int p9415_get_cep_change_status(struct op_p9415_ic *chip)
{
	char val_buf[2] = { 0, 0 };
	static int pre_val;
	int val;
	int rc;

	rc = p9415_read_reg(chip, 0x0020, val_buf, 2);
	if (rc) {
		pr_err("Couldn't read cep change status, rc=%d\n", rc);
		return rc;
	}
	val = val_buf[0] | val_buf[1] << 8;

	if (val != pre_val) {
		pre_val = val;
		return 0;
	} else {
		return -EINVAL;
	}
}

static int p9415_get_cep_val(struct op_p9415_ic *chip, int *val)
{
	int rc;
	char temp = 0;

	rc = p9415_get_cep_change_status(chip);
	if (rc) {
		pr_info("cep val is not updated\n");
		return rc;
	}

	rc = p9415_read_reg(chip, 0x0033, &temp, 1);
	if (rc) {
		pr_err("Couldn't read CEP, rc = %x\n", rc);
		return rc;
	}
	*val = (signed char)temp;

	return rc;
}


static int p9415_get_cep_val_skip_check_update(struct op_p9415_ic *chip, int *val)
{
	int rc;
	char temp = 0;

	rc = p9415_read_reg(chip, 0x0033, &temp, 1);
	if (rc) {
		pr_err("Couldn't read CEP, rc = %x\n", rc);
		return rc;
	}
	*val = (signed char)temp;

	return rc;
}

static int p9415_get_work_freq(struct op_p9415_ic *chip, int *val)
{
	int rc;
	char temp;

	rc = p9415_read_reg(chip, 0x5e, &temp, 1);
	if (rc) {
		pr_err("Couldn't read rx freq val, rc = %d\n", rc);
		return rc;
	}
	*val = (int)temp;
	return rc;
}

static int p9415_get_rx_run_mode(struct op_p9415_ic *chip, int *val)
{
	int rc;
	char temp;

	rc = p9415_read_reg(chip, 0x0088, &temp, 1);
	if (rc) {
		 pr_err("Couldn't read 0x0088 rc = %x\n",rc);
		 return rc;
	}
	if (temp == 0x31) {
		pr_info("RX running in EPP!\n");
		*val = RX_RUNNING_MODE_EPP;
	} else if (temp == 0x04) {
		pr_info("RX running in BPP!\n");
		*val = RX_RUNNING_MODE_BPP;
	} else{
		pr_info("RX running in Others!\n");
		*val = RX_RUNNING_MODE_OTHERS;
	}
	return 0;
}

static int p9415_set_dcdc_enable(void)
{
	struct op_p9415_ic *chip = g_p9415_chip;
	int rc;

	if (chip == NULL) {
		pr_err("op_p9415_ic not exist, return\n");
		return -ENODEV;
	}

	rc = p9415_config_interface(chip, 0xd4, 0x01, 0x01);
	if (rc)
		pr_err("set dcdc enable error, rc=%d\n", rc);

	return rc;
}

static int p9415_set_trx_enable(struct op_p9415_ic *chip, bool enable)
{
	int rc;

	if (enable)
		rc = p9415_config_interface(chip, 0x76, 0x01, 0xff);
	else
		rc = p9415_config_interface(chip, 0x76, 0x00, 0xff);
	if (rc)
		pr_err("can't %s trx, rc=%d\n", rc, enable ? "enable" : "disable");

	return rc;
}

static int p9415_ftm_test(struct op_p9415_ic *chip)
{
	int rc;
	int err_no = 0;
	char temp[4] = { 0, 0, 0, 0 };

	if (normal_charger == NULL) {
		pr_err("[FTM_TEST]smb_charger isn't ready!\n");
		return -ENODEV;
	}

	if (wlchg_wireless_charge_start()) {
		pr_err("[FTM_TEST]g_op_chip->charger_exist == 1, return!\n");
		return -EINVAL;
	}

	if (wlchg_get_usbin_val() != 0) {
		pr_err("[FTM_TEST]usb using, can't test\n");
		return -EINVAL;
	}

	op_set_wrx_en_value(2);
	msleep(20);
	op_set_wrx_otg_value(1);
	msleep(20);
	smblib_set_charge_param(normal_charger, &normal_charger->param.otg_cl,
				REVERSE_WIRELESS_CHARGE_CURR_LIMT);
	smblib_set_charge_param(normal_charger, &normal_charger->param.otg_vol,
				WIRELESS_CHARGE_FTM_TEST_VOL_LIMT);
	// set pm8150b vbus out.
	smblib_vbus_regulator_enable(normal_charger->vbus_vreg->rdev);
	msleep(50);
	rc = p9415_read_reg(chip, 0x001C, temp, 4);
	if (rc) {
		pr_err("[FTM_TEST]Couldn't read p9415 fw version, rc=%d\n", rc);
		err_no |= WLCHG_FTM_TEST_RX_ERR;
	}
	pr_info("[FTM_TEST]p9415 fw: %02x %02x %02x %02x\n", temp[0], temp[1], temp[2], temp[3]);

	if (wlchg_get_usbin_val() != 0) {
		pr_err("[FTM_TEST]usb_int status exception\n");
		err_no |= WLCHG_FTM_TEST_RX_ERR;
	}

	rc = chargepump_i2c_test();
	if (rc) {
		pr_err("[FTM_TEST]Couldn't get cp1 status, rc=%d\n", rc);
		err_no |= WLCHG_FTM_TEST_CP1_ERR;
	}

	// disable pm8150b vbus out.
	smblib_vbus_regulator_disable(normal_charger->vbus_vreg->rdev);
	msleep(20);
	op_set_wrx_otg_value(0);
	msleep(20);
	if (!typec_is_otg_mode())
		op_set_wrx_en_value(0);

	return err_no;
}

static int p9415_get_trx_status(struct op_p9415_ic *chip, int *status)
{
	int rc;
	char temp;

	rc = p9415_read_reg(chip, 0x78, &temp, 1);
	if (rc) {
		pr_err("Couldn't read trx status, rc = %d\n", rc);
		return rc;
	}
	*status = (int)temp;

	return rc;
}

static int p9415_get_trx_err(struct op_p9415_ic *chip, int *err)
{
	int rc;
	char temp;

	rc = p9415_read_reg(chip, 0x79, &temp, 1);
	if (rc) {
		pr_err("Couldn't read trx err code, rc = %d\n", rc);
		return rc;
	}
	*err = (int)temp;

	return rc;
}

static int p9415_get_headroom(struct op_p9415_ic *chip, int *val)
{
	int rc;
	char temp;

	rc = p9415_read_reg(chip, 0x9e, &temp, 1);
	if (rc) {
		pr_err("Couldn't read headroom, rc = %d\n", rc);
		return rc;
	}
	*val = (int)temp;

	return rc;
}

static int p9415_set_headroom(struct op_p9415_ic *chip, int val)
{
	int rc;

	rc = p9415_config_interface(chip, 0x76, val, 0xff);
	if (rc)
		pr_err("can't set headroom, rc=%d\n", rc);

	return rc;
}

static int p9415_get_prop(struct rx_chip_prop *prop,
			  enum rx_prop_type prop_type,
			  union rx_chip_propval *val)
{
	struct op_p9415_ic *chip = prop->private_data;
	int temp = 0;
	int rc = 0;

	switch (prop_type) {
	case RX_PROP_VOUT:
		rc = p9415_get_vout(chip, &temp);
		val->intval = temp;
		break;
	case RX_PROP_VRECT:
		rc = p9415_get_vrect(chip, &temp);
		val->intval = temp;
		break;
	case RX_PROP_IOUT:
		rc = p9415_get_iout(chip, &temp);
		val->intval = temp;
		break;
	case RX_PROP_CEP:
		rc = p9415_get_cep_val(chip, &temp);
		val->intval = temp;
		break;
	case RX_PROP_CEP_SKIP_CHECK_UPDATE:
		rc = p9415_get_cep_val_skip_check_update(chip, &temp);
		val->intval = temp;
		break;
	case RX_PROP_WORK_FREQ:
		rc = p9415_get_work_freq(chip, &temp);
		val->intval = temp;
		break;
	case RX_PROP_TRX_STATUS:
		rc = p9415_get_trx_status(chip, &temp);
		val->intval = temp;
		break;
	case RX_PROP_TRX_ERROR_CODE:
		rc = p9415_get_trx_err(chip, &temp);
		val->intval = temp;
		break;
	case RX_PROP_TRX_VOL:
		rc = p9415_get_trx_vol(chip, &temp);
		val->intval = temp;
		break;
	case RX_PROP_TRX_CURR:
		rc = p9415_get_trx_curr(chip, &temp);
		val->intval = temp;
		break;
	case RX_PROP_RUN_MODE:
		rc = p9415_get_rx_run_mode(chip, &temp);
		val->intval = temp;
		break;
	case RX_PROP_FTM_TEST:
		rc = p9415_ftm_test(chip);
		if (rc < 0)
			return rc;
		val->intval = rc;
		rc = 0;
		break;
	case RX_PROP_CHIP_SLEEP:
		val->intval = p9415_get_vbat_en_val(chip);
		break;
	case RX_PROP_CHIP_EN:
		val->intval = p9415_get_idt_en_val(chip);
		break;
	case RX_PROP_CHIP_CON:
		val->intval = p9415_get_idt_con_val(chip);
		break;
	case RX_PROP_FW_UPDATING:
		val->intval = chip->idt_fw_updating;
		break;
	case RX_PROP_HEADROOM:
		rc = p9415_get_headroom(chip, &temp);
		val->intval = temp;
		break;
	default:
		return -EINVAL;
	}

	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", prop_type, rc);
		return -ENODATA;
	}

	return 0;
}

static int p9415_set_prop(struct rx_chip_prop *prop,
			  enum rx_prop_type prop_type,
			  union rx_chip_propval *val)
{
	struct op_p9415_ic *chip = prop->private_data;
	int rc = 0;

	switch (prop_type) {
	case RX_PROP_VOUT:
		rc = p9415_set_vout(chip, val->intval);
		break;
	case RX_PROP_ENABLE_DCDC:
		if (val->intval > 0)
			rc = p9415_set_dcdc_enable();
		break;
	case RX_PROP_CHIP_SLEEP:
		rc = p9415_set_vbat_en_val(chip, val->intval);
		break;
	case RX_PROP_CHIP_EN:
		rc = p9415_set_idt_en_val(chip, val->intval);
		break;
	case RX_PROP_TRX_ENABLE:
		rc = p9415_set_trx_enable(chip, val->intval);
		break;
	case RX_PROP_HEADROOM:
		rc = p9415_set_headroom(chip, val->intval);
		break;
	default:
		pr_err("set prop %d is not supported\n", prop_type);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int p9415_send_msg(struct rx_chip_prop *prop,
			  enum rx_msg_type msg_type,
			  unsigned char msg)
{
	struct op_p9415_ic *chip = prop->private_data;
	char write_data[2] = { 0, 0 };

	if (msg_type == RX_MSG_LONG) {
		write_data[0] = 0x10;
		write_data[1] = 0x00;
		p9415_write_reg_multi_byte(chip, 0x0038, write_data, 2);

		write_data[0] = 0x10;
		write_data[1] = 0x00;
		p9415_write_reg_multi_byte(chip, 0x0056, write_data, 2);

		write_data[0] = 0x20;
		write_data[1] = 0x00;
		p9415_write_reg_multi_byte(chip, 0x004E, write_data, 2);
	}

	if (msg_type != RX_MSG_SHORT) {
		p9415_config_interface(chip, 0x0050, 0x48, 0xFF);
		p9415_config_interface(chip, 0x0051, msg, 0xFF);
		p9415_config_interface(chip, 0x0052, (~msg), 0xFF);
		p9415_config_interface(chip, 0x0053, 0xFF, 0xFF);
		p9415_config_interface(chip, 0x0054, 0x00, 0xFF);
	} else {
		p9415_config_interface(chip, 0x0050, 0x18, 0xFF);
		p9415_config_interface(chip, 0x0051, msg, 0xFF);
	}

	p9415_config_interface(chip, 0x004E, 0x01, 0x01); //BIT0

	return 0;
}

static unsigned char p9415_calculate_checksum(const unsigned char *data, int len)
{
	unsigned char temp = 0;

	while(len--)
		temp ^= *data++;

	pr_info("checksum = %d\n", temp);
	return temp;
}

static int p9415_send_match_q_parm(struct rx_chip_prop *prop,
				    unsigned char data)
{
	struct op_p9415_ic *chip = prop->private_data;
	unsigned char buf[4] = {0x38, 0x48, 0x00, data};
	unsigned char checksum;

	checksum = p9415_calculate_checksum(buf, 4);
	p9415_config_interface(chip, 0x0050, buf[0], 0xFF);
	p9415_config_interface(chip, 0x0051, buf[1], 0xFF);
	p9415_config_interface(chip, 0x0052, buf[2], 0xFF);
	p9415_config_interface(chip, 0x0053, buf[3], 0xFF);
	p9415_config_interface(chip, 0x0054, checksum, 0xFF);

	p9415_config_interface(chip, 0x004E, 0x01, 0x01);

	return 0;
}

static int p9415_set_fod_parm(struct rx_chip_prop *prop,
			      const char data[])
{
	struct op_p9415_ic *chip = prop->private_data;
	int rc;

	rc = p9415_write_reg_multi_byte(chip, 0x0068, data, FOD_PARM_LENGTH);
	if (rc < 0)
		pr_err("set fod parameter error, rc=%d\n", rc);

	return rc;
}

static void p9415_reset(struct rx_chip_prop *prop)
{
	int wpc_con_level = 0;
	int wait_wpc_disconn_cnt = 0;
	struct op_p9415_ic *chip = prop->private_data;

	wpc_con_level = p9415_get_idt_con_val(chip);
	if (wpc_con_level == 1) {
		p9415_set_vbat_en_val(chip, 1);
		msleep(100);

		while (wait_wpc_disconn_cnt < 10) {
			wpc_con_level = p9415_get_idt_con_val(chip);
			if (wpc_con_level == 0) {
				break;
			}
			msleep(150);
			wait_wpc_disconn_cnt++;
		}
		chargepump_disable();
	}
	return;
}

static struct rx_chip_prop p9415_prop = {
	.get_prop = p9415_get_prop,
	.set_prop = p9415_set_prop,
	.send_msg = p9415_send_msg,
	.send_match_q_parm = p9415_send_match_q_parm,
	.set_fod_parm = p9415_set_fod_parm,
	.rx_reset = p9415_reset,
};

int p9415_init_registers(struct op_p9415_ic *chip)
{
	char write_data[2] = { 0, 0 };
	if (!chip) {
		pr_err("op_p9415_ic not ready!\n");
		return -EINVAL;
	}

	write_data[0] = 0x50;
	write_data[1] = 0x00;
	p9415_write_reg_multi_byte(chip, 0x0038, write_data, 2);

	write_data[0] = 0x30;
	write_data[1] = 0x00;
	p9415_write_reg_multi_byte(chip, 0x0056, write_data, 2);

	write_data[0] = 0x20;
	write_data[1] = 0x00;
	p9415_write_reg_multi_byte(chip, 0x004E, write_data, 2);

	return 0;
}

static bool p9415_firmware_is_updating(struct op_p9415_ic *chip)
{
	return chip->idt_fw_updating;
}

#ifdef NO_FW_UPGRADE_CRC
static int p9415_MTP(struct op_p9415_ic *chip, unsigned char *fw_buf, int fw_size)
{
	int rc;
	int i, j;
	unsigned char *fw_data;
	unsigned char write_ack;
	unsigned short int StartAddr;
	unsigned short int CheckSum;
	unsigned short int CodeLength;

	pr_err("<IDT UPDATE>--1--!\n");
	// configure the system
	rc = __p9415_write_reg(chip, 0x3000, 0x5a); // write key
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3000 reg error!\n");
		return rc;
	}

	pr_err("<IDT UPDATE>--2--!\n");
	rc = __p9415_write_reg(chip, 0x3040, 0x10); // halt M0
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3000 reg error!\n");
		return rc;
	}

	pr_err("<IDT UPDATE>--3--!\n");
	rc = p9415_write_reg_multi_byte(
		chip, 0x1c00, MTPBootloader9320,
		sizeof(MTPBootloader9320)); // load provided by IDT array
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x1c00 reg error!\n");
		return rc;
	}

	pr_err("<IDT UPDATE>--4--!\n");
	rc = __p9415_write_reg(chip, 0x400, 0); // initialize buffer
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x400 reg error!\n");
		return rc;
	}

	pr_err("<IDT UPDATE>--5--!\n");
	rc = __p9415_write_reg(chip, 0x3048,
			       0x80); // map RAM address 0x1c00 to OTP 0x0000
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3048 reg error!\n");
		return rc;
	}

	pr_err("<IDT UPDATE>--6--!\n");
	rc = __p9415_write_reg(chip, 0x3040, 0x80); // run M0
	// this global variable is used by the i2c driver to block ACK error message

	msleep(100);

	pr_err("<IDT UPDATE>The idt firmware size: %d!\n", fw_size);

	// program pages of 128 bytes
	fw_data = kzalloc(144, GFP_KERNEL);
	if (!fw_data) {
		pr_err("<IDT UPDATE>can't alloc memory!\n");
		return -EINVAL;
	}

	for (i = 0; i < fw_size; i += 128) {
		pr_err("<IDT UPDATE>Begin to write chunk %d!\n", i);

		StartAddr = i;
		CheckSum = StartAddr;
		CodeLength = 128;

		memcpy(fw_data + 8, fw_buf + i, 128);

		j = fw_size - i;
		if (j < 128) {
			j = ((j + 15) / 16) * 16;
			CodeLength = (unsigned short int)j;
		} else {
			j = 128;
		}

		j -= 1;
		for (; j >= 0; j--) {
			CheckSum += fw_data[j + 8]; // add the non zero values
		}

		CheckSum += CodeLength; // finish calculation of the check sum

		memcpy(fw_data + 2, (char *)&StartAddr, 2);
		memcpy(fw_data + 4, (char *)&CodeLength, 2);
		memcpy(fw_data + 6, (char *)&CheckSum, 2);

		rc = p9415_write_reg_multi_byte(chip, 0x400, fw_data,
						((CodeLength + 8 + 15) / 16) *
							16);
		if (rc != 0) {
			pr_err("<IDT UPDATE>ERROR: Write fw data error!\n");
			goto MTP_ERROR;
		}

		rc = __p9415_write_reg(chip, 0x400, 0x01);
		if (rc != 0) {
			pr_err("<IDT UPDATE>ERROR: on OTP buffer validation\n");
			goto MTP_ERROR;
		}

		do {
			msleep(20);

			rc = p9415_read_reg(chip, 0x400, &write_ack, 1);
			if (rc != 0) {
				pr_err("<IDT UPDATE>ERROR: on reading OTP buffer status\n");
				goto MTP_ERROR;
			}
		} while ((write_ack & 0x01) != 0);

		// check status
		if (write_ack != 2) { // not OK
			if (write_ack == 4) {
				pr_err("<IDT UPDATE>ERROR: WRITE ERR\n");
			} else if (write_ack == 8) {
				pr_err("<IDT UPDATE>ERROR: CHECK SUM ERR\n");
			} else {
				pr_err("<IDT UPDATE>ERROR: UNKNOWN ERR\n");
			}

			goto MTP_ERROR;
		}
	}

	// restore system
	rc = __p9415_write_reg(chip, 0x3000, 0x5a); // write key
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3000 reg error!\n");
		goto MTP_ERROR;
	}

	rc = __p9415_write_reg(chip, 0x3048, 0x00); // remove code remapping
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3048 reg error!\n");
		goto MTP_ERROR;
	}

	pr_err("<IDT UPDATE>OTP Programming finished\n");

	kfree(fw_data);
	return 0;

MTP_ERROR:
	kfree(fw_data);
	return -EINVAL;
}
#else
static int p9415_load_bootloader(struct op_p9415_ic *chip)
{
	int rc = 0;
	// configure the system
	rc = __p9415_write_reg(chip, 0x3000, 0x5a); // write key
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3000 reg error!\n");
		return rc;
	}

	rc = __p9415_write_reg(chip, 0x3004, 0x00); // set HS clock
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3004 reg error!\n");
		return rc;
	}

	rc = __p9415_write_reg(chip, 0x3008, 0x09); // set AHB clock
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3008 reg error!\n");
		return rc;
	}

	rc = __p9415_write_reg(chip, 0x300C, 0x05); // configure 1us pulse
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x300c reg error!\n");
		return rc;
	}

	rc = __p9415_write_reg(chip, 0x300D, 0x1d); // configure 500ns pulse
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x300d reg error!\n");
		return rc;
	}

	rc = __p9415_write_reg(chip, 0x3040, 0x11); // Enable MTP access via I2C
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3040 reg error!\n");
		return rc;
	}

	msleep(20);

	pr_err("<IDT UPDATE>-b-2--!\n");
	rc = __p9415_write_reg(chip, 0x3040, 0x10); // halt microcontroller M0
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3040 reg error!\n");
		return rc;
	}

	pr_err("<IDT UPDATE>-b-3--!\n");
	rc = p9415_write_reg_multi_byte(
		chip, 0x0800, MTPBootloader9320,
		sizeof(MTPBootloader9320)); // load provided by IDT array
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x1c00 reg error!\n");
		return rc;
	}

	pr_err("<IDT UPDATE>-b-4--!\n");
	rc = __p9415_write_reg(chip, 0x400, 0); // initialize buffer
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x400 reg error!\n");
		return rc;
	}

	pr_err("<IDT UPDATE>-b-5--!\n");
	rc = __p9415_write_reg(chip, 0x3048, 0xD0); // map RAM address 0x1c00 to OTP 0x0000
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3048 reg error!\n");
		return rc;
	}

	pr_err("<IDT UPDATE>-b-6--!\n");
	rc = __p9415_write_reg(chip, 0x3040, 0x80); // run M0

	return 0;
}

static int p9415_load_fw(struct op_p9415_ic *chip, unsigned char *fw_data, int CodeLength)
{
	unsigned char write_ack = 0;
	int rc = 0;

	rc = p9415_write_reg_multi_byte(chip, 0x400, fw_data,
							((CodeLength + 8 + 15) / 16) * 16);
	if (rc != 0) {
		pr_err("<IDT UPDATE>ERROR: write multi byte data error!\n");
		goto LOAD_ERR;
	}
	rc = __p9415_write_reg(chip, 0x400, 0x01);
	if (rc != 0) {
		pr_err("<IDT UPDATE>ERROR: on OTP buffer validation\n");
		goto LOAD_ERR;
	}

	do {
		msleep(20);
		rc = p9415_read_reg(chip, 0x401, &write_ack, 1);
		if (rc != 0) {
			pr_err("<IDT UPDATE>ERROR: on reading OTP buffer status\n");
			goto LOAD_ERR;
		}
	} while ((write_ack & 0x01) != 0);

	// check status
	if (write_ack != 2) { // not OK
		if (write_ack == 4)
			pr_err("<IDT UPDATE>ERROR: WRITE ERR\n");
		else if (write_ack == 8)
			pr_err("<IDT UPDATE>ERROR: CHECK SUM ERR\n");
		else
			pr_err("<IDT UPDATE>ERROR: UNKNOWN ERR\n");

		rc = -1;
	}
LOAD_ERR:
	return rc;
}

static int p9415_MTP(struct op_p9415_ic *chip, unsigned char *fw_buf, int fw_size)
{
	int rc;
	int i, j;
	unsigned char *fw_data;
	unsigned char write_ack;
	unsigned short int StartAddr;
	unsigned short int CheckSum;
	unsigned short int CodeLength;
	// pure fw size not contains last 128 bytes fw version.
	int pure_fw_size = fw_size - 128;

	pr_err("<IDT UPDATE>--1--!\n");

	rc = p9415_load_bootloader(chip);
	if (rc != 0) {
		pr_err("<IDT UPDATE>Update bootloader 1 error!\n");
		return rc;
	}

	msleep(100);

	pr_err("<IDT UPDATE>The idt firmware size: %d!\n", fw_size);

	// program pages of 128 bytes
	// 8-bytes header, 128-bytes data, 8-bytes padding to round to 16-byte boundary
	fw_data = kzalloc(144, GFP_KERNEL);
	if (!fw_data) {
		pr_err("<IDT UPDATE>can't alloc memory!\n");
		return -EINVAL;
	}

	//ERASE FW VERSION(the last 128 byte of the MTP)
	memset(fw_data, 0x00, 144);
	StartAddr = pure_fw_size;
	CheckSum = StartAddr;
	CodeLength = 128;
	for (j = 127; j >= 0; j--)
		CheckSum += fw_data[j + 8]; // add the non zero values.

	CheckSum += CodeLength; // finish calculation of the check sum
	memcpy(fw_data + 2, (char *)&StartAddr, 2);
	memcpy(fw_data + 4, (char *)&CodeLength, 2);
	memcpy(fw_data + 6, (char *)&CheckSum, 2);
	rc = p9415_load_fw(chip, fw_data, CodeLength);
	if (rc < 0) { // not OK
		pr_err("<IDT UPDATE>ERROR: erase fw version ERR\n");
		goto MTP_ERROR;
	}

	// upgrade fw
	memset(fw_data, 0x00, 144);
	for (i = 0; i < pure_fw_size; i += 128) {
		pr_err("<IDT UPDATE>Begin to write chunk %d!\n", i);

		StartAddr = i;
		CheckSum = StartAddr;
		CodeLength = 128;

		memcpy(fw_data + 8, fw_buf + i, 128);

		j = pure_fw_size - i;
		if (j < 128) {
			j = ((j + 15) / 16) * 16;
			CodeLength = (unsigned short int)j;
		} else {
			j = 128;
		}

		j -= 1;
		for (; j >= 0; j--)
			CheckSum += fw_data[j + 8]; // add the non zero values

		CheckSum += CodeLength; // finish calculation of the check sum

		memcpy(fw_data + 2, (char *)&StartAddr, 2);
		memcpy(fw_data + 4, (char *)&CodeLength, 2);
		memcpy(fw_data + 6, (char *)&CheckSum, 2);

		//typedef struct { // write to structure at address 0x400
		// u16 Status;
		// u16 StartAddr;
		// u16 CodeLength;
		// u16 DataChksum;
		// u8 DataBuf[128];
		//} P9220PgmStrType;
		// read status is guaranteed to be != 1 at this point

		rc = p9415_load_fw(chip, fw_data, CodeLength);
		if (rc < 0) { // not OK
			pr_err("<IDT UPDATE>ERROR: write chunk %d ERR\n", i);
			goto MTP_ERROR;
		}
	}

	msleep(100);
	// disable pm8150b vbus out.
	pr_info("<IDT UPDATE>disable pm8150b vbus out\n");
	smblib_vbus_regulator_disable(normal_charger->vbus_vreg->rdev);
	msleep(3000);
	// enable pm8150b vbus out.
	pr_info("<IDT UPDATE>enable pm8150b vbus out\n");
	smblib_set_charge_param(normal_charger, &normal_charger->param.otg_cl,
				WIRELESS_CHARGE_UPGRADE_CURR_LIMT);
	smblib_set_charge_param(normal_charger, &normal_charger->param.otg_vol,
				WIRELESS_CHARGE_UPGRADE_VOL_LIMT);
	smblib_vbus_regulator_enable(normal_charger->vbus_vreg->rdev);
	msleep(500);

	// Verify
	rc = p9415_load_bootloader(chip);
	if (rc != 0) {
		pr_err("<IDT UPDATE>Update bootloader 2 error!\n");
		return rc;
	}
	msleep(100);
	rc = __p9415_write_reg(chip, 0x402, 0x00); // write start address
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x402 reg error!\n");
		return rc;
	}
	rc = __p9415_write_reg(chip, 0x404, pure_fw_size & 0xff); // write FW length low byte
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x404 reg error!\n");
		return rc;
	}
	rc = __p9415_write_reg(chip, 0x405, (pure_fw_size >> 8) & 0xff); // write FW length high byte
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x405 reg error!\n");
		return rc;
	}

	// write CRC from FW release package
	fw_data[0] = fw_buf[pure_fw_size + 0x08];
	fw_data[1] = fw_buf[pure_fw_size + 0x09];
	p9415_write_reg_multi_byte(chip, 0x406, fw_data, 2);

	rc = __p9415_write_reg(chip, 0x400, 0x11);
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x406 reg error!\n");
		return rc;
	}
	do {
		msleep(20);
		rc = p9415_read_reg(chip, 0x401, &write_ack, 1);
		if (rc != 0) {
			pr_err("<IDT UPDATE>ERROR: on reading OTP buffer status\n");
			goto MTP_ERROR;
		}
	} while ((write_ack & 0x01) != 0);
	// check status
	if (write_ack != 2) { // not OK
		if (write_ack == 4)
			pr_err("<IDT UPDATE>ERROR: CRC WRITE ERR\n");
		else if (write_ack == 8)
			pr_err("<IDT UPDATE>ERROR: CRC CHECK SUM ERR\n");
		else
			pr_err("<IDT UPDATE>ERROR: CRC UNKNOWN ERR\n");

		goto MTP_ERROR;
	}

	memset(fw_data, 0x00, 144);
	StartAddr = pure_fw_size;
	CheckSum = StartAddr;
	CodeLength = 128;
	memcpy(fw_data + 8, fw_buf + StartAddr, 128);
	j = 127;
	for (; j >= 0; j--)
		CheckSum += fw_data[j + 8]; // add the non zero values.

	CheckSum += CodeLength; // finish calculation of the check sum
	memcpy(fw_data + 2, (char *)&StartAddr, 2);
	memcpy(fw_data + 4, (char *)&CodeLength, 2);
	memcpy(fw_data + 6, (char *)&CheckSum, 2);

	rc = p9415_load_fw(chip, fw_data, CodeLength);
	if (rc < 0) { // not OK
		pr_err("<IDT UPDATE>ERROR: erase fw version ERR\n");
		goto MTP_ERROR;
	}

	// restore system
	rc = __p9415_write_reg(chip, 0x3000, 0x5a); // write key
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3000 reg error!\n");
		goto MTP_ERROR;
	}

	rc = __p9415_write_reg(chip, 0x3048, 0x00); // remove code remapping
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3048 reg error!\n");
		goto MTP_ERROR;
	}

	pr_err("<IDT UPDATE>OTP Programming finished\n");

	kfree(fw_data);
	return 0;

MTP_ERROR:
	kfree(fw_data);
	return -EINVAL;
}
#endif // NO_FW_UPGRADE_CRC

static int p9415_check_idt_fw_update(struct op_p9415_ic *chip)
{
	static int idt_update_retry_cnt;
	int rc = -1;
	char temp[4] = { 0, 0, 0, 0 };
	unsigned char *fw_buf;
	int fw_size;
	char pre_hw_version[10] = {0};
	char new_hw_version[10] = {0};
	bool fw_upgrade_successful = false;
#ifndef NO_FW_UPGRADE_CRC
	int fw_ver_start_addr = 0;
#endif
	fw_buf = idt_firmware;
	fw_size = ARRAY_SIZE(idt_firmware);

	pr_err("<IDT UPDATE> check idt fw <><><><><><><><>\n");

	if (normal_charger == NULL) {
		pr_err("<IDT UPDATE> smb chg isn't ready!\n");
		return rc;
	}

	if (wlchg_wireless_working()) {
		pr_err("<IDT UPDATE>p9415 is working, return!\n");
		chip->check_fw_update = true;
		return 0;
	}

	chip->idt_fw_updating = true;
	// disable irq
	disable_irq(chip->idt_con_irq);
	disable_irq(chip->idt_int_irq);

	op_set_wrx_en_value(2);
	msleep(20);
	op_set_wrx_otg_value(1);
	msleep(20);
	smblib_set_charge_param(normal_charger, &normal_charger->param.otg_cl,
				WIRELESS_CHARGE_UPGRADE_CURR_LIMT);
	smblib_set_charge_param(normal_charger, &normal_charger->param.otg_vol,
				WIRELESS_CHARGE_UPGRADE_VOL_LIMT);
	// set pm8150b vbus out.
	smblib_vbus_regulator_enable(normal_charger->vbus_vreg->rdev);
	msleep(500);

	// get idt id.
	rc = p9415_read_reg(chip, 0x5870, temp, 2);
	pr_info("<IDT UPDATE> ID= %02x %02x", temp[0], temp[1]);
	if (!rc)
		snprintf(chip->manu_name, 10, "IDTP9415");

	rc = p9415_read_reg(chip, 0x001C, temp, 4);
	if (rc) {
		chg_err("<IDT UPDATE>Couldn't read 0x%04x rc = %x\n", 0x001C, rc);
		chip->check_fw_update = false;
		idt_update_retry_cnt++;
	} else {
		snprintf(pre_hw_version, 10, "%02x%02x%02x%02x", temp[3],
			temp[2], temp[1], temp[0]);
		chg_info("<IDT UPDATE>The idt fw version: %s\n", pre_hw_version);
#ifdef NO_FW_UPGRADE_CRC
		snprintf(new_hw_version, 10, "%02x%02x%02x%02x", fw_buf[0x130F],
						fw_buf[0x130E], fw_buf[0x130D], fw_buf[0x130C]);
		chg_info("<IDT UPDATE>The new fw version: %s\n", new_hw_version);

		if ((temp[0] != fw_buf[0x130C]) ||
		    (temp[1] != fw_buf[0x130D]) ||
		    (temp[2] != fw_buf[0x130E]) ||
		    (temp[3] != fw_buf[0x130F]) ||
		    (idt_update_retry_cnt > 0)) {
#else
		fw_ver_start_addr = fw_size - 128;
		snprintf(new_hw_version, 10, "%02x%02x%02x%02x",
			fw_buf[fw_ver_start_addr + 0x07], fw_buf[fw_ver_start_addr + 0x06],
			fw_buf[fw_ver_start_addr + 0x05], fw_buf[fw_ver_start_addr + 0x04]);
		chg_info("<IDT UPDATE>The new fw version: %s\n", new_hw_version);

		if ((temp[0] != fw_buf[fw_ver_start_addr + 0x04]) ||
		    (temp[1] != fw_buf[fw_ver_start_addr + 0x05]) ||
		    (temp[2] != fw_buf[fw_ver_start_addr + 0x06]) ||
		    (temp[3] != fw_buf[fw_ver_start_addr + 0x07]) ||
		    (idt_update_retry_cnt > 0)) {
#endif
			pr_info("<IDT UPDATE>Need update the idt fw!\n");
			if (p9415_MTP(chip, fw_buf, fw_size) == 0) {
				idt_update_retry_cnt = 0;
				chip->check_fw_update = true;
				fw_upgrade_successful = true;
			} else {
				chip->check_fw_update = false;
				idt_update_retry_cnt++;
				pr_err("<IDT UPDATE>p9415_MTP failed, Retry %d!\n",
						idt_update_retry_cnt);
				rc = -1;
			}
		} else {
			pr_info("<IDT UPDATE>No Need update the idt fw!\n");
			fw_upgrade_successful = true;
			chip->check_fw_update = true;
		}
	}

	if (idt_update_retry_cnt >= 5) {
		pr_err("<IDT UPDATE>Retry more than 5 times, firmware upgrade failed\n");
		idt_update_retry_cnt = 0;
		chip->check_fw_update = true;
		rc = 0;
	}

	msleep(100);
	// disable pm8150b vbus out.
	smblib_vbus_regulator_disable(normal_charger->vbus_vreg->rdev);
	msleep(20);
	op_set_wrx_otg_value(0);
	msleep(20);
	if (!typec_is_otg_mode())
		op_set_wrx_en_value(0);

	// enable irq
	enable_irq(chip->idt_int_irq);
	enable_irq(chip->idt_con_irq);
	chip->idt_fw_updating = false;

	if (fw_upgrade_successful)
		snprintf(chip->fw_id, 16, "0x%s", new_hw_version);
	else
		snprintf(chip->fw_id, 16, "0x%s", pre_hw_version);
	push_component_info(WIRELESS_CHARGE, chip->fw_id, chip->manu_name);

	return rc;
}

int p9415_upgrade_firmware(struct op_p9415_ic *chip, unsigned char *fw_buf, int fw_size)
{
	int rc = 0;

	if (normal_charger == NULL) {
		pr_err("<IDT UPDATE>smb_charger isn't ready!\n");
		return -ENODEV;
	}

	if (wlchg_wireless_working()) {
		pr_err("<IDT UPDATE>p9415 is working, return!\n");
		return -EINVAL;
	}

	if (wlchg_get_usbin_val() != 0) {
		pr_err("<IDT UPDATE>usb using, can't update\n");
		return -EINVAL;
	}

	chip->idt_fw_updating = true;

	op_set_wrx_en_value(2);
	msleep(20);
	op_set_wrx_otg_value(1);
	msleep(20);
	smblib_set_charge_param(normal_charger, &normal_charger->param.otg_cl,
				WIRELESS_CHARGE_UPGRADE_CURR_LIMT);
	smblib_set_charge_param(normal_charger, &normal_charger->param.otg_vol,
				WIRELESS_CHARGE_UPGRADE_VOL_LIMT);
	// set pm8150b vbus out.
	smblib_vbus_regulator_enable(normal_charger->vbus_vreg->rdev);
	msleep(500);

	rc = p9415_MTP(chip, fw_buf, fw_size);
	if (rc != 0) {
		pr_err("<IDT UPDATE>update error, rc=%d\n", rc);
	}

	msleep(100);
	// disable pm8150b vbus out.
	smblib_vbus_regulator_disable(normal_charger->vbus_vreg->rdev);
	msleep(20);
	op_set_wrx_otg_value(0);
	msleep(20);
	if (!typec_is_otg_mode())
		op_set_wrx_en_value(0);

	chip->idt_fw_updating = false;

	return rc;
}

static void p9415_commu_data_process(struct op_p9415_ic *chip)
{
	int rc = -1;
	char temp[2] = { 0, 0 };
	char val_buf[6] = { 0, 0, 0, 0, 0, 0};
	struct rx_chip *rx_chip = chip->rx_chip;

	rc = p9415_read_reg(chip, P9415_STATUS_REG, temp, 2);
	if (rc) {
		pr_err("Couldn't read 0x%04x rc = %x\n", P9415_STATUS_REG, rc);
		temp[0] = 0;
	} else {
		pr_info("read 0x0036 = 0x%02x 0x%02x\n", temp[0], temp[1]);
	}

	if (temp[0] & P9415_LDO_ON_MASK) {
		pr_info("<~WPC~> LDO is on, connected.");
		if (p9415_firmware_is_updating(chip)) {
			pr_err("firmware_is_updating is true, return directly.");
			return;
		}
		wlchg_connect_callback_func(true);
		chip->connected_ldo_on = true;
	}
	if (temp[0] & P9415_VOUT_ERR_MASK) {
		pr_err("Vout residual voltage is too high\n");
	}
	if (temp[0] & P9415_EVENT_MASK) {
		rc = p9415_read_reg(chip, 0x0058, val_buf, 6);
		if (rc) {
			pr_err("Couldn't read 0x%04x rc = %x\n", 0x0058, rc);
		} else {
			pr_info("Received TX data: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
				val_buf[0], val_buf[1], val_buf[2], val_buf[3], val_buf[4], val_buf[5]);
			temp[0] = ~val_buf[2];
			temp[1] = ~val_buf[4];
			if ((val_buf[0] == 0x4F) && (val_buf[1] == temp[0]) &&
			    (val_buf[3] == temp[1])) {
				rc = wlchg_send_msg(WLCHG_MSG_CMD_RESULT, val_buf[3], val_buf[1]);
				pr_info("TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT\n");
				pr_info("<~WPC~> Received TX command: 0x%02X, data: 0x%02X\n",
					val_buf[1], val_buf[3]);
				pr_info("TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT\n");
				if (rc < 0) {
					pr_err("send cmd result err, try again\n");
					msleep(2);
					wlchg_send_msg(WLCHG_MSG_CMD_RESULT, val_buf[3], val_buf[1]);
				}
			} else if (val_buf[0] == 0x5F) {
				if (val_buf[5] == 0x04 && (val_buf[4] == 0x03 || val_buf[4] == 0x02)) {
					pr_info("It's on OP Trx phone.");
					rx_chip->on_op_trx = true;
				}
				rc = wlchg_send_msg(WLCHG_MSG_CMD_RESULT, 0, val_buf[0]);
				if (rc < 0) {
					pr_err("send cmd result err, try again\n");
					msleep(2);
					wlchg_send_msg(WLCHG_MSG_CMD_RESULT, 0, val_buf[0]);
				}
			}
		}
	}

	wlchg_tx_callback();

	p9415_config_interface(chip, 0x0036, 0x00, 0xFF);
	p9415_config_interface(chip, 0x0037, 0x00, 0xFF);
	p9415_config_interface(chip, 0x0056, 0x30, 0x30);
	p9415_config_interface(chip, 0x004E, 0x20, 0x20);
}

static void p9415_event_int_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct op_p9415_ic *chip =
		container_of(dwork, struct op_p9415_ic, idt_event_int_work);

	p9415_commu_data_process(chip);
}

static void p9415_connect_int_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct op_p9415_ic *chip =
		container_of(dwork, struct op_p9415_ic, idt_connect_int_work);

	if (p9415_firmware_is_updating(chip) == true) {
		pr_err("firmware_is_updating is true, return directly.");
		return;
	}

	msleep(50);
	wlchg_connect_callback_func(false);
	if (!charge_mode_startup && (get_boot_mode() == MSM_BOOT_MODE_CHARGE)) {
		charge_mode_startup = true;
		if (wlchg_get_usbin_val() == 1)
			p9415_set_vbat_en_val(chip, 1);
		else
			schedule_delayed_work(&chip->idt_event_int_work, msecs_to_jiffies(500));
	}
	if (p9415_get_idt_con_val(chip) == 1) {
		pm_stay_awake(chip->dev);
		cancel_delayed_work_sync(&chip->check_ldo_on_work);
		schedule_delayed_work(&chip->check_ldo_on_work, p9415_CHECK_LDO_ON_DELAY);
		chg_info("schedule delayed 2s work for check ldo on.");
	} else {
		pm_relax(chip->dev);
		chip->connected_ldo_on = false;
	}
}

static void p9415_check_ldo_on_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct op_p9415_ic *chip =
		container_of(dwork, struct op_p9415_ic, check_ldo_on_work);

	chg_info("connected_ldo_on is : %s", chip->connected_ldo_on ? "true" : "false");
	if ((!chip->connected_ldo_on)
		&& p9415_get_idt_con_val(chip) == 1) {
		chg_err("Connect but no ldo on event irq, check again.");
		p9415_commu_data_process(chip);
	}
}

static void p9415_idt_event_shedule_work(void)
{
	if (normal_charger == NULL) {
		pr_err("smbchg not ready\n");
		return;
	}

	if (!g_p9415_chip) {
		pr_err(" p9415_chip is NULL\n");
	} else {
		schedule_delayed_work(&g_p9415_chip->idt_event_int_work, 0);
	}
}

static void p9415_idt_connect_shedule_work(void)
{
	if (normal_charger == NULL) {
		pr_err("smbchg not ready\n");
		return;
	}

	if (!g_p9415_chip) {
		pr_err(" p9415_chip is NULL\n");
	} else {
		schedule_delayed_work(&g_p9415_chip->idt_connect_int_work, 0);
	}
}

static irqreturn_t irq_idt_event_int_handler(int irq, void *dev_id)
{
	pr_err(" op-wlchg test irq happened\n");
	p9415_idt_event_shedule_work();
	return IRQ_HANDLED;
}

static irqreturn_t irq_idt_connect_int_handler(int irq, void *dev_id)
{
	p9415_idt_connect_shedule_work();
	return IRQ_HANDLED;
}

static int p9415_idt_int_eint_register(struct op_p9415_ic *chip)
{
	int retval = 0;

	p9415_set_idt_int_active(chip);
	retval = devm_request_irq(chip->dev, chip->idt_int_irq,
				  irq_idt_event_int_handler,
				  IRQF_TRIGGER_FALLING, "p9415_idt_int",
				  chip); //0X01:rising edge, 0x02:falling edge
	if (retval < 0) {
		pr_err("%s request idt_int irq failed.\n", __func__);
	}
	return retval;
}

static int p9415_idt_con_eint_register(struct op_p9415_ic *chip)
{
	int retval = 0;

	pr_err("%s op-wlchg test start, irq happened\n", __func__);
	p9415_set_idt_con_active(chip);
	retval = devm_request_irq(chip->dev, chip->idt_con_irq,
				  irq_idt_connect_int_handler,
				  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				  "p9415_con_int",
				  chip); //0X01:rising edge, 0x02:falling edge
	if (retval < 0) {
		pr_err("%s request idt_con irq failed.\n", __func__);
	}
	return retval;
}

static int p9415_gpio_init(struct op_p9415_ic *chip)
{
	int rc = 0;
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		pr_err("device tree node missing\n");
		return -EINVAL;
	}

	// Parsing gpio idt_int
	chip->idt_int_gpio = of_get_named_gpio(node, "qcom,idt_int-gpio", 0);
	if (chip->idt_int_gpio < 0) {
		pr_err("chip->idt_int_gpio not specified\n");
		return -EINVAL;
	} else {
		if (gpio_is_valid(chip->idt_int_gpio)) {
			rc = gpio_request(chip->idt_int_gpio, "idt-idt-gpio");
			if (rc) {
				pr_err("unable to request gpio [%d]\n", chip->idt_int_gpio);
				goto free_gpio_1;
			} else {
				rc = p9415_idt_int_gpio_init(chip);
				if (rc) {
					pr_err("unable to init idt_int_gpio:%d\n", chip->idt_int_gpio);
					goto free_gpio_1;
				} else {
					p9415_idt_int_irq_init(chip);
					rc = p9415_idt_int_eint_register(chip);
					if (rc < 0) {
						pr_err("Init idt event irq failed.");
						goto free_gpio_1;
					} else {
						enable_irq_wake(chip->idt_int_irq);
					}
				}
			}
		}
		chg_debug("chip->idt_int_gpio =%d\n", chip->idt_int_gpio);
	}

	// Parsing gpio idt_connect
	chip->idt_con_gpio =
		of_get_named_gpio(node, "qcom,idt_connect-gpio", 0);
	if (chip->idt_con_gpio < 0) {
		pr_err("chip->idt_con_gpio not specified\n");
		rc = -EINVAL;
		goto free_gpio_1;
	} else {
		if (gpio_is_valid(chip->idt_con_gpio)) {
			rc = gpio_request(chip->idt_con_gpio,
					  "idt-connect-gpio");
			if (rc) {
				pr_err("unable to request gpio [%d]\n",
				       chip->idt_con_gpio);
				goto free_gpio_1;
			} else {
				rc = p9415_idt_con_gpio_init(chip);
				if (rc) {
					pr_err("unable to init idt_con_gpio:%d\n",	chip->idt_con_gpio);
					goto free_gpio_2;
				} else {
					p9415_idt_con_irq_init(chip);
					rc = p9415_idt_con_eint_register(chip);
					if (rc < 0) {
						pr_err("Init idt connect irq failed.");
						goto free_gpio_2;
					} else {
						enable_irq_wake(chip->idt_con_irq);
					}
				}
			}
		}
		chg_debug("chip->idt_con_gpio =%d\n", chip->idt_con_gpio);
	}

	// Parsing gpio vbat_en
	chip->vbat_en_gpio = of_get_named_gpio(node, "qcom,vbat_en-gpio", 0);
	if (chip->vbat_en_gpio < 0) {
		pr_err("chip->vbat_en_gpio not specified\n");
		rc = -EINVAL;
		goto free_gpio_2;
	} else {
		if (gpio_is_valid(chip->vbat_en_gpio)) {
			rc = gpio_request(chip->vbat_en_gpio, "vbat-en-gpio");
			if (rc) {
				pr_err("unable to request gpio [%d]\n", chip->vbat_en_gpio);
				goto free_gpio_2;
			} else {
				rc = p9415_vbat_en_gpio_init(chip);
				if (rc) {
					pr_err("unable to init vbat_en_gpio:%d\n",	chip->vbat_en_gpio);
					goto free_gpio_3;
				}
			}
		}
		chg_debug("chip->vbat_en_gpio =%d\n", chip->vbat_en_gpio);
	}

	// Parsing gpio booster_en -- not use now.
	chip->booster_en_gpio = of_get_named_gpio(node, "qcom,booster_en-gpio", 0);
	if (chip->booster_en_gpio < 0) {
		pr_err("chip->booster_en_gpio not specified\n");
		//rc = -EINVAL;
		//goto free_gpio_3;
	} else {
		if (gpio_is_valid(chip->booster_en_gpio)) {
			rc = gpio_request(chip->booster_en_gpio,
					  "booster-en-gpio");
			if (rc) {
				pr_err("unable to request gpio [%d]\n",
				       chip->booster_en_gpio);
				goto free_gpio_3;
			} else {
				rc = p9415_booster_en_gpio_init(chip);
				if (rc) {
					pr_err("unable to init booster_en_gpio:%d\n",
						chip->booster_en_gpio);
					goto free_gpio_4;
				}
			}
		}
		chg_debug("chip->booster_en_gpio =%d\n", chip->booster_en_gpio);
	}

	// Parsing gpio idt_en
	chip->idt_en_gpio = of_get_named_gpio(node, "qcom,idt_en-gpio", 0);
	if (chip->idt_en_gpio < 0) {
		pr_err("chip->idt_en_gpio not specified\n");
		rc = -EINVAL;
		goto free_gpio_4;
	} else {
		if (gpio_is_valid(chip->idt_en_gpio)) {
			rc = gpio_request(chip->idt_en_gpio, "idt_en-gpio");
			if (rc) {
				pr_err("unable to request gpio [%d]\n", chip->idt_en_gpio);
				goto free_gpio_4;
			} else {
				rc = p9415_idt_en_gpio_init(chip);
				if (rc) {
					pr_err("unable to init idt_en_gpio:%d\n", chip->idt_en_gpio);
					goto free_gpio_5;
				}
			}
		}
		chg_debug("chip->idt_en_gpio =%d\n", chip->idt_en_gpio);
	}

	return 0;

free_gpio_5:
	if (gpio_is_valid(chip->idt_en_gpio))
		gpio_free(chip->idt_en_gpio);
free_gpio_4:
	if (gpio_is_valid(chip->booster_en_gpio))
		gpio_free(chip->booster_en_gpio);
free_gpio_3:
	if (gpio_is_valid(chip->vbat_en_gpio))
		gpio_free(chip->vbat_en_gpio);
free_gpio_2:
	if (gpio_is_valid(chip->idt_con_gpio))
		gpio_free(chip->idt_con_gpio);
free_gpio_1:
	if (gpio_is_valid(chip->idt_int_gpio))
		gpio_free(chip->idt_int_gpio);
	return rc;
}

static void p9415_update_work_process(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct op_p9415_ic *chip =
		container_of(dwork, struct op_p9415_ic, p9415_update_work);
	int rc = 0;
	static int retrycount;
	int boot_mode = get_boot_mode();

	if (!chip) {
		pr_err("<IDT UPDATE> op_p9415_ic not ready!\n");
		return;
	}

	if (boot_mode == MSM_BOOT_MODE_FACTORY) {
		pr_err("<IDT UPDATE> MSM_BOOT_MODE__FACTORY do not update\n");
		p9415_set_vbat_en_val(chip, 1);
		msleep(500);
		p9415_set_vbat_en_val(chip, 0);
		return;
	}

	pr_err("<IDT UPDATE> p9415_update_work_process\n");

	if (!chip->check_fw_update) {
		if (wlchg_get_usbin_val() == 0) {
			__pm_stay_awake(chip->update_fw_wake_lock);
			rc = p9415_check_idt_fw_update(chip);
			__pm_relax(chip->update_fw_wake_lock);
		} else {
			pr_err("<IDT UPDATE> usb cable is in, retry later.!\n");
			p9415_set_vbat_en_val(chip, 1);
			rc = -1;
		}
		if (rc) {
			/* run again after interval */
			retrycount++;
			schedule_delayed_work(&chip->p9415_update_work, p9415_UPDATE_INTERVAL);
			pr_err("update fw failed, retry %d!", retrycount);
			return;
		}
	}

	if (boot_mode != MSM_BOOT_MODE_CHARGE) {
		p9415_set_vbat_en_val(chip, 1);
		msleep(500);
		p9415_set_vbat_en_val(chip, 0);
	}
}

#ifdef OP_DEBUG
#define UPGRADE_START 0
#define UPGRADE_FW    1
#define UPGRADE_END   2
struct idt_fw_head {
	u8 magic[4];
	int size;
};
static ssize_t p9415_upgrade_firmware_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	u8 temp_buf[sizeof(struct idt_fw_head)];
	int rc = 0;
	static u8 *fw_buf;
	static int upgrade_step = UPGRADE_START;
	static int fw_index;
	static int fw_size;
	struct idt_fw_head *fw_head;
	struct i2c_client *client;
	struct rx_chip *rx_chip;
	struct op_p9415_ic *chip;

	client = container_of(dev, struct i2c_client, dev);
	rx_chip = i2c_get_clientdata(client);
	chip = rx_chip->prop->private_data;

start:
	switch (upgrade_step) {
	case UPGRADE_START:
		if (count < sizeof(struct idt_fw_head)) {
			pr_err("<IDT UPDATE>image format error\n");
			return -EINVAL;
		}
		memset(temp_buf, 0, sizeof(struct idt_fw_head));
		memcpy(temp_buf, buf, sizeof(struct idt_fw_head));
		fw_head = (struct idt_fw_head *)temp_buf;
		if (fw_head->magic[0] == 0x02 && fw_head->magic[1] == 0x00 &&
		    fw_head->magic[2] == 0x03 && fw_head->magic[3] == 0x00) {
			fw_size = fw_head->size;
			fw_buf = kzalloc(fw_size, GFP_KERNEL);
			if (fw_buf == NULL) {
				pr_err("<IDT UPDATE>alloc fw_buf err\n");
				return -ENOMEM;
			}
			pr_err("<IDT UPDATE>image header verification succeeded, fw_size=%d\n", fw_size);
			memcpy(fw_buf, buf + sizeof(struct idt_fw_head), count - sizeof(struct idt_fw_head));
			fw_index = count - sizeof(struct idt_fw_head);
			pr_info("<IDT UPDATE>Receiving image, fw_size=%d, fw_index=%d\n", fw_size, fw_index);
			if (fw_index >= fw_size) {
				upgrade_step = UPGRADE_END;
				goto start;
			} else {
				upgrade_step = UPGRADE_FW;
			}
		} else {
			pr_err("<IDT UPDATE>image format error\n");
			return -EINVAL;
		}
		break;
	case UPGRADE_FW:
		memcpy(fw_buf + fw_index, buf, count);
		fw_index += count;
		pr_info("<IDT UPDATE>Receiving image, fw_size=%d, fw_index=%d\n", fw_size, fw_index);
		if (fw_index >= fw_size) {
			upgrade_step = UPGRADE_END;
			goto start;
		}
		break;
	case UPGRADE_END:
		rc = p9415_upgrade_firmware(chip, fw_buf, fw_size);
		kfree(fw_buf);
		fw_buf = NULL;
		upgrade_step = UPGRADE_START;
		if (rc < 0)
			return rc;
		break;
	default:
		upgrade_step = UPGRADE_START;
		pr_err("<IDT UPDATE>status error\n");
		if (fw_buf != NULL) {
			kfree(fw_buf);
			fw_buf = NULL;
		}
		break;
	}

	return count;
}

static DEVICE_ATTR(upgrade_firmware, S_IWUSR, NULL, p9415_upgrade_firmware_store);

static struct attribute *p9415_sysfs_attrs[] = {
	&dev_attr_upgrade_firmware.attr,
	NULL
};

static struct attribute_group p9415_attribute_group = {
	.attrs = p9415_sysfs_attrs
};

#endif

static int p9415_driver_probe(struct platform_device *pdev)
{
	struct op_p9415_ic *chip;
	int ret = 0;

	chg_debug(" call \n");

	chip = devm_kzalloc(&pdev->dev, sizeof(struct op_p9415_ic),
			    GFP_KERNEL);
	if (!chip) {
		pr_err(" kzalloc() failed\n");
		return -ENOMEM;
	}

	g_p9415_chip = chip;
	chip->dev = &pdev->dev;
	chip->client = container_of(chip->dev->parent, struct i2c_client, dev);
	chip->rx_chip = i2c_get_clientdata(chip->client);

	platform_set_drvdata(pdev, chip);

#ifdef OP_DEBUG
	ret = sysfs_create_group(&chip->dev->parent->kobj, &p9415_attribute_group);
	if (ret)
		goto free_platform_drvdata;
#endif

	ret = p9415_gpio_init(chip);
	if (ret) {
		pr_err("p9415 gpio init error.");
		goto free_sysfs_group;
	}

	device_init_wakeup(chip->dev, true);

	p9415_prop.private_data = chip;
	wlchg_rx_register_prop(chip->dev->parent, &p9415_prop);

	INIT_DELAYED_WORK(&chip->idt_event_int_work, p9415_event_int_func);
	INIT_DELAYED_WORK(&chip->idt_connect_int_work, p9415_connect_int_func);
	INIT_DELAYED_WORK(&chip->p9415_update_work, p9415_update_work_process);
	INIT_DELAYED_WORK(&chip->check_ldo_on_work, p9415_check_ldo_on_func);
	chip->update_fw_wake_lock = wakeup_source_register(chip->dev, "p9415_update_fw_wake_lock");

	if (get_boot_mode() == MSM_BOOT_MODE_CHARGE) {
		schedule_delayed_work(&chip->idt_connect_int_work, msecs_to_jiffies(5000));
	} else {
		schedule_delayed_work(&chip->p9415_update_work, p9415_UPDATE_INTERVAL);
	}

	chg_debug("call end\n");
	return 0;

free_sysfs_group:
#ifdef OP_DEBUG
	sysfs_remove_group(&chip->dev->parent->kobj, &p9415_attribute_group);
free_platform_drvdata:
#endif
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static int p9415_driver_remove(struct platform_device *pdev)
{
	struct op_p9415_ic *chip = platform_get_drvdata(pdev);

	if (gpio_is_valid(chip->idt_en_gpio))
		gpio_free(chip->idt_en_gpio);
	if (gpio_is_valid(chip->booster_en_gpio))
		gpio_free(chip->booster_en_gpio);
	if (gpio_is_valid(chip->vbat_en_gpio))
		gpio_free(chip->vbat_en_gpio);
	if (gpio_is_valid(chip->idt_con_gpio))
		gpio_free(chip->idt_con_gpio);
	if (gpio_is_valid(chip->idt_int_gpio))
		gpio_free(chip->idt_int_gpio);
#ifdef OP_DEBUG
	sysfs_remove_group(&chip->dev->parent->kobj, &p9415_attribute_group);
#endif
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int p9415_pm_resume(struct device *dev)
{
	return 0;
}

static int p9415_pm_suspend(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops p9415_pm_ops = {
	.resume = p9415_pm_resume,
	.suspend = p9415_pm_suspend,
};
#else
static int p9415_resume(struct i2c_client *client)
{
	return 0;
}

static int p9415_suspend(struct i2c_client *client, pm_message_t mesg)
{
	return 0;
}
#endif

static const struct of_device_id p9415_match[] = {
	{ .compatible = "op,p9415-charger" },
	{},
};

static struct platform_driver p9415_driver = {
	.driver = {
		.name = "p9415-charger",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(p9415_match),
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		.pm = &p9415_pm_ops,
#endif
	},
	.probe = p9415_driver_probe,
	.remove = p9415_driver_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	.resume = p9415_resume,
	.suspend = p9415_suspend,
#endif
};

module_platform_driver(p9415_driver);
MODULE_DESCRIPTION("Driver for p9415 charger chip");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("p9415-charger");
