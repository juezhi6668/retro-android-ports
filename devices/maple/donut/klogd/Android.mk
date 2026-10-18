# klogd - ramdisk kernel-log mirror for bringup (see klogd.c).
# Static executable in the ramdisk's /sbin so it runs with no /system.
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := klogd.c
LOCAL_CFLAGS := -O2 -Wall

LOCAL_MODULE := klogd
LOCAL_MODULE_TAGS := eng

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_STATIC_LIBRARIES := libc

LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT_SBIN)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_SBIN_UNSTRIPPED)

include $(BUILD_EXECUTABLE)
