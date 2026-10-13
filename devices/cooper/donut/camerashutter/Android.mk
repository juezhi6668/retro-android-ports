# toggleshutter for cooper (GT-S5830) on Donut.
# Ported from CM7 reference/android_device_samsung_cooper/camerashutter.
# Reference gates on TARGET_BOOTLOADER_BOARD_NAME==cooper, which Donut never
# sets (Donut cooper uses TARGET_BOARD_PLATFORM:=cooper to dodge Dream-era
# hardware/msm7k HALs). Gate on our platform name instead so the binary
# actually builds for full_cooper-eng. Small libcutils-only oneshot service
# started by init.gt-s5830.rc property triggers; symlinks shutter sounds to
# /dev/null when muted.
ifeq ($(TARGET_BOARD_PLATFORM),cooper)
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := toggleshutter.c
LOCAL_MODULE := toggleshutter
LOCAL_MODULE_TAGS := optional
LOCAL_SHARED_LIBRARIES := libcutils
include $(BUILD_EXECUTABLE)

endif
