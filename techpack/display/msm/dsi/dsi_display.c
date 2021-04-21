// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/err.h>

#include "msm_drv.h"
#include "sde_connector.h"
#include "msm_mmu.h"
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_ctrl.h"
#include "dsi_ctrl_hw.h"
#include "dsi_drm.h"
#include "dsi_clk.h"
#include "dsi_pwr.h"
#include "sde_dbg.h"
#include "dsi_parser.h"
#include <drm/drm_panel.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <linux/pm_qos.h>
#include <linux/cpufreq.h>
#include <linux/pm_wakeup.h>
#include <linux/input.h>
#include <linux/proc_fs.h>
#include "../sde/sde_trace.h"
#include "dsi_parser.h"
#if defined(CONFIG_PXLW_IRIS)
#include "iris/dsi_iris5_api.h"
#include "iris/dsi_iris5_lightup.h"
#include "iris/dsi_iris5_loop_back.h"
#include <video/mipi_display.h>
#elif defined(CONFIG_PXLW_SOFT_IRIS)
#include "iris/dsi_iris5_api.h"
#endif
#include <linux/oem/boot_mode.h>
#define to_dsi_display(x) container_of(x, struct dsi_display, host)
#define INT_BASE_10 10

#define MISR_BUFF_SIZE	256
#define ESD_MODE_STRING_MAX_LEN 256
#define ESD_TRIGGER_STRING_MAX_LEN 10

#define MAX_NAME_SIZE	64
#define INVALID_BL_VALUE 20190909

#define DSI_CLOCK_BITRATE_RADIX 10
#define MAX_TE_SOURCE_ID  2

#define WU_SEED_REGISTER 0x67
#define UG_SEED_REGISTER 0xB1

static char dsi_display_primary[MAX_CMDLINE_PARAM_LEN];
static char dsi_display_secondary[MAX_CMDLINE_PARAM_LEN];
static struct dsi_display_boot_param boot_displays[MAX_DSI_ACTIVE_DISPLAY] = {
	{.boot_param = dsi_display_primary},
	{.boot_param = dsi_display_secondary},
};

static const struct of_device_id dsi_display_dt_match[] = {
	{.compatible = "qcom,dsi-display"},
	{}
};

static int esd_black_count;
static int esd_greenish_count;
static struct dsi_display *primary_display;
static char reg_read_value[128] = {0};
int reg_read_len = 1;
EXPORT_SYMBOL(reg_read_len);

#define to_dsi_bridge(x)  container_of((x), struct dsi_bridge, base)

char buf_Lotid[6];

typedef enum {
	ToolB = 0,
	ToolA = 1,
	ToolA_HVS30 = 2,
	Tool_Green = 10,
} eTool;

typedef struct {
	char LotID[6];
	int wafer_Start;
	int wafer_End;
	int HVS30;
} LotDBItem;

LotDBItem ANA6705_ToolA_DB[109] = {
	{"K2T7N", 0, 0, 0},
	{"K2T7P", 0, 0, 0},
	{"K2TK4", 0, 0, 0},
	{"K4ART", 1, 12, 1},
	{"K4C07", 0, 0, 1},
	{"K4C0A", 1, 12, 1},
	{"K4C7S", 0, 0, 1},
	{"K4C7T", 0, 0, 1},
	{"K4CCH", 0, 0, 1},
	{"K4CCN", 0, 0, 1},
	{"K4CCP", 0, 0, 1},
	{"K4CJL", 0, 0, 1},
	{"K4CNS", 0, 0, 1},
	{"K4C06", 0, 0, 1},
	{"K4CNW", 0, 0, 1},
	{"K4JGT", 0, 0, 1},
	{"K4F8J", 0, 0, 1},
	{"K4FFA", 0, 0, 1},
	{"K4F4G", 0, 0, 1},
	{"K4C82", 0, 0, 1},
	{"K4CJM", 0, 0, 1},
	{"K4CNT", 0, 0, 1},
	{"K4F0T", 0, 0, 1},
	{"K4F4K", 0, 0, 1},
	{"K4F4N", 0, 0, 1},
	{"K4JA8", 0, 0, 1},
	{"K4JA8", 0, 0, 1},
	{"K4J54", 0, 0, 1},
	{"K4F4P", 0, 0, 1},
	{"K4M9N", 0, 0, 1},
	{"K4J6F", 0, 0, 1},
	{"K4FFC", 0, 0, 1},
	{"K4JQP", 0, 0, 1},
	{"K4K5A", 0, 0, 1},
	{"K4K19", 0, 0, 1},
	{"K4K7L", 0, 0, 1},
	{"K4JW4", 0, 0, 1},
	{"K4MGK", 0, 0, 1},
	{"K4KTR", 0, 0, 1},
	{"K4L07", 0, 0, 1},
	{"K4L07", 0, 0, 1},
	{"K4MGJ", 0, 0, 1},
	{"K4JLA", 0, 0, 1},
	{"K4KTS", 0, 0, 1},
	{"K4MGL", 0, 0, 1},
	{"K4JJS", 0, 0, 1},
	{"K4PYR", 0, 0, 1},
	{"K4PS4", 0, 0, 1},
	{"K4QC2", 0, 0, 1},
	{"K4Q7K", 0, 0, 1},
	{"K4PS5", 0, 0, 1},
	{"K4Q3Q", 0, 0, 1},
	{"K4Q3R", 0, 0, 1},
	{"K4QC0", 0, 0, 1},
	{"K4QHT", 0, 0, 1},
	{"K4QC1", 0, 0, 1},
	{"K4QHW", 0, 0, 1},
	{"K4QMP", 0, 0, 1},
	{"K4QMQ", 0, 0, 1},
	{"K4QMR", 0, 0, 1},
	{"K4Q7L", 0, 0, 1},
	{"K4QRL", 0, 0, 1},
	{"K4QYM", 0, 0, 1},
	{"K4PYQ", 0, 0, 1},
	{"K4QYN", 0, 0, 1},
	{"K4R7A", 0, 0, 1},
	{"K4QRM", 0, 0, 1},
	{"K4R7F", 0, 0, 1},
	{"K4R3L", 0, 0, 1},
	{"K4QYP", 0, 0, 1},
	{"K4R3K", 0, 0, 1},
	{"K4RJ7", 0, 0, 1},
	{"K4R7C", 0, 0, 1},
	{"K4RC8", 0, 0, 1},
	{"K4RNW", 0, 0, 1},
	{"K4RS4", 0, 0, 1},
	{"K4RC9", 0, 0, 1},
	{"K4RJ8", 0, 0, 1},
	{"K4RNS", 0, 0, 1},
	{"K4RNT", 0, 0, 1},
	{"K4RS5", 0, 0, 1},
	{"K4RYL", 0, 0, 1},
	{"K4RYM", 0, 0, 1},
	{"K4S1S", 0, 0, 1},
	{"K4S78", 0, 0, 1},
	{"K4SAY", 0, 0, 1},
	{"K4SHS", 0, 0, 1},
	{"K4SHT", 0, 0, 1},
	{"K4S1T", 0, 0, 1},
	{"K4S77", 0, 0, 1},
	{"K4SC1", 0, 0, 1},
	{"K4SMM", 0, 0, 1},
	{"K4SC0", 0, 0, 1},
	{"K4SRA", 0, 0, 1},
	{"K4TAM", 0, 0, 1},
	{"K4TAN", 0, 0, 1},
	{"K5G14", 0, 0, 1},
	{"K5G16", 0, 0, 1},
	{"K5G15", 0, 0, 1},
	{"K5G4W", 0, 0, 1},
	{"K5G4Y", 0, 0, 1},
	{"K5G8W", 0, 0, 1},
	{"K5G8Y", 0, 0, 1},
	{"K5GFS", 0, 0, 1},
	{"K5GFT", 0, 0, 1},
	{"K5GFW", 0, 0, 1},
	{"K5GL5", 0, 0, 1},
	{"K5GL6", 0, 0, 1},
	{"K5GNY", 0, 0, 1}
};

LotDBItem ANA6705_ToolB_DB[164] = {
	{"K2T30", 0, 0, 0},
	{"K2T2Y", 0, 0, 0},
	{"K2T35", 0, 0, 0},
	{"K2WJ7", 0, 0, 0},
	{"K2WJ7", 0, 0, 0},
	{"K2TPW", 0, 0, 0},
	{"K2W76", 0, 0, 0},
	{"K2TW7", 0, 0, 0},
	{"K2WJ6", 0, 0, 0},
	{"K2WJ6", 0, 0, 0},
	{"K2T7N", 0, 0, 0},
	{"K2T7P", 0, 0, 0},
	{"K2TK3", 0, 0, 0},
	{"K2TK4", 0, 0, 0},
	{"K2TPY", 0, 0, 0},
	{"K2TQ0", 0, 0, 0},
	{"K2TQ1", 0, 0, 0},
	{"K2TW8", 0, 0, 0},
	{"K2TW9", 0, 0, 0},
	{"K2TWA", 0, 0, 0},
	{"K2W2K", 0, 0, 0},
	{"K2W2L", 0, 0, 0},
	{"K2W2M", 0, 0, 0},
	{"K2W2N", 0, 0, 0},
	{"K2W75", 0, 0, 0},
	{"K2W77", 0, 0, 0},
	{"K2W78", 0, 0, 0},
	{"K2WCF", 0, 0, 0},
	{"K2WCG", 0, 0, 0},
	{"K2WCH", 0, 0, 0},
	{"K2WCJ", 0, 0, 0},
	{"K2WJ8", 0, 0, 0},
	{"K2WJ9", 0, 0, 0},
	{"K2WN8", 0, 0, 0},
	{"K2WN9", 0, 0, 0},
	{"K2WNC", 0, 0, 0},
	{"K2WNF", 0, 0, 0},
	{"K2WNH", 0, 0, 0},
	{"K2WSF", 0, 0, 0},
	{"K2WSG", 0, 0, 0},
	{"K2WSH", 0, 0, 0},
	{"K2WSJ", 0, 0, 0},
	{"K2WSK", 0, 0, 0},
	{"K2WSL", 0, 0, 0},
	{"K2Y14", 0, 0, 0},
	{"K2Y15", 0, 0, 0},
	{"K2Y16", 0, 0, 0},
	{"K2Y17", 0, 0, 0},
	{"K2Y18", 0, 0, 0},
	{"K2Y19", 0, 0, 0},
	{"K3C74", 0, 0, 0},
	{"K3FK0", 0, 0, 0},
	{"K2TF5", 0, 0, 0},
	{"K2TF7", 0, 0, 0},
	{"K2TK1", 0, 0, 0},
	{"K2TK2", 0, 0, 0},
	{"K2WNA", 0, 0, 0},
	{"K2Y13", 0, 0, 0},
	{"K4A0F", 0, 0, 0},
	{"K4A3C", 0, 0, 0},
	{"K3YSJ", 0, 0, 0},
	{"K4A6L", 0, 0, 0},
	{"K4A6M", 0, 0, 0},
	{"K4A83", 0, 0, 0},
	{"K4A83", 0, 0, 0},
	{"K4A83", 0, 0, 0},
	{"K4A80", 0, 0, 0},
	{"K3WYS", 0, 0, 0},
	{"K4AMP", 0, 0, 0},
	{"K3J0A", 0, 0, 0},
	{"K3TQY", 0, 0, 0},
	{"K4A6N", 0, 0, 0},
	{"K4A81", 0, 0, 0},
	{"K4A82", 0, 0, 0},
	{"K4AF1", 0, 0, 0},
	{"K4ALM", 0, 0, 0},
	{"K4ALN", 0, 0, 0},
	{"K4ALS", 0, 0, 0},
	{"K4ALT", 0, 0, 0},
	{"K4ALT", 0, 0, 0},
	{"K4ALW", 0, 0, 0},
	{"K4AMQ", 0, 0, 0},
	{"K4AMY", 0, 0, 0},
	{"K4AS0", 0, 0, 0},
	{"K3T2T", 0, 0, 0},
	{"K4ALR", 0, 0, 0},
	{"K4ART", 13, 25, 0},
	{"K4ARW", 0, 0, 0},
	{"K4AS2", 0, 0, 0},
	{"K4C4A", 0, 0, 0},
	{"K2T36", 0, 0, 0},
	{"K2T37", 0, 0, 0},
	{"K2T7M", 0, 0, 0},
	{"K3HT4", 0, 0, 0},
	{"K3PRW", 0, 0, 0},
	{"K4A84", 0, 0, 0},
	{"K4AF2", 0, 0, 0},
	{"K4ALQ", 0, 0, 0},
	{"K4AMT", 0, 0, 0},
	{"K4AS1", 0, 0, 0},
	{"K4AS3", 0, 0, 0},
	{"K4C04", 0, 0, 0},
	{"K4C05", 0, 0, 0},
	{"K4C08", 0, 0, 0},
	{"K4C0A", 13, 25, 0},
	{"K4C46", 0, 0, 0},
	{"K4C7R", 0, 0, 0},
	{"K4CJJ", 0, 0, 0},
	{"K4CSW", 0, 0, 0},
	{"K4FQW", 0, 0, 0},
	{"K4ARY", 0, 0, 0},
	{"K4ARY", 0, 0, 0},
	{"K4AMR", 0, 0, 0},
	{"K4CJH", 0, 0, 0},
	{"K4CP2", 0, 0, 0},
	{"K4CSS", 0, 0, 0},
	{"K4CT2", 0, 0, 0},
	{"K4CT3", 0, 0, 0},
	{"K4F0P", 0, 0, 0},
	{"K4F0Q", 0, 0, 0},
	{"K4F0R", 0, 0, 0},
	{"K4F0S", 0, 0, 0},
	{"K4F0W", 0, 0, 0},
	{"K4F0Y", 0, 0, 0},
	{"K4F10", 0, 0, 0},
	{"K4F4H", 0, 0, 0},
	{"K4F4J", 0, 0, 0},
	{"K4F4L", 0, 0, 0},
	{"K4F8G", 0, 0, 0},
	{"K4F8H", 0, 0, 0},
	{"K4F8L", 0, 0, 0},
	{"K4FQT", 0, 0, 0},
	{"K4GLQ", 0, 0, 0},
	{"K4GLR", 0, 0, 0},
	{"K4FFF", 0, 0, 0},
	{"K4FFG", 0, 0, 0},
	{"K2WNG", 0, 0, 0},
	{"K2WSC", 9, 25, 0},
	{"K4C44", 0, 0, 0},
	{"K4CCJ", 0, 0, 0},
	{"K4CT0", 0, 0, 0},
	{"K4CT1", 0, 0, 0},
	{"K4CP1", 0, 0, 0},
	{"K4CSY", 0, 0, 0},
	{"K4F8F", 0, 0, 0},
	{"K4C09", 0, 0, 0},
	{"K4C06", 13, 25, 0},
	{"K4C47", 0, 0, 0},
	{"K4C48", 0, 0, 0},
	{"K4C49", 0, 0, 0},
	{"K4C7W", 0, 0, 0},
	{"K4C80", 0, 0, 0},
	{"K4C81", 0, 0, 0},
	{"K4CCK", 0, 0, 0},
	{"K4CCL", 0, 0, 0},
	{"K4CCM", 0, 0, 0},
	{"K4CCQ", 0, 0, 0},
	{"K4CJC", 0, 0, 0},
	{"K4CJF", 0, 0, 0},
	{"K4CJG", 0, 0, 0},
	{"K4CJK", 0, 0, 0},
	{"K4CNY", 0, 0, 0},
	{"K4CP0", 0, 0, 0},
	{"K4CP3", 0, 0, 0}
};

LotDBItem ANA6706_ToolA_DB[121] = {
	{"K4AN0", 1, 12, 0},
	{"K4AJG", 1, 12, 0},
	{"K4AS4", 1, 12, 0},
	{"K4H99", 0, 0, 0},
	{"K4C4C", 0, 0, 1},
	{"K4H9A", 0, 0, 1},
	{"K4HAC", 0, 0, 1},
	{"K4J55", 0, 0, 1},
	{"K4HAC", 0, 0, 1},
	{"K4HM2", 0, 0, 1},
	{"K4HPW", 0, 0, 1},
	{"K4HYW", 0, 0, 1},
	{"K4J56", 0, 0, 1},
	{"K4J6G", 0, 0, 1},
	{"K4J6H", 0, 0, 1},
	{"K4J6J", 0, 0, 1},
	{"K4JA9", 0, 0, 1},
	{"K4JAA", 0, 0, 1},
	{"K4JLH", 0, 0, 1},
	{"K4JQR", 0, 0, 1},
	{"K4JLG", 0, 0, 1},
	{"K4HJ0", 0, 0, 1},
	{"K4JAF", 0, 0, 1},
	{"K4JGW", 0, 0, 1},
	{"K4JGY", 0, 0, 1},
	{"K4JLF", 0, 0, 1},
	{"K4J29", 0, 0, 1},
	{"K4JAC", 0, 0, 1},
	{"K4JH0", 0, 0, 1},
	{"K4JW7", 0, 0, 1},
	{"K4HS4", 0, 0, 1},
	{"K4HYY", 0, 0, 1},
	{"K4K5G", 0, 0, 1},
	{"K4JLC", 0, 0, 1},
	{"K4KL8", 0, 0, 1},
	{"K4K1G", 0, 0, 1},
	{"K4K5C", 0, 0, 1},
	{"K4JQQ", 0, 0, 1},
	{"K4KG8", 0, 0, 1},
	{"K4KQL", 0, 0, 1},
	{"K4KTT", 0, 0, 1},
	{"K4KG9", 0, 0, 1},
	{"K4L5G", 0, 0, 1},
	{"K4K1C", 0, 0, 1},
	{"K4K5F", 0, 0, 1},
	{"K4K9L", 0, 0, 1},
	{"K4KG6", 0, 0, 1},
	{"K4KQK", 0, 0, 1},
	{"K4KG9", 0, 0, 1},
	{"K4JQS", 0, 0, 1},
	{"K4JW5", 0, 0, 1},
	{"K4KG7", 0, 0, 1},
	{"K4KL9", 0, 0, 1},
	{"K4K9H", 0, 0, 1},
	{"K4L9G", 0, 0, 1},
	{"K4K5H", 0, 0, 1},
	{"K4K9J", 0, 0, 1},
	{"K4K9K", 0, 0, 1},
	{"K4KLA", 0, 0, 1},
	{"K4L1J", 0, 0, 1},
	{"K4L1K", 0, 0, 1},
	{"K4L1L", 0, 0, 1},
	{"K4L5H", 0, 0, 1},
	{"K4L5J", 0, 0, 1},
	{"K4L9H", 0, 0, 1},
	{"K4L9J", 0, 0, 1},
	{"K4LGA", 0, 0, 1},
	{"K4LGC", 0, 0, 1},
	{"K4LKY", 0, 0, 1},
	{"K4LL0", 0, 0, 1},
	{"K4LL1", 0, 0, 1},
	{"K4LPQ", 0, 0, 1},
	{"K4LPR", 0, 0, 1},
	{"K4LPS", 0, 0, 1},
	{"K4LTP", 0, 0, 1},
	{"K4LTQ", 0, 0, 1},
	{"K4LTR", 0, 0, 1},
	{"K4M1F", 0, 0, 1},
	{"K4M1G", 0, 0, 1},
	{"K4M5M", 0, 0, 1},
	{"K4M5N", 0, 0, 1},
	{"K4M5P", 0, 0, 1},
	{"K4M9P", 0, 0, 1},
	{"K4M9Q", 0, 0, 1},
	{"K4MLL", 0, 0, 1},
	{"K4MLM", 0, 0, 1},
	{"K4MLN", 0, 0, 1},
	{"K4MQY", 0, 0, 1},
	{"K4MR0", 0, 0, 1},
	{"K4MWS", 0, 0, 1},
	{"K4MWT", 0, 0, 1},
	{"K4MWW", 0, 0, 1},
	{"K4N2K", 0, 0, 1},
	{"K4N2L", 0, 0, 1},
	{"K4N66", 0, 0, 1},
	{"K4N67", 0, 0, 1},
	{"K4N68", 0, 0, 1},
	{"K4NPW", 0, 0, 1},
	{"K4NPY", 0, 0, 1},
	{"K4NQ0", 0, 0, 1},
	{"K4NTS", 0, 0, 1},
	{"K4NTT", 0, 0, 1},
	{"K4NTW", 0, 0, 1},
	{"K4P1F", 0, 0, 1},
	{"K4P1G", 0, 0, 1},
	{"K4P1H", 0, 0, 1},
	{"K4P51", 0, 0, 1},
	{"K4P52", 0, 0, 1},
	{"K4P53", 0, 0, 1},
	{"K4P8M", 0, 0, 1},
	{"K4P8N", 0, 0, 1},
	{"K4SMN", 0, 0, 1},
	{"K4SMP", 0, 0, 1},
	{"K4SRC", 0, 0, 1},
	{"K4SRF", 0, 0, 1},
	{"K4SY4", 0, 0, 1},
	{"K4SY5", 0, 0, 1},
	{"K4T6T", 0, 0, 1},
	{"K4T6W", 0, 0, 1},
	{"K4TAP", 0, 0, 1},
	{"K4TAQ", 0, 0, 1}
};

LotDBItem ANA6706_ToolB_DB[8] = {
	{"K4A6P", 0, 0, 0},
	{"K4C0C", 0, 0, 0},
	{"K4A85", 0, 0, 0},
	{"K4AF3", 0, 0, 0},
	{"K4AN0", 13, 25, 0},
	{"K4AJG", 13, 25, 0},
	{"K4AS4", 13, 24, 0},
	{"K4HAR", 0, 0, 0},
};

LotDBItem ANA6706_Green[18] = {
	{"K4C4C", 0, 0 ,0},
	{"K4H9A", 0, 0 ,0},
	{"K4HAR", 0, 0 ,0},
	{"K4HJ0", 0, 0 ,0},
	{"K4HM2", 0, 0 ,0},
	{"K4HM3", 0, 0 ,0},
	{"K4HPW", 0, 0 ,0},
	{"K4HS4", 0, 0 ,0},
	{"K4HYW", 0, 0 ,0},
	{"K4HYY", 0, 0 ,0},
	{"K4J29", 0, 0 ,0},
	{"K4J55", 0, 0 ,0},
	{"K4J6H", 0, 0 ,0},
	{"K4J6J", 0, 0 ,0},
	{"K4JAA", 0, 0 ,0},
	{"K4JAC", 0, 0 ,0},
	{"K4JH0", 0, 0 ,0},
	{"K4JW8", 0, 0 ,0},
};

void extractLotID(unsigned char* chipID, char *szLotID)
{
	int i;
	unsigned long lotValue = (chipID[0] << 14) + (chipID[1] << 6) + (chipID[2] >> 2);

	szLotID[0] = 'K';
	szLotID[1] = ((long)(lotValue / (36 * 36 * 36)) % 36) + 'A';

	szLotID[2] = ((long)(lotValue / (36 * 36)) % 36) + 'A';
	szLotID[3] = ((long)(lotValue / 36) % 36) + 'A';
	szLotID[4] = (lotValue % 36) + 'A';

	for (i = 1; i < 5; i++) {
		if (szLotID[i] > 90)
			szLotID[i] = (szLotID[i] - 91) + '0';
	}
}

int extractWaferNumber(unsigned char* chipID)
{
	int noWafer;
	noWafer = ((chipID[2] & 0x03) << 3) + (chipID[3] >> 5);
	return noWafer;
}

eTool discrimination_ANA6705_ToolsType(char* szLotID, int WaferNumber)
{
	int i;
	int count = sizeof(ANA6705_ToolA_DB) / sizeof(LotDBItem);
	bool bFound = false;
	eTool toolType;

	for (i = 0; i < count; i++) {
		if (strncmp(szLotID, ANA6705_ToolA_DB[i].LotID, 5) == 0) {
			if (ANA6705_ToolA_DB[i].wafer_Start > 0) {
				if (WaferNumber >= ANA6705_ToolA_DB[i].wafer_Start && WaferNumber <= ANA6705_ToolA_DB[i].wafer_End) {
					bFound = true;
					if (ANA6705_ToolA_DB[i].HVS30)
						toolType = ToolA_HVS30;
					else
						toolType = ToolA;
				}
				break;
			} else {
				bFound = true;
				if (ANA6705_ToolA_DB[i].HVS30)
					toolType = ToolA_HVS30;
				else
					toolType = ToolA;
				break;
			}
		}
	}

	if (bFound == false)
		toolType = ToolB;

	return toolType;
}

eTool discrimination_ANA6706_ToolsType(char* szLotID, int WaferNumber)
{
	int i;
	int count = sizeof(ANA6706_ToolA_DB) / sizeof(LotDBItem);
	bool bFound = false;
	eTool toolType;

	for (i = 0; i < count; i++) {
		if (strncmp(szLotID, ANA6706_ToolA_DB[i].LotID, 5) == 0) {
			if (ANA6706_ToolA_DB[i].wafer_Start > 0) {
				if (WaferNumber >= ANA6706_ToolA_DB[i].wafer_Start && WaferNumber <= ANA6706_ToolA_DB[i].wafer_End) {
					bFound = true;
					if (ANA6706_ToolA_DB[i].HVS30)
						toolType = ToolA_HVS30;
					else
						toolType = ToolA;
				}
				break;
			} else {
				bFound = true;
				if (ANA6706_ToolA_DB[i].HVS30)
					toolType = ToolA_HVS30;
				else
					toolType = ToolA;
				break;
			}
		}
	}

	if (bFound == false)
		toolType = ToolB;

	return toolType;
}

int dsi_display_back_ToolsType_ANA6706(u8 *buff)
{
	int i;
	int WaferNumber;
	eTool typeTool;
	char szLotID[6] = {0};
	unsigned char chipID1[4] = {0};

	for(i = 0; i < 4; i++)
		chipID1[i] = buff[i];

	// [6706] Chip IDLot IDWafer Number
	extractLotID(chipID1, szLotID);
	memcpy(buf_Lotid, szLotID, 6);
	WaferNumber = extractWaferNumber(chipID1);

	// LotID Wafer Number Tool Type
	typeTool = discrimination_ANA6706_ToolsType(szLotID, WaferNumber);

	if (typeTool == ToolB)
		DSI_ERR("Result: 6706 LotID: %s WaferNo: %d, Tool: Tool-B (%d)\n", szLotID, WaferNumber, typeTool);
	else if (typeTool == ToolA)
		DSI_ERR("Result: 6706 LotID: %s WaferNo: %d, Tool: Tool-A (%d)\n", szLotID, WaferNumber, typeTool);
	else if (typeTool == ToolA_HVS30)
		DSI_ERR("Result: 6706 LotID: %s WaferNo: %d, Tool: Tool-A HVS 3.0 (%d)\n", szLotID, WaferNumber, typeTool);

	for (i = 0; i < 18; i++) {
		if((strcmp(szLotID, ANA6706_Green[i].LotID) == 0)) {
			typeTool = Tool_Green;
			break;
		}
	}

	return typeTool;
}

static void dsi_display_mask_ctrl_error_interrupts(struct dsi_display *display,
			u32 mask, bool enable)
{
	int i;
	struct dsi_display_ctrl *ctrl;

	if (!display)
		return;

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl)
			continue;
		dsi_ctrl_mask_error_status_interrupts(ctrl->ctrl, mask, enable);
	}
}

static int dsi_display_config_clk_gating(struct dsi_display *display,
					bool enable)
{
	int rc = 0, i = 0;
	struct dsi_display_ctrl *mctrl, *ctrl;
	enum dsi_clk_gate_type clk_selection;
	enum dsi_clk_gate_type const default_clk_select = PIXEL_CLK | DSI_PHY;

	if (!display) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (display->panel->host_config.force_hs_clk_lane) {
		DSI_DEBUG("no dsi clock gating for continuous clock mode\n");
		return 0;
	}

	mctrl = &display->ctrl[display->clk_master_idx];
	if (!mctrl) {
		DSI_ERR("Invalid controller\n");
		return -EINVAL;
	}

	clk_selection = display->clk_gating_config;

	if (!enable) {
		/* for disable path, make sure to disable all clk gating */
		clk_selection = DSI_CLK_ALL;
	} else if (!clk_selection || clk_selection > DSI_CLK_NONE) {
		/* Default selection, no overrides */
		clk_selection = default_clk_select;
	} else if (clk_selection == DSI_CLK_NONE) {
		clk_selection = 0;
	}

	DSI_DEBUG("%s clock gating Byte:%s Pixel:%s PHY:%s\n",
		enable ? "Enabling" : "Disabling",
		clk_selection & BYTE_CLK ? "yes" : "no",
		clk_selection & PIXEL_CLK ? "yes" : "no",
		clk_selection & DSI_PHY ? "yes" : "no");
	rc = dsi_ctrl_config_clk_gating(mctrl->ctrl, enable, clk_selection);
	if (rc) {
		DSI_ERR("[%s] failed to %s clk gating for clocks %d, rc=%d\n",
				display->name, enable ? "enable" : "disable",
				clk_selection, rc);
		return rc;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || (ctrl == mctrl))
			continue;
		/**
		 * In Split DSI usecase we should not enable clock gating on
		 * DSI PHY1 to ensure no display atrifacts are seen.
		 */
		clk_selection &= ~DSI_PHY;
		rc = dsi_ctrl_config_clk_gating(ctrl->ctrl, enable,
				clk_selection);
		if (rc) {
			DSI_ERR("[%s] failed to %s clk gating for clocks %d, rc=%d\n",
				display->name, enable ? "enable" : "disable",
				clk_selection, rc);
			return rc;
		}
	}

	return 0;
}

static void dsi_display_set_ctrl_esd_check_flag(struct dsi_display *display,
			bool enable)
{
	int i;
	struct dsi_display_ctrl *ctrl;

	if (!display)
		return;

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl)
			continue;
		ctrl->ctrl->esd_check_underway = enable;
	}
}

static void dsi_display_ctrl_irq_update(struct dsi_display *display, bool en)
{
	int i;
	struct dsi_display_ctrl *ctrl;

	if (!display)
		return;

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl)
			continue;
		dsi_ctrl_irq_update(ctrl->ctrl, en);
	}
}

void dsi_rect_intersect(const struct dsi_rect *r1,
		const struct dsi_rect *r2,
		struct dsi_rect *result)
{
	int l, t, r, b;

	if (!r1 || !r2 || !result)
		return;

	l = max(r1->x, r2->x);
	t = max(r1->y, r2->y);
	r = min((r1->x + r1->w), (r2->x + r2->w));
	b = min((r1->y + r1->h), (r2->y + r2->h));

	if (r <= l || b <= t) {
		memset(result, 0, sizeof(*result));
	} else {
		result->x = l;
		result->y = t;
		result->w = r - l;
		result->h = b - t;
	}
}

extern int aod_layer_hide;
int dsi_display_set_backlight(struct drm_connector *connector,
		void *display, u32 bl_lvl)
{
	struct dsi_display *dsi_display = display;
	struct dsi_panel *panel;
	u32 bl_scale, bl_scale_sv;
	u64 bl_temp;
	int rc = 0;
	static int gamma_read_flag;

	if (dsi_display == NULL || dsi_display->panel == NULL)
		return -EINVAL;

	panel = dsi_display->panel;

	mutex_lock(&panel->panel_lock);
	if (!dsi_panel_initialized(panel)) {
		if (bl_lvl != INVALID_BL_VALUE) {
			panel->hbm_backlight = bl_lvl;
			panel->bl_config.bl_level = bl_lvl;
		}
		DSI_ERR("hbm_backlight = %d\n", panel->hbm_backlight);
		rc = -EINVAL;
		goto error;
	}

	if (bl_lvl != 0 && bl_lvl != INVALID_BL_VALUE && panel->bl_config.bl_level == 0) {
		if (strcmp(dsi_display->panel->name, "samsung dsc cmd mode oneplus dsi panel") == 0) {
			if (panel->naive_display_p3_mode) {
				mdelay(20);
				dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NATIVE_DISPLAY_P3_ON);
				DSI_ERR("Send DSI_CMD_SET_NATIVE_DISPLAY_P3_ON cmds\n");
			}
			if (panel->naive_display_wide_color_mode) {
				mdelay(20);
				dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NATIVE_DISPLAY_WIDE_COLOR_ON);
				DSI_ERR("Send DSI_CMD_SET_NATIVE_DISPLAY_WIDE_COLOR_ON cmds\n");
			}
			if (panel->naive_display_srgb_color_mode) {
				mdelay(20);
				dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NATIVE_DISPLAY_SRGB_COLOR_ON);
				DSI_ERR("Send DSI_CMD_SET_NATIVE_DISPLAY_SRGB_COLOR_ON cmds\n");
			}
			if (panel->naive_display_customer_srgb_mode) {
				dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_CUSTOMER_RGB_ON);
				DSI_ERR("Send DSI_CMD_LOADING_CUSTOMER_RGB_ON cmds\n");
			}
			if (panel->naive_display_customer_p3_mode) {
				dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_CUSTOMER_P3_ON);
				DSI_ERR("Send DSI_CMD_LOADING_CUSTOMER_P3_ON cmds\n");
			}
		} else if (strcmp(dsi_display->panel->name, "samsung sofef03f_m fhd cmd mode dsc dsi panel") == 0) {
			if (panel->naive_display_p3_mode) {
				msleep(20);
				dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NATIVE_DISPLAY_P3_ON);
				DSI_ERR("Send DSI_CMD_SET_NATIVE_DISPLAY_P3_ON cmds\n");
			}
			if (panel->naive_display_wide_color_mode) {
				msleep(20);
				dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NATIVE_DISPLAY_WIDE_COLOR_ON);
				DSI_ERR("Send DSI_CMD_SET_NATIVE_DISPLAY_WIDE_COLOR_ON cmds\n");
			}
			if (panel->naive_display_srgb_color_mode) {
				msleep(20);
				dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NATIVE_DISPLAY_SRGB_COLOR_ON);
				DSI_ERR("Send DSI_CMD_SET_NATIVE_DISPLAY_SRGB_COLOR_ON cmds\n");
			}
			if (panel->naive_display_customer_srgb_mode) {
				dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_CUSTOMER_RGB_ON);
				DSI_ERR("Send DSI_CMD_LOADING_CUSTOMER_RGB_ON cmds\n");
			} else {
				dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_CUSTOMER_RGB_OFF);
				DSI_ERR("Send DSI_CMD_LOADING_CUSTOMER_RGB_OFF cmds\n");
			}
			if (panel->naive_display_customer_p3_mode) {
				dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_CUSTOMER_RGB_OFF);
				DSI_ERR("Send DSI_CMD_LOADING_CUSTOMER_RGB_OFF cmds\n");
			} else {
				dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_CUSTOMER_P3_OFF);
				DSI_ERR("Send DSI_CMD_LOADING_CUSTOMER_P3_OFF cmds\n");
			}
		}
	}

	if (bl_lvl != INVALID_BL_VALUE)
		panel->bl_config.bl_level = bl_lvl;
	else
		bl_lvl = panel->bl_config.bl_level;
	/* scale backlight */
	bl_scale = panel->bl_config.bl_scale;
	bl_temp = bl_lvl * bl_scale / MAX_BL_SCALE_LEVEL;

	bl_scale_sv = panel->bl_config.bl_scale_sv;
	bl_temp = (u32)bl_temp * bl_scale_sv / MAX_SV_BL_SCALE_LEVEL;

	DSI_DEBUG("bl_scale = %u, bl_scale_sv = %u, bl_lvl = %u\n",
		bl_scale, bl_scale_sv, (u32)bl_temp);
	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_set_backlight(panel, (u32)bl_temp);
	if (rc)
		DSI_ERR("unable to set backlight\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}

error:
	mutex_unlock(&panel->panel_lock);

	if ((0 == panel->panel_serial_number) &&
		(strcmp(dsi_display->panel->name, "samsung sofef00_m video mode dsi panel") != 0)) {
		dsi_display_get_serial_number(connector);
	}

	if ((gamma_read_flag < 2) && ((strcmp(dsi_display->panel->name, "samsung dsc cmd mode oneplus dsi panel") == 0)
		|| (strcmp(dsi_display->panel->name, "samsung ana6706 dsc cmd mode panel") == 0)
			|| (strcmp(dsi_display->panel->name, "samsung amb655x fhd cmd mode dsc dsi panel") == 0))) {
		if (gamma_read_flag < 1) {
			gamma_read_flag++;
		}
		else {
			schedule_delayed_work(&dsi_display->panel->gamma_read_work, 0);
			gamma_read_flag++;
		}
	}

	return rc;
}

int dsi_display_cmd_engine_enable(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *m_ctrl, *ctrl;

	m_ctrl = &display->ctrl[display->cmd_master_idx];
	mutex_lock(&m_ctrl->ctrl->ctrl_lock);

	if (display->cmd_engine_refcount > 0) {
		display->cmd_engine_refcount++;
		goto done;
	}

	rc = dsi_ctrl_set_cmd_engine_state(m_ctrl->ctrl, DSI_CTRL_ENGINE_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable cmd engine, rc=%d\n",
		       display->name, rc);
		goto done;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || (ctrl == m_ctrl))
			continue;

		rc = dsi_ctrl_set_cmd_engine_state(ctrl->ctrl,
						   DSI_CTRL_ENGINE_ON);
		if (rc) {
			DSI_ERR("[%s] failed to enable cmd engine, rc=%d\n",
			       display->name, rc);
			goto error_disable_master;
		}
	}

	display->cmd_engine_refcount++;
	goto done;
error_disable_master:
	(void)dsi_ctrl_set_cmd_engine_state(m_ctrl->ctrl, DSI_CTRL_ENGINE_OFF);
done:
	mutex_unlock(&m_ctrl->ctrl->ctrl_lock);
	return rc;
}

#if defined(CONFIG_PXLW_IRIS)
int iris_display_cmd_engine_enable(struct dsi_display *display)
{
	return dsi_display_cmd_engine_enable(display);
}
#endif

int dsi_display_cmd_engine_disable(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *m_ctrl, *ctrl;

	m_ctrl = &display->ctrl[display->cmd_master_idx];
	mutex_lock(&m_ctrl->ctrl->ctrl_lock);

	if (display->cmd_engine_refcount == 0) {
		DSI_ERR("[%s] Invalid refcount\n", display->name);
		goto done;
	} else if (display->cmd_engine_refcount > 1) {
		display->cmd_engine_refcount--;
		goto done;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || (ctrl == m_ctrl))
			continue;

		rc = dsi_ctrl_set_cmd_engine_state(ctrl->ctrl,
						   DSI_CTRL_ENGINE_OFF);
		if (rc)
			DSI_ERR("[%s] failed to enable cmd engine, rc=%d\n",
			       display->name, rc);
	}

	rc = dsi_ctrl_set_cmd_engine_state(m_ctrl->ctrl, DSI_CTRL_ENGINE_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to enable cmd engine, rc=%d\n",
		       display->name, rc);
		goto error;
	}

error:
	display->cmd_engine_refcount = 0;
done:
	mutex_unlock(&m_ctrl->ctrl->ctrl_lock);
	return rc;
}
#if defined(CONFIG_PXLW_IRIS)
int iris_display_cmd_engine_disable(struct dsi_display *display)
{
	return dsi_display_cmd_engine_disable(display);
}
#endif

static void dsi_display_aspace_cb_locked(void *cb_data, bool is_detach)
{
	struct dsi_display *display;
	struct dsi_display_ctrl *display_ctrl;
	int rc, cnt;

	if (!cb_data) {
		DSI_ERR("aspace cb called with invalid cb_data\n");
		return;
	}
	display = (struct dsi_display *)cb_data;

	/*
	 * acquire panel_lock to make sure no commands are in-progress
	 * while detaching the non-secure context banks
	 */
	dsi_panel_acquire_panel_lock(display->panel);

	if (is_detach) {
		/* invalidate the stored iova */
		display->cmd_buffer_iova = 0;

		/* return the virtual address mapping */
		msm_gem_put_vaddr(display->tx_cmd_buf);
		msm_gem_vunmap(display->tx_cmd_buf, OBJ_LOCK_NORMAL);

	} else {
		rc = msm_gem_get_iova(display->tx_cmd_buf,
				display->aspace, &(display->cmd_buffer_iova));
		if (rc) {
			DSI_ERR("failed to get the iova rc %d\n", rc);
			goto end;
		}

		display->vaddr =
			(void *) msm_gem_get_vaddr(display->tx_cmd_buf);

		if (IS_ERR_OR_NULL(display->vaddr)) {
			DSI_ERR("failed to get va rc %d\n", rc);
			goto end;
		}
	}

	display_for_each_ctrl(cnt, display) {
		display_ctrl = &display->ctrl[cnt];
		display_ctrl->ctrl->cmd_buffer_size = display->cmd_buffer_size;
		display_ctrl->ctrl->cmd_buffer_iova = display->cmd_buffer_iova;
		display_ctrl->ctrl->vaddr = display->vaddr;
		display_ctrl->ctrl->secure_mode = is_detach;
	}

end:
	/* release panel_lock */
	dsi_panel_release_panel_lock(display->panel);
}

static irqreturn_t dsi_display_panel_err_flag_irq_handler(int irq, void *data)
{
	struct dsi_display *display = (struct dsi_display *)data;
	/*
	 * This irq handler is used for sole purpose of identifying
	 * ESD attacks on panel and we can safely assume IRQ_HANDLED
	 * in case of display not being initialized yet
	 */
	if ((!display) || (!display->panel->is_err_flag_irq_enabled) || (!display->panel->panel_initialized))
		return IRQ_HANDLED;

	DSI_ERR("%s\n", __func__);

	if (!display->panel->err_flag_status) {
		display->panel->err_flag_status = true;
		cancel_delayed_work_sync(sde_esk_check_delayed_work);
		schedule_delayed_work(sde_esk_check_delayed_work, 0);
		DSI_ERR("schedule sde_esd_check_delayed_work\n");
	}

	return IRQ_HANDLED;
}

void dsi_display_change_err_flag_irq_status(struct dsi_display *display,
					bool enable)
{
	if (!display) {
		DSI_ERR("Invalid params\n");
		return;
	}

	if (!gpio_is_valid(display->panel->err_flag_gpio))
		return;

	/* Handle unbalanced irq enable/disbale calls */
	if (enable && !display->panel->is_err_flag_irq_enabled) {
		enable_irq(gpio_to_irq(display->panel->err_flag_gpio));
		display->panel->is_err_flag_irq_enabled = true;
		DSI_ERR("enable err flag irq\n");
	} else if (!enable && display->panel->is_err_flag_irq_enabled) {
		disable_irq(gpio_to_irq(display->panel->err_flag_gpio));
		display->panel->is_err_flag_irq_enabled = false;
		DSI_ERR("disable err flag irq\n");
	}
}
EXPORT_SYMBOL(dsi_display_change_err_flag_irq_status);

static void dsi_display_register_err_flag_irq(struct dsi_display *display)
{
	int rc = 0;
	struct platform_device *pdev;
	struct device *dev;
	unsigned int err_flag_irq;

	pdev = display->pdev;
	if (!pdev) {
		DSI_ERR("invalid platform device\n");
		return;
	}

	dev = &pdev->dev;
	if (!dev) {
		DSI_ERR("invalid device\n");
		return;
	}

	if (!gpio_is_valid(display->panel->err_flag_gpio)) {
		DSI_ERR("Failed to get err-flag-gpio\n");
		rc = -EINVAL;
		return;
	}

	err_flag_irq = gpio_to_irq(display->panel->err_flag_gpio);

	/* Avoid deferred spurious irqs with disable_irq() */
	irq_set_status_flags(err_flag_irq, IRQ_DISABLE_UNLAZY);

	rc = devm_request_irq(dev, err_flag_irq, dsi_display_panel_err_flag_irq_handler,
			      IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			      "ERR_FLAG_GPIO", display);
	if (rc) {
		DSI_ERR("Err flag request_irq failed for ESD rc:%d\n", rc);
		irq_clear_status_flags(err_flag_irq, IRQ_DISABLE_UNLAZY);
		return;
	}

	disable_irq(err_flag_irq);
	display->panel->is_err_flag_irq_enabled = false;
}

static irqreturn_t dsi_display_panel_te_irq_handler(int irq, void *data)
{
	struct dsi_display *display = (struct dsi_display *)data;

	/*
	 * This irq handler is used for sole purpose of identifying
	 * ESD attacks on panel and we can safely assume IRQ_HANDLED
	 * in case of display not being initialized yet
	 */
	if (!display)
		return IRQ_HANDLED;

	SDE_EVT32(SDE_EVTLOG_FUNC_CASE1);
	complete_all(&display->esd_te_gate);
	return IRQ_HANDLED;
}

static void dsi_display_change_te_irq_status(struct dsi_display *display,
					bool enable)
{
	if (!display) {
		DSI_ERR("Invalid params\n");
		return;
	}

	/* Handle unbalanced irq enable/disable calls */
	if (enable && !display->is_te_irq_enabled) {
		enable_irq(gpio_to_irq(display->disp_te_gpio));
		display->is_te_irq_enabled = true;
	} else if (!enable && display->is_te_irq_enabled) {
		disable_irq(gpio_to_irq(display->disp_te_gpio));
		display->is_te_irq_enabled = false;
	}
}

static void dsi_display_register_te_irq(struct dsi_display *display)
{
	int rc = 0;
	struct platform_device *pdev;
	struct device *dev;
	unsigned int te_irq;

	pdev = display->pdev;
	if (!pdev) {
		DSI_ERR("invalid platform device\n");
		return;
	}

	dev = &pdev->dev;
	if (!dev) {
		DSI_ERR("invalid device\n");
		return;
	}

	if (!gpio_is_valid(display->disp_te_gpio)) {
		rc = -EINVAL;
		goto error;
	}

	init_completion(&display->esd_te_gate);
	te_irq = gpio_to_irq(display->disp_te_gpio);

	/* Avoid deferred spurious irqs with disable_irq() */
	irq_set_status_flags(te_irq, IRQ_DISABLE_UNLAZY);

	rc = devm_request_irq(dev, te_irq, dsi_display_panel_te_irq_handler,
			      IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			      "TE_GPIO", display);
	if (rc) {
		DSI_ERR("TE request_irq failed for ESD rc:%d\n", rc);
		irq_clear_status_flags(te_irq, IRQ_DISABLE_UNLAZY);
		goto error;
	}

	disable_irq(te_irq);
	display->is_te_irq_enabled = false;

	return;

error:
	/* disable the TE based ESD check */
	DSI_WARN("Unable to register for TE IRQ\n");
	if (display->panel->esd_config.status_mode == ESD_MODE_PANEL_TE)
		display->panel->esd_config.esd_enabled = false;
}

/* Allocate memory for cmd dma tx buffer */
static int dsi_host_alloc_cmd_tx_buffer(struct dsi_display *display)
{
	int rc = 0, cnt = 0;
	struct dsi_display_ctrl *display_ctrl;

#if defined(CONFIG_PXLW_IRIS)
	if (iris_is_chip_supported()) {
		display->tx_cmd_buf = msm_gem_new(display->drm_dev,
			SZ_256K,
			MSM_BO_UNCACHED);
	} else
#endif
		display->tx_cmd_buf = msm_gem_new(display->drm_dev,
			SZ_4K,
			MSM_BO_UNCACHED);

	if ((display->tx_cmd_buf) == NULL) {
		DSI_ERR("Failed to allocate cmd tx buf memory\n");
		rc = -ENOMEM;
		goto error;
	}

	display->cmd_buffer_size = SZ_4K;
#if defined(CONFIG_PXLW_IRIS)
	if (iris_is_chip_supported())
		display->cmd_buffer_size = SZ_256K;
#endif

	display->aspace = msm_gem_smmu_address_space_get(
			display->drm_dev, MSM_SMMU_DOMAIN_UNSECURE);
	if (!display->aspace) {
		DSI_ERR("failed to get aspace\n");
		rc = -EINVAL;
		goto free_gem;
	}
	/* register to aspace */
	rc = msm_gem_address_space_register_cb(display->aspace,
			dsi_display_aspace_cb_locked, (void *)display);
	if (rc) {
		DSI_ERR("failed to register callback %d\n", rc);
		goto free_gem;
	}

	rc = msm_gem_get_iova(display->tx_cmd_buf, display->aspace,
				&(display->cmd_buffer_iova));
	if (rc) {
		DSI_ERR("failed to get the iova rc %d\n", rc);
		goto free_aspace_cb;
	}

	display->vaddr =
		(void *) msm_gem_get_vaddr(display->tx_cmd_buf);
	if (IS_ERR_OR_NULL(display->vaddr)) {
		DSI_ERR("failed to get va rc %d\n", rc);
		rc = -EINVAL;
		goto put_iova;
	}

	display_for_each_ctrl(cnt, display) {
		display_ctrl = &display->ctrl[cnt];
		display_ctrl->ctrl->cmd_buffer_size = SZ_4K;
#if defined(CONFIG_PXLW_IRIS)
		if (iris_is_chip_supported())
			display_ctrl->ctrl->cmd_buffer_size = SZ_256K;
#endif
		display_ctrl->ctrl->cmd_buffer_iova =
					display->cmd_buffer_iova;
		display_ctrl->ctrl->vaddr = display->vaddr;
		display_ctrl->ctrl->tx_cmd_buf = display->tx_cmd_buf;
	}

	return rc;

put_iova:
	msm_gem_put_iova(display->tx_cmd_buf, display->aspace);
free_aspace_cb:
	msm_gem_address_space_unregister_cb(display->aspace,
			dsi_display_aspace_cb_locked, display);
free_gem:
	mutex_lock(&display->drm_dev->struct_mutex);
	msm_gem_free_object(display->tx_cmd_buf);
	mutex_unlock(&display->drm_dev->struct_mutex);
error:
	return rc;
}

static bool dsi_display_validate_reg_read(struct dsi_panel *panel)
{
	int i, j = 0;
	int len = 0, *lenp;
	int group = 0, count = 0;
	struct drm_panel_esd_config *config;

	if (!panel)
		return false;

	config = &(panel->esd_config);

	lenp = config->status_valid_params ?: config->status_cmds_rlen;
	count = config->status_cmd.count;

	for (i = 0; i < count; i++)
		len += lenp[i];

	for (i = 0; i < len; i++)
		j += len;

	for (j = 0; j < config->groups; ++j) {
		for (i = 0; i < len; ++i) {
			if (config->return_buf[i] !=
				config->status_value[group + i]) {
				DRM_ERROR("mismatch: 0x%x\n",
						config->return_buf[i]);
				break;
			}
		}

		if (i == len)
			return true;
		group += len;
	}

	return false;
}

static void dsi_display_parse_te_data(struct dsi_display *display)
{
	struct platform_device *pdev;
	struct device *dev;
	int rc = 0;
	u32 val = 0;

	pdev = display->pdev;
	if (!pdev) {
		DSI_ERR("Invalid platform device\n");
		return;
	}

	dev = &pdev->dev;
	if (!dev) {
		DSI_ERR("Invalid platform device\n");
		return;
	}

	display->disp_te_gpio = of_get_named_gpio(dev->of_node,
					"qcom,platform-te-gpio", 0);

	if (display->fw)
		rc = dsi_parser_read_u32(display->parser_node,
			"qcom,panel-te-source", &val);
	else
		rc = of_property_read_u32(dev->of_node,
			"qcom,panel-te-source", &val);

	if (rc || (val  > MAX_TE_SOURCE_ID)) {
		DSI_ERR("invalid vsync source selection\n");
		val = 0;
	}

	display->te_source = val;
}

static int dsi_display_read_status(struct dsi_display_ctrl *ctrl,
		struct dsi_panel *panel)
{
	int i, rc = 0, count = 0, start = 0, *lenp;
	struct drm_panel_esd_config *config;
	struct dsi_cmd_desc *cmds;
	u32 flags = 0;

	if (!panel || !ctrl || !ctrl->ctrl)
		return -EINVAL;

	/*
	 * When DSI controller is not in initialized state, we do not want to
	 * report a false ESD failure and hence we defer until next read
	 * happen.
	 */
	if (!dsi_ctrl_validate_host_state(ctrl->ctrl))
		return 1;

	config = &(panel->esd_config);
	lenp = config->status_valid_params ?: config->status_cmds_rlen;
	count = config->status_cmd.count;
	cmds = config->status_cmd.cmds;
	flags |= (DSI_CTRL_CMD_FETCH_MEMORY | DSI_CTRL_CMD_READ |
		  DSI_CTRL_CMD_CUSTOM_DMA_SCHED);

	for (i = 0; i < count; ++i) {
		memset(config->status_buf, 0x0, SZ_4K);
		if (cmds[i].last_command) {
			cmds[i].msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
			flags |= DSI_CTRL_CMD_LAST_COMMAND;
		}
		if (config->status_cmd.state == DSI_CMD_SET_STATE_LP)
			cmds[i].msg.flags |= MIPI_DSI_MSG_USE_LPM;
		cmds[i].msg.rx_buf = config->status_buf;
		cmds[i].msg.rx_len = config->status_cmds_rlen[i];
		rc = dsi_ctrl_cmd_transfer(ctrl->ctrl, &cmds[i].msg, &flags);
		if (rc <= 0) {
			DSI_ERR("rx cmd transfer failed rc=%d\n", rc);
			return rc;
		}

		memcpy(config->return_buf + start,
			config->status_buf, lenp[i]);
		start += lenp[i];
	}

	return rc;
}

static int dsi_display_validate_status(struct dsi_display_ctrl *ctrl,
		struct dsi_panel *panel)
{
	int rc = 0;

#if defined(CONFIG_PXLW_IRIS)
	if (iris_is_chip_supported()) {
		rc = iris_read_status(ctrl, panel);
		if (rc == 2)
			rc = dsi_display_read_status(ctrl, panel);
	} else
#endif
		rc = dsi_display_read_status(ctrl, panel);
	if (rc <= 0) {
		goto exit;
	} else {
		/*
		 * panel status read successfully.
		 * check for validity of the data read back.
		 */
		rc = dsi_display_validate_reg_read(panel);
		if (!rc) {
			rc = -EINVAL;
			goto exit;
		}
	}

exit:
	return rc;
}

static int dsi_display_status_reg_read(struct dsi_display *display)
{
	int rc = 0, i;
	struct dsi_display_ctrl *m_ctrl, *ctrl;

	struct dsi_display_mode *mode;
	struct dsi_panel *panel = NULL;
	
	int count = 0;
	#if defined(CONFIG_PXLW_IRIS)
	struct dsi_cmd_desc *cmds;
	unsigned char *payload;
	#endif
	unsigned char register1[10] = {0};
	unsigned char register2[10] = {0};
	unsigned char register3[10] = {0};
	unsigned char register4[10] = {0};

	DSI_DEBUG(" ++\n");

	m_ctrl = &display->ctrl[display->cmd_master_idx];

	if (display->tx_cmd_buf == NULL) {
		rc = dsi_host_alloc_cmd_tx_buffer(display);
		if (rc) {
			DSI_ERR("failed to allocate cmd tx buffer memory\n");
			goto done;
		}
	}

	rc = dsi_display_cmd_engine_enable(display);
	if (rc) {
		DSI_ERR("cmd engine enable failed\n");
		return -EPERM;
	}

	if (!display->panel || !display->panel->cur_mode) {
		rc = -EINVAL;
		goto exit;
	}

	if ((strcmp(display->panel->name, "samsung dsc cmd mode oneplus dsi panel") == 0) ||
		(strcmp(display->panel->name, "samsung sofef03f_m fhd cmd mode dsc dsi panel") == 0) ||
		(strcmp(display->panel->name, "samsung ana6706 dsc cmd mode panel") == 0) ||
		(strcmp(display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) ||
		(strcmp(display->panel->name, "samsung amb655x fhd cmd mode dsc dsi panel") == 0)) {

		mode = display->panel->cur_mode;
		panel = display->panel;

		count = mode->priv_info->cmd_sets[DSI_CMD_SET_LEVEL2_KEY_ENABLE].count;
		if (!count) {
			DSI_ERR("This panel does not support level2 key enable command\n");
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_ENABLE);
			if (rc) {
				DSI_ERR("Failed to send DSI_CMD_SET_LEVEL2_KEY_ENABLE commands\n");
				goto exit;
			}
		}

		if (strcmp(panel->name, "samsung dsc cmd mode oneplus dsi panel") == 0) {
			dsi_display_register_read(display, 0x0E, register1, 1);
			dsi_display_register_read(display, 0xEA, register2, 1);

			if((register1[0] !=0x80) && (register2[0] != 0x80)) {
				rc = -1;
			} else {
				rc = 1;
			}
		} else if (strcmp(panel->name, "samsung sofef03f_m fhd cmd mode dsc dsi panel") == 0) {
			dsi_display_register_read(display, 0x0A, register1, 1);
			dsi_display_register_read(display, 0xB6, register2, 1);

			if ((register1[0] != 0x9c) || (register2[0] != 0x0a)) {
				if (register1[0] != 0x9c)
					esd_black_count++;
				if (register2[0] != 0x0a)
					esd_greenish_count++;
				DSI_ERR("%s:black_count=%d, greenish_count=%d, total=%d\n",
					__func__, esd_black_count, esd_greenish_count,
						esd_black_count + esd_greenish_count);
				rc = -1;
			}
			else {
				rc = 1;
			}
		} else if (strcmp(panel->name, "samsung ana6706 dsc cmd mode panel") == 0) {
#if defined(CONFIG_PXLW_IRIS)
			if (iris_is_chip_supported() && iris_is_pt_mode(panel)) {
				rc = iris_get_status();
				if (rc <= 0) {
					DSI_ERR("Iris ESD snow screen error\n");
					goto exit;
				}

				cmds = mode->priv_info->cmd_sets[DSI_CMD_SET_REGISTER_READ].cmds;
				payload = (u8 *)cmds[0].msg.tx_buf;
				payload[0] = 0xE9;
				rc = iris_panel_ctrl_read_reg(m_ctrl, panel, register1, 4, cmds);
				if (rc <= 0) {
					DSI_ERR("iris_panel_ctrl_read_reg 1 failed, rc=%d\n", rc);
					goto exit;
				}

				payload[0] = 0x0A;
				rc = iris_panel_ctrl_read_reg(m_ctrl, panel, register2, 1, cmds);
				if (rc <= 0) {
					DSI_ERR("iris_panel_ctrl_read_reg 2 failed, rc=%d\n", rc);
					goto exit;
				}
			} else {
#else
			{
#endif
				rc = dsi_display_register_read(display, 0xE9, register1, 4);
				if (rc <= 0)
					goto exit;

				rc = dsi_display_register_read(display, 0x0A, register2, 1);
				if (rc <= 0)
					goto exit;
			}

			DSI_ERR("0xE9 = %02x, %02x, %02x, %02x, 0x0A = %02x\n", register1[0], register1[1], register1[2], register1[3], register2[0]);
			if (((register1[3] != 0x00) && (register1[3] != 0x02) && (register1[3] != 0x06) && (register1[3] != 0x04)) || (register2[0] != 0x9C)) {
				if ((register1[3] == 0x10) || (register1[3] == 0x30) || (register1[3] == 0x32)
					|| (register1[3] == 0x38) || (register1[3] == 0x18) || (register1[3] == 0x08))
					DSI_ERR("ESD color dot error\n");
				if ((register1[3] == 0x31) || (register1[3] == 0x33))
					DSI_ERR("ESD snow screen error\n");
				if (register2[0] != 0x9C)
					DSI_ERR("ESD black screen error\n");
				rc = -1;
			} else {
				rc = 1;
			}
		} else if (strcmp(panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) {
			rc = dsi_display_register_read(display, 0x0A, register1, 1);
			if (rc <= 0)
				goto exit;

			rc = dsi_display_register_read(display, 0xEE, register2, 1);
			if (rc <= 0)
				goto exit;

			rc = dsi_display_register_read(display, 0xE5, register3, 1);
			if (rc <= 0)
				goto exit;

			count = mode->priv_info->cmd_sets[DSI_CMD_READ_SAMSUNG_PANEL_REGISTER_ED_ON].count;
			if (!count) {
				DSI_ERR("This panel does not support esd register reading\n");
			} else {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_READ_SAMSUNG_PANEL_REGISTER_ED_ON);
				if (rc) {
					DSI_ERR("Failed to send DSI_CMD_READ_SAMSUNG_PANEL_REGISTER_ED_ON command\n");
					rc = -1;
					goto exit;
				}
			}
			rc = dsi_display_register_read(display, 0xED, register4, 1);
			if (rc <= 0)
				goto exit;

			if ((register1[0] != 0x9c) || ((register2[0] != 0x00) && (register2[0] != 0x80))
				|| ((register3[0] != 0x13) && (register3[0] != 0x12)) || (register4[0] != 0x97)) {
				DSI_ERR("0x0A = %02x, 0xEE = %02x, 0xE5 = %02x, 0xED = %02x\n", register1[0], register2[0], register3[0], register4[0]);
				if (register1[0] != 0x9c)
					esd_black_count++;
				if ((register2[0] != 0x00) || (register3[0] != 0x13) || (register4[0] != 0x97))
					esd_greenish_count++;
				DSI_ERR("%s:black_count=%d, greenish_count=%d, total=%d\n",
					__func__, esd_black_count, esd_greenish_count,
						esd_black_count + esd_greenish_count);
				rc = -1;
			}
			else {
				rc = 1;
			}
		} else if (strcmp(panel->name, "samsung amb655x fhd cmd mode dsc dsi panel") == 0) {
			rc = dsi_display_register_read(display, 0x0A, register1, 1);
			if (rc <= 0)
				goto exit;

			rc = dsi_display_register_read(display, 0xB6, register2, 1);
			if (rc <= 0)
				goto exit;

			rc = dsi_display_register_read(display, 0xA2, register3, 5);
			if (rc <= 0)
				goto exit;

			DSI_ERR("0x0A = %02x, 0xB6 = %02x, 0xA2 = %02x, %02x, %02x, %02x, %02x\n", register1[0], register2[0],
				register3[0], register3[1], register3[2], register3[3], register3[4]);

			if ((register1[0] != 0x9c) || (register2[0] != 0x0a) || (register3[0] != 0x11) || (register3[1] != 0x00)
					|| (register3[2] != 0x00) || (register3[3] != 0x89) || (register3[4] != 0x30)) {
				if ((register1[0] != 0x9c) || (register3[0] != 0x11) || (register3[1] != 0x00)
						|| (register3[2] != 0x00) || (register3[3] != 0x89) || (register3[4] != 0x30))
					esd_black_count++;
				if (register2[0] != 0x0a)
					esd_greenish_count++;
				DSI_ERR("black_count=%d, greenish_count=%d, total=%d\n",
					esd_black_count, esd_greenish_count, esd_black_count + esd_greenish_count);
				rc = -1;
			} else {
				rc = 1;
			}
		}

		count = mode->priv_info->cmd_sets[DSI_CMD_SET_LEVEL2_KEY_DISABLE].count;
		if (!count)
			DSI_ERR("This panel does not support level2 key disable command\n");
		else
			dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_DISABLE);
	} else {
		rc = dsi_display_validate_status(m_ctrl, display->panel);
	}
	if (rc <= 0) {
		DSI_ERR("[%s] read status failed on master,rc=%d\n",
		       display->name, rc);
		goto exit;
	}

	if (!display->panel->sync_broadcast_en)
		goto exit;

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (ctrl == m_ctrl)
			continue;

		rc = dsi_display_validate_status(ctrl, display->panel);
		if (rc <= 0) {
			DSI_ERR("[%s] read status failed on slave,rc=%d\n",
			       display->name, rc);
			goto exit;
		}
	}
exit:
	dsi_display_cmd_engine_disable(display);
done:
	return rc;
}

static int dsi_display_status_bta_request(struct dsi_display *display)
{
	int rc = 0;

	DSI_DEBUG(" ++\n");
	/* TODO: trigger SW BTA and wait for acknowledgment */

	return rc;
}

static int dsi_display_status_check_te(struct dsi_display *display)
{
	int rc = 1;
	int const esd_te_timeout = msecs_to_jiffies(3*20);

	dsi_display_change_te_irq_status(display, true);

	reinit_completion(&display->esd_te_gate);
	if (!wait_for_completion_timeout(&display->esd_te_gate,
				esd_te_timeout)) {
		DSI_ERR("TE check failed\n");
		rc = -EINVAL;
	}

	dsi_display_change_te_irq_status(display, false);

	return rc;
}

int dsi_display_check_status(struct drm_connector *connector, void *display,
					bool te_check_override)
{
	struct dsi_display *dsi_display = display;
	struct dsi_panel *panel;
	u32 status_mode;
	int rc = 0x1, ret;
	u32 mask;

	if (!dsi_display || !dsi_display->panel)
		return -EINVAL;

	panel = dsi_display->panel;

	dsi_panel_acquire_panel_lock(panel);

	if (!panel->panel_initialized) {
		DSI_DEBUG("Panel not initialized\n");
		goto release_panel_lock;
	}

	/* Prevent another ESD check,when ESD recovery is underway */
	if (atomic_read(&panel->esd_recovery_pending))
		goto release_panel_lock;

	status_mode = panel->esd_config.status_mode;

	if (status_mode == ESD_MODE_SW_SIM_SUCCESS)
		goto release_panel_lock;

	if (status_mode == ESD_MODE_SW_SIM_FAILURE) {
		rc = -EINVAL;
		goto release_panel_lock;
	}
	SDE_EVT32(SDE_EVTLOG_FUNC_ENTRY);

	if (te_check_override && gpio_is_valid(dsi_display->disp_te_gpio))
		status_mode = ESD_MODE_PANEL_TE;

	if (status_mode == ESD_MODE_PANEL_TE) {
		rc = dsi_display_status_check_te(dsi_display);
		goto exit;
	}

	if (dsi_display->panel->err_flag_status == true) {
		esd_black_count++;
		DSI_ERR("%s:black_count=%d, greenish_count=%d, total=%d\n",
			__func__, esd_black_count, esd_greenish_count,
				esd_black_count + esd_greenish_count);
		rc = -1;
		goto exit;
	}

	if ((dsi_display->panel->panel_switch_status == true)
		&& (strcmp(dsi_display->panel->name, "samsung ana6706 dsc cmd mode panel") == 0)) {
		DSI_ERR("panel_switch_status = true, skip ESD reading\n");
		goto release_panel_lock;
	}
	ret = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
		DSI_ALL_CLKS, DSI_CLK_ON);
	if (ret)
		goto release_panel_lock;

	/* Mask error interrupts before attempting ESD read */
	mask = BIT(DSI_FIFO_OVERFLOW) | BIT(DSI_FIFO_UNDERFLOW);
	dsi_display_set_ctrl_esd_check_flag(dsi_display, true);
	dsi_display_mask_ctrl_error_interrupts(dsi_display, mask, true);

	if (status_mode == ESD_MODE_REG_READ) {
		rc = dsi_display_status_reg_read(dsi_display);
	} else if (status_mode == ESD_MODE_SW_BTA) {
		rc = dsi_display_status_bta_request(dsi_display);
	} else if (status_mode == ESD_MODE_PANEL_TE) {
		rc = dsi_display_status_check_te(dsi_display);
	} else {
		DSI_WARN("Unsupported check status mode: %d\n", status_mode);
		panel->esd_config.esd_enabled = false;
	}

	/* Unmask error interrupts if check passed*/
	if (rc > 0) {
		dsi_display_set_ctrl_esd_check_flag(dsi_display, false);
		dsi_display_mask_ctrl_error_interrupts(dsi_display, mask,
							false);
	}

	dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
		DSI_ALL_CLKS, DSI_CLK_OFF);

exit:
	/* Handle Panel failures during display disable sequence */
	if (rc <=0)
		atomic_set(&panel->esd_recovery_pending, 1);

release_panel_lock:
	dsi_panel_release_panel_lock(panel);
	SDE_EVT32(SDE_EVTLOG_FUNC_EXIT);

	return rc;
}

static int dsi_display_cmd_prepare(const char *cmd_buf, u32 cmd_buf_len,
		struct dsi_cmd_desc *cmd, u8 *payload, u32 payload_len)
{
	int i;

	memset(cmd, 0x00, sizeof(*cmd));
	cmd->msg.type = cmd_buf[0];
	cmd->last_command = (cmd_buf[1] == 1);
	cmd->msg.channel = cmd_buf[2];
	cmd->msg.flags = cmd_buf[3];
	cmd->msg.ctrl = 0;
	cmd->post_wait_ms = cmd->msg.wait_ms = cmd_buf[4];
	cmd->msg.tx_len = ((cmd_buf[5] << 8) | (cmd_buf[6]));

	if (cmd->msg.tx_len > payload_len) {
		DSI_ERR("Incorrect payload length tx_len %zu, payload_len %d\n",
		       cmd->msg.tx_len, payload_len);
		return -EINVAL;
	}

	for (i = 0; i < cmd->msg.tx_len; i++)
		payload[i] = cmd_buf[7 + i];

	cmd->msg.tx_buf = payload;
	return 0;
}

static int dsi_display_ctrl_get_host_init_state(struct dsi_display *dsi_display,
		bool *state)
{
	struct dsi_display_ctrl *ctrl;
	int i, rc = -EINVAL;

	display_for_each_ctrl(i, dsi_display) {
		ctrl = &dsi_display->ctrl[i];
		rc = dsi_ctrl_get_host_engine_init_state(ctrl->ctrl, state);
		if (rc)
			break;
	}
	return rc;
}
#if defined(CONFIG_PXLW_IRIS)
int iris_dsi_display_ctrl_get_host_init_state(struct dsi_display *dsi_display,
		bool *state)
{
	return dsi_display_ctrl_get_host_init_state(dsi_display, state);
}
#endif

int dsi_display_cmd_transfer(struct drm_connector *connector,
		void *display, const char *cmd_buf,
		u32 cmd_buf_len)
{
	struct dsi_display *dsi_display = display;
	struct dsi_cmd_desc cmd;
	u8 cmd_payload[MAX_CMD_PAYLOAD_SIZE];
	int rc = 0;
	bool state = false;

	if (!dsi_display || !cmd_buf) {
		DSI_ERR("[DSI] invalid params\n");
		return -EINVAL;
	}

	DSI_DEBUG("[DSI] Display command transfer\n");

	rc = dsi_display_cmd_prepare(cmd_buf, cmd_buf_len,
			&cmd, cmd_payload, MAX_CMD_PAYLOAD_SIZE);
	if (rc) {
		DSI_ERR("[DSI] command prepare failed. rc %d\n", rc);
		return rc;
	}

	mutex_lock(&dsi_display->display_lock);
	rc = dsi_display_ctrl_get_host_init_state(dsi_display, &state);

	/**
	 * Handle scenario where a command transfer is initiated through
	 * sysfs interface when device is in suepnd state.
	 */
	if (!rc && !state) {
		pr_warn_ratelimited("Command xfer attempted while device is in suspend state\n"
				);
		rc = -EPERM;
		goto end;
	}
	if (rc || !state) {
		DSI_ERR("[DSI] Invalid host state %d rc %d\n",
				state, rc);
		rc = -EPERM;
		goto end;
	}

	rc = dsi_display->host.ops->transfer(&dsi_display->host,
			&cmd.msg);
end:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}

static void _dsi_display_continuous_clk_ctrl(struct dsi_display *display,
					     bool enable)
{
	int i;
	struct dsi_display_ctrl *ctrl;

	if (!display || !display->panel->host_config.force_hs_clk_lane)
		return;

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];

		/*
		 * For phy ver 4.0 chipsets, configure DSI controller and
		 * DSI PHY to force clk lane to HS mode always whereas
		 * for other phy ver chipsets, configure DSI controller only.
		 */
		if (ctrl->phy->hw.ops.set_continuous_clk) {
			dsi_ctrl_hs_req_sel(ctrl->ctrl, true);
			dsi_ctrl_set_continuous_clk(ctrl->ctrl, enable);
			dsi_phy_set_continuous_clk(ctrl->phy, enable);
		} else {
			dsi_ctrl_set_continuous_clk(ctrl->ctrl, enable);
		}
	}
}

int dsi_display_soft_reset(void *display)
{
	struct dsi_display *dsi_display;
	struct dsi_display_ctrl *ctrl;
	int rc = 0;
	int i;

	if (!display)
		return -EINVAL;

	dsi_display = display;

	display_for_each_ctrl(i, dsi_display) {
		ctrl = &dsi_display->ctrl[i];
		rc = dsi_ctrl_soft_reset(ctrl->ctrl);
		if (rc) {
			DSI_ERR("[%s] failed to soft reset host_%d, rc=%d\n",
					dsi_display->name, i, rc);
			break;
		}
	}

	return rc;
}

enum dsi_pixel_format dsi_display_get_dst_format(
		struct drm_connector *connector,
		void *display)
{
	enum dsi_pixel_format format = DSI_PIXEL_FORMAT_MAX;
	struct dsi_display *dsi_display = (struct dsi_display *)display;

	if (!dsi_display || !dsi_display->panel) {
		DSI_ERR("Invalid params(s) dsi_display %pK, panel %pK\n",
			dsi_display,
			((dsi_display) ? dsi_display->panel : NULL));
		return format;
	}

	format = dsi_display->panel->host_config.dst_format;
	return format;
}

static void _dsi_display_setup_misr(struct dsi_display *display)
{
	int i;

	display_for_each_ctrl(i, display) {
		dsi_ctrl_setup_misr(display->ctrl[i].ctrl,
				display->misr_enable,
				display->misr_frame_count);
	}
}

extern int dsi_panel_set_aod_mode(struct dsi_panel *panel, int level);

int dsi_display_set_power(struct drm_connector *connector,
		int power_mode, void *disp)
{
	struct dsi_display *display = disp;
	int rc = 0;
	struct drm_panel_notifier notifier_data;
	int blank;

#ifdef CONFIG_F2FS_OF2FS
	struct drm_panel_notifier notifier_data_f2fs;
	int blank_f2fs;
#endif

	if (!display || !display->panel) {
		DSI_ERR("invalid display/panel\n");
		return -EINVAL;
	}

	switch (power_mode) {
	case SDE_MODE_DPMS_LP1:
		DSI_ERR("SDE_MODE_DPMS_LP1\n");
		rc = dsi_panel_set_lp1(display->panel);
		if (display->panel->aod_mode && display->panel->aod_mode != 2) {
			display->panel->aod_status = 0;
			rc = dsi_panel_set_aod_mode(display->panel, 5);
			DSI_ERR("Send dsi_panel_set_aod_mode 5 cmds\n");
			if (rc) {
				DSI_ERR("[%s] failed to send dsi_panel_set_aod_mode cmds, rc=%d\n",
					display->panel->name, rc);
			}
		}
		blank = DRM_PANEL_BLANK_AOD;
		notifier_data.data = &blank;
		DSI_ERR("DRM_PANEL_BLANK_AOD\n");
		if (lcd_active_panel)
			drm_panel_notifier_call_chain(lcd_active_panel, DRM_PANEL_EARLY_EVENT_BLANK, &notifier_data);
	case SDE_MODE_DPMS_LP2:
		DSI_ERR("SDE_MODE_DPMS_LP2\n");
		rc = dsi_panel_set_lp2(display->panel);
		break;
	case SDE_MODE_DPMS_ON:
		DSI_ERR("SDE_MODE_DPMS_ON\n");
		if (display->panel->power_mode == SDE_MODE_DPMS_LP1 ||
			display->panel->power_mode == SDE_MODE_DPMS_LP2)
			rc = dsi_panel_set_nolp(display->panel);
		/* send screen on cmd for tp start */
		blank = DRM_PANEL_BLANK_UNBLANK_CUST;
		notifier_data.data = &blank;
		DSI_ERR("DRM_PANEL_BLANK_UNBLANK_CUST\n");
		if (lcd_active_panel)
			drm_panel_notifier_call_chain(lcd_active_panel, DRM_PANEL_EARLY_EVENT_BLANK, &notifier_data);
#ifdef CONFIG_F2FS_OF2FS
		blank_f2fs = DRM_PANEL_BLANK_UNBLANK_CUST;
		notifier_data_f2fs.data = &blank_f2fs;
		f2fs_panel_notifier_call_chain(DRM_PANEL_EARLY_EVENT_BLANK, &notifier_data_f2fs);
#endif
		/* send screen on cmd for tp end */
		break;
	case SDE_MODE_DPMS_OFF:
		/* send screen off cmd for tp start */
		blank = DRM_PANEL_BLANK_POWERDOWN_CUST;
		notifier_data.data = &blank;
		DSI_ERR("DRM_PANEL_BLANK_POWERDOWN_CUST\n");
		if (lcd_active_panel)
			drm_panel_notifier_call_chain(lcd_active_panel, DRM_PANEL_EARLY_EVENT_BLANK, &notifier_data);
#ifdef CONFIG_F2FS_OF2FS
		blank_f2fs = DRM_PANEL_BLANK_POWERDOWN_CUST;
		notifier_data_f2fs.data = &blank_f2fs;
		f2fs_panel_notifier_call_chain(DRM_PANEL_EARLY_EVENT_BLANK, &notifier_data_f2fs);
#endif
		/* send screen off cmd for tp end */
		break;
	default:
		return rc;
	}

	DSI_DEBUG("Power mode transition from %d to %d %s",
			display->panel->power_mode, power_mode,
			rc ? "failed" : "successful");
	if (!rc)
		display->panel->power_mode = power_mode;

	return rc;
}

#ifdef CONFIG_DEBUG_FS
static bool dsi_display_is_te_based_esd(struct dsi_display *display)
{
	u32 status_mode = 0;

	if (!display->panel) {
		DSI_ERR("Invalid panel data\n");
		return false;
	}

	status_mode = display->panel->esd_config.status_mode;

	if (status_mode == ESD_MODE_PANEL_TE &&
			gpio_is_valid(display->disp_te_gpio))
		return true;
	return false;
}

static ssize_t debugfs_dump_info_read(struct file *file,
				      char __user *user_buf,
				      size_t user_len,
				      loff_t *ppos)
{
	struct dsi_display *display = file->private_data;
	char *buf;
	u32 len = 0;
	int i;

	if (!display)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(SZ_4K, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += snprintf(buf + len, (SZ_4K - len), "name = %s\n", display->name);
	len += snprintf(buf + len, (SZ_4K - len),
			"\tResolution = %dx%d\n",
			display->config.video_timing.h_active,
			display->config.video_timing.v_active);

	display_for_each_ctrl(i, display) {
		len += snprintf(buf + len, (SZ_4K - len),
				"\tCTRL_%d:\n\t\tctrl = %s\n\t\tphy = %s\n",
				i, display->ctrl[i].ctrl->name,
				display->ctrl[i].phy->name);
	}

	len += snprintf(buf + len, (SZ_4K - len),
			"\tPanel = %s\n", display->panel->name);

	len += snprintf(buf + len, (SZ_4K - len),
			"\tClock master = %s\n",
			display->ctrl[display->clk_master_idx].ctrl->name);

	if (len > user_len)
		len = user_len;

	if (copy_to_user(user_buf, buf, len)) {
		kfree(buf);
		return -EFAULT;
	}

	*ppos += len;

	kfree(buf);
	return len;
}

static ssize_t debugfs_misr_setup(struct file *file,
				  const char __user *user_buf,
				  size_t user_len,
				  loff_t *ppos)
{
	struct dsi_display *display = file->private_data;
	char *buf;
	int rc = 0;
	size_t len;
	u32 enable, frame_count;

	if (!display)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(MISR_BUFF_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* leave room for termination char */
	len = min_t(size_t, user_len, MISR_BUFF_SIZE - 1);
	if (copy_from_user(buf, user_buf, len)) {
		rc = -EINVAL;
		goto error;
	}

	buf[len] = '\0'; /* terminate the string */

	if (sscanf(buf, "%u %u", &enable, &frame_count) != 2) {
		rc = -EINVAL;
		goto error;
	}

	display->misr_enable = enable;
	display->misr_frame_count = frame_count;

	mutex_lock(&display->display_lock);
	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       display->name, rc);
		goto unlock;
	}

	_dsi_display_setup_misr(display);

	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
		       display->name, rc);
		goto unlock;
	}

	rc = user_len;
unlock:
	mutex_unlock(&display->display_lock);
error:
	kfree(buf);
	return rc;
}

static ssize_t debugfs_misr_read(struct file *file,
				 char __user *user_buf,
				 size_t user_len,
				 loff_t *ppos)
{
	struct dsi_display *display = file->private_data;
	char *buf;
	u32 len = 0;
	int rc = 0;
	struct dsi_ctrl *dsi_ctrl;
	int i;
	u32 misr;
	size_t max_len = min_t(size_t, user_len, MISR_BUFF_SIZE);

	if (!display)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(max_len, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf))
		return -ENOMEM;

	mutex_lock(&display->display_lock);
	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       display->name, rc);
		goto error;
	}

	display_for_each_ctrl(i, display) {
		dsi_ctrl = display->ctrl[i].ctrl;
		misr = dsi_ctrl_collect_misr(display->ctrl[i].ctrl);

		len += snprintf((buf + len), max_len - len,
			"DSI_%d MISR: 0x%x\n", dsi_ctrl->cell_index, misr);

		if (len >= max_len)
			break;
	}

	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
		       display->name, rc);
		goto error;
	}

	if (copy_to_user(user_buf, buf, max_len)) {
		rc = -EFAULT;
		goto error;
	}

	*ppos += len;

error:
	mutex_unlock(&display->display_lock);
	kfree(buf);
	return len;
}

static ssize_t debugfs_esd_trigger_check(struct file *file,
				  const char __user *user_buf,
				  size_t user_len,
				  loff_t *ppos)
{
	struct dsi_display *display = file->private_data;
	char *buf;
	int rc = 0;
	struct drm_panel_esd_config *esd_config = &display->panel->esd_config;
	u32 esd_trigger;
	size_t len;

	if (!display)
		return -ENODEV;

	if (*ppos)
		return 0;

	if (user_len > sizeof(u32))
		return -EINVAL;

	if (!user_len || !user_buf)
		return -EINVAL;

	if (!display->panel ||
		atomic_read(&display->panel->esd_recovery_pending))
		return user_len;

	if (!esd_config->esd_enabled) {
		DSI_ERR("ESD feature is not enabled\n");
		return -EINVAL;
	}

	buf = kzalloc(ESD_TRIGGER_STRING_MAX_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len = min_t(size_t, user_len, ESD_TRIGGER_STRING_MAX_LEN - 1);
	if (copy_from_user(buf, user_buf, len)) {
		rc = -EINVAL;
		goto error;
	}

	buf[len] = '\0'; /* terminate the string */

	if (kstrtouint(buf, 10, &esd_trigger)) {
		rc = -EINVAL;
		goto error;
	}

	if (esd_trigger != 1) {
		rc = -EINVAL;
		goto error;
	}

	display->esd_trigger = esd_trigger;

	if (display->esd_trigger) {
		DSI_INFO("ESD attack triggered by user\n");
		rc = dsi_panel_trigger_esd_attack(display->panel);
		if (rc) {
			DSI_ERR("Failed to trigger ESD attack\n");
			goto error;
		}
	}

	rc = len;
error:
	kfree(buf);
	return rc;
}

static ssize_t debugfs_alter_esd_check_mode(struct file *file,
				  const char __user *user_buf,
				  size_t user_len,
				  loff_t *ppos)
{
	struct dsi_display *display = file->private_data;
	struct drm_panel_esd_config *esd_config;
	char *buf;
	int rc = 0;
	size_t len;

	if (!display)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(ESD_MODE_STRING_MAX_LEN, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf))
		return -ENOMEM;

	len = min_t(size_t, user_len, ESD_MODE_STRING_MAX_LEN - 1);
	if (copy_from_user(buf, user_buf, len)) {
		rc = -EINVAL;
		goto error;
	}

	buf[len] = '\0'; /* terminate the string */
	if (!display->panel) {
		rc = -EINVAL;
		goto error;
	}

	esd_config = &display->panel->esd_config;
	if (!esd_config) {
		DSI_ERR("Invalid panel esd config\n");
		rc = -EINVAL;
		goto error;
	}

	if (!esd_config->esd_enabled)
		goto error;

	if (!strcmp(buf, "te_signal_check\n")) {
		if (display->panel->panel_mode == DSI_OP_VIDEO_MODE) {
			DSI_INFO("TE based ESD check for Video Mode panels is not allowed\n");
			goto error;
		}
		DSI_INFO("ESD check is switched to TE mode by user\n");
		esd_config->status_mode = ESD_MODE_PANEL_TE;
		dsi_display_change_te_irq_status(display, true);
	}

	if (!strcmp(buf, "reg_read\n")) {
		DSI_INFO("ESD check is switched to reg read by user\n");
		rc = dsi_panel_parse_esd_reg_read_configs(display->panel);
		if (rc) {
			DSI_ERR("failed to alter esd check mode,rc=%d\n",
						rc);
			rc = user_len;
			goto error;
		}
		esd_config->status_mode = ESD_MODE_REG_READ;
		if (dsi_display_is_te_based_esd(display))
			dsi_display_change_te_irq_status(display, false);
	}

	if (!strcmp(buf, "esd_sw_sim_success\n"))
		esd_config->status_mode = ESD_MODE_SW_SIM_SUCCESS;

	if (!strcmp(buf, "esd_sw_sim_failure\n"))
		esd_config->status_mode = ESD_MODE_SW_SIM_FAILURE;

	rc = len;
error:
	kfree(buf);
	return rc;
}

static ssize_t debugfs_read_esd_check_mode(struct file *file,
				 char __user *user_buf,
				 size_t user_len,
				 loff_t *ppos)
{
	struct dsi_display *display = file->private_data;
	struct drm_panel_esd_config *esd_config;
	char *buf;
	int rc = 0;
	size_t len;

	if (!display)
		return -ENODEV;

	if (*ppos)
		return 0;

	if (!display->panel) {
		DSI_ERR("invalid panel data\n");
		return -EINVAL;
	}

	buf = kzalloc(ESD_MODE_STRING_MAX_LEN, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf))
		return -ENOMEM;

	esd_config = &display->panel->esd_config;
	if (!esd_config) {
		DSI_ERR("Invalid panel esd config\n");
		rc = -EINVAL;
		goto error;
	}

	len = min_t(size_t, user_len, ESD_MODE_STRING_MAX_LEN - 1);
	if (!esd_config->esd_enabled) {
		rc = snprintf(buf, len, "ESD feature not enabled");
		goto output_mode;
	}

	switch (esd_config->status_mode) {
	case ESD_MODE_REG_READ:
		rc = snprintf(buf, len, "reg_read");
		break;
	case ESD_MODE_PANEL_TE:
		rc = snprintf(buf, len, "te_signal_check");
		break;
	case ESD_MODE_SW_SIM_FAILURE:
		rc = snprintf(buf, len, "esd_sw_sim_failure");
		break;
	case ESD_MODE_SW_SIM_SUCCESS:
		rc = snprintf(buf, len, "esd_sw_sim_success");
		break;
	default:
		rc = snprintf(buf, len, "invalid");
		break;
	}

output_mode:
	if (!rc) {
		rc = -EINVAL;
		goto error;
	}

	if (copy_to_user(user_buf, buf, len)) {
		rc = -EFAULT;
		goto error;
	}

	*ppos += len;

error:
	kfree(buf);
	return len;
}

static const struct file_operations dump_info_fops = {
	.open = simple_open,
	.read = debugfs_dump_info_read,
};

static const struct file_operations misr_data_fops = {
	.open = simple_open,
	.read = debugfs_misr_read,
	.write = debugfs_misr_setup,
};

static const struct file_operations esd_trigger_fops = {
	.open = simple_open,
	.write = debugfs_esd_trigger_check,
};

static const struct file_operations esd_check_mode_fops = {
	.open = simple_open,
	.write = debugfs_alter_esd_check_mode,
	.read = debugfs_read_esd_check_mode,
};

static int dsi_display_debugfs_init(struct dsi_display *display)
{
	int rc = 0;
	struct dentry *dir, *dump_file, *misr_data;
	char name[MAX_NAME_SIZE];
	int i;

	dir = debugfs_create_dir(display->name, NULL);
	if (IS_ERR_OR_NULL(dir)) {
		rc = PTR_ERR(dir);
		DSI_ERR("[%s] debugfs create dir failed, rc = %d\n",
		       display->name, rc);
		goto error;
	}

	dump_file = debugfs_create_file("dump_info",
					0400,
					dir,
					display,
					&dump_info_fops);
	if (IS_ERR_OR_NULL(dump_file)) {
		rc = PTR_ERR(dump_file);
		DSI_ERR("[%s] debugfs create dump info file failed, rc=%d\n",
		       display->name, rc);
		goto error_remove_dir;
	}

	dump_file = debugfs_create_file("esd_trigger",
					0644,
					dir,
					display,
					&esd_trigger_fops);
	if (IS_ERR_OR_NULL(dump_file)) {
		rc = PTR_ERR(dump_file);
		DSI_ERR("[%s] debugfs for esd trigger file failed, rc=%d\n",
		       display->name, rc);
		goto error_remove_dir;
	}

	dump_file = debugfs_create_file("esd_check_mode",
					0644,
					dir,
					display,
					&esd_check_mode_fops);
	if (IS_ERR_OR_NULL(dump_file)) {
		rc = PTR_ERR(dump_file);
		DSI_ERR("[%s] debugfs for esd check mode failed, rc=%d\n",
		       display->name, rc);
		goto error_remove_dir;
	}

	misr_data = debugfs_create_file("misr_data",
					0600,
					dir,
					display,
					&misr_data_fops);
	if (IS_ERR_OR_NULL(misr_data)) {
		rc = PTR_ERR(misr_data);
		DSI_ERR("[%s] debugfs create misr datafile failed, rc=%d\n",
		       display->name, rc);
		goto error_remove_dir;
	}

	display_for_each_ctrl(i, display) {
		struct msm_dsi_phy *phy = display->ctrl[i].phy;

		if (!phy || !phy->name)
			continue;

		snprintf(name, ARRAY_SIZE(name),
				"%s_allow_phy_power_off", phy->name);
		dump_file = debugfs_create_bool(name, 0600, dir,
				&phy->allow_phy_power_off);
		if (IS_ERR_OR_NULL(dump_file)) {
			rc = PTR_ERR(dump_file);
			DSI_ERR("[%s] debugfs create %s failed, rc=%d\n",
			       display->name, name, rc);
			goto error_remove_dir;
		}

		snprintf(name, ARRAY_SIZE(name),
				"%s_regulator_min_datarate_bps", phy->name);
		dump_file = debugfs_create_u32(name, 0600, dir,
				&phy->regulator_min_datarate_bps);
		if (IS_ERR_OR_NULL(dump_file)) {
			rc = PTR_ERR(dump_file);
			DSI_ERR("[%s] debugfs create %s failed, rc=%d\n",
			       display->name, name, rc);
			goto error_remove_dir;
		}
	}

	if (!debugfs_create_bool("ulps_feature_enable", 0600, dir,
			&display->panel->ulps_feature_enabled)) {
		DSI_ERR("[%s] debugfs create ulps feature enable file failed\n",
		       display->name);
		goto error_remove_dir;
	}

	if (!debugfs_create_bool("ulps_suspend_feature_enable", 0600, dir,
			&display->panel->ulps_suspend_enabled)) {
		DSI_ERR("[%s] debugfs create ulps-suspend feature enable file failed\n",
		       display->name);
		goto error_remove_dir;
	}

	if (!debugfs_create_bool("ulps_status", 0400, dir,
			&display->ulps_enabled)) {
		DSI_ERR("[%s] debugfs create ulps status file failed\n",
		       display->name);
		goto error_remove_dir;
	}

	if (!debugfs_create_u32("clk_gating_config", 0600, dir,
			&display->clk_gating_config)) {
		DSI_ERR("[%s] debugfs create clk gating config failed\n",
		       display->name);
		goto error_remove_dir;
	}
#if defined(CONFIG_PXLW_IRIS)
	iris_dsi_display_debugfs_init(display, dir, dump_file);
#endif

	display->root = dir;
	dsi_parser_dbg_init(display->parser, dir);

	return rc;
error_remove_dir:
	debugfs_remove(dir);
error:
	return rc;
}

static int dsi_display_debugfs_deinit(struct dsi_display *display)
{
	debugfs_remove_recursive(display->root);

	return 0;
}
#else
static int dsi_display_debugfs_init(struct dsi_display *display)
{
	return 0;
}
static int dsi_display_debugfs_deinit(struct dsi_display *display)
{
	return 0;
}
#endif /* CONFIG_DEBUG_FS */

static void adjust_timing_by_ctrl_count(const struct dsi_display *display,
					struct dsi_display_mode *mode)
{
	struct dsi_host_common_cfg *host = &display->panel->host_config;
	bool is_split_link = host->split_link.split_link_enabled;
	u32 sublinks_count = host->split_link.num_sublinks;

	if (is_split_link && sublinks_count > 1) {
		mode->timing.h_active /= sublinks_count;
		mode->timing.h_front_porch /= sublinks_count;
		mode->timing.h_sync_width /= sublinks_count;
		mode->timing.h_back_porch /= sublinks_count;
		mode->timing.h_skew /= sublinks_count;
		mode->pixel_clk_khz /= sublinks_count;
	} else {
		mode->timing.h_active /= display->ctrl_count;
		mode->timing.h_front_porch /= display->ctrl_count;
		mode->timing.h_sync_width /= display->ctrl_count;
		mode->timing.h_back_porch /= display->ctrl_count;
		mode->timing.h_skew /= display->ctrl_count;
		mode->pixel_clk_khz /= display->ctrl_count;
	}
}

static int dsi_display_is_ulps_req_valid(struct dsi_display *display,
		bool enable)
{
	/* TODO: make checks based on cont. splash */

	DSI_DEBUG("checking ulps req validity\n");

	if (atomic_read(&display->panel->esd_recovery_pending)) {
		DSI_DEBUG("%s: ESD recovery sequence underway\n", __func__);
		return false;
	}

	if (!dsi_panel_ulps_feature_enabled(display->panel) &&
			!display->panel->ulps_suspend_enabled) {
		DSI_DEBUG("%s: ULPS feature is not enabled\n", __func__);
		return false;
	}

	if (!dsi_panel_initialized(display->panel) &&
			!display->panel->ulps_suspend_enabled) {
		DSI_DEBUG("%s: panel not yet initialized\n", __func__);
		return false;
	}

	if (enable && display->ulps_enabled) {
		DSI_DEBUG("ULPS already enabled\n");
		return false;
	} else if (!enable && !display->ulps_enabled) {
		DSI_DEBUG("ULPS already disabled\n");
		return false;
	}

	/*
	 * No need to enter ULPS when transitioning from splash screen to
	 * boot animation since it is expected that the clocks would be turned
	 * right back on.
	 */
	if (enable && display->is_cont_splash_enabled)
		return false;

	return true;
}


/**
 * dsi_display_set_ulps() - set ULPS state for DSI lanes.
 * @dsi_display:         DSI display handle.
 * @enable:           enable/disable ULPS.
 *
 * ULPS can be enabled/disabled after DSI host engine is turned on.
 *
 * Return: error code.
 */
static int dsi_display_set_ulps(struct dsi_display *display, bool enable)
{
	int rc = 0;
	int i = 0;
	struct dsi_display_ctrl *m_ctrl, *ctrl;


	if (!display) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (!dsi_display_is_ulps_req_valid(display, enable)) {
		DSI_DEBUG("%s: skipping ULPS config, enable=%d\n",
			__func__, enable);
		return 0;
	}

	m_ctrl = &display->ctrl[display->cmd_master_idx];
	/*
	 * ULPS entry-exit can be either through the DSI controller or
	 * the DSI PHY depending on hardware variation. For some chipsets,
	 * both controller version and phy version ulps entry-exit ops can
	 * be present. To handle such cases, send ulps request through PHY,
	 * if ulps request is handled in PHY, then no need to send request
	 * through controller.
	 */

	rc = dsi_phy_set_ulps(m_ctrl->phy, &display->config, enable,
			display->clamp_enabled);

	if (rc == DSI_PHY_ULPS_ERROR) {
		DSI_ERR("Ulps PHY state change(%d) failed\n", enable);
		return -EINVAL;
	}

	else if (rc == DSI_PHY_ULPS_HANDLED) {
		display_for_each_ctrl(i, display) {
			ctrl = &display->ctrl[i];
			if (!ctrl->ctrl || (ctrl == m_ctrl))
				continue;

			rc = dsi_phy_set_ulps(ctrl->phy, &display->config,
					enable, display->clamp_enabled);
			if (rc == DSI_PHY_ULPS_ERROR) {
				DSI_ERR("Ulps PHY state change(%d) failed\n",
						enable);
				return -EINVAL;
			}
		}
	}

	else if (rc == DSI_PHY_ULPS_NOT_HANDLED) {
		rc = dsi_ctrl_set_ulps(m_ctrl->ctrl, enable);
		if (rc) {
			DSI_ERR("Ulps controller state change(%d) failed\n",
					enable);
			return rc;
		}
		display_for_each_ctrl(i, display) {
			ctrl = &display->ctrl[i];
			if (!ctrl->ctrl || (ctrl == m_ctrl))
				continue;

			rc = dsi_ctrl_set_ulps(ctrl->ctrl, enable);
			if (rc) {
				DSI_ERR("Ulps controller state change(%d) failed\n",
						enable);
				return rc;
			}
		}
	}

	display->ulps_enabled = enable;
	return 0;
}

/**
 * dsi_display_set_clamp() - set clamp state for DSI IO.
 * @dsi_display:         DSI display handle.
 * @enable:           enable/disable clamping.
 *
 * Return: error code.
 */
static int dsi_display_set_clamp(struct dsi_display *display, bool enable)
{
	int rc = 0;
	int i = 0;
	struct dsi_display_ctrl *m_ctrl, *ctrl;
	bool ulps_enabled = false;


	if (!display) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	m_ctrl = &display->ctrl[display->cmd_master_idx];
	ulps_enabled = display->ulps_enabled;

	/*
	 * Clamp control can be either through the DSI controller or
	 * the DSI PHY depending on hardware variation
	 */
	rc = dsi_ctrl_set_clamp_state(m_ctrl->ctrl, enable, ulps_enabled);
	if (rc) {
		DSI_ERR("DSI ctrl clamp state change(%d) failed\n", enable);
		return rc;
	}

	rc = dsi_phy_set_clamp_state(m_ctrl->phy, enable);
	if (rc) {
		DSI_ERR("DSI phy clamp state change(%d) failed\n", enable);
		return rc;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || (ctrl == m_ctrl))
			continue;

		rc = dsi_ctrl_set_clamp_state(ctrl->ctrl, enable, ulps_enabled);
		if (rc) {
			DSI_ERR("DSI Clamp state change(%d) failed\n", enable);
			return rc;
		}

		rc = dsi_phy_set_clamp_state(ctrl->phy, enable);
		if (rc) {
			DSI_ERR("DSI phy clamp state change(%d) failed\n",
				enable);
			return rc;
		}

		DSI_DEBUG("Clamps %s for ctrl%d\n",
			enable ? "enabled" : "disabled", i);
	}

	display->clamp_enabled = enable;
	return 0;
}

/**
 * dsi_display_setup_ctrl() - setup DSI controller.
 * @dsi_display:         DSI display handle.
 *
 * Return: error code.
 */
static int dsi_display_ctrl_setup(struct dsi_display *display)
{
	int rc = 0;
	int i = 0;
	struct dsi_display_ctrl *ctrl, *m_ctrl;


	if (!display) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	m_ctrl = &display->ctrl[display->cmd_master_idx];
	rc = dsi_ctrl_setup(m_ctrl->ctrl);
	if (rc) {
		DSI_ERR("DSI controller setup failed\n");
		return rc;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || (ctrl == m_ctrl))
			continue;

		rc = dsi_ctrl_setup(ctrl->ctrl);
		if (rc) {
			DSI_ERR("DSI controller setup failed\n");
			return rc;
		}
	}
	return 0;
}

static int dsi_display_phy_enable(struct dsi_display *display);

/**
 * dsi_display_phy_idle_on() - enable DSI PHY while coming out of idle screen.
 * @dsi_display:         DSI display handle.
 * @mmss_clamp:          True if clamp is enabled.
 *
 * Return: error code.
 */
static int dsi_display_phy_idle_on(struct dsi_display *display,
		bool mmss_clamp)
{
	int rc = 0;
	int i = 0;
	struct dsi_display_ctrl *m_ctrl, *ctrl;


	if (!display) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (mmss_clamp && !display->phy_idle_power_off) {
		dsi_display_phy_enable(display);
		return 0;
	}

	m_ctrl = &display->ctrl[display->cmd_master_idx];
	rc = dsi_phy_idle_ctrl(m_ctrl->phy, true);
	if (rc) {
		DSI_ERR("DSI controller setup failed\n");
		return rc;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || (ctrl == m_ctrl))
			continue;

		rc = dsi_phy_idle_ctrl(ctrl->phy, true);
		if (rc) {
			DSI_ERR("DSI controller setup failed\n");
			return rc;
		}
	}
	display->phy_idle_power_off = false;
	return 0;
}

/**
 * dsi_display_phy_idle_off() - disable DSI PHY while going to idle screen.
 * @dsi_display:         DSI display handle.
 *
 * Return: error code.
 */
static int dsi_display_phy_idle_off(struct dsi_display *display)
{
	int rc = 0;
	int i = 0;
	struct dsi_display_ctrl *m_ctrl, *ctrl;

	if (!display) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	display_for_each_ctrl(i, display) {
		struct msm_dsi_phy *phy = display->ctrl[i].phy;

		if (!phy)
			continue;

		if (!phy->allow_phy_power_off) {
			DSI_DEBUG("phy doesn't support this feature\n");
			return 0;
		}
	}

	m_ctrl = &display->ctrl[display->cmd_master_idx];

	rc = dsi_phy_idle_ctrl(m_ctrl->phy, false);
	if (rc) {
		DSI_ERR("[%s] failed to enable cmd engine, rc=%d\n",
		       display->name, rc);
		return rc;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || (ctrl == m_ctrl))
			continue;

		rc = dsi_phy_idle_ctrl(ctrl->phy, false);
		if (rc) {
			DSI_ERR("DSI controller setup failed\n");
			return rc;
		}
	}
	display->phy_idle_power_off = true;
	return 0;
}

void dsi_display_enable_event(struct drm_connector *connector,
		struct dsi_display *display,
		uint32_t event_idx, struct dsi_event_cb_info *event_info,
		bool enable)
{
	uint32_t irq_status_idx = DSI_STATUS_INTERRUPT_COUNT;
	int i;

	if (!display) {
		DSI_ERR("invalid display\n");
		return;
	}

	if (event_info)
		event_info->event_idx = event_idx;

	switch (event_idx) {
	case SDE_CONN_EVENT_VID_DONE:
		irq_status_idx = DSI_SINT_VIDEO_MODE_FRAME_DONE;
		break;
	case SDE_CONN_EVENT_CMD_DONE:
		irq_status_idx = DSI_SINT_CMD_FRAME_DONE;
		break;
	case SDE_CONN_EVENT_VID_FIFO_OVERFLOW:
	case SDE_CONN_EVENT_CMD_FIFO_UNDERFLOW:
		if (event_info) {
			display_for_each_ctrl(i, display)
				display->ctrl[i].ctrl->recovery_cb =
							*event_info;
		}
		break;
	default:
		/* nothing to do */
		DSI_DEBUG("[%s] unhandled event %d\n", display->name, event_idx);
		return;
	}

	if (enable) {
		display_for_each_ctrl(i, display)
			dsi_ctrl_enable_status_interrupt(
					display->ctrl[i].ctrl, irq_status_idx,
					event_info);
	} else {
		display_for_each_ctrl(i, display)
			dsi_ctrl_disable_status_interrupt(
					display->ctrl[i].ctrl, irq_status_idx);
	}
}

/**
 * dsi_config_host_engine_state_for_cont_splash()- update host engine state
 *                                                 during continuous splash.
 * @display: Handle to dsi display
 *
 */
static void dsi_config_host_engine_state_for_cont_splash
					(struct dsi_display *display)
{
	int i;
	struct dsi_display_ctrl *ctrl;
	enum dsi_engine_state host_state = DSI_CTRL_ENGINE_ON;

	/* Sequence does not matter for split dsi usecases */
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl)
			continue;

		dsi_ctrl_update_host_engine_state_for_cont_splash(ctrl->ctrl,
							host_state);
	}
}

static int dsi_display_ctrl_power_on(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *ctrl;

	/* Sequence does not matter for split dsi usecases */
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl)
			continue;

		rc = dsi_ctrl_set_power_state(ctrl->ctrl,
					      DSI_CTRL_POWER_VREG_ON);
		if (rc) {
			DSI_ERR("[%s] Failed to set power state, rc=%d\n",
			       ctrl->ctrl->name, rc);
			goto error;
		}
	}

	return rc;
error:
	for (i = i - 1; i >= 0; i--) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl)
			continue;
		(void)dsi_ctrl_set_power_state(ctrl->ctrl,
			DSI_CTRL_POWER_VREG_OFF);
	}
	return rc;
}

static int dsi_display_ctrl_power_off(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *ctrl;

	/* Sequence does not matter for split dsi usecases */
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl)
			continue;

		rc = dsi_ctrl_set_power_state(ctrl->ctrl,
			DSI_CTRL_POWER_VREG_OFF);
		if (rc) {
			DSI_ERR("[%s] Failed to power off, rc=%d\n",
			       ctrl->ctrl->name, rc);
			goto error;
		}
	}
error:
	return rc;
}

static void dsi_display_parse_cmdline_topology(struct dsi_display *display,
					unsigned int display_type)
{
	char *boot_str = NULL;
	char *str = NULL;
	char *sw_te = NULL;
	unsigned long cmdline_topology = NO_OVERRIDE;
	unsigned long cmdline_timing = NO_OVERRIDE;

	if (display_type >= MAX_DSI_ACTIVE_DISPLAY) {
		DSI_ERR("display_type=%d not supported\n", display_type);
		goto end;
	}

	if (display_type == DSI_PRIMARY)
		boot_str = dsi_display_primary;
	else
		boot_str = dsi_display_secondary;

	sw_te = strnstr(boot_str, ":swte", strlen(boot_str));
	if (sw_te)
		display->sw_te_using_wd = true;

	str = strnstr(boot_str, ":config", strlen(boot_str));
	if (str) {
		if (sscanf(str, ":config%lu", &cmdline_topology) != 1) {
			DSI_ERR("invalid config index override: %s\n",
				boot_str);
			goto end;
		}
	}

	str = strnstr(boot_str, ":timing", strlen(boot_str));
	if (str) {
		if (sscanf(str, ":timing%lu", &cmdline_timing) != 1) {
			DSI_ERR("invalid timing index override: %s\n",
				boot_str);
			cmdline_topology = NO_OVERRIDE;
			goto end;
		}
	}
	DSI_DEBUG("successfully parsed command line topology and timing\n");
end:
	display->cmdline_topology = cmdline_topology;
	display->cmdline_timing = cmdline_timing;
}

/**
 * dsi_display_parse_boot_display_selection()- Parse DSI boot display name
 *
 * Return:	returns error status
 */
static int dsi_display_parse_boot_display_selection(void)
{
	char *pos = NULL;
	char disp_buf[MAX_CMDLINE_PARAM_LEN] = {'\0'};
	int i, j;

	for (i = 0; i < MAX_DSI_ACTIVE_DISPLAY; i++) {
		strlcpy(disp_buf, boot_displays[i].boot_param,
			MAX_CMDLINE_PARAM_LEN);

		pos = strnstr(disp_buf, ":", MAX_CMDLINE_PARAM_LEN);

		/* Use ':' as a delimiter to retrieve the display name */
		if (!pos) {
			DSI_DEBUG("display name[%s]is not valid\n", disp_buf);
			continue;
		}

		for (j = 0; (disp_buf + j) < pos; j++)
			boot_displays[i].name[j] = *(disp_buf + j);

		boot_displays[i].name[j] = '\0';

		boot_displays[i].boot_disp_en = true;
	}

	return 0;
}

static int dsi_display_phy_power_on(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *ctrl;

	/* Sequence does not matter for split dsi usecases */
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl)
			continue;

		rc = dsi_phy_set_power_state(ctrl->phy, true);
		if (rc) {
			DSI_ERR("[%s] Failed to set power state, rc=%d\n",
			       ctrl->phy->name, rc);
			goto error;
		}
	}

	return rc;
error:
	for (i = i - 1; i >= 0; i--) {
		ctrl = &display->ctrl[i];
		if (!ctrl->phy)
			continue;
		(void)dsi_phy_set_power_state(ctrl->phy, false);
	}
	return rc;
}

static int dsi_display_phy_power_off(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *ctrl;

	/* Sequence does not matter for split dsi usecases */
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->phy)
			continue;

		rc = dsi_phy_set_power_state(ctrl->phy, false);
		if (rc) {
			DSI_ERR("[%s] Failed to power off, rc=%d\n",
			       ctrl->ctrl->name, rc);
			goto error;
		}
	}
error:
	return rc;
}

static int dsi_display_set_clk_src(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *m_ctrl, *ctrl;

	/*
	 * For CPHY mode, the parent of mux_clks need to be set
	 * to Cphy_clks to have correct dividers for byte and
	 * pixel clocks.
	 */
	if (display->panel->host_config.phy_type == DSI_PHY_TYPE_CPHY) {
		rc = dsi_clk_update_parent(&display->clock_info.cphy_clks,
			      &display->clock_info.mux_clks);
		if (rc) {
			DSI_ERR("failed update mux parent to shadow\n");
			return rc;
		}
	}

	/*
	 * In case of split DSI usecases, the clock for master controller should
	 * be enabled before the other controller. Master controller in the
	 * clock context refers to the controller that sources the clock.
	 */
	m_ctrl = &display->ctrl[display->clk_master_idx];

	rc = dsi_ctrl_set_clock_source(m_ctrl->ctrl,
				&display->clock_info.mux_clks);
	if (rc) {
		DSI_ERR("[%s] failed to set source clocks for master, rc=%d\n",
			   display->name, rc);
		return rc;
	}

	/* Turn on rest of the controllers */
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || (ctrl == m_ctrl))
			continue;

		rc = dsi_ctrl_set_clock_source(ctrl->ctrl,
					&display->clock_info.mux_clks);
		if (rc) {
			DSI_ERR("[%s] failed to set source clocks, rc=%d\n",
				   display->name, rc);
			return rc;
		}
	}
	return 0;
}

static int dsi_display_phy_reset_config(struct dsi_display *display,
		bool enable)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *ctrl;

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		rc = dsi_ctrl_phy_reset_config(ctrl->ctrl, enable);
		if (rc) {
			DSI_ERR("[%s] failed to %s phy reset, rc=%d\n",
			       display->name, enable ? "mask" : "unmask", rc);
			return rc;
		}
	}
	return 0;
}

static void dsi_display_toggle_resync_fifo(struct dsi_display *display)
{
	struct dsi_display_ctrl *ctrl;
	int i;

	if (!display)
		return;

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		dsi_phy_toggle_resync_fifo(ctrl->phy);
	}

	/*
	 * After retime buffer synchronization we need to turn of clk_en_sel
	 * bit on each phy. Avoid this for Cphy.
	 */

	if (display->panel->host_config.phy_type == DSI_PHY_TYPE_CPHY)
		return;

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		dsi_phy_reset_clk_en_sel(ctrl->phy);
	}

}

static int dsi_display_ctrl_update(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *ctrl;

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		rc = dsi_ctrl_host_timing_update(ctrl->ctrl);
		if (rc) {
			DSI_ERR("[%s] failed to update host_%d, rc=%d\n",
				   display->name, i, rc);
			goto error_host_deinit;
		}
	}

	return 0;
error_host_deinit:
	for (i = i - 1; i >= 0; i--) {
		ctrl = &display->ctrl[i];
		(void)dsi_ctrl_host_deinit(ctrl->ctrl);
	}

	return rc;
}

static int dsi_display_ctrl_init(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *ctrl;

	/* when ULPS suspend feature is enabled, we will keep the lanes in
	 * ULPS during suspend state and clamp DSI phy. Hence while resuming
	 * we will programe DSI controller as part of core clock enable.
	 * After that we should not re-configure DSI controller again here for
	 * usecases where we are resuming from ulps suspend as it might put
	 * the HW in bad state.
	 */
	if (!display->panel->ulps_suspend_enabled || !display->ulps_enabled) {
		display_for_each_ctrl(i, display) {
			ctrl = &display->ctrl[i];
			rc = dsi_ctrl_host_init(ctrl->ctrl,
					display->is_cont_splash_enabled);
			if (rc) {
				DSI_ERR("[%s] failed to init host_%d, rc=%d\n",
				       display->name, i, rc);
				goto error_host_deinit;
			}
		}
	} else {
		display_for_each_ctrl(i, display) {
			ctrl = &display->ctrl[i];
			rc = dsi_ctrl_update_host_state(ctrl->ctrl,
							DSI_CTRL_OP_HOST_INIT,
							true);
			if (rc)
				DSI_DEBUG("host init update failed rc=%d\n",
						rc);
		}
	}

	return rc;
error_host_deinit:
	for (i = i - 1; i >= 0; i--) {
		ctrl = &display->ctrl[i];
		(void)dsi_ctrl_host_deinit(ctrl->ctrl);
	}
	return rc;
}

static int dsi_display_ctrl_deinit(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *ctrl;

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		rc = dsi_ctrl_host_deinit(ctrl->ctrl);
		if (rc) {
			DSI_ERR("[%s] failed to deinit host_%d, rc=%d\n",
			       display->name, i, rc);
		}
	}

	return rc;
}

static int dsi_display_ctrl_host_enable(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *m_ctrl, *ctrl;

	/* Host engine states are already taken care for
	 * continuous splash case
	 */
	if (display->is_cont_splash_enabled) {
		DSI_DEBUG("cont splash enabled, host enable not required\n");
		return 0;
	}

	m_ctrl = &display->ctrl[display->cmd_master_idx];

	rc = dsi_ctrl_set_host_engine_state(m_ctrl->ctrl, DSI_CTRL_ENGINE_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable host engine, rc=%d\n",
		       display->name, rc);
		goto error;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || (ctrl == m_ctrl))
			continue;

		rc = dsi_ctrl_set_host_engine_state(ctrl->ctrl,
						    DSI_CTRL_ENGINE_ON);
		if (rc) {
			DSI_ERR("[%s] failed to enable sl host engine, rc=%d\n",
			       display->name, rc);
			goto error_disable_master;
		}
	}

	return rc;
error_disable_master:
	(void)dsi_ctrl_set_host_engine_state(m_ctrl->ctrl, DSI_CTRL_ENGINE_OFF);
error:
	return rc;
}

static int dsi_display_ctrl_host_disable(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *m_ctrl, *ctrl;

	m_ctrl = &display->ctrl[display->cmd_master_idx];
	/*
	 * For platforms where ULPS is controlled by DSI controller block,
	 * do not disable dsi controller block if lanes are to be
	 * kept in ULPS during suspend. So just update the SW state
	 * and return early.
	 */
	if (display->panel->ulps_suspend_enabled &&
			!m_ctrl->phy->hw.ops.ulps_ops.ulps_request) {
		display_for_each_ctrl(i, display) {
			ctrl = &display->ctrl[i];
			rc = dsi_ctrl_update_host_state(ctrl->ctrl,
					DSI_CTRL_OP_HOST_ENGINE,
					false);
			if (rc)
				DSI_DEBUG("host state update failed %d\n", rc);
		}
		return rc;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || (ctrl == m_ctrl))
			continue;

		rc = dsi_ctrl_set_host_engine_state(ctrl->ctrl,
						    DSI_CTRL_ENGINE_OFF);
		if (rc)
			DSI_ERR("[%s] failed to disable host engine, rc=%d\n",
			       display->name, rc);
	}

	rc = dsi_ctrl_set_host_engine_state(m_ctrl->ctrl, DSI_CTRL_ENGINE_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable host engine, rc=%d\n",
		       display->name, rc);
		goto error;
	}

error:
	return rc;
}

static int dsi_display_vid_engine_enable(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *m_ctrl, *ctrl;

	m_ctrl = &display->ctrl[display->video_master_idx];

	rc = dsi_ctrl_set_vid_engine_state(m_ctrl->ctrl, DSI_CTRL_ENGINE_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable vid engine, rc=%d\n",
		       display->name, rc);
		goto error;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || (ctrl == m_ctrl))
			continue;

		rc = dsi_ctrl_set_vid_engine_state(ctrl->ctrl,
						   DSI_CTRL_ENGINE_ON);
		if (rc) {
			DSI_ERR("[%s] failed to enable vid engine, rc=%d\n",
			       display->name, rc);
			goto error_disable_master;
		}
	}

	return rc;
error_disable_master:
	(void)dsi_ctrl_set_vid_engine_state(m_ctrl->ctrl, DSI_CTRL_ENGINE_OFF);
error:
	return rc;
}

static int dsi_display_vid_engine_disable(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *m_ctrl, *ctrl;

	m_ctrl = &display->ctrl[display->video_master_idx];

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || (ctrl == m_ctrl))
			continue;

		rc = dsi_ctrl_set_vid_engine_state(ctrl->ctrl,
						   DSI_CTRL_ENGINE_OFF);
		if (rc)
			DSI_ERR("[%s] failed to disable vid engine, rc=%d\n",
			       display->name, rc);
	}

	rc = dsi_ctrl_set_vid_engine_state(m_ctrl->ctrl, DSI_CTRL_ENGINE_OFF);
	if (rc)
		DSI_ERR("[%s] failed to disable mvid engine, rc=%d\n",
		       display->name, rc);

	return rc;
}

static int dsi_display_phy_enable(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *m_ctrl, *ctrl;
	enum dsi_phy_pll_source m_src = DSI_PLL_SOURCE_STANDALONE;

	m_ctrl = &display->ctrl[display->clk_master_idx];
	if (display->ctrl_count > 1)
		m_src = DSI_PLL_SOURCE_NATIVE;

	rc = dsi_phy_enable(m_ctrl->phy,
			    &display->config,
			    m_src,
			    true,
			    display->is_cont_splash_enabled);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI PHY, rc=%d\n",
		       display->name, rc);
		goto error;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || (ctrl == m_ctrl))
			continue;

		rc = dsi_phy_enable(ctrl->phy,
				    &display->config,
				    DSI_PLL_SOURCE_NON_NATIVE,
				    true,
				    display->is_cont_splash_enabled);
		if (rc) {
			DSI_ERR("[%s] failed to enable DSI PHY, rc=%d\n",
			       display->name, rc);
			goto error_disable_master;
		}
	}

	return rc;

error_disable_master:
	(void)dsi_phy_disable(m_ctrl->phy);
error:
	return rc;
}

static int dsi_display_phy_disable(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *m_ctrl, *ctrl;

	m_ctrl = &display->ctrl[display->clk_master_idx];

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || (ctrl == m_ctrl))
			continue;

		rc = dsi_phy_disable(ctrl->phy);
		if (rc)
			DSI_ERR("[%s] failed to disable DSI PHY, rc=%d\n",
			       display->name, rc);
	}

	rc = dsi_phy_disable(m_ctrl->phy);
	if (rc)
		DSI_ERR("[%s] failed to disable DSI PHY, rc=%d\n",
		       display->name, rc);

	return rc;
}

static int dsi_display_wake_up(struct dsi_display *display)
{
	return 0;
}

static void dsi_display_mask_overflow(struct dsi_display *display, u32 flags,
						bool enable)
{
	struct dsi_display_ctrl *ctrl;
	int i;

	if (!(flags & DSI_CTRL_CMD_LAST_COMMAND))
		return;

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl)
			continue;
		dsi_ctrl_mask_overflow(ctrl->ctrl, enable);
	}
}

static int dsi_display_broadcast_cmd(struct dsi_display *display,
				     const struct mipi_dsi_msg *msg)
{
	int rc = 0;
	u32 flags, m_flags;
	struct dsi_display_ctrl *ctrl, *m_ctrl;
	int i;

	m_flags = (DSI_CTRL_CMD_BROADCAST | DSI_CTRL_CMD_BROADCAST_MASTER |
		   DSI_CTRL_CMD_DEFER_TRIGGER | DSI_CTRL_CMD_FETCH_MEMORY);
	flags = (DSI_CTRL_CMD_BROADCAST | DSI_CTRL_CMD_DEFER_TRIGGER |
		 DSI_CTRL_CMD_FETCH_MEMORY);

	if ((msg->flags & MIPI_DSI_MSG_LASTCOMMAND)) {
		flags |= DSI_CTRL_CMD_LAST_COMMAND;
		m_flags |= DSI_CTRL_CMD_LAST_COMMAND;
	}

	if (display->queue_cmd_waits ||
			msg->flags & MIPI_DSI_MSG_ASYNC_OVERRIDE) {
		flags |= DSI_CTRL_CMD_ASYNC_WAIT;
		m_flags |= DSI_CTRL_CMD_ASYNC_WAIT;
	}

	/*
	 * 1. Setup commands in FIFO
	 * 2. Trigger commands
	 */
	m_ctrl = &display->ctrl[display->cmd_master_idx];
	dsi_display_mask_overflow(display, m_flags, true);
	rc = dsi_ctrl_cmd_transfer(m_ctrl->ctrl, msg, &m_flags);
	if (rc) {
		DSI_ERR("[%s] cmd transfer failed on master,rc=%d\n",
		       display->name, rc);
		goto error;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (ctrl == m_ctrl)
			continue;

		rc = dsi_ctrl_cmd_transfer(ctrl->ctrl, msg, &flags);
		if (rc) {
			DSI_ERR("[%s] cmd transfer failed, rc=%d\n",
			       display->name, rc);
			goto error;
		}

		rc = dsi_ctrl_cmd_tx_trigger(ctrl->ctrl, flags);
		if (rc) {
			DSI_ERR("[%s] cmd trigger failed, rc=%d\n",
			       display->name, rc);
			goto error;
		}
	}

	rc = dsi_ctrl_cmd_tx_trigger(m_ctrl->ctrl, m_flags);
	if (rc) {
		DSI_ERR("[%s] cmd trigger failed for master, rc=%d\n",
		       display->name, rc);
		goto error;
	}

error:
	dsi_display_mask_overflow(display, m_flags, false);
	return rc;
}

static int dsi_display_phy_sw_reset(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *m_ctrl, *ctrl;

	/* For continuous splash use case ctrl states are updated
	 * separately and hence we do an early return
	 */
	if (display->is_cont_splash_enabled) {
		DSI_DEBUG("cont splash enabled, phy sw reset not required\n");
		return 0;
	}

	m_ctrl = &display->ctrl[display->cmd_master_idx];

	rc = dsi_ctrl_phy_sw_reset(m_ctrl->ctrl);
	if (rc) {
		DSI_ERR("[%s] failed to reset phy, rc=%d\n", display->name, rc);
		goto error;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || (ctrl == m_ctrl))
			continue;

		rc = dsi_ctrl_phy_sw_reset(ctrl->ctrl);
		if (rc) {
			DSI_ERR("[%s] failed to reset phy, rc=%d\n",
			       display->name, rc);
			goto error;
		}
	}

error:
	return rc;
}

static int dsi_host_attach(struct mipi_dsi_host *host,
			   struct mipi_dsi_device *dsi)
{
	return 0;
}

static int dsi_host_detach(struct mipi_dsi_host *host,
			   struct mipi_dsi_device *dsi)
{
	return 0;
}

static ssize_t dsi_host_transfer(struct mipi_dsi_host *host,
				 const struct mipi_dsi_msg *msg)
{
	struct dsi_display *display;
	int rc = 0, ret = 0;

	if (!host || !msg) {
		DSI_ERR("Invalid params\n");
		return 0;
	}

	display = to_dsi_display(host);

	/* Avoid sending DCS commands when ESD recovery is pending */
	if (atomic_read(&display->panel->esd_recovery_pending)) {
		DSI_DEBUG("ESD recovery pending\n");
		return 0;
	}

	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable all DSI clocks, rc=%d\n",
		       display->name, rc);
		goto error;
	}

	rc = dsi_display_wake_up(display);
	if (rc) {
		DSI_ERR("[%s] failed to wake up display, rc=%d\n",
		       display->name, rc);
		goto error_disable_clks;
	}

	rc = dsi_display_cmd_engine_enable(display);
	if (rc) {
		DSI_ERR("[%s] failed to enable cmd engine, rc=%d\n",
		       display->name, rc);
		goto error_disable_clks;
	}

	if (display->tx_cmd_buf == NULL) {
		rc = dsi_host_alloc_cmd_tx_buffer(display);
		if (rc) {
			DSI_ERR("failed to allocate cmd tx buffer memory\n");
			goto error_disable_cmd_engine;
		}
	}

	if (display->ctrl_count > 1 && !(msg->flags & MIPI_DSI_MSG_UNICAST)) {
		rc = dsi_display_broadcast_cmd(display, msg);
		if (rc) {
			DSI_ERR("[%s] cmd broadcast failed, rc=%d\n",
			       display->name, rc);
			goto error_disable_cmd_engine;
		}
	} else {
		int ctrl_idx = (msg->flags & MIPI_DSI_MSG_UNICAST) ?
				msg->ctrl : 0;
		u32 cmd_flags = DSI_CTRL_CMD_FETCH_MEMORY;

		if (display->queue_cmd_waits ||
				msg->flags & MIPI_DSI_MSG_ASYNC_OVERRIDE)
			cmd_flags |= DSI_CTRL_CMD_ASYNC_WAIT;

#if defined(CONFIG_PXLW_IRIS)
		if (iris_is_chip_supported()) {
			if (msg->rx_buf && msg->rx_len)
				cmd_flags |= DSI_CTRL_CMD_READ;
		}
#endif
		rc = dsi_ctrl_cmd_transfer(display->ctrl[ctrl_idx].ctrl, msg,
				&cmd_flags);
#if defined(CONFIG_PXLW_IRIS)
		if (iris_is_chip_supported()) {
			if (rc > 0)
				rc = 0;
		}
#endif
		if (rc) {
			DSI_ERR("[%s] cmd transfer failed, rc=%d\n",
			       display->name, rc);
			goto error_disable_cmd_engine;
		}
	}

error_disable_cmd_engine:
	ret = dsi_display_cmd_engine_disable(display);
	if (ret) {
		DSI_ERR("[%s]failed to disable DSI cmd engine, rc=%d\n",
				display->name, ret);
	}
error_disable_clks:
	ret = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_OFF);
	if (ret) {
		DSI_ERR("[%s] failed to disable all DSI clocks, rc=%d\n",
		       display->name, ret);
	}
error:
	return rc;
}


static struct mipi_dsi_host_ops dsi_host_ops = {
	.attach = dsi_host_attach,
	.detach = dsi_host_detach,
	.transfer = dsi_host_transfer,
};

static int dsi_display_mipi_host_init(struct dsi_display *display)
{
	int rc = 0;
	struct mipi_dsi_host *host = &display->host;

	host->dev = &display->pdev->dev;
	host->ops = &dsi_host_ops;

	rc = mipi_dsi_host_register(host);
	if (rc) {
		DSI_ERR("[%s] failed to register mipi dsi host, rc=%d\n",
		       display->name, rc);
		goto error;
	}

error:
	return rc;
}
static int dsi_display_mipi_host_deinit(struct dsi_display *display)
{
	int rc = 0;
	struct mipi_dsi_host *host = &display->host;

	mipi_dsi_host_unregister(host);

	host->dev = NULL;
	host->ops = NULL;

	return rc;
}

static int dsi_display_clocks_deinit(struct dsi_display *display)
{
	int rc = 0;
	struct dsi_clk_link_set *src = &display->clock_info.src_clks;
	struct dsi_clk_link_set *mux = &display->clock_info.mux_clks;
	struct dsi_clk_link_set *shadow = &display->clock_info.shadow_clks;

	if (src->byte_clk) {
		devm_clk_put(&display->pdev->dev, src->byte_clk);
		src->byte_clk = NULL;
	}

	if (src->pixel_clk) {
		devm_clk_put(&display->pdev->dev, src->pixel_clk);
		src->pixel_clk = NULL;
	}

	if (mux->byte_clk) {
		devm_clk_put(&display->pdev->dev, mux->byte_clk);
		mux->byte_clk = NULL;
	}

	if (mux->pixel_clk) {
		devm_clk_put(&display->pdev->dev, mux->pixel_clk);
		mux->pixel_clk = NULL;
	}

	if (shadow->byte_clk) {
		devm_clk_put(&display->pdev->dev, shadow->byte_clk);
		shadow->byte_clk = NULL;
	}

	if (shadow->pixel_clk) {
		devm_clk_put(&display->pdev->dev, shadow->pixel_clk);
		shadow->pixel_clk = NULL;
	}

	return rc;
}

static bool dsi_display_check_prefix(const char *clk_prefix,
					const char *clk_name)
{
	return !!strnstr(clk_name, clk_prefix, strlen(clk_name));
}

static int dsi_display_get_clocks_count(struct dsi_display *display,
						char *dsi_clk_name)
{
	if (display->fw)
		return dsi_parser_count_strings(display->parser_node,
			dsi_clk_name);
	else
		return of_property_count_strings(display->panel_node,
			dsi_clk_name);
}

static void dsi_display_get_clock_name(struct dsi_display *display,
					char *dsi_clk_name, int index,
					const char **clk_name)
{
	if (display->fw)
		dsi_parser_read_string_index(display->parser_node,
			dsi_clk_name, index, clk_name);
	else
		of_property_read_string_index(display->panel_node,
			dsi_clk_name, index, clk_name);
}

static int dsi_display_clocks_init(struct dsi_display *display)
{
	int i, rc = 0, num_clk = 0;
	const char *clk_name;
	const char *src_byte = "src_byte", *src_pixel = "src_pixel";
	const char *mux_byte = "mux_byte", *mux_pixel = "mux_pixel";
	const char *cphy_byte = "cphy_byte", *cphy_pixel = "cphy_pixel";
	const char *shadow_byte = "shadow_byte", *shadow_pixel = "shadow_pixel";
	const char *shadow_cphybyte = "shadow_cphybyte",
		   *shadow_cphypixel = "shadow_cphypixel";
	struct clk *dsi_clk;
	struct clk *bb_clk2;
	struct dsi_clk_link_set *src = &display->clock_info.src_clks;
	struct dsi_clk_link_set *mux = &display->clock_info.mux_clks;
	struct dsi_clk_link_set *cphy = &display->clock_info.cphy_clks;
	struct dsi_clk_link_set *shadow = &display->clock_info.shadow_clks;
	struct dsi_clk_link_set *shadow_cphy =
				&display->clock_info.shadow_cphy_clks;
	struct dsi_dyn_clk_caps *dyn_clk_caps = &(display->panel->dyn_clk_caps);
	char *dsi_clock_name;

	if (!strcmp(display->display_type, "primary"))
		dsi_clock_name = "qcom,dsi-select-clocks";
	else
		dsi_clock_name = "qcom,dsi-select-sec-clocks";

	num_clk = dsi_display_get_clocks_count(display, dsi_clock_name);

	DSI_DEBUG("clk count=%d\n", num_clk);

	for (i = 0; i < num_clk; i++) {
		dsi_display_get_clock_name(display, dsi_clock_name, i,
						&clk_name);

		DSI_DEBUG("clock name:%s\n", clk_name);

		dsi_clk = devm_clk_get(&display->pdev->dev, clk_name);
		if (IS_ERR_OR_NULL(dsi_clk)) {
			rc = PTR_ERR(dsi_clk);

			DSI_ERR("failed to get %s, rc=%d\n", clk_name, rc);

			if (dsi_display_check_prefix(mux_byte, clk_name)) {
				mux->byte_clk = NULL;
				goto error;
			}
			if (dsi_display_check_prefix(mux_pixel, clk_name)) {
				mux->pixel_clk = NULL;
				goto error;
			}

			if (dsi_display_check_prefix(cphy_byte, clk_name)) {
				cphy->byte_clk = NULL;
				goto error;
			}
			if (dsi_display_check_prefix(cphy_pixel, clk_name)) {
				cphy->pixel_clk = NULL;
				goto error;
			}

			if (dyn_clk_caps->dyn_clk_support &&
				(display->panel->panel_mode ==
					 DSI_OP_VIDEO_MODE)) {

				if (dsi_display_check_prefix(src_byte,
							clk_name))
					src->byte_clk = NULL;
				if (dsi_display_check_prefix(src_pixel,
							clk_name))
					src->pixel_clk = NULL;
				if (dsi_display_check_prefix(shadow_byte,
							clk_name))
					shadow->byte_clk = NULL;
				if (dsi_display_check_prefix(shadow_pixel,
							clk_name))
					shadow->pixel_clk = NULL;
				if (dsi_display_check_prefix(shadow_cphybyte,
							clk_name))
					shadow_cphy->byte_clk = NULL;
				if (dsi_display_check_prefix(shadow_cphypixel,
							clk_name))
					shadow_cphy->pixel_clk = NULL;

				dyn_clk_caps->dyn_clk_support = false;
			}
		}

		if (dsi_display_check_prefix(src_byte, clk_name)) {
			src->byte_clk = dsi_clk;
			continue;
		}

		if (dsi_display_check_prefix(src_pixel, clk_name)) {
			src->pixel_clk = dsi_clk;
			continue;
		}

		if (dsi_display_check_prefix(cphy_byte, clk_name)) {
			cphy->byte_clk = dsi_clk;
			continue;
		}

		if (dsi_display_check_prefix(cphy_pixel, clk_name)) {
			cphy->pixel_clk = dsi_clk;
			continue;
		}

		if (dsi_display_check_prefix(mux_byte, clk_name)) {
			mux->byte_clk = dsi_clk;
			continue;
		}

		if (dsi_display_check_prefix(mux_pixel, clk_name)) {
			mux->pixel_clk = dsi_clk;
			continue;
		}

		if (dsi_display_check_prefix(shadow_byte, clk_name)) {
			shadow->byte_clk = dsi_clk;
			continue;
		}

		if (dsi_display_check_prefix(shadow_pixel, clk_name)) {
			shadow->pixel_clk = dsi_clk;
			continue;
		}
		
		if (dsi_display_check_prefix(shadow_cphybyte, clk_name)) {
			shadow_cphy->byte_clk = dsi_clk;
			continue;
		}

		if (dsi_display_check_prefix(shadow_cphypixel, clk_name)) {
			shadow_cphy->pixel_clk = dsi_clk;
			continue;
		}
		if (dsi_display_check_prefix("pw_bb_clk2", clk_name)) {
			bb_clk2 = dsi_clk;
			if(get_oem_project() != 19811){
				clk_prepare_enable(bb_clk2);
				clk_disable_unprepare(bb_clk2); //disable the clk
				DSI_ERR("%s %d\n",__func__,get_oem_project());
			}
			continue;
			}
	}

	return 0;
error:
	(void)dsi_display_clocks_deinit(display);
	return rc;
}

static int dsi_display_clk_ctrl_cb(void *priv,
	struct dsi_clk_ctrl_info clk_state_info)
{
	int rc = 0;
	struct dsi_display *display = NULL;
	void *clk_handle = NULL;

	if (!priv) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	display = priv;

	if (clk_state_info.client == DSI_CLK_REQ_MDP_CLIENT) {
		clk_handle = display->mdp_clk_handle;
	} else if (clk_state_info.client == DSI_CLK_REQ_DSI_CLIENT) {
		clk_handle = display->dsi_clk_handle;
	} else {
		DSI_ERR("invalid clk handle, return error\n");
		return -EINVAL;
	}

	/*
	 * TODO: Wait for CMD_MDP_DONE interrupt if MDP client tries
	 * to turn off DSI clocks.
	 */
	rc = dsi_display_clk_ctrl(clk_handle,
		clk_state_info.clk_type, clk_state_info.clk_state);
	if (rc) {
		DSI_ERR("[%s] failed to %d DSI %d clocks, rc=%d\n",
		       display->name, clk_state_info.clk_state,
		       clk_state_info.clk_type, rc);
		return rc;
	}
	return 0;
}

static void dsi_display_ctrl_isr_configure(struct dsi_display *display, bool en)
{
	int i;
	struct dsi_display_ctrl *ctrl;

	if (!display)
		return;

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl)
			continue;
		dsi_ctrl_isr_configure(ctrl->ctrl, en);
	}
}

int dsi_pre_clkoff_cb(void *priv,
			   enum dsi_clk_type clk,
			   enum dsi_lclk_type l_type,
			   enum dsi_clk_state new_state)
{
	int rc = 0, i;
	struct dsi_display *display = priv;
	struct dsi_display_ctrl *ctrl;


	/*
	 * If Idle Power Collapse occurs immediately after a CMD
	 * transfer with an asynchronous wait for DMA done, ensure
	 * that the work queued is scheduled and completed before turning
	 * off the clocks and disabling interrupts to validate the command
	 * transfer.
	 */
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || !ctrl->ctrl->dma_wait_queued)
			continue;
		flush_workqueue(display->dma_cmd_workq);
		cancel_work_sync(&ctrl->ctrl->dma_cmd_wait);
		ctrl->ctrl->dma_wait_queued = false;
	}
	if ((clk & DSI_LINK_CLK) && (new_state == DSI_CLK_OFF) &&
		(l_type & DSI_LINK_LP_CLK)) {
		/*
		 * If continuous clock is enabled then disable it
		 * before entering into ULPS Mode.
		 */
		if (display->panel->host_config.force_hs_clk_lane)
			_dsi_display_continuous_clk_ctrl(display, false);
		/*
		 * If ULPS feature is enabled, enter ULPS first.
		 * However, when blanking the panel, we should enter ULPS
		 * only if ULPS during suspend feature is enabled.
		 */
		if (!dsi_panel_initialized(display->panel)) {
			if (display->panel->ulps_suspend_enabled)
				rc = dsi_display_set_ulps(display, true);
		} else if (dsi_panel_ulps_feature_enabled(display->panel)) {
			rc = dsi_display_set_ulps(display, true);
		}
		if (rc)
			DSI_ERR("%s: failed enable ulps, rc = %d\n",
			       __func__, rc);
	}

	if ((clk & DSI_LINK_CLK) && (new_state == DSI_CLK_OFF) &&
		(l_type & DSI_LINK_HS_CLK)) {
		/*
		 * PHY clock gating should be disabled before the PLL and the
		 * branch clocks are turned off. Otherwise, it is possible that
		 * the clock RCGs may not be turned off correctly resulting
		 * in clock warnings.
		 */
		rc = dsi_display_config_clk_gating(display, false);
		if (rc)
			DSI_ERR("[%s] failed to disable clk gating, rc=%d\n",
					display->name, rc);
	}

	if ((clk & DSI_CORE_CLK) && (new_state == DSI_CLK_OFF)) {
		/*
		 * Enable DSI clamps only if entering idle power collapse or
		 * when ULPS during suspend is enabled..
		 */
		if (dsi_panel_initialized(display->panel) ||
			display->panel->ulps_suspend_enabled) {
			dsi_display_phy_idle_off(display);
			rc = dsi_display_set_clamp(display, true);
			if (rc)
				DSI_ERR("%s: Failed to enable dsi clamps. rc=%d\n",
					__func__, rc);

			rc = dsi_display_phy_reset_config(display, false);
			if (rc)
				DSI_ERR("%s: Failed to reset phy, rc=%d\n",
						__func__, rc);
		} else {
			/* Make sure that controller is not in ULPS state when
			 * the DSI link is not active.
			 */
			rc = dsi_display_set_ulps(display, false);
			if (rc)
				DSI_ERR("%s: failed to disable ulps. rc=%d\n",
					__func__, rc);
		}
		/* dsi will not be able to serve irqs from here on */
		dsi_display_ctrl_irq_update(display, false);

		/* cache the MISR values */
		display_for_each_ctrl(i, display) {
			ctrl = &display->ctrl[i];
			if (!ctrl->ctrl)
				continue;
			dsi_ctrl_cache_misr(ctrl->ctrl);
		}

	}

	return rc;
}

int dsi_post_clkon_cb(void *priv,
			   enum dsi_clk_type clk,
			   enum dsi_lclk_type l_type,
			   enum dsi_clk_state curr_state)
{
	int rc = 0;
	struct dsi_display *display = priv;
	bool mmss_clamp = false;

	if ((clk & DSI_LINK_CLK) && (l_type & DSI_LINK_LP_CLK)) {
		mmss_clamp = display->clamp_enabled;
		/*
		 * controller setup is needed if coming out of idle
		 * power collapse with clamps enabled.
		 */
		if (mmss_clamp)
			dsi_display_ctrl_setup(display);

		/*
		 * Phy setup is needed if coming out of idle
		 * power collapse with clamps enabled.
		 */
		if (display->phy_idle_power_off || mmss_clamp)
			dsi_display_phy_idle_on(display, mmss_clamp);

		if (display->ulps_enabled && mmss_clamp) {
			/*
			 * ULPS Entry Request. This is needed if the lanes were
			 * in ULPS prior to power collapse, since after
			 * power collapse and reset, the DSI controller resets
			 * back to idle state and not ULPS. This ulps entry
			 * request will transition the state of the DSI
			 * controller to ULPS which will match the state of the
			 * DSI phy. This needs to be done prior to disabling
			 * the DSI clamps.
			 *
			 * Also, reset the ulps flag so that ulps_config
			 * function would reconfigure the controller state to
			 * ULPS.
			 */
			display->ulps_enabled = false;
			rc = dsi_display_set_ulps(display, true);
			if (rc) {
				DSI_ERR("%s: Failed to enter ULPS. rc=%d\n",
					__func__, rc);
				goto error;
			}
		}

		rc = dsi_display_phy_reset_config(display, true);
		if (rc) {
			DSI_ERR("%s: Failed to reset phy, rc=%d\n",
						__func__, rc);
			goto error;
		}

		rc = dsi_display_set_clamp(display, false);
		if (rc) {
			DSI_ERR("%s: Failed to disable dsi clamps. rc=%d\n",
				__func__, rc);
			goto error;
		}
	}

	if ((clk & DSI_LINK_CLK) && (l_type & DSI_LINK_HS_CLK)) {
		/*
		 * Toggle the resync FIFO everytime clock changes, except
		 * when cont-splash screen transition is going on.
		 * Toggling resync FIFO during cont splash transition
		 * can lead to blinks on the display.
		 */
		if (!display->is_cont_splash_enabled)
			dsi_display_toggle_resync_fifo(display);

		if (display->ulps_enabled) {
			rc = dsi_display_set_ulps(display, false);
			if (rc) {
				DSI_ERR("%s: failed to disable ulps, rc= %d\n",
				       __func__, rc);
				goto error;
			}
		}

		if (display->panel->host_config.force_hs_clk_lane)
			_dsi_display_continuous_clk_ctrl(display, true);

		rc = dsi_display_config_clk_gating(display, true);
		if (rc) {
			DSI_ERR("[%s] failed to enable clk gating %d\n",
					display->name, rc);
			goto error;
		}
	}

	/* enable dsi to serve irqs */
	if (clk & DSI_CORE_CLK)
		dsi_display_ctrl_irq_update(display, true);

error:
	return rc;
}

int dsi_post_clkoff_cb(void *priv,
			    enum dsi_clk_type clk_type,
			    enum dsi_lclk_type l_type,
			    enum dsi_clk_state curr_state)
{
	int rc = 0;
	struct dsi_display *display = priv;

	if (!display) {
		DSI_ERR("%s: Invalid arg\n", __func__);
		return -EINVAL;
	}

	if ((clk_type & DSI_CORE_CLK) &&
	    (curr_state == DSI_CLK_OFF)) {
		rc = dsi_display_phy_power_off(display);
		if (rc)
			DSI_ERR("[%s] failed to power off PHY, rc=%d\n",
				   display->name, rc);

		rc = dsi_display_ctrl_power_off(display);
		if (rc)
			DSI_ERR("[%s] failed to power DSI vregs, rc=%d\n",
				   display->name, rc);
	}
	return rc;
}

int dsi_pre_clkon_cb(void *priv,
			  enum dsi_clk_type clk_type,
			  enum dsi_lclk_type l_type,
			  enum dsi_clk_state new_state)
{
	int rc = 0;
	struct dsi_display *display = priv;

	if (!display) {
		DSI_ERR("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	if ((clk_type & DSI_CORE_CLK) && (new_state == DSI_CLK_ON)) {
		/*
		 * Enable DSI core power
		 * 1.> PANEL_PM are controlled as part of
		 *     panel_power_ctrl. Needed not be handled here.
		 * 2.> CTRL_PM need to be enabled/disabled
		 *     only during unblank/blank. Their state should
		 *     not be changed during static screen.
		 */

		DSI_DEBUG("updating power states for ctrl and phy\n");
		rc = dsi_display_ctrl_power_on(display);
		if (rc) {
			DSI_ERR("[%s] failed to power on dsi controllers, rc=%d\n",
				   display->name, rc);
			return rc;
		}

		rc = dsi_display_phy_power_on(display);
		if (rc) {
			DSI_ERR("[%s] failed to power on dsi phy, rc = %d\n",
				   display->name, rc);
			return rc;
		}

		DSI_DEBUG("%s: Enable DSI core power\n", __func__);
	}

	return rc;
}

static void __set_lane_map_v2(u8 *lane_map_v2,
	enum dsi_phy_data_lanes lane0,
	enum dsi_phy_data_lanes lane1,
	enum dsi_phy_data_lanes lane2,
	enum dsi_phy_data_lanes lane3)
{
	lane_map_v2[DSI_LOGICAL_LANE_0] = lane0;
	lane_map_v2[DSI_LOGICAL_LANE_1] = lane1;
	lane_map_v2[DSI_LOGICAL_LANE_2] = lane2;
	lane_map_v2[DSI_LOGICAL_LANE_3] = lane3;
}

static int dsi_display_parse_lane_map(struct dsi_display *display)
{
	int rc = 0, i = 0;
	const char *data;
	u8 temp[DSI_LANE_MAX - 1];

	if (!display) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	/* lane-map-v2 supersedes lane-map-v1 setting */
	rc = of_property_read_u8_array(display->pdev->dev.of_node,
		"qcom,lane-map-v2", temp, (DSI_LANE_MAX - 1));
	if (!rc) {
		for (i = DSI_LOGICAL_LANE_0; i < (DSI_LANE_MAX - 1); i++)
			display->lane_map.lane_map_v2[i] = BIT(temp[i]);
		return 0;
	} else if (rc != EINVAL) {
		DSI_DEBUG("Incorrect mapping, configure default\n");
		goto set_default;
	}

	/* lane-map older version, for DSI controller version < 2.0 */
	data = of_get_property(display->pdev->dev.of_node,
		"qcom,lane-map", NULL);
	if (!data)
		goto set_default;

	if (!strcmp(data, "lane_map_3012")) {
		display->lane_map.lane_map_v1 = DSI_LANE_MAP_3012;
		__set_lane_map_v2(display->lane_map.lane_map_v2,
			DSI_PHYSICAL_LANE_1,
			DSI_PHYSICAL_LANE_2,
			DSI_PHYSICAL_LANE_3,
			DSI_PHYSICAL_LANE_0);
	} else if (!strcmp(data, "lane_map_2301")) {
		display->lane_map.lane_map_v1 = DSI_LANE_MAP_2301;
		__set_lane_map_v2(display->lane_map.lane_map_v2,
			DSI_PHYSICAL_LANE_2,
			DSI_PHYSICAL_LANE_3,
			DSI_PHYSICAL_LANE_0,
			DSI_PHYSICAL_LANE_1);
	} else if (!strcmp(data, "lane_map_1230")) {
		display->lane_map.lane_map_v1 = DSI_LANE_MAP_1230;
		__set_lane_map_v2(display->lane_map.lane_map_v2,
			DSI_PHYSICAL_LANE_3,
			DSI_PHYSICAL_LANE_0,
			DSI_PHYSICAL_LANE_1,
			DSI_PHYSICAL_LANE_2);
	} else if (!strcmp(data, "lane_map_0321")) {
		display->lane_map.lane_map_v1 = DSI_LANE_MAP_0321;
		__set_lane_map_v2(display->lane_map.lane_map_v2,
			DSI_PHYSICAL_LANE_0,
			DSI_PHYSICAL_LANE_3,
			DSI_PHYSICAL_LANE_2,
			DSI_PHYSICAL_LANE_1);
	} else if (!strcmp(data, "lane_map_1032")) {
		display->lane_map.lane_map_v1 = DSI_LANE_MAP_1032;
		__set_lane_map_v2(display->lane_map.lane_map_v2,
			DSI_PHYSICAL_LANE_1,
			DSI_PHYSICAL_LANE_0,
			DSI_PHYSICAL_LANE_3,
			DSI_PHYSICAL_LANE_2);
	} else if (!strcmp(data, "lane_map_2103")) {
		display->lane_map.lane_map_v1 = DSI_LANE_MAP_2103;
		__set_lane_map_v2(display->lane_map.lane_map_v2,
			DSI_PHYSICAL_LANE_2,
			DSI_PHYSICAL_LANE_1,
			DSI_PHYSICAL_LANE_0,
			DSI_PHYSICAL_LANE_3);
	} else if (!strcmp(data, "lane_map_3210")) {
		display->lane_map.lane_map_v1 = DSI_LANE_MAP_3210;
		__set_lane_map_v2(display->lane_map.lane_map_v2,
			DSI_PHYSICAL_LANE_3,
			DSI_PHYSICAL_LANE_2,
			DSI_PHYSICAL_LANE_1,
			DSI_PHYSICAL_LANE_0);
	} else {
		DSI_WARN("%s: invalid lane map %s specified. defaulting to lane_map0123\n",
			__func__, data);
		goto set_default;
	}
	return 0;

set_default:
	/* default lane mapping */
	__set_lane_map_v2(display->lane_map.lane_map_v2, DSI_PHYSICAL_LANE_0,
		DSI_PHYSICAL_LANE_1, DSI_PHYSICAL_LANE_2, DSI_PHYSICAL_LANE_3);
	display->lane_map.lane_map_v1 = DSI_LANE_MAP_0123;
	return 0;
}

static int dsi_display_get_phandle_index(
			struct dsi_display *display,
			const char *propname, int count, int index)
{
	struct device_node *disp_node = display->panel_node;
	u32 *val = NULL;
	int rc = 0;

	val = kcalloc(count, sizeof(*val), GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(val)) {
		rc = -ENOMEM;
		goto end;
	}

	if (index >= count)
		goto end;

	if (display->fw)
		rc = dsi_parser_read_u32_array(display->parser_node,
			propname, val, count);
	else
		rc = of_property_read_u32_array(disp_node, propname,
			val, count);
	if (rc)
		goto end;

	rc = val[index];

	DSI_DEBUG("%s index=%d\n", propname, rc);
end:
	kfree(val);
	return rc;
}

static int dsi_display_get_phandle_count(struct dsi_display *display,
			const char *propname)
{
	if (display->fw)
		return dsi_parser_count_u32_elems(display->parser_node,
				propname);
	else
		return of_property_count_u32_elems(display->panel_node,
				propname);
}

static int dsi_display_parse_dt(struct dsi_display *display)
{
	int i, rc = 0;
	u32 phy_count = 0;
	struct device_node *of_node = display->pdev->dev.of_node;
	char *dsi_ctrl_name, *dsi_phy_name;

	if (!strcmp(display->display_type, "primary")) {
		dsi_ctrl_name = "qcom,dsi-ctrl-num";
		dsi_phy_name = "qcom,dsi-phy-num";
	} else {
		dsi_ctrl_name = "qcom,dsi-sec-ctrl-num";
		dsi_phy_name = "qcom,dsi-sec-phy-num";
	}

	display->ctrl_count = dsi_display_get_phandle_count(display,
					dsi_ctrl_name);
	phy_count = dsi_display_get_phandle_count(display, dsi_phy_name);

	DSI_DEBUG("ctrl count=%d, phy count=%d\n",
			display->ctrl_count, phy_count);

	if (!phy_count || !display->ctrl_count) {
		DSI_ERR("no ctrl/phys found\n");
		rc = -ENODEV;
		goto error;
	}

	if (phy_count != display->ctrl_count) {
		DSI_ERR("different ctrl and phy counts\n");
		rc = -ENODEV;
		goto error;
	}

	display_for_each_ctrl(i, display) {
		struct dsi_display_ctrl *ctrl = &display->ctrl[i];
		int index;
		index = dsi_display_get_phandle_index(display, dsi_ctrl_name,
			display->ctrl_count, i);
		ctrl->ctrl_of_node = of_parse_phandle(of_node,
				"qcom,dsi-ctrl", index);
		of_node_put(ctrl->ctrl_of_node);

		index = dsi_display_get_phandle_index(display, dsi_phy_name,
			display->ctrl_count, i);
		ctrl->phy_of_node = of_parse_phandle(of_node,
				"qcom,dsi-phy", index);
		of_node_put(ctrl->phy_of_node);
	}

	/* Parse TE data */
	dsi_display_parse_te_data(display);

	/* Parse all external bridges from port 0 */
	display_for_each_ctrl(i, display) {
		display->ext_bridge[i].node_of =
			of_graph_get_remote_node(of_node, 0, i);
		if (display->ext_bridge[i].node_of)
			display->ext_bridge_cnt++;
		else
			break;
	}

	DSI_DEBUG("success\n");
error:
	return rc;
}

static int dsi_display_res_init(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *ctrl;

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		ctrl->ctrl = dsi_ctrl_get(ctrl->ctrl_of_node);
		if (IS_ERR_OR_NULL(ctrl->ctrl)) {
			rc = PTR_ERR(ctrl->ctrl);
			DSI_ERR("failed to get dsi controller, rc=%d\n", rc);
			ctrl->ctrl = NULL;
			goto error_ctrl_put;
		}

		ctrl->phy = dsi_phy_get(ctrl->phy_of_node);
		if (IS_ERR_OR_NULL(ctrl->phy)) {
			rc = PTR_ERR(ctrl->phy);
			DSI_ERR("failed to get phy controller, rc=%d\n", rc);
			dsi_ctrl_put(ctrl->ctrl);
			ctrl->phy = NULL;
			goto error_ctrl_put;
		}
	}

	display->panel = dsi_panel_get(&display->pdev->dev,
				display->panel_node,
				display->parser_node,
				display->display_type,
				display->cmdline_topology);
	if (IS_ERR_OR_NULL(display->panel)) {
		rc = PTR_ERR(display->panel);
		DSI_ERR("failed to get panel, rc=%d\n", rc);
		display->panel = NULL;
		goto error_ctrl_put;
	}

#if defined(CONFIG_PXLW_IRIS) || defined(CONFIG_PXLW_SOFT_IRIS)
	iris_dsi_display_res_init(display);
#endif

	display_for_each_ctrl(i, display) {
		struct msm_dsi_phy *phy = display->ctrl[i].phy;

		phy->cfg.force_clk_lane_hs =
			display->panel->host_config.force_hs_clk_lane;
		phy->cfg.phy_type =
			display->panel->host_config.phy_type;
	}

	rc = dsi_display_parse_lane_map(display);
	if (rc) {
		DSI_ERR("Lane map not found, rc=%d\n", rc);
		goto error_ctrl_put;
	}

	rc = dsi_display_clocks_init(display);
	if (rc) {
		DSI_ERR("Failed to parse clock data, rc=%d\n", rc);
		goto error_ctrl_put;
	}

	if ((strcmp(display->panel->name, "samsung dsc cmd mode oneplus dsi panel") == 0)
			|| (strcmp(display->panel->name, "samsung ana6706 dsc cmd mode panel") == 0)
				|| (strcmp(display->panel->name, "samsung amb655x fhd cmd mode dsc dsi panel") == 0)) {
		INIT_DELAYED_WORK(&display->panel->gamma_read_work, dsi_display_gamma_read_work);
		DSI_ERR("INIT_DELAYED_WORK: dsi_display_gamma_read_work\n");
	}


	return 0;
error_ctrl_put:
	for (i = i - 1; i >= 0; i--) {
		ctrl = &display->ctrl[i];
		dsi_ctrl_put(ctrl->ctrl);
		dsi_phy_put(ctrl->phy);
	}
	return rc;
}

static int dsi_display_res_deinit(struct dsi_display *display)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *ctrl;

	rc = dsi_display_clocks_deinit(display);
	if (rc)
		DSI_ERR("clocks deinit failed, rc=%d\n", rc);

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		dsi_phy_put(ctrl->phy);
		dsi_ctrl_put(ctrl->ctrl);
	}

	if (display->panel)
		dsi_panel_put(display->panel);

	return rc;
}

static int dsi_display_validate_mode_set(struct dsi_display *display,
					 struct dsi_display_mode *mode,
					 u32 flags)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *ctrl;

	/*
	 * To set a mode:
	 * 1. Controllers should be turned off.
	 * 2. Link clocks should be off.
	 * 3. Phy should be disabled.
	 */

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if ((ctrl->power_state > DSI_CTRL_POWER_VREG_ON) ||
		    (ctrl->phy_enabled)) {
			rc = -EINVAL;
			goto error;
		}
	}

error:
	return rc;
}

static bool dsi_display_is_seamless_dfps_possible(
		const struct dsi_display *display,
		const struct dsi_display_mode *tgt,
		const enum dsi_dfps_type dfps_type)
{
	struct dsi_display_mode *cur;

	if (!display || !tgt || !display->panel) {
		DSI_ERR("Invalid params\n");
		return false;
	}

	cur = display->panel->cur_mode;

	if (cur->timing.h_active != tgt->timing.h_active) {
		DSI_DEBUG("timing.h_active differs %d %d\n",
				cur->timing.h_active, tgt->timing.h_active);
		return false;
	}

	if (cur->timing.h_back_porch != tgt->timing.h_back_porch) {
		DSI_DEBUG("timing.h_back_porch differs %d %d\n",
				cur->timing.h_back_porch,
				tgt->timing.h_back_porch);
		return false;
	}

	if (cur->timing.h_sync_width != tgt->timing.h_sync_width) {
		DSI_DEBUG("timing.h_sync_width differs %d %d\n",
				cur->timing.h_sync_width,
				tgt->timing.h_sync_width);
		return false;
	}

	if (cur->timing.h_front_porch != tgt->timing.h_front_porch) {
		DSI_DEBUG("timing.h_front_porch differs %d %d\n",
				cur->timing.h_front_porch,
				tgt->timing.h_front_porch);
		if (dfps_type != DSI_DFPS_IMMEDIATE_HFP)
			return false;
	}

	if (cur->timing.h_skew != tgt->timing.h_skew) {
		DSI_DEBUG("timing.h_skew differs %d %d\n",
				cur->timing.h_skew,
				tgt->timing.h_skew);
		return false;
	}

	/* skip polarity comparison */

	if (cur->timing.v_active != tgt->timing.v_active) {
		DSI_DEBUG("timing.v_active differs %d %d\n",
				cur->timing.v_active,
				tgt->timing.v_active);
		return false;
	}

	if (cur->timing.v_back_porch != tgt->timing.v_back_porch) {
		DSI_DEBUG("timing.v_back_porch differs %d %d\n",
				cur->timing.v_back_porch,
				tgt->timing.v_back_porch);
		return false;
	}

	if (cur->timing.v_sync_width != tgt->timing.v_sync_width) {
		DSI_DEBUG("timing.v_sync_width differs %d %d\n",
				cur->timing.v_sync_width,
				tgt->timing.v_sync_width);
		return false;
	}

	if (cur->timing.v_front_porch != tgt->timing.v_front_porch) {
		DSI_DEBUG("timing.v_front_porch differs %d %d\n",
				cur->timing.v_front_porch,
				tgt->timing.v_front_porch);
		if (dfps_type != DSI_DFPS_IMMEDIATE_VFP)
			return false;
	}

	/* skip polarity comparison */

	if (cur->timing.refresh_rate == tgt->timing.refresh_rate)
		DSI_DEBUG("timing.refresh_rate identical %d %d\n",
				cur->timing.refresh_rate,
				tgt->timing.refresh_rate);

	if (cur->pixel_clk_khz != tgt->pixel_clk_khz)
		DSI_DEBUG("pixel_clk_khz differs %d %d\n",
				cur->pixel_clk_khz, tgt->pixel_clk_khz);

	if (cur->dsi_mode_flags != tgt->dsi_mode_flags)
		DSI_DEBUG("flags differs %d %d\n",
				cur->dsi_mode_flags, tgt->dsi_mode_flags);

	return true;
}

void dsi_display_update_byte_intf_div(struct dsi_display *display)
{
	struct dsi_host_common_cfg *config;
	struct dsi_display_ctrl *m_ctrl;
	int phy_ver;

	m_ctrl = &display->ctrl[display->cmd_master_idx];
	config = &display->panel->host_config;

	phy_ver = dsi_phy_get_version(m_ctrl->phy);
	if (phy_ver <= DSI_PHY_VERSION_2_0)
		config->byte_intf_clk_div = 1;
	else
		config->byte_intf_clk_div = 2;
}

static int dsi_display_update_dsi_bitrate(struct dsi_display *display,
					  u32 bit_clk_rate)
{
	int rc = 0;
	int i;

	DSI_DEBUG("%s:bit rate:%d\n", __func__, bit_clk_rate);
	if (!display->panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (bit_clk_rate == 0) {
		DSI_ERR("Invalid bit clock rate\n");
		return -EINVAL;
	}

	display->config.bit_clk_rate_hz = bit_clk_rate;

	display_for_each_ctrl(i, display) {
		struct dsi_display_ctrl *dsi_disp_ctrl = &display->ctrl[i];
		struct dsi_ctrl *ctrl = dsi_disp_ctrl->ctrl;
		u32 num_of_lanes = 0, bpp, byte_intf_clk_div;
		u64 bit_rate, pclk_rate, bit_rate_per_lane, byte_clk_rate,
				byte_intf_clk_rate;
		u32 bits_per_symbol = 16, num_of_symbols = 7; /* For Cphy */
		struct dsi_host_common_cfg *host_cfg;

		mutex_lock(&ctrl->ctrl_lock);

		host_cfg = &display->panel->host_config;
		if (host_cfg->data_lanes & DSI_DATA_LANE_0)
			num_of_lanes++;
		if (host_cfg->data_lanes & DSI_DATA_LANE_1)
			num_of_lanes++;
		if (host_cfg->data_lanes & DSI_DATA_LANE_2)
			num_of_lanes++;
		if (host_cfg->data_lanes & DSI_DATA_LANE_3)
			num_of_lanes++;

		if (num_of_lanes == 0) {
			DSI_ERR("Invalid lane count\n");
			rc = -EINVAL;
			goto error;
		}

		bpp = dsi_pixel_format_to_bpp(host_cfg->dst_format);

		bit_rate = display->config.bit_clk_rate_hz * num_of_lanes;
		bit_rate_per_lane = bit_rate;
		do_div(bit_rate_per_lane, num_of_lanes);
		pclk_rate = bit_rate;
		do_div(pclk_rate, bpp);
		if (host_cfg->phy_type == DSI_PHY_TYPE_DPHY) {
			bit_rate_per_lane = bit_rate;
			do_div(bit_rate_per_lane, num_of_lanes);
			byte_clk_rate = bit_rate_per_lane;
			do_div(byte_clk_rate, 8);
			byte_intf_clk_rate = byte_clk_rate;
			byte_intf_clk_div = host_cfg->byte_intf_clk_div;
			do_div(byte_intf_clk_rate, byte_intf_clk_div);
		} else {
			bit_rate_per_lane = bit_clk_rate;
			pclk_rate *= bits_per_symbol;
			do_div(pclk_rate, num_of_symbols);
			byte_clk_rate = bit_clk_rate;
			do_div(byte_clk_rate, num_of_symbols);

			/* For CPHY, byte_intf_clk is same as byte_clk */
			byte_intf_clk_rate = byte_clk_rate;
		}

		DSI_DEBUG("bit_clk_rate = %llu, bit_clk_rate_per_lane = %llu\n",
			 bit_rate, bit_rate_per_lane);
		DSI_DEBUG("byte_clk_rate = %llu, byte_intf_clk_rate = %llu\n",
			  byte_clk_rate, byte_intf_clk_rate);
		DSI_DEBUG("pclk_rate = %llu\n", pclk_rate);

		ctrl->clk_freq.byte_clk_rate = byte_clk_rate;
		ctrl->clk_freq.byte_intf_clk_rate = byte_intf_clk_rate;
		ctrl->clk_freq.pix_clk_rate = pclk_rate;
		rc = dsi_clk_set_link_frequencies(display->dsi_clk_handle,
			ctrl->clk_freq, ctrl->cell_index);
		if (rc) {
			DSI_ERR("Failed to update link frequencies\n");
			goto error;
		}

		ctrl->host_config.bit_clk_rate_hz = bit_clk_rate;
error:
		mutex_unlock(&ctrl->ctrl_lock);

		/* TODO: recover ctrl->clk_freq in case of failure */
		if (rc)
			return rc;
	}

	return 0;
}

static void _dsi_display_calc_pipe_delay(struct dsi_display *display,
				    struct dsi_dyn_clk_delay *delay,
				    struct dsi_display_mode *mode)
{
	u32 esc_clk_rate_hz;
	u32 pclk_to_esc_ratio, byte_to_esc_ratio, hr_bit_to_esc_ratio;
	u32 hsync_period = 0;
	struct dsi_display_ctrl *m_ctrl;
	struct dsi_ctrl *dsi_ctrl;
	struct dsi_phy_cfg *cfg;
	int phy_ver;

	m_ctrl = &display->ctrl[display->clk_master_idx];
	dsi_ctrl = m_ctrl->ctrl;

	cfg = &(m_ctrl->phy->cfg);

	esc_clk_rate_hz = dsi_ctrl->clk_freq.esc_clk_rate;
	pclk_to_esc_ratio = (dsi_ctrl->clk_freq.pix_clk_rate /
			     esc_clk_rate_hz);
	byte_to_esc_ratio = (dsi_ctrl->clk_freq.byte_clk_rate /
			     esc_clk_rate_hz);
	hr_bit_to_esc_ratio = ((dsi_ctrl->clk_freq.byte_clk_rate * 4) /
					esc_clk_rate_hz);

	hsync_period = DSI_H_TOTAL_DSC(&mode->timing);
	delay->pipe_delay = (hsync_period + 1) / pclk_to_esc_ratio;
	if (!display->panel->video_config.eof_bllp_lp11_en)
		delay->pipe_delay += (17 / pclk_to_esc_ratio) +
			((21 + (display->config.common_config.t_clk_pre + 1) +
			  (display->config.common_config.t_clk_post + 1)) /
			 byte_to_esc_ratio) +
			((((cfg->timing.lane_v3[8] >> 1) + 1) +
			((cfg->timing.lane_v3[6] >> 1) + 1) +
			((cfg->timing.lane_v3[3] * 4) +
			 (cfg->timing.lane_v3[5] >> 1) + 1) +
			((cfg->timing.lane_v3[7] >> 1) + 1) +
			((cfg->timing.lane_v3[1] >> 1) + 1) +
			((cfg->timing.lane_v3[4] >> 1) + 1)) /
			 hr_bit_to_esc_ratio);

	delay->pipe_delay2 = 0;
	if (display->panel->host_config.force_hs_clk_lane)
		delay->pipe_delay2 = (6 / byte_to_esc_ratio) +
			((((cfg->timing.lane_v3[1] >> 1) + 1) +
			  ((cfg->timing.lane_v3[4] >> 1) + 1)) /
			 hr_bit_to_esc_ratio);

	/*
	 * 100us pll delay recommended for phy ver 2.0 and 3.0
	 * 25us pll delay recommended for phy ver 4.0
	 */
	phy_ver = dsi_phy_get_version(m_ctrl->phy);
	if (phy_ver <= DSI_PHY_VERSION_3_0)
		delay->pll_delay = 100;
	else
		delay->pll_delay = 25;

	delay->pll_delay = ((delay->pll_delay * esc_clk_rate_hz) / 1000000);
}

/*
 * dsi_display_is_type_cphy - check if panel type is cphy
 * @display: Pointer to private display structure
 * Returns: True if panel type is cphy
 */
static inline bool dsi_display_is_type_cphy(struct dsi_display *display)
{
	return (display->panel->host_config.phy_type ==
		DSI_PHY_TYPE_CPHY) ? true : false;
}

static int _dsi_display_dyn_update_clks(struct dsi_display *display,
					struct link_clk_freq *bkp_freq)
{
	int rc = 0, i;
	struct dsi_display_ctrl *m_ctrl, *ctrl;
	struct dsi_clk_link_set *parent_clk, *enable_clk;

	m_ctrl = &display->ctrl[display->clk_master_idx];

	if (dsi_display_is_type_cphy(display)) {
		enable_clk = &display->clock_info.cphy_clks;
		parent_clk = &display->clock_info.shadow_cphy_clks;
	} else {
		enable_clk = &display->clock_info.src_clks;
		parent_clk = &display->clock_info.shadow_clks;
	}

	dsi_clk_prepare_enable(enable_clk);

	rc = dsi_clk_update_parent(parent_clk,
				&display->clock_info.mux_clks);
	if (rc) {
		DSI_ERR("failed to update mux parent\n");
		goto exit;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl)
			continue;
		rc = dsi_clk_set_byte_clk_rate(display->dsi_clk_handle,
				ctrl->ctrl->clk_freq.byte_clk_rate,
				ctrl->ctrl->clk_freq.byte_intf_clk_rate, i);
		if (rc) {
			DSI_ERR("failed to set byte rate for index:%d\n", i);
			goto recover_byte_clk;
		}
		rc = dsi_clk_set_pixel_clk_rate(display->dsi_clk_handle,
				   ctrl->ctrl->clk_freq.pix_clk_rate, i);
		if (rc) {
			DSI_ERR("failed to set pix rate for index:%d\n", i);
			goto recover_pix_clk;
		}
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (ctrl == m_ctrl)
			continue;
		dsi_phy_dynamic_refresh_trigger(ctrl->phy, false);
	}
	dsi_phy_dynamic_refresh_trigger(m_ctrl->phy, true);

	/* wait for dynamic refresh done */
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		rc = dsi_ctrl_wait4dynamic_refresh_done(ctrl->ctrl);
		if (rc) {
			DSI_ERR("wait4dynamic refresh failed for dsi:%d\n", i);
			goto recover_pix_clk;
		} else {
			DSI_INFO("dynamic refresh done on dsi: %s\n",
				i ? "slave" : "master");
		}
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		dsi_phy_dynamic_refresh_clear(ctrl->phy);
	}

	rc = dsi_clk_update_parent(enable_clk,
				&display->clock_info.mux_clks);

	if (rc)
		DSI_ERR("could not switch back to src clks %d\n", rc);

	dsi_clk_disable_unprepare(enable_clk);

	return rc;

recover_pix_clk:
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl)
			continue;
		dsi_clk_set_pixel_clk_rate(display->dsi_clk_handle,
					   bkp_freq->pix_clk_rate, i);
	}

recover_byte_clk:
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl)
			continue;
		dsi_clk_set_byte_clk_rate(display->dsi_clk_handle,
					bkp_freq->byte_clk_rate,
					bkp_freq->byte_intf_clk_rate, i);
	}

exit:
	dsi_clk_disable_unprepare(&display->clock_info.src_clks);

	return rc;
}

static int dsi_display_dynamic_clk_switch_vid(struct dsi_display *display,
					  struct dsi_display_mode *mode)
{
	int rc = 0, mask, i;
	struct dsi_display_ctrl *m_ctrl, *ctrl;
	struct dsi_dyn_clk_delay delay;
	struct link_clk_freq bkp_freq;

	dsi_panel_acquire_panel_lock(display->panel);

	m_ctrl = &display->ctrl[display->clk_master_idx];

	dsi_display_clk_ctrl(display->dsi_clk_handle, DSI_ALL_CLKS, DSI_CLK_ON);

	/* mask PLL unlock, FIFO overflow and underflow errors */
	mask = BIT(DSI_PLL_UNLOCK_ERR) | BIT(DSI_FIFO_UNDERFLOW) |
		BIT(DSI_FIFO_OVERFLOW);
	dsi_display_mask_ctrl_error_interrupts(display, mask, true);

	/* update the phy timings based on new mode */
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		dsi_phy_update_phy_timings(ctrl->phy, &display->config);
	}

	/* back up existing rates to handle failure case */
	bkp_freq.byte_clk_rate = m_ctrl->ctrl->clk_freq.byte_clk_rate;
	bkp_freq.byte_intf_clk_rate = m_ctrl->ctrl->clk_freq.byte_intf_clk_rate;
	bkp_freq.pix_clk_rate = m_ctrl->ctrl->clk_freq.pix_clk_rate;
	bkp_freq.esc_clk_rate = m_ctrl->ctrl->clk_freq.esc_clk_rate;

	rc = dsi_display_update_dsi_bitrate(display, mode->timing.clk_rate_hz);
	if (rc) {
		DSI_ERR("failed set link frequencies %d\n", rc);
		goto exit;
	}

	/* calculate pipe delays */
	_dsi_display_calc_pipe_delay(display, &delay, mode);

	/* configure dynamic refresh ctrl registers */
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->phy)
			continue;
		if (ctrl == m_ctrl)
			dsi_phy_config_dynamic_refresh(ctrl->phy, &delay, true);
		else
			dsi_phy_config_dynamic_refresh(ctrl->phy, &delay,
						       false);
	}

	rc = _dsi_display_dyn_update_clks(display, &bkp_freq);

exit:
	dsi_display_mask_ctrl_error_interrupts(display, mask, false);

	dsi_display_clk_ctrl(display->dsi_clk_handle, DSI_ALL_CLKS,
			     DSI_CLK_OFF);

	/* store newly calculated phy timings in mode private info */
	dsi_phy_dyn_refresh_cache_phy_timings(m_ctrl->phy,
					      mode->priv_info->phy_timing_val,
					      mode->priv_info->phy_timing_len);

	dsi_panel_release_panel_lock(display->panel);

	return rc;
}

static int dsi_display_dynamic_clk_configure_cmd(struct dsi_display *display,
		int clk_rate)
{
	int rc = 0;

	if (clk_rate <= 0) {
		DSI_ERR("%s: bitrate should be greater than 0\n", __func__);
		return -EINVAL;
	}

	if (clk_rate == display->cached_clk_rate) {
		DSI_INFO("%s: ignore duplicated DSI clk setting\n", __func__);
		return rc;
	}

	display->cached_clk_rate = clk_rate;

	rc = dsi_display_update_dsi_bitrate(display, clk_rate);
	if (!rc) {
		DSI_INFO("%s: bit clk is ready to be configured to '%d'\n",
				__func__, clk_rate);
		atomic_set(&display->clkrate_change_pending, 1);
	} else {
		DSI_ERR("%s: Failed to prepare to configure '%d'. rc = %d\n",
				__func__, clk_rate, rc);
		/* Caching clock failed, so don't go on doing so. */
		atomic_set(&display->clkrate_change_pending, 0);
		display->cached_clk_rate = 0;
	}

	return rc;
}

static int dsi_display_dfps_update(struct dsi_display *display,
				   struct dsi_display_mode *dsi_mode)
{
	struct dsi_mode_info *timing;
	struct dsi_display_ctrl *m_ctrl, *ctrl;
	struct dsi_display_mode *panel_mode;
	struct dsi_dfps_capabilities dfps_caps;
	int rc = 0;
	int i = 0;
	struct dsi_dyn_clk_caps *dyn_clk_caps;

	if (!display || !dsi_mode || !display->panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}
	timing = &dsi_mode->timing;

	dsi_panel_get_dfps_caps(display->panel, &dfps_caps);
	dyn_clk_caps = &(display->panel->dyn_clk_caps);
	if (!dfps_caps.dfps_support && !dyn_clk_caps->maintain_const_fps) {
		DSI_ERR("dfps or constant fps not supported\n");
		return -ENOTSUPP;
	}

	if (dfps_caps.type == DSI_DFPS_IMMEDIATE_CLK) {
		DSI_ERR("dfps clock method not supported\n");
		return -ENOTSUPP;
	}

	/* For split DSI, update the clock master first */

	DSI_DEBUG("configuring seamless dynamic fps\n\n");
	SDE_EVT32(SDE_EVTLOG_FUNC_ENTRY);

	m_ctrl = &display->ctrl[display->clk_master_idx];
	rc = dsi_ctrl_async_timing_update(m_ctrl->ctrl, timing);
	if (rc) {
		DSI_ERR("[%s] failed to dfps update host_%d, rc=%d\n",
				display->name, i, rc);
		goto error;
	}

	/* Update the rest of the controllers */
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl || (ctrl == m_ctrl))
			continue;

		rc = dsi_ctrl_async_timing_update(ctrl->ctrl, timing);
		if (rc) {
			DSI_ERR("[%s] failed to dfps update host_%d, rc=%d\n",
					display->name, i, rc);
			goto error;
		}
	}

	panel_mode = display->panel->cur_mode;
	memcpy(panel_mode, dsi_mode, sizeof(*panel_mode));
	/*
	 * dsi_mode_flags flags are used to communicate with other drm driver
	 * components, and are transient. They aren't inherently part of the
	 * display panel's mode and shouldn't be saved into the cached currently
	 * active mode.
	 */
	panel_mode->dsi_mode_flags = 0;

error:
	SDE_EVT32(SDE_EVTLOG_FUNC_EXIT);
	return rc;
}

static int dsi_display_dfps_calc_front_porch(
		u32 old_fps,
		u32 new_fps,
		u32 a_total,
		u32 b_total,
		u32 b_fp,
		u32 *b_fp_out)
{
	s32 b_fp_new;
	int add_porches, diff;

	if (!b_fp_out) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (!a_total || !new_fps) {
		DSI_ERR("Invalid pixel total or new fps in mode request\n");
		return -EINVAL;
	}

	/*
	 * Keep clock, other porches constant, use new fps, calc front porch
	 * new_vtotal = old_vtotal * (old_fps / new_fps )
	 * new_vfp - old_vfp = new_vtotal - old_vtotal
	 * new_vfp = old_vfp + old_vtotal * ((old_fps - new_fps)/ new_fps)
	 */
	diff = abs(old_fps - new_fps);
	add_porches = mult_frac(b_total, diff, new_fps);

	if (old_fps > new_fps)
		b_fp_new = b_fp + add_porches;
	else
		b_fp_new = b_fp - add_porches;

	DSI_DEBUG("fps %u a %u b %u b_fp %u new_fp %d\n",
			new_fps, a_total, b_total, b_fp, b_fp_new);

	if (b_fp_new < 0) {
		DSI_ERR("Invalid new_hfp calcluated%d\n", b_fp_new);
		return -EINVAL;
	}

	/**
	 * TODO: To differentiate from clock method when communicating to the
	 * other components, perhaps we should set clk here to original value
	 */
	*b_fp_out = b_fp_new;

	return 0;
}

/**
 * dsi_display_get_dfps_timing() - Get the new dfps values.
 * @display:         DSI display handle.
 * @adj_mode:        Mode value structure to be changed.
 *                   It contains old timing values and latest fps value.
 *                   New timing values are updated based on new fps.
 * @curr_refresh_rate:  Current fps rate.
 *                      If zero , current fps rate is taken from
 *                      display->panel->cur_mode.
 * Return: error code.
 */
static int dsi_display_get_dfps_timing(struct dsi_display *display,
			struct dsi_display_mode *adj_mode,
				u32 curr_refresh_rate)
{
	struct dsi_dfps_capabilities dfps_caps;
	struct dsi_display_mode per_ctrl_mode;
	struct dsi_mode_info *timing;
	struct dsi_ctrl *m_ctrl;

	int rc = 0;

	if (!display || !adj_mode) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}
	m_ctrl = display->ctrl[display->clk_master_idx].ctrl;

	dsi_panel_get_dfps_caps(display->panel, &dfps_caps);
	if (!dfps_caps.dfps_support) {
		DSI_ERR("dfps not supported by panel\n");
		return -EINVAL;
	}

	per_ctrl_mode = *adj_mode;
	adjust_timing_by_ctrl_count(display, &per_ctrl_mode);

	if (!curr_refresh_rate) {
		if (!dsi_display_is_seamless_dfps_possible(display,
				&per_ctrl_mode, dfps_caps.type)) {
			DSI_ERR("seamless dynamic fps not supported for mode\n");
			return -EINVAL;
		}
		if (display->panel->cur_mode) {
			curr_refresh_rate =
				display->panel->cur_mode->timing.refresh_rate;
		} else {
			DSI_ERR("cur_mode is not initialized\n");
			return -EINVAL;
		}
	}
	/* TODO: Remove this direct reference to the dsi_ctrl */
	timing = &per_ctrl_mode.timing;

	switch (dfps_caps.type) {
	case DSI_DFPS_IMMEDIATE_VFP:
		rc = dsi_display_dfps_calc_front_porch(
				curr_refresh_rate,
				timing->refresh_rate,
				DSI_H_TOTAL_DSC(timing),
				DSI_V_TOTAL(timing),
				timing->v_front_porch,
				&adj_mode->timing.v_front_porch);
		break;

	case DSI_DFPS_IMMEDIATE_HFP:
		rc = dsi_display_dfps_calc_front_porch(
				curr_refresh_rate,
				timing->refresh_rate,
				DSI_V_TOTAL(timing),
				DSI_H_TOTAL_DSC(timing),
				timing->h_front_porch,
				&adj_mode->timing.h_front_porch);
		if (!rc)
			adj_mode->timing.h_front_porch *= display->ctrl_count;
		break;

	default:
		DSI_ERR("Unsupported DFPS mode %d\n", dfps_caps.type);
		rc = -ENOTSUPP;
	}

	return rc;
}

static bool dsi_display_validate_mode_seamless(struct dsi_display *display,
		struct dsi_display_mode *adj_mode)
{
	int rc = 0;

	if (!display || !adj_mode) {
		DSI_ERR("Invalid params\n");
		return false;
	}

	/* Currently the only seamless transition is dynamic fps */
	rc = dsi_display_get_dfps_timing(display, adj_mode, 0);
	if (rc) {
		DSI_DEBUG("Dynamic FPS not supported for seamless\n");
	} else {
		DSI_DEBUG("Mode switch is seamless Dynamic FPS\n");
		adj_mode->dsi_mode_flags |= DSI_MODE_FLAG_DFPS |
				DSI_MODE_FLAG_VBLANK_PRE_MODESET;
	}

	return rc;
}

static void dsi_display_validate_dms_fps(struct dsi_display_mode *cur_mode,
		struct dsi_display_mode *to_mode)
{
	u32 cur_fps, to_fps;
	u32 cur_h_active, to_h_active;
	u32 cur_v_active, to_v_active;

	cur_fps = cur_mode->timing.refresh_rate;
	to_fps = to_mode->timing.refresh_rate;
	cur_h_active = cur_mode->timing.h_active;
	cur_v_active = cur_mode->timing.v_active;
	to_h_active = to_mode->timing.h_active;
	to_v_active = to_mode->timing.v_active;

	if ((cur_h_active == to_h_active) && (cur_v_active == to_v_active) &&
			(cur_fps != to_fps)) {
		to_mode->dsi_mode_flags |= DSI_MODE_FLAG_DMS_FPS;
		DSI_DEBUG("DMS Modeset with FPS change\n");
	} else {
		to_mode->dsi_mode_flags &= ~DSI_MODE_FLAG_DMS_FPS;
	}
}


static int dsi_display_set_mode_sub(struct dsi_display *display,
				    struct dsi_display_mode *mode,
				    u32 flags)
{
	int rc = 0, clk_rate = 0;
	int i;
	struct dsi_display_ctrl *ctrl;
	struct dsi_display_mode_priv_info *priv_info;
	bool commit_phy_timing = false;

	priv_info = mode->priv_info;
	if (!priv_info) {
		DSI_ERR("[%s] failed to get private info of the display mode\n",
			display->name);
		return -EINVAL;
	}

	SDE_EVT32(mode->dsi_mode_flags);
	if (mode->dsi_mode_flags & DSI_MODE_FLAG_POMS) {
		display->config.panel_mode = mode->panel_mode;
		display->panel->panel_mode = mode->panel_mode;
	}
	rc = dsi_panel_get_host_cfg_for_mode(display->panel,
					     mode,
					     &display->config);
	if (rc) {
		DSI_ERR("[%s] failed to get host config for mode, rc=%d\n",
		       display->name, rc);
		goto error;
	}

	memcpy(&display->config.lane_map, &display->lane_map,
	       sizeof(display->lane_map));

	if (mode->dsi_mode_flags &
			(DSI_MODE_FLAG_DFPS | DSI_MODE_FLAG_VRR)) {
		rc = dsi_display_dfps_update(display, mode);
		if (rc) {
			DSI_ERR("[%s]DSI dfps update failed, rc=%d\n",
					display->name, rc);
			goto error;
		}
		display_for_each_ctrl(i, display) {
			ctrl = &display->ctrl[i];
			rc = dsi_ctrl_update_host_config(ctrl->ctrl,
				&display->config, mode, mode->dsi_mode_flags,
					display->dsi_clk_handle);
			if (rc) {
				DSI_ERR("failed to update ctrl config\n");
				goto error;
			}
		}
		if (priv_info->phy_timing_len) {
			display_for_each_ctrl(i, display) {
				ctrl = &display->ctrl[i];
				rc = dsi_phy_set_timing_params(ctrl->phy,
						priv_info->phy_timing_val,
						priv_info->phy_timing_len,
						commit_phy_timing);
				if (rc)
					DSI_ERR("Fail to add timing params\n");
			}
		}
		if (!(mode->dsi_mode_flags & DSI_MODE_FLAG_DYN_CLK))
			return rc;
	}

	if (mode->dsi_mode_flags & DSI_MODE_FLAG_DYN_CLK) {
		if (display->panel->panel_mode == DSI_OP_VIDEO_MODE) {
			rc = dsi_display_dynamic_clk_switch_vid(display, mode);
			if (rc)
				DSI_ERR("dynamic clk change failed %d\n", rc);
			/*
			 * skip rest of the opearations since
			 * dsi_display_dynamic_clk_switch_vid() already takes
			 * care of them.
			 */
			return rc;
		} else if (display->panel->panel_mode == DSI_OP_CMD_MODE) {
			clk_rate = mode->timing.clk_rate_hz;
			rc = dsi_display_dynamic_clk_configure_cmd(display,
					clk_rate);
			if (rc) {
				DSI_ERR("Failed to configure dynamic clk\n");
				return rc;
			}
		}
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		rc = dsi_ctrl_update_host_config(ctrl->ctrl, &display->config,
				mode, mode->dsi_mode_flags,
				display->dsi_clk_handle);
		if (rc) {
			DSI_ERR("[%s] failed to update ctrl config, rc=%d\n",
			       display->name, rc);
			goto error;
		}
	}

	if ((mode->dsi_mode_flags & DSI_MODE_FLAG_DMS) &&
			(display->panel->panel_mode == DSI_OP_CMD_MODE)) {
		u64 cur_bitclk = display->panel->cur_mode->timing.clk_rate_hz;
		u64 to_bitclk = mode->timing.clk_rate_hz;
		commit_phy_timing = true;

		/* No need to set clkrate pending flag if clocks are same */
		if ((!cur_bitclk && !to_bitclk) || (cur_bitclk != to_bitclk))
			atomic_set(&display->clkrate_change_pending, 1);

		dsi_display_validate_dms_fps(display->panel->cur_mode, mode);
	}

	if (priv_info->phy_timing_len) {
		display_for_each_ctrl(i, display) {
			ctrl = &display->ctrl[i];
			 rc = dsi_phy_set_timing_params(ctrl->phy,
				priv_info->phy_timing_val,
				priv_info->phy_timing_len,
				commit_phy_timing);
			if (rc)
				DSI_ERR("failed to add DSI PHY timing params\n");
		}
	}
error:
	return rc;
}

/**
 * _dsi_display_dev_init - initializes the display device
 * Initialization will acquire references to the resources required for the
 * display hardware to function.
 * @display:         Handle to the display
 * Returns:          Zero on success
 */
static int _dsi_display_dev_init(struct dsi_display *display)
{
	int rc = 0;

	if (!display) {
		DSI_ERR("invalid display\n");
		return -EINVAL;
	}

	if (!display->panel_node)
		return 0;

	mutex_lock(&display->display_lock);

	display->parser = dsi_parser_get(&display->pdev->dev);
	if (display->fw && display->parser)
		display->parser_node = dsi_parser_get_head_node(
				display->parser, display->fw->data,
				display->fw->size);

	rc = dsi_display_parse_dt(display);
	if (rc) {
		DSI_ERR("[%s] failed to parse dt, rc=%d\n", display->name, rc);
		goto error;
	}

	rc = dsi_display_res_init(display);
	if (rc) {
		DSI_ERR("[%s] failed to initialize resources, rc=%d\n",
		       display->name, rc);
		goto error;
	}
error:
	mutex_unlock(&display->display_lock);
	return rc;
}

/**
 * _dsi_display_dev_deinit - deinitializes the display device
 * All the resources acquired during device init will be released.
 * @display:        Handle to the display
 * Returns:         Zero on success
 */
static int _dsi_display_dev_deinit(struct dsi_display *display)
{
	int rc = 0;

	if (!display) {
		DSI_ERR("invalid display\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

	rc = dsi_display_res_deinit(display);
	if (rc)
		DSI_ERR("[%s] failed to deinitialize resource, rc=%d\n",
		       display->name, rc);

	mutex_unlock(&display->display_lock);

	return rc;
}

/**
 * dsi_display_cont_splash_config() - Initialize resources for continuous splash
 * @dsi_display:    Pointer to dsi display
 * Returns:     Zero on success
 */
int dsi_display_cont_splash_config(void *dsi_display)
{
	struct dsi_display *display = dsi_display;
	int rc = 0;

	/* Vote for gdsc required to read register address space */
	if (!display) {
		DSI_ERR("invalid input display param\n");
		return -EINVAL;
	}

	rc = pm_runtime_get_sync(display->drm_dev->dev);
	if (rc < 0) {
		DSI_ERR("failed to vote gdsc for continuous splash, rc=%d\n",
							rc);
		return rc;
	}

	mutex_lock(&display->display_lock);

	display->is_cont_splash_enabled = true;

	/* Update splash status for clock manager */
	dsi_display_clk_mngr_update_splash_status(display->clk_mngr,
				display->is_cont_splash_enabled);

	/* Set up ctrl isr before enabling core clk */
	dsi_display_ctrl_isr_configure(display, true);

	/* Vote for Core clk and link clk. Votes on ctrl and phy
	 * regulator are inplicit from  pre clk on callback
	 */
	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI link clocks, rc=%d\n",
		       display->name, rc);
		goto clk_manager_update;
	}
#if defined(CONFIG_PXLW_IRIS)
	iris_control_pwr_regulator(true);
#endif
	/* Vote on panel regulator will be removed during suspend path */
	rc = dsi_pwr_enable_regulator(&display->panel->power_info, true);
	if (rc) {
		DSI_ERR("[%s] failed to enable vregs, rc=%d\n",
				display->panel->name, rc);
		goto clks_disabled;
	}

	dsi_config_host_engine_state_for_cont_splash(display);
	mutex_unlock(&display->display_lock);

	/* Set the current brightness level */
	dsi_panel_bl_handoff(display->panel);

	return rc;

clks_disabled:
	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_OFF);

clk_manager_update:
	dsi_display_ctrl_isr_configure(display, false);
	/* Update splash status for clock manager */
	dsi_display_clk_mngr_update_splash_status(display->clk_mngr,
				false);
	pm_runtime_put_sync(display->drm_dev->dev);
	display->is_cont_splash_enabled = false;
	mutex_unlock(&display->display_lock);
	return rc;
}

/**
 * dsi_display_splash_res_cleanup() - cleanup for continuous splash
 * @display:    Pointer to dsi display
 * Returns:     Zero on success
 */
int dsi_display_splash_res_cleanup(struct  dsi_display *display)
{
	int rc = 0;

	if (!display->is_cont_splash_enabled)
		return 0;

	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_OFF);
	if (rc)
		DSI_ERR("[%s] failed to disable DSI link clocks, rc=%d\n",
		       display->name, rc);

	pm_runtime_put_sync(display->drm_dev->dev);

	display->is_cont_splash_enabled = false;
	/* Update splash status for clock manager */
	dsi_display_clk_mngr_update_splash_status(display->clk_mngr,
				display->is_cont_splash_enabled);

	return rc;
}

static int dsi_display_force_update_dsi_clk(struct dsi_display *display)
{
	int rc = 0;

	rc = dsi_display_link_clk_force_update_ctrl(display->dsi_clk_handle);

	if (!rc) {
		DSI_INFO("dsi bit clk has been configured to %d\n",
			display->cached_clk_rate);

		atomic_set(&display->clkrate_change_pending, 0);
	} else {
		DSI_ERR("Failed to configure dsi bit clock '%d'. rc = %d\n",
			display->cached_clk_rate, rc);
	}

	return rc;
}

static int dsi_display_validate_split_link(struct dsi_display *display)
{
	int i, rc = 0;
	struct dsi_display_ctrl *ctrl;
	struct dsi_host_common_cfg *host = &display->panel->host_config;

	if (!host->split_link.split_link_enabled)
		return 0;

	if (display->panel->panel_mode == DSI_OP_CMD_MODE) {
		DSI_ERR("[%s] split link is not supported in command mode\n",
			display->name);
		rc = -ENOTSUPP;
		goto error;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl->split_link_supported) {
			DSI_ERR("[%s] split link is not supported by hw\n",
				display->name);
			rc = -ENOTSUPP;
			goto error;
		}

		set_bit(DSI_PHY_SPLIT_LINK, ctrl->phy->hw.feature_map);
	}

	DSI_DEBUG("Split link is enabled\n");
	return 0;

error:
	host->split_link.split_link_enabled = false;
	return rc;
}

/**
 * dsi_display_bind - bind dsi device with controlling device
 * @dev:        Pointer to base of platform device
 * @master:     Pointer to container of drm device
 * @data:       Pointer to private data
 * Returns:     Zero on success
 */
static int dsi_display_bind(struct device *dev,
		struct device *master,
		void *data)
{
	struct dsi_display_ctrl *display_ctrl;
	struct drm_device *drm;
	struct dsi_display *display;
	struct dsi_clk_info info;
	struct clk_ctrl_cb clk_cb;
	void *handle = NULL;
	struct platform_device *pdev = to_platform_device(dev);
	char *client1 = "dsi_clk_client";
	char *client2 = "mdp_event_client";
	int i, rc = 0;

	if (!dev || !pdev || !master) {
		DSI_ERR("invalid param(s), dev %pK, pdev %pK, master %pK\n",
				dev, pdev, master);
		return -EINVAL;
	}

	drm = dev_get_drvdata(master);
	display = platform_get_drvdata(pdev);
	if (!drm || !display) {
		DSI_ERR("invalid param(s), drm %pK, display %pK\n",
				drm, display);
		return -EINVAL;
	}
	if (!display->panel_node)
		return 0;

	if (!display->fw)
		display->name = display->panel_node->name;

	/* defer bind if ext bridge driver is not loaded */
	if (display->panel && display->panel->host_config.ext_bridge_mode) {
		for (i = 0; i < display->ext_bridge_cnt; i++) {
			if (!of_drm_find_bridge(
					display->ext_bridge[i].node_of)) {
				DSI_DEBUG("defer for bridge[%d] %s\n", i,
				  display->ext_bridge[i].node_of->full_name);
				return -EPROBE_DEFER;
			}
		}
	}

	mutex_lock(&display->display_lock);

	rc = dsi_display_validate_split_link(display);
	if (rc) {
		DSI_ERR("[%s] split link validation failed, rc=%d\n",
						 display->name, rc);
		goto error;
	}

	rc = dsi_display_debugfs_init(display);
	if (rc) {
		DSI_ERR("[%s] debugfs init failed, rc=%d\n", display->name, rc);
		goto error;
	}

	atomic_set(&display->clkrate_change_pending, 0);
	display->cached_clk_rate = 0;

	memset(&info, 0x0, sizeof(info));

	display_for_each_ctrl(i, display) {
		display_ctrl = &display->ctrl[i];
		rc = dsi_ctrl_drv_init(display_ctrl->ctrl, display->root);
		if (rc) {
			DSI_ERR("[%s] failed to initialize ctrl[%d], rc=%d\n",
			       display->name, i, rc);
			goto error_ctrl_deinit;
		}
		display_ctrl->ctrl->horiz_index = i;

		rc = dsi_phy_drv_init(display_ctrl->phy);
		if (rc) {
			DSI_ERR("[%s] Failed to initialize phy[%d], rc=%d\n",
				display->name, i, rc);
			(void)dsi_ctrl_drv_deinit(display_ctrl->ctrl);
			goto error_ctrl_deinit;
		}

		display_ctrl->ctrl->dma_cmd_workq = display->dma_cmd_workq;
		memcpy(&info.c_clks[i],
				(&display_ctrl->ctrl->clk_info.core_clks),
				sizeof(struct dsi_core_clk_info));
		memcpy(&info.l_hs_clks[i],
				(&display_ctrl->ctrl->clk_info.hs_link_clks),
				sizeof(struct dsi_link_hs_clk_info));
		memcpy(&info.l_lp_clks[i],
				(&display_ctrl->ctrl->clk_info.lp_link_clks),
				sizeof(struct dsi_link_lp_clk_info));

		info.c_clks[i].drm = drm;
		info.bus_handle[i] =
			display_ctrl->ctrl->axi_bus_info.bus_handle;
		info.ctrl_index[i] = display_ctrl->ctrl->cell_index;
	}

	info.pre_clkoff_cb = dsi_pre_clkoff_cb;
	info.pre_clkon_cb = dsi_pre_clkon_cb;
	info.post_clkoff_cb = dsi_post_clkoff_cb;
	info.post_clkon_cb = dsi_post_clkon_cb;
	info.priv_data = display;
	info.master_ndx = display->clk_master_idx;
	info.dsi_ctrl_count = display->ctrl_count;
	snprintf(info.name, MAX_STRING_LEN,
			"DSI_MNGR-%s", display->name);

	display->clk_mngr = dsi_display_clk_mngr_register(&info);
	if (IS_ERR_OR_NULL(display->clk_mngr)) {
		rc = PTR_ERR(display->clk_mngr);
		display->clk_mngr = NULL;
		DSI_ERR("dsi clock registration failed, rc = %d\n", rc);
		goto error_ctrl_deinit;
	}

	handle = dsi_register_clk_handle(display->clk_mngr, client1);
	if (IS_ERR_OR_NULL(handle)) {
		rc = PTR_ERR(handle);
		DSI_ERR("failed to register %s client, rc = %d\n",
		       client1, rc);
		goto error_clk_deinit;
	} else {
		display->dsi_clk_handle = handle;
	}

	handle = dsi_register_clk_handle(display->clk_mngr, client2);
	if (IS_ERR_OR_NULL(handle)) {
		rc = PTR_ERR(handle);
		DSI_ERR("failed to register %s client, rc = %d\n",
		       client2, rc);
		goto error_clk_client_deinit;
	} else {
		display->mdp_clk_handle = handle;
	}

	clk_cb.priv = display;
	clk_cb.dsi_clk_cb = dsi_display_clk_ctrl_cb;

	display_for_each_ctrl(i, display) {
		display_ctrl = &display->ctrl[i];

		rc = dsi_ctrl_clk_cb_register(display_ctrl->ctrl, &clk_cb);
		if (rc) {
			DSI_ERR("[%s] failed to register ctrl clk_cb[%d], rc=%d\n",
			       display->name, i, rc);
			goto error_ctrl_deinit;
		}

		rc = dsi_phy_clk_cb_register(display_ctrl->phy, &clk_cb);
		if (rc) {
			DSI_ERR("[%s] failed to register phy clk_cb[%d], rc=%d\n",
			       display->name, i, rc);
			goto error_ctrl_deinit;
		}
	}

	dsi_display_update_byte_intf_div(display);
	rc = dsi_display_mipi_host_init(display);
	if (rc) {
		DSI_ERR("[%s] failed to initialize mipi host, rc=%d\n",
		       display->name, rc);
		goto error_ctrl_deinit;
	}

	rc = dsi_panel_drv_init(display->panel, &display->host);
	if (rc) {
		if (rc != -EPROBE_DEFER)
			DSI_ERR("[%s] failed to initialize panel driver, rc=%d\n",
			       display->name, rc);
		goto error_host_deinit;
	}

	DSI_INFO("Successfully bind display panel '%s'\n", display->name);
	display->drm_dev = drm;

	display_for_each_ctrl(i, display) {
		display_ctrl = &display->ctrl[i];

		if (!display_ctrl->phy || !display_ctrl->ctrl)
			continue;

		display_ctrl->ctrl->drm_dev = drm;

		rc = dsi_phy_set_clk_freq(display_ctrl->phy,
				&display_ctrl->ctrl->clk_freq);
		if (rc) {
			DSI_ERR("[%s] failed to set phy clk freq, rc=%d\n",
					display->name, rc);
			goto error;
		}
	}

	/* register te irq handler */
	dsi_display_register_te_irq(display);
	dsi_display_register_err_flag_irq(display);
#if defined(CONFIG_PXLW_IRIS)
	/* register osd irq handler */
	iris_register_osd_irq(display);
#endif

	goto error;

error_host_deinit:
	(void)dsi_display_mipi_host_deinit(display);
error_clk_client_deinit:
	(void)dsi_deregister_clk_handle(display->dsi_clk_handle);
error_clk_deinit:
	(void)dsi_display_clk_mngr_deregister(display->clk_mngr);
error_ctrl_deinit:
	for (i = i - 1; i >= 0; i--) {
		display_ctrl = &display->ctrl[i];
		(void)dsi_phy_drv_deinit(display_ctrl->phy);
		(void)dsi_ctrl_drv_deinit(display_ctrl->ctrl);
	}
	(void)dsi_display_debugfs_deinit(display);
error:
	mutex_unlock(&display->display_lock);
	return rc;
}

/**
 * dsi_display_unbind - unbind dsi from controlling device
 * @dev:        Pointer to base of platform device
 * @master:     Pointer to container of drm device
 * @data:       Pointer to private data
 */
static void dsi_display_unbind(struct device *dev,
		struct device *master, void *data)
{
	struct dsi_display_ctrl *display_ctrl;
	struct dsi_display *display;
	struct platform_device *pdev = to_platform_device(dev);
	int i, rc = 0;

	if (!dev || !pdev) {
		DSI_ERR("invalid param(s)\n");
		return;
	}

	display = platform_get_drvdata(pdev);
	if (!display) {
		DSI_ERR("invalid display\n");
		return;
	}

	mutex_lock(&display->display_lock);

	rc = dsi_panel_drv_deinit(display->panel);
	if (rc)
		DSI_ERR("[%s] failed to deinit panel driver, rc=%d\n",
		       display->name, rc);

	rc = dsi_display_mipi_host_deinit(display);
	if (rc)
		DSI_ERR("[%s] failed to deinit mipi hosts, rc=%d\n",
		       display->name,
		       rc);

	display_for_each_ctrl(i, display) {
		display_ctrl = &display->ctrl[i];

		rc = dsi_phy_drv_deinit(display_ctrl->phy);
		if (rc)
			DSI_ERR("[%s] failed to deinit phy%d driver, rc=%d\n",
			       display->name, i, rc);

		display->ctrl->ctrl->dma_cmd_workq = NULL;
		rc = dsi_ctrl_drv_deinit(display_ctrl->ctrl);
		if (rc)
			DSI_ERR("[%s] failed to deinit ctrl%d driver, rc=%d\n",
			       display->name, i, rc);
	}

	atomic_set(&display->clkrate_change_pending, 0);
	(void)dsi_display_debugfs_deinit(display);

	mutex_unlock(&display->display_lock);
}

static const struct component_ops dsi_display_comp_ops = {
	.bind = dsi_display_bind,
	.unbind = dsi_display_unbind,
};

static struct platform_driver dsi_display_driver = {
	.probe = dsi_display_dev_probe,
	.remove = dsi_display_dev_remove,
	.driver = {
		.name = "msm-dsi-display",
		.of_match_table = dsi_display_dt_match,
		.suppress_bind_attrs = true,
	},
};

static int dsi_display_init(struct dsi_display *display)
{
	int rc = 0;
	struct platform_device *pdev = display->pdev;

	mutex_init(&display->display_lock);

	rc = _dsi_display_dev_init(display);
	if (rc) {
		DSI_ERR("device init failed, rc=%d\n", rc);
		goto end;
	}

	rc = component_add(&pdev->dev, &dsi_display_comp_ops);
	if (rc)
		DSI_ERR("component add failed, rc=%d\n", rc);

	DSI_DEBUG("component add success: %s\n", display->name);
end:
	return rc;
}

static void dsi_display_firmware_display(const struct firmware *fw,
				void *context)
{
	struct dsi_display *display = context;

	if (fw) {
		DSI_INFO("reading data from firmware, size=%zd\n",
			fw->size);

		display->fw = fw;
		display->name = "dsi_firmware_display";
	} else {
		DSI_INFO("no firmware available, fallback to device node\n");
	}

	if (dsi_display_init(display))
		return;

	DSI_DEBUG("success\n");
}

#if defined(CONFIG_PXLW_IRIS)
static int dsi_display_parse_boot_display_selection_iris(struct platform_device *pdev)
{
	// Add secondary display.
	int i;
	struct device_node *node = NULL, *mdp_node = NULL;
	const char *disp_name = NULL;
	static const char * const disp_name_type[] = {
		"pxlw,dsi-display-primary-active",
		"pxlw,dsi-display-secondary-active"};

	node = pdev->dev.of_node;
	mdp_node = of_parse_phandle(node, "qcom,mdp", 0);
	if (!mdp_node) {
		DSI_ERR("mdp_node not found\n");
		return -ENODEV;
	}

	for (i = 0; i < MAX_DSI_ACTIVE_DISPLAY; i++) {
		DSI_INFO("IRIS_LOG I UEFI display[%d] name: %s\n", i, boot_displays[i].name);
		of_property_read_string(mdp_node, disp_name_type[i], &disp_name);
		if (disp_name) {
			if (i == 0) {
				if (strstr(boot_displays[i].name, disp_name) == NULL)
					break;
				disp_name = NULL;
			} else {
				DSI_INFO("IRIS_LOG I actual display[%d] name: %s\n", i, disp_name);
				strlcpy(boot_displays[i].name, disp_name,
						MAX_CMDLINE_PARAM_LEN);
				boot_displays[i].boot_disp_en = true;
				disp_name = NULL;
			}
		} else {
			break;
		}
	}
	return 0;
}
#endif

int dsi_display_dev_probe(struct platform_device *pdev)
{
	struct dsi_display *display = NULL;
	struct device_node *node = NULL, *panel_node = NULL, *mdp_node = NULL;
	int rc = 0, index = DSI_PRIMARY;
	bool firm_req = false;
	struct dsi_display_boot_param *boot_disp;

	if (!pdev || !pdev->dev.of_node) {
		DSI_ERR("pdev not found\n");
		rc = -ENODEV;
		goto end;
	}

	display = devm_kzalloc(&pdev->dev, sizeof(*display), GFP_KERNEL);
	if (!display) {
		rc = -ENOMEM;
		goto end;
	}

	display->dma_cmd_workq = create_singlethread_workqueue(
			"dsi_dma_cmd_workq");
	if (!display->dma_cmd_workq)  {
		DSI_ERR("failed to create work queue\n");
		rc =  -EINVAL;
		goto end;
	}

	display->display_type = of_get_property(pdev->dev.of_node,
				"label", NULL);
	if (!display->display_type)
		display->display_type = "primary";

	if (!strcmp(display->display_type, "secondary"))
		index = DSI_SECONDARY;

#if defined(CONFIG_PXLW_IRIS)
	if (index == DSI_PRIMARY)
		dsi_display_parse_boot_display_selection_iris(pdev);
#endif

	boot_disp = &boot_displays[index];
	node = pdev->dev.of_node;
	if (boot_disp->boot_disp_en) {
		mdp_node = of_parse_phandle(node, "qcom,mdp", 0);
		if (!mdp_node) {
			DSI_ERR("mdp_node not found\n");
			rc = -ENODEV;
			goto end;
		}

		/* The panel name should be same as UEFI name index */
		panel_node = of_find_node_by_name(mdp_node, boot_disp->name);
		if (!panel_node)
			DSI_WARN("panel_node %s not found\n", boot_disp->name);
	} else {
		panel_node = of_parse_phandle(node,
				"qcom,dsi-default-panel", 0);
		if (!panel_node)
			DSI_WARN("default panel not found\n");
	}

	boot_disp->node = pdev->dev.of_node;
	boot_disp->disp = display;

	display->panel_node = panel_node;
	display->pdev = pdev;
	display->boot_disp = boot_disp;

	dsi_display_parse_cmdline_topology(display, index);

	platform_set_drvdata(pdev, display);

	/* initialize display in firmware callback */
	if (!boot_disp->boot_disp_en && IS_ENABLED(CONFIG_DSI_PARSER)) {
		firm_req = !request_firmware_nowait(
			THIS_MODULE, 1, "dsi_prop",
			&pdev->dev, GFP_KERNEL, display,
			dsi_display_firmware_display);
	}

	if (!firm_req) {
		rc = dsi_display_init(display);
		if (rc)
			goto end;
	}

	return 0;
end:
	if (display)
		devm_kfree(&pdev->dev, display);

	return rc;
}

int dsi_display_dev_remove(struct platform_device *pdev)
{
	int rc = 0i, i = 0;
	struct dsi_display *display;
	struct dsi_display_ctrl *ctrl;

	if (!pdev) {
		DSI_ERR("Invalid device\n");
		return -EINVAL;
	}

	display = platform_get_drvdata(pdev);

#if defined(CONFIG_PXLW_IRIS)
	iris_deinit(display);
#endif
	/* decrement ref count */
	of_node_put(display->panel_node);

	if (display->dma_cmd_workq) {
		flush_workqueue(display->dma_cmd_workq);
		destroy_workqueue(display->dma_cmd_workq);
		display->dma_cmd_workq = NULL;
		display_for_each_ctrl(i, display) {
			ctrl = &display->ctrl[i];
			if (!ctrl->ctrl)
				continue;
			ctrl->ctrl->dma_cmd_workq = NULL;
		}
	}

	(void)_dsi_display_dev_deinit(display);

	platform_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, display);
	return rc;
}

int dsi_display_get_num_of_displays(void)
{
	int i, count = 0;

	for (i = 0; i < MAX_DSI_ACTIVE_DISPLAY; i++) {
		struct dsi_display *display = boot_displays[i].disp;

		if (display && display->panel_node)
			count++;
	}

	return count;
}

int dsi_display_get_active_displays(void **display_array, u32 max_display_count)
{
	int index = 0, count = 0;

	if (!display_array || !max_display_count) {
		DSI_ERR("invalid params\n");
		return 0;
	}

	for (index = 0; index < MAX_DSI_ACTIVE_DISPLAY; index++) {
		struct dsi_display *display = boot_displays[index].disp;

		if (display && display->panel_node)
			display_array[count++] = display;
	}

	return count;
}

int dsi_display_drm_bridge_init(struct dsi_display *display,
		struct drm_encoder *enc)
{
	int rc = 0;
	struct dsi_bridge *bridge;
	struct msm_drm_private *priv = NULL;

	if (!display || !display->drm_dev || !enc) {
		DSI_ERR("invalid param(s)\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);
	priv = display->drm_dev->dev_private;

	if (!priv) {
		DSI_ERR("Private data is not present\n");
		rc = -EINVAL;
		goto error;
	}

	if (display->bridge) {
		DSI_ERR("display is already initialize\n");
		goto error;
	}

	bridge = dsi_drm_bridge_init(display, display->drm_dev, enc);
	if (IS_ERR_OR_NULL(bridge)) {
		rc = PTR_ERR(bridge);
		DSI_ERR("[%s] brige init failed, %d\n", display->name, rc);
		goto error;
	}

	display->bridge = bridge;
	priv->bridges[priv->num_bridges++] = &bridge->base;

error:
	mutex_unlock(&display->display_lock);
	return rc;
}

int dsi_display_drm_bridge_deinit(struct dsi_display *display)
{
	int rc = 0;

	if (!display) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

	dsi_drm_bridge_cleanup(display->bridge);
	display->bridge = NULL;

	mutex_unlock(&display->display_lock);
	return rc;
}

/* Hook functions to call external connector, pointer validation is
 * done in dsi_display_drm_ext_bridge_init.
 */
static enum drm_connector_status dsi_display_drm_ext_detect(
		struct drm_connector *connector,
		bool force,
		void *disp)
{
	struct dsi_display *display = disp;

	return display->ext_conn->funcs->detect(display->ext_conn, force);
}

static int dsi_display_drm_ext_get_modes(
		struct drm_connector *connector, void *disp,
		const struct msm_resource_caps_info *avail_res)
{
	struct dsi_display *display = disp;
	struct drm_display_mode *pmode, *pt;
	int count;

	/* if there are modes defined in panel, ignore external modes */
	if (display->panel->num_timing_nodes)
		return dsi_connector_get_modes(connector, disp, avail_res);

	count = display->ext_conn->helper_private->get_modes(
			display->ext_conn);

	list_for_each_entry_safe(pmode, pt,
			&display->ext_conn->probed_modes, head) {
		list_move_tail(&pmode->head, &connector->probed_modes);
	}

	connector->display_info = display->ext_conn->display_info;

	return count;
}

static enum drm_mode_status dsi_display_drm_ext_mode_valid(
		struct drm_connector *connector,
		struct drm_display_mode *mode,
		void *disp, const struct msm_resource_caps_info *avail_res)
{
	struct dsi_display *display = disp;
	enum drm_mode_status status;

	/* always do internal mode_valid check */
	status = dsi_conn_mode_valid(connector, mode, disp, avail_res);
	if (status != MODE_OK)
		return status;

	return display->ext_conn->helper_private->mode_valid(
			display->ext_conn, mode);
}

static int dsi_display_drm_ext_atomic_check(struct drm_connector *connector,
		void *disp,
		struct drm_connector_state *c_state)
{
	struct dsi_display *display = disp;

	return display->ext_conn->helper_private->atomic_check(
			display->ext_conn, c_state);
}

static int dsi_display_ext_get_info(struct drm_connector *connector,
	struct msm_display_info *info, void *disp)
{
	struct dsi_display *display;
	int i;

	if (!info || !disp) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	display = disp;
	if (!display->panel) {
		DSI_ERR("invalid display panel\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

	memset(info, 0, sizeof(struct msm_display_info));

	info->intf_type = DRM_MODE_CONNECTOR_DSI;
	info->num_of_h_tiles = display->ctrl_count;
	for (i = 0; i < info->num_of_h_tiles; i++)
		info->h_tile_instance[i] = display->ctrl[i].ctrl->cell_index;

	info->is_connected = connector->status != connector_status_disconnected;

	if (!strcmp(display->display_type, "primary"))
		info->display_type = SDE_CONNECTOR_PRIMARY;
	else if (!strcmp(display->display_type, "secondary"))
		info->display_type = SDE_CONNECTOR_SECONDARY;

	info->capabilities |= (MSM_DISPLAY_CAP_VID_MODE |
			MSM_DISPLAY_CAP_EDID | MSM_DISPLAY_CAP_HOT_PLUG);
	info->curr_panel_mode = MSM_DISPLAY_VIDEO_MODE;

	mutex_unlock(&display->display_lock);
	return 0;
}

static int dsi_display_ext_get_mode_info(struct drm_connector *connector,
	const struct drm_display_mode *drm_mode,
	struct msm_mode_info *mode_info,
	void *display, const struct msm_resource_caps_info *avail_res)
{
	struct msm_display_topology *topology;

	if (!drm_mode || !mode_info ||
			!avail_res || !avail_res->max_mixer_width)
		return -EINVAL;

	memset(mode_info, 0, sizeof(*mode_info));
	mode_info->frame_rate = drm_mode->vrefresh;
	mode_info->vtotal = drm_mode->vtotal;

	topology = &mode_info->topology;
	topology->num_lm = (avail_res->max_mixer_width
			<= drm_mode->hdisplay) ? 2 : 1;
	topology->num_enc = 0;
	topology->num_intf = topology->num_lm;

	mode_info->comp_info.comp_type = MSM_DISPLAY_COMPRESSION_NONE;

	return 0;
}

static struct dsi_display_ext_bridge *dsi_display_ext_get_bridge(
		struct drm_bridge *bridge)
{
	struct msm_drm_private *priv;
	struct sde_kms *sde_kms;
	struct list_head *connector_list;
	struct drm_connector *conn_iter;
	struct sde_connector *sde_conn;
	struct dsi_display *display;
	int i;

	if (!bridge || !bridge->encoder) {
		SDE_ERROR("invalid argument\n");
		return NULL;
	}

	priv = bridge->dev->dev_private;
	sde_kms = to_sde_kms(priv->kms);
	connector_list = &sde_kms->dev->mode_config.connector_list;

	list_for_each_entry(conn_iter, connector_list, head) {
		sde_conn = to_sde_connector(conn_iter);
		if (sde_conn->encoder == bridge->encoder) {
			display = sde_conn->display;
			display_for_each_ctrl(i, display) {
				if (display->ext_bridge[i].bridge == bridge)
					return &display->ext_bridge[i];
			}
		}
	}

	return NULL;
}

static void dsi_display_drm_ext_adjust_timing(
		const struct dsi_display *display,
		struct drm_display_mode *mode)
{
	mode->hdisplay /= display->ctrl_count;
	mode->hsync_start /= display->ctrl_count;
	mode->hsync_end /= display->ctrl_count;
	mode->htotal /= display->ctrl_count;
	mode->hskew /= display->ctrl_count;
	mode->clock /= display->ctrl_count;
}

static enum drm_mode_status dsi_display_drm_ext_bridge_mode_valid(
		struct drm_bridge *bridge,
		const struct drm_display_mode *mode)
{
	struct dsi_display_ext_bridge *ext_bridge;
	struct drm_display_mode tmp;

	ext_bridge = dsi_display_ext_get_bridge(bridge);
	if (!ext_bridge)
		return MODE_ERROR;

	tmp = *mode;
	dsi_display_drm_ext_adjust_timing(ext_bridge->display, &tmp);
	return ext_bridge->orig_funcs->mode_valid(bridge, &tmp);
}

static bool dsi_display_drm_ext_bridge_mode_fixup(
		struct drm_bridge *bridge,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct dsi_display_ext_bridge *ext_bridge;
	struct drm_display_mode tmp;

	ext_bridge = dsi_display_ext_get_bridge(bridge);
	if (!ext_bridge)
		return false;

	tmp = *mode;
	dsi_display_drm_ext_adjust_timing(ext_bridge->display, &tmp);
	return ext_bridge->orig_funcs->mode_fixup(bridge, &tmp, &tmp);
}

static void dsi_display_drm_ext_bridge_mode_set(
		struct drm_bridge *bridge,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct dsi_display_ext_bridge *ext_bridge;
	struct drm_display_mode tmp;

	ext_bridge = dsi_display_ext_get_bridge(bridge);
	if (!ext_bridge)
		return;

	tmp = *mode;
	dsi_display_drm_ext_adjust_timing(ext_bridge->display, &tmp);
	ext_bridge->orig_funcs->mode_set(bridge, &tmp, &tmp);
}

static int dsi_host_ext_attach(struct mipi_dsi_host *host,
			   struct mipi_dsi_device *dsi)
{
	struct dsi_display *display = to_dsi_display(host);
	struct dsi_panel *panel;

	if (!host || !dsi || !display->panel) {
		DSI_ERR("Invalid param\n");
		return -EINVAL;
	}

	DSI_DEBUG("DSI[%s]: channel=%d, lanes=%d, format=%d, mode_flags=%lx\n",
		dsi->name, dsi->channel, dsi->lanes,
		dsi->format, dsi->mode_flags);

	panel = display->panel;
	panel->host_config.data_lanes = 0;
	if (dsi->lanes > 0)
		panel->host_config.data_lanes |= DSI_DATA_LANE_0;
	if (dsi->lanes > 1)
		panel->host_config.data_lanes |= DSI_DATA_LANE_1;
	if (dsi->lanes > 2)
		panel->host_config.data_lanes |= DSI_DATA_LANE_2;
	if (dsi->lanes > 3)
		panel->host_config.data_lanes |= DSI_DATA_LANE_3;

	switch (dsi->format) {
	case MIPI_DSI_FMT_RGB888:
		panel->host_config.dst_format = DSI_PIXEL_FORMAT_RGB888;
		break;
	case MIPI_DSI_FMT_RGB666:
		panel->host_config.dst_format = DSI_PIXEL_FORMAT_RGB666_LOOSE;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		panel->host_config.dst_format = DSI_PIXEL_FORMAT_RGB666;
		break;
	case MIPI_DSI_FMT_RGB565:
	default:
		panel->host_config.dst_format = DSI_PIXEL_FORMAT_RGB565;
		break;
	}

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO) {
		panel->panel_mode = DSI_OP_VIDEO_MODE;

		if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
			panel->video_config.traffic_mode =
					DSI_VIDEO_TRAFFIC_BURST_MODE;
		else if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
			panel->video_config.traffic_mode =
					DSI_VIDEO_TRAFFIC_SYNC_PULSES;
		else
			panel->video_config.traffic_mode =
					DSI_VIDEO_TRAFFIC_SYNC_START_EVENTS;

		panel->video_config.hsa_lp11_en =
			dsi->mode_flags & MIPI_DSI_MODE_VIDEO_HSA;
		panel->video_config.hbp_lp11_en =
			dsi->mode_flags & MIPI_DSI_MODE_VIDEO_HBP;
		panel->video_config.hfp_lp11_en =
			dsi->mode_flags & MIPI_DSI_MODE_VIDEO_HFP;
		panel->video_config.pulse_mode_hsa_he =
			dsi->mode_flags & MIPI_DSI_MODE_VIDEO_HSE;
		panel->video_config.bllp_lp11_en =
			dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BLLP;
		panel->video_config.eof_bllp_lp11_en =
			dsi->mode_flags & MIPI_DSI_MODE_VIDEO_EOF_BLLP;
	} else {
		panel->panel_mode = DSI_OP_CMD_MODE;
		DSI_ERR("command mode not supported by ext bridge\n");
		return -ENOTSUPP;
	}

	panel->bl_config.type = DSI_BACKLIGHT_UNKNOWN;

	return 0;
}

static struct mipi_dsi_host_ops dsi_host_ext_ops = {
	.attach = dsi_host_ext_attach,
	.detach = dsi_host_detach,
	.transfer = dsi_host_transfer,
};

struct drm_panel *dsi_display_get_drm_panel(struct dsi_display * display)
{
	if (!display || !display->panel) {
		pr_err("invalid param(s)\n");
		return NULL;
	}

	return &display->panel->drm_panel;
}

int dsi_display_drm_ext_bridge_init(struct dsi_display *display,
		struct drm_encoder *encoder, struct drm_connector *connector)
{
	struct drm_device *drm;
	struct drm_bridge *bridge;
	struct drm_bridge *ext_bridge;
	struct drm_connector *ext_conn;
	struct sde_connector *sde_conn;
	struct drm_bridge *prev_bridge;
	int rc = 0, i;

	if (!display || !encoder || !connector)
		return -EINVAL;

	drm = encoder->dev;
	bridge = encoder->bridge;
	sde_conn = to_sde_connector(connector);
	prev_bridge = bridge;

	if (display->panel && !display->panel->host_config.ext_bridge_mode)
		return 0;

	for (i = 0; i < display->ext_bridge_cnt; i++) {
		struct dsi_display_ext_bridge *ext_bridge_info =
				&display->ext_bridge[i];

		/* return if ext bridge is already initialized */
		if (ext_bridge_info->bridge)
			return 0;

		ext_bridge = of_drm_find_bridge(ext_bridge_info->node_of);
		if (IS_ERR_OR_NULL(ext_bridge)) {
			rc = PTR_ERR(ext_bridge);
			DSI_ERR("failed to find ext bridge\n");
			goto error;
		}

		/* override functions for mode adjustment */
		if (display->ext_bridge_cnt > 1) {
			ext_bridge_info->bridge_funcs = *ext_bridge->funcs;
			if (ext_bridge->funcs->mode_fixup)
				ext_bridge_info->bridge_funcs.mode_fixup =
					dsi_display_drm_ext_bridge_mode_fixup;
			if (ext_bridge->funcs->mode_valid)
				ext_bridge_info->bridge_funcs.mode_valid =
					dsi_display_drm_ext_bridge_mode_valid;
			if (ext_bridge->funcs->mode_set)
				ext_bridge_info->bridge_funcs.mode_set =
					dsi_display_drm_ext_bridge_mode_set;
			ext_bridge_info->orig_funcs = ext_bridge->funcs;
			ext_bridge->funcs = &ext_bridge_info->bridge_funcs;
		}

		rc = drm_bridge_attach(encoder, ext_bridge, prev_bridge);
		if (rc) {
			DSI_ERR("[%s] ext brige attach failed, %d\n",
				display->name, rc);
			goto error;
		}

		ext_bridge_info->display = display;
		ext_bridge_info->bridge = ext_bridge;
		prev_bridge = ext_bridge;

		/* ext bridge will init its own connector during attach,
		 * we need to extract it out of the connector list
		 */
		spin_lock_irq(&drm->mode_config.connector_list_lock);
		ext_conn = list_last_entry(&drm->mode_config.connector_list,
			struct drm_connector, head);
		if (ext_conn && ext_conn != connector &&
			ext_conn->encoder_ids[0] == bridge->encoder->base.id) {
			list_del_init(&ext_conn->head);
			display->ext_conn = ext_conn;
		}
		spin_unlock_irq(&drm->mode_config.connector_list_lock);

		/* if there is no valid external connector created, or in split
		 * mode, default setting is used from panel defined in DT file.
		 */
		if (!display->ext_conn ||
		    !display->ext_conn->funcs ||
		    !display->ext_conn->helper_private ||
		    display->ext_bridge_cnt > 1) {
			display->ext_conn = NULL;
			continue;
		}

		/* otherwise, hook up the functions to use external connector */
		if (display->ext_conn->funcs->detect)
			sde_conn->ops.detect = dsi_display_drm_ext_detect;

		if (display->ext_conn->helper_private->get_modes)
			sde_conn->ops.get_modes =
				dsi_display_drm_ext_get_modes;

		if (display->ext_conn->helper_private->mode_valid)
			sde_conn->ops.mode_valid =
				dsi_display_drm_ext_mode_valid;

		if (display->ext_conn->helper_private->atomic_check)
			sde_conn->ops.atomic_check =
				dsi_display_drm_ext_atomic_check;

		sde_conn->ops.get_info =
				dsi_display_ext_get_info;
		sde_conn->ops.get_mode_info =
				dsi_display_ext_get_mode_info;

		/* add support to attach/detach */
		display->host.ops = &dsi_host_ext_ops;
	}

	return 0;
error:
	return rc;
}

int dsi_display_get_info(struct drm_connector *connector,
		struct msm_display_info *info, void *disp)
{
	struct dsi_display *display;
	struct dsi_panel_phy_props phy_props;
	struct dsi_host_common_cfg *host;
	int i, rc;

	if (!info || !disp) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	display = disp;
	if (!display->panel) {
		DSI_ERR("invalid display panel\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);
	rc = dsi_panel_get_phy_props(display->panel, &phy_props);
	if (rc) {
		DSI_ERR("[%s] failed to get panel phy props, rc=%d\n",
		       display->name, rc);
		goto error;
	}

	memset(info, 0, sizeof(struct msm_display_info));
	info->intf_type = DRM_MODE_CONNECTOR_DSI;
	info->num_of_h_tiles = display->ctrl_count;
	for (i = 0; i < info->num_of_h_tiles; i++)
		info->h_tile_instance[i] = display->ctrl[i].ctrl->cell_index;

	info->is_connected = true;

	if (!strcmp(display->display_type, "primary"))
		info->display_type = SDE_CONNECTOR_PRIMARY;
	else if (!strcmp(display->display_type, "secondary"))
		info->display_type = SDE_CONNECTOR_SECONDARY;

	info->width_mm = phy_props.panel_width_mm;
	info->height_mm = phy_props.panel_height_mm;
	info->max_width = 1920;
	info->max_height = 1080;
	info->qsync_min_fps =
		display->panel->qsync_min_fps;

	switch (display->panel->panel_mode) {
	case DSI_OP_VIDEO_MODE:
		info->curr_panel_mode = MSM_DISPLAY_VIDEO_MODE;
		info->capabilities |= MSM_DISPLAY_CAP_VID_MODE;
		if (display->panel->panel_mode_switch_enabled)
			info->capabilities |= MSM_DISPLAY_CAP_CMD_MODE;
		break;
	case DSI_OP_CMD_MODE:
		info->curr_panel_mode = MSM_DISPLAY_CMD_MODE;
		info->capabilities |= MSM_DISPLAY_CAP_CMD_MODE;
		if (display->panel->panel_mode_switch_enabled)
			info->capabilities |= MSM_DISPLAY_CAP_VID_MODE;
		info->is_te_using_watchdog_timer =
			display->panel->te_using_watchdog_timer |
			display->sw_te_using_wd;
		break;
	default:
		DSI_ERR("unknwown dsi panel mode %d\n",
				display->panel->panel_mode);
		break;
	}

	if (display->panel->esd_config.esd_enabled)
		info->capabilities |= MSM_DISPLAY_ESD_ENABLED;

	info->te_source = display->te_source;

	host = &display->panel->host_config;
	if (host->split_link.split_link_enabled)
		info->capabilities |= MSM_DISPLAY_SPLIT_LINK;
error:
	mutex_unlock(&display->display_lock);
	return rc;
}

int dsi_display_get_mode_count(struct dsi_display *display,
			u32 *count)
{
	if (!display || !display->panel) {
		DSI_ERR("invalid display:%d panel:%d\n", display != NULL,
			display ? display->panel != NULL : 0);
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);
	*count = display->panel->num_display_modes;
	mutex_unlock(&display->display_lock);

	return 0;
}

void dsi_display_adjust_mode_timing(struct dsi_display *display,
			struct dsi_display_mode *dsi_mode,
			int lanes, int bpp)
{
	u64 new_htotal, new_vtotal, htotal, vtotal, old_htotal, div;
	struct dsi_dyn_clk_caps *dyn_clk_caps;
	u32 bits_per_symbol = 16, num_of_symbols = 7; /* For Cphy */

	dyn_clk_caps = &(display->panel->dyn_clk_caps);

	/* Constant FPS is not supported on command mode */
	if (dsi_mode->panel_mode == DSI_OP_CMD_MODE)
		return;

	if (!dyn_clk_caps->maintain_const_fps)
		return;
	/*
	 * When there is a dynamic clock switch, there is small change
	 * in FPS. To compensate for this difference in FPS, hfp or vfp
	 * is adjusted. It has been assumed that the refined porch values
	 * are supported by the panel. This logic can be enhanced further
	 * in future by taking min/max porches supported by the panel.
	 */
	switch (dyn_clk_caps->type) {
	case DSI_DYN_CLK_TYPE_CONST_FPS_ADJUST_HFP:
		vtotal = DSI_V_TOTAL(&dsi_mode->timing);
		old_htotal = DSI_H_TOTAL_DSC(&dsi_mode->timing);
		do_div(old_htotal, display->ctrl_count);
		new_htotal = dsi_mode->timing.clk_rate_hz * lanes;
		div = bpp * vtotal * dsi_mode->timing.refresh_rate;
		if (dsi_display_is_type_cphy(display)) {
			new_htotal = new_htotal * bits_per_symbol;
			div = div * num_of_symbols;
		}
		do_div(new_htotal, div);
		if (old_htotal > new_htotal)
			dsi_mode->timing.h_front_porch -=
			((old_htotal - new_htotal) * display->ctrl_count);
		else
			dsi_mode->timing.h_front_porch +=
			((new_htotal - old_htotal) * display->ctrl_count);
		break;

	case DSI_DYN_CLK_TYPE_CONST_FPS_ADJUST_VFP:
		htotal = DSI_H_TOTAL_DSC(&dsi_mode->timing);
		do_div(htotal, display->ctrl_count);
		new_vtotal = dsi_mode->timing.clk_rate_hz * lanes;
		div = bpp * htotal * dsi_mode->timing.refresh_rate;
		if (dsi_display_is_type_cphy(display)) {
			new_vtotal = new_vtotal * bits_per_symbol;
			div = div * num_of_symbols;
		}
		do_div(new_vtotal, div);
		dsi_mode->timing.v_front_porch = new_vtotal -
				dsi_mode->timing.v_back_porch -
				dsi_mode->timing.v_sync_width -
				dsi_mode->timing.v_active;
		break;

	default:
		break;
	}
}

static void _dsi_display_populate_bit_clks(struct dsi_display *display,
					   int start, int end, u32 *mode_idx)
{
	struct dsi_dyn_clk_caps *dyn_clk_caps;
	struct dsi_display_mode *src, *dst;
	struct dsi_host_common_cfg *cfg;
	int i, j, total_modes, bpp, lanes = 0;

	if (!display || !mode_idx)
		return;

	dyn_clk_caps = &(display->panel->dyn_clk_caps);
	if (!dyn_clk_caps->dyn_clk_support)
		return;

	cfg = &(display->panel->host_config);
	bpp = dsi_pixel_format_to_bpp(cfg->dst_format);

	if (cfg->data_lanes & DSI_DATA_LANE_0)
		lanes++;
	if (cfg->data_lanes & DSI_DATA_LANE_1)
		lanes++;
	if (cfg->data_lanes & DSI_DATA_LANE_2)
		lanes++;
	if (cfg->data_lanes & DSI_DATA_LANE_3)
		lanes++;

	total_modes = display->panel->num_display_modes;

	for (i = start; i < end; i++) {
		src = &display->modes[i];
		if (!src)
			return;
		/*
		 * TODO: currently setting the first bit rate in
		 * the list as preferred rate. But ideally should
		 * be based on user or device tree preferrence.
		 */
		src->timing.clk_rate_hz = dyn_clk_caps->bit_clk_list[0];

		dsi_display_adjust_mode_timing(display, src, lanes, bpp);

		src->pixel_clk_khz =
			div_u64(src->timing.clk_rate_hz * lanes, bpp);
		src->pixel_clk_khz /= 1000;
		src->pixel_clk_khz *= display->ctrl_count;
	}

	for (i = 1; i < dyn_clk_caps->bit_clk_list_len; i++) {
		if (*mode_idx >= total_modes)
			return;
		for (j = start; j < end; j++) {
			src = &display->modes[j];
			dst = &display->modes[*mode_idx];

			if (!src || !dst) {
				DSI_ERR("invalid mode index\n");
				return;
			}
			memcpy(dst, src, sizeof(struct dsi_display_mode));
			dst->timing.clk_rate_hz = dyn_clk_caps->bit_clk_list[i];

			dsi_display_adjust_mode_timing(display, dst, lanes,
									bpp);

			dst->pixel_clk_khz =
				div_u64(dst->timing.clk_rate_hz * lanes, bpp);
			dst->pixel_clk_khz /= 1000;
			dst->pixel_clk_khz *= display->ctrl_count;
			(*mode_idx)++;
		}
	}
}

void dsi_display_put_mode(struct dsi_display *display,
	struct dsi_display_mode *mode)
{
	dsi_panel_put_mode(mode);
}

int dsi_display_get_modes(struct dsi_display *display,
			  struct dsi_display_mode **out_modes)
{
	struct dsi_dfps_capabilities dfps_caps;
	struct dsi_display_ctrl *ctrl;
	struct dsi_host_common_cfg *host = &display->panel->host_config;
	bool is_split_link, is_cmd_mode;
	u32 num_dfps_rates, timing_mode_count, display_mode_count;
	u32 sublinks_count, mode_idx, array_idx = 0;
	struct dsi_dyn_clk_caps *dyn_clk_caps;
	int i, start, end, rc = -EINVAL;

	if (!display || !out_modes) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	*out_modes = NULL;
	ctrl = &display->ctrl[0];

	mutex_lock(&display->display_lock);

	if (display->modes)
		goto exit;

	display_mode_count = display->panel->num_display_modes;

	display->modes = kcalloc(display_mode_count, sizeof(*display->modes),
			GFP_KERNEL);
	if (!display->modes) {
		rc = -ENOMEM;
		goto error;
	}

	rc = dsi_panel_get_dfps_caps(display->panel, &dfps_caps);
	if (rc) {
		DSI_ERR("[%s] failed to get dfps caps from panel\n",
				display->name);
		goto error;
	}

	dyn_clk_caps = &(display->panel->dyn_clk_caps);

	timing_mode_count = display->panel->num_timing_nodes;

	/* Validate command line timing */
	if ((display->cmdline_timing != NO_OVERRIDE) &&
		(display->cmdline_timing >= timing_mode_count))
		display->cmdline_timing = NO_OVERRIDE;

	for (mode_idx = 0; mode_idx < timing_mode_count; mode_idx++) {
		struct dsi_display_mode display_mode;
		int topology_override = NO_OVERRIDE;
		bool is_preferred = false;
		u32 frame_threshold_us = ctrl->ctrl->frame_threshold_time_us;

		if (display->cmdline_timing == mode_idx) {
			topology_override = display->cmdline_topology;
			is_preferred = true;
		}

		memset(&display_mode, 0, sizeof(display_mode));

		rc = dsi_panel_get_mode(display->panel, mode_idx,
						&display_mode,
						topology_override);
		if (rc) {
			DSI_ERR("[%s] failed to get mode idx %d from panel\n",
				   display->name, mode_idx);
			goto error;
		}

		is_cmd_mode = (display_mode.panel_mode == DSI_OP_CMD_MODE);

		num_dfps_rates = ((!dfps_caps.dfps_support ||
			is_cmd_mode) ? 1 : dfps_caps.dfps_list_len);

		/* Calculate dsi frame transfer time */
		if (is_cmd_mode) {
			dsi_panel_calc_dsi_transfer_time(
					&display->panel->host_config,
					&display_mode, frame_threshold_us);
			display_mode.priv_info->dsi_transfer_time_us =
				display_mode.timing.dsi_transfer_time_us;
			display_mode.priv_info->min_dsi_clk_hz =
				display_mode.timing.min_dsi_clk_hz;

			display_mode.priv_info->mdp_transfer_time_us =
				display_mode.timing.mdp_transfer_time_us;
		}

		is_split_link = host->split_link.split_link_enabled;
		sublinks_count = host->split_link.num_sublinks;
		if (is_split_link && sublinks_count > 1) {
			display_mode.timing.h_active *= sublinks_count;
			display_mode.timing.h_front_porch *= sublinks_count;
			display_mode.timing.h_sync_width *= sublinks_count;
			display_mode.timing.h_back_porch *= sublinks_count;
			display_mode.timing.h_skew *= sublinks_count;
			display_mode.pixel_clk_khz *= sublinks_count;
		} else {
			display_mode.timing.h_active *= display->ctrl_count;
			display_mode.timing.h_front_porch *=
						display->ctrl_count;
			display_mode.timing.h_sync_width *=
						display->ctrl_count;
			display_mode.timing.h_back_porch *=
						display->ctrl_count;
			display_mode.timing.h_skew *= display->ctrl_count;
			display_mode.pixel_clk_khz *= display->ctrl_count;
		}

		start = array_idx;
		for (i = 0; i < num_dfps_rates; i++) {
			struct dsi_display_mode *sub_mode =
					&display->modes[array_idx];
			u32 curr_refresh_rate;

			if (!sub_mode) {
				DSI_ERR("invalid mode data\n");
				rc = -EFAULT;
				goto error;
			}

			memcpy(sub_mode, &display_mode, sizeof(display_mode));
			array_idx++;

			if (!dfps_caps.dfps_support || is_cmd_mode)
				continue;

			curr_refresh_rate = sub_mode->timing.refresh_rate;
			sub_mode->timing.refresh_rate = dfps_caps.dfps_list[i];

			dsi_display_get_dfps_timing(display, sub_mode,
					curr_refresh_rate);
		}
		end = array_idx;
		/*
		 * if POMS is enabled and boot up mode is video mode,
		 * skip bit clk rates update for command mode,
		 * else if dynamic clk switch is supported then update all
		 * the bit clk rates.
		 */

		if (is_cmd_mode &&
			(display->panel->panel_mode == DSI_OP_VIDEO_MODE))
			continue;

		_dsi_display_populate_bit_clks(display, start, end, &array_idx);
		if (is_preferred) {
			/* Set first timing sub mode as preferred mode */
			display->modes[start].is_preferred = true;
		}
	}

exit:
	*out_modes = display->modes;
    primary_display = display;
	rc = 0;

error:
	if (rc)
		kfree(display->modes);

	mutex_unlock(&display->display_lock);
	return rc;
}

int dsi_display_get_panel_vfp(void *dsi_display,
	int h_active, int v_active)
{
	int i, rc = 0;
	u32 count, refresh_rate = 0;
	struct dsi_dfps_capabilities dfps_caps;
	struct dsi_display *display = (struct dsi_display *)dsi_display;
	struct dsi_host_common_cfg *host;

	if (!display || !display->panel)
		return -EINVAL;

	mutex_lock(&display->display_lock);

	count = display->panel->num_display_modes;

	if (display->panel->cur_mode)
		refresh_rate = display->panel->cur_mode->timing.refresh_rate;

	dsi_panel_get_dfps_caps(display->panel, &dfps_caps);
	if (dfps_caps.dfps_support)
		refresh_rate = dfps_caps.max_refresh_rate;

	if (!refresh_rate) {
		mutex_unlock(&display->display_lock);
		DSI_ERR("Null Refresh Rate\n");
		return -EINVAL;
	}

	host = &display->panel->host_config;
	if (host->split_link.split_link_enabled)
		h_active *= host->split_link.num_sublinks;
	else
		h_active *= display->ctrl_count;

	for (i = 0; i < count; i++) {
		struct dsi_display_mode *m = &display->modes[i];

		if (m && v_active == m->timing.v_active &&
			h_active == m->timing.h_active &&
			refresh_rate == m->timing.refresh_rate) {
			rc = m->timing.v_front_porch;
			break;
		}
	}
	mutex_unlock(&display->display_lock);

	return rc;
}

int dsi_display_get_default_lms(void *dsi_display, u32 *num_lm)
{
	struct dsi_display *display = (struct dsi_display *)dsi_display;
	u32 count, i;
	int rc = 0;

	*num_lm = 0;

	mutex_lock(&display->display_lock);
	count = display->panel->num_display_modes;
	mutex_unlock(&display->display_lock);

	if (!display->modes) {
		struct dsi_display_mode *m;

		rc = dsi_display_get_modes(display, &m);
		if (rc)
			return rc;
	}

	mutex_lock(&display->display_lock);
	for (i = 0; i < count; i++) {
		struct dsi_display_mode *m = &display->modes[i];

		*num_lm = max(m->priv_info->topology.num_lm, *num_lm);
	}
	mutex_unlock(&display->display_lock);

	return rc;
}

int dsi_display_find_mode(struct dsi_display *display,
		const struct dsi_display_mode *cmp,
		struct dsi_display_mode **out_mode)
{
	u32 count, i;
	int rc;

	if (!display || !out_mode)
		return -EINVAL;

	*out_mode = NULL;

	mutex_lock(&display->display_lock);
	count = display->panel->num_display_modes;
	mutex_unlock(&display->display_lock);

	if (!display->modes) {
		struct dsi_display_mode *m;

		rc = dsi_display_get_modes(display, &m);
		if (rc)
			return rc;
	}

	mutex_lock(&display->display_lock);
	for (i = 0; i < count; i++) {
		struct dsi_display_mode *m = &display->modes[i];

		if (cmp->timing.v_active == m->timing.v_active &&
			cmp->timing.h_active == m->timing.h_active &&
			cmp->timing.refresh_rate == m->timing.refresh_rate &&
			cmp->panel_mode == m->panel_mode &&
			cmp->pixel_clk_khz == m->pixel_clk_khz) {
			*out_mode = m;
			rc = 0;
			break;
		}
	}
	mutex_unlock(&display->display_lock);

	if (!*out_mode) {
		DSI_ERR("[%s] failed to find mode for v_active %u h_active %u fps %u pclk %u\n",
				display->name, cmp->timing.v_active,
				cmp->timing.h_active, cmp->timing.refresh_rate,
				cmp->pixel_clk_khz);
		rc = -ENOENT;
	}

	return rc;
}

static inline bool dsi_display_mode_switch_dfps(struct dsi_display_mode *cur,
						struct dsi_display_mode *adj)
{
	/*
	 * If there is a change in the hfp or vfp of the current and adjoining
	 * mode,then either it is a dfps mode switch or dynamic clk change with
	 * constant fps.
	 */
	if ((cur->timing.h_front_porch != adj->timing.h_front_porch) ||
		(cur->timing.v_front_porch != adj->timing.v_front_porch))
		return true;
	else
		return false;
}

/**
 * dsi_display_validate_mode_change() - Validate mode change case.
 * @display:     DSI display handle.
 * @cur_mode:    Current mode.
 * @adj_mode:    Mode to be set.
 *               MSM_MODE_FLAG_SEAMLESS_VRR flag is set if there
 *               is change in hfp or vfp but vactive and hactive are same.
 *               DSI_MODE_FLAG_DYN_CLK flag is set if there
 *               is change in clk but vactive and hactive are same.
 * Return: error code.
 */
u32 mode_fps = 90;
EXPORT_SYMBOL(mode_fps);
int dsi_display_validate_mode_change(struct dsi_display *display,
			struct dsi_display_mode *cur_mode,
			struct dsi_display_mode *adj_mode)
{
	int rc = 0;
	struct dsi_dfps_capabilities dfps_caps;
	struct dsi_dyn_clk_caps *dyn_clk_caps;
	struct drm_panel_notifier notifier_data;
	int dynamic_fps;

	if (!display || !adj_mode) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (!display->panel || !display->panel->cur_mode) {
		DSI_DEBUG("Current panel mode not set\n");
		return rc;
	}

	mutex_lock(&display->display_lock);
	dyn_clk_caps = &(display->panel->dyn_clk_caps);
	if ((cur_mode->timing.v_active == adj_mode->timing.v_active) &&
		(cur_mode->timing.h_active == adj_mode->timing.h_active) &&
		(cur_mode->panel_mode == adj_mode->panel_mode)) {
		/* dfps and dynamic clock with const fps use case */
		if (dsi_display_mode_switch_dfps(cur_mode, adj_mode)) {
			dsi_panel_get_dfps_caps(display->panel, &dfps_caps);

			if (mode_fps != adj_mode->timing.refresh_rate) {
				mode_fps = adj_mode->timing.refresh_rate;
				dynamic_fps = mode_fps;
				notifier_data.data = &dynamic_fps;
				DSI_ERR("set fps: %d\n", dynamic_fps);
				if (lcd_active_panel)
					drm_panel_notifier_call_chain(lcd_active_panel, DRM_PANEL_EARLY_EVENT_BLANK, &notifier_data);
			}

			if (dfps_caps.dfps_support ||
				dyn_clk_caps->maintain_const_fps) {
				DSI_DEBUG("Mode switch is seamless variable refresh\n");
				adj_mode->dsi_mode_flags |= DSI_MODE_FLAG_VRR;
				SDE_EVT32(cur_mode->timing.refresh_rate,
					adj_mode->timing.refresh_rate,
					cur_mode->timing.h_front_porch,
					adj_mode->timing.h_front_porch);
			}
		}

		/* dynamic clk change use case */
		if (cur_mode->pixel_clk_khz != adj_mode->pixel_clk_khz) {
			if (dyn_clk_caps->dyn_clk_support) {
				DSI_DEBUG("dynamic clk change detected\n");
				if ((adj_mode->dsi_mode_flags &
					DSI_MODE_FLAG_VRR) &&
					(!dyn_clk_caps->maintain_const_fps)) {
					DSI_ERR("dfps and dyn clk not supported in same commit\n");
					rc = -ENOTSUPP;
					goto error;
				}

				adj_mode->dsi_mode_flags |=
						DSI_MODE_FLAG_DYN_CLK;
				SDE_EVT32(cur_mode->pixel_clk_khz,
						adj_mode->pixel_clk_khz);
			}
		}
	}

error:
	mutex_unlock(&display->display_lock);
	return rc;
}

int dsi_display_validate_mode(struct dsi_display *display,
			      struct dsi_display_mode *mode,
			      u32 flags)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *ctrl;
	struct dsi_display_mode adj_mode;

	if (!display || !mode) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

	adj_mode = *mode;
	adjust_timing_by_ctrl_count(display, &adj_mode);

	rc = dsi_panel_validate_mode(display->panel, &adj_mode);
	if (rc) {
		DSI_ERR("[%s] panel mode validation failed, rc=%d\n",
		       display->name, rc);
		goto error;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		rc = dsi_ctrl_validate_timing(ctrl->ctrl, &adj_mode.timing);
		if (rc) {
			DSI_ERR("[%s] ctrl mode validation failed, rc=%d\n",
			       display->name, rc);
			goto error;
		}

		rc = dsi_phy_validate_mode(ctrl->phy, &adj_mode.timing);
		if (rc) {
			DSI_ERR("[%s] phy mode validation failed, rc=%d\n",
			       display->name, rc);
			goto error;
		}
	}

	if ((flags & DSI_VALIDATE_FLAG_ALLOW_ADJUST) &&
			(mode->dsi_mode_flags & DSI_MODE_FLAG_SEAMLESS)) {
		rc = dsi_display_validate_mode_seamless(display, mode);
		if (rc) {
			DSI_ERR("[%s] seamless not possible rc=%d\n",
				display->name, rc);
			goto error;
		}
	}

error:
	mutex_unlock(&display->display_lock);
	return rc;
}

int dsi_display_set_mode(struct dsi_display *display,
			 struct dsi_display_mode *mode,
			 u32 flags)
{
	int rc = 0;
	struct dsi_display_mode adj_mode;
	struct dsi_mode_info timing;

	if (!display || !mode || !display->panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

	adj_mode = *mode;
	timing = adj_mode.timing;
	adjust_timing_by_ctrl_count(display, &adj_mode);

	if (!display->panel->cur_mode) {
		display->panel->cur_mode =
			kzalloc(sizeof(struct dsi_display_mode), GFP_KERNEL);
		if (!display->panel->cur_mode) {
			rc = -ENOMEM;
			goto error;
		}
	}

	/*For dynamic DSI setting, use specified clock rate */
	if (display->cached_clk_rate > 0)
		adj_mode.priv_info->clk_rate_hz = display->cached_clk_rate;

	rc = dsi_display_validate_mode_set(display, &adj_mode, flags);
	if (rc) {
		DSI_ERR("[%s] mode cannot be set\n", display->name);
		goto error;
	}

	rc = dsi_display_set_mode_sub(display, &adj_mode, flags);
	if (rc) {
		DSI_ERR("[%s] failed to set mode\n", display->name);
		goto error;
	}

	DSI_INFO("mdp_transfer_time_us=%d us\n",
			adj_mode.priv_info->mdp_transfer_time_us);
	DSI_INFO("hactive= %d,vactive= %d,fps=%d\n",
			timing.h_active, timing.v_active,
			timing.refresh_rate);

	memcpy(display->panel->cur_mode, &adj_mode, sizeof(adj_mode));

	mode_fps = display->panel->cur_mode->timing.refresh_rate;
error:
	mutex_unlock(&display->display_lock);
	return rc;
}

int dsi_display_set_tpg_state(struct dsi_display *display, bool enable)
{
	int rc = 0;
	int i;
	struct dsi_display_ctrl *ctrl;

	if (!display) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		rc = dsi_ctrl_set_tpg_state(ctrl->ctrl, enable);
		if (rc) {
			DSI_ERR("[%s] failed to set tpg state for host_%d\n",
			       display->name, i);
			goto error;
		}
	}

	display->is_tpg_enabled = enable;
error:
	return rc;
}

static int dsi_display_pre_switch(struct dsi_display *display)
{
	int rc = 0;

	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       display->name, rc);
		goto error;
	}

	rc = dsi_display_ctrl_update(display);
	if (rc) {
		DSI_ERR("[%s] failed to update DSI controller, rc=%d\n",
			   display->name, rc);
		goto error_ctrl_clk_off;
	}

	rc = dsi_display_set_clk_src(display);
	if (rc) {
		DSI_ERR("[%s] failed to set DSI link clock source, rc=%d\n",
			display->name, rc);
		goto error_ctrl_deinit;
	}

	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_LINK_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI link clocks, rc=%d\n",
			   display->name, rc);
		goto error_ctrl_deinit;
	}

	goto error;

error_ctrl_deinit:
	(void)dsi_display_ctrl_deinit(display);
error_ctrl_clk_off:
	(void)dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
error:
	return rc;
}

static bool _dsi_display_validate_host_state(struct dsi_display *display)
{
	int i;
	struct dsi_display_ctrl *ctrl;

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		if (!ctrl->ctrl)
			continue;
		if (!dsi_ctrl_validate_host_state(ctrl->ctrl))
			return false;
	}

	return true;
}

static void dsi_display_handle_fifo_underflow(struct work_struct *work)
{
	struct dsi_display *display = NULL;

	display = container_of(work, struct dsi_display, fifo_underflow_work);
	if (!display || !display->panel ||
	    atomic_read(&display->panel->esd_recovery_pending)) {
		DSI_DEBUG("Invalid recovery use case\n");
		return;
	}

	mutex_lock(&display->display_lock);

	if (!_dsi_display_validate_host_state(display)) {
		mutex_unlock(&display->display_lock);
		return;
	}

	DSI_DEBUG("handle DSI FIFO underflow error\n");

	dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_ON);
	dsi_display_soft_reset(display);
	dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_OFF);

	mutex_unlock(&display->display_lock);
}

static void dsi_display_handle_fifo_overflow(struct work_struct *work)
{
	struct dsi_display *display = NULL;
	struct dsi_display_ctrl *ctrl;
	int i, rc;
	int mask = BIT(20); /* clock lane */
	int (*cb_func)(void *event_usr_ptr,
		uint32_t event_idx, uint32_t instance_idx,
		uint32_t data0, uint32_t data1,
		uint32_t data2, uint32_t data3);
	void *data;
	u32 version = 0;

	display = container_of(work, struct dsi_display, fifo_overflow_work);
	if (!display || !display->panel ||
	    (display->panel->panel_mode != DSI_OP_VIDEO_MODE) ||
	    atomic_read(&display->panel->esd_recovery_pending)) {
		DSI_DEBUG("Invalid recovery use case\n");
		return;
	}

	mutex_lock(&display->display_lock);

	if (!_dsi_display_validate_host_state(display)) {
		mutex_unlock(&display->display_lock);
		return;
	}

	DSI_DEBUG("handle DSI FIFO overflow error\n");
	dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_ON);

	/*
	 * below recovery sequence is not applicable to
	 * hw version 2.0.0, 2.1.0 and 2.2.0, so return early.
	 */
	ctrl = &display->ctrl[display->clk_master_idx];
	version = dsi_ctrl_get_hw_version(ctrl->ctrl);
	if (!version || (version < 0x20020001))
		goto end;

	/* reset ctrl and lanes */
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		rc = dsi_ctrl_reset(ctrl->ctrl, mask);
		rc = dsi_phy_lane_reset(ctrl->phy);
	}

	/* wait for display line count to be in active area */
	ctrl = &display->ctrl[display->clk_master_idx];
	if (ctrl->ctrl->recovery_cb.event_cb) {
		cb_func = ctrl->ctrl->recovery_cb.event_cb;
		data = ctrl->ctrl->recovery_cb.event_usr_ptr;
		rc = cb_func(data, SDE_CONN_EVENT_VID_FIFO_OVERFLOW,
				display->clk_master_idx, 0, 0, 0, 0);
		if (rc < 0) {
			DSI_DEBUG("sde callback failed\n");
			goto end;
		}
	}

	/* Enable Video mode for DSI controller */
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		dsi_ctrl_vid_engine_en(ctrl->ctrl, true);
	}
	/*
	 * Add sufficient delay to make sure
	 * pixel transmission has started
	 */
	udelay(200);
end:
	dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_OFF);
	mutex_unlock(&display->display_lock);
}

static void dsi_display_handle_lp_rx_timeout(struct work_struct *work)
{
	struct dsi_display *display = NULL;
	struct dsi_display_ctrl *ctrl;
	int i, rc;
	int mask = (BIT(20) | (0xF << 16)); /* clock lane and 4 data lane */
	int (*cb_func)(void *event_usr_ptr,
		uint32_t event_idx, uint32_t instance_idx,
		uint32_t data0, uint32_t data1,
		uint32_t data2, uint32_t data3);
	void *data;
	u32 version = 0;

	display = container_of(work, struct dsi_display, lp_rx_timeout_work);
	if (!display || !display->panel ||
	    (display->panel->panel_mode != DSI_OP_VIDEO_MODE) ||
	    atomic_read(&display->panel->esd_recovery_pending)) {
		DSI_DEBUG("Invalid recovery use case\n");
		return;
	}

	mutex_lock(&display->display_lock);

	if (!_dsi_display_validate_host_state(display)) {
		mutex_unlock(&display->display_lock);
		return;
	}

	DSI_DEBUG("handle DSI LP RX Timeout error\n");

	dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_ON);

	/*
	 * below recovery sequence is not applicable to
	 * hw version 2.0.0, 2.1.0 and 2.2.0, so return early.
	 */
	ctrl = &display->ctrl[display->clk_master_idx];
	version = dsi_ctrl_get_hw_version(ctrl->ctrl);
	if (!version || (version < 0x20020001))
		goto end;

	/* reset ctrl and lanes */
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		rc = dsi_ctrl_reset(ctrl->ctrl, mask);
		rc = dsi_phy_lane_reset(ctrl->phy);
	}

	ctrl = &display->ctrl[display->clk_master_idx];
	if (ctrl->ctrl->recovery_cb.event_cb) {
		cb_func = ctrl->ctrl->recovery_cb.event_cb;
		data = ctrl->ctrl->recovery_cb.event_usr_ptr;
		rc = cb_func(data, SDE_CONN_EVENT_VID_FIFO_OVERFLOW,
				display->clk_master_idx, 0, 0, 0, 0);
		if (rc < 0) {
			DSI_DEBUG("Target is in suspend/shutdown\n");
			goto end;
		}
	}

	/* Enable Video mode for DSI controller */
	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		dsi_ctrl_vid_engine_en(ctrl->ctrl, true);
	}

	/*
	 * Add sufficient delay to make sure
	 * pixel transmission as started
	 */
	udelay(200);
end:
	dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_OFF);
	mutex_unlock(&display->display_lock);
}

static int dsi_display_cb_error_handler(void *data,
		uint32_t event_idx, uint32_t instance_idx,
		uint32_t data0, uint32_t data1,
		uint32_t data2, uint32_t data3)
{
	struct dsi_display *display =  data;

	if (!display || !(display->err_workq))
		return -EINVAL;

	switch (event_idx) {
	case DSI_FIFO_UNDERFLOW:
		queue_work(display->err_workq, &display->fifo_underflow_work);
		break;
	case DSI_FIFO_OVERFLOW:
		queue_work(display->err_workq, &display->fifo_overflow_work);
		break;
	case DSI_LP_Rx_TIMEOUT:
		queue_work(display->err_workq, &display->lp_rx_timeout_work);
		break;
	default:
		DSI_WARN("unhandled error interrupt: %d\n", event_idx);
		break;
	}

	return 0;
}

static void dsi_display_register_error_handler(struct dsi_display *display)
{
	int i = 0;
	struct dsi_display_ctrl *ctrl;
	struct dsi_event_cb_info event_info;

	if (!display)
		return;

	display->err_workq = create_singlethread_workqueue("dsi_err_workq");
	if (!display->err_workq) {
		DSI_ERR("failed to create dsi workq!\n");
		return;
	}

	INIT_WORK(&display->fifo_underflow_work,
				dsi_display_handle_fifo_underflow);
	INIT_WORK(&display->fifo_overflow_work,
				dsi_display_handle_fifo_overflow);
	INIT_WORK(&display->lp_rx_timeout_work,
				dsi_display_handle_lp_rx_timeout);

	memset(&event_info, 0, sizeof(event_info));

	event_info.event_cb = dsi_display_cb_error_handler;
	event_info.event_usr_ptr = display;

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		ctrl->ctrl->irq_info.irq_err_cb = event_info;
	}
}

static void dsi_display_unregister_error_handler(struct dsi_display *display)
{
	int i = 0;
	struct dsi_display_ctrl *ctrl;

	if (!display)
		return;

	display_for_each_ctrl(i, display) {
		ctrl = &display->ctrl[i];
		memset(&ctrl->ctrl->irq_info.irq_err_cb,
		       0, sizeof(struct dsi_event_cb_info));
	}

	if (display->err_workq) {
		destroy_workqueue(display->err_workq);
		display->err_workq = NULL;
	}
}

int dsi_display_prepare(struct dsi_display *display)
{
	int rc = 0;
	struct dsi_display_mode *mode;

	if (!display) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (!display->panel->cur_mode) {
		DSI_ERR("no valid mode set for the display\n");
		return -EINVAL;
	}

	SDE_EVT32(SDE_EVTLOG_FUNC_ENTRY);
	mutex_lock(&display->display_lock);

	mode = display->panel->cur_mode;

	dsi_display_set_ctrl_esd_check_flag(display, false);

	/* Set up ctrl isr before enabling core clk */
	dsi_display_ctrl_isr_configure(display, true);

	if (mode->dsi_mode_flags & DSI_MODE_FLAG_DMS) {
		if (display->is_cont_splash_enabled &&
		    display->config.panel_mode == DSI_OP_VIDEO_MODE) {
			DSI_ERR("DMS not supported on first frame\n");
			rc = -EINVAL;
			goto error;
		}

		if (!display->is_cont_splash_enabled) {
			/* update dsi ctrl for new mode */
			rc = dsi_display_pre_switch(display);
			if (rc)
				DSI_ERR("[%s] panel pre-switch failed, rc=%d\n",
					display->name, rc);
			goto error;
		}
	}

	if (!(mode->dsi_mode_flags & DSI_MODE_FLAG_POMS) &&
		(!display->is_cont_splash_enabled)) {
		/*
		 * For continuous splash usecase we skip panel
		 * pre prepare since the regulator vote is already
		 * taken care in splash resource init
		 */
		rc = dsi_panel_pre_prepare(display->panel);
		if (rc) {
			DSI_ERR("[%s] panel pre-prepare failed, rc=%d\n",
					display->name, rc);
			goto error;
		}
	}

	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       display->name, rc);
		goto error_panel_post_unprep;
	}

	/*
	 * If ULPS during suspend feature is enabled, then DSI PHY was
	 * left on during suspend. In this case, we do not need to reset/init
	 * PHY. This would have already been done when the CORE clocks are
	 * turned on. However, if cont splash is disabled, the first time DSI
	 * is powered on, phy init needs to be done unconditionally.
	 */
	if (!display->panel->ulps_suspend_enabled || !display->ulps_enabled) {
		rc = dsi_display_phy_sw_reset(display);
		if (rc) {
			DSI_ERR("[%s] failed to reset phy, rc=%d\n",
				display->name, rc);
			goto error_ctrl_clk_off;
		}

		rc = dsi_display_phy_enable(display);
		if (rc) {
			DSI_ERR("[%s] failed to enable DSI PHY, rc=%d\n",
			       display->name, rc);
			goto error_ctrl_clk_off;
		}
	}

	rc = dsi_display_set_clk_src(display);
	if (rc) {
		DSI_ERR("[%s] failed to set DSI link clock source, rc=%d\n",
			display->name, rc);
		goto error_phy_disable;
	}

	rc = dsi_display_ctrl_init(display);
	if (rc) {
		DSI_ERR("[%s] failed to setup DSI controller, rc=%d\n",
		       display->name, rc);
		goto error_phy_disable;
	}
	/* Set up DSI ERROR event callback */
	dsi_display_register_error_handler(display);

	rc = dsi_display_ctrl_host_enable(display);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI host, rc=%d\n",
		       display->name, rc);
		goto error_ctrl_deinit;
	}

	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_LINK_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI link clocks, rc=%d\n",
		       display->name, rc);
		goto error_host_engine_off;
	}

	if (!display->is_cont_splash_enabled) {
		/*
		 * For continuous splash usecase, skip panel prepare and
		 * ctl reset since the pnael and ctrl is already in active
		 * state and panel on commands are not needed
		 */
		rc = dsi_display_soft_reset(display);
		if (rc) {
			DSI_ERR("[%s] failed soft reset, rc=%d\n",
					display->name, rc);
			goto error_ctrl_link_off;
		}

		if (!(mode->dsi_mode_flags & DSI_MODE_FLAG_POMS)) {
			rc = dsi_panel_prepare(display->panel);
			if (rc) {
				DSI_ERR("[%s] panel prepare failed, rc=%d\n",
						display->name, rc);
				goto error_ctrl_link_off;
			}
		}
	}
	goto error;

error_ctrl_link_off:
	(void)dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_LINK_CLK, DSI_CLK_OFF);
error_host_engine_off:
	(void)dsi_display_ctrl_host_disable(display);
error_ctrl_deinit:
	(void)dsi_display_ctrl_deinit(display);
error_phy_disable:
	(void)dsi_display_phy_disable(display);
error_ctrl_clk_off:
	(void)dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
error_panel_post_unprep:
	(void)dsi_panel_post_unprepare(display->panel);
error:
	mutex_unlock(&display->display_lock);
	SDE_EVT32(SDE_EVTLOG_FUNC_EXIT);
#if defined(CONFIG_PXLW_IRIS)
	iris_prepare(display);
#endif
	return rc;
}

static int dsi_display_calc_ctrl_roi(const struct dsi_display *display,
		const struct dsi_display_ctrl *ctrl,
		const struct msm_roi_list *req_rois,
		struct dsi_rect *out_roi)
{
	const struct dsi_rect *bounds = &ctrl->ctrl->mode_bounds;
	struct dsi_display_mode *cur_mode;
	struct msm_roi_caps *roi_caps;
	struct dsi_rect req_roi = { 0 };
	int rc = 0;

	cur_mode = display->panel->cur_mode;
	if (!cur_mode)
		return 0;

	roi_caps = &cur_mode->priv_info->roi_caps;
	if (req_rois->num_rects > roi_caps->num_roi) {
		DSI_ERR("request for %d rois greater than max %d\n",
				req_rois->num_rects,
				roi_caps->num_roi);
		rc = -EINVAL;
		goto exit;
	}

	/**
	 * if no rois, user wants to reset back to full resolution
	 * note: h_active is already divided by ctrl_count
	 */
	if (!req_rois->num_rects) {
		*out_roi = *bounds;
		goto exit;
	}

	/* intersect with the bounds */
	req_roi.x = req_rois->roi[0].x1;
	req_roi.y = req_rois->roi[0].y1;
	req_roi.w = req_rois->roi[0].x2 - req_rois->roi[0].x1;
	req_roi.h = req_rois->roi[0].y2 - req_rois->roi[0].y1;
	dsi_rect_intersect(&req_roi, bounds, out_roi);

exit:
	/* adjust the ctrl origin to be top left within the ctrl */
	out_roi->x = out_roi->x - bounds->x;

	DSI_DEBUG("ctrl%d:%d: req (%d,%d,%d,%d) bnd (%d,%d,%d,%d) out (%d,%d,%d,%d)\n",
			ctrl->dsi_ctrl_idx, ctrl->ctrl->cell_index,
			req_roi.x, req_roi.y, req_roi.w, req_roi.h,
			bounds->x, bounds->y, bounds->w, bounds->h,
			out_roi->x, out_roi->y, out_roi->w, out_roi->h);

	return rc;
}

static int dsi_display_qsync(struct dsi_display *display, bool enable)
{
	int i;
	int rc = 0;

	if (!display->panel->qsync_min_fps) {
		DSI_ERR("%s:ERROR: qsync set, but no fps\n", __func__);
		return 0;
	}

	mutex_lock(&display->display_lock);

	display_for_each_ctrl(i, display) {
		if (enable) {
			/* send the commands to enable qsync */
			rc = dsi_panel_send_qsync_on_dcs(display->panel, i);
			if (rc) {
				DSI_ERR("fail qsync ON cmds rc:%d\n", rc);
				goto exit;
			}
		} else {
			/* send the commands to enable qsync */
			rc = dsi_panel_send_qsync_off_dcs(display->panel, i);
			if (rc) {
				DSI_ERR("fail qsync OFF cmds rc:%d\n", rc);
				goto exit;
			}
		}

		dsi_ctrl_setup_avr(display->ctrl[i].ctrl, enable);
	}

exit:
	SDE_EVT32(enable, display->panel->qsync_min_fps, rc);
	mutex_unlock(&display->display_lock);
	return rc;
}

static int dsi_display_set_roi(struct dsi_display *display,
		struct msm_roi_list *rois)
{
	struct dsi_display_mode *cur_mode;
	struct msm_roi_caps *roi_caps;
	int rc = 0;
	int i;

	if (!display || !rois || !display->panel)
		return -EINVAL;

	cur_mode = display->panel->cur_mode;
	if (!cur_mode)
		return 0;

	roi_caps = &cur_mode->priv_info->roi_caps;
	if (!roi_caps->enabled)
		return 0;

	display_for_each_ctrl(i, display) {
		struct dsi_display_ctrl *ctrl = &display->ctrl[i];
		struct dsi_rect ctrl_roi;
		bool changed = false;

		rc = dsi_display_calc_ctrl_roi(display, ctrl, rois, &ctrl_roi);
		if (rc) {
			DSI_ERR("dsi_display_calc_ctrl_roi failed rc %d\n", rc);
			return rc;
		}

		rc = dsi_ctrl_set_roi(ctrl->ctrl, &ctrl_roi, &changed);
		if (rc) {
			DSI_ERR("dsi_ctrl_set_roi failed rc %d\n", rc);
			return rc;
		}

		if (!changed)
			continue;

		/* send the new roi to the panel via dcs commands */
		rc = dsi_panel_send_roi_dcs(display->panel, i, &ctrl_roi);
		if (rc) {
			DSI_ERR("dsi_panel_set_roi failed rc %d\n", rc);
			return rc;
		}

		/* re-program the ctrl with the timing based on the new roi */
		rc = dsi_ctrl_timing_setup(ctrl->ctrl);
		if (rc) {
			DSI_ERR("dsi_ctrl_setup failed rc %d\n", rc);
			return rc;
		}
	}

	return rc;
}

int dsi_display_pre_kickoff(struct drm_connector *connector,
		struct dsi_display *display,
		struct msm_display_kickoff_params *params)
{
	int rc = 0;
	int i;

	SDE_ATRACE_BEGIN("dsi_display_pre_kickoff");
	/* check and setup MISR */
	if (display->misr_enable)
		_dsi_display_setup_misr(display);

	rc = dsi_display_set_roi(display, params->rois);

	/* dynamic DSI clock setting */
	if (atomic_read(&display->clkrate_change_pending)) {
		mutex_lock(&display->display_lock);
		/*
		 * acquire panel_lock to make sure no commands are in progress
		 */
		dsi_panel_acquire_panel_lock(display->panel);

		/*
		 * Wait for DSI command engine not to be busy sending data
		 * from display engine.
		 * If waiting fails, return "rc" instead of below "ret" so as
		 * not to impact DRM commit. The clock updating would be
		 * deferred to the next DRM commit.
		 */
		display_for_each_ctrl(i, display) {
			struct dsi_ctrl *ctrl = display->ctrl[i].ctrl;
			int ret = 0;
			SDE_ATRACE_BEGIN("dsi_ctrl_wait_for_cmd_mode_mdp_idle");
			ret = dsi_ctrl_wait_for_cmd_mode_mdp_idle(ctrl);
			SDE_ATRACE_END("dsi_ctrl_wait_for_cmd_mode_mdp_idle");
			if (ret)
				goto wait_failure;
		}

		/*
		 * Don't check the return value so as not to impact DRM commit
		 * when error occurs.
		 */
		SDE_ATRACE_BEGIN("dsi_display_force_update_dsi_clk");		 
		(void)dsi_display_force_update_dsi_clk(display);
		SDE_ATRACE_END("dsi_display_force_update_dsi_clk");
wait_failure:
		/* release panel_lock */
		dsi_panel_release_panel_lock(display->panel);
		mutex_unlock(&display->display_lock);
	}

	SDE_ATRACE_END("dsi_display_pre_kickoff");
	return rc;
}

int dsi_display_config_ctrl_for_cont_splash(struct dsi_display *display)
{
	int rc = 0;

	if (!display || !display->panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (!display->panel->cur_mode) {
		DSI_ERR("no valid mode set for the display\n");
		return -EINVAL;
	}

	if (!display->is_cont_splash_enabled)
		return 0;

	if (display->config.panel_mode == DSI_OP_VIDEO_MODE) {
		rc = dsi_display_vid_engine_enable(display);
		if (rc) {
			DSI_ERR("[%s]failed to enable DSI video engine, rc=%d\n",
			       display->name, rc);
			goto error_out;
		}
	} else if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_cmd_engine_enable(display);
		if (rc) {
			DSI_ERR("[%s]failed to enable DSI cmd engine, rc=%d\n",
			       display->name, rc);
			goto error_out;
		}
	} else {
		DSI_ERR("[%s] Invalid configuration\n", display->name);
		rc = -EINVAL;
	}

error_out:
	return rc;
}

int dsi_display_pre_commit(void *display,
		struct msm_display_conn_params *params)
{
	bool enable = false;
	int rc = 0;

	if (!display || !params) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	if (params->qsync_update) {
		enable = (params->qsync_mode > 0) ? true : false;
		rc = dsi_display_qsync(display, enable);
		if (rc)
			pr_err("%s failed to send qsync commands\n",
				__func__);
		SDE_EVT32(params->qsync_mode, rc);
	}

	return rc;
}

int dsi_display_enable(struct dsi_display *display)
{
	int rc = 0;
	struct dsi_display_mode *mode;
	SDE_ATRACE_BEGIN("dsi_display_enable");

	if (!display || !display->panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (!display->panel->cur_mode) {
		DSI_ERR("no valid mode set for the display\n");
		return -EINVAL;
	}
	SDE_EVT32(SDE_EVTLOG_FUNC_ENTRY);

	/* Engine states and panel states are populated during splash
	 * resource init and hence we return early
	 */
	if (display->is_cont_splash_enabled) {

		dsi_display_config_ctrl_for_cont_splash(display);
#if defined(CONFIG_PXLW_IRIS)
		iris_send_cont_splash(display);
#endif

		rc = dsi_display_splash_res_cleanup(display);
		if (rc) {
			DSI_ERR("Continuous splash res cleanup failed, rc=%d\n",
				rc);
			return -EINVAL;
		}

		display->panel->panel_initialized = true;
		DSI_DEBUG("cont splash enabled, display enable not required\n");
		return 0;
	}

	mutex_lock(&display->display_lock);

	mode = display->panel->cur_mode;

	if (mode->dsi_mode_flags & DSI_MODE_FLAG_DMS) {
		SDE_ATRACE_BEGIN("dsi_panel_post_switch");
		rc = dsi_panel_post_switch(display->panel);
		SDE_ATRACE_END("dsi_panel_post_switch");
		if (rc) {
			DSI_ERR("[%s] failed to switch DSI panel mode, rc=%d\n",
				   display->name, rc);
			goto error;
		}
	} else if (!(display->panel->cur_mode->dsi_mode_flags &
			DSI_MODE_FLAG_POMS)){
		SDE_ATRACE_BEGIN("dsi_panel_enable");	
		rc = dsi_panel_enable(display->panel);
		SDE_ATRACE_END("dsi_panel_enable");
		if (rc) {
			DSI_ERR("[%s] failed to enable DSI panel, rc=%d\n",
			       display->name, rc);
			goto error;
		}
	}

	/* Block sending pps command if modeset is due to fps difference */
	if (mode->priv_info->dsc_enabled) {
			mode->priv_info->dsc.pic_width *= display->ctrl_count;
			SDE_ATRACE_BEGIN("dsi_panel_update_pps");
			rc = dsi_panel_update_pps(display->panel);
			SDE_ATRACE_END("dsi_panel_update_pps");
			if (rc) {
			DSI_ERR("[%s] panel pps cmd update failed, rc=%d\n",
				display->name, rc);
			goto error;
		}
	}

	if (mode->dsi_mode_flags & DSI_MODE_FLAG_DMS) {
		SDE_ATRACE_BEGIN("dsi_panel_switch");
		rc = dsi_panel_switch(display->panel);
		SDE_ATRACE_END("dsi_panel_switch");
		if (rc)
			DSI_ERR("[%s] failed to switch DSI panel mode, rc=%d\n",
				   display->name, rc);

		goto error;
	}

	if (display->config.panel_mode == DSI_OP_VIDEO_MODE) {
		DSI_DEBUG("%s:enable video timing eng\n", __func__);
		rc = dsi_display_vid_engine_enable(display);
		if (rc) {
			DSI_ERR("[%s]failed to enable DSI video engine, rc=%d\n",
			       display->name, rc);
			goto error_disable_panel;
		}
	} else if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		DSI_DEBUG("%s:enable command timing eng\n", __func__);
		SDE_ATRACE_BEGIN("dsi_display_cmd_engine_enable");
		rc = dsi_display_cmd_engine_enable(display);
		SDE_ATRACE_END("dsi_display_cmd_engine_enable");
		if (rc) {
			DSI_ERR("[%s]failed to enable DSI cmd engine, rc=%d\n",
			       display->name, rc);
			goto error_disable_panel;
		}
	} else {
		DSI_ERR("[%s] Invalid configuration\n", display->name);
		rc = -EINVAL;
		goto error_disable_panel;
	}
	SDE_ATRACE_END("dsi_display_enable");

	goto error;

error_disable_panel:
	(void)dsi_panel_disable(display->panel);
error:
	mutex_unlock(&display->display_lock);
	SDE_EVT32(SDE_EVTLOG_FUNC_EXIT);

	SDE_ATRACE_END("dsi_display_enable");
	return rc;
}

int dsi_display_post_enable(struct dsi_display *display)
{
	int rc = 0;

	if (!display) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

	if (display->panel->cur_mode->dsi_mode_flags & DSI_MODE_FLAG_POMS) {
		if (display->config.panel_mode == DSI_OP_CMD_MODE)
			dsi_panel_mode_switch_to_cmd(display->panel);

		if (display->config.panel_mode == DSI_OP_VIDEO_MODE)
			dsi_panel_mode_switch_to_vid(display->panel);
	} else {
		rc = dsi_panel_post_enable(display->panel);
		if (rc)
			DSI_ERR("[%s] panel post-enable failed, rc=%d\n",
				display->name, rc);
	}

	/* remove the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE)
		dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_OFF);

	mutex_unlock(&display->display_lock);
	return rc;
}

int dsi_display_pre_disable(struct dsi_display *display)
{
	int rc = 0;

	if (!display) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

	/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE)
		dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_ON);
	if (display->poms_pending) {
		if (display->config.panel_mode == DSI_OP_CMD_MODE)
			dsi_panel_pre_mode_switch_to_video(display->panel);

		if (display->config.panel_mode == DSI_OP_VIDEO_MODE) {
			/*
			 * Add unbalanced vote for clock & cmd engine to enable
			 * async trigger of pre video to cmd mode switch.
			 */
			rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
					DSI_ALL_CLKS, DSI_CLK_ON);
			if (rc) {
				DSI_ERR("[%s]failed to enable all clocks,rc=%d",
						display->name, rc);
				goto exit;
			}

			rc = dsi_display_cmd_engine_enable(display);
			if (rc) {
				DSI_ERR("[%s]failed to enable cmd engine,rc=%d",
						display->name, rc);
				goto error_disable_clks;
			}

			dsi_panel_pre_mode_switch_to_cmd(display->panel);
		}
	} else {
		rc = dsi_panel_pre_disable(display->panel);
		if (rc)
			DSI_ERR("[%s] panel pre-disable failed, rc=%d\n",
				display->name, rc);
	}
	goto exit;

error_disable_clks:
	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_OFF);
	if (rc)
		DSI_ERR("[%s] failed to disable all DSI clocks, rc=%d\n",
		       display->name, rc);

exit:
	mutex_unlock(&display->display_lock);
	return rc;
}

int dsi_display_disable(struct dsi_display *display)
{
	int rc = 0;

	if (!display) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	SDE_EVT32(SDE_EVTLOG_FUNC_ENTRY);
	mutex_lock(&display->display_lock);

	rc = dsi_display_wake_up(display);
	if (rc)
		DSI_ERR("[%s] display wake up failed, rc=%d\n",
		       display->name, rc);

	if (display->config.panel_mode == DSI_OP_VIDEO_MODE) {
		rc = dsi_display_vid_engine_disable(display);
		if (rc)
			DSI_ERR("[%s]failed to disable DSI vid engine, rc=%d\n",
			       display->name, rc);
	} else if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_cmd_engine_disable(display);
		if (rc)
			DSI_ERR("[%s]failed to disable DSI cmd engine, rc=%d\n",
			       display->name, rc);
	} else {
		DSI_ERR("[%s] Invalid configuration\n", display->name);
		rc = -EINVAL;
	}

	if (!display->poms_pending) {
		rc = dsi_panel_disable(display->panel);
		if (rc)
			DSI_ERR("[%s] failed to disable DSI panel, rc=%d\n",
				display->name, rc);
	}
	mutex_unlock(&display->display_lock);
	SDE_EVT32(SDE_EVTLOG_FUNC_EXIT);
	return rc;
}

int dsi_display_update_pps(char *pps_cmd, void *disp)
{
	struct dsi_display *display;

	if (pps_cmd == NULL || disp == NULL) {
		DSI_ERR("Invalid parameter\n");
		return -EINVAL;
	}

	display = disp;
	mutex_lock(&display->display_lock);
	memcpy(display->panel->dsc_pps_cmd, pps_cmd, DSI_CMD_PPS_SIZE);
	mutex_unlock(&display->display_lock);

	return 0;
}
int dsi_display_set_acl_mode(struct drm_connector *connector, int level)
{
    struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	mutex_lock(&dsi_display->display_lock);

	panel->acl_mode = level;
	if (!dsi_panel_initialized(panel)) {
		goto error;
    }
	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_set_acl_mode(panel, level);
	if (rc)
		DSI_ERR("unable to set acl mode\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}
error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}

int dsi_display_get_acl_mode(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	return dsi_display->panel->acl_mode;
}

int dsi_display_register_read(struct dsi_display *dsi_display, unsigned char registers, char *buf, size_t count)
{
	int rc = 0;
	int flags = 0;
	int cmd_count = 0;
	int retry_times = 0;
	unsigned char *payload;
	struct dsi_cmd_desc *cmds;
	struct dsi_display_mode *mode;
	struct dsi_display_ctrl *m_ctrl;

	if (!dsi_display || !dsi_display->panel->cur_mode || !registers || !buf || !count) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	mode = dsi_display->panel->cur_mode;
	m_ctrl = &dsi_display->ctrl[dsi_display->cmd_master_idx];
	cmd_count = mode->priv_info->cmd_sets[DSI_CMD_SET_REGISTER_READ].count;

	if (!m_ctrl || !m_ctrl->ctrl->vaddr || !cmd_count) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	cmds = mode->priv_info->cmd_sets[DSI_CMD_SET_REGISTER_READ].cmds;
	payload = (u8 *)cmds[0].msg.tx_buf;
	payload[0] = registers;

	if (cmds->last_command) {
		cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
		flags |= DSI_CTRL_CMD_LAST_COMMAND;
	}
	flags |= (DSI_CTRL_CMD_FETCH_MEMORY | DSI_CTRL_CMD_READ);
	cmds->msg.rx_buf = buf;
	cmds->msg.rx_len = count;
	do {
		rc = dsi_ctrl_cmd_transfer(m_ctrl->ctrl, &cmds->msg, &flags);
		retry_times++;
	} while ((rc <= 0) && (retry_times < 3));
	if (rc <= 0)
		DSI_ERR("rx cmd transfer failed rc=%d\n", rc);

	return rc;
}

int dsi_display_get_gamma_para(struct dsi_display *dsi_display, struct dsi_panel *panel)
{
	int i = 0;
	int j = 0;
	int rc = 0;
	char fb_temp[13] = {0};
	char c8_temp[135] = {0};
	char c9_temp[180] = {0};
	char b3_temp[47] = {0};
	char gamma_para_60hz[452] = {0};
	char gamma_para_backup[413] = {0};
	int check_sum_60hz = 0;

	DSI_ERR("start\n", __func__);

	if (!panel || !panel->cur_mode)
		return -EINVAL;

	rc = dsi_display_cmd_engine_enable(dsi_display);
	if (rc) {
		DSI_ERR("cmd engine enable failed\n");
		return -EINVAL;
	}

	dsi_panel_acquire_panel_lock(panel);

/* Read 60hz gamma para */
	memcpy(gamma_para_backup, gamma_para[0], 413);
	do {
		check_sum_60hz = 0;
		if (j > 0) {
			DSI_ERR("Failed to read the 60hz gamma parameters %d!", j);
			for (i = 0; i < 52; i++) {
				if (i != 51) {
					DSI_ERR("[60hz][%d]0x%02X,[%d]0x%02X,[%d]0x%02X,[%d]0x%02X,[%d]0x%02X,[%d]0x%02X,[%d]0x%02X,[%d]0x%02X",
						i*8, gamma_para[0][i*8], i*8+1, gamma_para[0][i*8+1], i*8+2, gamma_para[0][i*8+2], i*8+3, gamma_para[0][i*8+3], i*8+4, gamma_para[0][i*8+4],
							i*8+5, gamma_para[0][i*8+5], i*8+6, gamma_para[0][i*8+6], i*8+7, gamma_para[0][i*8+7]);
				} else {
					DSI_ERR("[60hz][%d]0x%02X,[%d]0x%02X,[%d]0x%02X,[%d]0x%02X,[%d]0x%02X",
						i*8, gamma_para[0][i*8], i*8+1, gamma_para[0][i*8+1], i*8+2, gamma_para[0][i*8+2], i*8+3, gamma_para[0][i*8+3], i*8+4, gamma_para[0][i*8+4]);
				}
			}
			mdelay(1000);
		}
		for(i = 0; i < 452; i++) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_GAMMA_FLASH_PRE_READ_1);
			if (rc) {
				DSI_ERR("Failed to send DSI_CMD_SET_GAMMA_FLASH_PRE_READ_1 command\n");
				goto error;
			}

			rc = dsi_panel_gamma_read_address_setting(panel, i);
			if (rc) {
				DSI_ERR("Failed to set gamma read address\n");
				goto error;
			}

			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_GAMMA_FLASH_PRE_READ_2);
			if (rc) {
				DSI_ERR("Failed to send DSI_CMD_SET_GAMMA_FLASH_PRE_READ_2 command\n");
				goto error;
			}

			rc = dsi_display_register_read(dsi_display, 0xFB, fb_temp, 13);
			if (rc <= 0) {
				DSI_ERR("Failed to read 0xFB registers\n");
				goto error;
			}

			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_DISABLE);
			if (rc) {
				DSI_ERR("Failed to send DSI_CMD_SET_LEVEL2_KEY_DISABLE command\n");
				goto error;
			}

			if (i < 135) {
				gamma_para[0][i+18] = fb_temp[12];
			}
			else if (i < 315) {
				gamma_para[0][i+26] = fb_temp[12];
			}
			else if (i < 360) {
				gamma_para[0][i+43] = fb_temp[12];
			}

			gamma_para_60hz[i] = fb_temp[12];
			if (i < 449) {
				check_sum_60hz = gamma_para_60hz[i] + check_sum_60hz;
			}
			j++;
		}
	}
	while ((check_sum_60hz != (gamma_para_60hz[450] << 8) + gamma_para_60hz[451]) && (j < 10));

	if (check_sum_60hz == (gamma_para_60hz[450] << 8) + gamma_para_60hz[451]) {
		DSI_ERR("Read 60hz gamma done\n");
	}
	else {
		DSI_ERR("Failed to read 60hz gamma, use default 60hz gamma.\n");
		memcpy(gamma_para[0], gamma_para_backup, 413);
		gamma_read_flag = GAMMA_READ_ERROR;
	}

/* Read 90hz gamma para */
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_ENABLE);
	if (rc) {
		DSI_ERR("Failed to send DSI_CMD_SET_LEVEL2_KEY_ENABLE command\n");
		goto error;
	}

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_GAMMA_OTP_READ_C8_SMRPS);
	if (rc) {
		DSI_ERR("Failed to send DSI_CMD_SET_GAMMA_OTP_READ_C8_SMRPS command\n");
		goto error;
	}

	rc = dsi_display_register_read(dsi_display, 0xC8, c8_temp, 135);
	if (rc <= 0) {
		DSI_ERR("Failed to read 0xC8 registers\n");
		goto error;
	}
	memcpy(&gamma_para[1][18], c8_temp, 135);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_GAMMA_OTP_READ_C9_SMRPS);
	if (rc) {
		DSI_ERR("Failed to send DSI_CMD_SET_GAMMA_OTP_READ_C9_SMRPS command\n");
		goto error;
	}

	rc = dsi_display_register_read(dsi_display, 0xC9, c9_temp, 180);
	if (rc <= 0) {
		DSI_ERR("Failed to read 0xC8 registers\n");
		goto error;
	}
	memcpy(&gamma_para[1][161], c9_temp, 180);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_GAMMA_OTP_READ_B3_SMRPS);
	if (rc) {
		DSI_ERR("Failed to send DSI_CMD_SET_GAMMA_OTP_READ_C9_SMRPS command\n");
		goto error;
	}

	rc = dsi_display_register_read(dsi_display, 0xB3, b3_temp, 47);
	if (rc <= 0) {
		DSI_ERR("Failed to read 0xB3 registers\n");
		goto error;
	}
	memcpy(&gamma_para[1][358], &b3_temp[2], 45);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_DISABLE);
	if (rc) {
		DSI_ERR("Failed to send DSI_CMD_SET_GAMMA_OTP_READ_C9_SMRPS command\n");
		goto error;
	}
	DSI_ERR("Read 90hz gamma done\n");

error:
	dsi_panel_release_panel_lock(panel);
	dsi_display_cmd_engine_disable(dsi_display);
	DSI_ERR("%s end\n", __func__);
	return rc;
}

extern bool HBM_flag;
int dsi_display_get_dimming_gamma_para(struct dsi_display *dsi_display, struct dsi_panel *panel)
{
	int rc = 0;
	int count = 0;
	unsigned char payload = 0;
	struct mipi_dsi_device *dsi;
	struct dsi_display_mode *mode;

	mode = panel->cur_mode;
	dsi = &panel->mipi_device;
	if (!dsi_display || !panel || !mode || !dsi)
		return -EINVAL;

	rc = dsi_display_cmd_engine_enable(dsi_display);
	if (rc) {
		DSI_ERR("cmd engine enable failed\n");
		return rc;
	}

	dsi_panel_acquire_panel_lock(panel);

	if (strcmp(panel->name, "samsung ana6706 dsc cmd mode panel") == 0) {
		count = mode->priv_info->cmd_sets[DSI_CMD_SET_LEVEL2_KEY_ENABLE].count;
		if (!count) {
			DSI_ERR("This panel does not support level2 key enable command\n");
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_ENABLE);
			if (rc) {
				DSI_ERR("Failed to send DSI_CMD_SET_LEVEL2_KEY_ENABLE commands\n");
				goto error;
			}
		}

		payload = 0xA4;
		rc = mipi_dsi_dcs_write(dsi, 0xB0, &payload, sizeof(payload));
		if (rc < 0) {
			DSI_ERR("Failed to write mipi dsi dcs cmd\n");
			goto error;
		}

		rc = dsi_display_register_read(dsi_display, 0xC9, dimming_gamma_60hz, 5);
		if (rc <= 0) {
			DSI_ERR("Failed to read 0xC9 registers\n");
			goto error;
		}

		count = mode->priv_info->cmd_sets[DSI_CMD_SET_LEVEL2_KEY_DISABLE].count;
		if (!count) {
			DSI_ERR("This panel does not support level2 key disable command\n");
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_DISABLE);
			if (rc) {
				DSI_ERR("Failed to send DSI_CMD_SET_LEVEL2_KEY_DISABLE commands\n");
				goto error;
			}
		}

		count = mode->priv_info->cmd_sets[DSI_CMD_SET_LEVEL2_KEY_ENABLE].count;
		if (!count) {
			DSI_ERR("This panel does not support level2 key enable command\n");
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_ENABLE);
			if (rc) {
				DSI_ERR("Failed to send DSI_CMD_SET_LEVEL2_KEY_ENABLE commands\n");
				goto error;
			}
		}

		payload = 0xA7;
		rc = mipi_dsi_dcs_write(dsi, 0xB0, &payload, sizeof(payload));
		if (rc < 0) {
			DSI_ERR("Failed to write mipi dsi dcs cmd\n");
			goto error;
		}

		rc = dsi_display_register_read(dsi_display, 0xC7, dimming_gamma_120hz, 5);
		if (rc <= 0) {
			DSI_ERR("Failed to read 0xC7 registers\n");
			goto error;
		}

		count = mode->priv_info->cmd_sets[DSI_CMD_SET_LEVEL2_KEY_DISABLE].count;
		if (!count) {
			DSI_ERR("This panel does not support level2 key disable command\n");
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_DISABLE);
			if (rc) {
				DSI_ERR("Failed to send DSI_CMD_SET_LEVEL2_KEY_DISABLE commands\n");
				goto error;
			}
		}

		count = mode->priv_info->cmd_sets[DSI_CMD_SET_LEVEL2_KEY_ENABLE].count;
		if (!count) {
			DSI_ERR("This panel does not support level2 key enable command\n");
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_ENABLE);
			if (rc) {
				DSI_ERR("Failed to send DSI_CMD_SET_LEVEL2_KEY_ENABLE commands\n");
				goto error;
			}
		}

		payload = 0x17;
		rc = mipi_dsi_dcs_write(dsi, 0xB0, &payload, sizeof(payload));
		if (rc < 0) {
			DSI_ERR("Failed to write mipi dsi dcs cmd\n");
			goto error;
		}

		rc = dsi_display_register_read(dsi_display, 0xC9, &dimming_gamma_60hz[15], 5);
		if (rc <= 0) {
			DSI_ERR("Failed to read 0xC9 registers\n");
			goto error;
		}

		count = mode->priv_info->cmd_sets[DSI_CMD_SET_LEVEL2_KEY_DISABLE].count;
		if (!count) {
			DSI_ERR("This panel does not support level2 key disable command\n");
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_DISABLE);
			if (rc) {
				DSI_ERR("Failed to send DSI_CMD_SET_LEVEL2_KEY_DISABLE commands\n");
				goto error;
			}
		}
	} else {
	count = mode->priv_info->cmd_sets[DSI_CMD_SET_LEVEL2_KEY_ENABLE].count;
		if (!count) {
			DSI_ERR("This panel does not support level2 key enable command\n");
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_ENABLE);
			if (rc) {
				DSI_ERR("Failed to send DSI_CMD_SET_LEVEL2_KEY_ENABLE commands\n");
				goto error;
			}
		}

		rc = dsi_display_register_read(dsi_display, 0xB9, b9_register_value_500step, 229);
		if (rc <= 0) {
			DSI_ERR("Failed to read 0xB9 registers\n");
			goto error;
		}

		count = mode->priv_info->cmd_sets[DSI_CMD_SET_LEVEL2_KEY_DISABLE].count;
		if (!count) {
			DSI_ERR("This panel does not support level2 key disable command\n");
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_DISABLE);
			if (rc) {
				DSI_ERR("Failed to send DSI_CMD_SET_LEVEL2_KEY_DISABLE commands\n");
				goto error;
			}
		}
	}
	dsi_panel_update_gamma_change_write(panel);

	rc = dsi_panel_dimming_gamma_write(panel);
	if (rc < 0)
		DSI_ERR("Failed to write dimming gamma, rc=%d\n", rc);
	HBM_flag = false;

error:
	dsi_panel_release_panel_lock(panel);
	dsi_display_cmd_engine_disable(dsi_display);
	return rc;
}

int dsi_display_gamma_read(struct dsi_display *dsi_display)
{
	int rc = 0;
	struct dsi_panel *panel = NULL;

	DSI_ERR("start\n");
	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;
	mutex_lock(&dsi_display->display_lock);

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle, DSI_ALL_CLKS, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI clocks, rc=%d\n", dsi_display->name, rc);
		goto error;
	}

	if (strcmp(dsi_display->panel->name, "samsung dsc cmd mode oneplus dsi panel") == 0) {
		rc = dsi_display_get_gamma_para(dsi_display, panel);
		if (rc)
			DSI_ERR("Failed to dsi_display_get_gamma_para\n");
	} else if ((strcmp(dsi_display->panel->name, "samsung ana6706 dsc cmd mode panel") == 0)
		|| (strcmp(dsi_display->panel->name, "samsung amb655x fhd cmd mode dsc dsi panel") == 0)) {
		rc = dsi_display_get_dimming_gamma_para(dsi_display, panel);
		if (rc)
			DSI_ERR("Failed to dsi_display_get_dimming_gamma_para\n");
	}
	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle, DSI_ALL_CLKS, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI clocks, rc=%d\n", dsi_display->name, rc);
		goto error;
	}

error:
	mutex_unlock(&dsi_display->display_lock);
	DSI_ERR("end\n");
	return rc;
}

void dsi_display_gamma_read_work(struct work_struct *work)
{
	struct dsi_display *dsi_display;

	dsi_display = get_main_display();

	if (strcmp(dsi_display->panel->name, "samsung dsc cmd mode oneplus dsi panel") == 0) {
		if (((dsi_display->panel->panel_production_info & 0x0F) == 0x0C)
			|| ((dsi_display->panel->panel_production_info & 0x0F) == 0x0E)
				|| ((dsi_display->panel->panel_production_info & 0x0F) == 0x0D))
			dsi_display_gamma_read(dsi_display);
		dsi_panel_parse_gamma_cmd_sets();
	} else if ((strcmp(dsi_display->panel->name, "samsung ana6706 dsc cmd mode panel") == 0)
		|| (strcmp(dsi_display->panel->name, "samsung amb655x fhd cmd mode dsc dsi panel") == 0)) {
		dsi_display_gamma_read(dsi_display);
	}
}

int dsi_display_update_gamma_para(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

	if ((connector == NULL) || (connector->encoder == NULL)
			|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	rc = dsi_display_gamma_read(dsi_display);
	if (rc)
		DSI_ERR("Failed to read gamma para, rc=%d\n", rc);

	return rc;
}

int dsi_display_read_serial_number(struct dsi_display *dsi_display,
		struct dsi_panel *panel, char *buf, int len)
{
	int rc = 0;
	int count = 0;
	unsigned char panel_ic_v = 0;
	unsigned char register_d6[10] = {0};
	int ddic_x = 0;
	int ddic_y = 0;
	unsigned char code_info = 0;
	unsigned char stage_info = 0;
	unsigned char prodution_info = 0;

	struct dsi_display_mode *mode;

	if (!panel || !panel->cur_mode)
		return -EINVAL;

	rc = dsi_display_cmd_engine_enable(dsi_display);
	if (rc) {
		DSI_ERR("cmd engine enable failed\n");
		return -EINVAL;
	}

	dsi_panel_acquire_panel_lock(panel);
	mode = panel->cur_mode;

	count = mode->priv_info->cmd_sets[DSI_CMD_SET_LEVEL2_KEY_ENABLE].count;
	if (!count) {
		DSI_ERR("This panel does not support level2 key enable command\n");
	} else {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_ENABLE);
		if (rc) {
			DSI_ERR("Failed to send DSI_CMD_SET_LEVEL2_KEY_ENABLE commands\n");
			goto error;
		}
	}

	if(strcmp(panel->name, "samsung ana6706 dsc cmd mode panel") == 0) {
		dsi_display_register_read(dsi_display, 0xFA, &panel_ic_v, 1);
		panel->panel_ic_v = panel_ic_v & 0x0f;
	}

	if ((strcmp(panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0)
		|| (strcmp(panel->name, "samsung ana6706 dsc cmd mode panel") == 0)) {
		dsi_display_register_read(dsi_display, 0xD6, register_d6, 10);

		memcpy(panel->buf_select, register_d6, 10);
		panel->panel_tool = dsi_display_back_ToolsType_ANA6706(register_d6);
		DSI_ERR("reg_d6: %02x %02x %02x %02x %02x %02x %02x\n", register_d6[0], register_d6[1], register_d6[2], register_d6[3], register_d6[4], register_d6[5], register_d6[6]);

		ddic_x = (((register_d6[3] & 0x1f) << 4) | ((register_d6[4] & 0xf0) >> 4));
		ddic_y = (register_d6[4] & 0x0f);
		panel->ddic_x = ddic_x;
		panel->ddic_y = ddic_y;
		DSI_ERR("ddic_x = %d, ddic_y = %d\n", panel->ddic_x, panel->ddic_y);
		len = 14;
	}

	dsi_display_register_read(dsi_display, 0xA1, buf, len);

	dsi_display_register_read(dsi_display, 0xDA, &code_info, 1);
	panel->panel_code_info = code_info;
	DSI_ERR("Code info is 0x%X\n", panel->panel_code_info);

	dsi_display_register_read(dsi_display, 0xDB, &stage_info, 1);
	panel->panel_stage_info = stage_info;
	DSI_ERR("Stage info is 0x%X\n", panel->panel_stage_info);

	dsi_display_register_read(dsi_display, 0xDC, &prodution_info, 1);
	panel->panel_production_info = prodution_info;
	DSI_ERR("Production info is 0x%X\n", panel->panel_production_info);

	count = mode->priv_info->cmd_sets[DSI_CMD_SET_LEVEL2_KEY_DISABLE].count;
	if (!count) {
		DSI_ERR("This panel does not support level2 key disable command\n");
	} else {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_DISABLE);
		if (rc) {
			DSI_ERR("Failed to send DSI_CMD_SET_LEVEL2_KEY_DISABLE commands\n");
			goto error;
		}
	}

error:
	dsi_panel_release_panel_lock(panel);
	dsi_display_cmd_engine_disable(dsi_display);
	return rc;
}

int dsi_display_get_serial_number(struct drm_connector *connector)
{
	struct dsi_display_mode *mode;
	struct dsi_panel *panel;
	struct dsi_display *dsi_display;
	struct dsi_bridge *c_bridge;
	int len = 0;
	int count = 0;
	int rc = -EINVAL;

	char buf[32] = {0};
	int panel_year = 0;
	int panel_mon = 0;
	int panel_day = 0;
	int panel_hour = 0;
	int panel_min = 0;
	int panel_sec = 0;
	int panel_msec = 0;
	int panel_msec_int = 0;
	int panel_msec_rem = 0;

	DSI_DEBUG("start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;
	mutex_lock(&dsi_display->display_lock);

	if (!dsi_panel_initialized(panel) || !panel->cur_mode)
		goto error;

	mode = panel->cur_mode;
	count = mode->priv_info->cmd_sets[DSI_CMD_SET_REGISTER_READ].count;
	if (count) {
		len = panel->panel_msec_low_index;
		if (len > sizeof(buf)) {
			DSI_ERR("len is large than buf size!!!\n");
			goto error;
		}

		if ((panel->panel_year_index > len) || (panel->panel_mon_index > len)
			|| (panel->panel_day_index > len) || (panel->panel_hour_index > len)
				|| (panel->panel_min_index > len) || (panel->panel_sec_index > len)
					|| (panel->panel_msec_high_index > len)) {
			DSI_ERR("Panel serial number index not corrected\n");
			goto error;
		}

		rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_ON);
		if (rc) {
			DSI_ERR("[%s] failed to enable DSI clocks, rc=%d\n",
				dsi_display->name, rc);
			goto error;
		}

		dsi_display_read_serial_number(dsi_display, panel, buf, len);
		memcpy(panel->buf_id, buf, 32);

		panel_year = 2011 + ((buf[panel->panel_year_index - 1] >> 4) & 0x0f);
		if (panel_year == 2011)
			DSI_ERR("Panel Year not corrected.\n");
		panel_mon = buf[panel->panel_mon_index - 1] & 0x0f;
		if ((panel_mon > 12) || (panel_mon < 1)) {
			DSI_ERR("Panel Mon not corrected.\n");
			panel_mon = 0;
		}
		panel_day = buf[panel->panel_day_index - 1] & 0x3f;
		if ((panel_day > 31) || (panel_day < 1)) {
			DSI_ERR("Panel Day not corrected.\n");
			panel_day = 0;
		}
		panel_hour = buf[panel->panel_hour_index - 1] & 0x3f;
		if ((panel_hour > 23) || (panel_hour < 0)) {
			DSI_ERR("Panel Hour not corrected.\n");
			panel_hour = 0;
		}
		panel_min = buf[panel->panel_min_index - 1] & 0x3f;
		if ((panel_min > 59) || (panel_min < 0)) {
			DSI_ERR("Panel Min not corrected.\n");
			panel_min = 0;
		}
		panel_sec = buf[panel->panel_sec_index - 1] & 0x3f;
		if ((panel_sec > 59) || (panel_sec < 0)) {
			DSI_ERR("Panel sec not corrected.\n");
			panel_sec = 0;
		}
		panel_msec = ((buf[panel->panel_msec_high_index - 1] << 8) | buf[panel->panel_msec_low_index - 1]);
		if ((panel_msec > 9999) || (panel_msec < 0)) {
			DSI_ERR("Panel msec not corrected.\n");
			panel_msec = 0;
		}
		panel_msec_int = panel_msec / 10;
		panel_msec_rem = panel_msec % 10;

		panel->panel_year = panel_year;
		panel->panel_mon = panel_mon;
		panel->panel_day = panel_day;
		panel->panel_hour = panel_hour;
		panel->panel_min = panel_min;
		panel->panel_sec = panel_sec;
		panel->panel_msec = panel_msec;
		panel->panel_msec_int = panel_msec_int;
		panel->panel_msec_rem = panel_msec_rem;

		panel->panel_serial_number = (u64)panel_year * 10000000000 + (u64)panel_mon * 100000000 + (u64)panel_day * 1000000
											 + (u64)panel_hour * 10000 + (u64)panel_min * 100 + (u64)panel_sec;

		rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_OFF);
		if (rc) {
			DSI_ERR("[%s] failed to enable DSI clocks, rc=%d\n",
			dsi_display->name, rc);
			goto error;
		}
	} else {
		DSI_ERR("This panel not support serial number.\n");
	}

error:
	mutex_unlock(&dsi_display->display_lock);
	DSI_DEBUG("end\n");
	return rc;
}

int dsi_display_get_serial_number_year(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("end\n");

	return dsi_display->panel->panel_year;
}

int dsi_display_get_serial_number_mon(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("end\n");

	return dsi_display->panel->panel_mon;
}

int dsi_display_get_serial_number_day(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->panel_day;
}

int dsi_display_get_serial_number_hour(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("end\n");

	return dsi_display->panel->panel_hour;
}

int dsi_display_get_serial_number_min(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("end\n");

	return dsi_display->panel->panel_min;
}

int dsi_display_get_serial_number_sec(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("end\n");

	return dsi_display->panel->panel_sec;
}

int dsi_display_get_serial_number_msec_int(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("end\n");

	return dsi_display->panel->panel_msec_int;
}

int dsi_display_get_serial_number_msec_rem(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("end\n");

	return dsi_display->panel->panel_msec_rem;
}

int dsi_display_get_serial_number_at(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("end\n");

	return dsi_display->panel->panel_serial_number;
}

int dsi_display_get_code_info(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("end\n");

	return dsi_display->panel->panel_code_info;
}

int dsi_display_get_stage_info(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("end\n");

	return dsi_display->panel->panel_stage_info;
}

int dsi_display_get_production_info(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("end\n");

	return dsi_display->panel->panel_production_info;
}

int dsi_display_get_panel_ic_v_info(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("end\n");

	return dsi_display->panel->panel_ic_v;
}

char* dsi_display_get_ic_reg_buf(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	DSI_DEBUG("end\n");

	return dsi_display->panel->buf_select;
}

int dsi_display_get_ToolsType_ANA6706(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("end\n");

	return dsi_display->panel->panel_tool;
}

int dsi_display_get_ddic_coords_X(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("end\n");

	return dsi_display->panel->ddic_x;
}

int dsi_display_get_ddic_coords_Y(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("end\n");

	return dsi_display->panel->ddic_y;
}

int dsi_display_get_ddic_check_info(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;
	int ddic_x = 0;
	int panel_tool = 0;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	ddic_x = dsi_display->panel->ddic_x;
	panel_tool = dsi_display->panel->panel_tool;

/*
	ToolB			0
	ToolA			1
	ToolA_HVS30		2
*/

	switch (dsi_display->panel->ddic_y) {
	case 2:
		if ((strcmp(dsi_display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) &&
		(ddic_x > 115) && (ddic_x < 186)) {
			dsi_display->panel->ddic_check_info = 1;
		} else
			dsi_display->panel->ddic_check_info = 0;
		break;

	case 3:
		if ((strcmp(dsi_display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) &&
		(ddic_x > 56) && (ddic_x < 245)) {
			dsi_display->panel->ddic_check_info = 1;
		} else if (strcmp(dsi_display->panel->name, "samsung ana6706 dsc cmd mode panel") == 0) {
			if ((panel_tool == 0) && (ddic_x > 54) && (ddic_x < 140))
				dsi_display->panel->ddic_check_info = 1;
			else if (((panel_tool == 1) || (panel_tool == 2)) && (ddic_x > 32) && (ddic_x < 154))
				dsi_display->panel->ddic_check_info = 1;
			else
				dsi_display->panel->ddic_check_info = 0;
		} else
			dsi_display->panel->ddic_check_info = 0;
		break;

	case 4:
		if ((strcmp(dsi_display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) &&
		(ddic_x > 40) && (ddic_x < 261)) {
			dsi_display->panel->ddic_check_info = 1;
		} else if (strcmp(dsi_display->panel->name, "samsung ana6706 dsc cmd mode panel") == 0) {
			if ((panel_tool == 0) && (ddic_x > 46) && (ddic_x < 140))
				dsi_display->panel->ddic_check_info = 1;
			else if (((panel_tool == 1) || (panel_tool == 2)) && (ddic_x > 24) && (ddic_x < 162))
				dsi_display->panel->ddic_check_info = 1;
			else
				dsi_display->panel->ddic_check_info = 0;
		} else
			dsi_display->panel->ddic_check_info = 0;
		break;

	case 5:
		if ((strcmp(dsi_display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) &&
		(ddic_x > 33) && (ddic_x < 268)) {
			dsi_display->panel->ddic_check_info = 1;
		} else if (strcmp(dsi_display->panel->name, "samsung ana6706 dsc cmd mode panel") == 0) {
			if ((panel_tool == 0) && (ddic_x > 46) && (ddic_x < 140))
				dsi_display->panel->ddic_check_info = 1;
			else if (((panel_tool == 1) || (panel_tool == 2)) && (ddic_x > 23) && (ddic_x < 163))
				dsi_display->panel->ddic_check_info = 1;
			else
				dsi_display->panel->ddic_check_info = 0;
		} else
			dsi_display->panel->ddic_check_info = 0;
		break;

	case 6:
		if ((strcmp(dsi_display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) &&
		(ddic_x > 41) && (ddic_x < 261)) {
			dsi_display->panel->ddic_check_info = 1;
		} else if (strcmp(dsi_display->panel->name, "samsung ana6706 dsc cmd mode panel") == 0) {
			if((panel_tool == 0) && (ddic_x > 54) && (ddic_x < 132))
				dsi_display->panel->ddic_check_info = 1;
			else if (((panel_tool == 1) || (panel_tool == 2)) && (ddic_x > 30) && (ddic_x < 156))
				dsi_display->panel->ddic_check_info = 1;
			else
				dsi_display->panel->ddic_check_info = 0;
		} else
			dsi_display->panel->ddic_check_info = 0;
		break;

	case 7:
		if ((strcmp(dsi_display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) &&
		(ddic_x > 57) && (ddic_x < 245)) {
			dsi_display->panel->ddic_check_info = 1;
		} else if (strcmp(dsi_display->panel->name, "samsung ana6706 dsc cmd mode panel") == 0) {
			if(((panel_tool == 1) || (panel_tool == 2)) && (ddic_x > 45) && (ddic_x < 141))
				dsi_display->panel->ddic_check_info = 1;
			else
				dsi_display->panel->ddic_check_info = 0;
		} else
			dsi_display->panel->ddic_check_info = 0;
		break;

	case 8:
		if ((strcmp(dsi_display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) &&
		(ddic_x > 119) && (ddic_x < 183)) {
			dsi_display->panel->ddic_check_info = 1;
		} else
			dsi_display->panel->ddic_check_info = 0;
		break;

	default:
		dsi_display->panel->ddic_check_info = 0;
		break;
	}

	DSI_DEBUG("Result:panel_tool = %d\n", panel_tool);

	if (panel_tool == 10)
		dsi_display->panel->ddic_check_info = 0;

	DSI_DEBUG("end\n");

	return dsi_display->panel->ddic_check_info;
}

int iris_loop_back_test(struct drm_connector *connector)
{
	int ret = -1;
#if defined(CONFIG_PXLW_IRIS)
	struct iris_cfg *pcfg;
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_ERR("%s start\n", __func__);
	if ((connector == NULL) || (connector->encoder == NULL)
			|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	if (dsi_display->panel->panel_initialized == true) {
		pcfg = iris_get_cfg();
		mutex_lock(&pcfg->lb_mutex);
		ret = iris_loop_back_validate();
		DSI_ERR("iris_loop_back_validate finish, ret = %d", ret);
		mutex_unlock(&pcfg->lb_mutex);
	}
	DSI_ERR("%s end\n", __func__);
#endif
	return ret;
}

int dsi_display_set_seed_lp_mode(struct drm_connector *connector, int seed_lp_level)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;
	panel->seed_lp_mode = seed_lp_level;
	if (!dsi_panel_initialized(panel)) {
		goto error;
	}
	mutex_lock(&dsi_display->display_lock);
		rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_set_seed_lp_mode(panel, seed_lp_level);
	if (rc)
		DSI_ERR("unable to set seed lp mode\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}
error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}
int dsi_display_get_seed_lp_mode(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	return dsi_display->panel->seed_lp_mode;
}
int dsi_display_set_hbm_mode(struct drm_connector *connector, int level)
{
    struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	mutex_lock(&dsi_display->display_lock);

	panel->hbm_mode = level;
	if (!dsi_panel_initialized(panel)) {
		goto error;
	}

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_set_hbm_mode(panel, level);
	if (rc)
		DSI_ERR("unable to set hbm mode\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}
error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}

int dsi_display_get_hbm_mode(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	return dsi_display->panel->hbm_mode;
}

int dsi_display_set_hbm_brightness(struct drm_connector *connector, int level)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

	if ((connector == NULL) || (connector->encoder == NULL)
			|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	if ((strcmp(dsi_display->panel->name, "samsung dsc cmd mode oneplus dsi panel") != 0)
		&& (strcmp(dsi_display->panel->name, "BOE dsc cmd mode oneplus dsi panel") != 0)
		&& (strcmp(dsi_display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") != 0)
		&& (strcmp(panel->name, "samsung ana6706 dsc cmd mode panel") != 0)
		&& (strcmp(panel->name, "samsung dd305 fhd cmd mode dsc dsi panel") != 0)
		&& (strcmp(panel->name, "samsung amb655x fhd cmd mode dsc dsi panel") != 0)) {
		dsi_display->panel->hbm_brightness = 0;
		return 0;
	}

	mutex_lock(&dsi_display->display_lock);

	panel->hbm_brightness = level;

	if (!dsi_panel_initialized(panel))
		goto error;

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
			dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_set_hbm_brightness(panel, level);
	if (rc)
		DSI_ERR("Failed to set hbm brightness mode\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
			dsi_display->name, rc);
		goto error;
	}

error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}

int dsi_display_get_hbm_brightness(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	if ((connector == NULL) || (connector->encoder == NULL)
			|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	return dsi_display->panel->hbm_brightness;
}

extern int oneplus_force_screenfp;

int dsi_display_set_fp_hbm_mode(struct drm_connector *connector, int level)
{
    struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	mutex_lock(&dsi_display->display_lock);

	panel->op_force_screenfp = level;
	oneplus_force_screenfp=panel->op_force_screenfp;
	if (!dsi_panel_initialized(panel)) {
		goto error;
	}

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_op_set_hbm_mode(panel, level);
	if (rc)
		DSI_ERR("unable to set hbm mode\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}
error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}


int dsi_display_get_fp_hbm_mode(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	return dsi_display->panel->op_force_screenfp;
}

int dsi_display_set_dci_p3_mode(struct drm_connector *connector, int level)
{
    struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	mutex_lock(&dsi_display->display_lock);

	panel->dci_p3_mode = level;
	if (!dsi_panel_initialized(panel)) {
		goto error;
	}

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_set_dci_p3_mode(panel, level);
	if (rc)
		DSI_ERR("unable to set dci_p3 mode\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}
error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}

int dsi_display_get_dci_p3_mode(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	return dsi_display->panel->dci_p3_mode;
}

int dsi_display_set_night_mode(struct drm_connector *connector, int level)
{
    struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	mutex_lock(&dsi_display->display_lock);

	panel->night_mode = level;
	if (!dsi_panel_initialized(panel)) {
		goto error;
	}

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_set_night_mode(panel, level);
	if (rc)
		DSI_ERR("unable to set night mode\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}
error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}

int dsi_display_get_night_mode(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	return dsi_display->panel->night_mode;
}
int dsi_display_set_native_display_p3_mode(struct drm_connector *connector, int level)
{
    struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	mutex_lock(&dsi_display->display_lock);

	panel->naive_display_p3_mode = level;
	if (!dsi_panel_initialized(panel)) {
		goto error;
	}

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_set_native_display_p3_mode(panel, level);
	if (rc)
		DSI_ERR("unable to set native display p3 mode\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}
error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}

int dsi_display_get_native_display_p3_mode(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	return dsi_display->panel->naive_display_p3_mode;
}

int dsi_display_set_native_display_wide_color_mode(struct drm_connector *connector, int level)
{
    struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	mutex_lock(&dsi_display->display_lock);

	panel->naive_display_wide_color_mode = level;
	if (!dsi_panel_initialized(panel)) {
		goto error;
	}

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_set_native_display_wide_color_mode(panel, level);
	if (rc)
		DSI_ERR("unable to set native display p3 mode\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}
error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}

int dsi_display_set_native_loading_effect_mode(struct drm_connector *connector, int level)
{
    struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	mutex_lock(&dsi_display->display_lock);

	panel->naive_display_loading_effect_mode = level;
	if (!dsi_panel_initialized(panel)) {
		goto error;
	}

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_set_native_loading_effect_mode(panel, level);
	if (rc)
		DSI_ERR("unable to set loading effect mode\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}
error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}

int dsi_display_set_customer_srgb_mode(struct drm_connector *connector, int level)
{
    struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	mutex_lock(&dsi_display->display_lock);

	panel->naive_display_customer_srgb_mode = level;
	if (!dsi_panel_initialized(panel)) {
		goto error;
	}

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_set_customer_srgb_mode(panel, level);
	if (rc)
		DSI_ERR("unable to set customer srgb mode\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}
error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}

int dsi_display_set_customer_p3_mode(struct drm_connector *connector, int level)
{
    struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	mutex_lock(&dsi_display->display_lock);

	panel->naive_display_customer_p3_mode = level;
	if (!dsi_panel_initialized(panel)) {
		goto error;
	}

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_set_customer_p3_mode(panel, level);
	if (rc)
		DSI_ERR("unable to set customer srgb mode\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}
error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}
int dsi_display_set_native_display_srgb_color_mode(struct drm_connector *connector, int level)
{
    struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	mutex_lock(&dsi_display->display_lock);

	panel->naive_display_srgb_color_mode = level;
	if (!dsi_panel_initialized(panel)) {
		goto error;
	}

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_set_native_display_srgb_color_mode(panel, level);
	if (rc)
		DSI_ERR("unable to set native display p3 mode\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}
error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}

int dsi_display_get_native_display_srgb_color_mode(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	return dsi_display->panel->naive_display_srgb_color_mode;
}

int dsi_display_get_native_display_wide_color_mode(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	return dsi_display->panel->naive_display_wide_color_mode;
}

int dsi_display_get_native_display_loading_effect_mode(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	return dsi_display->panel->naive_display_loading_effect_mode;
}
int dsi_display_get_customer_srgb_mode(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	return dsi_display->panel->naive_display_customer_srgb_mode;
}
int dsi_display_get_customer_p3_mode(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	return dsi_display->panel->naive_display_customer_p3_mode;
}

int dsi_display_set_aod_mode(struct drm_connector *connector, int level)
{
    struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;
	panel->aod_mode = level;

	mutex_lock(&dsi_display->display_lock);
	if (!dsi_panel_initialized(panel)) {
		goto error;
	}

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}
	if ((dsi_display->panel->aod_mode != 5) && (dsi_display->panel->aod_mode != 4)) {
		rc = dsi_panel_set_aod_mode(panel, level);
		if (rc)
			DSI_ERR("unable to set aod mode\n");
	}

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
		       dsi_display->name, rc);
		goto error;
	}
error:
	mutex_unlock(&dsi_display->display_lock);

	return rc;
}

int dsi_display_get_aod_mode(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	return dsi_display->panel->aod_mode;
}

int dsi_display_set_aod_disable(struct drm_connector *connector, int disable)
{
    struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;
	mutex_lock(&dsi_display->display_lock);
	panel->aod_disable = disable;
	mutex_unlock(&dsi_display->display_lock);

	return rc;
}

int dsi_display_get_aod_disable(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	return dsi_display->panel->aod_disable;
}

int dsi_display_get_mca_setting_mode(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	if ((connector == NULL) || (connector->encoder == NULL)
			|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	return dsi_display->panel->mca_setting_mode;
}

int dsi_display_set_mca_setting_mode(struct drm_connector *connector, int mca_setting_mode)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

	if ((connector == NULL) || (connector->encoder == NULL)
			|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	if (strcmp(panel->name, "samsung ana6706 dsc cmd mode panel") != 0) {
		dsi_display->panel->mca_setting_mode = 1;
		return 0;
	}

	mutex_lock(&dsi_display->display_lock);

	panel->mca_setting_mode = mca_setting_mode;

	if (!dsi_panel_initialized(panel))
		goto error;

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
			dsi_display->name, rc);
		goto error;
	}

	mutex_lock(&panel->panel_lock);
	rc = dsi_panel_set_mca_setting_mode(panel, mca_setting_mode);
	if (rc)
		DSI_ERR("Failed to set mca setting mode %d\n", mca_setting_mode);
	mutex_unlock(&panel->panel_lock);

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
			dsi_display->name, rc);
		goto error;
	}

error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}

int dsi_display_read_panel_id(struct dsi_display *dsi_display,
		struct dsi_panel *panel, char* buf, int len)
{
	int rc = 0;
	u32 flags = 0;
	struct dsi_cmd_desc *cmds;
    struct dsi_display_mode *mode;
    struct dsi_display_ctrl *m_ctrl;
    int retry_times;

    m_ctrl = &dsi_display->ctrl[dsi_display->cmd_master_idx];

	if (!panel || !m_ctrl)
		return -EINVAL;

    rc = dsi_display_cmd_engine_enable(dsi_display);
    if (rc) {
        DSI_ERR("cmd engine enable failed\n");
        return -EINVAL;
    }

	dsi_panel_acquire_panel_lock(panel);

    mode = panel->cur_mode;
	cmds = mode->priv_info->cmd_sets[DSI_CMD_SET_PANEL_ID].cmds;;
	if (cmds->last_command) {
		cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
		flags |= DSI_CTRL_CMD_LAST_COMMAND;
	}
	flags |= (DSI_CTRL_CMD_FETCH_MEMORY | DSI_CTRL_CMD_READ);
    if (!m_ctrl->ctrl->vaddr)
        goto error;

	cmds->msg.rx_buf = buf;
	cmds->msg.rx_len = len;
	retry_times = 0;
    do {
	    rc = dsi_ctrl_cmd_transfer(m_ctrl->ctrl, &cmds->msg, &flags);
	    retry_times++;
	} while ((rc <= 0) && (retry_times < 3));

	if (rc <= 0)
		DSI_ERR("rx cmd transfer failed rc=%d\n", rc);

 error:
	dsi_panel_release_panel_lock(panel);

	dsi_display_cmd_engine_disable(dsi_display);

	return rc;
}

char dsi_display_ascii_to_int(char ascii, int *ascii_err)
{
	char int_value;

	if ((ascii >= 48) && (ascii <= 57)){
		int_value = ascii - 48;
	}
	else if ((ascii >= 65) && (ascii <= 70)) {
		int_value = ascii - 65 + 10;
	}
	else if ((ascii >= 97) && (ascii <= 102)) {
		int_value = ascii - 97 + 10;
	}
	else {
		int_value = 0;
		*ascii_err = 1;
		DSI_ERR("Bad para: %d , please enter the right value!", ascii);
	}

	return int_value;
}

int dsi_display_update_dsi_on_command(struct drm_connector *connector, const char *buf, size_t count)
{
	int i = 0;
	int j = 0;
	int ascii_err = 0;
	unsigned int length;
	char *data;
	struct dsi_panel_cmd_set *set;

	struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

	if ((connector == NULL) || (connector->encoder == NULL)
			|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	mutex_lock(&dsi_display->display_lock);

	length = count / 3;
	data = kzalloc(length + 1, GFP_KERNEL);

	for (i = 0; (buf[i+2] != 10) && (j < length); i = i+3) {
		data[j] = dsi_display_ascii_to_int(buf[i], &ascii_err) << 4;
		data[j] += dsi_display_ascii_to_int(buf[i+1], &ascii_err);
		j++;
	}
	data[j] = dsi_display_ascii_to_int(buf[i], &ascii_err) << 4;
	data[j] += dsi_display_ascii_to_int(buf[i+1], &ascii_err);
	if (ascii_err == 1) {
		DSI_ERR("Bad Para, ignore this command\n");
		goto error;
	}

	set = &panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_ON];

	rc = dsi_panel_update_cmd_sets_sub(set, DSI_CMD_SET_ON, data, length);
	if (rc)
		DSI_ERR("Failed to update_cmd_sets_sub, rc=%d\n", rc);

error:
	kfree(data);
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}

static int dsi_display_get_mipi_dsi_msg(const struct mipi_dsi_msg *msg, char* buf)
{
	int len = 0;
	size_t i;
	char *tx_buf = (char*)msg->tx_buf;
	/* Packet Info */
	len += snprintf(buf + len, PAGE_SIZE - len, "%02X ", msg->type);
	/* Last bit */
	len += snprintf(buf + len, PAGE_SIZE - len, "%02X ", (msg->flags & MIPI_DSI_MSG_LASTCOMMAND) ? 1 : 0);
	len += snprintf(buf + len, PAGE_SIZE - len, "%02X ", msg->channel);
	len += snprintf(buf + len, PAGE_SIZE - len, "%02X ", (unsigned int)msg->flags);
	/* Delay */
	len += snprintf(buf + len, PAGE_SIZE - len, "%02X ", msg->wait_ms);
	len += snprintf(buf + len, PAGE_SIZE - len, "%02X %02X ", msg->tx_len >> 8, msg->tx_len & 0x00FF);

	/* Packet Payload */
	for (i = 0 ; i < msg->tx_len ; i++) {
		len += snprintf(buf + len, PAGE_SIZE - len, "%02X ", tx_buf[i]);
	}
	len += snprintf(buf + len, PAGE_SIZE - len, "\n");

	return len;
}

int dsi_display_get_dsi_on_command(struct drm_connector *connector, char *buf)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;
	struct dsi_panel_cmd_set *cmd;
	int i = 0;
	int count = 0;

	if ((connector == NULL) || (connector->encoder == NULL)
			|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	cmd = &dsi_display->panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_ON];

	for (i = 0; i < cmd->count; i++) {
		count += dsi_display_get_mipi_dsi_msg(&cmd->cmds[i].msg, &buf[count]);
	}

	return count;
}

int dsi_display_update_dsi_panel_command(struct drm_connector *connector, const char *buf, size_t count)
{
	int i = 0;
	int j = 0;
	int ascii_err = 0;
	unsigned int length;
	char *data;
	struct dsi_panel_cmd_set *set;

	struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

	if ((connector == NULL) || (connector->encoder == NULL)
			|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	if ((strcmp(panel->name, "samsung dsc cmd mode oneplus dsi panel") != 0) &&
		(strcmp(panel->name, "samsung sofef03f_m fhd cmd mode dsc dsi panel") != 0) &&
			(strcmp(panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") != 0) &&
		(strcmp(panel->name, "samsung amb655x fhd cmd mode dsc dsi panel") != 0) &&
				(strcmp(panel->name, "samsung ana6706 dsc cmd mode panel") != 0)) {
		return 0;
	}

	mutex_lock(&dsi_display->display_lock);

	if (!dsi_panel_initialized(panel))
		goto error;

	length = count / 3;
	data = kzalloc(length + 1, GFP_KERNEL);

	for (i = 0; (buf[i+2] != 10) && (j < length); i = i+3) {
		data[j] = dsi_display_ascii_to_int(buf[i], &ascii_err) << 4;
		data[j] += dsi_display_ascii_to_int(buf[i+1], &ascii_err);
		j++;
	}
	data[j] = dsi_display_ascii_to_int(buf[i], &ascii_err) << 4;
	data[j] += dsi_display_ascii_to_int(buf[i+1], &ascii_err);
	if (ascii_err == 1) {
		DSI_ERR("Bad Para, ignore this command\n");
		kfree(data);
		goto error;
	}

	set = &panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_PANEL_COMMAND];

	rc = dsi_panel_update_cmd_sets_sub(set, DSI_CMD_SET_PANEL_COMMAND, data, length);
	if (rc)
		DSI_ERR("Failed to update_cmd_sets_sub, rc=%d\n", rc);
	kfree(data);

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
			dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_send_dsi_panel_command(panel);
	if (rc)
		DSI_ERR("Failed to send dsi panel command\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
			dsi_display->name, rc);
		goto error;
	}

error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}

int dsi_display_get_dsi_panel_command(struct drm_connector *connector, char *buf)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;
	struct dsi_panel_cmd_set *cmd;
	int i = 0;
	int count = 0;

	if ((connector == NULL) || (connector->encoder == NULL)
			|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	cmd = &dsi_display->panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_PANEL_COMMAND];

	for (i = 0; i < cmd->count; i++)
		count += dsi_display_get_mipi_dsi_msg(&cmd->cmds[i].msg, &buf[count]);

	return count;
}

int dsi_display_update_dsi_seed_command(struct drm_connector *connector, const char *buf, size_t count)
{
	int i = 0;
	int j = 0;
	int ascii_err = 0;
	unsigned int length;
	char *data;
	struct dsi_panel_cmd_set *set;

	struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

	if ((connector == NULL) || (connector->encoder == NULL)
			|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	if ((strcmp(panel->name, "samsung dsc cmd mode oneplus dsi panel") != 0) &&
		(strcmp(panel->name, "samsung sofef03f_m fhd cmd mode dsc dsi panel") != 0) &&
		(strcmp(panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") != 0) &&
		(strcmp(panel->name, "samsung amb655x fhd cmd mode dsc dsi panel") != 0) &&
		(strcmp(panel->name, "samsung ana6706 dsc cmd mode panel") != 0)) {
		return 0;
	}

	mutex_lock(&panel->panel_lock);

	if (!dsi_panel_initialized(panel))
		goto error;

	length = count / 3;
	if (length != 0x16) {
		DSI_ERR("Insufficient parameters!\n");
		goto error;
	}
	data = kzalloc(length + 1, GFP_KERNEL);

	for (i = 0; (buf[i+2] != 10) && (j < length); i = i+3) {
		data[j] = dsi_display_ascii_to_int(buf[i], &ascii_err) << 4;
		data[j] += dsi_display_ascii_to_int(buf[i+1], &ascii_err);
		j++;
	}
	data[j] = dsi_display_ascii_to_int(buf[i], &ascii_err) << 4;
	data[j] += dsi_display_ascii_to_int(buf[i+1], &ascii_err);
	if (ascii_err == 1) {
		DSI_ERR("Bad Para, ignore this command\n");
		kfree(data);
		goto error;
	}

	set = &panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_SEED_COMMAND];

	if (strcmp(panel->name, "samsung dsc cmd mode oneplus dsi panel") == 0)
		data[0] = WU_SEED_REGISTER;
	if ((strcmp(panel->name, "samsung sofef03f_m fhd cmd mode dsc dsi panel") == 0) ||
	(strcmp(panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) ||
	(strcmp(panel->name, "samsung ana6706 dsc cmd mode panel") == 0))
		data[0] = UG_SEED_REGISTER;

	rc = dsi_panel_update_dsi_seed_command(set->cmds, DSI_CMD_SET_SEED_COMMAND, data);
	if (rc)
		DSI_ERR("Failed to dsi_panel_update_dsi_seed_command, rc=%d\n", rc);
	kfree(data);

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
			dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_send_dsi_seed_command(panel);
	if (rc)
		DSI_ERR("Failed to send dsi seed command\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
			dsi_display->name, rc);
		goto error;
	}

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_display_get_dsi_seed_command(struct drm_connector *connector, char *buf)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;
	struct dsi_panel_cmd_set *cmd;
	int i = 0;
	int count = 0;

	if ((connector == NULL) || (connector->encoder == NULL)
			|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	if ((strcmp(dsi_display->panel->name, "samsung dsc cmd mode oneplus dsi panel") != 0) &&
		(strcmp(dsi_display->panel->name, "samsung sofef03f_m fhd cmd mode dsc dsi panel") != 0) &&
		(strcmp(dsi_display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") != 0) &&
		(strcmp(dsi_display->panel->name, "samsung amb655x fhd cmd mode dsc dsi panel") != 0) &&
		(strcmp(dsi_display->panel->name, "samsung ana6706 dsc cmd mode panel") != 0)) {
		return 0;
	}

	cmd = &dsi_display->panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_SEED_COMMAND];

	for (i = 0; i < cmd->count; i++) {
		count += dsi_display_get_mipi_dsi_msg(&cmd->cmds[i].msg, &buf[count]);
	}

	return count;
}

int dsi_display_get_reg_value(struct dsi_display *dsi_display, struct dsi_panel *panel)
{
	int rc = 0;
	int flags = 0;
	int i = 0;
	int retry_times = 0;
	int count = 0;
	struct dsi_cmd_desc *cmds;
	struct dsi_display_mode *mode;
	struct dsi_display_ctrl *m_ctrl;

	DSI_ERR("start\n");

	m_ctrl = &dsi_display->ctrl[dsi_display->cmd_master_idx];
	if (!panel || !m_ctrl)
		return -EINVAL;

	rc = dsi_display_cmd_engine_enable(dsi_display);
	if (rc) {
		DSI_ERR("cmd engine enable failed\n");
		return -EINVAL;
	}

	dsi_panel_acquire_panel_lock(panel);
	mode = panel->cur_mode;

	count = mode->priv_info->cmd_sets[DSI_CMD_SET_LEVEL2_KEY_ENABLE].count;
	if (!count) {
		DSI_ERR("This panel does not support level2 key enable command\n");
	} else {
		dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_ENABLE);
	}
#if defined(CONFIG_PXLW_IRIS)
	if (iris_is_chip_supported() && iris_is_pt_mode(panel)) {
		cmds = mode->priv_info->cmd_sets[DSI_CMD_SET_PANEL_COMMAND].cmds;
		rc = iris_panel_ctrl_read_reg(m_ctrl, panel, reg_read_value, reg_read_len, cmds);
		if (rc <= 0)
			DSI_ERR("iris_panel_ctrl_read_reg failed, rc=%d\n", rc);
	} else {
#else
	{
#endif
		cmds = mode->priv_info->cmd_sets[DSI_CMD_SET_PANEL_COMMAND].cmds;
		if (cmds->last_command) {
			cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
			flags |= DSI_CTRL_CMD_LAST_COMMAND;
		}
		flags |= (DSI_CTRL_CMD_FETCH_MEMORY | DSI_CTRL_CMD_READ);
		if (!m_ctrl->ctrl->vaddr)
			goto error;
		cmds->msg.rx_buf = reg_read_value;
		cmds->msg.rx_len = reg_read_len;
		do {
			rc = dsi_ctrl_cmd_transfer(m_ctrl->ctrl, &cmds->msg, &flags);
			retry_times++;
		} while ((rc <= 0) && (retry_times < 3));
		if (rc <= 0) {
			DSI_ERR("rx cmd transfer failed rc=%d\n", rc);
			goto error;
		}
	}

	count = mode->priv_info->cmd_sets[DSI_CMD_SET_LEVEL2_KEY_DISABLE].count;
	if (!count) {
		DSI_ERR("This panel does not support level2 key disable command\n");
	} else {
		dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_DISABLE);
	}

	for (i = 0; i < reg_read_len; i++)
		DSI_ERR("reg_read_value[%d] = %d\n", i, reg_read_value[i]);

error:
	dsi_panel_release_panel_lock(panel);
	dsi_display_cmd_engine_disable(dsi_display);
	DSI_ERR("end\n", __func__);
	return rc;
}

int dsi_display_reg_read(struct drm_connector *connector, const char *buf, size_t count)
{
	int i = 0;
	int j = 0;
	int ascii_err = 0;
	unsigned int length;
	char *data;
	struct dsi_panel_cmd_set *set;

	struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

	if ((connector == NULL) || (connector->encoder == NULL)
			|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	if ((strcmp(panel->name, "samsung dsc cmd mode oneplus dsi panel") != 0) &&
		(strcmp(panel->name, "samsung sofef03f_m fhd cmd mode dsc dsi panel") != 0) &&
			(strcmp(panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") != 0) &&
		(strcmp(panel->name, "samsung amb655x fhd cmd mode dsc dsi panel") != 0) &&
				(strcmp(panel->name, "samsung ana6706 dsc cmd mode panel") != 0)) {
		return 0;
	}

	mutex_lock(&dsi_display->display_lock);

	if (!dsi_panel_initialized(panel))
		goto error;

	length = count / 3;
	data = kzalloc(length + 1, GFP_KERNEL);

	for (i = 0; (buf[i+2] != 10) && (j < length); i = i+3) {
		data[j] = dsi_display_ascii_to_int(buf[i], &ascii_err) << 4;
		data[j] += dsi_display_ascii_to_int(buf[i+1], &ascii_err);
		j++;
	}
	data[j] = dsi_display_ascii_to_int(buf[i], &ascii_err) << 4;
	data[j] += dsi_display_ascii_to_int(buf[i+1], &ascii_err);
	if (ascii_err == 1) {
		DSI_ERR("Bad Para, ignore this command\n");
		kfree(data);
		goto error;
	}

	set = &panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_PANEL_COMMAND];

	rc = dsi_panel_update_cmd_sets_sub(set, DSI_CMD_SET_PANEL_COMMAND, data, length);
	if (rc)
		DSI_ERR("Failed to update_cmd_sets_sub, rc=%d\n", rc);
	kfree(data);

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI clocks, rc=%d\n",
			dsi_display->name, rc);
		goto error;
	}

	rc = dsi_display_get_reg_value(dsi_display, panel);
	if (rc <= 0)
		DSI_ERR("Failed to get reg value\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI clocks, rc=%d\n",
			dsi_display->name, rc);
		goto error;
	}

error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}

int dsi_display_get_reg_read_command_and_value(struct drm_connector *connector, char *buf)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;
	struct dsi_panel_cmd_set *cmd;
	int i = 0;
	int count = 0;

	if ((connector == NULL) || (connector->encoder == NULL)
			|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	cmd = &dsi_display->panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_PANEL_COMMAND];

	for (i = 0; i < cmd->count; i++)
		count += dsi_display_get_mipi_dsi_msg(&cmd->cmds[i].msg, &buf[count]);

	count += snprintf(&buf[count], PAGE_SIZE - count, "Reg value:");
	for (i = 0; i < reg_read_len; i++)
		count += snprintf(&buf[count], PAGE_SIZE - count, "%02X ", reg_read_value[i]);
	count += snprintf(&buf[count], PAGE_SIZE - count, "\n");

	return count;
}

int dsi_display_panel_mismatch_check(struct drm_connector *connector)
{
    struct dsi_display_mode *mode;
	struct dsi_panel *panel = NULL;
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;
	char buf[32];
	int panel_id;
	u32 count;
	int rc = 0;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;
	mutex_lock(&dsi_display->display_lock);

    if (!dsi_panel_initialized(panel) || !panel->cur_mode) {
        panel->panel_mismatch = 0;
		goto error;
	}

	if (!panel->panel_mismatch_check) {
	    panel->panel_mismatch = 0;
	    DSI_ERR("This hw not support panel mismatch check(dvt-mp)\n");
		goto error;
	}

    mode = panel->cur_mode;
	count = mode->priv_info->cmd_sets[DSI_CMD_SET_PANEL_ID].count;
    if (count) {
        rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
                DSI_ALL_CLKS, DSI_CLK_ON);
        if (rc) {
            DSI_ERR("[%s] failed to enable DSI clocks, rc=%d\n",
                dsi_display->name, rc);
            goto error;
        }

        memset(buf, 0, sizeof(buf));
        dsi_display_read_panel_id(dsi_display, panel, buf, 1);

        panel_id = buf[0];
        panel->panel_mismatch = (panel_id == 0x03)? 1 : 0;

        rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
            DSI_ALL_CLKS, DSI_CLK_OFF);
        if (rc) {
            DSI_ERR("[%s] failed to enable DSI clocks, rc=%d\n",
                dsi_display->name, rc);
            goto error;
        }
    } else{
        panel->panel_mismatch = 0;
        DSI_ERR("This panel not support panel mismatch check.\n");
    }
error:
	mutex_unlock(&dsi_display->display_lock);
	return 0;
}

int dsi_display_panel_mismatch(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

    if ((connector == NULL) || (connector->encoder == NULL)
            || (connector->encoder->bridge == NULL))
        return 0;

    c_bridge =  to_dsi_bridge(connector->encoder->bridge);
    dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	return dsi_display->panel->panel_mismatch;
}

int dsi_display_unprepare(struct dsi_display *display)
{
	int rc = 0, i;
	struct dsi_display_ctrl *ctrl;

	if (!display) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	SDE_EVT32(SDE_EVTLOG_FUNC_ENTRY);
	mutex_lock(&display->display_lock);

	rc = dsi_display_wake_up(display);
	if (rc)
		DSI_ERR("[%s] display wake up failed, rc=%d\n",
		       display->name, rc);
	if (!display->poms_pending) {
		rc = dsi_panel_unprepare(display->panel);
		if (rc)
			DSI_ERR("[%s] panel unprepare failed, rc=%d\n",
			       display->name, rc);
	}

		/* Remove additional vote added for pre_mode_switch_to_cmd */
	if (display->poms_pending &&
			display->config.panel_mode == DSI_OP_VIDEO_MODE) {
		display_for_each_ctrl(i, display) {
			ctrl = &display->ctrl[i];
			if (!ctrl->ctrl || !ctrl->ctrl->dma_wait_queued)
				continue;
			flush_workqueue(display->dma_cmd_workq);
			cancel_work_sync(&ctrl->ctrl->dma_cmd_wait);
			ctrl->ctrl->dma_wait_queued = false;
		}

		dsi_display_cmd_engine_disable(display);
		dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_ALL_CLKS, DSI_CLK_OFF);
	}

	rc = dsi_display_ctrl_host_disable(display);
	if (rc)
		DSI_ERR("[%s] failed to disable DSI host, rc=%d\n",
		       display->name, rc);

	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_LINK_CLK, DSI_CLK_OFF);
	if (rc)
		DSI_ERR("[%s] failed to disable Link clocks, rc=%d\n",
		       display->name, rc);

	rc = dsi_display_ctrl_deinit(display);
	if (rc)
		DSI_ERR("[%s] failed to deinit controller, rc=%d\n",
		       display->name, rc);

	if (!display->panel->ulps_suspend_enabled) {
		rc = dsi_display_phy_disable(display);
		if (rc)
			DSI_ERR("[%s] failed to disable DSI PHY, rc=%d\n",
			       display->name, rc);
	}

	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc)
		DSI_ERR("[%s] failed to disable DSI clocks, rc=%d\n",
		       display->name, rc);

	/* destrory dsi isr set up */
	dsi_display_ctrl_isr_configure(display, false);

	if (!display->poms_pending) {
		rc = dsi_panel_post_unprepare(display->panel);
		if (rc)
			DSI_ERR("[%s] panel post-unprepare failed, rc=%d\n",
			       display->name, rc);
	}

	mutex_unlock(&display->display_lock);

	/* Free up DSI ERROR event callback */
	dsi_display_unregister_error_handler(display);

	SDE_EVT32(SDE_EVTLOG_FUNC_EXIT);
	return rc;
}

//*mark.yao@PSW.MM.Display.LCD.Stability,2018/4/28,add for support aod,hbm,seed*/
struct dsi_display *get_main_display(void) {
		return primary_display;
}
EXPORT_SYMBOL(get_main_display);

static int __init dsi_display_register(void)
{
	dsi_phy_drv_register();
	dsi_ctrl_drv_register();

	dsi_display_parse_boot_display_selection();

	return platform_driver_register(&dsi_display_driver);
}

static void __exit dsi_display_unregister(void)
{
	platform_driver_unregister(&dsi_display_driver);
	dsi_ctrl_drv_unregister();
	dsi_phy_drv_unregister();
}
module_param_string(dsi_display0, dsi_display_primary, MAX_CMDLINE_PARAM_LEN,
								0600);
MODULE_PARM_DESC(dsi_display0,
	"msm_drm.dsi_display0=<display node>:<configX> where <display node> is 'primary dsi display node name' and <configX> where x represents index in the topology list");
module_param_string(dsi_display1, dsi_display_secondary, MAX_CMDLINE_PARAM_LEN,
								0600);
MODULE_PARM_DESC(dsi_display1,
	"msm_drm.dsi_display1=<display node>:<configX> where <display node> is 'secondary dsi display node name' and <configX> where x represents index in the topology list");
module_init(dsi_display_register);
module_exit(dsi_display_unregister);

