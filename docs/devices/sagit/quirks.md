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
| by-name | `/dev/block/by-name`, `/dev/block/bootdevice/by-name` and `/dev/block/platform/soc/1da4000.ufshc/by-name` all resolve | `fstab.msm8998`, init stanzas |
| partitions | boot `sde40` 64 MiB, recovery `sda15` 64 MiB, system `sda18` 9 GiB, cache `sda16` 256 MiB, persist `sda14` 64 MiB, modem `sde41` 192 MiB, misc `sda9` 4 MiB | `BoardConfig.mk` |
| cmdline | boot.img carries `androidboot.hardware=qcom`; bootloader appends `mdss_mdp.panel=1:dsi:0:qcom,mdss_dsi_jdi_fhd_r63452_cmd:config2:1:none:cfg:single_dsi`, `androidboot.serialno`, `veritymode` etc. — it does **not** append `androidboot.hardware`, so our `hardware=msm8998` survives | `BoardConfig.mk` |
| panel | `jdi fhd cmd incell dsi panel`, fb0 1080×1920 @60, virtual 1080×3840, density 440 | Panel DTB |
| touch | input name **`synaptics_dsx`**, ABS_X 0..1079 / ABS_Y 0..1919, `INPUT_PROP_DIRECT` | `synaptics_dsx.idc` |
| keys | `gpio-keys` = volume up/down + power (KEY_0x73/0x74 are on `qpnp_pon`) | `gpio-keys.kl` |
| backlight | **no `/sys/class/backlight`**; `lcd-backlight` lives under `/sys/class/leds` | `lights.c` (already) |
| LEDs | `white` (brightness/blink/max_brightness), `wled`, `button-backlight`, `vibrator` (pmi8998 haptics) | `lights.c` (already) |
| hw_platform | **`SAGIT`** — and it is a red herring: the vendor `libacdbloader.so` hardcodes **`Forte`** (`strings … \| grep -E '^Forte$'`), and the ROM ships `vendor/etc/acdbdata/Forte/` | `full_sagit.mk`, `stage.py --acdb-name=Forte` |
| sound card | `msm8998-tasha-snd-card` (only card 0); speaker is the TFA9888 amp on **QUAT_MI2S (I2S)**, WCD9335 only serves the headphone jack | `audio_hw.c` (already) |
| subsys | `ipa_fws venus spss a540_zap adsp slpi modem` — **no `wcnss`/`wlan_pd` entry** | `init.msm8998.rc` resolves by name, so indices are fine |
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

## Jelly Bean 4.1.2 (MIUI `mione_plus`) boot deltas

The side port in [miui-jb.md](miui-jb.md) reuses the KitKat ramdisk; these
are the places where a 4.1.2 userland cannot follow a 4.4 ramdisk.

- **A 4.1.2 system needs its own `BOOTCLASSPATH`.** The 4.4 build's
  `init.environ.rc` lists `core-libart.jar`, `framework2.jar`,
  `webviewchromium.jar`, `okhttp.jar`, `telephony-common.jar`,
  `mms-common.jar` — none exist in the MIUI tree, so zygote dies before
  `system_server`. Overlay `devices/sagit/jb/ramdisk/init.environ.rc`
  (`core`, `core-junit`, `bouncycastle`, `ext`, `framework`,
  MIUI's `framework_ext`, `android.policy`, `services`, `apache-xml`).
  Verify-me: once booted, `dmesg | grep -i class` and
  `getprop dalvik.vm.stack-trace-file` while zygote runs.
- **Jelly Bean's `sdcard` daemon takes positional uid/gid.** The ROM's
  `/system/bin/sdcard` prints `usage: sdcard [-l -f] <path> <uid> <gid>`
  and hardcodes its FUSE mount point `/storage/sdcard0` (both verified
  from the binary 2026-10-24). KitKat's
  `sdcard -u 1023 -g 1023 -l /data/media /mnt/shell/emulated` would print
  usage and exit, so `init.msm8998.rc` uses `sdcard /data/media 1023 1023`.
- **JB `vold` reads `/system/etc/vold.fstab`, not `/fstab.<hardware>`.**
  The ROM ships `dev_mount sdcard /storage/sdcard0 auto
  /devices/platform/msm_sdcc.3/mmc_host` — an MSM8660 SD controller that
  does not exist here, so that mount simply fails; the ramdisk
  `fstab.msm8998` is inert on 4.1.
- **The OTA ships only `system/` and `data/`** — no boot, no recovery —
  so the boot image has to be assembled (`scripts/build-sagit-jb-boot.py`).
  Its zip entries are FAT (`-rw----`, no symlinks): extracting on Windows
  loses all 62 links and every exec bit, so unpack on ext4 and apply
  `config/mione-symlinks.txt` before `mke2fs -d`.
- **EROFS cannot be used for the repacked system.** The stock
  `config/system_fs_options` recipe calls `mkfs.erofs`; the 4.4.247 kernel
  has no EROFS (`/proc/filesystems` on the device lists none), so an EROFS
  image flashes and never mounts. The re-packed system is ext4.
- **The MSM8660 firmware in the ROM has to be swapped out.** Its
  `etc/firmware/*` and `vendor/firmware/fw_bcm*` (yamato/leia/msm_sdcc
  era) never load here; the image carries the KitKat build's MSM8998
  closure instead (`a530*/a540*`, `adsp.*`, `cpe_9335/9340.*`, `modem.*`,
  `mba.mbn`, `wlanmdsp.mbn`, `wlan/qca_cld/`, `acdbdata/Forte/`) —
  `config/mione-pack.sh` restores those paths from
  `out/target/product/sagit/system/`. Note this is only what the kernel
  or a HAL may open by absolute path: the 4.1 `libaudio`/`libacdbloader`
  themselves are MSM8660 binaries and still will not drive the codec.
- **No `/dev/log` for the 4.1 userland.** The kernel has no
  `CONFIG_LOGGER`; the KitKat port patched `liblog` to fall back to
  `/dev/kmsg`, but this ROM's own `liblog` has no such fallback — expect
  no `logcat` sink at all, debug from `dmesg` and `/data/misc` snapshots.

## MIUI V5 4.4.2 (`cancro`) pack deltas

The V5 pair for this device is `C:\download\chiron\system_miui5_kxdcnb.img` +
`boot-miui7-ksu2-v5fix.img` (the MIUI 7 sagit boot image, reused because V5 is
the same SDK). WiFi bring-up on top of it is written up in sections 12-13 of
[chiron/wifi-progress.md](../chiron/wifi-progress.md); the pair that actually works
is the one at the end of this section, not the two base images named above.

What is specific to running a *prebuilt* ROM here:

- **The ROM's `libhardware_legacy.so` cannot bring WiFi up.** It was compiled
  with `WIFI_DRIVER_MODULE_PATH := /system/lib/modules/wlan.ko`, so
  `wifi_load_driver()` asks for `wlan.driver.status=ok` plus a `wlan` line in
  `/proc/modules`, and when both fail it `insmod`s the msm8974 prima module.
  qca_cld3 is *built into* the 4.4.247 image (its `wlan_hdd_*` symbols are in the
  `Image`, so there is no `/proc/modules` entry to find) and the prima `.ko` can
  never load, so the function returns -1 and the toggle refuses to turn on.
  Replace the library with this port's AOSP build
  (`out/target/product/sagit/system/lib/libhardware_legacy.so`, no insmod path) —
  the same swap `miui5-chiron-pack.sh` does, which also moves vibration off the
  Mi 3 Immersion engine. Nothing in V5's `/system/lib`, `lib/hw`, `bin` or `xbin`
  imports any of the 111 V5-only exports, so the swap is link-safe.
- **The V5 boot image still has the unpatched DTBs** (kernel 15774877 B, i.e. the
  raw LOS 17.1 blob; `patch-sagit-dtb.py` yields 15774349 B). Neither property it
  removes is WiFi-related, so if the panel shows a stale frame until touched or
  the tasha card never registers, that is the cause, not the WLAN stack.
- **`wifi.interface` is not in V5's build.prop, and the fallback is fatal.**
  `hardware/libhardware_legacy/wifi/wifi.c:530` reads it with default
  `WIFI_TEST_INTERFACE "sta"`, so `wifi_connect_to_supplicant()` opens
  `/dev/socket/wpa_sta`, times out after 5 retries, `WifiMonitor` reports
  `startMonitoring(wlan0) failed!` and drops wlan0 from `mIfaceMap` — every later
  enable then fails with `startMonitor called with unknown iface=wlan0` for the
  rest of the boot. This port's own AOSP images get the property from
  `devices/sagit/kitkat/system.prop`, a prebuilt ROM has nowhere to put it, so the
  ramdisk's WLAN block sets it in `on post-fs-data` (both init rc files, kept
  identical).
- **MIUI's WifiNative prefixes every supplicant command with `IFNAME=wlan0 `,
  which only the *global* control interface parses**
  (`wpa_supplicant_global_ctrl_iface_process()`,
  `external/wpa_supplicant_8/wpa_supplicant/ctrl_iface.c:6194`). The per-interface
  socket in `-O/data/misc/wifi/sockets/` answers `UNKNOWN COMMAND` to all of them,
  so the HAL has to talk to `/dev/socket/wpa_wlan0` — the socket init creates from
  the supplicant stanza `socket wpa_wlan0 dgram 660 wifi wifi`, whose fd MIUI's
  `-g@android:wpa_wlan0` binds as that *filesystem* name (no abstract name ever
  shows up in `/proc/net/unix`). Leave `-O` alone; that was a workaround that made
  things worse.
- **A stub `WCNSS_qcom_cfg.ini` gives you a dead Rx path**, which looks exactly
  like "the toggle is broken": association and the 4-way handshake complete,
  `Supplicant state: COMPLETED`, but `/sys/class/net/wlan0/statistics/rx_bytes`
  never moves, DHCP times out and the framework deauths itself
  (`reason=3 locally_generated=1`). qca_cld3 takes its Rx mode from the INI
  (`rx_mode` 1=Rx_thread, 2=RPS, 4=NAPI); the 20-line sagit stub had none of it,
  while chiron's proven 606-line file sets `rx_mode=4`, `gIPAConfig=0x7d`,
  `gEnableFlowSteering=1`, `ce_service_max_yield_time=500`, `maxMSDUsPerRxInd=8`.
  sagit now ships the same file (md5 `771c3e398d0f98da3f5710b66ec9b007`) in
  `devices/sagit/kitkat/wifi/` and the staged vendor copy.
- **Wake the screen before judging V5's WiFi.** With the screen off MIUI issues no
  scan request, `screenstate` switches WiFi off after ~66 s of idle, and the
  supplicant sits at `INACTIVE` — indistinguishable from a broken HAL.
- `init` on this base rejects the `respawn` option (`invalid option 'respawn'`); a
  non-`oneshot` service is restarted on exit anyway, so the two keeper services
  just omit the keyword.
- Native `libhardware_legacy`/`WifiHW` ALOGs never reach `/dev/kmsg` here (no
  logd), while the java framework's do (`<14>`/`<15>` prefixes in `dmesg`). For the
  HAL, trace to a per-pid file: `/data/misc/wifi/wfdbg.txt-<pid>`, mode 0666.
- `init.svc.wpa_supplicant` can stay absent — V5 has
  `/system/etc/permissions/android.hardware.wifi.direct.xml`, so
  `WifiSettings`/framework calls `startSupplicant(p2p_supported=1)` and the running
  service is `p2p_supplicant` (wlan0 + p2p0 in one process). Check
  `init.svc.p2p_supplicant` instead.
- Flashing this pair: recovery has no `/dev/block/bootdevice/by-name`, so write the
  raw blocks — boot `/dev/block/sde40` (64 MiB), system `/dev/block/sda18`
  (9.66 GiB); `/tmp` in recovery is a 2.9 GiB tmpfs.
- Storage on this V5 image behaves (TWRP sees `/sdcard`), while the AOSP KitKat
  image of the same device does not — still open, see
  [README.md](README.md) storage row.
- **The wall clock cannot be set by the ROM at all** — it boots at 1971-02-14 and
  every NTP attempt is silently dropped. Two kernel-side reasons: this image has no
  android_alarm driver, so there is no `/dev/alarm`, and KitKat's stock
  `SystemClock.setCurrentTimeMillis()` sets the clock *only* through it (it logs
  `Unable to open alarm driver: No such file or directory` and returns false); and
  `qpnp_rtc` in the sagit DT has no `allow-set-time`, so
  `/sys/class/rtc/rtc0/since_epoch` is a read-only bare second counter (measured
  35337894). Our AOSP tree works around the first with
  `patches/kitkat/frameworks-base-time-without-dev-alarm.patch`; a prebuilt ROM can't
  be patched, so `devices/sagit/kitkat/time-sync/` does the same job from root
  (`settimeofday()` + write `/data/system/time/rtc_offset`), and `rtc-restore` —
  which the V5 ramdisk already starts at `on post-fs-data` but which was never
  shipped as a binary — applies it on the next boot. Details: section 14 of
  [chiron/wifi-progress.md](../chiron/wifi-progress.md).
- **`getprop` is exact-match, so `dhcp.wlan0.ip` is always empty** — the lease
  property is `dhcp.wlan0.ipaddress`. A wait loop keyed on the wrong name never fires.
- A tool of our own cannot log natively here either (no logd, and ALOG from our own
  binary doesn't reach kmsg): print to stdout and let `/sbin/time-sync.sh` append it
  to `/data/local/tmp/time-sync.log`.
- UDP/123 from the WSL host times out while the same SNTP query from the phone
  succeeds first try — never conclude "NTP is blocked" from the PC.
- **The notification LED is dark for two independent reasons.** (1) V5's only lights module is
  MIUI's `lights.msm8974.so`, which writes `/sys/class/leds/{red,green,blue}/brightness` — this
  DTS registers none of those. The PMI8998 red channel is the notification LED and comes up as
  `/sys/class/leds/white` (`max_brightness` 255, measured in recovery). (2) The LED class
  attributes are created `root:root 0644`, while LightsService runs in system_server (uid
  system), so every HAL write returns EACCES; MIUI's own `init.rc` only chowns the three
  colour-named nodes that do not exist. Fix = this port's AOSP `lights.msm8998.so`
  (`devices/sagit/kitkat/lights/lights.c`, repo commit `0ec68b5`) installed into the V5 image —
  at first under **both** lookup names, `lights.sagit.so` and `lights.msm8974.so`, until the
  V5 image itself was renamed to `androidboot.hardware=msm8998` (2026-10-28), after which
  `lights.msm8998.so` is the one and only copy — plus the `on boot` chown stanza sliced into
  the V5 ramdisk. That stanza sits *before*
  the `# ---- WLAN bring-up stack` marker that `repack-v5-boot.sh` appends from, so the repack
  has to `sed`-slice it out of `init.msm8998.rc` separately; it stays its own stanza because a
  chown onto a missing node aborts the rest of the action.
- **`/sys/class/leds/white/blink` is a 1/0 flag, not an on/off millisecond pair**, and it is
  write-only here (`cat` returns I/O error, which is normal — judge blink by the LED or the HAL
  log). Write `brightness` *before* `blink 1`: `qpnp_rgb_set()` clears `pwm_cfg->blinking`, so a
  brightness write afterwards cancels the DT breathing LUT. And drop the channel's
  `battery-charging` trigger to `none` first, otherwise the trigger owns `brightness_set` and
  swallows the framework's write.
- **V5 pulses the LED in software instead of asking for `LIGHT_FLASH_HARDWARE`.**
  `dumpsys notification` lists every record with `led=0x00000000 onMs=0 offMs=0`, and the HAL
  log alternates `notification v=149` / `v=0` a couple of seconds apart, so on this ROM the
  steady on/off branch is what runs; the LUT branch stays for hand tests and other ROMs.

Working pair, verified by two consecutive real cold boots with no manual commands
(2026-10-27): `boot.img` md5 `ee2234588c8b53c5bfe9ec55b6662b97` (ramdisk 341990) +
`system.img` md5 `2014196202b6bd13258cae04ab73d503` (1 GiB raw ext4), packaged in
`C:\download\chiron\sagit-v5-wifi-20261027\` and `~/mi5-wifi/out/`. Result:
`Supplicant state: COMPLETED`, SSID "Xiaomi 15" at 866 Mbps, DHCP
`10.237.226.237/24`, ping out, and the Settings toggle working in both directions.
Full write-up: section 13 of
[chiron/wifi-progress.md](../chiron/wifi-progress.md).

Time sync on top of that pair (2026-10-27, same flashing procedure):
`boot-mi5-wifi5.img` md5 `c3f314881a7322b436ce0cf596c7e321` + `system_miui5_wifi3.img`
md5 `585504392664bcfa7f1a6de4df25696a` — at 25 s of uptime, with no network yet,
`date` already reads the correct 2026 time because `rtc-restore` applied the stored
offset; once DHCP bound, `/system/bin/time-sync` reported
`clock set to 2026-10-27 15:08:56 UTC from ntp.aliyun.com, rtc offset 1757773423 saved`.
`boot-mi5-wifi6.img` md5 `0ef956599e35a985ccc83b8131e3555c` replaces only
`/sbin/time-sync.sh` (the `dhcp.wlan0.ipaddress` fix plus a 2-minute retry) and is
awaiting its turn on the phone.

Notification LED on top of the same base (2026-10-28) — this pair supersedes the two above,
it is the same V5 tree re-packed with WiFi + clock + LED together:
`boot-mi5-led1.img` md5 `e398455c52f157b8e245d6ba6b29bfe5` (ramdisk 343285) +
`system_miui5_led.img` md5 `396e31bb43b5baf53d8a966ce0db00e9`, packaged in
`C:\download\chiron\sagit-v5-led-20261028\`, flashed to the raw blocks and read back
byte-exact. Verified at 60 s of uptime: the three `white/` attributes are `system system`,
`dmesg` carries `lights: notification v=149 flash=0 on=0 off=0` from pid 4764 (system_server),
i.e. our module is the loaded one, `cat /sys/class/leds/white/brightness` returns 149, and
`echo none > trigger; echo 255 > brightness; echo 1 > blink` arms the breathing pattern.
Write-up: section 15 of [chiron/wifi-progress.md](../chiron/wifi-progress.md).

**V5 renamed to `msm8998` too (2026-10-28, not yet flashed).** Repo commit `e8bd0dd` gave the
AOSP artifacts one set of names per board; the V5 image was then moved to the same scheme, so
there is no longer a "V5 name" and an "AOSP name" for the same file:

- cmdline `androidboot.hardware=sagit` → `=msm8998`. The ramdisk is patched in the header's
  512-byte cmdline field (offset 64) by `repack-v5-boot.sh`; nothing else in the image reads
  `ro.hardware` except HAL lookup and `init.rc`'s `import /init.${ro.hardware}.rc`.
- ramdisk `init.sagit.rc` / `init.sagit.usb.rc` / `ueventd.sagit.rc` / `fstab.sagit` →
  `init.msm8998.rc` / `init.msm8998.usb.rc` / `ueventd.msm8998.rc` / `fstab.msm8998`, plus the
  `import /init.sagit.usb.rc` inside the device rc.
- `/system/lib/hw`: `gralloc.msm8998.so`, `lights.msm8998.so`, `audio.primary.msm8998.so`,
  `audio_policy.msm8998.so`; the old `*.sagit.so` / `*.msm8974.so` copies are **deleted**
  (pristine copies in `~/mi5-wifi/backup/naming/`) so `hw_get_module` cannot pick a stale one.
- `libEGL_adreno.so` is swapped in the same step: the shim dlopens a hardcoded path.
- Safe because `ro.product.board=MSM8974` / `ro.board.platform=msm8974` stay as they are, so
  camera and the other MIUI `*.msm8974.so` modules still resolve through the fallback keys and
  `msm8998` only wins for the four files installed under that name. `/system` and `/data` are
  mounted by explicit `mount ext4 …` lines in the device rc (no `mount_all`, no vold), so
  `fstab.msm8998` is inert. The replaced audio HALs were already this port's builds
  (`audio_policy.msm8974.so` was byte-identical to our `audio_policy.default.so`), so MIUI's
  own audio path is untouched.

New pair: `boot-mi5-msm8998.img` md5 `f3aaaa65f27809a6ec99b59c4ed23b20` (ramdisk 343096,
kernel byte-identical to the LED image) + `system_miui5_msm8998.img` md5
`aca214389a6db23f683698c9fc5142c1`, packaged in `C:\download\chiron\sagit-v5-naming-20261028\`.
`verify-v5-wifi.sh` compares 70 files against the AOSP build: 0 mismatches, no stale names.
**Static checks only — the phone still runs the `-led-` pair, both halves must be flashed
together and cold-booted.**

**V5 boot with the healthd charging fix (2026-10-28, not yet flashed).** Charging while
booted showed no indicator on the status bar or lock screen: V5's `/sbin/healthd` is stock
KitKat and only classifies `Unknown/Battery/UPS/Mains/USB/USB_DCP/USB_CDP/USB_ACA/Wireless`,
while this kernel's smb2/smb5 reports the negotiated mode (`USB_HVDCP`, `USB_HVDCP_3`,
`USB_PD`, `USB_FLOAT` …) → unknown type → `plugged` 0. The boot in
`C:\download\chiron\sagit-v5-charge-20261028\` (md5 `a1b99d6bf7c7652d35ecdb04238affd4`,
ramdisk 343124) is the `-msm8998-` boot with **only** `/sbin/healthd` replaced by the binary
from `patches/kitkat/system-core-healthd-msm-power-supply-types.patch` (md5
`65399232bc14a69852dd4b75defc6078`); the kernel section is `cmp`-identical to
`boot-mi5-msm8998.img`, the cmdline and the ramdisk file list are unchanged. The system
image is untouched — flash `boot` alone. Healthd runs `service healthd /sbin/healthd` from
`init.rc`, so nothing else has to be wired. Same fix went into the chiron AOSP boot. See
the "KitKat userspace on a 4.4 kernel" bullet in
[porting-notes/kitkat.md](../../advanced/porting-notes/kitkat.md).

## MIUI V7 4.4.4 (`cancro`) pack deltas

The side port in [miui-v7-kitkat.md](miui-v7-kitkat.md) is a plain
`system.img` repack — being the same release as this port, no ramdisk or
bootclasspath changes are needed. What the packer has to know:

- **Permissions come from `set_metadata*`, not `set_perm`.** 50 rules in
  the updater-script (`bin` 0:2000 0755, `netcfg` 02750,
  `diag_mdlog` 06750, `run-as` 0750 + caps, `su` 06755, `shelld` 0754
  0:1000, `ppp` 0555 recursive, `vendor/*` 0:2000 0755) are parsed by
  `config/miui7-pack.sh`; zip entries carry no modes or links.
- **The OTA's `install-recovery.sh` is removed from the image.** It is
  0544 — owner-executable — and the ramdisk `init.rc` runs it as
  `flash_recovery`; left in, it would applypatch the cancro 14 MiB
  `recovery-from-boot.p` (built against the zip's own `boot.img`) onto
  sagit's recovery partition.
- **`run-as` loses its file capabilities** (`mke2fs -d` writes no
  `security.capability` xattrs); setgid 2000 survives, and adbd is root,
  so nothing depends on it.
- **The zip is a full OTA, not a system-only package**: its `boot.img`,
  `NON-HLOS.bin`, `emmc_appsboot.mbn` and `tz/rpm/sbl1/sdi/DDR` are
  MSM8974/Mi-3W images and must never be dd'd to a sagit partition.
- **Every HAL and kernel module is msm8974** (`audio.primary/camera/
  lights.msm8974`, prima `wlan.ko` for the 3.4 kernel); display is
  already swapped to this port's libhyb closure (pack section 1c), audio
  will need the tinyalsa + TFA9888/QUAT_MI2S path from
  `out/target/product/sagit/system/` (451 `vendor/lib` entries).
- **The ROM speaks the pre-unified binder ABI.** MIUI cancro userspace
  was built against QC 3.4-BSP bionic headers: `BINDER_WRITE_READ` =
  `0xc0186201` (24-byte struct) instead of the unified `0xc0306201`, and
  the transaction-buffer wire format differs with it. The 4.4.247
  kernel's binder answers the old ioctl with `-EINVAL`, so `servicemanager`
  crash-looped and init rebooted to recovery (first flash, 2026-10-24).
  Only `bin/servicemanager` and `lib/libbinder.so` carry the old constant,
  so `config/miui7-pack.sh` swaps both for the AOSP 4.4.4 sagit ones —
  see [miui-v7-kitkat.md](miui-v7-kitkat.md) for the symbol-compat checks.
- **The display closure is Adreno 330 userspace and must go.** Second
  flash (2026-10-24) hung at the boot logo: `surfaceflinger` aborted in
  `SurfaceFlinger::init()` with `__android_log_assert("EGLctx creation
  failed")` — cancro's `/system/vendor/lib/egl/*` are the msm8974
  Adreno 330 drivers and cannot drive the Adreno 540; 8 identical
  tombstones, one every ~5 s, and the `init: untracked pid` dmesg stream
  was debuggerd's fork writing them. Fix (pack section 1c): cancro's
  `lib/egl` + `vendor/lib/egl` replaced by the libhyb shims, gralloc shim
  installed **under the `msm8974` name too** (hw_get_module resolves the
  variant from `ro.board.platform` before trying `.sagit`),
  `hwcomposer/copybit/memtrack.msm8974` removed, `/system/a15` hybrid
  vendor closure added, `fbpresentd` installed.
- **MIUI liblog has no kmsg fallback, so logcat is invisible.** With no
  `CONFIG_LOGGER` there is no `/dev/log`, and a process' abort message
  only reaches logcat — tombstones are the only crash evidence. The pack
  swaps `lib/liblog.so` for the sagit build's `/dev/kmsg`-fallback liblog
  (coverage checked: all 509 importers satisfied); after that every
  ALOGx shows up in dmesg.
- **MIUI's `alloc_device_t` is shifted one pointer against KitKat.**
  MIUI libui's `GraphicBufferAllocator::alloc` calls `mAllocDev->[0x40]`
  (extra 8th argument) or `mAllocDev->[0x44]` (plain) to allocate,
  `free` calls `[0x48]`, `dump` `[0x4c]` — KitKat puts
  alloc/free/dump at 0x40/0x44/0x48 (`hw_device_t` = 0x40 bytes, `close`
  at 0x3c). Against a KitKat-layout gralloc device, MIUI's extra==0
  alloc call lands on `free`, which returns -EINVAL for the garbage
  handle: the third-flash logo hang (`eglshim: REAL dequeue FAILED
  r=-22` every 5.2 s, clean process death, no tombstone, no gralloc
  klog). The gralloc shim picks the layout at gpu0-open time from
  `ro.miui.ui.version.name`: on MIUI it installs gr_alloc at 0x40 *and*
  0x44, gr_free at 0x48, a dump stub at 0x4c. Applies to any MIUI
  repack, not just cancro.
- **The liblog→kmsg swap works for the ROM, not for the shims.** After
  the swap, framework/service ALOG lines reach dmesg (ZygoteInit
  banners, ServiceManager/AudioSystem waits, memtrack and netd
  warnings). ALOG calls made from the port's own shims
  (`libEGL_adreno`, `gralloc.sagit`) still vanish — cause unknown
  (2026-10-24) — so both shims carry their own direct-`/dev/kmsg`
  `klog()` (budgets: 40 / 150 lines per process) for diagnostics.
- **MIUI's bootanimation waits for audio.** With `mediaserver`
  crash-looping, MIUI's bootanim sits in `AudioSystem` waiting for
  `media.audio_flinger` (the MIUI anim has sound); audio may have to
  work before the animation completes.
- The MSM8998 firmware/acdb closure is added **additively** (the ROM's
  msm8974 `etc/firmware` stays); MIUI's `data/` preloads are not packed —
  `/data` is formatted anyway.

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
