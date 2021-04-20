#include <linux/interrupt.h>
#include <linux/i2c.h>

#include <linux/interrupt.h>
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
#include <linux/of_irq.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/rtc.h>
#include <linux/power_supply.h>
#include "linux/oem/project_info.h"

#include <op_mp2650.h>
#include <linux/oem/boot_mode.h>

#define DEBUG_BY_FILE_OPS
#define MP2762_CP_PSY

struct mp2650_charger *s_mcharger = NULL;
int reg_access_allow = 0;
int mp2650_reg = 0;

static void mp2650_set_mps_otg_en_val(int value);
static int mp2650_otg_enable(struct mp2650_charger *chg, bool en);
static int mp2650_get_vbus_voltage(int *vbus_mv);
static void mp2650_dump_registers(void);
static int mp2650_otg_ilim_set(int ilim_ma);
static int mp2650_read_reg(int reg, int *returnData);
static int mp2650_set_prechg_voltage_threshold(u8 bit);

static DEFINE_MUTEX(mp2650_i2c_access);

static int test_mp2650_write_reg(int reg, int val)
{
	int ret = 0;
	struct mp2650_charger *chg = s_mcharger;

	if(!chg) {
		chg_err("chg is NULL\n");
		return 0;
	}

	ret = i2c_smbus_write_byte_data(chg->client, reg, val);
	if (ret < 0) {
		chg_err("i2c write fail: can't write %02x to %02x: %d\n",
		val, reg, ret);
		return ret;
	}

	return 0;
}

static ssize_t mp2650_reg_access_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%02x\n", reg_access_allow);
}
static ssize_t mp2650_reg_access_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	pr_err("%s: value=%d\n", __FUNCTION__, val);
	reg_access_allow = val;

	return count;

}

static ssize_t mp2650_reg_set_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;
	int reg_val = 0;
	int ret = 0;

	ret = mp2650_read_reg(mp2650_reg, &reg_val);
	if (ret < 0) {
		pr_err("read reg 0x%02x err.", mp2650_reg);
		return ret;
	}

	count += snprintf(buf+count, PAGE_SIZE-count, "reg[0x%02x]: 0x%02x\n", mp2650_reg, reg_val);
	return count;
}

static ssize_t mp2650_reg_set_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int databuf[2] = {0, 0};

	if(2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
		pr_err("%s:reg[0x%02x]=0x%02x\n", __FUNCTION__, databuf[0], databuf[1]);
		mp2650_reg = databuf[0];
		test_mp2650_write_reg((int)databuf[0], (int)databuf[1]);
	}
	return count;
}

static ssize_t mp2650_regs_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	ssize_t len = 0;
	int i = 0;
	int reg_val = 0;

	for (i = MP2650_FIRST_REG; i <= MP2650_LAST_REG; i++) {
		(void)mp2650_read_reg(i, &reg_val);
		len += snprintf(buf+len, PAGE_SIZE-len, "reg:0x%02x=0x%02x \n", i, reg_val);
	}
	return len;
}
static ssize_t mp2650_regs_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{

	return count;
}

static DEVICE_ATTR(reg_access, S_IWUSR | S_IRUGO, mp2650_reg_access_show, mp2650_reg_access_store);
static DEVICE_ATTR(reg_set, S_IWUSR | S_IRUGO, mp2650_reg_set_show, mp2650_reg_set_store);
static DEVICE_ATTR(read_regs, S_IWUSR | S_IRUGO, mp2650_regs_show, mp2650_regs_store);

static struct attribute *mp2650_attributes[] = {
	&dev_attr_reg_access.attr,
	&dev_attr_reg_set.attr,
	&dev_attr_read_regs.attr,
	NULL
};

static struct attribute_group mp2650_attribute_group = {
	.attrs = mp2650_attributes
};

static int __mp2650_read_reg(int reg, int *returnData)
{
	int ret = 0;
	struct mp2650_charger *chg = s_mcharger;

	ret = i2c_smbus_read_byte_data(chg->client, (unsigned char)reg);
	if (ret < 0) {
		chg_err("i2c read fail: can't read from %02x: %d\n", reg, ret);
		return ret;
	} else {
		*returnData = ret;
	}

	return 0;
}

static int mp2650_read_reg(int reg, int *returnData)
{
	int ret = 0;

	mutex_lock(&mp2650_i2c_access);
	ret = __mp2650_read_reg(reg, returnData);
	mutex_unlock(&mp2650_i2c_access);
	return ret;
}

static int __mp2650_write_reg(int reg, int val)
{
	int ret = 0;
	struct mp2650_charger *chg = s_mcharger;

	if (reg_access_allow != 0) {
		chg_err("can not access registers\n");
		return 0;
	}
	ret = i2c_smbus_write_byte_data(chg->client, reg, val);
	if (ret < 0) {
		chg_err("i2c write fail: can't write %02x to %02x: %d\n",
		val, reg, ret);
		return ret;
	}

	return 0;
}

/**********************************************************
  *
  *   [Read / Write Function]
  *
  *********************************************************/
/*
*/

static int mp2650_config_interface(int RegNum, int val, int MASK)
{
    int mp2650_reg = 0;
    int ret = 0;

    mutex_lock(&mp2650_i2c_access);

    ret = __mp2650_read_reg(RegNum, &mp2650_reg);

    //chg_err(" Reg[%x]=0x%x\n", RegNum, mp2650_reg);

    mp2650_reg &= ~MASK;
    mp2650_reg |= val;

    ret = __mp2650_write_reg(RegNum, mp2650_reg);

    //chg_err(" write Reg[%x]=0x%x\n", RegNum, mp2650_reg);

    __mp2650_read_reg(RegNum, &mp2650_reg);

    //chg_err(" Check Reg[%x]=0x%x\n", RegNum, mp2650_reg);

    mutex_unlock(&mp2650_i2c_access);

    return ret;

}

static int mp2650_set_vindpm_vol(int vol_mv)//default 4.5V
{
	// Input voltage limit threshold (0-25.5V)
	int rc;
	int tmp = 0;

	struct mp2650_charger *chg = s_mcharger;

	if (atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	tmp = (vol_mv - REG01_MP2650_VINDPM_THRESHOLD_OFFSET) / REG01_MP2650_VINDPM_THRESHOLD_STEP;
	rc = mp2650_config_interface(REG01_MP2650_ADDRESS, tmp << REG01_MP2650_VINDPM_THRESHOLD_SHIFT, REG01_MP2650_VINDPM_THRESHOLD_MASK);

	return rc;
}

static int mp2650_get_vindpm_vol(void)
{
	int reg_val = 0;
	int rc = 0;
	rc = mp2650_read_reg(REG01_MP2650_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG00_mp2650_ADDRESS rc = %d\n", rc);
		return 0;
	}
	reg_val = reg_val * REG01_MP2650_VINDPM_THRESHOLD_STEP + REG01_MP2650_VINDPM_THRESHOLD_OFFSET;
	return reg_val;
}

static void mp2650_set_aicl_point(int vbatt_mv)
{
    struct mp2650_charger *chip = s_mcharger;

	if(chip->hw_aicl_point == 4440 && vbatt_mv > 4140) {
		chip->hw_aicl_point = 4520;
		chip->sw_aicl_point = 4535;
		mp2650_set_vindpm_vol(chip->hw_aicl_point);
	} else if(chip->hw_aicl_point == 4520 && vbatt_mv < 4000) {
		chip->hw_aicl_point = 4440;
		chip->sw_aicl_point = 4500;
		mp2650_set_vindpm_vol(chip->hw_aicl_point);
	}

	if (!chip->pre_chg_thd_6600 && vbatt_mv > 3500) {
		chip->pre_chg_thd_6600 = true;
		mp2650_set_prechg_voltage_threshold(REG07_MP2650_PRECHARGE_THRESHOLD_6600MV);
	}
}

static int mp2650_charging_current_write_fast(int chg_cur)
{
	int rc = 0;
	int tmp = 0;
	struct mp2650_charger *chg = s_mcharger;
	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	chg_err("set charge current = %d\n", chg_cur);


	tmp = chg_cur - REG02_MP2650_CHARGE_CURRENT_SETTING_OFFSET;
	tmp = tmp / REG02_MP2650_CHARGE_CURRENT_SETTING_STEP;

	rc = mp2650_config_interface(REG02_MP2650_ADDRESS, tmp << REG02_MP2650_CHARGE_CURRENT_SETTING_SHIFT, REG02_MP2650_CHARGE_CURRENT_SETTING_MASK);

	return rc;
}

static int mp2650_charging_current_read_fast(int *chg_cur)
{
	int rc = 0;
	int tmp = 0;
	struct mp2650_charger *chg = s_mcharger;
	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_read_reg(REG02_MP2650_ADDRESS, &tmp);
	if (rc < 0) {
		chg_err("can't read REG02 fcc, rc = %d\n", rc);
		*chg_cur = 0;
	} else
		*chg_cur = tmp * REG02_MP2650_CHARGE_CURRENT_SETTING_STEP + REG02_MP2650_CHARGE_CURRENT_SETTING_OFFSET;

	return rc;
}

static int mp2650_set_enable_volatile_writes(void)
{
	int rc = 0;
	struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}
	//need do nothing

	return rc;
}

static int mp2650_set_complete_charge_timeout(int val)
{
	int rc = 0;
	struct mp2650_charger *chg = s_mcharger;
	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	if (val == OVERTIME_AC) {
		val = REG09_MP2650_CHARGE_TIMER_EN_ENABLE | REG09_MP2650_FAST_CHARGE_TIMER_8H;
	} else if (val == OVERTIME_USB) {
		val = REG09_MP2650_CHARGE_TIMER_EN_ENABLE | REG09_MP2650_FAST_CHARGE_TIMER_12H;
	} else {
		val = REG09_MP2650_CHARGE_TIMER_EN_DISABLE | REG09_MP2650_FAST_CHARGE_TIMER_20H;
	}

	rc = mp2650_config_interface(REG09_MP2650_ADDRESS, val, REG09_MP2650_FAST_CHARGE_TIMER_MASK | REG09_MP2650_CHARGE_TIMER_EN_MASK);
	if (rc < 0) {
		chg_err("Couldn't complete charge timeout rc = %d\n", rc);
	}
	return 0;
}

static int mp2650_float_voltage_write(int vfloat_mv)
{
	int rc = 0;
	int tmp = 0;
	int fv_val = 0;
	struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	chg_err("vfloat_mv = %d\n", vfloat_mv);

	if (vfloat_mv > 5000) {
		if (vfloat_mv > 9000) {
			vfloat_mv = 9000;
		}
		vfloat_mv = vfloat_mv / 2;
	}

	if (vfloat_mv > 4500) {
		vfloat_mv = 4500;
	}

	tmp = vfloat_mv * 10 - REG04_MP2650_CHARGE_FULL_VOL_OFFSET;
	tmp = tmp / REG04_MP2650_CHARGE_FULL_VOL_STEP;
	tmp <<= REG04_MP2650_CHARGE_FULL_VOL_SHIFT;


	rc = mp2650_read_reg(REG04_MP2650_ADDRESS, &fv_val);
	if (rc) {
		rc = 0;
		chg_err("Couldn't read REG04_MP2650_ADDRESS rc = %d\n", rc);
	} else if ((fv_val & REG04_MP2650_CHARGE_FULL_VOL_MASK) == tmp) {
		chg_err("set the same fv 0x%2x, return!", tmp);
		return 0;
	}

	rc = mp2650_config_interface(REG04_MP2650_ADDRESS, tmp, REG04_MP2650_CHARGE_FULL_VOL_MASK);

	return rc;
}

static int mp2650_float_voltage_read(int *vfloat_mv)
{
	int rc = 0;
	int fv_val = 0;
	struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}
	rc = mp2650_read_reg(REG04_MP2650_ADDRESS, &fv_val);
	if (rc) {
		chg_err("Couldn't read REG04_MP2650_ADDRESS rc = %d\n", rc);
		return rc;
	}

	*vfloat_mv = ((fv_val & REG04_MP2650_CHARGE_FULL_VOL_MASK) * REG04_MP2650_CHARGE_FULL_VOL_STEP
				+ REG04_MP2650_CHARGE_FULL_VOL_OFFSET * 2) / 10;

	return 0;
}

static int mp2650_set_prechg_voltage_threshold(u8 bit)
{
	int rc = 0;
	struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_config_interface(REG07_MP2650_ADDRESS, bit, REG07_MP2650_PRECHARGE_THRESHOLD_MASK);

	return 0;
}

static int mp2650_set_prechg_current( int ipre_mA)
{
	int tmp = 0;
	int rc = 0;
	struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	tmp = ipre_mA - REG03_MP2650_PRECHARGE_CURRENT_LIMIT_OFFSET;
	tmp = tmp / REG03_MP2650_PRECHARGE_CURRENT_LIMIT_STEP;
	rc = mp2650_config_interface(REG03_MP2650_ADDRESS, (tmp + 1) << REG03_MP2650_PRECHARGE_CURRENT_LIMIT_SHIFT, REG03_MP2650_PRECHARGE_CURRENT_LIMIT_MASK);

	return 0;
}

static int mp2650_set_termchg_current(int term_curr)
{
	int rc = 0;
	int tmp = 0;

	struct mp2650_charger *chg = s_mcharger;
	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	chg_err("term_current = %d\n", term_curr);
	tmp = term_curr - REG03_MP2650_TERMINATION_CURRENT_LIMIT_OFFSET;
	tmp = tmp / REG03_MP2650_TERMINATION_CURRENT_LIMIT_STEP;

	rc = mp2650_config_interface(REG03_MP2650_ADDRESS, tmp << REG03_MP2650_TERMINATION_CURRENT_LIMIT_SHIFT, REG03_MP2650_TERMINATION_CURRENT_LIMIT_MASK);
	return 0;
}

static int mp2650_set_rechg_voltage(int recharge_mv)
{
	   int rc = 0;
	int tmp = 0;
	struct mp2650_charger *chg = s_mcharger;
	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	tmp = recharge_mv - REG04_MP2650_BAT_RECHARGE_THRESHOLD_OFFSET;
	tmp = tmp / REG04_MP2650_BAT_RECHARGE_THRESHOLD_STEP;


	/*The rechg voltage is: Charge Full Voltage - 100mV  or - 200mV, default is - 100mV*/
	rc = mp2650_config_interface(REG04_MP2650_ADDRESS, tmp << REG04_MP2650_BAT_RECHARGE_THRESHOLD_SHIFT, REG04_MP2650_BAT_RECHARGE_THRESHOLD_MASK);

	if (rc) {
		chg_err("Couldn't set recharging threshold rc = %d\n", rc);
	}
	 return rc;
}

static int mp2650_set_wdt_timer(int reg)
{
	int rc = 0;
	struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_config_interface(REG09_MP2650_ADDRESS, reg, REG09_MP2650_WTD_TIMER_MASK);
	if (rc) {
		chg_err("Couldn't set recharging threshold rc = %d\n", rc);
	}

	return 0;
}

static int mp2650_set_chging_term_enable(bool enable)
{
	int rc = 0;
	struct mp2650_charger *chg = s_mcharger;
	if (atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}
	chg_info("%sable charging term.", enable ? "en" : "dis");
	if (enable)
		rc = mp2650_config_interface(REG09_MP2650_ADDRESS, REG09_MP2650_CCHARGE_TERMINATION_EN_ENABLE, REG09_MP2650_CHARGE_TERMINATION_EN_MASK);
	else
		rc = mp2650_config_interface(REG09_MP2650_ADDRESS, REG09_MP2650_CHARGE_TERMINATION_EN_DISABLE, REG09_MP2650_CHARGE_TERMINATION_EN_MASK);
	if (rc) {
		chg_err("Couldn't set chging term %sable rc = %d\n", enable ? "en" : "dis",rc);
	}
	return rc;
}

static int mp2650_enable_charging(struct mp2650_charger *chg, bool enable)
{
	int rc;

	if (chg == NULL)
		return -ENODEV;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	if (enable) {
		mp2650_otg_enable(chg, false);
		rc = mp2650_config_interface(REG08_MP2650_ADDRESS,
			REG08_MP2650_CHG_EN_ENABLE, REG08_MP2650_CHG_EN_MASK);
	} else {
		rc = mp2650_config_interface(REG08_MP2650_ADDRESS,
			REG08_MP2650_CHG_EN_DISABLE, REG08_MP2650_CHG_EN_MASK);
	}
	if (rc < 0) {
		chg_err("Couldn'tmp2650_enable_charging rc = %d\n", rc);
	}

	chg_err("mp2650 %sable charging.", enable ? "en" : "dis");
	return rc;
}

static int mp2650_check_charging_enable(struct mp2650_charger *chg)
{
	int rc = 0;
	int reg_val = 0;
	//struct mp2650_charger *chg = s_mcharger;
	bool charging_enable = false;

	if (chg == NULL)
		return -ENODEV;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}
	rc = mp2650_read_reg(REG08_MP2650_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG08_MP2650_ADDRESS rc = %d\n", rc);
		return 0;
	}

	charging_enable = ((reg_val & REG08_MP2650_CHG_EN_MASK) == REG08_MP2650_CHG_EN_ENABLE) ? 1 : 0;

	return charging_enable;
}

static int mp2650_get_vbus_voltage(int *vbus_mv)
{
	int vol_high = 0;
	int vol_low = 0;
	int rc = 0;

	rc = mp2650_read_reg(REG1C1D_MP2650_ADDRESS, &vol_low);
	if (rc) {
		chg_err("Couldn't read REG1C1D_MP2650_ADDRESS rc = %d\n", rc);
		*vbus_mv = 0;
		return rc;
	}

	rc = mp2650_read_reg((REG1C1D_MP2650_ADDRESS + 1), &vol_high);
	if (rc) {
		chg_err("Couldn't read REG1C1D_MP2650_ADDRESS rc = %d\n", rc);
		*vbus_mv = 0;
		return rc;
	}

	*vbus_mv = (vol_high * 100) + ((vol_low >> 6) * 25);
	//chg_err("vol_high = 0x%x, vol_low = 0x%x, vbus_vol[%d]\n", vol_high, vol_low, vbus_vol);

	return rc;
}

static int mp2650_get_ibus_current(int *ibus_ua)
{
	int cur_high = 0;
	int cur_low = 0;
	int rc = 0;

	rc = mp2650_read_reg(REG1E1F_MP2650_ADDRESS, &cur_low);
	if (rc) {
		chg_err("Couldn't read REG1E1F_MP2650_ADDRESS rc = %d\n", rc);
		return rc;
	}

	rc = mp2650_read_reg((REG1E1F_MP2650_ADDRESS + 1), &cur_high);
	if (rc) {
		chg_err("Couldn't read REG1E1F_MP2650_ADDRESS + 1 rc = %d\n", rc);
		return rc;
	}

	*ibus_ua = (cur_high * 25000) + ((cur_low >> 6) * 6250);
	chg_err("cur_high = 0x%x, cur_low = 0x%x, ibus_curr[%d]\n", cur_high, cur_low, *ibus_ua);

	return rc;
}

static int mp2650_get_charge_status(int *status)
{
	int rc;
	int chg_stat_bit = 0;
	int chg_stat = 0;
	struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_read_reg(REG13_MP2650_ADDRESS, &chg_stat_bit);
	if (rc) {
		chg_err("Couldn't read STAT_C rc = %d\n", rc);
		return rc;
	}

	chg_stat = chg_stat_bit & REG13_MP2650_VIN_POWER_GOOD_MASK;
	if (chg_stat == REG13_MP2650_VIN_POWER_GOOD_NO) {
		*status = POWER_SUPPLY_STATUS_DISCHARGING;
		return 0;
	}

	chg_stat = chg_stat_bit & REG13_MP2650_CHARGING_STATUS_MASK;
	switch (chg_stat) {
	case REG13_MP2650_CHARGING_STATUS_NOT_CHARGING:
		*status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	// Fall-through.
	case REG13_MP2650_CHARGING_STATUS_PRE_CHARGE:
	case REG13_MP2650_CHARGING_STATUS_FAST_CHARGE:
		*status = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case REG13_MP2650_CHARGING_STATUS_CHARGE_TERMINATION:
		chg_err("the mp2650 is full");
		*status = POWER_SUPPLY_STATUS_FULL;
		mp2650_dump_registers();
		break;
	default:
		*status = POWER_SUPPLY_STATUS_UNKNOWN;
		break;
	}

	return 0;
}

static int mp2650_enable_suspend_charger(bool enable)
{
	int rc = 0;
	struct mp2650_charger *chg = s_mcharger;

	if (atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	chg->input_suspend = enable;
	if (enable)
		rc = mp2650_config_interface(REG08_MP2650_ADDRESS,
				REG08_MP2650_LEARN_EN_ENABLE, REG08_MP2650_LEARN_EN_MASK);
	else
		rc = mp2650_config_interface(REG08_MP2650_ADDRESS,
				REG08_MP2650_LEARN_EN_DISABLE, REG08_MP2650_LEARN_EN_MASK);

	chg_info( " rc = %d\n", rc);
	if (rc < 0) {
		chg_err("Couldn't mp2650_%ssuspend_charger rc = %d\n", enable ? "" : "un", rc);
	}

	return rc;
}

static int mp2650_otg_enable(struct mp2650_charger *chg, bool enable)
{
	int rc;

	if (chg == NULL)
		return -ENODEV;

	if (atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	chg->otg_enabled = enable;
	if (enable) {
		mp2650_set_mps_otg_en_val(1);  //set output 5V vbus

		rc = mp2650_otg_ilim_set(MP2650_OTG_CURRENT_LIMIT_DEFAULT);
		if (rc < 0) {
			chg_err("Couldn't mp2650_otg_ilim_set rc = %d\n", rc);
		}
		rc = mp2650_config_interface(REG08_MP2650_ADDRESS,
				REG08_MP2650_OTG_EN_ENABLE, REG08_MP2650_OTG_EN_MASK);
		if (rc < 0) {
			chg_err("Couldn't mp2650_otg_enable  rc = %d\n", rc);
		}

		mp2650_set_wdt_timer(REG09_MP2650_WTD_TIMER_DISABLE);
	} else {
		mp2650_set_mps_otg_en_val(0);  //set disable output 5V vbus
		rc = mp2650_config_interface(REG08_MP2650_ADDRESS,
					REG08_MP2650_OTG_EN_DISABLE, REG08_MP2650_OTG_EN_MASK);
		if (rc < 0) {
			chg_err("Couldn't mp2650_otg_disable  rc = %d\n", rc);
		}

		mp2650_set_wdt_timer(REG09_MP2650_WTD_TIMER_40S);
	}
	return rc;
}

static int mp2650_other_registers_init(void)
{
    int rc;
    struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_config_interface(REG10_MP2650_ADDRESS, 0x01, 0xff);
	rc = mp2650_config_interface(REG11_MP2650_ADDRESS, 0xfe, 0xff);
	return rc;
}

int mp2650_reset_charger(void)
{
	int rc;
	struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	chg_err(" mp2650_reset_charger start \n");
	rc = mp2650_config_interface(REG08_MP2650_ADDRESS,
		REG08_MP2650_REG_RST_RESET, REG08_MP2650_REG_RST_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_reset_charger  rc = %d\n", rc);
	}

	return rc;
}

static int mp2650_otg_ilim_set(int ilim_ma)
{
	int rc;
	struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	ilim_ma /= REG07_MP2650_OTG_CURRENT_LIMIT_STEP;
	rc = mp2650_config_interface(REG07_MP2650_ADDRESS,
				ilim_ma, REG07_MP2650_OTG_CURRENT_LIMIT_MASK );
	if (rc < 0) {
		chg_err("Couldn't mp2650_otg_ilim_set  rc = %d\n", rc);
	}

	return rc;
}

static int mp2650_otg_ilim_get(int *ilim_ma)
{
	int rc;
	int ocl_bit;
	struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_read_reg(REG07_MP2650_ADDRESS, &ocl_bit);
	if (rc < 0) {
		chg_err("Couldn't read REG07 rc = %d\n", rc);
		*ilim_ma = 0;
		return rc;
	}

	*ilim_ma = (ocl_bit & REG07_MP2650_OTG_CURRENT_LIMIT_MASK) * REG07_MP2650_OTG_CURRENT_LIMIT_STEP;

	return 0;
}

int mp2650_set_switching_frequency(void)
{
	// set default 800k;
	int rc = 0;
	struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_config_interface(REG0B_MP2650_ADDRESS, REG0B_MP2650_SW_FREQ_800K, REG0B_MP2650_SW_FREQ_MASK);
	return 0;
}

static int mp2650_set_mps_otg_voltage(bool is_9v)
{
	int rc = 0;
	struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	if(is_9v){
		rc = mp2650_config_interface(REG06_MP2650_ADDRESS, REG06_MP2650_OTG_VOL_OPTION_8750MV, REG06_MP2650_OTG_VOL_OPTION_MASK);
		rc = mp2650_config_interface(REG06_MP2650_ADDRESS, REG06_MP2650_2ND_OTG_VOL_SETTING_250MV, REG06_MP2650_2ND_OTG_VOL_SETTING_MASK);
	} else {
		rc = mp2650_config_interface(REG06_MP2650_ADDRESS, REG06_MP2650_OTG_VOL_OPTION_4750MV, REG06_MP2650_OTG_VOL_OPTION_MASK);
		rc = mp2650_config_interface(REG06_MP2650_ADDRESS, REG06_MP2650_2ND_OTG_VOL_SETTING_250MV, REG06_MP2650_2ND_OTG_VOL_SETTING_MASK);
	}
	return 0;
}

int mp2650_set_mps_otg_enable(bool enable)
{
	//int value;
	int rc = 0;
	struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	chg_err("%sable start.", enable ? "en" : "dis");
	if (enable)
		rc = mp2650_config_interface(REG08_MP2650_ADDRESS,
				REG08_MP2650_OTG_EN_ENABLE, REG08_MP2650_OTG_EN_MASK);
	else
		rc = mp2650_config_interface(REG08_MP2650_ADDRESS,
				REG08_MP2650_OTG_EN_DISABLE, REG08_MP2650_OTG_EN_MASK);
	return 0;
}

int mp2650_enable_hiz(bool enable)
{
	int rc;
	struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	if (enable)
		rc = mp2650_config_interface(REG08_MP2650_ADDRESS,
				 REG08_MP2650_LEARN_EN_ENABLE, REG08_MP2650_LEARN_EN_MASK);
	else
		rc = mp2650_config_interface(REG08_MP2650_ADDRESS,
				REG08_MP2650_LEARN_EN_DISABLE, REG08_MP2650_LEARN_EN_MASK);
	if (rc < 0) {
		chg_err("Couldn'mp2650_%sable_hiz rc = %d\n", enable ? "en" : "dis", rc);
	}

	chg_err("mp2650_%sable_hiz.", enable ? "en" : "dis");

	return rc;
}

static int mp2650_enable_buck_switch(bool enable)
{
	int rc;
	struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	chg->buck_switcher_on = enable;
	if (enable)
		rc = mp2650_config_interface(REG12_MP2650_ADDRESS,
				REG12_MP2650_BUCK_SWITCH_ENABLE, REG12_MP2650_BUCK_SWITCH_MASK);
	else
		rc = mp2650_config_interface(REG12_MP2650_ADDRESS,
				 REG12_MP2650_BUCK_SWITCH_DISABLE, REG12_MP2650_BUCK_SWITCH_MASK);
	if (rc < 0) {
		chg_err("Couldn'mp2650_%sable_dig_skip rc = %d\n", enable ? "en" : "dis", rc);
	}

	chg_err("mp2650_%sable_dig_skip \n", enable ? "en" : "dis");

	return rc;
}

static int mp2650_vbus_avoid_electric_config(void)
{
	int rc;
	struct mp2650_charger *chip = s_mcharger;
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	rc = mp2650_config_interface(REG53_MP2650_ADDRESS, 0x95, 0xff);
	rc = mp2650_config_interface(REG39_MP2650_ADDRESS, 0x40, 0xff);
 	rc = mp2650_config_interface(REG08_MP2650_ADDRESS, 0x36, 0xff);
	rc = mp2650_config_interface(REG08_MP2650_ADDRESS, 0x16, 0xff);
	rc = mp2650_config_interface(REG39_MP2650_ADDRESS, 0x00, 0xff);
	rc = mp2650_config_interface(REG53_MP2650_ADDRESS, 0x00, 0xff);
	rc = mp2650_config_interface(REG2F_MP2650_ADDRESS, 0x15, 0xff);
	return 0;
}

static int mp2650_set_charger_vsys_threshold(int val)
{
    int rc;
    struct mp2650_charger *chip = s_mcharger;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	//change Vsys Skip threshold
	rc = mp2650_config_interface(REG31_MP2650_ADDRESS, val, 0xff);

	return rc;
}

static int mp2650_burst_mode_enable(bool enable)
{
    int rc;
    struct mp2650_charger *chip = s_mcharger;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	//Enable or disable Burst mode
	if (enable)
		rc = mp2650_config_interface(REG37_MP2650_ADDRESS, 0x67, 0xff);
	else
		rc = mp2650_config_interface(REG37_MP2650_ADDRESS, 0x66, 0xff);

	return rc;
}

#define DUMP_REG_LOG_CNT_30S 6
static void mp2650_dump_registers(void)
{
	int rc;
	int addr;
	static int dump_count = 0;
	struct mp2650_charger *chg = s_mcharger;
	unsigned int val_buf[MP2650_DUMP_MAX_REG + 3] = {0x0};

	if(atomic_read(&chg->charger_suspended) == 1) {
		return ;
	}

	if(dump_count == DUMP_REG_LOG_CNT_30S) {
		dump_count = 0;
		for (addr = MP2650_FIRST_REG; addr <= MP2650_DUMP_MAX_REG; addr++) {
			rc = mp2650_read_reg(addr, &val_buf[addr]);
			if (rc) {
				chg_err("Couldn't read 0x%02x rc = %d\n", addr, rc);
			}
		}
		rc = mp2650_read_reg(0x48, &val_buf[MP2650_DUMP_MAX_REG + 1]);
		if (rc) {
			 chg_err("Couldn't  read 0x48 rc = %d\n", rc);
		}
		rc = mp2650_read_reg(0x49, &val_buf[MP2650_DUMP_MAX_REG + 2]);
		if (rc) {
			 chg_err("Couldn't  read 0x49 rc = %d\n", rc);
		}

	  printk(KERN_ERR "mp2650_dump_reg: [0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x],\
		  [0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x],\
		  [0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x],\
		  [0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x],\
		  [0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x],\
		  [0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x],\
		  [0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x],\
		  [reg48=0x%02x,reg49=0x%02x]\n",
		val_buf[0], val_buf[1], val_buf[2], val_buf[3], val_buf[4],
		val_buf[5], val_buf[6], val_buf[7], val_buf[8], val_buf[9],
		val_buf[10], val_buf[11], val_buf[12], val_buf[13], val_buf[14],
		val_buf[15], val_buf[16], val_buf[17], val_buf[18], val_buf[19],
		val_buf[20], val_buf[21], val_buf[22], val_buf[23], val_buf[24],
		val_buf[25], val_buf[26], val_buf[27], val_buf[28], val_buf[29],
		val_buf[30], val_buf[31], val_buf[32], val_buf[33], val_buf[34],
		val_buf[35], val_buf[36]);
	}
	dump_count++;
}
bool mp2650_need_to_check_ibatt(void)
{
	return false;
}

int mp2650_get_chg_current_step(void)
{
	int rc = 50;

	return rc;
}

static int mp2650_input_current_limit_init(void)
{
	int rc = 0;
	struct mp2650_charger *chg = s_mcharger;

	if (atomic_read(&chg->charger_suspended) == 1) {
		chg_err("mp2650_input_current_limit_init: in suspended\n");
		return 0;
	}
	rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_500MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_500MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);

	if (rc < 0) {
		chg_err("Couldn't mp2650_input_current_limit_init rc = %d\n", rc);
	}

	return rc;
}

static int mp2650_input_current_limit_without_aicl(int current_ma)
{
	int rc = 0;
	int reg_val = 0;
	struct mp2650_charger *chg = s_mcharger;

	if (atomic_read(&chg->charger_suspended) == 1) {
		chg_err("mp2650_input_current_limit_init: in suspended\n");
		return 0;
	}
	chg->pre_current_ma = current_ma;

	reg_val = current_ma / REG00_MP2650_1ST_CURRENT_LIMIT_STEP;
	chg_err(" reg_val current [%d]-%dma\n", reg_val, current_ma);
	rc = mp2650_config_interface(REG00_MP2650_ADDRESS, reg_val, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, reg_val, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);

	if (rc < 0) {
		chg_err("Couldn't mp2650_input_current_limit_init rc = %d\n", rc);
	}

	//debug only, remove after stable.
	//cancel_delayed_work_sync(&chg->dump_reg_work);
	//schedule_delayed_work(&chg->dump_reg_work, msecs_to_jiffies(1500));
	chg->sw_aicl_result_ma = current_ma;

	return rc;
}
/*
#define DEC_STEP 500
#define INC_STEP 200
static int mp2650_input_current_limit_sw_aicl(int current_ma)
{
	int rc = 0;
	int tmp = 0;
	int tmp_bit = 0;
	int count =0;
	int step = 0;
	int vbus = 0;
	int cur_now = 0;
	int dec_target = 0;
	struct mp2650_charger *chg = s_mcharger;

	if(atomic_read(&chg->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_read_reg(REG00_MP2650_ADDRESS, &tmp_bit);
	if (rc) {
		chg_err("Couldn't read REG00_mp2650_ADDRESS rc = %d\n", rc);
		return 0;
	}

	tmp = tmp_bit * 50;
	// warp(target ICL greater than 3A) decrease to 1A.
	if (current_ma > 3000)
		dec_target = 1000 / DEC_STEP;
	else
		dec_target = 500 / DEC_STEP;

	count = tmp / DEC_STEP;
	//chg_info("tmp=%d,count=%d", tmp, count);
	for (; count >= dec_target; count--) {
		tmp = DEC_STEP * count - REG00_MP2650_1ST_CURRENT_LIMIT_OFFSET;
		chg_err("set charge current limit = %d\n", tmp);
		tmp_bit = tmp / REG00_MP2650_1ST_CURRENT_LIMIT_STEP;
		rc = mp2650_config_interface(REG00_MP2650_ADDRESS, tmp_bit << REG00_MP2650_1ST_CURRENT_LIMIT_SHIFT, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, tmp_bit << REG0F_MP2650_2ND_CURRENT_LIMIT_SHIFT, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
		msleep(25);
	}

	mp2650_set_vindpm_vol(4400);

	step = (current_ma - tmp) / INC_STEP;
	cur_now = tmp + (current_ma - tmp) % INC_STEP;
	//chg_info("step=%d,cur_now=%d", step, cur_now);
	for (count = 0; count <= step; count++) {
		tmp = cur_now + INC_STEP * count - REG00_MP2650_1ST_CURRENT_LIMIT_OFFSET;
		chg_err("set charge current limit = %d\n", tmp);
		tmp_bit = tmp / REG00_MP2650_1ST_CURRENT_LIMIT_STEP;
		rc = mp2650_config_interface(REG00_MP2650_ADDRESS, tmp_bit << REG00_MP2650_1ST_CURRENT_LIMIT_SHIFT, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, tmp_bit << REG0F_MP2650_2ND_CURRENT_LIMIT_SHIFT, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
		msleep(90);
		mp2650_get_vbus_voltage(&vbus);
		if (vbus <= chg->sw_aicl_point)
			break;
	}

	tmp = count > step ? current_ma : (tmp - 200);
	tmp_bit = tmp / REG00_MP2650_1ST_CURRENT_LIMIT_STEP;
	chg_info("setting ICL=%d", tmp);
	rc = mp2650_config_interface(REG00_MP2650_ADDRESS, tmp_bit << REG00_MP2650_1ST_CURRENT_LIMIT_SHIFT, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, tmp_bit << REG0F_MP2650_2ND_CURRENT_LIMIT_SHIFT, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);

	mp2650_set_vindpm_vol(chg->hw_aicl_point);
	chg->sw_aicl_result_ma = tmp;
	return rc;
}
*/
int mp2650_parse_dt(void)
{
	return 0;
}

int mp2650_hardware_init(void)
{
	struct mp2650_charger *chg = s_mcharger;

	chg_err("init mp2650 hardware! \n");

	//must be before set_vindpm_vol and set_input_current
	chg->hw_aicl_point = 4440;
	chg->sw_aicl_point = 4500;
	chg->sw_aicl_result_ma = 500;

	mp2650_reset_charger();
	mp2650_enable_charging(chg, false);

	mp2650_set_chging_term_enable(false);

	mp2650_input_current_limit_init();

	mp2650_float_voltage_write(WPC_TERMINATION_VOLTAGE);

	mp2650_otg_ilim_set(MP2650_OTG_CURRENT_LIMIT_DEFAULT);

	mp2650_set_enable_volatile_writes();

	mp2650_set_complete_charge_timeout(OVERTIME_DISABLED);

	mp2650_set_prechg_voltage_threshold(REG07_MP2650_PRECHARGE_THRESHOLD_7200MV);
	chg->pre_chg_thd_6600 = false;

	mp2650_set_prechg_current(WPC_PRECHARGE_CURRENT);

	mp2650_charging_current_write_fast(WPC_CHARGE_CURRENT_DEFAULT);

	mp2650_set_termchg_current(WPC_TERMINATION_CURRENT);

	mp2650_set_rechg_voltage(WPC_RECHARGE_VOLTAGE_OFFSET);

	mp2650_set_switching_frequency();

	mp2650_set_vindpm_vol(chg->hw_aicl_point);

	mp2650_set_mps_otg_voltage(false);

	mp2650_set_mps_otg_enable(true);

	mp2650_other_registers_init();

	mp2650_enable_suspend_charger(false);

	mp2650_enable_charging(chg, true);

	mp2650_set_wdt_timer(REG09_MP2650_WTD_TIMER_40S);

	return true;
}

#ifdef CONFIG_OP_RTC_DET_SUPPORT
static int rtc_reset_check(void)
{
	struct rtc_time tm;
	struct rtc_device *rtc;
	int rc = 0;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
		__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return 0;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		pr_err("Error reading rtc device (%s) : %d\n",
		CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		pr_err("Invalid RTC time (%s): %d\n",
		CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	if ((tm.tm_year == 110) && (tm.tm_mon == 0) && (tm.tm_mday <= 1)) {
		chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  @@@ wday: %d, yday: %d, isdst: %d\n",
		tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
		tm.tm_wday, tm.tm_yday, tm.tm_isdst);
		rtc_class_close(rtc);
		return 1;
	}

	chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  ###  wday: %d, yday: %d, isdst: %d\n",
	tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
	tm.tm_wday, tm.tm_yday, tm.tm_isdst);

	close_time:
		rtc_class_close(rtc);
	return 0;
}
#endif /* CONFIG_OP_RTC_DET_SUPPORT */

static int mp2650_mps_otg_en_gpio_init(struct mp2650_charger *chg)
{

	if (!chg) {
		printk(KERN_ERR "[OP_CHG][%s]: mp2650_charger not ready!\n", __func__);
		return -EINVAL;
	}

	chg->pinctrl = devm_pinctrl_get(chg->dev);
	if (IS_ERR_OR_NULL(chg->pinctrl)) {
		chg_err("get mps_otg_en pinctrl fail\n");
		return -EINVAL;
	}

	chg->mps_otg_en_active = pinctrl_lookup_state(chg->pinctrl, "mps_otg_en_active");
	if (IS_ERR_OR_NULL(chg->mps_otg_en_active)) {
		chg_err("get mps_otg_en_active fail\n");
		return -EINVAL;
	}

	chg->mps_otg_en_sleep = pinctrl_lookup_state(chg->pinctrl, "mps_otg_en_sleep");
	if (IS_ERR_OR_NULL(chg->mps_otg_en_sleep)) {
		chg_err("get mps_otg_en fail\n");
		return -EINVAL;
	}

	chg->mps_otg_en_default = pinctrl_lookup_state(chg->pinctrl, "mps_otg_en_default");
	if (IS_ERR_OR_NULL(chg->mps_otg_en_default)) {
		chg_err("get mps_otg_en_default fail\n");
		return -EINVAL;
	}


	pinctrl_select_state(chg->pinctrl, chg->mps_otg_en_sleep);
	  chg_err("gpio_val:%d\n", gpio_get_value(chg->mps_otg_en_gpio));

	return 0;
}

static int mp2650_gpio_init(struct mp2650_charger *chg)
{

	int rc=0;
	struct device_node *node = chg->dev->of_node;

	// Parsing gpio mps_otg_en
	chg->mps_otg_en_gpio = of_get_named_gpio(node, "mp,mps_otg_en-gpio", 0);
	if(chg->mps_otg_en_gpio < 0 ){
		pr_err("chg->mps_otg_en_gpio not specified\n");
	}
	else
	{
		if( gpio_is_valid(chg->mps_otg_en_gpio) ){
			rc = gpio_request(chg->mps_otg_en_gpio, "mps_otg_en-gpio");
			if(rc){
				pr_err("unable to request gpio [%d]\n", chg->mps_otg_en_gpio);
			}
		}
		rc = mp2650_mps_otg_en_gpio_init(chg);
		pr_err("chg->mps_otg_en_gpio =%d\n",chg->mps_otg_en_gpio);
	}


	chg_err(" mp2650_gpio_init FINISH\n");

	return rc;
}

static void mp2650_set_mps_otg_en_val(int value)
{
	struct mp2650_charger *chg = s_mcharger;
	if (!chg) {
		printk(KERN_ERR "[OP_CHG][%s]: mp2650_charger not ready!\n", __func__);
		return;
	}

	if (chg->mps_otg_en_gpio <= 0) {
		chg_err("mps_otg_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(chg->pinctrl)
		|| IS_ERR_OR_NULL(chg->mps_otg_en_active)
		|| IS_ERR_OR_NULL(chg->mps_otg_en_sleep)
		|| IS_ERR_OR_NULL(chg->mps_otg_en_default)) {
		chg_err("pinctrl null, return\n");
		return;
	}

	if (value) {
		gpio_direction_output(chg->mps_otg_en_gpio, 1);
		pinctrl_select_state(chg->pinctrl,
				chg->mps_otg_en_active);
	} else {
		gpio_direction_output(chg->mps_otg_en_gpio, 0);
		pinctrl_select_state(chg->pinctrl,
				chg->mps_otg_en_default);
	}

	chg_err("<~WPC~>set value:%d, gpio_val:%d\n",
		value, gpio_get_value(chg->mps_otg_en_gpio));
}

int mp2650_get_mps_otg_en_val(void)
{
	struct mp2650_charger *chg = s_mcharger;

	if (!chg) {
		printk(KERN_ERR "[OP_CHG][%s]: mp2650_charger not ready!\n", __func__);
		return -1;
	}

	if (chg->mps_otg_en_gpio <= 0) {
		chg_err("mps_otg_en_gpio not exist, return\n");
		return -1;
	}

	if (IS_ERR_OR_NULL(chg->pinctrl)
		|| IS_ERR_OR_NULL(chg->mps_otg_en_active)
		|| IS_ERR_OR_NULL(chg->mps_otg_en_sleep)
		|| IS_ERR_OR_NULL(chg->mps_otg_en_default)) {
		chg_err("pinctrl null, return\n");
		return -1;
	}

	return gpio_get_value(chg->mps_otg_en_gpio);
}
#ifdef DEBUG_BY_FILE_OPS
char mp2650_add;

/*echo "xx" > /proc/mp2650_write_log*/
static ssize_t mp2650_data_log_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	char write_data[32] = {0};
	int critical_log = 0;
	int rc;

	if (copy_from_user(&write_data, buff, len)) {
		pr_err("mp2650_data_log_write error.\n");
		return -EFAULT;
	}

	write_data[len] = '\0';
	if(write_data[len - 1] == '\n') {
		write_data[len - 1] = '\0';
	}

	critical_log = (int)simple_strtoul(write_data, NULL, 10);
	if(critical_log > 256) {
		critical_log = 256;
	}

	pr_err("%s: input data = %s,  write_mp2650_data = 0x%02X\n", __func__, write_data, critical_log);

	rc = mp2650_config_interface(mp2650_add, critical_log, 0xff);
	if (rc) {
		 chg_err("Couldn't write 0x%02X rc = %d\n", mp2650_add, rc);
	}

	return len;
}

static const struct file_operations mp2650_write_log_proc_fops = {
	.write = mp2650_data_log_write,
};
static void init_mp2650_write_log(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("mp2650_write_log", 0664, NULL, &mp2650_write_log_proc_fops);
	if (!p) {
		pr_err("proc_create mp2650_write_log fail!\n");
	}
}

/*echo "xx" > /proc/mp2650_read_log*/
static ssize_t mp2650_data_log_read(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	char write_data[32] = {0};
	int critical_log = 0;
	int rc;
	int val_buf;

	if (copy_from_user(&write_data, buff, len)) {
		pr_err("mp2650_data_log_read error.\n");
		return -EFAULT;
	}

	write_data[len] = '\0';
	if(write_data[len - 1] == '\n') {
		write_data[len - 1] = '\0';
	}

	critical_log = (int)simple_strtoul(write_data, NULL, 10);
	if(critical_log > 256) {
		critical_log = 256;
	}

	mp2650_add = critical_log;

	pr_err("%s: input data = %s,  mp2650_addr = 0x%02X\n", __func__, write_data, mp2650_add);

	rc = mp2650_read_reg(mp2650_add, &val_buf);
	if (rc) {
		 chg_err("Couldn't read 0x%02X rc = %d\n", mp2650_add, rc);
	} else {
		 chg_err("mp2650_read 0x%02X = 0x%02X\n", mp2650_add, val_buf);
	}

	return len;
}

static const struct file_operations mp2650_read_log_proc_fops = {
	.write = mp2650_data_log_read,
};

static void init_mp2650_read_log(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("mp2650_read_log", 0664, NULL, &mp2650_read_log_proc_fops);
	if (!p) {
		pr_err("proc_create mp2650_read_log fail!\n");
	}
}
#endif /*DEBUG_BY_FILE_OPS*/

#ifdef MP2762_CP_PSY
static enum power_supply_property mp2650_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_OTG_SWITCH,
	POWER_SUPPLY_PROP_CHARGE_ENABLED,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_SUSPEND,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_CP_SWITCHER_EN,
};

static int mp2650_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct mp2650_charger *chg = power_supply_get_drvdata(psy);
	int rc = 0;
	int val_mx;

	if (!chg)
		return -ENODEV;
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		rc = mp2650_get_charge_status(&val_mx);
		if (rc)
			val->intval = 0;
		else
			val->intval = val_mx;
		break;
	case POWER_SUPPLY_PROP_OTG_SWITCH:
		val->intval = chg->otg_enabled?1:0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_ENABLED:
		val->intval = mp2650_check_charging_enable(chg)?1:0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		rc = mp2650_get_vbus_voltage(&val_mx);
		if (rc < 0)
			val->intval = 0;
		else
			val->intval = val_mx * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		rc = mp2650_get_ibus_current(&val_mx);
		if (rc < 0)
			val->intval = 0;
		else
			val->intval = val_mx; // getted ua
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = chg->sw_aicl_result_ma * 1000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		rc = mp2650_charging_current_read_fast(&val_mx);
		if (rc)
			val->intval = 0;
		else
			val->intval = val_mx * 1000;
		break;
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		val->intval = chg->input_suspend?1:0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = mp2650_float_voltage_read(&val_mx);
		if (rc)
			val->intval = 0;
		else
			val->intval = val_mx * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		val->intval = mp2650_get_vindpm_vol();
		if (val->intval <= 0)
			val->intval = chg->hw_aicl_point;
		break;
	case POWER_SUPPLY_PROP_CP_SWITCHER_EN:
		val->intval = chg->buck_switcher_on?1:0;
		break;
	case POWER_SUPPLY_PROP_OTG_OCL:
		rc = mp2650_otg_ilim_get(&val_mx);
		val->intval = val_mx * 1000;
		break;
	case POWER_SUPPLY_PROP_HWTERM:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_SWAICL:
		val->intval = chg->sw_aicl_result_ma * 1000;
		break;
	case POWER_SUPPLY_PROP_VSYS_THD:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_EN_BURST:
		val->intval = 0;
		break;
	default:
		chg_err("mp2650 power supply prop %d not supported\n", psp);
		return -EINVAL;
	}

	if (rc < 0) {
		chg_err("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int mp2650_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *val)
{
	int rc = 0;
	int val_mx;
	struct mp2650_charger *chg = power_supply_get_drvdata(psy);

	if (!chg)
		return -ENODEV;
	switch (prop) {
	case POWER_SUPPLY_PROP_OTG_SWITCH:
		if (val->intval == 1)
			rc = mp2650_otg_enable(chg, true);
		else
			rc = mp2650_otg_enable(chg, false);
		break;
	case POWER_SUPPLY_PROP_CHARGE_ENABLED:
		if (val->intval == 1)
			rc = mp2650_enable_charging(chg, true);
		else
			rc = mp2650_enable_charging(chg, false);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val_mx = val->intval / 1000; // ua to ma
		rc = mp2650_input_current_limit_without_aicl(val_mx);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val_mx = val->intval / 1000; // ua to ma
		rc = mp2650_charging_current_write_fast(val_mx);
		break;
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		if (val->intval == 1)
			rc = mp2650_enable_suspend_charger(true);
		else
			rc = mp2650_enable_suspend_charger(false);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val_mx = val->intval / 1000; // uv to mv
		rc = mp2650_float_voltage_write(val_mx);
		if (rc)
			chg_err("set float voltage fail, rc=%d", rc);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		mp2650_set_aicl_point(val->intval);
		rc = 0;
		break;
	case POWER_SUPPLY_PROP_CP_SWITCHER_EN:
		if (val->intval == 1)
			rc = mp2650_enable_buck_switch(true);
		else
			rc = mp2650_enable_buck_switch(false);
		break;
	case POWER_SUPPLY_PROP_OTG_OCL:
		val_mx = val->intval / 1000; // ua to ma
		rc = mp2650_otg_ilim_set(val_mx);
		break;
	case POWER_SUPPLY_PROP_HWTERM:
		if (val->intval == 1) {
			rc = mp2650_set_termchg_current(100);
			rc = mp2650_set_chging_term_enable(true);
		} else {
			rc = mp2650_set_chging_term_enable(false);
			rc = mp2650_set_termchg_current(WPC_TERMINATION_CURRENT);
		}
		break;
	case POWER_SUPPLY_PROP_SWAICL:
		val_mx = val->intval / 1000; // ua to ma
		//mp2650_input_current_limit_sw_aicl(val_mx);
		mp2650_input_current_limit_without_aicl(val_mx);
		rc = 0;
		break;
	case POWER_SUPPLY_PROP_VSYS_THD:
		mp2650_set_charger_vsys_threshold(val->intval);
		break;
	case POWER_SUPPLY_PROP_EN_BURST:
		val_mx = val->intval;
		mp2650_burst_mode_enable(!!val_mx);
		break;
	default:
		rc = -EINVAL;
	}

	return rc;
}

static int mp2650_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_OTG_SWITCH:
	case POWER_SUPPLY_PROP_CHARGE_ENABLED:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
	case POWER_SUPPLY_PROP_CP_SWITCHER_EN:
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct power_supply_desc mp2650_psy_desc = {
	.name = "op_charger",
	.type = POWER_SUPPLY_TYPE_PARALLEL,
	.properties = mp2650_props,
	.num_properties = ARRAY_SIZE(mp2650_props),
	.get_property = mp2650_get_prop,
	.set_property = mp2650_set_prop,
	.property_is_writeable = mp2650_prop_is_writeable,
};

static int mp2650_init_psy(struct mp2650_charger *chg)
{
	struct power_supply_config mp2650_cfg = {};
	int rc = 0;

	mp2650_cfg.drv_data = chg;
	mp2650_cfg.of_node = chg->dev->of_node;
	chg->cp_psy = devm_power_supply_register(chg->dev,
					&mp2650_psy_desc,
					&mp2650_cfg);
	if (IS_ERR(chg->cp_psy)) {
		chg_err("Couldn't register battery power supply\n");
		return PTR_ERR(chg->cp_psy);
	}

	return rc;
}
#endif

static bool mp2650_is_writeable_reg(struct device *dev, unsigned int reg)
{
	unsigned int addr;

	addr = reg;
	if ((addr >= 0x00 && addr <= 0x12)
		|| (addr == 0x31) || (addr == 0x33)
		|| (addr == 0x36))
		return true;
	return false;
}

static struct regmap_config mp2650_regmap_config = {
	.name = "mp2650",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x48,
	.writeable_reg = mp2650_is_writeable_reg,
};

static void mp2650_dump_reg_work(struct work_struct *work)
{
	int i = 0, rc = 0;
	int reg_val = 0;

	for (i = 0; i <= 0x48; i++) {
		rc = mp2650_read_reg(i, &reg_val);
		if (rc)
			chg_err("read reg 0x%2x error rc=%d.", i, rc);
		chg_err("reg 0x%02x value=0x%02x", i, reg_val);
	}
}

static int mp2650_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	struct mp2650_charger *chg_ic;

	chg_ic = devm_kzalloc(&client->dev,
		sizeof(struct mp2650_charger), GFP_KERNEL);
	if (!chg_ic) {
		chg_err(" kzalloc() failed\n");
		return -ENOMEM;
	}

	chg_debug( " call \n");
	chg_ic->client = client;
	chg_ic->dev = &client->dev;
	chg_ic->regmap = devm_regmap_init_i2c(client, &mp2650_regmap_config);
	if (!chg_ic->regmap) {
		chg_err("regmap init error.");
		return -ENODEV;
	}

	s_mcharger = chg_ic;
	atomic_set(&chg_ic->charger_suspended, 0);
	mp2650_dump_registers();
	mp2650_vbus_avoid_electric_config();
	mp2650_parse_dt();
	mp2650_hardware_init();
	mp2650_gpio_init(chg_ic);

	ret = sysfs_create_group(&chg_ic->dev->kobj, &mp2650_attribute_group);
	if (ret < 0) {
		chg_debug(" sysfs_create_group error fail\n");
		///return ret;
	}

#ifdef DEBUG_BY_FILE_OPS
	init_mp2650_write_log();
	init_mp2650_read_log();
#endif
	mp2650_init_psy(chg_ic);

	INIT_DELAYED_WORK(&chg_ic->dump_reg_work, mp2650_dump_reg_work);

	chg_debug(" success\n");

	return ret;
}

static struct i2c_driver mp2650_i2c_driver;

static int mp2650_driver_remove(struct i2c_client *client)
{

	int ret=0;

	//ret = i2c_del_driver(&mp2650_i2c_driver);
	chg_debug( "  ret = %d\n", ret);
	return 0;
}

static unsigned long suspend_tm_sec = 0;
static int get_current_time(unsigned long *now_tm_sec)
{
	struct rtc_time tm;
	struct rtc_device *rtc = NULL;
	int rc = 0;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		chg_err("%s: unable to open rtc device (%s)\n",
		__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return -EINVAL;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		chg_err("Error reading rtc device (%s) : %d\n",
		CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		chg_err("Invalid RTC time (%s): %d\n",
		CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}
	rtc_tm_to_time(&tm, now_tm_sec);

	close_time:
		rtc_class_close(rtc);
	return rc;
}

#ifdef CONFIG_PM_SLEEP
static int mp2650_pm_resume(struct device *dev)
{
	unsigned long resume_tm_sec = 0;
	unsigned long sleep_time = 0;
	int rc = 0;
	struct mp2650_charger *chg = s_mcharger;

	if(!chg) {
		return 0;
	}
	atomic_set(&chg->charger_suspended, 0);
	rc = get_current_time(&resume_tm_sec);
	if (rc || suspend_tm_sec == -1) {
		chg_err("RTC read failed\n");
		sleep_time = 0;
	} else {
		sleep_time = resume_tm_sec - suspend_tm_sec;
	}

	return 0;

}

static int mp2650_pm_suspend(struct device *dev)
{
	struct mp2650_charger *chg = s_mcharger;

	if(!chg) {
		return 0;
	}
	atomic_set(&chg->charger_suspended, 1);
	if (get_current_time(&suspend_tm_sec)) {
		chg_err("RTC read failed\n");
		suspend_tm_sec = -1;
	}
	return 0;

}

static const struct dev_pm_ops mp2650_pm_ops = {
	.resume  = mp2650_pm_resume,
	.suspend = mp2650_pm_suspend,
};
#else
static int mp2650_resume(struct i2c_client *client)
{
	unsigned long resume_tm_sec = 0;
	unsigned long sleep_time = 0;
	int rc = 0;
	struct mp2650_charger *chg = s_mcharger;

	if(!chg) {
		return 0;
	}
	atomic_set(&chg->charger_suspended, 0);
	rc = get_current_time(&resume_tm_sec);
	if (rc || suspend_tm_sec == -1) {
		chg_err("RTC read failed\n");
		sleep_time = 0;
	} else {
		sleep_time = resume_tm_sec - suspend_tm_sec;
	}

	return 0;
}

static int mp2650_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct mp2650_charger *chg = s_mcharger;

	if(!chg) {
		return 0;
	}
	atomic_set(&chg->charger_suspended, 1);
	if (get_current_time(&suspend_tm_sec)) {
		chg_err("RTC read failed\n");
		suspend_tm_sec = -1;
	}
	return 0;
}
#endif

static void mp2650_reset(struct i2c_client *client)
{
	struct mp2650_charger *chg = s_mcharger;
	mp2650_otg_enable(chg, false);
}

/**********************************************************
  *
  *   [platform_driver API]
  *
  *********************************************************/

static const struct of_device_id mp2650_match[] = {
	{ .compatible = "op,mp2650-charger"},
	{ },
};

static const struct i2c_device_id mp2650_id[] = {
	{"mp2650-charger", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, mp2650_id);

static struct i2c_driver mp2650_i2c_driver = {
	.driver = {
		.name = "mp2650-charger",
		.owner = THIS_MODULE,
		.of_match_table = mp2650_match,
#ifdef CONFIG_PM_SLEEP
		.pm = &mp2650_pm_ops,
#endif
	},
	.probe   = mp2650_driver_probe,
	.remove  = mp2650_driver_remove,
#ifndef CONFIG_PM_SLEEP
	.resume  = mp2650_resume,
	.suspend = mp2650_suspend,
#endif
	.shutdown = mp2650_reset,
	.id_table = mp2650_id,
};

module_i2c_driver(mp2650_i2c_driver);
MODULE_DESCRIPTION("Driver for mp2650 charger chg");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:mp2650-charger");
