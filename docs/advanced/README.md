# Advanced documentation

Everything beyond the device main pages: the per-release porting notes and
build guides, the build-server runbook, and the software this project
writes to make old Android run on newer hardware.

New here? Start with the [main README](../../README.md). Device main pages
(description, per-release status) and quirks pages live in
[`docs/devices/`](../devices/).

## Porting notes, per Android release

The generic playbook for a release: source/toolchain, build-system
surprises, init/Edify/ABI differences, and the field lessons that
generalize across devices of that era.

| Release | AOSP branch | Notes | Ported to |
| --- | --- | --- | --- |
| Donut 1.6 | `android-1.6_r2` (DRD35) | [porting-notes/donut.md](porting-notes/donut.md) | [cooper](../devices/cooper/README.md), [maple](../devices/maple/README.md) |
| KitKat 4.4.4 | `android-4.4.4_r2` (KTU84Q) | [porting-notes/kitkat.md](porting-notes/kitkat.md) | [maple](../devices/maple/README.md), [poplar](../devices/poplar/README.md), [sagit](../devices/sagit/README.md) (skeleton) |

Planned playbooks (not written yet — they land with the first port of each
release): Eclair, Froyo, Gingerbread, Ice Cream Sandwich, Jelly Bean (4.1–4.3),
Lollipop, Marshmallow, Nougat.

## Build guides, per Android release

Step-by-step, from a pristine AOSP checkout to a flashable image:

- [Building Donut 1.6 for cooper/maple](build/donut.md) — Hardy i386 chroot,
  JDK 5, GCC 4.2.3, cooper BML packaging; maple uses the same recipe with
  the 4096-page boot.img repack, the ext4 system.img packaging script and
  the maple device tree (`devices/maple/donut/`).
- [Building KitKat 4.4.4 for maple](build/kitkat.md) — Trusty/JDK 6
  container, prebuilt 4.4 kernel, libhyb closure staging, simg2img/TWRP
  flashing (poplar's older glrenderd-based images: block-map/panel deltas
  in [poplar quirks](../devices/poplar/quirks.md)).
- [Build server runbook](build-server.md) — layout, footguns and flash
  workflow when building on a remote host.

## Project software

- [vtouch](software/vtouch.md) — multi-touch to single-touch input proxy;
  makes multi-touch panels work on pre-multitouch releases (Donut 1.6).
- [libhyb](software/libhyb.md) — runs a newer GPU driver (Adreno 540,
  Lineage 22.1 closure) inside KitKat processes, with a 64-bit
  `fbpresentd` presenting through an MDSS atomic commit. Replaced the
  glrenderd chroot/socket design (sources in `tools/glrenderd/` are in
  git history). Since 2026-10-19 the same loader also carries the Android 15
  **ACDB audio calibration loader** for `audio.primary.maple` (speaker
  playback verified on hardware).
- [mkbootimg-cooper](software/mkbootimg-cooper.md) — pagesize-patched
  Donut `mkbootimg` that can pack cooper's boot.img.
- [ipc-router-tools](software/ipc-router-tools.md) — `irsc-load` (arms
  the IPC Router Security Configuration, without which every userspace
  QMI sender blocks forever in `sendmsg`) and `pdstart` (raw
  SERVREG_NOTIF client that proved the modem refuses `RESTART_PD`).

## Reference notes

- [charquirks-mapping.md](charquirks-mapping.md) — how a sister port's
  4.2-on-pdx203 notes map onto maple/KitKat (what was independently
  converged, what applied, what is queued).
- [taimen-kitkat-mapping.md](taimen-kitkat-mapping.md) — how the
  Pixel 2 XL KitKat port (DoomGuy41/Taimen-KitKat) maps onto ours: the
  libhyb graphics core, every one of their patches (adopted or skipped
  with reasons), and the assessment of their 64-bit daemon chroot +
  HAL bridges for maple (modem/wifi/sensors/GNSS/NFC/BT).
- [poplar-kitkat-gpu-fixes.md](poplar-kitkat-gpu-fixes.md) — sibling XZ1
  (`poplar`) GPU/display fix reference (developed on poplar, adopted by
  maple); superseded for maple by [libhyb](software/libhyb.md) — poplar
  still runs the older glrenderd design (see its README).
