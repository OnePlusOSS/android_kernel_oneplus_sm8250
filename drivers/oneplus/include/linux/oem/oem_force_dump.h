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
int open_selinux_switch(void);
int  set_oem_selinux_state(int state);
int get_oem_selinux_state(void);

#ifdef CONFIG_OEM_FORCE_DUMP
enum key_stat_item {
	KEY_RELEASED,
	KEY_PRESSED
};

extern void send_sig_to_get_trace(char *name);
extern void send_sig_to_get_tombstone(char *name);
extern void get_init_sched_info(void);
extern void dump_runqueue(void);
extern void compound_key_to_get_trace(char *name);
extern void compound_key_to_get_tombstone(char *name);
extern enum key_stat_item pwr_status, vol_up_status;

static inline void set_pwr_status(enum key_stat_item status)
{
	pwr_status = status;
}

static inline void set_vol_up_status(enum key_stat_item status)
{
	vol_up_status = status;
}
#else
static void send_sig_to_get_trace(char *name) {}
static void send_sig_to_get_tombstone(char *name) {}
static void get_init_sched_info(void) {}
static void dump_runqueue(void) {}
static void compound_key_to_get_trace(char *name) {}
static void compound_key_to_get_tombstone(char *name) {}
static inline void set_pwr_status(enum key_stat_item status) {}
static inline void set_vol_up_status(enum key_stat_item status) {}
#endif

#endif
