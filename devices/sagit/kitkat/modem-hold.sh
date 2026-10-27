#!/system/bin/sh
# modem-hold.sh — boot the MSM8998 modem and hold it open (TEST 39).
# See the modem_hold service in init.sagit.rc and TEST 39 in
# docs/devices/sagit/quirks.md.
#
# Opening /dev/subsys_modem runs subsystem_get_with_fwname("modem"): the PIL
# load (MBA = mba.mbn, then modem.mdt + modem.b00-28, all from
# firmware_class.path=/vendor/firmware) and release-from-reset. The fd must
# stay open for the life of the system — closing it shuts the modem down
# ("icnss: Modem went down, state: 0x80"), and with restart_level=SYSTEM
# that even reboots the phone (seen live 2026-10-17). Stock holds it from
# rild/tad; this port has neither, so hold it here.
#
# WLAN needs the modem up: WLFW ("wlan/fw") is a modem QMI service and the
# modem's WLAN PD only starts once icnss' servreg listener is registered
# (service_locator.enable=1).
#
# Quirks used here:
#  - the char device is created by ueventd from /sys/class/subsys/subsys_modem
#    (maj:min 244:6 live 2026-10-17 — dynamic, so read it, never hardcode);
#    mknod from the class file if coldboot hasn't made the node yet
#  - missing firmware makes the open fail EAGAIN ("can't open ... Try
#    again"), NOT ENOENT (TEST 38 quirk) — so retry, don't bail

# RFS readiness gate (chiron WLAN bring-up, 2026-10-26). The modem's RFSA
# client registers once at boot; if the AP-side RFS/TFTP server (rfssrv2) is
# not listening yet the modem gets no answer and never retries, so
# wlanmdsp.mbn is never fetched and WLFW/wlan0 never appear. wf-setup.sh
# touches the ready file after rfssrv2 announces its ports.
#
# The presence of /sbin/wf-setup.sh *is* the opt-in: it and this script are both
# copied into the boot ramdisk by the same product mk, so an image without the
# WLAN stack (or a system-only flash) never waits, and there is no separate gate
# file to keep in sync. Bounded at 60 s so a broken stack cannot hold the modem
# hostage forever.
if [ -f /sbin/wf-setup.sh ]; then
    g=0
    while [ ! -f /data/local/tmp/rfssrv2.ready ] && [ "$g" -lt 60 ]; do
        echo "modem-hold: waiting for rfssrv2 ($g)" > /dev/kmsg
        sleep 1
        g=$((g + 1))
    done
    echo "modem-hold: gate released" > /dev/kmsg
fi

i=0
while [ ! -c /dev/subsys_modem ] && [ "$i" -lt 60 ]; do
    dev="$(cat /sys/class/subsys/subsys_modem/dev 2>/dev/null)"
    [ -n "$dev" ] && mknod /dev/subsys_modem c "${dev%:*}" "${dev#*:}"
    [ -c /dev/subsys_modem ] || sleep 1
    i=$((i + 1))
done

while :; do
    if exec 9</dev/subsys_modem; then
        echo "modem-hold: /dev/subsys_modem held, modem booting" > /dev/kmsg
        break
    fi
    echo "modem-hold: open /dev/subsys_modem failed, retrying in 5s" > /dev/kmsg
    sleep 5
done

# Keep fd 9 (and the modem) alive; init keeps this process alive.
while :; do
    sleep 3600
done
