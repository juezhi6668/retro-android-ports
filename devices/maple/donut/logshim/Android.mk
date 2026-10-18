# logshim - bringup-only /dev/log FIFO shim for maple/Donut.
# Static executable in the ramdisk's /sbin (no /system needed), like klogd.
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := logshim.c
LOCAL_CFLAGS := -O2 -Wall

LOCAL_MODULE := logshim
LOCAL_MODULE_TAGS := eng

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_STATIC_LIBRARIES := libc

LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT_SBIN)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_SBIN_UNSTRIPPED)

include $(BUILD_EXECUTABLE)
