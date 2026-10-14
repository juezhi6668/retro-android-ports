#!/usr/bin/env bash
# Dump stock-device facts needed before the first maple/KitKat boot.
# Runs against a STOCK (or TWRP-booted) maple over adb — no AOSP build needed.
# Output: dumps/<serial>-<date>/ with default.prop, partitions, fstab-relevant
# nodes, framebuffer + density facts. Compare against devices/maple/kitkat/
# skeletons (fstab.maple block paths, system.prop density) and fix mismatches
# BEFORE flashing anything (cooper rule: measure, don't guess).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/dumps/maple-stock-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"

run() { echo "+ $*" >&2; "$@" ; }

adb start-server
adb wait-for-device
# `adb root` restarts adbd and can drop the USB connection on some builds
# (observed: maple disconnects). It is OPT-IN only:
#   scripts/dump-maple-stock.sh --root
# Default run uses the authorized shell as-is (worked for getprop, by-name,
# dumpsys). Root-gated nodes (/default.prop, /proc/cmdline, fb0 sysfs) may
# come back empty without it — that is expected, not an error.
if [ "${1:-}" = "--root" ]; then
    adb root || true
    sleep 2
    adb wait-for-device || true
fi
STATE="$(adb get-state 2>/dev/null || true)"
if [ "$STATE" != device ]; then
    echo "device not online (adb get-state: '${STATE:-none}'). Re-plug, accept the RSA prompt, then re-run." >&2
    exit 1
fi
run adb shell getprop > "$OUT/getprop.txt" || true
run adb shell cat /default.prop > "$OUT/default.prop.txt" || true
run adb shell cat /proc/partitions > "$OUT/proc-partitions.txt" || true
run adb shell ls -l /dev/block/bootdevice/by-name > "$OUT/by-name.txt" || true
run adb shell ls -l /dev/block/platform/soc/1da4000.ufshc/by-name > "$OUT/by-name-soc.txt" || true
run adb shell cat /proc/cmdline > "$OUT/cmdline.txt" || true
run adb shell cat /proc/fb > "$OUT/proc-fb.txt" || true
run adb shell dumpsys display > "$OUT/dumpsys-display.txt" || true
run adb shell getprop ro.sf.lcd_density > "$OUT/lcd-density.txt" || true
run adb shell cat /sys/class/graphics/fb0/modes > "$OUT/fb0-modes.txt" || true
run adb shell cat /sys/class/graphics/fb0/virtual_size > "$OUT/fb0-virtual-size.txt" || true
run adb shell ls -l /dev/input > "$OUT/inputs.txt" || true
run adb shell cat /proc/cpuinfo > "$OUT/cpuinfo.txt" || true
run adb shell uname -a > "$OUT/uname.txt" || true

echo "dump: $OUT"
ls -la "$OUT"
# Warn about empty captures (root-gated nodes without --root come back
# empty; anything else empty means the connection dropped mid-run).
for f in "$OUT"/*.txt; do
    [ -s "$f" ] || echo "EMPTY: $(basename "$f")"
done
