#!/system/bin/sh
# dump-boot-log.sh — DIAG for sagit KitKat bringup (no USB/adb yet and the
# 4.4 kernel has no CONFIG_LOGGER, so logcat is blind). Dumps kmsg once,
# then snapshots load + per-thread wait channels every ~30s using ONLY
# shell builtins + cat (toolbox `sort` segfaults on this kernel, `ps`
# prints nothing, `head`/`tail` output vanishes — all avoided). The service
# that runs this must NOT be oneshot. Remove once adb works.
dmesg > /data/misc/dmesg-boot.txt
logcat -v threadtime -f /data/misc/logcat-boot.txt 2>/dev/null &

# === WLAN bring-up finisher (WCN3990), runs EVERY boot — 2026-10-26 ===
# The ramdisk /sbin/wf-setup.sh (an init oneshot) already creates the by-name /
# persist symlinks, mounts debugfs, launches rfssrv2 + sm-vndk and does
# `start wf-efs`, whose keeper respawns rmt_storage + cnss-daemon from a shell.
# Its ONE broken step is `start wf-irsc`: KitKat init execs the 64-bit vendor
# blob irsc_util directly, and a vendor binary exec'd by init exits silently
# (the very reason the EFS daemons need a shell keeper). So irsc_util never runs,
# signal_irsc_completion never fires, and on msm8998 legacy ipc-router v1 that
# makes EVERY user-space CLIENT_PORT send stall in wait_for_irsc_completion --
# including cnss-daemon's wlfw_bdf_download_req. No BDF, the modem never posts
# FW_READY, and wlan0 never appears.
#
# Finish that one step from a loader that actually runs: our 32-bit static wlq
# opens an IRSC port, loads sec_config, and closes it, which fires the
# completion and unblocks cnss's send for the rest of the boot. Proven on-device
# 2026-10-26: with this, cnss reaches icnss State 0xd8d (FW READY | DRIVER
# PROBED), wlan0 gets a real MAC, and `wpa_supplicant -Dnl80211` scans 2.4/5 GHz.
# The loader lives in /system/bin (survives a /data wipe); /data/local/tmp is
# the bringup fallback location.
WLQ=/system/bin/wlq
[ -x $WLQ ] || WLQ=/data/local/tmp/wlq
if [ -x $WLQ ]; then
    $WLQ irsc /system/vendor/etc/sec_config >/data/local/tmp/wlq-irsc.log 2>&1 &
fi
# === end WLAN finisher ===

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
        case "$c" in zygote|system_server|dexopt|surfaceflinger|servicemanager|bootanimation|fbpresentd)
            echo "== $d $c ==" >> /data/misc/watch-wchan.txt
            for t in $d/task/*; do
                echo "${t##*/} $(cat $t/comm 2>/dev/null) $(cat $t/wchan 2>/dev/null)" >> /data/misc/watch-wchan.txt
            done
            ;;
        esac
    done
    sleep 25
done
