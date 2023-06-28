obj-$(CONFIG_OPLUS_COMMON_SOFT)  +=  device_info/

#ifdef OPLUS_FEATURE_LOWMEM_DBG
#Add for dump mem when shrink
obj-$(CONFIG_OPLUS_FEATURE_LOWMEM_DBG)	+=  lowmem_dbg/
#endif /* OPLUS_FEATURE_LOWMEM_DBG */

#ifdef VENDOR_EDIT
obj-$(CONFIG_OPLUS_FEATURE_MIDAS)    += midas/
#endif /* VENDOR_EDIT */

#ifdef OPLUS_FEATURE_MEMLEAK_DETECT
#Add for svelte logging
obj-$(CONFIG_SVELTE) +=  svelte/
#endif /* OPLUS_FEATURE_MEMLEAK_DETECT */


#ifdef OPLUS_FEATURE_THERMAL_HORAE
obj-$(CONFIG_HORAE_THERMAL_SHELL) += thermal/
#endif /* OPLUS_FEATURE_THERMAL_HORAE */

#ifdef OPLUS_FEATURE_GPIO_NC
obj-$(CONFIG_OPLUS_GPIO_NC) +=  oplus_gpio/
#endif

