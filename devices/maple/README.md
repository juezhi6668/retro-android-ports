# maple (Sony Xperia XZ Premium G8141/G8142) ports

## kitkat/ — KitKat 4.4.4 (KTU84Q) device tree (BOOTS — Oct 2026)

Drop-in target for `aosp-kitkat/vendor/sony/maple/` (`lunch full_maple-eng`).

Contents:

- `BoardConfig.mk`, `full_maple.mk`, `AndroidProducts.mk`, `AndroidBoard.mk` —
  build config (GPT/ext4 layout, boot base `0x00000000`/4096,
  `TARGET_PREBUILT_KERNEL` (TWRP-dumped blob, out of git),
  `TARGET_BOARD_PLATFORM := maple` to dodge KitKat's msm8960/8974 HALs,
  `phone-xhdpi-2048` Dalvik heap inherit, `WITH_DEXPREOPT`).
- `system.prop` — measured density 410, wifi/model/brand, GLES 2.0 report.
- `fstab.maple` — GPT/ext4 (system→sde46, cache→sda5, userdata→sda15,
  persist→sda4; by-name under `/dev/block/platform/soc/by-name/`, which
  is what KitKat's ueventd actually creates).
- `init.rc` (stock + single `${ro.hardware}` import), `init.maple.rc`
  (GPT mounts by raw node with pre-created dirs, setenforce 0, configfs
  USB gadget safety nets), `init.maple.usb.rc` + `usbudc.sh` (DWC3 adb
  gadget bringup), `ueventd.maple.rc` (node ownership).
- `glrenderd.sh`, `clearpad.idc`, `gpio-keys.kl`, overlay (emulated
  storage list, nav bar, density) — see `docs/devices/maple.md` for
  what works (touch/display/adb) and what's stub (glrenderd off,
  wifi/BT/RIL/camera).

Reference (not in git, same policy as cooper blobs): Sony
`android_device_sony_maple` (`ten` branch) + `yoshino-common`
`BoardConfigPlatform.mk` (msm8998, base 0/pagesize 4096, boot 64 MB),
kernel `sony/msm8998` (4.4, `Image.gz-dtb`).

Sync into a checkout:

```sh
scripts/sync-device-tree.sh maple kitkat  # -> $AOSP_DIR/vendor/sony/maple
# or: AOSP_DIR=$ROOT/aosp-kitkat scripts/sync-device-tree.sh maple kitkat
```

Build (once filled in, inside the KitKat container/chroot):
`lunch full_maple-eng && make -j$(nproc)`.
Device history, specs, and blockers:
[`docs/devices/maple.md`](../../docs/devices/maple.md).
KitKat playbook: [`docs/porting-notes-kitkat.md`](../../docs/porting-notes-kitkat.md).

### Why `TARGET_BOARD_PLATFORM` is still open

cooper used `TARGET_BOARD_PLATFORM := cooper` to keep Donut's Dream-era
`hardware/msm7k` HALs out of the build. maple needs the same decision for
KitKat's `hardware/qcom/*` (msm8960/msm8974-era) vs msm8998: do NOT set
`msm8998` until a QCOM HAL path is proven — the skeleton sets `maple` and
uses generic audio/camera/gps until each HAL is ported. See
`BoardConfig.mk` comments.
