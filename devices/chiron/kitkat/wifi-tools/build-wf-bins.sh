#!/bin/bash
# build-wf-bins.sh — build the three small binaries the WLAN stack needs and
# stage them into the out-of-git prebuilt tree (vendor/xiaomi/sagit-bin/bin),
# from where full_sagit.mk / full_chiron.mk install them as /system/bin.
#
# Replaces build-wlq.sh (which only did wlq and, because it chained cp && chmod
# under set -e, used to print "staged" after an EACCES copy failed).
#
# All three run as root at post-fs-data, before the vendor LD_LIBRARY_PATH
# keeper exists, so wlq and rfssrv2 are linked -static: no DT_NEEDED, no
# 32/64-bit mismatch against KitKat's 32-bit /system/lib.
#
# The kernel uapi header chain needs tools/include for linux/compiler.h.
set -e
TC=${TC:-/home/juezhi/clang13/linaro-gcc-4.9/aarch64-linux-gnu/bin/aarch64-linux-gnu-gcc}
K=${K:-/home/juezhi/linage17.1}
A=${AOSP:-/home/juezhi/aosp-kitkat}
STAGE="$A/vendor/xiaomi/sagit-bin/bin"
FLAGS="-static -O2 -w -std=gnu99 -I$K/include/uapi -I$K/tools/include"
cd "$(dirname "$0")"

for src in wlq.c rfssrv2.c; do
    out=${src%.c}
    rm -f /tmp/$out
    $TC $FLAGS -o /tmp/$out "$src"
    echo "built /tmp/$out ($(stat -c%s /tmp/$out) bytes)"
done

# sm-vndk: KitKat's own static servicemanager, with its hardcoded "/dev/binder"
# rewritten to "/dev/vb" so it manages vndbinder for the vendor cnss-daemon's
# libbinder (see patch-sm-vndk.py for why). Requires a built AOSP tree.
SM=""
for p in "$A/out/target/product/sagit/system/bin/servicemanager" \
         "$A/out/target/product/chiron/system/bin/servicemanager"; do
    [ -f "$p" ] && SM=$p && break
done
if [ -n "$SM" ]; then
    python3 patch-sm-vndk.py "$SM" /tmp/sm-vndk
    chmod 755 /tmp/sm-vndk
else
    echo "SKIP sm-vndk: no servicemanager in $A/out yet (run the AOSP build first)" >&2
fi

if [ ! -d "$STAGE" ]; then
    echo "SKIP staging: $STAGE does not exist (it lives in the AOSP tree, not this repo)" >&2
    exit 0
fi
for b in wlq rfssrv2 sm-vndk; do
    [ -f "/tmp/$b" ] || continue
    cp "/tmp/$b" "$STAGE/$b"
    chmod 755 "$STAGE/$b"
    echo "staged $STAGE/$b"
done
