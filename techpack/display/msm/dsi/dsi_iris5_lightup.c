#include <drm/drm_mipi_dsi.h>
#include <video/mipi_display.h>
#include <linux/of_gpio.h>
#include <dsi_drm.h>
#include <sde_encoder_phys.h>
#include <sde_trace.h>
#include "dsi_parser.h"
#include "dsi_iris5_api.h"
#include "dsi_iris5_lightup.h"
#include "dsi_iris5_lightup_ocp.h"
#include "dsi_iris5_lp.h"
#include "dsi_iris5_pq.h"
#include "dsi_iris5_ioctl.h"
#include "dsi_iris5_lut.h"
#include "dsi_iris5_mode_switch.h"
#include "dsi_iris5_osd.h"
#include "dsi_iris5_loop_back.h"
#include "iris_log.h"

/*
 * These files include source code for the Licensed Software that is provided under the terms and
 * conditions of a Master Software License Agreement and its applicable schedules.
 */

#define IRIS_CHIP_VER_0   0
#define IRIS_CHIP_VER_1   1
#define IRIS_OCP_HEADER_ADDR_LEN  8

//#define HIGH_ABYP_LOW_ABYP
//#define HIGH_PT_LOW_PT
//#define HIGH_ABYP_LOW_PT

#define to_dsi_display(x) container_of(x, struct dsi_display, host)

enum {
	MIPI2_LP_PRE = 0,
	MIPI2_LP_SWRST,
	MIPI2_LP_POST,
	MIPI2_LP_FINISH,
	MIPI2_LP_INVALID,
};

enum
{
	DSI_CMD_ONE_LAST_FOR_MULT_IPOPT = 0,
	DSI_CMD_ONE_LAST_FOR_ONE_IPOPT,
	DSI_CMD_ONE_LAST_FOR_ONE_PKT,
};

struct osd_blending_st {
	atomic_t compression_mode;
	uint32_t enter_lp_st;
};
static int gcfg_index = 0;
static struct iris_cfg gcfg[IRIS_CFG_NUM] = {};
static uint8_t g_cont_splash_type = IRIS_CONT_SPLASH_NONE;
static int iris_disable_osd_autorefresh = 0;
uint8_t iris_pq_update_path = PATH_DSI;

int iris_fstatus_debugfs_init(struct dsi_display *display);
static int iris_cont_splash_debugfs_init(struct dsi_display *display);
void iris_send_cont_splash_pkt(uint32_t type);
static struct osd_blending_st osd_blending_work;
static void iris_vfr_update_work(struct work_struct *work);
static int iris_update_pq_seq(struct iris_update_ipopt *popt, int len);

static int dsi_iris_vreg_get(void)
{
	int rc = 0;
	int i;
	struct regulator *vreg = NULL;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct dsi_panel *panel = pcfg->panel;

	for (i = 0; i < pcfg->iris_power_info.count; i++) {
		vreg = devm_regulator_get(panel->parent,
					  pcfg->iris_power_info.vregs[i].vreg_name);
		rc = PTR_RET(vreg);
		if (rc) {
			DSI_ERR("failed to get %s regulator\n",
			       pcfg->iris_power_info.vregs[i].vreg_name);
			goto error_put;
		}
		pcfg->iris_power_info.vregs[i].vreg = vreg;
	}

	return rc;
error_put:
	for (i = i - 1; i >= 0; i--) {
		devm_regulator_put(pcfg->iris_power_info.vregs[i].vreg);
		pcfg->iris_power_info.vregs[i].vreg = NULL;
	}
	return rc;
}

static int dsi_iris_vreg_put(void)
{
	int rc = 0;
	int i;
	struct iris_cfg *pcfg = iris_get_cfg();

	for (i = pcfg->iris_power_info.count - 1; i >= 0; i--)
		devm_regulator_put(pcfg->iris_power_info.vregs[i].vreg);

	return rc;
}

void iris5_init(struct dsi_display *display, struct dsi_panel *panel)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	IRIS_LOGI("%s:%d", __func__, __LINE__);
	pcfg->display = display;
	pcfg->panel = panel;
	pcfg->switch_case = 0; //SWITCH_ABYP_TO_ABYP;
	pcfg->iris5_i2c_read = NULL;
	pcfg->iris5_i2c_write = NULL;
	pcfg->lp_ctrl.esd_enable = true;
	pcfg->aod = false;
	pcfg->fod = false;
	pcfg->fod_pending = false;
	atomic_set(&pcfg->fod_cnt, 0);
	pcfg->iris_initialized = false;
#if defined(IRIS5_ABYP_LIGHTUP)
	pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE;
#else
	// UEFI is running bypass mode.
	pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE;

	if (iris_virtual_display(display))
		pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE;
	else if (pcfg->valid < 2)
		pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE;
	else
		INIT_WORK(&pcfg->vfr_update_work, iris_vfr_update_work);
#endif
	pcfg->abypss_ctrl.pending_mode = MAX_MODE;
	mutex_init(&pcfg->abypss_ctrl.abypass_mutex);

	pcfg->ext_clk = devm_clk_get(&display->pdev->dev, "pw_bb_clk2");

	if (!iris_virtual_display(display) && pcfg->valid >= 2) {
		pcfg->cmd_list_index = IRIS_DTSI0_PIP_IDX;
		pcfg->cur_fps_in_iris = LOW_FREQ;
		pcfg->next_fps_for_iris = LOW_FREQ;
		pcfg->cur_vres_in_iris = FHD_H;
		iris_lp_debugfs_init(display);
		iris_pq_debugfs_init(display);
		iris_cont_splash_debugfs_init(display);
		iris_ms_debugfs_init(display);
		iris_adb_type_debugfs_init(display);
		iris_fw_calibrate_status_debugfs_init();
		iris_fstatus_debugfs_init(display);
	}
	dsi_iris_vreg_get();
	//pcfg->mipi_pwr_st = true; //force to true for aux ch at boot up to workaround
	mutex_init(&pcfg->gs_mutex);
}

void iris5_deinit(struct dsi_display *display)
{
	struct iris_cfg *pcfg;
	int i;

	if (iris_virtual_display(display))
		pcfg = iris_get_cfg_by_index(DSI_SECONDARY);
	else
		pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (pcfg->ext_clk) {
		devm_clk_put(&display->pdev->dev, pcfg->ext_clk);
		pcfg->ext_clk = NULL;
	}

	for (i = 0; i < IRIS_PIP_IDX_CNT; i++) {
		iris_free_ipopt_buf(i); //CID89938 Resource leak
	}
	dsi_iris_vreg_put();
}

int iris5_control_pwr_regulator(bool on)
{
	int rc;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	rc = dsi_pwr_enable_regulator(&pcfg->iris_power_info, on);
	if (rc) {
		IRIS_LOGE("failed to %s iris vdd vreg, rc=%d", on? "enable" : "disable", rc);
	}

	return rc;
}

int iris5_power_on(struct dsi_panel *panel)
{
	struct dsi_panel_reset_config *r_config = &panel->reset_config;
	int rc = 0;

	IRIS_LOGW("%s for [%s] %s gcfg = %i is_secondary = %i", __func__, panel->name, panel->type, gcfg_index, panel->is_secondary);
	if (panel->is_secondary)
		return rc;

	if (gpio_is_valid(r_config->iris_vdd_gpio)) {
		rc = gpio_direction_output(r_config->iris_vdd_gpio, 1);
		if (rc)
			IRIS_LOGE("unable to set dir for iris vdd gpio rc=%d", rc);
		gpio_set_value(r_config->iris_vdd_gpio, 1);
		IRIS_LOGI("enable iris vdd gpio");
	} else {
		rc = iris5_control_pwr_regulator(true);
		if (rc)
			IRIS_LOGE("failed to power on iris vdd, rc=%d", rc);
		else
			IRIS_LOGW("enable iris vdd regulator");
	}

	return rc;
}

void iris5_reset(struct dsi_panel *panel)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	struct dsi_panel_reset_config *r_config = &panel->reset_config;
	int rc;

	IRIS_LOGW("%s for [%s] %s gcfg = %i is_secondary = %i", __func__, panel->name, panel->type, gcfg_index, panel->is_secondary);
	if (panel->is_secondary)
		return;

	IRIS_LOGW("iris5_reset for [%s]", panel->name);

	if (pcfg->ext_clk) {
		clk_prepare_enable(pcfg->ext_clk);
		IRIS_LOGI("clk enable");
		usleep_range(1000, 1000);
	} else { // No need to control vdd and clk
		IRIS_LOGV("clk does not valid");
		goto ERROR_CLK;
	}

	if (gpio_is_valid(panel->reset_config.iris_rst_gpio)) {
		rc = gpio_direction_output(r_config->iris_rst_gpio, 0);
		if (rc) {
			IRIS_LOGE("unable to set dir for iris reset gpio rc=%d", rc);
			goto ERROR_RST_GPIO;
		}
		gpio_set_value(r_config->iris_rst_gpio, 0);
		IRIS_LOGW("reset gpio 0");
		usleep_range(1000, 1000);
		gpio_set_value(r_config->iris_rst_gpio, 1);
		IRIS_LOGW("reset gpio 1");
	}
	return;

ERROR_RST_GPIO:
	clk_disable_unprepare(pcfg->ext_clk);
ERROR_CLK:
	iris5_control_pwr_regulator(false);

}

void iris5_gpio_parse(struct dsi_panel *panel)
{
	struct dsi_parser_utils *utils = &panel->utils;
	static int iris_osd_gpio = -1;

	IRIS_LOGW("%s for [%s] %s gcfg = %i is_secondary = %i", __func__, panel->name, panel->type, gcfg_index, panel->is_secondary);
	if (!strcmp(panel->type, "secondary")) {
		panel->reset_config.abyp_gpio = -1;
		panel->reset_config.abyp_status_gpio = -1;
		panel->reset_config.iris_rst_gpio = - 1;
		panel->reset_config.iris_osd_gpio = iris_osd_gpio;
		if (!gpio_is_valid(panel->reset_config.iris_osd_gpio))
			IRIS_LOGW("[%s:%d] osd gpio not specified", __func__, __LINE__,panel->reset_config.iris_osd_gpio);
		panel->reset_config.iris_vdd_gpio = -1;
	} else {
		panel->reset_config.abyp_gpio = utils->get_named_gpio(utils->data,
			"qcom,platform-analog-bypass-gpio", 0);
		IRIS_LOGI("[%s:%d] abyp gpio %d", __func__, __LINE__,panel->reset_config.abyp_gpio);
		if (!gpio_is_valid(panel->reset_config.abyp_gpio))
			IRIS_LOGW("[%s:%d] abyp gpio not specified", __func__, __LINE__);
		panel->reset_config.abyp_status_gpio = utils->get_named_gpio(utils->data,
			"qcom,platform-analog-bypass-status-gpio", 0);
		IRIS_LOGI("[%s:%d] abyp status gpio %d", __func__, __LINE__, panel->reset_config.abyp_status_gpio);
		if (!gpio_is_valid(panel->reset_config.abyp_status_gpio))
			IRIS_LOGW("[%s:%d] abyp status gpio not specified", __func__, __LINE__);
		else {
			IRIS_LOGI("[%s:%d] copy osd gpio by abyp status gpio.", __func__, __LINE__);
			iris_osd_gpio = panel->reset_config.abyp_status_gpio;
		}
		panel->reset_config.iris_rst_gpio = utils->get_named_gpio(utils->data,
			"qcom,platform-iris-reset-gpio", 0);
		IRIS_LOGI("[%s:%d] iris reset gpio %d.", __func__, __LINE__,panel->reset_config.iris_rst_gpio);
		if (!gpio_is_valid(panel->reset_config.iris_rst_gpio))
			IRIS_LOGW("[%s:%d] iris reset gpio not specified", __func__, __LINE__);

		panel->reset_config.iris_vdd_gpio = utils->get_named_gpio(utils->data,
			"qcom,platform-iris-vdd-gpio", 0);
		IRIS_LOGI("[%s:%d] iris vdd gpio %d.", __func__, __LINE__,panel->reset_config.iris_vdd_gpio);
		if (!gpio_is_valid(panel->reset_config.iris_vdd_gpio))
			IRIS_LOGW("[%s:%d] iris vdd gpio not specified", __func__, __LINE__);
	}

	return;
}

void iris5_power_off(struct dsi_panel *panel)
{
	struct dsi_panel_reset_config *r_config = &panel->reset_config;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	IRIS_LOGW("%s for [%s] %s gcfg = %i is_secondary = %i", __func__, panel->name, panel->type, gcfg_index, panel->is_secondary);
	if (panel->is_secondary)
		return;

	if (gpio_is_valid(r_config->iris_rst_gpio)) {
		gpio_set_value(r_config->iris_rst_gpio, 0);
		IRIS_LOGI("iris rst disable");
	}

	if (pcfg->ext_clk) {
		clk_disable_unprepare(pcfg->ext_clk);
		IRIS_LOGI("clk disable");
		usleep_range(1000, 1000);
	}

	if (gpio_is_valid(r_config->iris_vdd_gpio))
		gpio_set_value(r_config->iris_vdd_gpio, 0);
	else
		iris5_control_pwr_regulator(false);
	IRIS_LOGI("iris vdd disable");
	usleep_range(1000, 1000);
}

void iris5_gpio_request(struct dsi_panel *panel)
{
	struct dsi_panel_reset_config *r_config = &panel->reset_config;
	int rc = 0;

	IRIS_LOGW("%s for [%s] %s gcfg = %i is_secondary = %i", __func__, panel->name, panel->type, gcfg_index, panel->is_secondary);
	if (panel->is_secondary)
		return;

	if (gpio_is_valid(r_config->iris_vdd_gpio)) {
		rc = gpio_request(r_config->iris_vdd_gpio, "iris_vdd");
		if (rc)
			IRIS_LOGW("request for iris vdd failed, rc=%d", rc);
	}

	if (gpio_is_valid(r_config->abyp_gpio)) {
		rc = gpio_request(r_config->abyp_gpio, "analog_bypass");
		if (rc)
			IRIS_LOGW("request for iris abyp_gpio failed, rc=%d", rc);
	}

	if (gpio_is_valid(r_config->abyp_status_gpio)) {
		rc = gpio_request(r_config->abyp_status_gpio, "analog_bypass_status");
		if (rc)
			IRIS_LOGW("request for iris analog bypass status gpio failed,rc=%d", rc);
	}

	if (gpio_is_valid(r_config->iris_rst_gpio)) {
		rc = gpio_request(r_config->iris_rst_gpio, "iris_reset");
		if (rc) {
			IRIS_LOGW("request for iris_rst_gpio failed, rc=%d", rc);
			if (gpio_is_valid(r_config->iris_vdd_gpio))
				gpio_free(r_config->iris_vdd_gpio);
			if (gpio_is_valid(r_config->abyp_gpio))
				gpio_free(r_config->abyp_gpio);
			if (gpio_is_valid(r_config->abyp_status_gpio))
				gpio_free(r_config->abyp_status_gpio);
		}
	}
}

void iris5_gpio_free(struct dsi_panel *panel)
{
	struct dsi_panel_reset_config *r_config = &panel->reset_config;

	IRIS_LOGW("%s for [%s] %s gcfg = %i is_secondary = %i", __func__, panel->name, panel->type, gcfg_index, panel->is_secondary);
	if (panel->is_secondary)
		return;

	if (gpio_is_valid(r_config->abyp_gpio))
		gpio_free(r_config->abyp_gpio);
	if (gpio_is_valid(r_config->abyp_status_gpio))
		gpio_free(r_config->abyp_status_gpio);
	if (gpio_is_valid(r_config->iris_rst_gpio))
		gpio_free(r_config->iris_rst_gpio);
	if (gpio_is_valid(r_config->iris_vdd_gpio))
		gpio_free(r_config->iris_vdd_gpio);

	return;
}

void iris_set_cfg_index(int index)
{
	if (index < IRIS_CFG_NUM) {
		IRIS_LOGD("%s, index: %d", __func__, index);
		gcfg_index = index;
	} else {
		IRIS_LOGE("%s, index: %d exceed %d", __func__, index, IRIS_CFG_NUM);
	}
}

bool iris_virtual_display(const struct dsi_display *display)
{
	if (display && display->panel && display->panel->is_secondary)
		return true;
	else
		return false;
}

bool iris_virtual_encoder_phys(void *phys_enc)
{
	struct sde_encoder_phys *phys_encoder = phys_enc;
	struct sde_connector *c_conn = NULL;
	struct dsi_display *display = NULL;

	if (phys_encoder == NULL) {
		return false;
	}

	if (phys_encoder->connector == NULL) {
		return false;
	}

	c_conn = to_sde_connector(phys_encoder->connector);
	if (c_conn == NULL) {
		return false;
	}

	display = c_conn->display;
	if (display == NULL) {
		return false;
	}

	if (!iris_virtual_display(display)) {
		return false;
	}

	return true;
}

struct iris_cfg* iris_get_cfg(void)
{
	return &gcfg[gcfg_index];
}

struct iris_cfg* iris_get_cfg_by_index(int index)
{
	if (index < IRIS_CFG_NUM)
		return &gcfg[index];
	else
		return NULL;
}

int iris5_abypass_mode_get(struct dsi_panel *panel)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	struct iris_cfg *pcfg2 = iris_get_cfg_by_index(DSI_SECONDARY);
	IRIS_LOGD("%s:%d, secondary = %d abypass_mode = %d,%d", __func__, __LINE__,
			panel->is_secondary,
			pcfg->abypss_ctrl.abypass_mode,
			pcfg2->abypss_ctrl.abypass_mode);
	return (!panel->is_secondary) ? pcfg->abypss_ctrl.abypass_mode : pcfg2->abypss_ctrl.abypass_mode;
}

uint8_t iris_get_cont_splash_type(void)
{
	return g_cont_splash_type;
}

struct iris_ctrl_seq *iris_get_ctrl_seq_addr(
		struct iris_ctrl_seq *base, uint8_t chip_id)
{
	struct iris_ctrl_seq *pseq = NULL;

	switch (chip_id) {
		case IRIS_CHIP_VER_0:
			pseq = base;
			break;
		case IRIS_CHIP_VER_1:
			pseq = base + 1;
			break;
		default:
			IRIS_LOGE("Unknown chip id: %d", chip_id);
			break;
	}
	return pseq;
}

static int32_t iris_is_valid_ip(uint32_t ip)
{
	if (ip >= LUT_IP_START && ip < LUT_IP_END) {
		return 0;
	}

	if (ip >= IRIS_IP_CNT) {
		IRIS_LOGE("%s(), the index is not right ip: %#x", __func__, ip);
		return -EINVAL;
	}
	return 0;
}

struct iris_ctrl_seq *iris_get_ctrl_seq_common(
		struct iris_cfg *pcfg, int32_t type)
{
	struct iris_ctrl_seq *pseq = NULL;

	if (type == IRIS_CONT_SPLASH_NONE)
		pseq = iris_get_ctrl_seq_addr(pcfg->ctrl_seq, pcfg->chip_id);
	else if (type == IRIS_CONT_SPLASH_LK)
		pseq = iris_get_ctrl_seq_addr(pcfg->ctrl_seq_cs, pcfg->chip_id);

	return pseq;
}

struct iris_ctrl_seq *iris_get_ctrl_seq(struct iris_cfg *pcfg)
{
	return iris_get_ctrl_seq_common(pcfg, IRIS_CONT_SPLASH_NONE);
}

struct iris_ctrl_seq *iris_get_ctrl_seq_cs(struct iris_cfg *pcfg)
{
	return iris_get_ctrl_seq_common(pcfg, IRIS_CONT_SPLASH_LK);
}


static bool iris_is_lut(uint8_t ip)
{
	return ip >= LUT_IP_START ? true : false;
}

static uint32_t iris_get_ocp_type(const uint8_t *payload)
{
	uint32_t *pval = NULL;

	pval  = (uint32_t *)payload;
	return cpu_to_le32(pval[0]);
}

static uint32_t iris_get_ocp_base_addr(const uint8_t *payload)
{
	uint32_t *pval = NULL;

	pval  = (uint32_t *)payload;
	return cpu_to_le32(pval[1]);
}

static void iris_set_ocp_type(const uint8_t *payload, uint32_t val)
{
	uint32_t *pval = NULL;
	pval  = (uint32_t *)payload;

	IRIS_LOGV("%s(), change addr from %#x to %#x.", __func__, pval[0], val);
	pval[0] = val;
}

static void iris_set_ocp_base_addr(const uint8_t *payload, uint32_t val)
{
	uint32_t *pval = NULL;
	pval  = (uint32_t *)payload;

	IRIS_LOGV("%s(), change addr from %#x to %#x.", __func__, pval[1], val);
	pval[1] = val;
}

static bool iris_is_direct_bus(const uint8_t *payload)
{
	uint8_t val = 0;

	val = iris_get_ocp_type(payload) & 0x0f;
	//the last 4bit will show the ocp type
	if (val == 0x00 || val == 0x0c)
		return true;

	return false;
}

static int iris_split_mult_pkt(const uint8_t *payload, int len)
{
	uint32_t pkt_size = 0;
	int mult = 1;
	struct iris_cfg *pcfg = iris_get_cfg();

	if (!iris_is_direct_bus(payload))
		return mult;

	pkt_size = pcfg->split_pkt_size;
	if (len > pkt_size + IRIS_OCP_HEADER_ADDR_LEN)
		mult =  (len - IRIS_OCP_HEADER_ADDR_LEN + pkt_size - 1) / pkt_size;

	return mult;
}

static void iris_set_cont_splash_type(uint8_t type)
{
	g_cont_splash_type = type;
}

struct iris_ip_index *iris_get_ip_idx(int32_t type)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	if (unlikely(type<IRIS_DTSI0_PIP_IDX || type>=IRIS_PIP_IDX_CNT)) {
		IRIS_LOGE("%s, can not get pip idx %u", __func__, type);
		return NULL;
	}

	return pcfg->ip_index_arr[type];
}


static int32_t  iris_get_ip_idx_type(struct iris_ip_index *pip_index)
{
	int32_t type = 0;
	struct iris_cfg *pcfg = iris_get_cfg();

	if (pip_index == pcfg->ip_index_arr[IRIS_DTSI0_PIP_IDX])
		type = IRIS_DTSI0_PIP_IDX;
	else if (pip_index == pcfg->ip_index_arr[IRIS_DTSI1_PIP_IDX])
		type = IRIS_DTSI1_PIP_IDX;
	else if (pip_index == pcfg->ip_index_arr[IRIS_LUT_PIP_IDX])
		type = IRIS_LUT_PIP_IDX;

	return type;
}


static void iris_init_ip_index(struct iris_ip_index  *pip_index)
{
	int32_t i = 0;
	int32_t j = 0;
	int32_t cnt = 0;
	int32_t ip_cnt = IRIS_IP_CNT;

	if (iris_get_ip_idx_type(pip_index) == IRIS_LUT_PIP_IDX) {
		ip_cnt = LUT_IP_END - LUT_IP_START;
	}

	for (i = 0; i < ip_cnt; i++) {
		cnt = pip_index[i].opt_cnt;
		for (j = 0; j < cnt; j++) {
			pip_index[i].opt[j].cmd = NULL;
			pip_index[i].opt[j].link_state = 0xff;
		}
	}
}

static int32_t iris_alloc_buf_for_pip(struct iris_ip_index *pip_index)
{
	int i = 0;
	int j = 0;
	int opt_cnt = 0;
	int ip_cnt = IRIS_IP_CNT;

	if (iris_get_ip_idx_type(pip_index) == IRIS_LUT_PIP_IDX) {
		ip_cnt = LUT_IP_END - LUT_IP_START;
	}

	for (i = 0; i < ip_cnt; i++) {
		opt_cnt = pip_index[i].opt_cnt;
		if (opt_cnt != 0) {
			pip_index[i].opt =
				kzalloc(opt_cnt * sizeof(struct iris_ip_opt)
						, GFP_KERNEL);
			if (!pip_index[i].opt) {
				/*free already malloc space*/
				for (j = 0; j < i; j++) {
					kfree(pip_index[j].opt);
					pip_index[j].opt = NULL;
				}
				return -ENOMEM;
			}
		}
	}

	return 0;
}

static int32_t iris_alloc_buf_for_desc(struct dsi_cmd_desc **cmds,
		const struct iris_cmd_statics *pcmd_statics)
{
	int len = 0;

	/*create dsi cmds*/
	if (pcmd_statics->cnt == 0)
		return -EINVAL;

	len = pcmd_statics->cnt * sizeof(struct dsi_cmd_desc);
	*cmds = kzalloc(len, GFP_KERNEL);
	if (!(*cmds)) {
		IRIS_LOGE("can not kzalloc space for dsi");
		return -ENOMEM;
	}
	IRIS_LOGI("%s(), alloc %p, count %d", __func__, *cmds, pcmd_statics->cnt);
	return 0;
}


static int32_t  iris_alloc_buf(
		struct dsi_cmd_desc **cmds,
		const struct iris_cmd_statics *pcmd_statics,
		struct iris_ip_index *pip_index)
{
	int32_t rc = 0;

	rc = iris_alloc_buf_for_desc(cmds, pcmd_statics);
	if (rc)
		return rc;
	rc = iris_alloc_buf_for_pip(pip_index);
	if (rc) {
		kfree(*cmds);
		*cmds = NULL;
	}

	return rc;
}

static uint32_t iris_change_cmd_hdr(struct iris_parsed_hdr *phdr, int len)
{
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();

	phdr->last = (pcfg->add_last_flag) ? 1 : 0;

	phdr->dlen = (len + IRIS_OCP_HEADER_ADDR_LEN);
	return phdr->dlen;
}


static int32_t iris_write_ip_opt(
		struct dsi_cmd_desc *cmd,
		const struct iris_parsed_hdr *hdr, int32_t mult,
		struct iris_ip_index *pip_index)
{
	uint8_t i = 0;
	uint8_t ip = 0;
	uint8_t opt_id = 0;
	uint8_t cnt = 0;

	if (!hdr || !cmd || !pip_index) {
		IRIS_LOGE("%s(), invalid input parameter.", __func__);
		return -EINVAL;
	}

	ip = hdr->ip & 0xff;
	opt_id = hdr->opt & 0xff;

	if (ip >= LUT_IP_START)
		ip -= LUT_IP_START;

	cnt = pip_index[ip].opt_cnt;

	for (i = 0; i < cnt; i++) {
		if (pip_index[ip].opt[i].cmd == NULL) {
			pip_index[ip].opt[i].cmd = cmd;
			pip_index[ip].opt[i].len = mult;
			pip_index[ip].opt[i].opt_id = opt_id;
			break;
		} else if (pip_index[ip].opt[i].opt_id == opt_id) {
			/*find the right opt_id*/
			pip_index[ip].opt[i].len += mult;
			break;
		}
	}

	if (i == cnt) {
		IRIS_LOGE("%s(), find ip opt fail, ip = 0x%02x opt = 0x%02x.", __func__, ip , opt_id);
		return -EINVAL;
	}

	/*to set link state*/
	if (pip_index[ip].opt[i].link_state == 0xff
		&& pip_index[ip].opt[i].opt_id == opt_id) {
		uint8_t link_state = 0;

		link_state = (hdr->opt >> 8) & 0xff;
		pip_index[ip].opt[i].link_state =
			link_state ? DSI_CMD_SET_STATE_LP : DSI_CMD_SET_STATE_HS;
	}

	return 0;
}

static int32_t iris_create_cmd_hdr(
		struct dsi_cmd_desc *cmd, struct iris_parsed_hdr *hdr)
{
	memset(cmd, 0x00, sizeof(struct dsi_cmd_desc));

	cmd->msg.type = (hdr->dtype & 0xff);
	cmd->post_wait_ms = (hdr->wait & 0xff);
	cmd->last_command = ((hdr->last & 0xff) != 0);
	cmd->msg.tx_len = hdr->dlen;

	IRIS_LOGV("%s(), cmd list: dtype = %0x wait = %0x last = %s dlen = %zu", __func__,
		cmd->msg.type, cmd->post_wait_ms, cmd->last_command?"true":"false", cmd->msg.tx_len);

	return cmd->msg.tx_len;
}

static void iris_modify_parsed_hdr(
		struct iris_parsed_hdr *dest, const struct iris_parsed_hdr *src,
		int i, const int mult)
{
	int pkt_size = 0;
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();
	pkt_size = pcfg->split_pkt_size;

	memcpy(dest, src, sizeof(*src));
	if (i == mult - 1)
		dest->dlen = src->dlen * sizeof(uint32_t) - (mult - 1) * pkt_size;
	else
		iris_change_cmd_hdr(dest, pkt_size);
}


static int iris_write_cmd_hdr(
		const uint8_t *payload, struct dsi_cmd_desc *cmd,
		const struct iris_parsed_hdr *phdr, int mult)
{
	int i = 0;
	struct iris_parsed_hdr tmp_hdr;

	for (i = 0; i < mult; i++) {
		iris_modify_parsed_hdr(&tmp_hdr, phdr, i, mult);

		/*add cmds hdr information*/
		iris_create_cmd_hdr(cmd + i, &tmp_hdr);
	}
	return 0;
}


static void iris_create_cmd_payload(
		uint8_t *payload, const uint8_t *data , int32_t len)
{
	int32_t i = 0;
	uint32_t *pval = NULL;
	uint32_t cnt = 0;

	pval  = (uint32_t *)data;
	cnt = (len) >> 2;
	for (i = 0; i < cnt; i++)
		*(uint32_t *)(payload + (i << 2)) = cpu_to_le32(pval[i]);
}

static void iris_create_cmd_payload_directly(
		uint8_t *payload, const uint8_t *data , int32_t len)
{
	memcpy(payload, data, len);

}

static bool iris_need_direct_send(const struct iris_parsed_hdr *hdr)
{
	if (hdr == NULL) {
		IRIS_LOGE("%s(), invalid input!", __func__);
		return false;
	}

	if (hdr->ip == APP_CODE_LUT) {
		return true;
	}

	return false;
}

static int iris_write_cmd_payload(
		const char *buf, struct dsi_cmd_desc *pdesc,
		const struct iris_parsed_hdr *hdr, int mult)
{
	int i = 0;
	uint32_t dlen = 0;
	uint32_t ocp_type = 0;
	uint32_t base_addr = 0;
	uint32_t pkt_size = 0;
	uint8_t *ptr = NULL;
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();
	pkt_size = pcfg->split_pkt_size;

	ocp_type = iris_get_ocp_type(buf);
	base_addr = iris_get_ocp_base_addr(buf);

	if (mult == 1) {
		dlen = pdesc->msg.tx_len;
		ptr = (uint8_t *)kzalloc(dlen, GFP_KERNEL);
		if (!ptr) {
			IRIS_LOGE("%s Failed to allocate memory", __func__);
			return -ENOMEM;
		}

		if (iris_need_direct_send(hdr)) {
			iris_create_cmd_payload_directly(ptr, buf, dlen);
		} else {
			iris_create_cmd_payload(ptr, buf, dlen);
		}

		pdesc->msg.tx_buf = ptr;
	} else {
		/*remove header and base address*/
		buf += IRIS_OCP_HEADER_ADDR_LEN;
		for (i = 0; i < mult; i++) {
			dlen = pdesc[i].msg.tx_len;

			ptr = (uint8_t *)kzalloc(dlen, GFP_KERNEL);
			if (!ptr) {
				IRIS_LOGE("can not allocate space");
				return -ENOMEM;
			}

			iris_set_ocp_base_addr(ptr, base_addr + i * pkt_size);
			iris_set_ocp_type(ptr, ocp_type);

			if (iris_need_direct_send(hdr)) {
				iris_create_cmd_payload_directly(ptr + IRIS_OCP_HEADER_ADDR_LEN,
					buf, dlen - IRIS_OCP_HEADER_ADDR_LEN);
			} else {
				iris_create_cmd_payload(ptr + IRIS_OCP_HEADER_ADDR_LEN,
					buf, dlen - IRIS_OCP_HEADER_ADDR_LEN);
			}
			/* add payload */
			buf += (dlen - IRIS_OCP_HEADER_ADDR_LEN);
			pdesc[i].msg.tx_buf = ptr;
		}
	}

	IRIS_IF_LOGVV() {
		int len = 0;

		for (i = 0; i < mult; i++) {
			len = (pdesc[i].msg.tx_len > 16) ? 16 : pdesc[i].msg.tx_len;
			print_hex_dump(KERN_ERR, "", DUMP_PREFIX_NONE, 16, 4,
					pdesc[i].msg.tx_buf, len, false);
		}
	}

	return 0;
}

void iris_change_type_addr(
		struct iris_ip_opt * dest, struct iris_ip_opt * src)
{
	int i = 0;
	int mult = 0;
	uint32_t ocp_type = 0;
	uint32_t base_addr = 0;
	uint32_t pkt_size = 0;
	struct iris_cfg *pcfg = NULL;
	const void *buf = NULL;

	pcfg = iris_get_cfg();
	pkt_size = pcfg->split_pkt_size;

	buf = src->cmd->msg.tx_buf;
	mult = dest->len;

	ocp_type = iris_get_ocp_type(buf);
	base_addr = iris_get_ocp_base_addr(buf);

	for (i = 0; i < mult; i++) {
		buf = dest->cmd[i].msg.tx_buf;
		iris_set_ocp_base_addr(buf, base_addr + i * pkt_size);
		iris_set_ocp_type(buf, ocp_type);
		IRIS_LOGD_IF (i == 0) {
			IRIS_LOGD("%s(), change ocp type 0x%08x, change base addr to 0x%08x.",
				 __func__, ocp_type, base_addr);
		}
	}
}


struct iris_ip_opt *iris_find_ip_opt(uint8_t ip, uint8_t opt_id)
{
	int32_t i = 0;
	int32_t type = 0;
	struct iris_ip_opt *popt = NULL;
	struct iris_ip_index *pip_index = NULL;
	struct iris_cfg *pcfg = iris_get_cfg();

	IRIS_LOGV("%s(), ip: %#x, opt: %#x", __func__, ip, opt_id);
	if (iris_is_valid_ip(ip)) {
		IRIS_LOGE("%s(), ip %d is out of maxvalue", __func__, ip);
		return NULL;
	}

	if (ip >= LUT_IP_START) {
		type = IRIS_LUT_PIP_IDX;
		ip -= LUT_IP_START;
	} else {
		type = pcfg->cmd_list_index;
	}

	pip_index = iris_get_ip_idx(type) + ip;

	for (i = 0; i < pip_index->opt_cnt; i++) {
		popt = pip_index->opt + i;
		if (popt->opt_id == opt_id)
			return popt;
	}

	return NULL;
}

static void  iris_print_ipopt_info(struct iris_ip_index  *pip_index)
{
	int32_t i = 0;
	int32_t j = 0;
	int32_t ip_cnt = IRIS_IP_CNT;

	if (iris_get_ip_idx_type(pip_index) == IRIS_LUT_PIP_IDX)
		ip_cnt = LUT_IP_END - LUT_IP_START;

	for (i = 0; i < ip_cnt; i++) {
		for (j = 0; j < pip_index[i].opt_cnt; j++) {
			struct iris_ip_opt *popt = NULL;

			popt = &(pip_index[i].opt[j]);
			IRIS_LOGI("%s(%d), ip = %02x opt_id = %02x",
				__func__, __LINE__, i, popt->opt_id);
			IRIS_LOGI("%s(%d), cmd = %p len = %d  link_state = %0x",
				__func__, __LINE__, popt->cmd, popt->len, popt->link_state);
		}
	}
}

static int32_t iris_add_cmd_to_ipidx(struct iris_buf_len *data,
		struct dsi_cmd_desc *cmds, int pos,
		struct iris_ip_index *pip_index)
{
	int32_t span = 0;
	int32_t i = 0;
	int32_t mult = 0;
	int32_t total = 0;
	uint8_t *buf = NULL;
	int32_t len = 0;
	int32_t hdr_size = 0;

	const uint8_t *payload = NULL;
	struct dsi_cmd_desc *pdesc = NULL;
	const struct iris_parsed_hdr *hdr = NULL;

	buf = (uint8_t *)data->buf;
	len = data->len;

	i = pos;
	while (total < len) {
		hdr = (const struct iris_parsed_hdr *)buf;
		pdesc = &cmds[i];
		payload = buf + sizeof(struct iris_parsed_hdr);
		hdr_size = hdr->dlen * sizeof(uint32_t);
		total += sizeof(struct iris_parsed_hdr) + hdr_size;

		mult = iris_split_mult_pkt(payload, hdr_size);
		IRIS_LOGV_IF(mult > 1) {
			IRIS_LOGV("%s(), mult is: %d.", __func__, mult);
		}

		/*need to first write desc header and then write payload*/
		iris_write_cmd_hdr(payload, pdesc, hdr, mult);
		iris_write_cmd_payload(payload, pdesc, hdr, mult);

		/*write cmd link information*/
		iris_write_ip_opt(pdesc, hdr, mult, pip_index);

		buf += sizeof(struct iris_parsed_hdr) + hdr_size;
		i += mult;
	}
	span = i - pos;

	IRIS_IF_LOGVV() {
		iris_print_ipopt_info(pip_index);
	}

	return span;
}

static int32_t iris_create_ipidx(
		struct iris_buf_len *data, int32_t len,
		struct iris_cmd_statics *pcmd_statics,
		struct iris_ip_index *pip_index)
{
	int32_t i = 0;
	int32_t rc = 0;
	int32_t pos = 0;
	int32_t cnt = 0;
	struct dsi_cmd_desc * cmds = NULL;

	cnt = pcmd_statics->cnt;

	/*create dsi cmd list*/
	rc = iris_alloc_buf(&cmds, pcmd_statics, pip_index);
	if (rc) {
		IRIS_LOGE("create dsi memory failed!");
		return -ENOMEM;
	}

	iris_init_ip_index(pip_index);

	for (i = 0; i < len; i++) {
		if (data[i].len == 0) {
			IRIS_LOGW("data[%d] length is %d.", i, data[i].len);
			continue;
		}
		pos += iris_add_cmd_to_ipidx(&data[i], cmds, pos, pip_index);
	}

	if (cnt != pos) {
		IRIS_LOGE("iris desc is not right, cnt = %d, end = %d.", cnt, pos);
		//return -EINVAL; //CID799214
	}

	return 0;
}

static int32_t iris_cal_hdr_cnt(const struct iris_parsed_hdr *hdr,
		const uint8_t *payload, struct iris_cmd_statics *pcmd_statics)
{
	int mult = 1;
	uint16_t pkt_size = 0;
	struct iris_cfg *pcfg = NULL;
	int32_t hdr_size = 0;

	if (!hdr || !pcmd_statics || !payload) {
		IRIS_LOGE("%s(%d), invalid input parameter!", __func__, __LINE__);
		return -EINVAL;
	}

	pcfg = iris_get_cfg();
	pkt_size = pcfg->split_pkt_size;
	hdr_size = hdr->dlen * sizeof(uint32_t);

	mult = iris_split_mult_pkt(payload, hdr_size);

	/*it will split to mult dsi cmds
	add (mult-1) ocp_header(4 bytes) and ocp_type(4 bytes) */
	pcmd_statics->cnt += mult;
	/*TODO: total len is not used*/
	pcmd_statics->len +=
		sizeof(uint8_t) *
		((mult-1) * IRIS_OCP_HEADER_ADDR_LEN + hdr_size);

	IRIS_LOGV("%s(), dsi cnt = %d len = %d",
		__func__, pcmd_statics->cnt, pcmd_statics->len);

	return 0;
}

static int32_t iris_cal_ip_opt_cnt(const struct iris_parsed_hdr *hdr,
		struct iris_ip_index *pip_index)
{
	uint8_t last;
	uint8_t  ip;

	if (!hdr || !pip_index) {
		IRIS_LOGE("%s(%d), invalid input parameter.", __func__, __LINE__);
		return -EINVAL;
	}

	//CID799198
	last = hdr->last & 0xff;
	ip = hdr->ip & 0xff;

	if (last == 1) {
		if (ip >= LUT_IP_START)
			ip -= LUT_IP_START;
		pip_index[ip].opt_cnt++;
	}

	return 0;
}

static int32_t iris_add_statics_data(const struct iris_parsed_hdr *hdr,
		const char *payload, struct iris_cmd_statics *pcmd_statics,
		struct iris_ip_index *pip_index)
{
	int32_t rc = 0;

	rc = iris_cal_hdr_cnt(hdr, payload, pcmd_statics);
	if (rc)
		goto EXIT_VAL;

	rc = iris_cal_ip_opt_cnt(hdr, pip_index);
	if (rc)
		goto EXIT_VAL;

	return 0;

EXIT_VAL:

	IRIS_LOGE("cmd static is error!");
	return rc;
}

static int32_t iris_verify_dtsi(const struct iris_parsed_hdr *hdr,
	struct iris_ip_index *pip_index)
{
	uint32_t *pval = NULL;
	uint8_t  tmp = 0;
	int32_t rc = 0;
	int32_t type = 0;

	type = iris_get_ip_idx_type(pip_index);

	if (type >= IRIS_DTSI0_PIP_IDX && type <= IRIS_DTSI1_PIP_IDX) {
		if (hdr->ip >= IRIS_IP_CNT) {
			IRIS_LOGE("hdr->ip is  0x%0x out of max ip", hdr->ip);
			rc = -EINVAL;
		} else if (((hdr->opt >> 8) & 0xff)  > 1) {
			IRIS_LOGE("hdr->opt link state not right 0x%0x", hdr->opt);
			rc = -EINVAL;
		}
	} else {
		if (hdr->ip >= LUT_IP_END || hdr->ip < LUT_IP_START) {
			IRIS_LOGE("hdr->ip is  0x%0x out of ip range", hdr->ip);
			rc = -EINVAL;
		}
	}

	switch (hdr->dtype) {
		case MIPI_DSI_DCS_SHORT_WRITE:
		case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		case MIPI_DSI_DCS_READ:
		case MIPI_DSI_DCS_LONG_WRITE:
		case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
		case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
		case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
		case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
		case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
		case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
			break;
		case MIPI_DSI_GENERIC_LONG_WRITE:
			/*judge payload0 for iris header*/
			pval = (uint32_t *)hdr + (sizeof(*hdr) >> 2);
			tmp = *pval & 0x0f;
			if (tmp == 0x00 || tmp == 0x05 || tmp == 0x0c || tmp == 0x08) {
				break;
			} else if (tmp == 0x04) {
				if ((hdr->dlen - 1) % 2 != 0) {
					IRIS_LOGE("dlen is not right = %d", hdr->dlen);
					rc = -EINVAL;
				}
			} else {
				IRIS_LOGE("payload hdr is not right = %0x", *pval);
				rc = -EINVAL;
			}
			break;
		default:
			IRIS_LOGE("dtype is not right %0x", hdr->dtype);
		rc = -EINVAL;
	}

	if (rc) {
		IRIS_LOGE("hdr infor is %#x %#x %#x %#x %#x %#x",
			hdr->dtype, hdr->last,
			hdr->wait, hdr->ip,
			hdr->opt, hdr->dlen);
	}

	return rc;
}


static int32_t iris_parse_panel_type(
		struct device_node *np, struct iris_cfg *pcfg)
{
	const char *data;
	u32 value = 0;
	u8 values[2] = {};
	int32_t rc = 0;

	data = of_get_property(np, "pxlw,panel-type", NULL);
	if (data) {
		if (!strcmp(data, "PANEL_LCD_SRGB"))
			pcfg->panel_type = PANEL_LCD_SRGB;
		else if (!strcmp(data, "PANEL_LCD_P3"))
			pcfg->panel_type = PANEL_LCD_P3;
		else if (!strcmp(data, "PANEL_OLED"))
			pcfg->panel_type = PANEL_OLED;
		else/*default value is 0*/
			pcfg->panel_type = PANEL_LCD_SRGB;
	} else { /*default value is 0*/
		pcfg->panel_type = PANEL_LCD_SRGB;
		IRIS_LOGW("parse panel type failed!");
	}

	rc = of_property_read_u32(np, "pxlw,panel-dimming-brightness", &value);
	if (rc == 0) {
		pcfg->panel_dimming_brightness = value;
	} else {
		/* for V30 panel, 255 may cause panel during exit HDR, and lost TE.*/
		pcfg->panel_dimming_brightness = 250;
		IRIS_LOGW("parse panel dimming brightness failed!");
	}

	rc = of_property_read_u8_array(np, "pxlw,panel-hbm", values, 2);
	if (rc == 0) {
		pcfg->panel_hbm[0] = values[0];
		pcfg->panel_hbm[1] = values[1];
	} else {
		pcfg->panel_hbm[0] = 0xff;
		pcfg->panel_hbm[1] = 0xff;
	}

	return 0;
}

static int32_t iris_parse_chip_version(
		struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t rc = 0;

	rc = of_property_read_u32(np, "pxlw,chip-ver",
			&(pcfg->chip_ver));
	if (rc) {
		IRIS_LOGE("can not get property: pxlw, chip-ver");
		return rc;
	}
	IRIS_LOGE("pxlw,chip-version: %#x", pcfg->chip_ver);

	return rc;
}

static int32_t iris_parse_lut_mode(
		struct device_node *np, struct iris_cfg *pcfg)
{
	const char *data;

	data = of_get_property(np, "pxlw,lut-mode", NULL);
	if (data) {
		if (!strcmp(data, "single"))
			pcfg->lut_mode = SINGLE_MODE;
		else if (!strcmp(data, "interpolation"))
			pcfg->lut_mode = INTERPOLATION_MODE;
		else/*default value is 0*/
			pcfg->lut_mode = INTERPOLATION_MODE;
	} else { /*default value is 0*/
		pcfg->lut_mode = INTERPOLATION_MODE;
		IRIS_LOGI("no lut mode set, use default");
	}
	IRIS_LOGE("pxlw,lut-mode: %d", pcfg->lut_mode);
	return 0;
}

static int32_t iris_parse_lp_control(
			struct device_node *np, struct iris_cfg * pcfg)
{
	int32_t rc = 0;
	u8 vals[3];

	rc = of_property_read_u8_array(np, "pxlw,low-power", vals, 3);
	if (rc) {
		IRIS_LOGE("unable to find pxlw,low-power property, rc=%d",
			rc);
		return 0;
	}

	pcfg->lp_ctrl.dynamic_power = (bool)vals[0];
	pcfg->lp_ctrl.ulps_lp = (bool)vals[1];
	pcfg->lp_ctrl.abyp_enable = (bool)vals[2];
	IRIS_LOGE("parse pxlw,low-power:%d %d %d", vals[0], vals[1], vals[2]);

	return rc;
}

static int32_t iris_parse_split_pkt_info(
			struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t rc = 0;

	rc = of_property_read_u32(np, "pxlw,pkt-payload-size",
			&(pcfg->split_pkt_size));
	if (rc) {
		IRIS_LOGE("can not get property: pxlw,pkt-payload-size");
		return rc;
	}
	IRIS_LOGE("pxlw,split-pkt-payload-size: %d", pcfg->split_pkt_size);

	rc = of_property_read_u32(np, "pxlw,last-for-per-pkt",
			&(pcfg->add_on_last_flag));
	if (rc) {
		IRIS_LOGE("can not get property:pxlw,last-for-per-pkt");
		pcfg->add_on_last_flag = DSI_CMD_ONE_LAST_FOR_ONE_PKT;
	}
	rc = of_property_read_u32(np, "pxlw,pt-last-for-per-pkt",
			&(pcfg->add_pt_last_flag));
	if (rc) {
		IRIS_LOGE("can not get property:pxlw,pt-last-for-per-pkt");
		pcfg->add_pt_last_flag = DSI_CMD_ONE_LAST_FOR_MULT_IPOPT;
	}
	IRIS_LOGE("pxlw,add-last-for-split-pkt: %d, pxlw,add-pt-last-for-split-pkt: %d",
		 pcfg->add_on_last_flag, pcfg->add_pt_last_flag);

	pcfg->add_last_flag = pcfg->add_on_last_flag;

	return rc;
}

static int32_t iris_parse_color_temp_info(struct device_node *np, struct iris_cfg * pcfg)
{
	int32_t rc = 0;

	rc = of_property_read_u32(np, "pxlw,min-color-temp", &(pcfg->min_color_temp));
	if (rc) {
		IRIS_LOGE("can not get property: pxlw,min-color-temp");
		return rc;
	}
	IRIS_LOGI("pxlw,min-color-temp: %d", pcfg->min_color_temp);

	rc = of_property_read_u32(np, "pxlw,max-color-temp", &(pcfg->max_color_temp));
	if (rc) {
		IRIS_LOGE("can not get property:pxlw,max-color-temp");
		return rc;
	}
	IRIS_LOGI("pxlw,max-color-temp: %d", pcfg->max_color_temp);

	return rc;
}



static int32_t iris_oprt_statics_data(
		struct iris_buf_len * data, int num,
		struct iris_cmd_statics *pcmd_statics,
		struct iris_ip_index *pip_index)
{
	int32_t i = 0;
	int32_t len;
	int32_t hdr_size = 0;
	int32_t rc = 0;
	const uint8_t *bp = NULL;
	const struct iris_parsed_hdr *hdr = NULL;

	if (data == NULL || pcmd_statics == NULL || pip_index == NULL) {
		IRIS_LOGE("%s(), invalid input!", __func__);
		return -EINVAL;
	}

	for (i = 0; i < num; i++) {
		if (data[i].len == 0) {
			IRIS_LOGW("data length is = %d", data[i].len);
			continue;
		}

		bp = data[i].buf;
		len = data[i].len;
		while (len >= sizeof(struct iris_parsed_hdr)) {
			hdr = (const struct iris_parsed_hdr *)bp;
			len -= sizeof(struct iris_parsed_hdr);
			if (hdr->dlen > (len >> 2)) {
				IRIS_LOGE("%s: length error, ip = 0x%02x opt=0x%02x, len=%d",
					__func__, hdr->ip, hdr->opt, hdr->dlen);
				return -EINVAL;
			}

			IRIS_IF_LOGVV() {
				rc = iris_verify_dtsi(hdr, pip_index);
				if (rc) {
					IRIS_LOGE("%s(%d), verify dtis return: %d", __func__, __LINE__, rc);
					return rc;
				}
			}

			IRIS_LOGV("hdr info, type: 0x%02x, last: 0x%02x, wait: 0x%02x,"
				" ip: 0x%02x, opt: 0x%02x, len: %d.",
				hdr->dtype, hdr->last, hdr->wait,
				hdr->ip, hdr->opt, hdr->dlen);

			//payload
			bp += sizeof(struct iris_parsed_hdr);

			/*change to uint8_t length*/
			//hdr->dlen *= sizeof(uint32_t);
			hdr_size = hdr->dlen * sizeof(uint32_t);

			rc = iris_add_statics_data(hdr, bp, pcmd_statics, pip_index);
			if (rc) {
				IRIS_LOGE("add static data error");
				return rc;
			}

			bp += hdr_size;
			len -= hdr_size;
		}
	}

	return rc;
}


static int32_t iris_alloc_buf_for_dtsi_cmds(
		const struct device_node *np, const uint8_t *key, uint8_t ** buf)
{
	int32_t len = 0;
	int32_t blen = 0;
	const void * ret = NULL;

	ret = of_get_property(np, key, &blen);
	if (!ret) {
		IRIS_LOGE("%s: failed for parsing %s", __func__, key);
		return -EINVAL;
	}

	if (blen % 4 != 0) {
		IRIS_LOGE("lenght = %d is not multpile of 4", blen);
		return -EINVAL;
	}

	len = sizeof(char) * blen;
	*buf = kzalloc(len, GFP_KERNEL);
	if (!*buf) {
		IRIS_LOGE("can not kzalloc memory");
		return  -ENOMEM;
	}
	return len;
}


static int32_t iris_write_dtsi_cmds_to_buf(
		const struct device_node *np, const uint8_t * key,
		uint8_t ** buf, int len)
{
	int32_t rc = 0;

	rc = of_property_read_u32_array(np, key,
			(uint32_t *)(*buf), len >> 2);
	if (rc != 0) {
		IRIS_LOGE("%s(%d), read array is not right", __func__, __LINE__);
		return -EINVAL;
	}
	return rc;
}


static void iris_free_buf_for_dtsi_cmds(uint8_t **buf)
{
	if (*buf) {
		kfree(*buf);
		*buf = NULL;
	}
}


int32_t iris_attach_cmd_to_ipidx(struct iris_buf_len *data,
		int32_t cnt, struct iris_ip_index *pip_index, uint32_t *cmd_cnt)
{
	int32_t rc = 0;
	struct iris_cmd_statics cmd_statics;

	memset(&cmd_statics, 0x00, sizeof(cmd_statics));

	rc = iris_oprt_statics_data(data, cnt, &cmd_statics, pip_index);
	if (rc) {
		IRIS_LOGE("fail to parse dtsi/lut cmd list!");
		return rc;
	}

	*cmd_cnt = cmd_statics.cnt;

	rc = iris_create_ipidx(data, cnt, &cmd_statics, pip_index);
	return rc;
}

//!TODO may need to first modify.
static int32_t iris_parse_cmds(const struct device_node *lightup_node, uint32_t cmd_index)
{
	int32_t len = 0;
	int32_t cnt = 0;
	int32_t rc = 0;
	uint8_t *dtsi_buf = NULL;
	struct iris_ip_index *pip_index = NULL;
	struct iris_buf_len data[1];
	const uint8_t *key = "pxlw,iris-cmd-list";
	struct iris_cfg *pcfg = iris_get_cfg();

	if (IRIS_DTSI1_PIP_IDX == cmd_index) {
		key = "pxlw,iris-cmd-list-1";
	}
	memset(data, 0x00, sizeof(data));

	// need to keep dtsi buf and release after used
	len = iris_alloc_buf_for_dtsi_cmds(lightup_node, key, &dtsi_buf);
	if (len <= 0) {
		IRIS_LOGE("can not malloc space for dtsi cmd");
		return -ENOMEM;
	}

	rc = iris_write_dtsi_cmds_to_buf(lightup_node, key, &dtsi_buf, len);
	if (rc) {
		IRIS_LOGE("cant not write dtsi cmd to buf");
		goto FREE_DTSI_BUF;
	}
	data[0].buf = dtsi_buf;
	data[0].len = len;

	pip_index = iris_get_ip_idx(cmd_index);
	cnt = sizeof(data)/sizeof(data[0]);
	rc = iris_attach_cmd_to_ipidx(data, cnt, pip_index, &pcfg->none_lut_cmds_cnt);

FREE_DTSI_BUF:
	iris_free_buf_for_dtsi_cmds(&dtsi_buf);

	return rc;
}

static void iris_add_cmd_seq_common(
		struct iris_ctrl_opt *ctrl_opt, int len, const uint8_t *pdata)
{
	int32_t i = 0;
	int32_t span = 3;
	uint8_t ip = 0;
	uint8_t opt_id = 0;
	uint8_t skip_last = 0;

	for (i = 0; i < len; i++) {
		ip = pdata[span * i];
		opt_id = pdata[span * i + 1];
		skip_last = pdata[span * i + 2];

		ctrl_opt[i].ip = ip & 0xff;
		ctrl_opt[i].opt_id = opt_id & 0xff;
		ctrl_opt[i].skip_last = skip_last & 0xff;

		IRIS_IF_LOGV() {
			IRIS_LOGE("ip = %d opt = %d  skip=%d",
				ip, opt_id, skip_last);
		}
	}
}

static int32_t iris_alloc_cmd_seq_common(
		struct iris_ctrl_seq  *pctrl_seq, int32_t seq_cnt)
{
	pctrl_seq->ctrl_opt = kmalloc(seq_cnt * sizeof(struct iris_ctrl_seq),
						GFP_KERNEL);
	if (pctrl_seq->ctrl_opt == NULL) {
		IRIS_LOGE("can not malloc space for pctrl opt");
		return -ENOMEM;
	}
	pctrl_seq->cnt = seq_cnt;
	return 0;
}

static int32_t iris_parse_cmd_seq_data(
		struct device_node *np, const uint8_t *key,
		const uint8_t **pval)
{
	const uint8_t *pdata = NULL;
	int32_t blen = 0;
	int32_t seq_cnt = 0;
	int32_t span = 3;

	pdata = of_get_property(np, key, &blen);
	if (!pdata) {
		IRIS_LOGE("%s %s is error", __func__, key);
		return -EINVAL;
	}

	seq_cnt =  (blen / span);
	if (blen == 0 || blen != span * seq_cnt) {
		IRIS_LOGE("parse %s len is not right = %d", key, blen);
		return -EINVAL;
	}

	*pval = pdata;

	return seq_cnt;
}


static int32_t iris_parse_cmd_seq_common(
		struct device_node *np, const uint8_t *pre_key,
		const uint8_t *key, struct iris_ctrl_seq *pctrl_seq)
{
	int32_t pre_len = 0;
	int32_t len = 0;
	int32_t sum = 0;
	int32_t rc = 0;
	const uint8_t *pdata = NULL;
	const uint8_t *pre_pdata = NULL;

	pre_len = iris_parse_cmd_seq_data(np, pre_key, &pre_pdata);
	if (pre_len <= 0)
		return -EINVAL;

	len = iris_parse_cmd_seq_data(np, key, &pdata);
	if (len <= 0)
		return -EINVAL;

	sum = pre_len + len;

	rc = iris_alloc_cmd_seq_common(pctrl_seq, sum);
	if (rc != 0)
		return rc;

	iris_add_cmd_seq_common(pctrl_seq->ctrl_opt, pre_len, pre_pdata);
	iris_add_cmd_seq_common(&pctrl_seq->ctrl_opt[pre_len], len, pdata);

	return rc;
}


static int32_t iris_parse_cmd_seq(
		struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t rc = 0;
	uint8_t *pre0_key = "pxlw,iris-lightup-sequence-pre0";
	uint8_t *pre1_key = "pxlw,iris-lightup-sequence-pre1";
	uint8_t *key = "pxlw,iris-lightup-sequence";

	rc = iris_parse_cmd_seq_common(np, pre0_key, key, pcfg->ctrl_seq);
	if (rc != 0)
		return rc;

	return iris_parse_cmd_seq_common(np, pre1_key, key, pcfg->ctrl_seq + 1);
}

/*use for debug cont-splash lk part*/
static int32_t iris_parse_cmd_seq_cont_splash(
		struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t rc = 0;
	uint8_t *pre0_key = "pxlw,iris-lightup-sequence-pre0";
	uint8_t *pre1_key = "pxlw,iris-lightup-sequence-pre1";
	uint8_t *key = "pxlw,iris-lightup-sequence-cont-splash";

	rc = iris_parse_cmd_seq_common(np, pre0_key, key, pcfg->ctrl_seq_cs);
	if (rc != 0)
		return rc;

	return iris_parse_cmd_seq_common(np, pre1_key,
				key, pcfg->ctrl_seq_cs + 1);
}

static int32_t iris_parse_mode_switch_seq(
		struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t rc = 0;
	int32_t len = 0;
	const uint8_t *key = "pxlw,iris-mode-switch-sequence";
	const uint8_t *key_1 = "pxlw,iris-mode-switch-sequence-1";
	const uint8_t *pdata = NULL;

	len = iris_parse_cmd_seq_data(np, key, &pdata);
	if (len <= 0) {
		IRIS_LOGI("%s, [optional] without mode switch sequence, len %d", __func__, len);
		return 0;
	}

	rc = iris_alloc_cmd_seq_common(&pcfg->mode_switch_seq, len);
	if (rc != 0) {
		IRIS_LOGE("%s, alloc buffer for mode switch sequence failed, return %d", __func__, rc);
		return rc;
	}

	iris_add_cmd_seq_common(pcfg->mode_switch_seq.ctrl_opt, len, pdata);

	len = iris_parse_cmd_seq_data(np, key_1, &pdata);
	if (len <= 0) {
		IRIS_LOGI("%s, [optional] without mode switch sequence-1, len %d", __func__, len);
		return 0;
	}

	rc = iris_alloc_cmd_seq_common(&pcfg->mode_switch_seq_1, len);
	if (rc != 0) {
		IRIS_LOGE("%s, alloc buffer for mode switch sequence-1 failed, return %d", __func__, rc);
		return rc;
	}

	iris_add_cmd_seq_common(pcfg->mode_switch_seq_1.ctrl_opt, len, pdata);

	return rc;
}

static int32_t iris_parse_tx_mode(
		struct device_node *np,
		struct dsi_panel *panel, struct iris_cfg *pcfg)
{
	int32_t rc = 0;
	u8 tx_mode;
	pcfg->rx_mode = panel->panel_mode;
	pcfg->tx_mode = panel->panel_mode;
	IRIS_LOGE("%s, panel_mode = %d", __func__, panel->panel_mode);
	rc = of_property_read_u8(np, "pxlw,iris-tx-mode", &tx_mode);
	if (!rc) {
		IRIS_LOGE("get property: pxlw, iris-tx-mode: %d", tx_mode);
		//pcfg->tx_mode = tx_mode;
	}
	if (pcfg->rx_mode == pcfg->tx_mode)
		pcfg->pwil_mode = PT_MODE;
	else
		pcfg->pwil_mode = RFB_MODE;
	IRIS_LOGI("%s, pwil_mode: %d", __func__, pcfg->pwil_mode);
	return 0;
}

static int32_t iris_parse_extra_info(
		struct device_node *np,
		struct iris_cfg *pcfg)
{
	int32_t rc = 0;
	u32 loop_back_mode = 0;
	u32 loop_back_mode_res = 0;

	rc = of_property_read_u32(np, "pxlw,loop-back-mode", &loop_back_mode);
	if (!rc)
		IRIS_LOGE("get property: pxlw, loop-back-mode: %d", loop_back_mode);
	pcfg->loop_back_mode = loop_back_mode;

	rc = of_property_read_u32(np, "pxlw,loop-back-mode-res", &loop_back_mode_res);
	if (!rc)
		IRIS_LOGE("get property: pxlw, loop-back-mode-res: %d", loop_back_mode_res);
	pcfg->loop_back_mode_res = loop_back_mode_res;

	return 0;
}

static int32_t iris_parse_frc_setting(
		struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t rc = 0;
	pcfg->frc_enable = false;
	pcfg->frc_setting.memc_level = 3;
	pcfg->frc_setting.memc_osd = 0;
	pcfg->frc_setting.mv_buf_num = 7;
	pcfg->frc_setting.in_fps = 24;
	pcfg->frc_setting.mv_baseaddr = 0x002c0000; // 7 MV buffers

	// Move panel timing parse to iris_pq_parameter_init().
	/* panel_te, ap_te get too late in IrisService, add pxlw,panel-te and pxlw,ap-te in dtsi */
	pcfg->panel_te = 60;
	of_property_read_u32(np, "pxlw,panel-te", &(pcfg->panel_te));
	pcfg->ap_te = 60;
	of_property_read_u32(np, "pxlw,ap-te", &(pcfg->ap_te));

	rc = of_property_read_u32(np, "pxlw,iris-me-hres",
			&(pcfg->frc_setting.memc_hres));
	if (rc) {
		IRIS_LOGE("can not get property: pxlw, iris-me-hres");
		return rc;
	}
	rc = of_property_read_u32(np, "pxlw,iris-me-vres",
			&(pcfg->frc_setting.memc_vres));
	if (rc) {
		IRIS_LOGE("can not get property: pxlw, iris-me-vres");
		return rc;
	}
	rc = of_property_read_u32(np, "pxlw,iris-memc-dsc-bpp",
			&(pcfg->frc_setting.memc_dsc_bpp));
	if (rc) {
		IRIS_LOGE("can not get property: pxlw, iris-memc-dsc-bpp");
		return rc;
	}

	pcfg->frc_enable = true;

	return rc;
}

static int32_t iris_ip_statics_cal(
		const uint8_t *data, int32_t len, int32_t *pval)
{
	int tmp = 0;

	int i = 0;
	int j = 0;

	if (data == NULL || len == 0 || pval == NULL) {
		IRIS_LOGE("data is null or len = %d", len);
		return -EINVAL;
	}

	tmp = data[0];
	len = len >> 1;

	for (i = 0; i < len; i++) {
		if (tmp == data[2 * i]) {
			pval[j]++;
		} else {
			tmp = data[2 * i];
			j++;
			pval[j]++;
		}
	}

	/*j begin from 0*/
	return (j + 1);

}


static int32_t iris_alloc_pq_init_space(
		struct iris_cfg *pcfg, const uint8_t *pdata, int32_t blen)
{
	int32_t i = 0;
	int32_t len = 0;
	int32_t ip_cnt = 0;
	int32_t rc = 0;
	int32_t *ptr = NULL;
	struct iris_pq_init_val *pinit_val = NULL;

	pinit_val = &(pcfg->pq_init_val);

	if (pdata == NULL || blen == 0) {
		IRIS_LOGE("pdata is %p, blen = %0x", pdata, blen);
		return -EINVAL;
	}

	len = sizeof(*ptr) * (blen >> 1);
	ptr = kmalloc(len, GFP_KERNEL);
	if (ptr == NULL) {
		IRIS_LOGE("can not malloc space for ptr");
		return -EINVAL;
	}
	memset(ptr, 0x00, len);

	ip_cnt = iris_ip_statics_cal(pdata, blen, ptr);
	if (ip_cnt <= 0) {
		IRIS_LOGE("can not static ip option");
		rc = -EINVAL;
		goto EXIT_FREE;
	}

	pinit_val->ip_cnt = ip_cnt;
	len = sizeof(struct iris_pq_ipopt_val) * ip_cnt;
	pinit_val->val = kmalloc(len, GFP_KERNEL);
	if (pinit_val->val == NULL) {
		IRIS_LOGE("can not malloc pinit_val->val");
		rc = -EINVAL;
		goto EXIT_FREE;
	}

	for (i = 0; i < ip_cnt; i++) {
		pinit_val->val[i].opt_cnt = ptr[i];
		len = sizeof(uint8_t) * ptr[i];
		pinit_val->val[i].popt = kmalloc(len, GFP_KERNEL);
	}

EXIT_FREE:
	kfree(ptr);
	ptr = NULL;

	return rc;

}

static int32_t iris_parse_pq_default_params(
		struct device_node *np, struct iris_cfg *pcfg)
{
	int32_t i = 0;
	int32_t j = 0;
	int32_t k = 0;
	int32_t blen = 0;
	int32_t rc = 0;
	uint8_t *key = "pxlw,iris-pq-default-val";
	const uint8_t *pdata = NULL;
	struct iris_pq_init_val *pinit_val = NULL;

	pinit_val = &(pcfg->pq_init_val);

	pdata = of_get_property(np, key, &blen);
	if (!pdata) {
		IRIS_LOGE("%s pxlw,iris-pq-default-val fail", __func__);
		return -EINVAL;
	}

	rc = iris_alloc_pq_init_space(pcfg, pdata, blen);
	if (rc) {
		IRIS_LOGE("malloc error");
		return rc;
	}

	for (i = 0; i < pinit_val->ip_cnt; i++) {
		struct iris_pq_ipopt_val *pval = &(pinit_val->val[i]);

		pval->ip = pdata[k++];
		for (j = 0; j < pval->opt_cnt; j++) {
			pval->popt[j] = pdata[k];
				k += 2;
		}
		/*need to skip one*/
		k -= 1;
	}

	IRIS_IF_LOGV() {
		IRIS_LOGE("ip_cnt = %0x", pinit_val->ip_cnt);
		for (i = 0; i < pinit_val->ip_cnt; i++) {
			char ptr[256];
			int32_t len = 0;
			int32_t sum = 256;
			struct iris_pq_ipopt_val *pval = &(pinit_val->val[i]);

			snprintf(ptr, sum, "ip is %0x opt is ", pval->ip);
			for (j = 0; j < pval->opt_cnt; j++) {
				len = strlen(ptr);
				sum -= len;
				snprintf(ptr + len, sum, "%0x ", pval->popt[j]);
			}
			IRIS_LOGE("%s", ptr);
		}
	}
	return rc;
}

static int iris5_parse_pwr_entries(struct dsi_display *display)
{
	int32_t rc = 0;
	char *supply_name = NULL;
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (!display || !display->panel)
		return -EINVAL;

	if (!strcmp(display->panel->type, "primary")) {
		supply_name = "qcom,iris-supply-entries";

		rc = dsi_pwr_of_get_vreg_data(&display->panel->utils,
				&pcfg->iris_power_info, supply_name);
		if (rc) {
			rc = -EINVAL;
			IRIS_LOGE("%s pwr enters error", __func__);
		}
	}
	return rc;
}

static void __cont_splash_work_handler(struct work_struct *work)
{
	// struct iris_cfg *pcfg = iris_get_cfg();
	// bool done = false;

	// do {
	// 	done = iris_abypass_switch_proc(pcfg->display, ANALOG_BYPASS_MODE, false, true);
	// 	if (done)
	// 		done = iris_abypass_switch_proc(pcfg->display, PASS_THROUGH_MODE, false, true);
	// } while (!done);
}

// int iris5_parse_params(
//		struct device_node *np, struct dsi_panel *panel)
int iris5_parse_params(struct dsi_display *display)
{
	int32_t rc = 0;
	struct device_node *lightup_node = NULL;
	struct iris_cfg *pcfg = NULL;
	//int i;

	pcfg = iris_get_cfg();
	pcfg->valid = 1;	/* empty */

	IRIS_LOGI("%s(%d), enter.", __func__, __LINE__);
	if (!display || !display->pdev->dev.of_node || !display->panel_node) {
		IRIS_LOGE("the param is null");
		return -EINVAL;
	}

	if (display->panel->is_secondary) {
		// Copy valid flag to 2nd iris cfg.
		struct iris_cfg *pcfg1 = iris_get_cfg_by_index(DSI_PRIMARY);
		pcfg->valid = pcfg1->valid;
		return 0;
	}

	spin_lock_init(&pcfg->iris_lock);
	mutex_init(&pcfg->mutex);
	mutex_init(&pcfg->lb_mutex);
	mutex_init(&pcfg->lock_send_pkt);
	init_completion(&pcfg->frame_ready_completion);

	// FIXME: what is the max count?
	//for (i = 0; i < of_property_count_u32_elems(display->pdev->dev.of_node, "pxlw,iris-lightup-config"); i++) {
	//	lightup_node = of_parse_phandle(display->pdev->dev.of_node, "pxlw,iris-lightup-config", i);
	//	if (!lightup_node)
	//		break;
	//	// length is "pxlw,mdss_iris_cfg_"
	//	if (strstr(display->panel_node->name, lightup_node->name + 19) != NULL)
	//		break;
	//	lightup_node = NULL;
	//}

	lightup_node = of_parse_phandle(display->pdev->dev.of_node, "pxlw,iris-lightup-config", 0);
	if (!lightup_node) {
		IRIS_LOGE("can not find pxlw,iris-lightup-config node");
		return -EINVAL;
	}
	IRIS_LOGI("Lightup node: %s", lightup_node->name);

	rc = iris_parse_split_pkt_info(lightup_node, pcfg);
	if (rc) {
		/*use 64 split packet and do not add last for every packet.*/
		pcfg->split_pkt_size = 64;
		pcfg->add_last_flag = 0;
	}

	rc = iris_parse_color_temp_info(lightup_node, pcfg);
	if (rc) {
		/*use 2500K~7500K if do not define in dtsi*/
		pcfg->min_color_temp= 2500;
		pcfg->max_color_temp= 7500;
	}

	rc = iris_parse_cmds(lightup_node, IRIS_DTSI0_PIP_IDX);
	if (rc) {
		IRIS_LOGE("%s, parse cmds list failed", __func__);
		return -EINVAL;
	}

	rc = iris_parse_cmds(lightup_node, IRIS_DTSI1_PIP_IDX);
	if (rc) {
		IRIS_LOGI("%s, [optional] have not cmds list 1", __func__);
	}

	rc = iris_parse_cmd_seq(lightup_node, pcfg);
	if (rc) {
		IRIS_LOGE("parse cmd seq error");
		return -EINVAL;
	}

	rc = iris_parse_mode_switch_seq(lightup_node, pcfg);
	if (rc) {
		IRIS_LOGI("%s, [optional] have not mode switch sequence", __func__);
	}

	rc = iris_parse_pq_default_params(lightup_node, pcfg);
	if (rc) {
		IRIS_LOGE("parse pq init error");
		return -EINVAL;
	}

	rc = iris_parse_panel_type(lightup_node, pcfg);
	if (rc) {
		IRIS_LOGE("parse panel type error");
		return -EINVAL;
	}

	rc = iris_parse_lut_mode(lightup_node, pcfg);

	rc = iris_parse_chip_version(lightup_node, pcfg);
	if (rc) {
		IRIS_LOGE("parse chip ver error");
		return -EINVAL;
	}

	rc = iris_parse_lp_control(lightup_node, pcfg);
	if (rc) {
		IRIS_LOGE("parse low power control error");
		return -EINVAL;
	}

	rc = iris_parse_cmd_seq_cont_splash(lightup_node, pcfg);
	if (rc) {
		IRIS_LOGE("parse cont splash cmd seq error");
		return -EINVAL;
	}

	rc = iris_parse_frc_setting(lightup_node, pcfg);
	if (rc) {
		IRIS_LOGE("FRC not ready!");
	}

	rc = iris_parse_tx_mode(lightup_node, display->panel, pcfg);
	if (rc) {
		IRIS_LOGE("no set iris tx mode!");
	}

	rc = iris5_parse_pwr_entries(display);
	if (rc)
		IRIS_LOGE("pwr entries error\n");

	iris_parse_extra_info(lightup_node, pcfg);

	INIT_WORK(&pcfg->cont_splash_work, __cont_splash_work_handler);

	pcfg->valid = 2;	/* parse ok */
	IRIS_LOGI("%s(%d), exit.", __func__, __LINE__);

	return 0;
}


struct iris_pq_ipopt_val *iris_get_cur_ipopt_val(uint8_t ip)
{
	int i = 0;
	struct iris_cfg *pcfg = NULL;
	struct iris_pq_init_val *pinit_val = NULL;

	pcfg = iris_get_cfg();
	pinit_val = &(pcfg->pq_init_val);

	for (i = 0; i < pinit_val->ip_cnt; i++) {
		struct iris_pq_ipopt_val  *pq_ipopt_val = NULL;

		pq_ipopt_val = pinit_val->val + i;
		if (ip == pq_ipopt_val->ip)
			return pq_ipopt_val;
	}
	return NULL;
}


void iris_out_cmds_buf_reset(void)
{
	int sum = 0;
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();

	sum = pcfg->none_lut_cmds_cnt  + pcfg->lut_cmds_cnt;
	memset(pcfg->iris_cmds.iris_cmds_buf, 0x00,
			sum * sizeof(struct dsi_cmd_desc));
	pcfg->iris_cmds.cmds_index = 0;
}


int32_t iris_dsi_get_cmd_comp(
		int32_t ip,
		int32_t opt_index, struct iris_cmd_comp *pcmd_comp)
{
	int32_t rc = 0;
	struct iris_ip_opt *opt = NULL;

	rc = iris_is_valid_ip(ip);
	if (rc) {
		IRIS_LOGE("%s(), invalid ip: %#x", __func__, ip);
		return rc;
	}

	opt = iris_find_ip_opt(ip, opt_index);
	if (!opt) {
		IRIS_LOGE("%s(), can not find popt, ip: %#x, opt: %#x",
			__func__, ip, opt_index);
		return -EINVAL;
	}

	pcmd_comp->cmd = opt->cmd;
	pcmd_comp->cnt = opt->len;
	pcmd_comp->link_state = opt->link_state;
	IRIS_LOGV("%s(), opt count: %d, link state: %#x",
		__func__, pcmd_comp->cnt, pcmd_comp->link_state);

	return 0;
}

void iris_dump_packet(u8 *data, int size)
{
	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_NONE, 16, 4, data, size, false);
}

void iris_print_cmds(struct dsi_cmd_desc *p, int num, int state)
{
	int i = 0;
	int j = 0;
	int len = 0;
	int dlen = 0;
	uint8_t *arr = NULL;
	uint8_t *ptr = NULL;
	uint8_t *ptr_tx = NULL;
	struct dsi_cmd_desc *pcmd = NULL;

	IRIS_LOGI("%s cmd len = %d, state = %s",__func__, num,
			 (state == DSI_CMD_SET_STATE_HS) ? "high speed" : "low power");

	for (i = 0; i < num; i++) {
		pcmd = p + i;
		dlen = pcmd->msg.tx_len;
		len = 3 * dlen + 23; //3* 7(dchdr) + 1(\n) + 1 (0)
		arr = (uint8_t *)kmalloc(len * sizeof(uint8_t), GFP_KERNEL);
		if (!arr) {
			IRIS_LOGE("the malloc is error");
			return;
		}
		memset(arr, 0x00, sizeof(uint8_t)* len);

		ptr = arr;
		ptr_tx = (uint8_t *) pcmd->msg.tx_buf;
		len = sprintf(ptr, "\" %02X", pcmd->msg.type);
		ptr += len;
		for (j = 0; j < dlen; j++) {
			len = sprintf(ptr, " %02X", ptr_tx[j]);
			ptr += len;
		}
		sprintf(ptr, "\\n\"");
		IRIS_LOGE("%s", arr);

		if (pcmd->post_wait_ms > 0)
			IRIS_LOGE("\" FF %02X\\n\"", pcmd->post_wait_ms);

		kfree(arr);
		arr = NULL;
	}
}

static void iris_print_specific_cmds(struct dsi_cmd_desc *p, int num)
{
	int i = 0;
	int j = 0;
	int value_count = 0;
	int print_count = 0;
	struct dsi_cmd_desc *pcmd = NULL;
	uint32_t *pval = NULL;

	IRIS_IF_NOT_LOGD() {
		return;
	}

	IRIS_LOGD("%s(), package count in cmd list: %d", __func__, num);
	for (i=0; i<num; i++) {
		pcmd = p + i;
		value_count = pcmd->msg.tx_len/sizeof(uint32_t);
		print_count = value_count;
		if (value_count > 16) {
			print_count = 16;
		}
		pval = (uint32_t *)pcmd->msg.tx_buf;
		if (i == 0 || i == num-1) {
			IRIS_LOGD("%s(), package: %d, type: 0x%02x, last: %s, channel: 0x%02x, flags: 0x%04x,"
				" wait: 0x%02x, send size: %zu.", __func__, i,
				pcmd->msg.type, pcmd->last_command?"true":"false", pcmd->msg.channel,
				pcmd->msg.flags, pcmd->post_wait_ms, pcmd->msg.tx_len);

			IRIS_IF_NOT_LOGV() {
				continue;
			}

			IRIS_LOGV("%s(), payload value count: %d, print count: %d, ocp type: 0x%08x, addr: 0x%08x",
				__func__, value_count, print_count, pval[0], pval[1]);
			for (j=2; j<print_count; j++) {
				IRIS_LOGV("0x%08x", pval[j]);
			}

			if (i == num-1 && value_count > 4 && print_count != value_count) {
				IRIS_LOGV("%s(), payload tail: 0x%08x, 0x%08x, 0x%08x, 0x%08x.", __func__,
					pval[value_count-4], pval[value_count-3],
					pval[value_count-2], pval[value_count-1]);
			}
		}
	}
}


void iris_print_desc_info(struct dsi_cmd_desc *cmds, int32_t cnt)
{
	int i = 0;
	int len = 0;

	IRIS_LOGI("cmds cnt = %d", cnt);

	for (i = 0; i < cnt; i++) {
		len = cmds[i].msg.tx_len > 16 ? 16 : cmds[i].msg.tx_len;

		IRIS_LOGI("%s(), cmd[%d], type: 0x%02x wait: %d ms, last: %s, len: %zu",
			__func__, i, cmds[i].msg.type, cmds[i].post_wait_ms,
			cmds[i].last_command?"true":"false", cmds[i].msg.tx_len);
		print_hex_dump(KERN_ERR, "", DUMP_PREFIX_NONE, 16, 4,
			cmds[i].msg.tx_buf, len, false);
	}
}


void iris_print_none_lut_cmds_for_lk(
		struct dsi_cmd_desc *cmds, int32_t cnt,
		int32_t wait, int32_t link_state)
{
	if (IRIS_CONT_SPLASH_LK != iris_get_cont_splash_type())
		return;

	//restore the last cmd wait time
	if (wait != 0)
		cmds[cnt-1].post_wait_ms = 1;

	iris_print_cmds(cmds, cnt, link_state);
}

static int32_t iris_i2c_send_ocp_cmds(
						struct dsi_panel *panel,
						struct iris_cmd_comp *pcmd_comp)
{
	int i = 0;
	int ret = 0;
	bool is_burst;
	bool is_allburst;
	int len = 0;
	uint32_t *payload = NULL;
	uint32_t header = 0;
	struct iris_i2c_msg *msg = NULL;
	uint32_t iris_i2c_msg_num = 0;


	is_allburst = true;
	for (i = 0; i < pcmd_comp->cnt; i++) {
		is_burst = iris_is_direct_bus(pcmd_comp->cmd[i].msg.tx_buf);
		if (is_burst == false) {
			is_allburst = false;
			break;
		}
	}


	if (is_allburst == false) {
		for (i = 0; i < pcmd_comp->cnt; i++ ) {
			header = *(uint32_t *)(pcmd_comp->cmd[i].msg.tx_buf);
			payload = (uint32_t *)(pcmd_comp->cmd[i].msg.tx_buf) + 1;
			len = (pcmd_comp->cmd[i].msg.tx_len >> 2) - 1;
			is_burst = iris_is_direct_bus(pcmd_comp->cmd[i].msg.tx_buf);
			if (is_burst) {
				if ((header & 0x0f) == 0x0c)
					iris_i2c_direct_write(payload, len-1, header);
				else
					iris_i2c_ocp_burst_write(payload, len-1);
			} else {
				iris_i2c_ocp_single_write(payload, len/2);
			}
		}

		return 0;
	} else {
		iris_i2c_msg_num = pcmd_comp->cnt;
		msg = kmalloc(sizeof(struct iris_i2c_msg) * iris_i2c_msg_num + 1, GFP_KERNEL);
		if (NULL == msg) {
			IRIS_LOGE("[iris5] %s: allocate memory fails", __func__);
			return -EINVAL;
		}
		for (i = 0; i < iris_i2c_msg_num; i++) {
			msg[i].payload = (uint32_t *)(pcmd_comp->cmd[i].msg.tx_buf) + 1;
			msg[i].len = (pcmd_comp->cmd[i].msg.tx_len >> 2) - 1;
			msg[i].base_addr = *(uint32_t *)(pcmd_comp->cmd[i].msg.tx_buf);
		}
		ret = iris_i2c_burst_write(msg, iris_i2c_msg_num);
		kfree(msg);
		return ret;
	}
}


static int32_t iris_dsi_send_ocp_cmds(struct dsi_panel *panel, struct iris_cmd_comp *pcmd_comp)
{
	int ret;
	uint32_t wait = 0;
	struct dsi_cmd_desc *cmd = NULL;

	if (!pcmd_comp) {
		IRIS_LOGE("%s(), cmd list is null.", __func__);
		return -EINVAL;
	}

	/*use us than ms*/
	cmd = pcmd_comp->cmd + pcmd_comp->cnt - 1;
	wait = cmd->post_wait_ms;
	if (wait)
		cmd->post_wait_ms = 0;

	ret = iris5_dsi_cmds_send(panel, pcmd_comp->cmd,
			pcmd_comp->cnt, pcmd_comp->link_state);
	if (wait)
		udelay(wait);

	iris_print_specific_cmds(pcmd_comp->cmd, pcmd_comp->cnt);
	iris_print_none_lut_cmds_for_lk(pcmd_comp->cmd, pcmd_comp->cnt, wait, pcmd_comp->link_state);

	return ret;
}


int32_t iris_send_dsi_cmds(
		struct dsi_panel *panel,
		struct iris_cmd_comp *pcmd_comp, uint8_t path)
{
	int32_t ret = 0;


	IRIS_LOGD("%s,%d: path = %d", __func__, __LINE__, path);

	if (!pcmd_comp) {
		IRIS_LOGE("cmd list is null");
		return -EINVAL;
	}

	if (path == PATH_DSI)
		ret = iris_dsi_send_ocp_cmds(panel, pcmd_comp);
	else if (path == PATH_I2C)
		ret = iris_i2c_send_ocp_cmds(panel, pcmd_comp);
	else
		ret = -EINVAL;

	return ret;
}

static void iris_send_cmd_to_panel(
		struct dsi_panel *panel,
		struct dsi_panel_cmd_set *cmds)
{
	if (!cmds || !cmds->count) {
		IRIS_LOGE("cmds = %p or cmd_cnt = 0", cmds);
		return;
	}

	iris5_panel_cmd_passthrough(panel, cmds);
}


int32_t iris_send_ipopt_cmds(int32_t ip, int32_t opt_id)
{
	int32_t rc = 0;
	struct iris_cmd_comp cmd_comp;
	struct iris_cfg *pcfg = iris_get_cfg();

	IRIS_LOGD("%s(), ip: %#x, opt: %#x.", __func__, ip, opt_id);
	rc = iris_dsi_get_cmd_comp(ip, opt_id, &cmd_comp);
	if (rc) {
		IRIS_LOGE("%s(), can not find in seq, ip = 0x%02x  opt = 0x%02x.",
				__func__, ip, opt_id);
		return rc;
	}

	return iris_send_dsi_cmds(pcfg->panel, &cmd_comp, PATH_DSI);
}

/**********************************************
* the API will only be called when suspend/resume and boot up.
*
***********************************************/
static void iris_send_specific_lut(uint8_t lut_table, uint8_t lut_idx)
{
	if (lut_table == DBC_LUT) {
		if (lut_idx < CABC_DLV_OFF) {
			/*iris_dbc_income_set();*/
			iris_lut_send(lut_table, lut_idx, 1);
			iris_lut_send(lut_table, lut_idx, 0);
		} else {
			/*iris_dbc_compenk_set(lut_idx);*/
			iris_lut_send(lut_table, lut_idx, 0);
		}
	} else if ((lut_table != AMBINET_HDR_GAIN)
			&& (lut_table != AMBINET_SDR2HDR_LUT))
		iris_lut_send(lut_table, lut_idx, 0);
}

static void iris_send_new_lut(uint8_t lut_table, uint8_t lut_idx)
{
	uint8_t dbc_lut_index = iris_get_dbc_lut_index();

	/* don't change the following three lines source code*/
	if (lut_table == DBC_LUT)
		iris_lut_send(lut_table, lut_idx, dbc_lut_index);
	else
		iris_lut_send(lut_table, lut_idx, 0);
}

static void iris_init_cmds_buf(
		struct iris_cmd_comp *pcmd_comp, int32_t link_state)
{
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();

	iris_out_cmds_buf_reset();

	memset(pcmd_comp, 0x00, sizeof(*pcmd_comp));
	pcmd_comp->cmd = pcfg->iris_cmds.iris_cmds_buf;
	pcmd_comp->link_state = link_state;
	pcmd_comp->cnt = pcfg->iris_cmds.cmds_index;
}

static void iris_remove_desc_last(struct dsi_cmd_desc *pcmd, int len)
{
	int i = 0;

	for (i = 0; i < len; i++)
		pcmd[i].last_command = false;
}


static void iris_add_desc_last(struct dsi_cmd_desc *pcmd, int len)
{
	int i = 0;

	for (i = 0; i < len; i++)
		pcmd[i].last_command = true;
}

static void iris_add_last_pkt(struct dsi_cmd_desc *cmd, int len)
{
	iris_remove_desc_last(cmd,  len);
	iris_add_desc_last(cmd + len - 1, 1);
}

static void iris_add_last_multi_pkt(
		struct dsi_cmd_desc *cmd, int len, int skip_last)
{
	int i = 0;
	int pos = 0;
	int num = 0;
	int surplus = 0;
	int prev = 0;
	int span = 0;
	static int sum = 0;
	struct iris_cfg *pcfg = NULL;

	prev = sum;
	sum += len;

	pcfg = iris_get_cfg();
	span = pcfg->add_last_flag;

	num = sum / span;
	surplus = sum  - num * span;

	for (i = 0; i < num; i++) {
		if (i == 0) {
			iris_add_last_pkt(cmd, span - prev);
		} else {
			pos = i * span - prev;
			iris_add_last_pkt(cmd + pos, span);
		}
	}
	pos = len - surplus;

	if (skip_last) {
		iris_remove_desc_last(cmd + pos, surplus);
		sum = surplus;
	} else {
		iris_add_last_pkt(cmd + pos, surplus);
		sum = 0;
	}
}

static int iris_set_pkt_last(
	struct dsi_cmd_desc *cmd, int len, int skip_last)
{
	int32_t ret = 0;
	int32_t add_last_flag = 0;
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();
	add_last_flag = pcfg->add_last_flag;

	//if (skip_last)
	//	iris_remove_desc_last(cmd, len);
	switch (add_last_flag) {
		case DSI_CMD_ONE_LAST_FOR_MULT_IPOPT:
			iris_remove_desc_last(cmd, len);
			iris_add_desc_last(cmd + len - 1, 1);
			break;
		case DSI_CMD_ONE_LAST_FOR_ONE_IPOPT:
			/*only add the last packet*/
			iris_remove_desc_last(cmd, len - 1);
			iris_add_desc_last(cmd + len - 1, 1);
			break;
		case DSI_CMD_ONE_LAST_FOR_ONE_PKT:
			/*add all packets*/
			iris_add_desc_last(cmd, len);
			break;
		default:
			iris_add_last_multi_pkt(cmd, len, skip_last);
			break;
	}

	return ret;
}

static int iris_send_lut_table_pkt(
		struct iris_ctrl_opt *popt, struct iris_cmd_comp *pcomp,
		bool is_update, uint8_t path)
{
	uint8_t opt_id = 0;
	uint8_t ip = 0;
	int32_t skip_last = 0;
	int32_t prev = 0;
	int32_t cur = 0;
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();
	opt_id = popt->opt_id;
	ip = popt->ip;
	skip_last = popt->skip_last;

	prev = pcomp->cnt;

	IRIS_LOGD("%s ip: %#x opt: %#x, skip last: %d, update: %s",
		 __func__, ip, opt_id, skip_last, is_update?"true":"false");

	pcfg->iris_cmds.cmds_index = prev;
	if (is_update) {
		iris_send_new_lut(ip, opt_id);
	} else {
		iris_send_specific_lut(ip, opt_id);
	}

	cur = pcfg->iris_cmds.cmds_index;

	if (cur == prev) {
		IRIS_LOGD("lut table is empty ip = %02x opt_id = %02x",
				popt->ip, opt_id);
		return 0;
	}

	pcomp->cnt = cur;

	iris_set_pkt_last(pcomp->cmd + prev, cur - prev, skip_last);
	if (!skip_last) {
		iris_send_dsi_cmds(pcfg->panel, pcomp, path);
		iris_init_cmds_buf(pcomp, pcomp->link_state);
	}
	return 0;
}


static int iris_send_none_lut_table_pkt(
		struct iris_ctrl_opt *pip_opt, struct iris_cmd_comp *pcomp, uint8_t path)
{
	uint8_t ip = 0;
	uint8_t opt_id = 0;
	int32_t flag = 0;
	int32_t prev = 0;
	int32_t cur = 0;
	int32_t skip_last = 0;
	int32_t add_last_flag = 0;
	int32_t rc = 0;
	struct iris_cfg *pcfg = NULL;
	struct iris_cmd_comp comp_priv;

	pcfg = iris_get_cfg();
	ip = pip_opt->ip;
	opt_id = pip_opt->opt_id;
	skip_last = pip_opt->skip_last;
	add_last_flag = pcfg->add_last_flag;

	IRIS_LOGD("%s ip: %#x opt: %#x, skip last: %d.",
		 __func__, ip, opt_id, skip_last);

	/*get single/multiple selection(s) according to option of ip*/
	rc = iris_dsi_get_cmd_comp(ip, opt_id, &comp_priv);
	if (rc) {
		IRIS_LOGE("%s(), invalid ip: %#x opt: %#x.", __func__, ip , opt_id);
		return -EINVAL;
	}

	if (pcomp->cnt == 0)
		pcomp->link_state = comp_priv.link_state;
	else if (comp_priv.link_state != pcomp->link_state)
		flag = 1; /*send link state different packet.*/

	if (flag == 0) {
		prev = pcomp->cnt;
		/*move single/multiples selection to one command*/

		memcpy(pcomp->cmd + pcomp->cnt, comp_priv.cmd,
			comp_priv.cnt * sizeof(*comp_priv.cmd));
		pcomp->cnt += comp_priv.cnt;

		cur = pcomp->cnt;
		iris_set_pkt_last(pcomp->cmd + prev, cur - prev, skip_last);
	}

	/*if need to send or the last packet of sequence,
	it should send out to the MIPI*/
	if (!skip_last || flag == 1) {
		iris_send_dsi_cmds(pcfg->panel, pcomp, path);
		iris_init_cmds_buf(pcomp, pcomp->link_state);
	}
	return 0;
}

static void iris_send_assembled_pkt(struct iris_ctrl_opt *arr, int len)
{
	int i = 0;
	uint8_t ip = 0;
	uint8_t opt_id = 0;
	int32_t rc = -1;
	struct iris_cmd_comp cmd_comp;
	struct iris_cfg *pcfg;

	iris_init_cmds_buf(&cmd_comp, DSI_CMD_SET_STATE_HS);
	pcfg = iris_get_cfg();

	mutex_lock(&pcfg->mutex);
	for (i = 0; i < len; i++) {
		ip = arr[i].ip;
		opt_id = arr[i].opt_id;
		IRIS_LOGV("%s ip=%0x opt_id=%0x", __func__, ip, opt_id);

		/*lut table*/
		if (iris_is_lut(ip)) {
			rc = iris_send_lut_table_pkt(arr + i, &cmd_comp, false, PATH_DSI);
		} else {
			rc = iris_send_none_lut_table_pkt(arr + i, &cmd_comp, PATH_DSI);
		}

		if (rc) {
			IRIS_LOGE("%s: ip=%0x opt_id=%0x[FATAL FAILURE]", __func__, ip, opt_id);
			//panic("%s error\n", __func__);
		}
	}
	mutex_unlock(&pcfg->mutex);
}

void iris_send_mode_switch_pkt(void)
{
	int i = 0;
	uint8_t ip = 0;
	uint8_t opt_id = 0;
	int32_t rc = -1;
	struct iris_cmd_comp cmd_comp;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_ctrl_seq *pseq = NULL;
	struct iris_ctrl_opt *arr = NULL;
	int refresh_rate = pcfg->panel->cur_mode->timing.refresh_rate;
	int v_active = pcfg->panel->cur_mode->timing.v_active;

	if ((refresh_rate == HIGH_FREQ) && (v_active == FHD_H))
		pseq = &pcfg->mode_switch_seq_1;
	else
		pseq = &pcfg->mode_switch_seq;

	IRIS_LOGI("%s, cmd_list_index:%d, v_res: %d, fps:%d",
		__func__, pcfg->cmd_list_index, v_active, refresh_rate);

	if(pseq == NULL) {
		IRIS_LOGE("pseq == NULL");
		return;
	}
	arr = pseq->ctrl_opt;

	iris_init_cmds_buf(&cmd_comp, DSI_CMD_SET_STATE_HS);
	mutex_lock(&pcfg->mutex);
	for (i = 0; i < pseq->cnt; i++) {
		ip = arr[i].ip;
		opt_id = arr[i].opt_id;
		IRIS_LOGV("%s ip=%0x opt_id=%0x", __func__, ip, opt_id);

		if (iris_is_lut(ip)) {
			rc = iris_send_lut_table_pkt(arr + i, &cmd_comp, false, PATH_DSI);
		} else {
			rc = iris_send_none_lut_table_pkt(arr + i, &cmd_comp, PATH_DSI);
		}
		if (rc) {
			IRIS_LOGE("%s: ip=%0x opt_id=%0x", __func__, ip, opt_id);
		}
	}
	mutex_unlock(&pcfg->mutex);
	udelay(100);
}

static void iris_send_lightup_pkt(void)
{
	struct iris_cfg *pcfg = NULL;
	struct iris_ctrl_seq *pseq = NULL;

	pcfg = iris_get_cfg();
	pseq = iris_get_ctrl_seq(pcfg);

	iris_send_assembled_pkt(pseq->ctrl_opt, pseq->cnt);
}


void iris_init_update_ipopt(
		struct iris_update_ipopt *popt, uint8_t ip,
		uint8_t opt_old, uint8_t opt_new, uint8_t skip_last)
{
	popt->ip = ip;
	popt->opt_old = opt_old;
	popt->opt_new = opt_new;
	popt->skip_last = skip_last;
}


int iris_init_update_ipopt_t(
		struct iris_update_ipopt *popt,  int len,
		uint8_t ip, uint8_t opt_old,
		uint8_t opt_new, uint8_t skip_last)
{
	int i  = 0;
	int cnt = 0;

	for (i = 0; i < len; i++) {
		if (popt[i].ip == 0xff)
			break;
	}

	if (i >=  len) {
		IRIS_LOGE("there no empty space for install ip opt");
		return -EINVAL;
	}

	iris_init_update_ipopt(&popt[i],  ip, opt_old, opt_new, skip_last);
	cnt = i + 1;

	return cnt;
}

int iris_read_chip_id(void)
{
	struct iris_cfg *pcfg  = NULL;
	//uint32_t sys_pll_ro_status = 0xf0000010;

	pcfg = iris_get_cfg();

	// FIXME: if chip version is set by sw, skip hw read chip id.
	//if (pcfg->chip_ver == IRIS3_CHIP_VERSION)
		//pcfg->chip_id = (iris_ocp_read(sys_pll_ro_status, DSI_CMD_SET_STATE_HS)) & 0xFF;
	//else
	pcfg->chip_id = 0;

	IRIS_LOGI("chip ver = %#x", pcfg->chip_ver);
	IRIS_LOGI("chip id = %#x", pcfg->chip_id);

	return pcfg->chip_id;
}

static void iris_status_clean(void)
{
	struct iris_cfg *pcfg  = iris_get_cfg_by_index(DSI_PRIMARY);
	if (pcfg->rx_mode == pcfg->tx_mode)
		pcfg->pwil_mode = PT_MODE;
	else
		pcfg->pwil_mode = RFB_MODE;
	pcfg->switch_mode = IRIS_MODE_RFB;
	pcfg->osd_enable = false;
	pcfg->osd_on = false;
	pcfg->frc_setting.in_fps_configured = 0;
	IRIS_LOGI("%s, pwil_mode: %d", __func__, pcfg->pwil_mode);
	pcfg->mcu_code_downloaded = false;
	if (pcfg->tx_mode == 0) // video mode
		iris_set_frc_var_display(0);
	pcfg->dynamic_vfr = false;
	atomic_set(&pcfg->video_update_wo_osd, 0);
	cancel_work_sync(&pcfg->vfr_update_work);
	atomic_set(&pcfg->osd_irq_cnt, 0);
#if defined(PXLW_IRIS_DUAL)
	atomic_set(&pcfg->dom_cnt_in_frc, 0);
	atomic_set(&pcfg->dom_cnt_in_ioctl, 0);
#endif
}

void iris_free_ipopt_buf(uint32_t ip_type)
{
	int ip_index = 0;
	int opt_index = 0;
	uint32_t desc_index = 0;
	int ip_cnt = IRIS_IP_CNT;
	struct dsi_cmd_desc *pdesc_addr = NULL;
	struct iris_ip_index *pip_index = iris_get_ip_idx(ip_type);

	if (ip_type == IRIS_LUT_PIP_IDX)
		ip_cnt = LUT_IP_END - LUT_IP_START;

	for (ip_index = 0; ip_index < ip_cnt; ip_index++) {
		if (pip_index[ip_index].opt_cnt == 0 || pip_index[ip_index].opt == NULL)
			continue;

		for (opt_index = 0; opt_index < pip_index[ip_index].opt_cnt; opt_index++) {
			if (pip_index[ip_index].opt[opt_index].len == 0
				|| pip_index[ip_index].opt[opt_index].cmd == NULL)
				continue;

			/* get desc cmd start address */
			if (pdesc_addr == NULL || pip_index[ip_index].opt[opt_index].cmd < pdesc_addr)
				pdesc_addr = pip_index[ip_index].opt[opt_index].cmd;

			for (desc_index = 0; desc_index < pip_index[ip_index].opt[opt_index].len; desc_index++) {
				if (pip_index[ip_index].opt[opt_index].cmd[desc_index].msg.tx_buf == NULL
					|| pip_index[ip_index].opt[opt_index].cmd[desc_index].msg.tx_len == 0)
					continue;

				/* free cmd payload, which alloc in "iris_write_cmd_payload()" */
				kfree(pip_index[ip_index].opt[opt_index].cmd[desc_index].msg.tx_buf);
				pip_index[ip_index].opt[opt_index].cmd[desc_index].msg.tx_buf = NULL;
			}

			/* set each desc cmd to NULL first */
			pip_index[ip_index].opt[opt_index].cmd = NULL;
		}

		/* free opt buffer for each ip, which alloc in "iris_alloc_buf_for_pip()" */
		kfree(pip_index[ip_index].opt);
		pip_index[ip_index].opt = NULL;
		pip_index[ip_index].opt_cnt = 0;
	}

	/* free desc cmd buffer, which alloc in "iris_alloc_buf_for_desc()", desc
	 * cmd buffer is continus memory, so only free once on start address
	 */
	if (pdesc_addr != NULL) {
		IRIS_LOGI("%s(), free desc cmd buffer %p, type %#x", __func__, pdesc_addr, ip_type);
		kfree(pdesc_addr);
		pdesc_addr = NULL;
	}

}

void iris_free_seq_space(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	/* free cmd to sent buffer, which alloc in "iris_alloc_seq_space()" */
	if (pcfg->iris_cmds.iris_cmds_buf != NULL) {
		IRIS_LOGI("%s(), free %p", __func__, pcfg->iris_cmds.iris_cmds_buf);
		kfree(pcfg->iris_cmds.iris_cmds_buf);
		pcfg->iris_cmds.iris_cmds_buf = NULL;
	}

}

void iris_alloc_seq_space(void)
{
	int sum = 0;
	struct iris_cfg *pcfg = NULL;
	struct dsi_cmd_desc *pdesc = NULL;

	pcfg = iris_get_cfg();

	sum = pcfg->none_lut_cmds_cnt + pcfg->lut_cmds_cnt;
	IRIS_LOGI("%s(), seq = %u, lut = %u", __func__,
		pcfg->none_lut_cmds_cnt, pcfg->lut_cmds_cnt);

	sum = sum * sizeof(struct dsi_cmd_desc);
	pdesc = kmalloc(sum, GFP_KERNEL);
	if (!pdesc) {
		IRIS_LOGE("can not alloc buffer");
		return;
	}
	pcfg->iris_cmds.iris_cmds_buf = pdesc;
	IRIS_LOGI("%s(), alloc %p", __func__, pcfg->iris_cmds.iris_cmds_buf);
	iris_out_cmds_buf_reset();

	// Need to init PQ parameters here for video panel.
	iris_pq_parameter_init();
}

static void iris_load_release_mcu(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 values[2] = {0xF00000C8, 0x1};
	struct iris_ctrl_opt ctrl_opt;

	ctrl_opt.ip = APP_CODE_LUT;
	ctrl_opt.opt_id = 0;
	ctrl_opt.skip_last = 0;

	IRIS_LOGI("%s,%d: mcu downloading and running", __func__, __LINE__);
	iris_send_assembled_pkt(&ctrl_opt, 1);
	iris_ocp_write3(2, values);
	pcfg->mcu_code_downloaded = true;
}

static void iris_pre_lightup(struct dsi_panel *panel)
{
	static int num = 0;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_update_ipopt popt[IP_OPT_MAX];
	int len;
	bool high;

	if ((panel->cur_mode->timing.refresh_rate == HIGH_FREQ) && (pcfg->panel->cur_mode->timing.v_active == FHD_H))
		high = true;
	else
		high = false;

	iris_init_ipopt_ip(popt, IP_OPT_MAX);
	//sys pll
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_SYS, high ? 0xA1:0xA0, 0x1);
	//dtg
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_DTG, high ? 0x1:0x0, 0x1);
	//mipi tx
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_TX, high ? 0x4:0x0, 0x1);
	//mipi abp
	len = iris_update_ip_opt(popt, IP_OPT_MAX, IRIS_IP_RX, high ? 0xE1:0xE0, 0x0);
	iris_update_pq_seq(popt, len);

	/*send rx cmds first with low power*/
	iris_send_ipopt_cmds(IRIS_IP_RX, high ? 0xF2: 0xF1);

	//if (num == 0) {
	if (1) {
		/*read chip_id*/
		iris_read_chip_id();
		num++;
	}
	osd_blending_work.enter_lp_st = MIPI2_LP_FINISH;
	iris_pq_parameter_init();
	iris_frc_parameter_init();
	iris_status_clean();
}

void iris_read_power_mode(struct dsi_panel *panel)
{
	char get_power_mode[1] = {0x0a};
	char read_cmd_rbuf[16] = {0};
	struct dsi_cmd_desc cmds = {
			{0, MIPI_DSI_DCS_READ, MIPI_DSI_MSG_REQ_ACK, 0, 0, sizeof(get_power_mode), get_power_mode, 1, read_cmd_rbuf}, 1, 0};
	struct dsi_panel_cmd_set cmdset = {
		.state = DSI_CMD_SET_STATE_HS,
		.count = 1,
		.cmds = &cmds,
	};
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();
	IRIS_LOGW("[%s:%d], pcfg->abypss_ctrl.abypass_mode = %d", __func__, __LINE__, pcfg->abypss_ctrl.abypass_mode);
	if (ANALOG_BYPASS_MODE == pcfg->abypss_ctrl.abypass_mode) {
		iris5_dsi_cmds_send(panel, cmdset.cmds, cmdset.count, cmdset.state);
	} else {
		iris5_dsi_cmds_send(panel, cmdset.cmds, cmdset.count, cmdset.state);
		IRIS_LOGE("[a]power mode: 0x%02x", read_cmd_rbuf[0]);
		read_cmd_rbuf[0] = 0;
		iris_send_cmd_to_panel(panel, &cmdset);
	}
	pcfg->power_mode = read_cmd_rbuf[0];

	IRIS_LOGE("[b]power mode: 0x%02x", pcfg->power_mode);
}

static void iris_check_firmware_update_gamma(void)
{
	if (iris_get_firmware_status() != FIRMWARE_LOAD_SUCCESS)
		return;

	iris_scaler_gamma_enable(true, 1);
	iris_set_firmware_status(FIRMWARE_IN_USING);
}

int iris5_lightup(
		struct dsi_panel *panel,
		struct dsi_panel_cmd_set *on_cmds)
{
	ktime_t ktime0;
	ktime_t ktime1;
	uint32_t timeus0 = 0;
	uint32_t timeus1 = 0;
	uint8_t type = 0;
	struct dsi_display *display = to_dsi_display(panel->host);
	int rc;
	struct iris_cfg *pcfg = iris_get_cfg();

	ktime0 = ktime_get();

	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_ALL_CLKS, DSI_CLK_ON);
	if (rc) {
		IRIS_LOGE("[%s] failed to enable all DSI clocks, rc=%d",
			display->name,rc);
	}

	rc = dsi_display_cmd_engine_enable(display);
	if (rc) {
		IRIS_LOGE("[%s] failed to enable cmd engine, rc=%d",
			display->name, rc);
	}

	pcfg->add_last_flag = pcfg->add_on_last_flag;

	iris_pre_lightup(panel);

	type = iris_get_cont_splash_type();

	/*use to debug cont splash*/
	if (type == IRIS_CONT_SPLASH_LK) {
		IRIS_LOGI("%s(%d), enter cont splash", __func__, __LINE__);
		iris_send_cont_splash_pkt(IRIS_CONT_SPLASH_LK);
	} else {
		iris_send_lightup_pkt();
		iris_scaler_filter_ratio_get();
		iris_check_firmware_update_gamma();
#if defined(PXLW_IRIS_DUAL)
		if (!(pcfg->dual_test & 0x20))
			iris_dual_setting_switch(pcfg->dual_setting);
#endif
	}

	iris_load_release_mcu();

	if (panel->bl_config.type == DSI_BACKLIGHT_PWM)
		iris_pwm_freq_set(panel->bl_config.pwm_period_usecs);

	ktime1 = ktime_get();
	if (on_cmds)
		iris_send_cmd_to_panel(panel, on_cmds);

	if (type == IRIS_CONT_SPLASH_LK)
		IRIS_LOGI("exit cont splash");
	else
		/*continuous splahs should not use dma setting low power*/
		iris_lp_init();

	pcfg->add_last_flag = pcfg->add_pt_last_flag;

	rc = dsi_display_cmd_engine_disable(display);
	if (rc) {
		IRIS_LOGE("[%s] failed to disable cmd engine, rc=%d",
			display->name, rc);
	}
	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_ALL_CLKS, DSI_CLK_OFF);
	if (rc) {
		IRIS_LOGE("[%s] failed to disable all DSI clocks, rc=%d",
			display->name,rc);
	}

	pcfg->cur_fps_in_iris = panel->cur_mode->timing.refresh_rate;
	pcfg->cur_vres_in_iris = panel->cur_mode->timing.v_active;
	pcfg->frc_setting.out_fps = pcfg->cur_fps_in_iris;

	timeus0 = (u32) ktime_to_us(ktime1) - (u32)ktime_to_us(ktime0);
	timeus1 = (u32) ktime_to_us(ktime_get()) - (u32)ktime_to_us(ktime1);
	IRIS_LOGI("spend time0 %d us, time1 %d us.", timeus0, timeus1);

#ifdef IRIS5_MIPI_TEST
	iris_read_power_mode(panel);
#endif
	IRIS_LOGI("iris on end");

	return 0;
}

int iris_panel_enable(struct dsi_panel *panel, struct dsi_panel_cmd_set *on_cmds)
{
	int rc = 0;
	struct iris_cfg *pcfg = NULL;
	int abyp_status_gpio;
	int prev_mode;
	int lightup_opt = iris_lightup_opt_get();

	if (panel->is_secondary) {
		if (panel->reset_config.iris_osd_autorefresh) {
			IRIS_LOGI("reset iris_osd_autorefresh");
			iris_osd_autorefresh(0);
		}
		return rc;
	}

	iris_lp_preinit();

	pcfg = iris_get_cfg();
	pcfg->iris_initialized = false;

	pcfg->next_fps_for_iris = panel->cur_mode->timing.refresh_rate;

	/* Special process for WQHD@120Hz */
	if(panel->cur_mode->timing.refresh_rate == HIGH_FREQ && panel->cur_mode->timing.v_active == QHD_H) {
		/* Force Iris work in ABYP mode */
		pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE;
	}

	IRIS_LOGI("%s, mode:%d, rate: %d, v: %d, on_opt:0x%x", __func__, pcfg->abypss_ctrl.abypass_mode,
		panel->cur_mode->timing.refresh_rate, panel->cur_mode->timing.v_active, lightup_opt);

	// if (pcfg->fod == true && pcfg->fod_pending) {
	// 	iris_abyp_lp(1);
	// 	pcfg->abyp_prev_mode = pcfg->abypss_ctrl.abypass_mode;
	// 	pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE;
	// 	pcfg->fod_pending = false;
	// 	pcfg->initialized = false;
	// 	IRIS_LOGD("[%s:%d] fod = 1 in init, ABYP prev mode: %d ABYP mode: %d",
	// 		 __func__, __LINE__, pcfg->abyp_prev_mode, pcfg->abypss_ctrl.abypass_mode);
	// 	return rc;
	// }

	/* support lightup_opt */
	if (lightup_opt & 0x1) {
		if (on_cmds != NULL)
			rc = iris5_dsi_cmds_send(panel, on_cmds->cmds, on_cmds->count, on_cmds->state);
		IRIS_LOGW("%s force ABYP lightup.",__func__);
		return rc;
	}

	prev_mode = pcfg->abypss_ctrl.abypass_mode;

	abyp_status_gpio = iris_exit_abyp(false);
	if (abyp_status_gpio == 1) {
		IRIS_LOGW("%s failed, exit abyp failed!",__func__);
		return rc;
	}

	if (pcfg->loop_back_mode == 1) {
		pcfg->loop_back_mode_res = iris5_loop_back_verify();
		return rc;
	} else {
		rc = iris5_lightup(panel, NULL);
		pcfg->iris_initialized = true;
		if (on_cmds != NULL)
			rc = iris5_panel_cmd_passthrough(panel, on_cmds);
		if (pcfg->lp_ctrl.esd_cnt > 0) /* restore brightness setting for esd */
			iris_panel_nits_set(0, true, 0);
		//iris_set_out_frame_rate(panel->cur_mode->timing.refresh_rate);
		pcfg->abypss_ctrl.abypass_mode = PASS_THROUGH_MODE;
	}

	//Switch back to ABYP mode if need
	if (prev_mode == ANALOG_BYPASS_MODE) {
		iris_abypass_switch_proc(pcfg->display, ANALOG_BYPASS_MODE, false, true);
	}
	return rc;
}

int iris5_aod_set(struct dsi_panel *panel, bool aod)
{
	int rc = 0;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (!pcfg)
		return rc;

	if (panel->is_secondary)
		return rc;

	IRIS_LOGW("[%s:%d] aod: %d", __func__, __LINE__, aod);
	if (pcfg->aod == aod) {
		IRIS_LOGI("[%s:%d] aod: %d no change", __func__, __LINE__, aod);
		return rc;
	}

	if (aod == true) {
		if(pcfg->fod == false) {
			pcfg->abyp_prev_mode = pcfg->abypss_ctrl.abypass_mode;
			if (iris5_abypass_mode_get(panel) == PASS_THROUGH_MODE) {
				iris_abypass_switch_proc(pcfg->display, ANALOG_BYPASS_MODE, false, true);
			}
		}
	} else {
		if(pcfg->fod == false) {
			if (iris5_abypass_mode_get(panel) == ANALOG_BYPASS_MODE &&
					pcfg->abyp_prev_mode == PASS_THROUGH_MODE &&
					pcfg->fod == false) {
				iris_abypass_switch_proc(pcfg->display, PASS_THROUGH_MODE, false, true);
			}
		}
	}
	if (pcfg->fod_pending == true)
		pcfg->fod_pending = false;
	pcfg->aod = aod;
	return rc;
}

int iris5_fod_set(struct dsi_panel *panel, bool fod)
{
	int rc = 0;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (!pcfg)
		return rc;

	if (panel->is_secondary)
		return rc;

	IRIS_LOGD("[%s:%d] fod: %d", __func__, __LINE__, fod);
	if (pcfg->fod == fod) {
		IRIS_LOGD("[%s:%d] fod: %d no change", __func__, __LINE__, fod);
		return rc;
	}

	if (!dsi_panel_initialized(panel)) {
		IRIS_LOGD("[%s:%d] panel not initialized fod: %d", __func__, __LINE__, fod);
		pcfg->fod_pending = true;
		atomic_set(&pcfg->fod_cnt, 1);
		pcfg->fod = fod;
		return rc;
	}

	if (fod == true) {
		if (pcfg->aod == false) {
			pcfg->abyp_prev_mode = pcfg->abypss_ctrl.abypass_mode;
			if (iris5_abypass_mode_get(panel) == PASS_THROUGH_MODE) {
				iris_abypass_switch_proc(pcfg->display, ANALOG_BYPASS_MODE, false, true);
			}
		}
	} else {
		/* pending until hbm off cmds sent in update_hbm 1->0 */
		pcfg->fod_pending = true;
		atomic_set(&pcfg->fod_cnt, 1);
	}


	pcfg->fod = fod;
	return rc;
}

int iris5_fod_post(struct dsi_panel *panel)
{
	int rc = 0;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (!pcfg)
		return rc;

	if (panel->is_secondary)
		return rc;

	if (atomic_read(&pcfg->fod_cnt) > 0) {
		IRIS_LOGD("[%s:%d] fod delay %d", __func__, __LINE__, atomic_read(&pcfg->fod_cnt));
		atomic_dec(&pcfg->fod_cnt);
		return rc;
	}

	IRIS_LOGD("[%s:%d] fod: %d", __func__, __LINE__, pcfg->fod);

	if (pcfg->fod == true) {
		if (pcfg->aod == false) {
			pcfg->abyp_prev_mode = pcfg->abypss_ctrl.abypass_mode;
			if (iris5_abypass_mode_get(panel) == PASS_THROUGH_MODE) {
				iris_abypass_switch_proc(pcfg->display, ANALOG_BYPASS_MODE, false, true);
			}
		}
	} else {
		if (pcfg->aod == false) {
			if (iris5_abypass_mode_get(panel) == ANALOG_BYPASS_MODE &&
					pcfg->abyp_prev_mode == PASS_THROUGH_MODE) {
				iris_abypass_switch_proc(pcfg->display, PASS_THROUGH_MODE, false, true);
			}
		}
	}

	pcfg->fod_pending = false;
	return rc;
}

bool iris5_aod_get(struct dsi_panel *panel)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (!panel || !pcfg)
		return false;
	else
		return pcfg->aod;
}

enum {
	SWITCH_ABYP_TO_ABYP = 0,
	SWITCH_ABYP_TO_PT,
	SWITCH_PT_TO_ABYP,
	SWITCH_PT_TO_PT,
	SWITCH_NONE,
};

static const char *iris_switch_case_name(const uint32_t switch_case)
{
	const char *name = NULL;

	switch (switch_case) {
		case SWITCH_ABYP_TO_ABYP:
			name = "ABYP==>ABYP";
			break;
		case SWITCH_ABYP_TO_PT:
			name = "ABYP==>PT";
			break;
		case SWITCH_PT_TO_ABYP:
			name = "PT==>ABYP";
			break;
		case SWITCH_PT_TO_PT:
			name = "PT==>PT";
			break;
		default:
			name = "unknown";
	}

	return name;
}


static uint32_t iris_switch_case(const u32 refresh_rate, const u32 frame_height)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	bool cur_pt_mode = (PASS_THROUGH_MODE == pcfg->abypss_ctrl.abypass_mode);
	u32 switch_mode = SWITCH_ABYP_TO_ABYP;

	IRIS_LOGD("%s, refersh rate %u, frame height %u, iris current mode '%s'",
		__func__, refresh_rate, frame_height, cur_pt_mode?"PT":"ABYP");

	if (frame_height == QHD_H) {
		pcfg->cmd_list_index = IRIS_DTSI0_PIP_IDX;
		if (cur_pt_mode) {
			switch_mode = SWITCH_PT_TO_ABYP;
		}
	}

	if (frame_height == FHD_H) {
		pcfg->cmd_list_index = IRIS_DTSI1_PIP_IDX;
		if (cur_pt_mode) {
			if (pcfg->cur_v_active == QHD_H)
				switch_mode = SWITCH_PT_TO_ABYP;
			else
				switch_mode = SWITCH_PT_TO_PT;
		}
	}

	return switch_mode;
}

static bool iris_switch_resolution(struct dsi_mode_info *mode_info)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	IRIS_LOGD("%s, switch resolution from %ux%u to %ux%u", __func__,
		pcfg->cur_h_active, pcfg->cur_v_active,
		mode_info->h_active, mode_info->v_active);

	if (pcfg->cur_h_active != mode_info->h_active
		|| pcfg->cur_v_active != mode_info->v_active) {
		pcfg->cur_h_active = mode_info->h_active;
		pcfg->cur_v_active = mode_info->v_active;

		return true;
	}

	return false;
}

static void iris5_aod_state_clear(void)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (pcfg->aod) {
		pcfg->aod = false;
		pcfg->abypss_ctrl.abypass_mode = pcfg->abyp_prev_mode;
	}
}

int iris_panel_post_switch(struct dsi_panel *panel,
		struct dsi_panel_cmd_set *switch_cmds,
		struct dsi_mode_info *mode_info)
{
	int rc = 0;
	struct iris_cfg *pcfg = iris_get_cfg();
	int lightup_opt = iris_lightup_opt_get();
	u32 refresh_rate = mode_info->refresh_rate;
	u32 frame_width = mode_info->h_active;
	u32 frame_height = mode_info->v_active;
	u32 switch_case = iris_switch_case(refresh_rate, frame_height);

	IRIS_LOGI("%s, post switch to %ux%u@%uHz, cmd list %u, switch case %s",
		__func__, frame_width, frame_height, refresh_rate,
		pcfg->cmd_list_index, iris_switch_case_name(switch_case));

	pcfg->switch_case = switch_case;

	if (switch_cmds == NULL) {
		return 0;
	}

	if (lightup_opt & 0x8) {
		rc = iris5_dsi_cmds_send(panel, switch_cmds->cmds, switch_cmds->count, switch_cmds->state);
		IRIS_LOGI("%s, post switch Force ABYP", __func__);
		return 0;
	}

	if (iris5_abypass_mode_get(panel) == PASS_THROUGH_MODE) {
		rc = iris5_panel_cmd_passthrough(panel, &(panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_POST_TIMING_SWITCH]));
	} else {
		rc = iris5_dsi_cmds_send(panel, switch_cmds->cmds, switch_cmds->count, switch_cmds->state);
	}

	IRIS_LOGD("%s, return %d", __func__, rc);
	return 0;
}

void iris_framerate_switch(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	bool high;
	u32 framerate = pcfg->panel->cur_mode->timing.refresh_rate;

	if ((framerate == HIGH_FREQ) && (pcfg->panel->cur_mode->timing.v_active == FHD_H))
		high = true;
	else
		high = false;

	iris_send_ipopt_cmds(IRIS_IP_SYS, high ? 0xA1:0xA0);
	udelay(100);
	iris_send_ipopt_cmds(IRIS_IP_RX, high ? 0xE1:0xE0);
	iris_send_ipopt_cmds(IRIS_IP_TX, high ? 0x4:0x0);
	iris_set_out_frame_rate(framerate);
	iris_send_ipopt_cmds(IRIS_IP_RX, high ? 0xF2:0xF1);
#if defined(PXLW_IRIS_DUAL)
	iris_send_ipopt_cmds(IRIS_IP_RX_2, high ? 0xF2:0xF1);
	iris_send_ipopt_cmds(IRIS_IP_BLEND, high ? 0xF1:0xF0);
#endif
	udelay(2000); //delay 2ms
}

int iris_panel_switch(struct dsi_panel *panel,
		struct dsi_panel_cmd_set *switch_cmds,
		struct dsi_mode_info *mode_info)
{
	int rc = 0;
	struct iris_cfg *pcfg = iris_get_cfg();
	int lightup_opt = iris_lightup_opt_get();
	u32 refresh_rate = mode_info->refresh_rate;
	u32 switch_case = pcfg->switch_case;
	ktime_t ktime = 0; //CID89982 uninitialized variable

	IRIS_LOGD("%s(%d)", __func__, __LINE__);

	pcfg->panel_te = refresh_rate;
	pcfg->ap_te = refresh_rate;
	pcfg->next_fps_for_iris = refresh_rate;

	IRIS_IF_LOGI() {
		ktime = ktime_get();
	}
	if (lightup_opt & 0x8) {
		rc = iris5_dsi_cmds_send(panel, switch_cmds->cmds, switch_cmds->count, switch_cmds->state);
		IRIS_LOGI("%s, switch between ABYP and ABYP, total cost '%d us', ", __func__,
				(u32)(ktime_to_us(ktime_get()) - ktime_to_us(ktime)));
		IRIS_LOGW("%s force ABYP switch.",__func__);
		return rc;
	}

	if (switch_case == SWITCH_ABYP_TO_ABYP) {
		rc = iris5_dsi_cmds_send(panel, switch_cmds->cmds, switch_cmds->count, switch_cmds->state);
	}

	if (switch_case == SWITCH_PT_TO_PT) {
		rc = iris5_panel_cmd_passthrough(panel, switch_cmds);
		iris_framerate_switch();
	}

	if (switch_case == SWITCH_PT_TO_ABYP) {
		iris_abypass_switch_proc(pcfg->display, ANALOG_BYPASS_MODE, false, true);
		rc = iris5_dsi_cmds_send(panel, switch_cmds->cmds, switch_cmds->count, switch_cmds->state);
	}

	// Update panel timing
	iris_switch_resolution(mode_info);

	IRIS_LOGI("%s, return %d, total cost '%d us', ", __func__, rc,
		(u32)(ktime_to_us(ktime_get()) - ktime_to_us(ktime)));
	return 0;
}

/*check whether it is in initial cont-splash packet*/
static bool iris_check_cont_splash_ipopt(uint8_t ip, uint8_t opt_id)
{
	int i = 0;
	uint8_t cs_ip = 0;
	uint8_t cs_opt_id = 0;
	struct iris_cfg *pcfg = NULL;
	struct iris_ctrl_seq *pseq_cs = NULL;

	pcfg = iris_get_cfg();
	pseq_cs = iris_get_ctrl_seq_cs(pcfg);

	for (i = 0; i < pseq_cs->cnt; i++) {
		cs_ip = pseq_cs->ctrl_opt[i].ip;
		cs_opt_id = pseq_cs->ctrl_opt[i].opt_id;

		if (ip == cs_ip && opt_id == cs_opt_id)
			return true;
	}

	return false;
}

/*
select ip/opt to the opt_arr according to lightup stage type
*/
static int iris_select_cont_splash_ipopt(
		int type, struct iris_ctrl_opt *opt_arr)
{
	uint32_t i = 0;
	uint32_t j = 0;
	uint8_t ip = 0;
	uint8_t opt_id = 0;

	struct iris_cfg *pcfg = NULL;
	struct iris_ctrl_seq *pseq = NULL;
	struct iris_ctrl_opt *pctrl_opt = NULL;

	pcfg = iris_get_cfg();
	pseq = iris_get_ctrl_seq(pcfg);

	for (i = 0; i < pseq->cnt; i++) {
	    pctrl_opt = pseq->ctrl_opt + i;
		ip = pctrl_opt->ip;
		opt_id = pctrl_opt->opt_id;

		if (iris_check_cont_splash_ipopt(ip, opt_id))
			continue;

		memcpy(opt_arr + j, pctrl_opt, sizeof(*pctrl_opt));
		j++;
	}

	IRIS_LOGD("real len = %d", j);
	return j;
}

void iris_send_cont_splash_pkt(uint32_t type)
{
	int len = 0;
	uint32_t size = 0;
	const int iris_max_opt_cnt = 30;
	struct iris_ctrl_opt *opt_arr = NULL;
	struct iris_cfg *pcfg = NULL;
	struct iris_ctrl_seq   *pseq_cs = NULL;
	bool pt_mode;

	size = IRIS_IP_CNT * iris_max_opt_cnt * sizeof(struct iris_ctrl_opt);
	opt_arr = kmalloc(size, GFP_KERNEL);
	if (opt_arr == NULL) {
		IRIS_LOGE("%s(), malloc failed!", __func__);
		return;
	}

	pcfg = iris_get_cfg();
	memset(opt_arr, 0xff, size);

	if (type == IRIS_CONT_SPLASH_LK) {
		pseq_cs = iris_get_ctrl_seq_cs(pcfg);
		iris_send_assembled_pkt(pseq_cs->ctrl_opt, pseq_cs->cnt);
		pcfg->iris_initialized = true;
	} else if (type == IRIS_CONT_SPLASH_KERNEL) {
		len = iris_select_cont_splash_ipopt(type, opt_arr);
		/*stop video -->set pq --> start video*/
		iris_sde_encoder_rc_lock();
		mdelay(20);
		iris_send_assembled_pkt(opt_arr, len);
		iris_sde_encoder_rc_unlock();
		iris_lp_init();
		iris_read_chip_id();
		pcfg->iris_initialized = true;
	} else if (type == IRIS_CONT_SPLASH_BYPASS) {
		iris_lp_preinit();
		pcfg->iris_initialized = false;
		pt_mode = iris_abypass_switch_proc(pcfg->display, PASS_THROUGH_MODE, false, true);
		if (pt_mode)
			iris_set_out_frame_rate(pcfg->panel->cur_mode->timing.refresh_rate);
	} else if (type == IRIS_CONT_SPLASH_BYPASS_PRELOAD) {
		iris_reset_mipi();
		iris_panel_enable(pcfg->panel, NULL);
	}

	kfree(opt_arr);
}

void iris_send_cont_splash(struct dsi_display *display)
{
	struct dsi_panel *panel = display->panel;
	struct iris_cfg *pcfg = iris_get_cfg();
	int lightup_opt = iris_lightup_opt_get();
	u32 switch_case = SWITCH_NONE;
	uint32_t type;

	if (panel->is_secondary)
		return;

	if (pcfg->ext_clk) {
		IRIS_LOGI("clk enable");
		clk_prepare_enable(pcfg->ext_clk);
	}

	// Update panel timing from UEFI.
	iris_switch_resolution(&panel->cur_mode->timing);

	if (pcfg->valid >= 2)
		switch_case = iris_switch_case(panel->cur_mode->timing.refresh_rate, panel->cur_mode->timing.v_active);
	IRIS_LOGI("%s, switch case: %s, rate: %d, v: %d", __func__, iris_switch_case_name(switch_case),
			panel->cur_mode->timing.refresh_rate, panel->cur_mode->timing.v_active);

	switch (switch_case) {
		case SWITCH_PT_TO_PT:
			type = IRIS_CONT_SPLASH_LK;
			break;
		case SWITCH_ABYP_TO_ABYP:
		case SWITCH_ABYP_TO_PT:
			type = IRIS_CONT_SPLASH_BYPASS_PRELOAD;
			break;
		case SWITCH_PT_TO_ABYP:
		// This case does not happen
		default:
			type = IRIS_CONT_SPLASH_NONE;
			break;
	}
	if (lightup_opt & 0x1)
		type = IRIS_CONT_SPLASH_NONE;

	iris_send_cont_splash_pkt(type);
}

int iris5_lightoff(
		struct dsi_panel *panel,
		struct dsi_panel_cmd_set *off_cmds)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	struct iris_cfg *pcfg2 = iris_get_cfg_by_index(DSI_SECONDARY);
	int lightup_opt = iris_lightup_opt_get();
	pcfg2->mipi_pwr_st = false;

	if (!panel || panel->is_secondary) {
		IRIS_LOGE("No need to light off 2nd panel.");
		return 0;
	}

	if ((lightup_opt & 0x10) == 0)
		pcfg->abypss_ctrl.abypass_mode = ANALOG_BYPASS_MODE; //clear to ABYP mode

	iris_set_cfg_index(DSI_PRIMARY);
	IRIS_LOGI("%s(%d), mode: %s +++", __func__, __LINE__,
		PASS_THROUGH_MODE==pcfg->abypss_ctrl.abypass_mode ? "PT" : "ABYP");
	if (off_cmds) {
		if (pcfg->abypss_ctrl.abypass_mode == PASS_THROUGH_MODE)
			iris5_panel_cmd_passthrough(panel, off_cmds);
		else
			iris5_dsi_cmds_send(panel, off_cmds->cmds, off_cmds->count, off_cmds->state);
	}
	iris_quality_setting_off();
	iris_lp_setting_off();
	iris5_aod_state_clear();
	pcfg->panel_pending = 0;
	pcfg->iris_initialized = false;

	IRIS_LOGI("%s(%d) ---", __func__, __LINE__);

	return 0;
}

static void iris_send_update_new_opt(
		struct iris_update_ipopt *popt,
		struct iris_cmd_comp *pasm_comp, uint8_t path)
{
	int32_t ip = 0;
	int32_t rc = 0;
	struct iris_ctrl_opt ctrl_opt;

	ip = popt->ip;
	ctrl_opt.ip = popt->ip;
	ctrl_opt.opt_id = popt->opt_new;
	ctrl_opt.skip_last = popt->skip_last;

	/*speical deal with lut table*/
	if (iris_is_lut(ip))
		rc = iris_send_lut_table_pkt(&ctrl_opt, pasm_comp, true, path);
	else
		rc = iris_send_none_lut_table_pkt(&ctrl_opt, pasm_comp, path);

	if (rc) {
		IRIS_LOGE("%s: ip=%0x opt_id=%0x[FATAL FAILURE]", __func__, ip, ctrl_opt.opt_id);
		//panic("%s\n", __func__);
	}
}



static void iris_send_pq_cmds(struct iris_update_ipopt *popt, int len, uint8_t path)
{
	int32_t i = 0;
	struct iris_cmd_comp cmd_comp;
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();

	if (!popt || !len) {
		IRIS_LOGE("popt is null");
		return;
	}

	memset(&cmd_comp, 0x00, sizeof(cmd_comp));
	cmd_comp.cmd =  pcfg->iris_cmds.iris_cmds_buf;
	cmd_comp.link_state = DSI_CMD_SET_STATE_HS;
	cmd_comp.cnt = pcfg->iris_cmds.cmds_index;

	mutex_lock(&pcfg->mutex);
	for (i = 0; i < len; i++)
		iris_send_update_new_opt(&popt[i], &cmd_comp, path);

	mutex_unlock(&pcfg->mutex);
}


static int iris_update_pq_seq(struct iris_update_ipopt *popt, int len)
{
	int32_t i = 0;
	int32_t j = 0;
	int32_t ip = 0;
	int32_t opt_id = 0;
	struct iris_cfg *pcfg = NULL;
	struct iris_ctrl_seq *pseq = NULL;

	pcfg = iris_get_cfg();
	pseq = iris_get_ctrl_seq(pcfg);

	for (i = 0; i < len; i++) {
		/*need to update sequence*/
		if (popt[i].opt_new != popt[i].opt_old) {
			for (j = 0; j < pseq->cnt; j++) {
				ip = pseq->ctrl_opt[j].ip;
				opt_id = pseq->ctrl_opt[j].opt_id;

				if (ip == popt[i].ip &&
						opt_id == popt[i].opt_old)
					break;
			}

			if (j == pseq->cnt) {
				IRIS_LOGE("can not find the old ip = %d opt_id = %d",
						popt[i].ip, popt[i].opt_old);
				return -EINVAL;
			}

			pseq->ctrl_opt[j].opt_id = popt[i].opt_new;
		}
	}
	return 0;
}


void iris_update_pq_opt(struct iris_update_ipopt *popt, int len, uint8_t path)
{
	int32_t rc = 0;

	if (!popt  || !len) {
		IRIS_LOGE("popt is null");
		return;
	}

	rc = iris_update_pq_seq(popt, len);
	if (!rc)
		iris_send_pq_cmds(popt, len, path);
}


static struct dsi_cmd_desc *iris_get_ipopt_desc(uint8_t ip, uint8_t opt_id, int32_t pos)
{
	struct iris_ip_opt  *popt = NULL;
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();
	popt = iris_find_ip_opt(ip, opt_id);
	if (popt == NULL) {
		IRIS_LOGE("%s(), find ip opt failed, ip = 0x%02x opt_id = 0x%02x.", __func__, ip , opt_id);
		return NULL;
	}

	if (pos < 2) {
		IRIS_LOGE("pos = %d is not right", pos);
		return NULL;
	}

	return popt->cmd + (pos * 4 - IRIS_OCP_HEADER_ADDR_LEN) / pcfg->split_pkt_size;
}


uint32_t *iris_get_ipopt_payload_data(
		uint8_t ip, uint8_t opt_id, int32_t pos)
{
	struct dsi_cmd_desc *pdesc = NULL;
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();
	pdesc = iris_get_ipopt_desc(ip, opt_id, pos);
	if (!pdesc) {
		IRIS_LOGE("%s, can not find right desc.", __func__);
		return NULL;
	} else if (pos > pdesc->msg.tx_len) {
		IRIS_LOGE("pos %d is out of paload length %zu", pos , pdesc->msg.tx_len);
		return NULL;
	}

	return (uint32_t *)((uint8_t *)pdesc->msg.tx_buf + (pos * 4) % pcfg->split_pkt_size);
}

void iris_set_ipopt_payload_data(
		uint8_t ip, uint8_t opt_id, int32_t pos, uint32_t value)
{
	struct dsi_cmd_desc *pdesc = NULL;
	struct iris_cfg *pcfg = NULL;
	uint32_t *pvalue = NULL;

	pcfg = iris_get_cfg();
	pdesc = iris_get_ipopt_desc(ip, opt_id, pos);
	if (!pdesc) {
		IRIS_LOGE("%s, can not find right desc.", __func__);
		return;
	}

	pvalue = (uint32_t *)((uint8_t *)pdesc->msg.tx_buf + (pos * 4) % pcfg->split_pkt_size);
	pvalue[0] = value;
}

void iris_update_bitmask_regval_nonread(
		struct iris_update_regval *pregval, bool is_commit)
{
	int32_t ip = 0;
	int32_t opt_id = 0;
	uint32_t orig_val = 0;
	uint32_t *data = NULL;
	uint32_t val = 0;
	struct iris_ip_opt *popt = NULL;

	if (!pregval) {
		IRIS_LOGE("pregval is null");
		return;
	}

	ip = pregval->ip;
	opt_id = pregval->opt_id;

	popt = iris_find_ip_opt(ip, opt_id);
	if (popt == NULL) {
		IRIS_LOGE("can not find ip = 0x%02x opt_id = 0x%02x",
				ip, opt_id);
		return;
	} else if (popt->len != 1) {
		IRIS_LOGE("error for bitmask popt->len = %d",
				popt->len);
		return;
	}

	data = (uint32_t *)popt->cmd[0].msg.tx_buf;

	orig_val = cpu_to_le32(data[2]);
	val = orig_val & (~pregval->mask);
	val |= (pregval->value  & pregval->mask);
	data[2] = val;
	pregval->value = val;

	if (is_commit)
		iris_send_ipopt_cmds(ip, opt_id);

}


void iris_update_bitmask_regval(
		struct iris_update_regval *pregval, bool is_commit)
{
	int32_t ip = 0;
	int32_t opt_id = 0;
	uint32_t *data = NULL;
	struct iris_ip_opt *popt = NULL;

	if (!pregval) {
		IRIS_LOGE("pregval is null");
		return;
	}

	ip = pregval->ip;
	opt_id = pregval->opt_id;

	popt = iris_find_ip_opt(ip, opt_id);
	if (popt == NULL) {
		IRIS_LOGE("can not find ip = 0x%02x opt_id = 0x%02x",
				ip, opt_id);
		return;
	} else if (popt->len != 2) {
		IRIS_LOGE("error for bitmask popt->len = %d",
				popt->len);
		return;
	}

	data = (uint32_t *)popt->cmd[1].msg.tx_buf;
	data[2] = cpu_to_le32(pregval->mask);
	data[3] = cpu_to_le32(pregval->value);

	if (is_commit)
		iris_send_ipopt_cmds(ip, opt_id);
}

static ssize_t iris_cont_splash_write(
		struct file *file, const char __user *buff,
		size_t count, loff_t *ppos)
{
	unsigned long val;

	if (kstrtoul_from_user(buff, count, 0, &val))
		return -EFAULT;

	iris_set_cont_splash_type(val);

	if (val == IRIS_CONT_SPLASH_KERNEL)
		iris_send_cont_splash_pkt(val);
	else if (val != IRIS_CONT_SPLASH_LK &&
			val != IRIS_CONT_SPLASH_NONE)
		IRIS_LOGE("the value is %zu, need to be 1 or 2 3", val);
	return count;
}


static ssize_t iris_cont_splash_read(
		struct file *file, char __user *buff,
                size_t count, loff_t *ppos)
{
	uint8_t type;
	int len, tot = 0;
	char bp[512];

	if (*ppos)
		return 0;

	type = iris_get_cont_splash_type();
	len = sizeof(bp);
	tot = scnprintf(bp, len, "%u\n", type);

	if (copy_to_user(buff, bp, tot))
	        return -EFAULT;

	*ppos += tot;

	return tot;
}


static const struct file_operations iris_cont_splash_fops = {
	.open = simple_open,
	.write = iris_cont_splash_write,
	.read = iris_cont_splash_read,
};


static ssize_t iris_split_pkt_write(
		struct file *file, const char __user *buff,
		size_t count, loff_t *ppos)
{
	unsigned long val;
	struct iris_cfg *pcfg = NULL;

	if (kstrtoul_from_user(buff, count, 0, &val))
		return -EFAULT;

	pcfg = iris_get_cfg();
	pcfg->add_last_flag = val;

	return count;
}


static ssize_t iris_split_pkt_read(
		struct file *file, char __user *buff,
                size_t count, loff_t *ppos)
{
	uint8_t type;
	int len, tot = 0;
	struct iris_cfg *pcfg = NULL;
	char bp[512];

	if (*ppos)
		return 0;

	pcfg = iris_get_cfg();
	type = pcfg->add_last_flag;

	len = sizeof(bp);
	tot = scnprintf(bp, len, "%u\n", type);

	if (copy_to_user(buff, bp, tot))
	        return -EFAULT;

	*ppos += tot;

	return tot;
}

bool iris_get_dual2single_status(void)
{
	u32 rc;

	rc = iris_ocp_read(IRIS_PWIL_CUR_META0, DSI_CMD_SET_STATE_HS);
	if ((rc != 0 && (rc & BIT(10)) == 0))
		return true;
	else
		return false;
}

static void iris_update_blending_setting(bool on)
{
	static u32 write_data[4];
	if (on) {
		write_data[0] = 0xf1540030;
		write_data[1] = 0x000400f0;
		write_data[2] = 0xf1540040;
		write_data[3] = 0x000c00f0;
		iris_ocp_write3(4, write_data);
		iris_send_ipopt_cmds(IRIS_IP_DPP, 0x61);
	} else {
		write_data[0] = 0xf1540030;
		write_data[1] = 0x00040100;
		write_data[2] = 0xf1540040;
		write_data[3] = 0x000c0100;
		iris_ocp_write3(4, write_data);
		iris_send_ipopt_cmds(IRIS_IP_DPP, 0x60);
	}
}

int iris_second_channel_power(bool pwr)
{
	bool compression_mode;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_cfg *pcfg2 = iris_get_cfg_by_index(DSI_SECONDARY);
	struct iris_update_regval regval;

	if (unlikely(!pcfg2->panel)) {
		IRIS_LOGE("Warning: No secondary panel configured!");
		return -EFAULT;
	}

	iris_set_cfg_index(DSI_PRIMARY);
	iris_ulps_source_sel(ULPS_NONE);

	if (pwr) {	//on
		regval.ip = IRIS_IP_SYS;
		regval.opt_id = 0x06;
		regval.mask = 0x2000;
		regval.value = 0x2000;
		iris_update_bitmask_regval_nonread(&regval, true);
		if (osd_blending_work.enter_lp_st != MIPI2_LP_FINISH) {
			IRIS_LOGE("osd_disable_work is still in queue entry");
			osd_blending_work.enter_lp_st = MIPI2_LP_FINISH;
		}

		if (pcfg2->panel->reset_config.iris_osd_autorefresh_enabled) {
			IRIS_LOGW("osd_autorefresh is not disable before enable osd!");
		} else {
			IRIS_LOGI("osd_autorefresh is disable before enable osd!");
		}

		if (pcfg2->panel->power_info.refcount == 0) {
				IRIS_LOGW("%s: AP mipi2 tx hasn't been power on.", __func__);
				pcfg2->osd_switch_on_pending = true;
//		} else if (!pcfg2->panel->panel_initialized) {
//				IRIS_LOGW("%s: AP mipi2 tx hasn't been initialized.", __func__);
//				pcfg2->osd_switch_on_pending = true;
		} else {
			if (pcfg->panel->cur_mode && pcfg->panel->cur_mode->priv_info && pcfg->panel->cur_mode->priv_info->dsc_enabled)
				compression_mode = true;
			else
				compression_mode = false;

			IRIS_LOGI("%s: iris_pmu_mipi2 on.", __func__);
			/* power up & config mipi2 domain */
			iris_pmu_mipi2_set(true);
			/*Power up BSRAM domain if need*/
			iris_pmu_bsram_set(true);
			udelay(300);

			iris_second_channel_pre(compression_mode);
			iris_update_blending_setting(true);

			IRIS_LOGI("%s, mipi_pwr_st = true", __func__);
			pcfg2->mipi_pwr_st = true;
		}
	} else {	//off
		/* power down mipi2 domain */
		iris_pmu_mipi2_set(false);
		iris_update_blending_setting(false);
		IRIS_LOGI("%s: iris_pmu_mipi2 off.", __func__);
		pcfg2->mipi_pwr_st = false;
	}

	return 0;
}

void iris_second_channel_pre(bool dsc_enabled)
{
	iris_send_ipopt_cmds(IRIS_IP_DMA, 0xe7);
}

void iris_second_channel_post(u32 val)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	//struct iris_cfg *pcfg2 = iris_get_cfg_by_index(DSI_SECONDARY);
	osd_blending_work.enter_lp_st = MIPI2_LP_SWRST;
	iris_send_ipopt_cmds(IRIS_IP_PWIL_2, 0xf1);
	IRIS_LOGD("%s, MIPI2_LP_SWRST", __func__);
	/*wait a frame to ensure the pwil_v6 SW_RST is sent*/
	msleep(20);
	osd_blending_work.enter_lp_st = MIPI2_LP_POST;
	IRIS_LOGD("%s, MIPI2_LP_POST", __func__);
	/* power down mipi2 domain */
	//iris_pmu_mipi2_set(false);
	/* bulksram retain on when FRC power on */
	if (pcfg->pwil_mode == PT_MODE) {
		if (iris_pmu_frc_get())
			IRIS_LOGI("FRC power on, can't power off bulksram");
		else {
			iris_pmu_bsram_set(false);
			if (iris_i3c_status_get() == false)
				iris_ulps_source_sel(ULPS_MAIN);
		}
	}
	//pcfg->osd_enable = false;
	osd_blending_work.enter_lp_st = MIPI2_LP_FINISH;
	IRIS_LOGD("%s, MIPI2_LP_FINISH", __func__);
}

static void iris_osd_blending_on(void)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	IRIS_LOGD("%s, ", __func__);
	if (pcfg->pwil_mode == PT_MODE)
		iris_psf_mif_efifo_set(pcfg->pwil_mode, true);
	iris_pwil_mode_set(pcfg->panel, pcfg->pwil_mode, true, DSI_CMD_SET_STATE_HS);
}

static void iris_osd_blending_off(void)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_update_regval regval;
	IRIS_LOGD("%s, ", __func__);
	regval.ip = IRIS_IP_SYS;
	regval.opt_id = 0x06;
	regval.mask = 0x2000;
	regval.value = 0x0;
	iris_update_bitmask_regval_nonread(&regval, true);

	iris_pwil_mode_set(pcfg->panel, pcfg->pwil_mode, false, DSI_CMD_SET_STATE_HS);

	iris_psf_mif_efifo_set(pcfg->pwil_mode, false);
}

int iris_osd_blending_switch(u32 val)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	struct iris_cfg *pcfg2 = iris_get_cfg_by_index(DSI_SECONDARY);

	if (unlikely(!pcfg2->panel)) {
		IRIS_LOGE("Warning: No secondary panel configured!");
		return -EFAULT;
	}

	if (val) {
		if (pcfg->dual_test & 0x100) {
			// check MIPI_RX AUX 2b_page register
			uint32_t *payload;
			u32 cmd_2b_page;
			int count = 0;
			payload = iris_get_ipopt_payload_data(IRIS_IP_RX_2, 0xF0, 2);
			while(count < 20) {
				cmd_2b_page = iris_ocp_read(0xf1840304, DSI_CMD_SET_STATE_HS);
				if (cmd_2b_page != payload[2]) {
					count ++;
					IRIS_LOGW("Warning: cmd_2b_page: %x not right, %d!", cmd_2b_page, count);
					usleep_range(2000, 2100);
					iris_second_channel_pre(true);
				} else
					break;
			}
		}

		pcfg->osd_enable = true;
		if (osd_blending_work.enter_lp_st != MIPI2_LP_FINISH) {
		//	IRIS_LOGE("osd_disable_work is still in queue entry");
			osd_blending_work.enter_lp_st = MIPI2_LP_FINISH;
		}
		//if (pcfg2->panel->power_info.refcount == 0) {
		//	IRIS_LOGW("%s: AP mipi2 tx hasn't been power on.", __func__);
		//	pcfg2->osd_switch_on_pending = true;
		//} else
			iris_osd_blending_on();
			pcfg->osd_on = true;
	} else {
		osd_blending_work.enter_lp_st = MIPI2_LP_PRE;
		pcfg->osd_enable = false;
		iris_osd_blending_off();
		pcfg->osd_on = false;
	}

	return 0;
}

int iris_osd_autorefresh(u32 val)
{
	int osd_gpio = -1;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_SECONDARY);

	if (iris_disable_osd_autorefresh) {
		pcfg->panel->reset_config.iris_osd_autorefresh = false;
		IRIS_LOGI("%s, osd autofresh is disable.", __func__);
		return 0;
	}

	IRIS_LOGI("%s(%d), value: %d", __func__, __LINE__, val);
	if (NULL == pcfg) {
		IRIS_LOGE("%s, no secondary display.", __func__);
		return -EINVAL;
	}

	osd_gpio = pcfg->panel->reset_config.iris_osd_gpio;
	if (!gpio_is_valid(osd_gpio)) {
		IRIS_LOGE("%s, invalid GPIO %d", __func__, osd_gpio);
		return -EINVAL;
	}

	if (val) {
		IRIS_LOGI("%s, enable osd auto refresh", __func__);
		enable_irq(gpio_to_irq(osd_gpio));
		pcfg->panel->reset_config.iris_osd_autorefresh = true;
	} else {
		IRIS_LOGI("%s, disable osd auto refresh", __func__);
		disable_irq(gpio_to_irq(osd_gpio));
		pcfg->panel->reset_config.iris_osd_autorefresh = false;
	}

	return 0;
}

int iris_get_osd_overflow_st(void)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	if (atomic_read(&pcfg->osd_irq_cnt) >= 2)
		return 1;
	else
		return 0;	// overflow, old define
#if 0
	int osd_gpio = -1;
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_SECONDARY);

	IRIS_LOGI("%s(%d)", __func__, __LINE__);
	if (NULL == pcfg) {
		IRIS_LOGE("%s, no secondary display.", __func__);
		return 0;
	}

	osd_gpio = pcfg->panel->reset_config.iris_osd_gpio;
	if (!gpio_is_valid(osd_gpio)) {
		IRIS_LOGE("%s, invalid GPIO %d", __func__, osd_gpio);
		return 0;
	}

	return gpio_get_value(osd_gpio);
#endif
}

void iris_frc_low_latency(bool low_latency) {
	struct iris_cfg *pcfg = iris_get_cfg();
	pcfg->frc_low_latency = low_latency;
	IRIS_LOGI("(%s, %d) low_latency = %d.", __func__, __LINE__, low_latency);
}

void iris_set_panel_te(u8 panel_te) {
	struct iris_cfg *pcfg = iris_get_cfg();
	pcfg->panel_te = panel_te;
}

void iris_set_n2m_enable(bool bEn)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	pcfg->n2m_enable = bEn;
}

int iris_wait_vsync()
{
	struct iris_cfg *pcfg = iris_get_cfg();
	struct drm_encoder *drm_enc;

	if (pcfg->display->bridge == NULL)
		return -ENOLINK;
	drm_enc = pcfg->display->bridge->base.encoder;
	if (!drm_enc || !drm_enc->crtc)
		return -ENOLINK;
	if (sde_encoder_is_disabled(drm_enc))
		return -EIO;

	mutex_unlock(&pcfg->panel->panel_lock);
	sde_encoder_wait_for_event(drm_enc, MSM_ENC_VBLANK);
	mutex_lock(&pcfg->panel->panel_lock);

	return 0;
}

int iris_set_pending_panel_brightness(int32_t pending, int32_t delay, int32_t level)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	IRIS_LOGI("set pending panel %d,%d,%d", pending, delay, level);
	pcfg->panel_pending = pending;
	pcfg->panel_delay = delay;
	pcfg->panel_level = level;

	return 0;
}

int iris5_sync_panel_brightness(int32_t step, void *phys_enc)
{
	struct sde_encoder_phys *phys_encoder = phys_enc;
	struct sde_connector *c_conn = NULL;
	struct dsi_display *display = NULL;
	struct iris_cfg *pcfg;
	int rc = 0;

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

	if (pcfg->panel_pending == step) {
		IRIS_LOGI("sync pending panel %d %d,%d,%d", step, pcfg->panel_pending, pcfg->panel_delay, pcfg->panel_level);
		SDE_ATRACE_BEGIN("sync_panel_brightness");
		if (step <= 2) {
			rc = c_conn->ops.set_backlight(&c_conn->base,
					display, pcfg->panel_level);
			usleep_range(pcfg->panel_delay, pcfg->panel_delay);
		} else {
			usleep_range(pcfg->panel_delay, pcfg->panel_delay);
			rc = c_conn->ops.set_backlight(&c_conn->base,
					display, pcfg->panel_level);
		}
		if (c_conn->bl_device)
			c_conn->bl_device->props.brightness = pcfg->panel_level;
		pcfg->panel_pending = 0;
		SDE_ATRACE_END("sync_panel_brightness");
	}

	return rc;
}

static const struct file_operations iris_split_pkt_fops = {
	.open = simple_open,
	.write = iris_split_pkt_write,
	.read = iris_split_pkt_read,
};

static ssize_t iris_chip_id_read(struct file *file, char __user *buff,
                size_t count, loff_t *ppos)
{
	int tot = 0;
	struct iris_cfg *pcfg = NULL;
	char bp[512];
	if (*ppos)
		return 0;

	pcfg = iris_get_cfg();

	tot = scnprintf(bp, sizeof(bp), "%u\n", pcfg->chip_id);
	if (copy_to_user(buff, bp, tot))
	    return -EFAULT;
	*ppos += tot;

	return tot;
}

static const struct file_operations iris_chip_id_fops = {
	.open = simple_open,
	.read = iris_chip_id_read,
};

static ssize_t iris_power_mode_read(struct file *file, char __user *buff,
                size_t count, loff_t *ppos)
{
	int tot = 0;
	struct iris_cfg *pcfg = NULL;
	char bp[512];
	if (*ppos)
		return 0;

	pcfg = iris_get_cfg();

	tot = scnprintf(bp, sizeof(bp), "0x%02x\n", pcfg->power_mode);
	if (copy_to_user(buff, bp, tot))
	    return -EFAULT;
	*ppos += tot;

	return tot;
}

static const struct file_operations iris_power_mode_fops = {
	.open = simple_open,
	.read = iris_power_mode_read,
};

static ssize_t iris_dbg_i2c_write(struct file *file, const char __user *buff,
	size_t count, loff_t *ppos)
{

	unsigned long val;
	int ret = 0;
	bool is_ulps_enable = 0;
	uint32_t header = 0;
	uint32_t arr[100] = {0};


	if (kstrtoul_from_user(buff, count, 0, &val))
		return -EFAULT;
	IRIS_LOGI("%s,%d", __func__, __LINE__);

	//single write
	header = 0xFFFFFFF4;
	arr[0] = 0xf0000000;
	arr[1] = 0x12345678;

	is_ulps_enable = iris_ulps_enable_get();
	IRIS_LOGI("%s,%d: is_ulps_enable = %d", __func__, __LINE__, is_ulps_enable);
	if (is_ulps_enable)
		iris_ulps_source_sel(ULPS_NONE);
	ret = iris_i2c_ocp_write(arr, 1, 0);
	if (is_ulps_enable)
		iris_ulps_source_sel(ULPS_MAIN);
	if (ret)
		IRIS_LOGE("%s,%d: ret = %d", __func__, __LINE__, ret);

	return count;

}


static ssize_t iris_dbg_i2c_read(struct file *file, char __user *buff,
		size_t count, loff_t *ppos)
{
	int i = 0;
	int cnt = 0;
	int ret = 0;
	bool is_burst = 0;
	bool is_ulps_enable = 0;
	uint32_t arr[100] = {0};

	arr[0] = 0xf0000000;

	is_burst = 1;
	cnt = 5;

	is_ulps_enable = iris_ulps_enable_get();
	IRIS_LOGI("%s,%d: is_ulps_enable = %d", __func__, __LINE__, is_ulps_enable);
	if (is_ulps_enable)
		iris_ulps_source_sel(ULPS_NONE);
	ret = iris_i2c_ocp_read(arr, cnt, is_burst);
	if (is_ulps_enable)
		iris_ulps_source_sel(ULPS_MAIN);

	if (ret) {
		IRIS_LOGE("%s,%d: ret = %d", __func__, __LINE__, ret);
	} else {
		for (i = 0; i < cnt; i++)
			IRIS_LOGI("%s,%d: arr[%d] = %x", __func__, __LINE__, i, arr[i]);
	}
	return 0;
}

static const struct file_operations iris_i2c_srw_fops = {
	.open = simple_open,
	.write = iris_dbg_i2c_write,
	.read = iris_dbg_i2c_read,
};


static ssize_t iris_dbg_loop_back_ops(struct file *file,
       const char __user *buff, size_t count, loff_t *ppos)
{
	unsigned long val;
	struct iris_cfg *pcfg;
	uint32_t temp, values[2];

	pcfg = iris_get_cfg();

	if (kstrtoul_from_user(buff, count, 0, &val))
		return -EFAULT;

	if (0 == val) {
		iris5_reset(pcfg->panel);
		IRIS_LOGE("iris5 reset.");
	} else if (1 == val) {
		iris_exit_abyp(true);
		IRIS_LOGE("iris exit abyp.");
	} else if (2 == val) {
		iris_ocp_write(0xf00000c0, 0x0);
		IRIS_LOGE("enable analog bypass.");
	} else if (3 == val) {
		values[0] = 0xf1800000;
		values[1] = 0;
		iris_i2c_ocp_read(values, 1, 0);
		temp = values[0];
		IRIS_LOGD("%s,%d: value = 0x%x", __func__, __LINE__, temp);
		temp &= (~0x1);
		IRIS_LOGD("%s,%d: value = 0x%x", __func__, __LINE__, temp);
		values[0] = 0xf1800000;
		values[1] = temp;
		iris_i2c_ocp_single_write(values, 1);
		values[0] = 0xf1800000;
		values[1] = 0;
		iris_i2c_ocp_read(values, 1, 0);
		temp = values[0];
		IRIS_LOGD("%s,%d: value = 0x%x", __func__, __LINE__, temp);
		IRIS_LOGE("%s,%d: disable mipi rx\n", __func__, __LINE__);
	} else if (4 == val) {
		iris5_loop_back_verify();
	} else if (5 == val) {
		temp = 0x400;
		values[0] = 0xf0000044;
		values[1] = temp;
		iris_i2c_ocp_single_write(values, 1);
		IRIS_LOGE("%s,%d: rst dtg!\n", __func__, __LINE__);
	} else if (6 == val) {
		temp = 0x55;
		IRIS_LOGD("%s,%d: value = 0x%x", __func__, __LINE__, temp);
		values[0] = 0xf00000c0;
		values[1] = temp;
		iris_i2c_ocp_single_write(values, 1);
		IRIS_LOGE("%s,%d: disable ulps!\n", __func__, __LINE__);
	} else if (7 == val) {
		iris_send_ipopt_cmds(IRIS_IP_SYS, 0x26);
	} else {
		pr_err("%s,%d, parameter error!\n", __func__, __LINE__);
	}

	return count;
}

static ssize_t iris_dbg_loop_back_test(struct file *file, char __user *buff,
		size_t count, loff_t *ppos)
{
	int ret = 0;
	ktime_t ktime0;
	ktime_t ktime1;
	uint32_t timeus = 0;
	struct iris_cfg *pcfg;
	int tot = 0;
	char bp[512];
 
	if (*ppos)
		return 0;

        pcfg = iris_get_cfg();

	mutex_lock(&pcfg->lb_mutex);
	ktime0 = ktime_get();
	ret = iris_loop_back_validate();
	ktime1 = ktime_get();
	timeus = (u32) ktime_to_us(ktime1) - (u32)ktime_to_us(ktime0);
	mutex_unlock(&pcfg->lb_mutex);
	IRIS_LOGI("ret = %d", ret);
	IRIS_LOGI("spend time %d us.", timeus);


        tot = scnprintf(bp, sizeof(bp), "0x%02x\n", ret);
        if (copy_to_user(buff, bp, tot))
            return -EFAULT;
        *ppos += tot;

        return tot;

}

static const struct file_operations iris_loop_back_fops = {
	.open = simple_open,
	.write = iris_dbg_loop_back_ops,
	.read = iris_dbg_loop_back_test,
};

static ssize_t iris_list_debug(struct file *file,
			const char __user *user_buf, size_t count, loff_t *ppos)
{
	uint8_t ip;
	uint8_t opt_id;
	int32_t pos;
	uint32_t value;
	char buf[64];
	uint32_t *payload = NULL;

	if (count > sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	buf[count] = 0; // end if string

	if (sscanf(buf, "%x %x %x %x", &ip, &opt_id, &pos, &value) != 4)
		return -EINVAL;

	payload = iris_get_ipopt_payload_data(ip, opt_id, 2);

	IRIS_LOGI("%x %x %x %x->%x", ip, opt_id, pos, payload[pos], value);

	iris_set_ipopt_payload_data(ip, opt_id, pos, value);

	return count;
}

static const struct file_operations iris_list_debug_fops = {
	.open = simple_open,
	.write = iris_list_debug,
};

static int iris_cont_splash_debugfs_init(struct dsi_display *display)
{
	struct iris_cfg *pcfg;

	pcfg = iris_get_cfg();

	if (pcfg->dbg_root == NULL) {
		pcfg->dbg_root = debugfs_create_dir("iris", NULL);
		if (IS_ERR_OR_NULL(pcfg->dbg_root)) {
			IRIS_LOGE("debugfs_create_dir for iris_debug failed, error %ld",
				   PTR_ERR(pcfg->dbg_root));
			return -ENODEV;
		}
	}
	if (debugfs_create_file("iris_cont_splash", 0644, pcfg->dbg_root, display,
				&iris_cont_splash_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
			__FILE__, __LINE__);
		return -EFAULT;
	}

	if (debugfs_create_file("iris_split_pkt", 0644, pcfg->dbg_root, display,
				&iris_split_pkt_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
			__FILE__, __LINE__);
		return -EFAULT;
	}

	if (debugfs_create_file("chip_id", 0644, pcfg->dbg_root, display,
				&iris_chip_id_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
			__FILE__, __LINE__);
		return -EFAULT;
	}

	if (debugfs_create_file("power_mode", 0644, pcfg->dbg_root, display,
				&iris_power_mode_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
			__FILE__, __LINE__);
		return -EFAULT;
	}

	debugfs_create_u32("disable_osd_autorefresh", 0644, pcfg->dbg_root,
		(u32 *)&iris_disable_osd_autorefresh);
	debugfs_create_u8("iris_pq_update_path", 0644, pcfg->dbg_root,
		(uint8_t *)&iris_pq_update_path);

	if (debugfs_create_file("iris_i2c_srw",	0644, pcfg->dbg_root, display,
				&iris_i2c_srw_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
			__FILE__, __LINE__);
		return -EFAULT;
	}

	if (debugfs_create_file("iris_loop_back",	0644, pcfg->dbg_root, display,
				&iris_loop_back_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
			__FILE__, __LINE__);
		return -EFAULT;
	}

	if (debugfs_create_file("iris_list_debug",	0644, pcfg->dbg_root, display,
				&iris_list_debug_fops) == NULL) {
		IRIS_LOGE("%s(%d): debugfs_create_file: index fail",
			__FILE__, __LINE__);
		return -EFAULT;
	}

	return 0;
}


void iris5_display_prepare(struct dsi_display *display)
{
	static bool iris_boot[IRIS_CFG_NUM];
	struct iris_cfg *pcfg;
	int index;

	index = display->panel->is_secondary ? DSI_SECONDARY : DSI_PRIMARY;
	pcfg = iris_get_cfg_by_index(index);
	if (pcfg->valid < 2)
		return;

	if (pcfg->osd_switch_on_pending) {
		pcfg->osd_switch_on_pending = false;
		//schedule_work(&osd_blending_work.osd_enable_work);
		IRIS_LOGI("%s, call iris_second_channel_power", __func__);
		iris_second_channel_power(true);
	}
	//if (ANALOG_BYPASS_MODE == pcfg->abypss_ctrl.abypass_mode) {
	if (display->panel->is_secondary) {
		return;
	}

	if (iris_boot[index] == false) {
		iris_set_cfg_index(index);
		iris5_parse_lut_cmds(1);
		iris_alloc_seq_space();
		iris_boot[index] = true;
	}
}

irqreturn_t iris_osd_handler(int irq, void *data)
{
	struct dsi_display *display = data;
	struct drm_encoder *enc = NULL;

	if (display == NULL) {
		IRIS_LOGE("%s, invalid display.", __func__);
		return IRQ_NONE;
	}

	IRIS_LOGV("%s: irq: %d, display: %s", __func__, irq, display->name);
	if (display && display->bridge)
		enc = display->bridge->base.encoder;
	if (enc)
		sde_encoder_disable_autorefresh_handler(enc);
	else
		IRIS_LOGW("[%s] no encoder.", __func__);
	return IRQ_HANDLED;
}

void iris_register_osd_irq(void *disp)
{
	int rc = 0;
	int osd_gpio = -1;
	struct dsi_display *display = NULL;
	struct platform_device *pdev = NULL;

	if (!disp) {
		IRIS_LOGE("%s, invalid display.", __func__);
		return;
	}

	display = (struct dsi_display*)disp;
	if (!iris_virtual_display(display)) {
		return;
	}

	osd_gpio = display->panel->reset_config.iris_osd_gpio;
	IRIS_LOGI("%s, for display %s, osd status gpio is %d", __func__, display->name, osd_gpio);
	if (!gpio_is_valid(osd_gpio)) {
		IRIS_LOGE("%s(%d), osd status gpio not specified",
			__func__, __LINE__);
		return;
	}

	pdev = display->pdev;
	IRIS_LOGI("%s, display: %s, irq: %d", __func__, display->name, gpio_to_irq(osd_gpio));
	rc = devm_request_irq(&pdev->dev, gpio_to_irq(osd_gpio), iris_osd_handler,
		IRQF_TRIGGER_RISING, "OSD_GPIO", display);
	if (rc) {
		IRIS_LOGE("%s: IRIS OSD request irq failed", __func__);
		return;
	}

	disable_irq(gpio_to_irq(osd_gpio));
}

bool iris_secondary_display_autorefresh(void *phys_enc)
{
	struct sde_encoder_phys *phys_encoder = phys_enc;
	struct sde_connector *c_conn = NULL;
	struct dsi_display *display = NULL;

	if (phys_encoder == NULL) {
		return false;
	}

	if (phys_encoder->connector == NULL) {
		return false;
	}

	c_conn = to_sde_connector(phys_encoder->connector);
	if (c_conn == NULL) {
		return false;
	}

	display = c_conn->display;
	if (display == NULL) {
		return false;
	}

	if (!iris_virtual_display(display)) {
		return false;
	}

	IRIS_LOGV("%s, auto refresh: %s", __func__,
		display->panel->reset_config.iris_osd_autorefresh ? "true" : "false");
	if (!display->panel->reset_config.iris_osd_autorefresh) {
		display->panel->reset_config.iris_osd_autorefresh_enabled = false;
		return false;
	}

	display->panel->reset_config.iris_osd_autorefresh_enabled = true;
	iris_osd_irq_cnt_clean();
	return true;
}

static void iris_vfr_update_work(struct work_struct *work)
{
	struct iris_cfg *pcfg = container_of(work, struct iris_cfg, vfr_update_work);
	if (atomic_read(&pcfg->video_update_wo_osd) >= 4) {
		if (iris_vfr_update(pcfg, true))
			IRIS_LOGI("enable vfr");
	} else {
		if (iris_vfr_update(pcfg, false))
			IRIS_LOGI("disable vfr");
	}
}

void iris_osd_irq_cnt_clean(void)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	atomic_set(&pcfg->osd_irq_cnt, 0);
}

void iris_osd_irq_cnt_inc(void)
{
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	atomic_inc(&pcfg->osd_irq_cnt);
	IRIS_LOGD("osd_irq: %d", atomic_read(&pcfg->osd_irq_cnt));
}

#if defined(PXLW_IRIS_DUAL)
static void iris_frc_setting_switch(bool dual)
{
	uint32_t *payload = NULL;
	uint32_t process_hres[2];	// 0: single, 1: dual
	uint32_t process_vres[2];
	uint32_t mv_buf_number[2];
	uint32_t video_baseaddr[2];
	uint8_t opt_id[2] = {0xb0, 0xb1};
	struct iris_cfg *pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	int i;
	for (i=0; i<2; i++) {
		payload = iris_get_ipopt_payload_data(IRIS_IP_PWIL, opt_id[i], 2);
		mv_buf_number[i] = (payload[2] >> 16) & 0x7;
		video_baseaddr[i] = payload[7];
		process_hres[i] = payload[10] & 0xffff;
		process_vres[i] = (payload[10] >> 16) & 0xffff;
	}
	i = dual ? 1 : 0;
	pcfg->frc_setting.memc_hres = process_hres[i];
	pcfg->frc_setting.memc_vres = process_vres[i];
	pcfg->frc_setting.mv_buf_num = mv_buf_number[i];
	pcfg->frc_setting.video_baseaddr = video_baseaddr[i];

	if (process_hres[0] != process_hres[1]) {
		iris_scaler_filter_ratio_get();
		IRIS_LOGI("update scaler filter");
	}

}

void iris_dual_setting_switch(bool dual)
{
	struct iris_ctrl_opt arr_single[] = {
		{0x03, 0xa0, 0x01, 0x00},	// IRIS_IP_PWIL, pwil: ctrl graphic
		{0x03, 0xb0, 0x01, 0x00},	// IRIS_IP_PWIL, pwil: video
		{0x03, 0x80, 0x01, 0x00},	// IRIS_IP_PWIL, pwil: update
		{0x0b, 0xf0, 0x01, 0x00},	// IRIS_IP_SCALER1D, scaler1d
		{0x0b, 0xa0, 0x01, 0x00},	// IRIS_IP_SCALER1D, scaler1d: gc
		{0x2f, 0xf0, 0x01, 0x00},	// IRIS_IP_SCALER1D_2, scaler_pp: init
		{0x2d, 0xf0, 0x01, 0x00},	// IRIS_IP_PSR_MIF, psr_mif: init
		{0x11, 0xe2, 0x00, 0x00},	// IRIS_IP_DMA
	};
	struct iris_ctrl_opt arr_dual[] = {
		{0x03, 0xa1, 0x01, 0x00},	// IRIS_IP_PWIL, pwil: ctrl graphic
		{0x03, 0xb1, 0x01, 0x00},	// IRIS_IP_PWIL, pwil: video
		{0x03, 0x80, 0x01, 0x00},	// IRIS_IP_PWIL, pwil: update
		{0x0b, 0xf1, 0x01, 0x00},	// IRIS_IP_SCALER1D, scaler1d
		{0x0b, 0xa0, 0x01, 0x00},	// IRIS_IP_SCALER1D, scaler1d: gc
		{0x2f, 0xf1, 0x01, 0x00},	// IRIS_IP_SCALER1D_2, scaler_pp: init
		{0x2d, 0xf1, 0x01, 0x00},	// IRIS_IP_PSR_MIF, psr_mif: init
		{0x11, 0xe2, 0x00, 0x00},	// IRIS_IP_DMA
	};
	struct iris_ctrl_opt *opt_arr = dual ? arr_dual : arr_single;
	int len = sizeof(arr_single)/sizeof(struct iris_ctrl_opt);
	iris_send_assembled_pkt(opt_arr, len);
	IRIS_LOGI("iris_dual_setting_switch, dual: %d, len: %d", dual, len);
	iris_frc_setting_switch(dual);
}

void iris_frc_dsc_setting(bool dual)
{
	struct iris_ctrl_opt arr_single[] = {
		{0x25, 0xf1, 0x01, 0x00},	// IRIS_IP_DSC_DEN_2, dsc_encoder_frc: init
		{0x24, 0xf1, 0x01, 0x00},	// IRIS_IP_DSC_ENC_2, dsc_encoder_frc: init
		{0x11, 0xe8, 0x00, 0x00},	// IRIS_IP_DMA
	};
	struct iris_ctrl_opt arr_dual[] = {
		{0x25, 0xf2, 0x01, 0x00},	// IRIS_IP_DSC_DEN_2
		{0x24, 0xf2, 0x01, 0x00},	// IRIS_IP_DSC_ENC_2
		{0x11, 0xe8, 0x00, 0x00},	// IRIS_IP_DMA
	};
	struct iris_ctrl_opt *opt_arr = dual ? arr_dual : arr_single;
	int len = sizeof(arr_single)/sizeof(struct iris_ctrl_opt);
	iris_send_assembled_pkt(opt_arr, len);
	IRIS_LOGI("iris_frc_dsc_setting, dual: %d, len: %d", dual, len);
}
#endif
