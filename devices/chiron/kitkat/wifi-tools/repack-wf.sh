#!/bin/bash
# repack-wf.sh — refresh the WLAN bring-up stack inside an already-built boot image.
#
# The build path is `make bootimage`: init.<device>.rc carries the wf-* services
# and full_<device>.mk stages /sbin/wf-*.sh, /sbin/rfs-dirs.sh, /sbin/modem-hold.sh
# and the static binaries into the ramdisk. That is slow enough (device-tree sync +
# a full build-system parse) that iterating on the bring-up scripts by hand is
# worth a shortcut, so this one takes a built boot.img, replaces exactly those
# files with the current device-tree copies and repacks the ramdisk.
#
# Run it on WSL: the temp tree has to live on ext4, and mkbootfs on a Windows
# drive silently produces a ramdisk with every file 0700.
#
#   ./repack-wf.sh chiron /mnt/c/Users/16494/.../boot.img
#   ./repack-wf.sh sagit  /home/juezhi/aosp-kitkat/out/target/product/sagit/boot.img
#
# writes <base>-wf.img next to the input. The base must already be a boot image
# from this tree (its ramdisk init.<device>.rc must contain the wf block), so the
# system side of the stack -- /system/bin/sm-vndk, /vendor/bin daemons, the BDF
# files -- is present; only the ramdisk files are refreshed here.
set -e
DEVICE=${1:?usage: repack-wf.sh <sagit|chiron> <boot.img>}
BASE=${2:?usage: repack-wf.sh <sagit|chiron> <boot.img>}
RT=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$RT/../../.." && pwd)
OUT=${BASE%.img}-wf.img
MKBOOTFS=${MKBOOTFS:-/home/juezhi/aosp-kitkat/out/host/linux-x86/bin/mkbootfs}
BIN_DIR=${BIN_DIR:-/home/juezhi/aosp-kitkat/vendor/xiaomi/sagit-bin/bin}

SH=$TREE/sagit/kitkat            # the scripts are shared: same WCN3990, same icnss
DEV=$TREE/$DEVICE/kitkat
[ -f "$BASE" ]      || { echo "FATAL: no base image at $BASE" >&2; exit 1; }
[ -x "$MKBOOTFS" ]  || { echo "FATAL: no mkbootfs at $MKBOOTFS" >&2; exit 1; }
[ -f "$DEV/init.$DEVICE.rc" ] || { echo "FATAL: no $DEV/init.$DEVICE.rc" >&2; exit 1; }
grep -q "service wf-setup" "$DEV/init.$DEVICE.rc" || {
    echo "FATAL: $DEV/init.$DEVICE.rc lost the WLAN block" >&2; exit 1; }
for b in wlq rfssrv2 sm-vndk; do
    [ -x "$BIN_DIR/$b" ] || { echo "FATAL: no $BIN_DIR/$b (run build-wf-bins.sh)" >&2; exit 1; }
done

cd /tmp
rm -rf rdf
if [ -e rdf ]; then
    # A leftover directory owned by another user makes `cpio -idm` skip every
    # file and mkbootfs then silently emit a near-empty ramdisk.
    echo "FATAL: /tmp/rdf exists and could not be removed (chown or sudo rm -rf it)" >&2
    exit 1
fi
mkdir rdf && cd rdf
python3 - <<EOF
import struct
d = open('$BASE','rb').read()
magic,ksize,kaddr,rsize,raddr,ssize,saddr,tags,page,dt = struct.unpack_from('<8s9I', d, 0)
kn = (ksize + page - 1)//page*page
open('ramdisk.gz','wb').write(d[page+kn:page+kn+rsize])
print('page',page,'ksize',ksize,'rsize',rsize)
EOF
gunzip -c ramdisk.gz | cpio -idm 2>/dev/null
rm -f ramdisk.gz

# The in-tree rc replaces the one in the image wholesale -- including its
# comments, so what boots is what is in git.
cp "$DEV/init.$DEVICE.rc" "init.$DEVICE.rc"
for f in wf-setup.sh wf-efs-keeper.sh wf-wifi-gate.sh rfs-dirs.sh modem-hold.sh; do
    cp "$SH/$f" "sbin/$f"
    chmod 755 "sbin/$f"
done
for b in wlq rfssrv2 sm-vndk; do
    cp "$BIN_DIR/$b" "sbin/$b"
    chmod 755 "sbin/$b"
done
# The device-tree files are edited from Windows, so they can arrive CRLF; a
# stray \r kills the #! line and every `[ -x ... ]` test on the device.
sed -i 's/\r$//' "init.$DEVICE.rc" sbin/wf-setup.sh sbin/wf-efs-keeper.sh \
    sbin/wf-wifi-gate.sh sbin/rfs-dirs.sh sbin/modem-hold.sh
# Older images gated the modem boot on /etc/rfs_gate; modem-hold.sh now keys off
# the presence of /sbin/wf-setup.sh, so the marker has to go or a stale file
# keeps it waiting.
rm -f etc/rfs_gate

for f in "init.$DEVICE.rc" sbin/wf-setup.sh sbin/rfssrv2 sbin/sm-vndk sbin/wlq; do
    [ -e "$f" ] || { echo "FATAL: $f missing from the staged ramdisk" >&2; exit 1; }
done

$MKBOOTFS /tmp/rdf | gzip -9 > ramdisk.gz
NEW=$(stat -c%s ramdisk.gz)
echo "new ramdisk: $NEW bytes"
if [ "$NEW" -lt 250000 ]; then
    echo "FATAL: ramdisk is only $NEW bytes, refusing to write $OUT" >&2
    exit 1
fi

python3 - <<EOF
import struct
src = open('$BASE','rb').read()
magic,ksize,kaddr,rsize,raddr,ssize,saddr,tags,page,dt = struct.unpack_from('<8s9I', src, 0)
kn = (ksize + page - 1)//page*page
ram = open('ramdisk.gz','rb').read()
rn = (len(ram) + page - 1)//page*page
hdr = bytearray(src[:page])
struct.pack_into('<I', hdr, 16, len(ram))
out = bytes(hdr) + src[page:page+kn] + ram + b'\0'*(rn-len(ram))
open('$OUT','wb').write(out)
print('old rsize', rsize, 'new rsize', len(ram), 'total', len(out))
EOF
ls -la "$OUT"
md5sum "$OUT"
