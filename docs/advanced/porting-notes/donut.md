# Porting notes: Donut 1.6 on newer hardware

Playbook distilled from bringing AOSP Donut (`android-1.6_r2`, DRD35) up
on a Samsung Galaxy Ace GT-S5830 (`cooper`, 2011, MSM7227) with a modern
Arch host. Everything below applies to any attempt to build/run Donut on
post-2009 hardware; cooper-specific facts live in the
[cooper quirks](../../devices/cooper/quirks.md), the step-by-step build in
[build/donut.md](../build/donut.md). For the KitKat-era delta (4.4.4 on
newer hardware, e.g. maple), see [porting-notes/kitkat.md](kitkat.md).
Back to the [advanced index](../README.md).

## Source & toolchain
- **Cupcake is gone from AOSP.** `platform/manifest` has no `cupcake` /
  `android-1.5` branch left, only `android-1.6_r1…` and later, so
  `repo init -b cupcake` is impossible and Donut is the oldest thing you
  can still check out. (This is why the project starts at Donut; custom
  1.5 builds are out of scope.)
- **Donut on post-2009 hardware is a natural fit.** It builds `armeabi`
  (armv5te), which runs on ARMv6 SoCs like cooper's MSM7227; its HVGA-era
  UI matches the 320×480 class of 2010–2012 phones, making those a good
  candidate pool for more Donut ports.
- **Java 5 is mandatory** (`build/core/main.mk` greps for 1.5). Modern JDKs
  can't even target 1.5 bytecode. Only practical route: a period chroot.
- **Host GCC is too new** for Donut's host tools (`apriori` etc. die on
  `-Wincompatible-pointer-types` under GCC 16). Same answer: chroot.
- Our working recipe: `debootstrap --arch=i386 hardy` from
  `old-releases.ubuntu.com` + Sun `sun-java5-jdk` (1.5.0_15) + GCC 4.2.3 +
  Make 3.81 + Python 2.5. Donut's ARM toolchain (`arm-eabi-4.2.1`, 32-bit)
  runs fine on a 64-bit host once 32-bit loader libs exist.

## Chroot gotchas (Hardy on a modern host)
- Modern debootstrap tries `InRelease` first (Hardy has none; 404s) then
  falls back to `Release` — give it time, and bound `wget` retries/timeouts,
  because some `old-releases` endpoints blackhole (20s+ stalls otherwise).
- **Pin one good endpoint** if downloads stall (we used `185.125.190.37`
  via `/etc/hosts` + `nsswitch.conf → hosts: files` inside the chroot).
- **Ancient apt vs modern Apache:** disable pipelining
  (`Acquire::http::Pipeline-Depth "0"`) or index fetches corrupt.
- **`sudo`'s `secure_path` lacks `/bin`**, so apt inside the chroot cannot
  exec `/bin/bzip2`/`/bin/gzip`/`/bin/tar` → every `.bz2` index fails with
  "sub-process bzip2 returned an error code". Always invoke chroot commands
  with an explicit `PATH` including `/bin:/sbin`, or symlink the tools into
  `/usr/bin`.
- Sun Java 5 needs its license pre-accepted:
  `debconf-set-selections` with `shared/accepted-sun-dlj-v1-1 boolean true`
  for `sun-java5-bin/-jre/-jdk`, then `apt-get -f install`.
- Do **not** run debootstrap/mkbootimg-era commands under `linux32` unless
  needed; plain `chroot`/`sudo chroot` is fine on x86_64.

## Donut build-system surprises
- `TARGET_BOARD_PLATFORM := msm7k` drags in Dream-era `hardware/msm7k`
  HALs (`libcamera2`, …) that need **proprietary camera headers absent from
  Donut's bionic** → use `USE_CAMERA_STUB := true` and preferably your own
  platform name so none of them build; use generic audio
  (`HAVE_HTC_AUDIO_DRIVER`, `BOARD_USES_GENERIC_AUDIO`).
- `BOARD_GPS_LIBRARIES := libloc_api` links a Gingerbread-era proprietary
  lib with no Donut build rule (`No rule to make target ... libloc_api.so`)
  → leave it unset (generic `gps.cpp` builds).
- `packages/providers/CalendarProvider/tests` needs proprietary
  `com.google.android.googlelogin` (vendor/google, not in AOSP) → ship a
  tiny `vendor/googlelogin` stub java library + one line in the test's
  `LOCAL_JAVA_LIBRARIES` (see `patches/donut/`).
- **`frameworks/base/preloaded-classes` lists proprietary
  `com.google.common.*` / `com.google.masf.*`** (GTalk-era). On eng builds
  with adb enabled, `ZygoteInit` throws `IllegalStateException: Missing
  class(es) for preloading` and **zygote crash-loops forever** (61 restarts
  observed, load ~5). Fix: delete those lines (keep the two resolvable
  `io.protocol.ProtoBuf*` ones). Symptom in logcat: the exact class list.
- `unzip` missing in the chroot **silently** breaks host java-library merges
  (`layoutlib_create` then dies with `NoClassDefFoundError:
  org/objectweb/asm/...`). Install it.
- `mkbootimg` hardcodes pagesize 2048 **and** a 2048-byte padding buffer —
  larger pagesizes corrupt output. Patch in `--pagesize` + an 8K buffer
  (`patches/donut/mkbootimg-pagesize.patch`).
- Donut `make` does **not** track `BoardConfig.mk` flag changes: flipping
  e.g. `BOARD_USES_GENERIC_AUDIO` rebuilds nothing that consumes the flag
  (`libaudiointerface`/`libaudioflinger` stay stale — we shipped a whole
  build whose new `libaudio.so` was statically linked against the old
  GENERIC_AUDIO `AudioHardwareInterface.o`, so it silently kept using
  `/dev/eac` → stub). After flag changes, `rm -rf` the affected
  `out/target/product/<device>/obj/{STATIC_LIBRARIES/libaudiointerface_intermediates,SHARED_LIBRARIES/{libaudio,libaudioflinger}_intermediates}`
  and rebuild; verify the result (`AudioHardwareInterface.o` must reference
  `createAudioHardware`, `libaudioflinger.so` must reference `libaudio.so`
  and not `/dev/eac`).
- `lunch` exports go missing if you pipe it: `lunch full_cooper-eng 2>&1 |
  tail` runs lunch in a **subshell**, so the following `make` builds
  `generic` with no error. Redirect to a file and `cat` it instead.

## Donut init limitations (vs CWM-era memory)
- `mount` takes **3 args** (`type location mountpoint`); there is **no
  `wait` flag** (silently ignored), no `on fs` trigger (dead code), and
  `exec` is an unimplemented stub (returns -1).
- `format()` takes **2 args** and only understands `MTD` — anything else
  aborts the script. No ext4 formatting from Edify on non-MTD devices;
  wipe via recovery UI instead.
- `write_raw_image(file, partition)` treats the partition as an **MTD
  name**; anything else aborts. We patched ours with a raw block-device
  fallback (see `patches/donut/bootable-recovery.patch`), but prefer
  platform tools.
- Donut has **no `run_program`** (Eclair added it). We backported a ~30-line
  `fork`/`execv`/`waitpid` version returning `""` on failure so asserts work.
- `TARGET_PROVIDES_INIT_RC := true` lets a device ship its own ramdisk
  `init.rc` (else `system/core/rootdir/init.rc` wins).
- `import` inside init.rc sections is a **no-op** in Donut (do_import
  returns -1). Machine-specific `/init.<hardware>.rc` **is** parsed — with
  the hardware name lowercased and **truncated at the first space**
  (`GT-S5830 Board` → `gt-s5830`), which is how `init.gt-s5830.rc` loads.
- `package_extract_dir` is FILES_ONLY: symlinks in the zip extract as tiny
  text files. Ship symlinks as proper symlink entries + explicit
  `symlink()` commands (Donut's own `ota_from_target_files` pattern).
- Donut EGL **ignores `egl.cfg`** (hardcodes `libagl.so` software +
  `libhgl.so` hardware, falling back to software). There is **no gralloc
  HAL concept** (surfaceflinger drives the framebuffer directly).

## Donut userspace on modern kernels
- Donut adbd's shell uses **/dev/ptmx** (`grantpt`/`unlockpt`); when pty
  setup misbehaves every shell hangs with zero diagnostics while sync
  (pull/push) keeps working. Replacement: split pipes in
  `create_subprocess` (bionic of this era has **no `socketpair()`** — use
  two `pipe()`s and teach the poll loop the split fds).
- **`adb shell <cmd>` can still hang forever after the pipe rework: the
  service argument is a stack-use-after-return.** `service_to_fd("shell:…")`
  built `const char* args[2]` on its own stack and passed the pointer to
  `create_service_thread()`, which runs the service in a *new* thread; by
  the time the thread reads `args` the frame is gone, so the child is
  exec'd as a bare `/system/bin/sh` with no `-c` — it starts
  *interactively*, blocks in `read(0)` on stdin and the host waits for a
  payload that never comes (symptom: the client's trace shows `OKAY`
  right after sending `shell:<cmd>`, then nothing; `sync:` pull/push
  work because they pass `NULL`). Diagnose from the device side even with
  no working shell: `adb pull /proc/<pid>/cmdline` (shows only
  `/system/bin/sh\0`), `/proc/<pid>/wchan` (`pipe_read`) and
  `/proc/<pid>/syscall` (`3 …` = `read(0)`), with `<pid>` found in the
  bootlog process snapshot while a long `adb shell sleep 60` is open.
  Fix: `strdup()` the command in `service_to_fd`, build `-c <cmd>`
  inside `shell_service`, `free()` when the child exits. The same latent
  bug existed in `reboot:` (argument points into the OPEN packet, freed
  when the transport is done) — strdup there too. See
  `patches/donut/system-core.patch`.
- Donut vold **crash-loops (SIGSEGV, NULL devpath)** on foreign uevents and
  spins retrying `mknod` on `EEXIST`, pinning a core and flooding logcat.
  Guard the NULL + treat EEXIST as success, or drop vold until SD support
  is revisited.
- `rild` without its proprietary RIL `.so` crash-loops and drags dependent
  services with it — comment the service out if you have no Donut RIL lib.
- `system/core/adb` host tooling builds fine in the chroot if you ever need
  a period-correct adb client.
- **configfs USB gadget binding on a modern kernel: Bind, never unbind.**
  The UDC attribute write for `ffs` only succeeds after the userspace side
  has written descriptors + strings to ep0; before that the kernel logs
  `configfs-gadget …: failed to start g1: -19` and the write fails. The
  bind is *asynchronous* — the write returns (even `rc=0`) before the
  gadget has started (`~0.4 s` later), so reading the UDC back in the same
  instant shows empty. A maintenance loop must therefore poll and rebind
  when the UDC reads empty, but never write `""` to it: unbinding a live
  gadget drops the USB link to the host, and an unbind/rebind loop flaps
  the host port until the QCOM dwc3 PHY wedges (host log shows
  connect/disconnect every cycle; the phone needs a power cycle). Also
  remember that closing the ffs instance (adbd exit/disconnect — old adbd
  builds close ep0) unbinds the gadget, so the rebind loop must stay alive
  for the whole boot. Give adbd ~12 s to write descriptors + strings
  (earlier writes fail `-19` harmlessly but noisily), then just retry the
  write every few seconds: a redundant write against a bound UDC is
  refused with `-EBUSY`, so the loop needs no "readiness" gate at all —
  in particular no `ep1`-existence poll (tried on maple and abandoned;
  the boot that had "no USB" had a completely different cause, see the
  shell bullet below). If the loop should only bind while unbound, note
  that the unbound UDC attribute is exactly one `"\n"` byte — but that
  check needs a shell whose conditionals work, and **Donut's own shell
  has no `[`** (next bullet). Maple 2026-10-18 recipe that works:
  `cmp $UDC /cache/usbudc-unbound >/dev/null 2>&1 && echo a800000.dwc3 >
  $UDC` with a one-newline reference file. Implementation + evidence:
  maple [quirks.md](../../devices/maple/quirks.md) TEST 42.
- **Donut's `/system/bin/sh` has no `[` / `test` — not as a builtin, not
  as a binary.** Every `[ … ]` prints `[: not found`, returns non-zero,
  and the conditional silently takes the **else** branch; a
  `while [ $i -lt 600 ]` performs zero iterations. A bring-up script that
  gated its configfs UDC write on `[ -z "$(cat $UDC)" ]` thus never bound
  the gadget and a whole boot came up with no USB while all its log lines
  looked innocuous (maple, 2026-10-18; found by chrooting into the Donut
  `/system` from TWRP and running the shipped script with Donut's own
  shell). What does work — verified the same way — `while/until/if/case`,
  `break`, `&&`, `||`, `!`, `{ }`, `$( )`, `$(( ))` (including `%`),
  redirections and comments, all driven by *command exit statuses*;
  toolbox `cmp` (no `-s`) is the usual test stand-in, e.g.
  `cmp a b >/dev/null 2>&1 && …`. Also absent from `/system/bin`:
  `grep`, `sed`, `awk`, `tr`, `head`, `tail`, `wc`, `pidof`; `echo` is a
  builtin (`sleep`, `cat`, `ls`, `cmp`, `kill`, `ps`, `date`, `dd`,
  `sync`, `reboot`, `getprop` exist) and **there is no `/tmp`** — write
  scratch files to `/cache` or `/data`.
- **FunctionFS magic values are per kernel tree, not per kernel version.**
  This port's adbd writes the descriptors/strings blobs from hand-defined
  constants (Donut's bionic has no `<linux/usb/functionfs.h>`). Picking the
  modern `0x502`/`0x503` against a kernel whose uapi still carries the
  ancient enum (`DESCRIPTORS_MAGIC = 1`, `STRINGS_MAGIC = 2`,
  `DESCRIPTORS_MAGIC_V2 = 3` — whatawurst msm8998 4.4 does) makes
  `write(ep0)` return `-EINVAL` with no other symptom: adbd loops writing
  descriptors forever, `desc_ready` never latches, the UDC bind keeps
  failing `-19`, and nothing in kmsg explains it. The kernel's own f_fs
  QCOM IPC log (`/sys/kernel/debug/ipc_logging/f_fs/log`) shows
  `__ffs_data_got_descs: exit: ret -22` with no `ffs_do_descs` lines, which
  pins the rejection to the header check (length or magic). Always read the
  target kernel's `include/uapi/linux/usb/functionfs.h` first, or lift the
  values from a blob known to work on that kernel (TWRP's adbd carries them
  in `.rodata`). Full recipe + a no-reboot on-device test harness: maple
  [quirks.md](../../devices/maple/quirks.md) TEST 42.

## ARM atomics on weakly-ordered (multi-core) SoCs
- **Donut's bionic `__atomic_*` drops the kuser helpers' full memory
  barriers.** `bionic/libc/arch-arm/bionic/atomics_arm.S` reimplements
  `__atomic_cmpxchg/inc/dec/swap` with LDREX/STREX but omits the `dmb`
  that the original kuser helpers provided. `cutils/atomic.h` documents
  the contract ("all reads or writes issued before android_atomic_*
  operations are guaranteed to complete before the atomic operation takes
  place" = full barrier). On a weakly-ordered multi-core SoC (MSM8998
  Cortex-A73 in AArch32 compat mode) this breaks mutual exclusion in
  pthread mutexes (bionic pthread.c) and libutils `futex_mutex` (both
  call `__atomic_*`), corrupting the native heap under contention (abort
  in `mspace_free` during GC). Symptom: multi-threaded processes
  (system_server, acore) crash-loop with heap corruption; reproduced by
  `msptest 3` (4 threads × 1M iters) = SIGSEGV rc=139. Fix: add `dmb sy`
  (`.word 0xf57ff05f`) before and after every LDREX/STREX in all four
  `__atomic_*`; also rewrite libcutils `android_atomic_swap` (the one
  remaining raw `swp`) as LDREX/STREX+dmb. Verified: msptest 3/4 PASS on
  both kernels after the fix; SIGSEGV on the barrier-less baseline.
- **`swp` is gone on ARMv8; kuser helpers are still there.** On this
  4.4 kernel `CONFIG_KUSER_HELPERS=y` and `CONFIG_COMPAT=y`, so the
  kuser page (0xffff0000, version 5) is mapped and the kuser-based
  `android_atomic_*` (libcutils) all pass conformance. `CONFIG_SWP_EMULATION=y`
  means `swp` is emulated, not fatal — the earlier "kuser/page trap"
  theory was wrong; the real cause was the missing barriers. Reimplementing
  the swap with LDREX removes the emulation cost and the `swp` dependency.
- **Reproducer:** `external/msptest/msptest.c` modes 0–4 (mode 3 hammer,
  mode 4 conformance). Build with `scripts/build-maple-donut.sh msptest`.
  Invocation under TWRP/Donut: `LD_LIBRARY_PATH=/system/lib /data/msptest
  <mode>`. Push to `/data/msptest` (rw remount of `/dev/block/sde46`).

## Touch panels that wait for a userspace handshake
- Some modern panel drivers finish i2c probe into a parked state and emit
  nothing until userspace pokes a sysfs attribute — not a failure, a
  contract. On maple the Synaptics clearpad driver says so in dmesg
  (`post_probe_start sysfs is required to start post probe`) and stays
  silent (no IRQ, no events) until
  `/sys/devices/virtual/input/clearpad/post_probe_start` is written `1`.
  The symptom is indistinguishable from a broken relay: `getevent` on the
  framework-facing node *and* the real node shows nothing, the framework
  accepts the device (right classes, right ABS ranges — EventHub's one-shot
  scan only cares about those) and no error is logged anywhere.
- **Read the device's own sysfs attributes first.** Vendor one-offs
  (`post_probe_start`, `enabled`, `fwstate`) are the driver announcing the
  handshake; `ls /sys/devices/virtual/input/<name>/` is one command.
  Address them through the input-class symlink (`.../input/clearpad/...`),
  not `inputN/` — the number depends on probe order.
- **The recovery ramdisk documents the vendor's userspace contract.** The
  touch screen works in TWRP on the same kernel, so the recovery image
  must carry the missing piece: TWRP's `/init.recovery.maple.rc` has
  exactly `write /sys/devices/virtual/input/clearpad/post_probe_start 1`
  in `on post-fs` ("filesystem is available for fwflash"). Grep the
  recovery's `/init*.rc` before reverse-engineering anything.
- Trigger *availability* is release-dependent: Donut 1.6's init has no
  `post-fs`/`post-fs-data` triggers at all, and the driver's sysfs node
  can appear after init's `on boot` block (node at ~2.9 s kernel time on
  maple), so a one-shot `write` races it silently. Put the poke in a
  service that has to exist anyway and give it a bounded retry loop
  (maple: `vtouch`, after the virtual-node substitution so system_server's
  one-shot input scan still sees it, before the relay loop — no events can
  arrive before the write, so the wait costs nothing).
- Diagnosis order worth reusing: `getevent` both the real and the
  framework-facing node → `dmesg` for driver state messages → sysfs
  attribute listing → write the suspicious attribute at runtime and watch
  dmesg + `getevent` again (maple: 5800 raw MT protocol-A events and 1571
  translated events after the poke).

## Firmware loads that wait for a missing userspace helper

- On a kernel built with `CONFIG_FW_LOADER_USER_HELPER` (or the Android
  fallback path), `request_firmware()` first tries a direct filesystem
  lookup against `firmware_class.path` / `/lib/firmware`, and if that fails
  it emits a uevent and waits for a userspace loader to write the file into
  `/sys/class/firmware/<name>/...`. Donut 1.6 has no such loader (no
  ueventd firmware handling), so the wait always runs to its full timeout
  (60 s default) and returns `-EAGAIN` (rc=-11). A driver that retries N
  times therefore stalls for `N * 60 s` before giving up.
- Symptom on maple (clearpad): the post-probe "ensure firmware" step calls
  `request_firmware(touch_module_id_0xd1.img)` 3 times; touch only comes
  alive ~184 s after boot (`end post probe @ 183.759 (rc=0)`). The lock
  screen is up long before that, so it looks like "touch started working
  after the lock screen was shown" - it did not; it is a timer.
- Knob: the wait length is the class attribute `/sys/class/firmware/timeout`
  (default 60, seconds) - note it is a *class* attribute, not a module
  parameter (`/sys/module/firmware_class/parameters/` only has `path` on
  this kernel). Writing `1` makes each attempt fail in ~1 s. Set it before
  the driver's first request (maple: `vtouch` writes it first thing in
  `start_post_probe()`, before writing `post_probe_start`).
- If the firmware file itself is available (TWRP's recovery ramdisk or a
  stock `/system/etc/firmware` copy), the direct lookup succeeds and the
  fallback never runs - but providing a wrong file risks flashing the
  panel; the fail-fast path is the safe default. Do not generalize the
  TWRP-vs-Donut timing difference from one device without checking which
  kernel each runs (maple: TWRP 4.4.263 vs Donut 4.4.302).

## Key character maps shipped unreadable (mode 0640)

- Donut's host tool `build/tools/kcm/kcm` creates the `.kcm.bin` files with
  `open(..., O_RDWR|O_CREAT|O_TRUNC, 0660)`. The build's umask (022) leaves
  them 0640 (`root:root`, no other-read), so app processes (uid `app_*`)
  cannot open them. `KeyCharacterMap::load()` returns NULL and the native
  JNI path dereferences it -> SIGSEGV (fault addr 4) in `libui.so` in any
  app that loads a key character map (Settings, Launcher on Donut, where
  Launcher runs in `android.process.acore`).
- Fix: `fchmod(out, 0644)` in `kcm.cpp` (creat mode alone is masked by
  umask), recorded as `patches/donut/build-kcm-file-mode.patch` (applied
  from `aosp-donut/build`); the maple packaging script also normalizes
  `*.kcm.bin` to 0644 as belt-and-suspenders. The `.kl` and `.idc` files
  are already 0644 and were never affected.
- Verification recipe: `W/KeyCharacterMap: Can't open keycharmap file ...`
  followed by `E/KeyCharacterMap: Can't find any keycharmaps (also tried
  .../qwerty.kcm.bin)` then `I/DEBUG: signal 11 ... fault addr 00000004`
  with `#00 pc ... /system/lib/libui.so`.

## Display orientation on hardware with a flip-cover lid sensor

- Donut's `PhoneWindowManager.rotationForOrientationLw` treats
  `mLidOpen` (SW_LID == 0) as a sliding-keyboard reveal and returns
  `ROTATION_90` (landscape) for the unspecified case. A phone with a
  flip-cover hall sensor (maple: bu520x1nvx, SW_LID) reports "open" with
  no cover attached, so the whole UI comes up in landscape on a portrait
  panel. The framebuffer is already portrait (1080x1920); only the policy
  is wrong.
- Fix: guard the lid-open branch with `!KEYBOARD_ALWAYS_HIDDEN` and set
  `KEYBOARD_ALWAYS_HIDDEN = true` for devices with no sliding keyboard
  (maple). Recorded as
  `patches/donut/frameworks-policies-orientation.patch`. Auto-rotate still
  needs a working sensor HAL (`sensors.maple.so`), which this port does not
  have yet, so the orientation stays fixed at portrait.

## Keyguards on hardware without a MENU key
- Android 1.6's non-secure keyguard unlocks *only* on `KEYCODE_MENU`
  (`LockScreen.onKeyDown` → `mCallback.goToUnlockScreen()` →
  `keyguardDone(true)`); the platform assumed G1-class hardware with a MENU
  key in the nav row. Modern phones have none — and Donut predates action
  bars, so app menus need MENU too.
- Two independent fixes, both small: (1) remap a spare hardware key to
  MENU in the device key layout (maple: the two-stage camera shutter, full
  press 766 → `MENU WAKE_DROPPED`; leave the half press alone — a full
  press also emits the half-press code, so mapping both to MENU fires
  twice per press and toggles a just-opened menu shut). The half press
  (528) was later repurposed to `BACK` for in-app navigation (no wake
  flag) once tap-unlock made MENU-on-the-shutter unnecessary for
  unlocking; the camera's autofocus on half-press is given up for this; (2) unlock on
  touch — override `LockScreen.onTouchEvent` and call the very same
  `goToUnlockScreen()` on ACTION_DOWN, so a secure keyguard still routes
  through its own `UnlockScreen`, and update the
  `lockscreen_instructions_when_pattern_*` strings ("Press Menu to
  unlock." → "Tap to unlock."). The keyguard window is touchable
  (`KeyguardViewManager`: TYPE_KEYGUARD with FLAG_FORCE_NOT_FULLSCREEN /
  optional FLAG_ALT_FOCUSABLE_IM — no FLAG_NOT_TOUCHABLE) and only the
  emergency-call button consumes touches, so a tap outside it lands on the
  LockScreen view.
- **The SIM-missing lock screen is a dead end on radio-less ports.**
  `LockPatternKeyguardView.stuckOnLockScreenBecauseSimMissing()`
  (`mRequiresSim && !isDeviceProvisioned() && simState == ABSENT`) and
  PUK_REQUIRED both make `goToUnlockScreen()` a no-op, so neither MENU nor
  a tap unlocks anything. `mRequiresSim` is false iff the property
  `keyguard.no_require_sim` is set — put it in the port's `system.prop`
  (maple also sets `DEVICE_PROVISIONED=1`).
- Layout-token gotchas: key names are validated against
  `frameworks/base/include/ui/KeycodeLabels.h` and an unknown token makes
  `KeyLayoutMap` discard the whole file — check names there before
  shipping; the code change lives in the policies project while the hint
  strings live in `frameworks/base`'s core resources, so record them as
  two patches.
- Verification recipes: find a key's scancodes with `getevent` (no device
  argument dumps every node — press the key, see which eventN lights up);
  on the framework side a successful unlock is `keyguardDone(true)` in
  logcat.
