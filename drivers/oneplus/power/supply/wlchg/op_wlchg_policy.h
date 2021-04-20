#ifndef __OP_POLICY_H__
#define __OP_POLICY_H__
#include <linux/miscdevice.h>
#include "bq2597x_charger.h"

//#define HW_TEST_EDITION
//#define IDT_LAB_TEST
//#define OP_DEBUG

#define WLCHG_TASK_INTERVAL round_jiffies_relative(msecs_to_jiffies(500))
#define WLCHG_BATINFO_UPDATE_INTERVAL round_jiffies_relative(msecs_to_jiffies(1000))
#define WLCHG_PD_HARDRESET_WAIT_TIME round_jiffies_relative(msecs_to_jiffies(1000))
#define WLCHG_DISCONNECT_DELAYED msecs_to_jiffies(1500)
#define WLCHG_ACTIVE_DISCONNECT_DELAYED msecs_to_jiffies(5000)

#define TEMPERATURE_STATUS_CHANGE_TIMEOUT 10 // about 10s
#define WPC_CHARGE_CURRENT_LIMIT_300MA 300

#define WLCHG_CHARGE_TIMEOUT 36000 // 10h

#define PMIC_ICL_MAX 1100000

#define WPC_CHARGE_CURRENT_LIMIT_300MA 300
#define WPC_CHARGE_CURRENT_ZERO 0 // 0mA
#define WPC_CHARGE_CURRENT_INIT_100MA 100
#define WPC_CHARGE_CURRENT_200MA 200
#define WPC_CHARGE_CURRENT_DEFAULT 500 // 500mA
#define WPC_CHARGE_CURRENT_ON_TRX 650 // 650mA
#define WPC_CHARGE_CURRENT_BPP 1000 // 1000mA
#define WPC_CHARGE_CURRENT_EPP 1100
#define WPC_CHARGE_CURRENT_FASTCHG_INT 300 //400
#define WPC_CHARGE_CURRENT_FASTCHG_END 700 // 300mA
#define WPC_CHARGE_CURRENT_FASTCHG_MID 800 // 800mA
#define WPC_CHARGE_CURRENT_FASTCHG 1500 // 1500mA
#define WPC_CHARGE_CURRENT_CHANGE_STEP_200MA 200 // 200mA
#define WPC_CHARGE_CURRENT_CHANGE_STEP_50MA 50 // 50mA
#define WPC_CHARGE_CURRENT_FFC_TO_CV 1000 // 1000mA
#define WPC_CHARGE_CURRENT_CHGPUMP_TO_CHARGER 300
#define WPC_CHARGE_CURRENT_WAIT_FAST 300
#define WPC_CHARGE_CURRENT_STOP_CHG 300

#define WPC_CHARGE_1250MA_UPPER_LIMIT 1480 /*1300*/
#define WPC_CHARGE_1250MA_LOWER_LIMIT 1450 /*1200*/
#define WPC_CHARGE_1A_UPPER_LIMIT 1050
#define WPC_CHARGE_1A_LOWER_LIMIT 950
#define WPC_CHARGE_800MA_UPPER_LIMIT 850
#define WPC_CHARGE_800MA_LOWER_LIMIT 750
#define WPC_CHARGE_600MA_UPPER_LIMIT 650
#define WPC_CHARGE_600MA_LOWER_LIMIT 550
#define WPC_CHARGE_500MA_UPPER_LIMIT 600
#define WPC_CHARGE_500MA_LOWER_LIMIT 500

#define WPC_CHARGE_VOLTAGE_DEFAULT 5000 // 5V

#define WPC_CHARGE_VOLTAGE_FASTCHG_INIT 11000
#define WPC_CHARGE_VOLTAGE_STOP_CHG WPC_CHARGE_VOLTAGE_FASTCHG_INIT
#define WPC_CHARGE_VOLTAGE_OVP_MIN 12000
#define WPC_CHARGE_VOLTAGE_FTM 17380
#define WPC_CHARGE_VOLTAGE_FASTCHG WPC_CHARGE_VOLTAGE_DEFAULT // 12000
#define WPC_CHARGE_VOLTAGE_FASTCHG_MAX (WPC_CHARGE_VOLTAGE_DEFAULT + 100) // 15000
#define WPC_CHARGE_VOLTAGE_EPP 9000

#define WPC_CHARGE_VOLTAGE_CHGPUMP_MAX 20100
#define WPC_CHARGE_VOLTAGE_CHGPUMP_MIN 5000

#define WPC_CHARGE_IOUT_HIGH_LEVEL 1050 // 1050mA
#define WPC_CHARGE_IOUT_LOW_LEVEL 950 // 950mA

#define REVERSE_WIRELESS_CHARGE_CURR_LIMT 1500000
#define WIRELESS_CHARGE_UPGRADE_CURR_LIMT 1500000
#define REVERSE_WIRELESS_CHARGE_VOL_LIMT 5500000
#define WIRELESS_CHARGE_FTM_TEST_VOL_LIMT 5000000
#define WIRELESS_CHARGE_UPGRADE_VOL_LIMT 5000000

#define WPC_TERMINATION_CURRENT 200
#define WPC_TERMINATION_VOLTAGE WPC_TERMINATION_VOLTAGE_DEFAULT
#define WPC_RECHARGE_VOLTAGE_OFFSET 200
#define WPC_PRECHARGE_CURRENT 300
#define WPC_CHARGER_INPUT_CURRENT_LIMIT_DEFAULT 1000

#define DCP_TERMINATION_CURRENT 600
#define DCP_TERMINATION_VOLTAGE 4380
#define DCP_RECHARGE_VOLTAGE_OFFSET 200
#define DCP_PRECHARGE_CURRENT 300
#define DCP_CHARGER_INPUT_CURRENT_LIMIT_DEFAULT 1000
#define DCP_CHARGE_CURRENT_DEFAULT 1500

#define WPC_BATT_FULL_CNT 5
#define WPC_RECHARGE_CNT 5

#define WPC_INCREASE_CURRENT_DELAY 2
#define WPC_ADJUST_CV_DELAY 10
#define WPC_CEP_NONZERO_DELAY 1

#define NORMAL_MODE_VOL_MIN         WPC_CHARGE_VOLTAGE_DEFAULT
#define FASTCHG_MODE_VOL_MIN        WPC_CHARGE_VOLTAGE_EPP
#define RX_VOLTAGE_MAX              20000
#define FASTCHG_CURR_30W_MAX_UA     1500000
#define FASTCHG_CURR_20W_MAX_UA     1000000
#define FASTCHG_CURR_15W_MAX_UA     800000
#define FASTCHG_CURR_MIN_UA         600000
#define BATT_HOT_DECIDEGREE_MAX     600
#define FASTCHG_EXIT_DECIDEGREE_MAX 450
#define FASTCHG_EXIT_DECIDEGREE_MIN 0
#define FASTCHG_EXIT_VOL_MAX_UV     4350000

#define RX_EPP_SOFT_OVP_MV  14000
#define RX_FAST_SOFT_OVP_MV 22000

#define CURR_ERR_MIN 50
#define VOL_SET_STEP 20
#define VOL_INC_STEP_MAX 1000
#define VOL_DEC_STEP_MAX 1000
#define VOL_ADJ_LIMIT 14000

#define MAX_STEP_CHG_ENTRIES 8

#define ADAPTER_TYPE_UNKNOWN 0
#define ADAPTER_TYPE_FASTCHAGE_DASH 1
#define ADAPTER_TYPE_FASTCHAGE_WARP 2
#define ADAPTER_TYPE_USB 3
#define ADAPTER_TYPE_NORMAL_CHARGE 4
#define ADAPTER_TYPE_EPP 5

#define WPC_CHARGE_TYPE_DEFAULT 0
#define WPC_CHARGE_TYPE_FAST 1
#define WPC_CHARGE_TYPE_USB 2
#define WPC_CHARGE_TYPE_NORMAL 3
#define WPC_CHARGE_TYPE_EPP 4

#define HEARTBEAT_COUNT_MAX 4
#define EPP_CURR_STEP_MAX 2

#define FOD_PARM_LENGTH 12

#define DEFAULT_SKIN_TEMP 250
#define CHARGE_FULL_FAN_THREOD_LO 350
#define CHARGE_FULL_FAN_THREOD_HI 380

#define FASTCHG_CURR_ERR_MAX 5

enum {
	WPC_CHG_STATUS_DEFAULT,
	WPC_CHG_STATUS_READY_FOR_FASTCHG,
	WPC_CHG_STATUS_WAITING_FOR_TX_INTO_FASTCHG,
	WPC_CHG_STATUS_INCREASE_VOLTAGE,
	WPC_CHG_STATUS_ADJUST_VOL_AFTER_INC_CURRENT,
	WPC_CHG_STATUS_FAST_CHARGING_EXIT,
	WPC_CHG_STATUS_FAST_CHARGING_WAIT_EXIT,
	WPC_CHG_STATUS_STANDARD_CHARGING,
	WPC_CHG_STATUS_FAST_CHARGING_FROM_CHGPUMP,
	WPC_CHG_STATUS_FAST_CHARGING_FFC,
	WPC_CHG_STATUS_FAST_CHARGING_FROM_PMIC,
	WPC_CHG_STATUS_READY_FOR_EPP,
	WPC_CHG_STATUS_EPP,
	WPC_CHG_STATUS_EPP_WORKING,
	WPC_CHG_STATUS_READY_FOR_BPP,
	WPC_CHG_STATUS_BPP,
	WPC_CHG_STATUS_BPP_WORKING,
	WPC_CHG_STATUS_READY_FOR_FTM,
	WPC_CHG_STATUS_FTM_WORKING,
	WPC_CHG_STATUS_READY_FOR_QUIET,
	WPC_CHG_STATUS_WAIT_DISABLE_BATT_CHARGE,
	WPC_CHG_STATUS_DISABLE_BATT_CHARGE,
};

enum WLCHG_TEMP_REGION_TYPE {
	WLCHG_BATT_TEMP_COLD = 0,
	WLCHG_BATT_TEMP_LITTLE_COLD,
	WLCHG_BATT_TEMP_COOL,
	WLCHG_BATT_TEMP_LITTLE_COOL,
	WLCHG_BATT_TEMP_PRE_NORMAL,
	WLCHG_BATT_TEMP_NORMAL,
	WLCHG_BATT_TEMP_WARM,
	WLCHG_BATT_TEMP_HOT,
	WLCHG_TEMP_REGION_MAX,
};
typedef enum {
	WPC_DISCHG_STATUS_OFF,
	WPC_DISCHG_STATUS_ON,
	WPC_DISCHG_IC_READY,
	WPC_DISCHG_IC_PING_DEVICE,
	WPC_DISCHG_IC_TRANSFER,
	WPC_DISCHG_IC_ERR_TX_RXAC,
	WPC_DISCHG_IC_ERR_TX_OCP,
	WPC_DISCHG_IC_ERR_TX_OVP,
	WPC_DISCHG_IC_ERR_TX_LVP,
	WPC_DISCHG_IC_ERR_TX_FOD,
	WPC_DISCHG_IC_ERR_TX_OTP,
	WPC_DISCHG_IC_ERR_TX_CEPTIMEOUT,
	WPC_DISCHG_IC_ERR_TX_RXEPT,
	WPC_DISCHG_STATUS_UNKNOW,
} E_WPC_DISCHG_STATUS;

enum FASTCHG_STARTUP_STEP {
	FASTCHG_EN_CHGPUMP1_STEP,
	FASTCHG_WAIT_CP1_STABLE_STEP,
	FASTCHG_WAIT_PMIC_STABLE_STEP,
	FASTCHG_SET_CHGPUMP2_VOL_STEP,
	FASTCHG_SET_CHGPUMP2_VOL_AGAIN_STEP,
	FASTCHG_EN_CHGPUMP2_STEP,
	FASTCHG_CHECK_CHGPUMP2_STEP,
	FASTCHG_CHECK_CHGPUMP2_AGAIN_STEP,
	FASTCHG_EN_PMIC_CHG_STEP,
};

enum wlchg_msg_type {
	WLCHG_NULL_MSG,
	WLCHG_ADAPTER_MSG,
	WLCHG_FASTCHAGE_MSG,
	WLCHG_USB_CHARGE_MSG,
	WLCHG_NORMAL_CHARGE_MSG,
	WLCHG_NORMAL_MODE_MSG,
	WLCHG_QUIET_MODE_MSG,
	WLCHG_CEP_TIMEOUT_MSG,
	WLCHG_TX_ID_MSG,
};

struct wlchg_msg_t {
	char type;
	char data;
	char remark;
};
struct cmd_info_t {
	unsigned char cmd;
	enum rx_msg_type cmd_type;
	int cmd_retry_count;
};

struct wpc_data {
	char charge_status;
	enum FASTCHG_STARTUP_STEP fastchg_startup_step;
	E_WPC_DISCHG_STATUS wpc_dischg_status;
	bool charge_online;
	bool dock_on;
	bool tx_online;
	bool tx_present;
	bool charge_done;
	int adapter_type;
	int charge_type;
	int charge_voltage;
	int charge_current;
	enum WLCHG_TEMP_REGION_TYPE temp_region;
	int terminate_voltage;
	int terminate_current;
	int max_current;
	int target_curr;
	int target_vol;
	int vol_set;
	int fastchg_level;
	// Record the initial temperature when switching to the next gear.
	int fastchg_level_init_temp;
	// Deviation detection
	int freq_check_count;
	int freq_sum;
	int epp_curr_step;
	int fastchg_curr_step;
	int fastchg_retry_count;
	int curr_err_count;
	bool ftm_mode;
	bool curr_limit_mode;
	bool vol_set_ok;
	bool curr_set_ok;
	bool vol_set_start;
	bool vol_set_fast;
	bool startup_fast_chg;
	bool cep_err_flag;
	bool cep_check;
	/* Exit fast charge, check ffc condition */
	bool ffc_check;
	/* Record if the last current needs to drop */
	bool curr_need_dec;
	bool vol_not_step;
	bool is_power_changed;
	/*
	 * When the battery voltage is greater than the maximum voltage
	 * entering the fast charge, it will no longer be allowed to enter
	 * the fast charge.
	 */
	bool is_deviation;
	bool deviation_check_done;
	bool freq_thr_inc;
	bool wait_cep_stable;

	bool geted_tx_id;
	bool quiet_mode_enabled;
	bool get_adapter_err;
	bool epp_working;
	/* Indicates whether the message of getting adapter type was sent successfully */
	bool adapter_msg_send;

	unsigned long send_msg_timer;
	unsigned long cep_ok_wait_timeout;
	unsigned long fastchg_retry_timer;
	bool rx_ovp;
	bool fastchg_disable;
	/* The disappearance of the wireless fast charge icon requires a delay */
	bool fastchg_display_delay;
	bool cep_timeout_adjusted;
	bool fastchg_restart;
	bool startup_fod_parm;
};

struct op_range_data {
	int low_threshold;
	int high_threshold;
	u32 curr_ua;
	u32 vol_max_mv;
	int need_wait;
};

struct op_fastchg_ffc_step {
	int max_step;
	struct op_range_data ffc_step[MAX_STEP_CHG_ENTRIES];
	bool allow_fallback[MAX_STEP_CHG_ENTRIES];
	unsigned long ffc_wait_timeout;
};

struct charge_param {
	int fastchg_temp_min;
	int fastchg_temp_max;
	int fastchg_soc_min;
	int fastchg_soc_max;
	int fastchg_soc_mid;
	int fastchg_curr_min;
	int fastchg_curr_max;
	int fastchg_vol_min;
	int fastchg_vol_entry_max;
	int fastchg_vol_normal_max;
	int fastchg_vol_hot_max;
	int fastchg_discharge_curr_max;
	int batt_vol_max;
	int BATT_TEMP_T0;
	int BATT_TEMP_T1;
	int BATT_TEMP_T2;
	int BATT_TEMP_T3;
	int BATT_TEMP_T4;
	int BATT_TEMP_T5;
	int BATT_TEMP_T6;
	short mBattTempBoundT0;
	short mBattTempBoundT1;
	short mBattTempBoundT2;
	short mBattTempBoundT3;
	short mBattTempBoundT4;
	short mBattTempBoundT5;
	short mBattTempBoundT6;
	int epp_ibatmax[WLCHG_TEMP_REGION_MAX];
	int bpp_ibatmax[WLCHG_TEMP_REGION_MAX];
	int epp_iclmax[WLCHG_TEMP_REGION_MAX];
	int bpp_iclmax[WLCHG_TEMP_REGION_MAX];
	int vbatdet[WLCHG_TEMP_REGION_MAX];
	int fastchg_ibatmax[2];
	int cool_vbat_thr_mv;
	int cool_epp_ibat_ma;
	int cool_epp_icl_ma;
	int freq_threshold;
	int fastchg_skin_temp_max;
	int fastchg_skin_temp_min;
	int epp_skin_temp_max;
	int epp_skin_temp_min;
	int epp_curr_step[EPP_CURR_STEP_MAX];
	bool fastchg_fod_enable;
	unsigned char fastchg_match_q;
	unsigned char fastchg_fod_parm[FOD_PARM_LENGTH];
	unsigned char fastchg_fod_parm_startup[FOD_PARM_LENGTH];
	struct op_fastchg_ffc_step ffc_chg;
};

enum wireless_mode {
	WIRELESS_MODE_NULL,
	WIRELESS_MODE_TX,
	WIRELESS_MODE_RX,
};

struct op_chg_chip {
	struct device *dev;
	wait_queue_head_t read_wq;
	struct miscdevice wlchg_device;

	bool charger_exist;
	int temperature;
	int batt_volt;
	int batt_volt_max;
	int batt_volt_min;
	int icharging;
	int soc;
	bool otg_switch;
	bool batt_missing;
	bool disable_charge;
	bool disable_batt_charge;
	bool ap_ctrl_dcdc;
	bool pmic_high_vol;
	bool quiet_mode_need;
	bool wlchg_wake_lock_on;
	bool reverse_wlchg_wake_lock_on;
	int wrx_en_gpio;
	int wrx_otg_gpio;
	int dcdc_en_gpio;
	int usbin_int_gpio;
	int usbin_int_irq;
	bool pd_charger_online;
	int wlchg_time_count;
	struct pinctrl *pinctrl;
	struct pinctrl_state *usbin_int_active;
	struct pinctrl_state *usbin_int_sleep;
	struct pinctrl_state *usbin_int_default;
	struct pinctrl_state *wrx_en_active;
	struct pinctrl_state *wrx_en_sleep;
	struct pinctrl_state *wrx_en_default;
	struct pinctrl_state *wrx_otg_active;
	struct pinctrl_state *wrx_otg_sleep;
	struct pinctrl_state *wrx_otg_default;
	struct pinctrl_state *dcdc_en_active;
	struct pinctrl_state *dcdc_en_sleep;
	struct pinctrl_state *dcdc_en_default;

	struct wakeup_source *wlchg_wake_lock;
	struct wakeup_source *reverse_wlchg_wake_lock;

	struct mutex chg_lock;
	struct mutex connect_lock;
	struct mutex read_lock;
	struct mutex msg_lock;

	struct delayed_work wlchg_task_work; // for WPC
	struct delayed_work update_bat_info_work;
	struct delayed_work usbin_int_work;
	struct delayed_work wait_wpc_chg_quit;
	struct delayed_work dischg_work; // for WPC
	struct delayed_work fastchg_curr_vol_work;
	struct delayed_work tx_check_work;
	struct delayed_work charger_exit_work;
	struct delayed_work wlchg_connect_check_work;
	struct delayed_work wlchg_fcc_stepper_work;
	struct wpc_data wlchg_status; // for WPC
	struct charge_param chg_param;
	atomic_t suspended;
#ifdef HW_TEST_EDITION
	int w30w_time;
	bool w30w_timeout;
	bool w30w_work_started;
	struct delayed_work w30w_timeout_work;
#endif
	struct power_supply *wireless_psy;
	struct power_supply *batt_psy;
	struct votable *wlcs_fcc_votable;
	struct votable *fastchg_disable_votable;
	enum power_supply_type wireless_type;
	enum wireless_mode wireless_mode;

	bool wlchg_msg_ok;
	bool heart_stop;
	struct cmd_info_t cmd_info;
	struct wlchg_msg_t msg_info;

	atomic_t hb_count; //heartbeat_count
};

extern void wlchg_rx_policy_register(struct op_chg_chip *op_wlchg);

void wlchg_set_rtx_function(bool is_on);
bool wlchg_wireless_charge_start(void);
int wlchg_get_usbin_val(void);
void op_set_wrx_en_value(int value);
void op_set_wrx_otg_value(int value);
void op_set_dcdc_en_value(int value);
int wlchg_connect_callback_func(bool ldo_on);
int wlchg_tx_callback(void);
bool wlchg_wireless_working(void);
extern int register_reverse_charge_notifier(struct notifier_block *nb);
extern int unregister_reverse_charge_notifier(struct notifier_block *nb);


#endif
