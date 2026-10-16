# poplar (Sony Xperia XZ1 G8341/G8342/G8343) ports

## kitkat/ — KitKat 4.4.4 (KTU84Q) device tree (BOOTS + HW GL — Oct 2026)

Drop-in target for `aosp-kitkat/vendor/sony/poplar/` (`lunch full_poplar-eng`).
Same platform (yoshino/MSM8998) as [`maple`](../maple/README.md) — poplar is the tree
maple's was derived from, so the two are near-identical on purpose.

Contents:

- `BoardConfig.mk`, `full_poplar.mk`, `AndroidProducts.mk`, `AndroidBoard.mk` — build
  config (GPT/ext4 layout, boot base `0x00000000`/4096, `TARGET_PREBUILT_KERNEL`
  (dumped blob, out of git), `TARGET_BOARD_PLATFORM := poplar` to dodge KitKat's
  msm8960/8974 HALs, `phone-xhdpi-2048` Dalvik heap inherit, `WITH_DEXPREOPT`).
- `system.prop` — measured density 430, wifi/model/brand, GLES 2.0 report.
- `fstab.poplar` — GPT/ext4 by raw node (system→sda65, userdata→sda66, cache→sda53,
  persist→sda33; the by-name links live under `/dev/block/platform/soc/by-name/`,
  which is what KitKat's ueventd creates). The mounts themselves are in
  `init.poplar.rc`, not here.
- `init.rc` (stock + single `${ro.hardware}` import), `init.poplar.rc` (GPT mounts by
  raw node with pre-created dirs, `setenforce 0`, renderer start, displaytune +
  boot-log services), `init.poplar.usb.rc` + `usbudc.sh` (DWC3 adb gadget — split into
  one action per step, because a failing init command aborts its whole action
  silently), `ueventd.poplar.rc`.
- `lights/` — the `lights.poplar` HAL. Without a lights module the framework never
  blanks the panel and brightness does nothing (no lights HAL = no sleep and no wake).
  **Not present in maple's tree.**
- `displaytune.sh` — one-shot display fixups at boot, replacing the old keepawake
  workaround that made "screen off" come straight back on. **Not present in maple's
  tree.**
- `glrenderd.sh`, `clearpad.idc`, `gpio-keys.kl`, `autowinch.sh`, `dump-boot-log.sh`,
  overlay (emulated storage list, nav bar, density).

Reference (not in git, same policy as cooper blobs): Sony
`android_device_sony_poplar` (`ten` branch) + `yoshino-common`
`BoardConfigPlatform.mk` (msm8998, base 0/pagesize 4096, boot 64 MB), kernel
`sony/msm8998` (4.4, `Image.gz-dtb`).

Sync into a checkout — `scripts/sync-device-tree.sh` has no `poplar` case yet, so this
is what it would run:

```sh
rsync -a --delete devices/poplar/kitkat/ "$AOSP_DIR/vendor/sony/poplar/"
```

Build (inside the KitKat container/chroot): `lunch full_poplar-eng && make -j$(nproc)`.
Device history, specs and blockers:
[`docs/devices/poplar.md`](../../docs/devices/poplar.md).
KitKat playbook: [`docs/porting-notes-kitkat.md`](../../docs/porting-notes-kitkat.md);
GPU fixes: [`docs/kitkat-gpu-fixes.md`](../../docs/kitkat-gpu-fixes.md).

### poplar vs maple

Same tree shape. What differs:

| | poplar | maple |
| --- | --- | --- |
| panel | 5.2" 1080x1920 command-mode, native | 5.5" 3840x2160 4K HDR, rendered at 1080p |
| boot / system | `sda38` / `sda65` | `sde35` / `sde46` |
| userdata / cache / persist | `sda66` / `sda53` / `sda33` | `sda15` / `sda5` / `sda4` |
| density | 430 | 410 |
| extras | `lights/`, `displaytune.sh` | — |

poplar's `usbudc.sh` also (re)builds the gadget rather than only binding the UDC, and
its `init.*.usb.rc` splits the gadget bring-up into separate actions. A fix on one
device is a candidate fix for the other — verify by readback.
