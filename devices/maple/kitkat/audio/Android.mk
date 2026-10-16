# audio.primary.maple — tinyalsa HAL for the msm8998-tavil-snd-card.
# Builds from devices/maple/kitkat/audio (synced to vendor/sony/maple/audio
# by scripts/sync-device-tree.sh). See audio_hw.c for the CHARQUIRKS
# mapping (speaker/mic routes, no acdb, decimator +20 dB, silent-safe
# while the card is absent).
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := audio.primary.maple
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SRC_FILES := audio_hw.c
LOCAL_C_INCLUDES += external/tinyalsa/include
LOCAL_SHARED_LIBRARIES := liblog libcutils libtinyalsa
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
