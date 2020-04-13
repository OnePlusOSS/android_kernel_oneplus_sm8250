/* This file formats log print for 'iris', by adding 'IRIS_LOG'  */
/* prefix to normal log.                                         */
/* You can use 'adb shell irisConfig -loglevel' to get current   */
/* log level, and 'adb shell irisConfig -loglevel n' to switch   */
/* current log level, which affected by changing global value    */
/* 'iris_log_level'.                                             */
/* Log prefix can be redefined in respective module, but it      */
/* affects only for itself. You can redefine it just like this:  */
/* #ifdef IRIS_LOG_PREFIX                                        */
/* #undef IRIS_LOG_PREFIX                                        */
/* #define IRIS_LOG_PREFIX "IRIS_LOG_XXX"                        */
/* #endif                                                        */
#ifndef __IRIS_LOG_H_
#define __IRIS_LOG_H_

#include <linux/kernel.h>

/*
 * These files include source code for the Licensed Software that is provided under the terms and
 * conditions of a Master Software License Agreement and its applicable schedules.
 */

#ifndef IRIS_LOG_PREFIX
#define IRIS_LOG_PREFIX "IRIS_LOG"
#endif

#ifndef IRIS_PR
#define IRIS_PR(TAG, format, ...)                               \
	do {                                                    \
		pr_err("%s%s " format "\n",                     \
			IRIS_LOG_PREFIX, TAG, ## __VA_ARGS__);  \
	} while(0)
#endif

#ifndef IRIS_LOG_IF
#define IRIS_LOG_IF(cond, TAG, ...)                    \
	do {                                           \
		if(cond) IRIS_PR(TAG, __VA_ARGS__);    \
	} while(0)
#endif

void iris_set_loglevel(int level);
int iris_get_loglevel(void);

/* Priority:                                                     */
/* IRIS_LOGE > IRIS_LOGW > IRIS_LOGI > IRIS_LOGD > IRIS_LOGV     */
/* Instructions:                                                 */
/* 1. IRIS_LOGE, for error log, it can print always.             */
/* 2. IRIS_LOGW, for warrning log, 'iris_log_level' is 2, so it  */
/*    can print always by default.                               */
/* 3. IRIS_LOGI, for key info log, 'iris_log_level' is 2, so it  */
/*    can print always by default.                               */
/* 4. IRIS_LOGD, for debug log, it cann't print unless you set   */
/*    'iris_log_level' to '3' or more greater.                   */
/* 5. IRIS_LOGV, for debug log, that with block message or print */
/*    very frequently. You must cautiously to use it, because    */
/*    massive log may cause performance degradation. It cann't   */
/*    print unless you set 'iris_log_level' to '4' or more       */
/*    greater.                                                   */
#ifndef IRIS_LOGE
#define IRIS_LOGE(...)    \
	IRIS_LOG_IF(true, " E", __VA_ARGS__)
#endif

#ifndef IRIS_LOGW
#define IRIS_LOGW(...)    \
	IRIS_LOG_IF(iris_get_loglevel()>0, " W", __VA_ARGS__)
#endif

#ifndef IRIS_LOGI
#define IRIS_LOGI(...)    \
	IRIS_LOG_IF(iris_get_loglevel()>1, " I", __VA_ARGS__)
#endif

#ifndef IRIS_LOGD
#define IRIS_LOGD(...)    \
	IRIS_LOG_IF(iris_get_loglevel()>2, " D",  __VA_ARGS__)
#endif

#ifndef IRIS_LOGV
#define IRIS_LOGV(...)    \
	IRIS_LOG_IF(iris_get_loglevel()>3, " V", __VA_ARGS__)
#endif

#ifndef IRIS_LOGVV
#define IRIS_LOGVV(...)    \
	IRIS_LOG_IF(iris_get_loglevel()>4, " VV", __VA_ARGS__)
#endif

#ifndef IRIS_IF_LOGI
#define IRIS_IF_LOGI() if(iris_get_loglevel()>1)
#endif

#ifndef IRIS_IF_LOGD
#define IRIS_IF_LOGD() if(iris_get_loglevel()>2)
#endif

#ifndef IRIS_IF_LOGV
#define IRIS_IF_LOGV() if(iris_get_loglevel()>3)
#endif

#ifndef IRIS_IF_LOGVV
#define IRIS_IF_LOGVV() if(iris_get_loglevel()>4)
#endif

#ifndef IRIS_IF_NOT_LOGD
#define IRIS_IF_NOT_LOGD() if(iris_get_loglevel()<3)
#endif

#ifndef IRIS_IF_NOT_LOGV
#define IRIS_IF_NOT_LOGV() if(iris_get_loglevel()<4)
#endif

#ifndef IRIS_IF_NOT_LOGVV
#define IRIS_IF_NOT_LOGVV() if(iris_get_loglevel()<5)
#endif

#ifndef IRIS_LOGD_IF
#define IRIS_LOGD_IF(cond) if(cond && iris_get_loglevel()>2)
#endif

#ifndef IRIS_LOGV_IF
#define IRIS_LOGV_IF(cond) if(cond && iris_get_loglevel()>3)
#endif

#ifndef IRIS_LOGVV_IF
#define IRIS_LOGVV_IF(cond) if(cond && iris_get_loglevel()>4)
#endif


#endif /* __IRIS_LOG_H_ */
