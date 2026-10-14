#!/usr/bin/env python3
# ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
# builds glrenderd's chroot: walks DT_NEEDED from the adreno blobs and glrenderd, pulling each lib
# from vendor first, then lineage's system, then the apex libs. the gpu firmware goes in too.
# usage: stage.py <sysroot from extract-lineage.sh> <build.sh out dir> <glr dir>
# the glr dir gets hwchroot and root/, and is what full_maple.mk copies to /system/glr.
# keep it at $AOSP_DIR/vendor/sony/maple-glr, sync-device-tree.sh's rsync --delete owns vendor/sony/maple
import glob
import os
import shutil
import subprocess
import sys

sysroot, build, glr = (os.path.realpath(p) for p in sys.argv[1:4])
out = os.path.join(glr, "root")

# 32-bit, the emugl wire format carries 32-bit pointers so glrenderd is too
ROOTS = [
    "libEGL_adreno.so",
    "libGLESv2_adreno.so",
    "libGLESv1_CM_adreno.so",
]
# searched in this order, the same as the LD_LIBRARY_PATH hwchroot sets
SEARCH = [
    ("vendor/lib/egl", sysroot + "/vendor/lib/egl"),
    ("vendor/lib", sysroot + "/vendor/lib"),
    ("system/lib", sysroot + "/system/lib"),
    ("system/lib", sysroot + "/apexlib"),
]
# dlopened by name, never in DT_NEEDED. missing ones only cost a failed dlopen
DLOPENED = ["eglSubDriverAndroid.so", "libq3dtools_adreno.so", "libadreno_app_profiles.so"]
# the a540 microcode, gpmu and zap shader. kgsl loads them in glrenderd's context, so from the chroot
FIRMWARE = ["a530_*", "a540_*"]


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


seen, missing = {}, []


def walk(queue):
    while queue:
        name = queue.pop()
        if name in seen or name in missing:
            continue
        rel, src = find(name)
        if not src:
            missing.append(name)
            continue
        seen[name] = rel
        copy(src, os.path.join(rel, name))
        queue += needed(src)


if os.path.exists(glr):
    shutil.rmtree(glr)
walk(ROOTS + needed(os.path.join(build, "glrenderd")))
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

copy(sysroot + "/system/bin/linker", "system/bin/linker")
os.chmod(os.path.join(out, "system/bin/linker"), 0o755)
copy(os.path.join(build, "glrenderd"), "glrenderd")
os.chmod(os.path.join(out, "glrenderd"), 0o755)
shutil.copy2(os.path.join(build, "hwchroot"), os.path.join(glr, "hwchroot"))

fw = []
for pat in FIRMWARE:
    for d in ("vendor/firmware", "system/etc/firmware"):
        fw += glob.glob(os.path.join(sysroot, d, pat))
for f in fw:
    copy(f, os.path.join("vendor/firmware", os.path.basename(f)))

for d in ("dev", "proc", "sys", "data/vendor/gpu", "data/misc/gpu", "vendor/firmware"):
    os.makedirs(os.path.join(out, d), exist_ok=True)

by_origin = {}
for n, rel in sorted(seen.items()):
    by_origin.setdefault(rel, []).append(n)
for rel, names in by_origin.items():
    print(f"{rel} ({len(names)}): {' '.join(names)}")
print(f"gralloc: {os.path.basename(gralloc[0])}")
print(f"firmware ({len(fw)}): {' '.join(sorted(os.path.basename(f) for f in fw))}")
if not fw:
    print("no a530/a540 firmware found, kgsl won't start the gpu", file=sys.stderr)
if missing:
    sys.exit("missing: " + " ".join(sorted(missing)))
