# SPDX-License-Identifier: GPL-2.0
# UFSHCD makefile
obj-$(CONFIG_SCSI_UFS_DWC_TC_PCI) += tc-dwc-g210-pci.o ufshcd-dwc.o tc-dwc-g210.o
obj-$(CONFIG_SCSI_UFS_DWC_TC_PLATFORM) += tc-dwc-g210-pltfrm.o ufshcd-dwc.o tc-dwc-g210.o
obj-$(CONFIG_SCSI_UFS_QCOM) += ufs-qcom.o
obj-$(CONFIG_SCSI_UFS_QCOM_ICE) += ufs-qcom-ice.o
obj-$(CONFIG_SCSI_UFSHCD) += ufshcd-core.o
ufshcd-core-objs := ufshcd.o ufs-sysfs.o
ifneq ($(OPLUS_FEATURE_UFS_SHOW_LATENCY),)
ufshcd-core-objs += ufs_latency_hist.o
endif # OPLUS_FEATURE_UFS_SHOW_LATENCY
ifneq ($(OPLUS_FEATURE_PADL_STATISTICS),)
ufshcd-core-objs += ufs_signal_quality.o
endif # OPLUS_FEATURE_PADL_STATISTICS
ifneq ($(OPLUS_FEATURE_UFSPLUS),)
obj-$(CONFIG_UFSFEATURE) += ufsfeature.o
obj-$(CONFIG_UFSHPB) += ufshpb.o
obj-$(CONFIG_UFSTW) += ufstw.o
obj-$(CONFIG_UFSHID) += ufshid.o
endif #OPLUS_FEATURE_UFSPLUS
obj-$(CONFIG_SCSI_UFSHCD_PCI) += ufshcd-pci.o
obj-$(CONFIG_SCSI_UFSHCD_PLATFORM) += ufshcd-pltfrm.o
obj-$(CONFIG_SCSI_UFS_TEST) += ufs_test.o
obj-$(CONFIG_DEBUG_FS) += ufs-debugfs.o ufs-qcom-debugfs.o
obj-$(CONFIG_SCSI_UFS_HISI) += ufs-hisi.o
ufshcd-core-$(CONFIG_SCSI_UFS_CRYPTO) += ufshcd-crypto.o
ufshcd-core-$(CONFIG_SCSI_UFS_CRYPTO_QTI) += ufshcd-crypto-qti.o
