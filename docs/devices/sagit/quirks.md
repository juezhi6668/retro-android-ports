# sagit quirks (Xiaomi Mi 6)

Mi-6-specific deltas from the boot-verified maple port, the VERIFY-ME
checklist, and the open blockers. Status and file manifest live on the
[device page](README.md); the generic KitKat playbook is the
[KitKat porting notes](../../advanced/porting-notes/kitkat.md) and the
[maple quirks](../maple/quirks.md) are the reference bringup log.

Current state: **skeleton, never built or booted**. Nothing in this file
is a verified fix yet — treat every section as the hypothesis the dump
has to confirm.

## Hardware vs maple

| | maple (XZ Premium) | sagit (Mi 6) |
| --- | --- | --- |
| Panel | 5.5" 3840×2160 (1080p UI) | 5.15" 1080×1920, 480 dpi; **two panel suppliers** (JDI r63452, LGD td4322) — DTBs differ |
| Storage | 64 GB UFS + microSD | 64/128 GB UFS, **no microSD** → no sdcard1/vold row, emulated storage only |
| Touch | Synaptics Clearpad | **Synaptics DSX** (`synaptics_dsx.idc`); back/menu/home arrive as touch keys |
| Keys | gpio keys incl. camera key | only volume keys on gpio; power from pmic; no camera key |
| Audio codec | tasha or tavil depending on unit | **tasha (WCD9341)** always |
| LED | RGB notification pattern? (maple uses multi-led) | single **`white`** notification LED |
| Fingerprint | side reader | front FPC reader (not started) |
| Partitions | dedicated `/vendor` | **no vendor partition** (TWRP maps `/vendor` to `cust`); hybrid root must live in `/system/a15` |
| Bootloader | Sony unlock flow | Xiaomi unlock; TWRP is the flashing vehicle |

## VERIFY-ME checklist (from `scripts/dump-sagit-stock.sh`)

1. **by-name paths.** `by-name.txt` / `by-name-bootdevice.txt` /
   `by-name-soc.txt`. `fstab.sagit` and the `on init` mount stanzas try
   `/dev/block/by-name/...` and `/dev/block/platform/soc/1da4000.ufshc/by-name/...`;
   keep the one that resolves and delete the other when it is known.
2. **Partition sizes.** `BoardConfig.mk` has `BOARD_SYSTEMIMAGE_PARTITION_SIZE`
   = 3 GiB and `BOARD_CACHEIMAGE_PARTITION_SIZE` = 256 MiB marked verify-me.
   Take the real sizes from `cat /proc/partitions` (blocks × 1024) — an
   oversized system image simply fails to flash.
3. **Kernel cmdline.** Stock `cmdline.txt` vs `BOARD_KERNEL_CMDLINE`:
   confirm `androidboot.configfs=true`, the `a800000.dwc3` controller name,
   and whether `androidboot.hardware` is already `sagit`.
4. **Touch input name.** `getevent -p` — the `.idc` filename **must** equal
   the device name (e.g. `synaptics_dsx.idc`); also copy the real axis
   ranges into the idc if they are not 0..1079/0..1919.
5. **LED / backlight nodes.** `leds.txt`, `backlight-dir.txt`,
   `leds-white-brightness.txt`. `lights.c` writes `lcd-backlight` +
   `wled` for brightness and `/sys/class/leds/white/brightness` (+ `blink`)
   for notifications; fix names if the dump disagrees.
6. **hw_platform.** `hw-platform.txt` — `libacdbloader` reads
   `/vendor/etc/acdbdata/<hw_platform>/`, and the tree installs the blocks
   under `MTP`. If the phone reports something else, rename the staged
   directory in `full_sagit.mk`.
7. **Sound card name.** `sound-cards.txt` — the HAL wants
   `msm8998-tasha-snd-card` (tavil fallback compiled in, probably wrong for
   the Mi 6).
8. **Subsystem indices / names.** `subsys-names.txt` — `init.sagit.rc`
   resolves `modem` by scanning `/sys/bus/msm_subsys/devices/*/name` for
   the `restart_level=RELATED` writes, so indices do not matter; confirm the
   name strings are `modem`/`adsp`/`wcnss`.
9. **UDC name.** `udc.txt` — `usbudc.sh` writes the first entry of
   `/sys/class/udc` into the gadget; confirm the gadget enumerates.
10. **RTC.** The 4.4 kernel has no `/dev/alarm`, which KitKat's
    `TimeService` expects; maple seeds the clock with `rtc-restore` at
    boot. The same service is copied in — check `rtc_restore.c`'s RTC node
    against `/sys/class/rtc` if the clock resets across reboots.

## Flashing notes

- **FBE userdata.** A Lineage 22.1 (FBE) userdata cannot be reused; format
  userdata in TWRP before the first KitKat boot.
- **Sparse images.** TWRP will not accept a raw 3 GiB `system.img` from a
  FAT partition — convert with `simg2img` or flash the block directly.
- **No `/vendor` partition.** Do not flash a `vendor.img`; the hybrid root
  is part of `system.img` at `/system/a15`.
- **First boot is not silent-safe.** Keep `bootlog` (kmsg to `/data`) and
  `dump-boot-log.sh` handy; a wrong mount path shows up as a stall at the
  boot animation, not a kernel panic.

## Known blockers (inherited from maple)

- **WiFi/modem PD stability.** icnss needs SERVREG_LOC (`pdserve`, in-tree)
  and the modem root PD to stay up for `wlan_pd`/WLFW; on maple it crashes
  every ~41 s. Expect the same until the keepalive path is settled.
- **No RIL.** No KitKat-era Qualcomm RIL for this platform; cellular stays
  off (same posture as maple/cooper).
- **Camera / GPS / BT / fingerprint.** Proprietary HALs, not started.
- **BT SCO audio.** The `qcom,wcn-btfm` property is stripped by
  `patch-sagit-dtb.py` so the card registers; SCO itself stays dead
  (maple TEST 36).
