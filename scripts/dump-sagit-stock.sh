#!/usr/bin/env bash
# Dump stock-device facts needed before the first sagit/KitKat boot.
# Runs against a STOCK (or TWRP-booted) Mi 6 over adb — no AOSP build needed.
# Output: dumps/sagit-stock-<date>/ with everything the verify-me markers in
# devices/sagit/kitkat/ need (by-name paths, sizes, cmdline, touch input name,
# LED/backlight nodes, subsys names, hw_platform, UDC, fb0 facts).
# Measure, don't guess (cooper rule): fix the skeletons from these dumps
# BEFORE flashing anything.
#
#   scripts/dump-sagit-stock.sh              # adb shell as-is
#   scripts/dump-sagit-stock.sh --root       # adb root first (root-gated nodes)
#   scripts/dump-sagit-stock.sh --su         # wrap each command in `su -c`
#                                            # (Magisk/KernelSU ROMs, where
#                                            # adbd refuses `adb root`)
#   scripts/dump-sagit-stock.sh --root --pull-boot   # + pull boot partition
#
# TWRP works too: its adbd is already root, and by-name lives under
# /dev/block/bootdevice/by-name there (stock Android: /dev/block/by-name).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/dumps/sagit-stock-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"

WANT_ROOT=0
WANT_SU=0
WANT_BOOT=0
for a in "$@"; do
    case "$a" in
        --root) WANT_ROOT=1 ;;
        --su) WANT_SU=1 ;;
        --pull-boot) WANT_BOOT=1 ;;
        *) echo "unknown arg: $a" >&2; exit 2 ;;
    esac
done

# capture into a file named after the first arg; one remote command string
if [ "$WANT_SU" = 1 ]; then
    grab() { local f="$1"; shift; echo "+ adb shell su -c $*" >&2; adb shell "su -c $(printf '%q' "$*")" > "$OUT/$f" 2>/dev/null || true; }
else
    grab() { local f="$1"; shift; echo "+ adb shell $*" >&2; adb shell "$@" > "$OUT/$f" 2>/dev/null || true; }
fi

adb start-server
adb wait-for-device
if [ "$WANT_ROOT" = 1 ]; then
    # `adb root` restarts adbd and can drop USB on some builds; opt-in only.
    adb root || true
    sleep 2
    adb wait-for-device || true
fi
STATE="$(adb get-state 2>/dev/null || true)"
if [ "$STATE" != device ]; then
    echo "device not online (adb get-state: '${STATE:-none}'). Re-plug, accept the RSA prompt, then re-run." >&2
    exit 1
fi

grab getprop.txt getprop
grab default.prop.txt cat /default.prop
grab uname.txt uname -a
grab cpuinfo.txt cat /proc/cpuinfo
grab cmdline.txt cat /proc/cmdline
grab device-tree-model.txt cat /proc/device-tree/model
grab proc-partitions.txt cat /proc/partitions

# storage: stock Android, TWRP, and both possible ufshc paths
grab by-name.txt ls -l /dev/block/by-name
grab by-name-bootdevice.txt ls -l /dev/block/bootdevice/by-name
grab by-name-soc.txt ls -l /dev/block/platform/soc/1da4000.ufshc/by-name
grab mounts.txt cat /proc/mounts
grab df.txt df

# display
grab proc-fb.txt cat /proc/fb
grab fb0-name.txt cat /sys/class/graphics/fb0/name
grab fb0-modes.txt cat /sys/class/graphics/fb0/modes
grab fb0-virtual-size.txt cat /sys/class/graphics/fb0/virtual_size
grab fb0-panel-info.txt cat /sys/class/graphics/fb0/msm_fb_panel_info
grab dumpsys-display.txt dumpsys display
grab lcd-density.txt getprop ro.sf.lcd_density
grab backlight-dir.txt ls /sys/class/backlight
grab leds.txt ls -l /sys/class/leds
grab leds-white-brightness.txt cat /sys/class/leds/white/brightness

# input: the touchscreen's input NAME becomes the .idc filename
grab inputs.txt ls -l /dev/input
grab input-devices.txt cat /proc/bus/input/devices
grab getevent-p.txt getevent -p

# audio / firmware
grab hw-platform.txt cat /sys/devices/soc0/hw_platform
grab soc0.txt ls /sys/devices/soc0
grab sound-cards.txt cat /proc/asound/cards
grab mixer-controls.txt cat /proc/asound/card0/controlC0
grab firmware-dir.txt ls -l /vendor/firmware
grab subsys-names.txt 'for d in /sys/bus/msm_subsys/devices/*/name; do echo "$d: $(cat $d)"; done'

# usb gadget
grab udc.txt ls /sys/class/udc
grab configfs.txt cat /config/usb_gadget/g1/UDC

# wifi
grab wifi-modules.txt 'lsmod 2>/dev/null; ls /vendor/lib/modules 2>/dev/null'
grab wlan-mac.txt cat /sys/class/net/wlan0/address

if [ "$WANT_BOOT" = 1 ]; then
    BN=""
    for p in /dev/block/by-name /dev/block/bootdevice/by-name \
             /dev/block/platform/soc/1da4000.ufshc/by-name; do
        if adb shell "[ -e $p/boot ]" 2>/dev/null; then BN="$p"; break; fi
    done
    if [ -n "$BN" ]; then
        echo "+ pulling boot from $BN/boot" >&2
        if [ "$WANT_SU" = 1 ]; then
            adb shell "su -c 'dd if=$BN/boot of=/sdcard/sagit-boot.img bs=1048576'" >&2 || true
        else
            adb shell "dd if=$BN/boot of=/sdcard/sagit-boot.img bs=1048576" >&2 || true
        fi
        adb pull /sdcard/sagit-boot.img "$OUT/sagit-boot.img" || true
        adb shell rm -f /sdcard/sagit-boot.img || true
    else
        echo "boot by-name not found; pull it in TWRP by hand (extract Image.gz-dtb)" >&2
    fi
fi

echo "dump: $OUT"
ls -la "$OUT"
# Root-gated nodes come back empty without --root; anything else empty means
# the connection dropped or the node has a different name on this device.
for f in "$OUT"/*.txt; do
    [ -s "$f" ] || echo "EMPTY: $(basename "$f")"
done
