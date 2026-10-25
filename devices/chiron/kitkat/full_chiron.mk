# Product definition: full_chiron (Xiaomi Mi MIX 2, KitKat 4.4.4)
# Thin sibling of full_sagit: the shared sources (audio/lights/gralloc HALs,
# libhyb, EGL shims, fbpresentd, pdserve, rtc-restore, init.rc, scripts,
# keylayout/idc, wifi glue) are referenced straight out of
# vendor/xiaomi/sagit, and only the chiron configs live here.

PRODUCT_PACKAGES := \
    AlarmClock \
    Calendar \
    Camera \
    LatinIME \
    Music \
    Settings

$(call inherit-product, $(SRC_TARGET_DIR)/product/core.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/generic.mk)

# Same 2048-dalvik-heap bucket as sagit (6/8 GB RAM, 1080p panel); the
# defaults OOM system_server verifying framework-res.apk on first boot.
$(call inherit-product, frameworks/native/build/phone-xhdpi-2048-dalvik-heap.mk)
$(call inherit-product, frameworks/base/data/sounds/AllAudio.mk)

DEVICE_PACKAGE_OVERLAYS := vendor/xiaomi/sagit/overlay

PRODUCT_NAME := full_chiron
PRODUCT_DEVICE := chiron
PRODUCT_MODEL := MI MIX 2
PRODUCT_BRAND := Xiaomi
PRODUCT_MANUFACTURER := Xiaomi

PRODUCT_COPY_FILES += \
    vendor/xiaomi/chiron/system.prop:system/build.prop \
    vendor/xiaomi/chiron/fstab.chiron:root/fstab.chiron \
    vendor/xiaomi/sagit/init.rc:root/init.rc \
    vendor/xiaomi/chiron/init.chiron.rc:root/init.chiron.rc \
    vendor/xiaomi/chiron/init.chiron.usb.rc:root/init.chiron.usb.rc \
    vendor/xiaomi/sagit/usbudc.sh:root/sbin/usbudc.sh \
    vendor/xiaomi/chiron/ueventd.chiron.rc:root/ueventd.chiron.rc \
    vendor/xiaomi/sagit/autowinch.sh:root/sbin/autowinch.sh \
    vendor/xiaomi/sagit/dump-boot-log.sh:system/bin/dump-boot-log.sh \
    vendor/xiaomi/sagit/modem-hold.sh:root/sbin/modem-hold.sh \
    vendor/xiaomi/sagit/adsp-boot.sh:root/sbin/adsp-boot.sh \
    vendor/xiaomi/sagit/gpio-keys.kl:system/usr/keylayout/gpio-keys.kl \
    vendor/xiaomi/sagit/synaptics_dsx.idc:system/usr/idc/synaptics_dsx.idc \
    vendor/xiaomi/chiron/wifi/wpa_supplicant_overlay.conf:system/etc/wifi/wpa_supplicant_overlay.conf \
    vendor/xiaomi/chiron/wifi/p2p_supplicant_overlay.conf:system/etc/wifi/p2p_supplicant_overlay.conf \
    vendor/xiaomi/chiron/wifi/WCNSS_qcom_cfg.ini:system/vendor/etc/wifi/WCNSS_qcom_cfg.ini \
    vendor/xiaomi/chiron/wifi/WCNSS_qcom_cfg.ini:system/vendor/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini \
    vendor/xiaomi/sagit/audio/audio_policy.conf:system/etc/audio_policy.conf \
    vendor/xiaomi/chiron/mixer_paths_tasha.xml:system/etc/mixer_paths_tasha.xml \
    vendor/xiaomi/sagit/media/media_codecs.xml:system/etc/media_codecs.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml

# audio.primary.chiron (shared tinyalsa HAL, -DAMP_TAS2559) + sound recorder
# jni (same CHARQUIRKS reason as sagit).
PRODUCT_PACKAGES += \
    audio.primary.$(PRODUCT_DEVICE) \
    libsrec_jni

# Firmware. adsp/modem are the same msm8998 blobs as sagit (same baseband,
# user-confirmed) and are staged out-of-git in vendor/xiaomi/sagit-fw; the
# speaker amp is board-specific: chiron's TAS2559 needs its own uCDSP
# containers (vendor/xiaomi/chiron-fw/audio). No wlan_mac.bin: chiron keeps
# it in /persist, and the supplied persist.img is empty — the driver falls
# back to the address in bdwlan until the phone's own persist is restored.
SAGIT_FW := vendor/xiaomi/sagit-fw
CHIRON_FW := vendor/xiaomi/chiron-fw
ifneq ($(wildcard $(SAGIT_FW)/adsp/adsp.mdt),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(SAGIT_FW)/adsp && ls adsp.* cpe_* 2>/dev/null),$(SAGIT_FW)/adsp/$(f):system/vendor/firmware/$(f))
endif
ifneq ($(wildcard $(SAGIT_FW)/modem/modem.mdt),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(SAGIT_FW)/modem && ls mba.mbn modem.mdt modem.b* wlanmdsp.mbn 2>/dev/null),$(SAGIT_FW)/modem/$(f):system/vendor/firmware/$(f))
endif
ifneq ($(wildcard $(CHIRON_FW)/audio/tas2559_uCDSP.bin),)
PRODUCT_COPY_FILES += \
    $(CHIRON_FW)/audio/tas2559_uCDSP.bin:system/vendor/firmware/tas2559_uCDSP.bin \
    $(CHIRON_FW)/audio/tas2559_s_uCDSP.bin:system/vendor/firmware/tas2559_s_uCDSP.bin
endif
# bdwlan: the OS3 dump has none (it lives in the modem partition we were not
# given), so the WCN3990 board data from sagit is the bringup fallback.
ifneq ($(wildcard $(SAGIT_FW)/wlan/bdwlan.bin),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(SAGIT_FW)/wlan && ls bdwlan* 2>/dev/null),$(SAGIT_FW)/wlan/$(f):system/etc/wifi/$(f))
endif

# Graphics: the real Adreno 540 driver (Android 15 vendor closure) + QTI
# gralloc1 through libhyb, shared with sagit. gralloc.default and
# libGLES_android stay as fallbacks.
PRODUCT_PACKAGES += \
    libhyb \
    libEGL_adreno \
    libGLESv1_CM_adreno \
    libGLESv2_adreno \
    gralloc.$(PRODUCT_DEVICE) \
    gralloc.default \
    libGLES_android \
    hybtest \
    acdbtest \
    audiotrack_test \
    audiorecord_test \
    rtc-restore \
    lights.$(PRODUCT_DEVICE) \
    pdserve

# Android 15 vendor closure: same staged root at /system/a15 + a540 firmware.
HYB_DIR := vendor/xiaomi/sagit-hyb
ifneq ($(wildcard $(HYB_DIR)/root/vendor/lib/egl/libEGL_adreno.so),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(HYB_DIR)/root && find . -type f -printf '%P\n'),$(HYB_DIR)/root/$(f):system/a15/$(f)) \
    $(foreach f,$(shell cd $(HYB_DIR)/root/vendor/firmware && ls),$(HYB_DIR)/root/vendor/firmware/$(f):system/vendor/firmware/$(f))
endif

# ACDB calibration for chiron: the loader picks its directory by platform
# name; the chiron dump ships both ChironS/<blocks> and the generic Forte
# copies, so both are installed verbatim (libacdbloader on this device knows
# both names). Without them AFE/ASM cal lookups fail and playback drains
# silently.
CHIRON_ACDB := vendor/xiaomi/chiron-hyb/acdbdata
ifneq ($(wildcard $(CHIRON_ACDB)),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(CHIRON_ACDB) && find . -type f -printf '%P\n'),$(CHIRON_ACDB)/$(f):system/vendor/etc/acdbdata/$(f))
endif

# fbpresentd: shared 64-bit MDSS presenter (sources in
# vendor/xiaomi/sagit/fbpresentd, prebuilt in sagit-bin).
BIN_DIR := vendor/xiaomi/sagit-bin
ifneq ($(wildcard $(BIN_DIR)/bin/fbpresentd),)
PRODUCT_COPY_FILES += $(BIN_DIR)/bin/fbpresentd:system/bin/fbpresentd
endif
