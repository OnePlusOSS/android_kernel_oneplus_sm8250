#define pr_fmt(fmt) "WLCHG: %s: " fmt, __func__

#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/oem/boot_mode.h>
#include <linux/pmic-voter.h>
#include <linux/oem/power/op_wlc_helper.h>
#include "op_chargepump.h"
#include "op_wlchg_rx.h"
#include "op_wlchg_policy.h"
#include "smb5-lib.h"

static struct rx_chip *g_rx_chip;
static struct op_chg_chip *g_op_chip;

extern void exrx_information_register(struct rx_chip *chip);

int wlchg_rx_set_vout(struct rx_chip *chip, int val)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	pr_info("set rx chip vout to %d\n", val);
	pval.intval = val;
	rc = prop->set_prop(prop, RX_PROP_VOUT, &pval);
	if (rc) {
		pr_err("can't set rx chip vout, rc=%d\n", rc);
		return rc;
	}
	chip->chg_data.charge_voltage = val;

	return 0;
}

int wlchg_rx_get_vout(struct rx_chip *chip, int *val)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	rc = prop->get_prop(prop, RX_PROP_VOUT, &pval);
	if (rc) {
		pr_err("can't get rx vout, rc=%d\n", rc);
		*val = 0;
		return rc;
	}

	*val = pval.intval;
	return 0;
}

int wlchg_rx_set_match_q_parm(struct rx_chip *chip, unsigned char data)
{
	struct rx_chip_prop *prop;
	int rc = 0;

	if (chip == NULL) {
		pr_err("rx chip not ready\n");
		return -ENODEV;
	}

	prop = chip->prop;
	if (prop->send_match_q_parm)
		rc = prop->send_match_q_parm(prop, data);

	return rc;
}

int wlchg_rx_set_fod_parm(struct rx_chip *chip, const char data[])
{
	struct rx_chip_prop *prop;
	int rc = 0;

	if (chip == NULL) {
		pr_err("rx chip not ready\n");
		return -ENODEV;
	}

	prop = chip->prop;
	if (prop->set_fod_parm)
		rc = prop->set_fod_parm(prop, data);

	return rc;
}

int wlchg_rx_ftm_test(struct rx_chip *chip)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	rc = prop->get_prop(prop, RX_PROP_FTM_TEST, &pval);
	if (rc)
		return rc;
	return pval.intval;
}

int wlchg_rx_get_vrect_iout(struct rx_chip *chip)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	rc = prop->get_prop(prop, RX_PROP_VOUT, &pval);
	if (rc) {
		pr_err("can't get rx vout, rc=%d\n", rc);
		return rc;
	}
	chip->chg_data.vout = pval.intval;

	rc = prop->get_prop(prop, RX_PROP_VRECT, &pval);
	if (rc) {
		pr_err("can't get rx vrect, rc=%d\n", rc);
		return rc;
	}
	chip->chg_data.vrect = pval.intval;

	rc = prop->get_prop(prop, RX_PROP_IOUT, &pval);
	if (rc) {
		pr_err("can't get rx iout, rc=%d\n", rc);
		return rc;
	}
	chip->chg_data.iout = pval.intval;

	pr_info("vout:%d, vrect=%d, iout=%d\n", chip->chg_data.vout,
		chip->chg_data.vrect, chip->chg_data.iout);

	return 0;
}

int wlchg_rx_get_tx_vol(struct rx_chip *chip, int *val)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	rc = prop->get_prop(prop, RX_PROP_TRX_VOL, &pval);
	if (rc) {
		pr_err("can't get trx voltage, rc=%d\n", rc);
		return rc;
	}
	*val = pval.intval;

	return 0;
}

int wlchg_rx_get_tx_curr(struct rx_chip *chip, int *val)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	rc = prop->get_prop(prop, RX_PROP_TRX_CURR, &pval);
	if (rc) {
		pr_err("can't get trx current, rc=%d\n", rc);
		return rc;
	}
	*val = pval.intval;

	return 0;
}

int wlchg_rx_trx_enbale(struct rx_chip *chip, bool enable)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	pval.intval = enable;
	rc = prop->set_prop(prop, RX_PROP_TRX_ENABLE, &pval);
	if (rc) {
		pr_err("can't %s trx, rc=%d\n", rc, enable ? "enable" : "disable");
		return rc;
	}

	return 0;
}

int wlchg_rx_get_cep(struct rx_chip *chip, signed char *val)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	rc = prop->get_prop(prop, RX_PROP_CEP, &pval);
	if (rc) {
		pr_err("can't get cep, rc=%d\n", rc);
		return rc;
	}
	*val = (signed char)pval.intval;

	return 0;
}

int wlchg_rx_get_cep_skip_check_update(struct rx_chip *chip, signed char *val)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	rc = prop->get_prop(prop, RX_PROP_CEP_SKIP_CHECK_UPDATE, &pval);
	if (rc) {
		pr_err("can't get cep, rc=%d\n", rc);
		return rc;
	}
	*val = (signed char)pval.intval;

	return 0;
}

int wlchg_rx_get_cep_flag(struct rx_chip *chip)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	rc = prop->get_prop(prop, RX_PROP_CEP, &pval);
	if (rc) {
		pr_err("can't get cep, rc=%d\n", rc);
		return rc;
	}

	pr_info("cep = %d\n", pval.intval);
	if (abs(pval.intval) <= 2)
		return 0;

	return -EINVAL;
}

int wlchg_rx_get_cep_flag_skip_check_update(struct rx_chip *chip)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	rc = prop->get_prop(prop, RX_PROP_CEP_SKIP_CHECK_UPDATE, &pval);
	if (rc) {
		pr_err("can't get cep, rc=%d\n", rc);
		return rc;
	}

	pr_info("cep = %d\n", pval.intval);
	if (abs(pval.intval) <= 2)
		return 0;

	return -EINVAL;
}

void wlchg_rx_get_run_flag(struct rx_chip *chip)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	rc = prop->get_prop(prop, RX_PROP_RUN_MODE, &pval);
	if (rc) {
		pr_err("can't get rx run flag, rc=%d\n", rc);
		chip->chg_data.rx_runing_mode = RX_RUNNING_MODE_OTHERS;
		return;
	}
	chip->chg_data.rx_runing_mode = pval.intval;
}

int wlchg_rx_enable_dcdc(struct rx_chip *chip)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	pval.intval = 1;
	rc = prop->set_prop(prop, RX_PROP_ENABLE_DCDC, &pval);
	if (rc) {
		pr_err("can't enable dcdc, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int wlchg_rx_get_headroom(struct rx_chip *chip, int *val)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	rc = prop->get_prop(prop, RX_PROP_HEADROOM, &pval);
	if (rc) {
		pr_err("can't get headroom, rc=%d\n", rc);
		return rc;
	}
	*val = pval.intval;

	return 0;
}

int wlchg_rx_set_headroom(struct rx_chip *chip, int val)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	pval.intval = val;
	rc = prop->get_prop(prop, RX_PROP_HEADROOM, &pval);
	if (rc) {
		pr_err("can't set headroom, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

enum E_RX_MODE wlchg_rx_get_run_mode(struct rx_chip *chip)
{
	if (chip == NULL) {
		chg_err("rx chip not ready, return\n");
		return RX_RUNNING_MODE_BPP;
	}

	return chip->chg_data.rx_runing_mode;
}

int wlchg_rx_get_work_freq(struct rx_chip *chip, int *val)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	rc = prop->get_prop(prop, RX_PROP_WORK_FREQ, &pval);
	if (rc) {
		pr_err("can't get work freq, rc=%d\n", rc);
		return rc;
	}
	*val = pval.intval;

	return 0;
}

int wlchg_rx_set_chip_sleep(int val)
{
	struct rx_chip_prop *prop;
	union rx_chip_propval pval = {0,};
	int rc;

	if (g_rx_chip == NULL) {
		pr_err("rx chip is not ready\n");
		return -ENODEV;
	}

	prop = g_rx_chip->prop;
	pval.intval = val;
	rc = prop->set_prop(prop, RX_PROP_CHIP_SLEEP, &pval);
	if (rc) {
		pr_err("can't set chip sleep, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int wlchg_rx_get_chip_sleep(void)
{
	struct rx_chip_prop *prop;
	union rx_chip_propval pval = {0,};
	int rc;

	if (g_rx_chip == NULL) {
		pr_err("rx chip is not ready\n");
		return 0;
	}

	prop = g_rx_chip->prop;
	rc = prop->get_prop(prop, RX_PROP_CHIP_SLEEP, &pval);
	if (rc) {
		pr_err("can't get chip sleep val, rc=%d\n", rc);
		return 0;
	}

	return pval.intval;
}

int wlchg_rx_set_chip_en(int val)
{
	struct rx_chip_prop *prop;
	union rx_chip_propval pval = {0,};
	int rc;

	if (g_rx_chip == NULL) {
		pr_err("rx chip is not ready\n");
		return -ENODEV;
	}

	prop = g_rx_chip->prop;
	pval.intval = val;
	rc = prop->set_prop(prop, RX_PROP_CHIP_EN, &pval);
	if (rc) {
		pr_err("can't set chip enable, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int wlchg_rx_get_chip_en(void)
{
	struct rx_chip_prop *prop;
	union rx_chip_propval pval = {0,};
	int rc;

	if (g_rx_chip == NULL) {
		pr_err("rx chip is not ready\n");
		return 0;
	}

	prop = g_rx_chip->prop;
	rc = prop->get_prop(prop, RX_PROP_CHIP_EN, &pval);
	if (rc) {
		pr_err("can't get chip sleep enable val, rc=%d\n", rc);
		return 0;
	}

	return pval.intval;
}

int wlchg_rx_get_chip_con(void)
{
	struct rx_chip_prop *prop;
	union rx_chip_propval pval = {0,};
	int rc;

	if (g_rx_chip == NULL) {
		pr_err("rx chip is not ready\n");
		return 0;
	}

	prop = g_rx_chip->prop;
	rc = prop->get_prop(prop, RX_PROP_CHIP_CON, &pval);
	if (rc) {
		pr_err("can't get chip sleep con val, rc=%d\n", rc);
		return 0;
	}

	return pval.intval;
}

bool wlchg_rx_fw_updating(struct rx_chip *chip)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	rc = prop->get_prop(prop, RX_PROP_FW_UPDATING, &pval);
	if (rc) {
		pr_err("can't get fw update status, rc=%d\n", rc);
		return false;
	}

	return (bool)pval.intval;
}

int wlchg_rx_get_idt_rtx_status(struct rx_chip *chip, char *status, char *err)
{
	struct rx_chip_prop *prop = chip->prop;
	union rx_chip_propval pval = {0,};
	int rc;

	rc = prop->get_prop(prop, RX_PROP_TRX_STATUS, &pval);
	if (rc) {
		pr_err("can't get trx status, rc=%d\n", rc);
		return rc;
	}
	*status = (char)pval.intval;

	rc = prop->get_prop(prop, RX_PROP_TRX_ERROR_CODE, &pval);
	if (rc) {
		pr_err("can't get trx err code, rc=%d\n", rc);
		return rc;
	}
	*err = (char)pval.intval;

	return 0;
}

void wlchg_rx_reset_variables(struct rx_chip *chip)
{
	chip->chg_data.charge_voltage = 0;
	chip->chg_data.charge_current = 0;
	chip->chg_data.vrect = 0;
	chip->chg_data.vout = 0;
	chip->chg_data.iout = 0;
	chip->chg_data.rx_runing_mode = RX_RUNNING_MODE_BPP;
	chip->on_op_trx = false;
}

int wlchg_rx_register_prop(struct device *parent, struct rx_chip_prop *chip_prop)
{
	struct i2c_client *client;
	static struct rx_chip *chip;

	client = container_of(parent, struct i2c_client, dev);
	chip = i2c_get_clientdata(client);
	chip->prop = chip_prop;

	return 0;
}

void wlchg_rx_policy_register(struct op_chg_chip *op_wlchg)
{
	if (g_op_chip) {
		g_op_chip = op_wlchg;
		pr_err("multiple ex g_op_chip called\n");
	} else {
		g_op_chip = op_wlchg;
	}
}

static int wlchg_rx_parse_dt(struct rx_chip *chip)
{
	return 0;
}

static struct regmap_config wlchg_rx_regmap_config = {
	.reg_bits	= 16,
	.val_bits	= 8,
	.max_register	= 0xFFFF,
};

static int wlchg_rx_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct rx_chip *chip;
	int rc = 0;

	chip = devm_kzalloc(&client->dev, sizeof(struct rx_chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	g_rx_chip = chip;
	chip->dev = &client->dev;
	chip->regmap = devm_regmap_init_i2c(client, &wlchg_rx_regmap_config);
	if (!chip->regmap)
		return -ENODEV;

	i2c_set_clientdata(client, chip);
	exrx_information_register(chip);

	rc = wlchg_rx_parse_dt(chip);
	if (rc < 0) {
		pr_err("Couldn't parse device tree rc=%d\n", rc);
		goto cleanup;
	}

	of_platform_populate(chip->dev->of_node, NULL, NULL, chip->dev);
	pr_info("wlchg rx probe successful\n");
	return rc;

cleanup:
	i2c_set_clientdata(client, NULL);
	return rc;
}

static int wlchg_rx_remove(struct i2c_client *client)
{
	struct rx_chip *chip = i2c_get_clientdata(client);

	of_platform_depopulate(chip->dev);
	i2c_set_clientdata(client, NULL);
	return 0;
}

static int wlchg_rx_suspend(struct device *dev)
{
	return 0;
}
static int wlchg_rx_resume(struct device *dev)
{
	return 0;
}
static int wlchg_rx_suspend_noirq(struct device *dev)
{
	return 0;
}

static void wlchg_rx_reset(struct i2c_client *client)
{
	struct rx_chip *chip = i2c_get_clientdata(client);
	struct rx_chip_prop *prop = chip->prop;

	prop->rx_reset(prop);

	return;
}

static const struct dev_pm_ops wlchg_rx_pm_ops = {
	.suspend	= wlchg_rx_suspend,
	.suspend_noirq	= wlchg_rx_suspend_noirq,
	.resume		= wlchg_rx_resume,
};

static const struct of_device_id wlchg_rx_match_table[] = {
	{ .compatible = "op,wlchg-rx-chip", },
	{ },
};

static const struct i2c_device_id wlchg_rx_id[] = {
	{ "i2c-wlchg-rx", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, wlchg_rx_id);

static struct i2c_driver wlchg_rx_driver = {
	.driver		= {
		.name		= "wlchg_rx",
		.pm		= &wlchg_rx_pm_ops,
		.of_match_table	= wlchg_rx_match_table,
	},
	.probe		= wlchg_rx_probe,
	.remove		= wlchg_rx_remove,
	.shutdown	= wlchg_rx_reset,
	.id_table	= wlchg_rx_id,
};

module_i2c_driver(wlchg_rx_driver);

MODULE_LICENSE("GPL v2");