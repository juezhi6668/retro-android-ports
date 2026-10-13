# Cooper (GT-S5830, MSM7227) libaudio for Donut.
# Donut msm7k AudioHardware minus the Dream-only libhtc_acoustic.so
# dependency: endpoints via /dev/msm_snd ioctls, plain PCM out/in.
# Built as libaudio because TARGET_BOARD_PLATFORM=cooper keeps
# hardware/msm7k out of the build, and BOARD_USES_GENERIC_AUDIO is off,
# so AudioFlinger links this via createAudioHardware().
ifneq ($(BUILD_TINY_ANDROID),true)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libaudio

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libmedia \
    libhardware_legacy

ifneq ($(TARGET_SIMULATOR),true)
LOCAL_SHARED_LIBRARIES += libdl
endif

LOCAL_SRC_FILES += AudioHardware.cpp

LOCAL_CFLAGS += -fno-short-enums

LOCAL_STATIC_LIBRARIES += libaudiointerface

include $(BUILD_SHARED_LIBRARY)

endif # not BUILD_TINY_ANDROID
