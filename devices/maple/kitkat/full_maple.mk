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
    vendor/sony/maple/ueventd.maple.rc:root/ueventd.maple.rc \
    vendor/sony/maple/autowinch.sh:root/sbin/autowinch.sh \
    vendor/sony/maple/dump-boot-log.sh:system/bin/dump-boot-log.sh
# TODO proprietary wifi/firmware (BCM43xx + qcwcn) once extracted:
#   vendor/sony/maple/proprietary/...:/system/...
