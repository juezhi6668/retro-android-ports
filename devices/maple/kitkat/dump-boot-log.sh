#!/system/bin/sh
# dump-boot-log.sh — DIAG for maple KitKat bringup (no USB/adb yet and the
# 4.4 kernel has no CONFIG_LOGGER, so logcat is blind). Dumps kmsg once,
# then snapshots load + per-thread wait channels every ~30s using ONLY
# shell builtins + cat (toolbox `sort` segfaults on this kernel, `ps`
# prints nothing, `head`/`tail` output vanishes — all avoided). The service
# that runs this must NOT be oneshot. Remove once adb works.
dmesg > /data/misc/dmesg-boot.txt
logcat -v threadtime -f /data/misc/logcat-boot.txt 2>/dev/null &
while true; do
    echo "=== $(date) ===" >> /data/misc/watch.txt
    cat /proc/loadavg >> /data/misc/watch.txt
    head -8 /proc/meminfo >> /data/misc/watch.txt
    dmesg > /data/misc/kmsg-latest.txt 2>/dev/null
    echo "=== $(date) ===" >> /data/misc/watch-wchan.txt
    for d in /proc/[0-9]*; do
        p=${d#/proc/}
        case $p in *[!0-9]*) continue;; esac
        echo "$p $(cat $d/comm 2>/dev/null) $(cat $d/wchan 2>/dev/null)" >> /data/misc/watch-wchan.txt
    done
    echo "--- stat ---" >> /data/misc/watch-wchan.txt
    for d in /proc/[0-9]*; do
        p=${d#/proc/}
        case $p in *[!0-9]*) continue;; esac
        echo "$p $(cat $d/stat 2>/dev/null)" >> /data/misc/watch-wchan.txt
    done
    echo "--- zygote ---" >> /data/misc/watch-wchan.txt
    for d in /proc/[0-9]*; do
        c=$(cat $d/comm 2>/dev/null)
        if [ "$c" = "zygote" ] || [ "$c" = "system_server" ] || [ "$c" = "dexopt" ]; then
            echo "== $d $c ==" >> /data/misc/watch-wchan.txt
            ls -l $d/fd 2>/dev/null | head -12 >> /data/misc/watch-wchan.txt
            tail -6 $d/maps 2>/dev/null >> /data/misc/watch-wchan.txt
        fi
    done
    sleep 25
done
