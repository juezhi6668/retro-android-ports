# MIUI V7 4.4.4 (`cancro`) on Xiaomi Mi 6

Packing the Mi 3W MIUI V7 package (`miui_MI3WMI4W_5.12.17_974fcd8f8d_4.4.zip`,
473 MB, Android **4.4.4**, `KTU84P`, SDK 19, built 2015-12-23, 32-bit
`armeabi-v7a`, **MSM8974**) onto `sagit`. This is the easiest side port of
the three: the ROM is the *same release* as the booting
[KitKat port](README.md), so the KitKat ramdisk, `init`, `adbd`, `vold`,
`sdcard` and `BOOTCLASSPATH` are reused unchanged — only a repacked
`system.img` is needed. The hardware deltas (MSM8974 HALs vs MSM8998) are
the same class of problem as the [MIUI 4.1.2 port](miui-jb.md); see also
[quirks.md](quirks.md).

## Source package

Unlike the mione OTA, this is a **full OTA**: it carries
`system/` (2153 files, 544 MiB uncompressed), `data/` (MIUI preloads,
written to userdata by the OTA and not part of this pack),
`recovery/` (`recovery-from-boot.p` + `install-recovery.sh`), and
**cancro firmware images** (`boot.img`, `NON-HLOS.bin` modem,
`emmc_appsboot.mbn` aboot, `tz/rpm/sbl1/sdi/DDR`) — **none of which may be
flashed on sagit**; they are MSM8974/Mi-3W binaries.

The updater-script has no `set_perm*`; ownership, modes and SELinux labels
come from **50 `set_metadata`/`set_metadata_recursive` rules**, and the 85
symlinks (54 toolbox links, `bin/sh -> mksh`, `fonts/DroidSans* ->
Roboto*`, `lib/libGLESv3.so -> libGLESv2.so`, `framework/miuisdk.jar ->
/data/miui/miuisdk.apk`, ...) from 16 `symlink` statements. Zip entries are
FAT-style, so again: unpack on ext4, never from the Windows copy.

## system.img

`C:\download\sagit\system_miui7.img` — 1 GiB ext4 (~63 % used), 2275
files / 142 dirs / 85 symlinks,
md5 `b898d8fb4787a15fe0d97bc7fbd909fb`. Built by
`config/miui7-pack.sh` on WSL ext4 (`mke2fs -d`, `^has_journal`, label
`system`), reproducible in one shot; the generated metadata lives next to
it (`config/miui7-system_fs_config`, `config/miui7-system_file_contexts`,
`config/miui7-symlinks.txt`).

## Binder ABI (first-boot blocker, fixed in the pack)

The first flash rebooted after one screen: ramoops showed
`servicemanager` dying on `binder: 579:579 ioctl c0186201 ffb39d98
returned -22` four times, then `init: critical process 'servicemanager'
exited 4 times in 4 minutes; rebooting into recovery mode`. `0xc0186201`
is `BINDER_WRITE_READ` in the **pre-unified binder ABI** (24-byte struct,
`unsigned long` pointers — the bionic kernel headers of QC's 3.4 BSP,
which MIUI cancro was built against); the 4.4.247 kernel ships the
unified binder (`0xc0306201`, 48-byte struct) whose ioctl `default:` case
answers the old command with `-EINVAL`, and the wire format inside
transaction buffers (`flat_binder_object`, `binder_transaction_data`)
differs just as much — so no binary can speak old ABI to this kernel.

Binary scan over all 1121 ELFs in the tree: only `bin/servicemanager`
(own mini-binder) and `lib/libbinder.so` (everybody else's entry point)
carry the old constant — the fix is to overwrite exactly those two with
the AOSP 4.4.4 sagit build's (same platform release, same 32-bit ABI).
Symbol sets were checked before trusting it: the two MIUI-only libbinder
symbols (`IPCThreadState::freeBuffer` / `Parcel::ipcSetDataReference`,
pre-unified signatures) are imported by nobody, and every import of
AOSP libbinder plus its `DT_NEEDED` list is satisfied by the MIUI tree.
`config/miui7-pack.sh` does the swap and asserts both binaries contain
only `0xc0306201` afterwards.

Deviations from the OTA contents, all deliberate:

- **`etc/install-recovery.sh` + `recovery-from-boot.p` are removed.** The
  file is mode 0544 (owner-executable), and the ramdisk's `init.rc` runs it
  as the `flash_recovery` service — it would `applypatch` a cancro 14 MiB
  recovery image onto sagit's recovery partition, sourced from
  `by-name/boot`.
- **MSM8998 firmware closure added** (additive, from the KitKat build's
  `out/target/product/sagit/system/`): 80 files under `vendor/firmware`
  (`a530*`/`a540*` GPU, `adsp.*`, `modem.*`, `mba.mbn`, `wlanmdsp.mbn`,
  `wlan/`) plus `vendor/etc/acdbdata/{Forte,adsp_avs_config.acdb}`. The
  ROM's own msm8974 `etc/firmware` (venus/wcnss/a330 era) is kept —
  harmless, the 4.4.247 kernel just never opens it.
- **`run-as` file capabilities are lost.** The OTA gives it
  `capabilities=0xc0`; `mke2fs -d` does not write `security.capability`
  xattrs. The setgid 2000 survives; with adbd running as root this is
  cosmetic.
- `file_contexts` was generated from the OTA's `selabel` values but is not
  applied to the image (SELinux boots permissive; `mke2fs -d` writes no
  `security.selinux` xattrs).

## Display (boot-logo blocker, fixed in the pack)

The second flash got past init (servicemanager/vold/healthd running) but
hung on the boot logo with `surfaceflinger`/`zygote`/`netd` crash-looping
every ~5 s — the `init: untracked pid NNNN exited` stream in dmesg is
debuggerd's fork writing each tombstone. No crash reached dmesg itself
(no logger sink), but **8 identical tombstones** told the story:
`surfaceflinger` aborts in `SurfaceFlinger::init()` via
`__android_log_assert("EGLctx creation failed")`, having loaded
`/system/vendor/lib/egl/libGLESv2_adreno.so` — the **cancro Adreno 330
(msm8974) userspace GPU driver** — which cannot drive the Mi 6's
Adreno 540. `netd` exits 1 (its `ALOGE` invisible without the logger);
the liblog swap below makes both visible next boot.

Fix in `config/miui7-pack.sh` section 1c — replace the cancro GPU closure
with the [KitKat port's](README.md) libhyb display stack:

- `lib/egl/` → the sagit shims (`egl.cfg` + 9 KB `libEGL_adreno.so`,
  `libGLESv2_adreno.so`, `libGLESv1_CM_adreno.so`, `libGLES_android.so`);
  the shims `hyb_dlopen` the **Android 15 vendor 32-bit Adreno 540
  drivers** from `/system/a15`.
- `/system/a15` ← `vendor/xiaomi/sagit-hyb/root` (35 files, 17 MiB,
  the whole hybrid vendor closure).
- `lib/hw/gralloc.msm8974.so` **and** `gralloc.sagit.so` overwritten with
  the AOSP `gralloc.msm8998.so` shim — `hw_get_module` resolves the variant from cancro's
  `ro.board.platform=msm8974` before it ever tries `.msm8998`, so the shim
  must carry the cancro name.
- `hwcomposer.msm8974.so` / `copybit.msm8974.so` / `memtrack.msm8974.so`
  removed (the port has none; SF falls back to the fbdev path), and
  `vendor/lib/egl/` deleted — the KitKat EGL loader scans both `lib/egl`
  and `vendor/lib/egl`, so the a330 drivers must not survive.
- `bin/fbpresentd` installed (the KitKat ramdisk's `init.msm8998.rc`
  already runs the service; it was just missing here).
- `lib/liblog.so` → the sagit build's patched liblog (falls back to
  `/dev/kmsg` when `/dev/log` is absent, i.e. this kernel without
  `CONFIG_LOGGER`). Symbol coverage checked across the tree first: all
  509 liblog importers satisfied; the only export cancro has that the
  sagit liblog lacks (`__android_log_error_write`) is imported by
  nobody. Effect: **all logcat output lands in dmesg** on the next boot
  (`adb shell dmesg | grep -E 'E/|I/'`), the only observability this
  port has.

## Gralloc ABI (third boot-logo blocker, fixed in the shims)

The third flash still hung at the logo, but `surfaceflinger` now lived
5.2 s per cycle and died cleanly — no tombstone, no abort message. The
only evidence was one klog line per cycle from the EGL shim
(`eglshim: REAL dequeue FAILED r=-22 win=... W=1080 H=1920`); there are
no `grshim: alloc` lines at all, although the shim logs every alloc
outcome. MIUI's `BufferQueue::dequeueBuffer` only yields -22 after
`GraphicBufferAlloc::createGraphicBuffer` fails, and that (verified by
disassembly — it matches AOSP semantics plus one QC twist) merely
forwards `GraphicBuffer::initCheck()`, so the -22 is born in
`GraphicBufferAllocator::alloc`'s device call.

MIUI's `libui` shifts `alloc_device_t` one pointer against KitKat (QC
extension): `GraphicBufferAllocator::alloc` (cancro libui @0x63e4) calls
`mAllocDev->[0x40]` with an extra 8th argument when the QC "extra"
parameter — MIUI `GraphicBufferAlloc` member 0x10, set by
`setGraphicBufferSize()`, forwarded through the 5-arg `GraphicBuffer`
ctor and `initSize` — is non-zero, and `mAllocDev->[0x44]` with a plain
7-argument call when it is zero; `free` (@0x64d8) calls `[0x48]` and
`dump` `[0x4c]`. KitKat lays alloc/free/dump at 0x40/0x44/0x48
(`hw_device_t` is 0x40 bytes with `close` at 0x3c). So with the shim
device as built, MIUI's extra==0 alloc path called **`gr_free`**, which
found no map entry for the garbage "handle" (r1 = w = 1080) and returned
`-EINVAL` — a clean -22, no klog, no crash. On stock MIUI both slots are
the vendor gralloc's own alloc/free, which is why the ROM works at home.

Fix (`vendor/xiaomi/sagit/gralloc/gralloc.c`, kept in both the KitKat
tree and the ports tree): at `gpu0` open the shim now checks
`ro.miui.ui.version.name` (any MIUI image sets it) and, on MIUI,
installs `gr_alloc` at **both** [0x40] and [0x44] (the 8th argument is
ignored by the 7-argument C function), `gr_free` at [0x48] and a no-op
dump at [0x4c]; everywhere else the KitKat layout is kept, so the
booting KitKat port is unaffected.

Fourth flash (2026-10-24, shims pushed via TWRP onto the mounted
system): `grshim: gpu0 open miui layout`, `grshim: alloc 1080x1920 fmt
0x1 usage 0x10001a03 ok stride 1088`, zero dequeue failures, and
**surfaceflinger stays up with the MIUI bootanimation running** (pid
758, itself waiting on `media.audio_flinger`). Remaining blockers: both
`zygote` and `mediaserver` exit cleanly ~5.2 s after each start (no
tombstone — a clean exit; cause invisible with the logger missing;
probe next boot by running `app_process`/`mediaserver` manually over
adb shell to catch stderr), plus the modem subsystem restart at t=65 s.

## Boot image

None needed — use the **KitKat port's booting `boot.img` as-is**
(`out/target/product/sagit/boot.img`). MIUI V7 ships every jar the 4.4
build's `BOOTCLASSPATH` lists (`core`, `core-libart`, `framework2`,
`webviewchromium`, `okhttp`, `telephony-common`, `mms-common`, ...), and
its `sdcard`/`vold` are KitKat binaries: `vold` reads the ramdisk's
`fstab.msm8998`, the ROM's `etc/vold.fstab` (`msm_sdcc.2` SD slot) is inert.
Never flash the zip's `boot.img` or the `.mbn`/`NON-HLOS.bin` images.

## Flashing

Same recipe as the KitKat port, in TWRP: reformat `userdata` and `cache`
(MIUI V7 will not tolerate an Android 15 FBE `/data`), then

```
dd if=/sdcard/system_miui7.img of=/dev/block/sda18 bs=4M
sync
```

with the md5 read back, and the KitKat `boot.img` dd'd to `boot` if the
phone is not already on it.

## Open gaps (same shape as the 4.1.2 port)

- **Display is solved** (libhyb closure + gralloc ABI shift above). The
  remaining HALs are still msm8974: `audio.primary.msm8974`,
  `camera.msm8974`, `lights.msm8974`, `sensors.st_mve`, plus 451
  `vendor/lib` QC blobs. Audio wants the KitKat port's tinyalsa +
  TFA9888/QUAT_MI2S path ([quirks.md](quirks.md#audio-speaker-verified-2026-10-24));
  the others are not started. With mediaserver dead, MIUI's bootanim
  waits for `media.audio_flinger`.
- **zygote + mediaserver exit cleanly every ~5.2 s** (post-gralloc-fix
  blocker; no tombstone, no logger). First probe: run `app_process`
  / `mediaserver` by hand over adb shell to capture stderr.
- **Kernel modules cannot load**: `lib/modules/pronto/pronto_wlan.ko`
  (prima) was built for the cancro 3.4 kernel; sagit's 4.4.247 kernel
  carries its own qca_cld3 driver and needs `wlan/qca_cld` firmware, which
  the closure provides.
- **RIL**: qcril for msm8974; same "no cellular" posture as the other
  ports. Camera / GPS / BT / fingerprint not started.
- MIUI's `vendor/firmware` ncd files (Broadcom NFC for Mi 3W) are inert
  here.
