# AndroidBoard.mk for maple (Donut)
LOCAL_PATH := $(call my-dir)

# Prebuilt kernel (Sony whatawurst 4.4.302 Image.gz-dtb, TWRP-dumped from the
# boot partition; out-of-git blob). Used by the boot image build:
# $(TARGET_PREBUILT_KERNEL) -> kernel. The final flashable boot.img is packed
# by scripts/build-maple-donut-image.py (pagesize 4096/base 0; Donut's
# mkbootimg hardcodes pagesize 2048 and must not be used directly).
file := $(INSTALLED_KERNEL_TARGET)
ALL_PREBUILT += $(file)
$(file): $(TARGET_PREBUILT_KERNEL) | $(ACP)
	$(transform-prebuilt-to-target)
