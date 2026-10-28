# Xiaomi Mi MIX 2 (`chiron`)

The Mi MIX 2 is Xiaomi's 2017 bezel-less flagship on Qualcomm's
**Snapdragon 835 (MSM8998)** — the same SoC as sagit/maple: 4×2.45 + 4×1.9
GHz Kryo (ARM64) + Adreno 540, 6 GB LPDDR4, 64/128/256 GB UFS 2.1 (**no
microSD**), a 5.99" 1080×2160 IPS panel, USB-C, a rear fingerprint reader
and a **piezo** earpiece (the handset speaker is the display glass). It
shipped with MIUI on Nougat 7.1.1. This port targets **KitKat 4.4.4**
(AOSP `android-4.4.4_r2`, KTU84Q): a 32-bit userspace on the stock 4.4
arm64 kernel via `CONFIG_COMPAT`.

The tree in [`devices/chiron/kitkat/`](../../../devices/chiron/kitkat/) is
deliberately **thin**. Everything board-independent is shared with the
boot-verified [sagit tree](../sagit/README.md) and compiled from
`vendor/xiaomi/sagit`: the EGL/GLES shims, libhyb, gralloc, the tinyalsa
audio HAL, lights, `fbpresentd`, `pdserve`, `rtc-restore`. Chiron supplies
only its product/board config, its own `mixer_paths_tasha.xml`, the WLAN
ini, and firmware/ACDB staging kept outside git — even the boot ramdisk's
`init`/`fstab`/`ueventd` files are shared with sagit. Board artifacts are
named after the **SoC, not the phone**: both boards pass
`androidboot.hardware=msm8998`, so the ramdisk carries `init.msm8998.rc`,
`init.msm8998.usb.rc`, `ueventd.msm8998.rc` and `fstab.msm8998`, and
`/system/lib/hw` carries `gralloc.msm8998.so`, `lights.msm8998.so`,
`audio.primary.msm8998.so` (chiron's product adds `-DAMP_TAS2559`). The
MIUI V5 pack installs them under the ROM's own names
(`gralloc.chiron.so`, `lights.chiron.so`, `audio.primary.msm8974.so`).

## Status

### KitKat 4.4.4 — AOSP `android-4.4.4_r2` (KTU84Q) — boots

| Area | State |
| --- | --- |
| Boot | Boots to the UI on the stock `OS3` kernel (Linux 4.4.302, multi-DTB: `dtb_0` sagit id `0x1e`, `dtb_1` chiron id `0x20`) — verified on hardware 2026-10-25. The boot image is built with `BOARD_KERNEL_BASE := 0x00000000` / `pagesize 4096` so its layout (kernel `0x8000`, ramdisk `0x1000000`, tags `0x100`) matches the stock image |
| Display / GPU | Adreno 540 through libhyb + `fbpresentd`; panel `1080x2160`, density 440, stride 4352. SurfaceFlinger reaches the driver (ES 3.2, first frames swap) |
| Audio | WCD9341 tasha codec + **TAS2559** smart amp with its TAS2560 companion on QUAT_MI2S (I2S). Headphones use the AIF4 path on `SLIMBUS_6_RX` (SLIM RX2/RX3 → RX INT1_2/INT2_2). Builtin mic is DEC7/AMIC3, headset mic DEC0/ADC2. `mixer_paths_tasha.xml` is taken from the stock dump; the routes are compiled in but **not yet verified by playback** |
| WiFi | **Works, boot-automatic, verified on a freshly built image cold-booted on hardware 2026-10-27** (association + DHCP, and the Settings/`svc wifi` toggle both directions; earlier milestone 2026-10-26 on hand-pushed files). The whole WCN3990 bring-up stack is shared with sagit and installed by the build: `/sbin/{wf-setup,wf-efs-keeper,wf-wifi-gate,rfs-dirs,modem-hold}.sh` in the ramdisk, the `wf-*` services in `init.msm8998.rc`, `sec_config`, the BDF name variants, `wlanmdsp.mbn`, `/system/bin/{wlq,rfssrv2,sm-vndk,linker64}` and the 64-bit vendor daemon/library closure — `linker64` because those daemons are aarch64 and this tree builds arm-only. Root cause was the msm8998 ipc-router IRSC completion never firing; the two fresh-image prerequisites (linker64, `/dev/block/bootdevice` alias) are [wifi-progress.md](wifi-progress.md) §11. `persist` is empty, so the MAC comes from the firmware, not from provisioning |
| Storage | Internal emulated only; `system` 3 GiB. `persist` mounts but contains nothing (no WiFi MAC, no TAS2559 calibration) |
| Touch | Synaptics DSX, `synaptics_dsx.idc` reused from sagit — confirm the device name with `getevent -p` |
| USB | configfs gadget, UDC `a800000.dwc3` (same as sagit) |
| Cellular | **Automatic startup failed after flashing, verified 2026-10-28**: `qmxd` is running/ready, but `ril-daemon` restarts, baseband is empty and SIM is `NOT_READY`. The shared native-launcher fix for KitKat init's duplicate library-path environment awaits automatic-start verification. Earlier **manual** QCRIL/QMUX testing read `AT20-0506_2249_2f010e6`, entered `Radio On` and received VOICE/DMS/NAS/UIM replies, but both UIM slots returned `card_state=2`, `card_error=3`, no apps. SIM detection, registration, calls, SMS and mobile data remain open. |
| Camera / GPS / BT / fingerprint | Not started |

### Cellular control plane, 2026-10-28

- **Match the IDL objects to QCRIL.** `qmi_client_init` returned `-47`
  before any service allocation because the VOICE getter returned NULL.
  This RIL requests VOICE `(2,53,6)`, DMS `(1,39,6)` and NAS `(1,113,6)`;
  the earlier cancro V5 `libqmiservices.so` only accepted VOICE `(2,42,6)`
  and DMS `(1,36,6)`. The matching `coresysroot/system/vendor/lib` library
  has SHA256 `5c3bc57737222f4fde45686e88e7aeddb6edd3575342e3573209031f3c3c2611`.
  Both files are 80148 bytes, so size is not a compatibility check.
- **QMUX is a socket protocol, not the rmnet interface.** The logical
  connection name `rmnet0` does not bypass the QMUX socket. `qmxd` bridges
  the 32-bit client to AF_MSM_IPC (family 27), resolving each modem-node
  service with `IPC_ROUTER_IOCTL_LOOKUP_SERVER`; router ports change across
  boots. Missing services must fail allocation, not receive a fake client
  routed to port zero.
- **Keep the wire offsets exact.** Each frame is a 4-byte total length,
  4-byte link id, 32-byte header, then payload. Header offsets are type 0,
  link 4, full 32-bit transaction 8, system error 12, QMI error 16,
  connection 20, service 24, client byte 28 and modem-to-client flag
  `0x80` at byte 29. Allocation carries the service as a payload u32;
  release carries the client at payload byte 4. QMI payloads have their
  own 7-byte header and must match its TLV length. Host ASan/UBSan tests
  cover transaction `0x108`, owner isolation, release and malformed input;
  real VOICE/DMS/NAS/UIM traffic and baseband query succeeded on-device.
- **Do not equate Radio On with working cellular.** The manual test used
  files under `/data/local/tmp/rilp`, not an independently bootable image.
  Both slots returned UIM errors: `card_state=2` means ERROR, and
  `card_error=3` is `QMI_UIM_CARD_ERROR_NO_ATR_RECEIVED`, not a definitive
  indication that no card is inserted (see the
  [libqmi enum definitions](https://sources.debian.org/data/main/libq/libqmi/1.32.2-1/src/libqmi-glib/qmi-enums-uim.h)).
  Slot 0 power-up received a successful QMI response, but no working SIM
  application followed; slot mapping, electrical state and modem setup
  remain unproven explanations. Optional VT/presence services were absent,
  QCRIL could not open its database (`14`), and no rmnet data interface was
  present. No calls, SMS or data session have been verified. MIUI V5 was
  not changed. The phone was returned to recovery after log capture.
- **Capture logs without Android logger.** The kernel has no logger driver;
  creating `/dev/log` nodes still gives `ENODEV`. A temporary log shim sent
  `__android_log_*` to stderr with `persist.radio.adb_log_on=1`. Manual adb
  launches need `trap '' HUP` and all three standard streams redirected;
  KitKat has no `env` utility, so use shell `export`. Raw QMI/RIL logs may
  contain subscriber/device identifiers and must stay outside git.
- **One baseband implementation for sagit and chiron.** Both AOSP products
  inherit `vendor/xiaomi/sagit/ril/ril.mk`. The shared
  `init.msm8998.rc` imports `init.msm8998.ril.rc`, which starts `qmxd`
  and starts QCRIL only after the QMUX listener publishes readiness.
  The private library path `/system/vendor/msm8998-ril/lib` avoids
  replacing AOSP's global `libril.so` or the WiFi vendor closure. Input
  files live in `vendor/xiaomi/sagit-ril/{bin,lib}`, outside the synced
  device tree. The staging script and build both verify the exact 23-file
  SHA256 manifest; the tested `libqmi_client_qmux.so` includes the earlier
  open-connection/is-active patches and is not an unmodified donor blob.
  No proprietary files or device logs are stored in this repository.
  The old shared `init.rc` commented only the `ril-daemon` service header,
  leaving its socket/user/group options attached to `debuggerd`; remove
  that whole stale block so debuggerd cannot recreate the RIL sockets.
- **Check permissions inside the image, not just the staging tree.**
  The first integrated image had an executable `rild` in the build tree,
  but `debugfs stat` on the expanded system image showed mode `0644` at
  `/vendor/msm8998-ril/bin/rild`: KitKat's filesystem configuration does
  not recognize that nested bin directory. Install the same binary as
  `/system/bin/rild-msm8998`, which receives executable permissions;
  the private library directory stays unchanged. The recovery side-target
  also reports `cannot stat '.../root/init.recovery.*.rc'` (ignored by
  make); only boot/system are being validated, not the generated recovery.
- **Integration is not another hardware result.** The in-tree daemon has
  bounded nonblocking output queues, 128 service-client slots (the manual
  WDS initialization exhausted 32), radio-only socket access and opt-in
  packet dumps (`-v`). Unsupported control operations, including version
  list and data-format setup, return failure instead of fabricated service
  versions or success. Modem sockets use nonblocking I/O; send failure or
  a fatal receive error disconnects the owning QMUX client rather than
  blocking every service or repeatedly reading an oversized queued packet.
  Chiron's flashed image reached the listener but failed to keep RIL alive;
  successful automatic startup remains unverified on both phones. Modem
  restart recovery and rmnet setup are not complete.
- **Shared build verified 2026-10-28.** Both `full_sagit-eng` and
  `full_chiron-eng` build boot/system images. The packed boot ramdisks
  contain the shared RIL import with only one RIL socket owner. Expanded
  system images were checked with `debugfs`: all 23 blob hashes match,
  `qmxd` and `rild-msm8998` have executable modes, and both products carry
  byte-identical `qmxd`. Recursive runtime dependency checks cover 51 ELF
  files per product. Host ASan/UBSan tests cover 128 clients, fragmented
  frames, partial writes, EINTR/EAGAIN, queue overflow and transport errors.
  These are build/host checks, not proof of automatic radio startup on a
  freshly flashed phone; the user's subsequent chiron flash failed below.
- **Verify the installed startup chain, not the build date.** Readback from
  the user's flashed chiron on 2026-10-28 matched all 23 blob hashes, the
  built `qmxd` and the shared init files; both executables were executable.
  `qmxd=running`, `sys.msm8998.qmux.ready=1`, `ril-daemon=restarting`, empty
  `gsm.version.baseband` and `gsm.sim.state=NOT_READY` showed that socket
  readiness was not enough. The modem was online and EFS service running;
  framework logs repeatedly reported `Couldn't find 'rild' socket`.
  The old build-property date did not mean old radio files were flashed.
  Capture was read-only, without manually starting RIL, and the phone was
  returned to recovery. Init discards stderr, so no linker error was captured.
- **KitKat init cannot override its global library path with setenv.** In
  `system/core/init/init.c`, `add_environment()` appends a new entry rather
  than replacing a matching key. The service's `setenv LD_LIBRARY_PATH`
  followed the global `/vendor/lib:/system/lib` value from `init.environ.rc`,
  leaving the private RIL library path ineffective. Static ELF checks confirm
  that the old global dependency graph lacks `rild` and `RIL_setRilSocketName`,
  both required by this executable; the private-first startup graph resolves
  all its strong imports. This is not captured linker stderr or proof that
  modem initialization will succeed.
- **A shell launcher loses the hyphenated debug-socket variable.** A native
  host build of this AOSP tree's mksh source reproduced loss of
  `ANDROID_SOCKET_rild-debug` during the shell/export/exec chain. The library
  path was corrected, but preserving a descriptor alone does not preserve
  the environment name RIL uses to find it. That shell launcher was rejected
  before flashing. Its host build required mksh's existing `MKSH_GCC55009`
  compatibility flag for the compile-time signed-wrap assertion; no Android
  shell source or build flags were changed.
  Both products instead use `/system/bin/start-msm8998-ril`, built from shared
  `ril/start-ril.c`: libc `setenv(..., 1)` replaces the library path and
  `execv()` starts `rild-msm8998` without filtering other environment keys.
  Init's `publish_socket()` clears `FD_CLOEXEC`, so both socket descriptors
  survive. The host regression redirects only the exec target to a probe
  and checks the path, unchanged PID and both socket names/descriptors across
  a real exec. The native-launcher rebuild and packed-image checks are in
  progress. Flash matching new boot and system together: the service command
  is in boot, while the launcher is in system. Actual automatic startup
  remains unverified on both phones; no new image was flashed by the agent
  and no manual RIL launch was used.
- **Instrumented so the next flash explains itself.** KitKat init sends a
  service's stdout/stderr to `/dev/null` and records no exit status, and
  debuggerd left no `rild` tombstone (all ten in the flashed readback were
  `SIGPIPE` from our own capture pipes), so `rild` reaches `exit()` instead of
  dying on a signal. The launcher now forks, redirects the child's
  stdout/stderr to `/data/misc/radio/rild-console.log` and reports
  `exit=<code>` or `signal=<n>` to kmsg under `RILL`; `qmxd` writes
  listen/accept/alloc/disconnect lines under `QMXD` through the same kmsg
  fallback. QCRIL only ever wrote its own diagnostics to stderr, so this is the
  first build where they can be read.
- **`--` keeps QCRIL's argv identical across ROMs.** `hardware/ril/rild/rild.c`
  uses the library arguments that follow `--` and otherwise reads them from the
  `rild.libargs` property. MIUI V5 still ships the 8974-era
  `rild.libargs=-d /dev/smd0` while the AOSP build sets no such property, so the
  launcher always passes `--` and both ROMs hand `RIL_Init` exactly one
  argument.
- **ASan itself faults on this WSL host, so it cannot gate the build.** With its
  SIGSEGV handler enabled, a failing run printed nothing but
  `AddressSanitizer:DEADLYSIGNAL` in a loop -- 857 MB before `timeout` killed it,
  16 GB once before. Measured over repeated runs of both regressions: 20/20 clean
  with `-fsanitize=undefined`, 12/12 clean with no sanitizer at all, but roughly
  one in three ASan runs dies (139) *before its first line of output*, the same
  binary that passes the other two times. `ASAN_OPTIONS=handle_segv=0` exposes
  that as a plain early SIGSEGV, so the handler -- not the launcher and not a
  self-`execv` probe, which an earlier version of this note blamed -- is the
  faulting code. `ril/test.sh` now gates on UBSan and keeps ASan behind `ASAN=1`;
  the launcher regression asserts inside its forked child regardless, because
  that is where the inherited descriptors and environment actually are.

### Boot logo hang, fixed 2026-10-25

The first chiron build stopped at the logo with SurfaceFlinger in a restart
loop: EGL initialised on the Adreno 540 and the window surface was created,
then every `eglSwapBuffers` failed with `0x0000300d` and
`__android_log_assert` aborted the process, taking zygote/system_server
down with it.

Root cause: the EGL shim hardcoded sagit's gralloc module.
`egl_shim.c` did `dlopen("/system/lib/hw/gralloc.sagit.so")` and
`dlsym(..., "sagit_gralloc_import")`; on chiron the platform loads
`gralloc.chiron.so`, so the import function stayed NULL and the driver's
dequeue wrapper could not hand buffers over:

```
eglshim: dlopen gralloc.sagit.so FAILED: library "/system/lib/hw/gralloc.sagit.so" not found
eglshim: init done egl=... gles2=... gralloc=0x0 import=0x0
grshim: alloc 1080x2160 fmt 0x1 usage 0x10001a03 ok stride 1088
eglshim: dequeue wrap FAILED
Adreno: DequeueBuffer: dequeueBuffer failed
SurfaceFlinger: eglSwapBuffers(...) failed with 0x0000300d
```

Fix: `GRALLOC_MODULE` is now a compile-time macro. At the time it was
overridden from `egl/Android.mk` with
`-DGRALLOC_MODULE='"/system/lib/hw/gralloc.$(TARGET_DEVICE).so"'`; since
the msm8998 unification it is simply
`"/system/lib/hw/gralloc.msm8998.so"` on both phones. The
exported symbol keeps its name (`sagit_gralloc_import`) in every board's
gralloc module, so the shim resolves it unchanged. Only
`/system/lib/egl/libEGL_adreno.so` differs from the broken image; swapping
that one file into the installed system was enough to boot.

## Chiron vs sagit

| | sagit (Mi 6) | chiron (Mi MIX 2) |
| --- | --- | --- |
| Speaker | TFA9888 on QUAT_MI2S, `TFA Profile` (`tfa98xx_*.cnt`) | TAS2559 + TAS2560 on QUAT_MI2S, `TAS2559/TAS2560 Mute` + DAC volume; firmware `tas2559_uCDSP.bin`, `tas2559_s_uCDSP.bin` |
| Headphones | `SLIMBUS_0_RX`, SLIM RX0/RX1 ← AIF_MIX1_PB | `SLIMBUS_6_RX`, SLIM RX2/RX3 ← AIF4_PB |
| Builtin mic | DEC7/DMIC0 | DEC7/AMIC3 |
| Panel | 1080×1920, 480 dpi (`sagit` system.prop 440) | 1080×2160, 440 dpi |
| ACDB | `Forte/Forte_*.acdb` | `ChironS/ChironS_*.acdb` + `Forte/*` + `adsp_avs_config.acdb` |
| persist | WiFi MAC + TFA calibration | empty |
| Kernel | patched stock `Image.gz-dtb` (`scripts/patch-sagit-dtb.py`) | stock `Image.gz-dtb` as-is |

## Building and flashing

Stage the matched RIL once, then sync both the shared and board trees:

```
python3 devices/sagit/kitkat/ril/stage-ril.py <tested-flat-ril-directory> /home/juezhi/aosp-kitkat
AOSP_DIR=/home/juezhi/aosp-kitkat scripts/sync-device-tree.sh sagit
AOSP_DIR=/home/juezhi/aosp-kitkat scripts/sync-device-tree.sh chiron
```

The flat RIL directory must contain the 23 files identified by
`devices/sagit/kitkat/ril/blobs.sha256`; it is not a stock msm8998 vendor
folder. Do not substitute a same-sized `libqmiservices.so` or an unpatched
QMUX client. The build refuses missing or mismatched files rather than
silently producing a nonfunctional radio. Run the bridge's host regression
with `bash devices/sagit/kitkat/ril/test.sh` (C compiler + ASan/UBSan).

Then, in the KitKat build container (`/srv/aosp-kitkat`):

```
. build/envsetup.sh && lunch full_chiron-eng && make -j8 bootimage systemimage
out/host/linux-x86/bin/simg2img out/target/product/chiron/system.img out/target/product/chiron/system_raw.img
```

`boot.img` is 15 MiB, `system_raw.img` 3 GiB. Flash from TWRP: push
`system_raw.img` to `/data/local/tmp` and `dd` it to
`/dev/block/bootdevice/by-name/system`; `boot.img` goes to the `boot`
partition. The kernel blob (`vendor/xiaomi/chiron/kernel`) and all staging
directories are gitignored.

The ramdisk's `/sbin/healthd` comes from a `system/core` checkout carrying
[`system-core-healthd-msm-power-supply-types.patch`](../../../patches/kitkat/system-core-healthd-msm-power-supply-types.patch).
Without it `plugged` never leaves 0 on this kernel (the charger reports
`USB_HVDCP`/`USB_PD`/`USB_FLOAT`, which stock KitKat does not know), so a
rebuilt boot that skips the patch loses the charging indicator again.

## Files you must provide (and where they go)

Paths are relative to the AOSP root (`/home/juezhi/aosp-kitkat`); nothing
here is committed.

| What | Where it goes | How |
| --- | --- | --- |
| Stock 4.4 kernel (`Image.gz-dtb`) | `vendor/xiaomi/chiron/kernel` | extract from the stock `boot.img`; no DTB patch needed (chiron is `dtb_1` in the multi-DTB blob) |
| TAS2559 firmware | `vendor/xiaomi/chiron-fw/audio/` | `tas2559_uCDSP.bin` + `tas2559_s_uCDSP.bin` from the stock ROM's audio firmware |
| ACDB calibration | `vendor/xiaomi/chiron-hyb/acdbdata/` | copy `ChironS/`, `Forte/` and `adsp_avs_config.acdb` from the stock `/vendor/etc/acdbdata` |
| Modem / ADSP / WLAN firmware | `vendor/xiaomi/sagit-fw/` (reused) | same MSM8998 blobs as sagit (`mba.mbn`, `modem.*`, `adsp.*`, `cpe_*`, `bdwlan*`); replace the WLAN board data with chiron's once a source is found |
| Android 15 vendor closure + `fbpresentd` | `vendor/xiaomi/sagit-hyb/root/`, `vendor/xiaomi/sagit-bin/bin/` | reused from the sagit port (`tools/libhyb/`) |
| WLAN vendor daemons + 64-bit libraries | `vendor/xiaomi/sagit-wifi/{bin,lib64,system-lib64}/` | `devices/chiron/kitkat/wifi-tools/stage-wifi-vnd.sh` against a phone on stock firmware (see [wifi-progress.md §10](wifi-progress.md)) |
| `wlq` / `rfssrv2` / `sm-vndk` | `vendor/xiaomi/sagit-bin/bin/` | `devices/chiron/kitkat/wifi-tools/build-wf-bins.sh` |
| Shared 32-bit QCRIL closure | `vendor/xiaomi/sagit-ril/{bin,lib}/` | `devices/sagit/kitkat/ril/stage-ril.py` from the exact tested files; shared with sagit, version/hash-checked, not committed |
| `system.prop`, `fstab`, `init*`, `ueventd`, mixer, WLAN ini | in tree (the ramdisk files are `vendor/xiaomi/sagit/*msm8998*`; the mixer/WLAN ini are chiron's) | already captured from the stock dump |

## Documentation and links

- [sagit device page](../sagit/README.md) and [quirks](../sagit/quirks.md) — the shared tree this port derives from
- [KitKat porting notes](../../advanced/porting-notes/kitkat.md), [libhyb](../../advanced/software/libhyb.md)
