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
only its product/board configs, `init`/`fstab`/`ueventd`, its own
`mixer_paths_tasha.xml`, the WLAN ini, and firmware/ACDB staging kept
outside git. The module names that `ro.hardware` selects are parameterized
by `TARGET_DEVICE` (`audio.primary.chiron`, `gralloc.chiron`,
`lights.chiron`), so one source builds for either phone.

## Status

### KitKat 4.4.4 — AOSP `android-4.4.4_r2` (KTU84Q) — boots

| Area | State |
| --- | --- |
| Boot | Boots to the UI on the stock `OS3` kernel (Linux 4.4.302, multi-DTB: `dtb_0` sagit id `0x1e`, `dtb_1` chiron id `0x20`) — verified on hardware 2026-10-25. The boot image is built with `BOARD_KERNEL_BASE := 0x00000000` / `pagesize 4096` so its layout (kernel `0x8000`, ramdisk `0x1000000`, tags `0x100`) matches the stock image |
| Display / GPU | Adreno 540 through libhyb + `fbpresentd`; panel `1080x2160`, density 440, stride 4352. SurfaceFlinger reaches the driver (ES 3.2, first frames swap) |
| Audio | WCD9341 tasha codec + **TAS2559** smart amp with its TAS2560 companion on QUAT_MI2S (I2S). Headphones use the AIF4 path on `SLIMBUS_6_RX` (SLIM RX2/RX3 → RX INT1_2/INT2_2). Builtin mic is DEC7/AMIC3, headset mic DEC0/ADC2. `mixer_paths_tasha.xml` is taken from the stock dump; the routes are compiled in but **not yet verified by playback** |
| WiFi | **Works, boot-automatic, verified on a freshly built image cold-booted on hardware 2026-10-27** (association + DHCP, and the Settings/`svc wifi` toggle both directions; earlier milestone 2026-10-26 on hand-pushed files). The whole WCN3990 bring-up stack is shared with sagit and installed by the build: `/sbin/{wf-setup,wf-efs-keeper,wf-wifi-gate,rfs-dirs,modem-hold}.sh` in the ramdisk, the `wf-*` services in `init.chiron.rc`, `sec_config`, the BDF name variants, `wlanmdsp.mbn`, `/system/bin/{wlq,rfssrv2,sm-vndk,linker64}` and the 64-bit vendor daemon/library closure — `linker64` because those daemons are aarch64 and this tree builds arm-only. Root cause was the msm8998 ipc-router IRSC completion never firing; the two fresh-image prerequisites (linker64, `/dev/block/bootdevice` alias) are [wifi-progress.md](wifi-progress.md) §11. `persist` is empty, so the MAC comes from the firmware, not from provisioning |
| Storage | Internal emulated only; `system` 3 GiB. `persist` mounts but contains nothing (no WiFi MAC, no TAS2559 calibration) |
| Touch | Synaptics DSX, `synaptics_dsx.idc` reused from sagit — confirm the device name with `getevent -p` |
| USB | configfs gadget, UDC `a800000.dwc3` (same as sagit) |
| Cellular / Camera / GPS / BT / fingerprint | Not started |

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

Fix: `GRALLOC_MODULE` is now a compile-time macro defaulting to sagit's
path, overridden from `egl/Android.mk` with
`-DGRALLOC_MODULE='"/system/lib/hw/gralloc.$(TARGET_DEVICE).so"'`. The
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

```
scripts/sync-device-tree.sh chiron
```

then, in the KitKat build container (`/srv/aosp-kitkat`):

```
. build/envsetup.sh && lunch full_chiron-eng && make -j8 bootimage systemimage
out/host/linux-x86/bin/simg2img out/target/product/chiron/system.img out/target/product/chiron/system_raw.img
```

`boot.img` is 15 MiB, `system_raw.img` 3 GiB. Flash from TWRP: push
`system_raw.img` to `/data/local/tmp` and `dd` it to
`/dev/block/bootdevice/by-name/system`; `boot.img` goes to the `boot`
partition. The kernel blob (`vendor/xiaomi/chiron/kernel`) and all staging
directories are gitignored.

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
| `system.prop`, `fstab`, `init*`, `ueventd`, mixer, WLAN ini | in tree | already captured from the stock dump |

## Documentation and links

- [sagit device page](../sagit/README.md) and [quirks](../sagit/quirks.md) — the shared tree this port derives from
- [KitKat porting notes](../../advanced/porting-notes/kitkat.md), [libhyb](../../advanced/software/libhyb.md)
