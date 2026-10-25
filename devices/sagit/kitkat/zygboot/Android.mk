LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := zygboot
LOCAL_SRC_FILES := zygboot.c
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)
