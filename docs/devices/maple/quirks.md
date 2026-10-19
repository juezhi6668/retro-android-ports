# maple quirks (Sony Xperia XZ Premium G8141/G8142)

Everything needed to port to maple: hardware snapshot, flash layout,
kernel, blocker list, and the full TEST 1–49 bringup log with verified
fixes. Status, releases and links live on the
[device main page](README.md); the generic KitKat playbook is the
[KitKat porting notes](../../advanced/porting-notes/kitkat.md), with the
[Donut playbook](../../advanced/porting-notes/donut.md) +
[cooper quirks](../cooper/quirks.md) as the method reference.

Current state: **BOOTED 2026-10-15** — full KitKat userspace on the 4.4 kernel:
`sys.boot_completed=1`, 75 binder services, Launcher compositing at
1080x1920, working touch, USB adb (`18d1:4ee7`). **Hardware-accelerated
GL/GLES verified 2026-10-15** (Adreno 540 via glrenderd, GLES 3.2); on
**2026-10-18 the graphics stack was replaced by
[libhyb](../../advanced/software/libhyb.md)** (driver in-process,
`fbpresentd` atomic commit — built 2026-10-18, **flashed and verified on
hardware 2026-10-19**, TEST 49). The
tree in [`devices/maple/kitkat/`](../../../devices/maple/kitkat/) is a
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
  `docs/advanced/porting-notes/kitkat.md`).

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
  `docs/advanced/porting-notes/kitkat.md` (KitKat Edify has `run_program` + ext4
  `format`, unlike Donut — easier than cooper here).
- Keep blobs out of git (`.gitignore` covers `aosp-kitkat/`, `out/`, `work/`).

## Bringup log and next actions
Feature matrix and release status: [device main page](README.md).

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
- [ ] TEST 36 (wifi + audio bringup, 2026-10-16, IN PROGRESS): owner asked
      for wifi and audio using `reference/CHARQUIRKS.md` (the pdx203 4.2
      port). Live diagnosis first, tree changes second; nothing below is
      on-device verified yet except where marked LIVE.
      - LIVE facts (adb, `maplekk device`, `sys.boot_completed=1`):
        audio: `/proc/asound/cards` = "--- no soundcards ---", `/dev/snd/`
        holds only `timer`, `dmesg` shows "No soundcards found" with ZERO
        tavil/tasha/wcd/slim/qdsp probe lines (only TDM DAIs probe) and an
        empty `/sys/kernel/debug/asoc/{codecs,dais,platforms}` — the machine
        driver is silently deferring. DT has BOTH `sound-tavil`
        (`qcom,model=msm8998-tavil-snd-card`, the real card) and
        `sound-9335`, each with boolean `qcom,wcn-btfm` (0-byte prop) and
        `qcom,wsa-devs` (16 B) + `qcom,wsa-max-devs`; codecs sit under
        `slim@171c0000` (tavil_codec/tasha_codec), wcn3990 under
        `slim@17240000`. Running kernel (`/proc/config.gz`,
        `4.4.302-whatawurst+` Nov 2024) has everything built in
        (`CONFIG_SND_SOC_MSM8998=y`, WCD9335+WCD934X, WSA881X, QDSP6V2,
        APRV2_GLINK, PDR, `CONFIG_MSM_SERVICE_LOCATOR=y`,
        `CONFIG_QCA_CLD_WLAN=y` built-in — the lineage-17.1 defconfig's
        `=m` is stale). `service_locator.enable=1` on cmdline;
        `audio_notifer_reg_service: SSR_MODEM is in use`; all 7
        msm_subsys (ipa_fws venus spss a540_zap adsp slpi modem) at
        SYSTEM; `/system/vendor/firmware` holds only glr-staged a5xx GPU
        fw; no ADSP PIL load ever attempted (only a540_zap at 5.14 s).
        wifi: qca_cld3 built in, driver v5.1.1.77V loaded,
        `/dev/wlan` chr 229,0, `/sys/module/wlan/parameters/fwpath`
        present-but-empty — but NO wlan0, NO `/sys/class/ieee80211` phy,
        empty `/proc/net/wireless`. `wlan.driver.status=unloaded`,
        supplicant never ran. Root causes in userspace (both fixed in
        tree): (a) `/data/misc/wifi/sockets` + `/data/misc/dhcp` never
        created (only softap.conf existed); (b) NO `wpa_supplicant` /
        `p2p_supplicant` / `dhcpcd_*` services defined anywhere, so
        wifi.c's `ctl.start wpa_supplicant` was a silent no-op.
        `lib_driver_cmd_qcwcn` is STATIC (linked into wpa_supplicant —
        its absence from `/system/lib` is correct, not a bug).
        `echo ON > /dev/wlan` creates no interface; manual
        wpa_supplicant exits silently with no wiphy (needs the WLFW/QMI
        firmware handshake first — modem-side `wlanmdsp.mbn` exists on
        the modem partition but the modem never boots here).
        Modem partition (vfat `image/`, mounted ro live) carries
        `adsp.*`, `bdwlan.*` (+`.bin`), `cpe/wcd`, `slpi_v*`, `venus.*`,
        `wlanmdsp.mbn`; bluetooth partition carries cherokee BT
        (`crbtfw*.tlv`/`crnv*.bin`); `/persist/wlan/wlan_mac.bin`
        (32 B) exists. `dmesg` also shows
        `ipa_smmu_wlan_cb_probe: could not alloc iommu domain` (hence
        `gIPAConfig=0` in the new ini).
      - Tree changes (this change, build + flash + verify pending):
        wifi `init.maple.rc` gains the sockets/dhcp dirs, both
        supplicant services + `dhcpcd_*` (hammerhead pattern, class main
        since late_start never fires without mount_all), and RELATED
        writes for adsp/slpi (subsys4/5 — order verified live);
        `service_locator.enable=0` in BoardConfig (CHARQUIRKS audio:
        with =1 the card took ~5 min via pdr, cmdline sets 0);
        BoardConfig wifi comment corrected (WCN3990/cherokee qca_cld3,
        NOT BCM43xx — proven by `/proc/config.gz` + BT tlv names) with
        the CHARQUIRKS `BOARD_WLAN_DEVICE` divergence recorded (qcwcn
        stays: KitKat's driver_nl80211 needs the private lib to LINK);
        new `vendor/sony/maple/wifi/` (`WCNSS_qcom_cfg.ini` minimal
        with `gIPAConfig=0`, both `*_overlay.conf` stubs) and
        `vendor/sony/maple/audio/` (`audio_policy.conf`,
        `mixer_paths_tasha.xml` from sony-msm8998 ten,
        `audio.primary.maple` tinyalsa HAL: speaker = `speaker` +
        deep-buffer MM1/pcm0, mic = `handset-mic`/dmic1 + MM1/pcm0,
        DEC7 104 (+20 dB, no acdb), silent-safe while the card is
        absent, `libsrec_jni` added for sound recorder);
        `scripts/patch-maple-dtb.py` now also strips boolean
        `qcom,wcn-btfm` (CHARQUIRKS `qcom,wcn-bt = <0>` translated —
        verified on the real blob: all 4 DTBs -38 B, mdss fix intact,
        tavil model intact, idempotent). Firmware blobs (adsp.*,
        bdwlan.*, wlan_mac.bin) intentionally NOT in git (same
        out-of-git policy as the kernel blob) — extraction + staging
        is the next step after this image boots.
      - NEXT (verify-me, in order): (1) card probes after reflash
        (`/proc/asound/cards` shows tavil; if still absent, WSA881x
        deferral is the next suspect — DT carries wsa-devs/max-devs and
        NO wsa/swr probe line exists); (2) speaker + mic through the
        new HAL; (3) `setprop ctl.start wpa_supplicant` path once the
        WLFW firmware question is answered (may be modem-gated — the
        modem never boots on this port, same wall as cellular/GPS).
      - SETBACK 2026-10-16 (fixed same session): first boot with the new
        HAL + policy crash-looped mediaserver (SIGSEGV fault addr 0 at
        `AudioFlinger.cpp:1349`, `loadHwModule` → pc 0 in 10 tombstones;
        `init.svc.media=restarting`, no boot_completed). Root cause: the
        HAL never set `init_check` (or any stream getters) — every
        framework-reachable op must be assigned, see the new playbook
        lesson. Fix: full ops tables in `audio_hw.c` (device + both
        stream commons + out/in specifics, `-ENOSYS`/neutral defaults),
        mirroring the generic stub. HAL-only rebuild + live push.
      - VERIFIED 2026-10-16 (after the fix, single reboot): fixed HAL
        live-pushed, `sys.boot_completed=1`, `init.svc.media=running`,
        mediaserver pid stable 5+ min, no new tombstones. `boot.img`
        verified live: `service_locator.enable=0` in `/proc/cmdline`,
        `wcn-btfm` gone in all 4 DTBs, init-created
        `/data/misc/wifi/{sockets,wpa_supplicant.conf,entropy.bin}`.
        `setprop ctl.start wpa_supplicant` now resolves (`stopped`
        after the oneshot exits — previously blank/unknown); it exits
        because wlan0 still doesn't exist (WLFW handshake, next step).
        Card still `--- no soundcards ---` (kernel workstream), wlan0
        still absent (WLFW/modem question).
      - FOLLOW-UP 2026-10-16 (same session, owner report: no dialer or
        volume-key sound, no Wi-Fi in Settings): both explained live.
        Audio: AudioFlinger runs OUR primary output (dumpsys: 48000 Hz
        output thread, HAL frame count 1024; policy dump shows our
        rates) — the chain is complete to the HAL's silent-safe discard,
        so silence is exactly the missing kernel card, nothing more.
        Wi-Fi: `/system/etc/permissions` shipped NO feature xmls at all,
        so PackageManager hid Wi-Fi from Settings; added
        `android.hardware.wifi.xml` (frameworks source, one
        PRODUCT_COPY_FILES line — no p2p xml per the CHARQUIRKS
        posture). After push + reboot the feature is granted and the
        whole toggle path fires: `svc wifi enable` → WifiStateMachine
        DeviceActiveState → wifi.c → `wpa_supplicant` oneshot runs and
        exits (no wlan0). Settings shows Wi-Fi now; scans/connects wait
        on the kernel interface.
      - TEST 37 (SKIP_QMI experiment, 2026-10-16): `icnss.quirks=388`
        (SKIP_QMI + defaults) baked into a test boot.img to force the
        HDD probe without the QMI WLFW handshake. Result: bootloop at
        the Sony logo (pre-userspace, kernel panic class) — recovered
        via TWRP by reflashing the previous boot.img (`boot-test36.img`
        in `/data/media`, md5-verified `c31321cb…` on-device, warm
        reboot back to `boot_completed=1`). Lesson: the bypass ENGAGES
        (probe runs at boot) but crashes without QMI-provided setup;
        capturing the panic needs pstore across the loop (not done).
        Cmdline flag reverted in-tree. Remaining wifi paths: cnss-daemon
        in a 64-bit Lineage chroot (real QMI handshake), or modem boot.
      - TEST 37b (SKIP_QMI + BDF retest, 2026-10-16): the first SKIP_QMI
        run may have crashed only on missing board data, so the FULL
        test37 pair went in — `system-test37.raw.img` (5 GB,
        md5 `4b8539ac…`, verified on-partition after dd; contains the
        FW_DIR wiring: adsp.* + cpe_9335.* + bdwlan.* + wlan_mac.bin +
        tested HAL) plus the same quirks boot.img. Result: SAME
        bootloop (no adb after 110 s, twice). SKIP_QMI is dead without
        QMI setup regardless of BDF — reverted, not retried. Restore
        PENDING (owner away until tomorrow): TWRP → dd
        `/data/media/boot-test36.img` (`c31321cb…`) to
        `/dev/block/bootdevice/by-name/boot`, reboot. The test37 system
        partition stays (good: FW wiring + HAL persist there).
        Lesson learned the loud way: overwriting the RUNNING system
        partition breaks the live libc (`CANNOT LINK EXECUTABLE ...
        empty/missing DT_HASH`) — push, verify, then reboot
        IMMEDIATELY without running anything else.
      - Audio DSP stall (TEST 36 follow-up): with the tasha card up and
        all 24 speaker controls verified SET via a static `tone_test`
        build (`tools/tone_test.c`, NDK r21e armv7a, static tinyalsa),
        playback sticks at PREPARED with `msm_pcm_playback_copy:
        wait_event_timeout` — ADSP ONLINE but returns no buffers.
        Prime suspect: the audio protection domain (no pd-mapper/
        adsprpcd on this port; Lineage ships both plus rmt_storage and
        tftp_server). No new tombstones from the probe (the one new
        tombstone is a toolbox `ls|grep` SIGPIPE, known noise).
        **Resolved 2026-10-19 (TEST 50): not a protection domain.**
        The ADSP was up all along; it was missing its ACDB calibration
        (the kernel cal blocks come from a userspace loader) and the HAL
        wrote a quarter of each buffer (KitKat tinyalsa `pcm_write` takes
        bytes). No `adsprpcd` was needed — see the audio bringup log at
        the end of this file.
      - QRTR verdict (2026-10-16, definitive): `/proc/kallsyms` holds
        ZERO `qrtr` symbols (positive controls pass: binder 409,
        msm_ipc_router 58, qmi_handle 12), `/proc/net` has no qrtr, and
        the config has only legacy `CONFIG_IPC_ROUTER` + kernel-internal
        QMI. Consequence: NO modern QMI userspace (pd-mapper,
        cnss-daemon, rmt_storage, tftp_server — all 64-bit QRTR clients
        in Lineage vendor) can run on this kernel, chroot or not. The
        64-bit chroot is additionally blocked: no `linker64` anywhere
        (APEX payloads ship separately; `sysmnt/apex` is empty).
        What DOES work over legacy transports: kernel QMI
        (sysmon-qmi reached ADSP SSCTL live), SMD FastRPC
        (`/dev/adsprpc-smd` present — adsprpcd's channel), and PIL.
        So the audio-PD path, if any, must go through adsprpcd-over-SMD
        or in-HAL ACDB loading (`/dev/msm_audio_cal` exists;
        `maple-fw/acdbdata` 7.9 MB staged on the server), never through
        QRTR daemons. Same verdict kills the cnss-daemon chroot idea —
        wifi's remaining paths are modem boot or a kernel QRTR
        backport (both own workstreams).
      - QRTR follow-up (2026-10-18, static re-check from the dumps):
        the kernel half of the verdict above stands (no CONFIG_QRTR),
        but "no modern QMI userspace ... chroot or not" was too
        strong, and "no linker64 anywhere" looked at the wrong copy.
        Lineage 22.1's own vendor `libqmi_cci.so` is dual-transport —
        `strings` shows `qcci_ipc_router_ops` AND `qcci_qrtr_ops`
        with a runtime "QRTR NOT SUPPORTED/SUPPORTED" probe — and
        `cnss-daemon` links it, so libqmi_cci clients can fall back to
        the legacy IPC-router transport `pdserve` already uses (TEST
        40). And the Lineage system image carries a real
        `system/bin/bootstrap/linker64` (2,188,552 B aarch64 ELF; the
        earlier probe saw the live phone mount, where apexes are
        separate). The six daemons (cnss-daemon, pd-mapper,
        rmt_storage, tftp_server, irsc_util, sensors.qti) are all
        aarch64 ELF with `interpreter /system/bin/linker64`; transport
        split: cnss-daemon, pd-mapper, sensors.qti (`libqmi_cci`) and
        rmt_storage (`libqmi_csi`, also dual) are candidates, while
        tftp_server links `libqrtr.so` directly and stays out. So the
        cnss-daemon chroot idea is back on the table (WLFW is the
        missing half of wifi); only the device can show whether the
        libqmi_cci legacy path completes the handshake. `pdserve` still
        supersedes `pd-mapper` regardless; per-subsystem assessment in
        the Taimen mapping page (§"The daemon chroot and the HAL
        bridges").

## Bringup log: service locator + pdserve (2026-10-17)

- **TEST 39 (service_locator.enable=1, modem held, adsp after modem):**
  boot_completed ~25 s; modem ONLINE at ~6 s; adsp-boot saw modem ONLINE
  after 2 s and wrote `boot_adsp`; `/proc/asound/cards` shows
  `msm8998-tasha-snd-card` — **no audio regression with locator=1**.
  The locator's 5 stuck `pd_locator_work` kworkers (audio_pdr, icnss,
  slimbus, ath10k, +) were the gate: the kernel waited for a SERVREG_LOC
  (QMI 0x40, vers 1, instance 0x0101) server that nothing on this port
  provided (the stock 64-bit `pd-mapper` needs a 64-bit userspace; see
  the 2026-10-18 QRTR follow-up — `pdserve` stays the port's solution).
- **pdserve** (`devices/maple/kitkat/pdserve/pdserve.c`, static musl
  armv7, now shipped as `pdserve` in `PRODUCT_PACKAGES`): a 32-bit
  SERVREG_LOC provider over `AF_MSM_IPC` (SOCK_DGRAM, family 27), binding
  the name `{service=0x40, instance=0x0101}` and answering
  `GET_DOMAIN_LIST` (msg 0x21) from the modem-partition `.jsn` tables
  (modemuw.jsn, modemr.jsn, adspua.jsn, adspr.jsn). Verified live:
  `servloc: ... Service locator initialized`, and the kernel connected to
  instance 74 (adsp) and 180 (modem) servreg-notif services. The modem's
  own TMS acts as a client of this server (requests for `tms/pdr_enabled`,
  `tms/pddump_disabled`, `kernel/elf_loader`, `wlan/fw`, `avs/audio`
  all arrive and are answered).
- **Open blocker — modem stability:** the modem asserts its err-fatal
  interrupt and restarts every ~41 s (`subsys-restart: ... Restart
  sequence requested for modem`, failure reason
  `smem_get_entry_no_rlock failed`). This predates pdserve (seen from
  t=46 s in TEST 39) and is independent of the locator. Consequence: the
  WLAN PD never starts (no `wlfw`/`wlan0` observed) because the modem's
  root PD crashes ~3 s after the servreg-notif registration completes.
  Suspected cause: no rild/keepalive on this port (stock keeps the modem
  alive via `vendor.qcrild`); the modem watchdog fires without an AP-side
  QMI client. A 64-bit chroot running `vendor/bin/hw/qcrild` (or a
  minimal QMI keepalive) is the next workstream. Until then, Wi-Fi
  bring-up code is correct and unblocks the PDR chain but cannot be
  validated end-to-end.
- **Pending fix (next boot image):** `adsp_boot` is not oneshot, so
  `adsp-boot.sh` re-runs every ~5 s; make it `oneshot` (done in
  `init.maple.rc`). Also bake the audio HAL fix
  (`stop_threshold = INT_MAX` in `audio/audio_hw.c`) into the next
  system image.

## Bringup log: GL reliability on fresh images (2026-10-17)

- **TEST 41 (owner: "hardware accelerated GL only works sometimes" — full
  speed all the time; 2026-10-17):** three stacked defects found and fixed,
  verified with cold boots plus a crash/restart stress matrix.
  - **Fresh images dropped the chroot's empty dirs, so SF latched
    software.** Symptom: single-digit fps from boot,
    `init.svc.glrenderd=restarting`; dmesg `hwchroot: bind /dev ->
    /system/glr/root/dev: No such file or directory`; `/dev/block/sde46
    /system ext4 ro`. Root cause: `full_maple.mk` stages the renderer
    chroot with `find . -type f` + `PRODUCT_COPY_FILES`, which cannot
    carry the empty dirs `stage.py` stages (`dev`, `proc`, `sys`,
    `data/...`); hwchroot's runtime mkdir then fails EROFS on the
    read-only partition, every bind gets ENOENT, glrenderd
    restart-loops and surfaceflinger's one-shot hw-vs-software probe
    (30 s wait, latched per process) falls to the software renderer.
    Fix: `hwchroot` assembles the chroot from a writable tmpfs skeleton
    at `/dev/.glr` (generic entry binds, /dev bound first, synthetic
    dirs) instead of mkdir'ing inside `<root>` — log stamp `hwchroot:
    assembled /dev/.glr from /system/glr/root`. Verified on cold boots
    (final hwchroot md5 `24adff33…`): assembled at ~4.9 s, SF reports
    `GLES: Qualcomm, Adreno (TM) 540, OpenGL ES 3.2`, boot animation
    28–37 fps, steady `posts=60 drops=0`.
  - **The image also shipped a stale daemon — guest and server halves from
    different builds.** `/system/glr/root/glrenderd` was the pre-A1–A6
    binary (md5 `c88c3883…`, 162320 B, stats lines without `posts=`)
    while the guest libs in the same image were current (proof:
    `/system/lib/libOpenglSystemCommon.so` carries `glr-guest:
    reconnecting to the renderer`, `persist.glr.replytimeout`) — the
    server staging dir was never refreshed after the reliability-set
    work. The mismatch exposed ~3 daemon crashes in 5 min of use
    (FORTIFY `pthread_mutex_lock called on a destroyed mutex
    (0xf3ef332c)`; `signal 11 at 0x20` inside `glBindTexture+29`;
    `signal 11 at 0x4` in `EsxContext::TexImage` via
    `glTexSubImage2D`) around client drops and panel transitions. Fix:
    rebuilt from current sources (`tools/glrenderd/build.sh`; md5
    `bcb4ab7a…`, 170668 B), deployed, and the staging tree refreshed so
    images ship the same pair. Which-daemon check: the new build logs
    `posts=… drops=… postgap … stage …`. Verified: the identical stress
    (settings launch + scroll, page/drawer swipes, screen off/on cycles,
    daemon stop/start churn) → **0 crashes**, animated windows 12–35 fps
    depending on stimulus.
  - **A restarted daemon strands a live surfaceflinger:** the fresh
    daemon has no server-side state for SF's window objects, so `posts=0`
    under the new instance while SF keeps flipping its stale buffers
    (guest in-place reconnect does not rebuild window objects).
    Recovery is `setprop ctl.restart surfaceflinger` (`service_restart`,
    `system/core/init/init.c:400`: running → stop+start, not running →
    start, restarting → no-op) — verified: SF pid changes, re-probes to
    Adreno, frames resume. This generalizes the "`stop glrenderd` live
    needs a surfaceflinger restart" note in init.maple.rc to every daemon
    death.
  - **Self-heal in hwchroot (no watchdog service needed):** a marker file
    on the /dev tmpfs (`/dev/.glr/ran`) records "the daemon ran this
    boot"; on any 2nd+ hwchroot run since boot with the daemon binary
    present, hwchroot forks the host's `/system/bin/setprop ctl.restart
    surfaceflinger` itself, then execs the daemon. Cold boots are exempt
    (the first run is init's own from post-fs-data). No rate limit ships:
    init's own crash-restart backoff (`time_started + 5`,
    `restart_service_if_needed`, init.c:418) already spaces crash-loop
    cycles ≥5 s, and suppressing heals would strand the instance that
    finally survives a loop. Verified live: `stop/start glrenderd` → heal
    + SF re-probe + frames; `kill -ABRT` on the daemon → init restart →
    heal (SF pid changed the moment the service came back) → frames;
    cold boots → **no** heal line.
    - **Trap caught while building it: never use "is SF running?" as the
      restart signal.** At boot, post-fs-data's `start glrenderd` returns
      as soon as hwchroot is forked and init immediately runs `on boot`'s
      `class_start core` — SF's process appears milliseconds behind
      glrenderd's (the first attempt bounced an SF that was ~10 ms old:
      "daemon restart" at 4.87 s, replacement SF only up at 9.86 s =
      exactly the 5 s backoff; that is how the race was found). The
      run-count marker is the honest signal; SF's GL probe happens
      seconds later, by which time glrenderd listens.
  - Ops notes (each cost a boot cycle): replacing `/system/glr/hwchroot`
    is a plain push (the running process image is `/glrenderd`); replacing
    `/system/glr/root/glrenderd` hits `Text file busy` until
    `stop glrenderd` — stop → cp → chmod 755 → `sync` → remount ro →
    `start`. Verify deployed identity by md5, not timestamps.

## Bringup log: Donut USB adb (2026-10-18)

- **TEST 42 — the FunctionFS descriptor blob must use this kernel's
  ancient magic values (`1` / `2`), not the modern `0x502` / `0x503`.**
  - Symptom: on a Donut boot, adbd retries `open ep0` + `write
    descriptors` once a second forever (`/cache/adbd-ffs.log`: `write
    descriptors len=62 FAILED ret=-1 errno=22`), the kernel never logs
    "read strings", and the gadget never binds — `configfs-gadget
    a800000.dwc3: failed to start g1: -19` every ~6 s
    (`ffs_do_functionfs_bind` returns `-ENODEV` until the strings write
    has set `desc_ready`). `kmsg` shows `functionfs: file system
    registered` from init and nothing more.
  - Diagnosis: the f_fs QCOM IPC log
    (`/sys/kernel/debug/ipc_logging/f_fs/log`, needs debugfs mounted)
    told the whole story in four lines: `ffs_ep0_write: enter:len 62
    state 0` → `__ffs_data_got_descs: enter: len 62` →
    `__ffs_data_got_descs: exit: ret -22` → `ffs_ep0_write: exit:ret
    …594`, with **no `ffs_do_descs` entry in between**. Only two checks
    run before `ffs_do_descs`: the length field and the magic; the
    length matched (62 = 62), so the magic was rejected.
  - Root cause: our blob was built from the modern enum
    (`#define FUNCTIONFS_DESCRIPTORS_MAGIC 0x502`), but whatawurst's
    msm8998 4.4 tree kept the ancient one —
    `include/uapi/linux/usb/functionfs.h`
    (`android_kernel_sony_msm8998`, lineage-21):
    `FUNCTIONFS_DESCRIPTORS_MAGIC = 1`, `FUNCTIONFS_STRINGS_MAGIC = 2`,
    `FUNCTIONFS_DESCRIPTORS_MAGIC_V2 = 3`. `switch
    (get_unaligned_le32(data))` hits `default: goto error` for 0x502 —
    `-EINVAL` before any descriptor logging. TWRP's adbd works on the
    same kernel because it ships those values: its `init_functionfs`
    `.rodata` template is `[magic 1, length 62, fs_count 3, hs_count
    3]` (V1), with `[magic 3, length 0x90, flags 0xf, …]` tried first
    and `[magic 2, length 32, str_count 1, lang_count 1]` for the
    strings.
  - Live proof without a reboot: a *test* ffs instance beside TWRP's
    live one — `mkdir /config/usb_gadget/g1/functions/ffs.test`
    (never touch `ffs.adb`), `mkdir -p /dev/usb-ffs/test`,
    `mount -t functionfs test /dev/usb-ffs/test`, then
    `exec 3<>/dev/usb-ffs/test/ep0` (**must** be `<>`; an `O_TRUNC`
    open fails) and `cat blob >&3`. Magic 1/2 → write returns the blob
    length, the log shows the full `ffs_do_descs` /
    `ffs_do_single_desc` / `__ffs_data_do_entity` parse plus
    `ffs_epfiles_create: … eps_count 2`, and `ep1`/`ep2` appear in the
    mount. Magic 0x502/0x504 → the same one-line `exit: ret -22`.
    **Reading the ipc log consumes it** — read it exactly once,
    immediately after the write, or the evidence is gone.
  - Fix: `patches/donut/system-core-adbd-ffs.patch` now sets
    `FUNCTIONFS_DESCRIPTORS_MAGIC 1` / `FUNCTIONFS_STRINGS_MAGIC 2`
    (with a comment saying why). Verified 2026-10-18: boot.img with the
    fixed adbd flashed (prefix md5 match), `adb reboot` → host
    enumerates `mapledonut device` ~30 s later — a real adb transport,
    where every previous attempt showed only TWRP's `recovery`.
  - Follow-up (same TEST, later in the night — the "phone vanished"
    explanation): the diagnostic build's `usbudc.sh` was an unbind +
    rebind loop (`echo "" > $UDC`; sleep 3; `echo a800000.dwc3 > $UDC`)
    that ran forever and only *logged* `bind FAILED rc=0 err= eps=[]` —
    both the rc and the empty `eps` were readback races: the QCOM
    configfs bind is asynchronous (the write returns, the gadget starts
    ~0.4 s later), so reading the UDC in the same instant always showed
    empty, and the next cycle's unbind then tore down the connection it
    had just made. Host `journalctl -k` proves it: `maple-donut` connect
    → disconnect every 6 s, 42 cycles (11:52–11:57), device numbers
    119–160; the phone's kmsg has the matching
    `configfs-gadget gadget: high-speed config #1: b` every cycle and
    **zero** `failed to start g1: -19`. The 240 s autowinch then rebooted
    to recovery mid-flap (`mdss_fb_release_all: … name=reboot`) and the
    host saw TWRP again 17 s later. Fix: `usbudc.sh` now uses the KitKat
    port's proven semantics — poll the UDC, bind when it reads empty,
    **never unbind** (rebinding when adbd drops the gadget is still
    needed: closing the ffs instance unbinds it). Verified on-device
    (2026-10-18): adb enumerated as `mapledonut device` and worked; the
    flap explanation is host-log + phone-kmsg backed.
  - Follow-up 2 (same TEST) — **the real reason a boot came up with no
    USB: Donut's shell has no `[`.** The next boot used a `usbudc.sh`
    whose bind was gated on `[ -z "$(cat $UDC)" ]`. Donut's
    `/system/bin/sh` has no `[`/`test` — builtin or binary — so every
    `[ ... ]` prints `[: not found`, returns non-zero and silently takes
    the **else** branch. The script's `while [ $i -lt 600 ]` performed
    zero iterations: kmsg has exactly one usbudc line, `usbudc: bind
    FAILED after 0 tries rc= err=` at 2.840 s, the whole boot has zero
    `configfs-gadget` lines, and the 900 s autowinch returned the phone
    to TWRP (12:39, 2026-10-18). An earlier "first bind raced adbd's ep0
    setup" theory for this same boot was wrong — nothing was ever written
    to the UDC. Found by chrooting into the Donut `/system` from TWRP and
    running the shipped script with Donut's own shell; capability map and
    the generic lesson are in
    [donut.md](../../advanced/porting-notes/donut.md). The ep1-gate
    script and the state-aware autowinch that this (wrong) theory
    produced are gone — see Follow-up 3.
  - Follow-up 3 (same TEST) — **Donut-shell-safe scripts work: the gadget
    binds once and stays up; `adb shell` then needed one more fix (and
    got it).** `usbudc.sh` was rewritten with exit statuses only: after a
    12 s wait, every 3 s `cmp $UDC /cache/usbudc-unbound >/dev/null
    2>&1 && echo a800000.dwc3 > $UDC`, plus one `/dev/kmsg` tick per
    attempt; the reference file holds exactly the unbound UDC content.
    That content was verified on-device with Donut's own shell against a
    live *unbound* gadget (`mkdir /config/usb_gadget/testg`, never
    bound, TWRP's `g1` untouched): unbound UDC = 1 byte `"\n"` (cmp
    rc 0 → bind), bound UDC = `a800000.dwc3\n` (rc 1 → skip), and
    `echo "" > /cache/usbudc-unbound` reproduces the unbound content
    byte-for-byte. Flight evidence (boot 2026-10-18 12:45): kmsg
    `usbudc: start` 2.897 s, tick 1 14.899 s, `configfs-gadget gadget:
    high-speed config #1: b` 15.450 s, tick n every 3 s — no unbind, no
    rebind, no `-19`; host dmesg shows one single `new high-speed USB
    device … 18d1:4ee7 … maple-donut` (device 37) that stays attached
    the whole session, `adb devices` → `mapledonut device`.
    - `adb pull` (sync service) worked at once; `adb shell <cmd>`
      accepted the request (client trace: `OKAY` right after
      `shell:<cmd>`) and then hung forever. Root cause was **not** the
      transport: `service_to_fd`'s `const char* args[2]` was a local
      array handed to the service thread (stack-use-after-return), so
      the exec'd child was a bare `/system/bin/sh` with **no `-c`** and
      waited on stdin. Diagnosed without a shell by `adb pull`ing
      `/proc/<pid>/cmdline` (15 bytes = `/system/bin/sh\0` only),
      `/proc/<pid>/wchan` (`pipe_read`) and `/proc/<pid>/syscall`
      (`3 …` = `read(0, …)`), the pid taken from the bootlog snapshot
      while a long `adb shell sleep 60` was open.
    - Fix (patches/donut/system-core.patch): `strdup()` the command in
      `service_to_fd`, build the `-c <cmd>` args inside `shell_service`,
      `free()` when the child exits; the same latent bug in `reboot:`
      (argument points into the freed OPEN packet) fixed the same way.
      Verification — **verified 2026-10-18 13:01** (boot.img md5
      `e6ee2096b22c4956dd152f38dc681d98`, flashed by dd with a 17 MB
      prefix readback match): `adb shell echo hello-donut` →
      `hello-donut`, `adb shell id` → `uid=0(root) gid=0(root)`, stderr
      merges (`echo out; echo err >&2` returns both), three consecutive
      independent shell sessions succeed, `pull`/`push` unchanged. The
      same boot has the clean USB story: `usbudc: start` 2.837 s, tick 1
      14.840 s, `configfs-gadget gadget: high-speed config #1: b`
      15.449 s, ticks every 3 s after, zero `-19`, one host enumeration.
      The autowinch was killed from the host once the shell was up (pid
      found by `cat`ing `/proc/<pid>/cmdline` for every `/system/bin/sh`
      pid) so the session can outlive 15 minutes.

## Bringup log: Donut system_server crash + atomics (2026-10-18)

- **Root cause of the system_server crash loop = barrier-less bionic
  `__atomic_*` on a weakly-ordered multi-core CPU.** Donut's
  `bionic/libc/arch-arm/bionic/atomics_arm.S` reimplements
  `__atomic_cmpxchg/inc/dec/swap` with LDREX/STREX but **omits the full
  memory barriers** the original kuser helpers provided. `cutils/atomic.h`
  documents the contract ("all reads or writes issued before
  android_atomic_* operations are guaranteed to complete before the atomic
  operation takes place" = full barrier). On the 4-core Cortex-A73
  (MSM8998, AArch32 compat) this breaks mutual exclusion in bionic
  pthread mutexes and libutils `futex_mutex` (both call `__atomic_*`),
  and indirectly Dalvik locking. Symptom: system_server + acore crash loop
  (10 tombstones in 3 min, 2026-10-18 16:49–16:52), `abort ←
  mspace_free ← dvmHeapSourceFree ← sweepBitmapCallback ← ... ←
  gcForExternalAlloc` (heap corruption) and `free ←
  SharedBuffer::release ← ~String16 ← Parcel::writeInterfaceToken`
  (fault 0x0d). Controlled experiment: `msptest 3` (4 threads × 1M iters,
  CAS spinlock + protected counter + malloc churn) = SIGSEGV rc=139 on
  the old libc, PASS after the fix. Fix: add `dmb sy`
  (`.word 0xf57ff05f`) before and after every LDREX/STREX in all four
  `__atomic_*` functions; also rewrite libcutils `android_atomic_swap`
  (the one remaining `swp`) as LDREX/STREX+dmb. Verified:
  `msptest 3` and `msptest 4` (atomic conformance) PASS on both the TWRP
  kernel (4.4.263) and the Donut kernel (4.4.302) with the fixed libc
  (md5 `2b6469277b6c4375849ea3c0d96f85d3`) and libcutils (md5
  `af710d64a95e0936f8841de6f37afe44`); baseline (barrier-less) `msptest
  3` = SIGSEGV on both kernels.
- **Kuser helpers exist on this kernel** (`CONFIG_KUSER_HELPERS=y`,
  `CONFIG_COMPAT=y`); the kuser page is mapped at 0xffff0000 with version
  word 0x00000005. The kuser-based `android_atomic_*` (libcutils, full
  barrier) all pass conformance. The earlier "kuser/page missing"
  hypothesis was wrong: the trap was `swp`, not a missing page.
- **SWP emulation is on** (`CONFIG_SWP_EMULATION=y`) — `swp` is emulated
  (slow, not fatal), so the original crash was heap corruption from the
  missing barriers, not an instruction fault. `swp` traps on ARMv8 without
  emulation; the LDREX rewrite avoids the dependency entirely.
- **Secondary stopper: `com.android.phone` OOMs on the lock screen.**
  With the heap still at the Donut default (16 MB growth limit), the
  lock-pattern wallpaper bitmap (28 KB external alloc) overflowed the
  budget: `dalvikvm-heap: 28900-byte external allocation too large for
  this process` → `OutOfMemoryError: bitmap size exceeds VM budget` in
  `LockPatternView.getBitmapFor` → `InflateException` on `UnlockScreen`
  (Binary XML line #60) → WindowManagerPolicy thread dies → main-thread
  NPE at `ViewRoot.setView` (ViewRoot.java:428) → `I/Process: Sending
  signal … SIG: 9` → zygote exits → init restarts zygote → crash loop.
  Fix: set `USE_CUSTOM_RUNTIME_HEAP_MAX := 512m` in `BoardConfig.mk` —
  Donut's dalvik ignores the `dalvik.vm.heap*` props (those are a
  Honeycomb+ invention) and reads the max heap only from `-Xmx`, which
  defaults to 16 m (`AndroidRuntime.cpp` uses `-Xmx16m` unless
  `USE_CUSTOM_RUNTIME_HEAP_MAX` is defined). The `dalvik.vm.heap*` lines
  in `system.prop` are kept for documentation only. Also
  `com.android.phone` crashed separately: `PhoneApp.onCreate` →
  `PhoneUtils.setAudioMode` → `AudioManager.setMode` NPE (audio service
  not yet registered at that instant); made `AudioManager.setMode`
  null-safe. `patches/donut/` (AudioManager null-safety).
- **Bringup instrumentation added (TEMP, remove before release):**
  - `/dev/log` FIFO shim (`devices/maple/donut/logshim/logshim.c`,
    static binary `/sbin/logshim`): creates
    `/dev/log/{main,radio,events}` FIFOs at `on init` (via `exec
    /sbin/logshim --init`) and mirrors each to
    `/cache/logcat-{main,radio,events}.txt` via a non-blocking reader
    service defined before zygote. Donut's liblog opens all three with
    O_WRONLY and disables logging if any fails, so all three are
    required. Records are raw `[priority][tag\0][msg\0]` (no
    logger_entry header); `tools/parse-logcat.py` untangles the main one.
  - `system/core/init/init.c` `zap_stdio()`: stderr now goes to
    `/dev/kmsg` instead of `/dev/null`, so crash output (Java uncaught
    exception traces, linker errors) lands in `/cache/kmsg.txt` (already
    captured + rotated + fsync'd by klogd). TEMP bringup patch.
  - Both are logged as bringup-only in the device tree; remove once the
    port is stable (AGENTS.md patch-set discipline).
- **Validation:** `msptest 4` (conformance) + `msptest 3` (4 threads ×
  1M iters) PASS on both TWRP (4.4.263) and Donut (4.4.302) kernels
  with the fixed libc (md5 `2b6469277b6c4375849ea3c0d96f85d3`) and
  libcutils (md5 `af710d64a95e0936f8841de6f37afe44`); baseline
  (barrier-less) `msptest 3` = SIGSEGV (rc=139) on both kernels.

## Bringup log: Donut touch panel + keyguard (2026-10-18)

- **TEST 43 — the clearpad panel stays silent until userspace writes
  `post_probe_start`; `vtouch` now does it (Donut 1.6 has no init trigger
  for it).**
  - Symptom: touch works nowhere in Donut — `getevent` on vtouch's
    virtual node (`/dev/input/event7`) and on the preserved real node
    (`/dev/input/vtouch_saved`) both show zero events while a finger
    drags across the panel, and the lock screen never reacts. The
    framework side looks fine: EventHub/KeyInputQueue accept
    `maple_vtouch` (id 0x10000, ABS_X 0..2159 / ABS_Y 0..3839), so the
    fault is below the relay.
  - Diagnosis: boot dmesg has `clearpad clearpad: (clearpad_probe:9356)
    post_probe_start sysfs is required to start post probe` — the driver
    finishes i2c probe (~1.8 s) and then parks: panel powered but silent
    (no IRQ, no events). The gate
    `/sys/devices/virtual/input/clearpad/post_probe_start` (class symlink
    `clearpad -> input7`) reads `0` until the first write.
  - Runtime proof: writing `1` makes dmesg show `start post probewith
    post_probe_start sysfs` → `begin 'post probe' session` → `ensure
    firmware` → device id `PLG1163-C1` (family 0xd1, fw rev 0x03.02) →
    `end post probe ... (rc=0)`, and the same finger drag then produces
    5800 raw protocol-A events on the real node and 1571 translated
    events (BTN_TOUCH + ABS_X/Y) on the virtual one — the clearpad →
    vtouch → Donut chain is good once the panel is awake.
  - Where the write comes from elsewhere: stock Sony init, the KitKat
    port (`devices/maple/kitkat/init.maple.rc`, `on post-fs-data`) and
    TWRP (`/init.recovery.maple.rc`, `on post-fs`: `# start
    clearpad_post_probe when filesystem is available for fwflash`) —
    which is why the touch screen works in recovery on the same kernel.
    Donut 1.6's init has no `post-fs`/`post-fs-data` triggers at all
    (`system/core/init/init.c` queues `early-init`/`init`/`early-boot`/
    `boot` + property triggers), and the sysfs node only appears during
    kernel probe (~2.9 s in this boot), so a one-shot `on boot` write
    can silently race it.
  - Fix: `devices/maple/donut/vtouch/vtouch.c` `start_post_probe()` —
    run after the virtual-node substitution (system_server's one-shot
    input scan must still find `maple_vtouch`) and before the relay
    loop; opens `/sys/devices/virtual/input/clearpad/post_probe_start`
    (stable symlink path — never hardcode `inputN`) and writes `1`,
    retrying 1x/s for up to 60 s. No events can exist before the write,
    so the wait loses nothing.
  - Verification: runtime side done (above). Boot side verified (2026-10-19) — on
    the first boot of the vtouch-write build: dmesg post probe with no
    manual poke + `/dev/.vtouch.log` `touch panel post probe started
    (try N)`. Verified on the vtouch-write build (2026-10-19): dmesg shows
     clearpad_post_probe_start_store -> start post probe @ 3.043 -> ensure
     firmware -> end post probe ... (rc=0) with no manual poke, and
     /dev/.vtouch.log shows `touch panel post probe started (try 0)` +
     `relaying on /dev/input/event7`. The same boot also exposed the
     firmware-fallback stall (TEST 45).
- **TEST 44 — no MENU key on this hardware: the camera shutter full
  press now sends MENU, and the keyguard unlocks on a tap.**
  - Background: Donut predates action bars; the non-secure keyguard's
    only unlock input is `KEYCODE_MENU` (`LockScreen.onKeyDown` →
    `mCallback.goToUnlockScreen()` → `keyguardDone(true)`), and apps
    expect a MENU key too. The lock screen hint said "Press Menu to
    unlock." — a button that does not exist.
  - Hardware: `getevent` with no device argument (TWRP, same kernel)
    shows the two-stage shutter on `gpio-keys`/event6 emitting clean
    pairs — half press `0001 0210` (KEY_CAMERA_FOCUS 528), full press
    `0001 02fe` (KEY_CAMERA_SNAPSHOT 766); 16 down/up pairs for 16
    presses. First proof the button works at all.
  - Fix 1 — physical MENU: `devices/maple/donut/gpio-keys.kl` maps
    `key 766 MENU WAKE_DROPPED` (full press only — a full press also
    emits the half-press code, so mapping both to MENU would fire two
    MENU presses per press and toggle a just-opened menu shut). The half
    press (528) is now `BACK` (no wake flag) for in-app navigation
    (was `FOCUS`); the camera's autofocus on half-press is given up for
    this (camera HAL is not started on this port). Tokens checked against
    Donut's
    `frameworks/base/include/ui/KeycodeLabels.h` (`MENU`=82, `FOCUS`=80,
    `CAMERA`=27) because `KeyLayoutMap` discards the whole file on an
    unknown token.
  - Fix 2 — tap to unlock: `LockScreen.onTouchEvent` calls the very same
    `mCallback.goToUnlockScreen()` on ACTION_DOWN; a secure keyguard
    still routes through its own `UnlockScreen`. The keyguard window is
    touchable (`KeyguardViewManager`: TYPE_KEYGUARD with
    FLAG_FORCE_NOT_FULLSCREEN / optional FLAG_ALT_FOCUSABLE_IM — no
    FLAG_NOT_TOUCHABLE) and no child consumes touches outside the
    emergency button, so the tap lands on the LockScreen view. Hints
    updated to "Tap to unlock." / "Tap to unlock or place emergency
    call.".
  - The SIM-missing keyguard trap: `stuckOnLockScreenBecauseSimMissing()`
    (`mRequiresSim && !isDeviceProvisioned() && simState == ABSENT`) and
    PUK_REQUIRED make `goToUnlockScreen()` a silent no-op; the port's
    `system.prop` already sets `keyguard.no_require_sim=true` (so
    `mRequiresSim` is false), and the dead-end SIM lock screen can never
    appear (the test device has no SIM anyway).
  - Patches:
    `patches/donut/frameworks-policies-keyguard-touch-unlock.patch`
    (apply from `aosp-donut/frameworks/policies/base`) and
    `patches/donut/frameworks-base-keyguard-touch-unlock.patch` (apply
    from `aosp-donut/frameworks/base`).
  - Verification: hardware codes above. Verified (2026-10-19): tap unlocks
    the keyguard ("Tap to unlock."), camera full press (766) delivers MENU
    (launcher menu opens), volume up/down work, and the launcher stays up
    (no crash loop). The keychar fix (0644) is what made the launcher
    stable.
- **adb's device serial tells you which boot you are in.** Donut boots
  advertise `mapledonut`; TWRP advertises the phone's own serial +
  `recovery` (`CB512EGGHD  recovery`) and does NOT mount `/system`
  (`ls /system/bin` → `No such file or directory`, tools look
  "missing"). Check `adb devices` before debugging a vanished binary.

## Bringup log: Donut default orientation stuck in landscape (2026-10-19)

- **TEST 48 — the flip-cover hall sensor (bu520x1nvx) reports "lid open" and
  Donut forces landscape.**
  - Symptom: the home screen (and every app) renders in landscape
    (horizontal) on a portrait panel; rotating the phone does nothing
    (no auto-rotate — the sensor HAL, `sensors.maple.so`, is absent).
  - Diagnosis: `WindowManagerService` logged `Setting rotation to 1,
    animFlags=1` on every boot. `PhoneWindowManager.rotationForOrientationLw`
    returns `Surface.ROTATION_90` (1) when `mLidOpen` is true, and
    `readLidState()` sets `mLidOpen = (getSwitchState(0) == 0)`. The
    bu520x1nvx hall sensor (flip-cover, SW_LID, input4) reports 0 with no
    cover attached, so Donut treats the flip cover as a sliding keyboard
    being opened and forces landscape. The framebuffer itself is 1080x1920
    (portrait) and touch coordinates already match it; only the rotation
    policy is wrong.
  - Fix: `frameworks/policies/base/.../PhoneWindowManager.java`:
    `KEYBOARD_ALWAYS_HIDDEN = false -> true` (maple has no sliding
    keyboard), and guard the lid-open branch with `!KEYBOARD_ALWAYS_HIDDEN`,
    so a flip-cover "open" no longer forces landscape. Recorded as
    `patches/donut/frameworks-policies-orientation.patch` (applied from
    `aosp-donut/frameworks/policies/base`).
  - Verification: verified on the orientation-fixed image (2026-10-19) —
    home screen is upright (portrait) and stays portrait (no auto-rotate
    without the sensor HAL, which is absent on this port).

## Bringup log: power button handling (2026-10-19)

- **TEST 47 — the power button is handled by the kernel's `powerkey_handle`,
  not by Android's input system, and is blocked by USB debugging.**
  - Symptom: pressing the power button does nothing visible while the phone
    is on USB (adb). dmesg shows the kernel *does* react:
    `registering powerkey_handle handle for qpnp_pon` at boot, and each
    press logs `PM: suspend entry` -> `msm-dwc3 a800000.ssusb: Abort PM
    suspend!! (USB is outside LPM)` -> `PM: suspend exit`. The qpnp_pon
    input node declares only scancodes 19/20 (KEY_R/KEY_T) in its
    capability bitmask and delivers no evdev events to userspace, so
    Android's input system never sees a `KEYCODE_POWER`.
  - Diagnosis: the PMIC power key is handled at the kernel level by
    `powerkey_handle`, which attempts a full system suspend. With USB
    debugging active the USB controller is not in low-power mode, so the
    suspend is aborted and the screen stays on — "nothing happens". With
    USB unplugged the suspend succeeds and the screen locks (kernel path).
    `PowerManagerService.goToSleep()` does not turn off the display in this
    port either: sleep goes through the ported lights HAL, not
    PowerManagerService, so there is no Android-level lock path today.
  - Attempted/considered fixes: a keylayout entry for qpnp_pon cannot help
    (no evdev events arrive); a userspace daemon tailing `/dev/kmsg` for
    `PM: suspend entry` and calling `service call power 2 i32 <uptime>`
    (goToSleep) does not turn off the display in this port; turning the
    backlight off via `/sys/class/backlight/panel0-backlight/brightness`
    would only blank the panel, not lock. A real fix needs either the
    PM->lights integration in PowerManagerService or a kernel change making
    qpnp_pon report `KEY_POWER` via evdev (kernel source is a prebuilt blob
    here, `work/maple-donut/kernel-Image.gz-dtb`).
  - Workarounds documented: unplug USB and the power button locks the
    screen (kernel path); during development use
    `adb shell "service call power 2 i32 0"` is a no-op for display, so use
    `adb shell "echo 0 > /sys/class/backlight/panel0-backlight/brightness"`
    to blank and `echo 4095 > .../brightness` to restore. Not blocking the
    port; tracked as an open item.

## Bringup log: Donut touch firmware fallback + key character maps (2026-10-19)

- **TEST 45 — the clearpad post probe is gated behind a 3x60 s firmware-load
  fallback.**
  - Symptom: touch works nowhere for ~3 minutes after every boot, then
    suddenly works (lock screen starts responding to taps, home screen
    usable). Rebooting and testing within the first 3 minutes reproduces
    "touch is broken"; waiting past ~3 minutes "fixes" it. The panel is not
    actually broken — the kernel driver is busy.
  - Diagnosis: dmesg timeline on a clean boot: `[3.043] start post probe` ->
    `[3.306] ensure firmware` -> `[63.336] retry fwflash (0)` ->
    `[123.390] retry fwflash (1)` -> `[183.443] retry fwflash (2)` ->
    `[183.496] WA for Yoshino BU` -> `[183.759] end post probe @ 183.759
    (rc=0)`. Each `retry fwflash (N)` is preceded by a 60 s
    `request_firmware(touch_module_id_0xd1.img)` wait that fails with
    rc=-11 (EAGAIN) because Donut has no userspace firmware loader (no
    ueventd firmware handling); the driver retries 3x, then resets the
    panel (SW reset) and completes the post probe. Touch emits only after
    that. Confirmed by the user: "touch started working on home screen" on a
    boot older than ~3 minutes, "touch stopped working after restarting"
    (reboot, tested inside the 3-minute window), and "It reacts now" at
    uptime 328 s (> 184 s).
  - Knob: the firmware class exposes a class attribute
    `/sys/class/firmware/timeout` (default 60, seconds) — the wait length.
    Writing `1` makes each retry fail in ~1 s, so the whole post probe
    finishes ~4 s after the kick instead of ~184 s. Confirmed writable on
    this kernel (readback 1).
  - Fix: `devices/maple/donut/vtouch/vtouch.c` `set_fw_timeout()` writes `1`
    to that attr first thing in `start_post_probe()` (before the
    post_probe_start write), so the driver sees the new timeout before its
    first request. Version bumped to v1.2.0-maple3. No belt-and-suspenders
    write in init is needed: no other driver requests firmware during boot on
    this port (dmesg shows zero other `fw request` lines), and vtouch runs
    at ~3 s, before the first request at ~3.3 s.
  - Verification: knob write verified live (readback 1); the driver refuses
    to re-run once post probe has completed (`clearpad_post_probe_start_store:
    already post probed, need reboot`), so the end-to-end timing
    (`end post probe @ ~7 s`) is verified only after the next flash+reboot.
    Verified on the maple3 image (2026-10-19): dmesg shows `end post probe
    @ 6.529345232 (rc=0)` with retries at 4.10/5.16/6.21 s, vtouch log
    shows `firmware load timeout set to 1s`, and touch responds on the lock
    screen within seconds of boot (user-confirmed "touch works off the bat").
  - Generic lesson: porting-notes/donut.md "Firmware loads that wait for a
    missing userspace helper".

- **TEST 46 — key character maps shipped mode 0640: Settings/Launcher
  SIGSEGV.**
  - Symptom: opening Settings (or any app that loads a key character map)
    crashes the app with SIGSEGV. logcat shows
    `W/KeyCharacterMap: Can't open keycharmap file '/system/usr/keychars/
    gpio-keys.kcm.bin'` then `E/KeyCharacterMap: Can't find any keycharmaps
    (also tried /system/usr/keychars/qwerty.kcm.bin)` then
    `I/DEBUG: signal 11 (SIGSEGV), fault addr 00000004` with
    `#00 pc ... /system/lib/libui.so` -> `#02 ... /system/lib/
    libandroid_runtime.so` -> `#03 ... /system/lib/libdvm.so`. Tombstones
    tombstone_01/02/04/05/09 are com.android.settings; tombstone_00/03/06/07/
    08 are android.process.acore (the Launcher, which runs in that process on
    Donut). The launcher crash loop observed earlier in the bringup (TEST
    43/44 timeframe) is the same fault.
  - Root cause: `build/tools/kcm/kcm.cpp` creates the `.kcm.bin` files with
    `open(..., O_RDWR|O_CREAT|O_TRUNC, 0660)`. The build's umask (022) leaves
    them 0640 (`root:root`, no other-read), so app processes (uid app_*)
    cannot open them (`open()` returns EACCES -> `try_file()` returns NULL
    -> `KeyCharacterMap::load()` returns NULL -> the native JNI path
    dereferences it -> fault addr 4). The keylayout (`.kl`) and idc files
    are 0644 and load fine, so the camera->MENU mapping and touch idc were
    never affected by this.
  - Fix: `build/tools/kcm/kcm.cpp` 0660 -> 0644 plus `fchmod(out, 0644)`
    (creat mode alone is masked by umask), recorded as
    `patches/donut/build-kcm-file-mode.patch` (applied from
    `aosp-donut/build`); plus belt-and-suspenders `chmod 0644` on
    `*.kcm.bin` in `scripts/build-maple-donut-image.py`. The packaging
    script's chmod is what makes the flashable image correct even if the host
    tool is unpatched.
  - Verification: runtime side verified (debugfs showed the image's
    `/usr/keychars/*.kcm.bin` at mode 0640 before the fix; logcat showed the
    crash backtrace in libui; after the fix the files are 0644 in the new
    image). Flash side verified (2026-10-19): the device's
    `/system/usr/keychars/*.kcm.bin` are mode 0644, logcat shows 0
    `Can't find any keycharmaps` warnings and 0 `has died` lines on the
    fresh boot, and Settings opens without crashing.

## Bringup log: libhyb, the in-process Adreno driver (2026-10-18)

- **TEST 49 (owner: "glrenderd must become obsolete" — port
  [DoomGuy41/Taimen-KitKat](https://github.com/DoomGuy41/Taimen-KitKat);
  2026-10-18):** the graphics stack is replaced. `libhyb`
  (`devices/maple/kitkat/hyb/`) loads the LineageOS 22.1 (Android 15)
  closure — Adreno 540 driver, QTI gralloc1, dependencies — **directly into
  every KitKat process**; `egl/` shims keep the KitKat library names and
  forward (GLES through generated per-function stubs); `gralloc.maple`
  provides gralloc 0.2 + the fb HAL on the QTI gralloc1 (real ION buffers
  with the QTI handle, cloned across processes); `fbpresentd/` (64-bit,
  NDK-built) presents through an **MDSS atomic commit** on DMA0 with
  copy + `FBIOPUT_VSCREENINFO` as the fallback. The chroot, the emugl
  transport, `tools/glrenderd/`, the four `*glrenderd*` patches, the
  `patches/kitkat/files/` overlay, `BUILD_EMULATOR_OPENGL`, the goldfish
  guest libs and the `GLConsumer` bound are **gone**; **TEST 30–41 above
  describe the superseded glrenderd design** — kept as history, their
  tooling/patches are deleted. Full writeup:
  [libhyb](../../advanced/software/libhyb.md).
  - **Build evidence (server, 2026-10-18):** closure staged by
    `tools/libhyb/stage.py` from a Lineage 22.1 zip — 19 libs
    (`vendor/lib/egl`: driver + `eglSubDriverAndroid` + `libq3dtools`;
    `vendor/lib`: `libc++`, `libgsl`, `libllvm-glnext`,
    `libgrallocutils`, `libqdMetaData`, …; `system/lib`: `libsync`,
    `libvndksupport`) + a530/a540 firmware, with `libc/libm/libdl/liblog/
    libcutils/libz/ld-android/libdl_android` resolved to **KitKat's own**
    bionic. `fbpresentd` built aarch64 static (NDK r21e, 4.0 MB).
    Image contents verified: `/system/lib/libhyb.so`,
    `/system/lib/hw/gralloc.maple.so`, `/system/lib/egl/{egl.cfg,
    libEGL_adreno.so, libGLESv1_CM_adreno.so, libGLESv2_adreno.so,
    libGLES_android.so}`, `/system/bin/{fbpresentd,hybtest,rtc-restore}`,
    `/system/a15/{vendor,system}/...`.
  - **KitKat's EGL loader does not read `egl.cfg`.** `Loader.cpp` scans
    `/vendor/lib/egl` then `/system/lib/egl` with readdir for
    `libEGL_*.so` and takes **the first hit** (it explicitly skips only
    `libGLES_android.so`); `egl.cfg` is inert at runtime on this release.
    With the emulation libs (`libEGL_emulation.so`, …) still present the
    choice of driver was readdir-order luck — hence the emulation libs are
    not built any more, and a stale copy in the out tree must be deleted.
    (Verified against the server tree's `Loader.cpp`, lines 265–350.)
  - **Removing files from `PRODUCT_*` does not remove them from the out
    tree.** The first libhyb image silently shipped the whole 25 MB
    `system/glr/` chroot, `libEGL_emulation.so` + friends,
    `libOpenglSystemCommon.so`, `lib*_enc.so` and `/sbin/glrenderd.sh`:
    `mkuserimg.sh` packs whatever `out/target/product/maple/system` holds,
    and dropped `PRODUCT_COPY_FILES`/`PRODUCT_PACKAGES` entries leave the
    old files (owned by root from the container build) behind. Fix: delete
    the stale paths from the out tree and rebuild the images
    (`cleanup-rebuild.sh` on the server). Any "why is this file still in
    my image?" ends here.
  - **`LOGI` is not a KitKat macro.** `cutils/log.h` includes
    `log/log.h`, which defines `ALOGI`/`ALOGE` — the first `rtc-restore`
    build failed at link with `undefined reference to 'LOGI'` (the
    unexpanded name became an implicit function call).
  - **`__tls_get_addr`: the "risk closed by inspection" verdict was
    wrong.** The build-time walk (`llvm-objdump` over `libc++`, `libutils`,
    `libhidlbase`, `libgsl`, `libllvm-glnext`, `libgrallocutils`,
    `libqdMetaData`) found 0 call sites, and `hooks.c` shipped an aborting
    tripwire on that basis. The closure *does* need TLS, though:
    `libc++`, `libutils` and `libhidlbase` carry `R_ARM_TLS_DTPMOD32`
    relocations (5, all symbol index 0, `PT_TLS` a pure `.tbss`), and the
    first libhyb boot died on them (zygote killed at `preloadOpenGL`,
    surfaceflinger aborting in `init()`). A relocation is not a call site —
    `readelf -r` on the closure, not a disassembly symbol search, is what
    answers "does this need TLS". The loader now implements
    DTPMOD32/DTPOFF32 plus a real `__tls_get_addr` (fix and evidence at the
    end of this TEST).
  - **32-bit `MSMFB_ATOMIC_COMMIT` crashes the kernel** (from the Taimen
    port; `verify-me` on maple): that is why `fbpresentd` is a 64-bit
    out-of-tree binary. The copy fallback still works from 32-bit
    (`FBIOPUT_VSCREENINFO` + yoffset, TEST 30).
  - **hwui is enabled now** (`USE_OPENGL_RENDERER := true` in
    BoardConfig, Taimen parity): the glrenderd-era builds never set it, so
    `libhwui` was not built at all (the UI thread drew in software and only
    SurfaceFlinger composited on the GPU). With a real ES2/ES3 driver in
    the process the accelerated UI path is the point of the port, and the
    `frameworks-base-hwui-gles2-cap.patch` keeps hwui on its tested ES2
    behaviour. First-flash check: `dumpsys gfxinfo` shows real draw times
    and `libhwui.so` is in `/system/lib`. Taimen also sets
    `TARGET_DISABLE_TRIPLE_BUFFERING := true` (two app buffers instead of
    three) — a candidate tuning here, not adopted without measurement.
  - **The closure's TLS relocations made the driver unloadable (fixed
    2026-10-19).** Symptom: on the first libhyb boot every GL consumer
    died at once — zygote was killed a few seconds after start (last
    message before the kill: `ZygoteInit.preloadOpenGL()`,
    `ZygoteInit.java:237-245`) and surfaceflinger aborted inside
    `SurfaceFlinger::init()+86`. Root cause: the Android 15 closure's
    `libc++.so`, `libutils.so` and `libhidlbase.so` carry
    `R_ARM_TLS_DTPMOD32` relocations (5, all with symbol index 0, slots in
    RW/RELRO segments, `PT_TLS` a pure `.tbss` with filesz 0) and the
    loader rejected unknown relocation types, so `libEGL_adreno.so` never
    came up. Fix: real thread-local support in
    [`hyb/linker.c`](../../../devices/maple/kitkat/hyb/linker.c) — a module
    table (`hyb_tls_register` at load, `DT_TLS` captured in `map_file`),
    `DTPMOD32`/`DTPOFF32` resolved against it, and a `__tls_get_addr` that
    lazily allocates (and caches, per thread) one `posix_memalign` block per
    module, copying the `PT_TLS` template. Only `DTPMOD32` is actually
    used, 17 occurrences, all symbol 0 → module = self.
    `R_ARM_TLS_TPOFF32` is deliberately *not* implemented and fails loudly
    (same inspection that found `__emutls_get_address` absent, TEST 49
    build evidence). Verified: `hybtest` gets an ES3 context
    (`EGL 1.5`, `Adreno (TM) 540`, `OpenGL ES 3.2 V@331.0`), green clear
    pixel-checks, and clears 2000 frames + `glFinish` in 18.2 ms. Driver
    load takes 9 ms in-process.
  - **The GPU and ION nodes are root-only, and nothing non-root could
    render (fixed 2026-10-19).** Symptom: surfaceflinger aborted with
    `LOG_ALWAYS_FATAL("no suitable EGLConfig found, giving up")`
    (`SurfaceFlinger.cpp:407` → `selectConfigForAttribute` at 321 — all
    three attempts returning 0 configs) while the same closure rendered
    fine in `hybtest`. Root cause, visible only from a root-run
    surfaceflinger (see the logging item below): `Adreno-GSL:
    <ioctl_kgsl_driver_entry:777>: open(/dev/kgsl-3d0) failed: errno 13.
    Permission denied`, `qdgralloc: Init: Failed to open ion device -
    Permission denied`, then `libEGL: eglInitialize(...) failed
    (EGL_BAD_ALLOC)` — KitKat's `eglChooseConfig` reports every config as
    missing once `eglInitialize` failed, which is what the fatal assert
    really meant. The kernel creates both nodes `root:root 0600` and
    `ueventd.maple.rc` had no rules: the glrenderd design never needed
    them, its renderer ran as root inside the chroot. Fix:
    `/dev/kgsl-3d0` and `/dev/ion` at 0666 in
    [`ueventd.maple.rc`](../../../devices/maple/kitkat/ueventd.maple.rc)
    (0666 rather than a group rule: the callers are uid `system`, app uids
    `u0_aNN` and root, and DAC is the whole story on this
    SELinux-permissive port). A live `chmod 666` on an already-booted
    device confirms the fix without a reflash; the rules only take effect
    from a reflashed ramdisk. Related trap: **init's surfaceflinger
    service has `onrestart restart zygote`** (stock KitKat), so an SF
    crash-loop kills zygote every few seconds and the framework never
    gets through `preloadClasses` — one graphics bug presented as three
    unrelated failures (zygote restart loop, SF abort, no boot).
  - **A service's `ALOG*` output goes nowhere unless `/dev/kmsg` is
    writable (diagnosed 2026-10-19).** init gives services `/dev/null` as
    stdout/stderr and this kernel has no `/dev/log`, so liblog's kmsg
    fallback ([`patches/kitkat/README.md`](../../../patches/kitkat/README.md))
    is the only sink — but `open("/dev/kmsg", O_WRONLY)` from uid `system`
    fails EACCES and liblog then points `write_to_log` at
    `__write_to_log_null`: the message is dropped with no error anywhere.
    Only root processes (liblog opens kmsg itself) and zygote's
    descendants (they inherit zygote's already-open fd) ever reached
    dmesg, so app and system_server logs were visible while
    surfaceflinger, netd and media were mute. Fix: `chmod 0622
    /dev/kmsg` early in
    [`init.rc`](../../../devices/maple/kitkat/init.rc) — 0622, not 0666:
    non-root services may write the kernel log, reading stays root-only
    (`dmesg` needs CAP_SYSLOG anyway). Verify with
    `log -p i -t TESTTAG hi; dmesg | grep TESTTAG` as a non-root uid, or
    from the booted device by running a service by hand (a root-run copy
    opens kmsg itself: `stop surfaceflinger; surfaceflinger`).
  - **Zero-copy presentation leaves fb0 black (trap, 2026-10-19).** With
    the atomic-commit path active fb0's memory is never written — DMA0
    scans the ION buffer itself. A `dd if=/dev/graphics/fb0` readback of a
    perfectly driven panel is all zeros (both pages, `pan 0,0`), so the
    fb0-decode checks from the glrenderd era say nothing here; use
    `fbpresentd`'s commit lines below, `dumpsys SurfaceFlinger`, or force
    the copy path (`persist.gralloc.fb_commit=0`) when a readback is what
    you want.
  - **KitKat's screen timeout is 60 s** (`def_screen_off_timeout`), so a
    panel that is dark a minute after boot is the framework working, not a
    dead backlight: `/sys/class/leds/lcd-backlight/brightness` reads 0 and
    `settings put system screen_brightness 200` (idempotent; the lights HAL
    writes both lcd-backlight and wled) brings it back.
  - **`fbpresentd` now logs through `/dev/kmsg`** (init gives daemons
    /dev/null): `ready`, `client connected/disconnected`, commit failures
    with `strerror`/layer error code, the first three commits, and one line
    every 600 commits with a measured rate —
    `fbpresentd: commit #600 1080x1920: previous frame scanned out, 59.9 fps`.
    The release-fence state is the panel evidence: the MDP only signals the
    previous commit's `release_fence` once DMA0 has read the buffer for
    scanout, so "scanned out" separates a driven panel from commits the MDP
    merely accepted (a fence that never signals inside 100 ms is logged as
    `NOT scanned out in 100 ms`).
  - **On-device results (2026-10-19, first boot with the driver in the
    boot path).** `dmesg | grep -i hyb` shows the closure loading in order
    (`libc++`, `libutils`, `libhidlbase`, `libgsl`, `libllvm-glnext`, …,
    `egl/libEGL_adreno.so`, `egl/libGLESv2_adreno.so`), then
    `egl_adreno: Adreno 540 driver ready (gralloc import ok)`;
    `gralloc.maple` reports `fb0 1080x1920, line 4352 bytes, double buffer
    1, BGRA 0, atomic commit 1`; `dumpsys SurfaceFlinger` reports
    `GLES: Qualcomm, Adreno (TM) 540, OpenGL ES 3.2 V@331.0`, `EGL
    implementation: 1.5`, `EGL_NATIVE_VISUAL_ID: 1`, `refresh-rate:
    60.000002 fps`, with the built-in display's layers composing at
    1080x1920; and the boot reaches `sys.boot_completed=1` with
    `init.svc.surfaceflinger=running` and `init.svc.bootanim=stopped`
    without any manual step (the previous image needed the live
    `chmod 666`, this one has the rules in the ramdisk). Node readback on
    that boot: `crw-rw-rw- root root /dev/kgsl-3d0`, `crw-rw-rw- root
    root /dev/ion`, `crw--w--w- root root /dev/kmsg`.
  - **Backlight timeline on a cold boot (2026-10-19).** Full brightness
    during the boot animation (`4095`, left by the kernel/bootloader),
    then the framework takes over and both nodes read `1638`
    (`mScreenBrightnessSetting=102` scaled to 0-4095) — the lights HAL
    path of TEST 41 still works with libhyb; the panel is dark again a
    minute later because `def_screen_off_timeout` is 60000 (stock) and
    dims instead of switching off when `stay_on_while_plugged_in`
    matches.
  - **fps 59.5-60.0 measured, 59.9 reported by the presenter.**
    `dumpsys SurfaceFlinger --latency
    com.android.settings/com.android.settings.Settings` during a swipe
    scroll: 127 frames over 2.10-2.12 s = 59.5/60.0 fps; the presenter's
    600th-commit line reports `59.9 fps while active` for the same kind of
    burst (50.7 for a slow scroll). **Trap:** the first version of that
    line averaged over the whole window including idle gaps — SF posts
    *nothing* while the screen is static — and printed `2.3 fps` while
    `--latency` said 60.0; the metric now skips gaps over 100 ms, which is
    what "while active" means.
  - **`persist.gralloc.fb_commit=0` A/B (2026-10-19).** gralloc logs
    `atomic commit 0`, the copy +
    `FBIOPUT_VSCREENINFO`-yoffset path takes over, and fb0 readback now
    contains the composed frame in *both* pages (`dd if=/dev/graphics/fb0
    bs=4194304 count=4`, 2093/2108 sampled pixels non-zero); decoding with
    the 4352-byte line (1088 px) stride and cropping to 1080x1920 gives a
    clean UI frame — ~35k distinct colours, the KitKat wallpaper and
    status/nav bands in place,
    ![decoded fb0 frame](fb0-decode-copy-path-2026-10-19.png) — the TEST 30
    fb0-decode check still works on this path, and that is how the copy
    fallback itself was verified. Restored `persist.gralloc.fb_commit=1` and
    reflashed: the property lives in /data, so it survives a system flash —
    reset it by hand if an A/B is left behind, otherwise the device stays on
    the slow path.
  - **Panel stays up (idle power collapse).** After ~15 min of a static
    screen the backlight still reads `1638` and the next swipe burst
    presents at `59.9 fps while active` with every release fence
    signalling — the DMA0 pipe did not collapse. The DT property strip
    (`qcom,mdss-idle-power-collapse-enabled`, step 4 of the build docs) is
    in the flashed kernel (md5 `4a7b3eaf…`). Visual confirmation over a
    long idle still belongs to the owner; this is the automated
    evidence.

## Bringup log: audio — ACDB calibration + framework playback (2026-10-19)

- **TEST 50 (owner: audio parity with
  [DoomGuy41/Taimen-KitKat](https://github.com/DoomGuy41/Taimen-KitKat);
  2026-10-19):** **playback through the framework works.** An `AudioTrack`
  tone (`audiotrack_test`) plays through AudioFlinger and
  `audio.primary.maple`; a parallel DMIC capture shows the 440 Hz tone at
  **~99 % of the 150–3000 Hz band energy** for three consecutive seconds
  (peak ~180–240 at music volume 11/15; the quiet control sits at 0.02–0.08),
  with no `cal_block` errors in dmesg. Verified on the flashed images
  (boot.img `0b0e750db3d6b6381d1e17f95e89af8c`, sparse system.img
  `3b3d03bf03744ee8d468a0cd8039e7fd`; TWRP md5 readbacks matched). The
  chain, in the order it bit:
  - **No calibration means no audio, even with a running PCM.**
    q6asm/q6afe look ACDB blocks up in the kernel at stream/port start
    (`send_afe_cal_type`, `q6asm_send_cal`); with none they log
    `cal_block not found!!` / `cal_block is NULL` and the dsp drains the
    buffer faster than realtime with silence on the speaker. Blocks are
    pushed through `/dev/msm_audio_cal` by a userspace loader and are tied
    to that process's fd, so the HAL owns the loader for the life of
    mediaserver (the resident `acdbtest --v3 --hold` helper is the probe
    that established it; a die-after-sending helper loses the blocks).
  - **The loader is a newer-Android library, so it rides libhyb.**
    `libacdbloader.so` (+ `libaudcal`, `libacdbrtac`, `libacdb-fts`,
    `libadiertac`, `libmiscta`, `libdiag`, the vendor `libtinyalsa`) is
    staged by `tools/libhyb/stage.py` (`ACDB` roots) exactly like the
    Adreno driver, and `audio_hw.c` calls `acdb_loader_init_v2(card, NULL,
    0)` + `acdb_loader_send_audio_cal_v3(15, 1, 69936, 48000, 0)` before
    the route and `pcm_open` (`acdb_stream_cal()`; app type 69936 =
    qualcomm's `DEFAULT_APP_TYPE_RX_PATH`, acdb id 15 = speaker from the
    vendor `audio_platform_info.xml`).
  - **`hw_platform` picks the acdbdata directory.** The loader builds
    `/vendor/etc/acdbdata/<hw_platform>/` from
    `/sys/devices/soc0/hw_platform`, which reads **MTP** on maple, while
    the Lineage yoshino tree ships its data under `Sony/`. Staging it as
    `Sony/` leaves the loader without a workspace file; `stage.py` copies
    `vendor/etc/acdbdata/Sony` → `acdbdata/MTP` and `full_maple.mk`
    installs it to `/system/vendor/etc/acdbdata/MTP` (8 files: 7 `.acdb` +
    `workspaceFile_Sony_yoshino.qwsp`). The vendor tree's own `MTP/`
    directory holds a different (tavil-era) calibration — the yoshino
    (`Sony/`) set is the right one for this tasha codec, staged under the
    `MTP` name.
  - **`/dev/msm_audio_cal` is `root:root 0600` and mediaserver is uid
    `media`.** The loader's open() failed EACCES and the failure surfaced
    later and indirectly, as `ADIE RTAC INIT` →
    `allocate_cal_block failed!` → `Cannot allocate memory!` → init
    `-ENOMEM`. Fix in `ueventd.maple.rc`: `0660 media audio` (verified:
    `crw-rw---- media audio /dev/msm_audio_cal`). Stock uses the same
    group rule.
  - **mediaserver opens the HAL before the card probes.** `adev_open()` runs
    at ~5 s; the tasha card registers at ~8 s (the adsp boots first), so
    the old code cached `card_name = "tavil"` and a NULL mixer forever —
    `speaker_path(NULL)` was a no-op, `pcm_open` on the by-then-present
    card 0 still succeeded, and playback was silent with a *running* PCM.
    `ensure_card()` now re-detects and opens the mixer on first stream, and
    `acdb_ensure()` only initializes the loader once the card is up
    (initializing earlier dies in ADIE RTAC init with `-ENOMEM`).
  - **KitKat's tinyalsa `pcm_write()` / `pcm_read()` take BYTES, not
    frames.** `external/tinyalsa/pcm.c` divides the count by the frame size
    itself, so `pcm_write(pcm, buffer, frames)` wrote only 1024 of every
    4096 bytes AudioFlinger handed over — the dsp got a chopped signal and
    the speaker stayed silent. Dynamic debug showed it directly:
    `msm_pcm_playback_copy:fbytes =1024: xfer=1024 size=4096` for the HAL
    vs `=2048: xfer=2048` for `tone_test`. Fix: pass `bytes` on both paths
    (`out_write` and `in_read`), and the same in `tools/tone_test.c`.
  - **Debug method that worked.** debugfs is **not** mounted at boot
    (`/d` is a dangling symlink to `/sys/kernel/debug`); `mount -t debugfs
    none /d` then gives `dynamic_debug/control`, and
    `echo "file msm-pcm-q6-v2.c +p" > control` (also `q6asm.c`, `q6adm.c`,
    `q6afe.c`, `msm-pcm-routing-v2.c`) traces every buffer/cal step in
    dmesg. That is what proved the HAL was writing (it was) and the cal was
    arriving (it was) — the count/unit bug was underneath.
  - **Runner tools.** `acdbtest` (`devices/maple/kitkat/hyb/`, libhyb;
    `--init-only`, `--send ID TYPE`, `--v3 --hold 15 1`) proved loader +
    paths + the fd-lifetime rule before the HAL owned calibration.
    `audiotrack_test` (`devices/maple/kitkat/audio/`, in the image) drives
    AudioTrack → AudioFlinger → HAL, the only path that exercises the HAL's
    own stream handling; `tools/tone_test` talks to tinyalsa directly and
    stays the low-level route/capture tool.
  - **Two adjacent fixes.** `adsp_boot` gained `oneshot`: without it init
    restarted the script every ~5 s and every run re-wrote
    `/sys/kernel/boot_adsp/boot` (`adsp_loader_do: scheduling work to load
    ADSP fw` spam, and a reload tramples live streams); after the fix,
    `dmesg | grep -c "boot_adsp write returned"` = 1 per boot. And
    `full_maple.mk` inherits `frameworks/base/data/sounds/AllAudio.mk` —
    the image had no `/system/media/audio` at all, so there were no
    ringtones or UI effects.
  - **Still open.** Capture calibration (DEC7 dmic route runs
    uncalibrated — TX calibration is accepted by the loader but kills the
    ADM, see TEST 51; TX app type 69937 / `Audio Stream Capture 0 App
    Type Cfg` is wired but disabled by `MAPLE_TX_CAL`); BT SCO stays
    known-dead (CHARQUIRKS). UI key clicks were silent for an unrelated
    reason — no `media_codecs.xml`, so SoundPool could not decode
    `Effect_Tick.ogg` — fixed and verified in TEST 52 (a hardware volume
    key plays nothing in AOSP KitKat at all; the touch path is the one
    that plays a sound effect).

## Bringup log: audio — framework capture, routing, TX calibration (2026-10-19)

- **TEST 51 (owner: audio parity with
  [DoomGuy41/Taimen-KitKat](https://github.com/DoomGuy41/Taimen-KitKat);
  2026-10-19):** **capture through the framework works, the headphone
  route + its calibration work, and TX (capture) calibration is
  deliberately off.** `audiorecord_test` (new: AudioRecord →
  AudioFlinger → `audio.primary.maple`) records the 440 Hz tone that
  `audiotrack_test` plays through the same HAL: dominant frequency
  **440.00 Hz** over 5.97 s, per-second `rms` 92–94 (peak 199–299), no
  `cal_block` and no `adm_callback` errors. Solo capture of a quiet room
  reads peaks 85–174 / rms 20–32. Verified on the flashed images
  (boot.img `0b0e750db3d6b6381d1e17f95e89af8c`, sparse system.img
  `9d11eb2cb5a5036f6d74413e9b7c59d5`, TWRP raw readback
  `41f30fb19692367aab5f265ba00e3849`; TWRP md5s matched. The same checks
  passed on the intermediate system `5ff353b7b6c5b3d5e2892cef76022553`).
  - **`ACDB_DEV_TYPE_IN` is 2, not 0.** The magic 0 came from nowhere —
    `hardware/qcom/audio/hal/audio_hw.h` (the KitKat qcom HAL in this
    tree) defines `ACDB_DEV_TYPE_OUT 1` / `ACDB_DEV_TYPE_IN 2`. With 0 the
    Android 15 loader logs `ACDB -> Device is not RX or TX!acdb_id = 4` and
    `acdb_loader_send_audio_cal_v3` returns non-zero — 65 for that fresh
    first id, 66/67 for others, and 66 for an id that already went
    through with dev_type 2. With 2 the same call returns 0 and
    the log shows `send_audio_cal, acdb_id = 4, path = 1, app id =
    0x11131, sample rate = 48000` → `GET_AFE_TOPOLOGY_ID for adcd_id 4,
    Topology Id 112fb` → `send_hw_delay : acdb_id = 4 path = 1`.
  - **…but that TX calibration makes every capture prepare fail.** The
    next `pcm_open(PCM_IN)` + prepare dies: when the HAL itself sent the
    cal it is the ADM that gives up (`adm_callback: cmd = 0x10326
    returned error = 0x1`,
    `msm_pcm_routing_reg_phy_stream: adm open failed copp_idx:-131`,
    `msm_pcm_capture_prepare: stream reg failed ret:-22`,
    `msm-pcm-dsp … ASoC: platform prepare error: -22`); when only
    `acdbtest --scan` sent it (no capture app-type cfg, HAL never opened
    a capture) the SLIM TX backend fails instead
    (`msm-dai-q6-sb-0-tx: ASoC: cpu DAI prepare error: -131`,
    `SLIMBUS_0_TX: ASoC: backend prepare failed -131`). Either way
    `AudioRecord` never gets data (the record hangs) and a plain
    `tone_test cap` fails identically, so it is the cal blocks, not the
    HAL; and they are **not** freed by `kill -9 mediaserver` — capture
    stays dead until reboot (verified twice).
    `audio_hw.c` therefore has `MAPLE_TX_CAL 0` with these logs in the
    comment: capture runs uncalibrated (and works), the RX side is
    unaffected (acdb 15/10 both carry topology `112fc`). Repro on a
    scratch boot if you touch it: one `audiorecord_test` after enabling
    TX cal, or one `acdbtest --scan`, is enough to poison capture for
    that boot.
  - **The loader's RX/TX device table is a hardcoded list.** New
    `acdbtest --scan` (one loader init, then a batch of ids, documented
    in the tool's own comment) on a fresh boot of the final image:
    RX 15 (speaker), 10 (headphones), 799 (speaker+headphones) → all
    return 0; TX 4 (handset mic), **8 (headset mic)**, 9, 6, 35, 131,
    143, 152 → all return 0 with `dev_type=2`. `dev_type=0` (what the
    port first sent) is rejected — `ACDB -> Device is not RX or TX!acdb_id
    = %d` in the log, return 65/66/67 depending on the id and order.
    **The scan's TX sends load real blocks, so they poison capture for
    the rest of that boot** (the -131 SLIM TX failure above; playback is
    unaffected) — run it last. Also **run the scan on a fresh boot,
    before the first framework stream**: once mediaserver has
    initialized the loader, a second `acdb_loader_init_v2` dies in
    `cal_block_ion_alloc: audio ION import failed, rc = -22` →
    `voice_alloc_cal … failed` → init `-12` (the first process keeps the
    cal blocks, and they outlive it).
  - **`routing=` is a signed int, and the policy re-sends it at
    startInput.** `AudioPolicyManagerBase.cpp` uses
    `param.addInt(String8(AudioParameter::keyRouting), (int)newDevice)`
    (lines 235/476/1022), so input devices arrive as negative decimals
    (0x80000004 → -2147483644) and must be parsed with
    `str_parms_get_int` + a cast back to `uint32_t` (a `%u` sender
    overflows to `0x7fffffff` — verified the hard way). The HAL now picks
    the route from that value at stream open: speaker (acdb 15) or
    headphones (acdb 10); builtin mic (4) or headset mic (8). RX ids come
    from the vendor `audio_platform_info.xml`/`platform.c` table, TX ids
    from `hardware/qcom/audio/hal/msm8974/platform.c`'s
    `acdb_device_table[]` (`IN_HANDSET_MIC 4`, `IN_HEADSET_MIC 8` —
    headset mic is **8**, not the 9 guessed from the 8974-era table).
  - **`route out 0x8 -> headset (acdb 10)` verified.** `audiotrack_test 3
    48000 0.5 8` sends `routing=8` through
    `AudioSystem::setParameters(0, …)` the way the policy does on a jack
    insert; the HAL logs the headset choice, applies the vendor
    `headphones` path (RX INT1/RX2, `CLSH_DSM_OUT`, RX1/RX2 volume 76),
    sends cal 10 (`Topology Id 112fc`), prepares and plays — no ADM
    errors. Audibility still needs headphones on the owner's end
    (nothing plugged = no load). The route only re-applies when the HAL
    stream opens, so the test needs the previous output to go to standby
    (~3 s); a second track started immediately reuses the running stream
    and keeps the old route (observed).
  - **No `h2w` switch exists, so the jack never routes itself.**
    `/sys/class/switch/` holds `hdmi hdmi_audio lid sim_detect wfd`
    only. KitKat's `WiredAccessoryManager` watches
    `/sys/class/switch/h2w/state`, so nothing ever sends `routing=4/8`
    on its own, and an app cannot force the *input* device either: the
    policy re-sends its own input routing at `startInput`
    (`dev routing=0x80000010 (in)` immediately followed by
    `in routing=0x80000004` → builtin mic). The `headset-mic` path
    (`adc2` → DEC0 → SLIM TX0) is written and compiles into the route
    switch but is **unexercised** — verify-me once the kernel exposes a
    jack source (wcd-mbhc switch/extcon bridge).
  - **Runner tools.** `audiorecord_test [seconds] [rate] [file]
    [routing]` is the capture sibling of `audiotrack_test` (raw S16_LE
    mono + per-second peak/rms, routing optional); `audiotrack_test
    [seconds] [rate] [volume] [routing]` gained the routing argument;
    both are in the image and set the routing *after* constructing the
    track/record (openOutput/openInput would otherwise overwrite it) and
    *before* start/write (the HAL opens the PCM lazily on the first
    buffer). Loopback recipe:
    `adb shell 'audiotrack_test 8 48000 0.8 & sleep 1; audiorecord_test 6 48000 /data/local/tmp/loop.raw; wait'`
    then `python3 scripts/analyze-rec.py loop.raw` on the host after pulling
    it (raw S16_LE mono, 5.97 s → 440.00 Hz / rms 92–94 above).


## Bringup log: media decode — the missing media_codecs.xml (2026-10-19)

- **TEST 52 (owner: media parity — stock plays decoded audio, this port
  did not; 2026-10-19):** **every compressed media format was silent
  while raw PCM played: the image had no
  `/system/etc/media_codecs.xml`.** Found while chasing the UI click
  (a nav-bar tap): the click path itself is correct (`KeyButtonView` →
  `AudioManager.playSoundEffect` → AudioService's SoundPool), but
  SoundPool reported `Unable to load sample: (null)` and
  `onLoadSoundEffects(), Error -22 while loading samples`; Music's
  AudioPreview on the same kind of ogg ringtone reported
  `MediaPlayer: error (1, -2147483648)` = `MEDIA_ERROR_UNKNOWN` +
  `UNKNOWN_ERROR` (0x80000000, `utils/Errors.h`). (Hardware volume keys
  play no sound in AOSP KitKat — the old "volume-key click not
  observed" note was a red herring; the touch path is the one to test.)
  - **Why raw worked and compressed did not.**
    `AwesomePlayer::initAudioDecoder` bypasses OMX for `audio/raw`
    (`mAudioSource = mAudioTrack`), so WAV (WAVExtractor → audio/raw)
    played normally — the HAL opened for it (`route out 0x2 -> speaker
    (acdb 15)`, cal loaded), which is also why the earlier "speaker
    playback works" checks passed. ogg/mp3 need `OMXCodec::Create`; it
    returned NULL for every compressed MIME.
  - **Root cause.** This 4.4.4 tree's `OMXCodec::findMatchingCodecs`
    asks `MediaCodecList`, which `fopen`s **`/etc/media_codecs.xml`**
    (`frameworks/av/media/libstagefright/MediaCodecList.cpp`). If the
    file is missing it logs `MediaCodecList: unable to open media codecs
    configuration xml file.` (ALOGW — the only line that reached dmesg)
    and `initCheck()` stays `NO_INIT`, so `getInstance()` returns NULL;
    `findMatchingCodecs` leaves the vector empty and `OMXCodec::Create`
    returns NULL with `No matching codecs!` logged at **ALOGV** — which
    `-DNDEBUG` compiles out, so the failure is silent at default log
    level. Our image had no `/system/etc/media_codecs.xml`: AOSP ships
    one only for goldfish/sdk
    (`device/generic/goldfish/camera/media_codecs.xml`, pulled in by
    `sdk.mk`), and `full_maple.mk` wrongly assumed it "ships with the
    stock build".
  - **The two error codes are the same failure at two layers.** Music's
    MediaPlayer surfaces `initAudioDecoder` → `UNKNOWN_ERROR` (log
    `error (1, -2147483648)`); SoundPool's decode goes through
    `MediaPlayerService::decode` → `AudioCache::notify` records
    `MEDIA_ERROR`/`UNKNOWN_ERROR`, `cache->wait()` then fails and decode
    returns `BAD_VALUE` (-22) — hence `Error -22` from AudioService while
    the same dmesg window shows `AudioCache: Error 1, -2147483648
    occurred`.
  - **Fix.** New `devices/maple/kitkat/media/media_codecs.xml` lists the
    `OMX.google.*` components that `SoftOMXPlugin` provides and this
    image builds (17 `libstagefright_soft_*.so`; the soft MPEG4 library
    supplies both `mpeg4` and `h263`, `vpxdec` supplies vp8+vp9; the raw
    decoder is not listed because `MediaCodecList` adds it itself).
    `full_maple.mk` installs it as `/system/etc/media_codecs.xml`
    (`/etc` is the usual symlink to `/system/etc`, checked on-device).
  - **Verified on the flashed image (2026-10-19).** boot.img
    `0b0e750db3d6b6381d1e17f95e89af8c`, sparse system
    `f8ab40bee62442f96b55f5cecb369061`, TWRP raw readback
    `ab879f3b082338fc838d1818055d3536` (TWRP reported `ALL VERIFIED`).
    - **MP3:** Music's `AudioPreview` on `file:///sdcard/test440.mp3`
      (an ffmpeg 440 Hz sine) → HAL `route out 0x2 -> speaker (acdb
      15)`, topology `112fc`, no `MediaPlayer: error`; the DMIC loopback
      of it (`audiorecord_test 5 48000`, analysed with
      `scripts/analyze-rec.py`) reads **440.00 Hz** over 0.00–2.25 s
      (rms 48.9→18.9).
    - **OGG:** `ANDROMEDA.ogg` from `/system/media/audio/ringtones`
      through the same AudioPreview → same route/cal, no error.
    - **UI click:** a nav-bar tap in Settings now opens a HAL stream
      (`route out 0x2 -> speaker (acdb 15)`) and logs neither
      `SoundPool: Unable to load sample` nor `onLoadSoundEffects(),
      Error -22` (both were the pre-fix symptom).
    - **Video (same fix).** Gallery `MovieActivity` on
      `file:///sdcard/testvid10.mp4` (ffmpeg `testsrc` 320×240, h264
      Baseline + 880 Hz AAC) → `OMXCodec: [OMX.google.h264.decoder] AVC
      profile = 66 (Baseline), level = 12 / video dimensions are 320 x
      240`, `SoftAAC2: Reconfiguring decoder: 0->44100 Hz`, `MediaPlayer:
      info/warning (3, 0)` (= MEDIA_INFO_VIDEO_RENDERING_START), HAL
      `route out 0x2 -> speaker (acdb 15)` + cal 15/`112fc`. A screenshot
      taken mid-playback shows the testsrc pattern (colour bars + circle)
      scaled full-screen to 1080×1920 with letterboxing, and the DMIC
      loopback of the track reads **880.00 Hz** (rms 182) — so the
      software h264 decoder, the GL composition path and the audio track
      all work together.
    - `ACDB_CMD_GET_AFE_COMMON_TABLE_SIZE` / `ACDB AFE returned = -19`
      with `Failed to fetch the lookup information of the device
      0000000F` is **benign** here: the loader logs it on every speaker
      cal send and continues with `AUDIO_SET_AFE_CAL` — the successful
      video run shows the same pair (the yoshino MTP acdbdata has no AFE
      common table for dev 15; the AFE cal that is applied is the
      topology/`send_afe_cal` one, and the sound loops back at full
      level).
    - The first flash attempt aborted mid-way when TWRP dropped off USB
      for ~10 minutes; the battery read 100 % when TWRP came back, so
      that was a TWRP/USB crash, not a low battery. The second run
      flashed and verified cleanly.
  - **Port implication.** The "speaker audio" status the port carried
    was *raw-PCM-only*: ringtones, notification/alarm sounds, UI clicks
    and music (all ogg/mp3 in KitKat) could never play. The same missing
    file also disabled *video* decode (no h264/vpx components), so the
    port could not play video either; with the XML shipped, both are
    verified (above). A KitKat port that has no OEM codec list must
    supply its own.

- [ ] TEST 53 (WiFi bringup — modem restart loop, 2026-10-19, IN PROGRESS):
  the modem (MSM8998 PIL `pil-q6v5-mss 4080000.qcom,mss`) asserts its
  err-fatal line via **smp2p bit 0 of the `slave-kernel` entry (modem→AP
  SSR channel)** every **41.36 s** (`subsys-restart: Restart sequence
  requested for modem, restart_level = RELATED.`), so `wlan0` never
  appears (icnss reports `recovery.root_pd_crash` climbing, all WLFW QMI
  counters stay 0). Verified on-device (`maplekk`, `sys.boot_completed=1`).
  - **Root cause of the restart (identified).** The first kernel line at
    each cycle is `modem subsystem failure reason: (unknown,
    smem_get_entry_no_rlock failed).` — that print comes from
    `log_modem_sfr()` in `pil-q6v5-mss.c`, called only from
    `restart_modem()`, which is called only from
    `modem_err_fatal_intr_handler` (the PIL's err-fatal IRQ) and
    `modem_wdog_bite_intr_handler`. The wdog handler would print
    "Watchdog bite received from modem software!" (it doesn't), and its
    IRQ count is 0, so the path is the **err-fatal IRQ handler** running
    silently: the modem's FW asserts err-fatal (via smp2p bit 0) and the
    AP just collects the (empty) SSR reason and restarts it. The SFR
    SMEM item (`SMEM_SSR_REASON_MSS0`) is never written → no SFR string
    → consistent with a **watchdog bite / boot-time handshake timeout in
    the modem FW**, not an AP-side crash. The modem's err-fatal IRQ is
    delivered over smp2p (GPIO 709 = bit 0 of `slave-kernel`, remote-pid
    1): its count rises +1 per cycle, and raising the AP-side
    `master-kernel` bit 0 (GPIO 677, the SSR ack) to 1 makes the modem
    crash immediately (3.16 s cadence) — confirming bit 0 is the SSR ack,
    not a "boot complete" signal. Reverting restores 41.36 s.
  - **AP-side ruled out.** All A15 64-bit daemons run in situ (per
    process `LD_LIBRARY_PATH=/system/lib64:/vendor/lib64`) and the modem
    still cycles: `rmt_storage` (EFS reads succeed, no writes),
    `tftp_server` (RFS tree present, no requests observed),
    `cnss-daemon -n -l`, `pd-mapper` (real `.jsn` tables, identical
    cadence to the in-tree `pdserve` transcription), `irsc_util`. Extra
    daemons (`port-bridge`, `netmgrd`, `imsqmidaemon`, `ta_qmi_service`,
    `dpmQmiMgr`, `mlog_qmi_service`) also do not change the cadence.
    Killing the service locator (no pd-mapper/pdserve) does not change
    it. Opening SMD channels 7/8/11 does not change it. The kernel's
    QMI clients (service-notifier → TMS inst 180, sysmon → SSCTL inst 18)
    connect and ack the modem's SSCTL indications. SHM is not configured
    for the modem. The modem's err-fatal is therefore **internal to the
    modem FW**.
  - **Ramdump collection did not work.** `enable_ramdumps=1` was set at
    runtime and `/dev/ramdump_modem` + `/dev/ramdump_md_modem` were held
    open, but no `Ramdump(ramdump_modem)`/`Ramdump(ramdump_md_modem)`
    message appeared and both files stayed 0 bytes — the modem's ramdump
    callback (`modem_ramdump`) did not run (or took the minidump branch
    without pushing). The modem's EFS (`modemst1`) is encrypted, and
    Sony's `last_subsyslog` (`/proc/last_amsslog`) reports
    "Could not read SMEM ID. NULL" (no SFR/log written). So the modem's
    own log is not currently reachable.
  - **Hypothesis (open, updated 2026-10-19).** The modem's FW
    (A15/Sony, `DEBUG1` flag in `modem.mdt`) watchdog-bites ~41 s
    after boot on this port. Additional ruled-out since TEST 53 was
    opened: smp2p `master-kernel` bits 1 and 2 (neutral), ADSP boot
    (subsys6 ONLINE does not change the cadence), and all remaining
    A15 userspace daemons (`port-bridge`, `netmgrd`, `imsqmidaemon`,
    `ta_qmi_service`, `dpmQmiMgr`, `mlog_qmi_service`). The modem's
    watchdog bite is therefore **not** about any AP-side daemon, SMD
    channel, smp2p bit, SHM, QMI ack, or ADSP boot. Most likely
    cause remaining: a kernel-level feature the modem's FW depends
    on (QRTR) or a fundamental incompatibility between the A15 modem
    FW and this port's userspace. The modem's own log is still not
    reachable (ramdump path does not push data; EFS is encrypted;
    SMEM SFR item is never written). Candidates still to test:
    (1) provide the missing AP-side "boot complete" signal (not smp2p
    bit 0 — that is the SSR ack); (2) a QRTR-dependent QMI service the
    legacy 4.4 IPC router lacks; (3) a minidump/ramdump collection path
    that actually captures the modem's log. Until the modem stops
    restarting, WLFW/`wlan0` cannot come up.
  - **Next (verify-me):** (1) find the AP-side signal the modem waits
    for (try toggling other smp2p `master-kernel` bits / check the
    modem's DIAG via `/dev/diag`); (2) collect the modem's ramdump
    (fix the minidump/ramdump path so `/dev/ramdump_md_modem` receives
    data); (3) once the modem stays up, confirm WLFW QMI activity and
    `wlan0`; (4) integrate `root64/**` into `full_maple.mk`
    (`PRODUCT_COPY_FILES`), add init services with per-service env, the
    `/dev/block/bootdevice` symlink, and the RFS boot script; rebuild,
    flash, verify.
