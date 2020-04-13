/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _MSM_POWEROFF_H
#define _MSM_POWEROFF_H

extern void oem_force_minidump_mode(void);
extern int panic_flush_device_cache(int timeout);
extern void panic_flush_device_cache_circled_on(void);
extern void panic_flush_device_cache_circled_off(void);

#endif
