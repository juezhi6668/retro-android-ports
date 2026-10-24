# Product definition: full_sagit (Xiaomi Mi 6, KitKat 4.4.4)
# Ported from full_maple; every vendor path moved to vendor/xiaomi/sagit.

PRODUCT_PACKAGES := \
    AlarmClock \
    Calendar \
    Camera \
    LatinIME \
    Music \
    Settings

$(call inherit-product, $(SRC_TARGET_DIR)/product/core.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/generic.mk)

# Dalvik heap: 4/6 GB RAM, 480 dpi 1080p panel → the 2048 bucket
# (heapgrowthlimit 192m, heapsize 512m). Without this the build ships Dalvik's
# tiny defaults and system_server OOMs verifying the 11.8 MB framework-res.apk
# on first boot (maple 2026-10-15: OutOfMemoryError in JarUtils.verifySignature,
# dalvik-cache never populates).
$(call inherit-product, frameworks/native/build/phone-xhdpi-2048-dalvik-heap.mk)
# stock AOSP ringtones/alarms/ui effects; without it /system/media/audio is
# empty and there is nothing for AudioFlinger to play.
$(call inherit-product, frameworks/base/data/sounds/AllAudio.mk)

DEVICE_PACKAGE_OVERLAYS := vendor/xiaomi/sagit/overlay

PRODUCT_NAME := full_sagit
PRODUCT_DEVICE := sagit
PRODUCT_MODEL := MI 6
PRODUCT_BRAND := Xiaomi
PRODUCT_MANUFACTURER := Xiaomi

PRODUCT_COPY_FILES += \
    vendor/xiaomi/sagit/system.prop:system/build.prop \
    vendor/xiaomi/sagit/fstab.sagit:root/fstab.sagit \
    vendor/xiaomi/sagit/init.rc:root/init.rc \
    vendor/xiaomi/sagit/init.sagit.rc:root/init.sagit.rc \
    vendor/xiaomi/sagit/init.sagit.usb.rc:root/init.sagit.usb.rc \
    vendor/xiaomi/sagit/usbudc.sh:root/sbin/usbudc.sh \
    vendor/xiaomi/sagit/ueventd.sagit.rc:root/ueventd.sagit.rc \
    vendor/xiaomi/sagit/autowinch.sh:root/sbin/autowinch.sh \
    vendor/xiaomi/sagit/dump-boot-log.sh:system/bin/dump-boot-log.sh \
    vendor/xiaomi/sagit/modem-hold.sh:root/sbin/modem-hold.sh \
    vendor/xiaomi/sagit/adsp-boot.sh:root/sbin/adsp-boot.sh \
    vendor/xiaomi/sagit/gpio-keys.kl:system/usr/keylayout/gpio-keys.kl \
    vendor/xiaomi/sagit/synaptics_dsx.idc:system/usr/idc/synaptics_dsx.idc \
    vendor/xiaomi/sagit/wifi/wpa_supplicant_overlay.conf:system/etc/wifi/wpa_supplicant_overlay.conf \
    vendor/xiaomi/sagit/wifi/p2p_supplicant_overlay.conf:system/etc/wifi/p2p_supplicant_overlay.conf \
    vendor/xiaomi/sagit/wifi/WCNSS_qcom_cfg.ini:system/vendor/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini \
    vendor/xiaomi/sagit/audio/audio_policy.conf:system/etc/audio_policy.conf \
    vendor/xiaomi/sagit/audio/mixer_paths_tasha.xml:system/etc/mixer_paths_tasha.xml \
    vendor/xiaomi/sagit/media/media_codecs.xml:system/etc/media_codecs.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml

# audio.primary.sagit (tinyalsa HAL) + sound recorder jni (CHARQUIRKS:
# "sound recorder needs libsrec_jni in PRODUCT_PACKAGES or it crashes").
# media_codecs.xml is product-supplied: KitKat's MediaCodecList reads
# /etc/media_codecs.xml and AOSP only ships one for goldfish/sdk — without it
# every compressed format fails with UNKNOWN_ERROR (maple TEST 52).
PRODUCT_PACKAGES += \
    audio.primary.sagit \
    libsrec_jni

# Firmware blobs live out-of-git at vendor/xiaomi/sagit-fw/{adsp,wlan,modem,audio}/
# (pulled from the modem partition + persist + stock ROM; mirrors the kernel-blob
# and sagit-hyb precedent so sync-device-tree.sh --delete leaves them alone).
# They land where firmware_class.path points (/vendor/firmware, /vendor ->
# /system/vendor):
#  - adsp.* + cpe_* (audio DSP + codec engine)
#  - bdwlan.* (WLAN board-data) + wlan_mac.bin (from persist; without it the
#    MAC falls back per CHARQUIRKS)
#  - modem/ (mba.mbn, modem.mdt, modem.b00-*, wlanmdsp.mbn — the PIL boot
#    modem-hold.sh triggers)
#  - audio/ (tfa98xx_aac.cnt — the TFA98xx speaker-amp container; the codec
#    loads it while registering the card and the whole card fails (-11) when
#    it is missing, so /proc/asound/cards stays empty: no sound at all)
FW_DIR := vendor/xiaomi/sagit-fw
ifneq ($(wildcard $(FW_DIR)/adsp/adsp.mdt),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(FW_DIR)/adsp && ls adsp.* cpe_* 2>/dev/null),$(FW_DIR)/adsp/$(f):system/vendor/firmware/$(f)) \
    $(foreach f,$(shell cd $(FW_DIR)/wlan && ls bdwlan* 2>/dev/null),$(FW_DIR)/wlan/$(f):system/etc/wifi/$(f)) \
    $(FW_DIR)/wlan/wlan_mac.bin:system/vendor/firmware/wlan/qca_cld/wlan_mac.bin
endif
ifneq ($(wildcard $(FW_DIR)/modem/modem.mdt),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(FW_DIR)/modem && ls mba.mbn modem.mdt modem.b* wlanmdsp.mbn 2>/dev/null),$(FW_DIR)/modem/$(f):system/vendor/firmware/$(f))
endif
ifneq ($(wildcard $(FW_DIR)/audio/tfa98xx_aac.cnt),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(FW_DIR)/audio && ls tfa98xx*.cnt 2>/dev/null),$(FW_DIR)/audio/$(f):system/vendor/firmware/$(f))
endif

# Graphics: the real Adreno 540 driver (Android 15 vendor closure) and the QTI
# gralloc1 are loaded into every KitKat process by libhyb (hybtest probes the
# driver end to end); gralloc.default and libGLES_android stay as fallbacks.
PRODUCT_PACKAGES += \
    libhyb \
    libEGL_adreno \
    libGLESv1_CM_adreno \
    libGLESv2_adreno \
    gralloc.sagit \
    gralloc.default \
    libGLES_android \
    hybtest \
    acdbtest \
    audiotrack_test \
    audiorecord_test \
    rtc-restore \
    lights.sagit \
    pdserve

# Android 15 vendor closure staged by tools/libhyb/stage.py: Adreno 540 driver
# (vendor/lib/egl), QTI gralloc1, and their dependencies. Outside
# vendor/xiaomi/sagit so rsync --delete leaves it alone; lands at /system/a15
# (HYB_ROOT in hyb.h). The a540 firmware goes to /vendor/firmware too.
HYB_DIR := vendor/xiaomi/sagit-hyb
ifneq ($(wildcard $(HYB_DIR)/root/vendor/lib/egl/libEGL_adreno.so),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(HYB_DIR)/root && find . -type f -printf '%P\n'),$(HYB_DIR)/root/$(f):system/a15/$(f)) \
    $(foreach f,$(shell cd $(HYB_DIR)/root/vendor/firmware && ls),$(HYB_DIR)/root/vendor/firmware/$(f):system/vendor/firmware/$(f))
endif

# ACDB calibration: the vendor libacdbloader hardcodes "Forte" as its data
# directory (hw_platform reads SAGIT on this device and is not what the loader
# uses), so the Mi 6 blocks are staged as acdbdata/Forte and installed under
# that name. Without it the AFE/ASM cal lookups fail and playback drains
# silently (maple TEST 50). adsp_avs_config.acdb sits beside the platform dir.
ifneq ($(wildcard $(HYB_DIR)/acdbdata/Forte),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(HYB_DIR)/acdbdata/Forte && ls),$(HYB_DIR)/acdbdata/Forte/$(f):system/vendor/etc/acdbdata/Forte/$(f))
endif
ifneq ($(wildcard $(HYB_DIR)/acdbdata/adsp_avs_config.acdb),)
PRODUCT_COPY_FILES += $(HYB_DIR)/acdbdata/adsp_avs_config.acdb:system/vendor/etc/acdbdata/adsp_avs_config.acdb
endif

# fbpresentd: zero-copy MDSS presentation (sources in fbpresentd/). 64-bit, so
# the NDK builds it (tools/libhyb/build-native.sh) into vendor/xiaomi/sagit-bin.
BIN_DIR := vendor/xiaomi/sagit-bin
ifneq ($(wildcard $(BIN_DIR)/bin/fbpresentd),)
PRODUCT_COPY_FILES += $(BIN_DIR)/bin/fbpresentd:system/bin/fbpresentd
endif
