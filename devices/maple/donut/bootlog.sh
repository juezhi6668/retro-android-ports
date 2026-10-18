#!/system/bin/sh
# bootlog.sh — Donut bringup diagnostics for maple.
# This kernel has no Android logger driver (no /dev/log*), so init's own
# kmsg output and dmesg are the only boot evidence. Snapshot them to /data
# periodically; when adb is not up yet the file is pullable from TWRP
# (mount /data, adb pull /data/bootlog.txt). Started from init.rc; delete
# this service once the port is stable.
sleep 20
n=0
while true; do
    n=$((n + 1))
    {
        echo "=== bootlog snapshot $n, uptime $(cat /proc/uptime) ==="
        echo "--- mounts:"
        cat /proc/mounts
        echo "--- init service state:"
        getprop | grep init.svc
        echo "--- properties:"
        getprop
        echo "--- processes:"
        ps
        echo "--- dmesg:"
        dmesg
    } > /data/bootlog.txt 2>&1
    sleep 45
done
