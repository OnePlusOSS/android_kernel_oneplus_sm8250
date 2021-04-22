#ifndef _DEVICE_INFO_H_
#define _DEVICE_INFO_H_ 1
#ifdef CONFIG_PSTORE_DEVICE_INFO
void save_dump_reason_to_device_info(char *buf);
#else
inline void save_dump_reason_to_device_info(char *reason) {}
#endif

#endif
