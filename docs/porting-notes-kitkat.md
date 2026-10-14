# Porting notes: KitKat 4.4.4 on newer hardware

Playbook for bringing AOSP KitKat (`android-4.4.4_r2`, KTU84Q, API 19) up
on post-2014 hardware. Started for the Sony Xperia XZ Premium (`maple`,
2017, MSM8998); device specifics live in
[`docs/devices/maple.md`](devices/maple.md). The Donut playbook
([`docs/porting-notes.md`](porting-notes.md)) is the method reference —
this doc is the KitKat delta. **Everything below is pre-bringup (skeleton
stage); treat each section as a checklist, not a verified recipe.**

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
