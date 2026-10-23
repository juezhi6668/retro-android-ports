# AndroidBoard.mk for sagit (KitKat)
LOCAL_PATH := $(call my-dir)

# Prebuilt kernel (Xiaomi msm8998 4.4 Image.gz-dtb dumped from the phone).
# Used by the boot image build: $(TARGET_PREBUILT_KERNEL) -> kernel.
# The blob lives at vendor/xiaomi/sagit/kernel and is gitignored (vendor
# binaries never go in git — see repo README).
file := $(INSTALLED_KERNEL_TARGET)
ALL_PREBUILT += $(file)
$(file): $(TARGET_PREBUILT_KERNEL) | $(ACP)
	$(transform-prebuilt-to-target)
