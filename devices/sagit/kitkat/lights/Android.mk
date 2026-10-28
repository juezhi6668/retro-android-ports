LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SRC_FILES := lights.c
LOCAL_SHARED_LIBRARIES := liblog libcutils
# hw_get_module("lights") resolves lights.<ro.hardware>; both products boot
# with androidboot.hardware=msm8998
LOCAL_MODULE := lights.msm8998
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
