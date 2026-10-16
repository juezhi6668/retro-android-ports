# KitKat 4.4.4 on the Sony Xperia XZ1 (poplar)

Device-specific notes for the poplar port. KitKat playbook:
[`docs/porting-notes-kitkat.md`](../porting-notes-kitkat.md); Donut/cooper method
reference: [`docs/porting-notes.md`](../porting-notes.md) +
[`docs/devices/cooper.md`](cooper.md); sibling device: [`maple.md`](maple.md).

Status: **BOOTED, daily-drivable (Oct 2026)** — full KitKat userspace on the Sony 4.4
kernel: `sys.boot_completed=1`, launcher + nav bar at 1080x1920, touch, USB adb, and
hardware GL on the Adreno 540 via `glrenderd`
([`docs/kitkat-gpu-fixes.md`](../kitkat-gpu-fixes.md)). Open: no
cellular/audio/camera/GPS/WiFi/BT, and the renderer still stalls under sustained load.

## Hardware

- MSM8998 (SD835, Kryo 280 ARM64) + Adreno 540, 4 GB LPDDR4X, 64 GB UFS, microSDXC.
- 5.2" 1080x1920 IPS LCD, ~424 ppi, single-DSI **command-mode** panel (`dsi_cmd_mode`,
  4 lanes, `non_burst_sync_event`).
- Stock: Oreo 8.0 -> Pie 9. Reference: `sony-msm8998/android_device_sony_poplar` (10/11)
  + `yoshino-common`, 4.4 kernel (`Image.gz-dtb`).
- `poplar` (G8341) / `poplar_dsds` (G8342); carrier variant `poplar-kddi` (SOV36) — the DT
  reports `SoMC Poplar-KDDI(MSM8998 v2.1)`, `somc,poplar-kddi`.

## poplar vs maple

Same platform, same kernel, same init wiring — porting deltas only:

| | poplar | maple |
| --- | --- | --- |
| panel | 5.2" 1080x1920 IPS | 5.5" 3840x2160 4K HDR (renders 1080p) |
| panel nodes | `somc,9_panel` (active) + `somc,6_panel` | `somc,6_panel` family |
| fb stride | 4352 B (1088 px) | 4352 B (1088 px) |
| boot | `sda38` | `sde35` |
| system / userdata | `sda65` / `sda66` | `sde46` / `sda15` |
| cache / persist | `sda53` / `sda33` | `sda5` / `sda4` |
| density | 430 | 410 |

Everything else (`BOARD_KERNEL_BASE 0`, pagesize 4096, `Image.gz-dtb`, GPT/ext4,
`TARGET_BOARD_PLATFORM := <device>` not `msm8998`, configfs USB gadget, `lights` HAL,
`@glrenderd`) is identical. A fix on one is a candidate fix for the other — verify by
readback.

## Platform

- boot/recovery 64 MB, `BOARD_FLASH_BLOCK_SIZE 131072`, ext4 everywhere
  (`TARGET_USERIMAGES_USE_EXT4`, or KitKat silently builds a yaffs2 system.img). Fastboot
  `flash boot`; no BML dance. **Verify every flash by readback** — this bootloader has no
  `fastboot fetch`.
- Block map by raw node under `/dev/block/platform/soc/by-name/` (KitKat ueventd has no
  `/dev/block/bootdevice/`): boot `sda38`, system `sda65`, userdata `sda66`, cache
  `sda53`, persist `sda33` (vendor/oem `sda64`/`sda63`, unmounted).
- Kernel is a **prebuilt 4.4 blob** (`device/sony/poplar/kernel`, 15,828,088 B,
  `Image.gz-dtb`), dumped from the boot partition, kept out of git. No kernel rebuild was
  needed for anything here. **No `CONFIG_ANDROID_LOGGER` -> `logcat` unusable**; logging is
  file-based (`/dev/kmsg`, `/data/misc`, dropbox, tombstones, `dump-boot-log.sh`).
- Panel: `somc,9_panel` selected by `somc,lcd-id-adc` (`panel_name=9`, `lcdid_adc=228`);
  both panels are 1080x1920@60. `fb0` stride is **4352 B (1088 px)** — gralloc must report
  the kernel stride, not 1080, or every row shears 8 px (maple TEST 28). No DTB/driver work
  needed for basic display. `somc,change-fps-*` is Sony's panel retiming hook, not a
  refresh-rate knob — the driver accepting a value is not evidence the panel changed.

## init wiring

All of it lives in the boot image (`make bootimage`) — none of it is `adb push`-able.

- **A failing init command aborts its whole action, silently** — hence one mount per
  `on init`.
- `mkdir` the mountpoint first: mounting onto a missing dir fails `ENOENT` silently, and
  stock init.rc creates neither `/cache` nor `/persist`.
- KitKat mount syntax: space-separated flags, **one** trailing options arg. No
  `wait_for_coldboot_done` (not a KitKat command — aborts the action).
- `import` the device rc exactly once (a second import double-parses it and spams
  "ignored duplicate" for every service).
- `setenforce 0` for bringup — enforcing gives denial storms (once an audit-backlog D-state
  storm that pinned load for minutes with no CPU progress).
- `class main`, not `late_start`, when mounts are by hand. `chmod 0755` scripts copied by
  `PRODUCT_COPY_FILES` (they land 644 and init will not run them).
- adb: `import /init.poplar.usb.rc` — without it `usbudc` is unknown to init and there is
  **no USB at all**; `usbudc.sh` builds the configfs gadget + ffs, does
  `ctl.stop/start adbd` so adbd picks ffs up, waits before binding the UDC (else the host
  sees code 43), and re-binds on a loop (KitKat adbd closes ep0 on disconnect, unbinding
  the UDC); `start usbudc` `on boot`, **not** via `on property:sys.usb.config=adb`.
- renderer: `write /sys/module/firmware_class/parameters/path /vendor/firmware` then
  `start glrenderd` in `post-fs-data` — renderer before framework, one flipper only.

## Display — brightness and sleep

- The stock tree had **no `lights.*` HAL**, so the framework never blanked the panel and
  brightness did nothing. `lights/lights.c` -> `lights.poplar` writes
  `/sys/class/leds/lcd-backlight/brightness`. No lights HAL = no sleep and no wake.
- Don't paper over it with `stay_on_while_plugged_in=3` + a keep-awake timer — that gives
  "screen off doesn't stay off". Use a one-shot `displaytune.sh` at boot, no timer.
- `::send(..., 0)` raises `SIGPIPE`, which killed SurfaceFlinger -> `MSG_NOSIGNAL`.

## Input

- touch: `clearpad` emits nothing until `post_probe_start` is written from init
  (`post-fs-data`), as Lineage does. It is easy to miss — the input device registers and
  shows in `/proc/bus/input/devices` the whole time. Full-range firmware coords map 1:1.
  `clearpad.idc` + `gpio-keys.kl` (identical to maple).
- dispatch: a dead renderer freezes composition, and a stuck `InputDispatcher`
  (`DispatchFrozen: 1`) is a second, independent freeze — 10 s self-thaw, see
  [`docs/kitkat-gpu-fixes.md`](../kitkat-gpu-fixes.md).

## Verified on device

```
ro.product.device poplar   ro.product.model G8341   ro.product.name full_poplar
ro.sf.lcd_density 430      ro.opengles.version 131072   ro.hardware poplar
ro.build.version.sdk 19    ro.build.id KTU84Q           ro.boot.verifiedbootstate orange
fb0 modes U:1080x1920p-60  fb0 stride 4352
```

## Quirks

- KitKat toolbox lacks `head`/`tail`/`wc`/`sort`/`pkill`/`pidof` — push a static busybox.
- toolbox `grep` piped into an early-exiting reader SIGPIPEs into tombstones — write to a
  file instead.
- Device clock reads 1972, so dropbox timestamps are useless; `/proc/<pid>/stat` field 22
  (start ticks) is the reliable identity for when a process started.
- `adb pull` cannot read a block device — `dd` it to a file, then pull.

## Flash verification

```sh
adb shell /data/local/tmp/busybox dd if=/dev/block/platform/soc/by-name/boot \
    of=/data/local/tmp/b.img bs=4096 count=3947      # 16166912 B = one image
adb shell /data/local/tmp/busybox md5sum /data/local/tmp/b.img   # == the flashed boot.img
```
