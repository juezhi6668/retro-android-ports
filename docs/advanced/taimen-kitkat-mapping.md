# Taimen-KitKat mapping (Pixel 2 XL port → maple KitKat)

Reference notes: how [DoomGuy41/Taimen-KitKat](https://github.com/DoomGuy41/Taimen-KitKat)
(Pixel 2 XL / `taimen`, MSM8998, KitKat 4.4.4 on the stock Android 11
kernel + a 64-bit Android 11 closure in `/system/v11`) maps onto
maple/KitKat. Read and ported 2026-10-18. The graphics core — libhyb with
the in-process Adreno 540 driver, the KitKat gralloc on QTI gralloc1, the
64-bit `fbpresentd` — is ported and rewritten for maple's LineageOS 22.1
closure: [libhyb](software/libhyb.md), build evidence in
[maple quirks](../devices/maple/quirks.md) TEST 49. This page is the
disposition of *everything else*: their patch set (adopted, skipped,
queued) and their device-tree implementations — the
[Android 11 daemon chroot and the HAL bridges](#the-daemon-chroot-and-the-hal-bridges-assessed-not-ported),
assessed below for maple. Generic lessons:
[KitKat porting notes](porting-notes/kitkat.md).
Back to the [advanced index](README.md).

## Adopted (`patches/kitkat/`)

| Taimen patch | Ours | Why |
| --- | --- | --- |
| `system/core/0003-liblog-fall-back-to-dev-kmsg` | `system-core-liblog-kmsg-fallback.patch` | the 4.4 kernel has no `CONFIG_LOGGER`; without it every `ALOG*` in the tree (and in the libhyb-loaded libraries, which resolve `liblog.so` to KitKat's) is invisible |
| `system/core/0004-liblog-terminate-dev-kmsg-records` | `system-core-liblog-kmsg-terminator.patch` | kernel joins records without a trailing `\n` |
| `system/core/0007-fs_mgr-clear-the-ext4-project-quota-feature` | `system-core-fsmgr-ext4-project-quota.patch` | a LineageOS mke2fs on slot B sets `RO_COMPAT_PROJECT`; the 4.4 kernel refuses the `/data` mount |
| `system/core/0008-elapsedRealtime-on-CLOCK_BOOTTIME` | `system-core-elapsedrealtime-boottime.patch` | no `/dev/alarm`; also fixes the truncated 1 KiB kernel-cmdline read (maple's is longer) |
| `dalvik/0001` + `art/0001` (time without `/dev/alarm`, capset) | `dalvik-zygote-capset-v3.patch` (+ `frameworks-base-time-without-dev-alarm.patch`) | `capset` ABI v3 so `system_server` keeps `CAP_WAKE_ALARM`/`CAP_BLOCK_SUSPEND` for the timerfd alarms; only the Dalvik twin applies here (KitKat's default runtime) |
| `frameworks/base/0001-SystemUI-canvas-wallpaper` | `frameworks-base-systemui-wallpaper-canvas-fallback.patch` | we had converged on this independently during the glrenderd bringup; still the safety net when EGL has no ES2 config |
| `frameworks/base/0002-SystemServer-config.disable_atlas` | `frameworks-base-disable-atlas.patch` | the Lollipop kill-switch for the asset atlas, which assumes a GPU and kills `system_server` when it can't build one |
| `frameworks/base/0003-hwui-cap-the-reported-GL-version-at-2.0` | `frameworks-base-hwui-gles2-cap.patch` | the driver advertises ES 3.2; KitKat's hwui is only tested with ES2 behaviour |
| `frameworks/base/0004-Time-and-alarms-without-dev-alarm` | `frameworks-base-time-without-dev-alarm.patch` | `AlarmManagerService` on `*_ALARM` timerfds + `settimeofday` |
| `packages/apps/Settings/0001-kernel-version-for-a-kernel-built-with-clang` | `packages-apps-settings-kernel-version.patch` | `/proc/version` has a clang-built kernel; the stock GCC-only regex shows "Unknown" |
| `hardware/libhardware/0001-gralloc-framebuffer-stride` | `hardware-libhardware-gralloc-fb-stride.patch` | we had this one independently (mdss pads 1080→1088) |
| `frameworks/native/0001` (binder half) | `bionic-binder-uapi-64bit.patch` + `frameworks-native-binder-64bit.patch` | they credit this tree as the source |
| `frameworks/native/0002-EGL-use-the-hardware-driver` | *no patch* | it removes exactly what `frameworks/native/0001` had added for their glrenderd era; stock KitKat `checkGlesEmulationStatus` is the end state (dropping our glrenderd EGL patch was the whole port) |

## Skipped, and why

| Taimen patch | Why not here |
| --- | --- |
| `art/0001` (second copy) | maple runs Dalvik; the ART twin of the capset/time fix is dead code |
| `system/core/0005-0006 fs_config ... chroot` | the chroot design is gone (and wasn't needed for maple's libhyb: nothing under `/system/a15` is executed, `fbpresentd` is a single static binary) |
| `build/0001-definitions.mk sort (make 3.82)` | our container pins **Make 3.81**, the release's documented requirement — no reason to patch the build system for a newer host make |
| `build/0002-signed-release-keys` | we build `full_maple-eng` and flash by hand; no release-signing pipeline |
| `external/chromium_org WebKit (idl_serializer, rule_bison)` | host-tool fixes for Bison 3 / Perl 5.36; the Trusty container has period tools and the build passes unpatched |
| `frameworks/av HEVC` + `frameworks/native OMX HEVC` | media features; maple has no working video stack yet (out of scope) |
| `hardware/ril` (4 patches: AT+GSN, SIM-absent while radio off, persisted SIM state) | cellular isn't started on maple; when it is, these are *candidates*, but the modem behaviour behind them must be re-verified on yoshino firmware, not assumed |
| `system/core/0001-0002 (servicemanager/ueventd/healthd not critical)` | bring-up convenience on a tree that couldn't boot; maple's boot is past that point, and marking core services non-critical hides real failures |
| `frameworks/base/0005 bootanimation trim.txt + background colour` | only matters with a Lollipop+ boot animation; maple ships the KitKat `bootanimation.zip` (adopt if we ever ship a modern one) |
| `hardware/libhardware_legacy/0001 vibrator LED fallback` | needs a device check of which interface maple's 4.4 kernel exposes (`/sys/class/timed_output` vs `/sys/class/leds/vibrator`); queued, unverified |
| `hardware/libhardware_legacy/0002 wifi_load_driver waits for wlan0` | matches maple's failure mode (no `wlan0` because the WLFW handshake never runs) but a 30 s stall only hides it; adopt when the modem/wifi chain actually comes up (see quirks) |

## Deliberately different

- **Closure version.** Taimen loads a 32-bit Android 11 closure from
  `/system/a11` (`hyb/linker.c: s_root`); maple loads **LineageOS 22.1
  (Android 15)** from `/system/a15` — same design, a wider dependency set
  and a different flat-log/apex layout. Taimen's port is also the wider
  shape: besides libhyb it runs a 64-bit Android 11 chroot (`/system/v11`)
  for the modem/GNSS/sensor daemons, which maple does not run yet — see
  [below](#the-daemon-chroot-and-the-hal-bridges-assessed-not-ported) —
  so only the in-process half is ported here.
- **`rtc-restore` is a C binary, not a busybox script.** Taimen's
  `rtc-restore.sh` reads `/sys/class/rtc/rtc0/since_epoch` + the offset
  `system_server` saves and calls `busybox date -s @N`; maple's ramdisk
  has no busybox, so `devices/maple/kitkat/rtc-restore/` does the same
  job with `settimeofday` in C (and the identical
  `/data/system/time/rtc_offset` file that the frameworks patch writes).
- **`TARGET_BOARD_PLATFORM`/`TARGET_SYSTEM_PROP`/`egl.cfg` layout** follow
  the maple tree (`gralloc.maple`, `BOARD_EGL_CFG := vendor/sony/maple/egl.cfg`),
  and the MDSS structs are vendored from **maple's dumped kernel headers**,
  not Taimen's. `hwui` is enabled on both (`USE_OPENGL_RENDERER := true`);
  Taimen additionally sets `TARGET_DISABLE_TRIPLE_BUFFERING := true`,
  which maple does not (candidate tuning, needs measurement).

## The daemon chroot and the HAL bridges (assessed, not ported)

Their second device-tree implementation is a 64-bit Android 11 chroot,
`/system/v11`, that runs the Qualcomm daemons (modem, wifi, sensors,
GNSS, NFC) with one small bridge per KitKat HAL. It is code, not patches,
so it is not in the tables above; assessed for maple 2026-10-18 from our
own dumps (static evidence — no maple device attached).

### How the design works

- **The chroot root is a directory, shipped like one.**
  `scripts/extract-blobs.sh` fills `vendor/google/taimen/v11/root` from
  the factory image (64-bit bionic + `linker64`, `/vendor/bin` daemons
  and their `/vendor/lib64` closure, ~160 files), and the
  `taimen_v11_chroot` module ships it with `LOCAL_POST_INSTALL_CMD :=
  rm -rf $(TARGET_OUT)/v11 && cp -a .../v11 $(TARGET_OUT)/v11`. The
  `cp -a` is the point: it carries empty directories and *absolute*
  symlinks (`rfs/` is full of them) that `PRODUCT_COPY_FILES` cannot.
- **`native/hwchroot.c`** (2 KB, static) enters it: `unshare(CLONE_NEWNS)`
  + make `/` private, bind `/dev`, `/proc`, `/sys` wholesale into the
  root, `chroot`, `execve` with a clean env (`HOME=/`,
  `LD_LIBRARY_PATH=/vendor/lib/egl:/vendor/lib:/system/lib:/vendor/lib64/egl:/vendor/lib64:/system/lib64`),
  and does *not* pass KitKat's `ANDROID_PROPERTY_WORKSPACE`. KitKat's
  `toolbox` has no `chroot` and its `mount` cannot bind-mount, hence the
  helper. No binder node inside: no HIDL in the chroot.
- **The daemons**: `rmt_storage` (modem EFS), `tftp_server` (serves the
  modem partition to PIL), `pd-mapper` (SERVREG_LOC), `cnss-daemon`
  (WLAN/WLFW), `irsc_util` (IPC-router security rules from
  `/vendor/etc/sec_config` — *without them non-root QMI clients hang*).
- **Startup order** (`v11.sh`, from KitKat init): synthesise
  `/dev/block/bootdevice/by-name/*` links from
  `/sys/class/block/*/uevent` (`PARTNAME=`) → mount the vfat modem
  partition, bind it at `/firmware` → set `gsm.version.baseband` from
  `version.cfg` → bind `/persist` and `/data/v11data` → `irsc_util` →
  wait for rmt/tftp → point `firmware_class.path` *only while the modem
  boots* → open and hold `/dev/subsys_modem` → insmod `wlan.ko` → wait
  for `wlan0` → `exec sleep inf` (keeps the modem fd).
- **`pm_shim.c`** ships a replacement `libperipheral_client.so` that
  answers "online": `cnss-daemon` asks pm-service over vndbinder for
  power state, and vndbinder does not exist on KitKat.
- **`logdw.c`** (32-bit static) binds `/dev/socket/logdw` and copies
  *warnings and errors only* to `/dev/kmsg` — Android 8+ liblog writes
  there, and `tftp_server` floods the kernel buffer otherwise.
- **The bridges are HAL libraries, not HIDL services.** Each bridge runs
  inside the chroot, `dlopen`s the vendor HAL *library* in-process and
  republishes it to a small KitKat HAL over an abstract `AF_UNIX`
  `SOCK_STREAM` socket; the protocol is hand-rolled fixed-width fields
  (identical layout in 32- and 64-bit processes), one header per bridge
  (`sensorbridge.h`, `gnssbridge.h`, `nfcbridge.h`). Version skew is
  translated in the bridge, where both sides are visible: NCI 2.0→1.0
  for NFC, HAL3 3.5→3.0 for camera.
- **Built out of tree with NDK r21e** (`scripts/build-native.sh`):
  64-bit static helpers, 64-bit daemons/bridges against the chroot's
  bionic, 32-bit static `logdw`, plus `busybox-static` (armhf) for the
  init scripts.

### Maple's side: what exists, what a port would reuse

Maple is the same SoC family (msm8998; cherokee WCN3990 wifi/BT, PN553
NFC) and the dump we already unpack for libhyb — **LineageOS 22.1's own
maple vendor image** — ships the daemons as 64-bit aarch64 ELF with
`interpreter /system/bin/linker64` (built for Android 28/30 bionic):
`cnss-daemon`, `pd-mapper`, `rmt_storage`, `tftp_server`, `irsc_util`,
`sensors.qti`, `loc_launcher`, `lowi-server`, `hci_qcomm_init`, plus the
HAL libraries below (verified against the dump, 2026-10-18).

| Subsystem | Their piece | Maple today | A port would need |
| --- | --- | --- | --- |
| SERVREG_LOC | `pd-mapper` in the chroot | **`pdserve`**, in-tree 32-bit SERVREG_LOC over `AF_MSM_IPC` (TEST 40) | nothing — keep `pdserve`; `pd-mapper` is superseded |
| Modem firmware to PIL | `tftp_server` + `firmware_class.path` at the modem partition | firmware staged from `maple-fw/{modem,adsp,wlan}` into `/vendor/firmware`; `modem-hold.sh` holds `/dev/subsys_modem` (TEST 38/39) | nothing — and `tftp_server` links `libqrtr.so` directly, so it stays out; staging is the path |
| Modem EFS | `rmt_storage` + the four by-name nodes | none; the modem root PD crashes every ~41 s (TEST 36) | confirm `fsg`/`fsc`/`modemst1`/`modemst2` on the live GPT, create `/dev/block/bootdevice/by-name` links (ours live under `platform/soc`), run `rmt_storage` from a chroot. Candidate explanation for the crash cycle — verify |
| IPC-router security | `irsc_util` + `/vendor/etc/sec_config` | only root QMI clients so far (`pdserve`) | install the rules before any non-root daemon; `sec_config` ships in maple's vendor |
| WLAN | `cnss-daemon` + pm shim + `wlan.ko` wait | `CONFIG_QCA_CLD_WLAN=y` (built-in) + `icnss`; BDF/calibration staged; supplicant services in place; **no `wlan0`** — the WLFW handshake never runs | `cnss-daemon` (the WLFW QMI client) from a chroot + pm shim + logdw; the modem must stay up (WLFW is a modem service). The concrete next step for maple wifi |
| Sensors | SLPI boot + `sensors.qcom` + `sensorbridge` | SLPI in the DTB (`pil_slpi_region@94300000`); vendor has `sensors@1.0-service` + impl + `sensors.qti` (`libsensor1`) | boot SLPI (`/sys/kernel/boot_slpi/boot`), dlopen `lib64/hw/android.hardware.sensors@1.0-impl.so` in a bridge — same pattern, Android-15 libraries |
| GNSS | `gnssbridge`: raw QMI LOC over `libqmi_cci` | nothing; blocked on modem stability | the same bridge shape; maple's vendor has `gnss@2.1-service-qti`, `vendor.qti.gnss@4.0-impl.so`, `libgnss.so` |
| NFC | `nfcbridge` + `nfc_nci_nxp.so` | kernel `nxp,pn553` + `qcom,nq-nci` (`CONFIG_NFC_PN553_DEVICES=y`); vendor has the **same** `nfc_nci_nxp.so` | highest-fidelity port after graphics: same controller, same HAL file names; take the `-rdynamic` symbol override and the NCI 2.0→1.0 translation |
| Bluetooth | custom `libbt-vendor` for WCN3990: baud-pulse power, EDL firmware download, 3.2→3 Mbps rewrite, IBS off | nothing; `crbtfw*.tlv`/`crnv*.bin` (cherokee) staged on the server; kernel DT has the `qca,bt-vdd-*` supplies; vendor has `hci_qcomm_init` + `libbtnv.so` | port `btinit.c` against maple's UART/GPIO and firmware names — same chip family |
| Audio PD | not part of their chroot (audio works there) | ADSP boots (`adsp-boot.sh`); playback stalls with the ADSP online (TEST 36 follow-up) | `adsprpcd` over `/dev/adsprpc-smd` from maple's vendor is the vendor path, independent of this design |
| Camera / video | `camera.taimen` (HAL3 3.5→3.0), `libstagefrighthw` + HEVC | nothing; Sony's camera provider, different sensors | pattern only — the HAL layer does not transfer (see field notes) |
| Boot marking / thermal | `bootmark` (A/B retry counter), `thermal-compat.sh` (PVC zones) | single-slot, no Pixel Visual Core | n/a |

### Corrections to earlier maple verdicts

A 2026-10-16 note ([maple quirks](../devices/maple/quirks.md) TEST 36)
concluded from the absent QRTR that "NO modern QMI userspace (pd-mapper,
cnss-daemon, rmt_storage, tftp_server) can run on this kernel, chroot or
not", and that the chroot was blocked because there is "no `linker64`
anywhere". The kernel half stands — there is no `CONFIG_QRTR` — but the
userspace half was too strong, and the checks looked at the wrong
copies:

- **`libqmi_cci` is dual-transport.** `strings` on maple's Lineage 22.1
  vendor `libqmi_cci.so` shows `qcci_ipc_router_ops` *and*
  `qcci_qrtr_ops` with a runtime probe ("QRTR NOT SUPPORTED" / "QRTR
  SUPPORTED"); `cnss-daemon`, `pd-mapper` and `sensors.qti` link it,
  and `rmt_storage` links the server-side `libqmi_csi.so`, which is
  dual too (`qcsi_ipc_router_ops` + `qcsi_qrtr_ops`). On a kernel
  without QRTR the client is expected to fall back to the legacy
  IPC-router transport — the `AF_MSM_IPC` path `pdserve` already uses
  (TEST 40). Verify on device.
- **`linker64` is in the dump.** The earlier check looked at the live
  phone mount (where apexes are separate); the Lineage system image
  carries a real `system/bin/bootstrap/linker64` (2,188,552 B, aarch64
  ELF), and `tools/libhyb/extract-lineage.sh` already de-apexes the
  runtime apex for the 32-bit closure — the 64-bit side is the same loop
  plus `lib64`.
- **Anything linking `libqrtr.so` directly** is still out —
  `tftp_server` (NEEDED `libqrtr.so`, verified), `qrtr-ns`,
  `qrtr-cfg`, `qrtr-lookup`; the fallback lives in
  `libqmi_cci`/`libqmi_csi`, not in the kernel.
- What only the device can answer: whether the legacy path completes the
  WLFW handshake and brings up `wlan0`.

### Field notes worth carrying over

- **Sensors.** KitKat flushes a sensor before enabling it and the
  Qualcomm HAL refuses flush on an inactive sensor — the KitKat-side HAL
  synthesises "flush complete" itself. One sensor per KitKat type
  (non-wake-up preferred), steps from the IMU's hardware pedometer, and
  the KitKat HAL replays its state if the bridge restarts.
- **GPS.** The QMI LOC state field is unusable — count a satellite as
  tracked when it has a valid SNR. XTRA comes from KitKat's
  `GpsLocationProvider` via `gps.conf` and is injected in 1 KiB parts;
  the QMI service object values came from `libloc_api_v02.so` (maple's
  vendor has its own copy).
- **NFC.** The NXP libraries look for the secure element over HIDL and
  would wait forever for `hwservicemanager`; the bridge exports its own
  `getRawServiceInternal` (`-rdynamic`) to win over libhidlbase's.
  `CORE_RESET`/`CORE_INIT` are translated 2.0→1.0, and
  `libnfc-brcm.conf` disables the secure element and P2P listening.
- **Camera, if it ever comes up here.** Their HAL3 3.5→3.0 write-up is
  version-skew knowledge worth not rediscovering: metadata matched by
  tag *name* (11 removed two sections), static limits rebuilt from
  `availableStreamConfigurations`, vendor tags via
  `set_camera_metadata_vendor_ops`, partial settings merged into the
  HAL's template, session parameters mandatory, results merged and
  delivered one per frame after the shutter, twin `camera3_stream_t`s.
  Maple's camera provider is Sony's — the translation transfers, the HAL
  does not.
- **Eng-only debug tools worth copying:** `kmsglog.sh` keeps the previous
  boot's kmsg in `/data/log/kmsg.prev.txt` (survives crashes);
  `blackbox.sh` dumps ps/mounts/dmesg to a raw offset 2 GiB into
  `system_a` every 2 s, after a `PARTNAME=` check that refuses to write
  otherwise.
- **A/B-only, not applicable to maple:** `bootmark` + retry counter, the
  `skip_initramfs`→`want_initramfs` kernel rename, slot tie-in.
