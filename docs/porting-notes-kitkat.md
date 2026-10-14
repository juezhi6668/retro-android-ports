# Porting notes: KitKat 4.4.4 on newer hardware

Playbook for bringing AOSP KitKat (`android-4.4.4_r2`, KTU84Q, API 19) up
on post-2014 hardware. Started for the Sony Xperia XZ Premium (`maple`,
2017, MSM8998); device specifics live in
[`docs/devices/maple.md`](devices/maple.md). The Donut playbook
([`docs/porting-notes.md`](porting-notes.md)) is the method reference —
this doc is the KitKat delta. **Everything below the "Field lessons" header is boot-verified on maple
(Oct 2026); treat the sections above it as the original checklist.**

## Field lessons (maple, verified on-device)

- **Sparse images bite twice.** `system.img` builds SPARSE by default;
  `dd` it raw and the partition is unmountable garbage (no error anywhere
  — init just reports missing `/system/bin/*`). Either `fastboot flash`
  (unsparsifies) or `simg2img` first, then verify ext4 magic (`53 ef` at
  byte 1080) on the flashed partition. Set `TARGET_USERIMAGES_USE_EXT4`
  or KitKat builds yaffs2 (unflashable on GPT devices).
- **A failing init command aborts its action SILENTLY** (`do_mount`
  returns -1 with no kmsg). Split mounts into independent `on init`
  stanzas so one bad mount can't kill the rest. Mount syntax is
  space-separated flags + ONE trailing options arg
  (`mount ext4 SRC /data nosuid nodev wait noauto_da_alloc,barrier=0`);
  comma-joined flags are silently ignored and `wait` never engages.
- **`wait_for_coldboot_done` before mounts** on uevent-heavy SoCs, and
  prefer raw `/dev/block/sdXN` nodes (from a TWRP by-name dump) over
  by-name symlinks to remove timing variables.
- **Double imports are (mostly) harmless** (`import /init.${ro.hardware}.rc`
  + explicit import of the same file): duplicate services are ignored
  with a log line, duplicate actions run twice.
- **`write /dev/kmsg` from init is unobservable** on kernels without
  matching policy/context (no error). Diagnose with file outputs
  (`dmesg > /data/...`, per-pid `/proc` snapshots) pulled via TWRP, not
  kmsg markers. Related: the 4.4 kernel has no `CONFIG_LOGGER`, so
  logcat is blind and every ALOG failure is invisible — including the
  errno fallout that once sent us chasing a phantom EBADF.
- **First-boot services to disable** (cooper pattern, all verified
  needed here): `vold` (parks in decrypt-wait on foreign userdata and
  `on boot` never fires), `rild` (no RIL lib → crash-loop), `healthd`
  (binder failures → `critical` → forced recovery reboot, hiding later
  stages). Re-enable each with its real fix.
- **FBE userdata must be reformatted** (Lineage file-based encryption is
  unreadable to KitKat; `mke2fs -t ext4` on the userdata partition from
  TWRP — destroys the modern install, get approval).
- **Binder on a 64-bit-only kernel is a userspace port**, not a config
  tweak: 64-bit-clean UAPI header (`binder_uintptr_t`/`binder_size_t`
  as `__u64`, protocol v8), `Parcel` 8-byte offsets + `hdr.type` +
  pointer casts, `IPCThreadState` int64 read/write + `sizeof(uint64_t)`
  offsets math, servicemanager local structs to 64-bit layout. Symptoms
  of getting it wrong: `ioctl 0xc0186201 → EINVAL` (number not
  recognised), `sme_*`/p2p/driver_cmd undefined refs at wpa_supplicant
  link (needs the qcwcn private lib), silent transaction misparse.
  "Junk in the top half of 64-bit unions breaks lookups" — assign every
  field, leave no padding uninitialised.
- **EGL needs a driver it isn't allowed to skip**: KitKat's loader skips
  `libGLES_android.so`, aborting surfaceflinger in `eglGetDisplay` when
  no HW driver exists. Patch the skip out for software-rendered
  bringup (revisit if a real driver lands).
- **Toolbox lies on foreign kernels**: `sort` SEGVs, `ps` prints nothing,
  `head`/`tail` output vanishes, `dmesg | tail` SIGPIPEs (spamming
  debuggerd tombstones — write whole files instead). Shell builtins +
  `cat`/`grep`/`echo` are trustworthy. Validate every diag tool where
  it runs before believing its output.
- **TWRP shell realities**: `/sdcard` may refuse creates while
  `/data/media` works; `adb shell` stdin pipes fail (`Bad address`);
  `adb push` of 5 GB images works fine; `by-name` symlinks resolve to
  stable `sdXN` nodes; `console-ramoops` survives reboot for kernel logs
  (corrupts after hours of uptime); `/proc/config.gz` answers kernel
  questions outright.
- **CPU variant**: `cortex-a15` is the right KitKat-era choice for Kryo
  (HW atomics). A kuser-helpers scare was disproven live — on-boot
  32-bit processes run; the TWRP-side SEGV was environmental (missing
  `/system/bin/linker` in recovery ramdisk breaks PIE resolution).
- **mkbootimg needed NO backport** for base-0 + `Image.gz-dtb` (verified
  geometry: base 0+0x8000, pagesize 4096). The predicted patch wasn't
  needed — verify fears before building them.

## Container gotchas (verified)

- Trusty image needs `lib32stdc++6 lib32z1` (KitKat's 32-bit prebuilt
  host tools die without them), a `python` → `python2.7` symlink, and
  the JDK-vendor-check patch (Oracle JDK 6 is uninstallable;
  `patches/kitkat/build-jdk-openjdk6.patch`).
- `repo sync` MUST be `repo init --depth=1` + `repo sync -c --no-tags`
  or it fetches all of AOSP history (100+ GB and still growing).
- `TARGET_NO_KERNEL := true` so the build never tries to make boot.img
  itself (boot.img is hand-packed from the dumped kernel + built ramdisk).

## Source & toolchain (vs Donut)

- **Branch:** `repo init -u https://android.googlesource.com/platform/manifest
  -b android-4.4.4_r2` → `aosp-kitkat/` (11 GB+; gitignored). Confirm the
  exact `_r` rev against the build-numbers table before locking scripts.
- **64-bit host required** (Gingerbread+ rule; Donut's i386 chroot does NOT
  apply). Recipe to build: Ubuntu **14.04 (Trusty)** era container/chroot +
  **JDK 6** + Make 3.81 + Python 2.7. KitKat carries prebuilt OpenJDK bits
  in `prebuilts/jdk/` for some steps, but a separate JDK 6 install is still
  the documented path for 2.3–4.4. (Donut: Hardy + JDK 5; KitKat: Trusty +
  JDK 6.)
- **No ARM64 in KitKat.** `TARGET_ARCH := arm64` + `TARGET_2ND_ARCH` are a
  Lollipop-5.0 invention. MSM8998 bringup starts 32-bit (`armeabi-v7a`).
  See `docs/devices/maple.md` blocker 1 — spike this before anything else.
- Host GCC: Trusty-era GCC 4.6/4.8 is the safe baseline (same lesson as
  Donut's GCC-16-vs-`apriori` failure, one generation newer).

## KitKat build-system deltas (vs Donut gotchas)

- `lunch full_maple-eng` product discovery is the same shape
  (`AndroidProducts.mk` → `full_maple.mk`), but KitKat product files use the
  newer `inherit-product` + `PRODUCT_COPY_FILES` split — audit against
  `build/target/product/generic.mk` in the synced tree, not from memory.
- KitKat **has** `run_program`, ext4 `format()`, 3+-arg `mount`, and
  `write_raw_image` semantics Donut lacked — the cooper updater backports
  do NOT carry over. Re-derive the updater-script from KitKat's
  `bootable/recovery/updater` + TWRP 3.x expectations (API 19-era).
- `TARGET_PROVIDES_INIT_RC` pattern still applies; KitKat init language is
  richer than Donut's (more triggers/flags) — re-audit every cooper-era
  assumption (`import`, `wait`, `exec` semantics all changed since 1.6).
- `preloaded-classes` / provisioning / keymap lessons from cooper carry
  over in spirit but must be re-measured: KitKat's Zygote preload list,
  `Settings.Secure`, and `.kl` flag set all differ from Donut's.
- mkbootimg: KitKat predates `Image.gz-dtb`; expect a backport patch in
  `patches/kitkat/` (precedent: `patches/donut/mkbootimg-pagesize.patch`).
- SELinux is enforcing-era in KitKat (it was absent/permissive in Donut).
  First bringup likely needs permissive + policy audit against the 4.4
  kernel's security hooks.

## KitKat userspace on a 4.4 kernel (vs Donut on 2.6.35)

- Same problem class as cooper (new kernel, ancient userspace), one
  generation up: audit ueventd coldboot/node creation, cgroups, netd/vold
  against 4.4 uevents, firmware-loader paths, and `lowmemorykiller` tuning.
- vold/adbd/rild crash-loop triage from cooper applies: guard NULL
  uevent fields, split-pipe adbd diagnosis if pty misbehaves, comment out
  `ril-daemon` until an RIL lib exists for the SoC.

## Container gotchas (to fill in once built)

- (Empty — log Trusty-apt/JDK6/debootstrap issues here as they are hit,
  the way `docs/porting-notes.md` logs Hardy ones.)
