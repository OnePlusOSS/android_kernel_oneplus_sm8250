// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <cam_sensor_cmn_header.h>
#include "cam_sensor_core.h"
#include "cam_sensor_util.h"
#include "cam_soc_util.h"
#include "cam_trace.h"
#include "cam_common_util.h"
#include "cam_packet_util.h"
#ifdef CONFIG_OEM_BOOT_MODE
#include <linux/oem/boot_mode.h>
#endif
#ifdef CONFIG_PROJECT_INFO
#include <linux/oem/project_info.h>
#endif

struct camera_vendor_match_tbl {
	uint16_t sensor_id;
	char sensor_name[32];
	char vendor_name[32];
};

static struct camera_vendor_match_tbl match_tbl[] = {
	{0x586,  "imx586", "Sony"    },
	{0x30d5, "s5k3m5", "Samsung" },
	{0x5035, "gc5035", "Galaxyc"  },
	{0x471,  "imx471", "Sony"    },
	{0x481,  "imx481", "Sony"    },
	{0x2375, "gc2375", "Galaxyc"  },
        {0x689,  "imx689", "Sony"    },
        {0x0616, "imx616", "Sony"    },
        {0x4608, "hi846",  "Hynix"   },
        {0x8054, "gc8054", "Galaxyc" },
        {0x2b,   "ov02b10","OmniVision" },
        {0x88,   "ov8856", "OmniVision" },
        {0x02,   "gc02m1b", "Galaxyc" },
};


/******************** GC5035_OTP_EDIT_END*******************/
struct cam_sensor_dpc_reg_setting_array {
	struct cam_sensor_i2c_reg_array reg_setting[25];
	unsigned short size;
	enum camera_sensor_i2c_type addr_type;
	enum camera_sensor_i2c_type data_type;
	unsigned short delay;
};

struct cam_sensor_dpc_reg_setting_array gc5035OTPWrite_setting[7] = {
#include "CAM_GC5035_SPC_SENSOR_SETTINGS.h"
};

uint32_t totalDpcNum = 0;
uint32_t totalDpcFlag = 0;
uint32_t gc5035_chipversion_buffer[26]={0};

static int sensor_gc5035_get_dpc_data(struct cam_sensor_ctrl_t * s_ctrl)
{
	int rc = 0;
	uint32_t gc5035_dpcinfo[3] = {0};
	uint32_t i;
	uint32_t dpcinfoOffet = 0xcd;
	uint32_t chipPage8Offet = 0xd0;
	uint32_t chipPage9Offet = 0xc0;

	struct cam_sensor_i2c_reg_setting sensor_setting;
	/*write otp read init settings*/
	sensor_setting.reg_setting = gc5035OTPWrite_setting[0].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[0].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[0].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[0].size;
	sensor_setting.delay = gc5035OTPWrite_setting[0].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);

	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	/*write dpc page0 setting*/
	sensor_setting.reg_setting = gc5035OTPWrite_setting[1].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[1].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[1].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[1].size;
	sensor_setting.delay = gc5035OTPWrite_setting[1].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);

	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}

	/*read dpc data*/
	for (i = 0; i < 3; i++) {
	    rc = camera_io_dev_read(
	         &(s_ctrl->io_master_info),
	         dpcinfoOffet + i,
	         &gc5035_dpcinfo[i], CAMERA_SENSOR_I2C_TYPE_BYTE,
	         CAMERA_SENSOR_I2C_TYPE_BYTE);
	    if (rc < 0) {
	        CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to read dpc info sensor setting");
	        break;
	    }
            CAM_ERR(CAM_SENSOR, "info i=%d addr=0x%x data=0x%x",i,dpcinfoOffet + i,gc5035_dpcinfo[i]);
	}

	if (rc < 0)
	   return rc;
	/*close read data*/
	sensor_setting.reg_setting = gc5035OTPWrite_setting[2].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[2].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[2].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[2].size;
	sensor_setting.delay = gc5035OTPWrite_setting[2].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}

	for (i = 0; i < 19; i++) {
	    CAM_DBG(CAM_SENSOR, "gc5035SpcWrite_setting gc5035_dpcinfo[%d] = %x",i,gc5035_dpcinfo[i]);
	}
	if (gc5035_dpcinfo[0] == 1) {
	    totalDpcFlag = 1;
	    totalDpcNum = gc5035_dpcinfo[1] + gc5035_dpcinfo[2] ;
	    CAM_INFO(CAM_SENSOR, "gc5035SpcWrite_setting gc5035_dpcinfo[1] = %d",gc5035_dpcinfo[1]);
	    CAM_INFO(CAM_SENSOR, "gc5035SpcWrite_setting gc5035_dpcinfo[2] = %d",gc5035_dpcinfo[2]);
	    CAM_INFO(CAM_SENSOR, "gc5035SpcWrite_setting totalDpcNum = %d",totalDpcNum);

	}
	//write for update reg for page 8
	sensor_setting.reg_setting = gc5035OTPWrite_setting[5].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[5].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[5].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[5].size;
	sensor_setting.delay = gc5035OTPWrite_setting[5].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	for (i = 0; i < 0x10; i++) {
	    rc = camera_io_dev_read(
	         &(s_ctrl->io_master_info),
	         chipPage8Offet + i,
	         &gc5035_chipversion_buffer[i], CAMERA_SENSOR_I2C_TYPE_BYTE,
	         CAMERA_SENSOR_I2C_TYPE_BYTE);
	    if (rc < 0) {
	        CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to read dpc info sensor setting");
	        break;
	    }
	}
	/*close read data*/
	sensor_setting.reg_setting = gc5035OTPWrite_setting[2].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[2].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[2].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[2].size;
	sensor_setting.delay = gc5035OTPWrite_setting[2].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	//write for update reg for page 9
	sensor_setting.reg_setting = gc5035OTPWrite_setting[6].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[6].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[6].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[6].size;
	sensor_setting.delay = gc5035OTPWrite_setting[6].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	for (i = 0x00; i < 0x0a; i++) {
	    rc = camera_io_dev_read(
	          &(s_ctrl->io_master_info),
	          chipPage9Offet + i,
	          &gc5035_chipversion_buffer[0x10+i], CAMERA_SENSOR_I2C_TYPE_BYTE,
	          CAMERA_SENSOR_I2C_TYPE_BYTE);
	    if (rc < 0) {
	        CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to read dpc info sensor setting");
	        break;
	    }
	}
	/*close read data*/
	sensor_setting.reg_setting = gc5035OTPWrite_setting[2].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[2].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[2].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[2].size;
	sensor_setting.delay = gc5035OTPWrite_setting[2].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
        CAM_INFO(CAM_SENSOR, "gc5035S get dpc setting success");
	return rc;

}

static int sensor_gc5035_write_dpc_data(struct cam_sensor_ctrl_t * s_ctrl)
{
    int rc = 0;
    struct cam_sensor_i2c_reg_array gc5035SpcTotalNum_setting[2];
    struct cam_sensor_i2c_reg_setting sensor_setting;
    /*for test
    struct cam_sensor_i2c_reg_array gc5035SRAM_setting;
    uint32_t temp_val[4];
    int j,i;*/

    if (totalDpcFlag == 0)
        return 0;

	sensor_setting.reg_setting = gc5035OTPWrite_setting[3].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[3].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[3].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[3].size;
	sensor_setting.delay = gc5035OTPWrite_setting[3].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);

	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	gc5035SpcTotalNum_setting[0].reg_addr = 0x01;
	gc5035SpcTotalNum_setting[0].reg_data = (totalDpcNum >> 8) & 0x07;
	gc5035SpcTotalNum_setting[0].delay = gc5035SpcTotalNum_setting[0].data_mask = 0;

	gc5035SpcTotalNum_setting[1].reg_addr = 0x02;
	gc5035SpcTotalNum_setting[1].reg_data = totalDpcNum & 0xff;
	gc5035SpcTotalNum_setting[1].delay = gc5035SpcTotalNum_setting[1].data_mask = 0;

	sensor_setting.reg_setting = gc5035SpcTotalNum_setting;
	sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.size = 2;
	sensor_setting.delay = 0;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);

	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}

	sensor_setting.reg_setting = gc5035OTPWrite_setting[4].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[4].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[4].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[4].size;
	sensor_setting.delay = gc5035OTPWrite_setting[4].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);

	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
        CAM_INFO(CAM_SENSOR, "gc5035SpcWrite_setting  write sensor setting success");
        /*for test
        gc5035SpcTotalNum_setting[0].reg_addr = 0xfe;
	gc5035SpcTotalNum_setting[0].reg_data = 0x02;
	gc5035SpcTotalNum_setting[0].delay = gc5035SpcTotalNum_setting[0].data_mask = 0;

	gc5035SpcTotalNum_setting[1].reg_addr = 0xbe;
	gc5035SpcTotalNum_setting[1].reg_data = 0x00;
	gc5035SpcTotalNum_setting[1].delay = gc5035SpcTotalNum_setting[1].data_mask = 0;

	sensor_setting.reg_setting = gc5035SpcTotalNum_setting;
	sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.size = 2;
	sensor_setting.delay = 0;
	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);

	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	for (i=0; i<totalDpcNum*4; i++) {
	gc5035SRAM_setting.reg_addr = 0xaa;
	gc5035SRAM_setting.reg_data = i;
	gc5035SRAM_setting.delay = gc5035SRAM_setting.data_mask = 0;
	sensor_setting.reg_setting = &gc5035SRAM_setting;
	sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.size = 1;
	sensor_setting.delay = 0;
	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
	for (j=0; j<4; j++) {
	    rc = camera_io_dev_read(
	         &(s_ctrl->io_master_info),
	         0xac,
	         &temp_val[j], CAMERA_SENSOR_I2C_TYPE_BYTE,
	         CAMERA_SENSOR_I2C_TYPE_BYTE);
	    if (rc < 0) {
	       CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to read dpc info sensor setting");
	       break;
	    }
	}
	CAM_ERR(CAM_SENSOR,"GC5035_OTP_GC val0 = 0x%x , val1 = 0x%x , val2 = 0x%x,val3 = 0x%x \n",
	temp_val[0],temp_val[1],temp_val[2],temp_val[3]);
	CAM_ERR(CAM_SENSOR,"GC5035_OTP_GC x = %d , y = %d ,type = %d \n",
	        ((temp_val[1]&0x0f)<<8) + temp_val[0],((temp_val[2]&0x7f)<<4) + ((temp_val[1]&0xf0)>>4),(((temp_val[3]&0x01)<<1)+((temp_val[2]&0x80)>>7)));
	}

	gc5035SpcTotalNum_setting[0].reg_addr = 0xbe;
	gc5035SpcTotalNum_setting[0].reg_data = 0x01;
	gc5035SpcTotalNum_setting[0].delay = gc5035SpcTotalNum_setting[0].data_mask = 0;

	gc5035SpcTotalNum_setting[1].reg_addr = 0xfe;
	gc5035SpcTotalNum_setting[1].reg_data = 0x00;
	gc5035SpcTotalNum_setting[1].delay = gc5035SpcTotalNum_setting[1].data_mask = 0;

	sensor_setting.reg_setting = gc5035SpcTotalNum_setting;
	sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.size = 2;
	sensor_setting.delay = 0;
	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);

	if (rc < 0) {
	   CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	   return rc;
	}
        */
	return rc;
}

static int sensor_gc5035_update_reg(struct cam_sensor_ctrl_t * s_ctrl)
{
	int rc = -1;
	uint8_t flag_chipv = 0;
	int i = 0;
	uint8_t VALID_FLAG = 0x01;
	uint8_t CHIPV_FLAG_OFFSET = 0x0;
	uint8_t CHIPV_OFFSET = 0x01;
	uint8_t reg_setting_size = 0;
	struct cam_sensor_i2c_reg_array gc5035_update_reg_setting[20];
	struct cam_sensor_i2c_reg_setting sensor_setting;
	CAM_DBG(CAM_SENSOR,"Enter");

	flag_chipv = gc5035_chipversion_buffer[CHIPV_FLAG_OFFSET];
	CAM_DBG(CAM_SENSOR,"gc5035 otp chipv flag_chipv: 0x%x", flag_chipv);
	if (VALID_FLAG != (flag_chipv & 0x03)) {
	    CAM_ERR(CAM_SENSOR,"gc5035 otp chip regs data is Empty/Invalid!");
	    return rc;
	}

	for (i = 0; i < 5; i++) {
	    if (VALID_FLAG == ((gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i] >> 3) & 0x01)) {
	        gc5035_update_reg_setting[reg_setting_size].reg_addr = 0xfe;
	        gc5035_update_reg_setting[reg_setting_size].reg_data = gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i] & 0x07;
	        gc5035_update_reg_setting[reg_setting_size].delay = gc5035_update_reg_setting[reg_setting_size].data_mask = 0;
	        reg_setting_size++;
	        gc5035_update_reg_setting[reg_setting_size].reg_addr = gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i + 1];
	        gc5035_update_reg_setting[reg_setting_size].reg_data = gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i + 2];
	        gc5035_update_reg_setting[reg_setting_size].delay = gc5035_update_reg_setting[reg_setting_size].data_mask = 0;
	        reg_setting_size++;

	        CAM_DBG(CAM_SENSOR,"gc5035 otp chipv : 0xfe=0x%x, addr[%d]=0x%x, value[%d]=0x%x", gc5035_chipversion_buffer[CHIPV_OFFSET +  5 * i] & 0x07,i*2,
	                gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i + 1],i*2,gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i + 2]);
	    }
	    if (VALID_FLAG == ((gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i] >> 7) & 0x01)) {
	        gc5035_update_reg_setting[reg_setting_size].reg_addr = 0xfe;
	        gc5035_update_reg_setting[reg_setting_size].reg_data = (gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i] & 0x70) >> 4;
	        gc5035_update_reg_setting[reg_setting_size].delay = gc5035_update_reg_setting[reg_setting_size].data_mask = 0;
	        reg_setting_size++;
	        gc5035_update_reg_setting[reg_setting_size].reg_addr = gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i + 3];
	        gc5035_update_reg_setting[reg_setting_size].reg_data = gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i + 4];
	        gc5035_update_reg_setting[reg_setting_size].delay = gc5035_update_reg_setting[reg_setting_size].data_mask = 0;
	        reg_setting_size++;

	        CAM_DBG(CAM_SENSOR,"gc5035 otp chipv : 0xfe=0x%x, addr[%d]=0x%x, value[%d]=0x%x", (gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i] & 0x70) >> 4,i*2+1,
	                gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i + 3],i*2+1,gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i + 4]);
	    }
	}
	sensor_setting.reg_setting = gc5035_update_reg_setting;
	sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.size = reg_setting_size;
	sensor_setting.delay = 0;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);

	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	rc = 0;
	CAM_DBG(CAM_SENSOR,"Exit");
	return rc;

}
/******************** GC5035_OTP_EDIT_END*******************/

static void cam_sensor_update_req_mgr(
	struct cam_sensor_ctrl_t *s_ctrl,
	struct cam_packet *csl_packet)
{
	struct cam_req_mgr_add_request add_req;

	add_req.link_hdl = s_ctrl->bridge_intf.link_hdl;
	add_req.req_id = csl_packet->header.request_id;
	CAM_DBG(CAM_SENSOR, " Rxed Req Id: %lld",
		csl_packet->header.request_id);
	add_req.dev_hdl = s_ctrl->bridge_intf.device_hdl;
	add_req.skip_before_applying = 0;
	if (s_ctrl->bridge_intf.crm_cb &&
		s_ctrl->bridge_intf.crm_cb->add_req)
		s_ctrl->bridge_intf.crm_cb->add_req(&add_req);

	CAM_DBG(CAM_SENSOR, " add req to req mgr: %lld",
			add_req.req_id);
}

static void cam_sensor_release_stream_rsc(
	struct cam_sensor_ctrl_t *s_ctrl)
{
	struct i2c_settings_array *i2c_set = NULL;
	int rc;

	i2c_set = &(s_ctrl->i2c_data.streamoff_settings);
	if (i2c_set->is_settings_valid == 1) {
		i2c_set->is_settings_valid = -1;
		rc = delete_request(i2c_set);
		if (rc < 0)
			CAM_ERR(CAM_SENSOR,
				"failed while deleting Streamoff settings");
	}

	i2c_set = &(s_ctrl->i2c_data.streamon_settings);
	if (i2c_set->is_settings_valid == 1) {
		i2c_set->is_settings_valid = -1;
		rc = delete_request(i2c_set);
		if (rc < 0)
			CAM_ERR(CAM_SENSOR,
				"failed while deleting Streamon settings");
	}
}

static void cam_sensor_release_per_frame_resource(
	struct cam_sensor_ctrl_t *s_ctrl)
{
	struct i2c_settings_array *i2c_set = NULL;
	int i, rc;

	if (s_ctrl->i2c_data.per_frame != NULL) {
		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
			i2c_set = &(s_ctrl->i2c_data.per_frame[i]);
			if (i2c_set->is_settings_valid == 1) {
				i2c_set->is_settings_valid = -1;
				rc = delete_request(i2c_set);
				if (rc < 0)
					CAM_ERR(CAM_SENSOR,
						"delete request: %lld rc: %d",
						i2c_set->request_id, rc);
			}
		}
	}
}

static int32_t cam_sensor_i2c_pkt_parse(struct cam_sensor_ctrl_t *s_ctrl,
	void *arg)
{
	int32_t rc = 0;
	uintptr_t generic_ptr;
	struct cam_control *ioctl_ctrl = NULL;
	struct cam_packet *csl_packet = NULL;
	struct cam_cmd_buf_desc *cmd_desc = NULL;
	struct i2c_settings_array *i2c_reg_settings = NULL;
	size_t len_of_buff = 0;
	size_t remain_len = 0;
	uint32_t *offset = NULL;
	struct cam_config_dev_cmd config;
	struct i2c_data_settings *i2c_data = NULL;

	ioctl_ctrl = (struct cam_control *)arg;

	if (ioctl_ctrl->handle_type != CAM_HANDLE_USER_POINTER) {
		CAM_ERR(CAM_SENSOR, "Invalid Handle Type");
		return -EINVAL;
	}

	if (copy_from_user(&config,
		u64_to_user_ptr(ioctl_ctrl->handle),
		sizeof(config)))
		return -EFAULT;

	rc = cam_mem_get_cpu_buf(
		config.packet_handle,
		&generic_ptr,
		&len_of_buff);
	if (rc < 0) {
		CAM_ERR(CAM_SENSOR, "Failed in getting the packet: %d", rc);
		return rc;
	}

	remain_len = len_of_buff;
	if ((sizeof(struct cam_packet) > len_of_buff) ||
		((size_t)config.offset >= len_of_buff -
		sizeof(struct cam_packet))) {
		CAM_ERR(CAM_SENSOR,
			"Inval cam_packet strut size: %zu, len_of_buff: %zu",
			 sizeof(struct cam_packet), len_of_buff);
		rc = -EINVAL;
		goto end;
	}

	remain_len -= (size_t)config.offset;
	csl_packet = (struct cam_packet *)(generic_ptr +
		(uint32_t)config.offset);

	if (cam_packet_util_validate_packet(csl_packet,
		remain_len)) {
		CAM_ERR(CAM_SENSOR, "Invalid packet params");
		rc = -EINVAL;
		goto end;

	}

	if ((csl_packet->header.op_code & 0xFFFFFF) !=
		CAM_SENSOR_PACKET_OPCODE_SENSOR_INITIAL_CONFIG &&
		csl_packet->header.request_id <= s_ctrl->last_flush_req
		&& s_ctrl->last_flush_req != 0) {
		CAM_ERR(CAM_SENSOR,
			"reject request %lld, last request to flush %u",
			csl_packet->header.request_id, s_ctrl->last_flush_req);
		rc = -EINVAL;
		goto end;
	}

	if (csl_packet->header.request_id > s_ctrl->last_flush_req)
		s_ctrl->last_flush_req = 0;

	i2c_data = &(s_ctrl->i2c_data);
	CAM_DBG(CAM_SENSOR, "Header OpCode: %d", csl_packet->header.op_code);
	switch (csl_packet->header.op_code & 0xFFFFFF) {
	case CAM_SENSOR_PACKET_OPCODE_SENSOR_INITIAL_CONFIG: {
		i2c_reg_settings = &i2c_data->init_settings;
		i2c_reg_settings->request_id = 0;
		i2c_reg_settings->is_settings_valid = 1;
		break;
	}
	case CAM_SENSOR_PACKET_OPCODE_SENSOR_CONFIG: {
		i2c_reg_settings = &i2c_data->config_settings;
		i2c_reg_settings->request_id = 0;
		i2c_reg_settings->is_settings_valid = 1;
		break;
	}
	case CAM_SENSOR_PACKET_OPCODE_SENSOR_STREAMON: {
		if (s_ctrl->streamon_count > 0)
			goto end;

		s_ctrl->streamon_count = s_ctrl->streamon_count + 1;
		i2c_reg_settings = &i2c_data->streamon_settings;
		i2c_reg_settings->request_id = 0;
		i2c_reg_settings->is_settings_valid = 1;
		break;
	}
	case CAM_SENSOR_PACKET_OPCODE_SENSOR_STREAMOFF: {
		if (s_ctrl->streamoff_count > 0)
			goto end;

		s_ctrl->streamoff_count = s_ctrl->streamoff_count + 1;
		i2c_reg_settings = &i2c_data->streamoff_settings;
		i2c_reg_settings->request_id = 0;
		i2c_reg_settings->is_settings_valid = 1;
		break;
	}

	case CAM_SENSOR_PACKET_OPCODE_SENSOR_UPDATE: {
		if ((s_ctrl->sensor_state == CAM_SENSOR_INIT) ||
			(s_ctrl->sensor_state == CAM_SENSOR_ACQUIRE)) {
			CAM_WARN(CAM_SENSOR,
				"Rxed Update packets without linking");
			goto end;
		}

		i2c_reg_settings =
			&i2c_data->per_frame[csl_packet->header.request_id %
				MAX_PER_FRAME_ARRAY];
		CAM_DBG(CAM_SENSOR, "Received Packet: %lld req: %lld",
			csl_packet->header.request_id % MAX_PER_FRAME_ARRAY,
			csl_packet->header.request_id);
		if (i2c_reg_settings->is_settings_valid == 1) {
			CAM_ERR(CAM_SENSOR,
				"Already some pkt in offset req : %lld",
				csl_packet->header.request_id);
			/*
			 * Update req mgr even in case of failure.
			 * This will help not to wait indefinitely
			 * and freeze. If this log is triggered then
			 * fix it.
			 */
			cam_sensor_update_req_mgr(s_ctrl, csl_packet);
			goto end;
		}
		break;
	}
	case CAM_SENSOR_PACKET_OPCODE_SENSOR_NOP: {
		if ((s_ctrl->sensor_state == CAM_SENSOR_INIT) ||
			(s_ctrl->sensor_state == CAM_SENSOR_ACQUIRE)) {
			CAM_WARN(CAM_SENSOR,
				"Rxed NOP packets without linking");
			goto end;
		}

		cam_sensor_update_req_mgr(s_ctrl, csl_packet);
		goto end;
	}
	default:
		CAM_ERR(CAM_SENSOR, "Invalid Packet Header");
		rc = -EINVAL;
		goto end;
	}

	offset = (uint32_t *)&csl_packet->payload;
	offset += csl_packet->cmd_buf_offset / 4;
	cmd_desc = (struct cam_cmd_buf_desc *)(offset);

	rc = cam_sensor_i2c_command_parser(&s_ctrl->io_master_info,
			i2c_reg_settings, cmd_desc, 1);
	if (rc < 0) {
		CAM_ERR(CAM_SENSOR, "Fail parsing I2C Pkt: %d", rc);
		goto end;
	}

	if ((csl_packet->header.op_code & 0xFFFFFF) ==
		CAM_SENSOR_PACKET_OPCODE_SENSOR_UPDATE) {
		i2c_reg_settings->request_id =
			csl_packet->header.request_id;
		cam_sensor_update_req_mgr(s_ctrl, csl_packet);
	}

end:
	return rc;
}

static int32_t cam_sensor_i2c_modes_util(
	struct camera_io_master *io_master_info,
	struct i2c_settings_list *i2c_list)
{
	int32_t rc = 0;
	uint32_t i, size;

	if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_RANDOM) {
		rc = camera_io_dev_write(io_master_info,
			&(i2c_list->i2c_settings));
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR,
				"Failed to random write I2C settings: %d",
				rc);
			return rc;
		}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_SEQ) {
		rc = camera_io_dev_write_continuous(
			io_master_info,
			&(i2c_list->i2c_settings),
			0);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR,
				"Failed to seq write I2C settings: %d",
				rc);
			return rc;
		}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_BURST) {
		rc = camera_io_dev_write_continuous(
			io_master_info,
			&(i2c_list->i2c_settings),
			1);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR,
				"Failed to burst write I2C settings: %d",
				rc);
			return rc;
		}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_POLL) {
		size = i2c_list->i2c_settings.size;
		for (i = 0; i < size; i++) {
			rc = camera_io_dev_poll(
			io_master_info,
			i2c_list->i2c_settings.reg_setting[i].reg_addr,
			i2c_list->i2c_settings.reg_setting[i].reg_data,
			i2c_list->i2c_settings.reg_setting[i].data_mask,
			i2c_list->i2c_settings.addr_type,
			i2c_list->i2c_settings.data_type,
			i2c_list->i2c_settings.reg_setting[i].delay);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,
					"i2c poll apply setting Fail: %d", rc);
				return rc;
			}
		}
	}

	return rc;
}

int32_t cam_sensor_update_i2c_info(struct cam_cmd_i2c_info *i2c_info,
	struct cam_sensor_ctrl_t *s_ctrl)
{
	int32_t rc = 0;
	struct cam_sensor_cci_client   *cci_client = NULL;

	if (s_ctrl->io_master_info.master_type == CCI_MASTER) {
		cci_client = s_ctrl->io_master_info.cci_client;
		if (!cci_client) {
			CAM_ERR(CAM_SENSOR, "failed: cci_client %pK",
				cci_client);
			return -EINVAL;
		}
		cci_client->cci_i2c_master = s_ctrl->cci_i2c_master;
		cci_client->sid = i2c_info->slave_addr >> 1;
		cci_client->retries = 3;
		cci_client->id_map = 0;
		cci_client->i2c_freq_mode = i2c_info->i2c_freq_mode;
		CAM_DBG(CAM_SENSOR, " Master: %d sid: %d freq_mode: %d",
			cci_client->cci_i2c_master, i2c_info->slave_addr,
			i2c_info->i2c_freq_mode);
	}

	s_ctrl->sensordata->slave_info.sensor_slave_addr =
		i2c_info->slave_addr;
	return rc;
}

int32_t cam_sensor_update_slave_info(struct cam_cmd_probe *probe_info,
	struct cam_sensor_ctrl_t *s_ctrl)
{
	int32_t rc = 0;

	s_ctrl->sensordata->slave_info.sensor_id_reg_addr =
		probe_info->reg_addr;
	s_ctrl->sensordata->slave_info.sensor_id =
		probe_info->expected_data;
	s_ctrl->sensordata->slave_info.sensor_id_mask =
		probe_info->data_mask;
	/* Userspace passes the pipeline delay in reserved field */
	s_ctrl->pipeline_delay =
		probe_info->reserved;
        s_ctrl->sensordata->slave_info.addr_type =
		probe_info->addr_type;
	s_ctrl->sensordata->slave_info.data_type =
		probe_info->data_type;

	s_ctrl->sensor_probe_addr_type =  probe_info->addr_type;
	s_ctrl->sensor_probe_data_type =  probe_info->data_type;
	CAM_DBG(CAM_SENSOR,
		"Sensor Addr: 0x%x Sensor Addr Type: 0x%x Sensor Data Type: 0x%x sensor_id: 0x%x sensor_mask: 0x%x sensor_pipeline_delay:0x%x",
		s_ctrl->sensordata->slave_info.sensor_id_reg_addr,
		s_ctrl->sensordata->slave_info.addr_type,
		s_ctrl->sensordata->slave_info.data_type,
		s_ctrl->sensordata->slave_info.sensor_id,
		s_ctrl->sensordata->slave_info.sensor_id_mask,
		s_ctrl->pipeline_delay);
	return rc;
}

int32_t cam_sensor_update_id_info(struct cam_cmd_probe *probe_info,
    struct cam_sensor_ctrl_t *s_ctrl)
{
    int32_t rc = 0;

    s_ctrl->sensordata->id_info.sensor_slave_addr =
        probe_info->reserved;
    s_ctrl->sensordata->id_info.sensor_id_reg_addr =
        probe_info->reg_addr;
    s_ctrl->sensordata->id_info.sensor_id_mask =
        probe_info->data_mask;
    s_ctrl->sensordata->id_info.sensor_id =
        probe_info->expected_data;
    s_ctrl->sensordata->id_info.sensor_addr_type =
        probe_info->addr_type;
    s_ctrl->sensordata->id_info.sensor_data_type =
        probe_info->data_type;

    CAM_ERR(CAM_SENSOR,
        "vendor_slave_addr:  0x%x, vendor_id_Addr: 0x%x, vendorID: 0x%x, vendor_mask: 0x%x",
        s_ctrl->sensordata->id_info.sensor_slave_addr,
        s_ctrl->sensordata->id_info.sensor_id_reg_addr,
        s_ctrl->sensordata->id_info.sensor_id,
        s_ctrl->sensordata->id_info.sensor_id_mask);
    return rc;
}

int32_t cam_handle_cmd_buffers_for_probe(void *cmd_buf,
	struct cam_sensor_ctrl_t *s_ctrl,
	int32_t cmd_buf_num, uint32_t cmd_buf_length, size_t remain_len)
{
	int32_t rc = 0;

	switch (cmd_buf_num) {
	case 0: {
		struct cam_cmd_i2c_info *i2c_info = NULL;
		struct cam_cmd_probe *probe_info;

		if (remain_len <
			(sizeof(struct cam_cmd_i2c_info) +
			sizeof(struct cam_cmd_probe))) {
			CAM_ERR(CAM_SENSOR,
				"not enough buffer for cam_cmd_i2c_info");
			return -EINVAL;
		}
		i2c_info = (struct cam_cmd_i2c_info *)cmd_buf;
		rc = cam_sensor_update_i2c_info(i2c_info, s_ctrl);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "Failed in Updating the i2c Info");
			return rc;
		}
		probe_info = (struct cam_cmd_probe *)
			(cmd_buf + sizeof(struct cam_cmd_i2c_info));
		rc = cam_sensor_update_slave_info(probe_info, s_ctrl);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "Updating the slave Info");
			return rc;
		}

		probe_info = (struct cam_cmd_probe *)
			(cmd_buf + sizeof(struct cam_cmd_i2c_info) + sizeof(struct cam_cmd_probe));
		rc = cam_sensor_update_id_info(probe_info, s_ctrl);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "Updating the id Info");
			return rc;
		}
		cmd_buf = probe_info;
	}
		break;
	case 1: {
		rc = cam_sensor_update_power_settings(cmd_buf,
			cmd_buf_length, &s_ctrl->sensordata->power_info,
			remain_len);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR,
				"Failed in updating power settings");
			return rc;
		}
	}
		break;
	default:
		CAM_ERR(CAM_SENSOR, "Invalid command buffer");
		break;
	}
	return rc;
}

int32_t cam_handle_mem_ptr(uint64_t handle, struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc = 0, i;
	uint32_t *cmd_buf;
	void *ptr;
	size_t len;
	struct cam_packet *pkt = NULL;
	struct cam_cmd_buf_desc *cmd_desc = NULL;
	uintptr_t cmd_buf1 = 0;
	uintptr_t packet = 0;
	size_t    remain_len = 0;

	rc = cam_mem_get_cpu_buf(handle,
		&packet, &len);
	if (rc < 0) {
		CAM_ERR(CAM_SENSOR, "Failed to get the command Buffer");
		return -EINVAL;
	}

	pkt = (struct cam_packet *)packet;
	if (pkt == NULL) {
		CAM_ERR(CAM_SENSOR, "packet pos is invalid");
		rc = -EINVAL;
		goto end;
	}

	if ((len < sizeof(struct cam_packet)) ||
		(pkt->cmd_buf_offset >= (len - sizeof(struct cam_packet)))) {
		CAM_ERR(CAM_SENSOR, "Not enough buf provided");
		rc = -EINVAL;
		goto end;
	}

	cmd_desc = (struct cam_cmd_buf_desc *)
		((uint32_t *)&pkt->payload + pkt->cmd_buf_offset/4);
	if (cmd_desc == NULL) {
		CAM_ERR(CAM_SENSOR, "command descriptor pos is invalid");
		rc = -EINVAL;
		goto end;
	}
	if (pkt->num_cmd_buf != 2) {
		CAM_ERR(CAM_SENSOR, "Expected More Command Buffers : %d",
			 pkt->num_cmd_buf);
		rc = -EINVAL;
		goto end;
	}

	for (i = 0; i < pkt->num_cmd_buf; i++) {
		if (!(cmd_desc[i].length))
			continue;
		rc = cam_mem_get_cpu_buf(cmd_desc[i].mem_handle,
			&cmd_buf1, &len);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR,
				"Failed to parse the command Buffer Header");
			goto end;
		}
		if (cmd_desc[i].offset >= len) {
			CAM_ERR(CAM_SENSOR,
				"offset past length of buffer");
			rc = -EINVAL;
			goto end;
		}
		remain_len = len - cmd_desc[i].offset;
		if (cmd_desc[i].length > remain_len) {
			CAM_ERR(CAM_SENSOR,
				"Not enough buffer provided for cmd");
			rc = -EINVAL;
			goto end;
		}
		cmd_buf = (uint32_t *)cmd_buf1;
		cmd_buf += cmd_desc[i].offset/4;
		ptr = (void *) cmd_buf;

		rc = cam_handle_cmd_buffers_for_probe(ptr, s_ctrl,
			i, cmd_desc[i].length, remain_len);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR,
				"Failed to parse the command Buffer Header");
			goto end;
		}
	}

end:
	return rc;
}

void cam_sensor_query_cap(struct cam_sensor_ctrl_t *s_ctrl,
	struct  cam_sensor_query_cap *query_cap)
{
	query_cap->pos_roll = s_ctrl->sensordata->pos_roll;
	query_cap->pos_pitch = s_ctrl->sensordata->pos_pitch;
	query_cap->pos_yaw = s_ctrl->sensordata->pos_yaw;
	query_cap->secure_camera = 0;
	query_cap->actuator_slot_id =
		s_ctrl->sensordata->subdev_id[SUB_MODULE_ACTUATOR];
	query_cap->csiphy_slot_id =
		s_ctrl->sensordata->subdev_id[SUB_MODULE_CSIPHY];
	query_cap->eeprom_slot_id =
		s_ctrl->sensordata->subdev_id[SUB_MODULE_EEPROM];
	query_cap->flash_slot_id =
		s_ctrl->sensordata->subdev_id[SUB_MODULE_LED_FLASH];
	query_cap->ois_slot_id =
		s_ctrl->sensordata->subdev_id[SUB_MODULE_OIS];
	query_cap->slot_info =
		s_ctrl->soc_info.index;
}

static uint16_t cam_sensor_id_by_mask(struct cam_sensor_ctrl_t *s_ctrl,
	uint32_t chipid)
{
	uint16_t sensor_id = (uint16_t)(chipid & 0xFFFF);
	int16_t sensor_id_mask = s_ctrl->sensordata->slave_info.sensor_id_mask;

	if (!sensor_id_mask)
		sensor_id_mask = ~sensor_id_mask;

	sensor_id &= sensor_id_mask;
	sensor_id_mask &= -sensor_id_mask;
	sensor_id_mask -= 1;
	while (sensor_id_mask) {
		sensor_id_mask >>= 1;
		sensor_id >>= 1;
	}
	return sensor_id;
}

void cam_sensor_shutdown(struct cam_sensor_ctrl_t *s_ctrl)
{
	struct cam_sensor_power_ctrl_t *power_info =
		&s_ctrl->sensordata->power_info;
	int rc = 0;

	if ((s_ctrl->sensor_state == CAM_SENSOR_INIT) &&
		(s_ctrl->is_probe_succeed == 0))
		return;

	cam_sensor_release_stream_rsc(s_ctrl);
	cam_sensor_release_per_frame_resource(s_ctrl);

	if (s_ctrl->sensor_state != CAM_SENSOR_INIT)
		cam_sensor_power_down(s_ctrl);

	if (s_ctrl->bridge_intf.device_hdl != -1) {
		rc = cam_destroy_device_hdl(s_ctrl->bridge_intf.device_hdl);
		if (rc < 0)
			CAM_ERR(CAM_SENSOR,
				"dhdl already destroyed: rc = %d", rc);
	}

	s_ctrl->bridge_intf.device_hdl = -1;
	s_ctrl->bridge_intf.link_hdl = -1;
	s_ctrl->bridge_intf.session_hdl = -1;
	kfree(power_info->power_setting);
	kfree(power_info->power_down_setting);
	power_info->power_setting = NULL;
	power_info->power_down_setting = NULL;
	power_info->power_setting_size = 0;
	power_info->power_down_setting_size = 0;
	s_ctrl->streamon_count = 0;
	s_ctrl->streamoff_count = 0;
	s_ctrl->is_probe_succeed = 0;
	s_ctrl->last_flush_req = 0;
	s_ctrl->sensor_state = CAM_SENSOR_INIT;
}

int cam_sensor_match_id(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc = 0;
	uint32_t chipid = 0;
        uint32_t vendor_id = 0;
	struct cam_camera_slave_info *slave_info;
	int hb_id=0;
	uint32_t slave_sid = 0;
        uint32_t addr=0,data=0;

	struct cam_sensor_i2c_reg_array i2c_write_setting = {
		.reg_addr = addr,
		.reg_data = data,
		.delay = 0x00,
		.data_mask = 0x00,
	};
	struct cam_sensor_i2c_reg_setting i2c_write = {
		.reg_setting = &i2c_write_setting,
		.size = 1,
		.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD,
		.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
		.delay = 0x00,
	};

	slave_info = &(s_ctrl->sensordata->slave_info);

#ifdef CONFIG_OEM_BOOT_MODE
	hb_id = get_hw_board_version();
#endif
	if (!slave_info) {
		CAM_ERR(CAM_SENSOR, " failed: %pK",
			 slave_info);
		return -EINVAL;
	}

	rc = camera_io_dev_read(
		&(s_ctrl->io_master_info),
		slave_info->sensor_id_reg_addr,
		&chipid,slave_info->addr_type,
		slave_info->data_type);

	CAM_DBG(CAM_SENSOR, "read id: 0x%x expected id 0x%x:",
		chipid, slave_info->sensor_id);

	if (cam_sensor_id_by_mask(s_ctrl, chipid) != slave_info->sensor_id) {
		CAM_WARN(CAM_SENSOR, "read id: 0x%x expected id 0x%x:",
				chipid, slave_info->sensor_id);
		return -ENODEV;
	}

        if(chipid==0x689){
                camera_io_dev_read(
		        &(s_ctrl->io_master_info),
		        s_ctrl->sensordata->id_info.sensor_id_reg_addr,
		        &vendor_id,s_ctrl->sensordata->id_info.sensor_addr_type,
		        CAMERA_SENSOR_I2C_TYPE_BYTE);
                CAM_INFO(CAM_SENSOR, "read vendor_id_addr=0x%x vendor_id: 0x%x expected vendor_id 0x%x: rc=%d",
                        s_ctrl->sensordata->id_info.sensor_id_reg_addr,vendor_id, s_ctrl->sensordata->id_info.sensor_id,rc);
                if((vendor_id>>4)==1)
                    strcpy(match_tbl[6].sensor_name,"imx689_MP");
                if((vendor_id>>4) != s_ctrl->sensordata->id_info.sensor_id )
                        return -1;
        }
        if((chipid==0x2375||chipid==0x5035)&&(s_ctrl->sensordata->id_info.sensor_slave_addr!=0)){
		CAM_INFO(CAM_SENSOR, "id_info.sensor_slave_addr=%d hb_id=%d",s_ctrl->sensordata->id_info.sensor_slave_addr,hb_id);
		if(((s_ctrl->sensordata->id_info.sensor_slave_addr>11)&&(hb_id>11))||
		    ((s_ctrl->sensordata->id_info.sensor_slave_addr==11)&&(hb_id==11)))
			return rc;
		else
			return -1;
        }

        if(chipid == 0x586){
		if(s_ctrl->sensordata->id_info.sensor_id_reg_addr != 0){
			slave_sid = s_ctrl->io_master_info.cci_client->sid;
			s_ctrl->io_master_info.cci_client->sid = (s_ctrl->sensordata->id_info.sensor_slave_addr>>1);
			camera_io_dev_read(
				&(s_ctrl->io_master_info),
				s_ctrl->sensordata->id_info.sensor_id_reg_addr,
				&vendor_id,CAMERA_SENSOR_I2C_TYPE_WORD,
				CAMERA_SENSOR_I2C_TYPE_BYTE);
			s_ctrl->io_master_info.cci_client->sid = slave_sid;
			CAM_INFO(CAM_SENSOR, "read vendor_id_addr=0x%x vendor_id: 0x%x expected vendor_id 0x%x: rc=%d",
				s_ctrl->sensordata->id_info.sensor_id_reg_addr,vendor_id, s_ctrl->sensordata->id_info.sensor_id,rc);
			if((vendor_id>>4) == 1)
				strcpy(match_tbl[0].sensor_name,"imx586_BG");
			if((vendor_id>>4) != s_ctrl->sensordata->id_info.sensor_id)
				return -1;
                }
        }

        if(chipid == 0x481){
		if(s_ctrl->sensordata->id_info.sensor_id_reg_addr != 0){
			i2c_write_setting.reg_addr = 0x0A02;
			i2c_write_setting.reg_data = 0x1B;
			rc=camera_io_dev_write(&(s_ctrl->io_master_info), &i2c_write);
			if(rc<0) {
				CAM_ERR(CAM_SENSOR, "imx481 write otp failed");
				return rc;
			}
			i2c_write_setting.reg_addr = 0x0A00;
			i2c_write_setting.reg_data = 0x01;
			rc=camera_io_dev_write(&(s_ctrl->io_master_info), &i2c_write);
			if(rc<0) {
				CAM_ERR(CAM_SENSOR, "imx481 write otp failed");
				return rc;
			}
			i2c_write_setting.reg_addr = 0x0A01;
			i2c_write_setting.reg_data = 0x01;
			i2c_write_setting.delay = 100;
			rc = camera_io_dev_poll(
				&(s_ctrl->io_master_info),
				i2c_write_setting.reg_addr,
				i2c_write_setting.reg_data,
				i2c_write_setting.data_mask,
				CAMERA_SENSOR_I2C_TYPE_WORD,
				CAMERA_SENSOR_I2C_TYPE_BYTE,
				i2c_write_setting.delay);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,"poll otp status failed ,non fatal");
			}
			rc=camera_io_dev_read(
				&(s_ctrl->io_master_info),
				s_ctrl->sensordata->id_info.sensor_id_reg_addr,
				&vendor_id,CAMERA_SENSOR_I2C_TYPE_WORD,
				CAMERA_SENSOR_I2C_TYPE_BYTE);
			CAM_INFO(CAM_SENSOR, "read vendor_id_addr=0x%x vendor_id: 0x%x expected vendor_id 0x%x: rc=%d",
				s_ctrl->sensordata->id_info.sensor_id_reg_addr,vendor_id, s_ctrl->sensordata->id_info.sensor_id,rc);
			if(vendor_id == 0x2)
				strcpy(match_tbl[0].sensor_name,"imx481_SFK");
			if(vendor_id != s_ctrl->sensordata->id_info.sensor_id){
                                if(vendor_id == 0){
                                        CAM_ERR(CAM_SENSOR,"read imx481 module vendor failed");
                                        return 0;
                                }
				return -1;
                        }
		}
        }
        /******************** GC5035_OTP_EDIT_END*******************/

        if (slave_info->sensor_id == 0x5035 && s_ctrl->sensordata->id_info.sensor_id == 1) {
                s_ctrl->io_master_info.cci_client->i2c_freq_mode = I2C_FAST_MODE;
                sensor_gc5035_get_dpc_data(s_ctrl);
                s_ctrl->io_master_info.cci_client->i2c_freq_mode = I2C_FAST_PLUS_MODE;
        }
        /******************** GC5035_OTP_EDIT_END*******************/

	return rc;
}

int32_t cam_sensor_driver_cmd(struct cam_sensor_ctrl_t *s_ctrl,
	void *arg)
{
	int rc = 0;
	struct cam_control *cmd = (struct cam_control *)arg;
	struct cam_sensor_power_ctrl_t *power_info =
		&s_ctrl->sensordata->power_info;
#ifdef CONFIG_PROJECT_INFO
	enum COMPONENT_TYPE CameraID;
#endif
        uint32_t count = 0, i;
	if (!s_ctrl || !arg) {
		CAM_ERR(CAM_SENSOR, "s_ctrl is NULL");
		return -EINVAL;
	}

	if (cmd->op_code != CAM_SENSOR_PROBE_CMD) {
		if (cmd->handle_type != CAM_HANDLE_USER_POINTER) {
			CAM_ERR(CAM_SENSOR, "Invalid handle type: %d",
				cmd->handle_type);
			return -EINVAL;
		}
	}

	mutex_lock(&(s_ctrl->cam_sensor_mutex));
	switch (cmd->op_code) {
	case CAM_SENSOR_PROBE_CMD: {
		if (s_ctrl->is_probe_succeed == 1) {
			CAM_ERR(CAM_SENSOR,
				"Already Sensor Probed in the slot");
			break;
		}

		if (cmd->handle_type ==
			CAM_HANDLE_MEM_HANDLE) {
			rc = cam_handle_mem_ptr(cmd->handle, s_ctrl);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR, "Get Buffer Handle Failed");
				goto release_mutex;
			}
		} else {
			CAM_ERR(CAM_SENSOR, "Invalid Command Type: %d",
				 cmd->handle_type);
			rc = -EINVAL;
			goto release_mutex;
		}

		/* Parse and fill vreg params for powerup settings */
		rc = msm_camera_fill_vreg_params(
			&s_ctrl->soc_info,
			s_ctrl->sensordata->power_info.power_setting,
			s_ctrl->sensordata->power_info.power_setting_size);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR,
				"Fail in filling vreg params for PUP rc %d",
				 rc);
			goto free_power_settings;
		}

		/* Parse and fill vreg params for powerdown settings*/
		rc = msm_camera_fill_vreg_params(
			&s_ctrl->soc_info,
			s_ctrl->sensordata->power_info.power_down_setting,
			s_ctrl->sensordata->power_info.power_down_setting_size);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR,
				"Fail in filling vreg params for PDOWN rc %d",
				 rc);
			goto free_power_settings;
		}

		/* Power up and probe sensor */
		rc = cam_sensor_power_up(s_ctrl);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "power up failed");
			goto free_power_settings;
		}

		/* Match sensor ID */
		rc = cam_sensor_match_id(s_ctrl);
		if (rc < 0) {
			cam_sensor_power_down(s_ctrl);
			msleep(20);
			goto free_power_settings;
		}

		CAM_INFO(CAM_SENSOR,
			"Probe success,slot:%d,slave_addr:0x%x,sensor_id:0x%x",
			s_ctrl->soc_info.index,
			s_ctrl->sensordata->slave_info.sensor_slave_addr,
			s_ctrl->sensordata->slave_info.sensor_id);

		rc = cam_sensor_power_down(s_ctrl);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "fail in Sensor Power Down");
			goto free_power_settings;
		}
		/*
		 * Set probe succeeded flag to 1 so that no other camera shall
		 * probed on this slot
		 */
		s_ctrl->is_probe_succeed = 1;
		s_ctrl->sensor_state = CAM_SENSOR_INIT;

#ifdef CONFIG_PROJECT_INFO
		if (s_ctrl->id == 0)
			CameraID = R_CAMERA;
		else if (s_ctrl->id == 1)
			CameraID = SECOND_R_CAMERA;
		else if (s_ctrl->id == 2)
			CameraID = F_CAMERA;
		else if (s_ctrl->id == 3)
			CameraID = THIRD_R_CAMERA;
		else if (s_ctrl->id == 4)
			CameraID = FORTH_R_CAMERA;
		else if (s_ctrl->id == 5)
			CameraID = SECOND_F_CAMERA;
		else
			CameraID = -1;
#endif
		count = ARRAY_SIZE(match_tbl);
		for (i = 0; i < count; i++) {
			if (s_ctrl->sensordata->slave_info.sensor_id
				== match_tbl[i].sensor_id)
				break;
		}
		if (i >= count)
			CAM_ERR(CAM_SENSOR, "current sensor name is 0x%x",
				s_ctrl->sensordata->slave_info.sensor_id);
#ifdef CONFIG_PROJECT_INFO
		else
			push_component_info(CameraID, match_tbl[i].sensor_name,match_tbl[i].vendor_name);
#endif
	}
		break;
	case CAM_ACQUIRE_DEV: {
		struct cam_sensor_acquire_dev sensor_acq_dev;
		struct cam_create_dev_hdl bridge_params;

		if ((s_ctrl->is_probe_succeed == 0) ||
			(s_ctrl->sensor_state != CAM_SENSOR_INIT)) {
			CAM_WARN(CAM_SENSOR,
				"Not in right state to aquire %d",
				s_ctrl->sensor_state);
			rc = -EINVAL;
			goto release_mutex;
		}

		if (s_ctrl->bridge_intf.device_hdl != -1) {
			CAM_ERR(CAM_SENSOR, "Device is already acquired");
			rc = -EINVAL;
			goto release_mutex;
		}
		rc = copy_from_user(&sensor_acq_dev,
			u64_to_user_ptr(cmd->handle),
			sizeof(sensor_acq_dev));
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "Failed Copying from user");
			goto release_mutex;
		}

		bridge_params.session_hdl = sensor_acq_dev.session_handle;
		bridge_params.ops = &s_ctrl->bridge_intf.ops;
		bridge_params.v4l2_sub_dev_flag = 0;
		bridge_params.media_entity_flag = 0;
		bridge_params.priv = s_ctrl;

		sensor_acq_dev.device_handle =
			cam_create_device_hdl(&bridge_params);
		s_ctrl->bridge_intf.device_hdl = sensor_acq_dev.device_handle;
		s_ctrl->bridge_intf.session_hdl = sensor_acq_dev.session_handle;

		CAM_DBG(CAM_SENSOR, "Device Handle: %d",
			sensor_acq_dev.device_handle);
		if (copy_to_user(u64_to_user_ptr(cmd->handle),
			&sensor_acq_dev,
			sizeof(struct cam_sensor_acquire_dev))) {
			CAM_ERR(CAM_SENSOR, "Failed Copy to User");
			rc = -EFAULT;
			goto release_mutex;
		}

		rc = cam_sensor_power_up(s_ctrl);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "Sensor Power up failed");
			goto release_mutex;
		}

		s_ctrl->sensor_state = CAM_SENSOR_ACQUIRE;
		s_ctrl->last_flush_req = 0;
		CAM_INFO(CAM_SENSOR,
			"CAM_ACQUIRE_DEV Success, sensor_id:0x%x,sensor_slave_addr:0x%x",
			s_ctrl->sensordata->slave_info.sensor_id,
			s_ctrl->sensordata->slave_info.sensor_slave_addr);
	}
		break;
	case CAM_RELEASE_DEV: {

		/*STOP DEV when sensor is START DEV and RELEASE called*/
		if (s_ctrl->sensor_state == CAM_SENSOR_START)
		{
			CAM_WARN(CAM_SENSOR,
			"Unbalance Release called with out STOP: %d",
						s_ctrl->sensor_state);
			if (s_ctrl->i2c_data.streamoff_settings.is_settings_valid &&
				(s_ctrl->i2c_data.streamoff_settings.request_id == 0)) {
				rc = cam_sensor_apply_settings(s_ctrl, 0,
					CAM_SENSOR_PACKET_OPCODE_SENSOR_STREAMOFF);
				if (rc < 0) {
					/*Even Stream off failure do force power down*/
					CAM_ERR(CAM_SENSOR,
					"cannot apply streamoff settings");
				}
			}
			s_ctrl->sensor_state = CAM_SENSOR_ACQUIRE;
		}

		if ((s_ctrl->sensor_state == CAM_SENSOR_INIT) ||
			(s_ctrl->sensor_state == CAM_SENSOR_START)) {
			rc = -EINVAL;
			CAM_WARN(CAM_SENSOR,
			"Not in right state to release : %d",
			s_ctrl->sensor_state);
			goto release_mutex;
		}

		if (s_ctrl->bridge_intf.link_hdl != -1) {
			CAM_ERR(CAM_SENSOR,
				"Device [%d] still active on link 0x%x",
				s_ctrl->sensor_state,
				s_ctrl->bridge_intf.link_hdl);
			rc = -EAGAIN;
			goto release_mutex;
		}

		rc = cam_sensor_power_down(s_ctrl);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "Sensor Power Down failed");
			goto release_mutex;
		}

		cam_sensor_release_per_frame_resource(s_ctrl);
		cam_sensor_release_stream_rsc(s_ctrl);
		if (s_ctrl->bridge_intf.device_hdl == -1) {
			CAM_ERR(CAM_SENSOR,
				"Invalid Handles: link hdl: %d device hdl: %d",
				s_ctrl->bridge_intf.device_hdl,
				s_ctrl->bridge_intf.link_hdl);
			rc = -EINVAL;
			goto release_mutex;
		}
		rc = cam_destroy_device_hdl(s_ctrl->bridge_intf.device_hdl);
		if (rc < 0)
			CAM_ERR(CAM_SENSOR,
				"failed in destroying the device hdl");
		s_ctrl->bridge_intf.device_hdl = -1;
		s_ctrl->bridge_intf.link_hdl = -1;
		s_ctrl->bridge_intf.session_hdl = -1;

		s_ctrl->sensor_state = CAM_SENSOR_INIT;
		CAM_INFO(CAM_SENSOR,
			"CAM_RELEASE_DEV Success, sensor_id:0x%x,sensor_slave_addr:0x%x",
			s_ctrl->sensordata->slave_info.sensor_id,
			s_ctrl->sensordata->slave_info.sensor_slave_addr);
		s_ctrl->streamon_count = 0;
		s_ctrl->streamoff_count = 0;
		s_ctrl->last_flush_req = 0;
	}
		break;
	case CAM_QUERY_CAP: {
		struct  cam_sensor_query_cap sensor_cap;

		cam_sensor_query_cap(s_ctrl, &sensor_cap);
		if (copy_to_user(u64_to_user_ptr(cmd->handle),
			&sensor_cap, sizeof(struct  cam_sensor_query_cap))) {
			CAM_ERR(CAM_SENSOR, "Failed Copy to User");
			rc = -EFAULT;
			goto release_mutex;
		}
		break;
	}
	case CAM_START_DEV: {
		if ((s_ctrl->sensor_state == CAM_SENSOR_INIT) ||
			(s_ctrl->sensor_state == CAM_SENSOR_START)) {
			rc = -EINVAL;
			CAM_WARN(CAM_SENSOR,
			"Not in right state to start : %d",
			s_ctrl->sensor_state);
			goto release_mutex;
		}

		if (s_ctrl->i2c_data.streamon_settings.is_settings_valid &&
			(s_ctrl->i2c_data.streamon_settings.request_id == 0)) {
			rc = cam_sensor_apply_settings(s_ctrl, 0,
				CAM_SENSOR_PACKET_OPCODE_SENSOR_STREAMON);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,
					"cannot apply streamon settings");
				goto release_mutex;
			}
		}
		s_ctrl->sensor_state = CAM_SENSOR_START;
		CAM_INFO(CAM_SENSOR,
			"CAM_START_DEV Success, sensor_id:0x%x,sensor_slave_addr:0x%x",
			s_ctrl->sensordata->slave_info.sensor_id,
			s_ctrl->sensordata->slave_info.sensor_slave_addr);
	}
		break;
	case CAM_STOP_DEV: {
		if (s_ctrl->sensor_state != CAM_SENSOR_START) {
			rc = -EINVAL;
			CAM_WARN(CAM_SENSOR,
			"Not in right state to stop : %d",
			s_ctrl->sensor_state);
			goto release_mutex;
		}

		if (s_ctrl->i2c_data.streamoff_settings.is_settings_valid &&
			(s_ctrl->i2c_data.streamoff_settings.request_id == 0)) {
			rc = cam_sensor_apply_settings(s_ctrl, 0,
				CAM_SENSOR_PACKET_OPCODE_SENSOR_STREAMOFF);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,
				"cannot apply streamoff settings");
			}
		}

		cam_sensor_release_per_frame_resource(s_ctrl);
		s_ctrl->last_flush_req = 0;
		s_ctrl->sensor_state = CAM_SENSOR_ACQUIRE;
		CAM_INFO(CAM_SENSOR,
			"CAM_STOP_DEV Success, sensor_id:0x%x,sensor_slave_addr:0x%x",
			s_ctrl->sensordata->slave_info.sensor_id,
			s_ctrl->sensordata->slave_info.sensor_slave_addr);
	}
		break;
	case CAM_CONFIG_DEV: {
		rc = cam_sensor_i2c_pkt_parse(s_ctrl, arg);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "Failed i2c pkt parse: %d", rc);
			goto release_mutex;
		}
		if (s_ctrl->i2c_data.init_settings.is_settings_valid &&
			(s_ctrl->i2c_data.init_settings.request_id == 0)) {

			rc = cam_sensor_apply_settings(s_ctrl, 0,
				CAM_SENSOR_PACKET_OPCODE_SENSOR_INITIAL_CONFIG);

			if ((rc == -EAGAIN) &&
			(s_ctrl->io_master_info.master_type == CCI_MASTER)) {
				/* If CCI hardware is resetting we need to wait for sometime
				 * before reapply
				 */
				CAM_WARN(CAM_SENSOR,
					"Reapplying the Init settings due to cci hw reset");
				usleep_range(5000, 5010);
				rc = cam_sensor_apply_settings(s_ctrl, 0,
					CAM_SENSOR_PACKET_OPCODE_SENSOR_INITIAL_CONFIG);
			}
			s_ctrl->i2c_data.init_settings.request_id = -1;

			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,
					"cannot apply init settings rc= %d",
					rc);
				delete_request(&s_ctrl->i2c_data.init_settings);
				goto release_mutex;
			}
			rc = delete_request(&s_ctrl->i2c_data.init_settings);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,
					"Fail in deleting the Init settings");
				goto release_mutex;
			}
		}

		if (s_ctrl->i2c_data.config_settings.is_settings_valid &&
			(s_ctrl->i2c_data.config_settings.request_id == 0)) {
			rc = cam_sensor_apply_settings(s_ctrl, 0,
				CAM_SENSOR_PACKET_OPCODE_SENSOR_CONFIG);

			s_ctrl->i2c_data.config_settings.request_id = -1;

			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,
					"cannot apply config settings");
				delete_request(
					&s_ctrl->i2c_data.config_settings);
				goto release_mutex;
			}
			rc = delete_request(&s_ctrl->i2c_data.config_settings);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,
					"Fail in deleting the config settings");
				goto release_mutex;
			}
			s_ctrl->sensor_state = CAM_SENSOR_CONFIG;
		}
	}
		break;
	default:
		CAM_ERR(CAM_SENSOR, "Invalid Opcode: %d", cmd->op_code);
		rc = -EINVAL;
		goto release_mutex;
	}

release_mutex:
	mutex_unlock(&(s_ctrl->cam_sensor_mutex));
	return rc;

free_power_settings:
	kfree(power_info->power_setting);
	kfree(power_info->power_down_setting);
	power_info->power_setting = NULL;
	power_info->power_down_setting = NULL;
	power_info->power_down_setting_size = 0;
	power_info->power_setting_size = 0;
	mutex_unlock(&(s_ctrl->cam_sensor_mutex));
	return rc;
}

int cam_sensor_publish_dev_info(struct cam_req_mgr_device_info *info)
{
	int rc = 0;
	struct cam_sensor_ctrl_t *s_ctrl = NULL;

	if (!info)
		return -EINVAL;

	s_ctrl = (struct cam_sensor_ctrl_t *)
		cam_get_device_priv(info->dev_hdl);

	if (!s_ctrl) {
		CAM_ERR(CAM_SENSOR, "Device data is NULL");
		return -EINVAL;
	}

	info->dev_id = CAM_REQ_MGR_DEVICE_SENSOR;
	strlcpy(info->name, CAM_SENSOR_NAME, sizeof(info->name));
	if (s_ctrl->pipeline_delay >= 1 && s_ctrl->pipeline_delay <= 3)
		info->p_delay = s_ctrl->pipeline_delay;
	else
		info->p_delay = 2;
	info->trigger = CAM_TRIGGER_POINT_SOF;

	return rc;
}

int cam_sensor_establish_link(struct cam_req_mgr_core_dev_link_setup *link)
{
	struct cam_sensor_ctrl_t *s_ctrl = NULL;

	if (!link)
		return -EINVAL;

	s_ctrl = (struct cam_sensor_ctrl_t *)
		cam_get_device_priv(link->dev_hdl);
	if (!s_ctrl) {
		CAM_ERR(CAM_SENSOR, "Device data is NULL");
		return -EINVAL;
	}

	mutex_lock(&s_ctrl->cam_sensor_mutex);
	if (link->link_enable) {
		s_ctrl->bridge_intf.link_hdl = link->link_hdl;
		s_ctrl->bridge_intf.crm_cb = link->crm_cb;
	} else {
		s_ctrl->bridge_intf.link_hdl = -1;
		s_ctrl->bridge_intf.crm_cb = NULL;
	}
	mutex_unlock(&s_ctrl->cam_sensor_mutex);

	return 0;
}

int cam_sensor_power(struct v4l2_subdev *sd, int on)
{
	struct cam_sensor_ctrl_t *s_ctrl = v4l2_get_subdevdata(sd);

	mutex_lock(&(s_ctrl->cam_sensor_mutex));
	if (!on && s_ctrl->sensor_state == CAM_SENSOR_START) {
		cam_sensor_power_down(s_ctrl);
		s_ctrl->sensor_state = CAM_SENSOR_ACQUIRE;
	}
	mutex_unlock(&(s_ctrl->cam_sensor_mutex));

	return 0;
}

int cam_sensor_power_up(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc;
	struct cam_sensor_power_ctrl_t *power_info;
	struct cam_camera_slave_info *slave_info;
	struct cam_hw_soc_info *soc_info =
		&s_ctrl->soc_info;

	if (!s_ctrl) {
		CAM_ERR(CAM_SENSOR, "failed: %pK", s_ctrl);
		return -EINVAL;
	}

	power_info = &s_ctrl->sensordata->power_info;
	slave_info = &(s_ctrl->sensordata->slave_info);

	if (!power_info || !slave_info) {
		CAM_ERR(CAM_SENSOR, "failed: %pK %pK", power_info, slave_info);
		return -EINVAL;
	}

	if (s_ctrl->bob_pwm_switch) {
		rc = cam_sensor_bob_pwm_mode_switch(soc_info,
			s_ctrl->bob_reg_index, true);
		if (rc) {
			CAM_WARN(CAM_SENSOR,
			"BoB PWM setup failed rc: %d", rc);
			rc = 0;
		}
	}

	rc = cam_sensor_core_power_up(power_info, soc_info);
	if (rc < 0) {
		CAM_ERR(CAM_SENSOR, "power up the core is failed:%d", rc);
		return rc;
	}

	rc = camera_io_init(&(s_ctrl->io_master_info));
	if (rc < 0)
		CAM_ERR(CAM_SENSOR, "cci_init failed: rc: %d", rc);

	return rc;
}

int cam_sensor_power_down(struct cam_sensor_ctrl_t *s_ctrl)
{
	struct cam_sensor_power_ctrl_t *power_info;
	struct cam_hw_soc_info *soc_info;
	int rc = 0;

	if (!s_ctrl) {
		CAM_ERR(CAM_SENSOR, "failed: s_ctrl %pK", s_ctrl);
		return -EINVAL;
	}

	power_info = &s_ctrl->sensordata->power_info;
	soc_info = &s_ctrl->soc_info;

	if (!power_info) {
		CAM_ERR(CAM_SENSOR, "failed: power_info %pK", power_info);
		return -EINVAL;
	}
	rc = cam_sensor_util_power_down(power_info, soc_info);
	if (rc < 0) {
		CAM_ERR(CAM_SENSOR, "power down the core is failed:%d", rc);
		return rc;
	}

	if (s_ctrl->bob_pwm_switch) {
		rc = cam_sensor_bob_pwm_mode_switch(soc_info,
			s_ctrl->bob_reg_index, false);
		if (rc) {
			CAM_WARN(CAM_SENSOR,
				"BoB PWM setup failed rc: %d", rc);
			rc = 0;
		}
	}

	camera_io_release(&(s_ctrl->io_master_info));

	return rc;
}

int cam_sensor_apply_settings(struct cam_sensor_ctrl_t *s_ctrl,
	int64_t req_id, enum cam_sensor_packet_opcodes opcode)
{
	int rc = 0, offset, i;
	uint64_t top = 0, del_req_id = 0;
	struct i2c_settings_array *i2c_set = NULL;
	struct i2c_settings_list *i2c_list;

	if (req_id == 0) {
		switch (opcode) {
		case CAM_SENSOR_PACKET_OPCODE_SENSOR_STREAMON: {
			i2c_set = &s_ctrl->i2c_data.streamon_settings;
			break;
		}
		case CAM_SENSOR_PACKET_OPCODE_SENSOR_INITIAL_CONFIG: {
			i2c_set = &s_ctrl->i2c_data.init_settings;
			break;
		}
		case CAM_SENSOR_PACKET_OPCODE_SENSOR_CONFIG: {
			i2c_set = &s_ctrl->i2c_data.config_settings;
			break;
		}
		case CAM_SENSOR_PACKET_OPCODE_SENSOR_STREAMOFF: {
			i2c_set = &s_ctrl->i2c_data.streamoff_settings;
			break;
		}
		case CAM_SENSOR_PACKET_OPCODE_SENSOR_UPDATE:
		case CAM_SENSOR_PACKET_OPCODE_SENSOR_PROBE:
		default:
			return 0;
		}
		if (i2c_set->is_settings_valid == 1) {
			list_for_each_entry(i2c_list,
				&(i2c_set->list_head), list) {
				rc = cam_sensor_i2c_modes_util(
					&(s_ctrl->io_master_info),
					i2c_list);
				if (rc < 0) {
					CAM_ERR(CAM_SENSOR,
						"Failed to apply settings: %d",
						rc);
					return rc;
				}
                                /******************** GC5035_OTP_EDIT_END*******************/
				if (s_ctrl->sensordata->slave_info.sensor_id == 0x5035
				    && opcode ==  CAM_SENSOR_PACKET_OPCODE_SENSOR_INITIAL_CONFIG
				    && s_ctrl->sensordata->id_info.sensor_id == 1) {
				    sensor_gc5035_write_dpc_data(s_ctrl);
				    sensor_gc5035_update_reg(s_ctrl);
				}
                                /******************** GC5035_OTP_EDIT_END*******************/

			}
		}
	} else {
		offset = req_id % MAX_PER_FRAME_ARRAY;
		i2c_set = &(s_ctrl->i2c_data.per_frame[offset]);
		if (i2c_set->is_settings_valid == 1 &&
			i2c_set->request_id == req_id) {
			list_for_each_entry(i2c_list,
				&(i2c_set->list_head), list) {
				rc = cam_sensor_i2c_modes_util(
					&(s_ctrl->io_master_info),
					i2c_list);
				if (rc < 0) {
					CAM_ERR(CAM_SENSOR,
						"Failed to apply settings: %d",
						rc);
					return rc;
				}
			}
		} else {
			CAM_DBG(CAM_SENSOR,
				"Invalid/NOP request to apply: %lld", req_id);
		}

		/* Change the logic dynamically */
		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
			if ((req_id >=
				s_ctrl->i2c_data.per_frame[i].request_id) &&
				(top <
				s_ctrl->i2c_data.per_frame[i].request_id) &&
				(s_ctrl->i2c_data.per_frame[i].is_settings_valid
					== 1)) {
				del_req_id = top;
				top = s_ctrl->i2c_data.per_frame[i].request_id;
			}
		}

		if (top < req_id) {
			if ((((top % MAX_PER_FRAME_ARRAY) - (req_id %
				MAX_PER_FRAME_ARRAY)) >= BATCH_SIZE_MAX) ||
				(((top % MAX_PER_FRAME_ARRAY) - (req_id %
				MAX_PER_FRAME_ARRAY)) <= -BATCH_SIZE_MAX))
				del_req_id = req_id;
		}

		if (!del_req_id)
			return rc;

		CAM_DBG(CAM_SENSOR, "top: %llu, del_req_id:%llu",
			top, del_req_id);

		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
			if ((del_req_id >
				 s_ctrl->i2c_data.per_frame[i].request_id) && (
				 s_ctrl->i2c_data.per_frame[i].is_settings_valid
					== 1)) {
				s_ctrl->i2c_data.per_frame[i].request_id = 0;
				rc = delete_request(
					&(s_ctrl->i2c_data.per_frame[i]));
				if (rc < 0)
					CAM_ERR(CAM_SENSOR,
						"Delete request Fail:%lld rc:%d",
						del_req_id, rc);
			}
		}
	}

	return rc;
}

int32_t cam_sensor_apply_request(struct cam_req_mgr_apply_request *apply)
{
	int32_t rc = 0;
	struct cam_sensor_ctrl_t *s_ctrl = NULL;

	if (!apply)
		return -EINVAL;

	s_ctrl = (struct cam_sensor_ctrl_t *)
		cam_get_device_priv(apply->dev_hdl);
	if (!s_ctrl) {
		CAM_ERR(CAM_SENSOR, "Device data is NULL");
		return -EINVAL;
	}
	CAM_DBG(CAM_REQ, " Sensor update req id: %lld", apply->request_id);
	trace_cam_apply_req("Sensor", apply->request_id);
	mutex_lock(&(s_ctrl->cam_sensor_mutex));
	rc = cam_sensor_apply_settings(s_ctrl, apply->request_id,
		CAM_SENSOR_PACKET_OPCODE_SENSOR_UPDATE);
	mutex_unlock(&(s_ctrl->cam_sensor_mutex));
	return rc;
}

int32_t cam_sensor_flush_request(struct cam_req_mgr_flush_request *flush_req)
{
	int32_t rc = 0, i;
	uint32_t cancel_req_id_found = 0;
	struct cam_sensor_ctrl_t *s_ctrl = NULL;
	struct i2c_settings_array *i2c_set = NULL;

	if (!flush_req)
		return -EINVAL;

	s_ctrl = (struct cam_sensor_ctrl_t *)
		cam_get_device_priv(flush_req->dev_hdl);
	if (!s_ctrl) {
		CAM_ERR(CAM_SENSOR, "Device data is NULL");
		return -EINVAL;
	}

	mutex_lock(&(s_ctrl->cam_sensor_mutex));
	if (s_ctrl->sensor_state != CAM_SENSOR_START ||
		s_ctrl->sensor_state != CAM_SENSOR_CONFIG) {
		mutex_unlock(&(s_ctrl->cam_sensor_mutex));
		return rc;
	}

	if (s_ctrl->i2c_data.per_frame == NULL) {
		CAM_ERR(CAM_SENSOR, "i2c frame data is NULL");
		mutex_unlock(&(s_ctrl->cam_sensor_mutex));
		return -EINVAL;
	}

	if (flush_req->type == CAM_REQ_MGR_FLUSH_TYPE_ALL) {
		s_ctrl->last_flush_req = flush_req->req_id;
		CAM_DBG(CAM_SENSOR, "last reqest to flush is %lld",
			flush_req->req_id);
	}

	for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
		i2c_set = &(s_ctrl->i2c_data.per_frame[i]);

		if ((flush_req->type == CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ)
				&& (i2c_set->request_id != flush_req->req_id))
			continue;

		if (i2c_set->is_settings_valid == 1) {
			rc = delete_request(i2c_set);
			if (rc < 0)
				CAM_ERR(CAM_SENSOR,
					"delete request: %lld rc: %d",
					i2c_set->request_id, rc);

			if (flush_req->type ==
				CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ) {
				cancel_req_id_found = 1;
				break;
			}
		}
	}

	if (flush_req->type == CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ &&
		!cancel_req_id_found)
		CAM_DBG(CAM_SENSOR,
			"Flush request id:%lld not found in the pending list",
			flush_req->req_id);
	mutex_unlock(&(s_ctrl->cam_sensor_mutex));
	return rc;
}
