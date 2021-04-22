#ifndef __OP_WLCHG_RX_H__
#define __OP_WLCHG_RX_H__

#define RX_RESPONE_ADAPTER_TYPE 0xF1
#define RX_RESPONE_INTO_FASTCHAGE 0xF2
#define RX_RESPONE_INTO_USB_CHARGE 0xF3
#define RX_RESPONE_INTO_NORMAL_CHARGER 0xF4
#define RX_RESPONE_INTO_NORMAL_MODE 0xF5
#define RX_RESPONE_INTO_QUIET_MODE 0xF6
#define RX_RESPONE_GETED_TX_ID 0x5F
#define RX_COMMAND_READY_FOR_EPP 0xFA
#define RX_COMMAND_WORKING_IN_EPP 0xFB
#define RX_RESPONE_NULL 0x00

#define IOUT_AVERAGE_NUM 4

#define TRX_READY BIT(0)
#define TRX_DIGITALPING BIT(1)
#define TRX_ANALOGPING BIT(2)
#define TRX_TRANSFER BIT(3)

#define TRX_ERR_TX_RXAC BIT(0)
#define TRX_ERR_TX_OCP BIT(1)
#define TRX_ERR_TX_OVP BIT(2)
#define TRX_ERR_TX_LVP BIT(3)
#define TRX_ERR_TX_FOD BIT(4)
#define TRX_ERR_TX_OTP BIT(5)
#define TRX_ERR_TX_CEPTIMEOUT BIT(6)
#define TRX_ERR_TX_RXEPT BIT(7)

enum send_msg {
	RX_INDENTIFY_ADAPTER_MSG,
	RX_INTO_FASTCHAGE_MSG,
	RX_INTO_USB_CHARGE_MSG,
	RX_INTO_NORMAL_CHARGE_MSG,
	RX_INTO_NORMAL_MODE_MSG,
	RX_INTO_QUIET_MODE_MSG,
	RX_GET_TX_ID_MSG,
	RX_NULL_MSG,
};

enum E_RX_MODE {
	RX_RUNNING_MODE_EPP,
	RX_RUNNING_MODE_BPP,
	RX_RUNNING_MODE_OTHERS,
};

union rx_chip_propval {
	int intval;
	const char *strval;
	int64_t int64val;
};

enum rx_prop_type {
	RX_PROP_VOUT,
	RX_PROP_VRECT,
	RX_PROP_IOUT,
	RX_PROP_CEP,
	RX_PROP_CEP_SKIP_CHECK_UPDATE,
	RX_PROP_WORK_FREQ,
	RX_PROP_TRX_ENABLE,
	RX_PROP_TRX_STATUS,
	RX_PROP_TRX_ERROR_CODE,
	RX_PROP_TRX_VOL,
	RX_PROP_TRX_CURR,
	RX_PROP_RUN_MODE,
	RX_PROP_ENABLE_DCDC,
	RX_PROP_FTM_TEST,
	RX_PROP_CHIP_SLEEP,
	RX_PROP_CHIP_EN,
	RX_PROP_CHIP_CON,
	RX_PROP_FW_UPDATING,
	RX_PROP_HEADROOM,
};

enum rx_msg_type {
	RX_MSG_LONG,
	RX_MSG_MEDIUM,
	RX_MSG_SHORT,
};

struct rx_chip_prop {
	void *private_data;
	int (*get_prop)(struct rx_chip_prop *, enum rx_prop_type, union rx_chip_propval *);
	int (*set_prop)(struct rx_chip_prop *, enum rx_prop_type, union rx_chip_propval *);
	int (*send_msg)(struct rx_chip_prop *, enum rx_msg_type, unsigned char);
	int (*send_match_q_parm)(struct rx_chip_prop *, unsigned char);
	int (*set_fod_parm)(struct rx_chip_prop *, const char []);
	void (*rx_reset)(struct rx_chip_prop *);
};

struct rx_data {
	int send_message;
	unsigned long send_msg_timer;
	int charge_voltage;
	int charge_current;
	int vout;
	int vrect;
	int iout;
	int iout_now;
	enum E_RX_MODE rx_runing_mode;
	bool check_fw_update;
	bool idt_fw_updating;
};

struct rx_chip {
	struct device *dev;

	bool on_op_trx;

	struct rx_chip_prop *prop;
	struct regmap *regmap;
	struct rx_data chg_data;
};

int wlchg_rx_register_prop(struct device *parent, struct rx_chip_prop *chip_prop);
int wlchg_rx_set_vout(struct rx_chip *chip, int val);
int wlchg_rx_get_vout(struct rx_chip *chip, int *val);
int wlchg_rx_ftm_test(struct rx_chip *chip);
int wlchg_rx_get_vrect_iout(struct rx_chip *chip);
int wlchg_rx_get_tx_vol(struct rx_chip *chip, int *val);
int wlchg_rx_get_tx_curr(struct rx_chip *chip, int *val);
int wlchg_rx_get_cep(struct rx_chip *chip, signed char *val);
int wlchg_rx_get_cep_skip_check_update(struct rx_chip *chip, signed char *val);
int wlchg_rx_get_cep_flag(struct rx_chip *chip);
int wlchg_rx_get_cep_flag_skip_check_update(struct rx_chip *chip);
void wlchg_rx_get_run_flag(struct rx_chip *chip);
int wlchg_rx_enable_dcdc(struct rx_chip *chip);
enum E_RX_MODE wlchg_rx_get_run_mode(struct rx_chip *chip);
int wlchg_rx_get_work_freq(struct rx_chip *chip, int *val);
int wlchg_rx_set_chip_sleep(int val);
int wlchg_rx_get_chip_sleep(void);
int wlchg_rx_set_chip_en(int val);
int wlchg_rx_get_chip_en(void);
int wlchg_rx_get_chip_con(void);
bool wlchg_rx_fw_updating(struct rx_chip *chip);
int wlchg_rx_get_idt_rtx_status(struct rx_chip *chip, char *status, char *err);
void wlchg_rx_reset_variables(struct rx_chip *chip);
int wlchg_rx_trx_enbale(struct rx_chip *chip, bool enable);
int wlchg_rx_get_headroom(struct rx_chip *chip, int *val);
int wlchg_rx_set_headroom(struct rx_chip *chip, int val);
int wlchg_rx_set_match_q_parm(struct rx_chip *chip, unsigned char data);
int wlchg_rx_set_fod_parm(struct rx_chip *chip, const char data[]);

#endif
