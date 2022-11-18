#ifndef _LIBOLC_H_
#define _LIBOLC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <linux/types.h>

#define LOG_KERNEL           (0x1)
#define LOG_SYSTEM           (0x1 << 1)
#define LOG_MAIN             (0x1 << 2)
#define LOG_EVENTS           (0x1 << 3)
#define LOG_RADIO            (0x1 << 4)
#define LOG_ANDROID          (0x1 << 5)
#define LOG_THEIA            (0x1 << 6)
#define LOG_ADSP             (0x1 << 7)
#define LOG_OBRAIN           (0x1 << 8)
#define LOG_AUDIO            (0x1 << 9)
#define LOG_BTHCI            (0x1 << 10)
#define LOG_SYSTRACE         (0x1 << 11)
#define LOG_HPROF            (0x1 << 12)
#define LOG_QUALITYPROTECT   (0x1 << 13)
#define LOG_OTRTA            (0x1 << 14)
#define LOG_BATTERYSTATS     (0x1 << 15)
#define LOG_NWATCHCALL       (0x1 << 16)
#define LOG_VIDEO            (0x1 << 17)
#define LOG_WIFI             (0x1 << 18)
#define LOG_TCPDUMP          (0x1 << 19)
#define LOG_MODEM            (0x1 << 20)
#define LOG_WIFIMINIDUMP     (0x1 << 21)
#define LOG_GPS              (0x1 << 22)
#define LOG_SHUTDOWN_DETECT  (0x1 << 23)
#define LOG_BTSSRDUMP        (0x1 << 24)
#define LOG_BTSWITCH         (0X1 << 25)
#define LOG_PHOENIX          (0x1 << 26)
#define LOG_QCOM_MINIDUMP    (0x1 << 27)

#define EXP_LEVEL_CRITICAL  1
#define EXP_LEVEL_IMPORTANT 2
#define EXP_LEVEL_GENERAL   3
#define EXP_LEVEL_INFO      4
#define EXP_LEVEL_DEBUG     5

enum exception_type {
    EXCEPTION_KERNEL,
    EXCEPTION_NATIVE,
    EXCEPTION_FRAMEWROK,
    EXCEPTION_APP,
};

struct exception_info {
    uint64_t time;               // Exception time, Unix timestamp in seconds
    uint32_t exceptionId;        // ExceptionID 0x100XXYYY, XX is module identification, YY is specific exception identification
    uint32_t exceptionType;      // (Not used yet)exception type. eg: Kernel, Native
    uint32_t level;              // (Not used yet)exception level, eg: IMPORTTANT, CIRTAL
    uint64_t atomicLogs;         // atomic log types combination. eg: LOG_KERNEL,  LOG_KERNEL|LOG_SYSTEM
    char logParams[256];         // extra params of atomic log
};


int olc_raise_exception(struct exception_info *exp);

#ifdef __cplusplus
}
#endif

#endif