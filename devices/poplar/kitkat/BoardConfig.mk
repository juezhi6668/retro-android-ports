# Poplar (G8341/G8342) BoardConfig for Android KitKat (4.4.4)
# SKELETON — values from Sony yoshino-common + android_device_sony_poplar
# (ten/lineage-18.1 branches); KitKat adaptations are TODOs.
# Reference:
#   device/sony/yoshino-common/BoardConfigPlatform.mk (msm8998, 4.4 kernel)
#   device/sony/poplar/BoardConfig.mk (boot 64MB, density 400)

LOCAL_PATH := $(call my-dir)

TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true
TARGET_PROVIDES_INIT_RC := true

# poplar uses GPT/ext4, NOT BML/stl. Fastboot-flashable boot.img (64 MB).
# Stock/Reference geometry (yoshino-common):
TARGET_PREBUILT_KERNEL := vendor/sony/poplar/kernel
BOARD_KERNEL_BASE := 0x00000000
BOARD_KERNEL_PAGESIZE := 4096
BOARD_KERNEL_IMAGE_NAME := Image.gz-dtb
# Cmdline from the live Lineage boot.img (abootimg config, TWRP dump
# 2026-10-13) plus androidboot.hardware=poplar. base 0x00000000 confirmed
# (kerneladdr 0x8000), pagesize 4096 confirmed.
BOARD_KERNEL_CMDLINE := ehci-hcd.park=3 lpm_levels.sleep_disabled=1 sched_enable_hmp=0 sched_enable_power_aware=1 service_locator.enable=1 swiotlb=2048 androidboot.configfs=true androidboot.usbcontroller=a800000.dwc3 loop.max_part=7 androidboot.hardware=poplar
BOARD_BOOTIMAGE_PARTITION_SIZE := 67108864
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 67108864
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 947912704
BOARD_CACHEIMAGE_PARTITION_SIZE := 398458880
BOARD_FLASH_BLOCK_SIZE := 131072
# GPT/ext4 device (not MTD/yaffs2): without this KitKat builds a yaffs2
# system.img (seen 2026-10-13: mkyaffs2image ran). ext4 + sparse is what
# fastboot flashes on poplar.
TARGET_USERIMAGES_USE_EXT4 := true
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_USERDATAIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
# TODO: confirm KitKat mkbootimg handles base-0 + Image.gz-dtb layout.
# KitKat-era mkbootimg predates dtb-appended kernels; likely needs a
# backport (cf. patches/donut/mkbootimg-pagesize.patch precedent).

# Platform: DELIBERATELY `poplar`, not `msm8998` (cooper precedent:
# TARGET_BOARD_PLATFORM := cooper kept Donut's hardware/msm7k out).
# KitKat's hardware/qcom/* targets msm8960/8974-era HALs; nothing knows
# msm8998 + Adreno 540 + 4.4 kernel. Set `msm8998` only when a QCOM HAL
# path is proven; until then use generic audio/camera/gps + software EGL.
TARGET_BOARD_PLATFORM := poplar
TARGET_BOARD_PLATFORM_GPU := qcom-adreno540

# the goldfish gl libs are the guest half of glrenderd (tools/glrenderd), which replays them on
# the adreno 540 from lineage's chroot. see patches/kitkat/*glrenderd*
BUILD_EMULATOR_OPENGL := true

# kitkat only preopts user builds by default. without it first boot dexopts every jar and apk
# on the phone, and the boot animation hides that for many minutes
WITH_DEXPREOPT := true

# BLOCKER #1 — 32-bit userspace on a 64-bit SoC:
# KitKat (API 19) has NO arm64 support (TARGET_ARCH arm64 + TARGET_2ND_ARCH
# landed in Lollipop 5.0). MSM8998 (Kryo 280) is ARM64. First-bringup path
# is 32-bit KitKat userspace (armeabi-v7a) on the Sony 4.4 kernel in
# 32/64 hybrid or arm32 mode — feasibility spike required before any HAL
# work. Do NOT add TARGET_ARCH := arm64 until the tree moves to L+.
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
# CPU variant: cortex-a15 (HW 64-bit atomics; Kryo 280 is a superset in
# 32-bit mode). An earlier cortex-a9 setting was suspected of forcing
# kuser-helper atomics (absent on ARM64 kernels), but live testing showed
# on-boot 32-bit processes run — the TWRP-side SEGV was environmental
# (missing /system/bin/linker there). a15 stays as the correct variant.
TARGET_CPU_VARIANT := cortex-a15
TARGET_CPU_SMP := true

# Assert strings used by recovery updater-script
TARGET_OTA_ASSERT_DEVICE := poplar,poplar_dsds,G8341,G8342

# Audio/camera/gps: generic until ported (see docs/devices/poplar.md).
# KitKat equivalents of cooper's HAVE_HTC_AUDIO_DRIVER / USE_CAMERA_STUB
# decisions live here once measured:
#   BOARD_USES_GENERIC_AUDIO := true   # TODO: verify vs qcom audio HAL
#   USE_CAMERA_STUB := true            # TODO: flip when camera HAL lands
BOARD_HAVE_BLUETOOTH := true

# Wifi (BCM43xx on poplar/yoshino, NOT ath6kl):
# Reference uses qcwcn/NL80211. KitKat ships hardware/qcom/wlan/qcwcn
# (lib_driver_cmd_qcwcn provides wpa_driver_nl80211_driver_cmd + the
# ANDROID_P2P driver hooks driver_nl80211.c declares). qcwcn stays ON:
# without it the supplicant link fails (first expiry: p2p/driver_cmd
# undefined refs; generic no-driver config fails differently on sme_*).
# Wifi itself still won't WORK until the driver/firmware port lands
# (blocker 5) — this only keeps the supplicant linkable, cooper-style.
BOARD_WLAN_DEVICE := qcwcn
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_qcwcn
WPA_SUPPLICANT_VERSION := VER_0_8_X
WIFI_DRIVER_FW_PATH_STA := "sta"
WIFI_DRIVER_FW_PATH_AP := "ap"
WIFI_DRIVER_FW_PATH_P2P := "p2p"

# Display: 5.2" 1080x1920 (~424 ppi) IPS LCD, single-DSI command-mode panel.
# TARGET_SCREEN_DENSITY is deliberately unset; the live port runs at 430
# (measured, see system.prop) — cf. cooper's 120-vs-160 lesson: measure,
# don't guess.
