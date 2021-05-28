#define pr_fmt(fmt) "WLCHG: %s: " fmt, __func__

#include <linux/interrupt.h>
#include <linux/platform_device.h>
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
#include <linux/iio/consumer.h>
#include <linux/pmic-voter.h>
#include <linux/notifier.h>
#include <linux/oem/power/op_wlc_helper.h>
#include "op_chargepump.h"
#include "bq2597x_charger.h"
#include "op_wlchg_rx.h"
#include "op_wlchg_policy.h"

#include "smb5-lib.h"

#define JEITA_VOTER       "JEITA_VOTER"
#define STEP_VOTER        "STEP_VOTER"
#define USER_VOTER        "USER_VOTER"
#define DEF_VOTER         "DEF_VOTER"
#define MAX_VOTER         "MAX_VOTER"
#define EXIT_VOTER        "EXIT_VOTER"
#define FFC_VOTER         "FFC_VOTER"
#define CEP_VOTER         "CEP_VOTER"
#define QUIET_VOTER       "QUIET_VOTER"
#define BATT_VOL_VOTER    "BATT_VOL_VOTER"
#define BATT_CURR_VOTER   "BATT_CURR_VOTER"
#define SKIN_VOTER        "SKIN_VOTER"
#define STARTUP_CEP_VOTER "STARTUP_CEP_VOTER"
#define HW_ERR_VOTER      "HW_ERR_VOTER"
#define CURR_ERR_VOTER    "CURR_ERR_VOTER"

// pmic fcc vote
#define WLCH_VOTER      "WLCH_VOTER"
#define WLCH_SKIN_VOTER "WLCH_SKIN_VOTER"

#define BATT_TEMP_HYST 20

static struct op_chg_chip *g_op_chip;
static struct external_battery_gauge *exfg_instance;
static struct bq2597x *exchgpump_bq;
static struct rx_chip *g_rx_chip;
struct smb_charger *normal_charger;
static int reverse_charge_status = 0;
static int wpc_chg_quit_max_cnt;
/*
 * Determine whether to delay the disappearance of the charging
 * icon when charging is disconnected.
 */
static int chg_icon_update_delay = true;

#ifdef OP_DEBUG
static bool force_epp;
static bool force_bpp;
static int proc_charge_pump_status;
static bool auto_mode = true;
#endif
static BLOCKING_NOTIFIER_HEAD(reverse_charge_chain);

static bool enable_deviated_check;
module_param_named(
	enable_deviated_check, enable_deviated_check, bool, 0600
);

void op_set_wrx_en_value(int value);
void op_set_wrx_otg_value(int value);
void op_set_dcdc_en_value(int value);
int wlchg_get_usbin_val(void);
static void update_wlchg_started(bool enabled);
static int wlchg_disable_batt_charge(struct op_chg_chip *chip, bool en);

extern int bq2597x_get_adc_data(struct bq2597x *bq, int channel, int *result);
extern int bq2597x_enable_adc(struct bq2597x *bq, bool enable);
static int reverse_charge_notifier_call_chain(unsigned long val);


/*----For FCC/jeita----------------------------------------*/
#define FCC_DEFAULT 200000

static int read_range_data_from_node(struct device_node *node,
		const char *prop_str, struct op_range_data *ranges,
		int max_threshold, u32 max_value)
{
	int rc = 0, i, length, per_tuple_length, tuples;

	if (!node || !prop_str || !ranges) {
		pr_err("Invalid parameters passed\n");
		return -EINVAL;
	}

	rc = of_property_count_elems_of_size(node, prop_str, sizeof(u32));
	if (rc < 0) {
		pr_err("Count %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	length = rc;
	per_tuple_length = sizeof(struct op_range_data) / sizeof(u32);
	if (length % per_tuple_length) {
		pr_err("%s length (%d) should be multiple of %d\n",
				prop_str, length, per_tuple_length);
		return -EINVAL;
	}
	tuples = length / per_tuple_length;

	if (tuples > MAX_STEP_CHG_ENTRIES) {
		pr_err("too many entries(%d), only %d allowed\n",
				tuples, MAX_STEP_CHG_ENTRIES);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(node, prop_str,
			(u32 *)ranges, length);
	if (rc) {
		pr_err("Read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	for (i = 0; i < tuples; i++) {
		if (ranges[i].low_threshold >
				ranges[i].high_threshold) {
			pr_err("%s thresholds should be in ascendant ranges\n",
						prop_str);
			rc = -EINVAL;
			goto clean;
		}

		if (ranges[i].low_threshold > max_threshold)
			ranges[i].low_threshold = max_threshold;
		if (ranges[i].high_threshold > max_threshold)
			ranges[i].high_threshold = max_threshold;
		if (ranges[i].curr_ua > max_value)
			ranges[i].curr_ua = max_value;
	}

	return tuples;
clean:
	memset(ranges, 0, tuples * sizeof(struct op_range_data));
	return rc;
}

static int read_temp_region_data_from_node(struct device_node *node,
		const char *prop_str, int *addr)
{
	int rc = 0, length;

	if (!node || !prop_str || !addr) {
		pr_err("Invalid parameters passed\n");
		return -EINVAL;
	}

	rc = of_property_count_elems_of_size(node, prop_str, sizeof(u32));
	if (rc < 0) {
		pr_err("Count %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	length = rc;

	if (length > WLCHG_TEMP_REGION_MAX) {
		pr_err("too many entries(%d), only %d allowed\n",
				length, WLCHG_TEMP_REGION_MAX);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(node, prop_str,
			(u32 *)addr, length);
	if (rc) {
		pr_err("Read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	return rc;
}

static int read_data_from_node(struct device_node *node,
		const char *prop_str, int *addr, int len)
{
	int rc = 0, length;

	if (!node || !prop_str || !addr) {
		pr_err("Invalid parameters passed\n");
		return -EINVAL;
	}

	rc = of_property_count_elems_of_size(node, prop_str, sizeof(u32));
	if (rc < 0) {
		pr_err("Count %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	length = rc;

	if (length > len) {
		pr_err("too many entries(%d), only %d allowed\n", length, len);
		length = len;
	}

	rc = of_property_read_u32_array(node, prop_str,
			(u32 *)addr, length);
	if (rc) {
		pr_err("Read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	return rc;
}

static int wireless_chg_init(struct op_chg_chip *chip)
{
	struct device_node *node = chip->dev->of_node;
	struct charge_param *chg_param = &chip->chg_param;
	struct op_fastchg_ffc_step *ffc_chg = &chip->chg_param.ffc_chg;
	int i;
	int rc;

	rc = of_property_read_u32(node, "op,max-voltage-mv", &chg_param->batt_vol_max);
	if (rc < 0) {
		pr_err("max-voltage_uv reading failed, rc=%d\n", rc);
		chg_param->batt_vol_max = 4550;
	}

	rc = of_property_read_u32(node, "op,fastchg-curr-max-ma", &chg_param->fastchg_curr_max);
	if (rc < 0) {
		pr_err("fastchg-curr-max-ma reading failed, rc=%d\n", rc);
		chg_param->fastchg_curr_max = 1500;
	}

	rc = of_property_read_u32(node, "op,fastchg-curr-min-ma", &chg_param->fastchg_curr_min);
	if (rc < 0) {
		pr_err("fastchg-curr-min-ma reading failed, rc=%d\n", rc);
		chg_param->fastchg_curr_min = 400;
	}

	rc = of_property_read_u32(node, "op,fastchg-vol-entry-max-mv", &chg_param->fastchg_vol_entry_max);
	if (rc < 0) {
		pr_err("fastchg-vol-entry-max-mv reading failed, rc=%d\n", rc);
		chg_param->fastchg_vol_entry_max = 4380;
	}

	rc = of_property_read_u32(node, "op,fastchg-vol-normal-max-mv", &chg_param->fastchg_vol_normal_max);
	if (rc < 0) {
		pr_err("fastchg-vol-normal-max-mv reading failed, rc=%d\n", rc);
		chg_param->fastchg_vol_normal_max = 4480;
	}

	rc = of_property_read_u32(node, "op,fastchg-vol-hot-max-mv", &chg_param->fastchg_vol_hot_max);
	if (rc < 0) {
		pr_err("fastchg-vol-hot-max-mv reading failed, rc=%d\n", rc);
		chg_param->fastchg_vol_hot_max = 4130;
	}

	rc = of_property_read_u32(node, "op,fastchg-vol-min-mv", &chg_param->fastchg_vol_min);
	if (rc < 0) {
		pr_err("fastchg-vol-min-mv reading failed, rc=%d\n", rc);
		chg_param->fastchg_vol_min = 3300;
	}

	rc = of_property_read_u32(node, "op,fastchg-temp-max", &chg_param->fastchg_temp_max);
	if (rc < 0) {
		pr_err("fastchg-temp-max reading failed, rc=%d\n", rc);
		chg_param->fastchg_temp_max = 440;
	}

	rc = of_property_read_u32(node, "op,fastchg-temp-min", &chg_param->fastchg_temp_min);
	if (rc < 0) {
		pr_err("fastchg-temp-min reading failed, rc=%d\n", rc);
		chg_param->fastchg_temp_min = 120;
	}

	rc = of_property_read_u32(node, "op,fastchg-soc-max", &chg_param->fastchg_soc_max);
	if (rc < 0) {
		pr_err("fastchg-soc-max reading failed, rc=%d\n", rc);
		chg_param->fastchg_soc_max = 85;
	}

	rc = of_property_read_u32(node, "op,fastchg-soc-min", &chg_param->fastchg_soc_min);
	if (rc < 0) {
		pr_err("fastchg-soc-min reading failed, rc=%d\n", rc);
		chg_param->fastchg_soc_min = 0;
	}

	rc = of_property_read_u32(node, "op,fastchg-soc-mid", &chg_param->fastchg_soc_mid);
	if (rc < 0) {
		pr_err("fastchg-soc-mid reading failed, rc=%d\n", rc);
		chg_param->fastchg_soc_mid = 75;
	}

	rc = of_property_read_u32(node, "op,fastchg-discharge-curr-max", &chg_param->fastchg_discharge_curr_max);
	if (rc < 0) {
		pr_err("fastchg-discharge-curr-max reading failed, rc=%d\n", rc);
		chg_param->fastchg_discharge_curr_max = 2000;
	}

	rc = of_property_read_u32(node, "cold-bat-decidegc", &chg_param->BATT_TEMP_T0);
	if (rc < 0) {
		pr_err("cold-bat-decidegc reading failed, rc=%d\n", rc);
		chg_param->BATT_TEMP_T0 = -20;
	} else {
		chg_param->BATT_TEMP_T0 = 0 - chg_param->BATT_TEMP_T0;
	}

	rc = of_property_read_u32(node, "little-cold-bat-decidegc", &chg_param->BATT_TEMP_T1);
	if (rc < 0) {
		pr_err("little-cold-bat-decidegc reading failed, rc=%d\n", rc);
		chg_param->BATT_TEMP_T1 = 0;
	}

	rc = of_property_read_u32(node, "cool-bat-decidegc", &chg_param->BATT_TEMP_T2);
	if (rc < 0) {
		pr_err("cool-bat-decidegc reading failed, rc=%d\n", rc);
		chg_param->BATT_TEMP_T2 = 50;
	}

	rc = of_property_read_u32(node, "little-cool-bat-decidegc", &chg_param->BATT_TEMP_T3);
	if (rc < 0) {
		pr_err("little-cool-bat-decidegc reading failed, rc=%d\n", rc);
		chg_param->BATT_TEMP_T3 = 120;
	}

	rc = of_property_read_u32(node, "pre-normal-bat-decidegc", &chg_param->BATT_TEMP_T4);
	if (rc < 0) {
		pr_err("pre-normal-bat-decidegc reading failed, rc=%d\n", rc);
		chg_param->BATT_TEMP_T4 = 160;
	}

	rc = of_property_read_u32(node, "warm-bat-decidegc", &chg_param->BATT_TEMP_T5);
	if (rc < 0) {
		pr_err("warm-bat-decidegc reading failed, rc=%d\n", rc);
		chg_param->BATT_TEMP_T5 = 440;
	}

	rc = of_property_read_u32(node, "hot-bat-decidegc", &chg_param->BATT_TEMP_T6);
	if (rc < 0) {
		pr_err("hot-bat-decidegc reading failed, rc=%d\n", rc);
		chg_param->BATT_TEMP_T6 = 500;
	}

	chg_info("temp region: %d, %d, %d, %d, %d, %d, %d",
		 chg_param->BATT_TEMP_T0, chg_param->BATT_TEMP_T1,
		 chg_param->BATT_TEMP_T2, chg_param->BATT_TEMP_T3,
		 chg_param->BATT_TEMP_T4, chg_param->BATT_TEMP_T5,
		 chg_param->BATT_TEMP_T6);

	rc = read_temp_region_data_from_node(node, "op,epp-ibatmax-ma", chg_param->epp_ibatmax);
	if (rc < 0) {
		pr_err("Read op,epp-ibatmax-ma failed, rc=%d\n", rc);
		chg_param->epp_ibatmax[WLCHG_BATT_TEMP_COLD] = 0;
		chg_param->epp_ibatmax[WLCHG_BATT_TEMP_LITTLE_COLD] = 1000;
		chg_param->epp_ibatmax[WLCHG_BATT_TEMP_COOL] = 2500;
		chg_param->epp_ibatmax[WLCHG_BATT_TEMP_LITTLE_COOL] = 2500;
		chg_param->epp_ibatmax[WLCHG_BATT_TEMP_PRE_NORMAL] = 2500;
		chg_param->epp_ibatmax[WLCHG_BATT_TEMP_NORMAL] = 2500;
		chg_param->epp_ibatmax[WLCHG_BATT_TEMP_WARM] = 1500;
		chg_param->epp_ibatmax[WLCHG_BATT_TEMP_HOT] = 0;
	}
	chg_info("ibatmax-epp: %d, %d, %d, %d, %d, %d, %d, %d",
		 chg_param->epp_ibatmax[WLCHG_BATT_TEMP_COLD],
		 chg_param->epp_ibatmax[WLCHG_BATT_TEMP_LITTLE_COLD],
		 chg_param->epp_ibatmax[WLCHG_BATT_TEMP_COOL],
		 chg_param->epp_ibatmax[WLCHG_BATT_TEMP_LITTLE_COOL],
		 chg_param->epp_ibatmax[WLCHG_BATT_TEMP_PRE_NORMAL],
		 chg_param->epp_ibatmax[WLCHG_BATT_TEMP_NORMAL],
		 chg_param->epp_ibatmax[WLCHG_BATT_TEMP_WARM],
		 chg_param->epp_ibatmax[WLCHG_BATT_TEMP_HOT]);

	rc = read_temp_region_data_from_node(node, "op,bpp-ibatmax-ma", chg_param->bpp_ibatmax);
	if (rc < 0) {
		pr_err("Read op,bpp-ibatmax-ma failed, rc=%d\n", rc);
		chg_param->bpp_ibatmax[WLCHG_BATT_TEMP_COLD] = 0;
		chg_param->bpp_ibatmax[WLCHG_BATT_TEMP_LITTLE_COLD] = 1000;
		chg_param->bpp_ibatmax[WLCHG_BATT_TEMP_COOL] = 1500;
		chg_param->bpp_ibatmax[WLCHG_BATT_TEMP_LITTLE_COOL] = 1500;
		chg_param->bpp_ibatmax[WLCHG_BATT_TEMP_PRE_NORMAL] = 1500;
		chg_param->bpp_ibatmax[WLCHG_BATT_TEMP_NORMAL] = 1500;
		chg_param->bpp_ibatmax[WLCHG_BATT_TEMP_WARM] = 1500;
		chg_param->bpp_ibatmax[WLCHG_BATT_TEMP_HOT] = 0;
	}
	chg_info("ibatmax-bpp: %d, %d, %d, %d, %d, %d, %d, %d",
		 chg_param->bpp_ibatmax[WLCHG_BATT_TEMP_COLD],
		 chg_param->bpp_ibatmax[WLCHG_BATT_TEMP_LITTLE_COLD],
		 chg_param->bpp_ibatmax[WLCHG_BATT_TEMP_COOL],
		 chg_param->bpp_ibatmax[WLCHG_BATT_TEMP_LITTLE_COOL],
		 chg_param->bpp_ibatmax[WLCHG_BATT_TEMP_PRE_NORMAL],
		 chg_param->bpp_ibatmax[WLCHG_BATT_TEMP_NORMAL],
		 chg_param->bpp_ibatmax[WLCHG_BATT_TEMP_WARM],
		 chg_param->bpp_ibatmax[WLCHG_BATT_TEMP_HOT]);

	rc = read_temp_region_data_from_node(node, "op,epp-iclmax-ma", chg_param->epp_iclmax);
	if (rc < 0) {
		pr_err("Read op,epp-iclmax-ma failed, rc=%d\n", rc);
		chg_param->epp_iclmax[WLCHG_BATT_TEMP_COLD] = 0;
		chg_param->epp_iclmax[WLCHG_BATT_TEMP_LITTLE_COLD] = 300;
		chg_param->epp_iclmax[WLCHG_BATT_TEMP_COOL] = 1100;
		chg_param->epp_iclmax[WLCHG_BATT_TEMP_LITTLE_COOL] = 1100;
		chg_param->epp_iclmax[WLCHG_BATT_TEMP_PRE_NORMAL] = 1100;
		chg_param->epp_iclmax[WLCHG_BATT_TEMP_NORMAL] = 1100;
		chg_param->epp_iclmax[WLCHG_BATT_TEMP_WARM] = 650;
		chg_param->epp_iclmax[WLCHG_BATT_TEMP_HOT] = 0;
	}
	chg_info("iclmax-epp: %d, %d, %d, %d, %d, %d, %d, %d",
		 chg_param->epp_iclmax[WLCHG_BATT_TEMP_COLD],
		 chg_param->epp_iclmax[WLCHG_BATT_TEMP_LITTLE_COLD],
		 chg_param->epp_iclmax[WLCHG_BATT_TEMP_COOL],
		 chg_param->epp_iclmax[WLCHG_BATT_TEMP_LITTLE_COOL],
		 chg_param->epp_iclmax[WLCHG_BATT_TEMP_PRE_NORMAL],
		 chg_param->epp_iclmax[WLCHG_BATT_TEMP_NORMAL],
		 chg_param->epp_iclmax[WLCHG_BATT_TEMP_WARM],
		 chg_param->epp_iclmax[WLCHG_BATT_TEMP_HOT]);

	rc = read_temp_region_data_from_node(node, "op,bpp-iclmax-ma", chg_param->bpp_iclmax);
	if (rc < 0) {
		pr_err("Read op,bpp-iclmax-ma failed, rc=%d\n", rc);
		chg_param->bpp_iclmax[WLCHG_BATT_TEMP_COLD] = 0;
		chg_param->bpp_iclmax[WLCHG_BATT_TEMP_LITTLE_COLD] = 500;
		chg_param->bpp_iclmax[WLCHG_BATT_TEMP_COOL] = 1000;
		chg_param->bpp_iclmax[WLCHG_BATT_TEMP_LITTLE_COOL] = 1000;
		chg_param->bpp_iclmax[WLCHG_BATT_TEMP_PRE_NORMAL] = 1000;
		chg_param->bpp_iclmax[WLCHG_BATT_TEMP_NORMAL] = 1000;
		chg_param->bpp_iclmax[WLCHG_BATT_TEMP_WARM] = 1000;
		chg_param->bpp_iclmax[WLCHG_BATT_TEMP_HOT] = 0;
	}
	chg_info("iclmax-bpp: %d, %d, %d, %d, %d, %d, %d, %d",
		 chg_param->bpp_iclmax[WLCHG_BATT_TEMP_COLD],
		 chg_param->bpp_iclmax[WLCHG_BATT_TEMP_LITTLE_COLD],
		 chg_param->bpp_iclmax[WLCHG_BATT_TEMP_COOL],
		 chg_param->bpp_iclmax[WLCHG_BATT_TEMP_LITTLE_COOL],
		 chg_param->bpp_iclmax[WLCHG_BATT_TEMP_PRE_NORMAL],
		 chg_param->bpp_iclmax[WLCHG_BATT_TEMP_NORMAL],
		 chg_param->bpp_iclmax[WLCHG_BATT_TEMP_WARM],
		 chg_param->bpp_iclmax[WLCHG_BATT_TEMP_HOT]);

	rc = read_temp_region_data_from_node(node, "vbatdet-mv", chg_param->vbatdet);
	if (rc < 0) {
		pr_err("Read vbatdet-mv failed, rc=%d\n", rc);
		chg_param->vbatdet[WLCHG_BATT_TEMP_COLD] = 0;
		chg_param->vbatdet[WLCHG_BATT_TEMP_LITTLE_COLD] = 3675;
		chg_param->vbatdet[WLCHG_BATT_TEMP_COOL] = 4235;
		chg_param->vbatdet[WLCHG_BATT_TEMP_LITTLE_COOL] = 4370;
		chg_param->vbatdet[WLCHG_BATT_TEMP_PRE_NORMAL] = 4370;
		chg_param->vbatdet[WLCHG_BATT_TEMP_NORMAL] = 4370;
		chg_param->vbatdet[WLCHG_BATT_TEMP_WARM] = 4030;
		chg_param->vbatdet[WLCHG_BATT_TEMP_HOT] = 0;
	}

	rc = read_temp_region_data_from_node(node, "op,fastchg-ibatmax-ma", chg_param->fastchg_ibatmax);
	if (rc < 0) {
		pr_err("Read op,fastchg-ibatmax-ma failed, rc=%d\n", rc);
		chg_param->fastchg_ibatmax[0] = 4000;
		chg_param->fastchg_ibatmax[1] = 6000;
	}

	rc = of_property_read_u32(node, "op,rx-freq-threshold", &chg_param->freq_threshold);
	if (rc < 0) {
		pr_err("op,rx-freq-threshold reading failed, rc=%d\n", rc);
		chg_param->freq_threshold = 130;
	}

	rc = of_property_read_u32(node, "cool-vbat-thr-mv", &chg_param->cool_vbat_thr_mv);
	if (rc < 0) {
		pr_err("cool-vbat-thr-mv reading failed, rc=%d\n", rc);
		chg_param->cool_vbat_thr_mv = 4180;
	}

	rc = of_property_read_u32(node, "cool-epp-ibat-ma", &chg_param->cool_epp_ibat_ma);
	if (rc < 0) {
		pr_err("cool-epp-ibat-ma reading failed, rc=%d\n", rc);
		chg_param->cool_epp_ibat_ma = 1500;
	}

	rc = of_property_read_u32(node, "cool-epp-icl-ma", &chg_param->cool_epp_icl_ma);
	if (rc < 0) {
		pr_err("cool-epp-icl-ma reading failed, rc=%d\n", rc);
		chg_param->cool_epp_icl_ma = 650;
	}

	rc = of_property_read_u32(node, "fastchg-skin-temp-max", &chg_param->fastchg_skin_temp_max);
	if (rc < 0) {
		pr_err("fastchg-skin-temp-max reading failed, rc=%d\n", rc);
		chg_param->fastchg_skin_temp_max = 420;
	}

	rc = of_property_read_u32(node, "fastchg-skin-temp-min", &chg_param->fastchg_skin_temp_min);
	if (rc < 0) {
		pr_err("fastchg-skin-temp-min reading failed, rc=%d\n", rc);
		chg_param->fastchg_skin_temp_min = 400;
	}

	rc = of_property_read_u32(node, "epp-skin-temp-max", &chg_param->epp_skin_temp_max);
	if (rc < 0) {
		pr_err("epp-skin-temp-max reading failed, rc=%d\n", rc);
		chg_param->epp_skin_temp_max = 390;
	}

	rc = of_property_read_u32(node, "epp-skin-temp-min", &chg_param->epp_skin_temp_min);
	if (rc < 0) {
		pr_err("epp-skin-temp-min reading failed, rc=%d\n", rc);
		chg_param->epp_skin_temp_min = 370;
	}

	rc = read_data_from_node(node, "op,epp-curr-step",
				chg_param->epp_curr_step, EPP_CURR_STEP_MAX);
	if (rc < 0) {
		pr_err("Read op,epp-curr-step failed, rc=%d\n", rc);
		chg_param->epp_curr_step[0] = 1100;  // 10W
		chg_param->epp_curr_step[1] = 550;   // 5W
	}

	chg_param->fastchg_fod_enable = of_property_read_bool(node, "op,fastchg-fod-enable");
	if (chg_param->fastchg_fod_enable) {
		rc = of_property_read_u8(node, "op,fastchg-match-q-new",
			&chg_param->fastchg_match_q_new);
		if (rc < 0) {
			pr_err("op,fastchg-match-q-new reading failed, rc=%d\n", rc);
			chg_param->fastchg_match_q_new = 0x56;
		}

		rc = of_property_read_u8(node, "op,fastchg-match-q",
			&chg_param->fastchg_match_q);
		if (rc < 0) {
			pr_err("op,fastchg-match-q reading failed, rc=%d\n", rc);
			chg_param->fastchg_match_q = 0x44;
		}

		rc = of_property_read_u8_array(node, "op,fastchg-fod-parm-new",
			(u8 *)&chg_param->fastchg_fod_parm_new, FOD_PARM_LENGTH);
		if (rc < 0) {
			chg_param->fastchg_fod_enable = false;
			pr_err("Read op,fastchg-fod-parm-new failed, rc=%d\n", rc);
		}

		rc = of_property_read_u8_array(node, "op,fastchg-fod-parm",
			(u8 *)&chg_param->fastchg_fod_parm, FOD_PARM_LENGTH);
		if (rc < 0) {
			chg_param->fastchg_fod_enable = false;
			pr_err("Read op,fastchg-fod-parm failed, rc=%d\n", rc);
		}

		rc = of_property_read_u8_array(node, "op,fastchg-fod-parm-startup",
			(u8 *)&chg_param->fastchg_fod_parm_startup, FOD_PARM_LENGTH);
		if (rc < 0) {
			pr_err("Read op,fastchg-fod-parm failed, rc=%d\n", rc);
			for (i = 0; i < FOD_PARM_LENGTH; i++)
				chg_param->fastchg_fod_parm_startup[i] =
					chg_param->fastchg_fod_parm[i];
		}
	}

	rc = read_range_data_from_node(node, "op,fastchg-ffc_step",
				       chg_param->ffc_chg.ffc_step,
				       chg_param->BATT_TEMP_T5,
				       chg_param->fastchg_curr_max * 1000);
	if (rc < 0) {
		pr_err("Read op,fastchg-ffc_step failed, rc=%d\n", rc);
		ffc_chg->ffc_step[0].low_threshold = 0;
		ffc_chg->ffc_step[0].high_threshold = 405;
		ffc_chg->ffc_step[0].curr_ua = 1500000;
		ffc_chg->ffc_step[0].vol_max_mv = 4420;
		ffc_chg->ffc_step[0].need_wait = 1;

		ffc_chg->ffc_step[1].low_threshold = 380;
		ffc_chg->ffc_step[1].high_threshold = 420;
		ffc_chg->ffc_step[1].curr_ua = 1000000;
		ffc_chg->ffc_step[1].vol_max_mv = 4450;
		ffc_chg->ffc_step[1].need_wait = 1;

		ffc_chg->ffc_step[2].low_threshold = 390;
		ffc_chg->ffc_step[2].high_threshold = 420;
		ffc_chg->ffc_step[2].curr_ua = 850000;
		ffc_chg->ffc_step[2].vol_max_mv = 4480;
		ffc_chg->ffc_step[2].need_wait = 1;

		ffc_chg->ffc_step[3].low_threshold = 400;
		ffc_chg->ffc_step[3].high_threshold = 420;
		ffc_chg->ffc_step[3].curr_ua =625000;
		ffc_chg->ffc_step[3].vol_max_mv = 4480;
		ffc_chg->ffc_step[3].need_wait = 0;
		ffc_chg->max_step = 4;
	} else {
		ffc_chg->max_step = rc;
	}
	for(i = 0; i < ffc_chg->max_step; i++) {
		if (ffc_chg->ffc_step[i].low_threshold > 0)
			ffc_chg->allow_fallback[i] = true;
		else
			ffc_chg->allow_fallback[i] = false;
	}

	return 0;
}
/*----For FCC/jeita-------------------end-----------------------------------------*/
static void wlchg_set_rx_target_voltage(struct op_chg_chip *chip, int vol)
{
	if (chip->wlchg_status.adapter_type == ADAPTER_TYPE_UNKNOWN)
		return;

	mutex_lock(&chip->chg_lock);
	chip->wlchg_status.curr_limit_mode = false;
	chip->wlchg_status.vol_set_ok = false;
	chip->wlchg_status.vol_set_start = true;
	if (vol > RX_VOLTAGE_MAX) {
		chip->wlchg_status.target_vol = RX_VOLTAGE_MAX;
		goto out;
	}
	if (chip->wlchg_status.charge_type == WPC_CHARGE_TYPE_FAST) {
		if (vol < FASTCHG_MODE_VOL_MIN) {
			chip->wlchg_status.target_vol = FASTCHG_MODE_VOL_MIN;
			goto out;
		}
	} else {
		if (vol < NORMAL_MODE_VOL_MIN) {
			chip->wlchg_status.target_vol = NORMAL_MODE_VOL_MIN;
			goto out;
		}
	}
	chip->wlchg_status.target_vol = vol;
out:
	chip->wlchg_status.charge_voltage = chip->wlchg_status.target_vol;
	mutex_unlock(&chip->chg_lock);
	chg_err("set targte_vol to %d\n", chip->wlchg_status.target_vol);
	schedule_delayed_work(&chip->fastchg_curr_vol_work, 0);
}

/*
 * Set vout to the target voltage immediately, no need to set in fastchg_curr_vol_work.
 */
static void wlchg_set_rx_target_voltage_fast(struct op_chg_chip *chip, int vol)
{
	if (chip->wlchg_status.adapter_type == ADAPTER_TYPE_UNKNOWN)
		return;

	mutex_lock(&chip->chg_lock);
	chip->wlchg_status.curr_limit_mode = false;
	chip->wlchg_status.vol_set_ok = false;
	chip->wlchg_status.vol_set_start = true;
	chip->wlchg_status.vol_set_fast = true;
	if (vol > RX_VOLTAGE_MAX) {
		chip->wlchg_status.target_vol = RX_VOLTAGE_MAX;
		goto out;
	}
	if (chip->wlchg_status.charge_type == WPC_CHARGE_TYPE_FAST) {
		if (vol < FASTCHG_MODE_VOL_MIN) {
			chip->wlchg_status.target_vol = FASTCHG_MODE_VOL_MIN;
			goto out;
		}
	} else {
		if (vol < NORMAL_MODE_VOL_MIN) {
			chip->wlchg_status.target_vol = NORMAL_MODE_VOL_MIN;
			goto out;
		}
	}
	chip->wlchg_status.target_vol = vol;
out:
	chip->wlchg_status.charge_voltage = chip->wlchg_status.target_vol;
	chip->wlchg_status.vol_set = chip->wlchg_status.target_vol;
	wlchg_rx_set_vout(g_rx_chip, chip->wlchg_status.vol_set);
	mutex_unlock(&chip->chg_lock);
	chg_err("set targte_vol to %d\n", chip->wlchg_status.target_vol);
	schedule_delayed_work(&chip->fastchg_curr_vol_work, 0);
}

static int pmic_set_icl_current(int chg_current)
{
	if (normal_charger != NULL) {
		chg_err("set usb_icl vote to %d mA\n", chg_current);
		vote(normal_charger->usb_icl_votable, WIRED_CONN_VOTER, true,
		     chg_current * 1000);
		return 0;
	}

	return -EINVAL;
}

void notify_pd_in_to_wireless(void)
{
	chg_info("PD adapter in.");
	if (!g_op_chip) {
		chg_err("<~WPC~> g_op_chip is NULL!\n");
		return;
	}
	g_op_chip->pd_charger_online = true;
}

static int wlchg_set_rx_charge_current(struct op_chg_chip *chip,
				       int chg_current)
{
	if (chip != NULL && normal_charger != NULL) {
		chg_err("<~WPC~> set charge current: %d\n", chg_current);
		chip->wlchg_status.charge_current = chg_current;
		cancel_delayed_work_sync(&chip->wlchg_fcc_stepper_work);
		if (pmic_set_icl_current(chg_current) != 0)
			return -EINVAL;
		return 0;
	} else {
		return -EINVAL;
	}
}

static int wlchg_set_rx_charge_current_step(struct op_chg_chip *chip,
					    int chg_current)
{
	if (chip != NULL && normal_charger != NULL) {
		chg_err("<~WPC~> set charge current: %d\n", chg_current);
		chip->wlchg_status.charge_current = chg_current;
		cancel_delayed_work_sync(&chip->wlchg_fcc_stepper_work);
		schedule_delayed_work(&chip->wlchg_fcc_stepper_work, 0);
		return 0;
	} else {
		return -EINVAL;
	}
}

static int wlch_fcc_vote_callback(struct votable *votable, void *data,
				  int icl_ua, const char *client)
{
	struct op_chg_chip *chip = data;
	struct wpc_data *chg_status = &chip->wlchg_status;

	if (icl_ua < 0)
		return 0;

	if (icl_ua / 1000 > chg_status->max_current)
		icl_ua = chg_status->max_current * 1000;

	chg_status->target_curr = icl_ua / 1000;
	chg_info("set target current to %d\n", chg_status->target_curr);

	if (!chip->wireless_psy)
		chip->wireless_psy = power_supply_get_by_name("wireless");

	if (chip->wireless_psy)
		power_supply_changed(chip->wireless_psy);

	return 0;
}

static int wlchg_fastchg_disable_vote_callback(struct votable *votable, void *data,
				int disable, const char *client)
{
	struct op_chg_chip *chip = data;
	struct wpc_data *chg_status = &chip->wlchg_status;

	chg_status->fastchg_disable = disable;
	chg_info("%s wireless fast charge\n", disable ? "disable" : "enable");

	return 0;
}

static void wlchg_reset_variables(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;

	chip->pmic_high_vol = false;

	chg_status->charge_status = WPC_CHG_STATUS_DEFAULT;
	chg_status->fastchg_startup_step = FASTCHG_EN_CHGPUMP1_STEP;
	chg_status->charge_online = false;
	chg_status->tx_online = false;
	chg_status->tx_present = false;
	chg_status->charge_done = false;
	chg_status->charge_voltage = 0;
	chg_status->charge_current = 0;
	chg_status->temp_region = WLCHG_TEMP_REGION_MAX;
	chg_status->wpc_dischg_status = WPC_DISCHG_STATUS_OFF;
	chg_status->max_current = FASTCHG_CURR_30W_MAX_UA / 1000;
	chg_status->target_curr = WPC_CHARGE_CURRENT_DEFAULT;
	chg_status->target_vol = WPC_CHARGE_VOLTAGE_DEFAULT;
	chg_status->vol_set = WPC_CHARGE_VOLTAGE_DEFAULT;
	chg_status->curr_limit_mode = false;
	chg_status->vol_set_ok = true;
	chg_status->vol_set_start = false;
	chg_status->vol_set_fast = false;
	chg_status->curr_set_ok = true;
	chg_status->startup_fast_chg = false;
	chg_status->cep_err_flag = false;
	chg_status->ffc_check = false;
	chg_status->curr_need_dec = false;
	chg_status->vol_not_step = false; //By default, voltage drop requires step
	chg_status->is_power_changed = false;
	chg_status->deviation_check_done = false;
	chg_status->is_deviation = false;
	chg_status->freq_check_count = 0;
	chg_status->freq_thr_inc = false;
	chg_status->wait_cep_stable = false;
	chg_status->geted_tx_id = false;
	chg_status->quiet_mode_enabled = false;
	chg_status->quiet_mode_init = false;
	chg_status->get_adapter_err = false;
	chg_status->epp_working = false;
	chg_status->adapter_msg_send = false;
	chg_status->fastchg_disable = false;
	chg_status->cep_timeout_adjusted = false;
	chg_status->fastchg_restart = false;
	chg_status->startup_fod_parm = false;
	chg_status->adapter_type = ADAPTER_TYPE_UNKNOWN;
	chg_status->charge_type = WPC_CHARGE_TYPE_DEFAULT;
	chg_status->adapter_id = 0;
	chg_status->send_msg_timer = jiffies;
	chg_status->cep_ok_wait_timeout = jiffies;
	chg_status->fastchg_retry_timer = jiffies;
	chg_status->epp_curr_step = 0;
	chg_status->fastchg_curr_step = 0;
	chg_status->fastchg_retry_count = 0;
	chg_status->curr_err_count = 0;

	chg_param->mBattTempBoundT0 = chg_param->BATT_TEMP_T0;
	chg_param->mBattTempBoundT1 = chg_param->BATT_TEMP_T1;
	chg_param->mBattTempBoundT2 = chg_param->BATT_TEMP_T2;
	chg_param->mBattTempBoundT3 = chg_param->BATT_TEMP_T3;
	chg_param->mBattTempBoundT4 = chg_param->BATT_TEMP_T4;
	chg_param->mBattTempBoundT5 = chg_param->BATT_TEMP_T5;
	chg_param->mBattTempBoundT6 = chg_param->BATT_TEMP_T6;

	chip->cmd_info.cmd = 0;
	chip->cmd_info.cmd_type = 0;
	chip->cmd_info.cmd_retry_count = 0;
	chip->msg_info.type = 0;
	chip->msg_info.data = 0;
	chip->msg_info.remark = 0;
	chip->wlchg_msg_ok = false;

	chip->wlchg_time_count = 0;

	atomic_set(&chip->hb_count, HEARTBEAT_COUNT_MAX);

	if (g_rx_chip != NULL)
		wlchg_rx_reset_variables(g_rx_chip);

#ifdef HW_TEST_EDITION
	chip->w30w_time = 2;
	chip->w30w_timeout = false;
	chip->w30w_work_started = false;
#endif
}
#if 0
static void op_wireless_set_otg_en_val(int value)
{
	//do nothing now;
}
#endif
static int wlchg_init_connected_task(struct op_chg_chip *chip)
{
	if (!chip->wlchg_status.charge_online) {
		wlchg_reset_variables(chip);
		chip->wlchg_status.charge_online = true;
	}

	return 0;
}

static int wlchg_deinit_after_disconnected(struct op_chg_chip *chip)
{
	wlchg_reset_variables(chip);
	//chargepump_set_for_otg(0);
	chargepump_disable();
	bq2597x_enable_charge_pump(false);
	/* Resetting the RX chip when the wireless charging is disconnected */
	if (wlchg_get_usbin_val() == 0) {
		if (!chip->disable_charge) {
			wlchg_rx_set_chip_sleep(1);
			msleep(100);
			wlchg_rx_set_chip_sleep(0);
		}
	}
	update_wlchg_started(false);
	chip->wireless_type = POWER_SUPPLY_TYPE_UNKNOWN;
	return 0;
}

static int pmic_high_vol_en(struct op_chg_chip *chip, bool enable)
{
	if (normal_charger == NULL) {
		chg_err("smbchg not ready\n");
		return -ENODEV;
	}

	chip->pmic_high_vol = enable;
	op_wireless_high_vol_en(enable);

	return 0;
}

static int wlchg_disable_batt_charge(struct op_chg_chip *chip, bool en)
{
	if (chip->disable_batt_charge == en)
		return 0;

	if (normal_charger == NULL) {
		chg_err("smb charger is not ready\n");
		return -ENODEV;
	}

	chg_info("%s battery wireless charge\n", en ? "disable" : "enable");
	chip->disable_batt_charge = en;
	normal_charger->chg_disabled = en;
	vote(normal_charger->chg_disable_votable, WLCH_VOTER, en, 0);

	return 0;
}

#define WPC_DISCHG_WAIT_READY_EVENT                                            \
	round_jiffies_relative(msecs_to_jiffies(200))
#define WPC_DISCHG_WAIT_DEVICE_EVENT                                           \
	round_jiffies_relative(msecs_to_jiffies(60 * 1000))
#define WPC_DISCHG_POLL_STATUS_EVENT                                           \
		round_jiffies_relative(msecs_to_jiffies(5000))
#define WPC_DISCHG_WAIT_STATUS_EVENT                                           \
		round_jiffies_relative(msecs_to_jiffies(500))

void wlchg_enable_tx_function(bool is_on)
{
	if ((!g_op_chip) || (!g_rx_chip)) {
		chg_err("<~WPC~> Can't set rtx function!\n");
		return;
	}

	if (wlchg_rx_fw_updating(g_rx_chip)) {
		chg_err("<~WPC~> FW is updating, return!\n");
		return;
	}

	mutex_lock(&g_op_chip->connect_lock);
	if (is_on) {
		chg_err("<~WPC~> Enable rtx function!\n");
		if (g_op_chip->wireless_mode != WIRELESS_MODE_NULL) {
			chg_err("<~WPC~> Rtx is used, can't enable tx mode!\n");
			goto out;
		}
		g_op_chip->wlchg_status.tx_present = true;

		if (!g_op_chip->reverse_wlchg_wake_lock_on) {
			chg_info("acquire reverse_wlchg_wake_lock\n");
			__pm_stay_awake(g_op_chip->reverse_wlchg_wake_lock);
			g_op_chip->reverse_wlchg_wake_lock_on = true;
		} else {
			chg_err("reverse_wlchg_wake_lock is already stay awake.");
		}

		op_set_wrx_en_value(2);
		msleep(20);
		op_set_wrx_otg_value(1);
		msleep(20);
		// set pm8150b vbus out.
		smblib_vbus_regulator_enable(normal_charger->vbus_vreg->rdev);
		// set pm8150b otg current to 1A.
		smblib_set_charge_param(normal_charger, &normal_charger->param.otg_cl,
					REVERSE_WIRELESS_CHARGE_CURR_LIMT);
		smblib_set_charge_param(normal_charger, &normal_charger->param.otg_vol,
					REVERSE_WIRELESS_CHARGE_VOL_LIMT);
		msleep(50);

		g_op_chip->wlchg_status.wpc_dischg_status =
			WPC_DISCHG_STATUS_ON;
		g_op_chip->wireless_mode = WIRELESS_MODE_TX;
		cancel_delayed_work_sync(&g_op_chip->dischg_work);
		schedule_delayed_work(&g_op_chip->dischg_work,
					  WPC_DISCHG_WAIT_READY_EVENT);
		cancel_delayed_work_sync(&g_op_chip->tx_check_work);
		schedule_delayed_work(&g_op_chip->tx_check_work,
					  msecs_to_jiffies(500));
		chg_err("<~WPC~> Enable rtx end!\n");
	} else {
		chg_err("<~WPC~> Disable rtx function!\n");
		if (g_op_chip->wireless_mode != WIRELESS_MODE_TX) {
			chg_err("<~WPC~> Rtx function is not enabled, needn't disable!\n");
			goto out;
		}
		g_op_chip->wlchg_status.tx_present = false;
		cancel_delayed_work_sync(&g_op_chip->dischg_work);
		g_op_chip->wireless_mode = WIRELESS_MODE_NULL;
		if (g_op_chip->wireless_psy != NULL)
			power_supply_changed(g_op_chip->wireless_psy);
		g_op_chip->wlchg_status.wpc_dischg_status =
			WPC_DISCHG_STATUS_OFF;
		g_op_chip->wlchg_status.tx_online = false;
		//insert the wire charge, disable tp noise mode.
		if (g_op_chip->wlchg_status.wpc_dischg_status != WPC_DISCHG_IC_TRANSFER) {
			if (reverse_charge_status) {
				reverse_charge_notifier_call_chain(0);
				reverse_charge_status = 0;
			}
		}

		// disable pm8150b vbus out.
		smblib_vbus_regulator_disable(normal_charger->vbus_vreg->rdev);
		msleep(20);
		op_set_wrx_otg_value(0);
		msleep(20);
		if (!typec_is_otg_mode())
			op_set_wrx_en_value(0);

		if (g_op_chip->reverse_wlchg_wake_lock_on) {
			chg_info("release reverse_wlchg_wake_lock\n");
			__pm_relax(g_op_chip->reverse_wlchg_wake_lock);
			g_op_chip->reverse_wlchg_wake_lock_on = false;
		} else {
			chg_err("reverse_wlchg_wake_lock is already relax\n");
		}

		chg_err("<~WPC~> Disable rtx end!\n");
	}
	if (g_op_chip->wireless_psy != NULL)
		power_supply_changed(g_op_chip->wireless_psy);

out:
	mutex_unlock(&g_op_chip->connect_lock);
}

int wlchg_enable_ftm(bool enable)
{
	chg_err("<~WPC~> start, enable[%d]!\n", enable);

	if (!g_op_chip) {
		chg_err("<~WPC~> g_rx_chip is NULL!\n");
		return -EINVAL;
	}

	g_op_chip->wlchg_status.ftm_mode = enable;
	return 0;
}

void exfg_information_register(struct external_battery_gauge *exfg)
{
	if (exfg_instance) {
		exfg_instance = exfg;
		chg_err("multiple battery gauge called\n");
	} else {
		exfg_instance = exfg;
	}
}
EXPORT_SYMBOL(exfg_information_register);

void exchg_information_register(struct smb_charger *chg)
{
	if (normal_charger) {
		normal_charger = chg;
		chg_err("multiple exchg smb5 called\n");
	} else {
		normal_charger = chg;
	}
}
void exchgpump_information_register(struct bq2597x *bq)
{
	if (exchgpump_bq) {
		exchgpump_bq = bq;
		chg_err("multiple ex chargepump bq called\n");
	} else {
		exchgpump_bq = bq;
	}
}

void exrx_information_register(struct rx_chip *chip)
{
	if (g_rx_chip) {
		g_rx_chip = chip;
		chg_err("multiple ex chargepump bq called\n");
	} else {
		g_rx_chip = chip;
	}
}

static void update_wlchg_started(bool enabled)
{
	if (exfg_instance && exfg_instance->wlchg_started_status)
		exfg_instance->wlchg_started_status(enabled);

	if (normal_charger)
		normal_charger->wlchg_fast = enabled;
}

bool wlchg_wireless_charge_start(void)
{
	if (!g_op_chip) {
		return 0;
	}
	return g_op_chip->wlchg_status.charge_online;
}

bool wlchg_wireless_working(void)
{
	bool working = false;

	if (!g_op_chip) {
		chg_err("g_op_chip is null, not ready.");
		return false;
	}
	working = g_op_chip->charger_exist
			|| g_op_chip->wlchg_status.tx_present;
	return working;
}

int wlchg_wireless_get_vout(void)
{
	if (!g_rx_chip) {
		return 0;
	}
	return g_rx_chip->chg_data.vout;
}

static char wireless_mode_name[][5] = { "NULL", "TX", "RX" };

char *wlchg_wireless_get_mode(struct op_chg_chip *chip)
{
	return wireless_mode_name[chip->wireless_mode];
}

static void check_batt_present(struct op_chg_chip *chip)
{
	if (exfg_instance) {
		exfg_instance->set_allow_reading(true);
		chip->batt_missing = !exfg_instance->is_battery_present();
		exfg_instance->set_allow_reading(false);
	}
}

static void fastchg_curr_control_en(struct op_chg_chip *chip, bool enable)
{
	struct wpc_data *chg_status = &chip->wlchg_status;

	if (enable) {
		chg_status->curr_limit_mode = true;
		chg_status->curr_need_dec = false;
		schedule_delayed_work(&chip->fastchg_curr_vol_work, 0);
	} else {
		chg_status->curr_limit_mode = false;
	}
}

static bool wlchg_check_charge_done(struct op_chg_chip *chip)
{
	union power_supply_propval pval = {0, };
	int rc;

	if (!chip->batt_psy) {
		chip->batt_psy = power_supply_get_by_name("battery");
		if (!chip->batt_psy) {
			chg_err("battery psy is not ready\n");
			return false;
		}
	}

	rc = power_supply_get_property(chip->batt_psy,
		POWER_SUPPLY_PROP_STATUS, &pval);
	if (rc < 0) {
		pr_err("Couldn't get batt status, rc=%d\n", rc);
		return false;
	}
	if (pval.intval == POWER_SUPPLY_STATUS_FULL)
		return true;

	return false;
}

static int wlchg_get_skin_temp(int *temp)
{
	int result;
	int rc;

	if (normal_charger == NULL) {
		chg_err("smb charge is not ready, exit\n");
		return -ENODEV;
	}
	if (normal_charger->iio.op_skin_therm_chan == NULL) {
		chg_err("op_skin_therm_chan no found!\n");
		return -ENODATA;
	}

	rc = iio_read_channel_processed(
				normal_charger->iio.op_skin_therm_chan,
				&result);
	if (rc < 0) {
		chg_err("Error in reading IIO channel data, rc=%d\n", rc);
		return rc;
	}
	*temp = result / 100;

	return 0;
}

#define FFC_STEP_UA      100000
#define FFC_STEP_TIME_MS 1000
static void wlchg_fcc_stepper_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct op_chg_chip *chip =
		container_of(dwork, struct op_chg_chip, wlchg_fcc_stepper_work);
	struct wpc_data *chg_status = &chip->wlchg_status;
	const char *client_str;
	int ffc_tmp;
	int target_curr_ua;

	if (normal_charger == NULL) {
		chg_err("smb charge is not ready, exit\n");
		return;
	}

	if (!chg_status->charge_online)
		return;

	target_curr_ua = chg_status->charge_current * 1000;
	ffc_tmp = get_effective_result(normal_charger->usb_icl_votable);
	if (target_curr_ua == ffc_tmp)
		return;
	if (target_curr_ua > ffc_tmp) {
		ffc_tmp += FFC_STEP_UA;
		if (ffc_tmp > target_curr_ua)
			ffc_tmp = target_curr_ua;
	} else {
		ffc_tmp -= FFC_STEP_UA;
		if (ffc_tmp < target_curr_ua)
			ffc_tmp = target_curr_ua;
	}

	chg_err("set usb_icl vote to %d mA\n", ffc_tmp / 1000);
	vote(normal_charger->usb_icl_votable, WIRED_CONN_VOTER, true, ffc_tmp);
	client_str = get_effective_client(normal_charger->usb_icl_votable);
	if (strcmp(client_str, WIRED_CONN_VOTER)) {
		vote(normal_charger->usb_icl_votable, WIRED_CONN_VOTER, true, target_curr_ua);
		return;
	}
	if (ffc_tmp != target_curr_ua)
		schedule_delayed_work(&chip->wlchg_fcc_stepper_work,
				msecs_to_jiffies(FFC_STEP_TIME_MS));
}

static enum power_supply_property wlchg_wireless_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_TX_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TX_CURRENT_NOW,
	POWER_SUPPLY_PROP_CP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CP_CURRENT_NOW,
	POWER_SUPPLY_PROP_REAL_TYPE,
	POWER_SUPPLY_PROP_WIRELESS_MODE,
	POWER_SUPPLY_PROP_WIRELESS_TYPE,
	POWER_SUPPLY_PROP_OP_DISABLE_CHARGE,
	POWER_SUPPLY_PROP_ICON_DELAY,
};

static int wlchg_wireless_get_prop(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct op_chg_chip *chip = power_supply_get_drvdata(psy);
	int tmp;
	int rc = 0;

	if (g_rx_chip == NULL) {
		chg_err("rx chip is not ready\n");
		return -ENODEV;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		if (chip->wireless_mode == WIRELESS_MODE_RX &&
		    normal_charger != NULL)
			val->intval = normal_charger->wireless_present;
		else if (chip->wireless_mode == WIRELESS_MODE_TX)
			val->intval = chip->wlchg_status.tx_present;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		if (wlchg_wireless_charge_start() || chip->charger_exist)
			val->intval = 1;
		else
			val->intval = 0;

		if (chip->wlchg_status.rx_ovp)
			val->intval = 0;

		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (chip->wireless_mode == WIRELESS_MODE_RX)
			val->intval = wlchg_wireless_get_vout() * 1000;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		if (chip->wireless_mode == WIRELESS_MODE_RX)
			val->intval = chip->wlchg_status.target_vol * 1000;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (chip->wireless_mode == WIRELESS_MODE_RX)
			val->intval = g_rx_chip->chg_data.iout * 1000;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (chip->wireless_mode == WIRELESS_MODE_RX)
			val->intval = chip->wlchg_status.max_current * 1000;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_TX_VOLTAGE_NOW:
		if (chip->wireless_mode == WIRELESS_MODE_TX) {
			rc = wlchg_rx_get_tx_vol(g_rx_chip, &tmp);
			if (rc)
				val->intval = 0;
			else
				val->intval = tmp * 1000;
		} else {
			val->intval = 0;
		}
		break;
	case POWER_SUPPLY_PROP_TX_CURRENT_NOW:
		if (chip->wireless_mode == WIRELESS_MODE_TX) {
			rc = wlchg_rx_get_tx_curr(g_rx_chip, &tmp);
			if (rc)
				val->intval = 0;
			else
				val->intval = tmp * 1000;
		} else {
			val->intval = 0;
		}
		break;
	case POWER_SUPPLY_PROP_CP_VOLTAGE_NOW:
		if (chip->wireless_mode == WIRELESS_MODE_RX) {
			if (exchgpump_bq == NULL)
				return -ENODEV;
			bq2597x_get_adc_data(exchgpump_bq, ADC_VBUS, &tmp);
			val->intval = tmp * 1000;
		} else {
			val->intval = 0;
		}
		break;
	case POWER_SUPPLY_PROP_CP_CURRENT_NOW:
		if (chip->wireless_mode == WIRELESS_MODE_RX) {
			if (exchgpump_bq == NULL)
				return -ENODEV;
			bq2597x_get_adc_data(exchgpump_bq, ADC_IBUS, &tmp);
			val->intval = tmp * 1000;
		} else {
			val->intval = 0;
		}
		break;
	case POWER_SUPPLY_PROP_REAL_TYPE:
		if (chip->wlchg_status.fastchg_display_delay) {
			if (chip->wlchg_status.charge_online) {
				if (chip->wlchg_status.is_deviation ||
				    ((chip->wlchg_status.temp_region != WLCHG_BATT_TEMP_PRE_NORMAL) &&
				     (chip->wlchg_status.temp_region != WLCHG_BATT_TEMP_NORMAL))) {
					chip->wlchg_status.fastchg_display_delay = false;
				} else {
					val->intval = POWER_SUPPLY_TYPE_DASH;
					break;
				}
			} else {
				val->intval = POWER_SUPPLY_TYPE_DASH;
				break;
			}
		}
		switch (chip->wlchg_status.adapter_type) {
		case ADAPTER_TYPE_FASTCHAGE_DASH:
		case ADAPTER_TYPE_FASTCHAGE_WARP:
			if (chip->wlchg_status.deviation_check_done &&
			    ((chip->wlchg_status.temp_region == WLCHG_BATT_TEMP_PRE_NORMAL) ||
			     (chip->wlchg_status.temp_region == WLCHG_BATT_TEMP_NORMAL)))
				val->intval = POWER_SUPPLY_TYPE_DASH;
			else
				val->intval = POWER_SUPPLY_TYPE_UNKNOWN;
			break;
		case ADAPTER_TYPE_USB:
			val->intval = POWER_SUPPLY_TYPE_USB;
			break;
		case ADAPTER_TYPE_NORMAL_CHARGE:
			val->intval = POWER_SUPPLY_TYPE_USB_DCP;
			break;
		default:
			val->intval = POWER_SUPPLY_TYPE_UNKNOWN;
			break;
		}
		break;
	case POWER_SUPPLY_PROP_WIRELESS_MODE:
		val->strval = wlchg_wireless_get_mode(chip);
		break;
	case POWER_SUPPLY_PROP_WIRELESS_TYPE:
		val->intval = chip->wireless_type;
		break;
	case POWER_SUPPLY_PROP_OP_DISABLE_CHARGE:
		val->intval = chip->disable_batt_charge;
		break;
	case POWER_SUPPLY_PROP_VBATDET:
		tmp = chip->wlchg_status.temp_region;
		if (chip->wireless_mode == WIRELESS_MODE_RX &&
		    tmp < WLCHG_TEMP_REGION_MAX) {
			val->intval = chip->chg_param.vbatdet[tmp];
		} else {
			val->intval = 0;
		}
		break;
	case POWER_SUPPLY_PROP_ICON_DELAY:
		val->intval = chg_icon_update_delay;
		break;
	default:
		return -EINVAL;
	}
	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}
	return 0;
}

static int wlchg_wireless_set_prop(struct power_supply *psy,
				   enum power_supply_property psp,
				   const union power_supply_propval *val)
{
	struct op_chg_chip *chip = power_supply_get_drvdata(psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		chip->wlchg_status.max_current = val->intval / 1000;
		vote(chip->wlcs_fcc_votable, MAX_VOTER, true, val->intval);
		rc = 0;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		vote(chip->wlcs_fcc_votable, USER_VOTER, true, val->intval);
		rc = 0;
		break;
	case POWER_SUPPLY_PROP_OP_DISABLE_CHARGE:
		rc = wlchg_disable_batt_charge(chip, (bool)val->intval);
		break;
	case POWER_SUPPLY_PROP_ICON_DELAY:
		chg_icon_update_delay = (bool)val->intval;
		break;
	default:
		chg_err("set prop %d is not supported\n", psp);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int wlchg_wireless_prop_is_writeable(struct power_supply *psy,
					    enum power_supply_property psp)
{
	int rc;

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_OP_DISABLE_CHARGE:
	case POWER_SUPPLY_PROP_ICON_DELAY:
		rc = 1;
		break;
	default:
		rc = 0;
		break;
	}

	return rc;
}

static const struct power_supply_desc wireless_psy_desc = {
	.name = "wireless",
	.type = POWER_SUPPLY_TYPE_WIRELESS,
	.properties = wlchg_wireless_props,
	.num_properties = ARRAY_SIZE(wlchg_wireless_props),
	.get_property = wlchg_wireless_get_prop,
	.set_property = wlchg_wireless_set_prop,
	.property_is_writeable = wlchg_wireless_prop_is_writeable,
};

static int wlchg_init_wireless_psy(struct op_chg_chip *chip)
{
	struct power_supply_config wireless_cfg = {};

	wireless_cfg.drv_data = chip;
	wireless_cfg.of_node = chip->dev->of_node;
	chip->wireless_psy = devm_power_supply_register(
		chip->dev, &wireless_psy_desc, &wireless_cfg);
	if (IS_ERR(chip->wireless_psy)) {
		chg_err("Couldn't register wireless power supply\n");
		return PTR_ERR(chip->wireless_psy);
	}

	return 0;
}

int wlchg_send_msg(enum WLCHG_MSG_TYPE type, char data, char remark)
{
	struct wlchg_msg_t *msg_info;

	if (g_op_chip == NULL) {
		chg_err("wlchg is not ready\n");
		return -ENODEV;
	}

	if (!g_op_chip->wlchg_msg_ok) {
		mutex_lock(&g_op_chip->msg_lock);
		if ((type == WLCHG_MSG_CHG_INFO) && (remark == WLCHG_ADAPTER_MSG))
			g_op_chip->wlchg_status.get_adapter_err = false;
		msg_info = &g_op_chip->msg_info;
		msg_info->data = data;
		msg_info->type = type;
		msg_info->remark = remark;
		g_op_chip->wlchg_msg_ok = true;
		mutex_unlock(&g_op_chip->msg_lock);
		wake_up(&g_op_chip->read_wq);
	} else {
		chg_err("the previous message has not been sent successfully\n");
		return -EINVAL;
	}

	return 0;
}

static int wlchg_cmd_process(struct op_chg_chip *chip)
{
	struct cmd_info_t *cmd_info = &chip->cmd_info;
	struct rx_chip_prop *prop;
	struct wpc_data *chg_status = &chip->wlchg_status;

	if (g_rx_chip == NULL) {
		chg_err("rx chip is not ready\n");
		return -ENODEV;
	}

	if (time_is_before_jiffies(chg_status->send_msg_timer)) {
		prop = g_rx_chip->prop;
		if (cmd_info->cmd != 0) {
			if (cmd_info->cmd_retry_count != 0) {
				chg_info("cmd:%d, %d, %d\n", cmd_info->cmd_type, cmd_info->cmd, cmd_info->cmd_retry_count);
				prop->send_msg(prop, cmd_info->cmd_type, cmd_info->cmd);
				if (cmd_info->cmd_retry_count > 0)
					cmd_info->cmd_retry_count--;
			} else {
				wlchg_send_msg(WLCHG_MSG_CMD_ERR, 0, cmd_info->cmd);
				cmd_info->cmd = 0;
			}
		}
		chg_status->send_msg_timer = jiffies + HZ;
	}

	return 0;
}

static int wlchg_dev_open(struct inode *inode, struct file *filp)
{
	struct op_chg_chip *chip = container_of(filp->private_data,
		struct op_chg_chip, wlchg_device);

	filp->private_data = chip;
	pr_debug("%d,%d\n", imajor(inode), iminor(inode));
	return 0;
}

static ssize_t wlchg_dev_read(struct file *filp, char __user *buf,
		size_t count, loff_t *offset)
{
	struct op_chg_chip *chip = filp->private_data;
	struct wlchg_msg_t msg;
	int ret = 0;

	mutex_lock(&chip->read_lock);
	ret = wait_event_interruptible(chip->read_wq, chip->wlchg_msg_ok);
	mutex_unlock(&chip->read_lock);
	if (ret)
		return ret;
	if (!chip->wlchg_msg_ok)
		chg_err("wlchg false wakeup,ret=%d\n", ret);
	mutex_lock(&chip->msg_lock);
	chip->wlchg_msg_ok = false;
	msg.type = chip->msg_info.type;
	msg.data = chip->msg_info.data;
	msg.remark = chip->msg_info.remark;
	if ((msg.type == WLCHG_MSG_CHG_INFO) &&
	    (msg.remark == WLCHG_ADAPTER_MSG))
		chip->wlchg_status.adapter_msg_send = true;
	mutex_unlock(&chip->msg_lock);
	if (copy_to_user(buf, &msg, sizeof(struct wlchg_msg_t))) {
		chg_err("failed to copy to user space\n");
		return -EFAULT;
	}

	return ret;
}

#define WLCHG_IOC_MAGIC			0xfe
#define WLCHG_NOTIFY_ADAPTER_TYPE	_IOW(WLCHG_IOC_MAGIC, 1, int)
#define WLCHG_NOTIFY_ADAPTER_TYPE_ERR	_IO(WLCHG_IOC_MAGIC, 2)
#define WLCHG_NOTIFY_CHARGE_TYPE	_IOW(WLCHG_IOC_MAGIC, 3, int)
#define WLCHG_NOTIFY_CHARGE_TYPE_ERR	_IO(WLCHG_IOC_MAGIC, 4)
#define WLCHG_NOTIFY_TX_ID		_IO(WLCHG_IOC_MAGIC, 5)
#define WLCHG_NOTIFY_TX_ID_ERR		_IO(WLCHG_IOC_MAGIC, 6)
#define WLCHG_NOTIFY_QUIET_MODE		_IO(WLCHG_IOC_MAGIC, 7)
#define WLCHG_NOTIFY_QUIET_MODE_ERR	_IO(WLCHG_IOC_MAGIC, 8)
#define WLCHG_NOTIFY_NORMAL_MODE	_IO(WLCHG_IOC_MAGIC, 9)
#define WLCHG_NOTIFY_NORMAL_MODE_ERR	_IO(WLCHG_IOC_MAGIC, 10)
#define WLCHG_NOTIFY_READY_FOR_EPP	_IO(WLCHG_IOC_MAGIC, 11)
#define WLCHG_NOTIFY_WORKING_IN_EPP	_IO(WLCHG_IOC_MAGIC, 12)
#define WLCHG_NOTIFY_HEARTBEAT		_IO(WLCHG_IOC_MAGIC, 13)
#define WLCHG_NOTIFY_SET_CEP_TIMEOUT     _IO(WLCHG_IOC_MAGIC, 14)
#define WLCHG_NOTIFY_SET_CEP_TIMEOUT_ERR _IO(WLCHG_IOC_MAGIC, 15)

static long wlchg_dev_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	struct op_chg_chip *chip = filp->private_data;
	struct wpc_data *chg_status = &chip->wlchg_status;

	switch (cmd) {
	case WLCHG_NOTIFY_ADAPTER_TYPE:
		chg_status->adapter_type = arg & WPC_ADAPTER_TYPE_MASK;
		chg_status->adapter_id = (arg & WPC_ADAPTER_ID_MASK) >> 3;
		if (chip->wireless_psy != NULL)
			power_supply_changed(chip->wireless_psy);
		if (chg_status->adapter_type == ADAPTER_TYPE_FASTCHAGE_PD_65W)
			chg_status->adapter_type = ADAPTER_TYPE_FASTCHAGE_WARP;
		if (chip->chg_param.fastchg_fod_enable &&
		    (chg_status->adapter_type == ADAPTER_TYPE_FASTCHAGE_DASH ||
		     chg_status->adapter_type == ADAPTER_TYPE_FASTCHAGE_WARP)) {
			if (chg_status->adapter_id == 0x00 || chg_status->adapter_id == 0x01)
				wlchg_rx_set_match_q_parm(g_rx_chip, chip->chg_param.fastchg_match_q);
			else
			 	wlchg_rx_set_match_q_parm(g_rx_chip, chip->chg_param.fastchg_match_q_new);
		}
		chg_info("adapter arg is 0x%02x, adapter type is %d, adapter id is %d\n",
			arg, chg_status->adapter_type, chg_status->adapter_id);
		break;
	case WLCHG_NOTIFY_ADAPTER_TYPE_ERR:
		chg_status->get_adapter_err = true;
		chg_err("get adapter type error\n");
		break;
	case WLCHG_NOTIFY_CHARGE_TYPE:
		chg_status->charge_type = arg;
		chg_info("charge type is %d\n", arg);
		if (chip->chg_param.fastchg_fod_enable &&
		    chg_status->charge_type == WPC_CHARGE_TYPE_FAST) {
			if (chg_status->adapter_id == 0x00 || chg_status->adapter_id == 0x01)
				wlchg_rx_set_fod_parm(g_rx_chip, chip->chg_param.fastchg_fod_parm);
			else
				wlchg_rx_set_fod_parm(g_rx_chip, chip->chg_param.fastchg_fod_parm_new);

			chg_status->startup_fod_parm = false;
			chg_info("write fastchg fod parm\n");
		}
		break;
	case WLCHG_NOTIFY_CHARGE_TYPE_ERR:
		chg_err("get charge type error\n");
		break;
	case WLCHG_NOTIFY_TX_ID:
		chg_status->geted_tx_id = true;
		break;
	case WLCHG_NOTIFY_TX_ID_ERR:
		chg_status->geted_tx_id = true;
		chg_err("get tx id error\n");
		break;
	case WLCHG_NOTIFY_QUIET_MODE:
		chg_status->quiet_mode_enabled = true;
		chg_status->quiet_mode_init = true;
		break;
	case WLCHG_NOTIFY_QUIET_MODE_ERR:
		chg_err("set quiet mode error\n");
		break;
	case WLCHG_NOTIFY_NORMAL_MODE:
		chg_status->quiet_mode_enabled = false;
		chg_status->quiet_mode_init = true;
		break;
	case WLCHG_NOTIFY_NORMAL_MODE_ERR:
		chg_err("set normal mode error\n");
		break;
	case WLCHG_NOTIFY_SET_CEP_TIMEOUT:
		chg_status->cep_timeout_adjusted = true;
		break;
	case WLCHG_NOTIFY_SET_CEP_TIMEOUT_ERR:
		chg_err("set CEP TIMEOUT error\n");
		break;
	case WLCHG_NOTIFY_READY_FOR_EPP:
		chg_status->adapter_type = ADAPTER_TYPE_EPP;
		break;
	case WLCHG_NOTIFY_WORKING_IN_EPP:
		chg_status->epp_working = true;
		break;
	case WLCHG_NOTIFY_HEARTBEAT:
		pr_debug("heartbeat package\n");
		atomic_set(&chip->hb_count, HEARTBEAT_COUNT_MAX);
		break;
	default:
		chg_err("bad ioctl %u\n", cmd);
	}

	return 0;
}

static ssize_t wlchg_dev_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offset)
{
	struct op_chg_chip *chip = filp->private_data;
	struct cmd_info_t *cmd_info = &chip->cmd_info;
	char temp_buf[3];

	if (count != 3) {
		chg_err("Data length error, len=%d\n", count);
		return -EFAULT;
	}

	if (copy_from_user(temp_buf, buf, count)) {
		chg_err("failed to copy from user space\n");
		return -EFAULT;
	}

	cmd_info->cmd = temp_buf[0];
	cmd_info->cmd_type = temp_buf[1];
	cmd_info->cmd_retry_count = (signed char)temp_buf[2];
	chg_info("cmd=%d, cmd_info=%d, retry_count=%d\n", cmd_info->cmd,
		 cmd_info->cmd_type, cmd_info->cmd_retry_count);

	return count;
}

static const struct file_operations wlchg_dev_fops = {
	.owner			= THIS_MODULE,
	.llseek			= no_llseek,
	.write			= wlchg_dev_write,
	.read			= wlchg_dev_read,
	.open			= wlchg_dev_open,
	.unlocked_ioctl	= wlchg_dev_ioctl,
};

/* Tbatt < -3C */
static int handle_batt_temp_cold(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;

	if ((chg_status->temp_region != WLCHG_BATT_TEMP_COLD) || chg_status->is_power_changed) {
		chg_status->is_power_changed = false;
		if (normal_charger) {
			vote(normal_charger->fcc_votable, WLCH_VOTER, true, 0);
		} else {
			chg_err("smb charge is not ready\n");
			return -ENODEV;
		}
		wlchg_set_rx_charge_current(chip, 0);
		chg_status->temp_region = WLCHG_BATT_TEMP_COLD;
		chg_info("switch temp region to %d\n", chg_status->temp_region);

		/* Update the temperature boundaries */
		chg_param->mBattTempBoundT0 = chg_param->BATT_TEMP_T0 + BATT_TEMP_HYST;
		chg_param->mBattTempBoundT1 = chg_param->BATT_TEMP_T1;
		chg_param->mBattTempBoundT2 = chg_param->BATT_TEMP_T2;
		chg_param->mBattTempBoundT3 = chg_param->BATT_TEMP_T3;
		chg_param->mBattTempBoundT4 = chg_param->BATT_TEMP_T4;
		chg_param->mBattTempBoundT5 = chg_param->BATT_TEMP_T5;
		chg_param->mBattTempBoundT6 = chg_param->BATT_TEMP_T6;
	}

	return 0;
}

/* -3C <= Tbatt <= 0C */
static int handle_batt_temp_little_cold(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;

	if ((chg_status->temp_region != WLCHG_BATT_TEMP_LITTLE_COLD) || chg_status->is_power_changed) {
		chg_status->is_power_changed = false;
		chg_status->temp_region = WLCHG_BATT_TEMP_LITTLE_COLD;
		chg_info("switch temp region to %d\n", chg_status->temp_region);
		if (normal_charger) {
			if (chip->pmic_high_vol) {
				vote(normal_charger->usb_icl_votable, WLCH_VOTER, true,
				     chg_param->epp_iclmax[chg_status->temp_region] * 1000);
				vote(normal_charger->fcc_votable, WLCH_VOTER, true,
				     chg_param->epp_ibatmax[chg_status->temp_region] * 1000);
			} else {
				vote(normal_charger->usb_icl_votable, WLCH_VOTER, true,
				     chg_param->bpp_iclmax[chg_status->temp_region] * 1000);
				vote(normal_charger->fcc_votable, WLCH_VOTER, true,
				     chg_param->bpp_ibatmax[chg_status->temp_region] * 1000);
			}
		} else {
			chg_err("smb charge is not ready\n");
			return -ENODEV;
		}

		/* Update the temperature boundaries */
		chg_param->mBattTempBoundT0 = chg_param->BATT_TEMP_T0;
		chg_param->mBattTempBoundT1 = chg_param->BATT_TEMP_T1 + BATT_TEMP_HYST;
		chg_param->mBattTempBoundT2 = chg_param->BATT_TEMP_T2;
		chg_param->mBattTempBoundT3 = chg_param->BATT_TEMP_T3;
		chg_param->mBattTempBoundT4 = chg_param->BATT_TEMP_T4;
		chg_param->mBattTempBoundT5 = chg_param->BATT_TEMP_T5;
		chg_param->mBattTempBoundT6 = chg_param->BATT_TEMP_T6;
	}

	return 0;
}

/* 0C < Tbatt <= 5C*/
static int handle_batt_temp_cool(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;
	static int pre_vbat;
	static bool vbat_exce_thr; //vbat has exceeded the threshold

	if (((pre_vbat <= chg_param->cool_vbat_thr_mv) &&
	     (chip->batt_volt > chg_param->cool_vbat_thr_mv)) ||
	    ((pre_vbat > chg_param->cool_vbat_thr_mv) &&
	     (chip->batt_volt <= chg_param->cool_vbat_thr_mv))) {
		chg_info("battery voltage changes%d\n");
		chg_status->is_power_changed = true;
	}
	pre_vbat = chip->batt_volt;

	if ((chg_status->temp_region != WLCHG_BATT_TEMP_COOL) || chg_status->is_power_changed) {
		chg_status->is_power_changed = false;
		if (chg_status->temp_region != WLCHG_BATT_TEMP_COOL)
			vbat_exce_thr = false;
		chg_status->temp_region = WLCHG_BATT_TEMP_COOL;
		chg_info("switch temp region to %d\n", chg_status->temp_region);
		if (normal_charger) {
			if (chip->pmic_high_vol) {
				if ((!vbat_exce_thr && chip->batt_volt <= chg_param->cool_vbat_thr_mv) ||
				    (vbat_exce_thr && chip->batt_volt <= chg_param->cool_vbat_thr_mv - 150)) {
					vbat_exce_thr = false;
					vote(normal_charger->usb_icl_votable, WLCH_VOTER, true,
					     chg_param->epp_iclmax[chg_status->temp_region] * 1000);
					vote(normal_charger->fcc_votable, WLCH_VOTER, true,
					     chg_param->epp_ibatmax[chg_status->temp_region] * 1000);
				} else {
					vbat_exce_thr = true;
					vote(normal_charger->usb_icl_votable, WLCH_VOTER, true,
					     chg_param->cool_epp_icl_ma * 1000);
					vote(normal_charger->fcc_votable, WLCH_VOTER, true,
					     chg_param->cool_epp_ibat_ma * 1000);
				}
			} else {
				vote(normal_charger->usb_icl_votable, WLCH_VOTER, true,
				     chg_param->bpp_iclmax[chg_status->temp_region] * 1000);
				vote(normal_charger->fcc_votable, WLCH_VOTER, true,
				     chg_param->bpp_ibatmax[chg_status->temp_region] * 1000);
			}
		} else {
			chg_err("smb charge is not ready\n");
			return -ENODEV;
		}

		/* Update the temperature boundaries */
		chg_param->mBattTempBoundT0 = chg_param->BATT_TEMP_T0;
		chg_param->mBattTempBoundT1 = chg_param->BATT_TEMP_T1;
		chg_param->mBattTempBoundT2 = chg_param->BATT_TEMP_T2 + BATT_TEMP_HYST;
		chg_param->mBattTempBoundT3 = chg_param->BATT_TEMP_T3;
		chg_param->mBattTempBoundT4 = chg_param->BATT_TEMP_T4;
		chg_param->mBattTempBoundT5 = chg_param->BATT_TEMP_T5;
		chg_param->mBattTempBoundT6 = chg_param->BATT_TEMP_T6;
	}

	return 0;
}
/* 5C < Tbatt <= 12C */
static int handle_batt_temp_little_cool(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;

	if ((chg_status->temp_region != WLCHG_BATT_TEMP_LITTLE_COOL) || chg_status->is_power_changed) {
		chg_status->is_power_changed = false;
		chg_status->temp_region = WLCHG_BATT_TEMP_LITTLE_COOL;
		chg_info("switch temp region to %d\n", chg_status->temp_region);
		if (normal_charger) {
			if (chip->pmic_high_vol) {
				vote(normal_charger->usb_icl_votable, WLCH_VOTER, true,
				     chg_param->epp_iclmax[chg_status->temp_region] * 1000);
				vote(normal_charger->fcc_votable, WLCH_VOTER, true,
				     chg_param->epp_ibatmax[chg_status->temp_region] * 1000);
			} else {
				vote(normal_charger->usb_icl_votable, WLCH_VOTER, true,
				     chg_param->bpp_iclmax[chg_status->temp_region] * 1000);
				vote(normal_charger->fcc_votable, WLCH_VOTER, true,
				     chg_param->bpp_ibatmax[chg_status->temp_region] * 1000);
			}
		} else {
			chg_err("smb charge is not ready\n");
			return -ENODEV;
		}

		/* Update the temperature boundaries */
		chg_param->mBattTempBoundT0 = chg_param->BATT_TEMP_T0;
		chg_param->mBattTempBoundT1 = chg_param->BATT_TEMP_T1;
		chg_param->mBattTempBoundT2 = chg_param->BATT_TEMP_T2;
		chg_param->mBattTempBoundT3 = chg_param->BATT_TEMP_T3 + BATT_TEMP_HYST;
		chg_param->mBattTempBoundT4 = chg_param->BATT_TEMP_T4;
		chg_param->mBattTempBoundT5 = chg_param->BATT_TEMP_T5;
		chg_param->mBattTempBoundT6 = chg_param->BATT_TEMP_T6;
	}

	return 0;
}

/* 12C < Tbatt < 22C */
static int handle_batt_temp_prenormal(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;

	if ((chg_status->temp_region != WLCHG_BATT_TEMP_PRE_NORMAL) || chg_status->is_power_changed) {
		chg_status->is_power_changed = false;
		chg_status->temp_region = WLCHG_BATT_TEMP_PRE_NORMAL;
		chg_info("switch temp region to %d\n", chg_status->temp_region);
		if (normal_charger) {
			if (chip->pmic_high_vol) {
				vote(normal_charger->usb_icl_votable, WLCH_VOTER, true,
				     chg_param->epp_iclmax[chg_status->temp_region] * 1000);
				vote(normal_charger->fcc_votable, WLCH_VOTER, true,
				     chg_param->epp_ibatmax[chg_status->temp_region] * 1000);
			} else {
				vote(normal_charger->usb_icl_votable, WLCH_VOTER, true,
				     chg_param->bpp_iclmax[chg_status->temp_region] * 1000);
				vote(normal_charger->fcc_votable, WLCH_VOTER, true,
				     chg_param->bpp_ibatmax[chg_status->temp_region] * 1000);
			}
		} else {
			chg_err("smb charge is not ready\n");
			return -ENODEV;
		}

		if (chg_status->charge_status == WPC_CHG_STATUS_FAST_CHARGING_FROM_CHGPUMP)
			vote(chip->wlcs_fcc_votable, JEITA_VOTER, true, FASTCHG_CURR_20W_MAX_UA);
		else
			vote(chip->wlcs_fcc_votable, JEITA_VOTER, false, 0);

		/* Update the temperature boundaries */
		chg_param->mBattTempBoundT0 = chg_param->BATT_TEMP_T0;
		chg_param->mBattTempBoundT1 = chg_param->BATT_TEMP_T1;
		chg_param->mBattTempBoundT2 = chg_param->BATT_TEMP_T2;
		chg_param->mBattTempBoundT3 = chg_param->BATT_TEMP_T3;
		chg_param->mBattTempBoundT4 = chg_param->BATT_TEMP_T4 + BATT_TEMP_HYST;
		chg_param->mBattTempBoundT5 = chg_param->BATT_TEMP_T5;
		chg_param->mBattTempBoundT6 = chg_param->BATT_TEMP_T6;
	}

	return 0;
}

/* 15C < Tbatt < 45C */
static int handle_batt_temp_normal(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;

	if ((chg_status->temp_region != WLCHG_BATT_TEMP_NORMAL) || chg_status->is_power_changed) {
		chg_status->is_power_changed = false;
		chg_status->temp_region = WLCHG_BATT_TEMP_NORMAL;
		chg_info("switch temp region to %d\n", chg_status->temp_region);
		if (normal_charger) {
			if (chip->pmic_high_vol) {
				vote(normal_charger->usb_icl_votable, WLCH_VOTER, true,
				     chg_param->epp_iclmax[chg_status->temp_region] * 1000);
				vote(normal_charger->fcc_votable, WLCH_VOTER, true,
				     chg_param->epp_ibatmax[chg_status->temp_region] * 1000);
			} else {
				vote(normal_charger->usb_icl_votable, WLCH_VOTER, true,
				     chg_param->bpp_iclmax[chg_status->temp_region] * 1000);
				vote(normal_charger->fcc_votable, WLCH_VOTER, true,
				     chg_param->bpp_ibatmax[chg_status->temp_region] * 1000);
			}
		} else {
			chg_err("smb charge is not ready\n");
			return -ENODEV;
		}

		if (chg_status->charge_status == WPC_CHG_STATUS_FAST_CHARGING_FROM_CHGPUMP)
			vote(chip->wlcs_fcc_votable, JEITA_VOTER, true, FASTCHG_CURR_30W_MAX_UA);
		else
			vote(chip->wlcs_fcc_votable, JEITA_VOTER, false, 0);

		/* Update the temperature boundaries */
		chg_param->mBattTempBoundT0 = chg_param->BATT_TEMP_T0;
		chg_param->mBattTempBoundT1 = chg_param->BATT_TEMP_T1;
		chg_param->mBattTempBoundT2 = chg_param->BATT_TEMP_T2;
		chg_param->mBattTempBoundT3 = chg_param->BATT_TEMP_T3;
		chg_param->mBattTempBoundT4 = chg_param->BATT_TEMP_T4;
		chg_param->mBattTempBoundT5 = chg_param->BATT_TEMP_T5;
		chg_param->mBattTempBoundT6 = chg_param->BATT_TEMP_T6;
	}

	return 0;
}

/* 45C <= Tbatt <= 55C */
static int handle_batt_temp_warm(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;

	if ((chg_status->temp_region != WLCHG_BATT_TEMP_WARM) || chg_status->is_power_changed) {
		chg_status->is_power_changed = false;
		chg_status->temp_region = WLCHG_BATT_TEMP_WARM;
		chg_info("switch temp region to %d\n", chg_status->temp_region);
		if (normal_charger) {
			if (chip->pmic_high_vol) {
				vote(normal_charger->usb_icl_votable, WLCH_VOTER, true,
				     chg_param->epp_iclmax[chg_status->temp_region] * 1000);
				vote(normal_charger->fcc_votable, WLCH_VOTER, true,
				     chg_param->epp_ibatmax[chg_status->temp_region] * 1000);
			} else {
				vote(normal_charger->usb_icl_votable, WLCH_VOTER, true,
				     chg_param->bpp_iclmax[chg_status->temp_region] * 1000);
				vote(normal_charger->fcc_votable, WLCH_VOTER, true,
				     chg_param->bpp_ibatmax[chg_status->temp_region] * 1000);
			}
		} else {
			chg_err("smb charge is not ready\n");
			return -ENODEV;
		}

		/* Update the temperature boundaries */
		chg_param->mBattTempBoundT0 = chg_param->BATT_TEMP_T0;
		chg_param->mBattTempBoundT1 = chg_param->BATT_TEMP_T1;
		chg_param->mBattTempBoundT2 = chg_param->BATT_TEMP_T2;
		chg_param->mBattTempBoundT3 = chg_param->BATT_TEMP_T3;
		chg_param->mBattTempBoundT4 = chg_param->BATT_TEMP_T4;
		chg_param->mBattTempBoundT5 = chg_param->BATT_TEMP_T5 - BATT_TEMP_HYST;
		chg_param->mBattTempBoundT6 = chg_param->BATT_TEMP_T6;
	}

	return 0;
}

/* 55C < Tbatt */
static int handle_batt_temp_hot(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;

	if ((chg_status->temp_region != WLCHG_BATT_TEMP_HOT) || chg_status->is_power_changed) {
		chg_status->is_power_changed = false;
		if (normal_charger) {
			vote(normal_charger->fcc_votable, WLCH_VOTER, true, 0);
		} else {
			chg_err("smb charge is not ready\n");
			return -ENODEV;
		}
		wlchg_set_rx_charge_current(chip, 0);
		chg_status->temp_region = WLCHG_BATT_TEMP_HOT;
		chg_info("switch temp region to %d\n", chg_status->temp_region);

		/* Update the temperature boundaries */
		chg_param->mBattTempBoundT0 = chg_param->BATT_TEMP_T0;
		chg_param->mBattTempBoundT1 = chg_param->BATT_TEMP_T1;
		chg_param->mBattTempBoundT2 = chg_param->BATT_TEMP_T2;
		chg_param->mBattTempBoundT3 = chg_param->BATT_TEMP_T3;
		chg_param->mBattTempBoundT4 = chg_param->BATT_TEMP_T4;
		chg_param->mBattTempBoundT5 = chg_param->BATT_TEMP_T5;
		chg_param->mBattTempBoundT6 =
			chg_param->BATT_TEMP_T6 - BATT_TEMP_HYST;
	}

	return 0;
}

static int op_check_battery_temp(struct op_chg_chip *chip)
{
	int rc = -1;
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;
	enum WLCHG_TEMP_REGION_TYPE pre_temp_region;

	if (!wlchg_wireless_charge_start())
		return rc;

	if (chg_status->ftm_mode) {
		chg_err("ftm mode, don't check temp region\n");
		return 0;
	}

	pre_temp_region = chg_status->temp_region;
	if (chip->temperature < chg_param->mBattTempBoundT0) /* COLD */
		rc = handle_batt_temp_cold(chip);
	else if (chip->temperature >=  chg_param->mBattTempBoundT0 &&
			chip->temperature < chg_param->mBattTempBoundT1) /* LITTLE_COLD */
		rc = handle_batt_temp_little_cold(chip);
	else if (chip->temperature >=  chg_param->mBattTempBoundT1 &&
			chip->temperature < chg_param->mBattTempBoundT2) /* COOL */
		rc = handle_batt_temp_cool(chip);
	else if (chip->temperature >= chg_param->mBattTempBoundT2 &&
			chip->temperature < chg_param->mBattTempBoundT3) /* LITTLE_COOL */
		rc = handle_batt_temp_little_cool(chip);
	else if (chip->temperature >= chg_param->mBattTempBoundT3 &&
			chip->temperature < chg_param->mBattTempBoundT4) /* PRE_NORMAL */
		rc = handle_batt_temp_prenormal(chip);
	else if (chip->temperature >= chg_param->mBattTempBoundT4 &&
			chip->temperature < chg_param->mBattTempBoundT5) /* NORMAL */
		rc = handle_batt_temp_normal(chip);
	else if (chip->temperature >= chg_param->mBattTempBoundT5 &&
			chip->temperature <=  chg_param->mBattTempBoundT6) /* WARM */
		rc = handle_batt_temp_warm(chip);
	else if (chip->temperature > chg_param->mBattTempBoundT6) /* HOT */
		rc = handle_batt_temp_hot(chip);

	if ((pre_temp_region < WLCHG_TEMP_REGION_MAX) &&
		(pre_temp_region != chg_status->temp_region)) {
		chg_info("temp region changed, report event.");
		if (chip->wireless_psy != NULL)
			power_supply_changed(chip->wireless_psy);
	}
	return rc;
}

static int pmic_chan_check_skin_temp(struct op_chg_chip *chip)
{
	int rc = -1;
	int skin_temp;
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;
	static unsigned long wait_timeout;

	if (!wlchg_wireless_charge_start())
		return rc;

	if (chg_status->ftm_mode) {
		chg_err("ftm mode, don't check temp region\n");
		return 0;
	}

	if (wait_timeout == 0)
		wait_timeout = jiffies - HZ;

	rc = wlchg_get_skin_temp(&skin_temp);
	if (rc < 0)
		skin_temp = DEFAULT_SKIN_TEMP;

	chg_info("skin temp = %d\n", skin_temp);

	if (!time_after(jiffies, wait_timeout))
		return 0;

	if (skin_temp >= chg_param->epp_skin_temp_max) {
		if (chg_status->epp_curr_step >= EPP_CURR_STEP_MAX - 1)
			return 0;
		chg_status->epp_curr_step++;
		chg_info("skin temp(=%d) too high\n", skin_temp);
		vote(normal_charger->usb_icl_votable, WLCH_SKIN_VOTER, true,
		     chg_param->epp_curr_step[chg_status->epp_curr_step] * 1000);
		wait_timeout = jiffies + 30 * HZ;
	} else if (skin_temp <= chg_param->epp_skin_temp_min) {
		if (chg_status->epp_curr_step < 1) {
			if (is_client_vote_enabled(normal_charger->usb_icl_votable, WLCH_SKIN_VOTER))
				vote(normal_charger->usb_icl_votable, WLCH_SKIN_VOTER, false, 0);
			return 0;
		}
		chg_status->epp_curr_step--;
		chg_info("skin temp(=%d) reduce\n", skin_temp);
		vote(normal_charger->usb_icl_votable, WLCH_SKIN_VOTER, true,
		     chg_param->epp_curr_step[chg_status->epp_curr_step] * 1000);
		wait_timeout = jiffies + 30 * HZ;
	}

	return 0;
}

static int fastchg_check_skin_temp(struct op_chg_chip *chip)
{
	int rc = -1;
	int skin_temp;
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;
	struct op_fastchg_ffc_step *ffc_chg = &chip->chg_param.ffc_chg;
	static unsigned long wait_timeout;

	if (!wlchg_wireless_charge_start())
		return rc;

	if (chg_status->ftm_mode) {
		chg_err("ftm mode, don't check temp region\n");
		return 0;
	}

	if (wait_timeout == 0)
		wait_timeout = jiffies - HZ;

	rc = wlchg_get_skin_temp(&skin_temp);
	if (rc < 0)
		skin_temp = DEFAULT_SKIN_TEMP;

	pr_debug("skin temp = %d\n", skin_temp);

	if (!time_after(jiffies, wait_timeout))
		return 0;

	if (skin_temp >= chg_param->fastchg_skin_temp_max) {
		chg_info("skin temp(%d) too high(above %d)\n", skin_temp,
			chg_param->fastchg_skin_temp_max);
		chg_status->fastchg_curr_step++;

		if (chg_status->fastchg_curr_step <= chg_status->fastchg_level)
			chg_status->fastchg_curr_step = chg_status->fastchg_level + 1;

		if (chg_status->fastchg_curr_step >= ffc_chg->max_step) {
			vote(chip->fastchg_disable_votable, SKIN_VOTER, true, 0);
			chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_EXIT;
			chg_err("fast charge on the last step, exit fast charge.");
			return 0;
		}

		vote(chip->wlcs_fcc_votable, SKIN_VOTER, true,
			 ffc_chg->ffc_step[chg_status->fastchg_curr_step].curr_ua);
		wait_timeout = jiffies + 30 * HZ;
	} else if (skin_temp <= chg_param->fastchg_skin_temp_min) {
		if (chg_status->fastchg_curr_step <= chg_status->fastchg_level)
			return 0;
		chg_status->fastchg_curr_step--;
		chg_info("skin temp(%d) reduce(below %d)\n", skin_temp,
			chg_param->fastchg_skin_temp_min);
		vote(chip->wlcs_fcc_votable, SKIN_VOTER, true,
			 ffc_chg->ffc_step[chg_status->fastchg_curr_step].curr_ua);
		wait_timeout = jiffies + 30 * HZ;
	}

	return 0;
}

static void fastchg_ffc_param_init(struct op_chg_chip *chip)
{
	struct op_fastchg_ffc_step *ffc_chg = &chip->chg_param.ffc_chg;
	int i;

	for(i = 0; i < ffc_chg->max_step; i++) {
		if (ffc_chg->ffc_step[i].low_threshold > 0)
			ffc_chg->allow_fallback[i] = true;
		else
			ffc_chg->allow_fallback[i] = false;
	}
}

static int fastchg_err_check(struct op_chg_chip *chip)
{
	bool cp2_is_ok;
	bool cp2_is_enabled;
	u8 cp1_status = CP_REEADY;
	struct wpc_data *chg_status = &chip->wlchg_status;
	int ret;

	ret = chargepump_status_check(&cp1_status);
	if (ret != 0) {
		chg_err("read charge status err, ret=%d\n", ret);
		return ret;
	}
	if (cp1_status != CP_REEADY) {
		chg_err("charge pump 1 is err, status=%d\n", cp1_status);
		chargepump_disable();
		if (chg_status->charge_current != 0)
			wlchg_set_rx_charge_current(chip, 0);
		chg_status->fastchg_startup_step = FASTCHG_EN_CHGPUMP1_STEP;
		goto err;
	}

	if (exchgpump_bq != NULL) {
		bq2597x_check_charge_enabled(exchgpump_bq, &cp2_is_enabled);
		cp2_is_ok = bq2597x_charge_status_is_ok(exchgpump_bq);
	}
	if (!cp2_is_enabled) {
		chg_err("charge pump 2 is err\n");
		chg_status->fastchg_startup_step = FASTCHG_EN_PMIC_CHG_STEP;
		goto err;
	}

	return 0;

err:
	chg_status->startup_fast_chg = true;
	chg_status->curr_limit_mode = false;
	update_wlchg_started(false);
	chg_status->charge_status = WPC_CHG_STATUS_INCREASE_VOLTAGE;

	return 1;
}

static int fastchg_curr_filter(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status = &chip->wlchg_status;
	int bq_adc_ibus, iout;
	bool cp_enabled = false;
	int iout_shake = 0;
	static int iout_pre;

	if (exchgpump_bq == NULL) {
		chg_err("bq25970 is not ready\n");
		return -ENODEV;
	}
	if (g_rx_chip == NULL) {
		chg_err("rx chip is not ready\n");
		return -ENODEV;
	}

	bq2597x_check_charge_enabled(exchgpump_bq, &cp_enabled);
	if (!cp_enabled) {
		iout_pre = 0;
		return 0;
	}

	iout = g_rx_chip->chg_data.iout;
	if (iout_pre != 0)
		iout_shake = iout - iout_pre;
	bq2597x_get_adc_data(exchgpump_bq, ADC_IBUS, &bq_adc_ibus);
	if ((iout > WPC_CHARGE_CURRENT_FASTCHG) &&
	    ((abs(iout * 2 - bq_adc_ibus) > 500) || (abs(iout_shake) > 1000))) {
		iout = bq_adc_ibus / 2;
		chg_err("Iout exception, Iout=%d, Ibus=%d, Iout_shake=%d\n",
			iout, bq_adc_ibus, iout_shake);
		chg_status->curr_err_count++;
	} else {
		chg_status->curr_err_count = 0;
	}
	g_rx_chip->chg_data.iout = iout;
	iout_pre = iout;

	if (chg_status->curr_err_count > FASTCHG_CURR_ERR_MAX) {
		chg_err("Iout keeps abnormal, restart wireless charge\n");
		wlchg_rx_set_chip_sleep(1);
	}

	return 0;
}

static void fastchg_switch_next_step(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;
	struct op_fastchg_ffc_step *ffc_chg = &chip->chg_param.ffc_chg;
	u32 batt_vol_max = ffc_chg->ffc_step[chg_status->fastchg_level].vol_max_mv;

	if (ffc_chg->ffc_step[chg_status->fastchg_level].need_wait == 0) {
		if (chip->batt_volt >= batt_vol_max) {
			/* Must delay 1 sec and wait for the batt voltage to drop */
			ffc_chg->ffc_wait_timeout = jiffies + HZ * 5;
		} else {
			ffc_chg->ffc_wait_timeout = jiffies;
		}
	} else {
		/* Delay 1 minute and wait for the temperature to drop */
		ffc_chg->ffc_wait_timeout = jiffies + HZ * 60;
	}

	chg_status->fastchg_level++;
	chg_info("switch to next level=%d\n", chg_status->fastchg_level);
	if (chg_status->fastchg_level >= ffc_chg->max_step) {
		if (chip->batt_volt >= batt_vol_max) {
			chg_info("run normal charge ffc\n");
			chg_status->ffc_check = true;
		}
	} else {
		chg_status->wait_cep_stable = true;
		vote(chip->wlcs_fcc_votable, FFC_VOTER, true,
		     ffc_chg->ffc_step[chg_status->fastchg_level].curr_ua);
	}
	chg_status->fastchg_level_init_temp = chip->temperature;
	if (chip->batt_volt >= batt_vol_max) {
		ffc_chg->allow_fallback[chg_status->fastchg_level] = false;
		if ((chg_status->temp_region == WLCHG_BATT_TEMP_PRE_NORMAL) &&
		    (ffc_chg->ffc_step[chg_status->fastchg_level].curr_ua * 4 >= chg_param->fastchg_ibatmax[0])) {
			ffc_chg->ffc_wait_timeout = jiffies;
		}
	}
}

static void fastchg_switch_prev_step(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct op_fastchg_ffc_step *ffc_chg = &chip->chg_param.ffc_chg;

	chg_status->fastchg_level--;
	chg_info("switch to prev level=%d\n", chg_status->fastchg_level);
	vote(chip->wlcs_fcc_votable, FFC_VOTER, true,
	     ffc_chg->ffc_step[chg_status->fastchg_level].curr_ua);
	chg_status->fastchg_level_init_temp = 0;
	ffc_chg->ffc_wait_timeout = jiffies;
}

static void fastchg_temp_check(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;
	struct op_fastchg_ffc_step *ffc_chg = &chip->chg_param.ffc_chg;
	int batt_temp;
	int def_curr_ua, ffc_curr_ua;
	/*
	 * We want the temperature to drop when switching to a lower current range.
	 * If the temperature rises by 2 degrees before the next gear begins to
	 * detect temperature, then you should immediately switch to a lower gear.
	 */
	int temp_diff;
	u32 batt_vol_max = ffc_chg->ffc_step[chg_status->fastchg_level].vol_max_mv;

	if (chg_status->temp_region != WLCHG_BATT_TEMP_PRE_NORMAL &&
	    chg_status->temp_region != WLCHG_BATT_TEMP_NORMAL) {
		chg_info("Abnormal battery temperature, exit fast charge\n");
		vote(chip->fastchg_disable_votable, FFC_VOTER, true, 0);
		chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_EXIT;
	}

	batt_temp = chip->temperature;
	def_curr_ua = get_client_vote(chip->wlcs_fcc_votable, JEITA_VOTER);
	if (def_curr_ua <= 0)
		def_curr_ua = get_client_vote(chip->wlcs_fcc_votable, DEF_VOTER);
	else
		def_curr_ua = min(get_client_vote(chip->wlcs_fcc_votable, DEF_VOTER), def_curr_ua);
	ffc_curr_ua = ffc_chg->ffc_step[chg_status->fastchg_level].curr_ua;
	if (chg_status->fastchg_level_init_temp != 0)
		temp_diff = batt_temp - chg_status->fastchg_level_init_temp;
	else
		temp_diff = 0;

	pr_debug("battery temp = %d, vol = %d, level = %d, temp_diff = %d\n",
		 batt_temp, chip->batt_volt, chg_status->fastchg_level, temp_diff);

	if (chg_status->fastchg_level == 0) {
		if (def_curr_ua < ffc_curr_ua) {
			if ((chg_status->fastchg_level + 1) < ffc_chg->max_step) {
				if (def_curr_ua < ffc_chg->ffc_step[chg_status->fastchg_level + 1].curr_ua) {
					chg_info("target current too low, switch next step\n");
					fastchg_switch_next_step(chip);
					ffc_chg->ffc_wait_timeout = jiffies;
					return;
				}
			} else {
				chg_info("target current too low, switch next step\n");
				fastchg_switch_next_step(chip);
				ffc_chg->ffc_wait_timeout = jiffies;
				return;
			}
		}
		if ((batt_temp > ffc_chg->ffc_step[chg_status->fastchg_level].high_threshold) ||
		    (chip->batt_volt >= batt_vol_max)) {
			fastchg_switch_next_step(chip);
		}
	} else if (chg_status->fastchg_level >= ffc_chg->max_step) {  // switch to pmic
		vote(chip->fastchg_disable_votable, FFC_VOTER, true, 0);
		chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_EXIT;
	} else {
		if (def_curr_ua < ffc_curr_ua) {
			if ((chg_status->fastchg_level + 1) < ffc_chg->max_step) {
				if (def_curr_ua < ffc_chg->ffc_step[chg_status->fastchg_level + 1].curr_ua) {
					chg_info("target current too low, switch next step\n");
					fastchg_switch_next_step(chip);
					ffc_chg->ffc_wait_timeout = jiffies;
					return;
				}
			} else {
				chg_info("target current too low, switch next step\n");
				fastchg_switch_next_step(chip);
				ffc_chg->ffc_wait_timeout = jiffies;
				return;
			}
		}
		if (chip->batt_volt >= chg_param->batt_vol_max) {
			chg_info("batt voltage too high, switch next step\n");
			fastchg_switch_next_step(chip);
			return;
		}
		if ((batt_temp < ffc_chg->ffc_step[chg_status->fastchg_level].low_threshold) &&
		    ffc_chg->allow_fallback[chg_status->fastchg_level] &&
		    (def_curr_ua > ffc_chg->ffc_step[chg_status->fastchg_level].curr_ua)) {
			chg_info("target current too low, switch next step\n");
			fastchg_switch_prev_step(chip);
			return;
		}
		if (time_after(jiffies, ffc_chg->ffc_wait_timeout) || (temp_diff > 200)) {
			if ((batt_temp > ffc_chg->ffc_step[chg_status->fastchg_level].high_threshold) ||
			    (chip->batt_volt >= batt_vol_max)) {
				fastchg_switch_next_step(chip);
			}
		}
	}
}

void wlchg_check_term_charge(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status;
	struct charge_param *chg_param;
	int skin_temp = DEFAULT_SKIN_TEMP;

	if (chip == NULL) {
		chg_err("op_chg_chip is not ready.");
		return;
	}
	if (normal_charger == NULL) {
		chg_err("smb charger is not ready.");
		return;
	}

	chg_status = &chip->wlchg_status;
	chg_param = &chip->chg_param;

	if (!chg_status->cep_timeout_adjusted && chip->soc > chg_param->fastchg_soc_max)
		wlchg_send_msg(WLCHG_MSG_CHG_INFO, -1, WLCHG_CEP_TIMEOUT_MSG);

	wlchg_get_skin_temp(&skin_temp);

	if (wlchg_check_charge_done(chip)) {
		chg_status->charge_done = true;
		if (chg_status->charge_voltage != WPC_CHARGE_VOLTAGE_STOP_CHG) {
			wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_STOP_CHG);
			wlchg_set_rx_target_voltage(chip, WPC_CHARGE_VOLTAGE_STOP_CHG);
		}

		if (!normal_charger->chg_disabled) {
			chg_info("charge full, disable little current charge to battery.");
			normal_charger->chg_disabled = true;
			vote(normal_charger->chg_disable_votable, WLCH_VOTER, true, 0);
		}

		if (!chg_status->quiet_mode_enabled && skin_temp < CHARGE_FULL_FAN_THREOD_LO)
			wlchg_send_msg(WLCHG_MSG_CHG_INFO, -1, WLCHG_QUIET_MODE_MSG);
		if (chg_status->quiet_mode_enabled && !chip->quiet_mode_need
			&& skin_temp > CHARGE_FULL_FAN_THREOD_HI)
			wlchg_send_msg(WLCHG_MSG_CHG_INFO, -1, WLCHG_NORMAL_MODE_MSG);
	} else {
		chg_status->charge_done = false;
		if (normal_charger->chg_disabled) {
			chg_info("charge not full, restore charging.");
			normal_charger->chg_disabled = false;
			vote(normal_charger->chg_disable_votable, WLCH_VOTER, false, 0);
		}

		if (chg_status->charge_voltage != WPC_CHARGE_VOLTAGE_EPP) {
			wlchg_set_rx_target_voltage(chip, WPC_CHARGE_VOLTAGE_EPP);
			wlchg_set_rx_charge_current_step(chip, WPC_CHARGE_CURRENT_EPP);
		}

		if (!chip->quiet_mode_need && chg_status->quiet_mode_enabled)
			wlchg_send_msg(WLCHG_MSG_CHG_INFO, -1, WLCHG_NORMAL_MODE_MSG);

		if (skin_temp < chg_param->fastchg_skin_temp_min
			&& is_client_vote_enabled(chip->fastchg_disable_votable, SKIN_VOTER)) {
			vote(chip->fastchg_disable_votable, SKIN_VOTER, false, 0);
			chg_info("skin temp is %d(below %d), restore fastcharge.", skin_temp,
				chg_param->fastchg_skin_temp_min);
		}
	}
}

static void wlchg_fastchg_restart_check(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct op_fastchg_ffc_step *ffc_chg = &chip->chg_param.ffc_chg;

	if (!chg_status->fastchg_disable)
		return;

	if (is_client_vote_enabled(chip->fastchg_disable_votable, FFC_VOTER) &&
	    (chip->temperature < (ffc_chg->ffc_step[ffc_chg->max_step - 1].high_threshold - BATT_TEMP_HYST))) {
		vote(chip->fastchg_disable_votable, FFC_VOTER, false, 0);
		chg_status->fastchg_level = ffc_chg->max_step - 1;
	}

	if (is_client_vote_enabled(chip->fastchg_disable_votable, BATT_CURR_VOTER) &&
	    (chip->icharging < 0))
		vote(chip->fastchg_disable_votable, BATT_CURR_VOTER, false, 0);

	if (is_client_vote_enabled(chip->fastchg_disable_votable, QUIET_VOTER) &&
	    !chg_status->quiet_mode_enabled)
		vote(chip->fastchg_disable_votable, QUIET_VOTER, false, 0);

	if (is_client_vote_enabled(chip->fastchg_disable_votable, STARTUP_CEP_VOTER) &&
	    (chg_status->fastchg_retry_count < 10) &&
	    time_is_before_jiffies(chg_status->fastchg_retry_timer))
		vote(chip->fastchg_disable_votable, STARTUP_CEP_VOTER, false, 0);
}

#define CEP_ERR_MAX 3
#define CEP_OK_MAX 10
#define CEP_WAIT_MAX 20
#define CEP_OK_TIMEOUT_MAX 60
static void fastchg_cep_adj(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status = &chip->wlchg_status;
	signed char cep = 0;
	int curr_ua, cep_curr_ua;
	static int wait_cep_count;
	static int cep_err_count;
	static int cep_ok_count;
	int rc;

	if (g_rx_chip == NULL) {
		chg_err("rx chip is not ready\n");
		return;
	}

	rc = wlchg_rx_get_cep_skip_check_update(g_rx_chip, &cep);
	if (rc) {
		pr_err("can't get cep, rc=%d\n", rc);
		return;
	}

	if (!chg_status->wait_cep_stable) {
		/* Insufficient energy only when CEP is positive */
		if (cep < 3) {
			cep_ok_count++;
			cep_err_count = 0;
			if ((cep_ok_count >= CEP_OK_MAX) &&
			    time_after(jiffies, chg_status->cep_ok_wait_timeout) &&
			    is_client_vote_enabled(chip->wlcs_fcc_votable, CEP_VOTER)) {
				chg_info("recovery charging current\n");
				cep_ok_count = 0;
				chg_status->cep_err_flag = false;
				chg_status->wait_cep_stable = true;
				chg_status->cep_ok_wait_timeout = jiffies + CEP_OK_TIMEOUT_MAX * HZ;
				wait_cep_count = 0;
				vote(chip->wlcs_fcc_votable, CEP_VOTER, false, 0);
			}
		} else {
			cep_ok_count = 0;
			cep_err_count++;
			if (cep_err_count >= CEP_ERR_MAX) {
				chg_info("reduce charging current\n");
				cep_err_count = 0;
				chg_status->cep_err_flag = true;
				chg_status->wait_cep_stable = true;
				wait_cep_count = 0;
				if (is_client_vote_enabled(chip->wlcs_fcc_votable, CEP_VOTER))
					cep_curr_ua = get_client_vote(chip->wlcs_fcc_votable, CEP_VOTER);
				else
					cep_curr_ua = 0;
				if ((cep_curr_ua > 0) && (cep_curr_ua <= FASTCHG_CURR_MIN_UA)){
					chg_info("Energy is too low, exit fast charge\n");
					vote(chip->fastchg_disable_votable, CEP_VOTER, true, 0);
					chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_EXIT;
				} else {
					curr_ua = g_rx_chip->chg_data.iout;
					/* Target current is adjusted in 50ma steps*/
					curr_ua = (curr_ua - (curr_ua % CURR_ERR_MIN) - CURR_ERR_MIN) * 1000;
					if (curr_ua < FASTCHG_CURR_MIN_UA)
						curr_ua = FASTCHG_CURR_MIN_UA;
					vote(chip->wlcs_fcc_votable, CEP_VOTER, true, curr_ua);
				}
				chg_status->cep_ok_wait_timeout = jiffies + CEP_OK_TIMEOUT_MAX * HZ;
			}
		}
	} else {
		if (wait_cep_count < CEP_WAIT_MAX) {
			wait_cep_count++;
		} else {
			chg_status->wait_cep_stable = false;
			wait_cep_count =0;
		}
	}
}

static void fastchg_check_ibat(struct op_chg_chip *chip)
{
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;

	if (chip->icharging >= chg_param->fastchg_discharge_curr_max) {
		chg_err("discharge current is too large, exit fast charge\n");
		vote(chip->fastchg_disable_votable, BATT_CURR_VOTER, true, 0);
		chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_EXIT;
	}
}

#define OP20A_ENABLE_VOL_MIN_MV 10000
static int op20a_startup(struct op_chg_chip *chip)
{
	int ret;
	u8 cp_status = 0;
	int vout_mv = 0;
	int try_num = 0;

	if (g_rx_chip == NULL) {
		chg_err("rx chip is not ready\n");
		return -ENODEV;
	}

retry:
	if (try_num >= 40)
		return -EAGAIN;

	ret = wlchg_rx_get_vout(g_rx_chip, &vout_mv);
	if (ret < 0) {
		try_num++;
		goto retry;
	}
	if (vout_mv <= OP20A_ENABLE_VOL_MIN_MV) {
		chg_err("rx vout(=%d) < %d, retry\n", vout_mv, OP20A_ENABLE_VOL_MIN_MV);
		try_num++;
		wlchg_set_rx_target_voltage(chip, WPC_CHARGE_VOLTAGE_FASTCHG_INIT);
		goto retry;
	}
	ret = chargepump_hw_init();
	if (ret < 0) {
		chg_err("charge pump init error, rc=%d\n", ret);
		try_num++;
		goto retry;
	}
	ret = chargepump_enable();
	if (ret < 0) {
		chg_err("charge pump enable error, rc=%d\n", ret);
		try_num++;
		goto retry;
	}
	ret = chargepump_status_check(&cp_status);
	if (ret < 0) {
		chg_err("charge pump enable error, rc=%d\n", ret);
		goto disable_cp;
	}
	if (cp_status & (CP_DWP | CP_SWITCH_OCP | CP_CRP | CP_VOUT_OVP | CP_CLP | CP_VBUS_OVP)) {
		chg_err("charge pump status error, status=0x%02x\n", cp_status);
		goto disable_cp;
	}
	ret = chargepump_disable_dwp();
	if (ret < 0)
		goto disable_cp;
	ret = wlchg_rx_get_vout(g_rx_chip, &vout_mv);
	if (ret < 0)
		goto disable_cp;
	if (vout_mv <= OP20A_ENABLE_VOL_MIN_MV) {
		chg_err("rx vout(=%d) < %d, retry\n", vout_mv, OP20A_ENABLE_VOL_MIN_MV);
		goto disable_cp;
	}
	ret = chargepump_status_check(&cp_status);
	if (ret < 0) {
		chg_err("charge pump enable error, rc=%d\n", ret);
		goto disable_cp;
	}
	if (cp_status & (CP_SWITCH_OCP | CP_CRP | CP_VOUT_OVP | CP_CLP | CP_VBUS_OVP)) {
		chg_err("charge pump status error, status=0x%02x\n", cp_status);
		goto disable_cp;
	}
wait_cp_enable:
	mdelay(5);
	ret = wlchg_rx_get_vout(g_rx_chip, &vout_mv);
	if (ret < 0)
		goto disable_cp;
	if (vout_mv <= OP20A_ENABLE_VOL_MIN_MV) {
		chg_err("rx vout(=%d) < %d, retry\n", vout_mv, OP20A_ENABLE_VOL_MIN_MV);
		goto disable_cp;
	}
	ret = chargepump_status_check(&cp_status);
	if (ret < 0) {
		chg_err("charge pump enable error, rc=%d\n", ret);
		goto disable_cp;
	}
	if (cp_status & (CP_SWITCH_OCP | CP_CRP | CP_VOUT_OVP | CP_CLP | CP_VBUS_OVP)) {
		chg_err("charge pump status error, status=0x%02x\n", cp_status);
		goto disable_cp;
	}
	if (cp_status & CP_REEADY) {
		chg_info("charge pump successful start\n");
		return 0;
	}
	try_num++;
	chg_err("charge pump status=0x%02x, try_num=%d\n", cp_status, try_num);
	if (try_num < 40)
		goto wait_cp_enable;
	else
		return -EAGAIN;

disable_cp:
	chargepump_disable();
	try_num++;
	goto retry;
}

#define CP1_STABILITY_THR 90
static int fastchg_startup_process(struct op_chg_chip *chip)
{
	static int cp1_err_count;
	static int cp2_err_count;
	static int cep_err_count;
	static int curr_err_count;
	static int cp2_enabled_count;
	int bq_adc_vbat = 0;
	int bq_adc_vbus = 0;
	int temp_value = 0;
	int vout_mv;
	u8 cp1_status;
	bool cp2_is_enabled = false;
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;
	int ret;

	if (exchgpump_bq != NULL) {
		bq2597x_get_adc_data(exchgpump_bq, ADC_VBAT, &bq_adc_vbat);
		bq2597x_get_adc_data(exchgpump_bq, ADC_VBUS, &bq_adc_vbus);
	} else {
		chg_err("bq25970 err\n");
		return -ENODEV;
	}
	if (g_rx_chip == NULL) {
		chg_err("rx chip is not ready\n");
		return -ENODEV;
	}
	chg_err("<~WPC~> bq_adc_vbat=%d, bq_adc_vbus=%d\n", bq_adc_vbat, bq_adc_vbus);
	ret = chargepump_status_check(&cp1_status);
	if (ret != 0) {
		chg_err("read charge status err, ret=%d\n", ret);
		return ret;
	}
	if ((cp1_status != CP_REEADY) && (chg_status->fastchg_startup_step > FASTCHG_EN_CHGPUMP1_STEP)) {
		chg_err("charge pump 1 is not ready, status=0x%02x\n", cp1_status);
		__chargepump_show_registers();
		cp1_err_count++;
		if (cp1_err_count > 10) {
			chg_err("cp1 hw error\n");
			cp1_err_count = 0;
			vote(chip->fastchg_disable_votable, HW_ERR_VOTER, true, 0);
			chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_EXIT;
		} else {
			chargepump_disable();
			if (chg_status->charge_current != 0)
				wlchg_set_rx_charge_current(chip, 0);
			chg_status->fastchg_startup_step = FASTCHG_EN_CHGPUMP1_STEP;
		}
		return 0;
	} else {
		if (chg_status->fastchg_startup_step != FASTCHG_EN_CHGPUMP1_STEP)
			cp1_err_count = 0;
	}

	if (chg_status->vol_set_ok ||
	    (chg_status->fastchg_startup_step >= FASTCHG_EN_CHGPUMP2_STEP) ||
	    (((chg_status->fastchg_startup_step == FASTCHG_WAIT_PMIC_STABLE_STEP) ||
	      (chg_status->fastchg_startup_step == FASTCHG_SET_CHGPUMP2_VOL_AGAIN_STEP)) &&
	     (bq_adc_vbus > (bq_adc_vbat * 2 + 150)))) {
		chg_info("fastchg_startup_step:%d\n", chg_status->fastchg_startup_step);
		if (chg_status->vol_set_ok)
			cep_err_count = 0;
		switch (chg_status->fastchg_startup_step) {
		case FASTCHG_EN_CHGPUMP1_STEP:
			if (g_rx_chip->chg_data.vout <= OP20A_ENABLE_VOL_MIN_MV) {
				wlchg_set_rx_target_voltage(chip, WPC_CHARGE_VOLTAGE_FASTCHG_INIT);
				break;
			}
			ret = op20a_startup(chip);
			if (ret) {
				chg_err("cp1 hw error, rc=%d\n", ret);
				cp1_err_count = 0;
				vote(chip->fastchg_disable_votable, HW_ERR_VOTER, true, 0);
				chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_EXIT;
				break;
			}
			if (chip->ap_ctrl_dcdc) {
				ret = wlchg_rx_enable_dcdc(g_rx_chip);
				if (ret) {
					chg_err("can't write enable dcdc cmd\n");
					break;
				}
			}
			cp2_err_count = 0;
			chg_status->fastchg_startup_step = FASTCHG_WAIT_CP1_STABLE_STEP;
#ifdef OP_DEBUG
			if (!auto_mode) {
				wlchg_set_rx_target_voltage(chip, 17000);
				chg_status->charge_status = WPC_CHG_STATUS_BPP_WORKING;
				break;
			}
#endif
			/* There can start to adjust the voltage directly */
			// break;
		case FASTCHG_WAIT_CP1_STABLE_STEP:
			ret = wlchg_rx_get_vout(g_rx_chip, &vout_mv);
			if (ret) {
				if (chg_status->charge_current != 0)
					wlchg_set_rx_charge_current(chip, 0);
				break;
			}

			temp_value = vout_mv * CP1_STABILITY_THR / 200;
			if (bq_adc_vbus > temp_value) {
				if (chg_status->charge_current != WPC_CHARGE_CURRENT_WAIT_FAST) {
					chg_info("enable pmic charge\n");
					pmic_high_vol_en(chip, true);
					wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_WAIT_FAST);
				}
				chg_status->fastchg_startup_step = FASTCHG_SET_CHGPUMP2_VOL_STEP;
			} else {
				if (vout_mv >= WPC_CHARGE_VOLTAGE_OVP_MIN && chg_status->charge_current != 0)
					wlchg_set_rx_charge_current(chip, 0);
				chg_info("chargepump 1 is not ready, wait 100ms\n");
				break;
			}
		case FASTCHG_SET_CHGPUMP2_VOL_STEP:
			temp_value = (g_op_chip->batt_volt * 4) +
				     (g_op_chip->batt_volt * 4 / 10) + 200;
			wlchg_set_rx_target_voltage(chip, temp_value);
			curr_err_count = 0;
			chg_status->fastchg_startup_step = FASTCHG_WAIT_PMIC_STABLE_STEP;
			break;

		case FASTCHG_WAIT_PMIC_STABLE_STEP:
			if (g_rx_chip->chg_data.iout > 100) {
				curr_err_count = 0;
				cp2_enabled_count = 0;
				chg_status->fastchg_startup_step = FASTCHG_SET_CHGPUMP2_VOL_AGAIN_STEP;
			} else {
				curr_err_count++;
				if (curr_err_count > 100) {
					curr_err_count = 0;
					chg_err("pmic charging current is too small to start fast charge\n");
					//vote(chip->fastchg_disable_votable, CURR_ERR_VOTER, true, 0);
					chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_EXIT;
				}
				break;
			}
		case FASTCHG_SET_CHGPUMP2_VOL_AGAIN_STEP:
			if (exchgpump_bq != NULL) {
				bq2597x_check_charge_enabled(exchgpump_bq, &cp2_is_enabled);
				if (cp2_is_enabled) {
					if (cp2_enabled_count > 1) {
						cp2_enabled_count = 0;
						wlchg_set_rx_target_voltage(chip, g_rx_chip->chg_data.vout);
						chg_status->fastchg_startup_step =
							FASTCHG_CHECK_CHGPUMP2_AGAIN_STEP;
						break;
					} else {
						cp2_enabled_count++;
					}
				} else {
					cp2_enabled_count = 0;
				}
				temp_value = bq_adc_vbat * 2 + bq_adc_vbat * 2 / 10;
			} else {
				temp_value = chip->batt_volt * 2 + chip->batt_volt * 2 / 10;
			}
			if (bq_adc_vbus > (temp_value - 50) &&
			    bq_adc_vbus < (temp_value + 150)) {
				if (chg_status->vol_set_ok)
					chg_status->fastchg_startup_step = FASTCHG_EN_CHGPUMP2_STEP;
				else
					break;
			} else {
				if ((bq_adc_vbus > (bq_adc_vbat * 2 + 150)) &&
				    (bq_adc_vbus < temp_value) &&
				    (cp2_enabled_count == 0)) {
					chg_info("try enable cp2\n");
					bq2597x_enable_charge_pump(true);
				}

				if (chg_status->vol_set_ok && !cp2_is_enabled) {
					temp_value = (temp_value - bq_adc_vbus) * 2;
					wlchg_set_rx_target_voltage(chip, g_rx_chip->chg_data.vout + temp_value);
					chg_err("target_vol = %d\n", chg_status->target_vol);
				}
				break;
			}
		case FASTCHG_EN_CHGPUMP2_STEP:
			bq2597x_enable_charge_pump(true);
			chg_status->fastchg_startup_step = FASTCHG_CHECK_CHGPUMP2_STEP;
			break;
		case FASTCHG_CHECK_CHGPUMP2_STEP:
			if (exchgpump_bq != NULL)
				bq2597x_check_charge_enabled(exchgpump_bq, &cp2_is_enabled);
			if (cp2_is_enabled) {
				chg_status->fastchg_startup_step = FASTCHG_CHECK_CHGPUMP2_AGAIN_STEP;
			} else {
				chg_status->fastchg_startup_step = FASTCHG_EN_CHGPUMP2_STEP;
				cp2_err_count++;
				chg_info("enable chgpump try num: %d\n", cp2_err_count);
			}
			break;
		case FASTCHG_CHECK_CHGPUMP2_AGAIN_STEP:
			if (exchgpump_bq != NULL)
				bq2597x_check_charge_enabled(exchgpump_bq, &cp2_is_enabled);
			if (cp2_is_enabled) {
				if (chip->chg_param.fastchg_fod_enable) {
					if (chg_status->adapter_id == 0x00 || chg_status->adapter_id == 0x01)
						wlchg_rx_set_fod_parm(g_rx_chip, chip->chg_param.fastchg_fod_parm);
					else
						wlchg_rx_set_fod_parm(g_rx_chip, chip->chg_param.fastchg_fod_parm_new);
					chg_status->startup_fod_parm = false;
					chg_info("write fastchg fod parm\n");
				}
				chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_FROM_CHGPUMP;
				chip->wireless_type = POWER_SUPPLY_WIRELESS_TYPE_FAST;
				if (chip->soc < chg_param->fastchg_soc_mid) {
					if (chg_status->adapter_type == ADAPTER_TYPE_FASTCHAGE_WARP) {
						vote(chip->wlcs_fcc_votable, DEF_VOTER, true, FASTCHG_CURR_30W_MAX_UA);
						vote(chip->wlcs_fcc_votable, MAX_VOTER, true, FASTCHG_CURR_30W_MAX_UA);
						chg_status->max_current = FASTCHG_CURR_30W_MAX_UA / 1000;
					} else {
						vote(chip->wlcs_fcc_votable, DEF_VOTER, true, FASTCHG_CURR_15W_MAX_UA);
						vote(chip->wlcs_fcc_votable, MAX_VOTER, true, FASTCHG_CURR_15W_MAX_UA);
						chg_status->max_current = FASTCHG_CURR_15W_MAX_UA / 1000;
					}
				} else {
					vote(chip->wlcs_fcc_votable, DEF_VOTER, true, FASTCHG_CURR_15W_MAX_UA);
					vote(chip->wlcs_fcc_votable, MAX_VOTER, true, FASTCHG_CURR_15W_MAX_UA);
					chg_status->max_current = FASTCHG_CURR_15W_MAX_UA / 1000;
				}
				vote(chip->wlcs_fcc_votable, EXIT_VOTER, false, 0);
				vote(chip->wlcs_fcc_votable, JEITA_VOTER, false, 0);
				chg_status->startup_fast_chg = false;
				chg_status->fastchg_startup_step = FASTCHG_EN_CHGPUMP1_STEP;
				chg_status->fastchg_level_init_temp = 0;
				chg_status->wait_cep_stable = true;
				chg_status->fastchg_retry_count = 0;
				chg_param->ffc_chg.ffc_wait_timeout = jiffies;
				if (!chg_status->fastchg_restart) {
					chg_status->fastchg_level = 0;
					fastchg_ffc_param_init(chip);
					chg_status->fastchg_restart = true;
				} else {
					vote(chip->wlcs_fcc_votable, FFC_VOTER, true,
					     chg_param->ffc_chg.ffc_step[chg_status->fastchg_level].curr_ua);
				}
				fastchg_curr_control_en(chip, true);
				chg_info("enable chgpump success, try num: %d\n", cp2_err_count);
				cp2_err_count = 0;
			} else {
				chg_status->fastchg_startup_step = FASTCHG_EN_CHGPUMP2_STEP;
				cp2_err_count++;
				chg_info("enable chgpump try num: %d\n", cp2_err_count);
			}
			break;
		case FASTCHG_EN_PMIC_CHG_STEP:
			temp_value = g_rx_chip->chg_data.vout * CP1_STABILITY_THR / 200;
			if (bq_adc_vbus > temp_value) {
				chg_info("enable pmic charge\n");
				pmic_high_vol_en(chip, true);
				wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_WAIT_FAST);
				curr_err_count = 0;
				temp_value = g_op_chip->batt_volt * 4;
				if (temp_value < chg_status->vol_set)
					chg_status->vol_not_step = true;
				wlchg_set_rx_target_voltage(chip, temp_value);
				curr_err_count = 0;
				chg_status->fastchg_startup_step = FASTCHG_WAIT_PMIC_STABLE_STEP;
			} else {
				if (chg_status->charge_current != 0)
					wlchg_set_rx_charge_current(chip, 0);
				chg_info("chargepump 1 is not ready, wait 100ms\n");
			}
			break;
		}

		if (cp2_err_count > 10) {
			chg_err("can't enable chgpump, exit fastchg\n");
			cp2_err_count = 0;
			vote(chip->fastchg_disable_votable, HW_ERR_VOTER, true, 0);
			chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_EXIT;
		}
	}

	if (!chg_status->vol_set_ok &&
	    (chg_status->fastchg_startup_step < FASTCHG_EN_CHGPUMP2_STEP)) {
		cep_err_count++;
		if (cep_err_count > 300) { //30s
			cep_err_count = 0;
			chg_err("Cannot rise to target voltage, exit fast charge\n");
			chg_status->fastchg_retry_count++;
			chg_status->fastchg_retry_timer = jiffies + 300 * HZ; //5 min
			vote(chip->fastchg_disable_votable, STARTUP_CEP_VOTER, true, 0);
			chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_EXIT;
		}
	}

	return ret;
}

static int wlchg_charge_status_process(struct op_chg_chip *chip)
{
	static bool wait_fast_chg;
	static bool wlchg_status_abnormal;
	int bq_adc_vbat = 0;
	int work_freq;
	int temp_val;
	bool cp2_is_enabled;
	//static int wait_cep_count;
	struct rx_chip *rx_chip = g_rx_chip;
	union power_supply_propval pval = {0, };
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;
	struct cmd_info_t *cmd_info = &chip->cmd_info;
	int rc;

	if (exchgpump_bq != NULL) {
		if (exchgpump_bq->adc_enabled) {
			bq2597x_get_adc_data(exchgpump_bq, ADC_VBAT, &bq_adc_vbat);
		} else {
			bq_adc_vbat = chip->batt_volt;
		}
		pr_debug("<~WPC~> bq_adc_vbat=%d\n", bq_adc_vbat);
	} else {
		chg_err("exchgpump_bq not ready\n");
		return -ENODEV;
	}

	if (rx_chip == NULL) {
		chg_err("rx chip is not ready\n");
		return -ENODEV;
	}

	if (!chg_status->ftm_mode) {
		if (chip->batt_missing) {
			wlchg_rx_set_chip_sleep(1);
			chg_err("battery miss\n");
			return 0;
		}

		if (chg_status->temp_region == WLCHG_BATT_TEMP_COLD ||
		    chg_status->temp_region == WLCHG_BATT_TEMP_HOT ||
		    chg_status->rx_ovp) {
			chg_err("<~WPC~> The temperature or voltage is abnormal, stop charge!\n");
			if (!wlchg_status_abnormal) {
				wlchg_status_abnormal = true;
				chargepump_disable();
				bq2597x_enable_charge_pump(false);
				wlchg_rx_set_chip_sleep(1);
				return 0;
			}
			if (chg_status->charge_current != WPC_CHARGE_CURRENT_ZERO)
				wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_ZERO);

			if (chg_status->charge_voltage != WPC_CHARGE_VOLTAGE_DEFAULT) {
				chg_status->vol_not_step = true;
				wlchg_set_rx_target_voltage(chip, WPC_CHARGE_VOLTAGE_DEFAULT);
			}

			chg_status->charge_status = WPC_CHG_STATUS_DEFAULT;
			return 0;
		} else {
			wlchg_status_abnormal = false;
			if (((chip->quiet_mode_need != chg_status->quiet_mode_enabled) ||
			     !chg_status->quiet_mode_init) &&
			    ((chg_status->adapter_type == ADAPTER_TYPE_FASTCHAGE_DASH) ||
			     (chg_status->adapter_type == ADAPTER_TYPE_FASTCHAGE_WARP)) &&
			    (atomic_read(&chip->hb_count) > 0) && !chg_status->charge_done) {
				if (chip->quiet_mode_need) {
					// dock should in quiet mode, goto 10w.
					chg_info("send msg to dock into quiet mode.");
					wlchg_send_msg(WLCHG_MSG_CHG_INFO, -1, WLCHG_QUIET_MODE_MSG);
					if (chg_status->deviation_check_done)
						chg_status->charge_status = WPC_CHG_STATUS_READY_FOR_QUIET;
					else
						chg_status->charge_status = WPC_CHG_STATUS_DEFAULT;
				} else {
					chg_info("send msg to dock restore normal mode.");
					wlchg_send_msg(WLCHG_MSG_CHG_INFO, -1, WLCHG_NORMAL_MODE_MSG);
				}
			}

			if (chip->disable_batt_charge &&
			    (chg_status->charge_status != WPC_CHG_STATUS_WAIT_DISABLE_BATT_CHARGE) &&
			    (chg_status->charge_status != WPC_CHG_STATUS_DISABLE_BATT_CHARGE)) {
				if ((chg_status->adapter_type == ADAPTER_TYPE_FASTCHAGE_DASH ||
				     chg_status->adapter_type == ADAPTER_TYPE_FASTCHAGE_WARP) &&
				    (chg_status->charge_type == WPC_CHARGE_TYPE_FAST) &&
				    chg_status->deviation_check_done) {
					if (chip->chg_param.fastchg_fod_enable && chg_status->startup_fod_parm) {
						if (chg_status->adapter_id == 0x00 || chg_status->adapter_id == 0x01)
							wlchg_rx_set_fod_parm(g_rx_chip, chip->chg_param.fastchg_fod_parm);
						else
							wlchg_rx_set_fod_parm(g_rx_chip, chip->chg_param.fastchg_fod_parm_new);
						chg_status->startup_fod_parm = false;
						chg_info("write fastchg fod parm\n");
					}
					pmic_high_vol_en(chip, true);
					wlchg_set_rx_target_voltage(chip, WPC_CHARGE_VOLTAGE_STOP_CHG);
					wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_STOP_CHG);
					chg_status->charge_status = WPC_CHG_STATUS_WAIT_DISABLE_BATT_CHARGE;
				}
			}
		}
	}

	switch (chg_status->charge_status) {
	case WPC_CHG_STATUS_DEFAULT:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_DEFAULT..........\n");
#ifndef IDT_LAB_TEST
		if (!chg_status->adapter_msg_send && (atomic_read(&chip->hb_count) > 0)) {
			chg_err("can't send adapter msg, try again\n");
			wlchg_send_msg(WLCHG_MSG_CHG_INFO, 5, WLCHG_ADAPTER_MSG);
			break;
		}
#endif

		if (chg_status->ftm_mode) {
			if (chg_status->adapter_type == ADAPTER_TYPE_FASTCHAGE_DASH ||
			    chg_status->adapter_type == ADAPTER_TYPE_FASTCHAGE_WARP) {
				chargepump_hw_init();
				chg_status->charge_status = WPC_CHG_STATUS_READY_FOR_FASTCHG;
			}
			break;
		}
		if (chg_status->charge_voltage != WPC_CHARGE_VOLTAGE_DEFAULT) {
			wlchg_set_rx_target_voltage(chip, WPC_CHARGE_VOLTAGE_DEFAULT);
			pmic_high_vol_en(chip, false);
		}

		if (chg_status->adapter_type == ADAPTER_TYPE_UNKNOWN) {
			/*
			 * The energy here cannot be too small, otherwise it
			 * may affect unpacking when reverse charging.
			 */
			if (chg_status->charge_current != WPC_CHARGE_CURRENT_DEFAULT)
				wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_DEFAULT);

			if (wlchg_rx_get_run_mode(rx_chip) == RX_RUNNING_MODE_EPP) {
				chg_err("<~WPC~> RX_RUNNING_MODE_EPP, Change to EPP charge\n");
				chg_status->epp_working = true;
				chg_status->fastchg_display_delay = false;
				chg_status->charge_status = WPC_CHG_STATUS_READY_FOR_EPP;
				break;
			} else if (wlchg_rx_get_run_mode(rx_chip) == RX_RUNNING_MODE_BPP) {
#ifndef IDT_LAB_TEST
				if (!chg_status->get_adapter_err && cmd_info->cmd == 0) {
					rc = wlchg_send_msg(WLCHG_MSG_CHG_INFO, 5, WLCHG_ADAPTER_MSG);
					if (rc < 0)
						break;
				}
				if (chg_status->get_adapter_err || (atomic_read(&chip->hb_count) <= 0)) {
					wait_fast_chg = true;
#else
					wait_fast_chg = false;
#endif
					chg_err("<~WPC~> RX_RUNNING_MODE_BPP, Change to BPP charge\n");
					chg_status->fastchg_display_delay = false;
					chg_status->charge_status = WPC_CHG_STATUS_READY_FOR_BPP;
					break;
#ifndef IDT_LAB_TEST
				}
#endif
			}
		} else {
			if (chg_status->charge_current != WPC_CHARGE_CURRENT_DEFAULT)
				wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_DEFAULT);

#ifdef OP_DEBUG
			if (force_epp) {
				chg_status->epp_working = true;
				chg_status->adapter_type =
					ADAPTER_TYPE_EPP;
			} else if (force_bpp) {
				chg_status->adapter_type =
					ADAPTER_TYPE_NORMAL_CHARGE;
			}
#endif

			if (!chg_status->deviation_check_done &&
			    ((chg_status->adapter_type == ADAPTER_TYPE_FASTCHAGE_DASH) ||
			     (chg_status->adapter_type == ADAPTER_TYPE_FASTCHAGE_WARP))) {
				if (!enable_deviated_check) {
					chg_status->is_deviation = false;
					goto freq_check_done;
				}

				rc = wlchg_rx_get_work_freq(rx_chip, &work_freq);
				if (rc != 0) {
					chg_err("can't read rx work freq\n");
					return rc;
				}
				if (work_freq > chg_param->freq_threshold) {
					chg_status->is_deviation = false;
					chg_info("phone location is correct\n");
				} else {
					chg_status->is_deviation = true;
					chg_info("work_freq=%d\n", work_freq);
				}
freq_check_done:
				chg_status->deviation_check_done = true;
				if (chip->wireless_psy != NULL)
					power_supply_changed(chip->wireless_psy);
			}

			chg_status->fastchg_display_delay = false;
			chg_info("adapter = %d\n", chg_status->adapter_type);
			switch (chg_status->adapter_type) {
			case ADAPTER_TYPE_FASTCHAGE_DASH:
			case ADAPTER_TYPE_FASTCHAGE_WARP:
				if (!chip->quiet_mode_need) {
					chg_status->charge_status =
						WPC_CHG_STATUS_READY_FOR_FASTCHG;
				} else {
					chg_status->charge_status = WPC_CHG_STATUS_READY_FOR_QUIET;
				}
				break;
			case ADAPTER_TYPE_EPP:
				chg_status->charge_status = WPC_CHG_STATUS_READY_FOR_EPP;
				break;
			default:
				chg_status->charge_status =
					WPC_CHG_STATUS_READY_FOR_BPP;
				break;
			}
		}
		break;

	case WPC_CHG_STATUS_READY_FOR_BPP:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_READY_FOR_BPP..........\n");
#ifndef IDT_LAB_TEST
		if ((atomic_read(&chip->hb_count) > 0) &&
		    (chg_status->adapter_type == ADAPTER_TYPE_UNKNOWN)) {
			rc = wlchg_send_msg(WLCHG_MSG_CHG_INFO, 5, WLCHG_TX_ID_MSG);
			if (rc) {
				chg_err("send tx id msg err, tyr again\n");
				break;
			}
		}
		wlchg_set_rx_target_voltage(chip, WPC_CHARGE_VOLTAGE_DEFAULT);
#endif
		wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_DEFAULT);
		chg_status->is_power_changed = true;
		chg_status->charge_status = WPC_CHG_STATUS_BPP;
		break;

	case WPC_CHG_STATUS_BPP:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_BPP..........\n");
#ifndef IDT_LAB_TEST
		if (!chg_status->geted_tx_id &&
		    (chg_status->adapter_type == ADAPTER_TYPE_UNKNOWN) &&
		    (atomic_read(&chip->hb_count) > 0))
			break;
#endif
		if (rx_chip->on_op_trx) {
			wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_ON_TRX);
			chg_info("It's on back of OP phone Trx, set current %d", WPC_CHARGE_CURRENT_ON_TRX);
		} else {
			wait_fast_chg = true;
			wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_BPP);
			chg_info("It's on BPP dock, set current %d", WPC_CHARGE_CURRENT_BPP);
#ifndef IDT_LAB_TEST
			if (atomic_read(&chip->hb_count) > 0) {
				rc = wlchg_send_msg(WLCHG_MSG_CHG_INFO, -1, WLCHG_ADAPTER_MSG);
				if (rc) {
					chg_err("send adapter msg err, tyr again\n");
					break;
				}
			}
#endif
		}

		chg_status->charge_status =
			WPC_CHG_STATUS_BPP_WORKING;
		chip->wireless_type = POWER_SUPPLY_WIRELESS_TYPE_BPP;
		chg_status->startup_fast_chg = false;
		if (chip->wireless_psy != NULL)
			power_supply_changed(chip->wireless_psy);
		break;

	case WPC_CHG_STATUS_BPP_WORKING:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_BPP_WORKING..........\n");
#ifndef IDT_LAB_TEST
		if (wait_fast_chg && ((chg_status->adapter_type != ADAPTER_TYPE_UNKNOWN) &&
			(chg_status->adapter_type != ADAPTER_TYPE_NORMAL_CHARGE))) {
			wait_fast_chg = false;
			chg_status->startup_fast_chg = true;
			chg_status->charge_status = WPC_CHG_STATUS_DEFAULT;
			chip->wireless_type = POWER_SUPPLY_TYPE_UNKNOWN;
		}
		if ((atomic_read(&chip->hb_count) > 0) && chip->heart_stop) {
			rc = wlchg_send_msg(WLCHG_MSG_CHG_INFO, -1, WLCHG_ADAPTER_MSG);
			if (rc)
				chg_err("send adapter msg err, tyr again\n");
			else
				chip->heart_stop = false;
		}
#endif
		break;

	case WPC_CHG_STATUS_READY_FOR_EPP:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_READY_FOR_EPP..........\n");
		pmic_high_vol_en(chip, true);
		wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_DEFAULT);
#ifndef IDT_LAB_TEST
		// EPP not send msg to dock, not adjust voltage.
		wlchg_set_rx_target_voltage_fast(chip, WPC_CHARGE_VOLTAGE_EPP);
		wlchg_send_msg(WLCHG_MSG_CHG_INFO, -1, WLCHG_FASTCHAGE_MSG);
#endif
		chg_status->is_power_changed = true;
		chg_status->charge_status = WPC_CHG_STATUS_EPP;
		break;

	case WPC_CHG_STATUS_EPP:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_EPP..........\n");
		if (chg_status->epp_working) {
			if (chg_status->vol_set_ok) {
				wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_EPP);
				chg_status->charge_status =
					WPC_CHG_STATUS_EPP_WORKING;
				chip->wireless_type = POWER_SUPPLY_WIRELESS_TYPE_EPP;
			}
			chg_status->startup_fast_chg = false;
			if (chip->wireless_psy != NULL)
				power_supply_changed(chip->wireless_psy);
		}
		break;

	case WPC_CHG_STATUS_EPP_WORKING:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_EPP_WORKING..........\n");
		pmic_chan_check_skin_temp(chip);
		break;

	case WPC_CHG_STATUS_READY_FOR_QUIET:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_READY_FOR_QUIET..........\n");
		if (chg_status->quiet_mode_enabled) {
			if (chip->wireless_psy != NULL)
				power_supply_changed(chip->wireless_psy);
			if (chg_status->charge_type != WPC_CHARGE_TYPE_FAST) {
				wlchg_set_rx_target_voltage_fast(chip, WPC_CHARGE_VOLTAGE_EPP);
				wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_WAIT_FAST);
				rc = wlchg_send_msg(WLCHG_MSG_CHG_INFO, -1, WLCHG_FASTCHAGE_MSG);
				if (rc) {
					chg_err("send fast charge msg err, try again\n");
					break;
				}
			}
			vote(chip->fastchg_disable_votable, QUIET_VOTER, true, 0);
			chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_EXIT;
		} else {
			if (!chip->quiet_mode_need) {
				chg_err("quiet mode has been disabled, not waiting responds.");
				chg_status->charge_status = WPC_CHG_STATUS_DEFAULT;
			}
		}
		break;

	case WPC_CHG_STATUS_READY_FOR_FASTCHG:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_READY_FOR_FASTCHG..........\n");
		wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_WAIT_FAST);
		wlchg_set_rx_target_voltage_fast(chip, WPC_CHARGE_VOLTAGE_FASTCHG_INIT);
		pmic_high_vol_en(chip, true);
		rc = wlchg_send_msg(WLCHG_MSG_CHG_INFO, -1, WLCHG_FASTCHAGE_MSG);
		if (rc) {
			chg_err("send fast charge msg err, try again\n");
			break;
		}
		if (chip->ap_ctrl_dcdc)
			op_set_dcdc_en_value(1);
		if (exchgpump_bq != NULL) {
			chg_info("enable bq2597x adc.");
			bq2597x_enable_adc(exchgpump_bq, true);
		}
		chg_status->is_power_changed = true;
		chg_status->charge_status =
			WPC_CHG_STATUS_WAITING_FOR_TX_INTO_FASTCHG;
		break;

	case WPC_CHG_STATUS_WAITING_FOR_TX_INTO_FASTCHG:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_WAITING_FOR_TX_INTO_FASTCHG..........\n");
		if (chg_status->vol_set_ok &&
		    (chg_status->charge_type == WPC_CHARGE_TYPE_FAST)) {
			if (chg_status->ftm_mode) {
				if (!chip->batt_psy) {
					chip->batt_psy = power_supply_get_by_name("battery");
					if (!chip->batt_psy) {
						chg_err("battery psy is not ready\n");
						break;
					}
				}
				pval.intval = 0;
				rc = power_supply_set_property(chip->batt_psy,
					POWER_SUPPLY_PROP_INPUT_SUSPEND, &pval);
				if (rc < 0) {
					pr_err("Couldn't set input_suspend rc=%d\n", rc);
					break;
				}
				chargepump_hw_init();
				rc = chargepump_check_config();
				if (rc) {
					chg_err("charge pump status error\n");
					break;
				}
				chargepump_enable();
				chargepump_set_for_LDO();
				wlchg_set_rx_target_voltage(chip, WPC_CHARGE_VOLTAGE_FTM);
				chg_status->charge_status =
					WPC_CHG_STATUS_READY_FOR_FTM;
			} else {
				if (((chg_status->temp_region == WLCHG_BATT_TEMP_PRE_NORMAL) ||
				     (chg_status->temp_region == WLCHG_BATT_TEMP_NORMAL)) &&
				    (chip->batt_volt > chg_param->fastchg_vol_min) &&
				    (chip->batt_volt < chg_param->fastchg_vol_entry_max) &&
				    (chip->soc >= chg_param->fastchg_soc_min) &&
				    (chip->soc <= chg_param->fastchg_soc_max)) {
					chg_status->fastchg_startup_step = FASTCHG_EN_CHGPUMP1_STEP;
					chg_status->charge_status = WPC_CHG_STATUS_INCREASE_VOLTAGE;
					if (chip->chg_param.fastchg_fod_enable) {
						wlchg_rx_set_fod_parm(g_rx_chip, chip->chg_param.fastchg_fod_parm_startup);
						chg_status->startup_fod_parm = true;
						chg_info("write fastchg startup fod parm\n");
					}
				} else {
					if (chip->batt_volt >= chg_param->fastchg_vol_entry_max)
						vote(chip->fastchg_disable_votable, BATT_VOL_VOTER, true, 0);
					chg_err("batt_temp=%d, batt_volt=%d, soc=%d\n",
						chip->temperature, chip->batt_volt, chip->soc);
					chg_status->vol_not_step = true;
					chg_status->startup_fast_chg = false;
					chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_EXIT;
				}
			}
		}
		chg_info("vol_set_ok=%d, charge_type=%d", chg_status->vol_set_ok, chg_status->charge_type);
		break;

	case WPC_CHG_STATUS_INCREASE_VOLTAGE:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_INCREASE_VOLTAGE..........\n");
		if (chip->batt_volt >= chg_param->fastchg_vol_entry_max &&
		    chg_status->fastchg_startup_step < FASTCHG_EN_CHGPUMP2_STEP) {
			chg_err("battert voltage too high\n");
			chg_status->startup_fast_chg = false;
			vote(chip->fastchg_disable_votable, BATT_VOL_VOTER, true, 0);
			chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_EXIT;
			break;
		}
		fastchg_startup_process(chip);
		break;

	case WPC_CHG_STATUS_FAST_CHARGING_FROM_CHGPUMP:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_FAST_CHARGING_FROM_CHGPUMP..........\n");
		if (rx_chip->chg_data.iout > 500) {
			if (chg_status->charge_current > WPC_CHARGE_CURRENT_ZERO) {
				chg_err("<~WPC~> Iout > 500mA & ChargeCurrent > 200mA. Disable pmic\n");
				wlchg_set_rx_charge_current(chip, 0);
				pmic_high_vol_en(chip, false);
				update_wlchg_started(true);
			}
		}

		if (chg_status->is_deviation) {
			temp_val = get_client_vote(chip->wlcs_fcc_votable, MAX_VOTER);
			if (temp_val == FASTCHG_CURR_30W_MAX_UA) {
				if (rx_chip->chg_data.iout > 1000)
					chg_status->is_deviation = false;
			} else {
				if (rx_chip->chg_data.iout > 800)
					chg_status->is_deviation = false;
			}
		}

		if (fastchg_err_check(chip) > 0)
			break;
		fastchg_temp_check(chip);
		fastchg_check_skin_temp(chip);
		fastchg_cep_adj(chip);
		fastchg_check_ibat(chip);

#ifdef HW_TEST_EDITION
		if ((chip->icharging > 5600) &&
		    (chip->w30w_work_started == false)) {
			chip->w30w_work_started = true;
			schedule_delayed_work(
				&chip->w30w_timeout_work,
				round_jiffies_relative(msecs_to_jiffies(
					(chip->w30w_time) * 60 * 1000)));
		}
#endif
		break;

	case WPC_CHG_STATUS_FAST_CHARGING_EXIT:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_FAST_CHARGING_EXIT..........\n");
		if (chip->chg_param.fastchg_fod_enable && chg_status->startup_fod_parm &&
		    chg_status->charge_type == WPC_CHARGE_TYPE_FAST) {
			if (chg_status->adapter_id == 0x00 || chg_status->adapter_id == 0x01)
				wlchg_rx_set_fod_parm(g_rx_chip, chip->chg_param.fastchg_fod_parm);
			else
				wlchg_rx_set_fod_parm(g_rx_chip, chip->chg_param.fastchg_fod_parm_new);
			chg_status->startup_fod_parm = false;
			chg_info("write fastchg fod parm\n");
		}
		pmic_high_vol_en(chip, true);
		wlchg_set_rx_target_voltage(chip, WPC_CHARGE_VOLTAGE_EPP);
		wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_CHGPUMP_TO_CHARGER);
		chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_WAIT_EXIT;
		break;

	case WPC_CHG_STATUS_FAST_CHARGING_WAIT_EXIT:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_FAST_CHARGING_WAIT_EXIT..........\n");
		if (chg_status->vol_set_ok) {
			update_wlchg_started(false);
			chg_status->startup_fast_chg = false;
			chg_status->is_power_changed = true;
			chargepump_disable();
			bq2597x_enable_charge_pump(false);
			wlchg_set_rx_charge_current_step(chip, WPC_CHARGE_CURRENT_EPP);
			chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_FFC;
			if (!chg_status->cep_timeout_adjusted && chip->soc > chg_param->fastchg_soc_max)
				wlchg_send_msg(WLCHG_MSG_CHG_INFO, -1, WLCHG_CEP_TIMEOUT_MSG);
		}
		break;

	case WPC_CHG_STATUS_FAST_CHARGING_FFC:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_FAST_CHARGING_FFC..........\n");
		if (chg_status->ffc_check) {
			if (chg_status->temp_region == WLCHG_BATT_TEMP_NORMAL) {
				wlchg_set_rx_charge_current_step(chip, WPC_CHARGE_CURRENT_EPP);
				op_switch_normal_set();
				chg_status->ffc_check = false;
			} else {
				chg_err("battery temp = %d, can't run normal charge ffc\n", chip->temperature);
			}
			chg_status->is_power_changed = true;
			chg_status->charge_status = WPC_CHG_STATUS_FAST_CHARGING_FROM_PMIC;
		} else {
			if (((chg_status->temp_region == WLCHG_BATT_TEMP_PRE_NORMAL) ||
			     (chg_status->temp_region == WLCHG_BATT_TEMP_NORMAL)) &&
			    (chip->batt_volt < chg_param->fastchg_vol_entry_max) &&
			    (chip->soc >= chg_param->fastchg_soc_min) &&
			    (chip->soc <= chg_param->fastchg_soc_max)) {

				if (chg_status->quiet_mode_enabled &&
				    (chg_status->charge_type != WPC_CHARGE_TYPE_FAST)) {
					chg_err("quiet mode, but dock hasn't into fast type.");
					break;
				}
				wlchg_fastchg_restart_check(chip);
				if (!chg_status->fastchg_disable) {
					chg_status->is_power_changed = true;
					chg_status->startup_fast_chg = true;
					chg_status->charge_status = WPC_CHG_STATUS_READY_FOR_FASTCHG;
				}
			} else {
				if (chip->batt_volt >= chg_param->fastchg_vol_entry_max)
					vote(chip->fastchg_disable_votable, BATT_VOL_VOTER, true, 0);
			}

			wlchg_check_term_charge(chip);
		}
		break;

	case WPC_CHG_STATUS_FAST_CHARGING_FROM_PMIC:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_FAST_CHARGING_FROM_PMIC..........\n");
		wlchg_check_term_charge(chip);
		break;

	case WPC_CHG_STATUS_WAIT_DISABLE_BATT_CHARGE:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_WAIT_DISABLE_BATT_CHARGE..........\n");
		if (chg_status->vol_set_ok) {
			update_wlchg_started(false);
			chg_status->startup_fast_chg = false;
			chg_status->is_power_changed = true;
			chargepump_disable();
			bq2597x_enable_charge_pump(false);
			wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_STOP_CHG);
			chg_status->charge_status = WPC_CHG_STATUS_DISABLE_BATT_CHARGE;
		}
		break;

	case WPC_CHG_STATUS_DISABLE_BATT_CHARGE:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_DISABLE_BATT_CHARGE..........\n");
		if (!chip->disable_batt_charge) {
			chg_status->startup_fast_chg = true;
			chg_status->is_power_changed = true;
			chg_status->charge_status = WPC_CHG_STATUS_DEFAULT;
		}
		break;

	case WPC_CHG_STATUS_READY_FOR_FTM:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_READY_FOR_FTM..........\n");
		if (chg_status->vol_set_ok) {
			chg_status->startup_fast_chg = false;
			bq2597x_enable_charge_pump(true);
			msleep(500);
			if (exchgpump_bq != NULL)
				bq2597x_check_charge_enabled(exchgpump_bq, &cp2_is_enabled);
			if (cp2_is_enabled)
				chg_status->charge_status = WPC_CHG_STATUS_FTM_WORKING;
			else
				chg_err("wkcs: can't enable charge pump 2\n");
		}
		break;

	case WPC_CHG_STATUS_FTM_WORKING:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_FTM_WORKING..........\n");
		if (exchgpump_bq != NULL)
			bq2597x_check_charge_enabled(exchgpump_bq, &cp2_is_enabled);
		if (!cp2_is_enabled) {
			chg_err("wkcs: charge pump 2 err\n");
			chg_status->charge_status = WPC_CHG_STATUS_READY_FOR_FTM;
		}
		break;

	default:
		chg_err("<~WPC~> ..........WPC_CHG_STATUS_ERROR..........\n");
		break;
	}

	return 0;
}

void wlchg_dischg_status(struct op_chg_chip *chip)
{
	char tx_status = 0, err_flag = 0;
	int rc = 0;
	static int trycount;

	if (g_rx_chip == NULL) {
		chg_err("rc chip is not ready\n");
		return;
	}

	rc = wlchg_rx_get_idt_rtx_status(g_rx_chip, &tx_status, &err_flag);
	if (rc) {
		chg_err("can't get trx status\n");
		return;
	}
	chg_err("<~WPC~>rtx func status:0x%02x, err:0x%02x, wpc_dischg_status[%d]\n",
		tx_status, err_flag, chip->wlchg_status.wpc_dischg_status);
	if (err_flag != 0) {
		if (TRX_ERR_TX_RXAC & err_flag) {
			chip->wlchg_status.wpc_dischg_status = WPC_DISCHG_IC_ERR_TX_RXAC;
		} else if (TRX_ERR_TX_OCP & err_flag) {
			//chip->wlchg_status.wpc_dischg_status = WPC_DISCHG_IC_ERR_TX_OCP;
			// not care
			chg_err("ERR_TX_OCP error occurs.");
		} else if (TRX_ERR_TX_OVP & err_flag) {
			chip->wlchg_status.wpc_dischg_status = WPC_DISCHG_IC_ERR_TX_OVP;
		} else if (TRX_ERR_TX_LVP & err_flag) {
			chip->wlchg_status.wpc_dischg_status = WPC_DISCHG_IC_ERR_TX_LVP;
		} else if (TRX_ERR_TX_FOD & err_flag) {
			chip->wlchg_status.wpc_dischg_status = WPC_DISCHG_IC_ERR_TX_FOD;
		} else if (TRX_ERR_TX_OTP & err_flag) {
			chip->wlchg_status.wpc_dischg_status = WPC_DISCHG_IC_ERR_TX_OTP;
		} else if (TRX_ERR_TX_CEPTIMEOUT & err_flag) {
			//chip->wlchg_status.wpc_dischg_status = WPC_DISCHG_IC_ERR_TX_CEPTIMEOUT;
			// not care
			chg_err("ERR_TX_CEPTIMEOUT error occurs.");
		} else if (TRX_ERR_TX_RXEPT & err_flag) {
			chip->wlchg_status.wpc_dischg_status = WPC_DISCHG_IC_ERR_TX_RXEPT;
		}

		if (chip->wlchg_status.wpc_dischg_status >= WPC_DISCHG_IC_ERR_TX_RXAC) {
			chg_err("There is error-%d occurred, disable Trx func.",
					chip->wlchg_status.wpc_dischg_status);
			chip->wlchg_status.wpc_dischg_status = WPC_DISCHG_STATUS_OFF;
			return;
		}
	}

	if (tx_status != 0) {
		if (TRX_READY & tx_status) {
			chip->wlchg_status.tx_online = false;
			chip->wlchg_status.wpc_dischg_status =
				WPC_DISCHG_IC_READY;
			wlchg_rx_trx_enbale(g_rx_chip, true);
			schedule_delayed_work(&chip->dischg_work, WPC_DISCHG_WAIT_READY_EVENT);
			trycount = 0;
		} else if (TRX_DIGITALPING & tx_status ||
			   TRX_ANALOGPING & tx_status) {
			chip->wlchg_status.tx_online = false;
			if (WPC_DISCHG_IC_PING_DEVICE ==
			    chip->wlchg_status.wpc_dischg_status) {
				chg_err("<~WPC~>rtx func no device to be charged, 60s timeout, disable TRX!\n");
				chip->wlchg_status.wpc_dischg_status = WPC_DISCHG_STATUS_OFF;
			} else {
				chip->wlchg_status.wpc_dischg_status = WPC_DISCHG_IC_PING_DEVICE;
				schedule_delayed_work(&chip->dischg_work, WPC_DISCHG_WAIT_DEVICE_EVENT);
				chg_err("<~WPC~>rtx func waiting device 60s......\n");
			}
		} else if (TRX_TRANSFER & tx_status) {
			chip->wlchg_status.wpc_dischg_status = WPC_DISCHG_IC_TRANSFER;
			chip->wlchg_status.tx_online = true;
			// check status per 5s if in IC_TRANSFER.
			schedule_delayed_work(&chip->dischg_work, WPC_DISCHG_POLL_STATUS_EVENT);
			chg_err("<~WPC~>rtx func in discharging now, check status 5 seconds later!\n");
		}
		if (chip->wireless_psy != NULL) {
			if (exfg_instance != NULL)
				exfg_instance->set_allow_reading(true);
			power_supply_changed(chip->wireless_psy);
			chg_info("reported status change event.");
		}
	}

	if (chip->wlchg_status.wpc_dischg_status == WPC_DISCHG_IC_TRANSFER) {
		if (!reverse_charge_status) {
			reverse_charge_notifier_call_chain(1);
			reverse_charge_status = 1;
		}
	}
	if (chip->wlchg_status.wpc_dischg_status != WPC_DISCHG_IC_TRANSFER) {
		if (reverse_charge_status) {
			reverse_charge_notifier_call_chain(0);
			reverse_charge_status = 0;
		}
	}
	if ((tx_status == 0) && (err_flag == 0)) {
		// try again 5 times.
		if (trycount++ >= 5) {
			trycount = 0;
			chip->wlchg_status.wpc_dischg_status =
				WPC_DISCHG_STATUS_OFF;
		}
		schedule_delayed_work(&chip->dischg_work, WPC_DISCHG_WAIT_STATUS_EVENT);
	}

	return;
}

static void wlchg_dischg_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct op_chg_chip *chip =
		container_of(dwork, struct op_chg_chip, dischg_work);

	chg_err("<~WPC~>rtx func wpc_dischg_status[%d]\n",
		chip->wlchg_status.wpc_dischg_status);
	wlchg_dischg_status(chip);
	return;
}

int wlchg_tx_callback(void)
{
	struct op_chg_chip *chip = g_op_chip;
	if (chip == NULL) {
		chg_err("g_op_chip not exist, return\n");
		return -ENODEV;
	}
	chg_err("rtx func chip->chg_data.wpc_dischg_status[%d]\n",
		chip->wlchg_status.wpc_dischg_status);
	if (chip->wlchg_status.wpc_dischg_status == WPC_DISCHG_STATUS_ON ||
	    chip->wlchg_status.wpc_dischg_status == WPC_DISCHG_IC_READY ||
	    chip->wlchg_status.wpc_dischg_status ==
		    WPC_DISCHG_IC_PING_DEVICE ||
	    chip->wlchg_status.wpc_dischg_status ==
		    WPC_DISCHG_IC_TRANSFER) {
		cancel_delayed_work_sync(&chip->dischg_work);
		wlchg_dischg_status(chip);
	}
	return 0;
}

int switch_to_otg_mode(bool enable)
{
	if (g_op_chip == NULL) {
		chg_err("op_wireless_ic not exist, return\n");
		return -ENODEV;
	}


	if (enable) {
		op_set_wrx_en_value(2);
	} else {
		if (g_op_chip->wireless_mode == WIRELESS_MODE_NULL)
			op_set_wrx_en_value(0);
	}

	return 0;
}

static void wlchg_connect_func(struct op_chg_chip *chip)
{
	chg_err("<~WPC~> wpc dock has connected!>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	if (chip == NULL) {
		chg_err("g_op_chip not ready\n");
		return;
	}
	if (normal_charger == NULL) {
		chg_err("smbchg not ready\n");
		return;
	}
	if (g_rx_chip == NULL) {
		chg_err("g_rx_chip not ready\n");
		return;
	}
	if (exfg_instance == NULL) {
		chg_err("fuelgauger not ready\n");
		return;
	}

	if (chip->wireless_mode != WIRELESS_MODE_TX) {
		check_batt_present(chip);
		if (chip->batt_missing) {
			wlchg_rx_set_chip_sleep(1);
			chg_err("battery miss\n");
			return;
		}

		if (!chip->wlchg_wake_lock_on) {
			chg_info("acquire wlchg_wake_lock\n");
			__pm_stay_awake(chip->wlchg_wake_lock);
			chip->wlchg_wake_lock_on = true;
		} else {
			chg_err("wlchg_wake_lock is already stay awake.");
		}

		chip->wireless_mode = WIRELESS_MODE_RX;
		if (normal_charger != NULL) {
			normal_charger->real_charger_type =
				POWER_SUPPLY_TYPE_WIRELESS;
			normal_charger->usb_psy_desc.type =
				POWER_SUPPLY_TYPE_WIRELESS;
			vote(normal_charger->usb_icl_votable,
				SW_ICL_MAX_VOTER, true, PMIC_ICL_MAX);
		}
		wlchg_init_connected_task(chip);
		op_set_wrx_en_value(2);
		wlchg_rx_get_run_flag(g_rx_chip);
		schedule_delayed_work(&chip->update_bat_info_work, 0);
		schedule_delayed_work(&chip->wlchg_task_work, 0);
		//schedule_delayed_work(&chip->fastchg_curr_vol_work, 0);
		wlchg_set_rx_charge_current(chip, WPC_CHARGE_CURRENT_INIT_100MA);
		chip->wlchg_status.startup_fast_chg = true;
		wlchg_send_msg(WLCHG_MSG_CHG_INFO, 5, WLCHG_ADAPTER_MSG);
		exfg_instance->set_allow_reading(true);
	} else {
		chg_info("reverse charge did not exit, wait 100ms\n");
		schedule_delayed_work(&chip->wlchg_connect_check_work, msecs_to_jiffies(100));
	}
}

static void wlchg_disconnect_func(struct op_chg_chip *chip)
{
	union power_supply_propval pval = {0, };
	unsigned long delay_time;
	int rc;

	if (chip->wlchg_status.curr_err_count > FASTCHG_CURR_ERR_MAX)
		delay_time = jiffies + WLCHG_ACTIVE_DISCONNECT_DELAYED;
	else
		delay_time = jiffies + WLCHG_DISCONNECT_DELAYED;
	chg_err("<~WPC~> wpc dock has disconnected!< < < < < < < < < < < < <\n");
	if (chip == NULL) {
		chg_err("g_op_chip not exist, return\n");
		return;
	}

	if (chip->wireless_mode != WIRELESS_MODE_TX) {
		chip->wireless_mode = WIRELESS_MODE_NULL;
		if (!typec_is_otg_mode())
			op_set_wrx_en_value(0);
	}
	if (chip->ap_ctrl_dcdc)
		op_set_dcdc_en_value(0);
	chip->wlchg_status.charge_online = false;
	chip->disable_batt_charge = false;
	if (((chip->wlchg_status.adapter_type == ADAPTER_TYPE_FASTCHAGE_DASH) ||
	     (chip->wlchg_status.adapter_type == ADAPTER_TYPE_FASTCHAGE_WARP)) &&
	    ((chip->wlchg_status.temp_region == WLCHG_BATT_TEMP_PRE_NORMAL) ||
	     (chip->wlchg_status.temp_region == WLCHG_BATT_TEMP_NORMAL)) &&
	    chip->wlchg_status.deviation_check_done) {
		chip->wlchg_status.fastchg_display_delay = true;
	}
	cancel_delayed_work_sync(&chip->wlchg_task_work);
	cancel_delayed_work_sync(&chip->update_bat_info_work);
	cancel_delayed_work_sync(&chip->fastchg_curr_vol_work);
	vote(chip->fastchg_disable_votable, QUIET_VOTER, false, 0);
	vote(chip->fastchg_disable_votable, CEP_VOTER, false, 0);
	vote(chip->fastchg_disable_votable, FFC_VOTER, false, 0);
	vote(chip->fastchg_disable_votable, SKIN_VOTER, false, 0);
	vote(chip->fastchg_disable_votable, BATT_VOL_VOTER, false, 0);
	vote(chip->fastchg_disable_votable, BATT_CURR_VOTER, false, 0);
	vote(chip->fastchg_disable_votable, STARTUP_CEP_VOTER, false, 0);
	vote(chip->fastchg_disable_votable, HW_ERR_VOTER, false, 0);
	vote(chip->fastchg_disable_votable, CURR_ERR_VOTER, false, 0);
	vote(chip->wlcs_fcc_votable, DEF_VOTER, false, 0);
	vote(chip->wlcs_fcc_votable, MAX_VOTER, false, 0);
	vote(chip->wlcs_fcc_votable, STEP_VOTER, false, 0);
	vote(chip->wlcs_fcc_votable, EXIT_VOTER, false, 0);
	vote(chip->wlcs_fcc_votable, FFC_VOTER, false, 0);
	vote(chip->wlcs_fcc_votable, JEITA_VOTER, false, 0);
	vote(chip->wlcs_fcc_votable, CEP_VOTER, false, 0);
	vote(chip->wlcs_fcc_votable, SKIN_VOTER, false, 0);
	wlchg_deinit_after_disconnected(chip);
	if (normal_charger != NULL) {
		// disable wireless vote client
		vote(normal_charger->usb_icl_votable, WIRED_CONN_VOTER, false, 0);
		vote(normal_charger->usb_icl_votable, WLCH_VOTER, false, 0);
		vote(normal_charger->fcc_votable, WLCH_VOTER, false, 0);
		vote(normal_charger->chg_disable_votable, WLCH_VOTER, false, 0);
		vote(normal_charger->usb_icl_votable, WLCH_FFC_VOTER, false, 0);
		vote(normal_charger->usb_icl_votable, WLCH_SKIN_VOTER, false, 0);
		normal_charger->wireless_present = false;
		// remove typec related icl vote
		vote(normal_charger->usb_icl_votable, CHG_TERMINATION_VOTER, false, 0);
		//wireless_present must before below func call.
		pmic_high_vol_en(chip, false);
	}
	vote(chip->wlcs_fcc_votable, MAX_VOTER, true, 0);
	if (chip->wlchg_status.dock_on) {
		chip->wlchg_status.dock_on = false;
		cancel_delayed_work_sync(&chip->charger_exit_work);
		/*
		 * Here need to recalculate the time that needs to be delayed to
		 * compensate the time consumption from receiving the disconnect
		 * signal to running here.
		 */
		if (time_is_before_jiffies(delay_time) || !chg_icon_update_delay)
			delay_time = 0;
		else
			delay_time = delay_time - jiffies;
		schedule_delayed_work(&chip->charger_exit_work, delay_time);
	} else {
		if (chip->wlchg_wake_lock_on) {
			chg_info("release wlchg_wake_lock\n");
			__pm_relax(chip->wlchg_wake_lock);
			chip->wlchg_wake_lock_on = false;
		} else {
			chg_err("wlchg_wake_lock is already relax\n");
		}
	}

	if (chip->wlchg_status.ftm_mode) {
		if (!chip->batt_psy) {
			chip->batt_psy = power_supply_get_by_name("battery");
			if (!chip->batt_psy) {
				chg_err("battery psy is not ready\n");
				return;
			}
		}
		pval.intval = 1;
		rc = power_supply_set_property(chip->batt_psy,
			POWER_SUPPLY_PROP_INPUT_SUSPEND, &pval);
		if (rc < 0) {
			pr_err("Couldn't set input_suspend rc=%d\n", rc);
		}
	}
}

int wlchg_connect_callback_func(bool ldo_on)
{
  	struct op_chg_chip *chip = g_op_chip;

	if (chip == NULL) {
		chg_err("g_op_chip not exist, return\n");
		return -ENODEV;
	}

	mutex_lock(&chip->connect_lock);
	if (wlchg_get_usbin_val() == 0 && wlchg_rx_get_chip_con() == 1) {
		if (!(chip->wlchg_status.dock_on)) {
			chg_err("report wlchg online.");
			wlchg_set_rx_charge_current(chip, 0);
			op_set_wrx_en_value(2);
			chip->wlchg_status.dock_on = true;
			chip->charger_exist = true;
			if (normal_charger != NULL)
				normal_charger->wireless_present = true;
			if (chip->wireless_psy != NULL)
				power_supply_changed(chip->wireless_psy);
		}
		if (ldo_on) {
			chg_err("connected really.");
			wlchg_connect_func(chip);
		}
	} else {
		wlchg_disconnect_func(chip);
	}
	mutex_unlock(&chip->connect_lock);

	return 0;
}

static void wlchg_connect_check_work(struct work_struct *work)
{
	wlchg_connect_callback_func(true);
}

static void wlchg_usbin_int_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct op_chg_chip *chip =
		container_of(dwork, struct op_chg_chip, usbin_int_work);
	int level;

	if (!chip) {
		chg_err("wlchg driver not ready\n");
		return;
	}

	level = wlchg_get_usbin_val();
	msleep(50);
	if (level != wlchg_get_usbin_val()) {
		chg_err("Level duration is too short to ignore\n");
		return;
	}

	if (normal_charger != NULL)
		vote(normal_charger->awake_votable, WIRED_CONN_VOTER, true, 0);

	printk(KERN_ERR
	       "[OP_CHG][%s]: op-wlchg test level[%d], chip->otg_switch[%d]\n",
	       __func__, level, g_op_chip->otg_switch);
	if (level == 1) {
		if (normal_charger != NULL && normal_charger->wireless_present) {
			normal_charger->wireless_present = false;
			normal_charger->apsd_delayed = true;
		}
		wpc_chg_quit_max_cnt = 0;
		wlchg_rx_set_chip_sleep(1);
		if ((chip->wireless_mode == WIRELESS_MODE_NULL) &&
		    !chip->wlchg_status.dock_on &&
		    !chip->wlchg_status.tx_present) {
			normal_charger->apsd_delayed = false;
			vote(normal_charger->awake_votable, WIRED_CONN_VOTER, false, 0);
			return;
		}
		msleep(100);
		wlchg_enable_tx_function(false);
		schedule_delayed_work(&chip->wait_wpc_chg_quit, 0);
	} else {
		if (!chip->disable_charge)
			wlchg_rx_set_chip_sleep(0);
		msleep(20);
		wlchg_rx_set_chip_en(0);
		if (g_op_chip->otg_switch) {
			wlchg_enable_tx_function(true);
		}
		if (g_op_chip->pd_charger_online)
			g_op_chip->pd_charger_online = false;
		vote(normal_charger->awake_votable, WIRED_CONN_VOTER, false, 0);
		return;
	}

	if (normal_charger != NULL) {
		if (get_prop_fast_chg_started(normal_charger)) {
			chg_err("wkcs: is dash on, exit\n");
			normal_charger->apsd_delayed = false;
			vote(normal_charger->awake_votable, WIRED_CONN_VOTER, false, 0);
			return;
		}
		op_handle_usb_plugin(normal_charger);
		msleep(100);
		smblib_apsd_enable(normal_charger, true);
		smblib_rerun_apsd_if_required(normal_charger);
		msleep(50);
		normal_charger->apsd_delayed = false;
		vote(normal_charger->awake_votable, WIRED_CONN_VOTER, false, 0);
	}
	return;
}

static void wlchg_usbin_int_shedule_work(void)
{
	if (normal_charger == NULL) {
		chg_err("smbchg not ready\n");
		return;
	}

	if (g_rx_chip == NULL) {
		chg_err("g_rx_chip not ready\n");
		return;
	}

	if (typec_is_otg_mode()) {
		chg_err("wkcs: is otg mode, exit\n");
		return;
	}

	if (!g_op_chip) {
		chg_err(" g_rx_chip is NULL\n");
	} else {
		cancel_delayed_work(&g_op_chip->usbin_int_work);
		if (!g_op_chip->pd_charger_online)
			schedule_delayed_work(&g_op_chip->usbin_int_work, 0);
		else {
			chg_info("PD is in, usbin irq work func delay 1 seconds run.");
			schedule_delayed_work(&g_op_chip->usbin_int_work, WLCHG_PD_HARDRESET_WAIT_TIME);
		}
	}
	chg_err("usbin irq happened\n");
}

static irqreturn_t irq_usbin_event_int_handler(int irq, void *dev_id)
{
	chg_err("op-wlchg test usbin_int.\n");
	wlchg_usbin_int_shedule_work();
	return IRQ_HANDLED;
}

static void wlchg_set_usbin_int_active(struct op_chg_chip *chip)
{
	gpio_direction_input(chip->usbin_int_gpio); // in
	pinctrl_select_state(chip->pinctrl, chip->usbin_int_active); // no_PULL
}

static void wlchg_usbin_int_irq_init(struct op_chg_chip *chip)
{
	chip->usbin_int_irq = gpio_to_irq(chip->usbin_int_gpio);

	chg_err("op-wlchg test %s chip->usbin_int_irq[%d]\n", __func__,
	       chip->usbin_int_irq);
}

static int wlchg_usbin_int_eint_register(struct op_chg_chip *chip)
{
	int retval = 0;

	wlchg_set_usbin_int_active(chip);
	retval = devm_request_irq(chip->dev, chip->usbin_int_irq,
				  irq_usbin_event_int_handler,
				  IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING |
				  IRQF_ONESHOT,
				  "wlchg_usbin_int", chip);
	if (retval < 0) {
		chg_err("%s request usbin_int irq failed.\n", __func__);
	}
	return retval;
}

static int wlchg_usbin_int_gpio_init(struct op_chg_chip *chip)
{
	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -EINVAL;
	}

	//usbin_int
	chip->usbin_int_active =
		pinctrl_lookup_state(chip->pinctrl, "usbin_int_active");
	if (IS_ERR_OR_NULL(chip->usbin_int_active)) {
		chg_err("get usbin_int_active fail\n");
		return -EINVAL;
	}

	chip->usbin_int_sleep =
		pinctrl_lookup_state(chip->pinctrl, "usbin_int_sleep");
	if (IS_ERR_OR_NULL(chip->usbin_int_sleep)) {
		chg_err("get usbin_int_sleep fail\n");
		return -EINVAL;
	}

	chip->usbin_int_default =
		pinctrl_lookup_state(chip->pinctrl, "usbin_int_default");
	if (IS_ERR_OR_NULL(chip->usbin_int_default)) {
		chg_err("get usbin_int_default fail\n");
		return -EINVAL;
	}

	if (chip->usbin_int_gpio > 0) {
		gpio_direction_input(chip->usbin_int_gpio);
	}

	pinctrl_select_state(chip->pinctrl, chip->usbin_int_active);

	return 0;
}

int wlchg_get_usbin_val(void)
{
	struct op_chg_chip *chip = g_op_chip;

	if (chip == NULL) {
		chg_err("g_op_chip not exist, return\n");
		return -ENODEV;
	}

	if (chip->usbin_int_gpio <= 0) {
		chg_err("usbin_int_gpio not exist, return\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->usbin_int_active) ||
	    IS_ERR_OR_NULL(chip->usbin_int_sleep)) {
		chg_err("pinctrl null, return\n");
		return -EINVAL;
	}

	if (typec_is_otg_mode())
		return 0;
	return gpio_get_value(chip->usbin_int_gpio);
}

static int wlchg_wrx_en_gpio_init(struct op_chg_chip *chip)
{
	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -EINVAL;
	}

	//wrx_en
	chip->wrx_en_active =
		pinctrl_lookup_state(chip->pinctrl, "wrx_en_active");
	if (IS_ERR_OR_NULL(chip->wrx_en_active)) {
		chg_err("get wrx_en_active fail\n");
		return -EINVAL;
	}

	chip->wrx_en_sleep =
		pinctrl_lookup_state(chip->pinctrl, "wrx_en_sleep");
	if (IS_ERR_OR_NULL(chip->wrx_en_sleep)) {
		chg_err("get wrx_en_sleep fail\n");
		return -EINVAL;
	}

	chip->wrx_en_default =
		pinctrl_lookup_state(chip->pinctrl, "wrx_en_default");
	if (IS_ERR_OR_NULL(chip->wrx_en_default)) {
		chg_err("get wrx_en_default fail\n");
		return -EINVAL;
	}

	gpio_direction_output(chip->wrx_en_gpio, 0);
	pinctrl_select_state(chip->pinctrl, chip->wrx_en_sleep);

	chg_err("gpio_val:%d\n", gpio_get_value(chip->wrx_en_gpio));

	return 0;
}

void op_set_wrx_en_value(int value)
{
	struct op_chg_chip *chip = g_op_chip;

	if (!chip) {
		chg_err("op_chg_chip not ready, return\n");
		return;
	}

	if (chip->wrx_en_gpio <= 0) {
		chg_err("idt_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->wrx_en_active) ||
	    IS_ERR_OR_NULL(chip->wrx_en_sleep)) {
		chg_err("pinctrl null, return\n");
		return;
	}

	if (value == 2) {
		gpio_direction_output(chip->wrx_en_gpio, 1);
		pinctrl_select_state(chip->pinctrl, chip->wrx_en_active);
	} else {
		//gpio_direction_output(chip->wrx_en_gpio, 0);
		pinctrl_select_state(chip->pinctrl, chip->wrx_en_sleep);
	}
	chg_err("set value:%d, gpio_val:%d\n", value,
		gpio_get_value(chip->wrx_en_gpio));
}

static int wlchg_wrx_otg_gpio_init(struct op_chg_chip *chip)
{
	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -EINVAL;
	}

	//wrx_otg
	chip->wrx_otg_active =
		pinctrl_lookup_state(chip->pinctrl, "wrx_otg_active");
	if (IS_ERR_OR_NULL(chip->wrx_otg_active)) {
		chg_err("get wrx_otg_active fail\n");
		return -EINVAL;
	}

	chip->wrx_otg_sleep =
		pinctrl_lookup_state(chip->pinctrl, "wrx_otg_sleep");
	if (IS_ERR_OR_NULL(chip->wrx_otg_sleep)) {
		chg_err("get wrx_otg_sleep fail\n");
		return -EINVAL;
	}

	gpio_direction_output(chip->wrx_otg_gpio, 0);
	pinctrl_select_state(chip->pinctrl, chip->wrx_otg_sleep);

	chg_err("gpio_val:%d\n", gpio_get_value(chip->wrx_otg_gpio));

	return 0;
}

void op_set_wrx_otg_value(int value)
{
	struct op_chg_chip *chip = g_op_chip;

	if (!chip) {
		chg_err("op_chg_chip not ready, return\n");
		return;
	}

	if (chip->wrx_otg_gpio <= 0) {
		chg_err("wrx_otg_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->wrx_otg_active) ||
	    IS_ERR_OR_NULL(chip->wrx_otg_sleep)) {
		chg_err("pinctrl null, return\n");
		return;
	}

	if (value == 1) {
		gpio_direction_output(chip->wrx_otg_gpio, 1);
		pinctrl_select_state(chip->pinctrl, chip->wrx_otg_active);
	} else {
		gpio_direction_output(chip->wrx_otg_gpio, 0);
		pinctrl_select_state(chip->pinctrl, chip->wrx_otg_sleep);
	}
	chg_err("set value:%d, gpio_val:%d\n", value,
		gpio_get_value(chip->wrx_otg_gpio));
}

static int wlchg_dcdc_en_gpio_init(struct op_chg_chip *chip)
{
	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -EINVAL;
	}

	chip->dcdc_en_active =
		pinctrl_lookup_state(chip->pinctrl, "dcdc_en_active");
	if (IS_ERR_OR_NULL(chip->dcdc_en_active)) {
		chg_err("get dcdc_en_active fail\n");
		return -EINVAL;
	}

	chip->dcdc_en_sleep =
		pinctrl_lookup_state(chip->pinctrl, "dcdc_en_sleep");
	if (IS_ERR_OR_NULL(chip->dcdc_en_sleep)) {
		chg_err("get dcdc_en_sleep fail\n");
		return -EINVAL;
	}

	chip->dcdc_en_default =
		pinctrl_lookup_state(chip->pinctrl, "dcdc_en_default");
	if (IS_ERR_OR_NULL(chip->dcdc_en_default)) {
		chg_err("get dcdc_en_default fail\n");
		return -EINVAL;
	}

	gpio_direction_output(chip->dcdc_en_gpio, 0);
	pinctrl_select_state(chip->pinctrl, chip->dcdc_en_sleep);

	chg_err("gpio_val:%d\n", gpio_get_value(chip->dcdc_en_gpio));

	return 0;
}

void op_set_dcdc_en_value(int value)
{
	struct op_chg_chip *chip = g_op_chip;

	if (!chip) {
		chg_err("op_chg_chip not ready, return\n");
		return;
	}

	if (chip->dcdc_en_gpio <= 0) {
		chg_err("dcdc_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->dcdc_en_active) ||
	    IS_ERR_OR_NULL(chip->dcdc_en_sleep)) {
		chg_err("pinctrl null, return\n");
		return;
	}

	if (value == 1) {
		gpio_direction_output(chip->dcdc_en_gpio, 1);
		pinctrl_select_state(chip->pinctrl, chip->dcdc_en_active);
	} else {
		gpio_direction_output(chip->dcdc_en_gpio, 0);
		pinctrl_select_state(chip->pinctrl, chip->dcdc_en_sleep);
	}
	chg_err("set value:%d\n", value);
}

static int wlchg_gpio_init(struct op_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		chg_err("device tree node missing\n");
		return -EINVAL;
	}
	if (!chip) {
		chg_err("op_chg_chip not ready!\n");
		return -EINVAL;
	}

	// Parsing gpio usbin_int
	chip->usbin_int_gpio = of_get_named_gpio(node, "qcom,usbin_int-gpio", 0);
	if (chip->usbin_int_gpio < 0) {
		chg_err("chip->usbin_int_gpio not specified\n");
		return -EINVAL;
	} else {
		if (gpio_is_valid(chip->usbin_int_gpio)) {
			rc = gpio_request(chip->usbin_int_gpio, "usbin-int-gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n", chip->usbin_int_gpio);
				return rc;
			} else {
				rc = wlchg_usbin_int_gpio_init(chip);
				if (rc) {
					chg_err("unable to init usbin_int_gpio:%d\n", chip->usbin_int_gpio);
					goto free_gpio_1;
				} else {
					wlchg_usbin_int_irq_init(chip);
					rc = wlchg_usbin_int_eint_register(chip);
					if (rc < 0) {
						chg_err("Init usbin irq failed.");
						goto free_gpio_1;
					}
				}
			}
		}

		chg_err("chip->usbin_int_gpio =%d\n", chip->usbin_int_gpio);
	}

	// Parsing gpio wrx_en
	chip->wrx_en_gpio = of_get_named_gpio(node, "qcom,wrx_en-gpio", 0);
	if (chip->wrx_en_gpio < 0) {
		chg_err("chip->wrx_en_gpio not specified\n");
		rc = -EINVAL;
		goto free_gpio_1;
	} else {
		if (gpio_is_valid(chip->wrx_en_gpio)) {
			rc = gpio_request(chip->wrx_en_gpio, "wrx_en-gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n", chip->wrx_en_gpio);
				goto free_gpio_1;
			} else {
				rc = wlchg_wrx_en_gpio_init(chip);
				if (rc) {
					chg_err("unable to init wrx_en_gpio:%d\n", chip->wrx_en_gpio);
					goto free_gpio_2;
				}
			}
		}

		chg_err("chip->wrx_en_gpio =%d\n", chip->wrx_en_gpio);
	}

	// Parsing gpio wrx_otg
	chip->wrx_otg_gpio = of_get_named_gpio(node, "qcom,wrx_otg-gpio", 0);
	if (chip->wrx_otg_gpio < 0) {
		chg_err("chip->idt_otg_gpio not specified\n");
		rc = -EINVAL;
		goto free_gpio_2;
	} else {
		if (gpio_is_valid(chip->wrx_otg_gpio)) {
			rc = gpio_request(chip->wrx_otg_gpio, "wrx_otg-gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n", chip->wrx_otg_gpio);
				goto free_gpio_2;
			} else {
				rc = wlchg_wrx_otg_gpio_init(chip);
				if (rc) {
					chg_err("unable to init wrx_otg_gpio:%d\n", chip->wrx_otg_gpio);
					goto free_gpio_3;
				}
			}
		}

		chg_err("chip->wrx_otg_gpio =%d\n", chip->wrx_otg_gpio);
	}

	if (chip->ap_ctrl_dcdc) {
		// Parsing gpio dcdc_en
		chip->dcdc_en_gpio = of_get_named_gpio(node, "qcom,dcdc_en-gpio", 0);
		if (chip->dcdc_en_gpio < 0) {
			chg_err("chip->dcdc_en_gpio not specified\n");
			rc = -EINVAL;
			goto free_gpio_3;
		} else {
			if (gpio_is_valid(chip->dcdc_en_gpio)) {
				rc = gpio_request(chip->dcdc_en_gpio, "dcdc_en-gpio");
				if (rc) {
					chg_err("unable to request gpio [%d]\n", chip->dcdc_en_gpio);
					goto free_gpio_3;
				} else {
					rc = wlchg_dcdc_en_gpio_init(chip);
					if (rc) {
						chg_err("unable to init dcdc_en_gpio:%d\n", chip->dcdc_en_gpio);
						goto free_gpio_4;
					}
				}
			}
			chg_err("chip->dcdc_en_gpio =%d\n", chip->dcdc_en_gpio);
		}
	}

	return 0;

free_gpio_4:
	if (gpio_is_valid(chip->dcdc_en_gpio))
		gpio_free(chip->dcdc_en_gpio);
free_gpio_3:
	if (gpio_is_valid(chip->wrx_otg_gpio))
		gpio_free(chip->wrx_otg_gpio);
free_gpio_2:
	if (gpio_is_valid(chip->wrx_en_gpio))
		gpio_free(chip->wrx_en_gpio);
free_gpio_1:
	if (gpio_is_valid(chip->usbin_int_gpio))
		gpio_free(chip->usbin_int_gpio);
	return rc;
}

static void op_wait_wpc_chg_quit_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct op_chg_chip *chip =
		container_of(dwork, struct op_chg_chip, wait_wpc_chg_quit);

	int level;
	int wpc_con_level = 0;

	level = wlchg_get_usbin_val();
	//printk(KERN_ERR "[OP_CHG][%s]: op-wlchg test wired_connect level[%d]\n", __func__, level);
	if (level == 1) {
		wpc_con_level = wlchg_rx_get_chip_con();
		printk(KERN_ERR
		       "[OP_CHG][%s]: op-wlchg test wpc_connect level[%d]\n",
		       __func__, wpc_con_level);
		if (wpc_con_level == 0 || wpc_chg_quit_max_cnt >= 5) {
			chargepump_disable();
		} else {
			schedule_delayed_work(&chip->wait_wpc_chg_quit,
					      msecs_to_jiffies(500));
			wpc_chg_quit_max_cnt++;
		}
	}
	return;
}

static void op_check_wireless_ovp(struct op_chg_chip *chip)
{
	static int ov_count, nov_count;
	int detect_time = 10; /* 10 x (100 or 500)ms = (1 or 5)s */
	struct wpc_data *chg_status = &chip->wlchg_status;

	if (chg_status->ftm_mode)
		return;

	if (!chg_status->rx_ovp) {
		if ((g_rx_chip->chg_data.vout > RX_FAST_SOFT_OVP_MV) ||
		    (!(chg_status->charge_type == WPC_CHARGE_TYPE_FAST &&
		       (chg_status->adapter_type == ADAPTER_TYPE_FASTCHAGE_DASH ||
			chg_status->adapter_type == ADAPTER_TYPE_FASTCHAGE_WARP)) &&
		     g_rx_chip->chg_data.vout > RX_EPP_SOFT_OVP_MV)) {
			ov_count++;
			chg_err("Rx vout is over voltage, ov_count=%d\n", ov_count);
			if (detect_time <= ov_count) {
				/* vchg continuous higher than safety */
				chg_err("charger is over voltage, stop charging\n");
				ov_count = 0;
				chg_status->rx_ovp = true;
				if (normal_charger != NULL)
					normal_charger->chg_ovp = true;
			}
			if (nov_count != 0)
				nov_count = 0;
		}
	} else {
		if (g_rx_chip->chg_data.vout < RX_EPP_SOFT_OVP_MV - 100) {
			nov_count++;
			chg_err("Rx vout is back to ok, nov_count=%d\n", nov_count);
			if (detect_time <= nov_count) {
				chg_err("charger is normal.\n");
				nov_count = 0;
				chg_status->rx_ovp = false;
				if (normal_charger != NULL)
					normal_charger->chg_ovp = false;
			}
			if (ov_count != 0)
				ov_count = 0;
		}
	}
}

static void wlchg_task_work_process(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct op_chg_chip *chip =
		container_of(dwork, struct op_chg_chip, wlchg_task_work);
	struct wpc_data *chg_status = &chip->wlchg_status;

	if (g_rx_chip == NULL) {
		chg_err("rx chip is not ready\n");
		return;
	}

	pr_debug("op-wlchg test charge_online[%d]\n", chg_status->charge_online);

	if (chg_status->charge_online) {
#ifdef IDT_LAB_TEST
		if (wlchg_rx_get_run_mode(g_rx_chip) == RX_RUNNING_MODE_OTHERS)
#else
		if (wlchg_rx_get_run_mode(g_rx_chip) != RX_RUNNING_MODE_EPP)
#endif
			wlchg_cmd_process(chip);  // func *

		/* wlchg server watchdog*/
		if (atomic_read(&chip->hb_count) > 0) {
			atomic_dec(&chip->hb_count);
		} else {
			if (chg_status->adapter_type == ADAPTER_TYPE_FASTCHAGE_DASH ||
			    chg_status->adapter_type == ADAPTER_TYPE_FASTCHAGE_WARP) {
				chip->heart_stop = true;
				chg_err("wlchg service stops running and exits fast charging\n");
				wlchg_rx_set_chip_sleep(1);
				return;
			}
		}
		wlchg_send_msg(WLCHG_MSG_HEARTBEAT, 0, 0); //heartbeat packet

		op_check_battery_temp(chip);
		mutex_lock(&chip->chg_lock);
		wlchg_rx_get_vrect_iout(g_rx_chip);
		fastchg_curr_filter(chip);
		mutex_unlock(&chip->chg_lock);
		op_check_wireless_ovp(chip);
		wlchg_charge_status_process(chip);

		if (chg_status->charge_online) {
			/* run again after interval */
			if ((chg_status->temp_region == WLCHG_BATT_TEMP_COLD ||
			chg_status->temp_region == WLCHG_BATT_TEMP_HOT) &&
				chg_status->charge_current == 0) {
				schedule_delayed_work(&chip->wlchg_task_work, msecs_to_jiffies(5000));
			} else {
				if (chg_status->startup_fast_chg)
					schedule_delayed_work(&chip->wlchg_task_work,
							msecs_to_jiffies(100));
				else
					schedule_delayed_work(&chip->wlchg_task_work,
							WLCHG_TASK_INTERVAL);
			}
		}
	}
}

#define RX_VOL_MAX 20000
#define VOL_ADJUST_MAX 150
#define IBAT_MAX_MA 6000
static void curr_vol_check_process(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct op_chg_chip *chip =
		container_of(dwork, struct op_chg_chip, fastchg_curr_vol_work);
	struct wpc_data *chg_status = &chip->wlchg_status;
	struct charge_param *chg_param = &chip->chg_param;
	static bool wait_cep;
	static int cep_err_count;
	bool skip_cep_check = false;
	int tmp_val;
	int ibat_err;
	int ibat_max;
	int cep_flag;

	if (!chg_status->charge_online)
		return;
	if ((g_rx_chip != NULL) && (chg_status->adapter_type == ADAPTER_TYPE_UNKNOWN))
		return;

	mutex_lock(&chip->chg_lock);
	wlchg_rx_get_vrect_iout(g_rx_chip);
	fastchg_curr_filter(chip);

	if (chg_status->curr_limit_mode) {
		if (chg_status->temp_region == WLCHG_BATT_TEMP_NORMAL)
			ibat_max = chg_param->fastchg_ibatmax[1];
		else
			ibat_max = chg_param->fastchg_ibatmax[0];

		chg_info("Iout: target=%d, out=%d, ibat_max=%d, ibat=%d\n",
			chg_status->target_curr, g_rx_chip->chg_data.iout,
			ibat_max, chip->icharging);

		tmp_val = chg_status->target_curr - g_rx_chip->chg_data.iout;
		ibat_err = ((ibat_max - abs(chip->icharging)) / 4) - (CURR_ERR_MIN / 2);
		/* Prevent the voltage from increasing too much, ibat exceeds expectations */
		if ((ibat_err > -(CURR_ERR_MIN / 2)) && (ibat_err < 0) && (tmp_val > 0)) {
			/*
			 * When ibat is greater than 5800mA, the current is not
			 * allowed to continue to increase, preventing fluctuations.
			 */
			tmp_val = 0;
		} else {
			tmp_val = tmp_val > ibat_err ? ibat_err : tmp_val;
		}
		cep_flag = wlchg_rx_get_cep_flag(g_rx_chip);
		if (tmp_val < 0) {
			if (cep_flag != 0)
				cep_err_count++;
			else
				cep_err_count = 0;
			if (!chg_status->curr_need_dec || cep_err_count >= CEP_ERR_MAX) {
				skip_cep_check = true;
				chg_status->curr_need_dec = true;
				cep_err_count = 0;
			}
		} else {
			cep_err_count = 0;
			chg_status->curr_need_dec = false;
		}
		if (cep_flag == 0 || skip_cep_check) {
			if (tmp_val > 0 || tmp_val < -CURR_ERR_MIN) {
				if (tmp_val > 0) {
					if (tmp_val > 200)
						chg_status->vol_set += 200;
					else if (tmp_val > 50)
						chg_status->vol_set += 100;
					else
						chg_status->vol_set += 20;
				} else {
					if (tmp_val < -200)
						chg_status->vol_set -= 200;
					else if (tmp_val < -50)
						chg_status->vol_set -= 100;
					else
						chg_status->vol_set -= 20;
				}
				if (chg_status->vol_set > RX_VOLTAGE_MAX)
					chg_status->vol_set = RX_VOLTAGE_MAX;
				if (chg_status->charge_type == WPC_CHARGE_TYPE_FAST) {
					if (chg_status->vol_set < FASTCHG_MODE_VOL_MIN) {
						chg_status->vol_set = FASTCHG_MODE_VOL_MIN;
					}
				} else {
					if (chg_status->vol_set < NORMAL_MODE_VOL_MIN) {
						chg_status->vol_set = NORMAL_MODE_VOL_MIN;
					}
				}
				wlchg_rx_set_vout(g_rx_chip, chg_status->vol_set);
				wait_cep = false;
			}
		}
	} else {
		if (!chg_status->vol_set_ok) {
			chg_err("Vout: target=%d, set=%d, out=%d\n", chg_status->target_vol,
				chg_status->vol_set, g_rx_chip->chg_data.vout);
			if (chg_status->vol_set_start) {
				if (!chg_status->vol_set_fast)
					chg_status->vol_set = g_rx_chip->chg_data.vout;
				else
					chg_status->vol_set_fast = false;
				chg_status->vol_set_start = false;
				/*
				 * Refresh the CEP status to ensure that the CEP
				 * obtained next time is updated.
				 */
				(void)wlchg_rx_get_cep_flag(g_rx_chip);
				wait_cep = false;
			}
			if (wait_cep) {
				if (wlchg_rx_get_cep_flag(g_rx_chip) == 0) {
					wait_cep = false;
					if (chg_status->target_vol == chg_status->vol_set) {
						chg_status->vol_set_ok = true;
						mutex_unlock(&chip->chg_lock);
						return;
					}
				}
			} else {
				if (chg_status->target_vol > chg_status->vol_set) {
					tmp_val = chg_status->target_vol - chg_status->vol_set;
					if (tmp_val > VOL_INC_STEP_MAX && chg_status->target_vol > VOL_ADJ_LIMIT) {
						if (chg_status->vol_set < VOL_ADJ_LIMIT) {
							chg_status->vol_set = VOL_ADJ_LIMIT;
						} else {
							chg_status->vol_set += VOL_INC_STEP_MAX;
						}
					} else {
						chg_status->vol_set += tmp_val;
					}
				} else if (chg_status->target_vol < chg_status->vol_set) {
					if (chg_status->vol_not_step) {
						chg_status->vol_set = chg_status->target_vol;
						chg_status->vol_not_step = false;
					} else {
						tmp_val = chg_status->vol_set - chg_status->target_vol;
						tmp_val = tmp_val < VOL_DEC_STEP_MAX ? tmp_val : VOL_DEC_STEP_MAX;
						chg_status->vol_set -= tmp_val;
					}
				}
				wlchg_rx_set_vout(g_rx_chip, chg_status->vol_set);
				wait_cep = true;
			}
		} else {
			mutex_unlock(&chip->chg_lock);
			return;
		}
	}
	mutex_unlock(&chip->chg_lock);

	if (chg_status->charge_online) {
		if (chg_status->curr_limit_mode)
			schedule_delayed_work(&chip->fastchg_curr_vol_work,
				msecs_to_jiffies(500));
		else
			schedule_delayed_work(&chip->fastchg_curr_vol_work,
				msecs_to_jiffies(100));
	}
}

static void wlchg_tx_check_process(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct op_chg_chip *chip =
		container_of(dwork, struct op_chg_chip, tx_check_work);

	if (chip->wireless_mode == WIRELESS_MODE_TX) {
		if (chip->wlchg_status.dock_on) {
			chg_err("wireless charger dock detected, exit reverse charge\n");
			chip->wlchg_status.wpc_dischg_status = WPC_DISCHG_STATUS_OFF;
		}
		chg_info("<~WPC~>rtx func wpc_dischg_status[%d]\n",
			chip->wlchg_status.wpc_dischg_status);

		if (chip->wlchg_status.wpc_dischg_status == WPC_DISCHG_STATUS_OFF) {
			g_op_chip->otg_switch = false;
			wlchg_enable_tx_function(false);
		}
		schedule_delayed_work(&chip->tx_check_work, msecs_to_jiffies(500));
	} else {
		chg_err("<~WPC~ wireless mode is %d, not TX, exit.", chip->wireless_mode);
	}
}

static void wlchg_check_charge_timeout(struct op_chg_chip *chip)
{
	if (chip == NULL) {
		chg_err("op_chg_chip not ready.");
		return;
	}
	if (normal_charger == NULL) {
		chg_err("smb charger is not ready.");
		return;
	}

	if (chip->wlchg_status.ftm_mode) {
		chg_info("It's ftm mode, return.");
		return;
	}
	if (wlchg_check_charge_done(chip)) {
		chg_info("battery is full, return.");
		return;
	}
	if (chip->disable_batt_charge) {
		chg_info("wireless charge is disabled, reset count and return.");
		chip->wlchg_time_count = 0;
		return;
	}

	if (chip->wlchg_status.charge_online)
		chip->wlchg_time_count++;

	if (chip->wlchg_time_count >= WLCHG_CHARGE_TIMEOUT) {
		chg_info("wlchg timeout! stop chaging now.\n");
		wlchg_disable_batt_charge(chip, true);
		normal_charger->time_out = true;
	}
}

static void exfg_update_batinfo(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct op_chg_chip *chip =
		container_of(dwork, struct op_chg_chip, update_bat_info_work);

	if (chip->wlchg_status.charge_online && g_op_chip &&
	    exfg_instance) {
		g_op_chip->batt_volt =
			exfg_instance->get_battery_mvolts() / 1000;
		g_op_chip->icharging =
			exfg_instance->get_average_current() / 1000;
#ifdef HW_TEST_EDITION
		if (chip->w30w_timeout) {
			g_op_chip->temperature = 320; //for test.
		} else {
			g_op_chip->temperature = 280; //for test.
		}
#else
		g_op_chip->temperature =
			exfg_instance->get_battery_temperature();
#endif
		g_op_chip->soc = exfg_instance->get_battery_soc();
		g_op_chip->batt_missing = !exfg_instance->is_battery_present();
		chg_err("battery info: v=%d, i=%d, t=%d, soc=%d.",
			g_op_chip->batt_volt, g_op_chip->icharging,
			g_op_chip->temperature, g_op_chip->soc);
		wlchg_check_charge_timeout(chip);
	}
	/* run again after interval */
	if (chip && chip->wlchg_status.charge_online) {
		schedule_delayed_work(&chip->update_bat_info_work,
				      WLCHG_BATINFO_UPDATE_INTERVAL);
	}
}
int register_reverse_charge_notifier(struct notifier_block *nb)
{
	if (!nb)
	return -EINVAL;

	return blocking_notifier_chain_register(&reverse_charge_chain, nb);
}
EXPORT_SYMBOL(register_reverse_charge_notifier);


int unregister_reverse_charge_notifier(struct notifier_block *nb)
{
	if (!nb)
		return -EINVAL;

	return blocking_notifier_chain_unregister(&reverse_charge_chain, nb);
}
EXPORT_SYMBOL(unregister_reverse_charge_notifier);


static int reverse_charge_notifier_call_chain(unsigned long val)
{
	return blocking_notifier_call_chain(&reverse_charge_chain, val, NULL);
}

#ifdef HW_TEST_EDITION
static void w30w_timeout_work_process(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct op_chg_chip *chip =
		container_of(dwork, struct op_chg_chip, w30w_timeout_work);
	chip->w30w_timeout = true;
}
#endif

static void wlchg_charger_offline(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct op_chg_chip *chip =
		container_of(dwork, struct op_chg_chip, charger_exit_work);
	if (wlchg_rx_get_chip_con() == 0) {
		chg_err("report wlchg offline.");
		chip->wlchg_status.fastchg_display_delay = false;
		chip->charger_exist = false;
		if (chip->wireless_psy != NULL)
			power_supply_changed(chip->wireless_psy);


		/* Waiting for frameworks liting on display. To avoid*/
		/* enter deep sleep(can't lit on) once relax wake_lock.*/
		msleep(100);
		// check connect status again, make sure not recharge.
		if (wlchg_rx_get_chip_con() == 0) {
			if (chip->wlchg_wake_lock_on) {
				chg_info("release wlchg_wake_lock\n");
				__pm_relax(chip->wlchg_wake_lock);
				chip->wlchg_wake_lock_on = false;
			} else {
				chg_err("wlchg_wake_lock is already relax\n");
			}
		}
	}
}

#ifdef OP_DEBUG
static ssize_t proc_wireless_voltage_rect_read(struct file *file,
					       char __user *buf, size_t count,
					       loff_t *ppos)
{
	uint8_t ret = 0;
	char page[10];
	int vrect = 0;
	struct op_chg_chip *chip = g_op_chip;

	if (chip == NULL) {
		chg_err("rx_chip not exist, return\n");
		return -ENODEV;
	}

	if (atomic_read(&chip->suspended) == 1) {
		return 0;
	}

	vrect = g_rx_chip->chg_data.vrect;

	chg_err("%s: vrect = %d.\n", __func__, vrect);
	snprintf(page, 10, "%d", vrect);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t proc_wireless_voltage_rect_write(struct file *file,
						const char __user *buf,
						size_t count, loff_t *lo)
{
	return count;
}

static const struct file_operations proc_wireless_voltage_rect_ops = {
	.read = proc_wireless_voltage_rect_read,
	.write = proc_wireless_voltage_rect_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t proc_wireless_rx_voltage_read(struct file *file,
					     char __user *buf, size_t count,
					     loff_t *ppos)
{
	char vol_string[8];
	int len = 0;
	snprintf(vol_string, 8, "%d\n",
		     g_op_chip->wlchg_status.charge_voltage);
	len = simple_read_from_buffer(buf, count, ppos, vol_string, strlen(vol_string));
	return len;
}
static ssize_t proc_wireless_rx_voltage_write(struct file *file,
					      const char __user *buf,
					      size_t count, loff_t *lo)
{
	char vol_string[8] = {0};
	int vol = 0;
	int len = count < 8 ? count : 8;

	if (g_op_chip == NULL) {
		chg_err("%s: g_op_chip is not ready\n", __func__);
		return -ENODEV;
	}

	copy_from_user(vol_string, buf, len);
	kstrtoint(vol_string, 0, &vol);
	chg_err("set voltage: vol_string = %s, vol = %d.", vol_string, vol);
	wlchg_set_rx_target_voltage(g_op_chip, vol);
	return count;
}

static const struct file_operations proc_wireless_rx_voltage = {
	.read = proc_wireless_rx_voltage_read,
	.write = proc_wireless_rx_voltage_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t proc_wireless_current_out_read(struct file *file,
					      char __user *buf, size_t count,
					      loff_t *ppos)
{
	uint8_t ret = 0;
	char page[10];
	int iout = 0;
	struct op_chg_chip *chip = g_op_chip;

	if ((chip == NULL) || (g_rx_chip == NULL)) {
		chg_err("%s: wlchg driver is not ready\n", __func__);
		return 0;
	}
	if (atomic_read(&chip->suspended) == 1) {
		return 0;
	}

	iout = g_rx_chip->chg_data.iout;

	chg_err("%s: iout = %d.\n", __func__, iout);
	snprintf(page, 10, "%d", iout);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t proc_wireless_current_out_write(struct file *file,
					       const char __user *buf,
					       size_t count, loff_t *lo)
{
	char curr_string[8] = {0};
	int curr = 0;
	int len = count < 8 ? count : 8;

	if (g_op_chip == NULL) {
		chg_err("%s: g_op_chip is not ready\n", __func__);
		return -ENODEV;
	}

	copy_from_user(curr_string, buf, len);
	kstrtoint(curr_string, 0, &curr);
	chg_err("set current: curr_string = %s, curr = %d.", curr_string, curr);
	if (curr >= 0 && curr <= 1500) {
		vote(g_op_chip->wlcs_fcc_votable, MAX_VOTER, true, curr * 1000);
		if (!(g_op_chip->wlchg_status.curr_limit_mode))
			fastchg_curr_control_en(g_op_chip, true);
	} else {
		chg_err("The target current should in (0,1500), not match.");
	}
	return count;
}

static const struct file_operations proc_wireless_current_out_ops = {
	.read = proc_wireless_current_out_read,
	.write = proc_wireless_current_out_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};
#endif

static ssize_t proc_wireless_ftm_mode_read(struct file *file, char __user *buf,
					   size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[8];
	struct op_chg_chip *chip = g_op_chip;
	ssize_t len;

	if (chip == NULL) {
		chg_err("%s: wlchg driver is not ready\n", __func__);
		return 0;
	}

	memset(page, 0, 8);
	if (chip->wlchg_status.ftm_mode) {
		len = 7;
		snprintf(page, len, "enable\n");
	} else {
		len = 8;
		snprintf(page, len, "disable\n");
	}
	ret = simple_read_from_buffer(buf, count, ppos, page, len);

	return ret;
}

static ssize_t proc_wireless_ftm_mode_write(struct file *file,
					    const char __user *buf, size_t len,
					    loff_t *lo)
{
	char buffer[5] = { 0 };
	int val;

	chg_err("%s: len[%d] start.\n", __func__, len);
	if (len > 5) {
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, len)) {
		chg_err("%s: error.\n", __func__);
		return -EFAULT;
	}

	chg_err("buffer=%s", buffer);
	kstrtoint(buffer, 0, &val);
	chg_err("val = %d", val);

	if (val == 1) {
		chg_err("%s:ftm_mode enable\n", __func__);
		wlchg_enable_ftm(true);
	} else {
		chg_err("%s:ftm_mode disable\n", __func__);
		wlchg_enable_ftm(false);
	}
	chg_err("%s: end.\n", __func__);

	return len;
}

static const struct file_operations proc_wireless_ftm_mode_ops = {
	.read = proc_wireless_ftm_mode_read,
	.write = proc_wireless_ftm_mode_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t proc_wireless_tx_read(struct file *file, char __user *buf,
				     size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[10];
	struct op_chg_chip *chip = g_op_chip;

	if (chip == NULL) {
		chg_err("%s: wlchg driver is not ready\n", __func__);
		return -ENODEV;
	}

	if (chip->wireless_mode == WIRELESS_MODE_TX) {
		if (chip->wlchg_status.tx_online)
			snprintf(page, 10, "%s\n", "charging");
		else
			snprintf(page, 10, "%s\n", "enable");
	} else
		snprintf(page, 10, "%s\n", "disable");

	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t proc_wireless_tx_write(struct file *file, const char __user *buf,
				      size_t count, loff_t *lo)
{
	char buffer[5] = { 0 };
	struct op_chg_chip *chip = g_op_chip;
	int val;

	if (chip == NULL) {
		chg_err("%s: wlchg driver is not ready\n", __func__);
		return -ENODEV;
	}

	if (g_rx_chip == NULL) {
		chg_err("%s: rx chip is not ready\n", __func__);
		return -ENODEV;
	}

	if (count > 5) {
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, count)) {
		chg_err("%s: error.\n", __func__);
		return -EFAULT;
	}

	chg_err("buffer=%s", buffer);
	kstrtoint(buffer, 0, &val);
	chg_err("val = %d", val);

	if (val == 1) {
		if (chip->wireless_mode == WIRELESS_MODE_NULL) {
			if (wlchg_get_usbin_val() == 1) {
				chg_err("USB cable is in, don't allow enter otg wireless charge.");
				return -EFAULT;
			}
			if (wlchg_rx_fw_updating(g_rx_chip)) {
				chg_err("<~WPC~> FW is updating, return!\n");
				return -EFAULT;
			}
			wlchg_reset_variables(chip);
			wlchg_enable_tx_function(true);
		} else {
			return -EFAULT;
		}
	} else {
		if (chip->wireless_mode == WIRELESS_MODE_TX) {
			wlchg_enable_tx_function(false);
			wlchg_reset_variables(chip);
		} else {
			return -EFAULT;
		}
	}

	return count;
}

static const struct file_operations proc_wireless_tx_ops = {
	.read = proc_wireless_tx_read,
	.write = proc_wireless_tx_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t proc_wireless_quiet_mode_read(struct file *file, char __user *buf,
				     size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[7];
	int len = 0;
	struct op_chg_chip *chip = g_op_chip;
	struct wpc_data *chg_status;

	if (chip == NULL) {
		chg_err("%s: wlchg driver is not ready\n", __func__);
		return -ENODEV;
	}

	chg_status = &chip->wlchg_status;
	len = snprintf(page, 7, "%s\n",
		(chg_status->quiet_mode_enabled && chip->quiet_mode_need) ? "true" : "false");
	ret = simple_read_from_buffer(buf, count, ppos, page, len);
	return ret;
}

static ssize_t proc_wireless_quiet_mode_write(struct file *file, const char __user *buf,
				      size_t count, loff_t *lo)
{
	char buffer[3] = { 0 };
	struct op_chg_chip *chip = g_op_chip;
	int val;

	if (chip == NULL) {
		chg_err("%s: wlchg driver is not ready\n", __func__);
		return -ENODEV;
	}

	if (count > 3) {
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, count)) {
		chg_err("%s: error.\n", __func__);
		return -EFAULT;
	}

	chg_err("buffer=%s", buffer);
	kstrtoint(buffer, 0, &val);
	chg_err("val = %d", val);
	chip->quiet_mode_need = (val == 1) ? true : false;
	return count;
}

static const struct file_operations proc_wireless_quiet_mode_ops = {
	.read = proc_wireless_quiet_mode_read,
	.write = proc_wireless_quiet_mode_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

#ifdef OP_DEBUG
static ssize_t proc_wireless_epp_read(struct file *file, char __user *buf,
				      size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[6];
	struct op_chg_chip *chip = g_op_chip;
	size_t len = 6;

	if (chip == NULL) {
		chg_err("%s: wlchg driver is not ready\n", __func__);
		return 0;
	}

	memset(page, 0, 6);
	if (force_epp) {
		len = snprintf(page, len, "epp\n");
	} else if (force_bpp) {
		len = snprintf(page, len, "bpp\n");
	} else if (!auto_mode) {
		len = snprintf(page, len, "manu\n");
	} else {
		len = snprintf(page, len, "auto\n");
	}
	ret = simple_read_from_buffer(buf, count, ppos, page, len);

	return ret;
}

static ssize_t proc_wireless_epp_write(struct file *file,
				       const char __user *buf, size_t count,
				       loff_t *lo)
{
	char buffer[5] = { 0 };
	int val = 0;

	chg_err("%s: len[%d] start.\n", __func__, count);
	if (count > 5) {
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, count)) {
		chg_err("%s: error.\n", __func__);
		return -EFAULT;
	}
	chg_err("buffer=%s", buffer);
	kstrtoint(buffer, 0, &val);
	chg_err("val=%d", val);
	if (val == 1) {
		force_bpp = true;
		force_epp = false;
		auto_mode = true;
	} else if (val == 2) {
		force_bpp = false;
		force_epp = true;
		auto_mode = true;
	} else if (val == 3) {
		force_bpp = false;
		force_epp = false;
		auto_mode = false;
	} else {
		force_bpp = false;
		force_epp = false;
		auto_mode = true;
	}
	return count;
}

static const struct file_operations proc_wireless_epp_ops = {
	.read = proc_wireless_epp_read,
	.write = proc_wireless_epp_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t proc_wireless_charge_pump_read(struct file *file, char __user *buf,
					   size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[6];
	struct op_chg_chip *chip = g_op_chip;
	size_t len = 6;

	if (chip == NULL) {
		chg_err("%s: wlchg driver is not ready\n", __func__);
		return 0;
	}

	memset(page, 0, 6);
	len = snprintf(page, len, "%d\n", proc_charge_pump_status);
	ret = simple_read_from_buffer(buf, count, ppos, page, len);
	return ret;
}

static ssize_t proc_wireless_charge_pump_write(struct file *file,
					       const char __user *buf,
					       size_t count, loff_t *lo)
{
	char buffer[2] = { 0 };
	int val = 0;

	chg_err("%s: len[%d] start.\n", __func__, count);
	if (count > 2) {
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, count)) {
		chg_err("%s: error.\n", __func__);
		return -EFAULT;
	}
	chg_err("buffer=%s", buffer);
	val = buffer[0] - '0';
	chg_err("val=%d", val);
	if (val < 0 || val > 6) {
		return -EINVAL;
	}
	switch (val) {
	case 0:
		chg_err("wkcs: disable all charge pump\n");
		chargepump_disable();
		bq2597x_enable_charge_pump(false);
		break;
	case 1:
		chg_err("wkcs: disable charge pump 1\n");
		chargepump_disable();
		break;
	case 2:
		chg_err("wkcs: enable charge pump 1\n");
		chargepump_hw_init(); //enable chargepump
		chargepump_enable();
		chargepump_set_for_LDO();
		break;
	case 3:
		chg_err("wkcs: disable charge pump 2\n");
		bq2597x_enable_charge_pump(false);
		break;
	case 4:
		chg_err("wkcs: enable charge pump 2\n");
		bq2597x_enable_charge_pump(true);
		break;
	case 5:
		wlchg_set_rx_charge_current(g_op_chip, 0);
		pmic_high_vol_en(g_op_chip, false);
		break;
	case 6:
		pmic_high_vol_en(g_op_chip, true);
		wlchg_set_rx_charge_current(g_op_chip, 300);
		break;
	default:
		chg_err("wkcs: invalid value.");
		break;
	}
	proc_charge_pump_status = val;
	return count;
}

static const struct file_operations proc_wireless_charge_pump_ops = {
	.read = proc_wireless_charge_pump_read,
	.write = proc_wireless_charge_pump_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};
#endif

static ssize_t proc_wireless_deviated_read(struct file *file, char __user *buf,
					   size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[7];
	struct op_chg_chip *chip = g_op_chip;
	size_t len = 7;

	if (chip == NULL) {
		chg_err("%s: wlchg driver is not ready\n", __func__);
		return 0;
	}

	memset(page, 0, 7);
	if (chip->wlchg_status.is_deviation) {
		len = snprintf(page, len, "%s\n", "true");
	} else {
		len = snprintf(page, len, "%s\n", "false");
	}
	ret = simple_read_from_buffer(buf, count, ppos, page, len);

	return ret;
}

static const struct file_operations proc_wireless_deviated_ops = {
	.read = proc_wireless_deviated_read,
	.write = NULL,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t proc_wireless_rx_read(struct file *file, char __user *buf,
					    size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[3];

	if (g_op_chip == NULL) {
		chg_err("<~WPC~> g_rx_chip is NULL!\n");
		return -ENODEV;
	}


	memset(page, 0, 3);
	snprintf(page, 3, "%c\n", !g_op_chip->disable_charge ? '1' : '0');
	ret = simple_read_from_buffer(buf, count, ppos, page, 3);

	return ret;
}

static ssize_t proc_wireless_rx_write(struct file *file, const char __user *buf,
				      size_t count, loff_t *lo)
{
	char buffer[5] = { 0 };
	struct op_chg_chip *chip = g_op_chip;
	int val;

	if (chip == NULL) {
		chg_err("%s: wlchg driver is not ready\n", __func__);
		return -ENODEV;
	}

	if (count > 5) {
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, count)) {
		chg_err("%s: error.\n", __func__);
		return -EFAULT;
	}

	chg_err("buffer=%s", buffer);
	kstrtoint(buffer, 0, &val);
	chg_err("val = %d", val);

	if (val == 0) {
		chip->disable_charge = true;
		wlchg_rx_set_chip_sleep(1);
	} else {
		chip->disable_charge = false;
		wlchg_rx_set_chip_sleep(0);
	}

	return count;
}

static const struct file_operations proc_wireless_rx_ops = {
	.read = proc_wireless_rx_read,
	.write = proc_wireless_rx_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t proc_fast_skin_threld_read(struct file *file, char __user *buf,
					    size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[16];
	int len = 16;

	if (g_op_chip == NULL) {
		chg_err("<~WPC~> g_rx_chip is NULL!\n");
		return -ENODEV;
	}


	memset(page, 0, len);
	len = snprintf(page, len, "Hi:%d,Lo:%d\n",
		g_op_chip->chg_param.fastchg_skin_temp_max,
		g_op_chip->chg_param.fastchg_skin_temp_min);
	ret = simple_read_from_buffer(buf, count, ppos, page, len);

	return ret;
}

static ssize_t proc_fast_skin_threld_write(struct file *file, const char __user *buf,
				      size_t count, loff_t *lo)
{
	char buffer[16] = { 0 };
	struct op_chg_chip *chip = g_op_chip;
	int hi_val, lo_val;
	int ret = 0;

	if (chip == NULL) {
		chg_err("%s: wlchg driver is not ready\n", __func__);
		return -ENODEV;
	}

	if (count > 16) {
		chg_err("input too many words.");
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, count)) {
		chg_err("%s: error.\n", __func__);
		return -EFAULT;
	}

	chg_err("buffer=%s", buffer);
	ret = sscanf(buffer, "%d %d", &hi_val, &lo_val);
	chg_err("hi_val=%d, lo_val=%d", hi_val, lo_val);

	if (ret == 2) {
		if (hi_val > lo_val) {
			chip->chg_param.fastchg_skin_temp_max = hi_val;
			chip->chg_param.fastchg_skin_temp_min = lo_val;
		} else {
			chg_err("hi_val not bigger than lo_val");
			return -EINVAL;
		}
	} else {
		chg_err("need two decimal number.");
		return -EINVAL;
	}

	return count;
}

static const struct file_operations proc_fast_skin_threld_ops = {
	.read = proc_fast_skin_threld_read,
	.write = proc_fast_skin_threld_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

#ifdef OP_DEBUG
static ssize_t proc_wireless_rx_freq_read(struct file *file,
					  char __user *buf, size_t count,
					  loff_t *ppos)
{
	char string[8];
	int len = 0;
	int rc;
	struct charge_param *chg_param;

	if (g_op_chip == NULL) {
		chg_err("wlchg driver is not ready\n");
		return -ENODEV;
	}
	chg_param = &g_op_chip->chg_param;

	memset(string, 0, 8);

	len = snprintf(string, 8, "%d\n", chg_param->freq_threshold);
	rc = simple_read_from_buffer(buf, count, ppos, string, len);
	return rc;
}
static ssize_t proc_wireless_rx_freq_write(struct file *file,
					   const char __user *buf,
					   size_t count, loff_t *lo)
{
	char string[16];
	int freq = 0;
	struct charge_param *chg_param;

	if (g_op_chip == NULL) {
		chg_err("wlchg driver is not ready\n");
		return -ENODEV;
	}
	chg_param = &g_op_chip->chg_param;

	memset(string, 0, 16);
	copy_from_user(string, buf, count);
	chg_info("buf = %s, len = %d\n", string, count);
	kstrtoint(string, 0, &freq);
	chg_info("set freq threshold to %d\n", freq);
	chg_param->freq_threshold = freq;
	return count;
}

static const struct file_operations proc_wireless_rx_freq_ops = {
	.read = proc_wireless_rx_freq_read,
	.write = proc_wireless_rx_freq_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t proc_match_q_read(struct file *file,
				 char __user *buf, size_t count,
				 loff_t *ppos)
{
	char string[8];
	int len = 0;
	int rc;
	struct charge_param *chg_param;

	if (g_op_chip == NULL) {
		chg_err("wlchg driver is not ready\n");
		return -ENODEV;
	}
	chg_param = &g_op_chip->chg_param;

	memset(string, 0, 8);

	len = snprintf(string, 8, "%d\n", chg_param->fastchg_match_q);
	rc = simple_read_from_buffer(buf, count, ppos, string, len);
	return rc;
}
static ssize_t proc_match_q_write(struct file *file,
				  const char __user *buf,
				  size_t count, loff_t *lo)
{
	char string[16];
	int match_q = 0;
	struct charge_param *chg_param;

	if (g_op_chip == NULL) {
		chg_err("wlchg driver is not ready\n");
		return -ENODEV;
	}
	chg_param = &g_op_chip->chg_param;

	memset(string, 0, 16);
	copy_from_user(string, buf, count);
	chg_info("buf = %s, len = %d\n", string, count);
	kstrtoint(string, 0, &match_q);
	chg_info("set match q to %d\n", match_q);
	chg_param->fastchg_match_q = match_q;
	return count;
}

static const struct file_operations proc_match_q_ops = {
	.read = proc_match_q_read,
	.write = proc_match_q_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};
#endif

static ssize_t proc_wireless_ftm_test_read(struct file *file,
					  char __user *buf, size_t count,
					  loff_t *ppos)
{
	char string[2] = {0, 0};
	int rc;
	int err_no = 0;

	if (g_rx_chip == NULL) {
		chg_err("[FTM_TEST]rc chip driver is not ready\n");
		return -ENODEV;
	}
	if (exchgpump_bq == NULL) {
		chg_err("[FTM_TEST]bq2597x driver is not ready\n");
		err_no |= WLCHG_FTM_TEST_CP2_ERR;
	}

	rc = wlchg_rx_ftm_test(g_rx_chip);
	if (rc < 0)
		return rc;
	else if (rc > 0)
		err_no |= rc;
	rc = bq2597x_ftm_test(exchgpump_bq);
	if (rc < 0)
		return rc;
	else if (rc > 0)
		err_no |= WLCHG_FTM_TEST_CP2_ERR;

	snprintf(string, 2, "%d\n", err_no);
	rc = simple_read_from_buffer(buf, count, ppos, string, 2);

	return rc;
}

static const struct file_operations proc_wireless_ftm_test_ops = {
	.read = proc_wireless_ftm_test_read,
	.write = NULL,
	.open = simple_open,
	.owner = THIS_MODULE,
};

#ifdef HW_TEST_EDITION
static ssize_t proc_wireless_w30w_time_read(struct file *file, char __user *buf,
					    size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[32];
	struct op_chg_chip *chip = g_op_chip;

	if (chip == NULL) {
		chg_err("%s: wlchg driver is not ready\n", __func__);
		return 0;
	}

	snprintf(page, 32, "w30w_time:%d minutes\n", chip->w30w_time);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t proc_wireless_w30w_time_write(struct file *file,
					     const char __user *buf,
					     size_t count, loff_t *lo)
{
	char buffer[4] = { 0 };
	int timeminutes = 0;
	struct op_chg_chip *chip = g_op_chip;

	if (chip == NULL) {
		chg_err("%s: wlchg driver is not ready\n", __func__);
		return 0;
	}

	chg_err("%s: len[%d] start.\n", __func__, count);
	if (count > 3) {
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, count)) {
		chg_err("%s: error.\n", __func__);
		return -EFAULT;
	}
	chg_err("buffer=%s", buffer);
	kstrtoint(buffer, 0, &timeminutes);
	chg_err("set w30w_time = %dm", timeminutes);
	if (timeminutes >= 0 && timeminutes <= 60)
		chip->w30w_time = timeminutes;
	chip->w30w_work_started = false;
	chip->w30w_timeout = false;
	return count;
}

static const struct file_operations proc_wireless_w30w_time_ops = {
	.read = proc_wireless_w30w_time_read,
	.write = proc_wireless_w30w_time_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

#endif

static int init_wireless_charge_proc(struct op_chg_chip *chip)
{
	int ret = 0;
	struct proc_dir_entry *prEntry_da = NULL;
	struct proc_dir_entry *prEntry_tmp = NULL;

	prEntry_da = proc_mkdir("wireless", NULL);
	if (prEntry_da == NULL) {
		chg_err("%s: Couldn't create wireless proc entry\n",
			  __func__);
		return -ENOMEM;
	}

	prEntry_tmp = proc_create_data("ftm_mode", 0664, prEntry_da,
				       &proc_wireless_ftm_mode_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		chg_err("%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
		goto fail;
	}

	prEntry_tmp = proc_create_data("enable_tx", 0664, prEntry_da,
				       &proc_wireless_tx_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		chg_err("%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
		goto fail;
	}

	prEntry_tmp = proc_create_data("quiet_mode", 0664, prEntry_da,
					   &proc_wireless_quiet_mode_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		chg_err("%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
		goto fail;
	}

	prEntry_tmp = proc_create_data("deviated", 0664, prEntry_da,
				       &proc_wireless_deviated_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		chg_err("%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
		goto fail;
	}

	prEntry_tmp = proc_create_data("enable_rx", 0664, prEntry_da,
				       &proc_wireless_rx_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		chg_err("%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
		goto fail;
	}

	prEntry_tmp = proc_create_data("fast_skin_threld", 0664, prEntry_da,
					   &proc_fast_skin_threld_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		chg_err("%s: Couldn't create proc fast_skin_threld, %d\n",
				__func__, __LINE__);
		goto fail;
	}

	prEntry_tmp = proc_create_data("ftm_test", 0664, prEntry_da,
				       &proc_wireless_ftm_test_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		chg_err("%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
		goto fail;
	}

#ifdef OP_DEBUG
	prEntry_tmp = proc_create_data("voltage_rect", 0664, prEntry_da,
				       &proc_wireless_voltage_rect_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		chg_err("%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
		goto fail;
	}

	prEntry_tmp = proc_create_data("rx_voltage", 0664, prEntry_da,
				       &proc_wireless_rx_voltage, chip);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		chg_err("%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
		goto fail;
	}

	prEntry_tmp = proc_create_data("current_out", 0664, prEntry_da,
				       &proc_wireless_current_out_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		chg_err("%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
		goto fail;
	}

	prEntry_tmp = proc_create_data("epp_or_bpp", 0664, prEntry_da,
				       &proc_wireless_epp_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		chg_err("%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
		goto fail;
	}

	prEntry_tmp = proc_create_data("charge_pump_en", 0664, prEntry_da,
				       &proc_wireless_charge_pump_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		chg_err("%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
		goto fail;
	}

	prEntry_tmp = proc_create_data("rx_freq", 0664, prEntry_da,
				       &proc_wireless_rx_freq_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		chg_err("%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
		goto fail;
	}

	prEntry_tmp = proc_create_data("match_q", 0664, prEntry_da,
				       &proc_match_q_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		chg_err("%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
		goto fail;
	}
#endif

#ifdef HW_TEST_EDITION
	prEntry_tmp = proc_create_data("w30w_time", 0664, prEntry_da,
				       &proc_wireless_w30w_time_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		chg_err("%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
		goto fail;
	}
#endif
	return 0;

fail:
	remove_proc_entry("wireless", NULL);
	return ret;
}

static int wlchg_driver_probe(struct platform_device *pdev)
{
	struct op_chg_chip *chip;
	int ret = 0;
	int boot_mode = 0;

	chg_debug(" call \n");

	chip = devm_kzalloc(&pdev->dev, sizeof(struct op_chg_chip),
				 GFP_KERNEL);
	if (!chip) {
		chg_err(" g_op_chg chip is null, probe again \n");
		return -ENOMEM;
	}

	chip->dev = &pdev->dev;

	chip->ap_ctrl_dcdc = of_property_read_bool(chip->dev->of_node, "op-ap_control_dcdc");
	chg_info("%s control dcdc\n", chip->ap_ctrl_dcdc ? "AP" : "RX");

	ret = wlchg_gpio_init(chip);
	if (ret) {
		chg_err("Init wlchg gpio error.");
		return ret;
	}

	g_op_chip = chip;

	wlchg_reset_variables(chip);
	chip->wlchg_status.dock_on = false;

	chip->wlchg_status.ftm_mode = false;

	ret = wlchg_init_wireless_psy(chip);
	if (ret) {
		chg_err("Init wireless psy error.");
		goto free_gpio;
	}

	INIT_DELAYED_WORK(&chip->usbin_int_work, wlchg_usbin_int_func);
	INIT_DELAYED_WORK(&chip->wait_wpc_chg_quit, op_wait_wpc_chg_quit_work);
	INIT_DELAYED_WORK(&chip->dischg_work, wlchg_dischg_work);
	INIT_DELAYED_WORK(&chip->wlchg_task_work, wlchg_task_work_process);
	INIT_DELAYED_WORK(&chip->update_bat_info_work, exfg_update_batinfo);
	INIT_DELAYED_WORK(&chip->fastchg_curr_vol_work, curr_vol_check_process);
	INIT_DELAYED_WORK(&chip->tx_check_work, wlchg_tx_check_process);
	INIT_DELAYED_WORK(&chip->charger_exit_work, wlchg_charger_offline);
	INIT_DELAYED_WORK(&chip->wlchg_connect_check_work, wlchg_connect_check_work);
	INIT_DELAYED_WORK(&chip->wlchg_fcc_stepper_work, wlchg_fcc_stepper_work);
#ifdef HW_TEST_EDITION
	INIT_DELAYED_WORK(&chip->w30w_timeout_work, w30w_timeout_work_process);
#endif
	mutex_init(&chip->chg_lock);
	mutex_init(&chip->connect_lock);
	mutex_init(&chip->read_lock);
	mutex_init(&chip->msg_lock);

	init_waitqueue_head(&chip->read_wq);
	chip->wlchg_wake_lock = wakeup_source_register(chip->dev, "wlchg_wake_lock");
	chip->reverse_wlchg_wake_lock = wakeup_source_register(chip->dev, "reverse_wlchg_wake_lock");

	ret = init_wireless_charge_proc(chip);
	if (ret < 0) {
		chg_err("Create wireless charge proc error.");
		goto free_psy;
	}
	platform_set_drvdata(pdev, chip);

	chip->wlcs_fcc_votable = create_votable("WLCH_FCC", VOTE_MIN,
						wlch_fcc_vote_callback,
						chip);
	if (IS_ERR(chip->wlcs_fcc_votable)) {
		ret = PTR_ERR(chip->wlcs_fcc_votable);
		chip->wlcs_fcc_votable = NULL;
		goto free_proc;
	}

	chip->fastchg_disable_votable = create_votable("WLCHG_FASTCHG_DISABLE", VOTE_SET_ANY,
						wlchg_fastchg_disable_vote_callback,
						chip);
	if (IS_ERR(chip->fastchg_disable_votable)) {
		ret = PTR_ERR(chip->fastchg_disable_votable);
		chip->fastchg_disable_votable = NULL;
		goto free_proc;
	}

	ret = wireless_chg_init(chip);
	if (ret < 0) {
		pr_err("Couldn't init step and jieta char, ret = %d\n", ret);
		goto free_proc;
	}

	chip->wlchg_device.minor = MISC_DYNAMIC_MINOR;
	chip->wlchg_device.name = "wlchg";
	chip->wlchg_device.fops = &wlchg_dev_fops;
	ret = misc_register(&chip->wlchg_device);
	if (ret) {
		pr_err("%s : misc_register failed\n", __FILE__);
		goto free_proc;
	}

	boot_mode = get_boot_mode();
	if (boot_mode == MSM_BOOT_MODE_FACTORY) {
		chg_info("wkcs: boot on FTM mode\n");
		chip->wlchg_status.ftm_mode = true;
	} else {
		chip->wlchg_status.ftm_mode = false;
	}

	wlchg_rx_policy_register(chip);

	chg_debug(" call end\n");

	return 0;

free_proc:
	remove_proc_entry("wireless", NULL);
free_psy:
	power_supply_unregister(chip->wireless_psy);
free_gpio:
	if (gpio_is_valid(chip->dcdc_en_gpio))
		gpio_free(chip->dcdc_en_gpio);
	if (gpio_is_valid(chip->wrx_otg_gpio))
		gpio_free(chip->wrx_otg_gpio);
	if (gpio_is_valid(chip->wrx_en_gpio))
		gpio_free(chip->wrx_en_gpio);
	if (gpio_is_valid(chip->usbin_int_gpio))
		gpio_free(chip->usbin_int_gpio);

	return ret;
}

static int wlchg_driver_remove(struct platform_device *pdev)
{
	struct op_chg_chip *chip = platform_get_drvdata(pdev);

	remove_proc_entry("wireless", NULL);
	power_supply_unregister(chip->wireless_psy);
	if (gpio_is_valid(chip->dcdc_en_gpio))
		gpio_free(chip->dcdc_en_gpio);
	if (gpio_is_valid(chip->wrx_otg_gpio))
		gpio_free(chip->wrx_otg_gpio);
	if (gpio_is_valid(chip->wrx_en_gpio))
		gpio_free(chip->wrx_en_gpio);
	if (gpio_is_valid(chip->usbin_int_gpio))
		gpio_free(chip->usbin_int_gpio);

	return 0;
}

/**********************************************************
 *
 *  [platform_driver API]
 *
 *********************************************************/

static const struct of_device_id wlchg_match[] = {
	{ .compatible = "op,wireless-charger" },
	{},
};

static struct platform_driver wlchg_driver = {
	.driver = {
		.name = "wireless-charger",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(wlchg_match),
	},
	.probe = wlchg_driver_probe,
	.remove = wlchg_driver_remove,
};

module_platform_driver(wlchg_driver);
MODULE_DESCRIPTION("Driver for wireless charger");
MODULE_LICENSE("GPL v2");

