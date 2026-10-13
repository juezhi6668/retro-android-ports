# AndroidBoard.mk for cooper (Donut)
LOCAL_PATH := $(call my-dir)

# Prebuilt kernel (CM7 cooper zImage, Samsung 2.6.32-based, ARM boot executable)
# Used by boot image build: $(TARGET_PREBUILT_KERNEL) -> kernel
file := $(INSTALLED_KERNEL_TARGET)
ALL_PREBUILT += $(file)
$(file): $(TARGET_PREBUILT_KERNEL) | $(ACP)
	$(transform-prebuilt-to-target)
