# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux Kernel SOC specific device drivers.
#

obj-$(CONFIG_ARCH_ACTIONS)	+= actions/
obj-$(CONFIG_ARCH_AT91)		+= atmel/
obj-y				+= bcm/
obj-$(CONFIG_ARCH_DOVE)		+= dove/
obj-$(CONFIG_MACH_DOVE)		+= dove/
obj-y				+= fsl/
obj-$(CONFIG_ARCH_GEMINI)	+= gemini/
obj-$(CONFIG_ARCH_MXC)		+= imx/
obj-$(CONFIG_SOC_XWAY)		+= lantiq/
obj-y				+= mediatek/
obj-y				+= amlogic/
obj-y				+= qcom/
obj-y				+= renesas/
obj-$(CONFIG_ARCH_ROCKCHIP)	+= rockchip/
obj-$(CONFIG_SOC_SAMSUNG)	+= samsung/
obj-$(CONFIG_ARCH_SUNXI)	+= sunxi/
obj-$(CONFIG_ARCH_TEGRA)	+= tegra/
obj-$(CONFIG_SOC_TI)		+= ti/
obj-$(CONFIG_ARCH_U8500)	+= ux500/
obj-$(CONFIG_PLAT_VERSATILE)	+= versatile/
obj-y				+= xilinx/
obj-$(CONFIG_ARCH_ZX)		+= zte/
obj-y                           += oplus/
#ifdef VENDOR_EDIT
obj-$(CONFIG_OPLUS_SYSTEM_KERNEL)	+= oplus/system/
#endif /* VENDOR_EDIT */

#ifdef OPLUS_FEATURE_IOMONITOR
obj-$(CONFIG_IOMONITOR)    +=  oplus/iomonitor/
#endif /*OPLUS_FEATURE_IOMONITOR*/

#ifdef OPLUS_FEATURE_SENSOR_SMEM
obj-y						+= oplus/sensor/
#endif
#ifdef OPLUS_FEATURE_SENSOR_CRASH_MONITOR
obj-$(CONFIG_OPLUS_FEATURE_SENSOR_CRASH_MONITOR)	+= oplus/oplus_slpirst/
#endif


#ifdef OPLUS_FEATURE_HEALTHINFO
obj-y     +=  oplus/healthinfo/
#endif /* OPLUS_FEATURE_HEALTHINFO */

#ifdef OPLUS_FEATURE_POWERINFO_STANDBY
obj-$(CONFIG_OPLUS_WAKELOCK_PROFILER) += oplus/owakelock/
#endif /* OPLUS_FEATURE_POWERINFO_STANDBY */

#ifdef OPLUS_FEATURE_ESIM
obj-y += oplus/misc/
#endif /* OPLUS_FEATURE_ESIM */

#ifdef OPLUS_FEATURE_TPP
#jacky.ho@optimization add for tpp module
obj-$(CONFIG_OPLUS_FEATURE_TPP) += oplus/tpp/
#endif /* OPLUS_FEATURE_TPP */

#ifdef OPLUS_FEATURE_IM
#ted.lin@optimization add for im module
obj-$(CONFIG_OPLUS_FEATURE_IM) += oplus/im/
#endif /* OPLUS_FEATURE_IM */

#ifdef OPLUS_FEATURE_TPD
obj-$(CONFIG_OPLUS_FEATURE_TPD)     +=  oplus/tpd/
#endif

#Add for NandSwap
obj-$(CONFIG_NANDSWAP)		+= oplus/oplus_nandswap/

#ifdef OPLUS_FEATURE_GAME_OPT
obj-$(CONFIG_OPLUS_FEATURE_GAME_OPT) += oplus/game_opt/
#endif /* OPLUS_FEATURE_GAME_OPT */
