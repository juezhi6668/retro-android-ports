# Cooper (GT-S5830) libcamera for Donut.
# Minimal V4L2 HAL with graceful fallback to a test pattern (never crashes
# CameraService): if /dev/video0 opens and streams YUYV we show real sensor
# frames, otherwise we generate a checkerboard like the AOSP stub. This keeps
# the Camera app alive on kernels without a V4L2 sensor while giving real
# preview where the sensor exposes V4L2.
# Built as libcamera because TARGET_BOARD_PLATFORM=cooper keeps Dream-era
# hardware/msm7k out of the build. Enabled by USE_CAMERA_STUB=false (see
# BoardConfig; still true by default until on-device V4L2 is confirmed).
ifeq ($(TARGET_BOARD_PLATFORM),cooper)
ifneq ($(BUILD_TINY_ANDROID),true)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := CooperCamera.cpp

LOCAL_C_INCLUDES += external/jpeg

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libui \
    libmedia

LOCAL_STATIC_LIBRARIES := libjpeg

LOCAL_CFLAGS := -fno-short-enums -O2 -Wall -Wno-unused-parameter

LOCAL_MODULE := libcamera

include $(BUILD_SHARED_LIBRARY)

endif # not BUILD_TINY_ANDROID
endif # cooper
