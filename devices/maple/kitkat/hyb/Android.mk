# libhyb: loads the Lineage 22.1 (Android 15) vendor libraries (Adreno 540 driver, QTI
# gralloc1...) into KitKat processes; root of the newer-Android files: /system/a15 (hyb.h)
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := libhyb
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := linker.c hooks.c
LOCAL_CFLAGS := -std=gnu99 -O2 -Wall -Wno-unused-function -Wno-unused-parameter
LOCAL_SHARED_LIBRARIES := libcutils liblog libdl
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := hybtest
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := hybtest.c
LOCAL_CFLAGS := -std=gnu99 -O2
LOCAL_SHARED_LIBRARIES := libhyb liblog
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := acdbtest
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := acdbtest.c
LOCAL_CFLAGS := -std=gnu99 -O2
LOCAL_SHARED_LIBRARIES := libhyb liblog
include $(BUILD_EXECUTABLE)
