# Product definition: full_poplar (Sony Xperia XZ1, KitKat 4.4.4)
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

# Dalvik heap: 4 GB RAM, 430 dpi 1080p panel → the 2048 bucket
# (heapgrowthlimit 192m, heapsize 512m). Without this the build ships
# Dalvik's tiny defaults and system_server OOMs verifying the 11.8 MB
# framework-res.apk on first boot (seen 2026-10-15: OutOfMemoryError in
# JarUtils.verifySignature every life, dalvik-cache never populates).
$(call inherit-product, frameworks/native/build/phone-xhdpi-2048-dalvik-heap.mk)

DEVICE_PACKAGE_OVERLAYS := vendor/sony/poplar/overlay

PRODUCT_NAME := full_poplar
PRODUCT_DEVICE := poplar
PRODUCT_MODEL := G8341
PRODUCT_BRAND := sony
PRODUCT_MANUFACTURER := Sony

# poplar-specific files (copied into system/ and root/ by the build system).
# Kernel is a TODO: either prebuilt Sony msm8998 4.4 Image.gz-dtb or a
# locally built kernel/sony/msm8998 output.
PRODUCT_COPY_FILES += \
    vendor/sony/poplar/system.prop:system/build.prop \
    vendor/sony/poplar/fstab.poplar:root/fstab.poplar \
    vendor/sony/poplar/init.rc:root/init.rc \
    vendor/sony/poplar/init.poplar.rc:root/init.poplar.rc \
    vendor/sony/poplar/init.poplar.usb.rc:root/init.poplar.usb.rc \
    vendor/sony/poplar/usbudc.sh:root/sbin/usbudc.sh \
    vendor/sony/poplar/ueventd.poplar.rc:root/ueventd.poplar.rc \
    vendor/sony/poplar/displaytune.sh:root/sbin/displaytune.sh \
    vendor/sony/poplar/autowinch.sh:root/sbin/autowinch.sh \
    vendor/sony/poplar/dump-boot-log.sh:system/bin/dump-boot-log.sh \
    vendor/sony/poplar/glrenderd.sh:root/sbin/glrenderd.sh \
    vendor/sony/poplar/gpio-keys.kl:system/usr/keylayout/gpio-keys.kl \
    vendor/sony/poplar/clearpad.idc:system/usr/idc/clearpad.idc
# TODO proprietary wifi/firmware (BCM43xx + qcwcn) once extracted:
#   vendor/sony/poplar/proprietary/...:/system/...

# glrenderd's guest side. gralloc.poplar and the egl loader fall back to gralloc.default and
# libagl whenever glrenderd isn't there
PRODUCT_PACKAGES += \
    lights.poplar \
    libEGL_emulation \
    libGLESv1_CM_emulation \
    libGLESv2_emulation \
    gralloc.poplar \
    gralloc.default

# glrenderd's chroot from tools/glrenderd/stage.py. it lives outside vendor/sony/poplar so
# sync-device-tree.sh's rsync --delete leaves it alone. the a540 firmware also goes where
# kitkat's ueventd looks, in case firmware_class.path ever misses
GLR_DIR := vendor/sony/poplar-glr
ifneq ($(wildcard $(GLR_DIR)/root/glrenderd),)
PRODUCT_COPY_FILES += \
    $(foreach f,$(shell cd $(GLR_DIR) && find . -type f -printf '%P\n'),$(GLR_DIR)/$(f):system/glr/$(f)) \
    $(foreach f,$(shell cd $(GLR_DIR)/root/vendor/firmware && ls),$(GLR_DIR)/root/vendor/firmware/$(f):system/vendor/firmware/$(f))
endif
