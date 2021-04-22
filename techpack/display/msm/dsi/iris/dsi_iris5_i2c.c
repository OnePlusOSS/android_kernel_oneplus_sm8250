// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2015-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2015-2020.
 */

//#include <asm-generic/uaccess.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/debugfs.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include "dsi_iris5_i2c.h"

#define IRIS_COMPATIBLE_NAME  "pixelworks,iris-i2c"
#define IRIS_I2C_DRIVER_NAME  "pixelworks-i2c"

#define I2C_DBG_TAG      "iris_i2c"
#define IRIS_I2C_DBG
#ifdef IRIS_I2C_DBG
#define iris_i2c_dbg(fmt, args...)              pr_debug(I2C_DBG_TAG "[%s:%d]" fmt, __func__, __LINE__, args)
#else
#define iris_i2c_dbg(fmt, args...)        do {} while (0)
#endif

#define MAX_TRANSFER_MSG_LEN   32
/*
 * pixelworks extend i2c
 */
enum {
	MSMFB_IRIS_I2C_READ = 0x01,
	MSMFB_IRIS_I2C_WRITE = 0X02,
};

enum {
	ONE_BYTE_REG_LEN = 0x04,
	TWO_BYTE_REG_LEN = 0x08,
	FOUR_BYTE_REG_LEN = 0x0c,
	ONE_BYTE_REG_LEN_READ = (MSMFB_IRIS_I2C_READ << 16) | ONE_BYTE_REG_LEN,
	TWO_BYTE_REG_LEN_READ = (MSMFB_IRIS_I2C_READ << 16) | TWO_BYTE_REG_LEN,
	FOUR_BYTE_REG_LEN_READ = (MSMFB_IRIS_I2C_READ << 16) | FOUR_BYTE_REG_LEN,
	ONE_BYTE_REG_LEN_WRITE = (MSMFB_IRIS_I2C_WRITE << 16) | ONE_BYTE_REG_LEN,
	TWO_BYTE_REG_LEN_WRITE = (MSMFB_IRIS_I2C_WRITE << 16) | TWO_BYTE_REG_LEN,
	FOUR_BYTE_REG_LEN_WRITE = (MSMFB_IRIS_I2C_WRITE << 16) | FOUR_BYTE_REG_LEN,
};

enum {
	DBG_I2C_READ = 0x01,
	DBG_I2C_WRITE,
};

/*iris i2c handle*/
static struct i2c_client  *iris_i2c_handle;

static int iris_i2c_rw_t(uint32_t type, struct addr_val_i2c *val, int len);

static int iris_i2c_read_transfer(
		struct i2c_adapter *adapter, struct i2c_msg *msgs, int len)
{
	int i = 0;
	int pos = 0;
	int ret = -1;

	for (i = 0; i < len; i++) {
		pos = i << 1;
		ret = i2c_transfer(adapter, &msgs[pos], 2);
		if (ret < 1) {
			pr_err("%s: I2C READ FAILED=[%d]\n", __func__, ret);
			return -EACCES;
		}
	}
	return 0;
}

static int iris_i2c_cmd_four_read(struct addr_val_i2c *val, int len)
{
	int i = 0;
	int ret = -1;
	int pos = 0;
	const int reg_len = 5;
	const int ret_len = 4;
	uint8_t cmd = 0xcc;
	uint8_t slave_addr = 0;
	uint8_t *data = NULL;
	uint8_t *ret_data = NULL;
	struct i2c_client *client = iris_i2c_handle;

	/* for ret value need to be N * len
	 * N is cmd + val+ ret (1+1+1,1+2+2,1+4+4)
	 */
	uint8_t *nine_data_list = NULL;
	struct i2c_msg *msgs = NULL;

	nine_data_list = kmalloc(9 * len * sizeof(nine_data_list[0]), GFP_KERNEL);
	if (!nine_data_list)
		return -ENOMEM;

	msgs = kmalloc(2 * len * sizeof(msgs[0]), GFP_KERNEL);
	if (!msgs) {
		kfree(nine_data_list);
		nine_data_list = NULL;
		return -ENOMEM;
	}

	slave_addr = (client->addr & 0xff);
	memset(msgs, 0x00, 2 * len * sizeof(msgs[0]));

	for (i = 0; i < len; i++) {
		pos = 9 * i;
		nine_data_list[pos] = cmd;
		nine_data_list[pos + 1] = (val[i].addr & 0xff);
		nine_data_list[pos + 2] = ((val[i].addr >> 8) & 0xff);
		nine_data_list[pos + 3] = ((val[i].addr >> 16) & 0xff);
		nine_data_list[pos + 4] = ((val[i].addr >> 24) & 0xff);
		data = &nine_data_list[pos];
		ret_data = &nine_data_list[pos + reg_len];

		pos = i << 1;
		msgs[pos].addr = slave_addr;
		msgs[pos].flags = 0;
		msgs[pos].buf = data;
		msgs[pos].len = reg_len;

		msgs[pos + 1].addr = slave_addr;
		msgs[pos + 1].flags = I2C_M_RD;
		msgs[pos + 1].buf = ret_data;
		msgs[pos + 1].len = ret_len;
	}

	ret = iris_i2c_read_transfer(client->adapter, msgs, len);
	if (ret != 0)
		goto I2C_READ_ERR;

	for (i = 0; i < len; i++) {
		pos = 9 * i + 5;
		val[i].data = (nine_data_list[pos] << 24) | (nine_data_list[pos + 1] << 16)
			| (nine_data_list[pos + 2] << 8) | nine_data_list[pos + 3];
	}

I2C_READ_ERR:
	kfree(nine_data_list);
	nine_data_list = NULL;
	kfree(msgs);
	msgs = NULL;

	return 0;

}

static int iris_i2c_send_msg(struct i2c_msg *msgs, int len)
{
	int retry = 0;
	int ret = -EINVAL;
	struct i2c_client *client = iris_i2c_handle;

	if (msgs == NULL || len < 1)
		return ret;

	//pr_err("iris come here send msg\n");
	while (retry < 3) {
		ret = i2c_transfer(client->adapter, msgs, len);
		if (ret < 1) {
			retry++;
			pr_err("iris meet with error when send i2c msg ret = %d\n", ret);
			i2c_recover_bus(client->adapter);
			udelay(100);
		} else
			break;
	}

	if (retry == 3) {
		pr_err("iris can not transfer msgs\n");
		print_hex_dump(KERN_ERR, "", DUMP_PREFIX_NONE, 16, 4,
				(uint8_t *)(msgs[0].buf) + 1, 8, false);
		return -EINVAL;
	}

	return 0;
}

static int iris_i2c_cmd_four_write(struct addr_val_i2c *val, int len)
{
	int i = 0;
	int ret = -1;
	int pos = 0;
	int mult_val = 0;
	int pos_val = 0;
	const int reg_len = 9;
	const uint8_t cmd = 0xcc;
	uint8_t slave_addr = 0;
	uint8_t *data = NULL;
	struct i2c_client *client = iris_i2c_handle;
	struct i2c_msg *msgs = NULL;

	/* for ret value need to be N * len
	 * N is cmd + addr+ val (1+1+1,1+2+2,1+4+4)
	 */
	uint8_t *nine_data_list = NULL;

	nine_data_list = kmalloc(9 * len * sizeof(nine_data_list[0]), GFP_KERNEL);
	if (!nine_data_list)
		return -ENOMEM;

	msgs = kmalloc_array(len, sizeof(msgs[0]), GFP_KERNEL);
	if (!msgs) {
		kfree(nine_data_list);
		nine_data_list = NULL;
		return -ENOMEM;
	}

	slave_addr = (client->addr & 0xff);
	memset(msgs, 0x00, len * sizeof(msgs[0]));

	for (i = 0; i < len; i++) {
		pos = reg_len * i;
		nine_data_list[pos] = cmd;
		nine_data_list[pos + 1] = (val[i].addr & 0xff);
		nine_data_list[pos + 2] = ((val[i].addr >> 8) & 0xff);
		nine_data_list[pos + 3] = ((val[i].addr >> 16) & 0xff);
		nine_data_list[pos + 4] = ((val[i].addr >> 24) & 0xff);
		nine_data_list[pos + 5] = ((val[i].data >> 24) & 0xff);
		nine_data_list[pos + 6] = ((val[i].data >> 16) & 0xff);
		nine_data_list[pos + 7] = ((val[i].data >> 8) & 0xff);
		nine_data_list[pos + 8] = (val[i].data & 0xff);

		data = &nine_data_list[pos];

		msgs[i].addr = slave_addr;
		msgs[i].flags = 0;
		msgs[i].buf = data;
		msgs[i].len = reg_len;
	}


	/* according to I2C_MSM_BAM_CONS_SZ in i2c_msm_v2.h
	 * the write msg should be less than 32
	 */
	if (len <= MAX_TRANSFER_MSG_LEN) {
		ret = iris_i2c_send_msg(msgs, len);
		if (ret != 0)
			goto I2C_WRITE_ERR;
	} else {
		mult_val = (len / MAX_TRANSFER_MSG_LEN);
		pos_val = len - (mult_val * MAX_TRANSFER_MSG_LEN);
		for (i = 0; i < mult_val; i++) {
			ret = iris_i2c_send_msg(
					&msgs[i * MAX_TRANSFER_MSG_LEN], MAX_TRANSFER_MSG_LEN);
			if (ret != 0)
				goto I2C_WRITE_ERR;
		}

		if (pos_val != 0) {
			ret = iris_i2c_send_msg(&msgs[i * MAX_TRANSFER_MSG_LEN], pos_val);
			if (ret != 0)
				goto I2C_WRITE_ERR;
		}
	}

	ret = 0;

I2C_WRITE_ERR:
	kfree(nine_data_list);
	nine_data_list = NULL;
	kfree(msgs);
	msgs = NULL;

	return ret;

}

static int iris_i2c_rw_t(uint32_t type, struct addr_val_i2c *val, int len)
{
	int ret = -1;

	switch (type) {
	case FOUR_BYTE_REG_LEN_READ:
		ret = iris_i2c_cmd_four_read(val, len);
		break;
	case FOUR_BYTE_REG_LEN_WRITE:
		ret = iris_i2c_cmd_four_write(val, len);
		break;
	default:
		pr_err("can not identify the cmd = %x\n", type);
		return -EINVAL;
	}
	return ret;
}

/*currently we use four byte*/
static int iris_i2c_rw(uint32_t type, struct addr_val_i2c *val, int len)
{
	struct i2c_client *client = iris_i2c_handle;

	if (!client) {
		pr_err("iris i2c handle is NULL\n");
		return -EACCES;
	}

	if (!val || len == 0) {
		pr_err("the return buf = %p or len = %d\n",
				val, len);
		return -EINVAL;
	}

	return iris_i2c_rw_t(type, val, len);
}

static int iris_i2c_single_conver_ocp_read(uint32_t *ptr, uint32_t len)
{
	int ret, i;
	struct addr_val_i2c *val_tmp;
	uint32_t base_addr = *ptr;
	u8 *p = NULL;

	p = kmalloc(sizeof(*val_tmp) * (len+1), GFP_KERNEL);
	if (p == NULL) {
		pr_err("[iris] %s: allocate memory fails\n", __func__);
		return -EINVAL;
	}
	val_tmp = (struct addr_val_i2c *)p;
	pr_err("%s,%d,len=%d\n", __func__, __LINE__, len);


	for (i = 0; i < len; i++) {
		val_tmp[i].addr = base_addr + i*4;
		val_tmp[i].data = 0x0;
	}

	ret = iris_i2c_rw(FOUR_BYTE_REG_LEN_READ, val_tmp, len);

	for (i = 0; i < len; i++)
		ptr[i] = val_tmp[i].data;

	kfree(p);
	p = NULL;

	return ret;
}

static int iris_i2c_burst_conver_ocp_read(uint32_t *ptr, uint32_t dlen)
{
	int i;
	int ret = -1;
	const uint8_t cmd = 0xfc;
	u32 msg_len = 0;
	u8 slave_addr = 0;
	uint32_t reg_num = 0;
	u32 start_addr = 0;
	struct i2c_msg msgs[2];


	u8 *iris_payload = NULL;
	struct i2c_client *client = iris_i2c_handle;
#if 0
	if (!ptr || dlen < 2) {
		pr_err("the parameter is not right\n");
		return -EINVAL;
	}
#endif
	start_addr = *ptr;
	reg_num = dlen;

	slave_addr = (client->addr) & 0xff;
	memset(msgs, 0x00, 2 * sizeof(msgs[0]));

	msg_len = reg_num * 4;

	iris_payload = kmalloc(sizeof(iris_payload[0]) * (5+msg_len), GFP_KERNEL);
	if (iris_payload == NULL) {
		pr_err("[iris] %s: allocate memory fails\n", __func__);
		return -EINVAL;
	}


	iris_payload[0] = cmd;
	iris_payload[1] = (start_addr & 0xff);
	iris_payload[2] = ((start_addr >> 8) & 0xff);
	iris_payload[3] = ((start_addr >> 16) & 0xff);
	iris_payload[4] = ((start_addr >> 24) & 0xff);

	for (i = 0; i < reg_num; i++) {
		iris_payload[i*4 + 5] = 0x00;
		iris_payload[i*4 + 6] = 0x00;
		iris_payload[i*4 + 7] = 0x00;
		iris_payload[i*4 + 8] = 0x00;
	}

	msgs[0].addr = slave_addr;
	msgs[0].flags = 0;
	msgs[0].buf = iris_payload;
	msgs[0].len = 5;

	msgs[1].addr = slave_addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].buf = iris_payload+5;
	msgs[1].len = msg_len;

	ret = i2c_transfer(client->adapter, msgs, 2);

	if (ret == 2) {
		ret = 0;
	} else {
		ret = ret < 0 ? ret : -EIO;
		pr_err("[iris] %s: i2c_transfer failed, ret=%d\n", __func__, ret);
	}

	for (i = 0; i < reg_num; i++) {
		ptr[i] = (iris_payload[i*4 + 5]<<24) |
			(iris_payload[i*4 + 6]<<16) |
			(iris_payload[i*4 + 7]<<8)  |
			(iris_payload[i*4 + 8]);
	}

	kfree(iris_payload);
	iris_payload = NULL;

	return ret;

}

int iris_i2c_conver_ocp_read(uint32_t *ptr, uint32_t len, bool is_burst)
{
	int ret = -1;

	if (!iris_i2c_handle)
		return -EINVAL;

	if (is_burst)
		ret = iris_i2c_burst_conver_ocp_read(ptr, len);
	else
		ret = iris_i2c_single_conver_ocp_read(ptr, len);

	return ret;
}
static int iris_i2c_single_write(struct addr_val_i2c *val, int len)
{
	return iris_i2c_rw(FOUR_BYTE_REG_LEN_WRITE, val, len);
}
int iris_i2c_single_conver_ocp_write(uint32_t *arr, uint32_t dlen)
{
	int ret, i;
	struct addr_val_i2c *val_tmp;
	u8 *p = NULL;

	if (!iris_i2c_handle)
		return -EINVAL;

	p = kmalloc(sizeof(*val_tmp) * (dlen)+1, GFP_KERNEL);
	if (p == NULL) {
		pr_err("[iris] %s: allocate memory fails\n", __func__);
		return -EINVAL;
	}

	val_tmp = (struct addr_val_i2c *)p;
	for (i = 0; i < dlen; i++) {
		val_tmp->addr = arr[2*i];
		val_tmp->data = arr[2*i+1];
		val_tmp = val_tmp+1;
	}

	ret = iris_i2c_single_write((struct addr_val_i2c *)p, dlen);
	kfree(p);
	p = NULL;

	return ret;
}

int iris_i2c_burst_conver_ocp_write(uint32_t base_addr, uint32_t *arr, uint32_t dlen)
{

	int i;
	int ret = -1;
	const uint8_t cmd = 0xfc;
	u32 msg_len = 0;
	u8 slave_addr = 0;
	uint32_t reg_num = 0;
	u32 start_addr = 0;
	struct i2c_msg msgs;

	u8 *iris_payload = NULL;
	u32 *lut_buffer = NULL;

	struct i2c_client *client = iris_i2c_handle;

	if (!iris_i2c_handle)
		return -EINVAL;

	if (!arr || dlen < 1) {
		pr_err("the parameter is not right\n");
		return -EINVAL;
	}

	start_addr = base_addr;
	lut_buffer = arr + 1;
	reg_num = dlen;

	slave_addr = (client->addr) & 0xff;

	memset(&msgs, 0x00, sizeof(msgs));

	msg_len = 5 + reg_num * 4;

	iris_payload = kmalloc_array(msg_len, sizeof(iris_payload[0]), GFP_KERNEL);
	if (iris_payload == NULL) {
		pr_err("[iris3] %s: allocate memory fails\n", __func__);
		return -EINVAL;
	}


	iris_payload[0] = cmd;
	iris_payload[1] = (start_addr & 0xff);
	iris_payload[2] = ((start_addr >> 8) & 0xff);
	iris_payload[3] = ((start_addr >> 16) & 0xff);
	iris_payload[4] = ((start_addr >> 24) & 0xff);

	for (i = 0; i < reg_num; i++) {
		iris_payload[i*4 + 5] = ((lut_buffer[i] >> 24) & 0xff);
		iris_payload[i*4 + 6] = ((lut_buffer[i] >> 16) & 0xff);
		iris_payload[i*4 + 7] = ((lut_buffer[i] >> 8) & 0xff);
		iris_payload[i*4 + 8] = (lut_buffer[i] & 0xff);
	}

	msgs.addr = slave_addr;
	msgs.flags = 0;
	msgs.buf = iris_payload;
	msgs.len = msg_len;

	ret = i2c_transfer(client->adapter, &msgs, 1);

	if (ret == 1) {
		ret = 0;
	} else {
		ret = ret < 0 ? ret : -EIO;
		pr_err("[iris3] %s: i2c_transfer failed, ret=%d\n", __func__, ret);
	}

	kfree(iris_payload);
	iris_payload = NULL;

	return ret;

}

int iris_i2c_conver_ocp_write(uint32_t base_addr, uint32_t *ptr, uint32_t len, bool is_burst)
{
	int ret = 0;

	if (is_burst)
		ret = iris_i2c_burst_conver_ocp_write(base_addr, ptr, len-1);
	else
		ret = iris_i2c_single_conver_ocp_write(ptr, len/2);

	return ret;
}

int iris_i2c_group_write(struct iris_i2c_msg *iris_i2c_msg, uint32_t iris_i2c_msg_num)
{

	int i, j, k;
	int ret = -1;
	const uint8_t cmd = 0xfc;
	u32 msg_len = 0;
	u8 slave_addr = 0;
	uint32_t reg_num = 0;
	u32 start_addr = 0;
	struct i2c_msg *msgs;

	u8 *iris_payload = NULL;
	u32 *lut_buffer = NULL;

	struct i2c_client *client = iris_i2c_handle;

	if (!iris_i2c_handle)
		return -EINVAL;

	if (!iris_i2c_msg || iris_i2c_msg_num < 1) {
		pr_err("the parameter is not right\n");
		return -EINVAL;
	}

	msgs = kmalloc(sizeof(struct i2c_msg) * iris_i2c_msg_num + 1, GFP_KERNEL);
	if (msgs == NULL) {
		pr_err("[iris3] %s:%d: allocate memory fails\n", __func__, __LINE__);
		return -EINVAL;
	}
	memset(msgs, 0x00, sizeof(struct i2c_msg) * iris_i2c_msg_num);


	for (j = 0; j < iris_i2c_msg_num; j++) {

		start_addr = iris_i2c_msg[j].base_addr;
		lut_buffer = iris_i2c_msg[j].payload + 1;
		reg_num = iris_i2c_msg[j].len - 1;

		slave_addr = (client->addr) & 0xff;

		msg_len = 5 + reg_num * 4;

		iris_payload = kmalloc_array(msg_len, sizeof(iris_payload[0]), GFP_KERNEL);
		if (iris_payload == NULL) {
			pr_err("[iris3] %s: allocate memory fails\n", __func__);
			goto I2C_TRANSFER_ERR;
		}

		iris_payload[0] = cmd;
		iris_payload[1] = (start_addr & 0xff);
		iris_payload[2] = ((start_addr >> 8) & 0xff);
		iris_payload[3] = ((start_addr >> 16) & 0xff);
		iris_payload[4] = ((start_addr >> 24) & 0xff);

		for (i = 0; i < reg_num; i++) {
			iris_payload[i*4 + 5] = ((lut_buffer[i] >> 24) & 0xff);
			iris_payload[i*4 + 6] = ((lut_buffer[i] >> 16) & 0xff);
			iris_payload[i*4 + 7] = ((lut_buffer[i] >> 8) & 0xff);
			iris_payload[i*4 + 8] = (lut_buffer[i] & 0xff);
		}

		msgs[j].addr = slave_addr;
		msgs[j].flags = 0;
		msgs[j].buf = iris_payload;
		msgs[j].len = msg_len;
	}
	ret = i2c_transfer(client->adapter, msgs, iris_i2c_msg_num);

	if (ret == iris_i2c_msg_num) {
		ret = 0;
	} else {
		ret = ret < 0 ? ret : -EIO;
		pr_err("[iris3] %s: i2c_transfer failed, ret=%d\n", __func__, ret);
	}

I2C_TRANSFER_ERR:
	for (k = 0; k < j; k++) {
		kfree(msgs[k].buf);
		msgs[k].buf = NULL;
	}
	kfree(msgs);
	msgs = NULL;

	return ret;

}

static int iris_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *dev_id)
{
	iris_i2c_handle = client;
	return 0;
}

static int iris_i2c_remove(struct i2c_client *client)
{
	iris_i2c_handle = NULL;
	return 0;
}

static const struct i2c_device_id iris_i2c_id_table[] = {
	{IRIS_I2C_DRIVER_NAME, 0},
	{},
};


static const struct of_device_id iris_match_table[] = {
	{.compatible = IRIS_COMPATIBLE_NAME,},
	{ },
};

static struct i2c_driver plx_i2c_driver = {
	.driver = {
		.name = IRIS_I2C_DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = iris_match_table,
	},
	.probe = iris_i2c_probe,
	.remove =  iris_i2c_remove,
	.id_table = iris_i2c_id_table,
};


int iris_i2c_bus_init(void)
{
	int ret;

	pr_err("%s initialize begin!\n", __func__);
	ret = i2c_add_driver(&plx_i2c_driver);
	if (ret != 0)
		pr_err("i2c add driver fail: %d\n", ret);
	return 0;
}

void iris_i2c_bus_exit(void)
{
	i2c_del_driver(&plx_i2c_driver);
	iris_i2c_remove(iris_i2c_handle);
}


module_init(iris_i2c_bus_init);
module_exit(iris_i2c_bus_exit);

