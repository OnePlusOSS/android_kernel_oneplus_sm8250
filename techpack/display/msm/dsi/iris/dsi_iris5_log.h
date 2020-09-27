// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2020, Pixelworks, Inc.
 *
 * These files contain modifications made by Pixelworks, Inc., in 2019-2020.
 */

/* This file formats log print for 'iris', by adding 'IRIS_LOG'
 * prefix to normal log.
 * You can use 'adb shell irisConfig -loglevel' to get current
 * log level, and 'adb shell irisConfig -loglevel n' to switch
 * current log level, which affected by changing global value
 * 'iris_log_level'.
 * Log prefix can be redefined in respective module, but it
 * affects only for itself. You can redefine it just like this:
 * #ifdef IRIS_LOG_PREFIX
 * #undef IRIS_LOG_PREFIX
 * #define IRIS_LOG_PREFIX "IRIS_LOG_XXX"
 * #endif
 */
#ifndef __DSI_IRIS_LOG_H_
#define __DSI_IRIS_LOG_H_

#include <linux/kernel.h>

#ifndef IRIS_LOG_PREFIX
#define IRIS_LOG_PREFIX "IRIS_LOG"
#endif

#ifndef IRIS_PR
#define IRIS_PR(TAG, format, ...)	\
	pr_err("%s%s " format "\n", IRIS_LOG_PREFIX, TAG, ## __VA_ARGS__)
#endif

#ifndef IRIS_LOG_IF
#define IRIS_LOG_IF(cond, TAG, ...)	\
	do {	\
		if (cond)	\
			IRIS_PR(TAG, __VA_ARGS__);	\
	} while (0)
#endif

void iris_set_loglevel(int level);
int iris_get_loglevel(void);

/* Priority:
 * IRIS_LOGE > IRIS_LOGW > IRIS_LOGI > IRIS_LOGD > IRIS_LOGV
 * Instructions:
 * 1. IRIS_LOGE, for error log, it can print always.
 * 2. IRIS_LOGW, for warrning log, 'iris_log_level' is 2, so it
 *    can print always by default.
 * 3. IRIS_LOGI, for key info log, 'iris_log_level' is 2, so it
 *    can print always by default.
 * 4. IRIS_LOGD, for debug log, it cann't print unless you set
 *    'iris_log_level' to '3' or more greater.
 * 5. IRIS_LOGV, for debug log, that with block message or print
 *    very frequently. You must cautiously to use it, because
 *    massive log may cause performance degradation. It cann't
 *    print unless you set 'iris_log_level' to '4' or more
 *    greater.
 */
#ifndef IRIS_LOGE
#define IRIS_LOGE(...)	\
	IRIS_LOG_IF(true, " E", __VA_ARGS__)
#endif

#ifndef IRIS_LOGW
#define IRIS_LOGW(...)	\
	IRIS_LOG_IF(iris_get_loglevel() > 0, " W", __VA_ARGS__)
#endif

#ifndef IRIS_LOGI
#define IRIS_LOGI(...)	\
	IRIS_LOG_IF(iris_get_loglevel() > 1, " I", __VA_ARGS__)
#endif

#ifndef IRIS_LOGD
#define IRIS_LOGD(...)	\
	IRIS_LOG_IF(iris_get_loglevel() > 2, " D",  __VA_ARGS__)
#endif

#ifndef IRIS_LOGV
#define IRIS_LOGV(...)	\
	IRIS_LOG_IF(iris_get_loglevel() > 3, " V", __VA_ARGS__)
#endif

#ifndef IRIS_LOGVV
#define IRIS_LOGVV(...)	\
	IRIS_LOG_IF(iris_get_loglevel() > 4, " VV", __VA_ARGS__)
#endif

#ifndef IRIS_IF_LOGI
#define IRIS_IF_LOGI()	((iris_get_loglevel() > 1) ? true : false)
#endif

#ifndef IRIS_IF_LOGD
#define IRIS_IF_LOGD()	((iris_get_loglevel() > 2) ? true : false)
#endif

#ifndef IRIS_IF_LOGV
#define IRIS_IF_LOGV()	((iris_get_loglevel() > 3) ? true : false)
#endif

#ifndef IRIS_IF_LOGVV
#define IRIS_IF_LOGVV()	((iris_get_loglevel() > 4) ? true : false)
#endif

#ifndef IRIS_IF_NOT_LOGI
#define IRIS_IF_NOT_LOGI()	((iris_get_loglevel() < 2) ? true : false)
#endif

#ifndef IRIS_IF_NOT_LOGD
#define IRIS_IF_NOT_LOGD()	((iris_get_loglevel() < 3) ? true : false)
#endif

#ifndef IRIS_IF_NOT_LOGV
#define IRIS_IF_NOT_LOGV()	((iris_get_loglevel() < 4) ? true : false)
#endif

#ifndef IRIS_IF_NOT_LOGVV
#define IRIS_IF_NOT_LOGVV()	((iris_get_loglevel() < 5) ? true : false)
#endif

#ifndef IRIS_LOGI_IF
#define IRIS_LOGI_IF(cond)	(((cond) && iris_get_loglevel() > 1) ? true : false)
#endif

#ifndef IRIS_LOGD_IF
#define IRIS_LOGD_IF(cond)	(((cond) && iris_get_loglevel() > 2) ? true : false)
#endif

#ifndef IRIS_LOGV_IF
#define IRIS_LOGV_IF(cond)	(((cond) && iris_get_loglevel() > 3) ? true : false)
#endif

#ifndef IRIS_LOGVV_IF
#define IRIS_LOGVV_IF(cond)	(((cond) && iris_get_loglevel() > 4) ? true : false)
#endif

#endif /* __DSI_IRIS_LOG_H_ */
