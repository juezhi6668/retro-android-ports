# AndroidBoard.mk for poplar (KitKat)
LOCAL_PATH := $(call my-dir)

# Prebuilt kernel (Sony msm8998 4.4 Image.gz-dtb, ARM boot executable).
# Used by boot image build: $(TARGET_PREBUILT_KERNEL) -> kernel.
# TODO: drop the prebuilt Image.gz-dtb at vendor/sony/poplar/kernel
# (personal servicing only, do NOT commit blobs — see repo README).
file := $(INSTALLED_KERNEL_TARGET)
ALL_PREBUILT += $(file)
$(file): $(TARGET_PREBUILT_KERNEL) | $(ACP)
	$(transform-prebuilt-to-target)

