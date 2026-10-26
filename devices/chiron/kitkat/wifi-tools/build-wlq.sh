#!/bin/bash
# build-wlq.sh — cross-compile the static WLFW QMI client (wlq) for the device.
# Needs the kernel uapi linux/msm_ipc.h plus tools/include to satisfy the
# kernel-header chain (linux/compiler.h). -static so it runs on bionic.
set -e
TC=/home/juezhi/clang13/linaro-gcc-4.9/aarch64-linux-gnu/bin/aarch64-linux-gnu-gcc
K=/home/juezhi/linage17.1
cd "$(dirname "$0")"
$TC -static -O2 -w -std=gnu99 -I"$K/include/uapi" -I"$K/tools/include" -o /tmp/wlq wlq.c
echo "built /tmp/wlq ($(stat -c%s /tmp/wlq) bytes)"

# Stage into the out-of-git prebuilt tree so full_chiron.mk's
# $(BIN_DIR)/bin/wlq PRODUCT_COPY ships it in system/bin. Only if the vendor
# staging dir is present (it lives in the AOSP tree, not this repo).
A=/home/juezhi/aosp-kitkat
STAGE="$A/vendor/xiaomi/sagit-bin/bin"
if [ -d "$STAGE" ]; then
    cp /tmp/wlq "$STAGE/wlq"
    chmod 755 "$STAGE/wlq"
    echo "staged $STAGE/wlq"
fi
