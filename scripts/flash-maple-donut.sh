#!/usr/bin/env bash
# Flash the Donut images to maple via TWRP (recovery adb runs as root).
#
#   scripts/flash-maple-donut.sh [--no-wipe] [--images-dir DIR]
#
# Steps: reboot to recovery -> push images to /data/media -> dd to
# system (sde46) + boot (sde35) -> md5 readback verification -> mke2fs
# userdata + cache -> reboot. The readback compares against the host image
# (trimmed: the partitions are larger than the images; a full-partition md5
# can never match).
#
# WARNING: the data/cache format is destructive — it also wipes
# /data/media (including the pushed images). Use --no-wipe to keep /data
# (only safe when coming from an already-Donut /data).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMG_DIR="$ROOT/out/maple-donut"
WIPE=1
while [ $# -gt 0 ]; do
    case "$1" in
        --no-wipe) WIPE=0 ;;
        --images-dir) IMG_DIR="$2"; shift ;;
        *) echo "unknown arg: $1" >&2; exit 1 ;;
    esac
    shift
done

SYSTEM="$IMG_DIR/system.img"
BOOT="$IMG_DIR/boot.img"
[ -f "$SYSTEM" ] || { echo "missing $SYSTEM" >&2; exit 1; }
[ -f "$BOOT" ] || { echo "missing $BOOT" >&2; exit 1; }

adbd() { adb shell "$@"; }

echo "== rebooting to recovery =="
adb reboot recovery || true
# Do NOT use `adb wait-for-device` here: when the device is already in TWRP,
# `adb reboot recovery` produces no transition it can see and the waiter
# hangs forever even though `adb devices` lists the device (observed
# 2026-10-18, 5+ min stuck). Poll for TWRP adb instead (TWRP has mke2fs).
for i in $(seq 1 90); do
    if adbd "test -x /sbin/mke2fs" 2>/dev/null; then break; fi
    sleep 2
done
adbd "test -x /sbin/mke2fs" || { echo "not in TWRP (or mke2fs missing)" >&2; exit 1; }
echo "TWRP up"

echo "== pushing images to /data/media =="
adb push "$SYSTEM" /data/media/system.img
adb push "$BOOT" /data/media/boot.img

# host-side md5s + block counts (also used for the readback below)
SYS_BYTES=$(stat -c %s "$SYSTEM"); BOOT_BYTES=$(stat -c %s "$BOOT")
[ $((SYS_BYTES % 4096)) = 0 ] && [ $((BOOT_BYTES % 4096)) = 0 ] || {
    echo "image size not 4096-aligned" >&2; exit 1; }
HOST_SYS=$(md5sum "$SYSTEM" | cut -d' ' -f1)
HOST_BOOT=$(md5sum "$BOOT" | cut -d' ' -f1)

echo "== pushed-file md5 check (catches push corruption before we dd) =="
PUSH_SYS=$(adbd "md5sum /data/media/system.img" | cut -c1-32)
PUSH_BOOT=$(adbd "md5sum /data/media/boot.img" | cut -c1-32)
echo "push: system device=$PUSH_SYS host=$HOST_SYS"
echo "push: boot   device=$PUSH_BOOT host=$HOST_BOOT"
[ "$PUSH_SYS" = "$HOST_SYS" ] || { echo "SYSTEM PUSH MISMATCH" >&2; exit 1; }
[ "$PUSH_BOOT" = "$HOST_BOOT" ] || { echo "BOOT PUSH MISMATCH" >&2; exit 1; }

# NOTE: TWRP's dd is toybox and rejects GNU size suffixes (`bs=1M` ->
# "illegal number"); use a plain byte count.
echo "== flashing system (sde46) =="
adbd "dd if=/data/media/system.img of=/dev/block/bootdevice/by-name/system bs=1048576"
echo "== flashing boot (sde35) =="
adbd "dd if=/data/media/boot.img of=/dev/block/bootdevice/by-name/boot bs=1048576"
adbd sync

echo "== readback verification (device-side md5 of the written range) =="
# The partitions are larger than the images, so read back exactly the image
# bytes (4096-aligned counts) and compare against the host md5.
DEV_SYS=$(adbd "dd if=/dev/block/bootdevice/by-name/system bs=4096 count=$((SYS_BYTES / 4096)) 2>/dev/null | md5sum" | cut -c1-32)
DEV_BOOT=$(adbd "dd if=/dev/block/bootdevice/by-name/boot bs=4096 count=$((BOOT_BYTES / 4096)) 2>/dev/null | md5sum" | cut -c1-32)
echo "system: host=$HOST_SYS device=$DEV_SYS"
echo "boot:   host=$HOST_BOOT device=$DEV_BOOT"
[ "$HOST_SYS" = "$DEV_SYS" ] || { echo "SYSTEM READBACK MISMATCH" >&2; exit 1; }
[ "$HOST_BOOT" = "$DEV_BOOT" ] || { echo "BOOT READBACK MISMATCH" >&2; exit 1; }
echo "readback OK"

if [ "$WIPE" = 1 ]; then
    echo "== formatting userdata + cache (destroys /data/media) =="
    adbd "mke2fs -t ext4 -b 4096 -L userdata /dev/block/bootdevice/by-name/userdata"
    adbd "mke2fs -t ext4 -b 4096 -L cache /dev/block/bootdevice/by-name/cache"
fi

echo "== rebooting to Donut =="
adb reboot
echo "done — watch the phone; bootlog lands at /data/bootlog.txt"
