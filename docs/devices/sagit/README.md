# Xiaomi Mi 6 (`sagit`)

<!-- TODO: CC-licensed photo (no usable Wikimedia Commons shot found yet) -->

The Mi 6 is Xiaomi's 2017 flagship on Qualcomm's **Snapdragon 835
(MSM8998)** — the same SoC as maple/poplar: 4×2.45 + 4×1.9 GHz Kryo
(ARM64) + Adreno 540, 6 GB LPDDR4X, 64/128 GB UFS 2.1 (**no microSD**),
a 5.15" 1080×1920 IPS panel (JDI r63452 / LGD td4322 depending on the
unit, density 480), USB-C, capacitive Recents/Home/Back keys plus a
front fingerprint reader. It shipped with **MiUI on Nougat 7.1.1**;
LineageOS 22.1 (Android 15) is the current community release. This port
targets **KitKat 4.4.4** (AOSP `android-4.4.4_r2`, KTU84Q), a 32-bit
userspace on the stock 4.4 arm64 kernel via `CONFIG_COMPAT`.

The tree in [`devices/sagit/kitkat/`](../../../devices/sagit/kitkat/) is
derived from the boot-verified [maple tree](../maple/README.md). Its native
sources (`egl/`, `gralloc/`, `audio/`, `lights/`, `hyb/`, `fbpresentd/`,
`pdserve/`, `rtc-restore/`) are also compiled for the
[chiron port](../chiron/README.md). Board artifacts are named after the
**SoC, not the phone**: both `BoardConfig.mk`s pass
`androidboot.hardware=msm8998`, so the ramdisk carries
`init.msm8998.rc`, `init.msm8998.usb.rc`, `ueventd.msm8998.rc` and
`fstab.msm8998`, `/system/lib/hw` carries `gralloc.msm8998.so`,
`lights.msm8998.so` and `audio.primary.msm8998.so`, and the EGL shim dlopens
`/system/lib/hw/gralloc.msm8998.so`; one source builds both phones. The
MIUI packs keep the ROM's own per-device names — the pack scripts install
these files under `gralloc.chiron.so`, `lights.sagit.so`, … as needed.
`TARGET_BOARD_PLATFORM` deliberately stays the board name (it becomes
`ro.product.board`, the second `hw_get_module` key, and KitKat's qcom HAL
tree has no `msm8998` directory). The
Mi-6-specific hardware facts (by-name layout, partition sizes, panel,
touch, LEDs, ACDB directory, card name, RTC) were **measured on the phone
on 2026-10-24** with `scripts/dump-sagit-stock.sh --su` and are recorded in
[quirks.md](quirks.md); the KitKat image has since booted and been tested
on the phone (see [Status](#status)).

## Status

### KitKat 4.4.4 — AOSP `android-4.4.4_r2` (KTU84Q) — boots

| Area | State |
| --- | --- |
| Boot | Boots (4.4.247 kernel + KitKat system image, verified on the phone 2026-10-24). Stock by-name exists at `/dev/block/by-name`, `/dev/block/bootdevice/by-name` and `.../1da4000.ufshc/by-name`; KitKat's ueventd decides the final layout, so both mount stanzas are kept |
| Boot image | `TARGET_PREBUILT_KERNEL := vendor/xiaomi/sagit/kernel`; since 2026-10-27 that blob is the **LineageOS 17.1 KernelSU `Image.gz-dtb`** (`anykernel.sh` says `device.name1=sagit`; zip member 15774877 B, md5 `f32b9017cfa4b8a441bcae49a6758453`), swapped in for the dumped stock kernel because chiron's boot image with its own stock kernel reboot-looped until exactly this swap. `scripts/patch-sagit-dtb.py` still has to run after staging: it found both properties in all 6 appended DTBs (15774877 → 15774349 B). The previous stock blob is kept at `/home/juezhi/imgs/sagit-kernel-ba7ca7ab188114335ef58b217285ae61.bin` |
| Display / GPU | libhyb + `fbpresentd` copied from maple; measured panel is JDI r63452 (`1080x1920p-60`, virtual `1080,3840`), density 440; `gralloc.msm8998`/EGL shims expect `fb0` |
| Touch | **`synaptics_dsx`** (`synaptics_dsx.idc`, ABS 0..1079 × 0..1919, `INPUT_PROP_DIRECT`); back/menu/home are touch keys through `Generic.kl`, only volume is in `gpio-keys.kl` |
| USB | configfs gadget (`init.msm8998.usb.rc`), UDC `a800000.dwc3` auto-detected by `usbudc.sh` |
| Audio | tasha (WCD9341) card `msm8998-tasha-snd-card`, tinyalsa HAL + libhyb `libacdbloader`; ACDB blocks install to `/system/vendor/etc/acdbdata/Forte` (the loader hardcodes `Forte`; `hw_platform` says `SAGIT`). Speaker playback verified 2026-10-24: the loudspeaker is the **TFA9888 on QUAT_MI2S (I2S)**, not the codec — see [quirks.md](quirks.md#audio-speaker-verified-2026-10-24); headphones stay on WCD9335 (`SLIMBUS_0_RX`) |
| WiFi | The chiron bring-up stack is the single source of truth and is shared unchanged: `/sbin/{wf-setup,wf-efs-keeper,wf-wifi-gate,rfs-dirs,modem-hold}.sh` in the boot ramdisk, the `wf-*` services in `init.msm8998.rc`, and — from `full_sagit.mk` — `sec_config`, the BDF name variants, `firmware/image/wlanmdsp.mbn`, `/system/bin/{wlq,rfssrv2,sm-vndk}`, the 64-bit vendor daemon+library closure **and `/system/bin/linker64`** (that last one is what made a clean chiron image fail: every QC daemon is aarch64 with `PT_INTERP /system/bin/linker64` and this tree builds arm-only, so without it `exec()` reports "No such file or directory"). Same WCN3990, same icnss, same UFS by-name layout, so nothing is sagit-specific; **proven on chiron, not yet run on a Mi 6**. The stack is also carried into the
MIUI V5 (`cancro` 4.4.2) images on this device — that ROM ships a
`libhardware_legacy.so` built with `WIFI_DRIVER_MODULE_PATH`, so its
`wifi_load_driver()` insmods a msm8974 `wlan.ko` that can never load here and the
toggle refuses to turn on; the V5 pack swaps in this port's AOSP library (which
also moves the vibrator off the Mi 3 Immersion engine). Write-up:
[chiron wifi-progress.md](../chiron/wifi-progress.md) §12 |
| Storage | internal emulated only (no microSD); no `/vendor` partition on the stock layout — the hybrid root lives at `/system/a15` |
| Lights | `lcd-backlight` (no `/sys/class/backlight`) + `wled`, notification `white` LED with `blink` — all measured |
| Cellular | Shared msm8998 QMUX bridge and matched 32-bit QCRIL stack with chiron (`devices/sagit/kitkat/ril/`); both AOSP products use the same source, staging and init configuration. Baseband query + Radio On were verified on **chiron only**, manually launched 2026-10-28; the subsequent flashed chiron image restarts RIL. Both products now use a native launcher to replace KitKat init's ineffective library-path `setenv` without losing the hyphenated debug-socket variable in mksh; automatic startup remains unverified. SIM errors and all cellular services remain open. Not hardware-verified on sagit. See [chiron control-plane notes](../chiron/README.md#cellular-control-plane-2026-10-28). |
| Bluetooth | WCN3990 ("cherokee") bring-up is entirely userspace, because the booted kernel only supplies power: `devices/sagit/kitkat/bt/libbt-vendor` builds `libbt-vendor` (shared unchanged with chiron) and drives EDL → 3 Mbaud → rampatch + NVM TLV → HCI Reset over `/dev/ttyHS0`, while `ueventd.msm8998.rc` gives `/dev/btpower` + `/dev/ttyHS0` to `bluetooth:net_bt 0660` and `full_sagit.mk` installs `crbtfw21.tlv`/`crnv21.bin` to `/system/etc/firmware/bt/image`. Adapter turns ON and survives repeated off/on cycles — hardware-verified 2026-10-29, on chiron. No `qca,bt-reset-gpio` exists in any msm8998 DTS, so nothing but a real power-off returns the chip to ROM mode; see [bluetooth.md](bluetooth.md) |
| Camera / GPS / fingerprint | Not started |

Open work and the Mi-6-vs-maple deltas are tracked in
[quirks.md](quirks.md).

### MIUI 4.1.2 (`mione_plus`) — images built, not booted

A side port: the Mi-One-Plus MIUI V5 OTA (`4.11.28`, SDK 16, 32-bit)
repacked as a 1 GiB ext4 `system.img`, plus a `boot.img` assembled from
this port's 4.4.247 kernel and ramdisk with a Jelly Bean boot classpath
and device hook. Not booted yet; display HALs are the first blocker.
Full write-up: [miui-jb.md](miui-jb.md), build script
`scripts/build-sagit-jb-boot.py`.

### MIUI V7 4.4.4 (`cancro`) — system.img packed, not booted

The Mi 3W MIUI V7 package (`5.12.17`, SDK 19) — the same Android release
as this port, so the KitKat ramdisk/boot.img are reused unchanged and only
a repacked 1 GiB ext4 `system.img` is needed
(`C:\download\sagit\system_miui7.img`, `config/miui7-pack.sh`). The
ROM's MSM8974 HALs still have to be swapped for this port's 4.4/msm8998
closure before display/audio work. Full write-up:
[miui-v7-kitkat.md](miui-v7-kitkat.md).

## Files you must provide (and where they go)

Everything below is local build input, **never committed** (vendor blobs
are gitignored). Paths are relative to the AOSP root (`/home/juezhi/aosp-kitkat`).

| What | Where it goes | How |
| --- | --- | --- |
| 4.4 kernel blob (`Image.gz-dtb`) | `vendor/xiaomi/sagit/kernel` | currently the `Image.gz-dtb` member of `C:\mCloudDownload\lineage17.1_ksu.zip` (AnyKernel3, `device.name1=sagit`); the stock route (`scripts/dump-sagit-stock.sh --su --pull-boot` + split the gzip/DTB) still works if you want Xiaomi's own kernel. Either way: `scripts/patch-sagit-dtb.py vendor/xiaomi/sagit/kernel` |
| LineageOS 22.1 (Android 15) `sagit` install zip | keep anywhere, e.g. `~/lineage-22.1-sagit.zip` | download from the LineageOS `sagit` builds; used only as the vendor closure source |
| Android 15 vendor closure (`/system/a15`) | `vendor/xiaomi/sagit-hyb/root/` + `vendor/xiaomi/sagit-hyb/acdbdata/Forte/` | `tools/libhyb/extract-lineage.sh ~/lineage-22.1-sagit.zip /tmp/sagit-a15` then `tools/libhyb/stage.py /tmp/sagit-a15 vendor/xiaomi/sagit-hyb --acdb-name=Forte` |
| 64-bit `fbpresentd` | `vendor/xiaomi/sagit-bin/bin/fbpresentd` | `tools/libhyb/build-native.sh /home/juezhi/aosp-kitkat xiaomi/sagit` (needs the NDK path it expects) |
| WLAN static binaries (`wlq`, `rfssrv2`, `sm-vndk`) | `vendor/xiaomi/sagit-bin/bin/` | `devices/chiron/kitkat/wifi-tools/build-wf-bins.sh` (linaro aarch64 `-static`; `sm-vndk` needs one AOSP build first because it patches `out/.../system/bin/servicemanager`) |
| 64-bit Qualcomm WLAN daemons + their libraries | `vendor/xiaomi/sagit-wifi/{bin,lib64,system-lib64}/` | `devices/chiron/kitkat/wifi-tools/stage-wifi-vnd.sh`, run from Windows against a sagit/chiron phone still on its stock firmware (`adb pull` only; a KitKat phone cannot bootstrap this) |
| Shared 32-bit QCRIL closure | `vendor/xiaomi/sagit-ril/{bin,lib}/` | `python3 devices/sagit/kitkat/ril/stage-ril.py <tested-flat-ril-directory> <aosp-root>`; verifies every blob against `ril/blobs.sha256`, including the matched service descriptors and patched QMUX client. Required by both AOSP products. |
| Modem / ADSP / WLAN / BT firmware | `vendor/xiaomi/sagit-fw/{modem,adsp,wlan,bt}/` | copy from the device's `modem`+`bluetooth` partitions, `persist`, and `/vendor/firmware` on a rooted Lineage 22.1 install (the exact file names `init.msm8998.rc`/`ueventd.msm8998.rc` need are listed there) |
| `pdserve`/PDR mapping (if the ROM's `pdc` data differs) | `vendor/xiaomi/sagit-fw/` or the in-tree `pdserve` service args | compare `vendor/etc/pd-mappings` from the ROM; maple's mapping is in `devices/maple/kitkat/` |
| Stock-facts dump | `dumps/sagit-stock-<date>/` | `scripts/dump-sagit-stock.sh --su` (`--root` only if adbd allows `adb root`; this ROM is a production build and needs the `su` path) |
| Optional: stock MiUI / Lineage `system` images | `dumps/` | only for extracting HAL configs (mixer paths, `audio_policy`, acdb layout) |

Minimum to reach a first boot attempt: kernel blob, hybrid root,
`fbpresentd`, firmware, and the dump. The rest can follow.

## Documentation and links

- [quirks.md](quirks.md) — Mi 6 hardware deltas, VERIFY-ME checklist, open blockers
- [miui-jb.md](miui-jb.md) — the MIUI 4.1.2 side port (system.img + boot.img, open gaps)
- [miui-v7-kitkat.md](miui-v7-kitkat.md) — the MIUI V7 4.4.4 side port (system.img only, KitKat boot reused)
- [maple device page](../maple/README.md) — the proven MSM8998/KitKat port this tree derives from
- [KitKat porting notes](../../advanced/porting-notes/kitkat.md), [libhyb](../../advanced/software/libhyb.md)
