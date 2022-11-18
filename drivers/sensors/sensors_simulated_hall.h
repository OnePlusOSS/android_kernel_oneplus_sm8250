#ifndef __HALL_SIMULATED_H__
#define __HALL_SIMULATED_H__

struct hall_simulated_data {
	int id;                     /* show the current id */
	char DEV_NAME[64];

	int irq_gpio;               /* device use gpio number */
	int irq_number;             /* device request irq number */
	uint32_t irq_flags;         /* device irq flags */
	int active_low;             /* gpio active high or low for valid value */

	int hall_status;            /* device status of latest */
	int handle_option;          /* different option set about hall status*/

	struct mutex            report_mutex;
	struct input_dev        *hall_input_dev;
	struct pinctrl          *hall_pinctrl;
	struct pinctrl_state    *hall_int_active;	
	struct pinctrl_state    *hall_int_sleep;
};

typedef enum {
	TYPE_HALL_UNDEFINED,
	TYPE_HALL_NEAR = 1,     /*means in near status*/
	TYPE_HALL_FAR,          /*means in far status*/
} hall_status;

typedef enum {
	TYPE_HANDLE_UNDEFINED,
	TYPE_HANDLE_NOTIFY_WIRELESS = 1,         /*means notify wireless charge*/
	TYPE_HANDLE_REPORT_KEYS = 2,             /*means report keys to android*/
} handle_option_type;

#endif

