#!/usr/bin/env python3
"""Build boot.img for the MIUI 4.1.2 (mione_plus) system on Xiaomi Mi 6.

Reuses the KitKat port's built ramdisk - init, adbd, ueventd, healthd and
init.rc are 4.4 binaries that already boot this phone - and overlays the
Jelly Bean pieces:

  devices/sagit/jb/ramdisk/init.environ.rc   JB boot classpath
  devices/sagit/jb/ramdisk/init.sagit.rc     JB mounts/services

Kernel is the patched 4.4.247 Image.gz-dtb the KitKat port uses; the 4.4 init
reads /system/build.prop and hands the JB userland its BOOTCLASSPATH, so the
only coupling between init and the system image is that env file.

Usage: scripts/build-sagit-jb-boot.py [--aosp DIR] [--copy-to PATH]
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OVERLAY = os.path.join(REPO, "devices", "sagit", "jb", "ramdisk")
OUT_DIR = os.path.join(REPO, "out", "sagit-jb")
DEFAULT_COPY = "/mnt/c/download/sagit/boot_mione.img"

# BoardConfig.mk, unchanged from the KitKat port
CMDLINE = ("ehci-hcd.park=3 lpm_levels.sleep_disabled=1 sched_enable_hmp=0 "
           "sched_enable_power_aware=1 service_locator.enable=1 swiotlb=2048 "
           "androidboot.configfs=true androidboot.usbcontroller=a800000.dwc3 "
           "loop.max_part=7 androidboot.hardware=sagit")
BASE = "0x00000000"
PAGESIZE = 4096

# nothing in the JB system may satisfy a 4.4-only service; keep the ramdisk
# honest about what it launches
FORBIDDEN_FILES = ("fbpresentd", "pdserve", "rtc-restore", "dump-boot-log.sh")


def run(cmd, cwd=None, shell=False):
    print("+", cmd if isinstance(cmd, str) else " ".join(cmd))
    subprocess.run(cmd, cwd=cwd, shell=shell, check=True)


def unpack_ramdisk(src, dstdir):
    with open(os.path.join(dstdir, "..", "ramdisk.cpio"), "wb") as fh:
        subprocess.run(["gzip", "-dc", src], check=True, stdout=fh)
    run("cpio -idm --quiet < ../ramdisk.cpio", cwd=dstdir, shell=True)


def pack_ramdisk(srcdir, dst):
    cmd = ("find . -print0 | cpio --null -o -H newc --owner=0:0 --quiet "
           "| gzip -9 > " + dst)
    run(cmd, cwd=srcdir, shell=True)


def parse_bootimg(path):
    data = open(path, "rb").read()
    assert data[:8] == b"ANDROID!", "not an Android boot image"
    kernel_size, kernel_addr, ramdisk_size, ramdisk_addr, second_size, \
        second_addr, tags_addr, page_size = (
            int.from_bytes(data[8 + i * 4:12 + i * 4], "little") for i in range(8))
    assert data[48:52] == b"\x00" * 4, "unexpected header version"
    cmdline = data[64:64 + 512].split(b"\x00", 1)[0].decode()
    return {
        "kernel_size": kernel_size, "ramdisk_size": ramdisk_size,
        "second_size": second_size, "page_size": page_size,
        "kernel_addr": hex(kernel_addr), "ramdisk_addr": hex(ramdisk_addr),
        "cmdline": cmdline, "data": data,
    }


def page_up(n, ps):
    return (n + ps - 1) // ps * ps


def extract_ramdisk(img, page_size):
    off = page_size + page_up(img["kernel_size"], page_size)
    return img["data"][off:off + img["ramdisk_size"]]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--aosp", default="/home/juezhi/aosp-kitkat")
    ap.add_argument("--copy-to", default=DEFAULT_COPY)
    args = ap.parse_args()

    product = os.path.join(args.aosp, "out", "target", "product", "sagit")
    kernel = os.path.join(product, "kernel")
    ramdisk_in = os.path.join(product, "ramdisk.img")
    mkbootimg = os.path.join(args.aosp, "out", "host", "linux-x86", "bin", "mkbootimg")
    for p in (kernel, ramdisk_in, mkbootimg):
        if not os.path.exists(p):
            sys.exit("missing: " + p)

    os.makedirs(OUT_DIR, exist_ok=True)
    work = tempfile.mkdtemp(prefix="sagit-jb-")
    root = os.path.join(work, "root")
    os.makedirs(root)
    unpack_ramdisk(ramdisk_in, root)

    for name in sorted(os.listdir(OVERLAY)):
        shutil.copy2(os.path.join(OVERLAY, name), os.path.join(root, name))
        print("overlay:", name)

    rc = open(os.path.join(root, "init.sagit.rc")).read()
    env = open(os.path.join(root, "init.environ.rc")).read()
    for bad in FORBIDDEN_FILES:
        assert ("service " + bad) not in rc and ("start " + bad) not in rc, \
            "init.sagit.rc still launches " + bad
    assert "framework_ext.jar" in env, "BOOTCLASSPATH lost framework_ext.jar"
    assert "service sdcard /system/bin/sdcard /data/media 1023 1023" in rc
    # the 4.4 ramdisk must still be there under the overlay
    for f in ("init", "sbin/adbd", "sbin/ueventd", "init.rc", "init.usb.rc"):
        assert os.path.exists(os.path.join(root, f)), "ramdisk lost " + f

    ramdisk_out = os.path.join(work, "ramdisk-jb.img")
    pack_ramdisk(root, ramdisk_out)
    boot = os.path.join(OUT_DIR, "boot.img")
    run([mkbootimg, "--kernel", kernel, "--ramdisk", ramdisk_out,
         "--cmdline", CMDLINE, "--base", BASE, "--pagesize", str(PAGESIZE),
         "--output", boot])

    img = parse_bootimg(boot)
    assert img["page_size"] == PAGESIZE
    assert img["cmdline"] == CMDLINE
    inside = extract_ramdisk(img, img["page_size"])
    assert hashlib.sha256(inside).digest() == \
        hashlib.sha256(open(ramdisk_out, "rb").read()).digest(), \
        "ramdisk inside boot.img differs from the one we packed"

    # ramdisk contents, straight out of the image
    check = os.path.join(work, "check")
    os.makedirs(check)
    with open(os.path.join(work, "inside.gz"), "wb") as fh:
        fh.write(inside)
    run("gzip -dc ../inside.gz | cpio -idm --quiet", cwd=check, shell=True)
    for f in ("init.sagit.rc", "init.environ.rc", "init", "sbin/adbd", "fstab.sagit"):
        assert os.path.exists(os.path.join(check, f)), "image lost " + f
    rc2 = open(os.path.join(check, "init.sagit.rc")).read()
    assert "service sdcard /system/bin/sdcard /data/media 1023 1023" in rc2

    md5 = hashlib.md5(open(boot, "rb").read()).hexdigest()
    size = os.path.getsize(boot)
    print(f"\nboot.img: {boot} ({size} bytes, kernel {img['kernel_size']}, "
          f"ramdisk {img['ramdisk_size']}, pagesize {img['page_size']})")
    print("md5:", md5)

    if args.copy_to:
        shutil.copyfile(boot, args.copy_to)
        print("copied to", args.copy_to)

    shutil.rmtree(work)
    print("\nflash:")
    print(f"  adb push {args.copy_to} /data/media/boot.img")
    print("  adb shell dd if=/data/media/boot.img of=/dev/block/bootdevice/by-name/boot bs=1048576")
    print("  adb shell sync")
    print(f"  adb shell 'dd if=/dev/block/bootdevice/by-name/boot bs=4096 count={size // 4096} | md5sum'")
    print("  (expect", md5 + ")")


if __name__ == "__main__":
    main()
