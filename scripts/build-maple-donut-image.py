#!/usr/bin/env python3
"""Package the chroot-built Donut system for maple into flashable images.

Inputs (all produced by the Hardy chroot `make -j12 full_maple-eng` build):
  aosp-donut/out/target/product/maple/system/      full compiled Donut system
  aosp-donut/out/target/product/maple/ramdisk.img  full Donut ramdisk
  aosp-donut/vendor/sony/maple/kernel              Image.gz-dtb, TWRP-dumped
                                                   from the boot partition
                                                   (out-of-git blob)

Donut's build system cannot produce either image for this device:
  - boot.img: in-tree mkbootimg hardcodes pagesize 2048; maple needs 4096
    (base 0). Repacked with tools/mkbootimg-cooper (the --pagesize patched
    Donut mkbootimg, cf. patches/donut/mkbootimg-pagesize.patch).
  - system.img: Donut only makes yaffs2 images (mkyaffs2image); maple is
    GPT/ext4. Packed here with host mke2fs -d from the built system/ tree
    (root:root, modes preserved).

Outputs (in out/maple-donut/):
  boot.img     pagesize 4096, base 0, cmdline from BoardConfig.mk
  system.img   raw ext4 (dd to /dev/block/bootdevice/by-name/system = sde46)
  flash.txt    the exact TWRP commands used to flash (readback verified)

Usage:  python3 scripts/build-maple-donut-image.py [--size-mb N] [--skip-system]
"""
import os, re, shutil, struct, subprocess, sys, time

WS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AOSP = os.environ.get("AOSP_DIR", os.path.join(WS, "aosp-donut"))
PROD = os.path.join(AOSP, "out", "target", "product", "maple")
OUT = os.path.join(WS, "out", "maple-donut")
WORK = os.path.join(WS, "work", "maple-donut")

KERNEL_BASE = 0x00000000
PAGE_SIZE = 4096
SYSTEM_IMAGE_MB = 512          # Donut system is ~130 MB; 512 MB is ample
SYSTEM_PARTITION_MB = 5000     # sde46 (only informational)


def run(cmd, **kw):
    print("+", " ".join(cmd), flush=True)
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        print((r.stdout or "")[-3000:])
        print((r.stderr or "")[-3000:], file=sys.stderr)
        raise SystemExit(f"command failed ({r.returncode}): {' '.join(cmd)}")
    return r


def parse_cmdline():
    """BOARD_KERNEL_CMDLINE straight from the device tree, so the packed
    boot.img and BoardConfig.mk can never drift apart."""
    board = os.path.join(AOSP, "vendor", "sony", "maple", "BoardConfig.mk")
    m = re.search(r"^BOARD_KERNEL_CMDLINE\s*:=\s*(.*)$", open(board).read(),
                  re.M)
    assert m, "BOARD_KERNEL_CMDLINE not found in " + board
    cmdline = m.group(1).strip()
    assert "androidboot.hardware=maple" in cmdline
    return cmdline


def main():
    size_mb = SYSTEM_IMAGE_MB
    skip_system = False
    args = sys.argv[1:]
    while args:
        a = args.pop(0)
        if a == "--size-mb":
            size_mb = int(args.pop(0))
        elif a == "--skip-system":
            skip_system = True
        else:
            raise SystemExit("unknown arg " + a)

    system = os.path.join(PROD, "system")
    ramdisk = os.path.join(PROD, "ramdisk.img")
    kernel = os.path.join(AOSP, "vendor", "sony", "maple", "kernel")
    mkbootimg = os.path.join(WS, "tools", "mkbootimg-cooper")
    for p in (system, ramdisk, kernel, mkbootimg):
        assert os.path.exists(p), f"missing input: {p}"
    n_apk = len([f for f in os.listdir(os.path.join(system, "app"))
                 if f.endswith(".apk")])
    print(f"compiled system: {n_apk} apks")
    assert n_apk >= 25, "system/ looks incomplete"
    bp = open(os.path.join(system, "build.prop")).read()
    assert "ro.build.version.release=1.6" in bp and "full_maple" in bp
    assert "ro.sf.lcd_density=410" in bp
    assert "androidboot" not in bp

    cmdline = parse_cmdline()
    print("cmdline:", cmdline)

    os.makedirs(OUT, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)

    # 1) boot.img with maple geometry
    bootimg = os.path.join(OUT, "boot.img")
    run([mkbootimg, "--kernel", kernel, "--ramdisk", ramdisk,
         "--base", hex(KERNEL_BASE), "--pagesize", str(PAGE_SIZE),
         "--cmdline", cmdline, "-o", bootimg])
    d = open(bootimg, "rb").read()
    assert d[:8] == b"ANDROID!", d[:8]
    ks, ka, rs, ra, ss, sa, ta, ps = struct.unpack("<8I", d[8:40])
    name = d[48:64].split(b"\0")[0].decode()
    got_cmdline = d[64:576].split(b"\0")[0].decode()
    assert ps == PAGE_SIZE, ps
    assert ka == KERNEL_BASE + 0x8000, hex(ka)
    assert ra == KERNEL_BASE + 0x01000000, hex(ra)
    assert ta == KERNEL_BASE + 0x00000100, hex(ta)
    assert got_cmdline == cmdline, (got_cmdline, cmdline)
    assert name == "", repr(name)
    assert d[PAGE_SIZE:PAGE_SIZE + ks] == open(kernel, "rb").read(), \
        "kernel payload mismatch"
    assert rs > 100000 and rs < 8 * 1024 * 1024, rs
    print(f"boot.img OK: kernel={ks} ramdisk={rs} pagesize={ps} "
          f"base={hex(KERNEL_BASE)} cmdline-ok")

    if skip_system:
        print("--skip-system: done")
        return

    # 2) raw ext4 system image. Build a root-owned copy of the system tree
    # (the build runs as a host user; Android wants root:root) and pack it
    # with mke2fs -d, which preserves modes and symlinks.
    rootdir = os.path.join(WORK, "system-root")
    run(["sudo", "rm", "-rf", rootdir])
    run(["sudo", "cp", "-a", system, rootdir])
    run(["sudo", "chown", "-R", "0:0", rootdir])
    # Donut's kcm host tool creates the .kcm.bin files mode 0660 (root:root),
    # so app processes (uid app_*) cannot read them and libui SIGSEGVs when
    # KeyCharacterMap::load() fails on the unreadable file (fault addr 4).
    # Normalize them to world-readable in the image (belt-and-suspenders over
    # the kcm.cpp source fix).
    run(["sudo", "sh", "-c",
         "find " + rootdir + " -type f -name '*.kcm.bin' "
         "-exec chmod 0644 {} +"])
    # sanity: executables stayed executable, apks are 0644
    st = os.stat(os.path.join(rootdir, "bin", "app_process"))
    assert st.st_mode & 0o111, "app_process lost its exec bit"
    assert oct(os.stat(os.path.join(rootdir, "app",
                                    "Launcher.apk")).st_mode & 0o777) == "0o644"

    sysimg = os.path.join(OUT, "system.img")
    run(["sudo", "rm", "-f", sysimg])
    run(["sudo", "mke2fs", "-q", "-t", "ext4", "-b", "4096", "-I", "256",
         "-O", "^has_journal", "-L", "system", "-d", rootdir,
         "-E", "root_owner=0:0", sysimg, f"{size_mb}M"])
    # verify: ext4 magic, and the fsck says clean
    raw = open(sysimg, "rb").read(2048)
    assert raw[1080:1082] == b"\x53\xef", "no ext4 magic"
    run(["sudo", "e2fsck", "-fn", sysimg])
    sz = os.path.getsize(sysimg)
    print(f"system.img OK: {sz} bytes ({sz >> 20} MB), ext4, label=system")

    # 3) flash recipe (readback-verified dd on TWRP; keep with the images)
    sys_blocks = os.path.getsize(sysimg) // 4096
    boot_blocks = os.path.getsize(bootimg) // 4096
    recipe = f"""\
# Donut 1.6 for maple — flash via TWRP (adb runs as root there).
# system partition = sde46, boot = sde35 (stable GPT numbers, KitKat port).
# Images were built {time.strftime('%Y-%m-%d %H:%M')} from the chroot tree.
#
# TWRP's dd is toybox and rejects GNU size suffixes (`bs=1M` -> "illegal
# number") — use plain byte counts. The partitions are larger than the
# images, so read back exactly the image's bytes for the md5 check.

adb reboot recovery
# wait for TWRP to answer adb (do NOT use `adb wait-for-device`: it can
# miss the transition when the phone is already in recovery and hang
# forever — poll `adb shell "test -x /sbin/mke2fs"` instead), then:
adb push out/maple-donut/system.img /data/media/system.img
adb push out/maple-donut/boot.img   /data/media/boot.img
adb shell dd if=/data/media/system.img of=/dev/block/bootdevice/by-name/system bs=1048576
adb shell dd if=/data/media/boot.img   of=/dev/block/bootdevice/by-name/boot   bs=1048576
adb shell sync
# verify (device-side md5 of the written range; system {os.path.getsize(sysimg)} B, boot {os.path.getsize(bootimg)} B):
adb shell "dd if=/dev/block/bootdevice/by-name/system bs=4096 count={sys_blocks} | md5sum"
md5sum out/maple-donut/system.img
adb shell "dd if=/dev/block/bootdevice/by-name/boot bs=4096 count={boot_blocks} | md5sum"
md5sum out/maple-donut/boot.img
# wipe data + cache for a clean Donut first boot (destroys /data/media too):
adb shell mke2fs -t ext4 -b 4096 /dev/block/bootdevice/by-name/userdata
adb shell mke2fs -t ext4 -b 4096 /dev/block/bootdevice/by-name/cache
adb reboot
"""
    open(os.path.join(OUT, "flash.txt"), "w").write(recipe)
    print("flash recipe: out/maple-donut/flash.txt")
    print("=== DONE ===")


if __name__ == "__main__":
    main()
