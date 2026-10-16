#!/system/bin/sh
# dump-boot-log.sh — DIAG for poplar KitKat bringup (no USB/adb yet and the
# 4.4 kernel has no CONFIG_LOGGER, so logcat is blind). Dumps kmsg once,
# then snapshots load + per-thread wait channels every ~30s using ONLY
# shell builtins + cat (toolbox `sort` segfaults on this kernel, `ps`
# prints nothing, `head`/`tail` output vanishes — all avoided). The service
# that runs this must NOT be oneshot. Remove once adb works.
dmesg > /data/misc/dmesg-boot.txt
logcat -v threadtime -f /data/misc/logcat-boot.txt 2>/dev/null &
n=0
while true; do
    n=$((n + 1))
    echo "=== $(date) ===" >> /data/misc/watch.txt
    cat /proc/loadavg >> /data/misc/watch.txt
    cat /proc/meminfo >> /data/misc/watch.txt
    # backgrounded, it's a binder call and hangs too if servicemanager is wedged
    service list > /data/misc/service-list.txt 2>&1 &
    # about 3 minutes in, every d-state task's kernel stack goes to dmesg (kmsg-latest.txt)
    if [ $n -eq 7 ]; then
        echo w > /proc/sysrq-trigger
    fi
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
        case "$c" in zygote|system_server|dexopt|surfaceflinger|servicemanager|bootanimation|glrenderd)
            echo "== $d $c ==" >> /data/misc/watch-wchan.txt
            for t in $d/task/*; do
                echo "${t##*/} $(cat $t/comm 2>/dev/null) $(cat $t/wchan 2>/dev/null)" >> /data/misc/watch-wchan.txt
            done
            ;;
        esac
    done
    sleep 25
done
