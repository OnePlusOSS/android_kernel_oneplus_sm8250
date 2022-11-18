/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __OPLUS_WAKELOCK_PROFILER_H__
#define __OPLUS_WAKELOCK_PROFILER_H__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/ktime.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/wakeup_reason.h>
#include <linux/alarmtimer.h>
#include <linux/suspend.h>
#include <linux/msm_drm_notify.h>


#define WS_CNT_MASK 0xffff
#define WS_CNT_POWERKEY (1<<0)
#define WS_CNT_RTCALARM (1<<1)
/* Not a wakeup source interrupt, but a non-net alarm wakeup */
#define WS_CNT_ALARM (1<<2)
#define WS_CNT_MODEM (1<<3)
#define WS_CNT_WLAN (1<<4)
#define WS_CNT_ADSP (1<<5)
#define WS_CNT_CDSP (1<<6)
#define WS_CNT_SLPI (1<<7)

#define WS_CNT_GLINK (1<<13)
#define WS_CNT_ABORT (1<<14)
#define WS_CNT_SUM (1<<15)

#define WS_CNT_ALL WS_CNT_MASK

#define IRQ_NAME_POWERKEY "pon_kpdpwr_status"
#define IRQ_NAME_RTCALARM "pm8xxx_rtc_alarm"
#define IRQ_NAME_ALARM  "alarm"	/* pmic alarm */
#define IRQ_NAME_MODEM_QSI "msi_modem_irq"
#define IRQ_NAME_MODEM_GLINK "glink-native-modem"
#define IRQ_NAME_MODEM_IPA "ipa"
#define IRQ_NAME_MODEM_QMI "qmi" /* dummy irq name */
#define IRQ_NAME_MODEM_MODEM "modem"
#define IRQ_NAME_WLAN_MSI "msi_wlan_irq"
#define IRQ_NAME_WLAN_IPCC_DATA "WLAN"
#define IRQ_NAME_ADSP "adsp"
#define IRQ_NAME_ADSP_GLINK "glink-native-adsp"
#define IRQ_NAME_CDSP "cdsp"
#define IRQ_NAME_CDSP_GLINK "glink-native-cdsp"
#define IRQ_NAME_SLPI "slpi"
#define IRQ_NAME_SLPI_GLINK "glink-native-slpi"
#define IRQ_NAME_GLINK "glink" /* dummy irq name */
#define IRQ_NAME_ABORT "abort" /* dummy irq name */
#define IRQ_NAME_WAKE_SUM "wakeup_sum" /* dummy irq name */

#define MODULE_DETAIL_PRINT 0
#define MODULE_STATIS_PRINT 1

#define IRQ_PROP_MASK 0xff
/* real wakeup source irq name */
#define IRQ_PROP_REAL (1<<0)
/* exchanged wakeup source irq name */
#define IRQ_PROP_EXCHANGE (1<<1)
/* not wakeup source irq name, just used for statics*/
#define IRQ_PROP_DUMMY_STATICS (1<<2)

#define MODEM_REPORT_NUMBER 2 /*only IPA_WS and QMI_WS*/

#define KONA 1
#define LITO 2
#define TRINKET 3
#define LAHAINA 4
#define HOLI 5
#define BENGAL 6
#define LAGOON 7
#define BLAIR 8

#define PLATFORM_KONA "kona"
#define PLATFORM_LITO "lito"
#define PLATFORM_TRINKET "trinket"
#define PLATFORM_LAHAINA "lahaina"
#define PLATFORM_YUPIK "yupik"
#define PLATFORM_LAGOON "lagoon"
#define PLATFORM_BLAIR "blair"
#define PLATFORM_BENGAL "bengal"
#define PLATFORM_HOLI "holi"

struct ws_desc_t {
	int prop;
	/* abstract irq name */
	const char *name;
	uint64_t count;
};

struct wakeup_count_desc_t {
	const char *module_name;
	uint64_t module_all_count;
	int module_mask;
	int ws_number;
	struct ws_desc_t ws_desc[7]; /*shouldn't less than 7*/
};

#if defined(CONFIG_OPLUS_WAKELOCK_PROFILER) || defined(CONFIG_OPLUS_WAKELOCK_PROFILER)

int wakeup_reasons_statics(const char *irq_name, int choose_flag);
void wakeup_reasons_clear(int choose_flag);
void wakeup_reasons_print(int choose_flag, int datil);

void alarmtimer_suspend_flag_set(void);
void alarmtimer_suspend_flag_clear(void);
void alarmtimer_busy_flag_set(void);
void alarmtimer_busy_flag_clear(void);
void alarmtimer_wakeup_count(struct alarm *alarm);

void wakeup_get_start_time(void);
void wakeup_get_end_hold_time(void);
int get_cached_platform_id(void);

#else

static inline int wakeup_reasons_statics(const char *irq_name, int choose_flag) {return true;}
static inline void wakeup_reasons_clear(int choose_flag) {}
static inline void wakeup_reasons_print(int choose_flag, int datil) {}

static inline void alarmtimer_suspend_flag_set(void) {}
static inline void alarmtimer_suspend_flag_clear(void) {}
static inline void alarmtimer_busy_flag_set(void) {}
static inline void alarmtimer_busy_flag_clear(void) {}
static inline void alarmtimer_wakeup_count(struct alarm *alarm) {}

static inline void wakeup_get_start_time(void) {}
static inline void wakeup_get_end_hold_time(void) {}

#endif

#endif  /* __OPLUS_WAKELOCK_PROFILER_H__ */
