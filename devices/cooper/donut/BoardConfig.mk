# Cooper (GT-S5830) BoardConfig for Android Donut (1.6)
# Ported from LineageOS gb-release-7.2 device/samsung/cooper/BoardConfig.mk
# Adapted for Donut build system (armeabi/armv5te, no ext4/egl-config hooks).

LOCAL_PATH := $(call my-dir)

TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true
# Our vendor tree ships ramdisk root/init.rc (Donut stock init.rc + stl mounts
# + cooper import); tell system/core/rootdir not to install its own.
TARGET_PROVIDES_INIT_RC := true

# Cooper uses Samsung BML/stl, 8MB boot/recovery (bml8/bml9)
TARGET_PREBUILT_KERNEL := vendor/samsung/cooper/kernel
BOARD_KERNEL_BASE := 0x13600000
BOARD_KERNEL_PAGESIZE := 4096
BOARD_KERNEL_CMDLINE :=
BOARD_BOOTIMAGE_PARTITION_SIZE := 8388608
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 8388608
BOARD_FLASH_BLOCK_SIZE := 131072
BOARD_BML_BOOT := /dev/block/bml8
BOARD_BML_RECOVERY := /dev/block/bml9

# The MSM7227 is msm7k-family, but Donut's hardware/msm7k HALs (libcamera,
# librpc, ...) are Dream-era code needing proprietary camera/rpc headers
# absent from Donut bionic, and every module defaults to the `user` install
# tag so they all build. Use our own platform name so none of them are
# defined; cooper ships its own libaudio below + the camera stub.
TARGET_BOARD_PLATFORM := cooper
TARGET_CPU_ABI := armeabi
TARGET_CPU_ABI2 := armeabi
# Cooper audio: vendor/samsung/cooper/libaudio builds libaudio talking
# directly to the cooper kernel's /dev/msm_snd + /dev/msm_pcm_out/in
# (same ioctls as Donut msm7k + CM7 cooper libaudio, minus the Dream-only
# libhtc_acoustic.so). Generic (/dev/eac) audio is HTC Dream-only and
# always falls back to the silent stub on cooper, so it stays off.
HAVE_HTC_AUDIO_DRIVER := true
# BOARD_USES_GENERIC_AUDIO intentionally unset: AudioFlinger then links
# vendor cooper libaudio via createAudioHardware() instead of /dev/eac.
# Camera: cooper ships vendor/samsung/cooper/libcamera, a minimal V4L2 HAL
# (CooperCamera) with test-pattern fallback so CameraService never crashes
# when /dev/video0 is absent. Dream-era hardware/msm7k camera HALs are still
# out (TARGET_BOARD_PLATFORM=cooper keeps them unbuilt; they need proprietary
# camera/rpc headers absent from Donut bionic).
USE_CAMERA_STUB := false

# Assert strings used by recovery updater-script
TARGET_OTA_ASSERT_DEVICE := cooper,GT-S5830

# Bluetooth (BCM2049) / FM radio flags kept for documentation;
# Donut BlueZ stack consumes these via vendor init, not build-time HAL.
BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_FM_RADIO := true

# Wifi (Atheros AR6003 on SDIO, interface wlan0)
# NOTE: Donut's wpa_supplicant is 0.5.11 with WEXT only (no AWEXT backend;
# AWEXT is Gingerbread 0.6/0.7-only). -Dwext drives the stack via cfg80211's
# wext compat.
BOARD_WPA_SUPPLICANT_DRIVER := WEXT
BOARD_WLAN_DEVICE := ath6kl
# File logging for wpa_supplicant (external/wpa_supplicant/Android.mk only
# compiles -f support when CONFIG_DEBUG_FILE is set; .config leaves it off).
# The daemon's stdout goes to /dev/null under init and 0.5.11 has no logcat
# transport, so without this its driver errors are invisible. Log lands in
# /data/misc/wifi (pullable).
CONFIG_DEBUG_FILE := y
# Mainline ath6kl + cfg80211/compat deps (exact-vermagic OmniROM builds for
# the shipped kernel; deps pre-insmodded from init.rc, WifiService loads
# ath6kl itself). The legacy ar6000.ko (CM-kernel vermagic) PANICS this
# kernel at toggle and must stay unreferenced (kept in-tree for docs only).
WIFI_DRIVER_MODULE_PATH := /system/lib/modules/ath6kl.ko
WIFI_DRIVER_MODULE_ARG := ""
WIFI_DRIVER_MODULE_NAME := ath6kl

BOARD_MOBILEDATA_INTERFACE_NAME := pdp0
# NOTE: no BOARD_GPS_LIBRARIES on Donut — libloc_api is a Gingerbread-era
# proprietary lib with no Donut build rule; Donut builds generic gps.cpp.

# HVGA display of the Ace: 320x480, density 160 (mdpi, matches stock,
# CM7 cooper device_cooper.mk PRODUCT_PROPERTY_OVERRIDES and TWRP default.prop).
# The framebuffer itself reports ~159-160dpi (51x76mm), so 120 made everything tiny.
# See system.prop for lcd_density=160.
