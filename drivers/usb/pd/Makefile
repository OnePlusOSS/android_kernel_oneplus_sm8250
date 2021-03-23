# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for USB Power Delivery drivers
#
ifeq ($(CONFIG_OPLUS_CHARGER),y)
obj-$(CONFIG_USB_PD_POLICY)	+= oplus_policy_engine.o
else
obj-$(CONFIG_USB_PD_POLICY)	+= policy_engine.o
endif
obj-$(CONFIG_QPNP_USB_PDPHY)	+= qpnp-pdphy.o
