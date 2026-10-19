# Sony Xperia XZ Premium G8141/G8142 (`maple`)

![Sony Xperia XZ Premium](device.jpg)

*Photo: [Deoma12](https://commons.wikimedia.org/wiki/File:Xperia_XZ_Premium.png), CC BY-SA 4.0, via Wikimedia Commons.*

The XZ Premium is Sony's 2017 flagship on Qualcomm's **Snapdragon 835
(MSM8998)**: 4×2.45 + 4×1.9 GHz Kryo (ARM64) + Adreno 540, 4 GB RAM,
64 GB UFS, microSD, a 5.5" 3840×2160 HDR panel (rendered at 1080p /
density 410 in normal use), USB-C. It shipped with **Nougat 7.1.1**;
`maple` is the single-SIM G8141, `maple_dsds` the dual-SIM G8142. Our ports
put **KitKat 4.4.4** (a 32-bit, 2013 userspace) and, more recently,
**Donut 1.6** (AOSP `android-1.6_r2`, DRD35) on it.

## Status

### KitKat 4.4.4 — AOSP `android-4.4.4_r2` (KTU84Q) — boots to UI

| Area | State |
| --- | --- |
| Boot | Boots to launcher (`sys.boot_completed=1`, 75 binder services), all partitions mounted |
| Launcher / UI | Working; sustained animations 59.5–60.0 fps (libhyb, measured 2026-10-19 — was ~26–35 fps in the software-drawn glrenderd era) |
| Touch | Working (Clearpad, full-range coords, nav bar + gpio keys) |
| Display | Working, 1080×1920; `gralloc.maple` is the single fb0 flipper (`fbpresentd` atomic commit — release fences confirm DMA0 scanout — with copy + `FBIOPUT_VSCREENINFO` fallback, A/B-verified 2026-10-19) |
| GPU | **Hardware GL/GLES 3.2 on the Adreno 540** via [libhyb](../../advanced/software/libhyb.md) — the driver runs in-process (this replaced the glrenderd chroot; built 2026-10-18, verified on hardware 2026-10-19: EGL 1.5, `Adreno (TM) 540`, UI GL-drawn through hwui) |
| USB | adb working (configfs gadget, `18d1:4ee7`) |
| Lights | Brightness + sleep via the ported lights HAL (both backlight nodes) |
| Storage | Emulated internal storage working (`/sdcard`); microSD still untested |
| WiFi | Feature + supplicant services in place; driver loads but **no wlan0** — locator (SERVREG_LOC) now served by in-tree `pdserve` and the PDR chain (icnss) registers its notifier, but the modem root PD crashes every ~41 s before wlan_pd/WLFW can start; modem stability (keepalive/rild) is the open blocker. Taimen's 64-bit daemon chroot (`cnss-daemon`/WLFW) is the reference path — [mapping](../../advanced/taimen-kitkat-mapping.md) |
| Audio | **Framework playback + capture working, headset/headphone routing wired** (verified 2026-10-19: an `AudioTrack` tone is audible — 440 Hz at ~99 % of a DMIC loopback capture's band energy — and the same tone recorded back through `AudioRecord` reads 440.00 Hz; `dmesg` shows the calibration set at every stream start). ACDB calibration is pushed by the Android 15 `libacdbloader` through [libhyb](../../advanced/software/libhyb.md) inside mediaserver; `/dev/msm_audio_cal` is opened `0660 media audio`. The HAL honors AudioPolicyManager's `routing=` (speaker 15 / headphones 10, builtin mic 4 / headset mic 8) but the kernel exposes no `h2w` switch, so jack routing never triggers by itself, and TX calibration is disabled (it breaks the ADM capture path until reboot) — [quirks TEST 50](quirks.md)/[TEST 51](quirks.md), BT SCO known-dead |
| Cellular | No RIL library for this port — not working |
| Media playback | **Working for decoded audio** (verified 2026-10-19): ogg/mp3 through Music's AudioPreview opens the HAL (`route out 0x2 -> speaker (acdb 15)`, topology `112fc`) and a DMIC loopback of a 440 Hz mp3 reads **440.00 Hz**; UI clicks now play too. This needed `media/media_codecs.xml` — the image had no `/system/etc/media_codecs.xml`, so `MediaCodecList` returned no codecs and `OMXCodec::Create` failed silently (`SoundPool: onLoadSoundEffects(), Error -22`, `MediaPlayer: error (1, -2147483648)`); WAV worked only because `audio/raw` bypasses OMX. Video decode was missing for the same reason: the software h264/vpx decoders are now enabled, but video output is still untested — [quirks TEST 52](quirks.md) |
| Camera / GPS / BT / fingerprint | Not started (proprietary HALs) |

### Donut 1.6 — AOSP `android-1.6_r2` (DRD35) — boots to UI

| Area | State |
| --- | --- |
| Boot | Boots to launcher (`sys.boot_completed=1`); touch live ~6.5 s after boot |
| Launcher / UI | Working; portrait orientation (flip-cover lid sensor no longer forces landscape) |
| Touch | Working (Clearpad, full-range coords 0..2159 / 0..3839, vtouch proxy) |
| Keyguard | Tap-to-unlock ("Tap to unlock."); camera full-press → MENU |
| Camera key | Half-press → BACK, full-press → MENU (WAKE_DROPPED); volume keys work |
| Display | Working, 1080×1920 software SurfaceFlinger (no glrenderd on this port) |
| Settings | Opens without crashing (key character maps fixed to mode 0644) |
| USB | adb working (configfs gadget, `18d1:4ee7`) |
| Power button | Handled by the kernel's `powerkey_handle` (locks only when USB is unplugged); see quirks TEST 47 |
| Audio / WiFi / Cellular / Camera HAL | Not started (proprietary HALs; Donut era) |

Known device quirks: rare **pre-userspace boot hang** (stuck on the Sony
splash, **zero USB** — recover with a TWRP round-trip before reflashing);
the keyguard window caps screen-off at ~10 s (stock KitKat behavior, power
key wakes); `screencap` returns a constant image on this port (dump fb0
instead). Details in [quirks.md](quirks.md).

**Releases:** maple images are built locally — no published zips yet; see
[Building KitKat for maple](../../advanced/build/kitkat.md). Cooper's zips
live under [GitHub Releases](https://github.com/pukikiko/retro-android-ports/releases).

## Documentation and links

- [**Device quirks**](quirks.md) — hardware snapshot, flash layout, the
  full TEST 1–49 bringup log, blockers and verified fixes.
- [KitKat 4.4.4 porting notes](../../advanced/porting-notes/kitkat.md) —
  generic playbook this port produced.
- [Donut 1.6 porting notes](../../advanced/porting-notes/donut.md) —
  generic playbook (touch handshake, MENU-less keyguards, firmware-load
  fallback, key character maps).
- [Building KitKat for maple](../../advanced/build/kitkat.md).
- [Building Donut for maple](../../advanced/build/donut.md) — chroot recipe,
  patch order, packaging and flashing.
- [Device tree (KitKat)](../../../devices/maple/kitkat/)
  (`devices/maple/kitkat/`) and [patches](../../../patches/kitkat/README.md)
  (`patches/kitkat/`).
- [Device tree (Donut)](../../../devices/maple/donut/)
  (`devices/maple/donut/`) and [patches](../../../patches/donut/README.md)
  (`patches/donut/`).
- Tools used here: [libhyb](../../advanced/software/libhyb.md) (KitKat:
  Adreno 540 driver in-process + `fbpresentd`; staging tools in
  `tools/libhyb/`), [vtouch](../../advanced/software/vtouch.md).
