ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY_NOT_OEM),y)
        dtbo-$(CONFIG_ARCH_KONA) += \
                kona-cdp-overlay.dtbo \
                kona-cdp-lcd-overlay.dtbo \
                kona-mtp-overlay.dtbo \
                kona-mtp-ws-overlay.dtbo \
                kona-xr-overlay.dtbo \
                kona-rumi-overlay.dtbo \
                kona-qrd-overlay.dtbo \
		kona-xrfusion-overlay.dtbo \
                kona-hdk-overlay.dtbo

kona-cdp-overlay.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
kona-cdp-lcd-overlay.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
kona-mtp-overlay.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
kona-mtp-ws-overlay.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
kona-xr-overlay.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
kona-rumi-overlay.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
kona-qrd-overlay.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
kona-xrfusion-overlay.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
kona-hdk-overlay.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
else
ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
        dtbo-$(CONFIG_ARCH_KONA) += \
                instantnoodlep-overlay-evb.dtbo \
                instantnoodlep-overlay-t0.dtbo \
                instantnoodlep-overlay-evt1.dtbo \
                instantnoodlep-overlay-dvt.dtbo \
                instantnoodle-overlay-t0.dtbo \
                instantnoodle-overlay-evt1.dtbo \
                instantnoodle-overlay-dvt.dtbo \
                instantnoodlev-overlay-t0.dtbo \
                instantnoodlev-overlay-evt1.dtbo \
                instantnoodlev-overlay-dvt.dtbo \
                kebab-overlay.dtbo

instantnoodlep-overlay-evb.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
instantnoodlep-overlay-t0.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
instantnoodlep-overlay-evt1.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
instantnoodlep-overlay-dvt.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb

instantnoodle-overlay-t0.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
instantnoodle-overlay-evt1.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
instantnoodle-overlay-dvt.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb

instantnoodlev-overlay-t0.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
instantnoodlev-overlay-evt1.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
instantnoodlev-overlay-dvt.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb

kebab-overlay.dtbo-base := kona.dtb kona-v2.dtb kona-v2.1.dtb
else
dtb-$(CONFIG_ARCH_KONA) += kona-rumi.dtb \
        kona-mtp.dtb \
        kona-mtp-ws.dtb \
        kona-xr.dtb \
	kona-xrfusion.dtb \
        kona-cdp.dtb \
        kona-cdp-lcd.dtb \
        kona-qrd.dtb \
        kona-v2-rumi.dtb \
        kona-v2-mtp.dtb \
        kona-v2-mtp-ws.dtb \
        kona-v2-cdp.dtb \
        kona-v2-qrd.dtb \
	kona-v2-xrfusion.dtb \
        kona-hdk.dtb \
        kona-v2.1-mtp.dtb \
        kona-v2.1-mtp-ws.dtb \
        kona-v2.1-cdp.dtb \
        kona-v2.1-qrd.dtb \
        kona-v2.1-hdk.dtb \
	kona-v2.1-xrfusion.dtb \
        qrb5165-iot-rb5.dtb \
        kona-v2.1-iot-rb5.dtb
endif
endif



ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
        dtbo-$(CONFIG_ARCH_BENGAL) += \
                bengal-rumi-overlay.dtbo \
                bengal-qrd-overlay.dtbo \
                bengal-idp-overlay.dtbo \
                bengal-idp-usbc-overlay.dtbo \
                bengalp-idp-overlay.dtbo \
                bengal-idp-1gb-overlay.dtbo \
                bengal-idp-2gb-overlay.dtbo \
                bengal-idp-usbc-1gb-overlay.dtbo \
                bengal-idp-usbc-2gb-overlay.dtbo \
                qcm4290-iot-idp-overlay.dtbo \
                qcs4290-iot-idp-overlay.dtbo \
                qcm4290-iot-idp-1gb-overlay.dtbo \
                qcm4290-iot-idp-2gb-overlay.dtbo \
                qcm4290-iot-idp-usbc-1gb-overlay.dtbo \
                qcm4290-iot-idp-usbc-2gb-overlay.dtbo \
                qcm4290-iot-idp-usbc-overlay.dtbo

bengal-rumi-overlay.dtbo-base := bengal.dtb
bengal-qrd-overlay.dtbo-base := bengal.dtb
bengal-idp-overlay.dtbo-base := bengal.dtb
bengal-idp-usbc-overlay.dtbo-base := bengal.dtb
bengalp-idp-overlay.dtbo-base := bengalp.dtb
bengal-idp-1gb-overlay.dtbo-base := bengal-1gb.dtb
bengal-idp-2gb-overlay.dtbo-base := bengal-2gb.dtb
bengal-idp-usbc-1gb-overlay.dtbo-base := bengal-1gb.dtb
bengal-idp-usbc-2gb-overlay.dtbo-base := bengal-2gb.dtb
qcm4290-iot-idp-overlay.dtbo-base := qcm4290.dtb
qcs4290-iot-idp-overlay.dtbo-base := qcs4290.dtb
qcm4290-iot-idp-1gb-overlay.dtbo-base := qcm4290-iot-1gb.dtb
qcm4290-iot-idp-2gb-overlay.dtbo-base := qcm4290-iot-2gb.dtb
qcm4290-iot-idp-usbc-1gb-overlay.dtbo-base := qcm4290-iot-1gb.dtb
qcm4290-iot-idp-usbc-2gb-overlay.dtbo-base := qcm4290-iot-2gb.dtb
qcm4290-iot-idp-usbc-overlay.dtbo-base := qcm4290.dtb
else
dtb-$(CONFIG_ARCH_BENGAL) += bengal-rumi.dtb \
                bengal-qrd.dtb \
		bengal-idp.dtb \
		bengal-idp-usbc.dtb \
		bengalp-idp.dtb \
		bengal-idp-1gb.dtb \
		bengal-idp-2gb.dtb \
		bengal-idp-usbc-1gb.dtb \
		bengal-idp-usbc-2gb.dtb \
		qcm4290-iot-idp.dtb \
		qcs4290-iot-idp.dtb \
		qcm4290-iot-idp-1gb.dtb \
		qcm4290-iot-idp-2gb.dtb \
		qcm4290-iot-idp-usbc-1gb.dtb \
		qcm4290-iot-idp-usbc-2gb.dtb \
		qcm4290-iot-idp-usbc.dtb
endif

ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
        dtbo-$(CONFIG_ARCH_SCUBA) += \
		scuba-rumi-overlay.dtbo \
		scuba-idp-overlay.dtbo \
		scuba-idp-usbc-overlay.dtbo \
		scuba-qrd-eldo-overlay.dtbo \
		scuba-qrd-non-eldo-overlay.dtbo

scuba-rumi-overlay.dtbo-base := scuba.dtb scubap.dtb scuba-2gb.dtb
scuba-idp-overlay.dtbo-base := scuba.dtb scubap.dtb scuba-2gb.dtb
scuba-qrd-eldo-overlay.dtbo-base := scuba.dtb scubap.dtb scuba-2gb.dtb
scuba-qrd-non-eldo-overlay.dtbo-base := scuba.dtb scubap.dtb scuba-2gb.dtb
scuba-idp-usbc-overlay.dtbo-base := scuba.dtb scubap.dtb scuba-2gb.dtb
else
dtb-$(CONFIG_ARCH_SCUBA) += scuba-rumi.dtb \
	scuba-idp.dtb \
	scuba-idp-usbc.dtb \
	scuba-qrd-eldo.dtb \
	scuba-qrd-non-eldo.dtb \
	scubap-idp.dtb \
	scubap-idp-2gb.dtb \
	scuba-idp-2gb.dtb \
	scuba-idp-usbc-2gb.dtb
endif

always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files    := *.dtb *.dtbo
