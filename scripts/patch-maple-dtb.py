#!/usr/bin/env python3
# Patch the maple (Sony Xperia XZ Premium) kernel blob to disable mdss idle
# power collapse.
#
# The stock Image.gz-dtb has `qcom,mdss-idle-power-collapse-enabled` in the
# MDSS MDP node of every appended DTB. With it set, mdss powers the MDP down
# when the display goes idle; on maple the panel then goes black (the overlay
# stops scanning fb0) and the next fbdev flip is stalled, so a static UI
# disappears even though fb0 still holds correct frames. Removing the property
# (the pdx203-jellybean fix) keeps the panel scanning the last frame.
#
# This edits the kernel blob in place, which is a boot-time-only artifact and
# is NOT in git (see docs/devices/maple.md). Run it on the dumped kernel before
# a boot image is built:
#
#   scripts/patch-maple-dtb.py vendor/sony/maple/kernel
#
# Idempotent: a kernel whose DTBs already lack the property is left unchanged.
# Needs `dtc` (device-tree-compiler) on PATH.
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

PROP = "qcom,mdss-idle-power-collapse-enabled"
FDT_MAGIC = b"\xd0\x0d\xfe\xed"


def split_kernel(data):
    # Image.gz-dtb = gzip(Image) with the DTBs appended uncompressed. zlib tells
    # us exactly where the gzip stream ends via unused_data.
    d = zlib.decompressobj(16 + zlib.MAX_WBITS)
    d.decompress(data)
    gzlen = len(data) - len(d.unused_data)
    return data[:gzlen], d.unused_data


def parse_dtbs(region):
    dtbs, off = [], 0
    while off < len(region):
        if region[off:off + 4] != FDT_MAGIC:
            raise SystemExit("bad FDT magic at offset %d" % off)
        total = struct.unpack(">I", region[off + 4:off + 8])[0]
        dtbs.append(region[off:off + total])
        off += total
    return dtbs


def patch_dtb(dtb, dtc):
    with tempfile.TemporaryDirectory() as td:
        src = os.path.join(td, "in.dtb")
        dts = os.path.join(td, "in.dts")
        out = os.path.join(td, "out.dtb")
        with open(src, "wb") as f:
            f.write(dtb)
        subprocess.run([dtc, "-I", "dtb", "-O", "dts", "-o", dts, src],
                       check=True, stderr=subprocess.DEVNULL)
        with open(dts) as f:
            text = f.read()
        if PROP + ";" not in text:
            return dtb
        kept = [ln for ln in text.splitlines() if ln.strip() != PROP + ";"]
        with open(dts, "w") as f:
            f.write("\n".join(kept) + "\n")
        subprocess.run([dtc, "-I", "dts", "-O", "dtb", "-o", out, dts],
                       check=True, stderr=subprocess.DEVNULL)
        with open(out, "rb") as f:
            return f.read()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("kernel", help="Image.gz-dtb kernel blob (edited in place)")
    ap.add_argument("--dtc", default="dtc", help="path to dtc (default: dtc)")
    ap.add_argument("--no-backup", action="store_true",
                    help="do not keep a .orig copy")
    args = ap.parse_args()

    with open(args.kernel, "rb") as f:
        data = f.read()
    gz, region = split_kernel(data)
    dtbs = parse_dtbs(region)

    patched, changed = [], 0
    for i, dtb in enumerate(dtbs):
        new = patch_dtb(dtb, args.dtc)
        if new != dtb:
            changed += 1
        patched.append(new)
        print("dtb %d: %s (%d -> %d bytes)" %
              (i, "patched" if new != dtb else "unchanged", len(dtb), len(new)))

    if not changed:
        print("nothing to do: %s already absent in all %d DTBs" %
              (PROP, len(dtbs)))
        return 0

    if not args.no_backup and not os.path.exists(args.kernel + ".orig"):
        shutil.copy2(args.kernel, args.kernel + ".orig")

    out = gz + b"".join(patched)
    with open(args.kernel, "wb") as f:
        f.write(out)
    print("wrote %s (%d -> %d bytes), backup at %s" %
          (args.kernel, len(data), len(out), args.kernel + ".orig"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
