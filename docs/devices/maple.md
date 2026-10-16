# KitKat 4.4.4 on the Sony Xperia XZ Premium (maple)

Device-specific notes for the maple port — the generic KitKat playbook
lives in [`docs/porting-notes-kitkat.md`](../porting-notes-kitkat.md).
The Donut/cooper playbook
([`docs/porting-notes.md`](../porting-notes.md),
[`docs/devices/cooper.md`](cooper.md)) is the method reference; this doc
is the maple delta.

Status: **BOOTED 2026-10-15** — full KitKat userspace on the 4.4 kernel:
`sys.boot_completed=1`, 75 binder services, Launcher compositing at
1080x1920, working touch, USB adb (`18d1:4ee7`). **Hardware-accelerated
GL/GLES verified 2026-10-15** (Adreno 540 via glrenderd, GLES 3.2). The
tree in [`devices/maple/kitkat/`](../../devices/maple/kitkat/) is a
commented, boot-verified device tree. Remaining gaps (no
cellular/audio/camera/WiFi yet — same documented-limitations posture as
cooper early on) are tracked at the bottom; blockers 1–4 below are all
resolved.

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

### Blocker list (all resolved 2026-10-15 — kept for the record)

1. **ARM64 SoC vs 32-bit-only KitKat — VERDICT: VIABLE.** 32-bit
   `armeabi-v7a` userspace (cortex-a15) runs on the 4.4 kernel
   (CONFIG_COMPAT); the only kernel-side port needed was the binder
   64-bit UAPI (below). Modem/firmware blobs stay untouched (no
   cellular yet).
2. **Toolchain/container — DONE.** Trusty + JDK 6 container
   (`containers/kitkat/Dockerfile`), `build-jdk-openjdk6.patch`,
   `lib32stdc++6 lib32z1`, `python` symlink. Full `make -j6` green.
3. **Display/framebuffer — HARDWARE GL WORKING 2026-10-15.** No KitKat
   EGL/HWC for Adreno 540, so EGL routes through glrenderd (Adreno in a
   Lineage chroot) with libagl as the software fallback. glrenderd is
   the single fb0 flipper (SF posts via rcFBPost); verified on cold boot
   at 1080x1920 (`GLES: Qualcomm Adreno 540, ES 3.2`). See the gpu entry
   below and TEST 30 for the three fixed root causes. No DTB/driver work
   needed for display.
4. **Storage/init — DONE.** GPT mounts by raw node
   (system→sde46, cache→sda5, userdata→sda15, persist→sda4);
   `/dev/block/bootdevice/` does NOT exist on KitKat — by-name lives
   under `/dev/block/platform/soc/by-name/`. ext4 everywhere
   (`TARGET_USERIMAGES_USE_EXT4`, TWRP mke2fs for wiped partitions).
5. **WiFi/BT/RIL/camera/GPS.** All proprietary (BCM43xx + qcwcn, QCOM BT,
   MSM8998 RIL, QTI camera). Still stub/generic (cooper pattern:
   `USE_CAMERA_STUB`, rild removed, BT disabled) — per-HAL ports are
   the next work after input/display.
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

Boots to UI with touch + adb (2026-10-15 — see TEST 24–29 for the
bringup log). Latest (TEST 33, 2026-10-16): the poplar reliability set +
lights HAL + keyguard-scrim fix are live — systemui survives navbar
stress, GL stays on Adreno 540 through glrenderd, brightness/sleep work.
Pre-build tooling (no server needed for these):
`containers/kitkat/Dockerfile` (Trusty/JDK6, verified),
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
- [x] BLOCKER, resolved 2026-10-14 (no approval needed — data was
      already gone): `/data` (sda15) AND `/cache` (sda5) were found fully
      zeroed, no filesystem at all (`EXT4-fs (sda15): VFS: Can't find ext4
      filesystem` in kmsg, blkid blank, head + 1 MB samples all zero; the
      Lineage FBE data was already destroyed, cause unknown — an erase
      without format). Only `/system` mounted, rootfs stayed ro so every
      `/data/property` write failed, installd/keystore sat in `restarting`
      and boot never completed. Fix: formatted both ext4 live in TWRP
      (`mke2fs -t ext4 -b 4096 -L userdata/cache`). Tree fix in the same
      pass: `mkdir /cache + /persist` before the mounts in `init.maple.rc`
      (those mounts failed ENOENT SILENTLY — no ramdisk dirs, nothing in
      kmsg), drop the invalid `wait_for_coldboot_done` (KitKat init has no
      such command), drop the duplicate `import /init.maple.rc` (all five
      services logged as ignored duplicates), fstab by-name paths now
      `/dev/block/platform/soc/by-name/*` (`/dev/block/bootdevice/` does
      not exist on KitKat).
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
- [x] Re-run `scripts/dump-maple-stock.sh` (fixed version) for the missing
      `/proc/cmdline`, `/proc/partitions`, fb0 sysfs nodes
      → re-ran clean (no disconnect); root-gated nodes still empty under
      the shell user — pulled from TWRP recovery adb (runs as root)
      instead; nothing essential is blocked.
- [x] Run `scripts/dump-maple-stock.sh` against stock/TWRP maple; fix
      `fstab.maple` + `system.prop` density against the dump → done
      (platform/soc by-name paths, density 410 pinned).
- [x] `repo init -u https://android.googlesource.com/platform/manifest -b android-4.4.4_r2` → `aosp-kitkat/` (shallow `--depth=1 -c --no-tags`)
- [x] Build the container on the build server, first `lunch full_maple-eng`
- [x] ARM32-userspace-on-msm8998 feasibility spike (blocker 1 verdict)
      → VERDICT: VIABLE (see TEST 26 + binder saga below).
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
- [ ] TEST 24 (after the TWRP ext4 format of userdata/cache): reboot
      to KitKat, verify `/data` + `/cache` + `/persist` all mount,
      installd/keystore leave `restarting`, system_server appears under
      zygote, dexopt proceeds past the 15 boot jars. (A 15-min autowinch
      fired mid-diagnosis on 2026-10-14 and returned the phone to TWRP
      before `ps`/`service list` could be captured — consider disabling
      the autowinch start now that adb works.)
      → PARTIAL 2026-10-14/15: reboot showed `/data` (sda15) MOUNTED
      (`EXT4-fs (sda15): mounted ... noauto_da_alloc,barrier=0`, no more
      property errors), installd/keystore `running`, `/data` fully
      populated, servicemanager answers (16 native services incl.
      SurfaceFlinger/batterypropreg/keystore). system_server now FORKS
      regularly and reaches PMS apk scan (WM/AM/PackageManager threads,
      SettingsProvider + batterystats writes) but each life dies ~10 s in
      during `collectCertificatesLI` manifest parse (traces.txt via
      SIGQUIT: main in `CaseMapper.toLowerCase` ← `Attributes$Name.
      hashCode` ← `ManifestReader.readEntries` ← `scanPackageLI` ←
      `scanDirLI`), Java-level (no tombstones, logcat blind), dalvik-cache
      stays empty (dies before dexopt). Zygote daemon ALSO cycles every
      ~10-60 s with no tombstone/kmsg (prime suspect: RuntimeException
      path in ZygoteInit.main — blind without logcat). `stop autowinch`
      used live to defuse the 15-min fuse.
- [x] TEST 25 (diag build 2026-10-15): TEMP file markers added on the
      build server (ZygoteInit preload/fork/death → /data/misc/zygote.txt,
      ServerThread throwable → /data/misc/syscrash.txt, PMS per-apk →
      /data/misc/pmscan.txt), system+boot rebuilt (WITH_DEXPREOPT already
      on), flashed via TWRP (simmg2img raw 5 GB + 17 MB boot, both md5
      readback-verified) + reboot. Phone went DARK after `adb reboot`
      (blank screen, red LED, no USB at all — no adb/fastboot/enumeration;
      UART silent) and needs physical power-on (no remote hands). Images
      verified structurally sound (ANDROID!/sparse/ext4 magics, ramdisk
      contains usb.rc + usbudc.sh + fixed init files, markers in
      services.jar/framework2.jar, cmdline keeps androidboot.hardware).
      Suspect power event, not image content — TBR on power-on.
- [x] TEST 26 (heap fix 2026-10-15): ROOT CAUSE of the death loop —
      `java.lang.OutOfMemoryError` in `JarUtils.verifySignature` on the
      FIRST apk (`/system/framework/framework-res.apk`, 11.8 MB) every
      life; build.prop had NO dalvik heap tuning (Dalvik defaults on a
      4 GB phone). Fix: inherit
      `frameworks/native/build/phone-xhdpi-2048-dalvik-heap.mk`
      (192m/512m) in full_maple.mk. Result: scan sails through 54 pkgs,
      **75 services incl. activity/package/window**, ONE stable zygote
      life, zero syscrash, boot animation completes, **Android UI on
      screen**. WITH_DEXPREOPT keeps dalvik-cache empty (by design).
- [ ] TEST 27 (USB-ABD 2026-10-15): full boot has NO USB (gadget never
      binds; KitKat adb unreachable). kmsg shows adbd alive but ZERO
      usbudc lines — the `on property:sys.usb.config=adb` trigger never
      fires once system_server runs (UsbDeviceManager owns that property).
      Fix (in tree, flashed): `start usbudc` unconditionally `on boot`
      next to the adbd safety net. Display also garbled (hsync-tear —
      stride/timing; needs adb + dumpsys/fb0 probe, after USB works).
      TEMP diag reverted; server↔repo re-verified byte-identical.
      → ROOT CAUSE FOUND 2026-10-15: `import /init.maple.usb.rc` was
      missing from init.maple.rc — the import (and the usb files) lived
      only on the build server and an earlier repo→server sync dropped
      the import line, so usb.rc was copied to ramdisk but never parsed,
      `usbudc` unknown, `start usbudc` a silent no-op. Fix: import
      restored (+ unconditional `start usbudc` kept as belt-and-braces).
      Rebuilt boot-only, flashed, rebooted: **18d1:4ee7 enumerates,
      `maplekk device` in adb, `sys.boot_completed=1`, 75 services,
      /system+/data+/cache+/persist ALL mounted, Launcher compositing.**
      KitKat on maple is BOOTED. Remaining: display hsync-tear, autowinch
      still armed (reboots to recovery 15 min after every boot — remove
      once USB is trusted), TEMP diag already reverted.
- [x] TEST 28 (display 2026-10-15): panel shows stale xboot splash while
      fb0 holds perfect frames (launcher decodes clean) — mdss never
      presents fb0 (white-frame + unblank-cycle tests both no-ops
      visually; pan stuck 0,0). Sub-findings: (a) fb0 stride is 4352 B
      (1088 px) but gralloc reported 1080 → every row sheared 8 px into
      a parallelogram — FIXED in `gralloc_alloc` (report
      `finfo.line_length` stride for `HW_FB`; formalized as
      `patches/kitkat/hardware-libhardware-gralloc-fb-stride.patch`,
      live-pushed + verified by inverse shear in fb0 decode);
      (b) glrenderd IS running (12 fps, correct-stride copies) but
      computed `flips=0` at startup (fb HAL doubles yres_virtual later;
      startup race) — cosmetic while fbdev flips also stall;
      (c) panel liveness is RACY across boots (live in 2 of 4 observed
      boots: overlay start vs MDP idle-collapse/splash-handoff timing;
      the single hard error ever seen is `mdp ctl start failed`
      (EBUSY) on a mid-boot glrenderd restart). glrenderd stays OFF for
      now (removes the second uncoordinated flipper; its flips=0
      startup race also needs fixing before it returns), SF-only flips
      drive the panel. If a future boot freezes on the splash, suspect
      this race first (pan stuck + EBUSY in kmsg). No DTB/driver work
      needed for basic display.
      Autowinch REMOVED from tree (adb is trusted now).
      → RESOLVED 2026-10-15: glrenderd's uncoordinated FBIOPAN flips were
      wedging the mdss overlay start (`mdp ctl start failed`, EBUSY) and
      freezing the panel on the splash. With glrenderd NOT started (pure
      SF+libagl), the panel is live and (with the stride fix) CORRECT.
      → RE-ARCHITECTED 2026-10-15 (charquirks single-flipper): root cause
      was mixed-mode (SF latched software when glrenderd started late) +
      the flips=0 race. `start glrenderd` moved to post-fs-data (before
      SF probes) and `open_panel` waits-then-doubles yres_virtual itself.
      glrenderd verified live (listening, flips 1, no EBUSY). Pending:
      rebuild + reflash + boot verification (server unreachable).
- [x] TEST 29 (touch 2026-10-15): touches reach the kernel (clearpad MT
      events) and the dispatcher (RecentQueue) but nothing responds.
      Root cause: systemui crash-loop (`eglConfig not initialized` in
      ImageWallpaper — needs ES2, libagl is ES1-only, glrenderd down;
      ~1000 dropbox crashes) kills the keyguard window each life, and
      PhoneWindowManager parks a zombie fullscreen KeyguardScrim
      ("Removing keyguard window (Did it crash?)") that eats all input.
      Fix: `ImageWallpaper` falls back to the canvas path when EGL init
      throws (formalized as
      `patches/kitkat/frameworks-base-systemui-wallpaper-canvas-fallback.patch`;
      APK+odex live-pushed, takes effect after reboot). Result: systemui
      stable, scrim gone, launcher focused, **touch works perfectly**.
      (Touch mapping is fine — firmware sends full-range coords.)
- [x] SYNC AUDIT 2026-10-15 (server vs repo): all 7 patches/kitkat
      identical both sides AND match the live tree exactly (reverse-apply
      checks pass); no stray smdbg/bdbg left. Server was AHEAD in 4
      places — pulled into the repo (uncommitted): `init.maple.usb.rc` +
      `usbudc.sh` (new files, the ADB-via-configfs bringup),
      BoardConfig `WITH_DEXPREOPT := true`, full_maple.mk usb.rc/usbudc
      lines. devices/maple/kitkat now byte-identical server↔repo
      (excluding the kernel blob).
- [x] NEXT (resolved by TEST 26): zygote DOES fork system_server —
      the earlier "never forks" samples were the PMS heap-OOM death loop
      (each life died in `collectCertificatesLI` before forking).
      MountService/volume waits and the reply-path EINVAL did not block
      the boot once the heap was sized.
- [x] PENDING (unblocked): all live-server edits formalized —
      `bionic-binder-uapi-64bit` + `frameworks-native-binder-64bit`
      (+ egl/glrenderd set) are in `patches/kitkat/` and verified
      identical to the live tree (reverse-apply checks); smdbg/bdbg
      temp diag gone; `boot.img` is packed by `make bootimage` from the
      dumped prebuilt kernel (`TARGET_PREBUILT_KERNEL`, NOT in git) +
      built ramdisk; USB adb via configfs gadget works
      (`init.maple.usb.rc` + `usbudc.sh`); no kernel rebuild needed
      (logger-less logging via files + dropbox + debuggerd).
- [x] TEST 23 (superseded): the 15-jar wedge was the binder 64-bit ABI
      gap (24-byte vs 40-byte `binder_write_read`, now patched) plus the
      PMS heap sizing — NOT a lost-reply stall. system_server runs,
      dexopt is a non-issue (WITH_DEXPREOPT preopts at build).
- [x] storage (working): internal is emulated, `/data/media` through the
      `sdcard` fuse daemon at `/mnt/shell/emulated`, exposed as
      `/storage/emulated/legacy` with the `/sdcard` links (nexus 5
      kitkat layout, live-verified mounted). overlay `storage_list.xml`
      marks it emulated/primary. `sdcard` runs as `class main`
      (no `late_start` needed since mounts are by hand, not `mount_all`).
      Microsd (`sdcard1` via vold on
      `/devices/soc/c0a4900.sdhci/mmc_host`, vfat only) still UNTESTED —
      next storage item.
- [x] gpu — HARDWARE-ACCELERATED GL/GLES VERIFIED 2026-10-15 (single-flipper): no
      KitKat adreno driver for the 540, so the emulator GL stack is the transport.
      `glrenderd` (adreno in a Lineage-20 chroot) owns the panel; surfaceflinger
      composes on the host via eglimages and posts `rcFBPost` over the `glrenderd`
      abstract socket, so glrenderd is the ONLY flipper. Cold boot now comes
      up straight on the Adreno: `dumpsys SurfaceFlinger` → `EGL implementation: 1.4`,
      `GLES: Qualcomm, Adreno (TM) 540, OpenGL ES 3.2 V@331.0`, settings/launcher
      render, and a GLES2 shader triangle test rasterizes + reads back correctly.
      Five root causes, all fixed:
        (a) two flippers — starting glrenderd after boot leaves SF latched to the
            software renderer (the egl/gralloc emulation probe is cached for the
            process lifetime), so SF pans fb0 AND glrenderd pans → `mdp ctl start
            failed` (EBUSY, panel frozen on splash). fix: `start glrenderd` in
            `post-fs-data`, before SF's first probe, so SF takes the emulator path
            (rcFBPost, never pans fb0). charquirks single-flipper design.
        (b) flips=0 startup race — nobody opens the fb HAL in glrenderd mode so
            fb0's `yres_virtual` is still single-buffered when glrenderd samples it;
            glrenderd then memcpys without ever panning. fix: `open_panel`
            (tools/glrenderd/src/glrenderd.cpp) doubles it itself via
            `FBIOPUT_VSCREENINFO` exactly like the fb HAL would (immediately — a
            wait-then-double version added ~10s to socket bind, see (c)).
        (c) probe window too short — glrenderd's cold-boot startup to "listening" is
            ~0.2s once the pointless fb-HAL wait is gone, but with it was ~10s and
            SF's original 5s wait missed it (fell back to software, never
            reconsiders). fix: the egl + gralloc probes now wait up to 30s
            (`kRenderdWaitTries`/`kWaitTries` = 300).
        (d) wrong flip ioctl — glrenderd flipped with `FBIOPAN_DISPLAY`; maple's
            `mdss_mdp_overlay_pan_display` logs `unable to allocate base pipe` and
            returns SUCCESS without moving the offset, so the panel never changed
            while fb0 held correct frames (screenshots looked right, the panel was
            black). fix: flip with `FBIOPUT_VSCREENINFO` + new `yoffset`, exactly
            what the working stock gralloc's `fb_post` does. (the ported comment
            about `set_par` "redoing the whole mode" was from pdx203's drm path,
            not this fbdev one.)
        (e) mdss idle power collapse — `qcom,mdss-idle-power-collapse-enabled` in
            the DT powers the MDP down when the display goes idle; the panel then
            goes black (dim backlight, no scanout) and the next flip is stalled, so
            a static UI vanishes until touched. fix: strip the property from every
            appended DTB (scripts/patch-maple-dtb.py, the pdx203-jellybean fix).
            Verified on-device: the UI now stays up.
      ES2 gap closed: hwui/live-wallpaper ES2 now works (no more canvas-only
      fallback on this path; `ImageWallpaper` canvas fallback stays as the safety
      net when glrenderd is stopped). libagl software fallback still renders a
      correct 1080x1920 UI when glrenderd is absent. glrenderd design (ported from
      [pdx203-jellybean](https://github.com/chardidathing/pdx203-jellybean)
      by @chardidathing): emulator gl libs stream over the `glrenderd`
      abstract socket to the daemon (32-bit, in a chroot of lineage 20's
      system+vendor), which draws on adreno, reads back, pans fb0.
      Build: apply the four `*glrenderd*` patches, then
      `tools/glrenderd/build.sh $AOSP_DIR <android-ndk-r21e>`,
      `tools/glrenderd/extract-lineage.sh <lineage 20 maple zip>
      <sysroot> [oem.img]`, `tools/glrenderd/stage.py <sysroot>
      tools/glrenderd/out $AOSP_DIR/vendor/sony/maple-glr` (oem ==
      lineage vendor; dump it from TWRP if the zip is gone). Protocol
      unchanged from 4.2; daemon built against the KK sdk.
      Kernel blob: after dumping `Image.gz-dtb` to
      `vendor/sony/maple/kernel`, run
      `scripts/patch-maple-dtb.py vendor/sony/maple/kernel` to strip
      `qcom,mdss-idle-power-collapse-enabled` from the appended DTBs
      (needs `dtc`); the blob is out-of-git so the patch is not applied
      automatically. Then `make bootimage` packs the patched kernel.
      Build server note: NDK r21e is now installed at `~/android-ndk-r21e`
      (fetched from dl.google.com; the build script needs it + a host C++
      compiler + python3).
      KNOWN INTERMITTENT (separate from GL): the KitKat keyguard can rarely wedge
      into the documented zombie `KeyguardScrim` (a boot/unlock race in
      `KeyguardViewManager`/PhoneWindowManager, not GL). The keyguard window add
      throws `BadTokenException: another window of this type already exists` once,
      `removeWindowLw` then shows the scrim and the UI is stuck behind it until a
      reboot. Reproduces ~1 boot in several; not caused by glrenderd (the same loop
      predates it). Recovery: screen off/on (`input keyevent 26` twice) re-fires
      keyguard `onShown` → `hideScrim`, which cleared it live; a reboot also works.
      Non-secure keyguard unlocks with MENU (`input keyevent 82`). Real fix is the
      WMS/keyguard race, not yet done.
- [x] touch and buttons (touch TESTED working 2026-10-15): clearpad
      probes after `post_probe_start` in post-fs-data (as lineage);
      `clearpad.idc` + full-range firmware coords map 1:1 (no scaling
      fix needed); `gpio-keys.kl` shipped (volume keys dispatch
      verified: keyCode 24 in RecentQueue); power via qpnp_pon +
      Generic.kl; nav bar on (`config_showNavigationBar`). Two-stage
      camera key (528/766) UNTESTED.
- [x] TEST 30 (HARDWARE GL, 2026-10-15): brought glrenderd up as the default
      renderer and verified GLES 3.2 on the Adreno 540 end to end.
      Evidence: `dmesg` → `glrenderd: starting (pid N)` at 4.95s, `doubled fb0
      virtual to 1080x3840`, `panel 1080x1920 stride 4352 flips 1`, `listening
      on @glrenderd` at 5.19s; `dumpsys SurfaceFlinger` → `EGL implementation
      1.4`, `GLES: Qualcomm, Adreno (TM) 540, OpenGL ES 3.2 V@331.0`, flips
      climbing; an ES2 pbuffer test (`test_3d`: red clear + blue shader
      triangle) reads back 20808 triangle px / 44728 clear px, `glGetError=0`;
      Settings renders and fb0 decodes clean. Fixes (all in-tree):
      `start glrenderd` moved to post-fs-data (before SF's cached emulation
      probe — kills the two-flipper EBUSY wedge), `open_panel` doubles fb0's
      yres_virtual immediately (nobody else does — kills the flips=0 race),
      and the egl/gralloc emulation probes wait 30s (glrenderd's bind is slow
      on cold boot; 5s missed it and SF latched software for the whole life).
      Also: NDK r21e reinstalled on the build server (`~/android-ndk-r21e`);
      `frameworks-native-egl-glrenderd.patch` + `device-generic-goldfish-
      glrenderd.patch` updated to the 300-try wait; glrenderd rebuilt and
      live-pushed (`/system/glr/root/glrenderd`, `/system/lib/libEGL.so`,
      `/system/lib/libOpenglSystemCommon.so`, `gralloc.maple.so`); boot.img
      reflashed (start-in-post-fs-data, readback-verified). Test binaries
      (`test_egl*`, `test_3d`) left in `/data/local/tmp`. A separate
      intermittent keyguard-scrim race is noted above (not GL).
- [x] TEST 31 (PANEL PRESENTATION, 2026-10-15): GL was working but the physical
      panel showed a dim/black image while fb0 and screenshots held correct
      frames. Two independent causes, both fixed:
      (d) glrenderd flipped fb0 with `FBIOPAN_DISPLAY`; `dmesg` showed
      `mdss_mdp_overlay_pan_display: unable to allocate base pipe` (ioctl returns
      success, offset never moves). The working software path's stock gralloc
      flips with `FBIOPUT_VSCREENINFO` + `yoffset`; switching glrenderd to that
      made `/sys/class/graphics/fb0/pan` toggle and killed the base-pipe errors.
      (e) `qcom,mdss-idle-power-collapse-enabled` powered the MDP down when idle
      → panel black until touched. Removed it from all 4 appended DTBs
      (`scripts/patch-maple-dtb.py`, verified byte-identical to a manual dtc
      round-trip that changes only that one property), rebuilt boot.img
      (readback-verified), reflashed. Result: the UI now stays on the panel
      (owner-confirmed). Also learned: `mDisplayReady=false` and the
      `idle_power_collapse` sysfs string are unreliable indicators; the offset
      toggling in `/sys/class/graphics/fb0/pan` under activity is the real signal.
      Temporary diagnostics set during this test were reverted (lockscreen kept,
      `screen_brightness` back to 102, `stayon false`).
- [x] TEST 32 (post-GL restart boot-hang + freeze forensics + poplar fix set,
      2026-10-16): after TEST 30/31 the owner restarted; the device hung on the
      Sony bootloader splash with **zero USB** (no 18d1/0fce/05c6, usbmon
      silent). Facts from TWRP: the failed boot wrote NOTHING (`dmesg-boot.txt`
      / `watch.txt` mtimes unchanged — bootlog never ran), `/sys/fs/pstore`
      empty, `/proc/last_kmsg` absent, boot partition readback md5 == server
      `boot.img` (`3f0f8a7d…`) → the flash is intact; hang was pre-userspace
      state, not content. Same class as TEST 25 (no-USB dead boot after a
      reboot; needed a physical power-on). **Recovery: from TWRP, plain
      `adb reboot` → KitKat up in 18 s** (adb enumerates; glrenderd starting
      4.98 s / listening 5.19 s) — try this before reflashing anything.
      - Triage rule, "stuck on splash" splits by USB: adb alive + stale panel =
        display-handoff wedge (TEST 28 class, system up); **no USB at all =
        pre-userspace hang**; recover via a TWRP round-trip; don't expect
        pstore (a forced power-off loses ramoops before TWRP reads it).
      - Freeze forensics (why the restart happened): the last session ran
        ~9.6 h; at ~13:43/13:59 device-time systemui's main thread blocked
        FOREVER in `recvfrom` inside `SocketStream::readFully` — once via
        `eglInitialize`/`rcQueryEGLString_enc`, once via `Surface::lock` →
        `gralloc.maple.so` → `rcColorBufferCacheFlush_enc`. glrenderd stopped
        answering and the unbounded socket wait turned a renderer hiccup into a
        permanently frozen UI (Watchdog kernel dumps at U+33528/33625/33721/
        33818 s; SYSTEM_RESTART dropbox 13:44; ANRs 13:43/13:59).
      - Poplar (XZ1) fix set received — same MSM8998/Adreno 540/emugl-
        glrenderd architecture. Zip: `reference/poplar-kitkat-gpu-fixes-
        2026-10-16.zip` (sha256 `9effee3a…`; inside `GPU-FIX-AND-QUIRKS.md`
        = the full writeup, `source/` patched sources, `patches/` edit
        scripts, poplar-only reference binaries). Maple is missing ALL of its
        reliability set:
        - (A1) fork safety — the TLS GL connection is inherited across zygote
          fork and several processes then share one socket (framing loss).
          Fix = pid-owned file-static re-own in `HostConnection::get()`;
          NEVER a new member (inline `alloc()`/`flush()` offsets are burned
          into prebuilt enc libs).
        - (A2) raise `/proc/sys/net/core/{r,w}mem_max` to 16 MiB at glrenderd
          start — the 208 KB default silently clamps the big buffers and
          stalls full-frame pushes.
        - (A3) **bounded reply + reconnect + SIGPIPE** — `SO_RCVTIMEO`
          (default 20 s, tunable via `persist.glr.replytimeout`), timeout/
          EOF/EPIPE ⇒ mark dead, reconnect IN PLACE (never free: use-after-
          free — their `fix_reconnect.py` is the corrected design;
          `fix_wedge_FIRST-DESIGN-DO-NOT-USE.py` is the trap), `MSG_NOSIGNAL`
          on send, dead `writeFully` returns 0 (a failing one NULLs buffers
          the prebuilt encoders memcpy into). Fixes the freeze above.
        - (A4) bound the `glGetError()` clearing loops in `GLConsumer.cpp`
          (libgui) — each iteration is a socket round trip on the compositor
          thread.
        - (A5) renderer side: `ReadBuffer` 32 MiB cap + drop client,
          `RenderThread` one-message-at-a-time decode + framing diagnostics
          (`FRAMING LOST`/`RESYNC`/`NOPAYLOAD`, `[glr-rt] client pid=`),
          `shutdownStream()` on drop. Needs `peerPid()`/`shutdownStream()`
          added to the host-side IOStream/SocketStream (the bundle does not
          ship that edit).
        - gralloc: skip the `rcColorBufferCacheFlush` round trip on lock —
          host stub, but it blocks the app UI thread; it is IN our ANR stack.
        - C2: `InputDispatcher` freeze-thaw (10 s) for dead window-transition
          freezes (libinputservice); B8: `lights` HAL — maple has NO lights
          module (`/system/lib/hw` has no `lights.*`; brightness/sleep need
          it); B9: KeyguardScrim no-op/focus flags (maple has the canvas
          fallback but not the scrim patch).
      - Do NOT cargo-cult across devices (each conclusion verified on its own
        hardware): poplar ships `FBIOPAN_DISPLAY`, but maple TEST 31 proved
        FBIOPAN is a no-move no-op here ("unable to allocate base pipe") and
        `FBIOPUT_VSCREENINFO` + yoffset is the working flip — KEEP MAPLE'S.
        Poplar expects fb0 pre-doubled; maple's glrenderd must double
        `yres_virtual` itself (flips=0 race, TEST 30) — KEEP MAPLE'S.
        A6 direct readback (readback straight into the panel page): do not
        port; poplar tried and reverted it (boot hang / garbled frames).
        Renderer rebuild rule: `glrenderd`'s build regenerates codec tables;
        a rebuilt renderer deployed alone comes up dim/garbled — rebuild the
        guest `*_enc.so` from the same tree or keep the known-good binary.
      - Backups before the port: on-device `/data/local/tmp/glr-fix-backup-
        20261016/`, server `~/glr-backup-20261016/` — glrenderd `c88c3883…`,
        gralloc.maple `5088dd12…`, libOpenglSystemCommon `e8953e41…` (all
        md5-confirmed server↔device), libgui `6a18ca67…` / libinputservice
        `3a0b9ed8…` (server copies kept; md5 device readback still TODO).
      - NEXT: port A1–A5 + gralloc + C2 (+ lights HAL) in one pass, push,
        single reboot, then a long GL soak watching for `recvfrom`-class
        stalls (none should exceed the reply timeout now) and `FRAMING LOST`
        staying at 0. → DONE in TEST 33.
- [x] TEST 33 (poplar reliability port + lights HAL + scrim, 2026-10-16):
      the whole A1–A5 + gralloc + C2 set, the lights HAL and the scrim fix
      are ported, built, deployed, and verified on the live device (one
      batch push, single reboot; backups kept server + on-device):
      - (A1) `HostConnection.cpp`: pid-owned connection re-own in `get()`
        (file-static, NOT a class member — inline `alloc()`/`flush()`
        offsets are burned into the prebuilt enc libs).
      - (A3) `SocketStream.{h,cpp}` (guest + sdk copies): `SO_RCVTIMEO` +
        `SO_SNDTIMEO` 20 s (property `persist.glr.replytimeout`),
        `MSG_NOSIGNAL` on every send, and timeout/EOF/EPIPE ⇒ mark dead +
        **close the fd**. MAPLE ADDS over poplar: poplar's shipped bundle
        never closes the socket, so `valid()` stays true and
        `reconnectIfDead()` can never fire — the close is REQUIRED for the
        reconnect design to work; also a write on a dead stream drops the
        command and reports success (a failure would NULL
        `IOStream::alloc()` and the encoders memcpy into it).
      - (A3) `RenderdStream.{h,cpp}`: reconnect in place, rate-limited to
        1/s; `kWaitTries` stays **300** (30 s — maple cold boots need the
        whole window, poplar uses 50). MAPLE ADDS: the reconnect must replay
        the per-connection handshake word (`clientFlags=0`, 4 bytes) that
        `RenderServer::Main` reads before it starts decoding — the first
        connection gets it from `HostConnection::get()`, a reconnect has to
        send it; without it the renderer eats the first GL command as
        clientFlags, or trips `IOSTREAM_CLIENT_EXIT_SERVER` and exits.
      - (A4) `GLConsumer.cpp` (libgui): the two `glGetError` drain loops are
        bounded at 8 — each iteration is a synchronous round-trip.
      - (A5) host `ReadBuffer.cpp`: cap at 32 MiB, drop the client when full
        with no progress (the old code doubled until ~2 GB and killed the
        render thread). Host `RenderThread.cpp`: hand each decoder at most
        one declared message, resync on framing loss, dump a 12 MiB rolling
        stream history to `/dev/glr-stream-<pid>.bin`, log `[glr-rt] client
        pid=` and `dropped (getData=)`. Needed `peerPid()`/
        `shutdownStream()` on `IOStream` — host/sdk copy only, appended
        after every pre-existing virtual so guest vtable slots can't move;
        `SocketStream` implements them via `SO_PEERCRED` + `SHUT_RDWR`.
        The poplar bundle did not ship these two methods.
      - gralloc (`gralloc.maple`): `lock()` skips the
        `rcColorBufferCacheFlush` round-trip (a no-op stub host-side, but a
        synchronous round-trip that queued behind heavy GL work and blocked
        systemui's canvas draws — one of the two recorded `recvfrom` ANRs).
      - (A2) `glrenderd`: raise `net.core.{r,w}mem_max` to 16 MiB at start,
        and `SIGPIPE` ignored so a guest death mid-reply can't kill the
        daemon.
      - (C2) `InputDispatcher`: lift a dispatch freeze stuck >10 s (WMS
        freezes around window transitions; if the transition never
        completes the dispatcher slept forever and the UI accepted no
        touch); evidence at `/data/system/dispatch-thaw.log`.
      - (B8) new `vendor/sony/maple/lights/` (module `lights.maple`):
        brightness written to BOTH `/sys/class/leds/lcd-backlight` and
        `/sys/class/leds/wled` — the two nodes are independent (probe:
        writing one does not change the other), TWRP drives the panel
        through wled, AOSP convention is lcd-backlight. Plus
        `TARGET_PROVIDES_LIBLIGHT := true` and `lights.maple` in
        PRODUCT_PACKAGES.
      - (B9) `KeyguardServiceDelegate.showScrim()` is a no-op and the scrim
        window is `FLAG_NOT_TOUCHABLE|FLAG_NOT_FOCUSABLE`, so a dead
        keyguard can't eat every input (the zombie-scrim wedge).
      - Patches: `device-generic-goldfish-glrenderd.patch` and
        `sdk-glrenderd.patch` regenerated from the tree; new
        `frameworks-native-glconsumer-glgeterror-bound.patch`,
        `frameworks-base-input-dispatch-freeze-thaw.patch`,
        `frameworks-base-keyguard-scrim-no-input.patch`.
      Verified on the device (single reboot):
      - glrenderd: `rmem_max`/`wmem_max = 16777216`, `doubled fb0 virtual`,
        `listening on @glrenderd`, `[glr-rt] client pid=` lines, a
        `dropped (getData=-1)` followed by a fresh `client pid=` **6 ms
        later** (the in-place reconnect path demonstrably fires), zero
        `FRAMING LOST`/`RESYNC`, zero glrenderd restarts in the session.
      - surfaceflinger: `GLES: Qualcomm, Adreno (TM) 540, OpenGL ES 3.2` —
        hardware GL through glrenderd, no software fallback.
      - navbar stress (18 home/app-switch keyevents): systemui pid
        unchanged, no new drops, no watchdog/tombstone activity.
      - lights HAL: `settings put system screen_brightness 200` → both
        nodes read 3211 on wake and 0 on sleep — real backlight control
        (before the HAL the framework could never touch the panel).
      - Known quirk (pre-existing, newly visible): the Keyguard window sets
        `userActivityTimeout=10000` (AOSP `AWAKE_INTERVAL_DEFAULT_MS`), so
        WMS caps the screen-off timeout at 10 s whenever that window
        exists — the panel blanks after ~10 s idle. It is NOT a hang; the
        power key wakes it. Revisit only if it bothers the owner.
      - Quirk: the on-device KitKat toolbox has no `head`/`tail`/`sed`/
        `pidof` — adb one-liners may use `grep`/`cat`/`ls`/`ps` only.
      - Renderer rebuild rule respected: glrenderd was rebuilt from the
        same tree that produced the deployed `*_enc.so` (codec tables
        regenerate from unchanged `.in` files) — panel came up correct.
- [x] TEST 34 (animation performance audit + socket-buffer fix, 2026-10-16):
      owner asked "is hw accel 100%? some animations are a little laggy".
      Verdict: yes, 100% hardware everywhere; the lag is per-frame LATENCY in
      the emulation chain — no software fallback, no saturated resource.
      Facts + what was tightened:
      - Hardware check: surfaceflinger GL = "Qualcomm, Adreno (TM) 540,
        OpenGL ES 3.2" through glrenderd (no libagl anywhere); the GPU sits
        at its min clock (257 MHz, 0–19% busy) even while animating; all 8
        CPUs pinned at max (governor `performance`, 1.9/2.45 GHz); nothing
        runaway (`top` shows all-processes 0% on an idle UI).
      - Frame-path instrumentation is now permanent in glrenderd: every 60th
        present logs `posts= drops= postgap stage` next to readback/copy/flip.
        postgap = guest frame interval, drops = frames the panel never saw,
        stage = the render-thread memcpy into the presenter slot.
      - Per presented frame (measured): readback (glReadPixels) 8.7–10.0 ms,
        stage 1.3–2.7 ms, copy (presenter thread, parallel) 2.5–10.4 ms,
        flip 0.7–9.6 ms. drops = 0 in every window ever measured — the
        present path is never the limiter.
      - The limiter is guest frame cadence (postgap): pure window animations
        land ~20 ms/frame (50 fps bursts), app-redraw frames (lock-screen
        ring drag, settings fling, launcher drawer) 40–60 ms/frame
        (17–25 fps), boot animation ~30 fps (its zip's own target). CPU and
        GPU are idle while this happens ⇒ the cadence is set by serialized
        sync round-trips across the emulation (swap fences, BufferQueue
        handoffs, socket round trips), not compute. Further smoothness work
        has to target those sync points (see the option list below).
      - Fixed the poplar-documented-but-never-shipped socket clamp:
        `net.core.{r,w}mem_default` is **212 992 (208 KB)** while one frame's
        GL command stream is easily several hundred KB — the guest blocks in
        `send()` mid-frame and serializes with the renderer. Raising
        `{r,w}mem_max` (TEST 33) wasn't enough: now both ends actually
        `setsockopt(8 MiB)` per connection — host side in
        `UnixStream::accept()` (tools), guest side at connect/reconnect in
        `RenderdStream.cpp` (`glrSetSocketBuffers`, declared in both
        SocketStream.h copies). No regression on the boot windows (first
        window 7.2 fps/50.6 ms before and after); workload noise hides a
        clean A/B (see next note).
      - Test-method lessons: `input swipe` bursts are a noisy benchmark (a
        report needs 60 presented frames; a static UI never completes one —
        several "no data" runs here were just the screen being off/asleep).
        Use sustained animations and read multiple windows, or instrument
        the guest swap rate directly. Also: `screencap` returns a constant
        white frame on this port (broken capture path) — the truth is
        `/dev/graphics/fb0` (dump both 1080×1920 pages, stride 4352 B,
        decode as 1088×1920 RGBA then crop 1080; that is how launcher/lock
        screen states were confirmed). The lock screen's unlock ring at
        (540,1477) is a handy repeatable animation source; a zombie Keyguard
        window can hold FOCUS while the launcher is visible (touches still
        route by position, so on-screen navbar works but `input keyevent 3`
        does not).
      - Options if more smoothness is wanted (roughly by payoff, none done
        yet): (a) async/PBO double-buffered readback to hide most of the
        9–10 ms behind the next frame (adds ~1 frame latency); (b) readback
        straight into the presenter slot (poplar's A6) to drop the 2 ms
        stage + one 8.3 MB memory pass; (c) instrument the guest swap/fence
        path (BufferQueue depth, KHR_fence_sync waits) which looks like the
        real app-frame gate; (d) present at half resolution for the smoothest
        UI (readback+copy halve; softness trade-off, owner's call).
- [x] TEST 35 (poplar A6 + async readback test + swap instrumentation,
      2026-10-16): owner: "still feels a little bit laggy, launcher3 and
      scrolling specifically; implement all remaining poplar GL fixes and the
      proposed ones except half resolution". Done:
      - **A6 ported** (the last missing poplar GL item): the post readback now
        lands straight in a presenter slot instead of the FrameBuffer's
        buffer — `FrameBuffer::setPostReadback()` + `present_acquire()` in
        glrenderd + `ColorBuffer::readbackTo()` (the doc's "do NOT read back
        into the panel page" warning is honoured: slots only, never fb0).
        Live on device: per-frame `stage 0.0 ms` (was 1.3–2.7 ms); like-for-
        like boot windows went 9.8 ms readback+stage → 8.9 ms, and one full
        8.3 MB memory pass per frame is gone. Harness: `drops=0` in every
        reported window.
      - **Async PBO readback implemented and REJECTED BY MEASUREMENT**:
        behind `persist.glr.asyncreadback` (runtime, no reboot). Runtime
        result on the same drawer-swipe stimulus: **13.1 fps / postgap 76 ms
        vs 31–35 fps / 29–32 ms synchronous**; SF's swap-blocked time jumped
        from 11 ms to 58.6 ms — the PBO map+copy costs more than the
        synchronous readback it was hiding (the readback *issue* did drop to
        0.1 ms, but the map pays it back with interest). Ships off; the
        plumbing stays for future experiments.
      - **Swap-path instrumentation** (host, per connection): a window-surface
        swap is `rcFlushWindowColorBuffer` → queue/dequeue → `rcSetWindowColorBuffer`;
        the gap is logged per 60 swaps. Measured under sustained stimuli:
        surfaceflinger blocks **10.7–11.9 ms** in its own swap each frame (the
        framebuffer buffer's release waits on the previous post, i.e. the host
        readback lives inside that number) and its compose/wait gap is
        **17.0–26.4 ms** while animating. Sustained rates on this build:
        **drawer page swipes 31–35 fps** (postgap 29–32 ms), **settings list
        scroll 26–28 fps** (postgap 35–39 ms); stages readback 9.1–10.5 ms,
        presenter copy 5.0–11.7 ms, flip 1.1–3.9 ms. App clients (launcher,
        settings) never hit the swap log — their swaps must use a different
        op set (likely the v2 window-surface path), so app-side numbers still
        need instrumentation there if wanted.
      - **Measurement traps worth remembering** (each cost real time today):
        (1) fb0 pixels are RGBA — an all-ZERO frame is *transparent black*,
        and an RGBA PNG of it renders as white; decode with
        `format=rgb24` before believing anything; (2) the screen sleeps ~10 s
        after the last input (the keyguard window's `userActivityTimeout`)
        and **touch does not wake it** — every test must check `mScreenOn`
        and wake with power/menu first, or the "test" runs on a sleeping
        screen; (3) Launcher2's drawer remembers tabs and its WIDGETS tab
        renders blank here — check `dumpsys activity top` (view flags) before
        blaming the pipeline; (4) `screencap` on this port returns a constant
        image — use the fb0 dump; (5) `input keyevent 26` is a TOGGLE: check
        the state, don't assume.
      - Verdict for the owner: poplar's GL set is now **fully ported
        (A1–A6)**. UI animations sustained at ~26–35 fps; remaining cost is
        the emulation's per-frame sync path (readback + buffer release ≈ 11 ms
        + compose/wait ≈ 18–26 ms). The async experiment showed the readback
        can't be trivially hidden; next candidates (not done) are the
        app-side v2-surface op instrumentation and the guest fence/queue path.
