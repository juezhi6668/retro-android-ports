#!/system/bin/sh
# wf-wifi-gate.sh — gives the user-facing WiFi switch a working framework.
#
# The WCN3990 cannot be ready when the framework first asks for it. WifiService
# auto-enables at ~28 s (Settings.Global.wifi_on is 1) by starting
# wpa_supplicant, but wlan0 only registers after icnss reaches FW_READY, which
# needs the modem up (~27 s) plus the wlanmdsp load plus cnss-daemon's BDF
# download: measured 39 s. So the boot-time attempt is always made against a
# missing interface.
#
# /sbin/wf-setup.sh keeps /data/misc/wifi/wpa_supplicant.conf root:root 0600
# during that window so the attempt fails in wifi.c's
# ensure_config_file_exists(), before init is touched and before the state
# machine calls WifiMonitor.startMonitoring(). Failing there is recoverable;
# failing after is not, because startMonitoring() gives up after 6 tries and
# removes wlan0 from its iface map (WifiMonitor.java:436) for good -- later
# enables then log "startMonitor called with unknown iface=wlan0" and do nothing.
#
# So this script's job is: wait for the driver, put the config back the way
# wifi.c wants it (system:wifi 0660), and perform one genuine off->on toggle.
# The toggle has to be both edges: SettingsStore.handleWifiToggled() returns
# false for a request that matches the state it already tracks, and WifiService
# then skips the controller entirely, so `svc wifi enable` while wifi_on is
# already 1 silently does nothing. Verified on-device: after the driver is up,
# disable->enable lands on "Wi-Fi is enabled", Supplicant state COMPLETED and a
# DHCP lease.
#
# After that the framework owns WiFi again; every later user toggle works
# through the normal path and this script exits. It never enables WiFi against
# the user: it reads the framework's persisted wifi_on each pass and steps aside
# if that is not 1.
LOG=/data/local/tmp/wf-wifi-gate.log
IFACE=/sys/class/net/wlan0
CONF=/data/misc/wifi/wpa_supplicant.conf
DUMP=/data/local/tmp/wf-gate-dump.txt

log() {
    echo "$(cat /proc/uptime 2>/dev/null) $1" >> $LOG
}

framework_enabled() {
    dumpsys wifi > $DUMP 2>/dev/null
    grep -q 'Wi-Fi is enabled' $DUMP
}

i=0
while [ ! -d $IFACE ]; do
    sleep 2
    i=$((i + 1))
    if [ $i -gt 90 ]; then
        log "wlan0 never appeared; leaving the config locked"
        exit 0
    fi
done
log "wlan0 present, unlocking $CONF"

chown 1000:1010 $CONF 2>/dev/null
chmod 660 $CONF 2>/dev/null
sleep 2                      # let the freshly probed interface settle

t=0
while [ $t -lt 5 ]; do
    if [ "$(settings get global wifi_on 2>/dev/null)" != 1 ]; then
        log "wifi_on is not 1, stepping aside"
        exit 0
    fi
    if framework_enabled; then
        log "framework reports Wi-Fi enabled"
        exit 0
    fi
    t=$((t + 1))
    log "cycle #$t: svc wifi disable + enable"
    svc wifi disable
    sleep 4
    svc wifi enable
    sleep 14
done
log "gave up after $t cycles"
