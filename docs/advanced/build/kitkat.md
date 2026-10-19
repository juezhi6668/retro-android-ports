# Building KitKat 4.4.4 for maple

Step-by-step build of the maple KitKat port: pristine AOSP
`android-4.4.4_r2` → Trusty/JDK 6 container → patched tree + device tree +
dumped 4.4 kernel → TWRP-flashed images. Every trap hit along the way,
with on-device evidence, is in the
[KitKat porting notes](../porting-notes/kitkat.md) and the
[maple quirks](../../devices/maple/quirks.md) bringup log. Hardware GL
(the in-process Adreno driver) has its own
[tool page](../software/libhyb.md).

If you build on a remote host, read the
[build server runbook](../build-server.md) first — it has the server
layout, background-launch footguns and the flash workflow.

## 1. Host container

KitKat wants **JDK 6** and a Trusty-era userland. The recipe lives in
[`containers/kitkat/`](../../../containers/kitkat/):

```sh
docker build -f containers/kitkat/Dockerfile -t retro-kitkat:4.4.4 containers/kitkat
docker run --rm -it \
  -v <local aosp-kitkat>:/srv/aosp-kitkat \
  -v ~/ccache-kitkat:/srv/ccache-kitkat \
  retro-kitkat:4.4.4
```

The image provides the toolchain only (`lib32stdc++6 lib32z1`, a `python`
symlink, `openjdk-6-jdk`); the tree is bind-mounted, never baked in.

## 2. Get the source (shallow!)

```sh
repo init -u https://android.googlesource.com/platform/manifest \
    -b android-4.4.4_r2 --depth=1
repo sync -c --no-tags -j6                  # -> aosp-kitkat/ (~14 GB)
```

A full-history sync balloons past 150 GB — never run bare `repo sync`.

## 3. Apply the port

```sh
# patches: one git diff per AOSP project, applied from that project's root
# (full table + what each does: patches/kitkat/README.md)
cd aosp-kitkat
( cd build              && git apply ../../patches/kitkat/build-jdk-openjdk6.patch )
( cd bionic             && git apply ../../patches/kitkat/bionic-binder-uapi-64bit.patch )
( cd frameworks/native  && git apply ../../patches/kitkat/frameworks-native-binder-64bit.patch )
( cd system/core        && git apply ../../patches/kitkat/system-core-liblog-kmsg-fallback.patch )
( cd system/core        && git apply ../../patches/kitkat/system-core-liblog-kmsg-terminator.patch )
( cd system/core        && git apply ../../patches/kitkat/system-core-elapsedrealtime-boottime.patch )
( cd system/core        && git apply ../../patches/kitkat/system-core-fsmgr-ext4-project-quota.patch )
( cd dalvik             && git apply ../../patches/kitkat/dalvik-zygote-capset-v3.patch )
( cd frameworks/base    && git apply ../../patches/kitkat/frameworks-base-time-without-dev-alarm.patch )
( cd frameworks/base    && git apply ../../patches/kitkat/frameworks-base-disable-atlas.patch )
( cd frameworks/base    && git apply ../../patches/kitkat/frameworks-base-hwui-gles2-cap.patch )
( cd frameworks/base    && git apply ../../patches/kitkat/frameworks-base-input-dispatch-freeze-thaw.patch )
( cd frameworks/base    && git apply ../../patches/kitkat/frameworks-base-keyguard-scrim-no-input.patch )
( cd frameworks/base    && git apply ../../patches/kitkat/frameworks-base-systemui-wallpaper-canvas-fallback.patch )
( cd hardware/libhardware && git apply ../../patches/kitkat/hardware-libhardware-gralloc-fb-stride.patch )
( cd packages/apps/Settings && git apply ../../patches/kitkat/packages-apps-settings-kernel-version.patch )

# device tree -> $AOSP_DIR/vendor/sony/maple
scripts/sync-device-tree.sh maple kitkat
```

`sync-device-tree.sh` uses `rsync --delete`, so the staged graphics
closure (`vendor/sony/maple-hyb/`) and the NDK-built presenter
(`vendor/sony/maple-bin/`) live **outside** the synced path — that is what
`full_maple.mk` copies from (step 6). The one staged file that *cannot*
live outside is the kernel blob of step 4 (it must sit at
`vendor/sony/maple/kernel` for `TARGET_PREBUILT_KERNEL`), so the script
excludes `kernel` from the delete — without that, re-syncing after step 4
removes the blob and `make bootimage` fails with `No rule to make target
'vendor/sony/maple/kernel'` (hit 2026-10-19 on the local kitkat tree).

## 4. Kernel (dumped, not built)

`BoardConfig.mk` sets `TARGET_PREBUILT_KERNEL := vendor/sony/maple/kernel`,
so `make bootimage` packs the built ramdisk around the **4.4 kernel dumped
from the running device** (the blob is gitignored, like all vendor blobs):

```sh
# phone in TWRP
adb pull /dev/block/bootdevice/by-name/boot boot-dump.img
abootimg -x boot-dump.img                    # -> kernel (Image.gz-dtb)
cp kernel $AOSP_DIR/vendor/sony/maple/kernel
scripts/patch-maple-dtb.py $AOSP_DIR/vendor/sony/maple/kernel   # needs dtc
```

`patch-maple-dtb.py` strips `qcom,mdss-idle-power-collapse-enabled` (panel
would blank when idle) and `qcom,wcn-btfm` (audio card probe) from every
appended DTB. It is idempotent and byte-verifies its edit; re-run it if the
dumped kernel is replaced.

## 5. Build

Inside the container:

```sh
source build/envsetup.sh
lunch full_maple-eng
make -j$(nproc)              # full build (~1 h, needed after UAPI/bionic changes)
make bootimage systemimage   # iteration targets
```

KitKat `make` lies about staleness (product `.mk` edits don't retrigger
targets) — delete outputs and rebuild, then verify content, never
timestamps. `system.img` builds **sparse**; `simg2img` it before any `dd`.

## 6. libhyb (hardware GL/GLES + audio ACDB)

The graphics stack is the in-process Adreno 540 driver
([libhyb](../software/libhyb.md)): the *device tree* carries the loader,
shims, gralloc and presenter sources; the build needs the **Lineage 22.1
closure** staged and the 64-bit presenter built, both outside the synced
tree (`full_maple.mk` auto-includes `vendor/sony/maple-hyb` + `-bin` when
present, so rebuild the images afterwards):

```sh
tools/libhyb/extract-lineage.sh <lineage-22.1-maple.zip> ~/hyb-sysroot ~/vendorextract/mnt
tools/libhyb/stage.py ~/hyb-sysroot $AOSP_DIR/vendor/sony/maple-hyb
tools/libhyb/build-native.sh $AOSP_DIR          # NDK r21e; builds fbpresentd
make bootimage systemimage
```

`stage.py` walks `DT_NEEDED` from the driver + QTI gralloc1, adds the
runtime `dlopen` modules and the a540 firmware, and lists what it staged,
what KitKat provides itself and what is missing — a missing entry that is
not `libc/libm/libdl/liblog/libcutils/libz` (nor the virtual
`ld-android`/`libdl_android`) means the closure is incomplete. It also
stages the **audio ACDB** consumer (`libacdbloader.so` + its closure) and
copies the yoshino acdb data to `acdbdata/MTP/`, which `full_maple.mk`
installs to `/system/vendor/etc/acdbdata/MTP/` (the name
`/sys/devices/soc0/hw_platform` reports). Skipping the whole step is
supported: without the closure the EGL shims fail their load and EGL falls
back to the ES1-only `libagl` software path, and the audio HAL runs
uncalibrated (silent).

Rebuild the images after re-staging, and re-stage after changing any
device-tree graphics source — see the staging rules in
[libhyb](../software/libhyb.md#building-and-staging).

## 7. Flash and verify

Phone in TWRP, adb up. Push to `/data/media` (not `/sdcard`), never pipe
stdin over `adb shell`:

```sh
# system: unsparsify with the build's host tool (32-bit; run it where it runs)
aosp-kitkat/out/host/linux-x86/bin/simg2img system.img system.raw.img

adb push system.raw.img /data/media/
adb shell dd if=/data/media/system.raw.img \
    of=/dev/block/bootdevice/by-name/system bs=4M

adb push boot.img /data/media/
adb shell dd if=/data/media/boot.img \
    of=/dev/block/bootdevice/by-name/boot bs=4M
```

**Verify every flash by readback** (md5 of the pushed file vs the
partition) before rebooting. First boot checks: `sys.boot_completed=1`,
`hybtest` (ES3 context + pixel readback through the closure), `dmesg |
grep -i hyb`, `dumpsys SurfaceFlinger` shows `Adreno (TM) 540, OpenGL ES
3.2`, `/sdcard` and mounts present. Recovery from a stuck splash:
"stuck on splash" splits by USB — no USB at all is a pre-userspace hang;
recover via a TWRP round-trip (`adb reboot` from TWRP) before reflashing
anything ([triage rule](../porting-notes/kitkat.md)).

Flash-failure forensics (`/data/misc/watch*.txt`, tombstones, dropbox,
pstore) and the entire bringup TEST log live in
[maple quirks](../../devices/maple/quirks.md).

## Building without the server (workstation)

Same recipe as the [build server runbook](../build-server.md), run on the
local workstation (proven 2026-10-19 while the server was down). Only the
source fetch, the two out-of-git inputs and the flash conversion differ.

- **Container**: needs a running Docker daemon; `docker build -f
  containers/kitkat/Dockerfile -t retro-kitkat:4.4.4 containers/kitkat`
  pulls Trusty from `archive.ubuntu.com`, which still serves `trusty`
  (verified 2026-10-19). Build with the tree and a local ccache mounted:
  `docker run --rm -v ~/aosp-kitkat:/srv/aosp-kitkat -v
  ~/ccache-kitkat:/srv/ccache-kitkat retro-kitkat:4.4.4 bash -c 'cd
  /srv/aosp-kitkat && source build/envsetup.sh && lunch full_maple-eng &&
  make -j8'`.
- **Source**: `repo` is not packaged on the Arch workstation — fetch the
  launcher to `~/bin/repo` (`curl -sSL
  https://storage.googleapis.com/git-repo-downloads/repo`), then §2 as
  written (`repo init ... -b android-4.4.4_r2 --depth=1 && repo sync -c
  --no-tags -j8` → 14 GB). The sync runs on the host (python3), not in the
  Trusty container.
- **Patches + device tree + kernel**: §3 and §4. The patched kernel blob
  also exists off-server: the maple *donut* work tree keeps
  `work/maple-donut/kernel-Image.gz-dtb` (md5
  `4a7b3eaf19f808c15a0f45bbe455f2b8`); copying it to
  `vendor/sony/maple/kernel` and re-running `scripts/patch-maple-dtb.py`
  reports `nothing to do` (idempotent — that is the check the blob already
  carries the edits).
- **Firmware**: `vendor/sony/maple-fw/{adsp,wlan,modem,bt}` comes from the
  phone, not the server. In TWRP: `mount -t vfat
  /dev/block/bootdevice/by-name/modem /mnt/modem`, copy
  `image/{adsp.*,cpe_9335.*,bdwlan*,wlanmdsp.mbn,modem.mdt,modem.b*,mba.mbn}`
  out (`wlan_mac.bin` from `/persist/wlan/`), and md5-verify every file
  against the phone before staging (79 files on the 2026-10-19 run).
- **Closure**: the Lineage 22.1 zip is the SourceForge maple build
  `lineage-22.1-20250115-UNOFFICIAL-maple.zip` (~1.1 GB) from
  `sourceforge.net/projects/sony-xperia-xz-premium-rom/files/lineage/maple/A15/2025-1-15/`;
  §6's `extract-lineage.sh` auto-extracts vendor from the zip's
  `vendor.new.dat.br` (no third argument needed). `build-native.sh`
  downloads NDK r21e itself (`~/.cache/retro-android-ports/ndk`, ~1.1 GB).
- **Keep the NDK out of `$AOSP`**: the old `build-native.sh` cache default
  was `$AOSP/out-blobs`, and the AOSP make walks the whole tree for
  `Android.mk` files — the NDK's `sources/android/cpufeatures/Android.mk`
  then collides with the tree's `ndk/` copy and every later make dies at
  parse: `*** out-blobs/android-ndk-r21e/sources/android/cpufeatures:
  MODULE.TARGET.STATIC_LIBRARIES.cpufeatures already defined by
  ndk/sources/android/cpufeatures.  Stop.` The build that races the unzip
  can still *succeed* (it parsed before the NDK landed); the failure
  surfaces on the next image rebuild. Fixed in the script 2026-10-19.
- **Flash**: the workstation has no `simg2img`, so unsparsify **on the
  phone** with TWRP's `/sbin/simg2img` (verified: two-argument usage) and
  `dd` from `/data/media`. Read-backs must use the exact image size — the
  system partition is 15,052,800 sectors (~7.2 GB) and boot 131,072 (64 MB),
  so `dd if=<partition> bs=4096 count=<image bytes>/4096 | md5sum` is the
  form that matches the staged file. TWRP has busybox `md5sum`; the *booted*
  port's toolbox has only `md5` (and no `head`/`tail`/`grep`), so script
  on-device helper commands accordingly. First-boot checklist: §7.
