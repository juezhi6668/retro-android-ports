# KitKat 4.4.4 on the Sony Xperia XZ Premium (maple)

Device-specific notes for the maple port — the generic KitKat playbook
lives in [`docs/porting-notes-kitkat.md`](../porting-notes-kitkat.md).
The Donut/cooper playbook
([`docs/porting-notes.md`](../porting-notes.md),
[`docs/devices/cooper.md`](cooper.md)) is the method reference; this doc
is the maple delta.

Status: **skeleton only — does not build or boot yet.** The tree in
[`devices/maple/kitkat/`](../../devices/maple/kitkat/) is a commented
starting point synced from Sony/Lineage reference values. Work through the
blockers below in order; do not start HAL ports until blocker 1 is resolved.

## Confirmed device state (2026-10-13, owner-reported)

- Variant: single-SIM (G8141). Tree defaults (`PRODUCT_MODEL := G8141`,
  `TARGET_OTA_ASSERT_DEVICE` includes G8141 first) already match.
- TA partition backed up (DRM keys safe).
- Currently running LineageOS (bootloader unlocked, TWRP-era flashing flow
  available). Stock dump should be taken from the running LineageOS/TWRP
  via `scripts/dump-maple-stock.sh` — still valid for partitions,
  by-name nodes, framebuffer, and density facts.

## Stock dump findings (`dumps/maple-stock-20261013-213045/`)

Running **LineageOS 20 (Android 13, unofficial, built 2024-11-17)**,
kernel `4.4.302-whatawurst+` aarch64, 8× Kryo (4×0x801 + 4×0x800), platform
`msm8998`, `ro.product.device=maple`, `ro.product.board=G8141`.

- **Density: 410 measured** (`ro.sf.lcd_density=410`; dumpsys Built-in
  Screen 1080×1920 @ ~403 dpi physical). The 4K panel renders 1080p in
  normal use — `system.prop` now pins 410 (was 480 guess).
- **Framebuffer: 1080×1920 @ 60 Hz, HDR10** (HdrCapabilities type 2).
  KitKat software-rendering bringup targets this mode, not 4K.
- **Block map confirmed**: `/dev/block/bootdevice/by-name/*` with
  system→sde46, userdata→sda15, cache→sda5, boot→sde35, recovery→sde36 —
  matches `fstab.maple` (paths were already corrected from TWRP recovery
  fstab; live dump agrees).
- **32-bit compat present**: `ro.product.cpu.abilist32=armeabi-v7a,armeabi`
  — the running userspace ships 32-bit libs, a (weak) positive signal for
  blocker 1 (32-bit KitKat userspace on this kernel).
- Input: event0–event9 + mice (`system:input`, 0660-class nodes).
- Gaps (script bug: `run()` echoed into the files; fixed since — re-run
  with the `adb root` step added): `/proc/cmdline`, `/proc/partitions`,
  `/default.prop`, fb0 sysfs nodes all came back empty.

## Hardware snapshot (G8141 single-SIM / G8142 dual-SIM)

- SoC: Qualcomm Snapdragon 835 MSM8998 (10 nm, 4×2.45 GHz + 4×1.9 GHz
  Kryo 280, ARM64) + Adreno 540. 4 GB LPDDR4X, 64 GB UFS, microSDXC ≤256 GB.
- Display: 5.5" IPS LCD, 3840×2160 4K HDR (HDR10, 801 ppi, Gorilla Glass 5).
  Sony renders 1080p normally; 4K only for HDR video/gallery. Density TBD
  (reference `TARGET_SCREEN_DENSITY := 400`; skeleton `system.prop` uses 480
  as a verify-me starting point — cf. cooper's 120-vs-160 lesson).
- Stock OS: Nougat 7.1.1 → Oreo → Pie 9. Reference trees target 10/11
  (Lineage 17.1/18.1) with a 4.4 kernel (`Image.gz-dtb`).
- Battery 3230 mAh, IP65/68, USB-C, fingerprint (non-US), NFC, BT 5.0,
  WiFi a/b/g/n/ac, 19 MP rear (960 fps slow-mo) + 13 MP front.
- Codename: `maple` (single-SIM G8141) / `maple_dsds` (dual G8142).
  Platform: `yoshino`. Bootloader unlockable; TWRP + Lineage available.

## Platform specifics

### Flash layout (GPT/ext4, NOT BML/stl)

- boot/recovery 64 MB each (`BOARD_BOOTIMAGE_PARTITION_SIZE 67108864`),
  system ~5 GB, cache ~380 MB, userdata ~22 GB. `BOARD_FLASH_BLOCK_SIZE
  131072` (= pagesize 4096 × 32... actually ×64 = 262144? reference says
  131072 — re-verify; kept as reference value with TODO).
- `BOARD_KERNEL_BASE 0x00000000`, pagesize 4096, `Image.gz-dtb`.
  Fastboot-flashable (`fastboot flash boot boot.img`); no `erase_image` /
  `flash_image` BML dance like cooper. **Still verify every flash** by
  reading back (cooper rule carries over).
- Reference board params: `sony-msm8998/android_device_sony_maple` (`ten`)
  + `yoshino-common/BoardConfigPlatform.mk` (msm8998, 4.4 kernel,
  `TARGET_KERNEL_CONFIG lineage-msm8998-yoshino-maple_dsds_defconfig`).

### Kernel

- Shipped Sony msm8998 kernel is **4.4** — a decade newer than KitKat-era
  3.4/3.10 kernels. KitKat bionic + init on a 4.4 kernel is the same class
  of problem as Donut on 2.6.35 (ueventd nodes, cgroups, SELinux stubs,
  lowmemorykiller/firmware-loader paths all need auditing).
- Kernel image format `Image.gz-dtb` postdates KitKat's mkbootimg. Expect a
  mkbootimg backport (precedent:
  `patches/donut/mkbootimg-pagesize.patch`; see
  `docs/porting-notes-kitkat.md`).

### Blocker list (in order)

1. **ARM64 SoC vs 32-bit-only KitKat (HARD BLOCKER).** KitKat has no
   `arm64` target (64-bit support landed in Lollipop 5.0). Path is 32-bit
   `armeabi-v7a` userspace on the 4.4 kernel — needs a feasibility spike
   (can the yoshino kernel boot an ARM32 userspace? modem/firmware blobs
   are 64-bit; what breaks?). If the spike fails, maple/KitKat is
   infeasible and the next step is maple/Lollipop instead. **Do not invest
   in HALs before this is answered.**
2. **Toolchain/container.** KitKat needs JDK 6 + Ubuntu 14.04-era host
   (cf. Donut's Hardy/JDK5 chroot). See `docs/porting-notes-kitkat.md`.
   No container exists yet — first task.
3. **Display/framebuffer.** 4K panel + Adreno 540 have no KitKat EGL/HWC.
   First boot will be software rendering (cf. cooper `libagl` fallback) at
   a verified density; HWComposer/gralloc are later.
4. **Storage/init.** GPT block paths (`/dev/block/.../by-name/*`) must be
   confirmed from stock `/proc/partitions` + TWRP `fstab_maple.recovery`;
   init.rc/fstab skeletons are placeholders.
5. **WiFi/BT/RIL/camera/GPS.** All proprietary (BCM43xx + qcwcn, QCOM BT,
   MSM8998 RIL, QTI camera). Expect stub/generic first (cooper pattern:
   `USE_CAMERA_STUB`, rild removed, BT disabled), then per-PHAL ports.
6. **No cellular/audio/camera on first boot** — same documented-limitations
   posture as cooper (see Status below).

### What cooper teaches maple directly

- `TARGET_BOARD_PLATFORM := maple` (not `msm8998`) until a QCOM HAL path
  is proven — same move as cooper dodging `hardware/msm7k`.
- Density must be measured, not guessed (cooper 120→160).
- Every flash verified by readback (cooper BML rule, applies to fastboot too).
- Provisioning/`device_provisioned`, keymaps, `TARGET_PROVIDES_INIT_RC`,
  updater-script arities — all re-audited for KitKat in
  `docs/porting-notes-kitkat.md` (KitKat Edify has `run_program` + ext4
  `format`, unlike Donut — easier than cooper here).
- Keep blobs out of git (`.gitignore` covers `aosp-kitkat/`, `out/`, `work/`).

## Status

Skeleton synced from reference values; **no build, no boot, no blobs yet.**
Pre-build tooling is in place (no server needed for these):
`containers/kitkat/Dockerfile` (untested Trusty/JDK6 recipe),
`scripts/dump-maple-stock.sh` (stock facts over adb → `dumps/`, gitignored),
`scripts/sync-device-tree.sh maple kitkat` (verified).
Next actions (checkboxes for the log):

- [x] Skeleton tree + init/fstab skeletons (block paths corrected to
      `/dev/block/bootdevice/by-name/*` from TWRP `fstab_maple.recovery`,
      confirmed by live `by-name` dump: system→sde46, userdata→sda15,
      cache→sda5)
- [x] KitKat container recipe + stock-dump script (script fixed: echo went
      to stdout, `adb root` step added — re-run needed for cmdline/fb gaps)
- [x] Display density measured: **410** (live `ro.sf.lcd_density` + dumpsys
      1080x1920@403dpi); `system.prop` pinned, framebuffer target 1080x1920
- [x] `repo sync` clean on build server (shallow `--depth=1 -c --no-tags`,
      14 GB); `lunch full_maple-eng` passes (BUILD_ID=KTU84Q)
- [x] `boot.img` verified: ANDROID! magic, kernel 16744559 B (exact match
      to TWRP-dumped kernel), base 0 + 0x8000, pagesize 4096 — KitKat
      mkbootimg handles `Image.gz-dtb`/base-0 natively, no backport needed.
- [x] First `patches/kitkat` entry: `build-jdk-openjdk6.patch` (accept
      OpenJDK 6; Oracle JDK 6 uninstallable in 2026) + `python` symlink and
      32-bit runtime libs (`lib32stdc++6 lib32z1`) in container.
- [x] First full `make -j6`: userspace compiles (host tools, aidl, java);
      stops only at `No rule to make target vendor/sony/maple/kernel`.
- [x] wpa_supplicant saga resolved: qcwcn config was RIGHT (KitKat's
      `driver_nl80211.c` declares p2p/driver_cmd hooks that only
      `lib_driver_cmd_qcwcn` defines; generic no-driver config dies on
      `sme_*` because nothing sets NEED_SME). `lib_driver_cmd_qcwcn`
      builds; supplicant links. Wifi still non-functional until driver
      port (blocker 5) — linkable only.
- [x] ext4 fix: KitKat defaults to yaffs2 without
      `TARGET_USERIMAGES_USE_EXT4`; set ext4 for system/userdata/cache.
- [x] COMPLETE IMAGE SET 2026-10-13: `boot.img` 17 MB (verified geometry),
      `system.img` 284 MB (sparse ext4, magic ok), `cache.img`,
      `userdata.img`. Blocker 1 (ARM32 userspace on MSM8998 kernel) goes
      to live fire: flash test + log capture.
- [x] TEST 1 flashed (TWRP dd, hashes verified) → stuck at Sony logo, no
      USB. Root cause found in built ramdisk: **no `/init.rc`**
      (`TARGET_PROVIDES_INIT_RC=true` suppressed the stock one, tree
      shipped no replacement) → init idles with zero actions/services.
      Fix: `devices/maple/kitkat/init.rc` (stock KitKat init.rc + `import
      /init.maple.rc`), wired into `full_maple.mk`. TEST 2 `boot.img`
      (17,080,320 B, init.rc confirmed in ramdisk) flashed (boot only).
- [x] TEST 2: kernel booted, init RAN, then `reboot to recovery` — ramoops
      (`console-ramoops-0` via TWRP pstore) shows `/system` never mounted
      (`cannot find /system/bin/*` for every service; mkdirs landing on
      rootfs) → root cause: TEST 1 `dd`'d the **sparse** system.img raw
      (garbage ext4, unmountable). Fix: raw ext4 via simg2img pushed to
      `/data/media` + dd (5 GB, magic `53 ef` verified on-partition).
- [x] TEST 3/4 (init.rc present, kmsg markers): markers never appear —
      `write /dev/kmsg` from init is unobservable (SELinux/policy), and
      `do_mount` fails SILENTLY. Lessons recorded, markers removed.
- [x] TEST 5 (`wait_for_coldboot_done` + RAW sde/sda nodes + fixed mount
      syntax — KitKat takes space-separated flags, single trailing options
      arg): `/system` (sde46) AND `/data` (sda15) MOUNTED, all services
      found. New stop: `healthd` exits 4x → recovery reboot at 25 s.
- [x] BLOCKER-1 ROOT CAUSE, corrected: TWRP-side 32-bit SEGV was
      environmental (recovery ramdisk lacks `/system/bin/linker`; the PIE
      linker resolves its own path and fails without it). On-boot
      servicemanager shows NO crash-loop (no critical message), so 32-bit
      exec on the 4.4 kernel works — kernel has CONFIG_COMPAT, flash
      persists. The cortex-a15 rebuild stands (correct variant for Kryo
      regardless) but was not the healthd fix.
- [x] TEST 6 (cortex-a15 system): mounts hold, all services found, healthd
      loop persists.
- [x] TEST 7 (independent mount stanzas — a failing mount aborts its
      action SILENTLY in KitKat init; vold + rild disabled per cooper
      precedent; logcat-to-file service): only system+userdata mount
      (cache sda5 + persist sda4 fail, now isolated, non-fatal). Still no
      `on boot` (no logcat file) + healthd loop.
- [ ] BLOCKER: `/data` (sda15) is Lineage-FBE-encrypted (garbage filenames
      in `/data/system`). KitKat vold parks in decrypt-wait, post-fs-data
      copy/mkdir/restorecon all fail on FBE, `on boot` never fires.
      → userdata MUST be formatted ext4 (TWRP wipe, cooper RFS→ext4
      precedent). Awaiting owner approval (destroys Lineage apps/data).
- [ ] Kernel (`Image.gz-dtb`): critical path — need owner's Lineage build
      output. `make -j6 systemimage` running meanwhile to validate the rest
      of userspace.
      → RESOLVED 2026-10-13: dumped live from TWRP
      (`adb pull /dev/block/bootdevice/by-name/boot`, 64 MB, ANDROID!
      magic, pagesize 4096, base 0). Kernel (16.7 MB gzip, `Image.gz-dtb`)
      split with abootimg on the server → `vendor/sony/maple/kernel`
      (out-of-band blob, NOT in git — same policy as cooper blobs).
      `BOARD_KERNEL_CMDLINE` updated from the live bootimg config.
      Full `make -j6` running.
- [ ] Re-run `scripts/dump-maple-stock.sh` (fixed version) for the missing
      `/proc/cmdline`, `/proc/partitions`, fb0 sysfs nodes
      → re-ran clean (no disconnect); root-gated nodes still empty under
      the shell user. Get them from TWRP recovery adb (runs as root) later;
      nothing essential is blocked.
- [ ] Run `scripts/dump-maple-stock.sh` against stock/TWRP maple; fix
      `fstab.maple` + `system.prop` density against the dump
- [ ] `repo init -u https://android.googlesource.com/platform/manifest -b android-4.4.4_r2` → `aosp-kitkat/` (or confirm exact `_r` rev)
- [ ] Build the container on the build server, first `lunch full_maple-eng`
- [ ] ARM32-userspace-on-msm8998 feasibility spike (blocker 1 verdict)
      → VERDICT: VIABLE. Binder was the blocker, now fixed (below).
- [x] TEST 8-13 (binder saga): servicemanager/healthd died on binder
      WRITE_READ EINVAL. Root cause: KitKat's 24-byte binder_write_read
      (ioctl 0xc0186201) vs the v8-only kernel's 40-byte layout
      (0xc0286201) — plus v7-vs-v8 protocol, 4-byte Parcel offsets vs
      8-byte, 20-byte flat objects vs 24-byte. Fix:
      `patches/kitkat/binder-uapi-64bit.patch` (64-bit-clean UAPI header,
      Parcel mObjects u64 + hdr.type + casts, IPCThreadState casts +
      read/writeInt64 + 8-byte offsets, servicemanager local structs).
      ENTER_LOOPER res=0 live. Full details in commit message.
- [x] TEST 14: system_server boots, dexopt starts. surfaceflinger aborted
      in eglGetDisplay (10 tombstones) — KitKat's EGL loader skips
      libGLES_android.so. Fix: `egl-use-software-renderer.patch`.
- [x] TEST 15: NO recovery reboot, black screen + backlight, no new
      tombstones, dalvik-cache 1 → 15 jars. Boot is progressing through
      dexopt. One reply-path write still EINVAL (non-fatal; suspect
      FREE_BUFFER/death layout — revisit).
- [ ] Let first boot dexopt finish (~30-40 min), then check UI.
      → did NOT finish: dalvik frozen at 15 jars across all later boots.
- [x] TEST 16-19 (stall hunt): load pinned ~6.5, zero CPU anywhere, no
      crashes. Ruled out: SELinux audit storm (permissive changed nothing),
      storage errors (early kmsg clean, TWRP I/O fast), battery (100%).
      Watcher evolution: loadavg → +proc snapshots (toolbox `ps` prints
      nothing, `sort` SEGVs on this kernel — both avoided; pure sh +
      cat/grep) → +meminfo/dmesg (`head`/`tail` output vanishes, `dmesg`
      via pipe gives debuggerd SIGPIPE tombstones — write whole file
      instead) → +wchan per-PID.
- [x] TEST 20-21 (wchan): all userspace healthy-idle (binder reads, polls;
      zygote + mediaserver + netd + bootanimation ALL present). NO
      system_server in any sample — zygote never forks it. Load 6+ comes
      from kernel threads, not a userspace spin. Stall is UPSTREAM of
      system_server completion: PM/dexopt frozen because system_server
      isn't finishing (not because dexopt is slow).
- [x] TEST 22 (autowinch): `sbin/autowinch.sh` + service returns the phone
      to TWRP 15 min after boot — no more manual button dances. Full
      flash→boot→pull loop now runs unattended (except screen watch).
- [ ] NEXT: find why zygote never forks system_server. Prime suspects:
      (a) zygote stuck preloading (EGL/dlopen? needs fd/maps probe —
      watcher has a zygote-fd section staged in test21+),
      (b) MountService/volume wait (CHARQUIRKS: needs storage_list.xml +
      nosdcard or MountService NPEs; vold disabled),
      (c) reply-path EINVAL breaking a boot-critical RPC (addService with
      objects? FREE_BUFFER/death layout still suspect).
- [ ] PENDING (blocked on build-server access): formalize
      `patches/kitkat/binder-uapi-64bit.patch` (bionic binder.h,
      Parcel.h/Parcel.cpp, IPCThreadState.h/.cpp, servicemanager
      binder.h/binder.c/service_manager.c — all edited live on server,
      diffs not yet pulled) + `egl-use-software-renderer.patch`
      (Loader.cpp) + REMOVE temp diag (smdbg/bdbg in service_manager.c
      + binder.c, dump-boot-log/autowinch services). Then:
      `TARGET_NO_KERNEL := true`, USB adb via configfs gadget
      (CHARQUIRKS recipe), storage phase, kernel rebuild (logger +
      alarm drivers).
