#ifndef _OP_WLC_HELPER_H_
#define _OP_WLC_HELPER_H_
#include <linux/oem/power/oem_external_fg.h>
#include <linux/power_supply.h>
#include <linux/version.h>

#define chg_debug(fmt, ...)                                                    \
	printk(KERN_NOTICE "[WLCHG][%s]" fmt, __func__, ##__VA_ARGS__)

#define chg_err(fmt, ...)                                                      \
	printk(KERN_ERR "[WLCHG][%s]" fmt, __func__, ##__VA_ARGS__)

#define chg_info(fmt, ...)                                                     \
	printk(KERN_INFO "[WLCHG][%s]" fmt, __func__, ##__VA_ARGS__)

#define WLCHG_FTM_TEST_RX_ERR BIT(0)
#define WLCHG_FTM_TEST_CP1_ERR BIT(1)
#define WLCHG_FTM_TEST_CP2_ERR BIT(2)

enum WLCHG_MSG_TYPE {
	WLCHG_MSG_CHG_INFO,
	WLCHG_MSG_CMD_RESULT,
	WLCHG_MSG_CMD_ERR,
	WLCHG_MSG_HEARTBEAT,
};

extern bool wlchg_wireless_charge_start(void);
extern int p922x_wireless_get_vout(void);
bool typec_is_otg_mode(void);
int switch_to_otg_mode(bool enable);
#endif
