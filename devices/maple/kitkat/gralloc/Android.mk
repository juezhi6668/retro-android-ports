# KitKat gralloc (module 0.2 + allocator + framebuffer) on top of the Lineage 22.1 QTI
# gralloc1 (the msm8998 gralloc module) loaded by libhyb
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := gralloc.maple
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := gralloc.c
LOCAL_CFLAGS := -std=gnu99 -O2 -Wall -Wno-unused-parameter
LOCAL_SHARED_LIBRARIES := libhyb libcutils liblog
include $(BUILD_SHARED_LIBRARY)
