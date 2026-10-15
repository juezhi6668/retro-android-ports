# KitKat 4.4.4 on the Sony Xperia XZ Premium (maple)

Device-specific notes for the maple port — the generic KitKat playbook
lives in [`docs/porting-notes-kitkat.md`](../porting-notes-kitkat.md).
The Donut/cooper playbook
([`docs/porting-notes.md`](../porting-notes.md),
[`docs/devices/cooper.md`](cooper.md)) is the method reference; this doc
is the maple delta.

Status: **BOOTED 2026-10-15** — full KitKat userspace on the 4.4 kernel:
`sys.boot_completed=1`, 75 binder services, Launcher compositing at
1080x1920, working touch, USB adb (`18d1:4ee7`). The tree in
[`devices/maple/kitkat/`](../../devices/maple/kitkat/) is a commented,
boot-verified device tree. Remaining gaps (no cellular/audio/camera/HW GL
yet — same documented-limitations posture as cooper early on) are tracked
at the bottom; blockers 1–4 below are all resolved.

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
3. **Display/framebuffer — SOFTWARE PATH WORKING.** No KitKat EGL/HWC
   for Adreno 540; libagl fallback renders correctly at 1080x1920 once
   gralloc reports the kernel stride (see TEST 28). glrenderd (adreno
   in a Lineage chroot) runs but stays OFF: its uncoordinated flips
   wedge the mdss overlay start (EBUSY). No DTB/driver work needed
   for basic display.
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
bringup log). Pre-build tooling (no server needed for these):
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
      glrenderd stays off until its split is re-architected.
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
- [x] gpu (split tested 2026-10-15): no KitKat adreno driver for the 540;
      libagl software fallback renders the full UI correctly at
      1080x1920 (fb0 decodes pixel-perfect) once gralloc reports the
      kernel stride (see TEST 28). glrenderd (adreno in a Lineage-20
      chroot, 12 fps readback) RUNS but stays OFF by default: its
      uncoordinated FBIOPAN flips wedge the mdss overlay start (EBUSY)
      and freeze the panel; also its flips=0 startup race (fb HAL
      doubles yres_virtual after glrenderd samples it). Re-architect
      before returning (single flipper). ES2 gap stays: software GLES
      is ES1-only, so ES2 clients (live wallpaper → canvas fallback
      patch; hwui apps) need glrenderd or another driver. glrenderd
      design (ported from
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
- [x] touch and buttons (touch TESTED working 2026-10-15): clearpad
      probes after `post_probe_start` in post-fs-data (as lineage);
      `clearpad.idc` + full-range firmware coords map 1:1 (no scaling
      fix needed); `gpio-keys.kl` shipped (volume keys dispatch
      verified: keyCode 24 in RecentQueue); power via qpnp_pon +
      Generic.kl; nav bar on (`config_showNavigationBar`). Two-stage
      camera key (528/766) UNTESTED.
