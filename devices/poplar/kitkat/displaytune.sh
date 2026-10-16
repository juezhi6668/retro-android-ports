#!/system/bin/sh
# Display tuning, once per boot.
#
# This replaces "keepawake", which pinned the panel on as a workaround for the
# framework not restoring it after a screen timeout. That no longer happens, and the
# workaround itself caused the backlight to come back on seconds after the screen was
# switched off. All that is left to do is undo the settings it left behind in /data,
# so an already-installed device gets normal screen blanking without a data wipe.
#
# stay_on_while_plugged_in is also worth resetting: it was left at 3 (stay awake while
# on AC and USB), which keeps the screen on the whole time the device is on adb.
# Users who want that can turn it on again in Settings > Developer options.

n=0
while [ $n -lt 40 ]; do
    settings put global stay_on_while_plugged_in 0 2>/dev/null
    settings put system screen_off_timeout 60000 2>/dev/null
    t=$(settings get system screen_off_timeout 2>/dev/null)
    if [ "$t" = "60000" ]; then
        echo "displaytune: stay_on_while_plugged_in=0 screen_off_timeout=60000" > /dev/kmsg
        exit 0
    fi
    n=$((n + 1))
    sleep 3
done
echo "displaytune: settings provider never came up" > /dev/kmsg
exit 1
