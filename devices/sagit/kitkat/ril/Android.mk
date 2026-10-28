LOCAL_PATH := $(call my-dir)

ifneq ($(filter sagit chiron,$(TARGET_DEVICE)),)
include $(CLEAR_VARS)
LOCAL_MODULE := qmxd
LOCAL_SRC_FILES := qmxd.c
LOCAL_CFLAGS := -std=gnu99 -Wall -Wextra -Werror -DANDROID_INIT -DHAVE_ANDROID_LOG
LOCAL_SHARED_LIBRARIES := libcutils liblog
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := start-msm8998-ril
LOCAL_SRC_FILES := start-ril.c
LOCAL_CFLAGS := -std=gnu99 -Wall -Wextra -Werror -DHAVE_ANDROID_LOG
LOCAL_SHARED_LIBRARIES := liblog
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)
endif
