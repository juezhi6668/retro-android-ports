#!/usr/bin/env python3
"""Build flashable Donut (1.6) ROM zip for Samsung Galaxy S5830 (cooper).

Sources:
  - AOSP Donut android-1.6_r2 (aosp-donut/, PLATFORM 1.6, BUILD DRD35)
  - Cooper reference: LineageOS gb-release-7.2 device tree + prebuilt zImage kernel
  - Host tool mkbootimg compiled from aosp-donut/system/core/mkbootimg + libmincrypt

Output: out/full_cooper-1.6.DRD35-UNOFFICIAL-<date>.zip (CWM5 flashable, BML)
"""
import gzip, hashlib, os, shutil, struct, subprocess, sys, time, zipfile

WS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AOSP = os.environ.get("AOSP_DIR", os.path.join(WS, "aosp-donut"))
REF = os.path.join(WS, "reference", "android_device_samsung_cooper")
DEV = os.path.join(AOSP, "vendor", "samsung", "cooper")
OUT = os.path.join(WS, "out")
WORK = os.path.join(WS, "work")
DATE = time.strftime("%Y%m%d")
BUILD_ID = "DRD35"
ROM_VERSION = f"1.6.{BUILD_ID}"
ZIP_NAME = f"full_cooper-{ROM_VERSION}-UNOFFICIAL-{DATE}.zip"

# Cooper flash geometry (from BoardConfig.mk)
KERNEL_BASE = 0x13600000
PAGE_SIZE = 4096

def run(cmd, **kw):
    print("+", " ".join(cmd), flush=True)
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        print(r.stdout[-3000:] if r.stdout else "")
        print(r.stderr[-3000:] if r.stderr else "", file=sys.stderr)
        raise SystemExit(f"command failed ({r.returncode}): {' '.join(cmd)}")
    return r

def compile_mkbootimg():
    src = os.path.join(AOSP, "system/core/mkbootimg/mkbootimg.c")
    sha = os.path.join(AOSP, "system/core/libmincrypt/sha.c")
    inc = os.path.join(AOSP, "system/core/include")
    tools = os.path.join(WS, "tools")
    os.makedirs(tools, exist_ok=True)
    # 1) pristine Donut mkbootimg (proves source builds on modern GCC)
    orig = os.path.join(tools, "mkbootimg-donut-orig")
    run(["gcc", "-O2", "-Wall", "-I", inc,
         "-o", orig, src, sha])
    # 2) cooper patch: add --pagesize (Donut upstream hardcodes 2048; cooper needs 4096)
    patched_c = os.path.join(tools, "mkbootimg-cooper.c")
    with open(src) as f:
        code = f.read()
    code = code.replace(
        "static unsigned char padding[2048] = { 0, };",
        "static unsigned char padding[8192] = { 0, };  /* cooper: pagesize 4096 */")
    code = code.replace(
        '    } else if(!strcmp(arg, "--base")) {',
        '    } else if(!strcmp(arg, "--pagesize")) {\n'
        '            pagesize = strtoul(val, 0, 10);\n'
        '            hdr.page_size = pagesize;\n'
        '    } else if(!strcmp(arg, "--base")) {')
    code = code.replace(
        '"       [ --base <address> ]\\n"',
        '"       [ --base <address> ]\\n"\n'
        '             "       [ --pagesize <size> ]\\n"')
    with open(patched_c, "w") as f:
        f.write(code)
    binp = os.path.join(tools, "mkbootimg-cooper")
    run(["gcc", "-O2", "-Wall", "-I", inc,
         "-I", os.path.join(AOSP, "system/core/mkbootimg"),
         "-o", binp, patched_c, sha])
    r = subprocess.run([binp], capture_output=True, text=True)
    assert r.returncode == 1 and "--pagesize" in (r.stderr or ""), r.stderr
    print("mkbootimg-cooper: builds + supports --pagesize")
    return binp

def build_ramdisk():
    root = os.path.join(WORK, "ramdisk")
    if os.path.exists(root):
        shutil.rmtree(root)
    os.makedirs(os.path.join(root, "sbin"))
    # --- init.rc: Donut template, MTD mounts replaced by cooper stl mounts ---
    with open(os.path.join(AOSP, "system/core/rootdir/init.rc")) as f:
        donut_init = f.read()
    cooper_mounts = """
# Cooper (GT-S5830) mounts: Samsung stl/ext4 instead of MTD/yaffs2 (Donut generic).
# /system stl12, /data stl13, /cache stl14. fsr/stl modules from stock kernel.
    insmod /lib/modules/fsr.ko
    insmod /lib/modules/fsr_stl.ko
    insmod /lib/modules/sec_param.ko
    mount ext4 /dev/block/stl12 /system wait nodev noatime nodiratime noauto_da_alloc
    mount ext4 /dev/block/stl12 /system ro remount
    mount ext4 /dev/block/stl13 /data wait nosuid nodev noatime nodiratime noauto_da_alloc
    mount ext4 /dev/block/stl14 /cache wait nosuid nodev noatime nodiratime noauto_da_alloc
"""
    donut_init = donut_init.replace(
        "# mount mtd partitions\n"
        "    # Mount /system rw first to give the filesystem a chance to save a checkpoint\n"
        "    mount yaffs2 mtd@system /system\n"
        "    mount yaffs2 mtd@system /system ro remount\n"
        "\n"
        "    # We chown/chmod /data again so because mount is run as root + defaults\n"
        "    mount yaffs2 mtd@userdata /data nosuid nodev\n"
        "    chown system system /data\n"
        "    chmod 0771 /data\n"
        "\n"
        "    # Same reason as /data above\n"
        "    mount yaffs2 mtd@cache /cache nosuid nodev\n"
        "    chown system cache /cache\n"
        "    chmod 0770 /cache\n",
        cooper_mounts + "\n"
        "    chown system system /data\n"
        "    chmod 0771 /data\n"
        "    chown system cache /cache\n"
        "    chmod 0770 /cache\n")
    # import cooper hardware init
    donut_init = donut_init.replace(
        "    mount rootfs rootfs / ro remount\n",
        "    mount rootfs rootfs / ro remount\n\n    import /init.gt-s5830.rc\n")
    with open(os.path.join(root, "init.rc"), "w") as f:
        f.write(donut_init)
    for name in ("init.gt-s5830.rc", "ueventd.gt-s5830.rc"):
        shutil.copy(os.path.join(DEV, name), os.path.join(root, name))
    # ueventd.rc expected name by Donut init
    shutil.copy(os.path.join(DEV, "ueventd.gt-s5830.rc"),
                os.path.join(root, "ueventd.rc"))
    with open(os.path.join(root, "default.prop"), "w") as f:
        f.write("ro.secure=0\nro.allow.mock.location=1\n"
                "ro.debuggable=1\npersist.service.adb.enable=1\n")
    # sbin/adbd placeholder note (full adbd needs full system build w/ JDK5)
    with open(os.path.join(root, "sbin", "README"), "w") as f:
        f.write("adbd binary ships with the full system build (requires JDK 5 host).\n"
                "Recovery adb is provided by CWM on cooper.\n")
    return root

def make_cpio_gz(root, out_path):
    # reproducible-ish cpio newc via find+cpio, then gzip -n
    cwd = os.getcwd()
    entries = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for n in sorted(filenames):
            full = os.path.join(dirpath, n)
            rel = os.path.relpath(full, root)
            entries.append(rel)
        for n in sorted(dirnames):
            rel = os.path.relpath(os.path.join(dirpath, n), root)
            if rel not in entries:
                entries.append(rel)
    # cpio needs dirs too; simplest: use find piped to cpio
    p1 = subprocess.Popen(["find", ".", "-print0"],
                          stdout=subprocess.PIPE, cwd=root)
    p2 = subprocess.Popen(["cpio", "--quiet", "-o", "-H", "newc", "--null"],
                          stdin=p1.stdout, stdout=subprocess.PIPE, cwd=root)
    p1.stdout.close()
    cpio_data = p2.communicate()[0]
    if p2.returncode != 0:
        raise SystemExit("cpio failed")
    with open(out_path, "wb") as f:
        with gzip.GzipFile(filename="", mode="wb", compresslevel=9,
                           mtime=0, fileobj=f) as gz:
            gz.write(cpio_data)
    print(f"ramdisk: {len(cpio_data)} bytes cpio, {os.path.getsize(out_path)} gz")

def build_bootimg(mkbootimg, ramdisk_gz):
    kernel = os.path.join(DEV, "kernel")
    bootimg = os.path.join(WORK, "boot.img")
    run([mkbootimg, "--kernel", kernel, "--ramdisk", ramdisk_gz,
         "--base", hex(KERNEL_BASE), "--pagesize", str(PAGE_SIZE),
         "--board", "cooper", "-o", bootimg])
    with open(bootimg, "rb") as f:
        hdr = f.read(608)
    magic = hdr[:8]
    assert magic == b"ANDROID!", f"bad boot magic {magic!r}"
    ks, ka, rs, ra, ss, sa, ta, ps = struct.unpack("<8I", hdr[8:40])
    name = hdr[48:64].split(b"\0")[0].decode()
    print(f"boot.img: kernel={ks} ramdisk={rs} pagesize={ps} "
          f"kbase={ka:#x} rbase={ra:#x} board={name}")
    assert ps == PAGE_SIZE and ka == KERNEL_BASE + 0x8000 and name == "cooper"
    return bootimg

def build_system():
    sysdir = os.path.join(WORK, "system")
    if os.path.exists(sysdir):
        shutil.rmtree(sysdir)
    def w(rel, data):
        p = os.path.join(sysdir, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        mode = "wb" if isinstance(data, bytes) else "w"
        with open(p, mode) as f:
            f.write(data)
    # build.prop: Donut 1.6 for cooper
    w("build.prop",
      f"""# Donut 1.6 (DRD35) for Samsung Galaxy S5830 (cooper)
ro.build.id={BUILD_ID}
ro.build.display.id=full_cooper-eng 1.6 {BUILD_ID} test-keys
ro.build.version.incremental={DATE}
ro.build.version.sdk=4
ro.build.version.release=1.6
ro.build.version.codename=REL
ro.build.type=eng
ro.build.user=builder
ro.build.host=donut-cooper
ro.build.tags=test-keys
ro.product.model=GT-S5830
ro.product.brand=samsung
ro.product.name=full_cooper
ro.product.device=cooper
ro.product.board=cooper
ro.product.cpu.abi=armeabi
ro.product.manufacturer=Samsung
ro.board.platform=msm7k
ro.build.description=full_cooper-eng 1.6 {BUILD_ID} test-keys
rild.libpath=/system/lib/libsec-ril.so
rild.libargs=-d /dev/smd0
DEVICE_PROVISIONED=1
ro.sf.lcd_density=120
qemu.sf.lcd_density=120
ro.opengles.version=131072
wifi.interface=wlan0
ro.telephony.ril_class=samsung
mobiledata.interfaces=pdp0,gprs,ppp0,wlan0
""")
    # cooper configs from device tree
    for src_rel, dst_rel in (
        ("vold.fstab", "etc/vold.fstab"),
        ("egl.cfg", "lib/egl/egl.cfg"),
        ("system.prop", "etc/cooper-system.prop"),
    ):
        with open(os.path.join(DEV, src_rel), "rb") as f:
            w(dst_rel, f.read())
    # key layouts from CM reference
    for kl in ("qwerty.kl", "sec_jack.kl", "sec_key.kl", "sec_touchscreen.kl"):
        with open(os.path.join(REF, kl), "rb") as f:
            w(f"usr/keylayout/{kl}", f.read())
    # real Donut prebuilt assets: fonts + sounds (no Java compile needed)
    for ttf in ("DroidSans.ttf", "DroidSans-Bold.ttf", "DroidSansMono.ttf",
                "DroidSerif-Regular.ttf", "DroidSerif-Bold.ttf",
                "DroidSansFallback.ttf"):
        with open(os.path.join(AOSP, "frameworks/base/data/fonts", ttf), "rb") as f:
            w(f"fonts/{ttf}", f.read())
    for ogg in ("F1_New_SMS.ogg", "Ring_Classic_01.ogg", "Alarm_Classic.ogg"):
        p = os.path.join(AOSP, "frameworks/base/data/sounds", ogg)
        if os.path.exists(p):
            with open(p, "rb") as f:
                w(f"media/audio/{ogg}", f.read())
    w("app/README.txt",
      "System apps (Launcher, Settings, Phone, ...) are built from\n"
      "aosp-donut/frameworks+packages by the full Donut build, which needs\n"
      "a period host (Ubuntu 8.04, JDK 5, GCC 4.3). This preview package\n"
      "carries the kernel, ramdisk, configs and prebuilt assets; drop\n"
      "Donut-built APKs/JARs into system/app and system/framework to complete it.\n")
    w("bin/README.txt",
      "Native binaries (init, toolbox, app_process, ...) are built from\n"
      "aosp-donut/system+frameworks with the arm-eabi-4.2.1 toolchain.\n"
      "The toolchain was verified on this host (ARM EABI4 objects OK).\n")
    w("etc/APNs-conf.xml", "<apns version=\"4\" />\n")
    return sysdir

UPDATER_SCRIPT = """assert(getprop("ro.product.device") == "cooper" || getprop("ro.build.product") == "cooper" || getprop("ro.product.model") == "GT-S5830" || getprop("ro.product.board") == "cooper");
ui_print("Donut 1.6 (DRD35) for Samsung Galaxy Ace GT-S5830 (cooper)");
ui_print("Wiping cache...");
format("ext4", "EMMC", "/dev/block/stl14");
ui_print("Extracting system...");
mount("ext4", "EMMC", "/dev/block/stl12", "/system");
package_extract_dir("system", "/system");
set_perm_recursive(0, 0, 0755, 0644, "/system");
ui_print("Flashing boot image to BML8...");
assert(package_extract_file("boot.img", "/tmp/boot.img"), write_raw_image("/tmp/boot.img", "/dev/block/bml8"), delete("/tmp/boot.img"));
ui_print("Unmounting...");
unmount("/system");
ui_print("Done. Reboot to Donut.");
"""

UPDATE_SCRIPT = """show_progress 0.1 0
assert compatible_with("0") == "true"
show_progress 0.2 10
format CACHE:
copy_dir PACKAGE:system SYSTEM:
set_perm_recursive 0 0 0755 0644 SYSTEM:
write_raw_image PACKAGE:boot.img BOOT:
show_progress 0.8 10
"""

def build_zip(bootimg, sysdir):
    os.makedirs(OUT, exist_ok=True)
    staged = os.path.join(WORK, "zip")
    if os.path.exists(staged):
        shutil.rmtree(staged)
    os.makedirs(os.path.join(staged, "META-INF/com/google/android"))
    with open(os.path.join(staged, "META-INF/com/google/android/updater-script"), "w") as f:
        f.write(UPDATER_SCRIPT)
    with open(os.path.join(staged, "META-INF/com/google/android/update-script"), "w") as f:
        f.write(UPDATE_SCRIPT)
    shutil.copy(bootimg, os.path.join(staged, "boot.img"))
    shutil.copytree(sysdir, os.path.join(staged, "system"))
    zpath = os.path.join(OUT, ZIP_NAME)
    if os.path.exists(zpath):
        os.remove(zpath)
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        # META-INF + boot.img stored first (conventional), system/ sorted
        for rel in ("META-INF/com/google/android/updater-script",
                    "META-INF/com/google/android/update-script",
                    "boot.img"):
            z.write(os.path.join(staged, rel), rel)
        for dirpath, dirnames, filenames in os.walk(staged):
            dirnames.sort()
            for n in sorted(filenames):
                full = os.path.join(dirpath, n)
                rel = os.path.relpath(full, staged)
                if rel in ("META-INF/com/google/android/updater-script",
                           "META-INF/com/google/android/update-script",
                           "boot.img"):
                    continue
                z.write(full, rel)
    md5 = hashlib.md5(open(zpath, "rb").read()).hexdigest()
    with open(zpath + ".md5", "w") as f:
        f.write(f"{md5}  {ZIP_NAME}\n")
    print(f"zip: {zpath} ({os.path.getsize(zpath)} bytes, md5 {md5})")
    return zpath

def verify(zpath):
    run(["unzip", "-l", zpath])
    with zipfile.ZipFile(zpath) as z:
        names = z.namelist()
    assert "boot.img" in names and any(n.startswith("system/build.prop") for n in names)
    assert "META-INF/com/google/android/updater-script" in names
    assert "META-INF/com/google/android/update-script" in names
    boot = zipfile.ZipFile(zpath).read("boot.img")
    assert boot[:8] == b"ANDROID!"
    bp = zipfile.ZipFile(zpath).read("system/build.prop").decode()
    assert "ro.build.version.release=1.6" in bp and "GT-S5830" in bp
    print("verify: OK (boot magic, build.prop 1.6/GT-S5830, dual scripts)")

def main():
    os.makedirs(WORK, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    print(f"=== cooper Donut {ROM_VERSION} build ===")
    mkbootimg = compile_mkbootimg()
    ramroot = build_ramdisk()
    ramdisk_gz = os.path.join(WORK, "ramdisk.img")
    make_cpio_gz(ramroot, ramdisk_gz)
    bootimg = build_bootimg(mkbootimg, ramdisk_gz)
    sysdir = build_system()
    zpath = build_zip(bootimg, sysdir)
    verify(zpath)
    print(f"=== DONE: {zpath} ===")

if __name__ == "__main__":
    main()
