# sagit quirks (Xiaomi Mi 6)

Mi-6-specific deltas from the boot-verified maple port, the VERIFY-ME
checklist, and the open blockers. Status and file manifest live on the
[device page](README.md); the generic KitKat playbook is the
[KitKat porting notes](../../advanced/porting-notes/kitkat.md) and the
[maple quirks](../maple/quirks.md) are the reference bringup log.

Current state: **skeleton, never built or booted**. The hardware facts
below were measured on the phone on 2026-10-24 with
`scripts/dump-sagit-stock.sh --su` (Flyme 12.7 port, Android 16, msm8998
kernel); the device tree now carries those values, but nothing has been
flashed yet.

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

## Hardware facts (measured 2026-10-24)

Dump: `scripts/dump-sagit-stock.sh --su` → `dumps/sagit-stock-20261024-014254/`.
`--su` exists because the ROM's adbd is a production build (`adb root`
refuses) while KernelSU's `su` works; `adb shell su -c` gets the
root-gated nodes and SELinux is what makes the plain-shell files empty, not
missing hardware.

| what | value | where it lands |
| --- | --- | --- |
| by-name | `/dev/block/by-name`, `/dev/block/bootdevice/by-name` and `/dev/block/platform/soc/1da4000.ufshc/by-name` all resolve | `fstab.sagit`, init stanzas |
| partitions | boot `sde40` 64 MiB, recovery `sda15` 64 MiB, system `sda18` 9 GiB, cache `sda16` 256 MiB, persist `sda14` 64 MiB, modem `sde41` 192 MiB, misc `sda9` 4 MiB | `BoardConfig.mk` |
| cmdline | boot.img carries `androidboot.hardware=qcom`; bootloader appends `mdss_mdp.panel=1:dsi:0:qcom,mdss_dsi_jdi_fhd_r63452_cmd:config2:1:none:cfg:single_dsi`, `androidboot.serialno`, `veritymode` etc. — it does **not** append `androidboot.hardware`, so our `hardware=sagit` survives | `BoardConfig.mk` |
| panel | `jdi fhd cmd incell dsi panel`, fb0 1080×1920 @60, virtual 1080×3840, density 440 | Panel DTB |
| touch | input name **`synaptics_dsx`**, ABS_X 0..1079 / ABS_Y 0..1919, `INPUT_PROP_DIRECT` | `synaptics_dsx.idc` |
| keys | `gpio-keys` = volume up/down + power (KEY_0x73/0x74 are on `qpnp_pon`) | `gpio-keys.kl` |
| backlight | **no `/sys/class/backlight`**; `lcd-backlight` lives under `/sys/class/leds` | `lights.c` (already) |
| LEDs | `white` (brightness/blink/max_brightness), `wled`, `button-backlight`, `vibrator` (pmi8998 haptics) | `lights.c` (already) |
| hw_platform | **`SAGIT`** — and it is a red herring: the vendor `libacdbloader.so` hardcodes **`Forte`** (`strings … \| grep -E '^Forte$'`), and the ROM ships `vendor/etc/acdbdata/Forte/` | `full_sagit.mk`, `stage.py --acdb-name=Forte` |
| sound card | `msm8998-tasha-snd-card` (only card 0); speaker is the TFA9888 amp on **QUAT_MI2S (I2S)**, WCD9335 only serves the headphone jack | `audio_hw.c` (already) |
| subsys | `ipa_fws venus spss a540_zap adsp slpi modem` — **no `wcnss`/`wlan_pd` entry** | `init.sagit.rc` resolves by name, so indices are fine |
| UDC | `a800000.dwc3` (single entry) | `usbudc.sh` → gadget |
| RTC | `rtc0` = `qpnp_rtc`, `/sys/class/rtc/rtc0/since_epoch` present, no `/dev/alarm` | `rtc_restore.c` (already) |
| input devices | `event0 qpnp_pon`, `event1 synaptics_dsx`, `event2 uinput-goodix` (fingerprint), `event3 gpio-keys`, `event4/5 tasha jacks` | — |

Still unmeasured: nothing on the checklist, but **none of it is
boot-verified** — the first KitKat boot is what turns these into facts
about *our* userspace (ueventd naming, by-name creation, `/vendor`
symlink).

## Flashing notes

- **Modem partition image is FAT16.** The Mi 6 `modem` partition dumps as a
  partitionless FAT16 filesystem (Bliss MBR, 4096-byte sectors). Mount it
  with `mount -o loop,ro -t vfat modem.img /mnt/modem`; all PIL/BDF firmware
  lives under `image/` (`adsp.*`, `cpe_9335.*`/`cpe_9340.*`, `bdwlan.*`,
  `mba.mbn`, `modem.mdt`/`modem.b*`, `wlanmdsp.mbn`).
- **persist is ext4** and carries `wlan_mac.bin` at its root plus
  `wlan_bt/{wlan,bt}.mac` and `bluetooth/.bt_nv.bin`; read it with
  `debugfs -R "rdump / /tmp/persist-root" persist.img` (no root install
  needed).
- **ACDB directory is `Forte/`, not `SAGIT/`.** The Mi 6 is Xiaomi's
  "Forte"; Lineage 22.1 and this stock ROM both ship
  `vendor/etc/acdbdata/Forte/*.acdb` (8 files) plus a loose
  `adsp_avs_config.acdb`. `hw_platform` reads `SAGIT` but the closed
  `libacdbloader.so` hardcodes `Forte`, so stage with
  `stage.py … --acdb-name=Forte`; the tree installs
  `/system/vendor/etc/acdbdata/Forte/` and
  `/system/vendor/etc/acdbdata/adsp_avs_config.acdb`.
- **A vendor-only closure is incomplete.** `stage.py` walks DT_NEEDED and a
  vendor-only extraction (no `system` image) exits with
  `missing: libsync.so libvndksupport.so` — Android 15 keeps both in
  `/system/lib`. Pull them from the ROM's system image or the phone
  (`adb pull /system/lib/libsync.so`) into `<sysroot>/system/lib/` and rerun
  `stage.py` (measured 2026-10-24 on the Lineage 22.1 Forte vendor tree).
- **FBE userdata.** A Lineage 22.1 (FBE) userdata cannot be reused; format
  userdata in TWRP before the first KitKat boot.
- **Sparse images.** TWRP will not accept a raw 3 GiB `system.img` from a
  FAT partition — convert with `simg2img` or flash the block directly.
- **No `/vendor` partition.** Do not flash a `vendor.img`; the hybrid root
  is part of `system.img` at `/system/a15`.
- **First boot is not silent-safe.** Keep `bootlog` (kmsg to `/data`) and
  `dump-boot-log.sh` handy; a wrong mount path shows up as a stall at the
  boot animation, not a kernel panic.

## Audio (speaker verified 2026-10-24)

- **The speaker is on QUAT_MI2S, not on the codec.** The Mi 6 loudspeaker is
  the TFA9888 smart amp fed by I2S from `msm-dai-q6-mi2s.3`; the kernel binds
  `LPASS_BE_QUAT_MI2S_RX` to codec `tfa98xx.10-0034` / DAI
  `tfa98xx-aif-a-34` (`msm_quat_mi2s_tfa98xx_dai_links`, chosen when
  `hw_platform == SAGIT` and `qcom,mi2s-audio-intf` is set — both true on
  this DTB; `codec:tfa98xx.10-0034` shows up under
  `/sys/kernel/debug/asoc/<card>/`). The WCD9335 only feeds the headphone
  jack. A HAL that routes playback to `SLIMBUS_0_RX Audio Mixer
  MultiMedia1` + RX7/RX8 powers the codec (DAPM active, register diff
  moves, pcm RUNNING) and is still completely silent — nothing on that
  path reaches the amp. Stock's `deep-buffer-playback speaker` enables
  `QUAT_MI2S_RX Audio Mixer MultiMedia1` and `speaker` only switches
  `TFA Profile`.
- **The amp starts in stand-by and stays stopped until the profile
  changes.** At probe the container's default profile (`music_dual`) fails
  to start (`tfaRunSpeakerBoost error(4)`, no clock) but the driver records
  it as current, and `tfa98xx_set_profile()` early-returns for the profile
  that is already current. The HAL therefore parks `TFA Profile =
  Powerdown` once in `ensure_card()` and sets `music_dual` at route time,
  forcing a real transition (`tfaContWriteProfile`, then `tfaUnmute ...
  switching to external clock`). Without that park the amp never starts.
- **Reading the amp debugfs.** While playing,
  `/sys/kernel/debug/tfa98xx-34/accounting` must show `Wait4Src=0` and
  `NOCLK=0`. `dsp-state` = `Pending init` is normal here (this driver only
  sets DONE at probe), and `Invalid` is the INVALIDATED flag
  `set_profile()` deliberately leaves behind — neither is an error.
  `/sys/kernel/debug/asoc/<card>/dapm/bias_level` is meaningless on
  wcd9335 (it has no `set_bias_level`, so it always reads Off).
- **Kernel-side check of the whole path.** `dmesg | grep next_profile`
  must show 3 (Powerdown) then 0 (music_dual) per first playback;
  `/proc/asound/card0/pcm0p/sub0/status` = `RUNNING` with `hw_ptr`
  advancing; `tinymix 'QUAT_MI2S_RX Audio Mixer MultiMedia1'` = On and
  `QUAT_MI2S_RX Channels` = Two while playing, both back to Off/Powerdown
  after standby.

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
