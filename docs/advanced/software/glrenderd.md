# glrenderd — hardware GL for Android releases with no GPU driver

`glrenderd` runs **a newer GPU driver under an old Android**. On maple
(MSM8998, Adreno 540) KitKat has no EGL driver at all, so the guest uses
the Android emulator's GL stack (goldfish `libEGL`/GLES) as transport: GL
calls stream over a `@glrenderd` abstract socket to a daemon running
*inside a chroot of a modern LineageOS system*, which draws on the real
Adreno and presents frames to `/dev/graphics/fb0`.

Ported from [chardidathing/pdx203-jellybean](https://github.com/chardidathing/pdx203-jellybean)
by @chardidathing. Sources live in
[`tools/glrenderd/`](../../../tools/glrenderd/); the AOSP-side patches are
the `*glrenderd*` entries in
[`patches/kitkat/`](../../../patches/kitkat/README.md).

## Why it exists

- KitKat predates the Adreno 540; no vendor driver for this release exists.
  `libagl` is software, ES1-only — any ES2 client (SystemUI live wallpaper,
  hwui apps) finds no config and dies (`eglConfig not initialized`).
- A modern driver cannot run directly in the old userspace (bionic ABI,
  SELinux, root layout differ). The chroot + socket design sidesteps that.

## Architecture

- **Guest side**: patched goldfish GL libs. `HostConnection` connects to
  `@glrenderd`; `gralloc.maple.so` hands window buffers over; EGL and
  gralloc probes fall back to `libagl`/`gralloc.default` when the socket
  does not answer.
- **Daemon**: 32-bit `glrenderd` (the emugl wire format carries 32-bit
  pointers) runs under a chroot of Lineage's `system`+`vendor` closure
  (`hwchroot`), loads `libEGL_adreno`/`libGLESv2_adreno`, renders, reads
  back and pans the panel.
- **Single flipper rule**: exactly one component may flip fb0. glrenderd
  is started in `post-fs-data`, **before** SurfaceFlinger's first GL probe,
  so SF takes the emulation path and posts via `rcFBPost` — it never
  `FBIOPAN`s. A late start leaves SF latched to software (cached per
  process) and two flippers wedge mdss with `mdp ctl start failed` (EBUSY).
- **Panel flip**: maple's mdss ignores `FBIOPAN_DISPLAY` (returns success,
  `unable to allocate base pipe`, no offset move). glrenderd flips with
  `FBIOPUT_VSCREENINFO` + `yoffset`, the same ioctl stock gralloc's
  `fb_post` uses; `open_panel` doubles `yres_virtual` itself (nobody opens
  the fb HAL in glrenderd mode).
- **Idle power collapse** of the MDP must be disabled in the DT
  (`qcom,mdss-idle-power-collapse-enabled`), or a static UI goes black
  until touched — [scripts/patch-maple-dtb.py](../../../scripts/patch-maple-dtb.py).

## Building and staging

Prerequisites: apply the four `*glrenderd*` patches to the AOSP tree, an
NDK (r21e on our build server), plus a Lineage 20 maple build (zip or
system/vendor images) for the chroot. Then:

```sh
tools/glrenderd/build.sh $AOSP_DIR <android-ndk-r21e>
tools/glrenderd/extract-lineage.sh <lineage-20-maple.zip> <sysroot> [oem.img]
tools/glrenderd/stage.py <sysroot> tools/glrenderd/out $AOSP_DIR/vendor/sony/maple-glr
make bootimage systemimage
```

`full_maple.mk` copies `vendor/sony/maple-glr` into `/system/glr` when
present (the directory lives outside `vendor/sony/maple` because
`sync-device-tree.sh` rsyncs with `--delete`), so rebuild the images after
staging. The init service is `/system/bin/sh /sbin/glrenderd.sh` →
`hwchroot /system/glr/root /glrenderd 1080 1920 -fb 2/dev/kmsg`.

**Renderer rebuild rule**: `build.sh` regenerates the guest codec tables;
a rebuilt daemon deployed against stale `*_enc.so` comes up dim/garbled.
If the protocol `.in` files are untouched the tables are identical and a
rebuild is safe — keep the known-good binary as backup either way. See
[KitKat porting notes](../porting-notes/kitkat.md) for the full list.

## Reliability rules (learned the hard way)

The full symptom → root cause → fix writeups are in the
[KitKat field lessons](../porting-notes/kitkat.md); the short version:

- **Fork safety**: the TLS GL connection is inherited across zygote fork;
  re-own it on pid change in `HostConnection::get()` (file-static, never a
  new member — member offsets are burned into prebuilt encoder libs).
- **Bound every wait**: `SO_RCVTIMEO`/`SO_SNDTIMEO` (~20 s, property
  `persist.glr.replytimeout`), `MSG_NOSIGNAL` on send; timeout/EOF/EPIPE
  means the stream is dead — **close the fd** and reconnect **in place**
  (never free — use-after-free), replaying the per-connection handshake
  word the renderer reads before decoding.
- **Size the socket buffers**: `net.core.{r,w}mem_default` is ~208 KB on
  this kernel while one frame's command stream is >200 KB; both ends set
  ~8 MiB via `setsockopt` (guest at connect/reconnect, daemon after
  `accept`).
- **Renderer side**: cap `ReadBuffer` (32 MiB, drop the client), decode one
  declared message at a time, resync on framing loss, and never let a
  guest death kill the daemon (`signal(SIGPIPE, SIG_IGN)`).
- **Bound GL drain loops**: `GLConsumer`'s `glGetError()` clearing is a
  synchronous round trip per iteration — keep it bounded (8).
- **Readback lands in a presenter slot** (poplar A6), never in the fb0
  page itself. Async/PBO readback measured *slower* on this Adreno
  (`persist.glr.asyncreadback`, ships off).

## Status and performance (maple, verified)

- Cold boot comes up on the Adreno: `dumpsys SurfaceFlinger` →
  `EGL implementation 1.4`, `GLES: Qualcomm, Adreno (TM) 540, OpenGL ES 3.2`.
- Sustained UI animations ~**26–35 fps**; the per-frame floor is the
  synchronous `glReadPixels` (~9–10 ms at 1080p) plus buffer-release and
  compose waits, not compute (GPU at min clock, CPU idle).
- Reliability set (TEST 33–35): fork-safety re-own, bounded reply +
  in-place reconnect, 8 MiB buffers, desync-proof renderer, § above.

## Porting it to another device

1. Confirm no native driver exists for the target release; keep a
   non-GL fallback (libagl) for when the daemon is absent.
2. Extract the modern vendor GL closure for the *same SoC* into a chroot
   (Lineage build for that device); `extract-lineage.sh` handles the
   apex/bionic details for 13-era images.
3. Re-derive the flipper rules on the target — **do not cargo-cult**:
   maple flips with `FBIOPUT_VSCREENINFO` and doubles `yres_virtual`
   itself (TESTs 30–31); poplar uses `FBIOPAN_DISPLAY` and expects the
   buffer pre-doubled. Sibling-device conclusions conflict; re-verify.
4. Re-time the probe window for your cold boot (maple needs ~30 s) and
   start the daemon before the first SurfaceFlinger probe.
5. Re-run the socket-buffer sizing and framing instrumentation; these
   numbers are per-kernel and per-driver.
