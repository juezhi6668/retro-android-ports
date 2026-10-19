# audio.primary.maple — tinyalsa HAL for the msm8998-tavil-snd-card.
# Builds from devices/maple/kitkat/audio (synced to vendor/sony/maple/audio
# by scripts/sync-device-tree.sh). See audio_hw.c for the CHARQUIRKS
# mapping (speaker/mic routes, decimator +20 dB, silent-safe while the card
# is absent) and the libhyb ACDB loader that gives the dsp its calibration.
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := audio.primary.maple
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SRC_FILES := audio_hw.c
LOCAL_C_INCLUDES += external/tinyalsa/include $(LOCAL_PATH)/../hyb
LOCAL_SHARED_LIBRARIES := liblog libcutils libtinyalsa libhyb
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

# Framework-level audio checks: AudioTrack/AudioRecord -> AudioFlinger ->
# this HAL (the tinyalsa tools in tools/ bypass the HAL entirely). Pushed by
# the build to /system/bin; run as root:
#   audiotrack_test 5              (speaker tone)
#   audiorecord_test 5 48000 /data/local/tmp/rec.raw
include $(CLEAR_VARS)
LOCAL_MODULE := audiotrack_test
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := audiotrack_test.cpp
LOCAL_C_INCLUDES := frameworks/av/include
LOCAL_SHARED_LIBRARIES := libmedia libutils libbinder libcutils liblog
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := audiorecord_test
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := audiorecord_test.cpp
LOCAL_C_INCLUDES := frameworks/av/include
LOCAL_SHARED_LIBRARIES := libmedia libutils libbinder libcutils liblog
include $(BUILD_EXECUTABLE)
