LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := pdserve
LOCAL_SRC_FILES := pdserve.c
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)
