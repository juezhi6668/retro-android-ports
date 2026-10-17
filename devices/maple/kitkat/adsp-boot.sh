#!/system/bin/sh
# adsp-boot.sh — boot the ADSP, sequenced after the modem (TEST 39).
# See the adsp_boot service in init.maple.rc.
#
# `echo 1 > /sys/kernel/boot_adsp/boot` is the TEST 36 discovery that
# unblocked the whole audio stack (tavil/tasha slim probes, WSA amps, Q6
# DAIs, APR, card all came up within a second); the write is synchronous
# and PIL loads adsp.mdt + adsp.b00-* from /vendor/firmware.
#
# The wait exists because with service_locator.enable=1 the audio side
# (tavil card via audio_notifier, slimbus) registers through the
# servreg/PDR service locator and the locator's domain data comes from the
# modem's servreg_loc QMI service — CHARQUIRKS' "the card never registers"
# was exactly the locator enabled with no modem up. The wait is bounded
# (20 s): audio must come up even if the modem never does.

i=0
while [ "$i" -lt 20 ]; do
    [ "$(cat /sys/bus/msm_subsys/devices/subsys6/state 2>/dev/null)" = "ONLINE" ] && break
    sleep 1
    i=$((i + 1))
done

echo "adsp-boot: modem state '$(cat /sys/bus/msm_subsys/devices/subsys6/state 2>/dev/null)' after ${i}s wait, booting adsp" > /dev/kmsg
echo 1 > /sys/kernel/boot_adsp/boot
echo "adsp-boot: boot_adsp write returned" > /dev/kmsg
