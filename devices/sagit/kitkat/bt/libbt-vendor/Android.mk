# KitKat libbt-vendor for msm8998 / WCN3990 (cherokee): sagit and chiron.
#
# Deliberately NOT gated on BOARD_HAVE_BLUETOOTH_QCOM -- that flag also builds
# the SMD-era hardware/qcom/bt/libbt-vendor under the same module name, which
# is a different controller family and installs to a vendor path that does not
# exist on KitKat.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        src/bt_vendor_msm8998.c \
        src/hci_uart.c \
        src/hw_rome.c

LOCAL_C_INCLUDES := \
        $(LOCAL_PATH)/include \
        external/bluetooth/bluedroid/hci/include

LOCAL_SHARED_LIBRARIES := libcutils liblog

# bt_hci_bdroid.c dlopens /system/lib/libbt-vendor.so; this tree is arm-only
# and has no vendor partition.
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE := libbt-vendor
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := qcom

# hw_rome.c keeps the ar3k/wipower/nvm-tag-dump branches that a cherokee
# controller never reaches; their unused locals and labels are not worth
# chasing through 1900 lines of vendor code.
LOCAL_CFLAGS += -Wall -Wno-unused-variable -Wno-unused-function -Wno-unused-label

include $(BUILD_SHARED_LIBRARY)
