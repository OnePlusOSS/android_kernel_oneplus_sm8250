/* Copyright (C) 2008 Rodolfo Giometti <giometti@linux.it>
 * Copyright (C) 2008 Eurotech S.p.A. <info@eurotech.it>
 * Based on a previous work by Copyright (C) 2008 Texas Instruments, Inc.
 *
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#define pr_fmt(fmt) "BQ: %s: " fmt, __func__

#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include <linux/time.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/machine.h>
#include <linux/err.h>
#include <linux/rtc.h>

#ifdef CONFIG_OF
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#endif
#include <linux/proc_fs.h>
#include <linux/workqueue.h>
#include <linux/oem/power/oem_external_fg.h>
#include "bq27541_fuelgauger.h"

/* If the system has several batteries we need a different name for each
 * of them...
 */
static DEFINE_IDR(battery_id);
static DEFINE_MUTEX(battery_mutex);
static DEFINE_MUTEX(bq28z610_alt_manufacturer_access);

int panel_flag1;
int panel_flag2;

struct update_pre_capacity_data {
	struct delayed_work work;
	struct workqueue_struct *workqueue;
	int suspend_time;
};
static struct update_pre_capacity_data update_pre_capacity_data;
static int __debug_temp_mask;
module_param_named(
	debug_temp_mask, __debug_temp_mask, int, 0600
);
static void bq27411_modify_soc_smooth_parameter(
	struct bq27541_device_info *di, bool is_powerup);

static int bq27541_i2c_txsubcmd(u8 reg, unsigned short subcmd,
		struct bq27541_device_info *di);
static int bq27541_average_current(struct bq27541_device_info *di);
static bool check_bat_present(struct bq27541_device_info *di);

static int bq28z610_get_balancing_config(struct bq27541_device_info *di);
static int bq28z610_get_2cell_voltage(void);
static int bq27541_get_battery_mvolts_2cell_max(void);
static int bq27541_get_battery_mvolts_2cell_min(void);

static int bq27541_read_i2c_block(u8 cmd, u8 length, u8 *returnData,
								struct bq27541_device_info *di);
static void bq28z610_modify_soc_smooth_parameter(struct bq27541_device_info *di);
static int bq28z610_get_time_to_full(void);
static int bq27541_get_batt_bq_soc(void);
static bool get_dash_started(void);
static int bq27541_set_allow_reading(int enable);

static int bq27541_read(u8 reg, int *rt_value, int b_single,
		struct bq27541_device_info *di)
{
	return di->bus->read(reg, rt_value, b_single, di);
}

/*
 * Return the battery temperature in tenths of degree Celsius
 * Or < 0 if something fails.
 */
#define TEMP_LT_16C		1//-2-3-4-5-6-7
#define TEMP_LT_39C		2//-1-2-3-4-5
#define TEMP_HT_39C		3//-1-2-3-4
#define BATT_TEMP_16C	160
#define BATT_TEMP_39C	390
static int batt_balancing_config = 0;

enum {
	REFRESH_TRUE = 1,
	REFRESH_FALSE
};
#if 0 //not use now, build error
static int bq28z610_get_battery_balancing_status(void)
{
	return batt_balancing_config;
}
#endif

static int bq27541_battery_temperature(struct bq27541_device_info *di)
{
	int ret;
	int temp = 0;
	int error_temp;
	static int count;
	static int pre_batt_balancing_config = 0;
	static int cb_count = 0;
	static int cb_flag = 0;
	static int temp_status = 0;
	static int delta_temp = 0;
	struct task_struct *t = NULL;

	/* Add for get right*/
	/*soc when sleep long time */
	if (atomic_read(&di->suspended) == 1)
		return di->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;

	if (di->allow_reading) {

#ifdef CONFIG_GAUGE_BQ27411
		ret = bq27541_read(di->cmd_addr.reg_temp,
				&temp, 0, di);
#else
		ret = bq27541_read(BQ27541_REG_TEMP, &temp, 0, di);
#endif
		/* Add for don't report battery*/
		/*not connect when reading error once. */
		if (ret) {
			count++;
			pr_err("error reading temperature\n");
			if (count > 1) {
				count = 0;
				/* Add for it report bad*/
				/*status when plug out battery */
				di->temp_pre =
				-400 - ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
				error_temp = -400;
				return error_temp;
			} else {
				return di->temp_pre
				+ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
			}
		}
		count = 0;
	} else {
		return di->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
	}

	if (di->bq28z610_need_balancing) {
		t = get_current();
		if (t != NULL && !strncmp(t->comm, "tbatt_pwroff", 12))
			goto cb_exit;

		batt_balancing_config = bq28z610_get_balancing_config(di);
		if (pre_batt_balancing_config == 0 && batt_balancing_config == 1 && temp_status == 0) {
			if (di->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN <= BATT_TEMP_16C) {
				temp_status = TEMP_LT_16C;
				delta_temp = 10;
			} else if (di->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN <= BATT_TEMP_39C) {
				temp_status = TEMP_LT_39C;
				delta_temp = 0;
			} else {
				temp_status = TEMP_HT_39C;
				delta_temp = 0;
			}
			printk(KERN_ERR "SJC-TEST: temp_status[%d], delta_temp[%d]\n", temp_status, delta_temp);
		}
		pre_batt_balancing_config = batt_balancing_config;

		if (batt_balancing_config == 1) {

			cb_flag = 1;
			cb_count++;

			if (cb_count >= 6 && temp_status == TEMP_LT_16C) {
				temp = temp - (60 + delta_temp);
				printk(KERN_ERR "SJC-TEST: - %d\n", 60 + delta_temp);
			} else if (cb_count >= 5 && temp_status != TEMP_HT_39C) {
				temp = temp - (50 + delta_temp);
				printk(KERN_ERR "SJC-TEST: - %d\n", 50 + delta_temp);
			} else if (cb_count >= 4) {
				temp = temp - (40 + delta_temp);
				printk(KERN_ERR "SJC-TEST: - %d\n", 40 + delta_temp);
			} else if (cb_count >= 3) {
				temp = temp - (30 + delta_temp);
				printk(KERN_ERR "SJC-TEST: - %d\n", 30 + delta_temp);
			} else if (cb_count >= 2) {
				temp = temp - (20 + delta_temp);
				printk(KERN_ERR "SJC-TEST: - %d\n", 20 + delta_temp);
			} else if (cb_count >= 1) {
				temp = temp - (10 + delta_temp);
				printk(KERN_ERR "SJC-TEST: - %d\n", 10 + delta_temp);
			}

			if (temp_status == TEMP_LT_16C) {
				if (cb_count >= 6)
					cb_count = 6;
			} else if (temp_status != TEMP_HT_39C) {
				if (cb_count >= 5)
					cb_count = 5;
			} else {
				if (cb_count >= 4)
					cb_count = 4;
			}
		} else if (cb_flag == 1 && cb_count > 0) {
			if (cb_count >= 6 && temp_status == TEMP_LT_16C) {
				temp = temp - (60 + delta_temp);
				printk(KERN_ERR "SJC-TEST 6: - %d\n", 60 + delta_temp);
			} else if (cb_count >= 5 && temp_status != TEMP_HT_39C) {
				temp = temp - (50 + delta_temp);
				printk(KERN_ERR "SJC-TEST 5: - %d\n", 50 + delta_temp);
			} else if (cb_count >= 4) {
				temp = temp - (40 + delta_temp);
				printk(KERN_ERR "SJC-TEST 4: - %d\n", 40 + delta_temp);
			} else if (cb_count >= 3) {
				temp = temp - (30 + delta_temp);
				printk(KERN_ERR "SJC-TEST 3: - %d\n", 30 + delta_temp);
			} else if (cb_count >= 2) {
				temp = temp - (20 + delta_temp);
				printk(KERN_ERR "SJC-TEST 2: - %d\n", 20 + delta_temp);
			} else if (cb_count >= 1) {
				temp = temp - (10 + delta_temp);
				printk(KERN_ERR "SJC-TEST 1: - %d\n", 10 + delta_temp);
			}
			cb_count--;
		} else {
			cb_count = 0;
			cb_flag = 0;
			temp_status = 0;
			delta_temp = 0;
		}
	}

cb_exit:	
	di->temp_pre = temp;
	return temp + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
}

/*
 * Return the battery Voltage in milivolts
 * Or < 0 if something fails.
 */
static int bq27541_battery_voltage(struct bq27541_device_info *di)
{
	int ret;
	int volt = 0;

	/* Add for get right soc when sleep long time */
	if (atomic_read(&di->suspended) == 1)
		return di->batt_vol_pre;

	if (di->allow_reading) {
#ifdef CONFIG_GAUGE_BQ27411
		ret = bq27541_read(di->cmd_addr.reg_volt,
				&volt, 0, di);
#else
		ret = bq27541_read(BQ27541_REG_VOLT, &volt, 0, di);
#endif
		if (ret) {
			pr_err("error reading voltage,ret:%d\n", ret);
			di->batt_cell_max_vol = di->max_vol_pre;
			di->batt_cell_min_vol = di->min_vol_pre;
			return di->batt_vol_pre;
		}

		if(di->batt_bq28z610) {
			bq28z610_get_2cell_voltage();
			di->max_vol_pre = di->batt_cell_max_vol;
			di->min_vol_pre = di->batt_cell_min_vol;
			//return di->batt_cell_max_vol;
			volt = di->batt_cell_max_vol;
			//bq28z610_get_2cell_balance_time();
		} else {
			di->batt_cell_max_vol = volt;
			di->batt_cell_min_vol = volt;
			di->max_vol_pre = di->batt_cell_max_vol;
			di->min_vol_pre = di->batt_cell_min_vol;
		}
		di->batt_vol_pre = volt * 1000;
	}

	return di->batt_vol_pre;
}

static int bq28z610_get_balancing_config(struct bq27541_device_info *di)
{
	u8 data[4] = {0, 0, 0, 0};
	int ret = 0;
	int balancing_config = 0;
	static int pre_balancing_config = 0;
	static int count = 0;

	if (!di) {
		return 0;
	}

	if (atomic_read(&di->suspended) == 1) {
		return 0;
	}

	if (di->batt_bq28z610) {
		if (di->allow_reading) {
			mutex_lock(&bq28z610_alt_manufacturer_access);
			bq27541_i2c_txsubcmd(BQ28Z610_OPERATION_STATUS_EN_ADDR,
				BQ28Z610_OPERATION_STATUS_CMD, di);
			usleep_range(1000, 1000);
			ret = bq27541_read_i2c_block(BQ28Z610_REG_OPERATION_STATUS_ADDR,
				BQ28Z610_OPERATION_STATUS_SIZE, data, di);
			mutex_unlock(&bq28z610_alt_manufacturer_access);
			if (ret) {
				dev_err(di->dev, "error reading operation status.\n");
				return pre_balancing_config;
			}
			balancing_config = ((data[3] << 24 | data[2] << 16 | data[1] << 8 | data[0])
				& BQ28Z610_BALANCING_CONFIG_BIT) >> 28;
			count++;
			if (balancing_config ^ pre_balancing_config || count >= 10) {
				count = 0;
				dev_info(di->dev, "operation status[0x%x], cb28[%d]\n",
					data[3] << 24 | data[2] << 16 | data[1] << 8 | data[0], balancing_config);
			}
			pre_balancing_config = balancing_config;
			return balancing_config;
		} else {
			return pre_balancing_config;
		}
	}
	return 0;
}

static void bq27541_cntl_cmd(struct bq27541_device_info *di,
		int subcmd)
{
	mutex_lock(&battery_mutex);
	bq27541_i2c_txsubcmd(BQ27541_REG_CNTL, subcmd, di);
	mutex_unlock(&battery_mutex);

}

/*
 * i2c specific code
 */
static int bq27541_i2c_txsubcmd(u8 reg, unsigned short subcmd,
		struct bq27541_device_info *di)
{
	struct i2c_msg msg;
	unsigned char data[3];
	int ret;

	if (!di->client)
		return -ENODEV;

	memset(data, 0, sizeof(data));
	data[0] = reg;
	data[1] = subcmd & 0x00FF;
	data[2] = (subcmd & 0xFF00) >> 8;

	msg.addr = di->client->addr;
	msg.flags = 0;
	msg.len = 3;
	msg.buf = data;

	ret = i2c_transfer(di->client->adapter, &msg, 1);
	if (ret < 0)
		return -EIO;

	return 0;
}

static int bq27541_chip_config(struct bq27541_device_info *di)
{
	int flags = 0, ret = 0;

	bq27541_cntl_cmd(di, BQ27541_SUBCMD_CTNL_STATUS);
	udelay(66);
	ret = bq27541_read(BQ27541_REG_CNTL, &flags, 0, di);
	if (ret < 0) {
		pr_err("error reading register %02x ret = %d\n",
				BQ27541_REG_CNTL, ret);
		return ret;
	}
	udelay(66);

	bq27541_cntl_cmd(di, BQ27541_SUBCMD_ENABLE_IT);
	udelay(66);

	if (!(flags & BQ27541_CS_DLOGEN)) {
		bq27541_cntl_cmd(di, BQ27541_SUBCMD_ENABLE_DLOG);
		udelay(66);
	}

	return 0;
}
static bool bq27541_registered;
struct bq27541_device_info *bq27541_di;
static struct i2c_client *new_client;

#define TEN_PERCENT                            10
#define SOC_SHUTDOWN_VALID_LIMITS              19
#define TEN_MINUTES                            600
#define FIVE_MINUTES                           300
#define TWO_POINT_FIVE_MINUTES                 150
#define ONE_MINUTE                             60
#define TWENTY_MINUTES                         1200
#define TWENTY_PERCENT                         20
#define TWENTY_SECS                         20


#define CAPACITY_SALTATE_COUNTER_60            38 /* 40 1min */
#define CAPACITY_SALTATE_COUNTER_95            78 /* 60 2.5min */
#define CAPACITY_SALTATE_COUNTER_FULL          200 /* 150 120 5min */
#define CAPACITY_SALTATE_COUNTER_CHARGING_TERM 30 /* 30 1min */
#define CAPACITY_SALTATE_COUNTER               4
#define CAPACITY_SALTATE_COUNTER_NOT_CHARGING  24 /* >=40sec */
#define LOW_BATTERY_PROTECT_VOLTAGE  3300000
#define CAPACITY_CALIBRATE_TIME_60_PERCENT     45 /* 45s */
#define LOW_BATTERY_CAPACITY_THRESHOLD         20

#define SHORT_TIME_STANDBY_SOC_CHECK_COUNT     15
#define LOW_BATTERY_LEVEL_THRESHOLD            8
#define BATTERY_SOC_UPDATE_MS 12000
#define LOW_BAT_SOC_UPDATE_MS 6000

#define RESUME_SCHDULE_SOC_UPDATE_WORK_MS 60000

static int get_current_time(unsigned long *now_tm_sec)
{
	struct rtc_time tm;
	struct rtc_device *rtc;
	int rc;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
				__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return -EINVAL;
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
	rtc_tm_to_time(&tm, now_tm_sec);

close_time:
	rtc_class_close(rtc);
	return rc;
}

bool get_4p45_battery_support(void)
{
	if (bq27541_di->bat_4p45v)
		return true;
	else
		return false;
}

int get_prop_pre_shutdown_soc(void)
{
	int soc_load;

	soc_load = load_soc();
	if (soc_load == -1)
		return 50;
	else
		return soc_load;
}

static int fg_soc_calibrate(struct  bq27541_device_info *di, int soc)
{
	union power_supply_propval ret = {0,};
	unsigned int soc_calib;
	unsigned long soc_current_time, time_last;
	static bool first_enter;
	static int charging_status, charging_status_pre;
	bool chg_done;
	int temp_region, vbat_mv, ibat_ma, soc_load, soc_temp, counter_temp = 0;

	if (false == first_enter) {
		di->batt_psy = power_supply_get_by_name("battery");
		if (di->batt_psy) {
			first_enter = true;
			soc_load = load_soc();
			pr_info("soc=%d, soc_load=%d\n", soc, soc_load);
			if (soc_load < 0) {
				/* get last soc error */
				di->soc_pre = soc;
			} else if (soc_load >= 0 && soc_load < 100) {
				if (abs(soc_load - soc) > SOC_SHUTDOWN_VALID_LIMITS)
					di->soc_pre = soc;
				else if (soc_load > soc)
					di->soc_pre = soc_load - 1;
				else
					di->soc_pre = soc_load;
			} else if (soc_load == 100
					&& abs(soc_load - soc) > TEN_PERCENT) {
				/* decrease soc when gap between soc_load and */
				/* real_soc is over 10%                       */
				di->soc_pre = soc_load - 1;
			} else {
				di->soc_pre = soc_load;
			}

			if (!di->batt_psy) {
				pr_err(
				"batt_psy is absent, soc_pre=%d\n",
				di->soc_pre);
				return di->soc_pre;
			}
			/* store the soc when boot first time */
			get_current_time(&di->soc_pre_time);
			clean_backup_soc_ex();
		} else {
			return soc;
		}
	}
	soc_temp = di->soc_pre;

	if (!di->batt_psy) {
		soc_calib = soc;
		goto out;
	}

	ret.intval = get_charging_status();
	di->batt_vol_pre = bq27541_battery_voltage(di);
	chg_done = get_oem_charge_done_status();
	temp_region = fuelgauge_battery_temp_region_get();
	if ((temp_region == BATT_TEMP_LITTLE_COOL
			|| temp_region == BATT_TEMP_COOL
			|| temp_region == BATT_TEMP_NORMAL
			|| temp_region == BATT_TEMP_PRE_NORMAL)
			&& chg_done) {
		ret.intval = POWER_SUPPLY_STATUS_FULL;
	}

	if (ret.intval == POWER_SUPPLY_STATUS_CHARGING
			|| ret.intval == POWER_SUPPLY_STATUS_FULL
			|| bq27541_di->fastchg_started
			|| bq27541_di->wlchg_started)
		charging_status = 1;
	else
		charging_status = 0;

	if (charging_status ^ charging_status_pre) {
		if (charging_status_pre) {
			get_current_time(&soc_current_time);
			di->soc_store_time =
			soc_current_time - di->soc_pre_time;
		}

		get_current_time(&di->soc_pre_time);
		if (!charging_status_pre && di->soc_store_time)
			di->soc_pre_time -= di->soc_store_time;
		charging_status_pre = charging_status;
		di->saltate_counter = 0;
	}

	get_current_time(&soc_current_time);
	time_last = soc_current_time - di->soc_pre_time;
	if (charging_status) { /* is charging */
		if (ret.intval == POWER_SUPPLY_STATUS_FULL) {
			soc_calib = di->soc_pre;
			if (di->soc_pre < 100
					&& (temp_region == BATT_TEMP_LITTLE_COOL
					|| temp_region == BATT_TEMP_NORMAL
					|| temp_region == BATT_TEMP_PRE_NORMAL
					|| temp_region == BATT_TEMP_COOL)) {
				if (time_last > TWENTY_SECS)
					soc_calib = di->soc_pre + 1;
			}
		} else {
			if (soc - di->soc_pre > 0) {
				di->saltate_counter++;
				if ((bq27541_di->fastchg_started
					&& time_last < 10)
				|| (!bq27541_di->fastchg_started
				&& time_last < 20))
					return di->soc_pre;
				di->saltate_counter = 0;
				soc_calib = di->soc_pre + 1;
			} else if (soc < (di->soc_pre - 1)) {
				di->saltate_counter++;
				if (di->soc_pre == 100) {
					counter_temp =
					CAPACITY_SALTATE_COUNTER_FULL;
					/* t>=5min */
				} else if (di->soc_pre > 95) {
					counter_temp =
					CAPACITY_SALTATE_COUNTER_95;
					/* t>=2.5min */
				} else if (di->soc_pre > 60) {
					counter_temp =
					CAPACITY_SALTATE_COUNTER_60;
					/* t>=1min */
				} else {
					if (time_last >
					CAPACITY_CALIBRATE_TIME_60_PERCENT
						&& (soc - di->soc_pre) < 0)
						counter_temp = 0;
					else
						/* t>=40sec */
						counter_temp =
					CAPACITY_SALTATE_COUNTER_NOT_CHARGING;
				}

				/* avoid dead battery shutdown */
				if (di->batt_vol_pre <=
					LOW_BATTERY_PROTECT_VOLTAGE
					&& di->batt_vol_pre > 2500 * 1000
					&& di->soc_pre
					<= LOW_BATTERY_CAPACITY_THRESHOLD) {
					/* check again */
					vbat_mv =
					bq27541_battery_voltage(di);
					if (vbat_mv <=
						LOW_BATTERY_PROTECT_VOLTAGE
						&& vbat_mv > 2500 * 1000) {
						/* about 9s */
						counter_temp =
						CAPACITY_SALTATE_COUNTER - 1;
					}
				}

				ibat_ma = bq27541_average_current(di);
				if (ibat_ma <= 0 && di->soc_pre == 100) {
					di->saltate_counter = 0;
					return di->soc_pre;
				}
				/* don't allow soc down*/
				/*if chg current > -200mA */
				if (di->saltate_counter < counter_temp
						|| ibat_ma < -200 * 1000)
					return di->soc_pre;
				di->saltate_counter = 0;

				soc_calib = di->soc_pre - 1;
			} else if ((soc == 0 && soc < di->soc_pre)
					&& di->soc_pre <= 2) {
				di->saltate_counter++;
				if (time_last >
					CAPACITY_CALIBRATE_TIME_60_PERCENT
						&& (soc - di->soc_pre) < 0)
					counter_temp = 0;
				else
					/* t>=40sec */
					counter_temp =
					CAPACITY_SALTATE_COUNTER_NOT_CHARGING;

				if (di->saltate_counter < counter_temp)
					return di->soc_pre;
				di->saltate_counter = 0;
				soc_calib = di->soc_pre - 1;
			} else {
				soc_calib = di->soc_pre;
			}
		}
	} else { /* not charging */
		if ((soc < di->soc_pre)
			|| (di->batt_vol_pre <= LOW_BATTERY_PROTECT_VOLTAGE
			&& di->batt_vol_pre > 2500 * 1000)) {
			if (di->soc_pre == 100) {
				counter_temp = FIVE_MINUTES;
			} else if (di->soc_pre >= 95) {
				counter_temp = TWO_POINT_FIVE_MINUTES;
			} else if (di->soc_pre >= 60) {
				counter_temp = ONE_MINUTE;
			} else {
				if (time_last >=
					CAPACITY_CALIBRATE_TIME_60_PERCENT
						&& (soc - di->soc_pre) < 0)
					counter_temp = 0;
				else
					/* t>=40sec */
				counter_temp =
				CAPACITY_SALTATE_COUNTER_NOT_CHARGING
				+ 20;
			}
			/* avoid dead battery shutdown */
			if (di->batt_vol_pre <=
				LOW_BATTERY_PROTECT_VOLTAGE
				&& di->batt_vol_pre > 2500 * 1000
				&& di->soc_pre <=
				LOW_BATTERY_CAPACITY_THRESHOLD) {
				/* check again */
				vbat_mv = bq27541_battery_voltage(di);
				if (vbat_mv <= LOW_BATTERY_PROTECT_VOLTAGE
				&& vbat_mv > 2500 * 1000 && time_last > 9)
				counter_temp = 0;
			}

			if (time_last < counter_temp)
				return di->soc_pre;
		}

		if (soc < di->soc_pre)
			soc_calib = di->soc_pre - 1;
		else if (di->batt_vol_pre <= LOW_BATTERY_PROTECT_VOLTAGE
				&& di->batt_vol_pre > 2500 * 1000
				&& di->soc_pre > 0 && time_last > 9)
			soc_calib = di->soc_pre - 1;
		else
			soc_calib = di->soc_pre;
	}

out:
	if (soc_calib > 100)
		soc_calib = 100;
	if (soc_calib < 0)
		soc_calib = 0;
	if (soc_calib == 0) {
		if ((di->batt_vol_pre/1000) > 3300)
			soc_calib = 1;
	}
	di->soc_pre = soc_calib;
	if (soc_temp != soc_calib) {
		get_current_time(&di->soc_pre_time);
		/* store when soc changed */
		power_supply_changed(di->batt_psy);
		pr_info("soc:%d, soc_calib:%d, VOLT:%d, current:%d\n",
		soc, soc_calib, bq27541_battery_voltage(di) / 1000,
		bq27541_average_current(di) / 1000);
	}

	return soc_calib;
}


static int bq27541_battery_soc(
struct bq27541_device_info *di, int suspend_time_ms)
{
	int ret;
	int soc = 0;
	int soc_delt = 0;
	static int soc_pre;
	bool fg_soc_changed = false;

	/* Add for get right soc when sleep long time */
	if (atomic_read(&di->suspended) == 1) {
		dev_warn(di->dev,
		"di->suspended di->soc_pre=%d\n", di->soc_pre);
		return di->soc_pre;
	}

	if (di->allow_reading) {
#ifdef CONFIG_GAUGE_BQ27411
		ret = bq27541_read(di->cmd_addr.reg_soc,
				&soc, 0, di);
#else
		ret = bq27541_read(BQ27541_REG_SOC, &soc, 0, di);
#endif
		if (ret) {
			pr_err("error reading soc=%d, ret:%d\n", soc, ret);
			goto read_soc_err;
		}
		if (soc_pre != soc)
			pr_err("bq27541_battery_soc = %d\n", soc);

		soc_pre = soc;
	} else {
		if (di->soc_pre)
			return di->soc_pre;
		else
			return 0;
	}
	/* Add for get right soc when sleep long time */
	if (suspend_time_ms && di->lcd_is_off) {
		if (soc < di->soc_pre) {
			soc_delt  =  di->soc_pre - soc;
			fg_soc_changed = (soc < TWENTY_PERCENT
					|| soc_delt > di->lcd_off_delt_soc
					|| suspend_time_ms > TEN_MINUTES);
			pr_info("suspend_time_ms=%d,soc_delt=%d,di->lcd_off_delt_soc=%d\n",
			suspend_time_ms, soc_delt, di->lcd_off_delt_soc);
			if (fg_soc_changed) {
				if (suspend_time_ms/TEN_MINUTES) {
				di->soc_pre -=
				(suspend_time_ms / TEN_MINUTES < soc_delt
				? suspend_time_ms / TEN_MINUTES : soc_delt);
				} else if (di->soc_pre - soc > TEN_PERCENT)
					di->soc_pre -= TEN_PERCENT/2;
				else {
					di->soc_pre -= 1;
				}
				/* store when soc changed */
				get_current_time(&di->soc_pre_time);
				power_supply_changed(di->batt_psy);
				pr_err("system resume,soc:%d, soc_calib:%d,VOLT:%d,current:%d\n",
				soc, di->soc_pre,
				bq27541_battery_voltage(di) / 1000,
				bq27541_average_current(di) / 1000);
			}
		}
		goto read_soc_err;
	}
	if (di->disable_calib_soc)
		return soc;
	soc = fg_soc_calibrate(di, soc);
	return soc;

read_soc_err:
	if (di->soc_pre) {
		dev_warn(di->dev,
		"read_soc_exit ,di->soc_pre=%d\n", di->soc_pre);
		return di->soc_pre;
	} else
		return 0;
}

static int bq27541_average_current(struct bq27541_device_info *di)
{
	int ret;
	int curr = 0;

	/* Add for get right soc when sleep long time */
	if (atomic_read(&di->suspended) == 1)
		return -di->current_pre;

	if (di->allow_reading) {
#ifdef CONFIG_GAUGE_BQ27411
		ret = bq27541_read(di->cmd_addr.reg_ai,
				&curr, 0, di);
#else
		ret = bq27541_read(BQ27541_REG_AI, &curr, 0, di);
#endif
		if (ret) {
			pr_err("error reading current.\n");
			return ret;
		}
	} else {
		return -di->current_pre;
	}
	/* negative current */
	if (curr & 0x8000)
		curr = -((~(curr-1)) & 0xFFFF);
	di->current_pre = 1000 * curr;
	return -curr * 1000;
}

static int bq27541_remaining_capacity(struct bq27541_device_info *di)
{
	int ret;
	int cap = 0;

	if (atomic_read(&di->suspended) == 1)
		return di->remain_pre;
	if (di->allow_reading || panel_flag1) {
#ifdef CONFIG_GAUGE_BQ27411
		ret = bq27541_read(di->cmd_addr.reg_rm,
				&cap, 0, di);
#else
		ret = bq27541_read(BQ27541_REG_RM, &cap, 0, di);
#endif
		if (ret) {
			pr_err("error reading capacity.\n");
			return ret;
		}
		if (panel_flag1)
			panel_flag1 = 0;
	} else {
		return di->remain_pre;
	}

	di->remain_pre = cap;
	return cap;
}

static int get_batt_full_available_capacity(struct bq27541_device_info *di)
{
        int ret;
        int cap = 0;

        if (atomic_read(&di->suspended) == 1)
                return di->full_available_capacity_pre;
        if (di->allow_reading || panel_flag1) {
#ifdef CONFIG_GAUGE_BQ27411
                ret = bq27541_read(BQ27411_REG_FAC,
                                &cap, 0, di);
#else
                ret = bq27541_read(BQ27541_REG_RM, &cap, 0, di);
#endif
                if (ret) {
                        pr_err("error reading full_available_capacity.\n");
                        return ret;
                }
                if (panel_flag1)
                        panel_flag1 = 0;
        } else {
                return di->full_available_capacity_pre;
        }

        di->full_available_capacity_pre = cap;
        return cap;
}


static int get_batt_full_available_capacity_filtered(struct bq27541_device_info *di)
{
        int ret;
        int cap = 0;

        if (atomic_read(&di->suspended) == 1)
                return di->full_available_capacity_filtered_pre;
        if (di->allow_reading || panel_flag1) {
#ifdef CONFIG_GAUGE_BQ27411
                ret = bq27541_read(BQ27411_REG_FCCF,
                                &cap, 0, di);
#else
                ret = bq27541_read(BQ27541_REG_RM, &cap, 0, di);
#endif
                if (ret) {
                        pr_err("error reading full_available_capacity_filtered_pre.\n");
                        return ret;
                }
                if (panel_flag1)
                        panel_flag1 = 0;
        } else {
                return di->full_available_capacity_filtered_pre;
        }

        di->full_available_capacity_filtered_pre = cap;
        return cap;
}

static int bq27541_full_chg_capacity(struct bq27541_device_info *di)
{
	int ret;
	int cap = 0;

	/* Add for get right soc when sleep long time */
	if (atomic_read(&di->suspended) == 1)
		return di->cap_pre;

	if (di->allow_reading || panel_flag2) {
#ifdef CONFIG_GAUGE_BQ27411
		if (di->batt_bq28z610)
			ret = bq27541_read(BQ28Z610_REG_CHARGE_FULL_CAPACITY,
					&cap, 0, di);
		else
			ret = bq27541_read(di->cmd_addr.reg_fcc,
					&cap, 0, di);
#else
		ret = bq27541_read(BQ27541_REG_FCC, &cap, 0, di);
#endif
		if (ret) {
			pr_err("error reading full chg capacity.\n");
			return ret;
		}
		if (panel_flag2)
			panel_flag2 = 0;
	} else {
		return di->cap_pre;
	}

	di->cap_pre = cap;
	return cap;
}


static int bq27541_batt_health(struct bq27541_device_info *di)
{
	int ret;
	int health = 0;

	if (di->allow_reading) {
		if (di->batt_bq28z610)
			ret = bq27541_read(BQ28Z610_REG_BATTERY_HEALTH,
					&health, 0, di);
		else
			ret = bq27541_read(di->cmd_addr.reg_helth,
					&health, 0, di);
		if (ret) {
			pr_err("error reading health\n");
			return ret;
		}
		if (di->device_type == DEVICE_BQ27411)
			di->health_pre = (health & 0xFF);
		else
			di->health_pre = health;
	}

	return di->health_pre;
}

static int bq27541_get_battery_mvolts(void)
{
	return bq27541_battery_voltage(bq27541_di);
}

static int bq27541_get_batt_remaining_capacity(void)
{
	return bq27541_remaining_capacity(bq27541_di);
}

static int bq27541_get_batt_full_available_capacity(void)
{
        return get_batt_full_available_capacity(bq27541_di);
}

static int bq27541_get_batt_full_available_capacity_filtered(void)
{
        return get_batt_full_available_capacity_filtered(bq27541_di);
}

static int bq27541_get_batt_full_chg_capacity(void)
{
	return bq27541_full_chg_capacity(bq27541_di);
}
static int bq27541_get_batt_health(void)
{
	return bq27541_batt_health(bq27541_di);
}
static int bq27541_get_batt_bq_soc(void)
{
	int soc;

	if (!get_dash_started()) {
		if (!bq27541_di->allow_reading)
			bq27541_set_allow_reading(true);
	}
	bq27541_di->disable_calib_soc = true;
	soc = bq27541_battery_soc(bq27541_di, 0);
	bq27541_di->disable_calib_soc = false;
	if (!get_dash_started()) {
		if (!bq27541_di->allow_reading)
			bq27541_set_allow_reading(false);
	}
	return soc;
}
static bool battery_is_match(void)
{
	if (!bq27541_di->check_match)
		return true;

	if ((bq27541_di->fw_ver == FW_VERSION_4P45V_01
		|| bq27541_di->fw_ver == FW_VERSION_4P45V_02)
		&& bq27541_di->bat_4p45v)
		return true;
	else if (bq27541_di->fw_ver != FW_VERSION_4P45V_01
		&& bq27541_di->fw_ver != FW_VERSION_4P45V_02
		&& !bq27541_di->bat_4p45v)
		return true;
	else
		return false;
}

#define SHUTDOWN_TBAT 680
static int bq27541_get_battery_temperature(void)
{
	int ret;
	static unsigned long pre_time;
	unsigned long current_time, time_last;

	if (__debug_temp_mask)
			return __debug_temp_mask;
	if (bq27541_di->bat_4p45v) {
		if (!battery_is_match())
			return SHUTDOWN_TBAT+10;
	}
	ret = bq27541_battery_temperature(bq27541_di);
	if (ret >= SHUTDOWN_TBAT) {
		bq27541_di->t_count++;
		if (bq27541_di->t_count == 1)
			get_current_time(&pre_time);
		get_current_time(&current_time);
		time_last = current_time - pre_time;
		if (time_last < 8)
			return SHUTDOWN_TBAT - 1;
		else {
			pr_info("Tbat =%d T_tol=%d\n",
				ret, (int)(current_time - pre_time));
		}
	}
	bq27541_di->t_count = 0;
	return ret;
}
static bool bq27541_is_battery_present(void)
{
	return check_bat_present(bq27541_di);
}

static bool bq27541_is_battery_temp_within_range(void)
{
	return true;
}

static bool bq27541_is_battery_id_valid(void)
{
	return true;
}

#ifdef CONFIG_GAUGE_BQ27411
static int bq27541_get_device_type(void)
{
	if (bq27541_di)
		return bq27541_di->device_type;

	return 0;
}
#endif

static int bq27541_get_battery_soc(void)
{
	return bq27541_battery_soc(bq27541_di, 0);
}

static int bq27541_get_average_current(void)
{
	return bq27541_average_current(bq27541_di);
}

static int bq27541_set_allow_reading(int enable)
{
	if (bq27541_di)
		bq27541_di->allow_reading = enable;

	return 0;
}

static int bq27541_set_lcd_off_status(int off)
{
	int soc;

	pr_info("off=%d\n", off);
	if (bq27541_di && bq27541_registered) {
		if (off) {
			soc = bq27541_get_batt_bq_soc();
			bq27541_di->lcd_off_delt_soc =
					bq27541_di->soc_pre - soc;
			pr_info("lcd_off_delt_soc:%d,soc=%d,soc_pre=%d\n",
			bq27541_di->lcd_off_delt_soc, soc,
					bq27541_di->soc_pre);
			get_current_time(&bq27541_di->lcd_off_time);
					bq27541_di->lcd_is_off = true;
		} else {
			bq27541_di->lcd_is_off = false;
			bq27541_di->lcd_off_delt_soc = 0;
			schedule_delayed_work(&bq27541_di->battery_soc_work,
				msecs_to_jiffies(RESUME_SCHDULE_SOC_UPDATE_WORK_MS));
		}
	}
	return 0;
}

static int bq27541_get_fastchg_started_status(bool fastchg_started_status)
{
	if (bq27541_di)
		bq27541_di->fastchg_started = fastchg_started_status;

	return 0;
}

static int bq27541_set_wlchg_started_status(bool wlchg_started_status)
{
	if (bq27541_di)
		bq27541_di->wlchg_started = wlchg_started_status;

	return 0;
}

static struct external_battery_gauge bq27541_batt_gauge = {
	.get_battery_mvolts     = bq27541_get_battery_mvolts,
	.get_battery_mvolts_2cell_max = bq27541_get_battery_mvolts_2cell_max,
	.get_battery_mvolts_2cell_min = bq27541_get_battery_mvolts_2cell_min,
	.get_battery_temperature    = bq27541_get_battery_temperature,
	.is_battery_present     = bq27541_is_battery_present,
	.is_battery_temp_within_range   = bq27541_is_battery_temp_within_range,
	.is_battery_id_valid        = bq27541_is_battery_id_valid,
	.get_batt_remaining_capacity
		= bq27541_get_batt_remaining_capacity,
	.get_batt_full_available_capacity
                = bq27541_get_batt_full_available_capacity,
	.get_batt_full_available_capacity_filtered
                = bq27541_get_batt_full_available_capacity_filtered,
	.get_batt_full_chg_capacity
		= bq27541_get_batt_full_chg_capacity,
	.get_batt_health        = bq27541_get_batt_health,
	.get_batt_bq_soc        = bq27541_get_batt_bq_soc,
#ifdef CONFIG_GAUGE_BQ27411
	.get_device_type            = bq27541_get_device_type,
#endif
	.get_battery_soc            = bq27541_get_battery_soc,
	.get_average_current        = bq27541_get_average_current,
	.set_allow_reading          = bq27541_set_allow_reading,
	.set_lcd_off_status         = bq27541_set_lcd_off_status,
	.fast_chg_started_status    = bq27541_get_fastchg_started_status,
	.wlchg_started_status       = bq27541_set_wlchg_started_status,
	.get_time_to_full           = bq28z610_get_time_to_full,
};


static int is_usb_pluged(void)
{
	static struct power_supply *psy;
	union power_supply_propval ret = {0,};
	int usb_present, rc;

	if (!psy) {
		psy = power_supply_get_by_name("usb");
		if (!psy) {
			pr_err("failed to get ps usb\n");
			return -EINVAL;
		}
	}

	rc = power_supply_get_property(psy, POWER_SUPPLY_PROP_PRESENT, &ret);
	if (rc)
		return -EINVAL;

	if (ret.intval < 0)
		return -EINVAL;

	usb_present = ret.intval;
	return usb_present;
}

static bool get_dash_started(void)
{
	if (bq27541_di && bq27541_di->fastchg_started)
		return bq27541_di->fastchg_started;
	else
		return false;
}
#define TEMP_UPDATE_COUNT 5
#define TEMP_UPDATE_THRESHOLD  450


static int bq27541_temperature_thrshold_update(int temp)
{
	int ret;

	if (!bq27541_di->batt_psy)
		return 0;
	if (temp >= TEMP_UPDATE_THRESHOLD) {
		bq27541_di->temp_thr_update_count++;
		if (bq27541_di->temp_thr_update_count > TEMP_UPDATE_COUNT) {
			bq27541_di->temp_thr_update_count = 0;
			power_supply_changed(bq27541_di->batt_psy);
		}
	} else {
		bq27541_di->temp_thr_update_count = 0;
	}

	return ret;
}

static void update_battery_soc_work(struct work_struct *work)
{
	int schedule_time, vbat, temp, switch_flag = 0;
	static int pre_plugin_status = 0;
	static bool pre_dash_started = 0;

	pr_info("plugin:%d,dash_start:%d:smooth:%d\n",
				is_usb_pluged(), get_dash_started(),bq27541_di->set_smoothing);
	switch_flag = REFRESH_TRUE;
	if (pre_plugin_status != is_usb_pluged()
		|| pre_dash_started != get_dash_started())
		pr_info("usb_plugin:%d,dash_started:%d:set_smooth:%d\n",
				is_usb_pluged(), get_dash_started(),bq27541_di->set_smoothing);
	pre_plugin_status = is_usb_pluged();
	pre_dash_started = get_dash_started();
	if (is_usb_pluged() || get_dash_started()) {
		schedule_delayed_work(
				&bq27541_di->battery_soc_work,
				msecs_to_jiffies(BATTERY_SOC_UPDATE_MS));
		if (get_dash_started())
			return;
		if (bq27541_di->set_smoothing)
			return;
		if (!bq27541_di->allow_reading)
			bq27541_set_allow_reading(true);
		return;
	}
	bq27541_set_allow_reading(true);
	bq28z610_get_time_to_full();
	vbat = bq27541_get_battery_mvolts()/1000;
	bq27541_get_average_current();
	temp = bq27541_get_battery_temperature();
	bq27541_get_battery_soc();
	bq27541_get_batt_remaining_capacity();
	bq27541_get_batt_full_chg_capacity();
	bq27541_set_allow_reading(false);
	bq27541_temperature_thrshold_update(temp);
	if (!bq27541_di->already_modify_smooth)
		schedule_delayed_work(
		&bq27541_di->modify_soc_smooth_parameter, 1000);
	if (bq27541_di->lcd_is_off)
		schedule_time = 2 * RESUME_SCHDULE_SOC_UPDATE_WORK_MS;
	else
		schedule_time =
			vbat < 3600 ? LOW_BAT_SOC_UPDATE_MS : BATTERY_SOC_UPDATE_MS;
	schedule_delayed_work(&bq27541_di->battery_soc_work,
			msecs_to_jiffies(schedule_time));
}

bool get_extern_fg_regist_done(void)
{
	return bq27541_registered;
}
bool get_extern_bq_present(void)
{
	if (bq27541_di)
		return bq27541_di->bq_present;
	return 0;
}

#ifdef CONFIG_HOUSTON
void bq27541_force_update_current(bool enable)
{
	if (likely(bq27541_registered)) {
		if (atomic_read(&bq27541_di->suspended) != 1) {
			pr_info("bq27541_force_update_current\n");
			cancel_delayed_work_sync(&bq27541_di->battery_soc_work);
			schedule_delayed_work(&bq27541_di->battery_soc_work,
				msecs_to_jiffies(1));
		}
	}
}
#endif

#ifdef CONFIG_GAUGE_BQ27411
static void gauge_set_cmd_addr(int device_type)
{
	if (device_type == DEVICE_BQ27541) {
		bq27541_di->cmd_addr.reg_temp = BQ27541_REG_TEMP;
		bq27541_di->cmd_addr.reg_volt = BQ27541_REG_VOLT;
		bq27541_di->cmd_addr.reg_rm = BQ27541_REG_RM;
		bq27541_di->cmd_addr.reg_ai = BQ27541_REG_AI;
		bq27541_di->cmd_addr.reg_soc = BQ27541_REG_SOC;
		bq27541_di->cmd_addr.reg_helth = BQ27541_REG_NIC;
		bq27541_di->cmd_addr.reg_fcc = BQ27541_REG_FCC;
	} else {
		bq27541_di->cmd_addr.reg_temp = BQ27411_REG_TEMP;
		bq27541_di->cmd_addr.reg_volt = BQ27411_REG_VOLT;
		bq27541_di->cmd_addr.reg_rm = BQ27411_REG_RM;
		bq27541_di->cmd_addr.reg_ai = BQ27411_REG_AI;
		bq27541_di->cmd_addr.reg_soc = BQ27411_REG_SOC;
		bq27541_di->cmd_addr.reg_helth = BQ27411_REG_HEALTH;
		bq27541_di->cmd_addr.reg_fcc = BQ27411_REG_FCC;
	}
}
#endif

static void bq_modify_soc_smooth_parameter(struct work_struct *work)
{
	struct bq27541_device_info *di;

	di = container_of(work, struct bq27541_device_info,
			modify_soc_smooth_parameter.work);
	if (get_dash_started())
		return;
	if (di->already_modify_smooth)
		return;
	bq27541_set_allow_reading(false);
	di->set_smoothing = true;
	if (di->batt_bq28z610)
		bq28z610_modify_soc_smooth_parameter(di);
	else
		bq27411_modify_soc_smooth_parameter(di, true);
	di->set_smoothing = false;
	bq27541_set_allow_reading(true);
}
static ssize_t battery_exist_read(struct file *p_file,
	char __user *puser_buf, size_t count, loff_t *p_offset)
{
	return 0;
}

static ssize_t battery_exist_write(struct file *p_file,
	const char __user *puser_buf,
	size_t count, loff_t *p_offset)
{
	return 0;
}

static const struct file_operations battery_exist_operations = {
	.read = battery_exist_read,
	.write = battery_exist_write,
};

static void init_battery_exist_node(void)
{
	if (!proc_create("battery_exist", 0644, NULL,
			 &battery_exist_operations)){
		pr_err("%s : Failed to register proc interface\n", __func__);
	}
}


static bool check_bat_present(struct bq27541_device_info *di)
{
	int flags = 0, ret = 0;

	ret = bq27541_read(BQ27541_SUBCMD_CHEM_ID, &flags, 0, di);
	if (ret < 0) {
		pr_err("read bq27541  fail\n");
		mdelay(100);
		ret = bq27541_read(BQ27541_SUBCMD_CHEM_ID, &flags, 0, di);
		if (ret < 0) {
			pr_err("read bq27541  fail again\n");
			di->bq_present = false;
			return false;
		}
	}
	di->bq_present = true;
	return true;
}

static void bq27541_hw_config(struct work_struct *work)
{
	int ret = 0, flags = 0, type = 0, fw_ver = 0;
	struct bq27541_device_info *di;

	di = container_of(work, struct bq27541_device_info,
			hw_config.work);
	ret = bq27541_chip_config(di);
	if (ret) {
		pr_err("Failed to config Bq27541\n");
		/* Add for retry when config fail */
		di->retry_count--;
		if (di->retry_count > 0)
			schedule_delayed_work(&di->hw_config, HZ);
		else
			bq27541_registered = true;

		return;
	}
	external_battery_gauge_register(&bq27541_batt_gauge);
	bq27541_information_register(&bq27541_batt_gauge);
	exfg_information_register(&bq27541_batt_gauge);
	bq27541_cntl_cmd(di, BQ27541_SUBCMD_CTNL_STATUS);
	udelay(66);
	bq27541_read(BQ27541_REG_CNTL, &flags, 0, di);
	bq27541_cntl_cmd(di, BQ27541_SUBCMD_DEVCIE_TYPE);
	udelay(66);
	bq27541_read(BQ27541_REG_CNTL, &type, 0, di);
	bq27541_cntl_cmd(di, BQ27541_SUBCMD_FW_VER);
	udelay(66);
	bq27541_read(BQ27541_REG_CNTL, &fw_ver, 0, di);
	di->fw_ver = fw_ver;

#ifdef CONFIG_GAUGE_BQ27411
	if (type == DEVICE_TYPE_BQ27411) {
		di->device_type = DEVICE_BQ27411;
		pr_info("DEVICE_BQ27411\n");
	} else {
		di->device_type = DEVICE_BQ27541;
		pr_info("DEVICE_BQ27541\n");
	}
	gauge_set_cmd_addr(di->device_type);
	if (type == DEVICE_TYPE_BQ28Z610) {
		di->cmd_addr.reg_ai = Bq28Z610_REG_TI;
	}
	di->allow_reading = true;
#endif

	bq27541_registered = true;
	pr_info("DEVICE_TYPE is 0x%02X, FIRMWARE_VERSION is 0x%02X\n",
			type, fw_ver);
	pr_info("Complete bq27541 configuration 0x%02X\n", flags);
	schedule_delayed_work(
		&di->modify_soc_smooth_parameter,
		SET_BQ_PARAM_DELAY_MS);
}

static int bq27541_read_i2c(u8 reg, int *rt_value, int b_single,
		struct bq27541_device_info *di)
{
	struct i2c_client *client = di->client;
	struct i2c_msg msg[2];
	unsigned char data[2];
	int err;

	if (!client->adapter)
		return -ENODEV;
	mutex_lock(&battery_mutex);

	/* Write register */
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = data;
	data[0] = reg;
	/* Read data */
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	if (!b_single)
		msg[1].len = 2;
	else
		msg[1].len = 1;
	msg[1].buf = data;
	err = i2c_transfer(client->adapter, msg, 2);
	if (err >= 0) {
		if (!b_single)
			*rt_value = get_unaligned_le16(data);
		else
			*rt_value = data[0];
		mutex_unlock(&battery_mutex);
		return 0;
	}
	mutex_unlock(&battery_mutex);
	return err;
}

static int bq27541_read_i2c_block(u8 cmd, u8 length, u8 *returnData,
						struct bq27541_device_info *di)
{
	if(!di->client) {
		pr_err(" di->client NULL,return\n");
		return 0;
	}
	if(cmd == BQ27541_BQ27411_CMD_INVALID)
		return 0;
	mutex_lock(&battery_mutex);
	i2c_smbus_read_i2c_block_data(di->client, cmd, length, returnData);
	mutex_unlock(&battery_mutex);
	//pr_err(" cmd = 0x%x, returnData = 0x%x\r\n",cmd,*returnData)	;
	return 0;
}

static int bq27541_write_i2c_block(u8 cmd, u8 length, u8 *writeData,
						struct bq27541_device_info *di)
{
	if (!di->client) {
		pr_err(" di->client NULL, return\n");
		return 0;
	}
	if (cmd == BQ27541_BQ27411_CMD_INVALID) {
		return 0;
	}
	mutex_lock(&battery_mutex);
	i2c_smbus_write_i2c_block_data(di->client, cmd, length, writeData);
	mutex_unlock(&battery_mutex);
	return 0;
}


#ifdef CONFIG_BQ27541_TEST_ENABLE
static int reg;
static int subcmd;
static ssize_t bq27541_read_stdcmd(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	int temp = 0;
	struct platform_device *client;
	struct bq27541_device_info *di;

	client = to_platform_device(dev);
	di = platform_get_drvdata(client);

	if (reg <= BQ27541_REG_ICR && reg > 0x00) {
		ret = bq27541_read(reg, &temp, 0, di);
		if (ret)
			ret = snprintf(buf, PAGE_SIZE, "Read Error!\n");
		else
			ret = snprintf(buf, PAGE_SIZE, "0x%02x\n", temp);
	} else
		ret = snprintf(buf, PAGE_SIZE, "Register Error!\n");

	return ret;
}

static ssize_t bq27541_write_stdcmd(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	int cmd;
	int rc;

	rc = kstrtou32(buf, 0, &cmd);
	if (rc != 1)
		pr_err("%s,scanf error\n");
	reg = cmd;
	return ret;
}

static ssize_t bq27541_read_subcmd(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	int temp = 0;
	struct platform_device *client;
	struct bq27541_device_info *di;

	client = to_platform_device(dev);
	di = platform_get_drvdata(client);

	if (subcmd == BQ27541_SUBCMD_DEVCIE_TYPE ||
			subcmd == BQ27541_SUBCMD_FW_VER ||
			subcmd == BQ27541_SUBCMD_HW_VER ||
			subcmd == BQ27541_SUBCMD_CHEM_ID) {

		bq27541_cntl_cmd(di, subcmd); /* Retrieve Chip status */
		udelay(66);
		ret = bq27541_read(BQ27541_REG_CNTL, &temp, 0, di);

		if (ret)
			ret = snprintf(buf, PAGE_SIZE, "Read Error!\n");
		else
			ret = snprintf(buf, PAGE_SIZE, "0x%02x\n", temp);
	} else
		ret = snprintf(buf, PAGE_SIZE, "Register Error!\n");

	return ret;
}

static ssize_t bq27541_write_subcmd(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	int cmd, rc;

	rc = kstrtou32(buf, 0, &cmd);
	if (rc != 1)
		pr_err("%s,scanf error\n");
	subcmd = cmd;
	return ret;
}

static DEVICE_ATTR(std_cmd, 0644, bq27541_read_stdcmd,
		bq27541_write_stdcmd);
static DEVICE_ATTR(sub_cmd, 0644, bq27541_read_subcmd,
		bq27541_write_subcmd);
static struct attribute *fs_attrs[] = {
	&dev_attr_std_cmd.attr,
	&dev_attr_sub_cmd.attr,
	NULL,
};
static struct attribute_group fs_attr_group = {
	.attrs = fs_attrs,
};

static struct platform_device this_device = {
	.name			= "bq27541-test",
	.id			= -1,
	.dev.platform_data	= NULL,
};
#endif

static void update_pre_capacity_func(struct work_struct *w)
{
	pr_info("enter\n");
	bq27541_set_allow_reading(true);
	bq27541_get_battery_temperature();
	bq27541_battery_soc(bq27541_di, update_pre_capacity_data.suspend_time);
	bq27541_get_batt_remaining_capacity();
	bq27541_get_batt_full_chg_capacity();
	bq27541_set_allow_reading(false);
	__pm_relax(bq27541_di->update_soc_wake_lock);
	pr_info("exit\n");
}

#define MAX_RETRY_COUNT	5
#define DEFAULT_INVALID_SOC_PRE  -22

static void bq27541_parse_dt(struct bq27541_device_info *di)
{
	struct device_node *node = di->dev->of_node;

	di->modify_soc_smooth = of_property_read_bool(node,
				"qcom,modify-soc-smooth");
	pr_info("di->modify_soc_smooth=%d\n", di->modify_soc_smooth);
	di->bat_4p45v = of_property_read_bool(node,
				"op,bat-4p45v");
	di->check_match = of_property_read_bool(node,
				"op,check-match");
	pr_info("BQ 4p45V=%d, check-match=%d\n", di->bat_4p45v, di->check_match);
#ifdef CONFIG_GAUGE_BQ27411
	di->batt_bq28z610 = of_property_read_bool(node, "op,batt_bq28z610");
	di->bq28z610_need_balancing = of_property_read_bool(node,
				"op,bq28z610_need_balancing");
#endif
}
static int sealed(void)
{
	/*return control_cmd_read(di, CONTROL_STATUS) & (1 << 13);*/
	int value = 0;

	bq27541_cntl_cmd(bq27541_di, CONTROL_STATUS);
	/*bq27541_cntl_cmd(di,CONTROL_STATUS);*/
	usleep_range(10000, 10001);
	bq27541_read_i2c(CONTROL_STATUS, &value, 0, bq27541_di);

	pr_debug(" REG_CNTL: 0x%x\n", value);

	if (bq27541_di->device_type == DEVICE_BQ27541)
		return value & BIT(14);
	else if (bq27541_di->device_type == DEVICE_BQ27411)
		return value & BIT(13);
	else
		return 1;
}

static int seal(void)
{
	int i = 0;

	if (sealed()) {
		pr_err("bq27541/27411 sealed,return\n");
		return 1;
	}
	bq27541_cntl_cmd(bq27541_di, SEAL_SUBCMD);
	usleep_range(10000, 10001);
	for (i = 0; i < SEAL_POLLING_RETRY_LIMIT; i++) {
		if (sealed())
			return 1;
		usleep_range(10000, 10001);
	}
	return 0;
}


static int unseal(u32 key)
{
	int i = 0;

	if (!sealed())
		goto out;

re_unseal:
	if (bq27541_di->device_type == DEVICE_BQ27411) {
		usleep_range(10000, 10001);
	/*bq27541_write(CONTROL_CMD, key & 0xFFFF, false, di);*/
		bq27541_cntl_cmd(bq27541_di, 0x8000);
		usleep_range(10000, 10001);
		bq27541_cntl_cmd(bq27541_di, 0x8000);
		usleep_range(10000, 10001);
	}
	bq27541_cntl_cmd(bq27541_di, 0xffff);
	usleep_range(10000, 10001);
	bq27541_cntl_cmd(bq27541_di, 0xffff);
	usleep_range(10000, 10001);

	while (i < SEAL_POLLING_RETRY_LIMIT) {
		i++;
		if (!sealed())
			break;
		usleep_range(10000, 10001);
		goto re_unseal;
	}

out:
	pr_info("bq27541 : i=%d,bq27541_di->device_type=%d\n",
		i, bq27541_di->device_type);

	if (i == SEAL_POLLING_RETRY_LIMIT) {
		pr_err("bq27541 failed\n");
		return 0;
	} else {
		return 1;
	}
}

static int bq27541_read_i2c_onebyte(u8 cmd, u8 *returnData)
{
	if (!new_client) {
		pr_err(" new_client NULL,return\n");
		return 0;
	}
	if (cmd == BQ27541_BQ27411_CMD_INVALID)
		return 0;

	mutex_lock(&battery_mutex);
	*returnData = i2c_smbus_read_byte_data(new_client, cmd);

	mutex_unlock(&battery_mutex);
	/*pr_err(" cmd = 0x%x, returnData = 0x%x\r\n",cmd,*returnData)  ;*/
	if (*returnData < 0)
		return 1;
	else
		return 0;
}

static int bq27541_i2c_txsubcmd_onebyte(u8 cmd, u8 writeData)
{
	if (!new_client) {
		pr_err(" new_client NULL,return\n");
		return 0;
	}
	if (cmd == BQ27541_BQ27411_CMD_INVALID)
		return 0;

	mutex_lock(&battery_mutex);
	i2c_smbus_write_byte_data(new_client, cmd, writeData);
	mutex_unlock(&battery_mutex);
	return 0;
}


static int bq27411_write_block_data_cmd(struct bq27541_device_info *di,
				int block_id, u8 reg_addr, u8 new_value)
{
	int rc = 0;
	u8 old_value = 0, old_csum = 0, new_csum = 0;
	/*u8 new_csum_test = 0, csum_temp = 0;*/

	usleep_range(1000, 1001);
	bq27541_i2c_txsubcmd(BQ27411_DATA_CLASS_ACCESS, block_id, di);
	usleep_range(10000, 10001);
	rc = bq27541_read_i2c_onebyte(reg_addr, &old_value);
	if (rc) {
		pr_err("%s read reg_addr = 0x%x fail\n", __func__, reg_addr);
		return 1;
	}
	if (old_value == new_value)
		return 0;
	usleep_range(1000, 1001);
	rc = bq27541_read_i2c_onebyte(BQ27411_CHECKSUM_ADDR, &old_csum);
	if (rc) {
		pr_err("%s read checksum fail\n", __func__);
		return 1;
	}
	usleep_range(1000, 1001);
	bq27541_i2c_txsubcmd_onebyte(reg_addr, new_value);
	usleep_range(1000, 1001);
	new_csum = (old_value + old_csum - new_value) & 0xff;
	usleep_range(1000, 1001);
	bq27541_i2c_txsubcmd_onebyte(BQ27411_CHECKSUM_ADDR, new_csum);
	pr_err("bq27411 write blk_id = 0x%x, addr = 0x%x, old_val = 0x%x, new_val = 0x%x, old_csum = 0x%x, new_csum = 0x%x\n",
		block_id, reg_addr, old_value, new_value, old_csum, new_csum);
	return 0;
}

static int bq27411_read_block_data_cmd(struct bq27541_device_info *di,
				int block_id, u8 reg_addr)
{
	u8 value = 0;

	usleep_range(1000, 1001);
	bq27541_i2c_txsubcmd(BQ27411_DATA_CLASS_ACCESS, block_id, di);
	usleep_range(10000, 10001);
	bq27541_read_i2c_onebyte(reg_addr, &value);
	return value;
}

static int bq27411_enable_config_mode(
	struct bq27541_device_info *di, bool enable)
{
	int config_mode = 0, i = 0, rc = 0;

	if (enable) {		/*enter config mode*/
		usleep_range(1000, 1001);
		bq27541_cntl_cmd(bq27541_di, BQ27411_SUBCMD_SET_CFG);
		usleep_range(1000, 1001);
		for (i = 0; i < BQ27411_CONFIG_MODE_POLLING_LIMIT; i++) {
			i++;
			rc = bq27541_read_i2c(BQ27411_SUBCMD_CONFIG_MODE,
				&config_mode, 0, di);
			if (rc < 0) {
				pr_err("%s i2c read error\n", __func__);
				return 1;
			}
			if (config_mode & BIT(4))
				break;
			msleep(50);
		}
	} else {		/* exit config mode*/
		usleep_range(1000, 1001);
		bq27541_cntl_cmd(bq27541_di, BQ27411_SUBCMD_EXIT_CFG);
		usleep_range(1000, 1001);
		for (i = 0; i < BQ27411_CONFIG_MODE_POLLING_LIMIT; i++) {
			i++;
			rc = bq27541_read_i2c(BQ27411_SUBCMD_CONFIG_MODE,
				&config_mode, 0, di);
			if (rc < 0) {
				pr_err("%s i2c read error\n", __func__);
				return 1;
			}
			if ((config_mode & BIT(4)) == 0)
				break;
			msleep(50);
		}
	}
	if (i == BQ27411_CONFIG_MODE_POLLING_LIMIT) {
		pr_err("%s fail config_mode = 0x%x, enable = %d\n",
			__func__, config_mode, enable);
		return 1;
	}
		pr_err("%s success i = %d, config_mode = 0x%x, enable = %d\n",
			__func__, i, config_mode, enable);
		return 0;
}

static bool bq27411_check_soc_smooth_parameter(
	struct bq27541_device_info *di, bool is_powerup)
{
	int value_read = 0;
	u8 dead_band_val = 0, op_cfgb_val = 0, dodat_val = 0, rc = 0;

	return true;	/*not check because it costs 5.5 seconds*/

	msleep(4000);
	if (sealed()) {
		if (!unseal(BQ27411_UNSEAL_KEY))
			return false;
		msleep(50);
	}

	if (is_powerup) {
		dead_band_val = BQ27411_CC_DEAD_BAND_POWERUP_VALUE;
		op_cfgb_val = BQ27411_OPCONFIGB_POWERUP_VALUE;
		dodat_val = BQ27411_DODATEOC_POWERUP_VALUE;
	} else {	/*shutdown*/
		dead_band_val = BQ27411_CC_DEAD_BAND_SHUTDOWN_VALUE;
		op_cfgb_val = BQ27411_OPCONFIGB_SHUTDOWN_VALUE;
		dodat_val = BQ27411_DODATEOC_SHUTDOWN_VALUE;
	}
	rc = bq27411_enable_config_mode(di, true);
	if (rc) {
		pr_err("%s enable config mode fail\n", __func__);
		return false;
	}
	/*enable block data control*/
	rc = bq27541_i2c_txsubcmd_onebyte(BQ27411_BLOCK_DATA_CONTROL, 0x00);
	if (rc) {
		pr_err("%s enable block data control fail\n", __func__);
		goto check_error;
	}
	usleep_range(5000, 5001);

	/*check cc-dead-band*/
	value_read = bq27411_read_block_data_cmd(di,
	BQ27411_CC_DEAD_BAND_ID, BQ27411_CC_DEAD_BAND_ADDR);
	if (value_read != dead_band_val) {
		pr_err("%s cc_dead_band error, value_read = 0x%x\n",
			__func__, value_read);
		goto check_error;
	}

	/*check opconfigB*/
	value_read = bq27411_read_block_data_cmd(di,
	BQ27411_OPCONFIGB_ID,
	BQ27411_OPCONFIGB_ADDR);
	if (value_read != op_cfgb_val) {
		pr_err("%s opconfigb error, value_read = 0x%x\n",
			__func__, value_read);
		goto check_error;
	}

	/*check dodateoc*/
	value_read = bq27411_read_block_data_cmd(di,
				BQ27411_DODATEOC_ID, BQ27411_DODATEOC_ADDR);
	if (value_read != dodat_val) {
		pr_err("%s dodateoc error, value_read = 0x%x\n",
			__func__, value_read);
		goto check_error;
	}
	bq27411_enable_config_mode(di, false);
	return true;

check_error:
	bq27411_enable_config_mode(di, false);
	return false;
}

static int bq27411_write_soc_smooth_parameter(
	struct bq27541_device_info *di, bool is_powerup)
{
	int rc = 0;
	u8 dead_band_val = 0, op_cfgb_val = 0, dodat_val = 0;

	if (is_powerup) {
		dead_band_val = BQ27411_CC_DEAD_BAND_POWERUP_VALUE;
		op_cfgb_val = BQ27411_OPCONFIGB_POWERUP_VALUE;
		dodat_val = BQ27411_DODATEOC_POWERUP_VALUE;
	} else {	/*shutdown*/
		dead_band_val = BQ27411_CC_DEAD_BAND_SHUTDOWN_VALUE;
		op_cfgb_val = BQ27411_OPCONFIGB_SHUTDOWN_VALUE;
		dodat_val = BQ27411_DODATEOC_SHUTDOWN_VALUE;
	}

	/*enter config mode*/
	rc = bq27411_enable_config_mode(di, true);
	if (rc) {
		pr_err("%s enable config mode fail\n", __func__);
		return 1;
	}
	/*enable block data control*/
	bq27541_i2c_txsubcmd_onebyte(BQ27411_BLOCK_DATA_CONTROL, 0x00);

	usleep_range(5000, 5001);
	/*step1: update cc-dead-band*/
	rc = bq27411_write_block_data_cmd(di, BQ27411_CC_DEAD_BAND_ID,
			BQ27411_CC_DEAD_BAND_ADDR, dead_band_val);
	if (rc) {
		pr_err("%s cc_dead_band fail\n", __func__);
		goto exit_config_mode;
	}
	/*step2: update opconfigB*/
	rc = bq27411_write_block_data_cmd(di, BQ27411_OPCONFIGB_ID,
			BQ27411_OPCONFIGB_ADDR, op_cfgb_val);
	if (rc) {
		pr_err("%s opconfigB fail\n", __func__);
		goto exit_config_mode;
	}
	/*step3: update dodateoc*/
	rc = bq27411_write_block_data_cmd(di, BQ27411_DODATEOC_ID,
			BQ27411_DODATEOC_ADDR, dodat_val);
	if (rc) {
		pr_err("%s dodateoc fail\n", __func__);
		goto exit_config_mode;
	}
	bq27411_enable_config_mode(di, false);
	return 0;

exit_config_mode:
	bq27411_enable_config_mode(di, false);
	return 1;
}

static void bq27411_modify_soc_smooth_parameter(
	struct bq27541_device_info *di, bool is_powerup)
{
	int rc = 0;
	bool check_result = false, tried_again = false;

	if (di->modify_soc_smooth == false
		|| di->device_type == DEVICE_BQ27541) {
		return;
	}

	pr_info("%s begin\n", __func__);
	if (sealed()) {
		if (!unseal(BQ27411_UNSEAL_KEY))
			return;
		msleep(50);
	}
write_parameter:
	rc = bq27411_write_soc_smooth_parameter(di, is_powerup);
	if (rc && tried_again == false) {
		tried_again = true;
		goto write_parameter;
	} else {
		check_result =
		bq27411_check_soc_smooth_parameter(di, is_powerup);
		if (check_result == false && tried_again == false) {
			tried_again = true;
			goto write_parameter;
		}
	}

	usleep_range(1000, 1001);
	if (sealed() == 0) {
		usleep_range(1000, 1001);
		seal();
	}
	di->already_modify_smooth = true;
	pr_info("%s end\n", __func__);
}

static int bq8z610_sealed(void)
{
	int value = 0;
	u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = {0,0,0,0};
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, BQ28Z610_SEAL_STATUS, bq27541_di);
	usleep_range(10000, 10000);
	bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL, bq27541_di);
	pr_err("%s bq8z610_sealed CNTL1_VAL[0] = %x,CNTL1_VAL[1] = %x,\
		CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
		__func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);
		value = (CNTL1_VAL[3] & BQ28Z610_SEAL_BIT);
	if(value == BQ28Z610_SEAL_VALUE) {
		pr_err("bq8z610 sealed, value = %x return 1\n",value);
		return 1;
	} else {
		pr_err("bq8z610 sealed, value = %x return 0\n",value);
		return 0;
	}
}

static int bq8z610_seal(void)
{
	int i = 0;

	if (bq8z610_sealed()) {
		pr_err("bq8z610 sealed, return\n");
		return 1;
	}
	bq27541_i2c_txsubcmd(0, BQ28Z610_SEAL_SUBCMD, bq27541_di);
	//usleep_range(10000, 10000);
	msleep(1000);
	for (i = 0;i < BQ28Z610_SEAL_POLLING_RETRY_LIMIT;i++) {
		if (bq8z610_sealed()) {
			return 1;
		}
		//bq27541_i2c_txsubcmd(0, BQ28Z610_SEAL_SUBCMD, bq27541_di);
		usleep_range(10000, 10000);
	}
	return 0;
}

static int bq8z610_unseal(void)
{
	int i = 0;

	if (!bq8z610_sealed()) {
		goto out;
	}
	bq27541_i2c_txsubcmd(0, BQ28Z610_UNSEAL_SUBCMD1, bq27541_di);
	usleep_range(10000, 10000);
	//msleep(100);
	bq27541_i2c_txsubcmd(0, BQ28Z610_UNSEAL_SUBCMD2, bq27541_di);
	//usleep_range(10000, 10000);
	msleep(1000);
	while (i < BQ28Z610_SEAL_POLLING_RETRY_LIMIT) {
		i++;
		if (!bq8z610_sealed()) {
			break;
		}
		usleep_range(10000, 10000);
	}

out:
	pr_info("bq8z610 : i=%d\n", i);
	if (i == SEAL_POLLING_RETRY_LIMIT) {
		pr_err("bq8z610 unseal failed\n");
		return 0;
	} else {
		return 1;
	}
}

static int bq28z610_write_flash_busy_wait_i2c_err(struct bq27541_device_info *di)
{
	//int rc = 0;
	u8 I2C_VAL[BQ28Z610_REG_I2C_SIZE] = {0,0,0};
	u8 I2C_write1[BQ28Z610_REG_I2C_SIZE] = {0x03,0x46,0xA0};

	bq27541_write_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_I2C_SIZE, &I2C_write1[0], di);
	msleep(100);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x0516, di);
	msleep(100);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x4603, di);//physical address is 0x4603
	msleep(100);
	bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_I2C_SIZE, I2C_VAL, di);
	pr_err("%s I2C Configuration I2C_VAL[0] = %x,I2C_VAL[1] = %x,I2C_VAL[2] = %x\n",
		__func__,I2C_VAL[0],I2C_VAL[1],I2C_VAL[2]);
	if(((I2C_VAL[2] << 16) |(I2C_VAL[1] << 8) | I2C_VAL[0]) != 0xA04603) {
		pr_err("%s To change I2C Configuration 0x20 -> 0xA0. ERR.\n", __func__);
		return -1;
	} else {
		pr_err("%s To change I2C Configuration 0x20 -> 0xA0. OK\n", __func__);
	}
	return 0;
}

#if 0 //not use now, build error.
int bq28z610_write_soc_smooth_parameter(struct bq27541_device_info *di)
{
	//int rc = 0;
	u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = {0,0,0,0};
	u8 CNTL1_write1[BQ28Z610_REG_CNTL1_SIZE] = {0xF4,0x46,0xdC,0x00};
	//u8 CNTL1_write2[BQ28Z610_REG_CNTL1_SIZE] = {0x08,0x47,0x78,0x00};//120ma
	u8 CNTL1_write2[BQ28Z610_REG_CNTL1_SIZE] = {0x08,0x47,0x96,0x00};//150ma
	u8 CNTL1_write3[BQ28Z610_REG_CNTL1_SIZE] = {0x0C,0x47,0x28,0x00};
	bq27541_write_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, &CNTL1_write1[0], di);
	msleep(100);
	//bq8z610_cntl2_cmd(0x06E9);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x06E9, di);
	//usleep_range(10000, 5000);
	msleep(100);
	//bq8z610_cntl1_cmd(0x46F4);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x46F4, di);
	//usleep_range(5000, 5000);
	msleep(100);
	bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL, di);
		pr_err("%s Charge Term Taper Current CNTL1_VAL[0] = %x,\
			CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
			__func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);
		if((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x46F4)
				|| (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x00DC)) {
			pr_err("%s Charge Term Taper Current 150mA (=0x0096) -> 220mA (=0x00DC). ERR.\n", __func__);
			return -1;
		} else {
			pr_err("%s Charge Term Taper Current  (=0x0096) -> 220mA (=0x00DC). OK\n", __func__);
		}
	bq27541_write_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, &CNTL1_write2[0], di);
	msleep(100);
	//bq8z610_cntl2_cmd(0x06E9);
	//bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x0638, di);//120ma
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x061A, di);//150ma
	//usleep_range(5000, 5000);
	msleep(100);
	//bq8z610_cntl1_cmd(0x46F4);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x4708, di);
	//usleep_range(5000, 5000);
	msleep(100);
	bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL, di);
	pr_err("%s Dsg Current Threshold CNTL1_VAL[0] = %x,\
		CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
		__func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);
		//if((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x4708) || (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0078))//120ma
	if((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x4708)
			|| (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0096)) {
		pr_err("%s Dsg Current Threshold 40mA (0x0028) -> 150mA (0x0078) ERR.\n", __func__);
		return -1;
	} else {
		pr_err("%s Dsg Current Threshold 40mA (0x0028) -> 150mA (0x0078) OK\n", __func__);
	}
	bq27541_write_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, &CNTL1_write3[0], di);
	msleep(100);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x0684, di);
	msleep(100);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x470C, di);
	msleep(100);
	bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL, di);
	pr_err("%s Quit Current CNTL1_VAL[0] = %x,\
		CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
		__func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);
	if((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x470C)
			|| (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0028)) {
		pr_err("%s Quit Current 20mA (0x0014) -> 40mA (0x0028). ERR.\n", __func__);
		return -1;
	} else {
		pr_err("%s Quit Current 20mA (0x0014) -> 40mA (0x0028). OK\n", __func__);
	}
	return 0;
}
#endif

static int bq28z610_write_iterm_Taper_parameter(struct bq27541_device_info *di) {
	u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = {0,0,0,0};
	u8 CNTL1_write1[BQ28Z610_REG_CNTL1_SIZE] = {0xF4,0x46,0x96,0x00};
	bq27541_write_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, &CNTL1_write1[0], di);
	msleep(100);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x062F, di);
	msleep(100);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x46F4, di);
	msleep(100);
	bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL, di);
	pr_err("%s Charge Term Taper Current CNTL1_VAL[0] = %x,\
		CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
		__func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);
	if((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x46F4)
			|| (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0096)) {
		pr_err("%s Charge Term Taper Current 220mA (=0x00DC) -> 150mA (=0x0096). ERR.\n", __func__);
		return -1;
	} else {
		pr_err("%s Charge Term Taper Current 220mA (=0x00DC) -> 150mA (=0x0096). OK\n", __func__);
	}
	return 0;
}

static void bq28z610_modify_soc_smooth_parameter(struct bq27541_device_info *di)
{
	int rc = 0;
	bool tried_again = false;

	if (di->modify_soc_smooth == false
		|| di->device_type != DEVICE_BQ28Z610) {
		return;
	}

	pr_err("%s begin\n", __func__);
	if (bq8z610_sealed()) {
		if (!bq8z610_unseal()) {
			return;
		} else {
			msleep(50);
		}
	}

write_parameter:
	//rc = bq28z610_write_soc_smooth_parameter(chip);
	rc = bq28z610_write_iterm_Taper_parameter(di);
	rc = bq28z610_write_flash_busy_wait_i2c_err(di);
	if (rc && tried_again == false) {
		tried_again = true;
		goto write_parameter;
	}
	usleep_range(1000, 1000);
	if (bq8z610_sealed() == 0) {
		usleep_range(1000, 1000);
		bq8z610_seal();
	}
	di->already_modify_smooth = true;
	pr_err("%s end\n", __func__);
}

#if 0 // not use now, build error.
static int bq8z610_check_gauge_enable(void)
{
	/*	  return control_cmd_read(di, CONTROL_STATUS) & (1 << 13);*/
	int value = 0;
	u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = {0,0,0,0};
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, BQ28Z610_REG_GAUGE_EN, bq27541_di);
	//usleep_range(10000, 10000);
	msleep(1000);
	bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL, bq27541_di);
	pr_err("%s	CNTL1_VAL[0] = %x,CNTL1_VAL[1] = %x,\
		CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
		__func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);
	value = (CNTL1_VAL[2] & BQ28Z610_GAUGE_EN_BIT);
	if(value == BQ28Z610_GAUGE_EN_BIT) {
		pr_err("bq8z610 gauge_enable, value = %x return 1\n",value);
		return 1;
	} else {
		pr_err("bq8z610 gauge_enable, value = %x return 0\n",value);
		return 0;
	}
}

static int bq28z610_write_dod0_parameter(struct bq27541_device_info *di)
{
	//bq8z610_cntl1_cmd(0x46F4);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x0021, di);
	//usleep_range(5000, 5000);
	msleep(1000);
	//bq8z610_cntl1_cmd(0x00DC);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x0021, di);
	//usleep_range(5000, 5000);
	msleep(2000);
	if(bq8z610_check_gauge_enable() == false) {
		//bq8z610_cntl1_cmd(0x00DC);
		bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x0021, di);
		//usleep_range(5000, 5000);
		msleep(300);
	}
	return 0;
}

static void bq28z610_modify_dod0_parameter(struct bq27541_device_info *di)
{
	int rc = 0;

	pr_err("%s begin\n", __func__);
	if (bq8z610_sealed()) {
		if (!bq8z610_unseal()) {
			return;
		} else {
			msleep(50);
		}
	}
	rc = bq28z610_write_dod0_parameter(di);
	usleep_range(1000, 1000);
	if (bq8z610_sealed() == 0) {
		usleep_range(1000, 1000);
		bq8z610_seal();
	}
	pr_err("%s end\n", __func__);
}
#endif

static int bq28z610_get_2cell_voltage(void)
{
	u8 cell_vol[BQ28Z610_MAC_CELL_VOLTAGE_SIZE] = {0, 0, 0, 0};
	struct bq27541_device_info *di = bq27541_di;

	if (!di) {
		return 0;
	}

	mutex_lock(&bq28z610_alt_manufacturer_access);
	bq27541_i2c_txsubcmd(BQ28Z610_MAC_CELL_VOLTAGE_EN_ADDR,
		BQ28Z610_MAC_CELL_VOLTAGE_CMD, di);
	usleep_range(1000, 1000);
	bq27541_read_i2c_block(BQ28Z610_MAC_CELL_VOLTAGE_ADDR,
		BQ28Z610_MAC_CELL_VOLTAGE_SIZE, cell_vol, di);
	mutex_unlock(&bq28z610_alt_manufacturer_access);
	di->batt_cell_1_vol = (cell_vol[1] << 8) | cell_vol[0];
	di->batt_cell_2_vol = (cell_vol[3] << 8) | cell_vol[2];
	if (di->batt_cell_1_vol < di->batt_cell_2_vol) {
		di->batt_cell_max_vol = di->batt_cell_2_vol;
		di->batt_cell_min_vol = di->batt_cell_1_vol;
	} else {
		di->batt_cell_max_vol = di->batt_cell_1_vol;
		di->batt_cell_min_vol = di->batt_cell_2_vol;
	}
	/*chg_err("batt_cell_1_vol = %dmV, batt_cell_2_vol = %dmV, batt_cell_max_vol = %dmV\n",
		di->batt_cell_1_vol,
		di->batt_cell_2_vol,
		di->batt_cell_max_vol);*/

	return 0;
}

static int bq27541_get_battery_mvolts_2cell_max(void)
{
	if(!bq27541_di) {
		return 0;
	}
	return bq27541_di->batt_cell_max_vol;
}

static int bq27541_get_battery_mvolts_2cell_min(void)
{
	if(!bq27541_di) {
		return 0;
	}
	return bq27541_di->batt_cell_min_vol;
}

static int bq28z610_get_time_to_full(void)
{
	int ret = -1;
	int time_to_full = 0;

	if(!bq27541_di)
		return -ENODEV;

	if (atomic_read(&bq27541_di->suspended) == 1)
		return bq27541_di->time_to_full;

	if (!bq27541_di->batt_bq28z610)
		return -ENODATA;

	if (bq27541_di->allow_reading) {
		ret = bq27541_read(BQ28Z610_REG_TIME_TO_FULL, &time_to_full, 0, bq27541_di);
		if (ret < 0) {
			pr_err("error reading time to full,ret:%d\n", ret);
			return ret;
		}
		if (time_to_full == 65535) {
			if (bq27541_di->time_to_full == 0)
				return -ENODATA;
			return bq27541_di->time_to_full;
		}
		bq27541_di->time_to_full = time_to_full * 60;
		return time_to_full * 60;
	}

	return bq27541_di->time_to_full;
}

static int bq27541_battery_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	char *name;
	struct bq27541_device_info *di;
	struct bq27541_access_methods *bus;
	int num;
	int retval = 0;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	update_pre_capacity_data.workqueue =
		create_workqueue("update_pre_capacity");
	INIT_DELAYED_WORK(&(update_pre_capacity_data.work),
		update_pre_capacity_func);

	mutex_lock(&battery_mutex);
	num = idr_alloc(&battery_id, client, 0, 0, GFP_KERNEL);
	mutex_unlock(&battery_mutex);
	if (retval < 0)
		return retval;

	name = kasprintf(GFP_KERNEL, "%s-%d", id->name, num);
	if (!name) {
		pr_err("failed to allocate device name\n");
		retval = -ENOMEM;
		goto batt_failed_1;
	}

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di) {
		retval = -ENOMEM;
		goto batt_failed_2;
	}
	di->id = num;

	bus = kzalloc(sizeof(*bus), GFP_KERNEL);
	if (!bus) {
		retval = -ENOMEM;
		goto batt_failed_3;
	}

	i2c_set_clientdata(client, di);
	di->dev = &client->dev;
	bus->read = &bq27541_read_i2c;
	di->bus = bus;
	di->client = client;

	new_client = client;

	di->update_soc_wake_lock = wakeup_source_register(&client->dev, "bq_delt_soc_wake_lock");
	di->soc_pre = DEFAULT_INVALID_SOC_PRE;
	di->temp_pre = 0;
#ifndef CONFIG_GAUGE_BQ27411
	di->allow_reading = true;
#endif
	/* Add for retry when config fail */
	di->retry_count = MAX_RETRY_COUNT;
	atomic_set(&di->suspended, 0);

#ifdef CONFIG_BQ27541_TEST_ENABLE
	platform_set_drvdata(&this_device, di);
	retval = platform_device_register(&this_device);
	if (!retval) {
		retval = sysfs_create_group(&this_device.dev.kobj,
				&fs_attr_group);
		if (retval)
			goto batt_failed_4;
	} else
		goto batt_failed_4;
#endif

	if (retval) {
		pr_err("failed to setup bq27541\n");
		goto batt_failed_4;
	}

	if (retval) {
		pr_err("failed to powerup bq27541\n");
		goto batt_failed_4;
	}
	bq27541_di = di;
	bq27541_parse_dt(di);
	di->t_count = 0;
	di->lcd_is_off = false;
	INIT_DELAYED_WORK(&di->hw_config, bq27541_hw_config);
	INIT_DELAYED_WORK(&di->modify_soc_smooth_parameter,
		bq_modify_soc_smooth_parameter);
	INIT_DELAYED_WORK(&di->battery_soc_work, update_battery_soc_work);
	schedule_delayed_work(&di->hw_config, BQ27541_INIT_DELAY);
	schedule_delayed_work(&di->battery_soc_work, BATTERY_SOC_UPDATE_MS);
	retval = check_bat_present(di);
	if( retval ) {
		init_battery_exist_node();
		pr_info("probe success battery exist \n");
	}
	else {
		pr_info("probe success battery not exist \n");
	}
	return 0;

batt_failed_4:
	kfree(bus);
batt_failed_3:
	kfree(di);
batt_failed_2:
	kfree(name);
batt_failed_1:
	mutex_lock(&battery_mutex);
	idr_remove(&battery_id, num);
	mutex_unlock(&battery_mutex);

	return retval;
}

static int bq27541_battery_remove(struct i2c_client *client)
{
	struct bq27541_device_info *di = i2c_get_clientdata(client);

	external_battery_gauge_unregister(&bq27541_batt_gauge);
	bq27541_information_unregister(&bq27541_batt_gauge);
	bq27541_cntl_cmd(di, BQ27541_SUBCMD_DISABLE_DLOG);
	udelay(66);
	bq27541_cntl_cmd(di, BQ27541_SUBCMD_DISABLE_IT);
	cancel_delayed_work_sync(&di->hw_config);
	cancel_delayed_work_sync(&di->modify_soc_smooth_parameter);
	kfree(di->bus);

	mutex_lock(&battery_mutex);
	idr_remove(&battery_id, di->id);
	mutex_unlock(&battery_mutex);

	kfree(di);
	return 0;
}


static int bq27541_battery_suspend(struct device *dev)
{
	int ret = 0;
	struct bq27541_device_info *di = dev_get_drvdata(dev);

	pr_info("bq27541_battery_suspend\n");
	//cancel_delayed_work_sync(&di->battery_soc_work);
	atomic_set(&di->suspended, 1);
	ret = get_current_time(&di->rtc_suspend_time);
	if (ret) {
		pr_err("Failed to read RTC time\n");
		return 0;
	}
	return 0;
}


/*1 minute*/

#define RESUME_TIME  60
static int bq27541_battery_resume(struct device *dev)
{
	int ret = 0;
	int suspend_time;
	struct bq27541_device_info *di =  dev_get_drvdata(dev);

	atomic_set(&di->suspended, 0);
	ret = get_current_time(&di->rtc_resume_time);
	if (ret) {
		pr_err("Failed to read RTC time\n");
		return 0;
	}
	suspend_time =  di->rtc_resume_time - di->rtc_suspend_time;
	pr_info("suspend_time=%d\n", suspend_time);
	update_pre_capacity_data.suspend_time = suspend_time;
	if (di->soc_pre < LOW_BATTERY_LEVEL_THRESHOLD)
		di->short_time_standby_count += SHORT_TIME_STANDBY_SOC_CHECK_COUNT;
	if ((di->rtc_resume_time - di->lcd_off_time >= TWO_POINT_FIVE_MINUTES)
		|| di->short_time_standby_count >= SHORT_TIME_STANDBY_SOC_CHECK_COUNT) {
		pr_err("di->rtc_resume_time - di->lcd_off_time=%ld\n",
				di->rtc_resume_time - di->lcd_off_time);
		__pm_stay_awake(di->update_soc_wake_lock);
		get_current_time(&di->lcd_off_time);
		queue_delayed_work_on(0,
				update_pre_capacity_data.workqueue,
				&(update_pre_capacity_data.work),
				msecs_to_jiffies(1000));
				di->short_time_standby_count = 0;
	} else {
			di->short_time_standby_count++;
	}
	schedule_delayed_work(&bq27541_di->battery_soc_work,
			msecs_to_jiffies(RESUME_SCHDULE_SOC_UPDATE_WORK_MS));
	return 0;
}


static void bq27541_shutdown(struct i2c_client *client)
{
	struct bq27541_device_info *di = i2c_get_clientdata(client);

	if (bq27541_di) {
		if (di->already_modify_smooth)
			bq27411_modify_soc_smooth_parameter(bq27541_di, false);
	}

	if (di->soc_pre != DEFAULT_INVALID_SOC_PRE)
		backup_soc_ex(di->soc_pre);
}

static const struct of_device_id bq27541_match[] = {
	{ .compatible = "ti,bq27541-battery" },
	{ },
};

static const struct i2c_device_id bq27541_id[] = {
	{ "bq27541-battery", 1 },
	{},
};
MODULE_DEVICE_TABLE(i2c, BQ27541_id);
static const struct dev_pm_ops bq27541_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(bq27541_battery_suspend, bq27541_battery_resume)
};

static struct i2c_driver bq27541_battery_driver = {
	.driver		= {
		.name = "bq27541-battery",
		.pm = &bq27541_pm,
	},
	.probe		= bq27541_battery_probe,
	.remove		= bq27541_battery_remove,
	.shutdown	= bq27541_shutdown,
	.id_table	= bq27541_id,
};

static int __init bq27541_battery_init(void)
{
	int ret;

	ret = i2c_add_driver(&bq27541_battery_driver);
	if (ret)
		pr_err("Unable to register BQ27541 driver\n");

	return ret;
}
module_init(bq27541_battery_init);

static void __exit bq27541_battery_exit(void)
{
	i2c_del_driver(&bq27541_battery_driver);
}
module_exit(bq27541_battery_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Qualcomm Innovation Center, Inc.");
MODULE_DESCRIPTION("BQ27541 battery monitor driver");
