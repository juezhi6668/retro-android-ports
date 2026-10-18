# Maple (Sony Xperia XZ Premium G8141/G8142) BoardConfig for Android Donut (1.6)
# Modeled on devices/cooper/donut/BoardConfig.mk (the boot-verified Donut port)
# and devices/maple/kitkat/BoardConfig.mk (the boot-verified KitKat port on
# this exact device). Donut is 32-bit-only armeabi (armv5te) — see the Donut
# porting notes for why that is a feature here (it is the era-correct ABI).
#
# Flash geometry: GPT/ext4, NOT BML/stl (cooper). Raw node numbers (from the
# KitKat port's TWRP dump, stable GPT): system->sde46, userdata->sda15,
# cache->sda5, persist->sda4, boot->sde35, recovery->sde36.

LOCAL_PATH := $(call my-dir)

TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true
# Our vendor tree ships ramdisk root/init.rc (stock Donut init.rc adapted for
# maple); tell system/core/rootdir not to install its own.
TARGET_PROVIDES_INIT_RC := true

# Kernel: TWRP-dumped Sony whatawurst 4.4.302 Image.gz-dtb (out-of-git blob,
# same one the KitKat port runs; already patched with scripts/patch-maple-dtb.py
# — mdss idle-power-collapse + wcn-btfm stripped). mkbootimg handles the
# appended DTB as an opaque kernel blob. Base 0 / pagesize 4096 confirmed from
# the live boot.img (kerneladdr 0x8000, tags 0x100).
TARGET_PREBUILT_KERNEL := vendor/sony/maple/kernel
BOARD_KERNEL_BASE := 0x00000000
BOARD_KERNEL_PAGESIZE := 4096
# Cmdline from the live KitKat boot.img (verified booting on this device):
# androidboot.hardware=maple drives Donut init's /init.<hardware>.rc load.
BOARD_KERNEL_CMDLINE := ehci-hcd.park=3 lpm_levels.sleep_disabled=1 sched_enable_hmp=0 sched_enable_power_aware=1 service_locator.enable=1 swiotlb=2048 androidboot.configfs=true androidboot.usbcontroller=a800000.dwc3 loop.max_part=7 androidboot.hardware=maple
BOARD_BOOTIMAGE_PARTITION_SIZE := 67108864
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 67108864
BOARD_FLASH_BLOCK_SIZE := 131072

# Donut's build system only knows how to make yaffs2 system images
# (mkyaffs2image) and pagesize-2048 boot images. Neither fits maple: the
# device is ext4/GPT and the kernel needs pagesize 4096. Both images are
# therefore packed by scripts/build-maple-donut-image.py using the chroot
# build outputs (system/ tree, ramdisk.img) + tools/mkbootimg-cooper
# (the --pagesize patched Donut mkbootimg). Do NOT use `make systemimage`.
# Informational sizes for the packaging script (partition sizes from
# devices/maple/kitkat/BoardConfig.mk):
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 5242880000
BOARD_USERDATAIMAGE_PARTITION_SIZE := 0
BOARD_CACHEIMAGE_PARTITION_SIZE := 398458880

# Platform: DELIBERATELY `maple`, not `msm8998` (cooper precedent:
# TARGET_BOARD_PLATFORM := cooper kept Donut's hardware/msm7k HALs out; they
# need proprietary Dream-era headers absent from Donut's bionic).
TARGET_BOARD_PLATFORM := maple
TARGET_CPU_ABI := armeabi
TARGET_CPU_ABI2 := armeabi

# Camera: Donut's CameraHardwareStub (frameworks/base/camera/libcameraservice)
# keeps CameraService alive; no V4L2 HAL for maple yet.
USE_CAMERA_STUB := true

# Dalvik heap: Donut's AndroidRuntime defaults to -Xmx16m, far too small for a
# 1080x1920 device — LockPatternView's 28 KB bitmaps overflow the
# external-alloc budget and crash system_server ("bitmap size exceeds VM
# budget"). Raise the max heap to 512 m (KitKat port's
# phone-xhdpi-2048-dalvik-heap.mk recipe). The dalvik.vm.heap* props in
# system.prop are inert on Donut (no code reads them); this flag is what
# actually drives -Xmx via CUSTOM_RUNTIME_HEAP_MAX.
USE_CUSTOM_RUNTIME_HEAP_MAX := 512m

# Audio: AudioHardwareGeneric -> /dev/eac does not exist on maple, so
# AudioFlinger falls back to the silent stub (AudioHardwareInterface::create
# checks initCheck() and swaps in AudioHardwareStub). Nothing crashes;
# audible audio needs a tinyalsa HAL against the tasha card (KitKat's ADSP
# stall workstream applies here too) — future work, documented in quirks.md.
BOARD_USES_GENERIC_AUDIO := true

# No Bluetooth bringup on Donut (kernel is configfs/btqca; Donut ships
# BlueZ 3.x which has no maple transport) — leave BOARD_HAVE_BLUETOOTH unset.

# No WiFi: same kernel wall as KitKat (qca_cld3 needs the WLFW/QMI handshake
# from a modem that never boots on this port). No driver flags on Donut.

# No RIL: there is no Donut RIL library for MSM8998; rild is disabled in
# init.rc (cooper precedent — rild without its .so crash-loops).

# Assert strings used by the (future) recovery updater-script
TARGET_OTA_ASSERT_DEVICE := maple,maple_dsds,G8141,G8142

# Display: 1080x1920 panel (4K panel rendered at 1080p), density 410 from the
# KitKat port's live measurement. Donut's EGLDisplaySurface requests RGB565 +
# double-buffer + FBIOPUT_VSCREENINFO flips — all verified working on this
# mdss fb0 (stride stays 4352 B; EGLDisplaySurface divides line_length by bpp).
