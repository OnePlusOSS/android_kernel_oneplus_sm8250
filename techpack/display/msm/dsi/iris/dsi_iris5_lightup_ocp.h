// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#ifndef _DSI_IRIS_LIGHTUP_OCP_H_
#define _DSI_IRIS_LIGHTUP_OCP_H_


#define IRIS_MIPI_TX_HEADER_ADDR  0xF189C010
#define IRIS_MIPI_TX_PAYLOAD_ADDR  0xF189C014
#define IRIS_MIPI_TX_HEADER_ADDR_I3  0xF0C1C010
#define IRIS_MIPI_TX_PAYLOAD_ADDR_I3  0xF0C1C014
#define IRIS_TOP_PMU_STATUS     0xF0000094
#define IRIS_ULPS_CTRL		0xF00000C0

#define OCP_BURST_WRITE 0x0
#define OCP_SINGLE_WRITE_BYTEMASK 0x4
#define OCP_SINGLE_WRITE_BITMASK 0x5
#define PXLW_DIRECTBUS_WRITE 0xC
#define OCP_SINGLE_READ 0x8

#define ULPS_EN_MASK  0x0080
#define MIPIRX_ULPS_SELECT 0x03

#define DMA_TPG_FIFO_LEN 64

#define CMD_PKT_SIZE 512

#define TEST_ADDR 0xF1240114

#define OCP_HEADER 4  /*4bytes */
#define OCP_MIN_LEN 12  /* 12bytes */

#define DSI_CMD_CNT 20

struct iris_ocp_burst_header {
	u32 ocp_type:4;
	u32 reserved:28;
};

struct iris_ocp_read_header {
	u32 ocp_type:4;
	u32 reserved:28;
};

struct iris_ocp_single_bytemask_header {
	u32 ocp_type:4;
	u32 bytemask1:4;
	u32 bytemask2:4;
	u32 bytemask3:4;
	u32 bytemask4:4;
	u32 bytemask5:4;
	u32 bytemask6:4;
	u32 bytemask7:4;
};

struct iris_ocp_single_bitmask_header {
	u32 ocp_type:4;
	u32 reserved:28;
};

struct iris_ocp_direct_bus_header {
	u16 ocp_type:4;
	u16 reserved:12;
	u16 slot_select;
};

union iris_ocp_cmd_header {
	struct iris_ocp_burst_header st_ocp_burst;
	struct iris_ocp_read_header st_ocp_rd;
	struct iris_ocp_single_bytemask_header st_ocp_bytemask;
	struct iris_ocp_single_bitmask_header st_ocp_bitmask;
	struct iris_ocp_direct_bus_header st_ocp_directbus;
	u32 header32;
};


struct iris_mipi_tx_cmd_hdr {
	u8 dtype;
	u8 len[2]; // for short command, means parameter1 and parameter2. for long command, means command length
	u8 writeFlag:1; //Read = 0, Write =1
	u8 linkState:1; // HS=0, LP =1
	u8 longCmdFlag:1; // short = 0, long =1
	u8 reserved:5; // =0
};

union iris_mipi_tx_cmd_header {
	struct iris_mipi_tx_cmd_hdr stHdr;
	u32 hdr32;
};

union iris_mipi_tx_cmd_payload {
	u8 p[4];
	u32 pld32;
};

struct iris_ocp_cmd {
	char cmd[CMD_PKT_SIZE];
	int cmd_len;
};

struct iris_ocp_dsi_tool_input {
	__u16 iris_ocp_type;
	__u16 iris_ocp_cnt;
	__u32 iris_ocp_addr;
	__u32 iris_ocp_value;
	__u32 iris_ocp_size;
};


void iris_ocp_write_val(u32 address, u32 value);
void iris_ocp_write_vals(u32 header, u32 address, u32 size, u32 *pvalues);
void iris_ocp_write_mult_vals(u32 size, u32 *pvalues);
u32 iris_ocp_read(u32 address, u32 type);
void iris_write_test(struct dsi_panel *panel, u32 iris_addr, int ocp_type, u32 pkt_size);
void iris_write_test_muti_pkt(struct dsi_panel *panel, struct iris_ocp_dsi_tool_input *ocp_input);

int iris_dsi_send_cmds(struct dsi_panel *panel, struct dsi_cmd_desc *cmds, u32 count, enum dsi_cmd_set_state state);
int iris_i2c_read_panel_data(u32 reg_addr, u32 size, u32 *pvalues);
void iris_set_pwil_mode(struct dsi_panel *pane, u8 mode, bool osd_enable, int state);

#endif // _DSI_IRIS_LIGHTUP_OCP_H_
