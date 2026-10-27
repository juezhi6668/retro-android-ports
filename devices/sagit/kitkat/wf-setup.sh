#!/system/bin/sh
# wf-setup.sh — WLAN stack prerequisites at boot, run by init (oneshot).
# Shared by sagit + chiron (same msm8998 SoC, same WCN3990, same 4.4 kernel).
#
# Order matters: the modem must not power up until rfssrv2 owns its RFSA/TFTP
# ports, because the modem's RFSA client registers once at boot and never
# retries. So this script prepares the tree, launches rfssrv2, opens the
# ready file that /sbin/modem-hold.sh waits on, and only then starts the
# EFS/cnss keeper (rmt_storage/cnss-daemon) and the QMI helpers.
#
# Binary lookup: the ramdisk (/sbin) first, then /system/bin, then
# /data/local/tmp. That order lets a boot image carry its own copy (the
# chiron repack path) while a normal AOSP build installs them into /system.
W=/data/local/tmp
LOG=$W/wf-setup.log

find_bin() {
    for p in /sbin/$1 /system/bin/$1 $W/$1; do
        if [ -x $p ]; then echo $p; return 0; fi
    done
    return 1
}

# /dev/block/bootdevice/by-name — the path rmt_storage (and therefore the modem's
# whole EFS stack) resolves its partitions through. This kernel's ueventd already
# builds the complete set one level up, at /dev/block/platform/soc/by-name
# (measured on chiron 2026-10-27: modemst1=sdf4, modemst2=sdf5, fsc=sdf1,
# fsg=sde34, persist=sda14 — the same devices sagit's stock dump
# dumps/sagit-stock-20261024-014254/by-name-bootdevice.txt lists); only the
# bootdevice alias androidboot.bootdevice would normally create is missing.
#
# This is not a cosmetic path. rmt_storage exits on "Failed to open partition
# /dev/block/bootdevice/by-name/modemst1", the modem then re-crashes on its ~41 s
# timer, and while the root PD is down icnss never sees a WLFW server at all
# (SERVER_ARRIVE 0) — so no BDF, no FW_READY, no wlan0.
#
# The previous version of this block made the directory with
# `mkdir -p /mnt/vendor /dev/block/bootdevice/by-name`, and KitKat's mkdir stops
# at the first operand that fails — /mnt is the read-only ramdir — so on a fresh
# image by-name was never created at all.
if [ -d /dev/block/platform/soc/by-name ]; then
    [ -e /dev/block/bootdevice ] || ln -s /dev/block/platform/soc /dev/block/bootdevice
else
    echo "no /dev/block/platform/soc/by-name: rmt_storage cannot reach modemst1" >> $LOG
fi
[ -e /dev/vb ] || ln -s /dev/vndbinder /dev/vb
mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null

RFS=$(find_bin rfs-dirs.sh)
if [ -n "$RFS" ]; then
    sh $RFS >$W/rfs-dirs.log 2>&1
else
    # Bare `sh` (empty $RFS) would read stdin instead of failing, so gate it.
    echo "no rfs-dirs.sh found; the modem's RFS tree is missing" >> $LOG
fi

RFSSRV2=$(find_bin rfssrv2)
rm -f $W/rfssrv2.ready
# KitKat has no nohup; the ( ... & ) subshell detaches the child so it is
# re-parented to init and outlives this script.
[ -n "$RFSSRV2" ] && ( $RFSSRV2 >$W/rfssrv2.log 2>&1 & )

i=0
while [ "$i" -lt 10 ]; do
    if grep -q "rfssrv2 on" $W/rfssrv2.log 2>/dev/null; then
        sleep 2          # banner is printed before the ports are bound
        touch $W/rfssrv2.ready
        echo "rfssrv2 listening" >> $LOG
        break
    fi
    sleep 1
    i=$((i + 1))
done
[ -f $W/rfssrv2.ready ] || echo "rfssrv2 NOT ready, releasing gate anyway" >> $LOG
touch $W/rfssrv2.ready

# Fire the ipc-router IRSC completion BEFORE anything starts talking QMI.
# On msm8998 legacy ipc-router v1 every user-space CLIENT_PORT sendmsg waits in
# wait_for_irsc_completion(), and only releasing an IRSC_PORT (a socket that got
# IPC_ROUTER_IOCTL_CONFIG_SEC_RULES) signals it. wlq is our static loader: it
# installs sec_config and closes the socket, which unblocks cnss-daemon's
# wlfw_bdf_download_req for the rest of the boot. This has to happen here rather
# than from the /system boot-log hook: that hook is a class-main service and
# only starts at ~27 s, which pushes FW_READY to ~39 s -- after KitKat's
# WifiService has already tried (and failed) to start wpa_supplicant at ~28 s,
# and its state machine never recovers from that.
# init cannot exec the 64-bit vendor irsc_util (it exits silently), which is why
# the old `service wf-irsc` never did anything; wlq is static and root, so
# neither LD_LIBRARY_PATH nor sec_config group ids matter here.
WLQ=$(find_bin wlq)
SEC=/system/vendor/etc/sec_config
[ -f $SEC ] || SEC=/vendor/etc/sec_config
if [ -n "$WLQ" ] && [ -f $SEC ]; then
    $WLQ irsc $SEC >$W/wlq-irsc.log 2>&1
    echo "irsc fired: $(cat $W/wlq-irsc.log)" >> $LOG
else
    echo "no wlq ($WLQ) or no sec_config ($SEC), IRSC never fired" >> $LOG
fi

start wf-efs
start wf-sm

# Lock the supplicant config until the driver exists.
#
# wifi_start_supplicant() calls ensure_config_file_exists() *before* it touches
# init, and that check fails hard ("Wi-Fi will not be enabled") when the file is
# not readable-writable by uid system. The failure is the good one: it happens
# before the service is started, so the state machine never reaches
# WifiMonitor.startMonitoring() and simply reports "Failed to start supplicant!"
# while staying in InitialState.
#
# That matters because startMonitoring() against a dead supplicant burns its 6
# connect tries and then does mIfaceMap.remove(iface) (WifiMonitor.java:436).
# Nothing ever puts wlan0 back in that map, so every later enable logs
# "startMonitor called with unknown iface=wlan0" and the state machine is dead
# for the rest of the boot. Without the lock the boot-time attempt at ~29 s
# lands on one of those two outcomes by luck -- whether init's transient
# "running" property is observed before the supplicant dies trying to open a
# wlan0 that does not exist until ~39 s.
#
# wf-wifi-gate.sh restores system:wifi 0660 the moment wlan0 appears and then
# drives one real off->on toggle, which is the only sequence SettingsStore
# accepts (handleWifiToggled() ignores a request for the state it already
# tracks, so a bare `svc wifi enable` is a no-op once wifi_on is 1).
SUPPCONF=/data/misc/wifi/wpa_supplicant.conf
touch $SUPPCONF 2>/dev/null
chown 0:0 $SUPPCONF 2>/dev/null
chmod 600 $SUPPCONF 2>/dev/null
echo "locked $SUPPCONF until the driver is up" >> $LOG
