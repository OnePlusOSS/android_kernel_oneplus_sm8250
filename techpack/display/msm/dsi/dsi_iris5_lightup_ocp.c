#include <linux/vmalloc.h>
#include <video/mipi_display.h>
#include "dsi_iris5_api.h"
#include "dsi_iris5_lightup.h"
#include "dsi_iris5_lightup_ocp.h"
#include "dsi_iris5_lp.h"
#include "iris_log.h"

/*
 * These files include source code for the Licensed Software that is provided under the terms and
 * conditions of a Master Software License Agreement and its applicable schedules.
 */

#define IRIS_TX_HV_PAYLOAD_LEN   120
#define IRIS_TX_PAYLOAD_LEN 124
#define IRIS_PT_RD_CMD_NUM 3
#define IRIS_RD_PACKET_DATA  0xF189C018
#define IRIS_RD_PACKET_DATA_I3  0xF0C1C018

extern int iris_w_path_select;
extern int iris_r_path_select;
static char iris_read_cmd_rbuf[16];
static struct iris_ocp_cmd ocp_cmd;
static struct iris_ocp_cmd ocp_test_cmd[DSI_CMD_CNT];
static struct dsi_cmd_desc iris_test_cmd[DSI_CMD_CNT];

static void iris_cmd_reg_add(
		struct iris_ocp_cmd *pcmd, u32 addr, u32 val)
{
	*(u32 *)(pcmd->cmd + pcmd->cmd_len) = cpu_to_le32(addr);
	*(u32 *)(pcmd->cmd + pcmd->cmd_len + 4) = cpu_to_le32(val);
	pcmd->cmd_len += 8;
}

static void iris_cmd_add(struct iris_ocp_cmd *pcmd, u32 payload)
{
	*(u32 *)(pcmd->cmd + pcmd->cmd_len) = cpu_to_le32(payload);
	pcmd->cmd_len += 4;
}

void iris_ocp_write(u32 address, u32 value)
{
	struct iris_ocp_cmd ocp_cmd;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct dsi_cmd_desc iris_ocp_cmd[] = {
		{{0, MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, 0, CMD_PKT_SIZE, ocp_cmd.cmd, 0, NULL}, 1, 0}};

	memset(&ocp_cmd, 0, sizeof(ocp_cmd));

	iris_cmd_add(&ocp_cmd, 0xFFFFFFF0 | OCP_SINGLE_WRITE_BYTEMASK);
	iris_cmd_reg_add(&ocp_cmd, address, value);
	iris_ocp_cmd[0].msg.tx_len = ocp_cmd.cmd_len;
	IRIS_LOGD("[ocp][write]addr=0x%08x, value=0x%08x", address, value);

	iris5_dsi_cmds_send(pcfg->panel, iris_ocp_cmd, 1, DSI_CMD_SET_STATE_HS);
}

void iris_ocp_write2(u32 header, u32 address, u32 size, u32 *pvalues)
{
	struct iris_ocp_cmd ocp_cmd;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct dsi_cmd_desc iris_ocp_cmd[] = {
		{{0, MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, 0, CMD_PKT_SIZE, ocp_cmd.cmd, 0, NULL}, 1, 0}};
	u32 max_size = CMD_PKT_SIZE / 4 - 2;
	u32 i;

	while (size > 0) {
		memset(&ocp_cmd, 0, sizeof(ocp_cmd));

		iris_cmd_add(&ocp_cmd, header);
		iris_cmd_add(&ocp_cmd, address);
		if (size < max_size) {
			for (i = 0; i < size; i++)
				iris_cmd_add(&ocp_cmd, pvalues[i]);

			size = 0;
		} else {
			for (i = 0; i < max_size; i++)
				iris_cmd_add(&ocp_cmd, pvalues[i]);

			address += max_size * 4;
			pvalues += max_size;
			size -= max_size;
		}
		iris_ocp_cmd[0].msg.tx_len = ocp_cmd.cmd_len;
		IRIS_LOGD("[ocp][write2]header=0x%08x, addr=0x%08x, dlen=%zu",
				header, address, iris_ocp_cmd[0].msg.tx_len);

		iris5_dsi_cmds_send(pcfg->panel, iris_ocp_cmd, 1, DSI_CMD_SET_STATE_HS);
	}
}


/*pvalues need to be one address and one value*/
static void iris_dsi_ocp_write3(u32 size, u32 *pvalues)
{
	u32 header = 0xFFFFFFF4;
	struct iris_ocp_cmd ocp_cmd;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct dsi_cmd_desc iris_ocp_cmd[] = {
		{{0, MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, 0, CMD_PKT_SIZE, ocp_cmd.cmd, 0, NULL}, 1, 0}};
	/*need to remove one header length*/
	u32 max_size = 60; /*(244 -4)/4*/
	u32 i;

	if (size % 2 != 0) {
		IRIS_LOGE("need to be mult pair of  address and value");
		return;
	}

	while (size > 0) {
		memset(&ocp_cmd, 0, sizeof(ocp_cmd));

		iris_cmd_add(&ocp_cmd, header);
		if (size < max_size) {
			for (i = 0; i < size; i++)
				iris_cmd_add(&ocp_cmd, pvalues[i]);

			size = 0;
		} else {
			for (i = 0; i < max_size; i++)
				iris_cmd_add(&ocp_cmd, pvalues[i]);

			pvalues += max_size;
			size -= max_size;
		}
		iris_ocp_cmd[0].msg.tx_len = ocp_cmd.cmd_len;
		IRIS_LOGD("[ocp][write3]header=0x%08x, dlen=%zu",
				header, iris_ocp_cmd[0].msg.tx_len);

		iris5_dsi_cmds_send(pcfg->panel, iris_ocp_cmd, 1, DSI_CMD_SET_STATE_HS);
	}

}
static void iris_i2c_ocp_write3(u32 size, u32 *pvalues)
{
	int ret = 0;
	bool is_burst = 0;
	bool is_ulps_enable = 0;

	if (size % 2 != 0) {
		IRIS_LOGE("need to be mult pair of  address and value");
		return;
	}

	is_ulps_enable = iris_disable_ulps(PATH_I2C);
	ret = iris_i2c_ocp_write(pvalues, size/2, is_burst);
	iris_enable_ulps(PATH_I2C, is_ulps_enable);

	if (ret)
		IRIS_LOGE("%s,%d: i2c send fail, ret = %d", __func__, __LINE__, ret);

	return;
}
/*pvalues need to be one address and one value*/
void iris_ocp_write3(u32 size, u32 *pvalues)
{
	int path = iris_w_path_select;

	if (path == PATH_I2C) {
		IRIS_LOGD("%s,%d: path select i2c", __func__, __LINE__);
		iris_i2c_ocp_write3(size, pvalues);
	} else if (path == PATH_DSI) {
		IRIS_LOGD("%s,%d: path select dsi", __func__, __LINE__);
		iris_dsi_ocp_write3(size, pvalues);
	} else {
		IRIS_LOGE("%s,%d: path not i2c or dsi, path = %d", __func__, __LINE__, path);
	}
}

void iris_ocp_write_address(u32 address, u32 mode)
{
	struct iris_ocp_cmd ocp_cmd;
	struct iris_cfg *pcfg = iris_get_cfg();
	struct dsi_cmd_desc iris_ocp_cmd[] = {
		{{0, MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, 0, CMD_PKT_SIZE, ocp_cmd.cmd, 0, NULL}, 1, 0}};

	/* Send OCP command.*/
	memset(&ocp_cmd, 0, sizeof(ocp_cmd));
	iris_cmd_add(&ocp_cmd, OCP_SINGLE_READ);
	iris_cmd_add(&ocp_cmd, address);
	iris_ocp_cmd[0].msg.tx_len = ocp_cmd.cmd_len;

	iris5_dsi_cmds_send(pcfg->panel, iris_ocp_cmd, 1, mode);
}

u32 iris_ocp_read_value(u32 mode)
{
	struct iris_cfg *pcfg = iris_get_cfg();

	char pi_read[1] = {0x00};
	struct dsi_cmd_desc pi_read_cmd[] = {
		{{0, MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM, MIPI_DSI_MSG_REQ_ACK, 0, 0, sizeof(pi_read), pi_read, 0, NULL}, 1, 0}};
	u32 response_value;

	/* Read response.*/
	memset(iris_read_cmd_rbuf, 0, sizeof(iris_read_cmd_rbuf));
	pi_read_cmd[0].msg.rx_len = 4;
	pi_read_cmd[0].msg.rx_buf = iris_read_cmd_rbuf;
	iris5_dsi_cmds_send(pcfg->panel, pi_read_cmd, 1, mode);
	IRIS_LOGD("read register %02x %02x %02x %02x",
			iris_read_cmd_rbuf[0], iris_read_cmd_rbuf[1], iris_read_cmd_rbuf[2], iris_read_cmd_rbuf[3]);
	response_value = iris_read_cmd_rbuf[0] | (iris_read_cmd_rbuf[1] << 8) |
			(iris_read_cmd_rbuf[2] << 16) | (iris_read_cmd_rbuf[3] << 24);

	return response_value;
}

static u32 iris_i2c_ocp_singleread(u32 address)
{
	u32 arr[2] = {0};
	bool is_burst = 0;
	int ret = 0;
	bool is_ulps_enable = 0;

	arr[0] = address;
	is_ulps_enable = iris_disable_ulps(PATH_I2C);
	ret = iris_i2c_ocp_read(arr, 1, is_burst);
	iris_enable_ulps(PATH_I2C, is_ulps_enable);
	if (ret) {
		IRIS_LOGE("%s,%d: i2c ocp single read fail, ret = %d", __func__, __LINE__, ret);
		return 0;
	} else {
		IRIS_LOGD("%s(), addr=%#x, value=%#x", __func__, address, arr[0]);
		return arr[0];
	}
}

u32 iris_dsi_ocp_read(u32 address, u32 mode)
{
	u32 value = 0;

	iris_ocp_write_address(address, mode);

	value = iris_ocp_read_value(mode);
	IRIS_LOGD("%s(), addr=%#x, value=%#x", __func__, address, value);

	return value;
}

u32 iris_ocp_read(u32 address, u32 mode)
{
	u32 value = 0;
	int path = iris_r_path_select;

	if (path == PATH_I2C) {
		IRIS_LOGD("%s,%d: path select i2c", __func__, __LINE__);
		value = iris_i2c_ocp_singleread(address);
	} else if (path == PATH_DSI) {
		IRIS_LOGD("%s,%d: path select dsi", __func__, __LINE__);
		value = iris_dsi_ocp_read(address, mode);
	} else {
		IRIS_LOGE("%s,%d: path not i2c or dsi, path = %d", __func__, __LINE__, path);
	}

	return value;
}


void iris_write_test(struct dsi_panel *panel, u32 iris_addr, int ocp_type, u32 pkt_size)
{
	union iris_ocp_cmd_header ocp_header;
	struct dsi_cmd_desc iris_cmd = {
		{0, MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, 0, CMD_PKT_SIZE, ocp_cmd.cmd, 0, NULL}, 1, 0};

	u32 test_value = 0xFFFF0000;
	memset(&ocp_header, 0, sizeof(ocp_header));
	ocp_header.header32 = 0xFFFFFFF0 | ocp_type;

	memset(&ocp_cmd, 0, sizeof(ocp_cmd));
	memcpy(ocp_cmd.cmd, &ocp_header.header32, OCP_HEADER);
	ocp_cmd.cmd_len = OCP_HEADER;

	switch (ocp_type) {
	case OCP_SINGLE_WRITE_BYTEMASK:
	case OCP_SINGLE_WRITE_BITMASK:
		for (; ocp_cmd.cmd_len <= (pkt_size - 8); ) {
			iris_cmd_reg_add(&ocp_cmd, iris_addr, test_value);
			test_value++;
			}
		break;

	case OCP_BURST_WRITE:
		test_value = 0xFFFF0000;
		iris_cmd_reg_add(&ocp_cmd, iris_addr, test_value);
		if (pkt_size <= ocp_cmd.cmd_len)
			break;
		test_value++;
		for (; ocp_cmd.cmd_len <= pkt_size - 4;) {
			iris_cmd_add(&ocp_cmd, test_value);
			test_value++;
		}
		break;
	default:
		break;
	}

	IRIS_LOGI("len=%d iris_addr=%#x  test_value=%#x", ocp_cmd.cmd_len, iris_addr, test_value);
	iris_cmd.msg.tx_len = ocp_cmd.cmd_len;

	iris5_dsi_cmds_send(panel, &iris_cmd, 1, DSI_CMD_SET_STATE_HS);

	IRIS_IF_LOGD() {
		iris_dump_packet(ocp_cmd.cmd, ocp_cmd.cmd_len);
	}
}


void iris_write_test_muti_pkt(struct dsi_panel *panel, struct iris_ocp_dsi_tool_input * ocp_input)
{
	union iris_ocp_cmd_header ocp_header;
	u32 test_value = 0xFF000000;
	int iCnt = 0;

	u32 iris_addr, ocp_type, pkt_size, totalCnt;

	ocp_type = ocp_input->iris_ocp_type;
	test_value = ocp_input->iris_ocp_value;
	iris_addr = ocp_input->iris_ocp_addr;
	totalCnt = ocp_input->iris_ocp_cnt;
	pkt_size = ocp_input->iris_ocp_size;

	memset(iris_test_cmd, 0, sizeof(iris_test_cmd));
	memset(ocp_test_cmd, 0, sizeof(ocp_test_cmd));

	memset(&ocp_header, 0, sizeof(ocp_header));
	ocp_header.header32 = 0xFFFFFFF0 | ocp_type;

	switch (ocp_type) {
	case OCP_SINGLE_WRITE_BYTEMASK:
	case OCP_SINGLE_WRITE_BITMASK:

		for (iCnt = 0; iCnt < totalCnt; iCnt++) {

			memcpy(ocp_test_cmd[iCnt].cmd,
					&ocp_header.header32, OCP_HEADER);
			ocp_test_cmd[iCnt].cmd_len = OCP_HEADER;

			//CID799190
			test_value = 0xFF000000 | (iCnt << 16);
			while (ocp_test_cmd[iCnt].cmd_len <= (pkt_size - 8)) {
				iris_cmd_reg_add(&ocp_test_cmd[iCnt],
					(iris_addr + iCnt*4), test_value);
				test_value++;
			}

			iris_test_cmd[iCnt].msg.type = MIPI_DSI_GENERIC_LONG_WRITE;
			iris_test_cmd[iCnt].msg.tx_len= ocp_test_cmd[iCnt].cmd_len;
			iris_test_cmd[iCnt].msg.tx_buf = ocp_test_cmd[iCnt].cmd;
		}
		iris_test_cmd[totalCnt - 1].last_command = true;
		break;

	case OCP_BURST_WRITE:
		for (iCnt = 0; iCnt < totalCnt; iCnt++) {
			memcpy(ocp_test_cmd[iCnt].cmd,
					&ocp_header.header32, OCP_HEADER);
			ocp_test_cmd[iCnt].cmd_len = OCP_HEADER;
			//CID799190
			test_value = 0xFF000000 | (iCnt << 16);

			iris_cmd_reg_add(&ocp_test_cmd[iCnt],
					(iris_addr + iCnt*4), test_value);
			/*if(pkt_size <= ocp_test_cmd[iCnt].cmd_len)
				break;*/
			test_value++;
			while (ocp_test_cmd[iCnt].cmd_len <= pkt_size - 4) {
				iris_cmd_add(&ocp_test_cmd[iCnt], test_value);
				test_value++;
			}

			iris_test_cmd[iCnt].msg.type = MIPI_DSI_GENERIC_LONG_WRITE;
			iris_test_cmd[iCnt].msg.tx_len = ocp_test_cmd[iCnt].cmd_len;
			iris_test_cmd[iCnt].msg.tx_buf = ocp_test_cmd[iCnt].cmd;

		}
		iris_test_cmd[totalCnt - 1].last_command = true;
		break;
	default:
		break;

	}

	IRIS_LOGI("%s: totalCnt=%#x iris_addr=%#x test_value=%#x",
		__func__, totalCnt, iris_addr, test_value);
	iris5_dsi_cmds_send(panel, iris_test_cmd, totalCnt, DSI_CMD_SET_STATE_HS);

	IRIS_IF_NOT_LOGV() {
		return;
	}
	for (iCnt = 0; iCnt < totalCnt; iCnt++)
		iris_dump_packet(ocp_test_cmd[iCnt].cmd,
			ocp_test_cmd[iCnt].cmd_len);

}

int iris5_dsi_cmds_send(struct dsi_panel *panel,
				struct dsi_cmd_desc *cmds,
				u32 count,
				enum dsi_cmd_set_state state)
{
	int rc = 0;
	int i = 0;
	ssize_t len;
	const struct mipi_dsi_host_ops *ops;
	struct iris_cfg *pcfg = NULL;
	struct dsi_display * display = NULL;

	if (!panel || !panel->cur_mode)
		return -EINVAL;

	if (count == 0) {
		IRIS_LOGD("[%s] No commands to be sent for state %d",
			 panel->name, state);
		goto error;
	}

	pcfg = iris_get_cfg();
	display = pcfg->display;

	//CID799182
	ops = panel->host->ops;

	for (i = 0; i < count; i++) {
		if (state == DSI_CMD_SET_STATE_LP)
			cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;

		if (cmds->last_command)
			cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;

		dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_ALL_CLKS, DSI_CLK_ON);
		len = ops->transfer(panel->host, &cmds->msg);
		dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_ALL_CLKS, DSI_CLK_OFF);

		IRIS_IF_LOGVV() {
			iris_dump_packet((u8 *) cmds->msg.tx_buf, cmds->msg.tx_len);
		}

		if (len < 0) {
			rc = len;
			IRIS_LOGE("failed to set cmds(%d), rc=%d", cmds->msg.type, rc);
			dump_stack();
			goto error;
		}
		if (cmds->post_wait_ms)
			usleep_range(cmds->post_wait_ms*1000,
					((cmds->post_wait_ms*1000)+10));
		cmds++;
	}
error:
	return rc;
}

static u32 iris_pt_split_pkt_cnt(int dlen)
{
	u32 sum = 1;

	if (dlen > IRIS_TX_HV_PAYLOAD_LEN)
		sum = (dlen - IRIS_TX_HV_PAYLOAD_LEN
			+ IRIS_TX_PAYLOAD_LEN - 1) / IRIS_TX_PAYLOAD_LEN + 1;
	return sum;
}


/*
* @Description: use to do statitics for cmds which should not less than 252
*               if the payload is out of 252, it will change to more than one cmds
the first payload need to be
	4 (ocp_header) + 8 (tx_addr_header + tx_val_header)
	+ 2* payload_len (TX_payloadaddr + payload_len)<= 252
the sequence payloader need to be
	4 (ocp_header) + 2* payload_len (TX_payloadaddr + payload_len)<= 252
	so the first payload should be no more than 120
	the second and sequence need to be no more than 124

* @Param: cmdset  cmds request
* @return: the cmds number need to split
*/
static u32 iris_pt_calc_cmds_num(struct dsi_panel_cmd_set *cmdset)
{
	u32 i = 0;
	u32 sum = 0;
	u32 dlen = 0;

	for (i = 0; i < cmdset->count; i++) {
		dlen = cmdset->cmds[i].msg.tx_len;

		sum += iris_pt_split_pkt_cnt(dlen);
	}
	return sum;
}

static int iris_pt_alloc_cmds_space(
		struct dsi_panel_cmd_set *cmdset, struct dsi_cmd_desc **ptx_cmds,
		struct iris_ocp_cmd **pocp_cmds)
{
	int cmds_cnt = 0;

	cmds_cnt = iris_pt_calc_cmds_num(cmdset);

	IRIS_LOGD("[%s:%d] cmds_cnt: %d vmalloc len = %lu", __func__, __LINE__, cmds_cnt, cmds_cnt * sizeof(**ptx_cmds));
	*ptx_cmds = (struct dsi_cmd_desc *) vmalloc(cmds_cnt *sizeof(**ptx_cmds));
	if (!(*ptx_cmds)) {
		IRIS_LOGE("can not vmalloc len = %lu", cmds_cnt * sizeof(**ptx_cmds));
		return -ENOMEM;
	}

	*pocp_cmds = (struct iris_ocp_cmd *)vmalloc(cmds_cnt * sizeof(**pocp_cmds));
	if (!(*pocp_cmds)) {
		IRIS_LOGE("can not vmalloc pocp cmds");
		vfree(*ptx_cmds);
		*ptx_cmds = NULL;
		return -ENOMEM;
	}
	return cmds_cnt;
}

static void iris_pt_init_tx_cmd_header(
		struct dsi_panel_cmd_set *cmdset, struct dsi_cmd_desc *dsi_cmd,
		union iris_mipi_tx_cmd_header *header)
{
	u8 dtype = 0;

	dtype = dsi_cmd->msg.type;

	memset(header, 0x00, sizeof(*header));
	header->stHdr.dtype = dtype;
	header->stHdr.linkState = (cmdset->state == DSI_CMD_SET_STATE_LP) ? 1 : 0;
}


static void iris_pt_set_cmd_header(
		union iris_mipi_tx_cmd_header *pheader,
		struct dsi_cmd_desc *dsi_cmd, bool is_write)
{
	u32 dlen = 0;
	u8 * ptr = NULL;

	if (dsi_cmd == NULL)
		return;

	dlen = dsi_cmd->msg.tx_len;

	if (is_write)
		pheader->stHdr.writeFlag = 0x01;
	else
		pheader->stHdr.writeFlag = 0x00;

	if (pheader->stHdr.longCmdFlag == 0) {
		ptr = (u8 *)dsi_cmd->msg.tx_buf;
		if (dlen == 1) {
			pheader->stHdr.len[0] = ptr[0];
		} else if (dlen == 2) {
			pheader->stHdr.len[0] = ptr[0];
			pheader->stHdr.len[1] = ptr[1];
		}
	} else {
		pheader->stHdr.len[0] = dlen & 0xff;
		pheader->stHdr.len[1] = (dlen >> 8) & 0xff;
	}
}

static void iris_pt_set_wrcmd_header(
		union iris_mipi_tx_cmd_header *pheader,
		struct dsi_cmd_desc *dsi_cmd)
{
	iris_pt_set_cmd_header(pheader, dsi_cmd, true);
}


static void iris_pt_set_rdcmd_header(
		union iris_mipi_tx_cmd_header *pheader,
		struct dsi_cmd_desc *dsi_cmd)
{
	iris_pt_set_cmd_header(pheader, dsi_cmd, false);
}

static void iris_pt_init_ocp_cmd(struct iris_ocp_cmd *pocp_cmd)
{
	union iris_ocp_cmd_header ocp_header;

	if (!pocp_cmd) {
		IRIS_LOGE("pocp_cmd is null");
		return;
	}

	memset(pocp_cmd, 0x00, sizeof(*pocp_cmd));
	ocp_header.header32 = 0xfffffff0 | OCP_SINGLE_WRITE_BYTEMASK;
	memcpy(pocp_cmd->cmd, &ocp_header.header32, OCP_HEADER);
	pocp_cmd->cmd_len = OCP_HEADER;
}

static void iris_add_tx_cmds(
		struct dsi_cmd_desc *ptx_cmd,
		struct iris_ocp_cmd *pocp_cmd, u8 wait)
{
	struct dsi_cmd_desc desc_init_val = {
		{0, MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, 0, CMD_PKT_SIZE, NULL, 0, NULL}, 1, 0};

	memcpy(ptx_cmd, &desc_init_val, sizeof(struct dsi_cmd_desc));
	ptx_cmd->msg.tx_buf = pocp_cmd->cmd;
	ptx_cmd->msg.tx_len = pocp_cmd->cmd_len;
	ptx_cmd->post_wait_ms = wait;
}

static u32 iris_pt_short_write(
		struct iris_ocp_cmd *pocp_cmd,
		union iris_mipi_tx_cmd_header *pheader,
		struct dsi_cmd_desc *dsi_cmd)
{
	u32 sum = 1;
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 address = pcfg->chip_ver == IRIS5_CHIP_VERSION ?
		IRIS_MIPI_TX_HEADER_ADDR : IRIS_MIPI_TX_HEADER_ADDR_I3;

	pheader->stHdr.longCmdFlag = 0x00;

	iris_pt_set_wrcmd_header(pheader, dsi_cmd);

	IRIS_LOGD("%s, line%d, header=0x%4x",
			__func__, __LINE__, pheader->hdr32);
	iris_cmd_reg_add(pocp_cmd, address, pheader->hdr32);

	return sum;
}

static u32 iris_pt_short_read(
		struct iris_ocp_cmd *pocp_cmd,
		union iris_mipi_tx_cmd_header *pheader,
		struct dsi_cmd_desc *dsi_cmd)
{
	u32 sum = 1;
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 address = pcfg->chip_ver == IRIS5_CHIP_VERSION ?
		IRIS_MIPI_TX_HEADER_ADDR : IRIS_MIPI_TX_HEADER_ADDR_I3;

	pheader->stHdr.longCmdFlag = 0x00;
	iris_pt_set_rdcmd_header(pheader, dsi_cmd);

	IRIS_LOGD("%s, line%d, header=0x%4x",
			__func__, __LINE__, pheader->hdr32);
	iris_cmd_reg_add(pocp_cmd, address, pheader->hdr32);

	return sum;
}


static u32 iris_pt_split_pkt_len(u16 dlen, int sum, int k)
{
	u16 split_len = 0;

	if (k == 0)
		split_len = dlen <  IRIS_TX_HV_PAYLOAD_LEN
					? dlen : IRIS_TX_HV_PAYLOAD_LEN;
	else if (k == sum - 1)
		split_len = dlen - IRIS_TX_HV_PAYLOAD_LEN
				- (k - 1) * IRIS_TX_PAYLOAD_LEN;
	else
		split_len = IRIS_TX_PAYLOAD_LEN;

	return split_len;
}

static void iris_pt_add_split_pkt_payload(
		struct iris_ocp_cmd *pocp_cmd, u8 *ptr, u16 split_len)
{
	u32 i = 0;
	union iris_mipi_tx_cmd_payload payload;
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 address = pcfg->chip_ver == IRIS5_CHIP_VERSION ?
		IRIS_MIPI_TX_PAYLOAD_ADDR : IRIS_MIPI_TX_PAYLOAD_ADDR_I3;

	memset(&payload, 0x00, sizeof(payload));
	for (i = 0; i < split_len; i += 4, ptr += 4) {
		if (i + 4 > split_len) {
			payload.pld32 = 0;
			memcpy(payload.p, ptr, split_len - i);
		} else
			payload.pld32 = *(u32 *)ptr;

		IRIS_LOGD("payload=%#x", payload.pld32);
		iris_cmd_reg_add(pocp_cmd, address,
				payload.pld32);
	}
}

static u32 iris_pt_long_write(
		struct iris_ocp_cmd *pocp_cmd,
		union iris_mipi_tx_cmd_header *pheader,
		struct dsi_cmd_desc *dsi_cmd)
{
	u8 *ptr = NULL;
	u32 i = 0;
	u32 sum = 0;
	u16 dlen = 0;
	u32 split_len = 0;
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 address = pcfg->chip_ver == IRIS5_CHIP_VERSION ?
		IRIS_MIPI_TX_HEADER_ADDR : IRIS_MIPI_TX_HEADER_ADDR_I3;

	dlen = dsi_cmd->msg.tx_len;

	pheader->stHdr.longCmdFlag = 0x1;
	iris_pt_set_wrcmd_header(pheader, dsi_cmd);

	IRIS_LOGD("%s, line%d, header=%#x",
			__func__, __LINE__, pheader->hdr32);
	iris_cmd_reg_add(pocp_cmd, address,
			pheader->hdr32);

	ptr = (u8 *)dsi_cmd->msg.tx_buf;
	sum = iris_pt_split_pkt_cnt(dlen);

	while (i < sum) {
		ptr += split_len;

		split_len = iris_pt_split_pkt_len(dlen, sum, i);

		iris_pt_add_split_pkt_payload(pocp_cmd + i, ptr, split_len);

		i++;
		if (i < sum)
			iris_pt_init_ocp_cmd(pocp_cmd + i);
	}
	return sum;
}

static u32 iris_pt_add_cmd(
		struct dsi_cmd_desc *ptx_cmd, struct iris_ocp_cmd *pocp_cmd,
		struct dsi_cmd_desc *dsi_cmd, struct dsi_panel_cmd_set *cmdset)
{
	u32 i = 0;
	u16 dtype = 0;
	u32 sum = 0;
	u8 wait = 0;
	union iris_mipi_tx_cmd_header header;

	iris_pt_init_tx_cmd_header(cmdset, dsi_cmd, &header);

	dtype = dsi_cmd->msg.type;
	switch (dtype) {
		case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
		case MIPI_DSI_DCS_READ:
		case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
		case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
			sum = iris_pt_short_read(pocp_cmd, &header, dsi_cmd);
			break;
		case MIPI_DSI_DCS_SHORT_WRITE:
		case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
		case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
		case MIPI_DSI_DCS_COMPRESSION_MODE:
		case MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE:
			sum = iris_pt_short_write(pocp_cmd, &header, dsi_cmd);
			break;
		case MIPI_DSI_GENERIC_LONG_WRITE:
		case MIPI_DSI_DCS_LONG_WRITE:
		case MIPI_DSI_PPS_LONG_WRITE:
			sum = iris_pt_long_write(pocp_cmd, &header,dsi_cmd);
			break;
		default:
			IRIS_LOGE("could not identify the type = %#x",
					dsi_cmd->msg.type);
			break;
	}

	for (i = 0; i < sum; i++) {
		wait = (i == sum - 1) ? dsi_cmd->post_wait_ms : 0;
		iris_add_tx_cmds(ptx_cmd + i, pocp_cmd + i, wait);
	}
	return sum;
}

static void iris_pt_send_cmds(
		struct dsi_panel *panel,
		struct dsi_cmd_desc *ptx_cmds, u32 cmds_cnt)
{
	struct dsi_panel_cmd_set panel_cmds;

	memset(&panel_cmds, 0x00, sizeof(panel_cmds));

	panel_cmds.cmds = ptx_cmds;
	panel_cmds.count = cmds_cnt;
	panel_cmds.state = DSI_CMD_SET_STATE_HS;
	iris5_dsi_cmds_send(panel, panel_cmds.cmds, panel_cmds.count, panel_cmds.state);

	if (IRIS_CONT_SPLASH_LK == iris_get_cont_splash_type()) {
		iris_print_cmds(panel_cmds.cmds, panel_cmds.count, panel_cmds.state);
	}
}


void iris_panel_cmd_passthrough_wr(
		struct dsi_panel *panel, struct dsi_panel_cmd_set *cmdset)
{
	u32 i = 0;
	u32 j = 0;
	int cmds_cnt = 0;
	u32 offset = 0;
	struct iris_ocp_cmd *pocp_cmds = NULL;
	struct dsi_cmd_desc *ptx_cmds = NULL;
	struct dsi_cmd_desc *dsi_cmds = NULL;

	if (!panel || !cmdset) {
		IRIS_LOGE("cmdset is null!");
		return;
	}

	if (cmdset->count == 0) {
		IRIS_LOGI("cmdset count is zero!");
		return;
	}

	cmds_cnt =  iris_pt_alloc_cmds_space(cmdset, &ptx_cmds, &pocp_cmds);
	if (cmds_cnt < 0) {
		IRIS_LOGE("cmds_cnt is negative!");
		return;
	}

	for (i = 0; i < cmdset->count; i++) {
		/*initial val*/
		dsi_cmds = cmdset->cmds + i;

		iris_pt_init_ocp_cmd(pocp_cmds + j);

		offset = iris_pt_add_cmd(
				ptx_cmds + j, pocp_cmds + j, dsi_cmds, cmdset);
		j += offset;
	}

	if (j != (u32)cmds_cnt)
		IRIS_LOGE("cmds cnt is not right real cmds_cnt = %d j = %d", cmds_cnt, j);
	else
		iris_pt_send_cmds(panel, ptx_cmds, (u32)cmds_cnt);

	vfree(pocp_cmds);
	vfree(ptx_cmds);
	pocp_cmds = NULL;
	ptx_cmds = NULL;
}

static void iris_pt_switch_cmd(
		struct dsi_panel *panel,
		struct dsi_panel_cmd_set *cmdset,
		struct dsi_cmd_desc *dsi_cmd)
{
	if (!cmdset || !panel || !dsi_cmd) {
		IRIS_LOGE("%s there have null pointer", __func__);
		return;
	}

	cmdset->cmds = dsi_cmd;
	cmdset->count = 1;
}

static int iris_pt_write_max_ptksize(
		struct dsi_panel *panel,
		struct dsi_panel_cmd_set *cmdset)
{
	u32 rlen = 0;
	struct dsi_panel_cmd_set local_cmdset;
	static char max_pktsize[2] = {0x00, 0x00}; /* LSB tx first, 10 bytes */
	static struct dsi_cmd_desc pkt_size_cmd = {
		{0, MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE, MIPI_DSI_MSG_REQ_ACK, 0, 0, sizeof(max_pktsize), max_pktsize, 0, NULL}, 1, 0};

	rlen = cmdset->cmds[0].msg.rx_len;
	if (rlen > 128) {
		IRIS_LOGE("dlen = %d  > 128", rlen);
		return -EINVAL;
	}

	max_pktsize[0] = (rlen & 0xFF);

	memset(&local_cmdset, 0x00, sizeof(local_cmdset));

	iris_pt_switch_cmd(panel, &local_cmdset, &pkt_size_cmd);

	iris_panel_cmd_passthrough_wr(panel, &local_cmdset);

	return 0;
}


static void iris_pt_write_rdcmd_to_panel(
		struct dsi_panel *panel, struct dsi_panel_cmd_set *cmdset)
{
	struct dsi_panel_cmd_set local_cmdset;
	struct dsi_cmd_desc *dsi_cmd = NULL;

	dsi_cmd = cmdset->cmds;

	memset(&local_cmdset, 0x00, sizeof(local_cmdset));

	iris_pt_switch_cmd(panel, &local_cmdset, dsi_cmd);

	/*passthrough write to panel*/
	iris_panel_cmd_passthrough_wr(panel, &local_cmdset);
}

static int iris_pt_remove_resp_header(char *ptr, int *offset)
{
	int rc = 0;
	char cmd;

	if (!ptr)
		return -EINVAL;

	cmd = ptr[0];
	IRIS_LOGV("[%s] cmd = 0x%02x", __func__, cmd);
	switch (cmd) {
	case MIPI_DSI_RX_ACKNOWLEDGE_AND_ERROR_REPORT:
		IRIS_LOGD("%s: rx ACK_ERR_REPORT", __func__);
		rc = -EINVAL;
		break;
	case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_1BYTE:
	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_1BYTE:
		*offset = 1;
		rc = 1;
		break;
	case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_2BYTE:
	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_2BYTE:
		*offset = 1;
		rc = 2;
		break;
	case MIPI_DSI_RX_GENERIC_LONG_READ_RESPONSE:
	case MIPI_DSI_RX_DCS_LONG_READ_RESPONSE:
		*offset = 4;
		rc = ptr[1];
		break;
	default:
		rc = 0;
	}

	return rc;
}


static void iris_pt_read_value(struct dsi_panel_cmd_set *cmdset)
{
	u32 i = 0;
	u32 rlen = 0;
	u32 offset = 0;
	union iris_mipi_tx_cmd_payload val;
	u8 *rbuf = NULL;
	struct iris_cfg *pcfg = iris_get_cfg();
	u32 address = pcfg->chip_ver == IRIS5_CHIP_VERSION ?
		IRIS_RD_PACKET_DATA : IRIS_RD_PACKET_DATA_I3;

	rbuf = (u8 *)cmdset->cmds[0].msg.rx_buf;
	rlen = cmdset->cmds[0].msg.rx_len;

	if (!rbuf || rlen <= 0) {
		IRIS_LOGE("rbuf %p  rlen =%d", rbuf, rlen);
		return;
	}

	/*read iris for data*/
	val.pld32 = iris_ocp_read(address, cmdset->state);

	rlen = iris_pt_remove_resp_header(val.p, &offset);
	IRIS_LOGV("rlen = %d", rlen);

	if (rlen <= 0) {
		IRIS_LOGE("do not return value");
		return;
	}

	if (rlen <= 2) {
		for (i = 0; i < rlen; i++)
			rbuf[i] = val.p[offset + i];
	} else {
		int j = 0;
		int len = 0;
		int num = (rlen + 3) / 4;

		for (i = 0; i < num; i++) {
			len = (i == num - 1) ? rlen - 4 * i : 4;

			val.pld32 = iris_ocp_read(address, DSI_CMD_SET_STATE_HS);
			for (j = 0; j < len; j++)
				rbuf[i * 4 + j] = val.p[j];
		}
	}
}

void iris_panel_cmd_passthrough_rd(struct dsi_panel *panel, struct dsi_panel_cmd_set *cmdset)
{
	struct iris_cfg *pcfg = NULL;
	struct dsi_display * display = NULL;

	IRIS_LOGD("enter rd commands");

	if (!panel || !cmdset || cmdset->count != 1) {
		IRIS_LOGE("cmdset is error cmdset = %p", cmdset);
		return;
	}

	pcfg = iris_get_cfg();
	display = pcfg->display;

	dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_ON);

	/*step1  write max packket size*/
	iris_pt_write_max_ptksize(panel, cmdset);

	/*step2 write read cmd to panel*/
	iris_pt_write_rdcmd_to_panel(panel, cmdset);

	/*step3 read panel data*/
	iris_pt_read_value(cmdset);

	dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_OFF);
}


int iris5_panel_cmd_passthrough(
		struct dsi_panel *panel, struct dsi_panel_cmd_set *cmdset)
{
	struct iris_cfg *pcfg = NULL;

	if (!cmdset || !panel) {
		IRIS_LOGE("cmdset = %p  panel = %p", cmdset, panel);
		return -1;
	}

	pcfg = iris_get_cfg();
	mutex_lock(&pcfg->mutex);
	if (cmdset->count == 1 && cmdset->cmds[0].msg.type == MIPI_DSI_DCS_READ)
		iris_panel_cmd_passthrough_rd(panel, cmdset);
	else
		iris_panel_cmd_passthrough_wr(panel, cmdset);
	mutex_unlock(&pcfg->mutex);

	return 0;
}

void iris_pwil_mode_set(struct dsi_panel *pane, u8 mode, bool osd_enable, int state)
{
	char pwil_mode[2] = {0x00, 0x00};
	struct dsi_cmd_desc iris_pwil_mode_cmd = {
			{0, MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM, 0, 0, 0, sizeof(pwil_mode), pwil_mode, 0, NULL}, 1, 0};
	struct dsi_panel_cmd_set panel_cmds = {
		.state = DSI_CMD_SET_STATE_HS,
		.count = 1,
		.cmds = &iris_pwil_mode_cmd,
	};
	struct iris_cfg *pcfg = iris_get_cfg();

	if (PT_MODE == mode) {
		pwil_mode[0] = 0x0;
		pwil_mode[1] = 0x81;
	} else if (RFB_MODE == mode) {
		pwil_mode[0] = 0xc;
		pwil_mode[1] = 0x81;
	} else if (FRC_MODE == mode) {
		pwil_mode[0] = 0x4;
		pwil_mode[1] = 0x82;
	}
	if (osd_enable)
		pwil_mode[0] |= 0x80;

	if (pcfg->panel->cur_mode && pcfg->panel->cur_mode->priv_info && pcfg->panel->cur_mode->priv_info->dsc_enabled)
		pwil_mode[0] |= 0x10;

	IRIS_LOGI("iris: set pwil mode: %x, %x", pwil_mode[0], pwil_mode[1]);

	iris5_dsi_cmds_send(pcfg->panel, panel_cmds.cmds, panel_cmds.count, panel_cmds.state);
}

static int iris_ctrl_ocp_read_value(struct dsi_display_ctrl *ctrl,
		u32 mode, u32 *val)
{
	char pi_read[1] = {0x00};
	struct dsi_cmd_desc pi_read_cmd[] = {
		{{0, MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM, MIPI_DSI_MSG_REQ_ACK, 0, 0, sizeof(pi_read), pi_read, 0, NULL}, 1, 0}};
	u32 response_value;
	u32 flags = 0;
	int rc = 0;

	/* Read response.*/
	memset(iris_read_cmd_rbuf, 0, sizeof(iris_read_cmd_rbuf));
	pi_read_cmd[0].msg.rx_len = 4;
	pi_read_cmd[0].msg.rx_buf = iris_read_cmd_rbuf;

	flags |= (DSI_CTRL_CMD_FETCH_MEMORY | DSI_CTRL_CMD_READ |
		  DSI_CTRL_CMD_CUSTOM_DMA_SCHED);
	flags |= DSI_CTRL_CMD_LAST_COMMAND;
	pi_read_cmd->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
	if (mode == DSI_CMD_SET_STATE_LP)
		pi_read_cmd->msg.flags |= MIPI_DSI_MSG_USE_LPM;
	rc = dsi_ctrl_cmd_transfer(ctrl->ctrl, &pi_read_cmd->msg, flags);
	if (rc <= 0) {
		IRIS_LOGW("rx cmd transfer failed rc=%d", rc);
		return rc;
	}
	IRIS_LOGD("read register %02x %02x %02x %02x",
			iris_read_cmd_rbuf[0], iris_read_cmd_rbuf[1], iris_read_cmd_rbuf[2], iris_read_cmd_rbuf[3]);
	response_value = iris_read_cmd_rbuf[0] | (iris_read_cmd_rbuf[1] << 8) |
			(iris_read_cmd_rbuf[2] << 16) | (iris_read_cmd_rbuf[3] << 24);
	*val = response_value;

	return 4;
}

static void iris_ctrl_ocp_write_address(struct dsi_display_ctrl *ctrl,
		u32 address,
		u32 mode)
{
	struct iris_ocp_cmd ocp_cmd;
	struct dsi_cmd_desc iris_ocp_cmd[] = {
		{{0, MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, 0, CMD_PKT_SIZE, ocp_cmd.cmd, 0, NULL}, 1, 0}};
	u32 flags = 0;

	/* Send OCP command.*/
	memset(&ocp_cmd, 0, sizeof(ocp_cmd));
	iris_cmd_add(&ocp_cmd, OCP_SINGLE_READ);
	iris_cmd_add(&ocp_cmd, address);
	iris_ocp_cmd[0].msg.tx_len = ocp_cmd.cmd_len;

	flags |= DSI_CTRL_CMD_FETCH_MEMORY;
	flags |= DSI_CTRL_CMD_LAST_COMMAND;
	iris_ocp_cmd->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
	dsi_ctrl_cmd_transfer(ctrl->ctrl, &iris_ocp_cmd->msg, flags);
}

static int iris_ctrl_ocp_read(struct dsi_display_ctrl *ctrl, u32 address,
		u32 mode, u32 *pval)
{
	int rc = 0;
	iris_ctrl_ocp_write_address(ctrl, address, DSI_CMD_SET_STATE_HS);
	rc = iris_ctrl_ocp_read_value(ctrl, mode, pval);
	if (rc != 4)
		return -EINVAL;
	else
		return 0;
}

static int iris_panel_ctrl_send(struct dsi_display_ctrl *ctrl,
		struct dsi_panel *panel,
		struct dsi_cmd_desc *ptx_cmds, u32 cmds_cnt)
{
	u32 flags = 0;
	int i;
	int rc = 0;

	flags |= DSI_CTRL_CMD_FETCH_MEMORY;
	for (i = 0; i < cmds_cnt; i++) {
		if (ptx_cmds[i].last_command) {
			ptx_cmds[i].msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
			flags |= DSI_CTRL_CMD_LAST_COMMAND;
		}
		rc = dsi_ctrl_cmd_transfer(ctrl->ctrl, &ptx_cmds[i].msg, flags);
		if (ptx_cmds[i].post_wait_ms) {
			usleep_range(ptx_cmds[i].post_wait_ms*1000,
					((ptx_cmds[i].post_wait_ms*1000)+10));
			IRIS_LOGV("[%s:%d] wait =  %d", __func__, __LINE__, ptx_cmds[i].post_wait_ms);
		}
	}
	return rc;
}

static int iris_panel_ctrl_wr(struct dsi_display_ctrl *ctrl,
		struct dsi_panel *panel,
		struct dsi_panel_cmd_set *cmdset)
{
	u32 i = 0;
	u32 j = 0;
	int cmds_cnt = 0;
	u32 offset = 0;
	struct iris_ocp_cmd *pocp_cmds = NULL;
	struct dsi_cmd_desc *ptx_cmds = NULL;
	struct dsi_cmd_desc *dsi_cmds = NULL;
	int rc = 0;

	if (!panel || !cmdset) {
		IRIS_LOGE("cmdset is null!");
		return -EINVAL;
	}

	cmds_cnt =  iris_pt_alloc_cmds_space(cmdset, &ptx_cmds, &pocp_cmds);
	IRIS_LOGV("[%s:%d] cmdset.cnt: %d cmds_cnt: %d", __func__, __LINE__, cmdset->count, cmds_cnt);
	if (cmds_cnt < 0) {
		IRIS_LOGE("cmds_cnt is negative!");
		return -ENOMEM;
	}

	for (i = 0; i < cmdset->count; i++) {
		/*initial val*/
		dsi_cmds = cmdset->cmds + i;

		iris_pt_init_ocp_cmd(pocp_cmds + j);

		offset = iris_pt_add_cmd(
				ptx_cmds + j, pocp_cmds + j, dsi_cmds, cmdset);
		j += offset;
	}

	if (j != (u32)cmds_cnt)
		IRIS_LOGE("cmds cnt is not right real cmds_cnt = %d j = %d", cmds_cnt, j);
	else {
		rc = iris_panel_ctrl_send(ctrl, panel, ptx_cmds, (u32)cmds_cnt);
	}

	vfree(pocp_cmds);
	vfree(ptx_cmds);
	pocp_cmds = NULL;
	ptx_cmds = NULL;
	return rc;
}

static int iris_panel_ctrl_pt_read_data(struct dsi_display_ctrl *ctrl,
		struct dsi_panel *panel,
		struct dsi_panel_cmd_set *cmdset)
{
	u32 i = 0;
	u32 rlen = 0;
	u32 offset = 0;
	union iris_mipi_tx_cmd_payload val;
	u8 *rbuf = NULL;
	u32 address = IRIS_RD_PACKET_DATA;
	int rc = 0;

	rbuf = (u8 *)cmdset->cmds[0].msg.rx_buf;
	rlen = cmdset->cmds[0].msg.rx_len;

	if (!rbuf || rlen <= 0) {
		IRIS_LOGE("rbuf %p  rlen =%d", rbuf, rlen);
		return -EINVAL;
	}

	/* read iris for data */
	rc = iris_ctrl_ocp_read(ctrl, address, cmdset->state, &val.pld32);
	if(rc) {
		IRIS_LOGE("do not return value");
		return -EINVAL;
	}

	IRIS_LOGD("[%s:%d] rbuf = %p rlen = %d pld32 = 0x%08x", __func__, __LINE__, rbuf, rlen, val.pld32);
	rlen = iris_pt_remove_resp_header(val.p, &offset);

	if (rlen <= 0) {
		IRIS_LOGE("do not return value");
		return -EINVAL;
	}

	if (rlen <= 2) {
		for (i = 0; i < rlen; i++) {
			rbuf[i] = val.p[offset + i];
			IRIS_LOGV("[%s:%d] rlen = %d d = 0x%02x", __func__, __LINE__, rlen, rbuf[i]);
		}
	} else {
		int j = 0;
		int len = 0;
		int num = (rlen + 3) / 4;

		for (i = 0; i < num; i++) {
			len = (i == num - 1) ? rlen - 4 * i : 4;

			rc = iris_ctrl_ocp_read(ctrl, address, cmdset->state, &val.pld32);
			if(rc) {
				IRIS_LOGE("do not return value");
				return -EINVAL;
			}
			for (j = 0; j < len; j++) {
				rbuf[i * 4 + j] = val.p[j];
				IRIS_LOGV("[%s:%d] rlen = %d d = 0x%02x", __func__, __LINE__, rlen, rbuf[i]);
			}
		}
	}
	return rlen;
}

static void iris_panel_ctrl_write_rdcmd(struct dsi_display_ctrl *ctrl,
		struct dsi_panel *panel,
		struct dsi_panel_cmd_set *cmdset)
{
	struct dsi_panel_cmd_set local_cmdset;
	struct dsi_cmd_desc *dsi_cmd = NULL;

	IRIS_LOGD("[%s:%d]", __func__, __LINE__);
	dsi_cmd = cmdset->cmds;
	memset(&local_cmdset, 0x00, sizeof(local_cmdset));
	iris_pt_switch_cmd(panel, &local_cmdset, dsi_cmd);
	iris_panel_ctrl_wr(ctrl, panel, &local_cmdset);
}

static int iris_panel_ctrl_set_max_pktsize(struct dsi_display_ctrl *ctrl,
		struct dsi_panel *panel,
		struct dsi_panel_cmd_set *cmdset)
{
	int rc = 0;
	size_t rlen;
	struct dsi_panel_cmd_set local_cmdset;
	static char max_pktsize[2] = {0x00, 0x00}; /* LSB tx first, 10 bytes */
	static struct dsi_cmd_desc pkt_size_cmd = {
		{0, MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE, MIPI_DSI_MSG_REQ_ACK, 0, 0, sizeof(max_pktsize), max_pktsize, 0, NULL}, 1, 0};

	IRIS_LOGV("[%si:%d]", __func__, __LINE__);
	rlen = cmdset->cmds[0].msg.rx_len;
	if (rlen > 128) {
		IRIS_LOGE("dlen = %d  > 128", rlen);
		return -EINVAL;
	} else {
		IRIS_LOGD("[%s] rlen = %d", __func__, rlen);
	}

	max_pktsize[0] = (rlen & 0xFF);

	memset(&local_cmdset, 0x00, sizeof(local_cmdset));
	iris_pt_switch_cmd(panel, &local_cmdset, &pkt_size_cmd);
	local_cmdset.state = DSI_CMD_SET_STATE_HS;
	iris_panel_ctrl_wr(ctrl, panel, &local_cmdset);
	return rc;
}

static u32 iris_get_panel_frame_ms(void)
{
	u32 frame = 0;
	struct iris_cfg *pcfg = NULL;
	struct dsi_display_mode *mode = NULL;

	pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	if (!pcfg || !pcfg->panel || !pcfg->panel->cur_mode) {
		return 1;
	}

	mode = pcfg->panel->cur_mode;
	frame = mode->timing.refresh_rate;
	if ((frame < 24) || (frame > 240))
		frame = 24;

	frame = ((1000/frame) + 1);

	return frame;
}

static int iris_panel_ctrl_read(struct dsi_display_ctrl *ctrl,
				struct dsi_panel *panel,
				struct dsi_panel_cmd_set *cmdset)
{
	int rc = 0;
	struct iris_cfg *pcfg;
	u32 ms = iris_get_panel_frame_ms();

	pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	if(pcfg == NULL)
		return rc;

	mutex_lock(&pcfg->lock_send_pkt);
	// step 1: max return size
	iris_panel_ctrl_set_max_pktsize(ctrl, panel, cmdset);
	// step 2: read command
	iris_panel_ctrl_write_rdcmd(ctrl, panel, cmdset);
	// step 3: delay one frame
	usleep_range(1000 * ms, 1000 * ms + 1);
	// step 4: read response data
	rc = iris_panel_ctrl_pt_read_data(ctrl, panel, cmdset);
	mutex_unlock(&pcfg->lock_send_pkt);

	return rc;
}

int iris_panel_ctrl_read_status(struct dsi_display_ctrl *ctrl, struct dsi_panel *panel)
{
	int i, rc = 0, count = 0, start = 0, *lenp;
	struct drm_panel_esd_config *config;
	struct dsi_cmd_desc *cmds;
	int retry = 3;

	config = &(panel->esd_config);
	lenp = config->status_valid_params ?: config->status_cmds_rlen;
	count = config->status_cmd.count;
	cmds = config->status_cmd.cmds;

	for (i = 0; i < count; ++i) {
		struct dsi_panel_cmd_set local_cmdset;
		memset(&local_cmdset, 0x00, sizeof(local_cmdset));
		memset(config->status_buf, 0x0, SZ_4K);
		cmds[i].msg.rx_buf = config->status_buf;
		cmds[i].msg.rx_len = config->status_cmds_rlen[i];
		local_cmdset.state = config->status_cmd.state;
		iris_pt_switch_cmd(panel, &local_cmdset, &cmds[i]);
		do {
			rc = iris_panel_ctrl_read(ctrl, panel, &local_cmdset);
		} while ((rc <= 0) && (--retry));
		if (rc <= 0) {
			IRIS_LOGE("iris_panel_ctrl_read() failed rc=%d", rc);
			return rc;
		}
		IRIS_LOGV("[%s:%d] status[0]: 0x%02x len: 0x%02x", __func__, __LINE__, config->status_buf[0], lenp[i]);
		memcpy(config->return_buf + start,
			config->status_buf, lenp[i]);
		start += lenp[i];
	}

	return 1;
}

void iris_esd_register_dump(void)
{
	u32 value = 0;
	u32 index = 0;
	static u32 iris_register_list[] = {
		0xf0000000,
		0xf0000004,
		0xf0000008,
		0xf000001c,
		0xf0000020,
		0xf0000024,
		0xf0000040,
		0xf0000044,
		0xf0000048,
		0xf000004c,
		0xf0000060,
		0xf0000094,
		0xf1800004,
		0xf1800034,
		0xf123ffe4,
		0xf155ffe4,
		0xf1240030,
		0xf125ffe4,
		0xf163ffe4,
		0xf165ffe4,
		0xf169ffe4,
		0xf16bffe4,
		0xf16dffe4,
	};


	IRIS_LOGE("iris esd register dump: ");
	for (index = 0; index < sizeof(iris_register_list)/ sizeof(u32); index++) {
		value = iris_ocp_read(iris_register_list[index], DSI_CMD_SET_STATE_HS);
		IRIS_LOGE("%08x : %08x", iris_register_list[index], value);
	}
}

int get_iris_status(void)
{
	int ret = 1;
	unsigned int data = 0;
	unsigned int reg_update = 0x00;
	int cnt = 3;
	struct iris_cfg *pcfg;
	u32 ms = iris_get_panel_frame_ms();

	pcfg = iris_get_cfg_by_index(DSI_PRIMARY);
	if (pcfg == NULL)
		return ret;

	if (!pcfg->lp_ctrl.esd_enable) {
		return 1;
	}

	if (!mutex_trylock(&pcfg->lock_send_pkt)) {
		IRIS_LOGE("%s:%d lock_send_pkt is locked!", __func__, __LINE__);
		mutex_lock(&pcfg->lock_send_pkt);
	}

	data = iris_ocp_read(IRIS_REG_INTSTAT_RAW, DSI_CMD_SET_STATE_HS);
	if (data & TXFALSE_CONTROL_MASK) {
		IRIS_LOGE("INTSTAT_RAW: 0x%x", data);
		cnt = 0;
		mutex_unlock(&pcfg->lock_send_pkt);
		goto status_check_done;
	}
	IRIS_LOGD("INTSTAT_RAW: 0x%x", data);

	data = iris_ocp_read(IRIS_REG_UPDATE, DSI_CMD_SET_STATE_HS);
	iris_ocp_write(IRIS_REG_UPDATE, data | (1<<DISP_CMD_SHAWDOW_EN_SHIFT));
	do {
		data = iris_ocp_read(IRIS_REG_UPDATE, DSI_CMD_SET_STATE_HS);
		reg_update = data & DISP_CMD_SHAWDOW_EN_MASK;
		if (!reg_update) {
			IRIS_LOGD("esd %d reg_update: 0x%x", cnt, data);
			break;
		}
		IRIS_LOGW("esd %d data: 0x%x reg_update: 0x%x", cnt, data, reg_update);
		usleep_range(1000 * ms, 1000 * ms + 1);
	} while(--cnt);
	mutex_unlock(&pcfg->lock_send_pkt);

status_check_done:
	if (cnt == 0) {
		if (pcfg->lp_ctrl.esd_cnt < 10000)
			pcfg->lp_ctrl.esd_cnt++;
		else
			pcfg->lp_ctrl.esd_cnt = 0;

		IRIS_LOGE("esd detected. enable: %d", pcfg->lp_ctrl.esd_enable);
		if (pcfg->lp_ctrl.esd_enable) {
			IRIS_LOGE("esd recovery");
			iris_esd_register_dump();
			ret = -1;
		}
	} else {
		ret = 1;
	}
	IRIS_LOGD("%s ret:%d", __func__, ret);

	return ret;
}

int iris_read_status(struct dsi_display_ctrl *ctrl, struct dsi_panel *panel)
{
	struct iris_cfg *pcfg;

	IRIS_LOGV("[%s:%d]", __func__, __LINE__);
	if (!panel || !ctrl || !ctrl->ctrl)
		return -EINVAL;

	if (!dsi_ctrl_validate_host_state(ctrl->ctrl))
		return 1;

	if (panel->is_secondary)
		return 1;

	pcfg = iris_get_cfg_by_index(DSI_PRIMARY);

	if (pcfg->abypss_ctrl.abypass_mode == ANALOG_BYPASS_MODE) {
		IRIS_LOGI("[%s] in bypass mode", __func__);
		return 2;
	}

	if (1) {
		int rc;
		IRIS_LOGI("[%s:%d]", __func__, __LINE__);
		rc = iris_panel_ctrl_read_status(ctrl, panel);
		if (rc <= 0)
			return -EINVAL;

		rc = get_iris_status();
		if (rc <= 0)
			return -EINVAL;

		return 1;
	} else {
		get_iris_status();
		return 1;
	}
}

int iris_panel_ctrl_read_reg(struct dsi_display_ctrl *ctrl, struct dsi_panel *panel,
			u8 *rx_buf, int rlen, struct dsi_cmd_desc *cmd)
{
	int rc = 0;
	int retry = 3;
	struct dsi_panel_cmd_set local_cmdset;
	struct dsi_cmd_desc *cmds = cmd;

	if (ctrl == NULL || panel == NULL || rx_buf == NULL || cmds == NULL || rlen <= 0) {
		return -EINVAL;
	}

	memset(&local_cmdset, 0x00, sizeof(local_cmdset));
	cmds->msg.rx_buf = rx_buf;
	cmds->msg.rx_len = rlen;
	cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;
	iris_pt_switch_cmd(panel, &local_cmdset, cmds);
	do {
		rc = iris_panel_ctrl_read(ctrl, panel, &local_cmdset);
	} while ((rc <= 0) && (--retry));

	if (rc <= 0) {
		IRIS_LOGE("iris_panel_ctrl_read() failed rc=%d", rc);
		return rc;
	}

	return 1;
}
