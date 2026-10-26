#!/bin/bash
# repack-wf.sh — rebuild the chiron boot ramdisk with the WLAN bring-up stack.
#
# The stack has to live in the boot ramdisk, not in /system: init only runs
# /sbin/*.sh from the ramdisk at post-fs-data (~4 s), which is the earliest
# point at which the modem can be gated and the IRSC completion fired — see
# wifi-progress.md. Unpacks $BASE (the known-good chiron boot image) fresh every
# time, appends the wf block to init.chiron.rc, stages the scripts and wlq into
# /sbin, repacks and patches the header. Run on WSL so the temp tree is on ext4.
#
#   WS=/mnt/c/Users/16494/Documents/Qoder/2026-10-23/4eba4900 ./repack-wf.sh
set -e
WS=${WS:-/mnt/c/Users/16494/Documents/Qoder/2026-10-23/4eba4900}
RT=$(cd "$(dirname "$0")" && pwd)
BASE=$WS/boot-good.img
OUT=$WS/boot-wf.img
MKBOOTFS=/home/juezhi/aosp-kitkat/out/host/linux-x86/bin/mkbootfs
WLQ=${WLQ:-/home/juezhi/aosp-kitkat/vendor/xiaomi/sagit-bin/bin/wlq}
MODEM_HOLD=$RT/../../../sagit/kitkat/modem-hold.sh

[ -f "$BASE" ] || { echo "FATAL: no base image at $BASE (set WS=)" >&2; exit 1; }
[ -x "$WLQ" ] || { echo "FATAL: no wlq at $WLQ (run build-wlq.sh)" >&2; exit 1; }
[ -f "$MODEM_HOLD" ] || { echo "FATAL: no modem-hold.sh at $MODEM_HOLD" >&2; exit 1; }

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

grep -q "service wf-setup" init.chiron.rc && {
    echo "FATAL: $BASE already contains the wf block; unpack a pristine image" >&2
    exit 1
}
cat "$RT/wf-block-append.rc" >> init.chiron.rc
# post-fs-data runs with /system mounted, but keep the scripts in the ramdisk
# anyway (/sbin) so they also work if a trigger is moved earlier and so they
# survive a system-image swap.
for f in wf-setup.sh wf-efs-keeper.sh wf-wifi-gate.sh; do
    cp "$RT/$f" "sbin/$f"
    chmod 755 "sbin/$f"
done
cp "$MODEM_HOLD" sbin/modem-hold.sh
chmod 755 sbin/modem-hold.sh

# Static IRSC loader. It has to be in the ramdisk because wf-setup.sh fires the
# ipc-router completion at post-fs-data, and /sbin is the only path guaranteed
# to exist that early (and to survive a system-image swap).
cp "$WLQ" sbin/wlq
chmod 755 sbin/wlq
# The device-tree sources are edited from Windows too, so they can arrive CRLF;
# a stray \r kills the #! line and every `[ -x ... ]` test on the device.
sed -i 's/\r$//' sbin/wf-setup.sh sbin/wf-efs-keeper.sh sbin/wf-wifi-gate.sh \
    sbin/modem-hold.sh init.chiron.rc

# Gate the modem boot on rfssrv2 readiness (see modem-hold.sh). The gate file is
# what opts a boot image into waiting, so sagit builds stay unaffected.
mkdir -p etc
: > etc/rfs_gate

# init starts the whole post-fs-data action list in file order, so modem_hold
# would still be started before wf-setup could signal readiness; `disabled`
# makes init ignore the early start, and wf-setup starts it after the gate.
python3 - <<'EOF'
import re
src = open('init.chiron.rc', 'rb').read().decode('utf-8', errors='replace')
m = re.search(r'service modem_hold[^\n]*\n(?:[ \t][^\n]*\n)+', src)
assert m, 'modem_hold service stanza not found'
stanza = m.group(0)
if 'disabled' not in stanza:
    src = src.replace(stanza, stanza.rstrip('\n') + '\n    disabled\n', 1)
open('init.chiron.rc', 'wb').write(src.encode('utf-8'))
EOF
sed -n '/service modem_hold/,/^$/p' init.chiron.rc
echo "--- wf services ---"
grep -n "service wf-" init.chiron.rc

for f in init.chiron.rc sbin/wf-setup.sh sbin/wf-efs-keeper.sh sbin/wf-wifi-gate.sh \
         sbin/modem-hold.sh sbin/wlq etc/rfs_gate; do
    [ -e "$f" ] || { echo "FATAL: $f missing from the staged ramdisk" >&2; exit 1; }
done

$MKBOOTFS /tmp/rdf | gzip -9 > ramdisk.gz
NEW=$(stat -c%s ramdisk.gz)
echo "new ramdisk: $NEW bytes"
# boot-good.img's own ramdisk is ~330 KB and the stack adds a static wlq;
# anything far smaller means the extraction or the mkbootfs walk went wrong and
# the image would not boot.
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
