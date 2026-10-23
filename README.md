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
  touch, USB adb, and hardware GL/GLES 3.2 on the Adreno 540 (first via the
  glrenderd chroot, then the in-process
  [libhyb](docs/advanced/software/libhyb.md) stack — built 2026-10-18,
  flashed and verified on hardware 2026-10-19, 59.5–60.0 fps).

Cupcake 1.5 is out of scope: `platform/manifest` no longer carries a
`cupcake`/`android-1.5` branch (`android-1.6_r1` is the oldest), so Donut
is the oldest release that can still be synced. The full story — and why a
2011 phone running Froyo/Gingerbread is a true retro target — is in the
[Donut porting notes](docs/advanced/porting-notes/donut.md).

## Supported devices

| Device | Codename | Ports | Status |
| --- | --- | --- | --- |
| Samsung Galaxy Ace GT-S5830/L/T | `cooper` | Donut 1.6 (DRD35) | Boots on-device; launcher, touchscreen + capacitive keys, 160 dpi display, speaker audio. Cellular impossible (no Donut RIL for MSM7227); camera preview is test-pattern only. [Device page](docs/devices/cooper/README.md) |
| Sony Xperia XZ Premium G8141/G8142 | `maple` | KitKat 4.4.4 (KTU84Q) | Boots to UI; touch, display, USB adb, hardware GL/GLES 3.2 (Adreno 540 in-process via libhyb, 59–60 fps, verified 2026-10-19), brightness/sleep, emulated storage, **playback + capture + headset/headphone routing through the audio HAL** (ACDB calibration from the Android 15 loader, in-process via libhyb; routing driven by AudioPolicyManager's `routing=`; verified 2026-10-19; decoded ogg/mp3 too — TEST 52, a DMIC loopback of a 440 Hz mp3 reads 440.00 Hz; video plays as well: software h264 via Gallery, test pattern rendered full-screen + 880 Hz track looped back). WiFi interface still open: SERVREG_LOC now served in-tree by `pdserve` and the PDR chain (icnss) registers its notifier, but the modem root PD crashes every ~41 s before wlan_pd/WLFW can start (modem stability/keepalive open). [Device page](docs/devices/maple/README.md) |
| Sony Xperia XZ Premium G8141/G8142 | `maple` | Donut 1.6 (DRD35) | Boots to launcher; touch (stable from ~6.5 s, `vtouch` v1.2.0-maple3), tap-to-unlock keyguard, camera half-press → BACK / full-press → MENU, volume keys, readable key character maps (Settings works), portrait orientation. No audio/WiFi/cellular/camera HAL yet. [Device page](docs/devices/maple/README.md) |
| Sony Xperia XZ1 G8341/G8342 | `poplar` | KitKat 4.4.4 (KTU84Q) | Boots to UI (launcher + nav bar, touch, display, USB adb, HW GL via glrenderd). Same yoshino/MSM8998 platform as maple; tree predates maple TEST 36-37. [Device page](docs/devices/poplar/README.md) |
| Xiaomi Mi 6 | `sagit` | KitKat 4.4.4 (KTU84Q) | **Skeleton, not yet built** — device tree derived from maple (same MSM8998/Adreno 540), with Mi-6 deltas: no microSD, Synaptics DSX touch, tasha-only audio, no vendor partition; every hardware fact is a `VERIFY-ME` resolved by `scripts/dump-sagit-stock.sh`. [Device page](docs/devices/sagit/README.md) |

Each device has a main page (description, per-release status, links) and a
**quirks** page (everything needed to port to that device — flash layout,
verified fixes, open gaps): [cooper](docs/devices/cooper/quirks.md),
[maple](docs/devices/maple/quirks.md), [poplar](docs/devices/poplar/quirks.md),
[sagit](docs/devices/sagit/quirks.md).

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
  [libhyb](docs/advanced/software/libhyb.md) (a newer GPU driver running
  inside old-Android processes, Adreno 540 on KitKat),
  [mkbootimg-cooper](docs/advanced/software/mkbootimg-cooper.md)
  (pagesize-patched Donut mkbootimg).

## Repository layout

```
devices/<codename>/<release>/      device tree, drop-in for the AOSP vendor/ path
patches/<release>/                 diffs this port makes to pristine AOSP (+ files/ overlays)
docs/advanced/README.md            advanced documentation index
docs/advanced/porting-notes/       per-release playbooks (donut.md, kitkat.md)
docs/advanced/build/               per-release build guides (donut.md, kitkat.md)
docs/advanced/build-server.md      remote build-host runbook
docs/advanced/software/            project-made software (vtouch, libhyb, mkbootimg-cooper)
docs/devices/<codename>/README.md  device main page: description, status, links
docs/devices/<codename>/quirks.md  device quirks: everything needed to port to it
scripts/                           build + packaging + device-tree sync (+ dump/patch scripts per device)
containers/<release>/              period build-container recipes (kitkat/)
tools/                             project-made tools (mkbootimg-cooper, libhyb staging/closure)
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
  Trusty + JDK 6 container, TWRP-dumped 4.4 kernel, libhyb closure
  staging; flash via TWRP `dd`.
- Doing it on a remote host: [build server runbook](docs/advanced/build-server.md).

## Roadmap

- [x] Donut 1.6 on `cooper`: boot to the home screen with touchscreen +
      speaker audio (2026-10-12). Release zips shipped.
- [x] Donut 1.6 on `maple`: boots to the launcher with touch (stable from
      ~6.5 s after boot, `vtouch` v1.2.0-maple3 sets the firmware-class
      timeout to 1 s), tap-to-unlock keyguard, camera half-press -> BACK /
      full-press -> MENU, Settings/key character maps (0644) fixed, volume
      keys, portrait orientation (flip-cover lid sensor no longer forces
      landscape). Device tree at `devices/maple/donut/`; quirks in
      `docs/devices/maple/quirks.md` (TEST 43-48).
- [ ] Finish cooper: USB adb shell, SD/vold, WiFi (ath6kl mid-bringup),
      camera sensor feed (needs a kernel sensor driver, not a HAL change).
- [x] KitKat 4.4.4 on `maple`: boots to the launcher with touch, display,
      USB adb (Oct 2026); hardware GL/GLES on the Adreno 540; panel
      presentation; XZ1/poplar reliability + GL perf sets (TESTs 30–35);
      fresh-image GL boot + daemon crash/restart self-heal (TEST 41);
      lights HAL; wifi feature/supplicant plumbing (TEST 36).
- [x] maple graphics, second design: glrenderd (chroot + GL socket) replaced
      by [libhyb](docs/advanced/software/libhyb.md) — the Adreno driver runs
      in-process, `fbpresentd` presents through an MDSS atomic commit, the
      glrenderd tooling/patches are deleted. Built 2026-10-18; flashed and
      **verified on hardware 2026-10-19** (cold boots with md5 readbacks:
      EGL 1.5 on the Adreno 540, 59.5–60.0 fps, scanout fences signal,
      copy-path A/B clean — the checklist lives in the libhyb write-up).
- [ ] Finish maple: WiFi interface (SERVREG_LOC now served in-tree by
      `pdserve` and the PDR chain registers its notifier; blocked on the
      modem root PD crashing every ~41 s — modem stability/keepalive open,
      the missing userspace half is `cnss-daemon`/WLFW, mapped
      per-subsystem in the
      [Taimen daemon-chroot assessment](docs/advanced/taimen-kitkat-mapping.md)),
      cellular, camera, GPS, microSD; intermittent pre-userspace boot hang
      documented.
- [x] maple audio: **playback and capture through the framework work**
      (2026-10-19). The Android 15 `libacdbloader` runs in mediaserver through
      libhyb and the HAL declares the device acdb id + loads the matching
      blocks before route/`pcm_open` (`acdb_stream_cal`); the yoshino acdbdata
      is staged as `MTP` (what `/sys/devices/soc0/hw_platform` reports), and
      `pcm_write`/`pcm_read` now pass bytes, not frames. Audible AudioTrack
      tone at 440 Hz, ~99 % of a DMIC loopback's band energy; the same tone
      recorded back through AudioRecord (new `audiorecord_test`) reads
      440.00 Hz. The HAL honors AudioPolicyManager's `routing=` — speaker 15 /
      headphones 10 (RX cal and stream verified), builtin mic 4 / headset mic
      8 — but the kernel exposes no `h2w` switch so jack routing never
      triggers by itself, and TX calibration is off on purpose (the loader
      accepts it but the ADM then refuses every capture prepare until reboot);
      quirks TEST 50/TEST 51. BT SCO known-dead.
- [x] Generalize the layout/scripts for `devices/<codename>/<release>/`
      (cooper/donut + maple/kitkat share one sync script).
- [x] KitKat 4.4.4 on `poplar` (sibling yoshino device): boots to launcher +
      nav bar with touch, display, USB adb, HW GL via glrenderd (Oct 2026,
      sherm port — tree committed as `devices/poplar/kitkat/`, predates maple
      TEST 36-37).
- [x] maple media decode (TEST 52): the image was missing
      `/system/etc/media_codecs.xml`, so `MediaCodecList` returned no codecs
      and every compressed format failed (ogg/mp3 system sounds, ringtones,
      music; WAV worked only because `audio/raw` bypasses OMX). Now shipped
      from `devices/maple/kitkat/media/media_codecs.xml` and verified
      2026-10-19: Music plays mp3/ogg through the HAL with cal, a DMIC
      loopback of a 440 Hz mp3 reads 440.00 Hz, UI clicks play.
- [x] maple video decode (2026-10-19): the software h264/vpx/mpeg4 decoders
      arrived with the same `media_codecs.xml`. Gallery's `MovieActivity`
      on a 320×240 h264+AAC clip creates `OMX.google.h264.decoder`,
      renders the test pattern full-screen (`MEDIA_INFO_VIDEO_RENDERING_START`,
      GL composition on the existing display path) and loops the 880 Hz
      track back at 880.00 Hz — quirks TEST 52.
- [ ] maple audio leftovers: a `h2w`-style jack switch in the kernel (the
      tasha codec registers only `hdmi`/`lid`/`sim_detect`), so
      `WiredAccessoryManager` can route headphones/headset mic by itself; a
      fix or workaround for the ADM topology mismatch that makes TX
      calibration poison capture (`copp_idx:-131`); BT SCO stays known-dead
      (CHARQUIRKS).
- [ ] Finish poplar: add `poplar` to `scripts/sync-device-tree.sh` (done in-tree
      — verify), forward-port maple TEST 36-37 (wifi supplicant services, adsp
      boot, dual-node lights, `service_locator.enable=0`), formalise GPU fixes
      into `patches/kitkat/`, and port the
      [libhyb](docs/advanced/software/libhyb.md) stack (poplar still builds the
      removed glrenderd design, see its README).
- [ ] KitKat 4.4.4 on `sagit` (Xiaomi Mi 6, same MSM8998/Adreno 540 as
      maple): device tree scaffolded as `devices/sagit/kitkat/` with
      Mi-6 deltas (no microSD, Synaptics DSX touch, tasha-only audio, no
      vendor partition); nothing built or booted yet. Next: dump stock
      facts with `scripts/dump-sagit-stock.sh`, resolve the `VERIFY-ME`
      markers, stage the Lineage 22.1 closure + kernel, then first boot.
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
  patched sources, reference binaries), kept for the poplar port; maple's
  own GL path has since moved to libhyb.
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
- `vtouch` is ported from MLXProjects' Froyo `vtouch` 1.2.0; `libhyb` is
  ported from [DoomGuy41/Taimen-KitKat](https://github.com/DoomGuy41/Taimen-KitKat)
  (the retired glrenderd was ported from
  [chardidathing/pdx203-jellybean](https://github.com/chardidathing/pdx203-jellybean);
  its sources remain in git history).
