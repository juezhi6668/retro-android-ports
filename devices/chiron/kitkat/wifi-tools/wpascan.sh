#!/system/bin/sh
rm -f /data/local/tmp/wpas.err
/system/bin/wpa_supplicant -Dnl80211 -iwlan0 -c/data/misc/wifi/wpa_supplicant.conf -dd >/data/local/tmp/wpas.err 2>&1 &
P=$!
sleep 3
echo "pid=$P alive=$(ps | grep -c wpa_supplicant)"
echo "--- supplicant stderr (head) ---"
cat /data/local/tmp/wpas.err
echo "--- status ---"
wpa_cli -p/data/misc/wifi/sockets -iwlan0 status
echo "--- scan ---"
wpa_cli -p/data/misc/wifi/sockets -iwlan0 scan
sleep 7
echo "--- scan_results ---"
wpa_cli -p/data/misc/wifi/sockets -iwlan0 scan_results
kill $P 2>/dev/null
echo done
