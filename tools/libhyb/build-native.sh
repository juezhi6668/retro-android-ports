#!/usr/bin/env bash
# Builds the libhyb side with the Android NDK r21e:
#  - fbpresentd, the 64-bit MDSS atomic-commit presenter (KitKat's build system only produces
#    32-bit binaries, and the kernel's 32-bit compat path for MSMFB_ATOMIC_COMMIT crashes).
# The result is staged out-of-git for full_<device>.mk at vendor/<vendor>/<device>-bin/fbpresentd.
#
#   tools/libhyb/build-native.sh <aosp dir> [vendor/device]
#       (default device dir: sony/maple; NDK=<path to android-ndk-r21e> to reuse one)
set -euo pipefail

AOSP=$(realpath "${1:?usage: $0 <aosp dir> [vendor/device]}")
DEV=${2:-sony/maple}
SRC=$AOSP/vendor/$DEV
DEST=$AOSP/vendor/${DEV%%/*}/${DEV##*/}-bin
# Cache the NDK OUTSIDE $AOSP: the AOSP make walks the tree for Android.mk
# files and the NDK's own sources/android/cpufeatures/Android.mk collides
# with the tree's ndk/ copy ("MODULE.TARGET.STATIC_LIBRARIES.cpufeatures
# already defined by ndk/sources/android/cpufeatures"), breaking every
# later make. The old default was $AOSP/out-blobs and bit exactly that way
# on 2026-10-19 once the NDK landed mid-build (first make parsed before the
# unzip, the image rebuild did not).
CACHE=${CACHE:-$HOME/.cache/retro-android-ports/ndk}
NDK_URL=https://dl.google.com/android/repository/android-ndk-r21e-linux-x86_64.zip
NDK_SHA1=c3ebc83c96a4d7f539bd72c241b2be9dcd29bda9

[ -d "$SRC" ] || { echo "$SRC not found: sync the device tree first (scripts/sync-device-tree.sh ${DEV##*/})" >&2; exit 1; }

if [ -z "${NDK:-}" ]; then
    NDK=$CACHE/android-ndk-r21e
    if [ ! -x "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang" ]; then
        echo "==> Downloading $NDK_URL"
        mkdir -p "$CACHE"
        tmp=$(mktemp -d)
        curl -fL -o "$tmp/ndk.zip" "$NDK_URL"
        echo "$NDK_SHA1  $tmp/ndk.zip" | sha1sum -c --quiet
        unzip -q "$tmp/ndk.zip" -d "$CACHE"
        rm -rf "$tmp"
    fi
fi
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
[ -x "$TC/aarch64-linux-android29-clang" ] || { echo "no aarch64-linux-android29-clang in $NDK" >&2; exit 1; }

mkdir -p "$DEST/bin"
echo "==> fbpresentd (aarch64, static)"
"$TC/aarch64-linux-android29-clang" -O2 -Wall -static -o "$DEST/bin/fbpresentd" "$SRC/fbpresentd/fbpresentd.c"
echo "staged: $DEST/bin/fbpresentd"
