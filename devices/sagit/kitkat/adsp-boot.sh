#!/system/bin/sh
# adsp-boot.sh — boot the ADSP, sequenced after the modem.
# Mi 6 adaptation of maple's script: the modem subsystem is resolved by NAME
# (maple hardcoded subsys6; the indices are board/kernel dependent).
#
# `echo 1 > /sys/kernel/boot_adsp/boot` is the TEST 36 discovery that
# unblocked the whole audio stack; the write is synchronous and PIL loads
# adsp.mdt + adsp.b00-* from /vendor/firmware.
#
# The wait exists because with service_locator.enable=1 the audio side
# (card via audio_notifier, slimbus) registers through the servreg/PDR
# service locator, whose domain data comes from the modem's servreg_loc QMI
# service. The wait is bounded (20 s): audio must come up even if the modem
# never does.

modem_state() {
    for d in /sys/bus/msm_subsys/devices/*; do
        if [ "$(cat $d/name 2>/dev/null)" = "modem" ]; then
            cat $d/state 2>/dev/null
            return
        fi
    done
}

i=0
while [ "$i" -lt 20 ]; do
    [ "$(modem_state)" = "ONLINE" ] && break
    sleep 1
    i=$((i + 1))
done

echo "adsp-boot: modem '$(modem_state)' after ${i}s wait, booting adsp" > /dev/kmsg
echo 1 > /sys/kernel/boot_adsp/boot
echo "adsp-boot: boot_adsp write returned" > /dev/kmsg
