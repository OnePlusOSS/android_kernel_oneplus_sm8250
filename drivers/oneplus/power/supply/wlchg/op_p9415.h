#ifndef __OP_P9415_H__
#define __OP_P9415_H__

#define p9415_UPDATE_INTERVAL round_jiffies_relative(msecs_to_jiffies(5000))
#define p9415_CHECK_LDO_ON_DELAY round_jiffies_relative(msecs_to_jiffies(2000))

#define P9415_STATUS_REG 0x0036
#define P9415_VOUT_ERR_MASK BIT(3)
#define P9415_EVENT_MASK    BIT(4)
#define P9415_LDO_ON_MASK   BIT(6)

struct op_p9415_ic {
	struct i2c_client *client;
	struct device *dev;
        struct rx_chip *rx_chip;

	int idt_en_gpio;
	int idt_con_gpio;
	int idt_con_irq;
	int idt_int_gpio;
	int idt_int_irq;
	int vbat_en_gpio;
	int booster_en_gpio;

	bool idt_fw_updating;
	bool check_fw_update;
	bool connected_ldo_on;

	char fw_id[16];
	char manu_name[16];

	struct pinctrl *pinctrl;
	struct pinctrl_state *idt_en_active;
	struct pinctrl_state *idt_en_sleep;
	struct pinctrl_state *idt_en_default;
	struct pinctrl_state *idt_con_active;
	struct pinctrl_state *idt_con_sleep;
	struct pinctrl_state *idt_con_default;
	struct pinctrl_state *idt_int_active;
	struct pinctrl_state *idt_int_sleep;
	struct pinctrl_state *idt_int_default;
	struct pinctrl_state *vbat_en_active;
	struct pinctrl_state *vbat_en_sleep;
	struct pinctrl_state *vbat_en_default;
	struct pinctrl_state *booster_en_active;
	struct pinctrl_state *booster_en_sleep;
	struct pinctrl_state *booster_en_default;

	struct delayed_work p9415_update_work;
	struct delayed_work idt_event_int_work;
	struct delayed_work idt_connect_int_work;
	struct delayed_work check_ldo_on_work;
	struct wakeup_source *update_fw_wake_lock;
};

#endif