#!/bin/bash
# build-shim.sh — cross-build liblog-shim.c for both userspace bitnesses.
#
# The KitKat tree ships 32 bit ARM (/system/bin/wpa_supplicant, the framework)
# and the vendor partition is 64 bit only (/system/vendor/bin/cnss-daemon), so
# a shim for each is needed. -nostdlib is mandatory: a glibc-linked .so carries
# SHT_GNU_verneed entries and Android's linker refuses to bind those against
# bionic ("cannot locate symbol stderr"), and -fomit-frame-pointer frees r7 for
# the syscall number on ARM.
set -e
cd "$(dirname "$0")"
TC=/home/juezhi/clang13/linaro-gcc-4.9
FLAGS="-shared -fPIC -O2 -std=gnu99 -nostdlib -ffreestanding -fno-stack-protector -fno-builtin -fomit-frame-pointer -Wall"

$TC/aarch64-linux-gnu/bin/aarch64-linux-gnu-gcc $FLAGS -o /tmp/liblog-shim64.so liblog-shim.c
$TC/arm-linux-gnueabi/bin/arm-linux-gnueabi-gcc $FLAGS -march=armv7-a -marm -o /tmp/liblog-shim32.so liblog-shim.c

for f in /tmp/liblog-shim64.so /tmp/liblog-shim32.so; do
	readelf -d $f | grep -q NEEDED && { echo "FATAL: $f has DT_NEEDED"; exit 1; }
	nm -D --undefined-only $f | grep -vE '_ITM_|_Jv_|__cxa_|__gmon_' && {
		echo "FATAL: $f has undefined symbols"; exit 1; }
done
ls -la /tmp/liblog-shim32.so /tmp/liblog-shim64.so
