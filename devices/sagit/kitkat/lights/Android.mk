LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SRC_FILES := lights.c
LOCAL_SHARED_LIBRARIES := liblog libcutils
# named after the board so hw_get_module("lights", "chiron") finds it
LOCAL_MODULE := lights.$(TARGET_DEVICE)
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
