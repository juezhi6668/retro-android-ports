# Building KitKat 4.4.4 for maple

Step-by-step build of the maple KitKat port: pristine AOSP
`android-4.4.4_r2` → Trusty/JDK 6 container → patched tree + device tree +
dumped 4.4 kernel → TWRP-flashed images. Every trap hit along the way,
with on-device evidence, is in the
[KitKat porting notes](../porting-notes/kitkat.md) and the
[maple quirks](../../devices/maple/quirks.md) bringup log. glrenderd
(hardware GL) has its own [tool page](../software/glrenderd.md).

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
( cd build             && git apply ../../patches/kitkat/build-jdk-openjdk6.patch )
( cd bionic            && git apply ../../patches/kitkat/bionic-binder-uapi-64bit.patch )
( cd frameworks/native && git apply ../../patches/kitkat/frameworks-native-binder-64bit.patch )
( cd frameworks/native && git apply ../../patches/kitkat/frameworks-native-egl-glrenderd.patch )
( cd frameworks/native && git apply ../../patches/kitkat/frameworks-native-glconsumer-glgeterror-bound.patch )
( cd device/generic/goldfish && git apply ../../patches/kitkat/device-generic-goldfish-glrenderd.patch )
( cd sdk               && git apply ../../patches/kitkat/sdk-glrenderd.patch )
( cd system/core       && git apply ../../patches/kitkat/system-core-glrenderd-fs-config.patch )
( cd hardware/libhardware && git apply ../../patches/kitkat/hardware-libhardware-gralloc-fb-stride.patch )
( cd frameworks/base   && git apply ../../patches/kitkat/frameworks-base-input-dispatch-freeze-thaw.patch )
( cd frameworks/base   && git apply ../../patches/kitkat/frameworks-base-keyguard-scrim-no-input.patch )
( cd frameworks/base   && git apply ../../patches/kitkat/frameworks-base-systemui-wallpaper-canvas-fallback.patch )

# device tree -> $AOSP_DIR/vendor/sony/maple
scripts/sync-device-tree.sh maple kitkat
```

`sync-device-tree.sh` uses `rsync --delete`, so keep glrenderd's staged
tree at `vendor/sony/maple-glr` (outside the synced path) — that is what
`full_maple.mk` copies from.

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

## 6. glrenderd (hardware GL/GLES)

The guest stack is patched in step 3; the daemon needs its chroot built
and staged (`full_maple.mk` auto-includes `vendor/sony/maple-glr` when
present, so rebuild the images afterwards):

```sh
tools/glrenderd/build.sh $AOSP_DIR <android-ndk-r21e>
tools/glrenderd/extract-lineage.sh <lineage-20-maple.zip> <sysroot> [oem.img]
tools/glrenderd/stage.py <sysroot> tools/glrenderd/out $AOSP_DIR/vendor/sony/maple-glr
make bootimage systemimage
```

Prerequisites and the rebuild rule (rebuild the guest `*_enc.so` from the
same tree if you rebuild the renderer) are in
[glrenderd](../software/glrenderd.md#building-and-staging). Skipping
glrenderd entirely is supported: EGL falls back to the ES1-only `libagl`
software path.

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
`dumpsys SurfaceFlinger` shows `Adreno (TM) 540, OpenGL ES 3.2` through
glrenderd, `/sdcard` and mounts present. Recovery from a stuck splash:
"stuck on splash" splits by USB — no USB at all is a pre-userspace hang;
recover via a TWRP round-trip (`adb reboot` from TWRP) before reflashing
anything ([triage rule](../porting-notes/kitkat.md)).

Flash-failure forensics (`/data/misc/watch*.txt`, tombstones, dropbox,
pstore) and the entire bringup TEST log live in
[maple quirks](../../devices/maple/quirks.md).
