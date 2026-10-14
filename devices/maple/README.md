# maple (Sony Xperia XZ Premium G8141/G8142) ports

## kitkat/ — KitKat 4.4.4 (KTU84Q) device tree (skeleton, NOT booting yet)

Drop-in target for `aosp-kitkat/vendor/sony/maple/` (`lunch full_maple-eng`).

Contents (skeleton):

- `BoardConfig.mk`, `full_maple.mk`, `AndroidProducts.mk`, `AndroidBoard.mk` —
  build config (GPT/ext4 layout, boot base `0x00000000`/4096,
  `TARGET_PREBUILT_KERNEL` + `Image.gz-dtb` note, `TARGET_BOARD_PLATFORM`
  discussion — see below).
- `system.prop` — density/wifi/model skeleton with TODOs.
- `fstab.maple` — partition skeleton (boot 64 MB, system ~5 GB; block
  paths from TWRP `fstab_maple.recovery`: `/dev/block/bootdevice/by-name/*`).
- `init.maple.rc`, `ueventd.maple.rc` — ramdisk init skeletons (GPT mounts
  + DWC3 adb gadget + node ownership; KitKat `init.rc` itself still TODO).

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
