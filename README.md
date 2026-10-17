# Retro Android Ports

Running ancient Android releases on hardware that postdates them. The
project ports AOSP from **Donut 1.6 through Nougat 7.1** to many different
devices that never officially ran them — and distils every hard-won lesson
into reusable playbooks so the next device is easier than the last.

Verified so far:

- **Android 1.6 Donut** (AOSP `android-1.6_r2`, DRD35) on the **Samsung
  Galaxy Ace GT-S5830** (`cooper`, 2011, MSM7227) — boots with touchscreen
  (incl. capacitive keys), display at the stock 160 dpi, speaker audio.
  Flashable CWM/TWRP zips are in [Releases](https://github.com/pukikiko/retro-android-ports/releases).
- **Android 4.4.4 KitKat** (AOSP `android-4.4.4_r2`, KTU84Q) on the **Sony
  Xperia XZ Premium** (`maple`, 2017, MSM8998) — boots to the launcher with
  touch, USB adb, and hardware GL/GLES 3.2 on the Adreno 540 (via
  [glrenderd](docs/advanced/software/glrenderd.md)).

Cupcake 1.5 is out of scope: `platform/manifest` no longer carries a
`cupcake`/`android-1.5` branch (`android-1.6_r1` is the oldest), so Donut
is the oldest release that can still be synced. The full story — and why a
2011 phone running Froyo/Gingerbread is a true retro target — is in the
[Donut porting notes](docs/advanced/porting-notes/donut.md).

## Supported devices

| Device | Codename | Ports | Status |
| --- | --- | --- | --- |
| Samsung Galaxy Ace GT-S5830/L/T | `cooper` | Donut 1.6 (DRD35) | Boots on-device; launcher, touchscreen + capacitive keys, 160 dpi display, speaker audio. Cellular impossible (no Donut RIL for MSM7227); camera preview is test-pattern only. [Device page](docs/devices/cooper/README.md) |
| Sony Xperia XZ Premium G8141/G8142 | `maple` | KitKat 4.4.4 (KTU84Q) | Boots to UI; touch, display, USB adb, hardware GL/GLES 3.2 (Adreno 540), brightness/sleep, emulated storage, **audio playback (tasha card)**. WiFi interface still open: SERVREG_LOC now served in-tree by `pdserve` and the PDR chain (icnss) registers its notifier, but the modem root PD crashes every ~41 s before wlan_pd/WLFW can start (modem stability/keepalive open). [Device page](docs/devices/maple/README.md) |
| Sony Xperia XZ1 G8341/G8342 | `poplar` | KitKat 4.4.4 (KTU84Q) | Boots to UI (launcher + nav bar, touch, display, USB adb, HW GL via glrenderd). Same yoshino/MSM8998 platform as maple; tree predates maple TEST 36-37. [Device page](docs/devices/poplar/README.md) |

Each device has a main page (description, per-release status, links) and a
**quirks** page (everything needed to port to that device — flash layout,
verified fixes, open gaps): [cooper](docs/devices/cooper/quirks.md),
[maple](docs/devices/maple/quirks.md), [poplar](docs/devices/poplar/quirks.md).

Release zips: [GitHub Releases](https://github.com/pukikiko/retro-android-ports/releases)
(cooper assets are named `full_cooper-*`; maple images are built locally —
see the [KitKat build guide](docs/advanced/build/kitkat.md)).

## Advanced documentation

The [advanced documentation index](docs/advanced/README.md) collects
everything beyond the device pages:

- **Porting notes per release** — [Donut 1.6](docs/advanced/porting-notes/donut.md),
  [KitKat 4.4.4](docs/advanced/porting-notes/kitkat.md): toolchain, build-system
  surprises, init/Edify/ABI differences and field lessons.
- **Build guides per release** — [Donut for cooper](docs/advanced/build/donut.md),
  [KitKat for maple](docs/advanced/build/kitkat.md), plus the
  [build-server runbook](docs/advanced/build-server.md).
- **Project software** — [vtouch](docs/advanced/software/vtouch.md)
  (multi-touch → single-touch proxy for pre-multitouch Android),
  [glrenderd](docs/advanced/software/glrenderd.md) (a newer GPU driver
  under an old Android, in a chroot), [mkbootimg-cooper](docs/advanced/software/mkbootimg-cooper.md)
  (pagesize-patched Donut mkbootimg).

## Repository layout

```
devices/<codename>/<release>/      device tree, drop-in for the AOSP vendor/ path
patches/<release>/                 diffs this port makes to pristine AOSP (+ files/ overlays)
docs/advanced/README.md            advanced documentation index
docs/advanced/porting-notes/       per-release playbooks (donut.md, kitkat.md)
docs/advanced/build/               per-release build guides (donut.md, kitkat.md)
docs/advanced/build-server.md      remote build-host runbook
docs/advanced/software/            project-made software (vtouch, glrenderd, mkbootimg-cooper)
docs/devices/<codename>/README.md  device main page: description, status, links
docs/devices/<codename>/quirks.md  device quirks: everything needed to port to it
scripts/                           build + packaging + device-tree sync (+ dump-maple-stock.sh, patch-maple-dtb.py)
containers/<release>/              period build-container recipes (kitkat/)
tools/                             project-made tools (mkbootimg-cooper, glrenderd sources)
reference/                         third-party trees/images kept for board parameters
```

Not in git (gitignored): `aosp-donut/`, `aosp-kitkat/` (re-syncable
checkouts), `out/` (flashable zip outputs), `work/`, `dumps/` (packaging
and device-dump scratch).

## Building

Each release has its own end-to-end guide; the README stays short:

- [Building Donut 1.6 for cooper](docs/advanced/build/donut.md) — Hardy
  i386 chroot + JDK 5 + GCC 4.2.3; packages
  `out/full_cooper-1.6.DRD35-UNOFFICIAL-FULL-<date>.zip`.
- [Building KitKat 4.4.4 for maple](docs/advanced/build/kitkat.md) —
  Trusty + JDK 6 container, TWRP-dumped 4.4 kernel, glrenderd; flash via
  TWRP `dd`.
- Doing it on a remote host: [build server runbook](docs/advanced/build-server.md).

## Roadmap

- [x] Donut 1.6 on `cooper`: boot to the home screen with touchscreen +
      speaker audio (2026-10-12). Release zips shipped.
- [ ] Finish cooper: USB adb shell, SD/vold, WiFi (ath6kl mid-bringup),
      camera sensor feed (needs a kernel sensor driver, not a HAL change).
- [x] KitKat 4.4.4 on `maple`: boots to the launcher with touch, display,
      USB adb (Oct 2026); hardware GL/GLES on the Adreno 540; panel
      presentation; XZ1/poplar reliability + GL perf sets (TESTs 30–35);
      fresh-image GL boot + daemon crash/restart self-heal (TEST 41);
      lights HAL; wifi feature/supplicant plumbing (TEST 36).
- [ ] Finish maple: WiFi interface (SERVREG_LOC now served in-tree by
      `pdserve` and the PDR chain registers its notifier; blocked on the
      modem root PD crashing every ~41 s — modem stability/keepalive open),
      cellular, camera, GPS, microSD; intermittent pre-userspace boot hang
      documented.
- [x] maple audio: playback works on the msm8998-tasha card (audio HAL
      `stop_threshold = INT_MAX`), stutter polish deferred.
- [x] Generalize the layout/scripts for `devices/<codename>/<release>/`
      (cooper/donut + maple/kitkat share one sync script).
- [x] KitKat 4.4.4 on `poplar` (sibling yoshino device): boots to launcher +
      nav bar with touch, display, USB adb, HW GL via glrenderd (Oct 2026,
      sherm port — tree committed as `devices/poplar/kitkat/`, predates maple
      TEST 36-37).
- [ ] Finish poplar: add `poplar` to `scripts/sync-device-tree.sh` (done in-tree
      — verify), forward-port maple TEST 36-37 (wifi supplicant services, adsp
      boot, dual-node lights, `service_locator.enable=0`), formalise GPU fixes
      into `patches/kitkat/`.
- [ ] Other releases on newer hardware — Eclair/Froyo next (Gingerbread
      through Nougat planned; Cupcake is impossible, source gone).

Device pages and their quirks logs carry the full detail and evidence:
[cooper](docs/devices/cooper/README.md),
[maple](docs/devices/maple/README.md),
[poplar](docs/devices/poplar/README.md).

## Licensing & credits

- AOSP code is Apache 2.0; patches/overlays here are derivative and carry
  the same terms.
- `reference/android_device_samsung_cooper` — LineageOS/CM
  (`gb-release-7.2`), Apache 2.0, kept for board parameters.
- `reference/poplar-kitkat-gpu-fixes-2026-10-16.zip` — the complete KitKat
  GPU/display fix bundle from a sibling XZ1 (`poplar`) port (full writeup,
  patched sources, reference binaries), kept for maple's glrenderd work.
- Vendor blobs (Samsung kernel/modules/firmware, Broadcom Atheros wifi
  firmware; reference binaries inside the poplar bundle) are retained to
  keep the ports buildable and remain under their vendors' original
  licenses — they are **not** covered by the Apache 2.0 terms above.
- `android_device_sony_maple` (sony-msm8998, `ten`) + `yoshino-common`
  (`BoardConfigPlatform.mk`, msm8998) — reference only, Apache 2.0; board
  params distilled into `devices/maple/kitkat/`, no blobs committed.
- `android_device_sony_poplar` (sony-msm8998, `ten`) — same, distilled into
  `devices/poplar/kitkat/` (sherm port).
- Device photos on the device pages come from Wikimedia Commons under
  CC BY-SA (credit on each page).
- `vtouch` is ported from MLXProjects' Froyo `vtouch` 1.2.0; `glrenderd`
  is ported from [chardidathing/pdx203-jellybean](https://github.com/chardidathing/pdx203-jellybean).
