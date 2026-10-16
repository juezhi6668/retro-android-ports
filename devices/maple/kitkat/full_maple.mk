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
    vendor/sony/maple/glrenderd.sh:root/sbin/glrenderd.sh \
    vendor/sony/maple/gpio-keys.kl:system/usr/keylayout/gpio-keys.kl \
    vendor/sony/maple/clearpad.idc:system/usr/idc/clearpad.idc
# TODO proprietary wifi/firmware (BCM43xx + qcwcn) once extracted:
#   vendor/sony/maple/proprietary/...:/system/...

# glrenderd's guest side. gralloc.maple and the egl loader fall back to gralloc.default and
# libagl whenever glrenderd isn't there
PRODUCT_PACKAGES += \
    libEGL_emulation \
    libGLESv1_CM_emulation \
    libGLESv2_emulation \
    gralloc.maple \
    gralloc.default \
    lights.maple

# glrenderd's chroot from tools/glrenderd/stage.py. it lives outside vendor/sony/maple so
# sync-device-tree.sh's rsync --delete leaves it alone. the a540 firmware also goes where
# kitkat's ueventd looks, in case firmware_class.path ever misses
GLR_DIR := vendor/sony/maple-glr
ifneq ($(wildcard $(GLR_DIR)/root/glrenderd),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(GLR_DIR) && find . -type f -printf '%P\n'),$(GLR_DIR)/$(f):system/glr/$(f)) \
    $(foreach f,$(shell cd $(GLR_DIR)/root/vendor/firmware && ls),$(GLR_DIR)/root/vendor/firmware/$(f):system/vendor/firmware/$(f))
endif
