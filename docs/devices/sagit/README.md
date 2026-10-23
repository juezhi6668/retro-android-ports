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
derived from the boot-verified [maple tree](../maple/README.md) and is a
**skeleton**: every Mi-6-specific fact that needs the phone in hand is
marked `VERIFY-ME` and must be confirmed from
`scripts/dump-sagit-stock.sh` before the first flash. Nothing here has
been built or booted on hardware yet.

## Status

### KitKat 4.4.4 — AOSP `android-4.4.4_r2` (KTU84Q) — skeleton only

| Area | State |
| --- | --- |
| Boot | Not booted. Mounts are written to try stock (`/dev/block/by-name`) and UFS (`.../1da4000.ufshc/by-name`) paths; confirm from the dump and delete the losing stanzas |
| Boot image | `TARGET_PREBUILT_KERNEL := vendor/xiaomi/sagit/kernel` (dumped stock `Image.gz-dtb`); run `scripts/patch-sagit-dtb.py` on it first |
| Display / GPU | libhyb + `fbpresentd` copied from maple; panel timings come from the DTBs, but the panel node names are Xiaomi's — `gralloc.sagit`/EGL shims expect `fb0` |
| Touch | Synaptics DSX (`synaptics_dsx.idc`, name must equal the input device name from `getevent -p`); back/menu/home are expected as touch keys through `Generic.kl`, only volume keys are in `gpio-keys.kl` |
| USB | configfs gadget (`init.sagit.usb.rc`), UDC auto-detected by `usbudc.sh` |
| Audio | tasha (WCD9341) card `msm8998-tasha-snd-card`, tinyalsa HAL + libhyb `libacdbloader`; ACDB blocks staged to `/system/vendor/etc/acdbdata/MTP` (check `hw_platform`) |
| WiFi | qca_cld3/qcwcn plumbing copied from maple; modem PIL firmware from the `modem` partition; the maplemodem PD keepalive problem (see [maple quirks](../maple/quirks.md)) will apply |
| Storage | internal emulated only (no microSD); no `/vendor` partition on the stock layout — the hybrid root lives at `/system/a15` |
| Lights | backlight `lcd-backlight` + `wled`, notification `white` LED — node names from the dump |
| Cellular / Camera / GPS / BT / fingerprint | Not started |

Open work and the Mi-6-vs-maple deltas are tracked in
[quirks.md](quirks.md).

## Files you must provide (and where they go)

Everything below is local build input, **never committed** (vendor blobs
are gitignored). Paths are relative to the AOSP root (`/home/juezhi/aosp-kitkat`).

| What | Where it goes | How |
| --- | --- | --- |
| Stock 4.4 kernel blob (`Image.gz-dtb`) | `vendor/xiaomi/sagit/kernel` | `scripts/dump-sagit-stock.sh --root --pull-boot` (or pull `boot` in TWRP and split the gzip/DTB); then `scripts/patch-sagit-dtb.py vendor/xiaomi/sagit/kernel` |
| LineageOS 22.1 (Android 15) `sagit` install zip | keep anywhere, e.g. `~/lineage-22.1-sagit.zip` | download from the LineageOS `sagit` builds; used only as the vendor closure source |
| Android 15 vendor closure (`/system/a15`) | `vendor/xiaomi/sagit-hyb/root/` + `vendor/xiaomi/sagit-hyb/acdbdata/MTP/` | `tools/libhyb/extract-lineage.sh ~/lineage-22.1-sagit.zip /tmp/sagit-a15` then `tools/libhyb/stage.py /tmp/sagit-a15 vendor/xiaomi/sagit-hyb` |
| 64-bit `fbpresentd` | `vendor/xiaomi/sagit-bin/fbpresentd` | `tools/libhyb/build-native.sh /home/juezhi/aosp-kitkat xiaomi/sagit` (needs the NDK path it expects) |
| Modem / ADSP / WLAN / BT firmware | `vendor/xiaomi/sagit-fw/{modem,adsp,wlan,bt}/` | copy from the device's `modem`+`bluetooth` partitions, `persist`, and `/vendor/firmware` on a rooted Lineage 22.1 install (the exact file names `init.sagit.rc`/`ueventd.sagit.rc` need are listed there) |
| `pdserve`/PDR mapping (if the ROM's `pdc` data differs) | `vendor/xiaomi/sagit-fw/` or the in-tree `pdserve` service args | compare `vendor/etc/pd-mappings` from the ROM; maple's mapping is in `devices/maple/kitkat/` |
| Stock-facts dump | `dumps/sagit-stock-<date>/` | `scripts/dump-sagit-stock.sh --root` — then fix every `VERIFY-ME` from it |
| Optional: stock MiUI / Lineage `system` images | `dumps/` | only for extracting HAL configs (mixer paths, `audio_policy`, acdb layout) |

Minimum to reach a first boot attempt: kernel blob, hybrid root,
`fbpresentd`, firmware, and the dump. The rest can follow.

## Documentation and links

- [quirks.md](quirks.md) — Mi 6 hardware deltas, VERIFY-ME checklist, open blockers
- [maple device page](../maple/README.md) — the proven MSM8998/KitKat port this tree derives from
- [KitKat porting notes](../../advanced/porting-notes/kitkat.md), [libhyb](../../advanced/software/libhyb.md)
