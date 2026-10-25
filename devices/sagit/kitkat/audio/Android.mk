# audio.primary.<device> — tinyalsa HAL for the msm8998-tasha-snd-card,
# shared by every msm8998 board in this tree (sagit, chiron). The module is
# named after TARGET_DEVICE because hw_get_module_by_class looks up
# audio.primary.$(ro.hardware) first. See audio_hw.c for the route tables
# (speaker/mic, decimator +20 dB, silent-safe while the card is absent) and
# the libhyb ACDB loader that feeds the dsp the ROM's acdb blocks.
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := audio.primary.$(TARGET_DEVICE)
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SRC_FILES := audio_hw.c
LOCAL_C_INCLUDES += external/tinyalsa/include $(LOCAL_PATH)/../hyb
LOCAL_SHARED_LIBRARIES := liblog libcutils libtinyalsa libhyb
LOCAL_MODULE_TAGS := optional
# Speaker amp differs per board (kernel DT): sagit has a TFA9888 on
# i2c 10-0034 ("tfa98xx-aif-a-34" on QUAT_MI2S), chiron a TAS2559 on
# i2c 10-004c ("tas2559 ASI1" on QUAT_MI2S, msm_quat_mi2s_tas2559_dai_links).
ifeq ($(TARGET_DEVICE),chiron)
LOCAL_CFLAGS += -DAMP_TAS2559
endif
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
