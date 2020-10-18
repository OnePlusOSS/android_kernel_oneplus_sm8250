#ifndef __OP_BQ27541_H__
#define __OP_BQ27541_H__
/* david.liu@bsp, 20161004 Add BQ27411 support */
#define CONFIG_GAUGE_BQ27411		1
#define DEVICE_TYPE_BQ27541		0x0541
#define DEVICE_TYPE_BQ27411		0x0421
#define DEVICE_TYPE_BQ28Z610	0xFFA5
#define DEVICE_BQ27541			0
#define DEVICE_BQ27411			1
#define DEVICE_BQ28Z610			2

#define DRIVER_VERSION			"1.1.0"

/* Bq28Z610 standard data commands */
#define Bq28Z610_REG_TI			0x0c
#define Bq28Z610_REG_AI			0x14

/* Bq27541 standard data commands */
#define BQ27541_REG_CNTL		0x00
#define BQ27541_REG_AR			0x02
#define BQ27541_REG_ARTTE		0x04
#define BQ27541_REG_TEMP		0x06
#define BQ27541_REG_VOLT		0x08
#define BQ27541_REG_FLAGS		0x0A
#define BQ27541_REG_NAC			0x0C
#define BQ27541_REG_FAC			0x0e
#define BQ27541_REG_RM			0x10
#define BQ27541_REG_FCC			0x12
#define BQ27541_REG_AI			0x14
#define BQ27541_REG_TTE			0x16
#define BQ27541_REG_TTF			0x18
#define BQ27541_REG_SI			0x1a
#define BQ27541_REG_STTE		0x1c
#define BQ27541_REG_MLI			0x1e
#define BQ27541_REG_MLTTE		0x20
#define BQ27541_REG_AE			0x22
#define BQ27541_REG_AP			0x24
#define BQ27541_REG_TTECP		0x26
#define BQ27541_REG_SOH			0x28
#define BQ27541_REG_SOC			0x2c
#define BQ27541_REG_NIC			0x2e
#define BQ27541_REG_ICR			0x30
#define BQ27541_REG_LOGIDX		0x32
#define BQ27541_REG_LOGBUF		0x34

#define BQ27541_FLAG_DSC		BIT(0)
#define BQ27541_FLAG_FC			BIT(9)

#define BQ27541_CS_DLOGEN		BIT(15)
#define BQ27541_CS_SS		    BIT(13)

#ifdef CONFIG_GAUGE_BQ27411
/* david.liu@bsp, 20161004 Add BQ27411 support */
/* Bq27411 standard data commands */
#define BQ27411_REG_TEMP                0x02
#define BQ27411_REG_VOLT                0x04
#define BQ27411_REG_RM                  0x0A
#define BQ27411_REG_AI                  0x10
#define BQ27411_REG_SOC                 0x1c
#define BQ27411_REG_HEALTH              0x20
#define BQ27411_REG_FCC                 0x2E

#define CONTROL_CMD                 0x00
#define CONTROL_STATUS              0x00
#define SEAL_POLLING_RETRY_LIMIT    100
#define BQ27541_UNSEAL_KEY          0x11151986
#define BQ27411_UNSEAL_KEY          0x80008000

#define BQ27541_RESET_SUBCMD        0x0041
#define BQ27411_RESET_SUBCMD        0x0042
#define SEAL_SUBCMD                 0x0020

#define BQ27411_CONFIG_MODE_POLLING_LIMIT	60
#define BQ27411_CONFIG_MODE_BIT                 BIT(4)
#define BQ27411_BLOCK_DATA_CONTROL		0x61
#define BQ27411_DATA_CLASS_ACCESS		0x003e
#define BQ27411_CC_DEAD_BAND_ID                 0x006b
#define BQ27411_CC_DEAD_BAND_ADDR		0x42
#define BQ27411_CHECKSUM_ADDR				0x60
#define BQ27411_CC_DEAD_BAND_POWERUP_VALUE		0x11
#define BQ27411_CC_DEAD_BAND_SHUTDOWN_VALUE		0x71

#define BQ27411_OPCONFIGB_ID                            0x0040
#define BQ27411_OPCONFIGB_ADDR                          0x42
#define BQ27411_OPCONFIGB_POWERUP_VALUE                 0x07
#define BQ27411_OPCONFIGB_SHUTDOWN_VALUE                0x0f

#define BQ27411_DODATEOC_ID                           0x0024
#define BQ27411_DODATEOC_ADDR                         0x48
#define BQ27411_DODATEOC_POWERUP_VALUE                0x32
#define BQ27411_DODATEOC_SHUTDOWN_VALUE               0x32

/* Bq28z610 standard data commands */
#define BQ28Z610_REG_CNTL1					0x3e
#define BQ28Z610_REG_CNTL2					0x60
#define BQ28Z610_SEAL_POLLING_RETRY_LIMIT	10

#define BQ28Z610_SEAL_STATUS				0x0054
#define BQ28Z610_SEAL_SUBCMD				0x0030
#define BQ28Z610_UNSEAL_SUBCMD1				0x0414
#define BQ28Z610_UNSEAL_SUBCMD2				0x3672
//#define BQ28Z610_SEAL_BIT		     (BIT(8) | BIT(9))
#define BQ28Z610_SEAL_BIT				(BIT(0) | BIT(1))

#define BQ28Z610_SEAL_SHIFT					8
#define BQ28Z610_SEAL_VALUE					3
#define BQ28Z610_MAC_CELL_VOLTAGE_ADDR		0x40
#define BQ28Z610_REG_CNTL1_SIZE				4

#define BQ28Z610_REG_I2C_SIZE				3

#define BQ28Z610_REG_GAUGE_EN				0x0057
#define BQ28Z610_GAUGE_EN_BIT				BIT(3)

#define BQ28Z610_MAC_CELL_VOLTAGE_EN_ADDR		0x3E
#define BQ28Z610_MAC_CELL_VOLTAGE_CMD			0x0071
#define BQ28Z610_MAC_CELL_VOLTAGE_ADDR			0x40
#define BQ28Z610_MAC_CELL_VOLTAGE_SIZE			4//total 34byte,only read 4byte(aaAA bbBB)

#define BQ28Z610_MAC_CELL_BALANCE_TIME_EN_ADDR	0x3E
#define BQ28Z610_MAC_CELL_BALANCE_TIME_CMD		0x0076
#define BQ28Z610_MAC_CELL_BALANCE_TIME_ADDR		0x40
#define BQ28Z610_MAC_CELL_BALANCE_TIME_SIZE		4//total 10byte,only read 4byte(aaAA bbBB)

#define BQ28Z610_OPERATION_STATUS_EN_ADDR		0x3E
#define BQ28Z610_OPERATION_STATUS_CMD			0x0054
#define BQ28Z610_REG_OPERATION_STATUS_ADDR		0x40
#define BQ28Z610_OPERATION_STATUS_SIZE			4
#define BQ28Z610_BALANCING_CONFIG_BIT			BIT(28)

#define BQ28Z610_REG_TIME_TO_FULL			0x18
#endif

/* BQ27541 Control subcommands */
#define BQ27541_SUBCMD_CTNL_STATUS  0x0000
#define BQ27541_SUBCMD_DEVCIE_TYPE  0x0001
#define BQ27541_SUBCMD_FW_VER  0x0002
#define BQ27541_SUBCMD_HW_VER  0x0003
#define BQ27541_SUBCMD_DF_CSUM 0x0004
#define BQ27541_SUBCMD_PREV_MACW   0x0007
#define BQ27541_SUBCMD_CHEM_ID     0x0008
#define BQ27541_SUBCMD_BD_OFFSET   0x0009
#define BQ27541_SUBCMD_INT_OFFSET  0x000a
#define BQ27541_SUBCMD_CC_VER   0x000b
#define BQ27541_SUBCMD_OCV      0x000c
#define BQ27541_SUBCMD_BAT_INS   0x000d
#define BQ27541_SUBCMD_BAT_REM   0x000e
#define BQ27541_SUBCMD_SET_HIB   0x0011
#define BQ27541_SUBCMD_CLR_HIB   0x0012
#define BQ27541_SUBCMD_SET_SLP   0x0013
#define BQ27541_SUBCMD_CLR_SLP   0x0014
#define BQ27541_SUBCMD_FCT_RES   0x0015
#define BQ27541_SUBCMD_ENABLE_DLOG  0x0018
#define BQ27541_SUBCMD_DISABLE_DLOG 0x0019
#define BQ27541_SUBCMD_SEALED       0x0020
#define BQ27541_SUBCMD_ENABLE_IT    0x0021
#define BQ27541_SUBCMD_DISABLE_IT   0x0023
#define BQ27541_SUBCMD_CAL_MODE  0x0040
#define BQ27541_SUBCMD_RESET     0x0041
#define ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN   (-2731)
#define BQ27541_INIT_DELAY   ((HZ)*1)
#define SET_BQ_PARAM_DELAY_MS 6000


/* Bq27411 sub commands */
#define BQ27411_SUBCMD_CNTL_STATUS				0x0000
#define BQ27411_SUBCMD_DEVICE_TYPE				0x0001
#define BQ27411_SUBCMD_FW_VER                                   0x0002
#define BQ27411_SUBCMD_DM_CODE					0x0004
#define BQ27411_SUBCMD_CONFIG_MODE				0x0006
#define BQ27411_SUBCMD_PREV_MACW				0x0007
#define BQ27411_SUBCMD_CHEM_ID					0x0008
#define BQ27411_SUBCMD_SET_HIB					0x0011
#define BQ27411_SUBCMD_CLR_HIB					0x0012
#define BQ27411_SUBCMD_SET_CFG					0x0013
#define BQ27411_SUBCMD_SEALED					0x0020
#define BQ27411_SUBCMD_RESET                                    0x0041
#define BQ27411_SUBCMD_SOFTRESET				0x0042
#define BQ27411_SUBCMD_EXIT_CFG                                 0x0043

#define BQ27411_SUBCMD_ENABLE_DLOG				0x0018
#define BQ27411_SUBCMD_DISABLE_DLOG				0x0019
#define BQ27411_SUBCMD_ENABLE_IT				0x0021
#define BQ27411_SUBCMD_DISABLE_IT				0x0023

#define BQ27541_BQ27411_CMD_INVALID			0xFF
#define FW_VERSION_4P45V_01			0x0110
#define FW_VERSION_4P45V_02			0x0200


#define ERROR_SOC  33
#define ERROR_BATT_VOL  (3800 * 1000)

#ifdef CONFIG_GAUGE_BQ27411
/* david.liu@bsp, 20161004 Add BQ27411 support */
struct cmd_address {
	u8	reg_temp;
	u8	reg_volt;
	u8	reg_rm;
	u8	reg_ai;
	u8	reg_soc;
	u8	reg_helth;
	u8	reg_fcc;
};
#endif

struct bq27541_device_info;
struct bq27541_access_methods {
	int (*read)(u8 reg, int *rt_value, int b_single,
			struct bq27541_device_info *di);
};

struct bq27541_device_info {
	struct device			*dev;
	int				id;
	struct bq27541_access_methods	*bus;
	struct i2c_client		*client;
	struct work_struct		counter;
	/* 300ms delay is needed after bq27541 is powered up
	 * and before any successful I2C transaction
	 */
	struct  delayed_work		hw_config;
	struct  delayed_work		modify_soc_smooth_parameter;
	struct  delayed_work		battery_soc_work;
	struct wakeup_source *update_soc_wake_lock;
	struct power_supply	*batt_psy;
	int saltate_counter;
	/*  Add for retry when config fail */
	int retry_count;
	/*  Add for get right soc when sleep long time */
	int soc_pre;
	int  batt_vol_pre;
	int current_pre;
	int cap_pre;
	int remain_pre;
	int health_pre;
	unsigned long rtc_resume_time;
	unsigned long rtc_suspend_time;
	atomic_t suspended;
	int temp_pre;
	int lcd_off_delt_soc;
	int  t_count;
	int  temp_thr_update_count;
	int  fw_ver;
	int time_to_full;
	int short_time_standby_count;

	bool lcd_is_off;
	bool allow_reading;
	bool fastchg_started;
	bool wlchg_started;
	bool bq_present;
	bool set_smoothing;
	bool disable_calib_soc;
	unsigned long	lcd_off_time;
	unsigned long	soc_pre_time;
	/* david.liu@oneplus.tw, 2016/05/16  Fix capacity won't udate */
	unsigned long	soc_store_time;
#ifdef CONFIG_GAUGE_BQ27411
	/* david.liu@bsp, 20161004 Add BQ27411 support */
	int device_type;
	struct cmd_address cmd_addr;
	bool modify_soc_smooth;
	bool already_modify_smooth;
	bool batt_bq28z610;
	bool bq28z610_need_balancing;
	int batt_cell_1_vol;
	int batt_cell_2_vol;
	int batt_cell_max_vol;
	int batt_cell_min_vol;
	int max_vol_pre;
	int min_vol_pre;
#endif
	bool bat_4p45v;
	bool check_match;
};


#endif
