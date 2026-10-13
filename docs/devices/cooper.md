# Donut 1.6 on the Samsung Galaxy S5830 (cooper)

Device-specific notes for the cooper port — the generic playbook lives in
[`docs/porting-notes.md`](../porting-notes.md). Covers GT-S5830/L/T
(`cooper`); the Broadcom GT-S5830i (`cooperve`) is a different SoC entirely
and is NOT covered.

## Platform specifics

### Flash layout (Samsung BML/stl, not MTD)
- `/dev/block/bml8` = boot (8 MB), `bml9` = recovery. `stl12/13/14` =
  system/data/cache. `BOARD_KERNEL_BASE 0x13600000`, pagesize 4096,
  `BOARD_BOOTIMAGE_PARTITION_SIZE 8388608`.
- **Raw writes to BML do not stick** (no erase) and fail silently — verified
  live (bml8 kept the old image across a "successful" write). Always flash
  boot the cooper way: `/sbin/erase_image boot` + `/sbin/flash_image boot`
  (needs the `run_program` backport above). **Verify every flash** by
  reading back and comparing hashes; partial/corrupt writes happen.
- Reference board params live in LineageOS
  `android_device_samsung_cooper@gb-release-7.2` (BoardConfig, init.rc
  files, keylayouts, vold.fstab, egl.cfg, prebuilt zImage + fsr modules).

### The FSR/STL storage stack (the hard part)
- cooper's NAND needs Samsung's proprietary modules: `fsr.ko` (BML),
  `fsr_stl.ko` (STL), `sec_param.ko`. Without them the kernel shows only
  mmcblk (no bml/stl at all).
- Module sets are kernel-specific: CM set (`2.6.35.7-perf-CL882825`) vs
  TWRP/Omni set (`2.6.35.7-1203276`) vs kernels (`-perf-CL882825` /
  `-androidarmv6+`). Match them or use a `CONFIG_MODULE_FORCE_LOAD`
  kernel (shows `(F)` in `/proc/modules`). `modinfo -F vermagic` is law.
- The STL driver registers at insmod but its `stl12/13/14` disks (major
  138, minors **stable: 12/13/14**) only materialize once userspace
  actively scans volumes (open + `BLKGETSIZE64` + superblock-area reads —
  what recovery partition scans and Gingerbread vold do). Donut never
  scans, so Donut never sees stl. Our fix, all inside Donut init
  (`system/core/init/builtins.c`, hook in `do_insmod_inner`): fork a child
  that insmods, scans BML volumes, polls `/proc/partitions`, mknods nodes
  (Donut processes hotplug uevents only in its main loop, too late for
  mounts), mounts stl12/13/14 itself, creates data dirs, then the parent
  runs `class_start default` directly (correct service bookkeeping; no
  property-trigger races).
- Stock RFS partitions **must be converted to ext4** (TWRP wipe) — Donut
  can't format them. Note TWRP 2.7.1's own wipe uses `mkdosfs` (vfat!);
  use `make_ext4fs` explicitly and confirm with `blkid`.
- Keep a known-good `bml8` backup on the PC before experimenting.

### Kernels
- CM kernel (`2.6.35.7-perf-CL882825`, in the CM device tree) + CM modules:
  boots to adb reliably; STL needs the init-side bringup above.
- TWRP/Omni kernel (`2.6.35.7-androidarmv6+`, from bml9) + TWRP modules:
  identical bringup path; USB gadget is `android_usb` (program it in
  init.rc or no adb). Either kernel works once storage is up; we ship the
  TWRP one + TWRP module set (keep the CM set in-tree as fallback).

### Audio (working, 2026-10-12)
- Neither stock Donut audio path can work on cooper: generic audio opens
  HTC-Dream-only `/dev/eac` (absent → `initCheck` fails → *"Using stubbed
  audio hardware. No sound will be produced."*), and Donut's `hardware/msm7k`
  audio needs the proprietary HTC `libhtc_acoustic.so` for endpoint
  discovery + audpp/audpre (absent → same stub). `/dev/msm_snd`,
  `/dev/msm_pcm_out`, `/dev/msm_pcm_ctl` **do** exist on the cooper 2.6.35
  kernel (verified live: `adb pull` on a char device fails with `Invalid
  argument`, while a missing node says `does not exist`); `/dev/snd/*`
  (ALSA) does **not** (Donut ueventd never creates it).
- Fix, all in the vendor tree: `devices/cooper/donut/libaudio/` — Donut
  msm7k `AudioHardware` minus the acoustic dependency. Endpoints are
  discovered directly via `SND_GET_NUM_ENDPOINTS`/`SND_GET_ENDPOINT` on
  `/dev/msm_snd` (the same ioctl path CM7 cooper libaudio uses; 20
  endpoints found live, `HANDSET=0 SPEAKER=6 BT=12 CURRENT=34`), falling
  back to the CM7 cooper defaults; audpp/audpre tuning is a no-op (plain
  PCM still plays). `BoardConfig.mk` leaves `BOARD_USES_GENERIC_AUDIO`
  unset so AudioFlinger links this `libaudio` via `createAudioHardware()`
  (`TARGET_BOARD_PLATFORM := cooper` keeps `hardware/msm7k` out of the
  build, so there is no module clash); `ueventd.gt-s5830.rc` gains
  `0660 system audio` entries for `msm_snd`/`msm_pcm_out`/`msm_pcm_in`/
  `msm_pcm_ctl` (mediaserver runs as `media` + `audio` group).
- **32-vs-40-byte trap (this one cost a crash-loop):** Donut bionic's
  `struct msm_audio_config` is 32 bytes, but the 2.6.35 kernel's is 40
  (`meta_field` + `bits` after `type`, same offsets for the first five
  fields — compare CM7 `msm_audio.h`). Passing Donut's struct to
  `AUDIO_GET_CONFIG` makes the kernel copy 8 bytes past the stack slot:
  mediaserver SIGSEGVs in `MixerThread::threadLoop` at the
  `mOutput->write()` call site (fault addr 0; diagnosed via tombstones +
  `arm-eabi-addr2line` against `out/.../symbols/...`). Fix is
  `cooper_msm_audio_config` (kernel layout, `memset` + `type=PCM`,
  `meta_field=0`, `bits=16`) for every PCM GET/SET_CONFIG ioctl, in both
  output and input paths. Verified: endpoints → "Routing audio to
  Speakerphone" → volume ioctls → both mixer threads ready, no tombstones,
  ringtone preview audible on the speaker.

### Display density (160, not 120)
- Shipped `ro.sf.lcd_density=120` first ("ldpi-ish" guess) — everything
  rendered tiny. The framebuffer reports 51×76 mm for 320×480
  (~159/160 dpi), and every other cooper ROM agrees: CM7's `system.prop`
  says 120 but its `device_cooper.mk` overrides to 160 via
  `PRODUCT_PROPERTY_OVERRIDES`, and TWRP/Omni `default.prop` is 160. The
  Ace is a ~165 dpi physical panel: **160 (mdpi) is correct**.
  `devices/cooper/donut/system.prop` now sets both `ro.sf.lcd_density`
  and `qemu.sf.lcd_density` to 160 (verified live in `/system/build.prop`).

### MLXProjects `android_device_mlx_test` (froyo branch) audit
- Same problem space (old Android on newer kernels) but **no audio
  solution**: it also uses `BOARD_USES_GENERIC_AUDIO` and its TODO says
  "Enable sound using ALSA" (not done). Useful as confirmation/reference
  only: DPI 160, `TARGET_PROVIDES_INIT_RC`, mkbootimg pagesize patch
  (already equivalent to our `mkbootimg-cooper`), `lights/` + `gralloc.test`
  patterns (not needed — Donut ignores `egl.cfg`, software `libagl`
  already drives the panel), and `vtouch` (already ported and
  cooper-adapted: explicit `/dev/input/event1`, type-A group → slot-0
  logic, HVGA fallbacks, `/dev/uinput` autocreate, `cooper_vtouch`).

### Recovery (TWRP 2.7.1.0 cooper) interaction notes
- TWRP speaks updater API **3**; Donut's updater binary only knew 1/2
  ("error executing updater binary" until patched). Every zip needs a real
  ARM-static `update-binary` (Donut builds one: `make updater`).
- TWRP's own Edify differs from Donut's — but what runs is *our*
  update-binary, so write scripts strictly to Donut's arities (above).
- `openrecoveryscript` auto-run is unreliable here; sideload (Advanced →
  ADB Sideload → swipe) or direct shell flashing (`erase_image` /
  `flash_image`, `mount` + `unzip`, `make_ext4fs`) is deterministic.
- `/tmp` is tmpfs (vanishes on reboot); host `/tmp` gets cleaned by the OS
  too — keep artifacts (backups, images, logs) in the project tree.
- **Manual shell flashing vs symlinks:** TWRP's busybox `unzip` writes the
  zip's symlink entries as tiny **text files** (same FILES_ONLY behaviour as
  Donut's `package_extract_dir`) — including *over* good symlinks, and it
  can leave `/system/bin/toolbox` itself as a 7-byte text file. The updater
  path is immune (explicit `symlink()` commands rm + relink). When flashing
  by hand: extract to a temp dir, `cp -a` over `/system`, then recreate all
  51 links from the updater-script (`cd /system/bin; rm -f <name>; ln -s
  toolbox|dumpstate <name>`), then fix perms (dirs 0755; `/system/bin/*` +
  `/system/xbin/*` 0755; everything under `lib/`/`etc/`/`usr/`/`fonts/`/
  `media/` 0644; `/system/xbin/su` 06755). Verify with sizes + md5 against
  the chroot tree before rebooting.
- **Verify boot flashes by trimmed readback:** `bml8` is 8 MB but
  `boot.img` is ~3 MB, so a full-partition md5 will *never* match. Compare
  `dd if=/dev/block/bml8 bs=<boot.img bytes> count=1` against the image.

### Device realities (documented limitations of this port)
- **No cellular**: no Donut RIL lib exists for MSM7227 (rild removed).
- **Camera**: stub replaced by `devices/cooper/donut/libcamera`
  (CooperCamera: best-effort V4L2 `/dev/video0` YUYV capture, test-pattern
  fallback so CameraService never crashes, permissive setParameters, libjpeg
  snapshot encode; `USE_CAMERA_STUB=false`) + ported `toggleshutter` +
  ueventd `/dev/video0` nodes. Verified on-device: app opens, pattern
  previews, `registerBuffers`/`postBuffer` display path works (an earlier
  black preview was fallout from broken `/system` dir modes after a manual
  recovery install — fixed, see recovery notes). No live sensor feed: the
  S5K4ECGX sits on the MSM_CAMERA stack with `CONFIG_MSM_CAMERA_V4L2` off
  in this kernel (no `/dev/video*` nodes exist), and its proprietary
  `libseccamera` stack cannot link on Donut — real frames need a kernel
  sensor-driver project, not a HAL change.
- **No GPU acceleration**: no Adreno200 EGL in Donut (software `libagl`
  path); expect software rendering + no HW overlays.
- **WiFi (AR6003)**: two stacks tried. (1) Legacy `ar6000.ko` (CM7 build,
  vermagic `-perf-CL882825`): force-loads on the shipped `-androidarmv6+`
  kernel but PANICS at WiFi toggle (verified live, no last_kmsg) — keep
  WiFi OFF with it; kept in-tree only for a possible CM-kernel boot.
  (2) Mainline ath6kl + cfg80211/compat (OmniROM builds, vermagic
  `-androidarmv6+` = exact match): insmod-clean, probes the SDIO card and
  requests `ath6k/AR6003/hw2.0/bdata.*` (proven in recovery via dmesg);
  firmware lives at `/system/etc/firmware` (= `/etc/firmware`, where Donut
  init's firmware loader in `devices.c` looks — recovery has no firmware
  helper, so that half is only verifiable on Donut boot). Deps pre-insmod
  from `init.rc`; WifiService loads ath6kl (`WIFI_DRIVER_*` in BoardConfig);
  supplicant stays `-Dwext` through cfg80211's wext compat. BT (BCM),
  FM, GPS: need proprietary modules/libs not yet integrated.
- **Power button**: fixed in keymaps — `sec_key.kl`/`sec_jack.kl`/`qwerty.kl`
  mapped scancodes 107+116 to `ENDCALL WAKE_DROPPED` (Donut's
  PhoneWindowManager sleeps/wakes/dialogs on ENDCALL only; POWER is dead
  code on 1.6). Verified on-device: short-press sleeps, screen-off press
  wakes. (Live proof that 107 is the power scancode: `sec_key`'s KEY bitmap
  decodes to exactly 102/107/114/115, and EventHub loads `sec_key.kl`.)
- **HOME button / provisioning**: the center key (102 → HOME) arrived at
  WindowManager fine but never went home — silent swallow in
  `launchHomeFromHotKey`. Root cause: this GMS-less port never provisions
  the device (no SetupWizard is the only in-tree writer of
  `Settings.Secure device_provisioned`), so `KeyguardViewMediator.
  isInputRestricted()` stayed true forever and every HOME press was routed
  to an unlock verification that can never complete (traced live:
  `isShowing=false inputRestricted=true`). Fix, all permanent:
  `devices/cooper/donut/system.prop` sets `keyguard.no_require_sim=true`
  (CM parity), and `DatabaseHelper.loadSecureSettings` seeds
  `DEVICE_PROVISIONED=1` on fresh databases (`INSERT OR IGNORE`, safe on
  upgrade; see `patches/donut/frameworks-base.patch`). Verified on-device:
  HOME goes home, lock screen behaves. Diagnosed via a temporary
  `interceptKeyTq`/`launchHome` Log.d, reverted after (no trace code left
  in tree).
- **SD card**: pending the vold hardening above; boot without it for now.
- Practical: keep the battery genuinely charged (PC USB can't keep up
  under load; low-voltage boots produce phantom failures), and never flash
  boot below ~50%.

## Status
`aosp-donut/` (Donut r2 + `devices/cooper/donut` device port, incl. cooper
libaudio + 160 dpi) builds `full_cooper-eng` in `/srv/donut-hardy` (Hardy
chroot); `scripts/build-cooper-donut-full.py` packs
`out/full_cooper-*-FULL-*.zip` (CWM/TWRP-flashable, BML-aware).
**MILESTONE 2026-10-12: full boot to the Donut home screen on-device,
touchscreen + capacitive keys working**, display via software rendering at
stock 160 dpi, speaker audio working (ringtone preview audible). Still open:
adb shell over USB, SD/vold, cellular, WiFi/BT/GPS, camera HAL — see below.

## What got it booting (second half)

- **Zygote crash-loop**: `ZygoteInit.preloadClasses` throws
  `IllegalStateException: Missing class(es) for preloading` for the
  proprietary `com.google.common.*`/`com.google.masf.*` entries — but only
  when `persist.service.adb.enable=1` (i.e. every eng build!). 61 restarts
  observed, load ~5, zero dex progress. Fix: strip those entries from
  `frameworks/base/preloaded-classes` (kept the two resolvable protobuf
  ones). Diagnose via tombstones (`/data/tombstones/tombstone_*` pull
  fine over adb sync) + `/dev/log/main` pulls (logger works; parse with
  `strings`).
- **vold crash-loop**: SIGSEGV (NULL devpath) on foreign uevents, 10
  tombstones, plus an `EEXIST` mknod retry storm on the SD card that pinned
  a core and flooded logcat (469 KB). Fix for now: service removed (SD
  card stays out); proper fix is NULL-guards + EEXIST-as-success later.
- **rild crash-loop** (no proprietary RIL lib) waking get_macaddrs
  forever: service commented out.
- **First boot takes 10–20 min** on 800 MHz (dexopt of 32 APKs; watch
  `/data/dalvik-cache` grow + `packages.xml` appear). Don't mistake it for
  a hang; do check `loadavg` (sustained ~5 = working, idle = stuck).
- **Display**: SurfaceFlinger runs in-process (no separate service needed);
  Donut EGL falls back to software `libagl.so` (present) since `libhgl.so
  ` is absent — no `egl.cfg` surgery needed (Donut ignores it). Verified
  via `/dev/graphics/fb0` pulls (double-buffered 320x480x4; zeros = nothing
  drawing yet, thousands of colors = UI alive).
- **adb shell hangs but sync works**: Donut adbd's pty-based shell
  (`/dev/ptmx` + grantpt) is fragile here; replaced with split pipes
  (bionic of this era has **no `socketpair()`** — use two `pipe()`s and
  teach the poll loop the split fds). Shell still under investigation;
  meanwhile diagnose via `adb pull` (`/proc/*`, `/dev/log/*`,
  `/data/dalvik-cache`, tombstones) + on-device Terminal app.

## Touchscreen (working)
- Panel is Synaptics RMI4 over I2C (`SYNAPTICS_I2C_RMI4=y`, probes fine:
  `max_x: 320, max_y: 480`, irq mode). But it **streams protocol type-A
  (SYN_MT_REPORT groups, X/Y/pressure/width) while declaring no MT axes**
  (`ABS=650000`: HAT bits only) — so Donut classifies it keyboard-only and
  Donut (pre-multitouch) drops every touch event. Verified by pulling
  `/dev/input/event1` during taps and decoding MT_POSITION_X/Y (53/54).
-   Fix, with **zero framework changes**: ported MLXProjects' Froyo `vtouch`
  MT→ST uinput proxy (`devices/cooper/donut/vtouch/`, static ramdisk
  binary + `service vtouch /sbin/vtouch /dev/input/event1`). Cooper
  adaptations: explicit path forces MT mode (auto-detect can't see the
  caps), type-A group → slot-0 commit logic, HVGA fallbacks (driver reports
  max 320/480), `/dev/uinput` autocreate (c 10 223; kernel has
  `CONFIG_INPUT_UINPUT=y` but no node), renamed device `cooper_vtouch`.
  EventHub picks it up via inotify as `classes=0x5` (keyboard+touchscreen).
- Keymaps: `VIRTUAL` flag doesn't exist on Donut and **aborts the whole
  `.kl` file** → replaced with `WAKE` in `sec_touchscreen.kl`; new
  `cooper_vtouch.kl` maps MENU/BACK/HOME/SEARCH (BTN_TOUCH needs no entry:
  Donut consumes it by scancode, and it isn't a valid Donut keycode name).
  `.kcm.bin` absence is harmless (falls back; only matters for text input).
- Diagnose via `/dev/.vtouch.log` pull + EventHub `New device` lines in
  the main log buffer.
