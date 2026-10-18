# CHARQUIRKS mapping (sister 4.2-on-pdx203 port → maple KitKat)

Reference notes: how a sister port's `CHARQUIRKS.md` (Android 4.2.2 on the
Xperia 1 II, SD865, kernel 4.19; a private document, not in this
repository) maps onto maple/KitKat. Read 2026-10-14. Below: what we
independently converged on (validates our path), what applied to our stall
at the time, and what is queued for later phases. It is a dated snapshot:
maple has since gone through hardware GL (glrenderd, then the in-process
[libhyb](software/libhyb.md) on 2026-10-18), so the graphics rows below
are historical. Current status:
[maple quirks](../devices/maple/quirks.md); generic lessons:
[KitKat porting notes](porting-notes/kitkat.md). Back to the
[advanced index](README.md).

## Independently converged (validates our approach)

- **Binder 64-bit protocol patched into userspace, not the kernel**
  (`Parcel`, `IPCThreadState`, servicemanager `binder.c`/`binder.h`,
  bionic `linux/binder.h` replaced with 64-bit layout). We derived this
  from first principles via ioctl-number mismatch (0xc0186201 vs
  0xc0286201); their doc states it outright. Same fix shape.
  - Extra detail to apply: **"zero the top half of every 64-bit union,
    junk there breaks lookups."** Audited 2026-10-14: our stack-built
    structs (`binder_call` writebuf, `binder_send_reply` data,
    `flatten_binder` obj) assign every field; layouts have no padding
    (`binder_txn` 64, `flat_binder_object` 24, `binder_write_read` 48).
    OK, no action.
- **"No super" layout**: maple (2017, no dynamic partitions) matches their
  "no super" section exactly — plain GPT partitions, mount straight from
  init.rc, bootloader fastboot writes raw. Our raw-node mounts + dd
  flashing are the documented-good path. No lpmount/dm needed.
- **Userdata wipe on first boot** (encrypted f2fs unreadable to old
  Android). We did the manual equivalent (mke2fs sda15). Same.
- **ueventd by-name links don't exist early** — we mount by raw sde/sda
  nodes + `wait_for_coldboot_done`. Same conclusion.
- **SurfaceFlinger without hwcomposer, framebuffer hal / software
  rendering.** Our libagl path matches; their glrenderd (GPU) is a later,
  bigger project we do NOT need for UI.
- **`ro.opengles.version=131072`** — already in our system.prop.
- **Java/JDK: Oracle JDK 6 gone, patch out the OpenJDK check** — same as
  our `build-jdk-openjdk6.patch`.
- **PRODUCT_COPY_FILES can't do symlinks; build.prop not regenerated**
  — noted for packaging phase.

## Applies to our CURRENT stall (black screen, load 6+, dalvik 15)

- **Firmware-load stall signature**: "a load that misses falls through to
  ueventd's user helper and stalls ~60s per file, that delay is the tell."
  Our 11-minute freeze-then-release pattern (test18 watch: quiet at 23:15,
  climb at 23:16) fits SERIALIZED ~60s stalls resolving. Candidates on
  maple: venus/video firmware (mediaserver/OMX probes at boot), adsp,
  slpi, tzapps. ACTION: wchan watcher (test20) will show it if present
  (syfs request_firmware threads in D-state); if confirmed, either ship
  the firmware blobs or stub the requests.
- **Audit-backlog D-state storm**: ruled OUT (permissive SELinux changed
  nothing). Their doc doesn't mention it either; dropping the theory.
- **`on fs`/`wait` 5-second give-up (4.2)**: KitKat's `do_wait` takes an
  optional timeout — check `system/core/init/builtins.c` if we need
  longer waits. Currently covered by `wait_for_coldboot_done`. No action
  unless mounts regress.

## Queued: adb bringup (right after UI)

- USB is configfs-only on 4.4 kernels; `android_usb` writes fail silently.
  Need a configfs gadget + functionfs adb function from init
  (`init.maple.usb.rc`), modeled on their `init.pdx203.usb.rc`.
- Force the gadget to HIGH speed (4.x adbd has no superspeed
  descriptors) and use a USB-2 cable.
- adbd closes ep0 on disconnect and unbinds the UDC (no ffs handshake
  in old adbd) → rebind loop watching for empty UDC.
- `adb shell` needs working `/dev/pts`.
- Eng build (`ro.secure=0`, `ro.adb.secure=0`) → root adb, no auth.
- `printk.devkmsg=on` on cmdline lets non-root scripts log to kmsg
  (would have made our init markers visible; add with the USB cmdline
  work).

## Queued: storage phase

- `class late_start` only starts via mount_all→nonencrypted. We dropped
  mount_all (direct mounts). When storage lands: run sdcard daemon in
  `class main`, emulated layout (`/data/media` → fuse → `/storage`),
  `storage_list.xml` overlay (`android:emulated="true"` +
  `android:primary="true"`), `PRODUCT_CHARACTERISTICS := nosdcard`,
  `/cache` as tmpfs. (MountService NPEs without a known volume —
  suspect #2 if boot stalls again post-dexopt.)
- KitKat fs_mgr CAN do /data (fstab + `mount_all` brings late_start
  back); keep direct mounts until UI, then revisit.

## Queued: UI/polish phase

- `ActivityManager.isHighEndGfx()` true on 4 GB RAM → effects needing
  hwui. Patch to false while on software rendering.
- `ro.config.low_ram=false`, `dalvik.vm.heapsize=256m`,
  `dalvik.vm.heapgrowthlimit=128m`.
- `media_profiles.xml` required (camera app throws without it).
- Property names: only system apps can set some prefixes; rename custom
  settings into allowed ones.
- `media_codecs.xml` (software codecs) or MediaPlayer plays nothing;
  `libsrec_jni` in PRODUCT_PACKAGES or sound recorder crashes.

## Queued: kernel rebuild project (needs own workstream)

- **Logger driver** (`drivers/staging/android/logger.c`,
  `CONFIG_ANDROID_LOGGER` + 32-bit compat entry header): KitKat liblog
  needs `/dev/log/*` (logd arrived in 5.0). Without it we're blind
  (logcat exits, ALOG drops, ALOGE errno fallout). Restores real logging.
- **Alarm driver** (`alarm-dev.c`, `CONFIG_ANDROID_ALARM_DEV`):
  KitKat AlarmManager needs `/dev/alarm` (timerfd is 5.0+). Without it
  no clock-set from Settings and alarms fire only while awake.
- Keep `CONFIG_COMPAT` (already in Sony kernel), binderfs OFF
  (`/dev/binder` must register as misc), single `binder` device
  (`CONFIG_ANDROID_BINDER_DEVICES="binder"` — no hwbinder/vndbinder or
  chrooted blobs hang in waitForHwServiceManager).
- Their tree is Sony open + techpacks with clang; ours would start from
  the whatawurst msm8998 tree. DO NOT start until UI boots on the
  prebuilt kernel.

## Queued: radios (much later)

- Subsystem defaults (SYSTEM restart / modem PANIC) must be set to
  RELATED/NOP first or a dsp/modem crash panics the phone.
  → DONE for adsp/slpi in TEST 36 (`init.maple.rc` writes RELATED to
  subsys4/5 on post-fs-data; all 7 read SYSTEM live 2026-10-16). Modem
  deliberately left alone (never boots here; no crash source).
- Modem errfatal ~40s pattern known; expect a fight. GPS blocked on modem.
- `service_locator.enable=0` on cmdline (with =1 their audio card took
  ~5 min to register). → DONE in TEST 36 (BoardConfig flipped 1→0;
  verify-me: tavil card probes after reflash).
- WiFi: their 4.2 used wpa_supplicant_8 + nl80211 + lib_driver_cmd_qcwcn
  with `BOARD_WLAN_DEVICE` UNSET (qcwcn turns on p2p; they run plain
  `wpa_supplicant` + `wifi.interface=wlan0`). Maple keeps qcwcn (KitKat's
  driver_nl80211 NEEDS the private lib to link — static, so no .so on
  device) and defines both supplicant services, framework chooses;
  `wifi.direct.xml` still not shipped (same posture). Maple wifi chip is
  WCN3990/cherokee via built-in qca_cld3 (NOT BCM43xx — corrected from
  `/proc/config.gz` + BT tlv names), `gIPAConfig=0` in the new ini
  (ipa_smmu_wlan can't alloc its iommu domain live). Driver loads
  (v5.1.1.77V, /dev/wlan chr 229,0) but no wlan0/phy — WLFW/QMI handshake
  likely modem-gated. TEST 36 has the full inventory.
- TEST 37 verdicts (maple-verified, constrain all future radio work):
  SKIP_QMI panics at boot with AND without BDF staged (reverted); QRTR
  is ABSENT from the kernel (0 kallsyms hits, legacy IPC_ROUTER only),
  so NO QRTR userspace (cnss-daemon, pd-mapper, rmt_storage, tftp_server
  — the 64-bit chroot idea is dead twice over: no `linker64` either).
  Remaining wifi paths are modem boot or kernel QRTR backport. Audio's
  remaining paths must likewise avoid QRTR: adsprpcd-over-SMD
  (`/dev/adsprpc-smd` exists) or in-HAL ACDB via `/dev/msm_audio_cal`
  (`maple-fw/acdbdata` staged).
- Audio: tinyalsa `audio.primary.maple` written for the port in TEST 36
  (speaker = `speaker` + deep-buffer MM1/pcm0, mic = `handset-mic`/dmic1
  + MM1/pcm0, DEC7 104, no acdb, silent-safe pre-probe) with
  `mixer_paths_tasha.xml`, `audio_policy.conf`, `libsrec_jni`. Card-side
  fix first: `qcom,wcn-btfm` stripped from all 4 DTBs (their
  `qcom,wcn-bt = <0>`), adsp firmware staging next (modem `image/adsp.*`
  inventoried live; blobs stay out-of-git like the kernel).

## Queued: debug hygiene (applies now)

- `run-on-device.sh` pattern (push binary to `/data/local/tmp`, run,
  read log file) for testing pieces without reflashing — adopt once adb
  works; until then our init-service + file pattern is the equivalent.
- Keep kmsg writes minimal (console_lock pan stalls); our markers are
  already removed.
- `TARGET_NO_KERNEL := true` so the build never tries to make boot.img
  itself — ADOPT NOW (we pack boot.img by hand; one less surprise).
