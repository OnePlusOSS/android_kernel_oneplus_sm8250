// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 */

#include "cam_ois_dev.h"
#include "cam_req_mgr_dev.h"
#include "cam_ois_soc.h"
#include "cam_ois_core.h"
#include "cam_debug_util.h"

#include "onsemi_fw/fw_download_interface.h"
static int cam_ois_slaveInfo_pkt_parser_oem(struct cam_ois_ctrl_t *o_ctrl)
{
        o_ctrl->io_master_info.cci_client->i2c_freq_mode = I2C_FAST_PLUS_MODE;
        o_ctrl->io_master_info.cci_client->sid = (0x7c >> 1);
        o_ctrl->io_master_info.cci_client->retries = 3;
        o_ctrl->io_master_info.cci_client->id_map = 0;

        return 0;
}

int ois_download_fw_thread(void *arg)
{
        struct cam_ois_ctrl_t *o_ctrl = (struct cam_ois_ctrl_t *)arg;
        int rc = -1;
        mutex_lock(&(o_ctrl->ois_mutex));
        cam_ois_slaveInfo_pkt_parser_oem(o_ctrl);
        if (o_ctrl->cam_ois_state != CAM_OIS_CONFIG) {
                mutex_lock(&(o_ctrl->ois_power_down_mutex));
                o_ctrl->ois_power_down_thread_exit = true;
                if (o_ctrl->ois_power_state == CAM_OIS_POWER_OFF){
                        rc = cam_ois_power_up(o_ctrl);
                        if(rc != 0) {
                                CAM_ERR(CAM_OIS, "ois power up failed");
                                mutex_unlock(&(o_ctrl->ois_power_down_mutex));
                                mutex_unlock(&(o_ctrl->ois_mutex));
                                return rc;
                        }
                } else {
                        CAM_ERR(CAM_OIS, "ois type=%d,OIS already power on, no need to power on again",o_ctrl->ois_type);
                }
                CAM_ERR(CAM_OIS, "ois power up successful");
                o_ctrl->ois_power_state = CAM_OIS_POWER_ON;
                mutex_unlock(&(o_ctrl->ois_power_down_mutex));
        }
        o_ctrl->cam_ois_state = CAM_OIS_CONFIG;
        mutex_unlock(&(o_ctrl->ois_mutex));
        mutex_lock(&(o_ctrl->do_ioctl_ois));
        if(o_ctrl->ois_download_fw_done == CAM_OIS_FW_NOT_DOWNLOAD){
                rc = DownloadFW(o_ctrl);
                if(rc != 0) {
                        CAM_ERR(CAM_OIS, "ois download fw failed");
                        o_ctrl->ois_download_fw_done = CAM_OIS_FW_NOT_DOWNLOAD;
                        mutex_unlock(&(o_ctrl->do_ioctl_ois));
                        return rc;
                } else {
                        o_ctrl->ois_download_fw_done = CAM_OIS_FW_DOWNLOAD_DONE;
                }
        }
        mutex_unlock(&(o_ctrl->do_ioctl_ois));
        return rc;
}

static long cam_ois_subdev_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, void *arg)
{
	int                       rc     = 0;
	struct cam_ois_ctrl_t *o_ctrl = v4l2_get_subdevdata(sd);
	switch (cmd) {
	case VIDIOC_CAM_CONTROL:
		rc = cam_ois_driver_cmd(o_ctrl, arg);
		break;
	case VIDIOC_CAM_FTM_POWNER_UP:
                mutex_lock(&(o_ctrl->ois_power_down_mutex));
                if (o_ctrl->ois_power_state == CAM_OIS_POWER_ON){
                        CAM_INFO(CAM_OIS, "do not need to create ois download fw thread");
                        o_ctrl->ois_power_down_thread_exit = true;
                        mutex_unlock(&(o_ctrl->ois_power_down_mutex));
                        return rc;
                } else {
                        CAM_INFO(CAM_OIS, "create ois download fw thread");
                        o_ctrl->ois_downloadfw_thread = kthread_run(ois_download_fw_thread, o_ctrl, o_ctrl->ois_name);
                        if (!o_ctrl->ois_downloadfw_thread) {
                                CAM_ERR(CAM_OIS, "create ois download fw thread failed");
                                mutex_unlock(&(o_ctrl->ois_power_down_mutex));
                                return -1;
                        }
                }
                mutex_unlock(&(o_ctrl->ois_power_down_mutex));
                mutex_lock(&(o_ctrl->do_ioctl_ois));
                o_ctrl->ois_fd_have_close_state = CAM_OIS_IS_OPEN;
                mutex_unlock(&(o_ctrl->do_ioctl_ois));
                break;
	default:
		rc = -ENOIOCTLCMD;
		break;
	}

	return rc;
}

static int cam_ois_subdev_close(struct v4l2_subdev *sd,
	struct v4l2_subdev_fh *fh)
{
	struct cam_ois_ctrl_t *o_ctrl =
		v4l2_get_subdevdata(sd);

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "o_ctrl ptr is NULL");
			return -EINVAL;
	}

	mutex_lock(&(o_ctrl->ois_mutex));
        if(o_ctrl->cam_ois_download_fw_in_advance){
                //when close ois,should be disable ois
                mutex_lock(&(o_ctrl->ois_power_down_mutex));
                if (o_ctrl->ois_power_state == CAM_OIS_POWER_ON){
                        RamWrite32A_oneplus(o_ctrl,0xf012,0x0);
                }
                mutex_unlock(&(o_ctrl->ois_power_down_mutex));
                mutex_lock(&(o_ctrl->do_ioctl_ois));
                o_ctrl->ois_fd_have_close_state = CAM_OIS_IS_DOING_CLOSE;
                mutex_unlock(&(o_ctrl->do_ioctl_ois));
        }
        cam_ois_shutdown(o_ctrl);
	mutex_unlock(&(o_ctrl->ois_mutex));

	return 0;
}

static int32_t cam_ois_update_i2c_info(struct cam_ois_ctrl_t *o_ctrl,
	struct cam_ois_i2c_info_t *i2c_info)
{
	struct cam_sensor_cci_client        *cci_client = NULL;

	if (o_ctrl->io_master_info.master_type == CCI_MASTER) {
		cci_client = o_ctrl->io_master_info.cci_client;
		if (!cci_client) {
			CAM_ERR(CAM_OIS, "failed: cci_client %pK",
				cci_client);
			return -EINVAL;
		}
		cci_client->cci_i2c_master = o_ctrl->cci_i2c_master;
		cci_client->sid = (i2c_info->slave_addr) >> 1;
		cci_client->retries = 3;
		cci_client->id_map = 0;
		cci_client->i2c_freq_mode = i2c_info->i2c_freq_mode;
	}

	return 0;
}

#ifdef CONFIG_COMPAT
static long cam_ois_init_subdev_do_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, unsigned long arg)
{
	struct cam_control cmd_data;
	int32_t rc = 0;

	if (copy_from_user(&cmd_data, (void __user *)arg,
		sizeof(cmd_data))) {
		CAM_ERR(CAM_OIS,
			"Failed to copy from user_ptr=%pK size=%zu",
			(void __user *)arg, sizeof(cmd_data));
		return -EFAULT;
	}

	switch (cmd) {
	case VIDIOC_CAM_CONTROL:
		rc = cam_ois_subdev_ioctl(sd, cmd, &cmd_data);
		if (rc) {
			CAM_ERR(CAM_OIS,
				"Failed in ois suddev handling rc %d",
				rc);
			return rc;
		}
		break;
	default:
		CAM_ERR(CAM_OIS, "Invalid compat ioctl: %d", cmd);
		rc = -EINVAL;
	}

	if (!rc) {
		if (copy_to_user((void __user *)arg, &cmd_data,
			sizeof(cmd_data))) {
			CAM_ERR(CAM_OIS,
				"Failed to copy from user_ptr=%pK size=%zu",
				(void __user *)arg, sizeof(cmd_data));
			rc = -EFAULT;
		}
	}
	return rc;
}
#endif

static const struct v4l2_subdev_internal_ops cam_ois_internal_ops = {
	.close = cam_ois_subdev_close,
};

static struct v4l2_subdev_core_ops cam_ois_subdev_core_ops = {
	.ioctl = cam_ois_subdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = cam_ois_init_subdev_do_ioctl,
#endif
};

static struct v4l2_subdev_ops cam_ois_subdev_ops = {
	.core = &cam_ois_subdev_core_ops,
};

static int cam_ois_init_subdev_param(struct cam_ois_ctrl_t *o_ctrl)
{
	int rc = 0;

	o_ctrl->v4l2_dev_str.internal_ops = &cam_ois_internal_ops;
	o_ctrl->v4l2_dev_str.ops = &cam_ois_subdev_ops;
	strlcpy(o_ctrl->device_name, CAM_OIS_NAME,
		sizeof(o_ctrl->device_name));
	o_ctrl->v4l2_dev_str.name = o_ctrl->device_name;
	o_ctrl->v4l2_dev_str.sd_flags =
		(V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS);
	o_ctrl->v4l2_dev_str.ent_function = CAM_OIS_DEVICE_TYPE;
	o_ctrl->v4l2_dev_str.token = o_ctrl;

	rc = cam_register_subdev(&(o_ctrl->v4l2_dev_str));
	if (rc)
		CAM_ERR(CAM_OIS, "fail to create subdev");

	return rc;
}

static int cam_ois_i2c_driver_probe(struct i2c_client *client,
	 const struct i2c_device_id *id)
{
	int                          rc = 0;
	struct cam_ois_ctrl_t       *o_ctrl = NULL;
	struct cam_ois_soc_private  *soc_private = NULL;

	if (client == NULL || id == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args client: %pK id: %pK",
			client, id);
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		CAM_ERR(CAM_OIS, "i2c_check_functionality failed");
		goto probe_failure;
	}

	o_ctrl = kzalloc(sizeof(*o_ctrl), GFP_KERNEL);
	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "kzalloc failed");
		rc = -ENOMEM;
		goto probe_failure;
	}

	i2c_set_clientdata(client, o_ctrl);

	o_ctrl->soc_info.dev = &client->dev;
	o_ctrl->soc_info.dev_name = client->name;
	o_ctrl->ois_device_type = MSM_CAMERA_I2C_DEVICE;
	o_ctrl->io_master_info.master_type = I2C_MASTER;
	o_ctrl->io_master_info.client = client;

	soc_private = kzalloc(sizeof(struct cam_ois_soc_private),
		GFP_KERNEL);
	if (!soc_private) {
		rc = -ENOMEM;
		goto octrl_free;
	}

	o_ctrl->soc_info.soc_private = soc_private;
	rc = cam_ois_driver_soc_init(o_ctrl);
	if (rc) {
		CAM_ERR(CAM_OIS, "failed: cam_sensor_parse_dt rc %d", rc);
		goto soc_free;
	}

	rc = cam_ois_init_subdev_param(o_ctrl);
	if (rc)
		goto soc_free;

	o_ctrl->cam_ois_state = CAM_OIS_INIT;

	return rc;

soc_free:
	kfree(soc_private);
octrl_free:
	kfree(o_ctrl);
probe_failure:
	return rc;
}

static int cam_ois_i2c_driver_remove(struct i2c_client *client)
{
	int                             i;
	struct cam_ois_ctrl_t          *o_ctrl = i2c_get_clientdata(client);
	struct cam_hw_soc_info         *soc_info;
	struct cam_ois_soc_private     *soc_private;
	struct cam_sensor_power_ctrl_t *power_info;

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "ois device is NULL");
		return -EINVAL;
	}

	CAM_INFO(CAM_OIS, "i2c driver remove invoked");
	soc_info = &o_ctrl->soc_info;

	for (i = 0; i < soc_info->num_clk; i++)
		devm_clk_put(soc_info->dev, soc_info->clk[i]);

	mutex_lock(&(o_ctrl->ois_mutex));
	cam_ois_shutdown(o_ctrl);
	mutex_unlock(&(o_ctrl->ois_mutex));
	cam_unregister_subdev(&(o_ctrl->v4l2_dev_str));

	soc_private =
		(struct cam_ois_soc_private *)soc_info->soc_private;
	power_info = &soc_private->power_info;

	kfree(o_ctrl->soc_info.soc_private);
	v4l2_set_subdevdata(&o_ctrl->v4l2_dev_str.sd, NULL);
	kfree(o_ctrl);

	return 0;
}

static int32_t cam_ois_platform_driver_probe(
	struct platform_device *pdev)
{
	int32_t                         rc = 0;
	struct cam_ois_ctrl_t          *o_ctrl = NULL;
	struct cam_ois_soc_private     *soc_private = NULL;

	o_ctrl = kzalloc(sizeof(struct cam_ois_ctrl_t), GFP_KERNEL);
	if (!o_ctrl)
		return -ENOMEM;

	o_ctrl->soc_info.pdev = pdev;
	o_ctrl->pdev = pdev;
	o_ctrl->soc_info.dev = &pdev->dev;
	o_ctrl->soc_info.dev_name = pdev->name;

	o_ctrl->ois_device_type = MSM_CAMERA_PLATFORM_DEVICE;

	o_ctrl->io_master_info.master_type = CCI_MASTER;
	o_ctrl->io_master_info.cci_client = kzalloc(
		sizeof(struct cam_sensor_cci_client), GFP_KERNEL);
	if (!o_ctrl->io_master_info.cci_client)
		goto free_o_ctrl;

	soc_private = kzalloc(sizeof(struct cam_ois_soc_private),
		GFP_KERNEL);
	if (!soc_private) {
		rc = -ENOMEM;
		goto free_cci_client;
	}
	o_ctrl->soc_info.soc_private = soc_private;
	soc_private->power_info.dev  = &pdev->dev;

	INIT_LIST_HEAD(&(o_ctrl->i2c_init_data.list_head));
	INIT_LIST_HEAD(&(o_ctrl->i2c_calib_data.list_head));
	INIT_LIST_HEAD(&(o_ctrl->i2c_mode_data.list_head));
	mutex_init(&(o_ctrl->ois_mutex));
	mutex_init(&(o_ctrl->ois_read_mutex));
        mutex_init(&(o_ctrl->do_ioctl_ois));
        o_ctrl->ois_download_fw_done = CAM_OIS_FW_NOT_DOWNLOAD;
        o_ctrl->ois_fd_have_close_state = CAM_OIS_IS_OPEN;
#ifdef ENABLE_OIS_DELAY_POWER_DOWN
	o_ctrl->ois_power_down_thread_state = CAM_OIS_POWER_DOWN_THREAD_STOPPED;
	o_ctrl->ois_power_state = CAM_OIS_POWER_OFF;
	o_ctrl->ois_power_down_thread_exit = false;
	mutex_init(&(o_ctrl->ois_power_down_mutex));
#endif
	rc = cam_ois_driver_soc_init(o_ctrl);
	if (rc) {
		CAM_ERR(CAM_OIS, "failed: soc init rc %d", rc);
		goto free_soc;
	}

	rc = cam_ois_init_subdev_param(o_ctrl);
	if (rc)
		goto free_soc;

	rc = cam_ois_update_i2c_info(o_ctrl, &soc_private->i2c_info);
	if (rc) {
		CAM_ERR(CAM_OIS, "failed: to update i2c info rc %d", rc);
		goto unreg_subdev;
	}
	o_ctrl->bridge_intf.device_hdl = -1;

	platform_set_drvdata(pdev, o_ctrl);
	o_ctrl->cam_ois_state = CAM_OIS_INIT;

	mutex_init(&(o_ctrl->ois_hall_data_mutex));
	mutex_init(&(o_ctrl->ois_poll_thread_mutex));

	o_ctrl->ois_poll_thread_control_cmd = 0;
	if (kfifo_alloc(&o_ctrl->ois_hall_data_fifo, SAMPLE_COUNT_IN_DRIVER*SAMPLE_SIZE_IN_DRIVER, GFP_KERNEL)) {
		CAM_ERR(CAM_OIS, "failed to init ois_hall_data_fifo");
	}

	if (kfifo_alloc(&o_ctrl->ois_hall_data_fifoV2, SAMPLE_COUNT_IN_DRIVER*SAMPLE_SIZE_IN_DRIVER, GFP_KERNEL)) {
		CAM_ERR(CAM_OIS, "failed to init ois_hall_data_fifoV2");
	}
	InitOISResource(o_ctrl);

	return rc;
unreg_subdev:
	cam_unregister_subdev(&(o_ctrl->v4l2_dev_str));
free_soc:
	kfree(soc_private);
free_cci_client:
	kfree(o_ctrl->io_master_info.cci_client);
free_o_ctrl:
	kfree(o_ctrl);
	return rc;
}

static int cam_ois_platform_driver_remove(struct platform_device *pdev)
{
	int                             i;
	struct cam_ois_ctrl_t          *o_ctrl;
	struct cam_ois_soc_private     *soc_private;
	struct cam_sensor_power_ctrl_t *power_info;
	struct cam_hw_soc_info         *soc_info;

	o_ctrl = platform_get_drvdata(pdev);
	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "ois device is NULL");
		return -EINVAL;
	}

	CAM_INFO(CAM_OIS, "platform driver remove invoked");
	soc_info = &o_ctrl->soc_info;
	for (i = 0; i < soc_info->num_clk; i++)
		devm_clk_put(soc_info->dev, soc_info->clk[i]);

	mutex_lock(&(o_ctrl->ois_mutex));
	cam_ois_shutdown(o_ctrl);
	mutex_unlock(&(o_ctrl->ois_mutex));
	cam_unregister_subdev(&(o_ctrl->v4l2_dev_str));

	soc_private =
		(struct cam_ois_soc_private *)o_ctrl->soc_info.soc_private;
	power_info = &soc_private->power_info;

	kfree(o_ctrl->soc_info.soc_private);
	kfree(o_ctrl->io_master_info.cci_client);
	platform_set_drvdata(pdev, NULL);
	v4l2_set_subdevdata(&o_ctrl->v4l2_dev_str.sd, NULL);
	kfree(o_ctrl);

	return 0;
}

static const struct of_device_id cam_ois_dt_match[] = {
	{ .compatible = "qcom,ois" },
	{ }
};


MODULE_DEVICE_TABLE(of, cam_ois_dt_match);

static struct platform_driver cam_ois_platform_driver = {
	.driver = {
		.name = "qcom,ois",
		.owner = THIS_MODULE,
		.of_match_table = cam_ois_dt_match,
	},
	.probe = cam_ois_platform_driver_probe,
	.remove = cam_ois_platform_driver_remove,
};
static const struct i2c_device_id cam_ois_i2c_id[] = {
	{ "msm_ois", (kernel_ulong_t)NULL},
	{ }
};

static struct i2c_driver cam_ois_i2c_driver = {
	.id_table = cam_ois_i2c_id,
	.probe  = cam_ois_i2c_driver_probe,
	.remove = cam_ois_i2c_driver_remove,
	.driver = {
		.name = "msm_ois",
	},
};

static struct cam_ois_registered_driver_t registered_driver = {
	0, 0};

static int __init cam_ois_driver_init(void)
{
	int rc = 0;

	rc = platform_driver_register(&cam_ois_platform_driver);
	if (rc) {
		CAM_ERR(CAM_OIS, "platform_driver_register failed rc = %d",
			rc);
		return rc;
	}

	registered_driver.platform_driver = 1;

	rc = i2c_add_driver(&cam_ois_i2c_driver);
	if (rc) {
		CAM_ERR(CAM_OIS, "i2c_add_driver failed rc = %d", rc);
		return rc;
	}

	registered_driver.i2c_driver = 1;
	return rc;
}

static void __exit cam_ois_driver_exit(void)
{
	if (registered_driver.platform_driver)
		platform_driver_unregister(&cam_ois_platform_driver);

	if (registered_driver.i2c_driver)
		i2c_del_driver(&cam_ois_i2c_driver);
}

module_init(cam_ois_driver_init);
module_exit(cam_ois_driver_exit);
MODULE_DESCRIPTION("CAM OIS driver");
MODULE_LICENSE("GPL v2");
