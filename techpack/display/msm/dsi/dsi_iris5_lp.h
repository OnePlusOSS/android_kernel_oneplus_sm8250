#ifndef _DSI_IRIS5_LP_H_
#define _DSI_IRIS5_LP_H_
#include "dsi_iris5_i3c.h"

/*
 * These files include source code for the Licensed Software that is provided under the terms and
 * conditions of a Master Software License Agreement and its applicable schedules.
 */

#define POR_CLOCK 180	/* 0.1 Mhz*/

/* option IDs */
#define ID_SYS_PMU_CTRL 0xf0
#define ID_SYS_ULPS	0xf1
#define ID_SYS_MEM_REPAIR 0x07

#define ID_SYS_ENTER_ABYP 0x0104
#define ID_SYS_EXIT_ABYP 0x0105

#define ID_RX_ENTER_TTL 0xD0
#define ID_RX_EXIT_TTL 0xD1

/* regs */
#define REG_ADDR_PMU_CTRL 0xf0000060
#define REG_ADDR_PMU_STATUS 0xf0000094
#define IRIS_RUN_STATUS 0xf1240030
#define IRIS_REG_UPDATE 0xf1250000
#define DISP_CMD_SHAWDOW_EN_MASK  0x00000040
#define DISP_CMD_SHAWDOW_EN_SHIFT 6

#define IRIS_REG_INTSTAT_RAW 0xf189ffe4
#define TXFALSE_CONTROL_MASK  0x00040000
#define PQ_SWITCH_MASK BIT(3)

enum iris_onewired_cmd {
	POWER_UP_SYS = 1,
	ENTER_ANALOG_BYPASS = 2,
	EXIT_ANALOG_BYPASS = 3,
	POWER_DOWN_SYS = 4,
	RESET_SYS = 5,
	FORCE_ENTER_ANALOG_BYPASS = 6,
	FORCE_EXIT_ANALOG_BYPASS = 7,
	POWER_UP_MIPI = 8,
	POWER_DOWN_MIPI = 9,
};

enum iris_pmu_domain {
	MIPI_PWR = (0x1 << 2),
	MIPI2_PWR = (0x1 << 3),
	BSRAM_PWR = (0x1 << 4),
	FRC_PWR = (0x1 << 5),
	PQ_PWR = (0x1 << 6),
	DSCU_PWR = (0x1 << 7),
	DDSC_PWR = (0x1 << 8),
	LCE_PWR = (0x1 << 9),
};

enum iris_ulps_sel {
	ULPS_NONE = 0x0,
	ULPS_MAIN = 0x1,
	ULPS_AUX = 0x2,
	ULPS_MAIN_AUX = 0x3,
};

enum iris_abyp_switch_state {
	PASS_THROUGH_STATE = 0,
	POWER_UP_STATE,
	TTL_CMD_BYPASS_STATE,
	ANALOG_BYPASS_ENTER_STATE,
	ANALOG_BYPASS_CHECK_STATE,
	ANALOG_BYPASS_EXIT_STATE,
	CONFIG_MIPI_STATE,
	CONFIG_DMA_STATE,
	EXIT_TTL_STATE,
};

enum iris_abyp_lp_mode {
	ABYP_POWER_DOWN_SYS = 1,
	ABYP_POWER_DOWN_MIPI = 2,
	ABYP_POWER_DOWN_PLL = 3,
};

/* init iris low power*/
void iris_lp_preinit(void);
void iris_lp_init(void);

/* dynamic power gating set */
void iris_dynamic_power_set(bool enable);

/* dynamic power gating get */
bool iris_dynamic_power_get(void);

/* power on & off mipi2 domain */
int iris_pmu_mipi2_set(bool on);

/* power on & off bulksram domain */
int iris_pmu_bsram_set(bool on);

/* power on & off frc domain */
int iris_pmu_frc_set(bool on);

/* power on & off dsc unit domain */
int iris_pmu_dscu_set(bool on);

/* power on & off lce domain */
int iris_pmu_lce_set(bool on);

/* lce dynamic pmu mask enable */
void iris_lce_dynamic_pmu_mask_set(bool enable);

/* send one wired commands via GPIO */
void iris_one_wired_cmd_send(struct dsi_panel *panel, int cmd);

int iris_one_wired_cmd_init(struct dsi_panel *panel);

/* Switch PT and Bypass mode */
bool iris_abypass_switch_proc(struct dsi_display *display, int mode, bool pending, bool first);

void iris_lce_power_status_set(bool enable);

bool iris_lce_power_status_get(void);

/* trigger DMA to load */
void iris_dma_trigger_load(void);

void iris_ulps_source_sel(enum iris_ulps_sel ulps_sel);

bool iris_ulps_enable_get(void);

int iris_lp_debugfs_init(struct dsi_display *display);

void iris_sde_encoder_rc_lock(void);

void iris_sde_encoder_rc_unlock(void);

bool iris_fast_cmd_abyp_exit(void);

void iris_lp_setting_off(void);

/* Get Iris lightup opt */
int iris_lightup_opt_get(void);

int iris_exit_abyp(bool one_wired);

void iris_video_abyp_enter(void);

void iris_video_abyp_exit(void);

/* get frc domain power state */
bool iris_pmu_frc_get(void);
/* get bsram domain power state */
bool iris_pmu_bsram_get(void);
/* Iris abyp lp */
void iris_abyp_lp(int mode); //CID90440
/* Iris power up MIPI */
void iris_power_up_mipi(void);
/* Iris reset MIPI domain */
void iris_reset_mipi(void);

static inline bool iris_disable_ulps(uint8_t path)
{
	bool is_ulps_enable = 0;

	if (path == PATH_I2C) {
		is_ulps_enable = iris_ulps_enable_get();
		if (is_ulps_enable)
			iris_ulps_source_sel(ULPS_NONE);
		iris_i3c_status_set(true);
	}

	return is_ulps_enable;
}

static inline void iris_enable_ulps(uint8_t path, bool is_ulps_enable)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	if (path == PATH_I2C) {
		iris_i3c_status_set(false);
		if (is_ulps_enable) {
			if (pcfg->pwil_mode == PT_MODE) {
				if (iris_pmu_frc_get() == false) {
					if (iris_pmu_bsram_get() == false)
						iris_ulps_source_sel(ULPS_MAIN);
				}
			} else if (iris_pmu_frc_get() == false) {
				if (pcfg->osd_enable == false)
					iris_ulps_source_sel(ULPS_MAIN);
			}
		}
	}
}

#endif // _DSI_IRIS5_LP_H_
