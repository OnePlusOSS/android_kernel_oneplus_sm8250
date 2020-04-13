/************************************************************************************
** File:  op_chargepump.c
** VENDOR_EDIT
** Copyright (C), 2008-2012, OP Mobile Comm Corp., Ltd
**
** Description:
**
**
** Version: 1.0
** Date created: 21:03:46,09/04/2019
** Author: Lin Shangbo
**
** --------------------------- Revision History:
*------------------------------------------------------------ <version> <date>
*<author>              			<desc> Revision 1.0    2019-04-09    Lin
*Shangbo    		Created for new charger
************************************************************************************************************/

#ifndef __OP_CHARGEPUMP_H__
#define __OP_CHARGEPUMP_H__

#define CP_STATUS_ADDR 0x04
#define CP_NOT_REEADY  0
#define CP_REEADY      BIT(0)
#define CP_DWP         BIT(1)
#define CP_OTP         BIT(2)
#define CP_SWITCH_OCP  BIT(3)
#define CP_CRP         BIT(4)
#define CP_VOUT_OVP    BIT(5)
#define CP_CLP         BIT(6)
#define CP_VBUS_OVP    BIT(7)

struct chip_chargepump {
	struct i2c_client *client;
	struct device *dev;
	struct pinctrl *pinctrl;
	int chargepump_en_gpio;
	struct pinctrl_state *chargepump_en_active;
	struct pinctrl_state *chargepump_en_sleep;
	struct pinctrl_state *chargepump_en_default;
	atomic_t chargepump_suspended;
	struct delayed_work watch_dog_work;
};

extern int chargepump_hw_init(void);
extern int chargepump_enable(void);
extern int chargepump_set_for_otg(char enable);
extern int chargepump_set_for_LDO(void);
extern int chargepump_disable(void);
void __chargepump_show_registers(void);
int chargepump_enable_dwp(struct chip_chargepump *chip, bool enable);
int chargepump_disable_dwp(void);
int chargepump_status_check(u8 *status);
int chargepump_i2c_test(void);
int chargepump_check_config(void);

#endif
