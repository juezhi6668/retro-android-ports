#!/usr/bin/env python3
"""Package the FULL chroot-built Donut system into a CWM5-flashable cooper zip.

Inputs (all produced by the Hardy chroot `make -j8 full_cooper-eng` build):
  aosp-donut/out/target/product/cooper/system/      full compiled Donut system
  aosp-donut/out/target/product/cooper/ramdisk.img  full Donut ramdisk
  aosp-donut/vendor/samsung/cooper/kernel-twrp      TWRP/Omni cooper kernel
                                                    (2.6.35.7-androidarmv6+;
                                                    the CM kernel in ./kernel
                                                    can't drive STL, see README)

Only transformation: repack boot.img with cooper flash geometry
(base 0x13600000, pagesize 4096). Donut's in-tree mkbootimg hardcodes
pagesize 2048, so we use work/tools/mkbootimg-cooper (compiled from Donut
mkbootimg.c + libmincrypt/sha.c with a --pagesize patch).

Output: out/full_cooper-1.6.DRD35-UNOFFICIAL-FULL-<date>.zip (+ .md5)
"""
import hashlib, os, stat, struct, subprocess, sys, time, zipfile

WS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AOSP = os.environ.get("AOSP_DIR", os.path.join(WS, "aosp-donut"))
PROD = os.path.join(AOSP, "out", "target", "product", "cooper")
OUT = os.path.join(WS, "out")
WORK = os.path.join(WS, "work")
DATE = time.strftime("%Y%m%d")
ZIP_NAME = f"full_cooper-1.6.DRD35-UNOFFICIAL-FULL-{DATE}.zip"

KERNEL_BASE = 0x13600000
PAGE_SIZE = 4096

def run(cmd):
    print("+", " ".join(cmd), flush=True)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print((r.stdout or "")[-2000:])
        print((r.stderr or "")[-2000:], file=sys.stderr)
        raise SystemExit(f"command failed ({r.returncode}): {' '.join(cmd)}")
    return r

def main():
    system = os.path.join(PROD, "system")
    ramdisk = os.path.join(PROD, "ramdisk.img")
    kernel = os.path.join(AOSP, "vendor", "samsung", "cooper", "kernel-twrp")
    mkbootimg = os.path.join(WS, "tools", "mkbootimg-cooper")
    updater_bin = os.path.join(
        PROD, "obj", "EXECUTABLES", "updater_intermediates", "updater")
    for p in (system, ramdisk, kernel, mkbootimg, updater_bin):
        assert os.path.exists(p), f"missing input: {p}"
    n_apk = len([f for f in os.listdir(os.path.join(system, "app")) if f.endswith(".apk")])
    print(f"compiled system: {n_apk} apks")
    assert n_apk >= 25, "system/ looks incomplete"
    bp = open(os.path.join(system, "build.prop")).read()
    assert "ro.build.version.release=1.6" in bp and "GT-S5830" in bp

    # 1) repack boot.img with cooper geometry
    bootimg = os.path.join(WORK, "boot-full.img")
    run([mkbootimg, "--kernel", kernel, "--ramdisk", ramdisk,
         "--base", hex(KERNEL_BASE), "--pagesize", str(PAGE_SIZE),
         "--board", "cooper", "-o", bootimg])
    hdr = open(bootimg, "rb").read(608)
    assert hdr[:8] == b"ANDROID!"
    ks, ka, rs, ra, ss, sa, ta, ps = struct.unpack("<8I", hdr[8:40])
    name = hdr[48:64].split(b"\0")[0].decode()
    assert ps == PAGE_SIZE and ka == KERNEL_BASE + 0x8000 and name == "cooper", \
        (ps, hex(ka), name)
    print(f"boot.img OK: kernel={ks} ramdisk={rs} pagesize={ps} base={ka:#x} board={name}")

    # 2) collect system files + symlinks (do NOT follow symlinks: Donut's
    # package_extract_dir extracts FILES_ONLY, so symlinks must be stored as
    # symlink entries + created by explicit symlink() commands, exactly like
    # Donut's own releasetools/ota_from_target_files does).
    reg_files, symlinks = [], []
    for dirpath, dirnames, filenames in os.walk(system, followlinks=False):
        dirnames.sort()
        for n in sorted(filenames):
            full = os.path.join(dirpath, n)
            rel = os.path.relpath(full, PROD)  # system/...
            if os.path.islink(full):
                symlinks.append((os.readlink(full), "/" + rel))
            else:
                reg_files.append((full, rel))
    by_dest = {}
    for target, link in symlinks:
        by_dest.setdefault(target, []).append(link)
    symlink_cmds = "\n".join(
        'symlink("%s", %s);' % (dest, ", ".join('"%s"' % l for l in sorted(links)))
        for dest, links in sorted(by_dest.items()))
    print(f"system: {len(reg_files)} regular files, {len(symlinks)} symlinks "
          f"in {len(by_dest)} groups")
    updater = ("""assert(getprop("ro.product.device") == "cooper" || getprop("ro.build.product") == "cooper" || getprop("ro.product.model") == "GT-S5830" || getprop("ro.product.board") == "cooper");
ui_print("Donut 1.6 (DRD35) FULL for Samsung Galaxy Ace GT-S5830 (cooper)");
ui_print("Built in Ubuntu 8.04 chroot, JDK 5, from AOSP android-1.6_r2");
ui_print("Extracting system (wipe cache from TWRP if needed)...");
unmount("/system");
mount("ext4", "/dev/block/stl12", "/system");
package_extract_dir("system", "/system");
""" + symlink_cmds + """
set_perm_recursive(0, 0, 0755, 0644, "/system");
set_perm(0, 0, 06755, "/system/xbin/su");
ui_print("Flashing boot image to BML8 (erase + flash_image)...");
assert(package_extract_file("boot.img", "/tmp/boot.img"),
       run_program("/sbin/erase_image", "boot"),
       run_program("/sbin/flash_image", "boot", "/tmp/boot.img"),
       delete("/tmp/boot.img"));
ui_print("Unmounting...");
unmount("/system");
ui_print("Done. Reboot to Donut.");
""")
    amend = """show_progress 0.1 0
assert compatible_with("0") == "true"
show_progress 0.2 10
format CACHE:
copy_dir PACKAGE:system SYSTEM:
set_perm_recursive 0 0 0755 0644 SYSTEM:
write_raw_image PACKAGE:boot.img BOOT:
show_progress 0.8 10
"""
    # 3) zip it
    os.makedirs(OUT, exist_ok=True)
    zpath = os.path.join(OUT, ZIP_NAME)
    if os.path.exists(zpath):
        os.remove(zpath)
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.writestr("META-INF/com/google/android/updater-script", updater)
        z.writestr("META-INF/com/google/android/update-script", amend)
        # update-binary: Donut-built static ARM updater (recovery has nothing
        # to execute without it). Stored uncompressed, like OTA packages.
        zi = zipfile.ZipInfo("META-INF/com/google/android/update-binary")
        zi.compress_type = zipfile.ZIP_STORED
        zi.external_attr = (0o755 << 16)
        z.writestr(zi, open(updater_bin, "rb").read())
        z.write(bootimg, "boot.img")
        for full, rel in reg_files:
            z.write(full, rel)
        for target, link in symlinks:
            rel = link[1:]  # strip leading /
            zi = zipfile.ZipInfo(rel)
            zi.compress_type = zipfile.ZIP_STORED
            zi.external_attr = (stat.S_IFLNK | 0o777) << 16
            z.writestr(zi, target)
    md5 = hashlib.md5(open(zpath, "rb").read()).hexdigest()
    open(zpath + ".md5", "w").write(f"{md5}  {ZIP_NAME}\n")
    print(f"zip: {zpath} ({os.path.getsize(zpath)} bytes, md5 {md5})")

    # 4) verify
    run(["unzip", "-t", zpath])
    z = zipfile.ZipFile(zpath)
    names = z.namelist()
    assert "boot.img" in names and "system/build.prop" in names
    assert "META-INF/com/google/android/update-binary" in names
    assert z.read("boot.img")[:8] == b"ANDROID!"
    ub = z.read("META-INF/com/google/android/update-binary")
    assert ub[:4] == b"\x7fELF" and struct.unpack("<H", ub[18:20])[0] == 40, \
        "update-binary is not ARM"
    apks = [n for n in names if n.startswith("system/app/") and n.endswith(".apk")]
    script = z.read("META-INF/com/google/android/updater-script").decode()
    assert "symlink(" in script and "update-binary" not in script
    assert "06755" in script and "run_program" in script and "flash_image" in script
    n_sym = sum(1 for i in z.infolist()
                if (i.external_attr >> 16) & 0o170000 == 0o120000)
    assert n_sym == len(symlinks), (n_sym, len(symlinks))
    print(f"verify OK: {len(names)} entries ({n_sym} symlinks), "
          f"{len(apks)} apks, ARM update-binary, boot magic OK")
    assert len(apks) >= 25
    print(f"=== DONE: {zpath} ===")

if __name__ == "__main__":
    main()
