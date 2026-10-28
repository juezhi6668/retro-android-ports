# KitKat gralloc (module 0.2 + allocator + framebuffer) on top of the Lineage 22.1 QTI
# gralloc1 (the msm8998 gralloc module) loaded by libhyb
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
# One module for every msm8998 board in this tree: hw_get_module("gralloc")
# resolves gralloc.<ro.hardware> and both products boot with
# androidboot.hardware=msm8998. Same QTI gralloc1 closure on both.
LOCAL_MODULE := gralloc.msm8998
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := gralloc.c
LOCAL_CFLAGS := -std=gnu99 -O2 -Wall -Wno-unused-parameter
LOCAL_SHARED_LIBRARIES := libhyb libcutils liblog
include $(BUILD_SHARED_LIBRARY)
