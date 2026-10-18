# Product definition: full_maple (Sony Xperia XZ Premium, KitKat 4.4.4)
# SKELETON — mirrors devices/cooper/donut/cooper.mk structure for KitKat.

PRODUCT_PACKAGES := \
    AlarmClock \
    Calendar \
    Camera \
    LatinIME \
    Music \
    Settings

$(call inherit-product, $(SRC_TARGET_DIR)/product/core.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/generic.mk)

# Dalvik heap: 4 GB RAM, 410 dpi 1080p panel → the 2048 bucket
# (heapgrowthlimit 192m, heapsize 512m). Without this the build ships
# Dalvik's tiny defaults and system_server OOMs verifying the 11.8 MB
# framework-res.apk on first boot (seen 2026-10-15: OutOfMemoryError in
# JarUtils.verifySignature every life, dalvik-cache never populates).
$(call inherit-product, frameworks/native/build/phone-xhdpi-2048-dalvik-heap.mk)

DEVICE_PACKAGE_OVERLAYS := vendor/sony/maple/overlay

PRODUCT_NAME := full_maple
PRODUCT_DEVICE := maple
PRODUCT_MODEL := G8141
PRODUCT_BRAND := sony
PRODUCT_MANUFACTURER := Sony

# maple-specific files (copied into system/ and root/ by the build system).
# Kernel is a TODO: either prebuilt Sony msm8998 4.4 Image.gz-dtb or a
# locally built kernel/sony/msm8998 output.
PRODUCT_COPY_FILES += \
    vendor/sony/maple/system.prop:system/build.prop \
    vendor/sony/maple/fstab.maple:root/fstab.maple \
    vendor/sony/maple/init.rc:root/init.rc \
    vendor/sony/maple/init.maple.rc:root/init.maple.rc \
    vendor/sony/maple/init.maple.usb.rc:root/init.maple.usb.rc \
    vendor/sony/maple/usbudc.sh:root/sbin/usbudc.sh \
    vendor/sony/maple/ueventd.maple.rc:root/ueventd.maple.rc \
    vendor/sony/maple/autowinch.sh:root/sbin/autowinch.sh \
    vendor/sony/maple/dump-boot-log.sh:system/bin/dump-boot-log.sh \
    vendor/sony/maple/modem-hold.sh:root/sbin/modem-hold.sh \
    vendor/sony/maple/adsp-boot.sh:root/sbin/adsp-boot.sh \
    vendor/sony/maple/gpio-keys.kl:system/usr/keylayout/gpio-keys.kl \
    vendor/sony/maple/clearpad.idc:system/usr/idc/clearpad.idc \
    vendor/sony/maple/wifi/wpa_supplicant_overlay.conf:system/etc/wifi/wpa_supplicant_overlay.conf \
    vendor/sony/maple/wifi/p2p_supplicant_overlay.conf:system/etc/wifi/p2p_supplicant_overlay.conf \
    vendor/sony/maple/wifi/WCNSS_qcom_cfg.ini:system/vendor/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini \
    vendor/sony/maple/audio/audio_policy.conf:system/etc/audio_policy.conf \
    vendor/sony/maple/audio/mixer_paths_tasha.xml:system/etc/mixer_paths_tasha.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml
# adsp + wlan calibration/BDF firmware blobs (modem `image/adsp.*`,
# `image/bdwlan.*`, persist `wlan/wlan_mac.bin` — inventoried live
# 2026-10-16) stay OUT of git (same policy as the kernel blob and the
# Lineage closure: Sony/Qualcomm binaries, plus board-specific
# calibration). Staged on the build server at
# vendor/sony/maple-fw/{adsp,wlan,bt}/ (outside the sync-device-tree
# rsync path, like vendor/sony/maple-hyb); the FW_DIR block below copies
# them into the image (CHARQUIRKS: firmware resolves against the
# requester root with firmware_class.path=/vendor/firmware; a miss
# stalls ~60 s per file in ueventd's user helper — that delay is the
# tell). WCNSS_qcom_cfg.ini above ships from the tree.

# audio.primary.maple (tinyalsa HAL) + sound recorder jni (CHARQUIRKS:
# "sound recorder needs libsrec_jni in PRODUCT_PACKAGES or it crashes").
# media_codecs.xml ships with the stock build (software codecs) — kept.
PRODUCT_PACKAGES += \
    audio.primary.maple \
    libsrec_jni

# Firmware blobs live out-of-git on the build server at
# vendor/sony/maple-fw/{adsp,wlan,modem,bt}/ (pulled from the modem/bluetooth
# partitions + persist; mirrors the kernel-blob and maple-hyb precedent so
# sync-device-tree.sh --delete leaves them alone). Copy them into the
# image where the kernel's firmware loader looks (firmware_class.path is
# /vendor/firmware; /vendor -> /system/vendor):
#  - adsp.* + cpe_9335.* (tavil/tasha audio DSP + codec engine)
#  - bdwlan.* (WLAN board-data/calibration; driver picks by board id) +
#    wlan_mac.bin (Sony's MAC from persist; without it the MAC falls back
#    to otp/soc serial per CHARQUIRKS)
#  - modem/ (mba.mbn, modem.mdt, modem.b00-28, wlanmdsp.mbn — the PIL boot
#    firmware modem-hold.sh triggers; TEST 39)
#  - BT tlv/bin stay out for now (bluez bringup is a later change).
FW_DIR := vendor/sony/maple-fw
ifneq ($(wildcard $(FW_DIR)/adsp/adsp.mdt),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(FW_DIR)/adsp && ls adsp.* cpe_9335.* 2>/dev/null),$(FW_DIR)/adsp/$(f):system/vendor/firmware/$(f)) \
    $(foreach f,$(shell cd $(FW_DIR)/wlan && ls bdwlan* 2>/dev/null),$(FW_DIR)/wlan/$(f):system/etc/wifi/$(f)) \
    $(FW_DIR)/wlan/wlan_mac.bin:system/vendor/firmware/wlan/qca_cld/wlan_mac.bin
endif
# Modem PIL firmware (TEST 39): mba.mbn + modem.mdt + modem.b00-28 +
# wlanmdsp.mbn, pulled from the device's modem partition
# (/dev/block/sde33 -> /vendor/firmware_mnt/image) and staged here so the
# modem boots with NO partition mount — the live symlinks into the partition
# failed exactly that way (open /dev/subsys_modem -> EAGAIN when the mount
# was missing, TEST 38). ~58 MB, board-specific: keep out of git like the
# adsp/wlan dirs.
ifneq ($(wildcard $(FW_DIR)/modem/modem.mdt),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(FW_DIR)/modem && ls mba.mbn modem.mdt modem.b* wlanmdsp.mbn 2>/dev/null),$(FW_DIR)/modem/$(f):system/vendor/firmware/$(f))
endif

# Graphics: the real Adreno 540 driver (Lineage 22.1 / Android 15 vendor closure) and the QTI
# gralloc1 are loaded into every KitKat process by libhyb (hybtest probes the driver end to
# end); gralloc.default and libGLES_android stay as the fallbacks. No glrenderd, no emulation
# libs: KitKat's stock EGL loader picks /system/lib/egl/libEGL_adreno.so (BoardConfig +
# egl.cfg). The emulation libs were also removed from the build (BUILD_EMULATOR_OPENGL gone),
# so the loader can never latch the wrong driver by readdir order.
PRODUCT_PACKAGES += \
    libhyb \
    libEGL_adreno \
    libGLESv1_CM_adreno \
    libGLESv2_adreno \
    gralloc.maple \
    gralloc.default \
    libGLES_android \
    hybtest \
    rtc-restore \
    lights.maple \
    pdserve

# Lineage 22.1 (Android 15) vendor closure staged by tools/libhyb/stage.py: the Adreno 540
# driver (vendor/lib/egl), the QTI gralloc1 (vendor/lib/hw/gralloc.default.so) and their
# dependencies. It lives outside vendor/sony/maple so sync-device-tree.sh's rsync --delete
# leaves it alone, and lands at /system/a15 — the root libhyb looks in (HYB_ROOT in hyb.h).
# The a540 firmware also goes where the kernel's firmware loader looks (firmware_class.path
# is /vendor/firmware; /vendor -> /system/vendor), same place as the modem/adsp blobs.
HYB_DIR := vendor/sony/maple-hyb
ifneq ($(wildcard $(HYB_DIR)/root/vendor/lib/egl/libEGL_adreno.so),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(HYB_DIR)/root && find . -type f -printf '%P\n'),$(HYB_DIR)/root/$(f):system/a15/$(f)) \
    $(foreach f,$(shell cd $(HYB_DIR)/root/vendor/firmware && ls),$(HYB_DIR)/root/vendor/firmware/$(f):system/vendor/firmware/$(f))
endif

# fbpresentd: zero-copy presentation through an MDSS atomic commit (sources in fbpresentd/).
# 64-bit, so the NDK builds it (tools/libhyb/build-native.sh) and it is staged out-of-git at
# vendor/sony/maple-bin/, like the kernel blob. Without it gralloc.maple falls back to
# copy + FBIOPUT_VSCREENINFO flips.
BIN_DIR := vendor/sony/maple-bin
ifneq ($(wildcard $(BIN_DIR)/bin/fbpresentd),)
PRODUCT_COPY_FILES += $(BIN_DIR)/bin/fbpresentd:system/bin/fbpresentd
endif
