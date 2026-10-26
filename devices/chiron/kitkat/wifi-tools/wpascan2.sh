#!/system/bin/sh
# Isolate the config-permission failure from any nl80211 problem by using a
# conf and ctrl dir under /data/local/tmp (root-searchable, world-readable).
D=/data/local/tmp/wp
rm -rf $D; mkdir -p $D/sock
echo "ctrl_interface=$D/sock" > $D/wp.conf
echo "ap_scan=1" >> $D/wp.conf
echo "update_config=1" >> $D/wp.conf
chmod -R 777 $D
echo "--- conf ---"; cat $D/wp.conf
/system/bin/wpa_supplicant -Dnl80211 -iwlan0 -c$D/wp.conf -dd >/dev/null 2>&1 &
P=$!
sleep 6
echo "pid=$P alive=$(ps | grep -c '[w]pa_supplicant')"
echo "--- dmesg wpa ---"; dmesg | grep -i wpa_supplicant | grep -viE "cancell|P2P:" 
echo "--- status ---"; wpa_cli -p$D/sock -iwlan0 status 2>&1
echo "--- scan ---"; wpa_cli -p$D/sock -iwlan0 scan 2>&1
sleep 8
echo "--- scan_results ---"; wpa_cli -p$D/sock -iwlan0 scan_results 2>&1
kill $P 2>/dev/null
echo done
