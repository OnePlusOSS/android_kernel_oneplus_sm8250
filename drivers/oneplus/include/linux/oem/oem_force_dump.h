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

enum key_stat_item {
	KEY_RELEASED,
	KEY_PRESSED
};

extern void send_sig_to_get_trace(char *name);
extern void compound_key_to_get_trace(char *name);
extern enum key_stat_item pwr_status, vol_up_status;

static inline void set_pwr_status(enum key_stat_item status)
{
	pwr_status = status;
}

static inline void set_vol_up_status(enum key_stat_item status)
{
	vol_up_status = status;
}

#endif

extern int oem_get_modemdump_mode(void);
extern bool oem_get_twice_modemdump_state(void);
