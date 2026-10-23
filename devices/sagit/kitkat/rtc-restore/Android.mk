LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := rtc-restore
LOCAL_SRC_FILES := rtc_restore.c
LOCAL_SHARED_LIBRARIES := libcutils liblog
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)
