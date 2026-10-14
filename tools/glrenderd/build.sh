#!/bin/bash
# ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
# builds glrenderd (32-bit, it runs in lineage's 32-bit closure) and hwchroot into out/
# usage: build.sh <aosp-kitkat dir> <android-ndk-r21e dir>
set -e
H=$(cd "$(dirname "$0")" && pwd)
AOSP=$(cd "$1" && pwd)
NDK=$(cd "$2" && pwd)
E=$AOSP/sdk/emulator/opengl
OUT=$H/out

if ! grep -q withAlpha $E/host/libs/libOpenglRender/FBConfig.cpp; then
    echo "apply patches/kitkat/sdk-glrenderd.patch to $AOSP/sdk first" >&2
    exit 1
fi

rm -rf $OUT && mkdir -p $OUT/gen
# newer host compilers stopped pulling these in for emugen implicitly
${HOSTCXX:-c++} -O2 -include unistd.h -include stdlib.h -include string.h -include stdio.h \
    -o $OUT/emugen $E/host/tools/emugen/*.cpp
for c in GLESv1_dec:gl GLESv2_dec:gl2 renderControl_dec:renderControl; do
    d=${c%%:*}
    b=${c##*:}
    mkdir -p $OUT/gen/$d
    $OUT/emugen -i $E/host/libs/$d -D $OUT/gen/$d $b
    python3 -I $H/unalign.py $OUT/gen/$d/${b}_dec.cpp
done

$NDK/ndk-build -C $H NDK_PROJECT_PATH=$H APP_BUILD_SCRIPT=$H/jni/Android.mk \
    NDK_APPLICATION_MK=$H/jni/Application.mk NDK_OUT=$OUT/obj NDK_LIBS_OUT=$OUT/libs \
    GLR_SDK=$AOSP/sdk GLR_GEN=$OUT/gen -j$(nproc)
cp $OUT/libs/armeabi-v7a/glrenderd $OUT/glrenderd

# static and 64-bit, it only needs the kernel
$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang -O2 -Wall -static \
    -o $OUT/hwchroot $H/src/hwchroot.c
echo "built $OUT/glrenderd and $OUT/hwchroot"
