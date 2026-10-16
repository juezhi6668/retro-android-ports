# Retro Android Ports

Running ancient Android on hardware that postdates it — with the goal of
(a) porting **Donut 1.6** to more devices than the one it started on, and
(b) distilling every hard-won lesson into a reusable playbook for bringing
other ancient releases to newer devices.

Verified so far: **Android 1.6 Donut (AOSP `android-1.6_r2`, DRD35)** builds
in a period-correct Ubuntu 8.04 / JDK 5 chroot and **boots on the Samsung
Galaxy Ace GT-S5830 (`cooper`, 2011, MSM7227)** — launcher, touchscreen
(incl. capacitive keys), display at the stock 160 dpi, speaker audio,
camera preview. Flashable CWM/TWRP zips are in
[Releases](https://github.com/pukikiko/retro-android-ports/releases).

## Why Donut and not Cupcake?

Cupcake cannot be synced any more: `platform/manifest` on
android.googlesource.com has no `cupcake` / `android-1.5` branch left (only
`android-1.6_r1…` and later), so `repo init -b cupcake` is impossible.
Donut is the oldest AOSP you can still check out. The S5830 (2011,
Froyo/Gingerbread stock) postdates it by years; Donut's `armeabi`/`armv5te`
runs on the ARMv6 MSM7227 and HVGA matches Donut-era devices, making this a
true retro-port (candidate for more 2010–2012 hardware).

## Ports

| Device | Codename | Release | Status |
| --- | --- | --- | --- |
| Samsung Galaxy Ace GT-S5830/L/T | `cooper` | Donut 1.6 (DRD35) | Boots on-device, daily-driving basics OK (see device doc) |
| Sony Xperia XZ Premium G8141/G8142 | `maple` | KitKat 4.4.4 (KTU84Q) | Boots to UI: launcher, touch, display, USB adb (see device doc) |
| Sony Xperia XZ1 G8341/G8342/G8343 (JP: SO-01K/SOV36) | `poplar` | KitKat 4.4.4 (KTU84Q) | Boots to UI: launcher + nav bar, touch, display, USB adb, HW GL via `glrenderd` (see device doc) |

Device docs (hardware specifics, verified fixes, open gaps) live in
[`docs/devices/`](docs/devices/): [cooper](docs/devices/cooper.md),
[maple](docs/devices/maple.md), [poplar](docs/devices/poplar.md). `maple` and
`poplar` are the same yoshino/MSM8998 platform — poplar is where the display and
GPU work originated, so read the two together. KitKat playbook:
[`docs/porting-notes-kitkat.md`](docs/porting-notes-kitkat.md); GPU fixes:
[`docs/kitkat-gpu-fixes.md`](docs/kitkat-gpu-fixes.md).

## Repository layout

```
devices/<codename>/<release>/   device tree, drop-in for aosp vendor/ path
patches/<release>/              diffs this port makes to pristine AOSP
  files/                        new-file overlays (e.g. vendor/googlelogin)
docs/porting-notes.md           generic playbook: ancient Android on newer hardware (Donut era)
docs/porting-notes-kitkat.md    KitKat-era playbook delta (4.4.4 on newer hardware)
docs/devices/<codename>.md      per-device porting log
scripts/                        build + packaging + device-tree sync (+ dump-maple-stock.sh)
containers/kitkat/              KitKat build-container recipe (Trusty + JDK 6, untested)
tools/                          mkbootimg-cooper (patched Donut mkbootimg)
reference/                      third-party trees/images kept for board params
```

Not in git (gitignored): `aosp-donut/`, `aosp-kitkat/` (re-syncable
checkouts), `out/` (flashable zip outputs), `work/` (packaging scratch).

## Building the cooper Donut port

1. **AOSP tree**: `repo init -u https://android.googlesource.com/platform/manifest -b android-1.6_r2`
   then `repo sync` (→ `aosp-donut/`, or point `AOSP_DIR` elsewhere).
2. **Pristine → ported**: apply [`patches/donut/`](patches/donut/README.md)
   and copy the `files/` overlay, then sync the device tree:
   `scripts/sync-device-tree.sh cooper donut`.
3. **Period chroot** (`/srv/donut-hardy`): Ubuntu 8.04 i386 + Sun JDK
   1.5.0_15 + GCC 4.2.3 + Make 3.81 + Python 2.5; bind-mount the AOSP tree,
   `lunch full_cooper-eng && make -j8`. All the gotchas (apt over modern
   Apache, `secure_path` vs `bzip2`, zygote preload crash-loop, stale
   `BoardConfig.mk` flags, …) are in
   [`docs/porting-notes.md`](docs/porting-notes.md).
4. **Package**: `scripts/build-cooper-donut-full.py` →
   `out/full_cooper-1.6.DRD35-UNOFFICIAL-FULL-<date>.zip` (BML-aware CWM/
   TWRP zip: cooper geometry boot.img, Donut updater with API-3 + raw-block
   + `run_program` shims, 51 toolbox symlinks as real symlinks).
   `scripts/build-cooper-donut-skeleton.py` builds the small preview zip.

## Building the maple KitKat port (boots to UI — see device doc)

1. **AOSP tree**: `repo init -u https://android.googlesource.com/platform/manifest -b android-4.4.4_r2`
   then `repo sync` (→ `aosp-kitkat/`, or point `AOSP_DIR` elsewhere;
   use a shallow `repo sync -c --no-tags`, full history is 100+ GB).
2. **Ported tree**: `scripts/sync-device-tree.sh maple kitkat`
   (→ `$AOSP_DIR/vendor/sony/maple`), then apply `patches/kitkat/`
   (see [`patches/kitkat/README.md`](patches/kitkat/README.md) — binder
   64-bit UAPI, EGL/glrenderd, gralloc stride, wallpaper fallback, JDK 6).
   The kernel (`vendor/sony/maple/kernel`) is a TWRP-dumped blob, kept
   out of git.
3. **Period container**: Ubuntu 14.04 + JDK 6 (`containers/kitkat/`),
   `lunch full_maple-eng && make -j$(nproc)`. Flash `boot.img` +
   raw-ext4 `system.img` (via `simg2img`) over TWRP `dd`, hashes
   verified by readback. Current state (Oct 2026): launcher, touch,
   display, USB adb working; WiFi/BT/RIL/camera/GPS still stub —
   see [`docs/devices/maple.md`](docs/devices/maple.md).

## Building the poplar KitKat port (boots to UI + HW GL — see device doc)

Same container, AOSP tree and kernel handling as maple. Deltas: a 1080x1920
command-mode panel (not 4K) and a different block map (`boot` `sda38`, `system`
`sda65`, `userdata` `sda66`, `cache` `sda53`, `persist` `sda33`). `lunch
full_poplar-eng`. poplar is where the `lights` HAL, the one-shot `displaytune.sh`
sleep fix and the `glrenderd` GPU fixes
([`docs/kitkat-gpu-fixes.md`](docs/kitkat-gpu-fixes.md)) were developed, then
carried to maple. The device tree and GPU patches are not committed yet — see
[`docs/devices/poplar.md`](docs/devices/poplar.md).

## Roadmap

- [x] Donut 1.6 boot on `cooper` with touchscreen + speaker audio
- [ ] Finish cooper: USB adb shell, SD/vold, WiFi (ath6kl mid-bringup),
      camera sensor feed (needs kernel driver work)
- [x] Generalize the device layout/scripts for `devices/<codename>/`
      (`cooper/donut` + `maple/kitkat` skeletons share one sync script)
- [x] KitKat 4.4.4 on `maple`: boots to launcher with touch, display,
      USB adb (Oct 2026 — see `docs/devices/maple.md`)
- [x] KitKat 4.4.4 on `poplar`: boots to launcher + nav bar with touch, display,
      USB adb and Adreno 540 GL via `glrenderd` (Oct 2026 — see
      `docs/devices/poplar.md`)
- [ ] Finish poplar: commit `devices/poplar/kitkat/`, add `poplar` to
      `scripts/sync-device-tree.sh`, and formalise the GPU fixes into
      `patches/kitkat/`
- [ ] Finish maple: WiFi/BT/RIL/camera/GPS HALs, glrenderd HW compositing,
      microsd, audio
- [ ] Other ancient releases on newer hardware — Eclair/Froyo next;
      Cupcake is impossible (source gone), documented learning still
      applies to custom 1.5 builds if a tree surfaces

## Licensing & credits

- AOSP code is Apache 2.0; patches/overlays here are derivative and carry
  the same terms.
- `reference/android_device_samsung_cooper` — LineageOS/CM
  (`gb-release-7.2`), Apache 2.0, kept for board parameters.
- Vendor blobs (Samsung kernel/modules/firmware, Broadcom Atheros firmware)
  are retained for personal device servicing only; keep this repo private
  and do not redistribute.
- `android_device_sony_maple` + `android_device_sony_poplar` (sony-msm8998,
  `ten`) + `yoshino-common` (`BoardConfigPlatform.mk`, msm8998) — reference only,
  Apache 2.0; board params distilled into `devices/maple/kitkat/` (and poplar's
  tree once committed), no blobs committed.
