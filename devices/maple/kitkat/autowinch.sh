#!/system/bin/sh
# autowinch.sh — DIAG for maple KitKat bringup: no adb yet, so a stuck
# boot needs manual button dances to recover. This returns the phone to
# TWRP automatically 15 min after boot. REMOVE once adb works (or keep
# with a much longer fuse as a bootloop guard).
sleep 900
setprop sys.powerctl reboot,recovery
