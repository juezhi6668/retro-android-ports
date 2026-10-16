# Building Donut 1.6 for cooper

Step-by-step build of the boot-verified cooper Donut port: pristine AOSP
`android-1.6_r2` → period chroot → patched tree + device tree → flashable
CWM/TWRP zip. For why each step exists and every trap hit along the way,
read the [Donut porting notes](../porting-notes/donut.md) and the
[cooper quirks](../../devices/cooper/quirks.md).

## 1. Period chroot (the one hard prerequisite)

Donut needs **Java 5** (`build/core/main.mk` checks the version; modern
JDKs cannot target 1.5 bytecode) and an old host GCC (current GCC dies on
Donut's host tools). The supported route is a period chroot:

- Ubuntu 8.04 (Hardy) i386, via `debootstrap --arch=i386 hardy` from
  `old-releases.ubuntu.com`
- Sun `sun-java5-jdk` (1.5.0_15), GCC 4.2.3, Make 3.81, Python 2.5
- Our working mount point: `/srv/donut-hardy`; the AOSP tree is
  bind-mounted into it
- 32-bit loader libs on the host so Donut's `arm-eabi-4.2.1` toolchain
  binaries run

Chroot/apt/debootstrap pitfalls (missing `unzip`, `secure_path` vs
`bzip2`, apt pipelining, Sun license pre-accept, endpoint pinning) are all
logged in [porting-notes/donut.md § Chroot gotchas](../porting-notes/donut.md#chroot-gotchas-hardy-on-a-modern-host).

## 2. Get the source

```sh
repo init -u https://android.googlesource.com/platform/manifest -b android-1.6_r2
repo sync                                # -> aosp-donut/ (gitignored)
```

`AOSP_DIR` selects an alternate checkout for the scripts below.

## 3. Apply the port

```sh
# patches: one git diff per AOSP project, applied from that project's root
# (full table + what each does: patches/donut/README.md)
cd aosp-donut
( cd system/core                       && git apply ../../patches/donut/system-core.patch )
( cd system/core                       && git apply ../../patches/donut/mkbootimg-pagesize.patch )
( cd frameworks/base                   && git apply ../../patches/donut/frameworks-base.patch )
( cd bootable/recovery                 && git apply ../../patches/donut/bootable-recovery.patch )
( cd hardware/libhardware_legacy       && git apply ../../patches/donut/hardware-libhardware_legacy.patch )
( cd external/wpa_supplicant           && git apply ../../patches/donut/external-wpa_supplicant.patch )
( cd packages/providers/CalendarProvider && git apply ../../patches/donut/packages-providers-CalendarProvider.patch )
cd .. && cp -a patches/donut/files/vendor aosp-donut/vendor/   # googlelogin stub overlay

# device tree -> $AOSP_DIR/vendor/samsung/cooper
scripts/sync-device-tree.sh cooper donut
```

## 4. Build

Inside the chroot, with the tree mounted:

```sh
source build/envsetup.sh
lunch full_cooper-eng
make -j8
```

Gotchas worth pre-reading: `lunch` must not be piped (it runs in a
subshell and `make` silently builds `generic`), and Donut `make` does not
track `BoardConfig.mk` flag changes — after flag edits, delete the
affected `out/.../obj/..._intermediates` and rebuild
([full list](../porting-notes/donut.md#donut-build-system-surprises)).

## 5. Package

From the repo root (`python3`):

```sh
python3 scripts/build-cooper-donut-full.py       # full system zip
python3 scripts/build-cooper-donut-skeleton.py   # small preview zip
```

| Script | Output | Notes |
| --- | --- | --- |
| `build-cooper-donut-full.py` | `out/full_cooper-1.6.DRD35-UNOFFICIAL-FULL-<date>.zip` | Full system, BML-aware CWM/TWRP installer (API-3 + raw-block + `run_program` shims, 51 toolbox symlinks as real symlinks). Repacks `boot.img` with cooper geometry using `tools/mkbootimg-cooper`. |
| `build-cooper-donut-skeleton.py` | `out/full_cooper-1.6.DRD35-UNOFFICIAL-<date>.zip` | Small preview zip; also compiles `tools/mkbootimg-cooper` (the full packager expects it to exist). |

Both verification steps in the scripts assert on real content (APK count,
`build.prop`, `ANDROID!` header fields) — treat script failures as build
failures.

## 6. Flash and verify

- Flash the zip from CWM/TWRP. **BML boots must be erased and written via
  `/sbin/erase_image boot` + `/sbin/flash_image boot`** (raw writes to BML
  silently don't stick) — the installer does this, and every flash should be
  verified by trimmed readback (`dd if=/dev/block/bml8 ...` vs the image;
  a full-partition md5 can never match).
- First boot takes **10–20 minutes** (dexopt of 32 APKs); watch
  `/data/dalvik-cache` grow rather than assuming a hang.
- Practical: keep the battery charged (USB can't keep up under load), and
  never flash boot below ~50%.

Recovery interaction notes, symlink/permission traps for manual flashing,
and the full on-device verification history live in
[cooper quirks](../../devices/cooper/quirks.md).
