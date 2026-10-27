#!/usr/bin/env python3
"""check-bootimg.py — dump a boot.img header and prove its ramdisk is complete.

Why this exists: on msm8998 KitKat boot images (stock dump AND `make bootimage`
for sagit/chiron) the header ramdisk-size field is a 32768-byte page-aligned
stub while the gzip ramdisk stream actually continues into the next slot. Parse
it naively and the ramdisk looks truncated (gunzip stops at ~49 KB "premature
end of file"), which reads like a broken build. Decompress from the ramdisk
offset to the end of the file and you get the real cpio: 33 entries, including
sbin/wf-*.sh and init.<device>.rc.

    python3 check-bootimg.py out/target/product/sagit/boot.img [more...]
"""
import gzip
import io
import struct
import subprocess
import sys

for path in sys.argv[1:]:
    data = open(path, "rb").read()
    kernel_size, ramdisk_size, second_size = struct.unpack("<3I", data[8:20])
    page = struct.unpack("<I", data[36:40])[0]
    off = page + ((kernel_size + page - 1) // page) * page
    print("==", path, "kernel", kernel_size, "ramdisk field", ramdisk_size,
          "next field", second_size, "page", page)
    ramdisk = gzip.GzipFile(fileobj=io.BytesIO(data[off:])).read()
    print("   ramdisk decoded", len(ramdisk), "bytes")
    listing = subprocess.run(["cpio", "-it"], input=ramdisk, capture_output=True)
    names = listing.stdout.decode("utf-8", "replace").split()
    print("   entries", len(names), "|", " ".join(sorted(n for n in names if n.startswith("sbin/"))))
    print("   rc files", " ".join(sorted(n for n in names if n.endswith(".rc"))))
    if listing.stderr.strip():
        print("   cpio stderr:", listing.stderr.decode("utf-8", "replace").strip())
