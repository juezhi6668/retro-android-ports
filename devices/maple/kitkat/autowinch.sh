#!/system/bin/sh
# autowinch.sh — unattended-bringup bootloop guard (reworked 2026-10-19).
#
# The old form was an unconditional `sleep 900; reboot recovery`, which
# killed every good boot 15 min in. This form only returns the phone to TWRP
# when the boot never comes *alive*: no sys.boot_completed AND no running
# adbd within the fuse. Both alive states are adb-debuggable, so the guard
# stands down there; while adb is up it can also be killed outright:
#   adb shell pkill -f autowinch.sh
# The fuse is overridable for experiments (setprop or editing this file):
#   AUTOWINCH_FUSE seconds (default 900).
#
# Why adbd counts as alive: on this port usbudc.sh binds the USB gadget at
# boot and adbd is started by init (`on boot: start adbd`), so a running
# adbd means the phone can be driven without buttons — which is the whole
# job of the guard. A boot that dies before either state is the failure
# this nets (USB silent, screen dark, no way in).
FUSE=${AUTOWINCH_FUSE:-900}
i=0
while [ "$i" -lt "$FUSE" ]; do
    [ "$(getprop sys.boot_completed)" = "1" ] && exit 0
    [ "$(getprop init.svc.adbd)" = "running" ] && exit 0
    sleep 15
    i=$((i + 15))
done
# Still no sign of life: return to TWRP (adb recovery) to be pulled/re-flashed.
setprop sys.powerctl reboot,recovery
