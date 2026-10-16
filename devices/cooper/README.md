# cooper (Samsung Galaxy Ace GT-S5830) ports

## donut/ — Donut 1.6 (DRD35) device tree

Drop-in for `aosp-donut/vendor/samsung/cooper/` (`lunch full_cooper-eng`).
Contents:

- `BoardConfig.mk`, `cooper.mk`, `AndroidProducts.mk`, `AndroidBoard.mk` —
  build config (BML geometry `0x13600000`/4096, platform `cooper`,
  `USE_CAMERA_STUB=false`, `TARGET_PROVIDES_INIT_RC`).
- `kernel`, `modules/` — CM kernel (`2.6.35.7-perf-CL882825`) + FSR/STL
  modules (fallback); `kernel-twrp`, `modules-twrp/` — TWRP/Omni kernel
  (`2.6.35.7-androidarmv6+`), the pair actually shipped.
- `init.rc`, `init.gt-s5830.rc`, `ueventd.gt-s5830.rc` — Donut init with
  stl12/13/14 mounts, STL volume-scan bringup, ath6kl pre-insmod.
- `libaudio/` — cooper `libaudio.so` (msm7k audio minus HTC acoustic dep;
  40-byte kernel `msm_audio_config` fix). Speaker audio works.
- `libcamera/` — CooperCamera V4L2/test-pattern HAL (`toggleshutter` in
  `camerashutter/`).
- `vtouch/` — Synaptics RMI4 type-A MT → single-touch uinput proxy
  (touchscreen on pre-multitouch Donut).
- `proprietary/` — device blobs used by the build.
- keymaps (`qwerty.kl`, `sec_key.kl`, `sec_jack.kl`, `sec_touchscreen.kl`,
  `cooper_vtouch.kl`), `system.prop` (160 dpi, `keyguard.no_require_sim`),
  `vold.fstab`, `egl.cfg`, `wpa_supplicant.conf`.

Sync into a checkout:

```sh
scripts/sync-device-tree.sh cooper donut        # -> $AOSP_DIR/vendor/samsung/cooper
```

Build (inside the Hardy chroot): `lunch full_cooper-eng && make -j8`.
Package: `scripts/build-cooper-donut-full.py` (or `...-skeleton.py` for the
small preview zip). Device page and quirks:
[`docs/devices/cooper/`](../../docs/devices/cooper/README.md); build guide:
[`docs/advanced/build/donut.md`](../../docs/advanced/build/donut.md).
