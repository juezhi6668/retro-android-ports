# Porting notes: ancient Android on newer hardware

Playbook distilled from bringing AOSP Donut (`android-1.6_r2`, DRD35) up on a
Samsung Galaxy Ace GT-S5830 (`cooper`, 2011, MSM7227) with a modern Arch host.
Everything below applies to any attempt to build/run Donut on post-2009
hardware. Device-specific notes live in `docs/devices/<codename>.md`
(cooper: [`docs/devices/cooper.md`](devices/cooper.md)).
For the KitKat-era delta (4.4.4 on newer hardware, e.g. maple), see
[`docs/porting-notes-kitkat.md`](porting-notes-kitkat.md).

## Source & toolchain
- **Cupcake is gone from AOSP.** `platform/manifest` has no `cupcake` /
  `android-1.5` branch left, only `android-1.6_r1…` and later. Donut is the
  oldest thing you can still `repo init`.
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
  two `pipe()`s and teach the poll loop the split fds); see
  `patches/donut/system-core.patch`.
- Donut vold **crash-loops (SIGSEGV, NULL devpath)** on foreign uevents and
  spins retrying `mknod` on `EEXIST`, pinning a core and flooding logcat.
  Guard the NULL + treat EEXIST as success, or drop vold until SD support
  is revisited.
- `rild` without its proprietary RIL `.so` crash-loops and drags dependent
  services with it — comment the service out if you have no Donut RIL lib.
- `system/core/adb` host tooling builds fine in the chroot if you ever need
  a period-correct adb client.
