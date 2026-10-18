#!/system/bin/sh
# autowinch.sh - Donut bringup safety net for maple.
# Disabled if /cache/no-autowinch exists (long debugging sessions).
ls /cache/no-autowinch >/dev/null 2>&1 && { echo "autowinch: disabled by /cache/no-autowinch"; exit 0; }
# If adb never comes up (broken gadget, silent crash), return to TWRP after
# 15 minutes so the captures on /cache (kmsg.txt + markers) can be read.
# Donut's shell has no [ / test, so this is the plain proven version; with
# adb working, `adb reboot recovery` is the way back, and the service can be
# killed from the host (adb shell ps, then kill) if the session must outlive
# the countdown. This is KitKat's autowinch idea, Donut edition. Delete
# this service once adb is trusted.
sleep 900
{
    echo "autowinch: 900s elapsed, rebooting to recovery"
    cat /proc/uptime
} >> /cache/kmsg.txt 2>&1
sync
reboot recovery
