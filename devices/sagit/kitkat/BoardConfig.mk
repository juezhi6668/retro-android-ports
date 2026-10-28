# Sagit (Xiaomi Mi 6) BoardConfig for Android KitKat (4.4.4)
# Ported from devices/maple/kitkat (Sony XZ Premium) — same MSM8998 SoC,
# Adreno 540 and 4.4 kernel, so facts marked "maple" were verified on that
# phone and carry over; everything marked verify-me must be checked against a
# dump of THIS phone before the first flash (scripts/dump-sagit-stock.sh,
# docs/devices/sagit/quirks.md).
# Reference trees: LineageOS device/xiaomi/sagit +
# android_kernel_xiaomi_msm8998 (4.4, arch/arm64/configs/sagit_defconfig).

LOCAL_PATH := $(call my-dir)

TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true
TARGET_PROVIDES_INIT_RC := true
# lights.msm8998 (vendor/xiaomi/sagit/lights) is the lights HAL; stop the build
# from adding the generic default
TARGET_PROVIDES_LIBLIGHT := true

# GPT/ext4 UFS, fastboot-flashable boot.img (64 MB). Same kernel class as
# maple: Image.gz-dtb, base 0, pagesize 4096 (sagit_defconfig is 4.4).
TARGET_PREBUILT_KERNEL := vendor/xiaomi/sagit/kernel
BOARD_KERNEL_BASE := 0x00000000
BOARD_KERNEL_PAGESIZE := 4096
BOARD_KERNEL_IMAGE_NAME := Image.gz-dtb
# Cmdline: take the REAL one from the dumped Lineage boot.img before judging
# any boot failure — `abootimg -x boot.img` prints it. androidboot.hardware
# must be msm8998, not the board name: it is what KitKat init imports
# (/init.${ro.hardware}.rc), what vold reads (/fstab.${ro.hardware}), what
# ueventd reads (/ueventd.${ro.hardware}.rc) and the first key hw_get_module
# tries (*.msm8998.so) — shared by every msm8998 board here.
# service_locator.enable=1 is required for WLAN (icnss learns the modem's
# wlan/fw domain through the servreg locator — maple TEST 39).
# androidboot.configfs=true is the 4.4 USB path: init.msm8998.usb.rc builds the
# gadget, the legacy android_usb sysfs nodes do not exist on this kernel.
BOARD_KERNEL_CMDLINE := ehci-hcd.park=3 lpm_levels.sleep_disabled=1 sched_enable_hmp=0 sched_enable_power_aware=1 service_locator.enable=1 swiotlb=2048 androidboot.configfs=true androidboot.usbcontroller=a800000.dwc3 loop.max_part=7 androidboot.hardware=msm8998

BOARD_BOOTIMAGE_PARTITION_SIZE := 67108864
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 67108864
# measured on the phone 2026-10-24 (dumps/sagit-stock-*): boot sde40 64 MiB,
# recovery sda15 64 MiB, system sda18 9 GiB, cache sda16 256 MiB, persist sda14
# 64 MiB. The system image is declared at 3 GiB, which fits the 9 GiB partition
# with room to spare; the declared size is what sizes the ext4 image.
BOARD_BOOTIMAGE_PARTITION_SIZE := 67108864
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 67108864
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 3221225472
BOARD_CACHEIMAGE_PARTITION_SIZE := 268435456
BOARD_FLASH_BLOCK_SIZE := 131072
# androidboot.hardware is NOT appended by the bootloader (the stock boot.img
# carries it and /proc/cmdline shows one copy), so hardware=msm8998 selects
# init.msm8998.rc. The bootloader does append mdss_mdp.panel=... for the JDI
# r63452 panel; nothing to do about that on our side.
# GPT/ext4 device (not MTD/yaffs2): without this KitKat builds a yaffs2
# system.img (maple 2026-10-13: mkyaffs2image ran). system.img builds sparse —
# unsparsify with simg2img before dd.
TARGET_USERIMAGES_USE_EXT4 := true
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_USERDATAIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4

# Platform: DELIBERATELY the board name, not `msm8998` (cooper/maple
# precedent): this is the device identity (ro.product.board) and KitKat's
# hardware/qcom/* only knows msm8960/8974-era HALs, so it must never select
# one. HAL module lookup does not go through it — the shared modules
# (gralloc/lights/audio.primary) resolve via ro.hardware=msm8998 first (the
# cmdline above). Graphics is not a generic path: libhyb loads the real
# Adreno 540 driver (below).
TARGET_BOARD_PLATFORM := sagit
TARGET_BOARD_PLATFORM_GPU := qcom-adreno540

# Graphics: the real Adreno 540 driver (Lineage 22.1 / Android 15 vendor
# closure) is loaded into every KitKat process by libhyb
# (devices/sagit/kitkat/{hyb,egl,gralloc}); libGLES_android stays as the
# software fallback. egl.cfg keeps the stock layout; the EGL loader finds
# libEGL_adreno.so in /system/lib/egl by naming convention. Do NOT set
# ro.kernel.qemu anywhere: its presence switches the loader to emulation and
# disables hwui.
USE_OPENGL_RENDERER := true
BOARD_EGL_CFG := vendor/xiaomi/sagit/egl.cfg

# kitkat only preopts user builds by default; without it first boot dexopts
# every jar/apk on the phone and the boot animation hides it for minutes
WITH_DEXPREOPT := true

# BLOCKER #1 (maple, resolved): KitKat (API 19) has no arm64 support. Mi 6 is
# ARM64 (Kryo 280); this port is 32-bit userspace on the 4.4 arm64 kernel via
# CONFIG_COMPAT (sagit_defconfig has CONFIG_COMPAT=y). Do NOT add
# TARGET_ARCH := arm64 until the tree moves to L+.
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := cortex-a15
TARGET_CPU_SMP := true

# Assert string used by the recovery updater-script
TARGET_OTA_ASSERT_DEVICE := sagit

# Audio: tinyalsa HAL in vendor/xiaomi/sagit/audio (audio.primary.msm8998).
# Mi 6 is WCD9341 ("tasha") through the ADSP, so the card is
# msm8998-tasha-snd-card — the card name maple's HAL already prefers (maple
# runs tavil and tasha side by side; tasha is the one that registers there).
# Mi 6 has no WSA881x speaker amps; the speaker route is the same
# SLIMBUS_0_RX/MultiMedia1 one. ACDB calibration comes from the Mi 6 ROM via
# tools/libhyb/stage.py; adsp firmware (modem partition `image/adsp.*`) is
# staged out-of-git (full_sagit.mk).
#   BOARD_USES_GENERIC_AUDIO intentionally unset so AudioFlinger links
#   audio.primary.msm8998 instead of the silent stub.
#   USE_CAMERA_STUB true for first bringup: the Mi 6 camera is a QTI 64-bit
#   closure with no KitKat HAL, and camera_service otherwise waits on it.
BOARD_HAVE_BLUETOOTH := true
USE_CAMERA_STUB := true

# Wifi: Qualcomm WCN3990 (qca_cld3 built into the 4.4 kernel, same as maple;
# verify: `zcat /proc/config.gz | grep QCA_CLD_WLAN`). KitKat ships
# hardware/qcom/wlan/qcwcn; its lib_driver_cmd_qcwcn is what lets KitKat's
# driver_nl80211 link at all (maple "wpa_supplicant saga"). Driver is
# built-in, so no WIFI_DRIVER_MODULE_*: wifi.c treats it as always loaded and
# the qca_cld3 fwpath parameter takes sta/ap/p2p from wifi_change_fw_path.
BOARD_WLAN_DEVICE := qcwcn
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_qcwcn
WPA_SUPPLICANT_VERSION := VER_0_8_X
WIFI_DRIVER_FW_PATH_STA := "sta"
WIFI_DRIVER_FW_PATH_AP := "ap"
WIFI_DRIVER_FW_PATH_P2P := "p2p"

# Display: 5.15" 1080x1920 IPS. Unlike maple's 4K XZ Premium there is no 4K
# mode: the panel is 1080p (dual-source JDI r63452 / LGD td4322, picked by
# panel id in the DT). Density 480 is stock Mi 6 (ro.sf.lcd_density=480).
