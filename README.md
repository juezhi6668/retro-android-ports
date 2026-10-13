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

Device docs (hardware specifics, verified fixes, open gaps) live in
[`docs/devices/`](docs/devices/): [cooper](docs/devices/cooper.md).

## Repository layout

```
devices/<codename>/<release>/   device tree, drop-in for aosp vendor/ path
patches/<release>/              diffs this port makes to pristine AOSP
  files/                        new-file overlays (e.g. vendor/googlelogin)
docs/porting-notes.md           generic playbook: ancient Android on newer hardware
docs/devices/<codename>.md      per-device porting log
scripts/                        build + packaging + device-tree sync
tools/                          mkbootimg-cooper (patched Donut mkbootimg)
reference/                      third-party trees/images kept for board params
```

Not in git (gitignored): `aosp-donut/` (11 GB repo checkout), `out/`
(flashable zip outputs), `work/` (packaging scratch).

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

## Roadmap

- [x] Donut 1.6 boot on `cooper` with touchscreen + speaker audio
- [ ] Finish cooper: USB adb shell, SD/vold, WiFi (ath6kl mid-bringup),
      camera sensor feed (needs kernel driver work)
- [ ] Donut on a second device family (playbook extraction continues)
- [ ] Other ancient releases on newer hardware — Eclair/Froyo next;
      Cupcake is impossible (source gone), documented learning still
      applies to custom 1.5 builds if a tree surfaces
- [ ] Generalize the device layout/scripts for `devices/<codename>/`
      as new ports land

## Licensing & credits

- AOSP code is Apache 2.0; patches/overlays here are derivative and carry
  the same terms.
- `reference/android_device_samsung_cooper` — LineageOS/CM
  (`gb-release-7.2`), Apache 2.0, kept for board parameters.
- Vendor blobs (Samsung kernel/modules/firmware, Broadcom Atheros firmware)
  are retained for personal device servicing only; keep this repo private
  and do not redistribute.
