// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */
#include "msm_drv.h"
#include "msm_kms.h"


int msm_ioctl_iris_operate_conf(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	int ret = -EINVAL;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;

	ret = kms->funcs->iris_operate(kms, DRM_MSM_IRIS_OPERATE_CONF, data);
	return ret;
}

int msm_ioctl_iris_operate_tool(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	int ret = -EINVAL;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;

	ret = kms->funcs->iris_operate(kms, DRM_MSM_IRIS_OPERATE_TOOL, data);
	return ret;
}
