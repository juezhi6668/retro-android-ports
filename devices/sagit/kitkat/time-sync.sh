#!/system/bin/sh
# time-sync.sh — decide *when* to correct the wall clock; time-sync does the work.
#
# This kernel has no /dev/alarm and the PM8998 RTC has no allow-set-time, so the clock
# is whatever the RTC's bare second counter says (1971-02-14 on this phone, measured
# 2026-10-27) and a prebuilt ROM's stock framework cannot move it: system_server's
# NetworkTimeUpdateService reaches SystemClock.setCurrentTimeMillis(), which only knows
# how to write through /dev/alarm, and logs "Unable to open alarm driver".
#
# /system/bin/time-sync runs as root, asks the network for the time, calls
# settimeofday() itself and saves (wall time - rtc since_epoch) in
# /data/system/time/rtc_offset. init's rtc-restore service applies that offset at the
# next post-fs-data, so the correct time survives a reboot even with no network.
#
# Waiting policy: run as soon as DHCP has handed out a lease (a lookup against a down
# interface only burns DNS timeouts), then every 12 h. The RTC keeps its own crystal, so
# nothing more frequent is needed; the periodic pass is what pulls a phone back onto the
# right date after days of WiFi being off.
W=/data/local/tmp
LOG=$W/time-sync.log

find_bin() {
    for p in /sbin/$1 /system/bin/$1 $W/$1; do
        if [ -x $p ]; then echo $p; return 0; fi
    done
    return 1
}

log() {
    echo "$(cat /proc/uptime 2>/dev/null) $1" >> $LOG
}

SYNC=$(find_bin time-sync)
if [ -z "$SYNC" ]; then
    log "no time-sync binary in /sbin, /system/bin or $W; giving up"
    exit 0
fi
log "start, using $SYNC"

while true; do
    i=0
    while [ -z "$(getprop dhcp.wlan0.ipaddress)" ]; do
        sleep 10
        i=$((i + 1))
        if [ $i -eq 120 ]; then
            log "no dhcp lease after 20 min, still retrying every minute"
        fi
    done
    ip=$(getprop dhcp.wlan0.ipaddress)
    $SYNC >> $LOG 2>&1
    rc=$?
    log "time-sync exit=$rc via $ip"
    if [ $rc -eq 0 ]; then
        sleep 43200
    else
        sleep 120
    fi
done
