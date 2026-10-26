#!/system/bin/sh
# Prove: CLIENT_PORT send stalls on IRSC completion; wlq irsc signals it and the
# stalled send returns. Fresh boot, keeper/cnss stopped.
WL=/data/local/tmp/wlq
LOG=/data/local/tmp/wq.log

echo "before_wait_count=$(dmesg | grep -c 'waiting for IPC Security')"

# a blocking client send (cap mode)
$WL >$LOG 2>&1 &
BG=$!
sleep 5
echo "bg_pid=$BG stalled_count=$(dmesg | grep -c 'waiting for IPC Security')"

# fire the completion
$WL irsc
echo "irsc_exit=$?"

sleep 3
echo "after_count=$(dmesg | grep -c 'waiting for IPC Security')"
echo "--- bg send log (should now show the send returned) ---"
cat $LOG
kill $BG 2>/dev/null
kill -9 $BG 2>/dev/null
echo done
