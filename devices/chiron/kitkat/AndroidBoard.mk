# AndroidBoard.mk for chiron (KitKat)
LOCAL_PATH := $(call my-dir)

# Prebuilt kernel: the user-built 4.4.302 Image.gz-dtb extracted from the
# OS3 chiron boot.img. Only the selected product's AndroidBoard.mk is
# included (build/target/board/Android.mk -include), so this and sagit's do
# not collide. The blob is gitignored (vendor binaries never go in git).
file := $(INSTALLED_KERNEL_TARGET)
ALL_PREBUILT += $(file)
$(file): $(TARGET_PREBUILT_KERNEL) | $(ACP)
	$(transform-prebuilt-to-target)
