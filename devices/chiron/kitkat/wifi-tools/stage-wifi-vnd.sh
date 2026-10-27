#!/usr/bin/env bash
# stage-wifi-vnd.sh — rebuild the out-of-git WLAN vendor staging tree.
#
# full_sagit.mk and full_chiron.mk install the 64-bit Qualcomm WLAN daemons and
# their library closure from $(WIFI_VND) = vendor/xiaomi/sagit-wifi/{bin,lib64,
# system-lib64}. They cannot be built from this tree (KitKat builds arm-only and
# ships none of the QC userspace) and they are device dumps, so they stay out of
# git like the kernel blob and sagit-hyb/sagit-bin. A fresh checkout therefore
# needs this script to recreate the staging dir from a sagit/chiron phone running
# its stock (Android 8/O3-ish) firmware. Nothing on the phone is touched: pull only.
#
# Run it from Windows, where adb is:
#
#   C:\...\wifi-tools\stage-wifi-vnd.sh
#
# The sets below are the load-map closure of cnss-daemon + rmt_storage as it ran
# on the working chiron (docs/devices/chiron/wifi-progress.md). The bionic set is
# taken from /system/lib64 of that same phone, not from another build: the 64-bit
# linker namespace is only self-consistent within one generation, and OS3's
# /system/lib64 copied off a different image gave a libc that did not match the
# vendor blobs.
set -euo pipefail
export MSYS_NO_PATHCONV=1

PULL=${PULL:-C:/wifi-vnd-pull}
A=${1:-//wsl.localhost/Ubuntu-20.04/home/juezhi/aosp-kitkat}
S=$A/vendor/xiaomi/sagit-wifi
# the same directory seen from inside WSL, for the ownership fix-up
W=/home/juezhi/aosp-kitkat/vendor/xiaomi/sagit-wifi

BIN="cnss-daemon irsc_util pd-mapper rmt_storage tftp_server"
VLIB="libapexsupport libbase libbinder libc++ libcld80211 libcutils libdiag libidl
      libjson liblog libmdmdetect libnetutils libnl libpackagelistparser libpcre2
      libperfetto_c libperipheral_client libqmi_cci libqmi_common_so libqmi_csi
      libqmi_encdec libqmiservices libqrtr libqsocket libselinux libtinyxml2
      libutils libvintf libvndksupport libxml2 libz"
SLIB="ld-android libapexsupport libc libdl libdl_android liblog libm libvndksupport"

cd "$(dirname "$0")"
command -v adb >/dev/null || { echo "FATAL: no adb in PATH" >&2; exit 1; }
adb wait-for-device

rm -rf "$PULL"
mkdir -p "$PULL/bin" "$PULL/lib64" "$PULL/system-lib64"
for f in $BIN;  do adb pull "/vendor/bin/$f"      "$PULL/bin/$f";            done
for f in $VLIB; do adb pull "/vendor/lib64/$f.so" "$PULL/lib64/$f.so";       done
for f in $SLIB; do adb pull "/system/lib64/$f.so" "$PULL/system-lib64/$f.so"; done
# The daemons above are aarch64 with PT_INTERP /system/bin/linker64. This is the
# only file of the set that KitKat's build replaces with nothing at all, and if
# it is missing exec() of every daemon fails with plain ENOENT.
adb pull /system/bin/linker64 "$PULL/linker64"

mkdir -p "$S"
for d in bin lib64 system-lib64; do
    cp "$PULL/$d/"* "$S/$d/"
done
cp "$PULL/linker64" "$S/linker64"
# The tree's vendor/xiaomi is root-owned from the earlier libhyb staging, so the
# first mkdir/cp through the UNC path lands files as root and the build (which
# runs as juezhi) cannot read them. -u root avoids the sudo password prompt.
wsl.exe -d Ubuntu-20.04 -u root -- chown -R juezhi:juezhi "$W"
wsl.exe -d Ubuntu-20.04 -u root -- chmod -R a+rX "$W"

echo "--- staged in $S ---"
for d in bin lib64 system-lib64; do
    n=$(ls "$S/$d" | wc -l)
    echo "$d: $n files, $(du -sh "$S/$d" | cut -f1)"
done
# Every one of these is a real ELF; an adb pull that hit a missing source file
# leaves a truncated or empty copy behind, which the daemons then fail on with a
# linker error at boot rather than anything readable in the log.
bad=$(find "$S" -type f -size -1k | wc -l)
[ "$bad" -eq 0 ] || { echo "FATAL: $bad staged file(s) under 1 KiB — recheck the pulls" >&2; exit 1; }
