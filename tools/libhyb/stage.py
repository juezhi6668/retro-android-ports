#!/usr/bin/env python3
# ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
# builds libhyb's root (/system/a15): walks DT_NEEDED from the Adreno driver and the QTI gralloc1
# and pulls each library from the sysroot (vendor first, then lineage's system, then the apex
# libs). the gpu firmware goes in too.
# usage: stage.py <sysroot from extract-lineage.sh> <out dir>
# the out dir gets root/, and is what full_maple.mk copies to /system/a15.
# keep it at $AOSP_DIR/vendor/sony/maple-hyb, sync-device-tree.sh's rsync --delete owns
# vendor/sony/maple only.
import glob
import os
import shutil
import subprocess
import sys

sysroot, hyb = (os.path.realpath(p) for p in sys.argv[1:3])
out = os.path.join(hyb, "root")

# 32-bit: libhyb loads them into KitKat processes (armeabi-v7a)
ROOTS = [
    "libEGL_adreno.so",
    "libGLESv2_adreno.so",
    "libGLESv1_CM_adreno.so",
]
# searched in this order, the same order libhyb's locate() uses on the device
SEARCH = [
    ("vendor/lib/egl", sysroot + "/vendor/lib/egl"),
    ("vendor/lib", sysroot + "/vendor/lib"),
    ("system/lib", sysroot + "/system/lib"),
    ("system/lib", sysroot + "/apexlib"),
]
# audio calibration: KitKat's audio HAL loads libacdbloader.so through libhyb and
# pushes the tasha acdb blocks into /dev/msm_audio_cal (the loader itself is an
# Android 15 32-bit library, same as the Adreno driver). Its DT_NEEDED closure
# brings libaudcal/libacdbrtac/libacdb-fts/libadiertac/libmiscta/libdiag and the
# vendor libtinyalsa.
ACDB = ["libacdbloader.so"]
# dlopened by name, never in DT_NEEDED. missing ones only cost a failed dlopen
DLOPENED = ["eglSubDriverAndroid.so", "libq3dtools_adreno.so", "libadreno_app_profiles.so"]
# the a540 microcode, gpmu and zap shader; kgsl loads them from /vendor/firmware
FIRMWARE = ["a530_*", "a540_*"]

# the QTI gralloc1 module, staged under the name gralloc.maple (gralloc.c) loads
# (vendor/lib/hw/gralloc.default.so. the sysroot's own gralloc.default.so is NOT that module)


# names libhyb resolves to KitKat's own 32-bit bionic, never to the hyb root
HOST = {"libc.so", "libm.so", "libdl.so", "liblog.so", "libcutils.so", "libz.so",
        "ld-android.so", "libdl_android.so"}


def needed(path):
    r = subprocess.run(["readelf", "-d", path], capture_output=True, text=True, check=True)
    return [l.split("[")[1].split("]")[0] for l in r.stdout.splitlines() if "(NEEDED)" in l]


def find(name):
    for rel, src in SEARCH:
        p = os.path.join(src, name)
        if os.path.isfile(p):
            return rel, p
    return None, None


def copy(src, rel_dst):
    dst = os.path.join(out, rel_dst)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)
    shutil.copymode(src, dst)


seen, missing, hosts = {}, [], set()


def walk(queue):
    while queue:
        name = queue.pop()
        if name in HOST:
            hosts.add(name)
            continue
        if name in seen or name in missing:
            continue
        rel, src = find(name)
        if not src:
            missing.append(name)
            continue
        seen[name] = rel
        copy(src, os.path.join(rel, name))
        queue += needed(src)


if os.path.exists(hyb):
    shutil.rmtree(hyb)
walk(ROOTS)
walk(ACDB)
walk([n for n in DLOPENED if find(n)[1]])

# eglInitialize hard fails unless the android subdriver gets a gralloc module. there's no
# hwbinder for the mapper, so it falls back to hw_get_module, which ends up at gralloc.default
# the qcom one (gralloc.msm8998 on maple), never a stock gralloc.default the vendor might carry
gralloc = sorted(g for g in glob.glob(sysroot + "/vendor/lib/hw/gralloc.*.so")
                 if not g.endswith("/gralloc.default.so"))
if not gralloc:
    sys.exit("no vendor/lib/hw/gralloc.*.so in the sysroot")
copy(gralloc[0], "vendor/lib/hw/gralloc.default.so")
walk(needed(gralloc[0]))

# the calibration data itself: /sys/devices/soc0/hw_platform reads "MTP" on
# maple, and libacdbloader builds /vendor/etc/acdbdata/<hw_platform>/ from it, so
# the yoshino (Sony) files have to sit under that form-factor name. Staged next
# to root/ (not inside) because full_maple.mk copies them to /system/vendor/etc,
# not into the /system/a15 closure.
acdb_src = os.path.join(sysroot, "vendor/etc/acdbdata/Sony")
acdb_dst = os.path.join(hyb, "acdbdata/MTP")
if os.path.isdir(acdb_src):
    shutil.copytree(acdb_src, acdb_dst, dirs_exist_ok=True)
    print(f"acdbdata ({len(os.listdir(acdb_dst))} files): Sony/ -> acdbdata/MTP/")
else:
    print("no vendor/etc/acdbdata/Sony in the sysroot, audio will run uncalibrated",
          file=sys.stderr)

fw = []
for pat in FIRMWARE:
    for d in ("vendor/firmware", "system/etc/firmware"):
        fw += glob.glob(os.path.join(sysroot, d, pat))
for f in fw:
    copy(f, os.path.join("vendor/firmware", os.path.basename(f)))

for d in ("vendor/lib/egl", "vendor/lib/hw", "vendor/lib", "system/lib", "vendor/firmware"):
    os.makedirs(os.path.join(out, d), exist_ok=True)

by_origin = {}
for n, rel in sorted(seen.items()):
    by_origin.setdefault(rel, []).append(n)
for rel, names in by_origin.items():
    print(f"{rel} ({len(names)}): {' '.join(names)}")
print(f"kitkat (host, not staged): {' '.join(sorted(hosts))}")
print(f"gralloc: {os.path.basename(gralloc[0])}")
print(f"firmware ({len(fw)}): {' '.join(sorted(os.path.basename(f) for f in fw))}")
if not fw:
    print("no a530/a540 firmware found, kgsl won't start the gpu", file=sys.stderr)
if missing:
    sys.exit("missing: " + " ".join(sorted(missing)))
