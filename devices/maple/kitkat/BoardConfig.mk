# Maple (G8141/G8142) BoardConfig for Android KitKat (4.4.4)
# SKELETON — values from Sony yoshino-common + android_device_sony_maple
# (ten/lineage-18.1 branches); KitKat adaptations are TODOs.
# Reference:
#   device/sony/yoshino-common/BoardConfigPlatform.mk (msm8998, 4.4 kernel)
#   device/sony/maple/BoardConfig.mk (boot 64MB, density 400)

LOCAL_PATH := $(call my-dir)

TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true
TARGET_PROVIDES_INIT_RC := true
# lights.maple (vendor/sony/maple/lights) is the lights HAL; stop the build
# from adding the generic default
TARGET_PROVIDES_LIBLIGHT := true

# maple uses GPT/ext4, NOT BML/stl. Fastboot-flashable boot.img (64 MB).
# Stock/Reference geometry (yoshino-common):
TARGET_PREBUILT_KERNEL := vendor/sony/maple/kernel
BOARD_KERNEL_BASE := 0x00000000
BOARD_KERNEL_PAGESIZE := 4096
BOARD_KERNEL_IMAGE_NAME := Image.gz-dtb
# Cmdline from the live Lineage boot.img (abootimg config, TWRP dump
# 2026-10-13) plus androidboot.hardware=maple. base 0x00000000 confirmed
# (kerneladdr 0x8000), pagesize 4096 confirmed.
# service_locator.enable=0 (was 1 from the Lineage cmdline): CHARQUIRKS
# audio ("the audio techpack's audio_notifier is forced to ssr mode,
# there's no pd-mapper for pdr when the card probes, so pdr never reports
# and the card never registers (it did after ~5 minutes with
# service_locator.enable=1, the cmdline sets 0)"). maple shows the same
# symptom live 2026-10-16 (no soundcards, silent ASoC deferral); flip to 0
# at this cmdline edit. verify-me: confirm msm8998-tavil-snd-card probes
# after reflash (`cat /proc/asound/cards`).
BOARD_KERNEL_CMDLINE := ehci-hcd.park=3 lpm_levels.sleep_disabled=1 sched_enable_hmp=0 sched_enable_power_aware=1 service_locator.enable=0 swiotlb=2048 androidboot.configfs=true androidboot.usbcontroller=a800000.dwc3 loop.max_part=7 androidboot.hardware=maple
# Wifi experiment REVERTED 2026-10-16 (TEST 37): icnss.quirks=388
# (SKIP_QMI) bootloops at the Sony logo — the HDD probe runs immediately
# but crashes without whatever the QMI WLFW handshake sets up (no pstore
# captured; ramoops doesn't survive the loop). The bypass engages (that's
# the good news: probe timing confirmed) but is not sufficient alone.
# Next wifi paths: (a) cnss-daemon in a 64-bit Lineage chroot to do the
# real QMI handshake (vendor/bin/cnss-daemon exists on Lineage; needs
# vndbinder/pm-service per CHARQUIRKS — same chroot pattern as
# sensors/glrenderd), or (b) modem boot (wlanmdsp.mbn is modem-side;
# modem never boots here — same wall as cellular). See TEST 37.
BOARD_BOOTIMAGE_PARTITION_SIZE := 67108864
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 67108864
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 5242880000
BOARD_CACHEIMAGE_PARTITION_SIZE := 398458880
BOARD_FLASH_BLOCK_SIZE := 131072
# GPT/ext4 device (not MTD/yaffs2): without this KitKat builds a yaffs2
# system.img (seen 2026-10-13: mkyaffs2image ran). ext4 + sparse is what
# fastboot flashes on maple.
TARGET_USERIMAGES_USE_EXT4 := true
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_USERDATAIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
# TODO: confirm KitKat mkbootimg handles base-0 + Image.gz-dtb layout.
# KitKat-era mkbootimg predates dtb-appended kernels; likely needs a
# backport (cf. patches/donut/mkbootimg-pagesize.patch precedent).

# Platform: DELIBERATELY `maple`, not `msm8998` (cooper precedent:
# TARGET_BOARD_PLATFORM := cooper kept Donut's hardware/msm7k out).
# KitKat's hardware/qcom/* targets msm8960/8974-era HALs; nothing knows
# msm8998 + Adreno 540 + 4.4 kernel. Set `msm8998` only when a QCOM HAL
# path is proven; until then use generic audio/camera/gps + software EGL.
TARGET_BOARD_PLATFORM := maple
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
TARGET_OTA_ASSERT_DEVICE := maple,maple_dsds,G8141,G8142

# Audio: tinyalsa HAL in vendor/sony/maple/audio (audio.primary.maple),
# written for this port per the CHARQUIRKS audio section (playback +
# capture + usb fallback, no acdb, decimator +20 dB). Card is
# msm8998-tavil-snd-card through the adsp (WCD9340 + WSA881x amps);
# adsp firmware (modem partition `image/adsp.*`) stages out-of-git on the
# build server (see full_maple.mk) and lands at /vendor/firmware where
# firmware_class.path points (CHARQUIRKS: a miss stalls ~60 s per file).
# HAVE_HTC_AUDIO_DRIVER / USE_CAMERA_STUB decisions:
#   BOARD_USES_GENERIC_AUDIO intentionally unset so AudioFlinger links
#   audio.primary.maple instead of the silent stub.
#   USE_CAMERA_STUB stays false (cooper-style minimal V4L2 HAL is future
#   work, not this change).
#   USE_CAMERA_STUB := true            # TODO: flip when camera HAL lands
BOARD_HAVE_BLUETOOTH := true

# Wifi (Qualcomm WCN3990/cherokee on maple/yoshino via qca_cld3, NOT
# BCM43xx and NOT ath6kl — earlier comment was wrong; corrected
# 2026-10-16 from the running kernel's /proc/config.gz:
# CONFIG_QCA_CLD_WLAN=y built-in, driver v5.1.1.77V, /dev/wlan chr 229,0;
# BT firmware crbtfw*/crnv* on the bluetooth partition confirms cherokee):
# Reference uses qcwcn/NL80211. KitKat ships hardware/qcom/wlan/qcwcn
# (lib_driver_cmd_qcwcn provides wpa_driver_nl80211_driver_cmd + the
# ANDROID_P2P driver hooks driver_nl80211.c declares). qcwcn stays ON:
# without it the supplicant link fails (first expiry: p2p/driver_cmd
# undefined refs; generic no-driver config fails differently on sme_*).
# CHARQUIRKS divergence: pdx203 leaves BOARD_WLAN_DEVICE unset (qcwcn
# there turns on p2p; they run plain wpa_supplicant). maple keeps qcwcn
# because KitKat's driver_nl80211 NEEDS the private lib to link at all
# (maple.md TEST "wpa_supplicant saga"); p2p is handled by defining both
# supplicant services and letting the framework choose, with
# android.hardware.wifi.direct.xml deliberately not shipped (same posture
# as CHARQUIRKS).
# No WIFI_DRIVER_MODULE_* (driver is built-in, wifi.c treats it as always
# loaded). The qca_cld3 fwpath param (/sys/module/wlan/parameters/fwpath)
# takes sta/ap/p2p from wifi.c wifi_change_fw_path.
BOARD_WLAN_DEVICE := qcwcn
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_qcwcn
WPA_SUPPLICANT_VERSION := VER_0_8_X
WIFI_DRIVER_FW_PATH_STA := "sta"
WIFI_DRIVER_FW_PATH_AP := "ap"
WIFI_DRIVER_FW_PATH_P2P := "p2p"

# Display: 5.5" 3840x2160 (801 ppi) IPS HDR. Sony renders 1080p normally,
# 4K only for HDR video/gallery. Reference TARGET_SCREEN_DENSITY := 400.
# KitKat skeleton default below is TBD — verify against the live
# framebuffer + stock default.prop before locking (see system.prop).
