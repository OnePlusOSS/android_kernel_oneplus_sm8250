#ifndef _OPLUS_WFD_WLAN_H
#define _OPLUS_WFD_WLAN_H

#include <net/sock.h>

struct oplus_wfd_wlan_ops_t {
	void (*remove_he_ie_from_probe_request)(int remove);
	int (*get_dbs_capacity)(void);
	int (*get_phy_capacity)(int band);
	void (*get_supported_channels)(int band, int* len, int* freqs, int max_num);
	void (*get_avoid_channels)(int* len, int* freqs, int max_num);
};

extern void register_oplus_wfd_wlan_ops(struct oplus_wfd_wlan_ops_t *ops);
#endif
