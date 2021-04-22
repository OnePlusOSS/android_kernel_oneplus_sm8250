#ifndef __BQ2597X_CHARGER_H__
#define __BQ2597X_CHARGER_H__
enum { ADC_IBUS,
       ADC_VBUS,
       ADC_VAC,
       ADC_VOUT,
       ADC_VBAT,
       ADC_IBAT,
       ADC_TBUS,
       ADC_TBAT,
       ADC_TDIE,
       ADC_MAX_NUM,
};

struct bq2597x {
	struct device *dev;
	struct i2c_client *client;

	int part_no;
	int revision;

	int mode;
	int irq_gpio;

	struct mutex data_lock;
	struct mutex i2c_rw_lock;
	struct mutex charging_disable_lock;
	struct mutex irq_complete;

	bool irq_waiting;
	bool irq_disabled;
	bool resume_completed;

	bool batt_present;
	bool vbus_present;

	bool usb_present;
	bool charge_enabled; /* Register bit status */
	bool adc_enabled;

	/* ADC reading */
	int vbat_volt;
	int vbus_volt;
	int vout_volt;
	int vac_volt;

	int ibat_curr;
	int ibus_curr;

	int bat_temp;
	int bus_temp;
	int die_temp;

	/* alarm/fault status */
	bool bat_ovp_fault;
	bool bat_ocp_fault;
	bool bus_ovp_fault;
	bool bus_ocp_fault;
	bool bus_ucp_fault;

	bool bat_ovp_alarm;
	bool bat_ocp_alarm;
	bool bus_ovp_alarm;
	bool bus_ocp_alarm;

	bool bat_ucp_alarm;

	bool bat_therm_alarm;
	bool bus_therm_alarm;
	bool die_therm_alarm;

	bool bat_therm_fault;
	bool bus_therm_fault;
	bool die_therm_fault;

	bool therm_shutdown_flag;
	bool therm_shutdown_stat;

	bool vbat_reg;
	bool ibat_reg;

	bool chg_alarm;
	bool chg_fault;

	int prev_alarm;
	int prev_fault;

	int chg_ma;
	int chg_mv;

	int charge_state;

	struct bq2597x_cfg *cfg;

	int skip_writes;
	int skip_reads;

	struct bq2597x_platform_data *platform_data;

	struct delayed_work monitor_work;
	struct delayed_work irq_int_work;

	struct dentry *debug_root;

	struct power_supply_desc psy_desc;
	struct power_supply_config psy_cfg;
	struct power_supply *fc2_psy;
};

extern int bq2597x_enable_charge_pump(bool enable);
extern int bq2597x_check_charge_enabled(struct bq2597x *bq, bool *enabled);
extern void bq2597x_dump_reg(struct bq2597x *bq);
extern void exchgpump_information_register(struct bq2597x *bq);
bool bq2597x_charge_status_is_ok(struct bq2597x *bq);
int bq2597x_ftm_test(struct bq2597x *bq);
#endif
