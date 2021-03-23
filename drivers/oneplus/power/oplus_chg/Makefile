
subdir-ccflags-$(CONFIG_OPLUS_CHARGER) += -I$(srctree)/drivers/power/supply/qcom/
subdir-ccflags-$(CONFIG_OPLUS_CHARGER) += -DVENDOR_EDIT
subdir-ccflags-$(CONFIG_OPLUS_CHARGER) += -DOPLUS_FEATURE_CHG_BASIC

obj-y      += charger_ic/
obj-y      += warp_ic/

obj-y	+= oplus_charger.o
obj-y	+= oplus_gauge.o
obj-y	+= oplus_warp.o
obj-y	+= oplus_adapter.o
obj-y	+= oplus_wireless.o
obj-y	+= oplus_short.o