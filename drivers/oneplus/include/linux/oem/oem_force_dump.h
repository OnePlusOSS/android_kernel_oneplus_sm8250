/*
 * oem_force_dump.h
 *
 * header file supporting debug functions for Oneplus device.
 *
 * hefaxi@filesystems, 2015/07/03.
 */

#ifndef OEM_FORCE_DUMP_H
#define OEM_FORCE_DUMP_H

#ifndef NETLINK_ADB
#define NETLINK_ADB         23
#endif

#define OEM_SERIAL_INIT

extern void oem_check_force_dump_key(unsigned int code, int value);
extern int oem_get_download_mode(void);
void send_msg(char *message);
void send_msg_sync_mdm_dump(void);
#ifdef OEM_SERIAL_INIT
int  msm_serial_oem_init(void);
#else
inline int  msm_serial_oem_init(void){ return 0;}
#endif
#endif
