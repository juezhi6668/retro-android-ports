#!/bin/bash
# ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
# unpacks a lineage 20 maple build into a sysroot that stage.py can walk. bionic lives in the
# runtime apex on 13, so the linker and libc get pulled out of it into system/bin and system/lib,
# and every apex's libs land in apexlib/ for anything else the adreno closure needs.
# yoshino's lineage vendor is its own image (the oem partition), take it from the zip or pass
# a dump of the phone's oem partition, which kitkat never touched.
# usage: extract-lineage.sh <lineage zip | system.img> <out dir> [vendor.img | vendor dir]
# needs unzip, brotli, debugfs (e2fsprogs) and python3, plus unsquashfs for squashfs
# images (later unofficial maple builds) or fsck.erofs for erofs
set -e
IN=$(realpath "$1")
OUT=$(realpath -m "$2")
VENDOR=${3:+$(realpath "$3")}
rm -rf "$OUT" && mkdir -p "$OUT/work"
W=$OUT/work

# the block map in <name>.transfer.list, the same thing sdat2img does
sdat_to_img() {
    unzip -q -o "$IN" "$1.new.dat.br" "$1.transfer.list" -d "$W"
    brotli -d -f "$W/$1.new.dat.br" -o "$W/$1.new.dat"
    python3 -I - "$W/$1.transfer.list" "$W/$1.new.dat" "$W/$1.img" <<'PY'
import sys
lst, dat, out = sys.argv[1:4]
lines = open(lst).read().split("\n")
blocks = int(lines[1])
with open(dat, "rb") as src, open(out, "wb") as dst:
    for line in lines[4:]:
        parts = line.split(" ")
        if parts[0] != "new":
            continue
        r = [int(x) for x in parts[1].split(",")[1:]]
        for start, end in zip(r[0::2], r[1::2]):
            dst.seek(start * 4096)
            dst.write(src.read((end - start) * 4096))
    dst.truncate(blocks * 4096)
PY
    rm "$W/$1.new.dat.br" "$W/$1.new.dat"
}

unpack_img() {
    if [ -d "$1" ]; then
        cp -a "$1" "$2"
        return
    fi
    if [ "$(od -An -tx4 -N4 "$1" | tr -d ' ')" = "ed26ff3a" ]; then
        echo "$1 is sparse, run simg2img on it first" >&2
        exit 1
    fi
    if [ "$(od -An -c -N4 "$1" | tr -d ' ')" = "hsqs" ]; then
        unsquashfs -q -n -no-xattrs -d "$2" "$1" >/dev/null
        return
    fi
    mkdir -p "$2"
    if [ "$(od -An -tx4 -j1024 -N4 "$1" | tr -d ' ')" = "e0f5e1e2" ]; then
        fsck.erofs --extract="$2" "$1"
        return
    fi
    # -c skips the bitmaps, which android's image tools leave in a state debugfs refuses
    debugfs -c -R "rdump / $2" "$1" 2>/dev/null
}

sysimg=$IN
if unzip -l "$IN" >/dev/null 2>&1; then
    if unzip -l "$IN" payload.bin >/dev/null 2>&1; then
        echo "a/b payload zip, dump system.img and vendor.img with a payload dumper and pass those" >&2
        exit 1
    fi
    sdat_to_img system
    sysimg=$W/system.img
    if [ -z "$VENDOR" ] && unzip -l "$IN" vendor.new.dat.br >/dev/null 2>&1; then
        sdat_to_img vendor
        VENDOR=$W/vendor.img
    fi
fi

unpack_img "$sysimg" "$W/sys"
# system as root images have system/ inside, older ones are the system dir itself
if [ -f "$W/sys/system/build.prop" ]; then
    mv "$W/sys/system" "$OUT/system"
else
    mv "$W/sys" "$OUT/system"
fi
if [ -n "$VENDOR" ]; then
    unpack_img "$VENDOR" "$OUT/vendor"
elif [ -d "$OUT/system/vendor/lib" ]; then
    mv "$OUT/system/vendor" "$OUT/vendor"
fi

mkdir -p "$OUT/apexlib" "$W/apex"
for a in "$OUT"/system/apex/*.apex "$OUT"/system/apex/*.capex; do
    [ -f "$a" ] || continue
    n=$(basename "$a")
    d=$W/apex/$n
    mkdir -p "$d"
    unzip -q -o "$a" -d "$d"
    # compressed apexes wrap the real one
    if [ -f "$d/original_apex" ]; then
        unzip -q -o "$d/original_apex" apex_payload.img -d "$d"
    fi
    [ -f "$d/apex_payload.img" ] || continue
    mkdir -p "$d/root"
    debugfs -c -R "rdump / $d/root" "$d/apex_payload.img" 2>/dev/null || continue
    if [ -d "$d/root/lib" ]; then
        find "$d/root/lib" -maxdepth 2 -name '*.so' -type f -exec cp -n -t "$OUT/apexlib" {} +
    fi
    case $n in
    com.android.runtime.apex|com.android.runtime.capex)
        rm -f "$OUT/system/bin/linker"
        cp "$d/root/bin/linker" "$OUT/system/bin/linker"
        # system/lib only has symlinks into /apex for these. the apex's own symlinks point back
        # at system/lib, which already has those
        for so in "$d"/root/lib/bionic/*.so "$d"/root/lib/*.so; do
            [ -L "$so" ] && continue
            rm -f "$OUT/system/lib/$(basename "$so")"
            cp "$so" "$OUT/system/lib/"
        done
        ;;
    esac
done
rm -rf "$W"

for f in system/bin/linker system/lib/libc.so vendor/lib/egl/libGLESv2_adreno.so; do
    if [ ! -f "$OUT/$f" ] || [ -L "$OUT/$f" ]; then
        echo "missing $f in the extracted build" >&2
        exit 1
    fi
done
echo "sysroot ready in $OUT"
