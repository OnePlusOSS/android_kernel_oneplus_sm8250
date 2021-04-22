// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#include <drm/drm_bridge.h>
#include <drm/drm_encoder.h>
#include "dsi_drm.h"
#include <sde_encoder.h>
#include <sde_encoder_phys.h>
#include <sde_trace.h>
#include "dsi_iris5_api.h"
#include "dsi_iris5_lightup.h"
#include "dsi_iris5_lightup_ocp.h"
#include "dsi_iris5_mode_switch.h"
#include "dsi_iris5_lp.h"
#include "dsi_iris5_pq.h"
#include "dsi_iris5_gpio.h"
#include "dsi_iris5_timing_switch.h"
#include "dsi_iris5_log.h"

#define DEBUG_READ_PMU
#define DEFAULT_ABYP_LP_MODE ABYP_POWER_DOWN_PLL

static int debug_lp_opt;
extern uint8_t iris_pq_update_path;

/* abyp light up option (need panel off/on to take effect)
 * bit[0]: 0 -- light up with PT, 1 -- light up with ABYP
 * bit[1]: 0 -- efuse mode is ABYP, 1 -- efuse mode is PT
 * bit[2]: 0 -- use mipi command to switch, 1 -- use GPIO to switch
 * bit[3]: 0 -- non force, 1 -- force abyp during panel switch
 */
static int debug_on_opt;

static bool iris_lce_power;

static bool iris_bsram_power; /* BSRAM domain power status */

#define IRIS_TRACE_FPS       0x01
#define IRIS_TRACE_CADENCE   0X02
static int debug_trace_opt;
static int debug_abyp_gpio_status = -1;

int32_t iris_parse_lp_ctrl(struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t rc = 0;
	u8 vals[3];

	rc = of_property_read_u8_array(np, "pxlw,low-power", vals, 3);
	if (rc) {
		IRIS_LOGE("%s(), failed to find low power property, return: %d",
				__func__, rc);
		return 0;
	}

	pcfg->lp_ctrl.dynamic_power = (bool)vals[0];
	pcfg->lp_ctrl.ulps_lp = (bool)vals[1];
	pcfg->lp_ctrl.abyp_enable = (bool)vals[2];
	IRIS_LOGI("%s(), parse low power info: %d %d %d",
			__func__, vals[0], vals[1], vals[2]);

	return rc;
}

void iris_lp_preinit(void)
{
	struct iris_cfg *pcfg;

	pcfg = iris_get_cfg();
	if (iris_virtual_display(pcfg->display) || pcfg->valid < PARAM_PARSED)
		return;

	if (debug_on_opt & 0x1)
		pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE;
	IRIS_LOGI("%s:%d, pcfg->abypss_ctrl.abypass_mode = %d", __func__, __LINE__, pcfg->abypss_ctrl.abypass_mode);

	iris_init_one_wired();
}

/* clear some pmu domains */
static void iris_clear_pmu(void)
{
	struct iris_update_regval regval;
	struct iris_cfg *pcfg;

	pcfg = iris_get_cfg();

	iris_bsram_power = false;

	regval.ip = IRIS_IP_SYS;
	regval.opt_id = ID_SYS_PMU_CTRL;
	regval.mask = 0x000000b8; /*clear MIPI2, BSRAM, FRC, DSCU */
	regval.value = 0x0;

	iris_update_bitmask_regval_nonread(&regval, true);
}

/* init iris low power */
void iris_lp_init(void)
{
	struct iris_cfg *pcfg;

	pcfg = iris_get_cfg();
	if (iris_virtual_display(pcfg->display) || pcfg->valid < PARAM_PARSED)
		return;

	IRIS_LOGI("lp dynamic_power:%d, ulps_lp:%d, abyp_lp_enable:%d",
			pcfg->lp_ctrl.dynamic_power, pcfg->lp_ctrl.ulps_lp,
			pcfg->lp_ctrl.abyp_enable);

	if (pcfg->lp_ctrl.dynamic_power) {
		IRIS_LOGD(" [%s, %d] open psr_mif osd first address eco.", __func__, __LINE__);
		iris_psf_mif_dyn_addr_set(true);
		iris_dynamic_power_set(true);
	} else {
		IRIS_LOGD(" [%s, %d] close psr_mif osd first address eco.", __func__, __LINE__);
		iris_psf_mif_dyn_addr_set(false);
	}

	iris_clear_pmu();

	if (pcfg->lp_ctrl.ulps_lp)
		iris_ulps_source_sel(ULPS_MAIN);
	else
		iris_ulps_source_sel(ULPS_NONE);
}

/*== PMU related APIs ==*/

/* dynamic power gating set */
void iris_dynamic_power_set(bool enable)
{
	struct iris_update_regval regval;
	struct iris_cfg *pcfg;

	pcfg = iris_get_cfg();

	regval.ip = IRIS_IP_SYS;
	regval.opt_id = ID_SYS_PMU_CTRL;
	regval.mask = 0x00000001;
	regval.value = (enable ? 0x1 : 0x0);

	if (enable) {
		/* 0xf0: read; 0xf1: non-read */
		iris_send_ipopt_cmds(IRIS_IP_DMA, 0xf0);

		iris_update_bitmask_regval_nonread(&regval, true);
	} else {
		iris_update_bitmask_regval_nonread(&regval, true);

		/* delay for disabling dynamic power gating take effect */
		usleep_range(1000 * 20, 1000 * 20 + 1);
		/* 0xf0: read; 0xf1: non-read */
		iris_send_ipopt_cmds(IRIS_IP_DMA, 0xf1);
	}

	pcfg->lp_ctrl.dynamic_power = enable;
	IRIS_LOGE("%s: %d", __func__, enable);
}

/* dynamic power gating get */
bool iris_dynamic_power_get(void)
{
	struct iris_cfg *pcfg;

	pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	return pcfg->lp_ctrl.dynamic_power;
}

static int iris_pmu_power_set(enum iris_pmu_domain domain_id, bool on)
{
	struct iris_update_regval regval;
	struct iris_update_ipopt popt;
#ifdef DEBUG_READ_PMU
	uint32_t set_pmu_ctrl, pmu_ctrl;
	uint32_t  *payload = NULL;
	uint32_t reg_pmu_ctrl, top_pmu_status, pmu_status;
	int i;
#endif
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	regval.ip = IRIS_IP_SYS;
	regval.opt_id = ID_SYS_PMU_CTRL;
	regval.mask = domain_id;
	regval.value = (on ? domain_id : 0x0);
	iris_update_bitmask_regval_nonread(&regval, false);
	iris_init_update_ipopt(&popt, IRIS_IP_SYS, regval.opt_id, regval.opt_id, 0);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(&popt, 1, path);
	iris_enable_ulps(path, is_ulps_enable);

#ifdef DEBUG_READ_PMU
	if ((debug_lp_opt & 0x100) == 0x100) {
		payload = iris_get_ipopt_payload_data(IRIS_IP_SYS, ID_SYS_PMU_CTRL, 2);
		set_pmu_ctrl = payload[0];

		reg_pmu_ctrl = iris_ocp_read(REG_ADDR_PMU_CTRL, DSI_CMD_SET_STATE_HS);

		if (reg_pmu_ctrl != set_pmu_ctrl) {
			IRIS_LOGE("Err: read pmu ctrl 0x%08x != set_pmu_ctrl 0x%08x", reg_pmu_ctrl, set_pmu_ctrl);
			return 2;
		}
		pmu_ctrl = (reg_pmu_ctrl >> 2) & 0xff;

		for (i = 0; i < 10; i++) {
			top_pmu_status = iris_ocp_read(REG_ADDR_PMU_STATUS, DSI_CMD_SET_STATE_HS);
			pmu_status = ((top_pmu_status>>8)&0x3) + (((top_pmu_status>>15)&0x1)<<2) +
				(((top_pmu_status>>11)&0x1)<<3) + (((top_pmu_status>>10)&0x1)<<4) +
				(((top_pmu_status>>12)&0x7)<<5);
			IRIS_LOGI("read pmu ctrl 0x%08x top_pmu_status 0x%08x, pmu_status 0x%02x",
					reg_pmu_ctrl, top_pmu_status, pmu_status);

			if (pmu_status == pmu_ctrl)
				break;

			IRIS_LOGE("Err %d: pmu_status: 0x%08x != pmu_ctrl 0x%02x", i, pmu_status, pmu_ctrl);
			usleep_range(1000 * 10, 1000 * 10 + 1);
		}
		if (i == 10) {
			IRIS_LOGE("Err: return!");
			return 3;
		}
	}
#endif

	return 0;
}

static bool iris_pmu_power_get(enum iris_pmu_domain domain_id)
{
	uint32_t pmu_ctrl;
	uint32_t  *payload = NULL;

	payload = iris_get_ipopt_payload_data(IRIS_IP_SYS, ID_SYS_PMU_CTRL, 2);
	pmu_ctrl = payload[0];
	return ((pmu_ctrl & domain_id) != 0);
}

void iris_video_abyp_power(bool on)
{
	struct iris_update_regval regval;
	struct iris_update_ipopt popt;
	struct iris_cfg *pcfg;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	pcfg = iris_get_cfg();

	regval.ip = IRIS_IP_SYS;
	regval.opt_id = ID_SYS_PMU_CTRL;
	regval.mask = 0x40800003;
	if (on)
		regval.value = (pcfg->lp_ctrl.dynamic_power ? 0x3 : 0x2);
	else
		regval.value = 0x40800000; /*MIPI0_AUTO_DMA_EN, CORE_DOMAINS_OFF_BY_MIPI_EN*/

	IRIS_LOGE("%s 0x%x 0x%x", __func__, regval.mask, regval.value);

	iris_update_bitmask_regval_nonread(&regval, false);
	iris_init_update_ipopt(&popt, IRIS_IP_SYS, regval.opt_id, regval.opt_id, 0);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(&popt, 1, path);
	iris_enable_ulps(path, is_ulps_enable);
}

/* power on & off mipi2 domain */
int iris_pmu_mipi2_set(bool on)
{
	int rt = 0;

	if (((debug_lp_opt & 0x1) == 0x1) && !on) {
		IRIS_LOGI("%s: not power down!", __func__);
		return 0;
	}
	rt = iris_pmu_power_set(MIPI2_PWR, on);
	IRIS_LOGI("%s: on - %d, rt - %d", __func__, on, rt);
	return rt;
}

/* power on & off bulksram domain */
int iris_pmu_bsram_set(bool on)
{
	int rt = 0;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;
	int i = 0;

	if (((debug_lp_opt & 0x2) == 0x2) && !on) {
		IRIS_LOGI("%s: not power down!", __func__);
		return 0;
	}
	if (on != iris_bsram_power) {
		struct iris_update_regval regval;
		struct iris_update_ipopt popt;

		rt = iris_pmu_power_set(BSRAM_PWR, on);
		iris_bsram_power = on;

		regval.ip = IRIS_IP_SYS;
		regval.opt_id = ID_SYS_MEM_REPAIR;
		regval.mask = 0x330000;
		regval.value = (on ? 0x330000 : 0x0);
		iris_update_bitmask_regval_nonread(&regval, false);
		iris_init_update_ipopt(&popt, IRIS_IP_SYS, regval.opt_id, regval.opt_id, 0);
		is_ulps_enable = iris_disable_ulps(path);
		iris_update_pq_opt(&popt, 1, path);
		iris_enable_ulps(path, is_ulps_enable);
		if(on){
                    udelay(100);
                    for(i = 0; i < 10; i++)
                        iris_pmu_power_set(BSRAM_PWR,on);
	       }
	} else {
		IRIS_LOGW("%s: cur %d == on %d", __func__, iris_bsram_power, on);
		return 2;
	}
	IRIS_LOGI("%s: on - %d, rt - %d", __func__, on, rt);
	return rt;
}

bool iris_pmu_bsram_get(void)
{
	return iris_bsram_power;
}

/* power on & off frc domain */
int iris_pmu_frc_set(bool on)
{
	int rt = 0;

	if (((debug_lp_opt & 0x4) == 0x4) && !on) {
		IRIS_LOGI("%s: not power down!", __func__);
		return 0;
	}
	rt = iris_pmu_power_set(FRC_PWR, on);
	IRIS_LOGI("%s: on - %d, rt - %d", __func__, on, rt);
	return rt;
}

bool iris_pmu_frc_get(void)
{
	return iris_pmu_power_get(FRC_PWR);
}

/* power on & off dsc unit domain */
int iris_pmu_dscu_set(bool on)
{
	int rt = 0;

	if (((debug_lp_opt & 0x8) == 0x8) && !on) {
		IRIS_LOGI("%s: not power down!", __func__);
		return 0;
	}
	rt = iris_pmu_power_set(DSCU_PWR, on);
	IRIS_LOGI("%s: on - %d, rt - %d", __func__, on, rt);
	return rt;
}

/* power on & off lce domain */
int iris_pmu_lce_set(bool on)
{
	int rt = 0;

	if (((debug_lp_opt & 0x10) == 0x10) && !on) {
		IRIS_LOGI("%s: not power down!", __func__);
		return 0;
	}
	rt = iris_pmu_power_set(LCE_PWR, on);
	iris_lce_power_status_set(on);

	IRIS_LOGI("%s: on - %d, rt - %d", __func__, on, rt);
	return rt;
}

/* lce dynamic pmu mask enable */
void iris_lce_dynamic_pmu_mask_set(bool enable)
{
	//TODO: compile error
	//enable = enable;
	enable = false;
}

void iris_lce_power_status_set(bool enable)
{
	iris_lce_power = enable;

	IRIS_LOGI("%s: %d", __func__, enable);
}

bool iris_lce_power_status_get(void)
{
	return iris_lce_power;
}

/* trigger DMA to load */
void iris_dma_trigger_load(void)
{
	struct iris_cfg *pcfg;

	pcfg = iris_get_cfg();
	/* only effective when dynamic power gating off */
	if (!pcfg->lp_ctrl.dynamic_power) {
		if (iris_lce_power_status_get())
			iris_send_ipopt_cmds(IRIS_IP_DMA, 0xe1);
		else
			iris_send_ipopt_cmds(IRIS_IP_DMA, 0xe5);
	}
}

void iris_ulps_source_sel(enum iris_ulps_sel ulps_sel)
{
	struct iris_update_regval regval;
	struct iris_update_ipopt popt;
	struct iris_cfg *pcfg;

	pcfg = iris_get_cfg();

	if ((debug_lp_opt & 0x200) == 0x200) {
		IRIS_LOGE("not set ulps source sel: %d", ulps_sel);
		return;
	}

	regval.ip = IRIS_IP_SYS;
	regval.opt_id = 0xf1;
	regval.mask = 0x3;
	regval.value = ulps_sel;
	iris_update_bitmask_regval_nonread(&regval, false);
	iris_init_update_ipopt(&popt, IRIS_IP_SYS, regval.opt_id, regval.opt_id, 0);
	iris_update_pq_opt(&popt, 1, PATH_DSI);
	IRIS_LOGD("ulps source sel: %d", ulps_sel);

	if (ulps_sel == ULPS_NONE)
		pcfg->lp_ctrl.ulps_lp = false;
	else
		pcfg->lp_ctrl.ulps_lp = true;
}

bool iris_ulps_enable_get(void)
{
	struct iris_cfg *pcfg;

	pcfg = iris_get_cfg();

	IRIS_LOGI("ulps ap:%d, iris:%d",
			pcfg->display->panel->ulps_feature_enabled, pcfg->lp_ctrl.ulps_lp);

	if (pcfg->display->panel->ulps_feature_enabled && pcfg->lp_ctrl.ulps_lp)
		return true;
	else
		return false;
}

/* TE delay or EVS delay select.
 * 0: TE delay; 1: EVS delay
 */
void iris_te_select(int sel)
{
	struct iris_update_ipopt popt[IP_OPT_MAX];
	int len;
	bool is_ulps_enable = 0;
	uint8_t path = iris_pq_update_path;

	iris_init_ipopt_ip(popt, IP_OPT_MAX);

	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_SYS,
			sel ? 0xe1 : 0xe0, 0x1);
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_TX,
			sel ? 0xe1 : 0xe0, 0x1);
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_DTG,
			sel ? 0xe1 : 0xe0, 0x0);
	is_ulps_enable = iris_disable_ulps(path);
	iris_update_pq_opt(popt, len, path);
	iris_enable_ulps(path, is_ulps_enable);

	IRIS_LOGD("%s: %s", __func__, (sel ? "EVS delay" : "TE delay"));
}

/*== Analog bypass related APIs ==*/


static struct drm_encoder *iris_get_drm_encoder_handle(void)
{
	struct iris_cfg *pcfg;

	pcfg = iris_get_cfg();

	if (pcfg->display->bridge == NULL || pcfg->display->bridge->base.encoder == NULL)
		IRIS_LOGE("Can not get drm encoder");

	return pcfg->display->bridge->base.encoder;
}

void iris_sde_encoder_rc_lock(void)
{
	struct drm_encoder *drm_enc = NULL;

	drm_enc = iris_get_drm_encoder_handle();

	sde_encoder_rc_lock(drm_enc);
}

void iris_sde_encoder_rc_unlock(void)
{
	struct drm_encoder *drm_enc = NULL;

	drm_enc = iris_get_drm_encoder_handle();

	sde_encoder_rc_unlock(drm_enc);
}

/* Switch ABYP by mipi commands
 * enter_abyp: true -- Enter ABYP;
 * false -- Exit ABYP
 */
static void iris_mipi_abyp_switch(bool enter_abyp)
{
	if (enter_abyp) {
		iris_send_ipopt_cmds(IRIS_IP_SYS, 4);
		IRIS_LOGD("%s, Enter ABYP.", __func__);
	} else {
		iris_send_ipopt_cmds(IRIS_IP_SYS, 5);
		IRIS_LOGD("%s, Exit ABYP.", __func__);
	}
}

bool iris_fast_cmd_abyp_enter(void)
{
	struct iris_cfg *pcfg;
	int i;
	int abyp_status_gpio;
	ktime_t ktime0;
	ktime_t ktime1;
	uint32_t timeus;

	pcfg = iris_get_cfg();
	if (debug_lp_opt & 0x400)
		ktime0 = ktime_get();

	IRIS_LOGI("Enter abyp mode start");
	/* HS enter abyp */
	iris_send_ipopt_cmds(IRIS_IP_SYS, 0x8);
	udelay(100);

	/* check abyp gpio status */
	for (i = 0; i < 10; i++) {
		abyp_status_gpio = iris_check_abyp_ready();
		IRIS_LOGD("%s, ABYP status: %d.", __func__, abyp_status_gpio);
		if (abyp_status_gpio == 1) {
			if (debug_lp_opt & 0x400) {
				ktime1 = ktime_get();
				timeus = (u32)ktime_to_us(ktime1) - (u32)ktime_to_us(ktime0);
				ktime0 = ktime1;
				IRIS_LOGI("spend time switch ABYP %d us", timeus);
			}
			//power off domains, switch clocks mux
			iris_send_ipopt_cmds(IRIS_IP_SYS, 0x22);
			//power off PLL, gate clocks
			iris_send_ipopt_cmds(IRIS_IP_SYS, 0x23);
			IRIS_LOGD("ABYP enter LP");
			if (debug_lp_opt & 0x400) {
				ktime1 = ktime_get();
				timeus = (u32)ktime_to_us(ktime1) - (u32)ktime_to_us(ktime0);
				ktime0 = ktime1;
				IRIS_LOGI("spend time ABYP LP %d us", timeus);
			}

			pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE;
			break;
		}
		udelay(3 * 1000);
	}
	if (abyp_status_gpio == 0) {
		IRIS_LOGE("Enter abyp mode Failed!");
		return true;
	}
	IRIS_LOGI("Enter abyp done");
	return false;
}

bool iris_fast_cmd_abyp_exit(void)
{
	struct dsi_display *display = NULL;
	struct iris_cfg *pcfg;
	int abyp_status_gpio;
	int next_fps, next_vres;
	bool high;
	ktime_t ktime0;
	ktime_t ktime1;
	uint32_t timeus;

	pcfg = iris_get_cfg();

	display = pcfg->display;
	next_fps = pcfg->panel->cur_mode->timing.refresh_rate;
	next_vres = pcfg->panel->cur_mode->timing.v_active;
	if ((next_fps == HIGH_FREQ) && (next_vres == FHD_H))
		high = true;
	else
		high = false;

	if (debug_lp_opt & 0x400)
		ktime0 = ktime_get();

	IRIS_LOGI("Exit abyp mode start");
	IRIS_LOGI("cur_fps:%d, cur_vres:%d, next_fps:%d, next_vres:%d, high:%d",
			pcfg->cur_fps_in_iris, pcfg->cur_vres_in_iris, next_fps, next_vres, high);

	iris_send_one_wired_cmd(IRIS_POWER_DOWN_MIPI);
	udelay(3500);
	iris_send_one_wired_cmd(IRIS_POWER_UP_MIPI);
	udelay(3500);
	if (debug_lp_opt & 0x400) {
		ktime1 = ktime_get();
		timeus = (u32)ktime_to_us(ktime1) - (u32)ktime_to_us(ktime0);
		ktime0 = ktime1;
		IRIS_LOGI("spend time MIPI off/on %d us", timeus);
	}

	if (pcfg->iris_initialized) {
		if ((pcfg->cur_fps_in_iris == next_fps) && (pcfg->cur_vres_in_iris == next_vres)) {
			//ungate clocks & power on PLLs
			iris_send_ipopt_cmds(IRIS_IP_SYS, 0x20);
			udelay(100);
			//switch clock mux & power on domains
			iris_send_ipopt_cmds(IRIS_IP_SYS, 0x21);
			//configure MIPI and other domains via DMA
			iris_send_ipopt_cmds(IRIS_IP_DMA, 0xE9);
			udelay(100);
			IRIS_LOGD("configure DMA");
		} else {
			//ungate clocks && re-program PLL
			iris_send_ipopt_cmds(IRIS_IP_SYS, high ? 0x28:0x27);
			udelay(100);
			//switch clock mux & power on domains
			iris_send_ipopt_cmds(IRIS_IP_SYS, 0x21);

			//configure MIPI Rx
			iris_send_ipopt_cmds(IRIS_IP_RX, high ? 0xF2:0xF1);
		}
		if (debug_lp_opt & 0x400) {
			ktime1 = ktime_get();
			timeus = (u32)ktime_to_us(ktime1) - (u32)ktime_to_us(ktime0);
			ktime0 = ktime1;
			IRIS_LOGI("spend time ABP send LP command %d us", timeus);
		}

	} else {
		//ungate clocks, power on MIPI PLL
		iris_send_ipopt_cmds(IRIS_IP_SYS, 0x24);
		//switch clock mux default
		iris_send_ipopt_cmds(IRIS_IP_SYS, 0x25);
		IRIS_LOGD("ABYP exit LP default");
	}

	/* exit abyp */
	abyp_status_gpio = iris_exit_abyp(false);
	if (debug_lp_opt & 0x400) {
		ktime1 = ktime_get();
		timeus = (u32)ktime_to_us(ktime1) - (u32)ktime_to_us(ktime0);
		ktime0 = ktime1;
		IRIS_LOGI("spend time switch to PT %d us", timeus);
	}
	if (abyp_status_gpio == 0) {
		if (pcfg->iris_initialized == false) {
			iris_lightup(pcfg->panel, NULL);
			pcfg->iris_initialized = true;
			IRIS_LOGI("%s, light up iris", __func__);
		} else {
			if (pcfg->cur_vres_in_iris != next_vres) {
				//resolution change (may have fps change)
				iris_send_timing_switch_pkt();
				iris_send_ipopt_cmds(IRIS_IP_DMA, 0xE9);
				udelay(200);
			} else if (pcfg->cur_fps_in_iris != next_fps) {
				//only fps change
				iris_send_ipopt_cmds(IRIS_IP_RX, high ? 0xE1:0xE0);
				iris_set_out_frame_rate(next_fps);
				iris_send_ipopt_cmds(IRIS_IP_DMA, 0xE9);
				udelay(200);
			}
			iris_send_ipopt_cmds(IRIS_IP_SYS, ID_SYS_PMU_CTRL);
			if (debug_lp_opt & 0x400) {
				ktime1 = ktime_get();
				timeus = (u32)ktime_to_us(ktime1) - (u32)ktime_to_us(ktime0);
				ktime0 = ktime1;
				IRIS_LOGI("spend time PT HS commmand %d us", timeus);
			}
		}

		pcfg->cur_fps_in_iris = next_fps;
		pcfg->cur_vres_in_iris = next_vres;
		iris_update_frc_fps(pcfg->cur_fps_in_iris & 0xFF);

		//mutex_lock(&pcfg->abypss_ctrl.abypass_mutex);
		pcfg->abypss_ctrl.pending_mode = MAX_MODE;
		pcfg->abypss_ctrl.abypass_mode = PASS_THROUGH_MODE;
		//mutex_unlock(&pcfg->abypss_ctrl.abypass_mutex);

		IRIS_LOGI("Exit abyp done");
		return true;
	}

	IRIS_LOGE("Exit abyp mode Failed!");
	return false;
}

void iris_video_abyp_enter(void)
{
	//todo: I3C or i2c to save more power (one-wire power down/up sys)
	struct iris_cfg *pcfg;
	int i;
	int abyp_status_gpio;

	pcfg = iris_get_cfg();

	iris_send_ipopt_cmds(IRIS_IP_RX, ID_RX_ENTER_TTL);
	IRIS_LOGI("enter TTL bypass");

	//disable dynamic power gating, power down other domains
	iris_video_abyp_power(false);
	IRIS_LOGI("power down other domains");

	iris_send_one_wired_cmd(IRIS_ENTER_ANALOG_BYPASS);
	IRIS_LOGI("enter abyp");

	/* check abyp gpio status */
	for (i = 0; i < 3; i++) {
		udelay(10 * 1000);
		abyp_status_gpio = iris_check_abyp_ready();
		IRIS_LOGD("%s(%d), ABYP status: %d.", __func__, __LINE__, abyp_status_gpio);
		if (abyp_status_gpio == 1) {
			iris_send_one_wired_cmd(IRIS_POWER_DOWN_MIPI);
			//IRIS_LOGI("power down mipi");
			pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE;
			break;
		}
	}
	//usleep_range(1000 * 40, 1000 * 40 + 1);
}

void iris_video_abyp_exit(void)
{
	struct iris_cfg *pcfg;
	int i;
	int abyp_status_gpio;

	pcfg = iris_get_cfg();
	IRIS_LOGI("%s", __func__);


	iris_send_one_wired_cmd(IRIS_POWER_UP_MIPI);
	//IRIS_LOGI("power up mipi");
	udelay(10 * 1000);

	iris_send_one_wired_cmd(IRIS_EXIT_ANALOG_BYPASS);
	//IRIS_LOGI("exit abyp");

	/* check abyp gpio status */
	for (i = 0; i < 3; i++) {
		udelay(10 * 1000);
		abyp_status_gpio = iris_check_abyp_ready();
		IRIS_LOGI("%s(%d), ABYP status: %d.", __func__, __LINE__, abyp_status_gpio);
		if (abyp_status_gpio == 0) {
			pcfg->abypss_ctrl.abypass_mode = PASS_THROUGH_MODE;
			break;
		}
	}

	/*power up other domains manually */
	iris_video_abyp_power(true);
	IRIS_LOGI("power up other domains");
	usleep_range(1000 * 20, 1000 * 20 + 1);

	/*configure other domains IP via DMA trigger */
	iris_send_ipopt_cmds(IRIS_IP_DMA, 0xe5);
	IRIS_LOGI("write dma to trigger other domains");
	usleep_range(1000 * 40, 1000 * 40 + 1);

	/*enable dynamic power gating if need */
	if (pcfg->lp_ctrl.dynamic_power) {
		IRIS_LOGI(" [%s, %d] open psr_mif osd first address eco.", __func__, __LINE__);
		iris_psf_mif_dyn_addr_set(true);
		iris_dynamic_power_set(true);
	} else {
		IRIS_LOGI(" [%s, %d] close psr_mif osd first address eco.", __func__, __LINE__);
		iris_psf_mif_dyn_addr_set(false);
	}
	//usleep_range(1000 * 40, 1000 * 40 + 1);
	iris_send_ipopt_cmds(IRIS_IP_RX, ID_RX_EXIT_TTL);
	IRIS_LOGI("exit TTL bypass");
	//usleep_range(1000 * 40, 1000 * 40 + 1);

}

#if 0
int iris_pt_to_abyp_switch(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	int abyp_status_gpio;
	int *pswitch_state = &pcfg->abypss_ctrl.abyp_switch_state;
	int *pframe_delay = &pcfg->abypss_ctrl.frame_delay;
	int ret = false;

	//mutex_lock(&pcfg->abypss_ctrl.abypass_mutex);
	pcfg->abypss_ctrl.pending_mode = ANALOG_BYPASS_MODE;
	//mutex_unlock(&pcfg->abypss_ctrl.abypass_mutex);

	switch (*pswitch_state) {
	case ANALOG_BYPASS_ENTER_STATE:
		/* enter abyp */
		iris_send_ipopt_cmds(IRIS_IP_SYS, 6);
		IRIS_LOGI("send enter abyp");
		*pswitch_state = ANALOG_BYPASS_CHECK_STATE;
		break;
	case ANALOG_BYPASS_CHECK_STATE:
		abyp_status_gpio = iris_check_abyp_ready();
		if (abyp_status_gpio == 1) {
			iris_send_one_wired_cmd(IRIS_POWER_DOWN_MIPI);
			IRIS_LOGD("power down mipi");
			pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE;
			IRIS_LOGE("Enter abyp done");
			//mutex_lock(&pcfg->abypss_ctrl.abypass_mutex);
			pcfg->abypss_ctrl.pending_mode = MAX_MODE;
			//mutex_unlock(&pcfg->abypss_ctrl.abypass_mutex);
			ret = true;
		}
		break;
	default:
		break;
	}
	IRIS_LOGI("%s state: %d, delay: %d", __func__, *pswitch_state, *pframe_delay);

	return ret;
}

int iris_abyp_to_pt_switch(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	int abyp_status_gpio;
	int *pswitch_state = &pcfg->abypss_ctrl.abyp_switch_state;
	int *pframe_delay = &pcfg->abypss_ctrl.frame_delay;
	int ret = false;

	static ktime_t kt0, kt1;

	//mutex_lock(&pcfg->abypss_ctrl.abypass_mutex);
	pcfg->abypss_ctrl.pending_mode = PASS_THROUGH_MODE;
	//mutex_unlock(&pcfg->abypss_ctrl.abypass_mutex);

	/*if state switch need delay several video frames*/
	if (*pframe_delay > 1) {
		*pframe_delay -= 1;
		return ret;
	}
	if (pcfg->iris_initialized == false) {
		/* light up iris */
		int i;

		iris_send_one_wired_cmd(IRIS_POWER_UP_MIPI);
		udelay(5000);
		iris_mipi_abyp_switch(false);
		for (i = 0; i < 10; i++) {
			abyp_status_gpio = iris_check_abyp_ready();
			if (abyp_status_gpio == 0)
				break;
			udelay(5000);
		}
		if (abyp_status_gpio == 0) {
			iris_lightup(pcfg->panel, NULL);
			pcfg->iris_initialized = true;
			IRIS_LOGI("%s, light up iris", __func__);
			//mutex_lock(&pcfg->abypss_ctrl.abypass_mutex);
			pcfg->abypss_ctrl.abypass_mode = PASS_THROUGH_MODE;
			pcfg->abypss_ctrl.pending_mode = MAX_MODE;
			//mutex_unlock(&pcfg->abypss_ctrl.abypass_mutex);
			ret = true;
		}

		return ret;
	}

	switch (*pswitch_state) {
	case POWER_UP_STATE:
		kt0 = ktime_get();
		/* exit low power mode */
		iris_send_one_wired_cmd(IRIS_POWER_UP_MIPI);
		*pswitch_state = CONFIG_MIPI_STATE;
		IRIS_LOGI("power up");
		//udelay(3500);
		//iris_mipi_abyp_switch(false);
		*pframe_delay = 2;
		break;
	case CONFIG_MIPI_STATE:
		kt0 = ktime_get();
		/* configure MIPI via DMA */
		iris_send_ipopt_cmds(IRIS_IP_DMA, 0xe9);
		*pswitch_state = ANALOG_BYPASS_EXIT_STATE;
		IRIS_LOGI("configure mipi");
		*pframe_delay = 1;
		break;

	case ANALOG_BYPASS_EXIT_STATE:
		/* exit abyp */
		//iris_send_one_wired_cmd(IRIS_EXIT_ANALOG_BYPASS);
		iris_mipi_abyp_switch(false);
		IRIS_LOGI("send exit abyp");
		*pswitch_state = ANALOG_BYPASS_CHECK_STATE;
		break;
	case ANALOG_BYPASS_CHECK_STATE:
		abyp_status_gpio = iris_check_abyp_ready();
		if (abyp_status_gpio == 0) {
			iris_send_ipopt_cmds(IRIS_IP_SYS, ID_SYS_PMU_CTRL);
			IRIS_LOGI("set pmu");
			*pswitch_state = CONFIG_DMA_STATE;
		}
		break;
	case CONFIG_DMA_STATE:
		iris_send_ipopt_cmds(IRIS_IP_PWIL, 0x51); /*WA force enable PWIL capture*/
		iris_send_ipopt_cmds(IRIS_IP_DMA, 0xe5);
		IRIS_LOGI("trigger dma");
		*pswitch_state = EXIT_TTL_STATE;
		break;
	case EXIT_TTL_STATE:
		iris_send_ipopt_cmds(IRIS_IP_RX, ID_RX_EXIT_TTL);
		IRIS_LOGI("exit TTL bypass");
		IRIS_LOGE("Exit abyp done");
		kt1 = ktime_get();
		IRIS_LOGI("abyp->pt: total_time: %d us",
				((u32) ktime_to_us(kt1) - (u32) ktime_to_us(kt0)));
		//mutex_lock(&pcfg->abypss_ctrl.abypass_mutex);
		pcfg->abypss_ctrl.pending_mode = MAX_MODE;
		pcfg->abypss_ctrl.abypass_mode = PASS_THROUGH_MODE;
		//mutex_unlock(&pcfg->abypss_ctrl.abypass_mutex);
		ret = true;
		break;
	default:
		break;
	}
	IRIS_LOGI("%s state: %d, delay: %d", __func__, *pswitch_state, *pframe_delay);

	return ret;
}
#endif

/* Switch PT and Bypass mode */
/* Return: true is PT, false is Bypass */
bool iris_abypass_switch_proc(struct dsi_display *display, int mode, bool pending, bool first)
{
	struct iris_cfg *pcfg;
	bool pt_mode;
	int dport_status = 0;

	pcfg = iris_get_cfg();
	pt_mode = (pcfg->abypss_ctrl.abypass_mode == PASS_THROUGH_MODE);

	if (!pcfg->lp_ctrl.abyp_enable) {
		IRIS_LOGE("abyp is disable!");
		return pt_mode;
	}

	if (pcfg->rx_mode != pcfg->tx_mode) {
		IRIS_LOGE("abyp can't be supported! rx_mode != tx_mode!");
		return pt_mode;
	}

	if (pending) {
		mutex_lock(&pcfg->abypss_ctrl.abypass_mutex);
		pcfg->abypss_ctrl.pending_mode = mode & BIT(0);
		mutex_unlock(&pcfg->abypss_ctrl.abypass_mutex);
		return pt_mode;
	}

	SDE_ATRACE_BEGIN("iris_abyp_switch");
	mutex_lock(&pcfg->abypss_ctrl.abypass_mutex);
	dport_status = mode & PQ_SWITCH_MASK; // check pq switch mask
	// Check GPIO or mipi inside abyp_enter, abyp_exit
	if ((mode & BIT(0)) == ANALOG_BYPASS_MODE) {
		if (first)
			pcfg->abypss_ctrl.abyp_switch_state = ANALOG_BYPASS_ENTER_STATE;
		if (pcfg->rx_mode == DSI_OP_CMD_MODE) { /* command mode */
			pt_mode = iris_fast_cmd_abyp_enter();
		} else {
			iris_video_abyp_enter();
			pt_mode = false;
		}
	} else if ((mode & BIT(0)) == PASS_THROUGH_MODE) {
		if (first)
			pcfg->abypss_ctrl.abyp_switch_state = POWER_UP_STATE;
		if (pcfg->rx_mode == DSI_OP_CMD_MODE) {/* command mode */
			pt_mode = iris_fast_cmd_abyp_exit();
		} else {
			iris_video_abyp_exit();
			pt_mode = true;
		}
		/* Soft Iris switch iris PQ: Close dport after enter PT imediately,
		 * IrisService will open dport after PQ switch
		 */
		if (dport_status && pt_mode)
			iris_dom_set(0);
	} else
		IRIS_LOGE("%s: switch mode: %d not supported!", __func__, mode);
	mutex_unlock(&pcfg->abypss_ctrl.abypass_mutex);
	SDE_ATRACE_END("iris_abyp_switch");

	return pt_mode;
}

void iris_abyp_lp(int mode)
{
	int abyp_status_gpio;

	abyp_status_gpio = iris_check_abyp_ready();
	IRIS_LOGD("%s(%d), ABYP status: %d, lp_mode: %d",
			__func__, __LINE__, abyp_status_gpio, mode);

	if (abyp_status_gpio == 1) {
		if (mode == ABYP_POWER_DOWN_SYS)
			iris_send_one_wired_cmd(IRIS_POWER_DOWN_SYS);
		else if (mode == ABYP_POWER_DOWN_MIPI)
			iris_send_one_wired_cmd(IRIS_POWER_DOWN_MIPI);
		else if (mode == ABYP_POWER_DOWN_PLL)
			iris_send_ipopt_cmds(IRIS_IP_SYS, 0x26);
		else
			IRIS_LOGW("[%s:%d] mode: %d error", __func__, __LINE__, mode);
	} else {
		IRIS_LOGW("iris is not in ABYP mode");
	}

}

int iris_exit_abyp(bool one_wired)
{
	int i = 0;
	int abyp_status_gpio;

	/* try to exit abyp */
	if (one_wired) {
		iris_send_one_wired_cmd(IRIS_EXIT_ANALOG_BYPASS);
		udelay(2000);
	} else {
		iris_mipi_abyp_switch(false); /* switch by MIPI command */
		udelay(100);
	}
	IRIS_LOGI("send exit abyp, one_wired:%d.", one_wired);

	/* check abyp gpio status */
	for (i = 0; i < 10; i++) {
		abyp_status_gpio = iris_check_abyp_ready();
		IRIS_LOGD("%s, ABYP status: %d.", __func__, abyp_status_gpio);
		if (abyp_status_gpio == 0)
			break;
		udelay(3 * 1000);
	}

	return abyp_status_gpio;
}

int iris_lightup_opt_get(void)
{
	return debug_on_opt;
}

void iris_lp_setting_off(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	pcfg->abypss_ctrl.pending_mode = MAX_MODE;
}

#define CHECK_KICKOFF_FPS_CADNENCE
#if defined(CHECK_KICKOFF_FPS_CADNENCE)
int getFrameDiff(long timeDiff)
{
	int frameDiff;

	if (timeDiff < 11) // 16.7-5 ms
		frameDiff = 0;
	else if (timeDiff < 28) // 33.3-5
		frameDiff = 1;
	else if (timeDiff < 45)    // 50-5
		frameDiff = 2;
	else if (timeDiff < 61)    // 66.7-5
		frameDiff = 3;
	else if (timeDiff < 78)    // 83.3-5
		frameDiff = 4;
	else if (timeDiff < 95)    // 100 - 5
		frameDiff = 5;
	else if (timeDiff < 111)   // 116.7 - 5
		frameDiff = 6;
	else
		frameDiff = 7;
	return frameDiff;
}

#define CHECK_KICKOFF_FPS_DURATION      5 /*EVERY 5s*/

void iris_check_kickoff_fps_cadence(void)
{
	static u32 kickoff_cnt;
	u32 timeusDelta = 0;
	static ktime_t ktime_kickoff_start;
	static u32 us_last_kickoff;
	ktime_t ktime_kickoff;
	static u32 cadence[10];
	static int cdIndex;
	u32 us_timediff;

	if (kickoff_cnt == 0) {
		kickoff_cnt++;
		ktime_kickoff_start = ktime_get();
		memset(cadence, 0, sizeof(cadence));
		cdIndex = 0;
		cadence[cdIndex++] = 0;
		us_last_kickoff = (u32)ktime_to_us(ktime_kickoff_start);
	} else {
		kickoff_cnt++;
		ktime_kickoff = ktime_get();
		timeusDelta = (u32)ktime_to_us(ktime_kickoff) - (u32)ktime_to_us(ktime_kickoff_start);
		us_timediff = (u32)ktime_to_us(ktime_kickoff) - us_last_kickoff;
		us_last_kickoff = (u32)ktime_to_us(ktime_kickoff);
		if (cdIndex > 9)
			cdIndex = 0;

		cadence[cdIndex++] = getFrameDiff((us_timediff+500)/1000);//16667
		if (timeusDelta > 1000000*CHECK_KICKOFF_FPS_DURATION) {
			if ((debug_trace_opt&IRIS_TRACE_FPS) == IRIS_TRACE_FPS)
				IRIS_LOGI("iris: kickoff fps % d", kickoff_cnt/CHECK_KICKOFF_FPS_DURATION);
			if ((debug_trace_opt&IRIS_TRACE_CADENCE) == IRIS_TRACE_CADENCE)
				IRIS_LOGI("iris: Latest cadence: %d %d %d %d %d, %d %d %d %d %d",
						cadence[0], cadence[1], cadence[2], cadence[3], cadence[4],
						cadence[5], cadence[6], cadence[7], cadence[8], cadence[9]);
			kickoff_cnt = 0;
		}
	}
}
#endif

int iris_prepare_for_kickoff(void *phys_enc)
{
	struct sde_encoder_phys *phys_encoder = phys_enc;
	struct sde_connector *c_conn = NULL;
	struct dsi_display *display = NULL;
	struct iris_cfg *pcfg;
	int mode;

	if (phys_encoder == NULL)
		return -EFAULT;
	if (phys_encoder->connector == NULL)
		return -EFAULT;

	c_conn = to_sde_connector(phys_encoder->connector);
	if (c_conn == NULL)
		return -EFAULT;

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI)
		return 0;

	display = c_conn->display;
	if (display == NULL)
		return -EFAULT;

	pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	if (iris_virtual_display(display) || pcfg->valid < PARAM_PARSED)
		return 0;

#if defined(CHECK_KICKOFF_FPS_CADNENCE)
	if (debug_trace_opt > 0)
		iris_check_kickoff_fps_cadence();
#endif
	mutex_lock(&pcfg->abypss_ctrl.abypass_mutex);
	if (pcfg->abypss_ctrl.pending_mode != MAX_MODE) {
		mode = pcfg->abypss_ctrl.pending_mode;
		pcfg->abypss_ctrl.pending_mode = MAX_MODE;
		mutex_unlock(&pcfg->abypss_ctrl.abypass_mutex);
		mutex_lock(&pcfg->panel->panel_lock);
		iris_abypass_switch_proc(pcfg->display, mode, false, false);
		mutex_unlock(&pcfg->panel->panel_lock);
	} else
		mutex_unlock(&pcfg->abypss_ctrl.abypass_mutex);

	return 0;
}

void iris_power_up_mipi(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	struct dsi_display *display = pcfg->display;

	if (iris_virtual_display(display))
		return;

	IRIS_LOGI("%s(%d), power up mipi", __func__, __LINE__);
	iris_send_one_wired_cmd(IRIS_POWER_UP_MIPI);
	udelay(3500);
}

void iris_reset_mipi(void)
{
	iris_send_one_wired_cmd(IRIS_POWER_DOWN_MIPI);
	IRIS_LOGI("%s(%d), power down mipi", __func__, __LINE__);
	udelay(3500);

	iris_send_one_wired_cmd(IRIS_POWER_UP_MIPI);
	IRIS_LOGI("%s(%d), power up mipi", __func__, __LINE__);
	udelay(3500);
}

int iris_get_abyp_mode(struct dsi_panel *panel)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	struct iris_cfg *pcfg2 = iris_get_cfg_by_index(DSI_SECONDARY);

	IRIS_LOGD("%s(%d), secondary: %d abyp mode: %d, %d",
			__func__, __LINE__,
			panel->is_secondary,
			pcfg->abypss_ctrl.abypass_mode,
			pcfg2->abypss_ctrl.abypass_mode);
	return (!panel->is_secondary) ?
		pcfg->abypss_ctrl.abypass_mode : pcfg2->abypss_ctrl.abypass_mode;
}


/*== Low Power debug related ==*/

static ssize_t iris_abyp_dbg_write(struct file *file,
		const char __user *buff, size_t count, loff_t *ppos)
{
	unsigned long val;
	struct iris_cfg *pcfg;
	static int cnt;

	pcfg = iris_get_cfg();

	if (kstrtoul_from_user(buff, count, 0, &val))
		return -EFAULT;

	if (val == 0) {
		iris_fast_cmd_abyp_exit();
		IRIS_LOGI("abyp->pt, %d", cnt);
	} else if (val == 1) {
		iris_fast_cmd_abyp_enter();
		IRIS_LOGI("pt->abyp, %d", cnt);
	} else if (val == 2) {
		iris_video_abyp_enter();
	} else if (val == 3) {
		iris_video_abyp_exit();
	} else if (val >= 11 && val <= 19) {
		IRIS_LOGI("%s one wired %d", __func__, (int)(val - 10));
		iris_send_one_wired_cmd((int)(val - 10));
	} else if (val == 20) {
		iris_send_ipopt_cmds(IRIS_IP_SYS, 5);
		IRIS_LOGI("miniPMU abyp->pt");
	} else if (val == 21) {
		iris_send_ipopt_cmds(IRIS_IP_SYS, 4);
		IRIS_LOGI("miniPMU pt->abyp");
	} else if (val == 22) {
		iris_send_ipopt_cmds(IRIS_IP_TX, 4);
		IRIS_LOGI("Enable Tx");
	} else if (val == 23) {
		// mutex_lock(&g_debug_mfd->switch_lock);
		iris_lightup(pcfg->panel, &(pcfg->panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_ABYP]));
		// mutex_unlock(&g_debug_mfd->switch_lock);
		IRIS_LOGI("lightup Iris abyp_panel_cmds");
	} else if (val == 24) {
		iris_abypass_switch_proc(pcfg->display, PASS_THROUGH_MODE, false, true);
	} else if (val == 25) {
		iris_abypass_switch_proc(pcfg->display, ANALOG_BYPASS_MODE, false, true);
	} else if (val == 100) {
		debug_abyp_gpio_status = iris_check_abyp_ready();
	}

	return count;
}

static ssize_t iris_lp_dbg_write(struct file *file,
		const char __user *buff, size_t count, loff_t *ppos)
{
	unsigned long val;
	struct iris_cfg *pcfg;

	pcfg = iris_get_cfg();

	if (kstrtoul_from_user(buff, count, 0, &val))
		return -EFAULT;

	if (val == 0) {
		iris_dynamic_power_set(false);
		iris_ulps_source_sel(ULPS_NONE);
		IRIS_LOGE("disable dynamic & ulps low power.");
	} else if (val == 1) {
		iris_dynamic_power_set(true);
		iris_ulps_source_sel(ULPS_MAIN);
		IRIS_LOGE("enable dynamic & ulps low power.");
	} else if (val == 2) {
		IRIS_LOGE("dynamic power: %d", iris_dynamic_power_get());
		IRIS_LOGE("abyp enable: %d", pcfg->lp_ctrl.abyp_enable);
		IRIS_LOGE("ulps enable: %d", iris_ulps_enable_get());
	} else if (val == 3) {
		pcfg->lp_ctrl.abyp_enable = true;
		IRIS_LOGE("enable abyp.");
	} else if (val == 4) {
		pcfg->lp_ctrl.abyp_enable = false;
		IRIS_LOGE("disable abyp.");
	} else if (val == 5) {
		iris_ulps_source_sel(ULPS_MAIN);
		IRIS_LOGE("enable iris ulps lp.");
	} else if (val == 6) {
		iris_ulps_source_sel(ULPS_NONE);
		IRIS_LOGE("disable iris ulps lp.");
	} else if (val == 11) {
		iris_pmu_mipi2_set(true);
	} else if (val == 12) {
		iris_pmu_mipi2_set(false);
	} else if (val == 13) {
		iris_pmu_bsram_set(true);
	} else if (val == 14) {
		iris_pmu_bsram_set(false);
	} else if (val == 15) {
		iris_pmu_frc_set(true);
	} else if (val == 16) {
		iris_pmu_frc_set(false);
	} else if (val == 17) {
		iris_pmu_dscu_set(true);
	} else if (val == 18) {
		iris_pmu_dscu_set(false);
	} else if (val == 19) {
		iris_pmu_lce_set(true);
	} else if (val == 20) {
		iris_pmu_lce_set(false);
	} else if (val == 254) {
		IRIS_LOGI("lp_opt usages:");
		IRIS_LOGI("bit 0 -- MIPI2");
		IRIS_LOGI("bit 1 -- BSRAM");
		IRIS_LOGI("bit 2 -- FRC");
		IRIS_LOGI("bit 3 -- DSCU");
		IRIS_LOGI("bit 4 -- LCE");
	} else if (val == 255) {
		IRIS_LOGI("lp debug usages:");
		IRIS_LOGI("0  -- disable dynamic & ulps low power.");
		IRIS_LOGI("1  -- enable dynamic & ulps low power.");
		IRIS_LOGI("2  -- show low power flag.");
		IRIS_LOGI("3  -- enable abyp.");
		IRIS_LOGI("4  -- disable abyp.");
		IRIS_LOGI("11 -- enable mipi2 power.");
		IRIS_LOGI("12 -- disable mipi2 power.");
		IRIS_LOGI("13 -- enable bsram power.");
		IRIS_LOGI("14 -- disable bram power.");
		IRIS_LOGI("15 -- enable frc power.");
		IRIS_LOGI("16 -- disable frc power.");
		IRIS_LOGI("17 -- enable dsc unit power.");
		IRIS_LOGI("18 -- disable dsc unit power.");
		IRIS_LOGI("19 -- enable lce power.");
		IRIS_LOGI("20 -- disable lce power.");
		IRIS_LOGI("254 -- show lp_opt usages.");
		IRIS_LOGI("255 -- show debug usages.");
	}
	return count;
}

int iris_dbgfs_lp_init(struct dsi_display *display)
{
	struct iris_cfg *pcfg;
	static const struct file_operations iris_abyp_dbg_fops = {
		.open = simple_open,
		.write = iris_abyp_dbg_write,
	};

	static const struct file_operations iris_lp_dbg_fops = {
		.open = simple_open,
		.write = iris_lp_dbg_write,
	};

	pcfg = iris_get_cfg();

	if (pcfg->dbg_root == NULL) {
		pcfg->dbg_root = debugfs_create_dir("iris", NULL);
		if (IS_ERR_OR_NULL(pcfg->dbg_root)) {
			IRIS_LOGE("debugfs_create_dir for iris_debug failed, error %ld",
					PTR_ERR(pcfg->dbg_root));
			return -ENODEV;
		}
	}

	debugfs_create_u32("lp_opt", 0644, pcfg->dbg_root,
			(u32 *)&debug_lp_opt);

	debugfs_create_u32("abyp_opt", 0644, pcfg->dbg_root,
			(u32 *)&debug_on_opt);

	debugfs_create_u32("abyp_gpio", 0644, pcfg->dbg_root,
			(u32 *)&debug_abyp_gpio_status);

	debugfs_create_u32("trace", 0644, pcfg->dbg_root,
			(u32 *)&debug_trace_opt);

	debugfs_create_u32("dual_test", 0644, pcfg->dbg_root,
		(u32 *)&pcfg->dual_test);

	debugfs_create_bool("esd_enable", 0644, pcfg->dbg_root,
			&(pcfg->lp_ctrl.esd_enable));

	debugfs_create_u32("esd_cnt", 0644, pcfg->dbg_root,
			(u32 *)&(pcfg->lp_ctrl.esd_cnt));

	if (debugfs_create_file("abyp", 0644, pcfg->dbg_root, display,
				&iris_abyp_dbg_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
				__FILE__, __LINE__);
		return -EFAULT;
	}

	if (debugfs_create_file("lp", 0644, pcfg->dbg_root, display,
				&iris_lp_dbg_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
				__FILE__, __LINE__);
		return -EFAULT;
	}

	return 0;
}
