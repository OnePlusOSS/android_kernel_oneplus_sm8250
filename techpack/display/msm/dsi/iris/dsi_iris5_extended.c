// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */

#include <video/mipi_display.h>

#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_iris5_api.h"
#include "dsi_iris5.h"
#include "dsi_iris5_log.h"


static bool iris_chip_enable;
static bool soft_iris_enable;
static bool iris_dual_enable;

void iris_query_capability(struct dsi_panel *panel)
{
	bool chip_enable = false;
	bool soft_enable = false;
	struct dsi_parser_utils *utils = NULL;

	if (!panel)
		return;

	if (!strcmp(panel->type, "secondary")) {
		IRIS_LOGI("%s(), sencondary panel: %s", __func__, panel->name);
		iris_dual_enable = true;
		return;
	}

	utils = &panel->utils;
	if (!utils)
		return;

	chip_enable = utils->read_bool(utils->data,
			"pxlw,iris-chip-enable");
	soft_enable = utils->read_bool(utils->data,
			"pxlw,soft-iris-enable");

	IRIS_LOGI("%s(), iris chip enable: %s, soft iris enable: %s",
			__func__,
			chip_enable ? "true" : "false",
			soft_enable ? "true" : "false");
	iris_chip_enable = chip_enable;
	soft_iris_enable = soft_enable;
}

bool iris_is_chip_supported(void)
{
	return iris_chip_enable;
}

bool iris_is_softiris_supported(void)
{
	return soft_iris_enable;
}

bool iris_is_dual_supported(void)
{
	return iris_chip_enable && iris_dual_enable;
}


bool iris_is_pt_mode(struct dsi_panel *panel)
{
	return iris_get_abyp_mode(panel) == PASS_THROUGH_MODE;
}

void iris_dsi_display_res_init(struct dsi_display *display)
{
	if (!iris_is_chip_supported() && !iris_is_softiris_supported())
		return;

	IRIS_LOGI("%s(), display type: %s", __func__, display->display_type);

	if (iris_is_chip_supported()) {
		if (NULL != display->display_type && !strcmp(display->display_type, "secondary")) {
			display->panel->is_secondary = true;
			iris_set_cfg_index(DSI_SECONDARY);
		} else {
			display->panel->is_secondary = false;
			iris_set_cfg_index(DSI_PRIMARY);
		}

		iris_parse_param(display);
	}
	iris_init(display, display->panel);
}

static int panel_debug_base_open(struct inode *inode, struct file *file)
{
	/* non-seekable */
	file->f_mode &= ~(FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);
	file->private_data = inode->i_private;
	return 0;
}

static int panel_debug_base_release(struct inode *inode, struct file *file)
{
	return 0;
}

#define PANEL_REG_MAX_OFFSET 1024 // FIXME

static ssize_t panel_debug_base_offset_write(struct file *file,
		    const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct dsi_display *display = file->private_data;
	u32 off, cnt;
	char buf[64];

	if (!display)
		return -ENODEV;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	buf[count] = 0;	/* end of string */

	if (sscanf(buf, "%x %u", &off, &cnt) != 2)
		return -EINVAL;

	if (off > PANEL_REG_MAX_OFFSET)
		return -EINVAL;

	if (cnt > (PANEL_REG_MAX_OFFSET - off))
		cnt = PANEL_REG_MAX_OFFSET - off;

	display->off = off;
	display->cnt = cnt;

	pr_debug("offset=%x cnt=%d\n", off, cnt);

	return count;
}

static ssize_t panel_debug_base_offset_read(struct file *file,
			char __user *buff, size_t count, loff_t *ppos)
{
	struct dsi_display *display = file->private_data;
	int len;
	char buf[64];

	if (!display)
		return -ENODEV;

	if (*ppos)
		return 0;	/* the end */

	len = snprintf(buf, sizeof(buf), "0x%02x %x\n", display->off, display->cnt);

	if (len < 0 || len >= sizeof(buf))
		return -EINVAL;

	if (count < sizeof(buf))
		return -EINVAL;
	if (copy_to_user(buff, buf, len))
		return -EFAULT;

	*ppos += len;	/* increase offset */
	return len;
}

/* Hex number + whitespace */
#define NEXT_VALUE_OFFSET 3

#define PANEL_CMD_MIN_TX_COUNT 2


extern int iris_dsi_display_ctrl_get_host_init_state(struct dsi_display *dsi_display,
		bool *state);
static ssize_t panel_debug_base_reg_write(struct file *file,
		const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct dsi_display *display = file->private_data;
	char buf[64];
	char reg[64];
	u32 len = 0, value = 0;
	char *bufp;
	bool state = false;
	int rc;

	struct dsi_cmd_desc cmds = {
		{ 0 },	// msg
		1,	// last
		0	// wait
	};
#ifndef IRIS_ABYP_LIGHTUP
	struct dsi_panel_cmd_set cmdset = {
		.state = DSI_CMD_SET_STATE_HS,
		.count = 1,
		.cmds = &cmds,
	};
#endif

	if (!display)
		return -ENODEV;

	/* get command string from user */
	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	buf[count] = 0;	/* end of string */

	bufp = buf;
	/* End of a hex value in given string */
	bufp[NEXT_VALUE_OFFSET - 1] = 0;
	while (kstrtouint(bufp, 16, &value) == 0) {
		reg[len++] = value;
		if (len >= sizeof(reg)) {
			pr_err("wrong input reg len\n");
			return -EINVAL;
		}
		bufp += NEXT_VALUE_OFFSET;
		if ((bufp >= (buf + count)) || (bufp < buf)) {
			pr_warn("%s,buffer out-of-bounds\n", __func__);
			break;
		}
		/* End of a hex value in given string */
		if ((bufp + NEXT_VALUE_OFFSET - 1) < (buf + count))
			bufp[NEXT_VALUE_OFFSET - 1] = 0;
	}
	if (len < PANEL_CMD_MIN_TX_COUNT) {
		pr_err("wrong input reg len\n");
		return -EINVAL;
	}

	cmds.msg.type = display->cmd_data_type;
	cmds.msg.flags = MIPI_DSI_MSG_LASTCOMMAND;
	cmds.msg.tx_len = len;
	cmds.msg.tx_buf = reg;

	rc = iris_dsi_display_ctrl_get_host_init_state(display, &state);
	if (!rc && state) {
		dsi_panel_acquire_panel_lock(display->panel);
#ifdef IRIS_ABYP_LIGHTUP
		rc = display->host.ops->transfer(&display->host, &cmds.msg);
#else
		iris_pt_send_panel_cmd(display->panel, &cmdset);
#endif
		dsi_panel_release_panel_lock(display->panel);
	}

	return rc ? rc : count;
}

#define PANEL_REG_ADDR_LEN 8
#define PANEL_REG_FORMAT_LEN 5

static ssize_t panel_debug_base_reg_read(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	struct dsi_display *display = file->private_data;
	u32 i, len = 0, reg_buf_len = 0;
	char *panel_reg_buf, *rx_buf;
	int rc;
	bool state = false;
	char panel_reg[2] = { 0 };
	struct dsi_cmd_desc cmds = {
		{ 0 },	// msg
		1,	// last
		0	// wait
	};
#ifndef IRIS_ABYP_LIGHTUP
	struct dsi_panel_cmd_set cmdset = {
		.state = DSI_CMD_SET_STATE_HS,
		.count = 1,
		.cmds = &cmds,
	};
#endif

	if (!display)
		return -ENODEV;
	if (!display->cnt)
		return 0;
	if (*ppos)
		return 0;	/* the end */

	/* '0x' + 2 digit + blank = 5 bytes for each number */
	reg_buf_len = (display->cnt * PANEL_REG_FORMAT_LEN)
		    + PANEL_REG_ADDR_LEN + 1;
	if (count < reg_buf_len)
		return -EINVAL;

	rx_buf = kzalloc(display->cnt, GFP_KERNEL);
	panel_reg_buf = kzalloc(reg_buf_len, GFP_KERNEL);

	if (!rx_buf || !panel_reg_buf) {
		pr_err("not enough memory to hold panel reg dump\n");
		rc = -ENOMEM;
		goto read_reg_fail;
	}

	panel_reg[0] = display->off;

	cmds.msg.type = MIPI_DSI_DCS_READ;
	cmds.msg.flags = MIPI_DSI_MSG_LASTCOMMAND | MIPI_DSI_MSG_REQ_ACK;
	cmds.msg.tx_len = 2;
	cmds.msg.tx_buf = panel_reg;
	cmds.msg.rx_len = display->cnt;
	cmds.msg.rx_buf = rx_buf;

	rc = iris_dsi_display_ctrl_get_host_init_state(display, &state);
	if (!rc && state) {
		dsi_panel_acquire_panel_lock(display->panel);
#ifdef IRIS_ABYP_LIGHTUP
		rc = display->host.ops->transfer(&display->host, &cmds.msg);
#else
		iris_pt_send_panel_cmd(display->panel, &cmdset);
#endif
		dsi_panel_release_panel_lock(display->panel);
	}

	if (rc)
		goto read_reg_fail;

	len = scnprintf(panel_reg_buf, reg_buf_len, "0x%02x: ", display->off);

	for (i = 0; (len < reg_buf_len) && (i < display->cnt); i++)
		len += scnprintf(panel_reg_buf + len, reg_buf_len - len,
				"0x%02x ", rx_buf[i]);

	if (len)
		panel_reg_buf[len - 1] = '\n';

	if (copy_to_user(user_buf, panel_reg_buf, len)) {
		rc = -EFAULT;
		goto read_reg_fail;
	}

	*ppos += len;	/* increase offset */
	rc = len;

read_reg_fail:
	kfree(rx_buf);
	kfree(panel_reg_buf);
	return rc;
}

static const struct file_operations panel_off_fops = {
	.open = panel_debug_base_open,
	.release = panel_debug_base_release,
	.read = panel_debug_base_offset_read,
	.write = panel_debug_base_offset_write,
};

static const struct file_operations panel_reg_fops = {
	.open = panel_debug_base_open,
	.release = panel_debug_base_release,
	.read = panel_debug_base_reg_read,
	.write = panel_debug_base_reg_write,
};

void iris_dsi_display_debugfs_init(struct dsi_display *display,
		struct dentry *dir, struct dentry *dump_file)
{
	if (!iris_is_chip_supported())
		return;

	display->off = 0x0a;
	display->cnt = 1;
	display->cmd_data_type = MIPI_DSI_DCS_LONG_WRITE;

	dump_file = debugfs_create_x8("cmd_data_type", 0600, dir, &display->cmd_data_type);
	if (IS_ERR_OR_NULL(dump_file))
		pr_err("[%s] debugfs create panel cmd_data_type file failed, rc=%ld\n",
				display->name, PTR_ERR(dump_file));

	dump_file = debugfs_create_file("off",
			0600,
			dir,
			display,
			&panel_off_fops);
	if (IS_ERR_OR_NULL(dump_file))
		pr_err("[%s] debugfs create panel off file failed, rc=%ld\n",
				display->name, PTR_ERR(dump_file));

	dump_file = debugfs_create_file("reg",
			0600,
			dir,
			display,
			&panel_reg_fops);
	if (IS_ERR_OR_NULL(dump_file))
		pr_err("[%s] debugfs create panel reg file failed, rc=%ld\n",
				display->name, PTR_ERR(dump_file));
}

void iris_dsi_panel_dump_pps(struct dsi_panel_cmd_set *set)
{
	if (!iris_is_chip_supported())
		return;

	if (!set)
		return;

	IRIS_LOGI("%s(), qcom pps table:", __func__);
	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_NONE, 16, 4,
			set->cmds->msg.tx_buf, set->cmds->msg.tx_len, false);
	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_NONE, 4, 10,
			set->cmds->msg.tx_buf, set->cmds->msg.tx_len, false);
}

bool iris_enable_dsi_cmd_log = false;

void iris_dsi_ctrl_dump_desc_cmd(struct dsi_ctrl *dsi_ctrl,
		const struct mipi_dsi_msg *msg)
{
	char buf[1024];
	int len = 0;
	size_t i;
	char *tx_buf = (char*)msg->tx_buf;

	if (!iris_enable_dsi_cmd_log)
		return;

	if (!iris_is_chip_supported())
		return;

	/* Packet Info */
	len += snprintf(buf, sizeof(buf) - len,  "%02X ", msg->type);
	/* Last bit */
	len += snprintf(buf + len, sizeof(buf) - len, "%02X ", (msg->flags & MIPI_DSI_MSG_LASTCOMMAND) ? 1 : 0);
	len += snprintf(buf + len, sizeof(buf) - len, "%02X ", msg->channel);
	len += snprintf(buf + len, sizeof(buf) - len, "%02X ", (unsigned int)msg->flags);
	/* Delay */
	len += snprintf(buf + len, sizeof(buf) - len, "%02X ", msg->wait_ms);
	len += snprintf(buf + len, sizeof(buf) - len, "%02X %02X ", (unsigned int)(msg->tx_len) >> 8, (unsigned int)(msg->tx_len) & 0x00FF);//CID101695

	/* Packet Payload */
	for (i = 0 ; i < msg->tx_len ; i++) {
		len += snprintf(buf + len, sizeof(buf) - len, "%02X ", tx_buf[i]);
		/* Break to prevent show too long command */
		if (i > 250)
			break;
	}

	IRIS_LOGI("%s(), %s", __func__, buf);
}
