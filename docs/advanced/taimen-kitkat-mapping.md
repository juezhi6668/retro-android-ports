# Taimen-KitKat mapping (Pixel 2 XL port → maple KitKat)

Reference notes: how [DoomGuy41/Taimen-KitKat](https://github.com/DoomGuy41/Taimen-KitKat)
(Pixel 2 XL / `taimen`, MSM8998, KitKat 4.4.4 on the stock Android 11
kernel + a 64-bit Android 11 closure in `/system/v11`) maps onto
maple/KitKat. Read and ported 2026-10-18. The graphics core — libhyb with
the in-process Adreno 540 driver, the KitKat gralloc on QTI gralloc1, the
64-bit `fbpresentd` — is ported and rewritten for maple's LineageOS 22.1
closure: [libhyb](software/libhyb.md), build evidence in
[maple quirks](../devices/maple/quirks.md) TEST 49. This page is the
disposition of *everything else* in their patch set: adopted, skipped and
queued. Generic lessons: [KitKat porting notes](porting-notes/kitkat.md).
Back to the [advanced index](README.md).

## Adopted (`patches/kitkat/`)

| Taimen patch | Ours | Why |
| --- | --- | --- |
| `system/core/0003-liblog-fall-back-to-dev-kmsg` | `system-core-liblog-kmsg-fallback.patch` | the 4.4 kernel has no `CONFIG_LOGGER`; without it every `ALOG*` in the tree (and in the libhyb-loaded libraries, which resolve `liblog.so` to KitKat's) is invisible |
| `system/core/0004-liblog-terminate-dev-kmsg-records` | `system-core-liblog-kmsg-terminator.patch` | kernel joins records without a trailing `\n` |
| `system/core/0007-fs_mgr-clear-the-ext4-project-quota-feature` | `system-core-fsmgr-ext4-project-quota.patch` | a LineageOS mke2fs on slot B sets `RO_COMPAT_PROJECT`; the 4.4 kernel refuses the `/data` mount |
| `system/core/0008-elapsedRealtime-on-CLOCK_BOOTTIME` | `system-core-elapsedrealtime-boottime.patch` | no `/dev/alarm`; also fixes the truncated 1 KiB kernel-cmdline read (maple's is longer) |
| `dalvik/0001` + `art/0001` (time without `/dev/alarm`, capset) | `dalvik-zygote-capset-v3.patch` (+ `frameworks-base-time-without-dev-alarm.patch`) | `capset` ABI v3 so `system_server` keeps `CAP_WAKE_ALARM`/`CAP_BLOCK_SUSPEND` for the timerfd alarms; only the Dalvik twin applies here (KitKat's default runtime) |
| `frameworks/base/0001-SystemUI-canvas-wallpaper` | `frameworks-base-systemui-wallpaper-canvas-fallback.patch` | we had converged on this independently during the glrenderd bringup; still the safety net when EGL has no ES2 config |
| `frameworks/base/0002-SystemServer-config.disable_atlas` | `frameworks-base-disable-atlas.patch` | the Lollipop kill-switch for the asset atlas, which assumes a GPU and kills `system_server` when it can't build one |
| `frameworks/base/0003-hwui-cap-the-reported-GL-version-at-2.0` | `frameworks-base-hwui-gles2-cap.patch` | the driver advertises ES 3.2; KitKat's hwui is only tested with ES2 behaviour |
| `frameworks/base/0004-Time-and-alarms-without-dev-alarm` | `frameworks-base-time-without-dev-alarm.patch` | `AlarmManagerService` on `*_ALARM` timerfds + `settimeofday` |
| `packages/apps/Settings/0001-kernel-version-for-a-kernel-built-with-clang` | `packages-apps-settings-kernel-version.patch` | `/proc/version` has a clang-built kernel; the stock GCC-only regex shows "Unknown" |
| `hardware/libhardware/0001-gralloc-framebuffer-stride` | `hardware-libhardware-gralloc-fb-stride.patch` | we had this one independently (mdss pads 1080→1088) |
| `frameworks/native/0001` (binder half) | `bionic-binder-uapi-64bit.patch` + `frameworks-native-binder-64bit.patch` | they credit this tree as the source |
| `frameworks/native/0002-EGL-use-the-hardware-driver` | *no patch* | it removes exactly what `frameworks/native/0001` had added for their glrenderd era; stock KitKat `checkGlesEmulationStatus` is the end state (dropping our glrenderd EGL patch was the whole port) |

## Skipped, and why

| Taimen patch | Why not here |
| --- | --- |
| `art/0001` (second copy) | maple runs Dalvik; the ART twin of the capset/time fix is dead code |
| `system/core/0005-0006 fs_config ... chroot` | the chroot design is gone (and wasn't needed for maple's libhyb: nothing under `/system/a15` is executed, `fbpresentd` is a single static binary) |
| `build/0001-definitions.mk sort (make 3.82)` | our container pins **Make 3.81**, the release's documented requirement — no reason to patch the build system for a newer host make |
| `build/0002-signed-release-keys` | we build `full_maple-eng` and flash by hand; no release-signing pipeline |
| `external/chromium_org WebKit (idl_serializer, rule_bison)` | host-tool fixes for Bison 3 / Perl 5.36; the Trusty container has period tools and the build passes unpatched |
| `frameworks/av HEVC` + `frameworks/native OMX HEVC` | media features; maple has no working video stack yet (out of scope) |
| `hardware/ril` (4 patches: AT+GSN, SIM-absent while radio off, persisted SIM state) | cellular isn't started on maple; when it is, these are *candidates*, but the modem behaviour behind them must be re-verified on yoshino firmware, not assumed |
| `system/core/0001-0002 (servicemanager/ueventd/healthd not critical)` | bring-up convenience on a tree that couldn't boot; maple's boot is past that point, and marking core services non-critical hides real failures |
| `frameworks/base/0005 bootanimation trim.txt + background colour` | only matters with a Lollipop+ boot animation; maple ships the KitKat `bootanimation.zip` (adopt if we ever ship a modern one) |
| `hardware/libhardware_legacy/0001 vibrator LED fallback` | needs a device check of which interface maple's 4.4 kernel exposes (`/sys/class/timed_output` vs `/sys/class/leds/vibrator`); queued, unverified |
| `hardware/libhardware_legacy/0002 wifi_load_driver waits for wlan0` | matches maple's failure mode (no `wlan0` while the modem never boots) but a 30 s stall only hides it; adopt when the modem/wifi chain actually comes up (see quirks) |

## Deliberately different

- **Closure version.** Taimen loads an Android 11 closure from `/system/v11`;
  maple loads **LineageOS 22.1 (Android 15)** from `/system/a15` — same
  design, a wider dependency set and a different flat-log/apex layout.
  Taimen's port is also the older, wider shape: besides libhyb it runs a
  64-bit Android 11 chroot for the modem/GNSS/sensor daemons, which maple
  does not need yet, so only the in-process half is ported here.
- **`rtc-restore` is a C binary, not a busybox script.** Taimen's
  `rtc-restore.sh` reads `/sys/class/rtc/rtc0/since_epoch` + the offset
  `system_server` saves and calls `busybox date -s @N`; maple's ramdisk
  has no busybox, so `devices/maple/kitkat/rtc-restore/` does the same
  job with `settimeofday` in C (and the identical
  `/data/system/time/rtc_offset` file that the frameworks patch writes).
- **`TARGET_BOARD_PLATFORM`/`TARGET_SYSTEM_PROP`/`egl.cfg` layout** follow
  the maple tree (`gralloc.maple`, `BOARD_EGL_CFG := vendor/sony/maple/egl.cfg`),
  and the MDSS structs are vendored from **maple's dumped kernel headers**,
  not Taimen's. `hwui` is enabled on both (`USE_OPENGL_RENDERER := true`);
  Taimen additionally sets `TARGET_DISABLE_TRIPLE_BUFFERING := true`,
  which maple does not (candidate tuning, needs measurement).
