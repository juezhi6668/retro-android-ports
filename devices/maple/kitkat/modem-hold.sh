#!/system/bin/sh
# modem-hold.sh — boot the MSM8998 modem and hold it open (TEST 39).
# See the modem_hold service in init.maple.rc and TEST 39 in
# docs/devices/maple/quirks.md.
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
