# Product definition: full_cooper (Samsung Galaxy Ace GT-S5830, Donut 1.6)
# Donut-era product file. Mimics build/target/product/generic.mk + cooper hardware.

PRODUCT_PACKAGES := \
    AlarmClock \
    AlarmProvider \
    Calendar \
    Camera \
    DrmProvider \
    LatinIME \
    Mms \
    Music \
    Settings \
    Sync \
    Updater \
    CalendarProvider \
    SubscribedFeedsProvider \
    SyncProvider \
    toggleshutter \
    vtouch

$(call inherit-product, $(SRC_TARGET_DIR)/product/core.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/generic.mk)
# Donut ringtones/alarms/notifications (no Donut product includes sounds by
# default; AudioPackage2 is the documented "larger package for devices with
# larger internal flash" — cooper stl12 fits it easily).
$(call inherit-product, frameworks/base/data/sounds/AudioPackage2.mk)

PRODUCT_NAME := full_cooper
PRODUCT_DEVICE := cooper
PRODUCT_MODEL := GT-S5830
PRODUCT_BRAND := samsung
PRODUCT_MANUFACTURER := Samsung

# Cooper-specific files (copied into system/ and root/ by the build system)
# FSR/STL storage modules: two sets live in the tree, modules/ (CM7 set) and
# modules-twrp/ (Omni/TWRP set). The shipped boot.img uses kernel-twrp, so the
# modules-twrp set is installed (same kernel+modules combo TWRP boots with).
PRODUCT_COPY_FILES += \
    vendor/samsung/cooper/egl.cfg:system/lib/egl/egl.cfg \
    vendor/samsung/cooper/vold.fstab:system/etc/vold.fstab \
    vendor/samsung/cooper/system.prop:system/build.prop \
    vendor/samsung/cooper/wpa_supplicant.conf:system/etc/wifi/wpa_supplicant.conf \
    vendor/samsung/cooper/init.rc:root/init.rc \
    vendor/samsung/cooper/init.gt-s5830.rc:root/init.gt-s5830.rc \
    vendor/samsung/cooper/ueventd.gt-s5830.rc:root/ueventd.gt-s5830.rc \
    vendor/samsung/cooper/modules-twrp/fsr.ko:root/lib/modules/fsr.ko \
    vendor/samsung/cooper/modules-twrp/fsr_stl.ko:root/lib/modules/fsr_stl.ko \
    vendor/samsung/cooper/modules-twrp/sec_param.ko:root/lib/modules/sec_param.ko \
    vendor/samsung/cooper/qwerty.kl:system/usr/keylayout/qwerty.kl \
    vendor/samsung/cooper/sec_jack.kl:system/usr/keylayout/sec_jack.kl \
    vendor/samsung/cooper/sec_key.kl:system/usr/keylayout/sec_key.kl \
    vendor/samsung/cooper/sec_touchscreen.kl:system/usr/keylayout/sec_touchscreen.kl \
    vendor/samsung/cooper/cooper_vtouch.kl:system/usr/keylayout/cooper_vtouch.kl
# Proprietary WiFi. Two stacks live here, only one is active:
#  - ACTIVE: mainline ath6kl (compat-wireless backport) + cfg80211/compat
#    deps, all vermagic 2.6.35.7-androidarmv6+ = EXACT match for the shipped
#    kernel-twrp (proven insmod-clean in recovery, talks to the AR6003 over
#    SDIO). Deps are pre-insmodded from init.rc; WifiService loads ath6kl
#    itself (WIFI_DRIVER_* in BoardConfig) and wpa_supplicant drives it via
#    -Dwext through cfg80211's wext compat. Firmware goes to
#    /system/etc/firmware (= /etc/firmware, where Donut init's firmware
#    loader in devices.c looks): full OmniROM hw2.0 set incl. samsung_*
#    variants and both bdata revisions.
#  - INACTIVE: Atheros ar6000.ko (v3.0.70.35, WEXT, vermagic
#    2.6.35.7-perf-CL882825 = CM-kernel build). Force-loading it on the
#    shipped kernel PANICS the device at WiFi toggle (verified live), so
#    nothing references it; kept with its CM7 /system/wifi firmware set for
#    a possible future CM-kernel boot. DO NOT point WIFI_DRIVER at it.
PRODUCT_COPY_FILES += \
    vendor/samsung/cooper/proprietary/wifi/modules/compat.ko:system/lib/modules/compat.ko \
    vendor/samsung/cooper/proprietary/wifi/modules/cfg80211.ko:system/lib/modules/cfg80211.ko \
    vendor/samsung/cooper/proprietary/wifi/modules/ath6kl.ko:system/lib/modules/ath6kl.ko \
    vendor/samsung/cooper/proprietary/wifi/ar6000.ko:system/wifi/ar6000.ko \
    vendor/samsung/cooper/proprietary/wifi/ath6k/AR6003/hw2.0/athwlan.bin.z77:system/wifi/ath6k/AR6003/hw2.0/athwlan.bin.z77 \
    vendor/samsung/cooper/proprietary/wifi/ath6k/AR6003/hw2.0/athtcmd_ram.bin:system/wifi/ath6k/AR6003/hw2.0/athtcmd_ram.bin \
    vendor/samsung/cooper/proprietary/wifi/ath6k/AR6003/hw2.0/bdata.SD31.bin:system/wifi/ath6k/AR6003/hw2.0/bdata.SD31.bin \
    vendor/samsung/cooper/proprietary/wifi/ath6k/AR6003/hw2.0/bdata.SD31.bin.04:system/wifi/ath6k/AR6003/hw2.0/bdata.SD31.bin.04 \
    vendor/samsung/cooper/proprietary/wifi/ath6k/AR6003/hw2.0/data.patch.bin:system/wifi/ath6k/AR6003/hw2.0/data.patch.bin \
    vendor/samsung/cooper/proprietary/wifi/ath6k/AR6003/hw2.0/otp.bin.z77:system/wifi/ath6k/AR6003/hw2.0/otp.bin.z77 \
    vendor/samsung/cooper/proprietary/wifi/firmware/ath6k/AR6003/hw2.0/athwlan.bin.z77:system/etc/firmware/ath6k/AR6003/hw2.0/athwlan.bin.z77 \
    vendor/samsung/cooper/proprietary/wifi/firmware/ath6k/AR6003/hw2.0/athtcmd_ram.bin:system/etc/firmware/ath6k/AR6003/hw2.0/athtcmd_ram.bin \
    vendor/samsung/cooper/proprietary/wifi/firmware/ath6k/AR6003/hw2.0/bdata.SD31.bin:system/etc/firmware/ath6k/AR6003/hw2.0/bdata.SD31.bin \
    vendor/samsung/cooper/proprietary/wifi/firmware/ath6k/AR6003/hw2.0/bdata.SD31.bin.04:system/etc/firmware/ath6k/AR6003/hw2.0/bdata.SD31.bin.04 \
    vendor/samsung/cooper/proprietary/wifi/firmware/ath6k/AR6003/hw2.0/data.patch.bin:system/etc/firmware/ath6k/AR6003/hw2.0/data.patch.bin \
    vendor/samsung/cooper/proprietary/wifi/firmware/ath6k/AR6003/hw2.0/otp.bin.z77:system/etc/firmware/ath6k/AR6003/hw2.0/otp.bin.z77 \
    vendor/samsung/cooper/proprietary/wifi/firmware/ath6k/AR6003/hw2.0/samsung_athwlan.bin.z77:system/etc/firmware/ath6k/AR6003/hw2.0/samsung_athwlan.bin.z77 \
    vendor/samsung/cooper/proprietary/wifi/firmware/ath6k/AR6003/hw2.0/samsung_athtcmd_ram.bin:system/etc/firmware/ath6k/AR6003/hw2.0/samsung_athtcmd_ram.bin \
    vendor/samsung/cooper/proprietary/wifi/firmware/ath6k/AR6003/hw2.0/samsung_data.patch.bin:system/etc/firmware/ath6k/AR6003/hw2.0/samsung_data.patch.bin \
    vendor/samsung/cooper/proprietary/wifi/firmware/ath6k/AR6003/hw2.0/samsung_otp.bin.z77:system/etc/firmware/ath6k/AR6003/hw2.0/samsung_otp.bin.z77
