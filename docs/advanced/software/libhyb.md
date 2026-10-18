# libhyb — the newer Adreno 540 driver inside KitKat processes

`libhyb` runs **a newer GPU driver under an old Android, in-process**. On maple
(MSM8998, Adreno 540) KitKat has no EGL driver at all, so a 32-bit ELF loader
loads the LineageOS 22.1 (Android 15) vendor closure — Adreno 540 driver, QTI
gralloc1, dependencies — into every KitKat process, and a 64-bit `fbpresentd`
puts SurfaceFlinger's buffers on the panel through an MDSS atomic commit. It
replaces the glrenderd design (GL streamed over a socket into a Lineage
chroot), which is deleted: the driver now runs inside the process that calls it.

Ported from [DoomGuy41/Taimen-KitKat](https://github.com/DoomGuy41/Taimen-KitKat)
(Pixel 2 XL, same SoC family) and landed on maple first — the disposition of
every other Taimen patch (adopted/skipped/queued) is in the
[taimen-kitkat mapping](../taimen-kitkat-mapping.md). Sources:
[`devices/maple/kitkat/{hyb,egl,gralloc,fbpresentd}/`](../../../devices/maple/kitkat/);
staging tools: [`tools/libhyb/`](../../../tools/libhyb/); AOSP-side patches: the
[`patches/kitkat/`](../../../patches/kitkat/README.md) table. Field lessons:
[KitKat porting notes](../porting-notes/kitkat.md), [maple quirks](../../devices/maple/quirks.md).

**Status: built 2026-10-18, image pending flash** — no device was attached while
the port was written. Everything below marked `verify-me` comes from the Taimen
port or from cross-checks against the sources, not from maple hardware; the
on-device checklist is at the bottom.

## Why in-process

- KitKat predates the Adreno 540 by three years; no vendor driver for this
  release exists. `libagl` is software, ES1-only — any ES2 client (SystemUI live
  wallpaper, hwui apps) finds no config and dies (`eglConfig not initialized`).
- glrenderd sidestepped the ABI gap with a chroot + `@glrenderd` socket: every
  GL call was a round trip, the guest ran the emulator's GL stack (the emugl wire
  format carries 32-bit pointers), presentation needed `glReadPixels` (~9–10 ms
  per 1080p frame) and the UI capped at ~26–35 fps. It also had a rule —
  never two flippers — that constrained everything else.
- The C-level EGL/GLES ABI between KitKat and Android 15 is stable; what differs
  is **bionic** (a handful of newer libc symbols) and the **buffer/handle
  formats**. The loader and hooks cover both, so the driver's real entry points
  can be called directly. No transport, no readback, no chroot, no second
  flipper beyond gralloc's own fb HAL.

## Architecture

- **Loader** (`hyb/linker.c`): ARM 32-bit ELF loader for the newer-Android
  libraries. `HYB_ROOT` is `/system/a15` ([`hyb/hyb.h`](../../../devices/maple/kitkat/hyb/hyb.h);
  Taimen hardcoded `/system/a11` — one macro here). It handles PT_LOAD +
  PT_GNU_RELRO, REL/APS2 Android packed relocations, RELR, SysV + GNU hashes,
  symbol versions and DT_INIT/DT_INIT_ARRAY, recursive `DT_NEEDED`, and
  `PT_ARM_EXIDX` (C++ unwinding). Search order when resolving a plain
  library name: `vendor/lib/egl`, `vendor/lib/hw`, `vendor/lib`,
  `system/lib/hw`, `system/lib` (relative to `HYB_ROOT`); the newer side's
  absolute paths (e.g. `/vendor/lib/hw/gralloc.default.so`) are rebased
  under `HYB_ROOT` first and fall back to the bare name in those
  directories. Symbol resolution order: hooks → local group (the root of the
  dlopen, then dependencies breadth-first) → already-loaded libraries → KitKat.
  The local group matters: a global defined in several libraries with different
  sizes must bind to its own group's copy.
- **Host names**: `libc.so`, `libm.so`, `libdl.so`, `liblog.so`, `libcutils.so`,
  `libz.so` resolve to **KitKat's** versions (same 32-bit ABI); `ld-android.so`
  and `libdl_android.so` are virtual (resolved internally). Everything else comes
  from the hyb root.
- **Hooks** (`hyb/hooks.c`): functions the newer libraries import that KitKat's
  bionic lacks or implements differently — 68 symbols, derived from a
  `readelf`-walked closure of the driver + gralloc + dependencies (100 missing
  symbols found; the rest are provided by other means). Examples: `iswblank`,
  `strtold`, `aligned_alloc`, `fseeko64`/`ftello64`/`sendfile64`/`truncate64`,
  `iswblank_l`, `localeconv`, fortify wrappers, `__tls_get_addr`.
  `__tls_get_addr` is special: it aborts with a fatal message instead of
  half-working — 0 call sites in the whole closure (`llvm-objdump` over
  `libc++.so`, `libutils.so`, `libhidlbase.so`, `libgsl.so`, `libllvm-glnext.so`,
  `libgrallocutils.so`, `libqdMetaData.so`), so it should never run. If it ever
  does, that is a real bug to chase, not something to paper over.
- **EGL/GLES shims** (`egl/`): `libEGL_adreno.so` / `libGLESv1_CM_adreno.so` /
  `libGLESv2_adreno.so` keep their KitKat names but are thin shims (built from
  this tree) that load the real driver through libhyb and forward. GLES goes
  through a generated jump table (`gles*_stubs.S` + `*_names.c`, one entry per
  function in KitKat's `frameworks/native/opengl/libs/entries.in` present in the
  driver; regenerate with `egl/gen_stubs.py` — Android 11 vs 15 exports turned
  out identical). EGL intercepts the two places where the newer driver would
  touch KitKat structures directly:
  - **window surfaces**: an intermediate `ANativeWindow` translates the query/
    perform codes KitKat does not know (`NATIVE_WINDOW_*`) and gives the driver
    buffers in the newer format (`layerCount`, 64-bit usage) whose handle is the
    one imported into the QTI gralloc1;
  - **`eglCreateImageKHR` of a native buffer** (`EGL_NATIVE_BUFFER_ANDROID`):
    same intermediate buffer.
- **gralloc** (`gralloc/gralloc.c`, module `gralloc.maple`): KitKat's gralloc
  module 0.2 + allocator + framebuffer HAL implemented on top of the **QTI
  gralloc1** from the same closure (`gralloc.msm8998.so`, staged as
  `vendor/lib/hw/gralloc.default.so` so no stock `gralloc.default.so` naming
  collision exists; loaded by libhyb). Buffers are therefore real ION buffers
  with the QTI `private_handle_t` layout — exactly what the Adreno driver
  expects. Handles crossing processes are cloned (duplicated fds) before import:
  QTI gralloc1 destroys the handle it was given on last release, while KitKat's
  `GraphicBuffer` closes its own.
- **fbpresentd** (`fbpresentd/fbpresentd.c`): 64-bit, static, NDK-built
  (`tools/libhyb/build-native.sh`). KitKat's build system only makes 32-bit
  binaries and the kernel's **32-bit compat path for `MSMFB_ATOMIC_COMMIT`
  crashes the system**, so this piece is out of tree. Protocol: abstract
  `SOCK_SEQPACKET` socket `@fbpresent`, one `struct fbp_req` per frame, the
  buffer's descriptor sent once (SCM_RIGHTS) and addressed by id afterwards, an
  int32 ack per commit ([`fbpresentd/fbpresent.h`](../../../devices/maple/kitkat/fbpresentd/fbpresent.h)).
  It commits the buffer as the single layer of the main display's DMA0 pipe
  (`pipe_ndx = 64` = `BIT(6)`, the kernel's `MDSS_MDP_SSPP_DMA0` encoding;
  verified against `~/kernel-44`'s `msm_mdp_ext.h`), and does an empty commit
  when the client dies so the panel does not freeze on a dead layer.
- **Firmware**: the a540 microcode/GPMU/zap blobs are staged into
  `vendor/firmware` (same place as the modem/adsp blobs) and found through
  `firmware_class.path=/vendor/firmware` (see [quirks](../../devices/maple/quirks.md)).

## The EGL path (why there is no EGL patch any more)

- Stock KitKat logic selects the driver by scanning, not by config:
  `Loader::load_driver` takes the first `libEGL_*.so` in `/vendor/lib/egl`
  then `/system/lib/egl` (skipping only `libGLES_android.so`) — our shim
  is the only hardware one there. `BOARD_EGL_CFG` still ships the stock
  device layout in
  [`devices/maple/kitkat/egl.cfg`](../../../devices/maple/kitkat/egl.cfg)
  (`0 0 android` / `0 1 adreno`), inert at runtime on this release.
  `Loader::checkGlesEmulationStatus` is pristine — the deleted glrenderd
  EGL patch only ever restored that stock shape.
- **Never set `ro.kernel.qemu`** anywhere: its presence switches the loader to
  the emulation path and disables hwui. The emulation libs
  (`libEGL_emulation` etc.), `BUILD_EMULATOR_OPENGL`, the goldfish/sdk patches
  and the `GLConsumer` `glGetError` bound are all gone with glrenderd — the
  stock tree plus our shims is the whole story.
- `libGLES_android` (software ES1) stays in the build as the no-driver fallback.
- **hwui is built now** (`USE_OPENGL_RENDERER := true` in
  `BoardConfig.mk`, Taimen parity; `frameworks-base-hwui-gles2-cap.patch`
  keeps it on the ES2 behaviour KitKat is tested with). The glrenderd-era
  builds never set the flag, so `libhwui` was never built and the UI was
  software-drawn — accelerated UI is part of the point of the switch, and
  it is a behaviour change to watch on the first flash (`dumpsys gfxinfo`,
  `libhwui.so` in `/system/lib`).

## Presentation

- **Single flipper rule** (carried over): exactly one component may drive fb0's
  offset. `gralloc.maple` owns the framebuffer HAL, so SurfaceFlinger's posts go
  through it and SF itself never `FBIOPAN`s (`FramebufferSurface` consumes the
  HAL's `post`). `fbpresentd` performs the actual panel commit on gralloc's
  behalf.
- **Zero-copy primary path**: gralloc connects to `@fbpresent` at fb-open time
  and sends each buffer's descriptor once; presentation is an MDSS atomic commit
  of the DMA0 pipe. No copy, no pan, no readback. `verify-me` on maple: Taimen
  runs this path on the same MSM8998 generation.
- **Copy fallback**: if the socket is absent or the daemon refuses (init starts
  `fbpresentd` in `post-fs-data`; a boot without the binary falls back), gralloc
  copies into fb0 and flips with **`FBIOPUT_VSCREENINFO` + new `yoffset`**.
  maple's mdss ignores `FBIOPAN_DISPLAY` (it logs `unable to allocate base pipe`
  and returns success without moving the offset — TEST 30 on maple); this is the
  same ioctl stock gralloc's `fb_post` uses. `persist.gralloc.fb_commit=0` forces
  the copy path (useful for bisecting the presenter).
- **Idle power collapse** of the MDP must stay disabled in the DT
  (`qcom,mdss-idle-power-collapse-enabled`, stripped by
  [`scripts/patch-maple-dtb.py`](../../../scripts/patch-maple-dtb.py)) or a
  static UI goes black until touched — independent of the presenter, see
  TEST 30.
- **SurfaceFlinger config**: the fb HAL reports `numFramebuffers = 3`,
  `RGBA_8888`, `minSwapInterval = maxSwapInterval = 1`, fps 60; SF reads
  width/height/format from the HAL (it ignores stride/numFramebuffers for the
  framebuffer surface).

## Building and staging

Patch set: the graphics work needs **no** graphics patches to AOSP any more — the
tree is stock KitKat plus the `patches/kitkat/` entries that belong to the port
(binder 64-bit, and the newer-kernel fixes: liblog→kmsg, alarms/time, …). What
the stack needs is the **staged closure**:

```sh
tools/libhyb/extract-lineage.sh <lineage-22.1-maple.zip> <sysroot> [vendor.img|dir]
tools/libhyb/stage.py <sysroot> $AOSP_DIR/vendor/sony/maple-hyb
tools/libhyb/build-native.sh $AOSP_DIR            # NDK r21e; builds fbpresentd
make bootimage systemimage                        # HYB_DIR/BIN_DIR copied by full_maple.mk
```

- `extract-lineage.sh` unpacks a Lineage 22.1 maple build (system + vendor, and
  the runtime apex for bionic); `stage.py` walks `DT_NEEDED` from the Adreno
  driver + QTI gralloc1 and copies the closure plus the a540 firmware into the
  hyb root (`root/vendor/...`, `root/system/...`).
- `full_maple.mk` copies `vendor/sony/maple-hyb` into `/system/a15` and
  `vendor/sony/maple-bin` into `/system/bin` **when present**; both staging dirs
  live outside `vendor/sony/maple` because `sync-device-tree.sh` rsyncs with
  `--delete`. Rebuild the images after re-staging.
- **Staging refresh rule** (kept, adapted): an image build ships whatever the
  staging dirs hold; a `fbpresentd` or closure that was only live-pushed to a
  device is silently reverted by the next image build from un-refreshed staging.
  Refresh staging in the same change as the rebuild, and compare hashes:
  `md5sum vendor/sony/maple-bin/bin/fbpresentd .../maple-hyb/root/vendor/lib/egl/libEGL_adreno.so`.
- **Header authority**: the MDSS structs come from the **maple kernel's**
  `include/uapi/linux/msm_mdp_ext.h` (+ `msm_mdp.h`), vendored under
  [`gralloc/mdss/`](../../../devices/maple/kitkat/gralloc/mdss/) — the same
  headers the running kernel was built from. Taimen's copy differs only by the
  include style, a `rect_num` (reserved) field, and HDR defines.

## Status and on-device checklist

Built (with the rest of the KitKat image) 2026-10-18; **not yet flashed**
(no device attached). To verify on the first boot with a device:

1. `hybtest` — loads the closure, makes an ES3 context, draws and reads pixels
   back; prints the driver strings. Failure here means the loader/hooks/closure,
   not the framework.
2. `dmesg` (liblog now lands in `/dev/kmsg` on this kernel, see the porting
   notes): `gralloc.maple` fb0 line (`atomic commit 1`) and any
   `presentation by fbpresentd refused (...)` fallback message.
3. `dumpsys SurfaceFlinger` → `EGL implementation 1.4`,
   `GLES: Qualcomm, Adreno (TM) 540, OpenGL ES 3.2` and climbing frame counts.
4. UI is GL-drawn now (hwui was never built in the glrenderd era):
   `ls /system/lib/libhwui.so`, `dumpsys gfxinfo` shows real draw times,
   scrolling stays smooth.
5. `logcat`-free debugging works: `dmesg | grep -i hyb` for loader errors.
6. Panel sanity: static UI stays up (idle power collapse), no parallelogram/shear
   (stride), `fbpresentd` vs `persist.gralloc.fb_commit=0` A/B.
7. FPS and power under animation vs the glrenderd baseline (~26–35 fps).

## Porting it to another device

1. Confirm there is no native driver for the target release and keep a
   non-GL fallback for when the closure is absent.
2. Extract the modern vendor closure for the **same SoC** (a Lineage build for
   that device); the 32-bit/64-bit split matters — the loader loads 32-bit
   libraries, the presenter is 64-bit.
3. Re-derive the **symbol closure** (`readelf -d` walk, or Taimen's
   `scripts/closure.py`) and write hooks for what the target's bionic lacks.
   Do not copy another device's hook list blindly; bionic gaps depend on the
   closure.
4. Re-derive the **flipper rules** on the target — do not cargo-cult: maple
   ignores `FBIOPAN_DISPLAY` and flips with `FBIOPUT_VSCREENINFO` (TEST 30);
   sibling devices differ. Re-verify the atomic-commit pipe encoding against the
   target's kernel headers.
5. Re-check the gralloc headers (`msm_mdp*.h`) against the **kernel** the device
   runs, and the QTI `private_handle_t` against the staged gralloc for that SoC.
6. Re-time init: `fbpresentd` before SurfaceFlinger's first post
   (`post-fs-data`), and keep exactly one flipper.
7. Set `USE_OPENGL_RENDERER := true` in `BoardConfig.mk` (and carry the
   hwui ES2 cap if the driver reports ES3) — without the flag KitKat
   builds no `libhwui` at all and every UI frame is software-drawn.
