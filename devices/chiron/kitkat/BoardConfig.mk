# Chiron (Xiaomi Mi MIX 2) BoardConfig for Android KitKat (4.4.4)
# The Mi MIX 2 is the Mi 6's sibling: same MSM8998, Adreno 540, WCN3990,
# Synaptics touch, tasha (WCD9341) codec. Everything shared (graphics
# closure, audio HAL sources, libhyb, fbpresentd, pdserve, init.rc and the
# shell scripts) lives in vendor/xiaomi/sagit; only the board-specific
# configs and the speaker amp firmware differ (TAS2559 instead of TFA9888).

LOCAL_PATH := $(call my-dir)

TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true
TARGET_PROVIDES_INIT_RC := true
# lights.msm8998 (vendor/xiaomi/sagit/lights) is the lights HAL; stop the
# build from adding the generic default
TARGET_PROVIDES_LIBLIGHT := true

# Kernel: the user-built 4.4.302 Image.gz-dtb extracted from the chiron
# boot.img supplied with the OS3 dump. It is multi-DTB (chiron board-id 0x20,
# tas2559) so the same blob also serves sagit. Header of that boot.img:
# kernel 0x8000, ramdisk 0x1000000, tags 0x100, page 4096 — exactly what
# KitKat's mkbootimg emits for base 0, so the built boot.img is flashable.
TARGET_PREBUILT_KERNEL := vendor/xiaomi/chiron/kernel
BOARD_KERNEL_BASE := 0x00000000
BOARD_KERNEL_PAGESIZE := 4096
BOARD_KERNEL_IMAGE_NAME := Image.gz-dtb
# Cmdline: the stock chiron one (dumped from OS3/boot.img) with
# androidboot.hardware switched to msm8998, the name the shared userspace uses:
# KitKat init imports /init.${ro.hardware}.rc, vold reads /fstab.${ro.hardware},
# ueventd reads /ueventd.${ro.hardware}.rc and hw_get_module tries
# *.msm8998.so first. configfs and the dwc3 controller name drive the 4.4 USB
# gadget in init.msm8998.usb.rc;
# service_locator is required for WLAN (icnss), swiotlb for the 4.4 ion/dma
# path, loop.max_part for the 7-partition loop devices.
BOARD_KERNEL_CMDLINE := androidboot.console=ttyMSM0 msm_rtb.filter=0x37 ehci-hcd.park=3 service_locator.enable=1 swiotlb=2048 loop.max_part=7 sched_enable_hmp=0 sched_enable_power_aware=1 androidboot.configfs=true androidboot.usbcontroller=a800000.dwc3 androidboot.hardware=msm8998

BOARD_BOOTIMAGE_PARTITION_SIZE := 67108864
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 67108864
# Same GPT family as sagit; system is declared at 3 GiB which fits chiron's
# system partition (>= 3 GiB on every Mi MIX 2 variant; the partition table
# is dumped by TWRP as /dev/block/by-name/system).
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 3221225472
BOARD_CACHEIMAGE_PARTITION_SIZE := 268435456
BOARD_FLASH_BLOCK_SIZE := 131072
TARGET_USERIMAGES_USE_EXT4 := true
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_USERDATAIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4

# Platform: `chiron`, same reasoning as sagit — this is the device identity
# (ro.product.board), KitKat's hardware/qcom has no path for it, and HAL module
# lookup goes through ro.hardware=msm8998 instead.
TARGET_BOARD_PLATFORM := chiron
TARGET_BOARD_PLATFORM_GPU := qcom-adreno540

# Graphics: reuses sagit's Adreno 540 closure and the QTI gralloc1; egl.cfg
# is shared.
USE_OPENGL_RENDERER := true
BOARD_EGL_CFG := vendor/xiaomi/sagit/egl.cfg

WITH_DEXPREOPT := true

TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := cortex-a15
TARGET_CPU_SMP := true

TARGET_OTA_ASSERT_DEVICE := chiron

# Audio: shared tinyalsa HAL source, built as audio.primary.msm8998 with
# -DAMP_TAS2559 for this product (vendor/xiaomi/sagit/audio/Android.mk). Card is
# msm8998-tasha-snd-card; the speaker amp is a TAS2559 on QUAT_MI2S
# (kernel DAI link msm_quat_mi2s_tas2559_dai_links); ACDB data (ChironS +
# Forte) is staged under vendor/xiaomi/chiron-hyb.
BOARD_HAVE_BLUETOOTH := true
USE_CAMERA_STUB := true

# Wifi: WCN3990, qca_cld3 built into the 4.4 kernel (chiron_defconfig:
# CONFIG_QCA_CLD_WLAN=y). KitKat's qcwcn supplicant glue as on sagit.
BOARD_WLAN_DEVICE := qcwcn
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_qcwcn
WPA_SUPPLICANT_VERSION := VER_0_8_X
WIFI_DRIVER_FW_PATH_STA := "sta"
WIFI_DRIVER_FW_PATH_AP := "ap"
WIFI_DRIVER_FW_PATH_P2P := "p2p"

# Display: 5.99" 1080x2160 IPS (18:9), stock density 440
# (system.prop ro.sf.lcd_density=440).
