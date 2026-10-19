#!/usr/bin/env python3
# stage-v15.py — stage the 64-bit Android 15 daemon closure (modem storage + RFS + WLAN).
#
# KitKat's userspace is 32-bit, but the kernel is arm64, so the 64-bit daemons the
# modem and WLAN need can run straight off the image — no chroot: the A15 daemons
# link against Android 15's 64-bit bionic, which lands in /system/lib64 and never
# collides with KitKat's 32-bit /system/lib (Android's multilib layout does the
# namespacing for us). The four daemons:
#   rmt_storage - remote storage (the modem's EFS/NV block access) over /dev/uio0,
#                 the kernel's "rmtfs" UIO device (dt: 0.qcom,rmtfs_sharedmem)
#   tftp_server - RFS: serves the modem's file I/O requests (paths under
#                 /vendor/rfs, symlinked into /mnt/vendor/persist)
#   cnss-daemon - the WLFW handshake with icnss (kernel) that starts the WLAN PD
#   irsc_util   - loads the ipc_router security rules (/vendor/etc/sec_config)
#
# the out dir gets root64/, mirroring the final /system:
#   root64/bin/linker64          -> /system/bin/linker64
#   root64/lib64/*.so            -> /system/lib64
#   root64/vendor/bin/*          -> /vendor/bin        (/vendor -> /system/vendor)
#   root64/vendor/lib64/*.so     -> /vendor/lib64
#   root64/vendor/etc/sec_config -> /vendor/etc/sec_config
#
# usage: stage-v15.py <sysroot from extract-lineage.sh> <out dir>
# keep it at $AOSP_DIR/vendor/sony/maple-hyb, next to stage.py's root/.
import os
import shutil
import subprocess
import sys

sysroot, hyb = (os.path.realpath(p) for p in sys.argv[1:3])
out = os.path.join(hyb, "root64")

DAEMONS = ["rmt_storage", "tftp_server", "cnss-daemon", "irsc_util", "pd-mapper"]
# 64-bit bionic (linker + libc/libm/libdl/libdl_android) is shipped as a real-file
# "bootstrap" set under system/lib64/bootstrap; system/lib64's libc.so & co are
# symlinks into the runtime apex (/apex/com.android.runtime/lib64/bionic), which a
# KitKat image has no room for. Same bionic either way — the bootstrap set is the
# one init itself runs on.
SEARCH = [
    ("lib64", sysroot + "/system/lib64/bootstrap"),
    ("vendor/lib64", sysroot + "/vendor/lib64"),
    ("lib64", sysroot + "/system/lib64"),
    ("lib64", sysroot + "/system/system_ext/lib64"),
    ("lib64", sysroot + "/system/product/lib64"),
]


def needed(path):
    r = subprocess.run(["readelf", "-d", path], capture_output=True, text=True, check=True)
    return [l.split("[")[1].split("]")[0] for l in r.stdout.splitlines() if "(NEEDED)" in l]


def find(name):
    skipped = []
    for rel, src in SEARCH:
        p = os.path.join(src, name)
        if os.path.islink(p):
            skipped.append(p)
            continue
        if os.path.isfile(p):
            return rel, p
    return None, skipped


def copy(src, rel_dst):
    dst = os.path.join(out, rel_dst)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)
    shutil.copymode(src, dst)


seen, missing, apex_only = {}, [], []
queue = []


def walk():
    while queue:
        name = queue.pop()
        if name in seen or name in missing or name in apex_only:
            continue
        rel, src = find(name)
        if not src:
            missing.append(name)
            continue
        if isinstance(src, list):  # only apex symlinks
            apex_only.append(name)
            continue
        seen[name] = rel
        copy(src, os.path.join(rel, name))
        queue.extend(needed(src))


if os.path.exists(hyb):
    shutil.rmtree(hyb)
for d in DAEMONS:
    src = os.path.join(sysroot, "vendor/bin", d)
    if not os.path.isfile(src):
        sys.exit(f"no vendor/bin/{d} in the sysroot")
    copy(src, os.path.join("vendor/bin", d))
    queue += needed(src)
walk()

# irsc_util's rules file: without it the ipc_router's default rules apply and
# non-root QMI clients hang instead of reaching the modem's services
sec = os.path.join(sysroot, "vendor/etc/sec_config")
if os.path.isfile(sec):
    copy(sec, "vendor/etc/sec_config")
else:
    print("no vendor/etc/sec_config in the sysroot", file=sys.stderr)

# linker64 itself: system/bin/linker64 is an apex symlink, the real file is the
# bootstrap one (the same binary init runs on)
linker = os.path.join(sysroot, "system/bin/bootstrap/linker64")
if not os.path.isfile(linker):
    sys.exit("no system/bin/bootstrap/linker64 in the sysroot")
copy(linker, "bin/linker64")

by_origin = {}
for n, rel in sorted(seen.items()):
    by_origin.setdefault(rel, []).append(n)
for rel, names in by_origin.items():
    print(f"{rel} ({len(names)}): {' '.join(names)}")
print(f"daemons: {' '.join(DAEMONS)}")
if apex_only:
    print(f"only in the runtime apex (not staged): {' '.join(sorted(apex_only))}", file=sys.stderr)
if missing:
    sys.exit("missing: " + " ".join(sorted(missing)))
