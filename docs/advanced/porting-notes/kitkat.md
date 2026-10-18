# Porting notes: KitKat 4.4.4 on newer hardware

Playbook for bringing AOSP KitKat (`android-4.4.4_r2`, KTU84Q, API 19) up
on post-2014 hardware. Started for the Sony Xperia XZ Premium (`maple`,
2017, MSM8998); device quirks (incl. the TEST 1–49 bringup log) live in
the [maple quirks](../../devices/maple/quirks.md), the step-by-step build
in [build/kitkat.md](../build/kitkat.md). The [Donut playbook](donut.md)
is the method reference — this doc is the KitKat delta. Back to the
[advanced index](../README.md).
**Everything below the "Field lessons" header is boot-verified on maple
(Oct 2026) except the [libhyb section](#libhyb-lessons-maple-2026-10-18-built-on-device-verification-pending),
which was built 2026-10-18 and awaits its first flash; treat the sections
above it as the original checklist.**

## Field lessons (maple, verified on-device)

- **Sparse images bite twice.** `system.img` builds SPARSE by default;
  `dd` it raw and the partition is unmountable garbage (no error anywhere
  — init just reports missing `/system/bin/*`). Either `fastboot flash`
  (unsparsifies) or `simg2img` first, then verify ext4 magic (`53 ef` at
  byte 1080) on the flashed partition. Set `TARGET_USERIMAGES_USE_EXT4`
  or KitKat builds yaffs2 (unflashable on GPT devices).
- **A failing init command aborts its action SILENTLY** (`do_mount`
  returns -1 with no kmsg). Split mounts into independent `on init`
  stanzas so one bad mount can't kill the rest. Mount syntax is
  space-separated flags + ONE trailing options arg
  (`mount ext4 SRC /data nosuid nodev wait noauto_da_alloc,barrier=0`);
  comma-joined flags are silently ignored and `wait` never engages.
- **`wait_for_coldboot_done` does NOT exist in KitKat init** (it is an
  invalid command that aborts its action). Prefer raw `/dev/block/sdXN`
  nodes (from a TWRP by-name dump) over by-name symlinks to remove
  timing variables entirely — no wait needed. Note: on KitKat there is
  no `/dev/block/bootdevice/`; symlinks (if wanted) live under
  `/dev/block/platform/soc/by-name/`.
- **Duplicate imports are NOT harmless**: a second parse of the same file
  gets every service definition IGNORED ("ignored duplicate definition"
  in kmsg). Worse, if an import line itself goes missing, the file is
  copied to ramdisk but never parsed and every service in it is silently
  unknown (`start` becomes a no-op — this cost USB for a full cycle:
  `init.maple.usb.rc` without its import). Keep exactly one import path
  and verify services exist (`getprop init.svc.<name>`).
- **Mkdir before you mount**: mounting onto a missing dir fails ENOENT
  silently (no kmsg). Stock init.rc does not create `/cache` or
  `/persist` for you — create them in your own `on init` stanza first.
- **`write /dev/kmsg` from init is unobservable** on kernels without
  matching policy/context (no error). Diagnose with file outputs
  (`dmesg > /data/...`, per-pid `/proc` snapshots) pulled via TWRP, not
  kmsg markers. Related: the 4.4 kernel has no `CONFIG_LOGGER`, so there
  is no `/dev/log` and stock logcat is blind. Patch liblog to fall back
  to `/dev/kmsg` (`patches/kitkat/system-core-liblog-kmsg-*.patch`) and
  every `ALOG*` becomes a `<level>Tag[pid]: msg` line in `dmesg` — it
  also means the *newer* 32-bit libraries loaded by
  [libhyb](../../advanced/software/libhyb.md) log through the old
  liblog, so their messages land in `dmesg` too. Watch the errno fallout
  that once sent us chasing a phantom EBADF (journal-file helpers lie by
  omission).
- **First-boot services to disable** (cooper pattern — all TEMPORARY,
  re-enable with the real fix): `vold` if it parks in decrypt-wait on
  foreign userdata (re-enable: ours runs fine once userdata is ext4);
  `rild` (no RIL lib → crash-loop; stays off); `healthd` only if binder
  failures make it `critical` (re-enable: ours runs once binder + the
  `batterypropreg` path work — BatteryService aborts system_server
  without it).
- **FBE userdata must be reformatted** (Lineage file-based encryption is
  unreadable to KitKat; `mke2fs -t ext4` on the userdata partition from
  TWRP — destroys the modern install, get approval).
- **Binder on a 64-bit-only kernel is a userspace port**, not a config
  tweak: 64-bit-clean UAPI header (`binder_uintptr_t`/`binder_size_t`
  as `__u64`, protocol v8), `Parcel` 8-byte offsets + `hdr.type` +
  pointer casts, `IPCThreadState` int64 read/write + `sizeof(uint64_t)`
  offsets math, servicemanager local structs to 64-bit layout. Symptoms
  of getting it wrong: `ioctl 0xc0186201 → EINVAL` (number not
  recognised), `sme_*`/p2p/driver_cmd undefined refs at wpa_supplicant
  link (needs the qcwcn private lib), silent transaction misparse.
  "Junk in the top half of 64-bit unions breaks lookups" — assign every
  field, leave no padding uninitialised.
- **EGL needs a driver it isn't allowed to skip**: KitKat's loader skips
  `libGLES_android.so` ("always skip the software renderer") and aborts
  surfaceflinger in `eglGetDisplay` when no other driver exists. Know the
  limits of the fallback: **libagl is ES1-only** — any ES2 client (live
  wallpaper, hwui apps) finds no matching config and dies (`eglConfig not
  initialized`). Bring a real ES2 path and keep a non-GL fallback for when
  it's absent (the wallpaper canvas path when EGL init throws, plus the
  Atlas backport below).
- **On KitKat the EGL driver is chosen by readdir, not by `egl.cfg`**:
  `Loader::MatchFile::find()` scans `/vendor/lib/egl` then
  `/system/lib/egl` for `libEGL_*.so` and takes **the first hit**
  (verified in the server tree's `Loader.cpp`, ~lines 265–350); `egl.cfg`
  is inert at runtime on this release (it only documents the layout).
  That makes a *leftover* `libEGL_emulation.so` an unpredictable winner
  and the choice one-shot: SF latches it per process, so a bad pick means
  software GL until SF restarts. Make the probe deterministic (build
  exactly one hardware `libEGL_*.so`, delete stale emulator libs from the
  out tree) instead of trying to win a race. **Never set
  `ro.kernel.qemu`** — its presence short-circuits the same loader to
  `libGLES_android.so` and disables hwui.
- **A retired transport leaves workarounds behind**: the in-process
  [libhyb](../../advanced/software/libhyb.md) design (maple 2026-10-18)
  deleted the glrenderd socket, and with it the bounded-reply/reconnect/
  socket-buffer machinery, the readback presenter and the EGL loader
  patch — GL calls are plain function calls now. When you change a
  transport, sweep the workarounds it justified (they will still "work",
  which is how they survive): grep the patch set and init scripts for the
  old component's name before calling the migration done.
- **Two flippers wedge mdss**: exactly one thing may drive fb0's offset.
  The trap is "mixed mode": a second flipper competing with surfaceflinger
  after SF has already picked its renderer (the choice is cached per
  process) — both flip, `yoffset` goes out of sync and mdss logs
  `mdp ctl start failed` (EBUSY) with the panel frozen on the splash. Fix
  is ordering, not arbitration: the component that owns the fb HAL must be
  the only flipper. In the libhyb design `gralloc.maple` owns the fb HAL
  (SurfaceFlinger posts through it and never `FBIOPAN`s) and `fbpresentd`
  is started in `post-fs-data`, before SF's first post; the old
  glrenderd design instead had to start its daemon before SF's first EGL
  probe.
- **The glrenderd-era socket lessons (this bullet and the
  readback/socket cluster further down) are retired on maple but still
  live on poplar and documented here as the template for a
  socket-transport GL bridge**: size every "is the renderer there?"
  probe to real startup latency. glrenderd's cold-boot startup (adreno
  init + firmware load) is slow; a 5s window missed it, SF fell back to
  software and never reconsiders for the process lifetime. Wait
  generously (we used 300 × 100 ms). Corollary: keep the daemon's own
  startup fast — a "wait for the fb HAL to double-buffer, then do it
  ourselves" made bind take 10s instead of 0.2s; nobody opens the fb HAL
  in glrenderd mode, so just double `yres_virtual` immediately
  (`FBIOPUT_VSCREENINFO`) or the daemon samples `flips=0` and memcpys
  without ever panning.
- **Flip with the ioctl the working path uses, not the "obvious" one**: on
  maple `FBIOPAN_DISPLAY` returns success but `mdss_mdp_overlay_pan_display`
  logs `unable to allocate base pipe` and never moves `yoffset` — the panel
  stays black while fb0 and screenshots are correct. The stock gralloc's
  `fb_post` flips with `FBIOPUT_VSCREENINFO` + a new `yoffset`; mirroring that
  made `/sys/class/graphics/fb0/pan` toggle. Trust the offset under activity
  (and `dmesg`), not `mDisplayReady` or the `idle_power_collapse` sysfs string,
  which lied. A ported comment claiming `FBIOPUT` "redoes the whole mode" was
  from a drm target; on fbdev it is the correct, cheap flip.
- **Disable mdss idle power collapse for a static UI**: `qcom,mdss-idle-power-collapse-enabled`
  in the MDSS MDP DT node powers the MDP down when idle; a command-mode panel
  then goes dim/black (no scanout) and the next flip is stalled, so a static
  screen vanishes until touched. Strip the property from every appended DTB
  (`scripts/patch-maple-dtb.py`) and repack the kernel. With a prebuilt
  `Image.gz-dtb` the DTBs are appended uncompressed after the gzip stream
  (`zlib`'s `unused_data` finds the boundary); decompile/recompile each with
  `dtc`, or just drop the property line — a `dtc` round-trip that changes only
  that property is safe.
- **Gralloc stride must match the kernel's `line_length`**: mdss pads
  scanlines (1080 RGBA → 1088 px on maple) while generic gralloc aligns
  to 4 bytes — every row shears and the panel shows a parallelogram.
  Report `finfo.line_length` stride for `HW_FB` buffers
  (`hardware-libhardware-gralloc-fb-stride.patch`); verify by decoding
  a raw fb0 dump at both strides.
- **Toolbox lies on foreign kernels**: `sort` SEGVs, `ps` prints nothing,
  `head`/`tail` output vanishes, `dmesg | tail` SIGPIPEs (spamming
  debuggerd tombstones — write whole files instead). Shell builtins +
  `cat`/`grep`/`echo` are trustworthy. Validate every diag tool where
  it runs before believing its output. KitKat toolbox additionally
  lacks `restart` (use `stop` + `start`), `md5sum`, `tail`, `head`
  (TWRP's toolbox has them all); `cmp` exists. Never run bare `stop`
  (kills adbd too — `stop zygote` is the safe scoped restart).
- **Crash forensics without logcat** (no `CONFIG_LOGGER`, logcat blind):
  `debuggerd -b <pid>` for native stacks; `kill -3 <pid>` (SIGQUIT)
  writes full JAVA stacks to `/data/anr/traces.txt` (mkdir it,
  `chown system`, else the dump fails silently); Java crash traces
  accumulate in `/data/system/dropbox/` (watch it — it caps and
  rotates); `service call window <N>` with N counted from
  `IWindowManager.aidl` order reaches hidden WM calls.
- **Live surgery beats reflashing**: the boot partition is dd-able from
  a running system (`/dev/block/platform/soc/by-name/boot`); single
  `/system` files push over `adb remount` (APK **and** odex together —
  a stale odex silently wins); `adb reboot` to apply. Reserve 5 GB
  push+dd cycles for full system rebuilds.
- **Zombie scrim eats touch**: if systemui crash-loops, each death drops
  the keyguard window and PhoneWindowManager parks a fullscreen
  KeyguardScrim ("Removing keyguard window (Did it crash?)") that
  consumes all input while `mShowingLockscreen=false`. Diagnose via
  dropbox (crash stack), `dumpsys window` focus, dispatcher
  RecentQueue (proves events arrive). Fix the crash, not the input.
- **Autowinch safety net**: while remote and adb-less, a 15-min
  `sleep 900; setprop sys.powerctl reboot,recovery` service returns a
  wedged phone to TWRP unattended. Defuse (`stop`, then remove) the
  moment adb is trusted — it will otherwise kill good boots.
- **TWRP shell realities**: `/sdcard` may refuse creates while
  `/data/media` works; `adb shell` stdin pipes fail (`Bad address`);
  `adb push` of 5 GB images works fine; `by-name` symlinks resolve to
  stable `sdXN` nodes; `console-ramoops` survives reboot for kernel logs
  (corrupts after hours of uptime); `/proc/config.gz` answers kernel
  questions outright.
- **CPU variant**: `cortex-a15` is the right KitKat-era choice for Kryo
  (HW atomics). A kuser-helpers scare was disproven live — on-boot
  32-bit processes run; the TWRP-side SEGV was environmental (missing
  `/system/bin/linker` in recovery ramdisk breaks PIE resolution).
- **Dalvik heap buckets are mandatory, not tuning**: with no
  `phone-*-dalvik-heap.mk` inherit the build ships tiny defaults and
  system_server OOMs verifying the first big APK
  (`OutOfMemoryError` in `JarUtils.verifySignature`, same stack every
  life, dalvik-cache never populates). Size the bucket to RAM
  (4 GB → `phone-xhdpi-2048`: 192m/512m). Related: `WITH_DEXPREOPT`
  for eng builds, or first boot dexopts every jar/apk on-device.
- **`system.prop` APPENDS to build.prop** (`PRODUCT_COPY_FILES ...
  :system/build.prop` merges into the generated file), so
  `PRODUCT_PROPERTY_OVERRIDES` (heap, etc.) land fine alongside it.
- **Make lies about staleness**: product `.mk` edits do not retrigger
  `systemimage`/`bootimage` ("Nothing to be done" with changed inputs).
  Delete the outputs and rebuild, then VERIFY content (unpack the
  ramdisk, grep the jars), never timestamps.
- **mkbootimg needed NO backport** for base-0 + `Image.gz-dtb` (verified
  geometry: base 0+0x8000, pagesize 4096). The predicted patch wasn't
  needed — verify fears before building them.
- **An unbounded renderer reply is a permanent UI freeze** (maple TEST 32):
  a synchronous GL call is a socket round trip; if the renderer stops
  answering, the caller blocks forever — for systemui that is a frozen UI
  (`recvfrom` inside `SocketStream::readFully`). Bound every wait
  (`SO_RCVTIMEO`, prop-tunable), treat timeout/EOF/EPIPE as dead, reconnect
  IN PLACE (never free the connection — use-after-free), send with
  `MSG_NOSIGNAL`, and let a dead `writeFully` report success (a failing one
  NULLs buffers the prebuilt encoders memcpy into). Renderer side: cap the
  read buffer, bound one message per decode, drop the client.
- **Forked apps must not share the GL socket** (same batch): the TLS
  connection is inherited across zygote fork and several processes then
  interleave writes on one stream (framing lost for good). Re-own the
  connection on pid change in `HostConnection::get()` — as a file-static,
  never a new member (inline `alloc()`/`flush()` carry hard-coded member
  offsets into every already-built encoder lib).
- **"stuck on splash" splits by USB** (maple TEST 32): adb alive + stale
  panel = display-handoff wedge (system up); **no USB at all = pre-userspace
  hang** (bootlog never wrote — check `/data` mtimes to confirm). The no-USB
  form recovered via a TWRP round-trip (`adb reboot` from TWRP) — try that
  before reflashing; a forced power-off loses ramoops, so pstore will not
  explain the hang.
- **Sibling-device fix sets are references, not recipes**: two msm8998
  KitKat ports (maple/poplar) drew OPPOSITE conclusions on the fb0 flip
  ioctl (`FBIOPAN_DISPLAY` vs `FBIOPUT_VSCREENINFO` — each verified on its
  own device) and on who doubles `yres_virtual`. Keep on-device evidence
  over cross-device advice; re-verify every quirk on the target.
- **A borrowable fix set still needs reading, not copying** (maple TEST 33,
  port execution): three things in the poplar bundle only worked because of
  code it did not ship, or were outright incomplete:
  (a) its reconnect design requires *closing* the socket when a stream is
  marked dead — its shipped `SocketStream.cpp` marks dead and returns, so
  `valid()` stays true and `reconnectIfDead()` can never fire; close the fd
  (and invalidate it) or the "self-healing" stream never heals;
  (b) the renderer reads a **per-connection handshake word** before it
  decodes (`RenderServer::Main`: one `clientFlags` uint) — the first
  connection gets it from `HostConnection::get()`, so an in-place
  reconnect must replay it or the renderer eats the first GL command as
  clientFlags (or trips `IOSTREAM_CLIENT_EXIT_SERVER` and exits);
  (c) `RenderThread`'s drop path calls `peerPid()`/`shutdownStream()` that
  are not in the bundle — append them to the HOST `IOStream` AFTER every
  pre-existing virtual (guest vtable slots must not move), implement with
  `SO_PEERCRED` + `SHUT_RDWR` (both present in KitKat bionic and the NDK).
- **Bound the write side too**: a wedged renderer stops draining its
  socket; `send()` then blocks forever, so `SO_RCVTIMEO` alone does not
  save the compositor. Set `SO_SNDTIMEO`, `MSG_NOSIGNAL` on every send,
  and have the daemon `signal(SIGPIPE, SIG_IGN)` — a guest dying mid-reply
  must not be able to kill the renderer (its silent SIGPIPE death is the
  classic "randomly reverted to software GL" cause).
- **Raising `rmem_max`/`wmem_max` is not "setting the socket buffers"**
  (maple TEST 34): the per-socket default is `net.core.{r,w}mem_default`
  (208 KB on this kernel) regardless of the max — existing and new sockets
  keep it until someone actually `setsockopt(SO_SNDBUF/SO_RCVBUF)`. On an
  AF_UNIX stream the sender's `SO_SNDBUF` is what in-flight data is
  accounted against, so a guest pushing a frame's command stream (>200 KB)
  blocks in `send()` mid-frame and guest+renderer advance in lockstep
  (measured: nothing else saturated, GPU at min clock, CPU idle). Set
  ~8 MiB on both ends — sender at connect/reconnect, acceptor right after
  `accept()`; the buffers are demand-allocated so the limit costs nothing
  when idle. Sibling-port writeups claiming "SocketStream asks for 8 MB"
  can be aspirational: read the code, not the doc (the shipped poplar
  SocketStream asked for nothing).
- **`userActivityTimeout` on the keyguard window caps screen-off at 10 s**
  (maple TEST 33): AOSP `KeyguardViewManager` sets
  `AWAKE_INTERVAL_DEFAULT_MS` (10000 ms) on the keyguard window's
  `LayoutParams`, and WMS hands that to `PowerManagerService` as
  `mUserActivityTimeoutOverrideFromWindowManager` — the effective timeout
  becomes `min(setting, 10000)`. On a port that has just gained a working
  lights HAL (before it, the backlight never moved, so nobody noticed the
  display state), the panel now visibly blanks ~10 s after the last touch.
  That is stock behavior, not a hang: the power key wakes it. Check
  `dumpsys power | grep mUserActivityTimeoutOverride` before chasing it.
- **Sony msm8998 exposes TWO independent backlight nodes**: the mdss led
  (`/sys/class/leds/lcd-backlight`, AOSP convention) and the PMIC WLED
  (`/sys/class/leds/wled`, what TWRP drives). Writing one does NOT change
  the other's value (probe on live device, both max 4095). A lights HAL
  should write both with the same scaled value — whichever is wired to the
  panel takes effect, the other is inert. Verify brightness with
  `settings put system screen_brightness N` + reading both nodes while the
  screen is ON (`mScreenOn=true`; a sleeping screen reads 0 by design).
- **Renderer rebuilds are safe when the protocol files are untouched**
  (glrenderd era, kept for poplar/leftover references): the "never
  rebuild glrenderd alone" rule is about `*_enc`/`*_dec` codec
  tables generated from the `.in` files — if those are unchanged, the
  regenerated tables are identical and the deployed guest `*_enc.so`
  matches (maple TEST 33 rebuilt glrenderd from the same tree; panel came
  up correct). Keep the known-good binary as the backup either way.
- **The post readback is the per-frame cost floor on a framebuffer-backed
  renderer** (maple TEST 35; the design libhyb replaced — retired on
  maple, still the shape of poplar's glrenderd): with the guest
  composing on the host GPU and
  the panel fed from fb0, every presented frame pays a synchronous
  `glReadPixels` (~9–10 ms at 1080p) plus a copy into the presenter.
  Bounce the readback straight into the presenter's staging slot (poplar's
  A6: `FrameBuffer::setPostReadback` + a slot picker + an fb0-facing
  presenter) and the extra full-frame copy disappears — ~1–2 ms/frame, no
  added latency. Hiding the readback behind a PBO mapped a frame later
  measured **slower** on this Adreno driver (13 vs 31–35 fps — the map
  costs more than the sync readback it hides), so measure before assuming.
  And keep the readback OUT of the panel page itself (fb0 page races /
  vblank coupling — poplar tried it and reverted).
- **Wifi needs its init services defined, not just its BoardConfig**
  (maple TEST 36): KitKat wifi.c starts the supplicant with `ctl.start
  wpa_supplicant`, which is a silent no-op when no such service exists —
  and this tree defined none. The BoardConfig (`BOARD_WLAN_DEVICE :=
  qcwcn`, NL80211, `lib_driver_cmd_qcwcn`) only keeps the supplicant
  LINKABLE. Ship the hammerhead set (`wpa_supplicant`, `p2p_supplicant`,
  `dhcpcd_wlan0/p2p`, `iprenew_*`, with the `-O.../sockets` ctrl dir and
  `-I..._overlay.conf` files that must exist as early as the service
  does) plus the dirs wifi.c never creates (`/data/misc/wifi/sockets`,
  `/data/misc/dhcp`). `lib_driver_cmd_qcwcn` is a STATIC lib — its
  absence from `/system/lib` is correct. Diagnose splits: driver loaded
  (`/sys/module/wlan`, `/dev/wlan`) but no wlan0 and no
  `/sys/class/ieee80211` phy = firmware/QMI handshake never ran (on
  qca_cld3+icnss the WLFW side lives modem-side; a modem that never boots
  may gate wifi too — verify before promising it).
- **A silent sound card is a DT + daemon problem, not a HAL problem**
  (maple TEST 36): `--- no soundcards ---` with zero tavil/tasha/wcd/slim
  probe lines and empty `/sys/kernel/debug/asoc/*` means the machine
  driver is deferring before it ever logs (the WSA/wcn-btfm waits are
  `dev_dbg`, invisible without dynamic debug). Write the HAL anyway
  (silent-safe while the card is absent so mediaserver stays up), but fix
  the kernel side first, CHARQUIRKS order: strip the BT slimbus dai from
  the sound node (`qcom,wcn-btfm` on msm8998 = pdx203's `qcom,wcn-bt =
  <0>`; `scripts/patch-maple-dtb.py` does mdss + this together,
  idempotent, byte-verified per DTB), set `service_locator.enable=0`
  (with =1 the card waited ~5 min on pdr), put adsp firmware where
  `firmware_class.path` points (a miss stalls ~60 s per file in ueventd's
  user helper — that delay is the tell), and pin the dsp/slpi restart
  levels to RELATED (SYSTEM panics the phone on any dsp crash). If the
  card still doesn't probe, the WSA881x amps (`qcom,wsa-devs`) are the
  next deferral suspect — check for any wsa/swr probe line before
  stripping them (speakers need them).
- **Never dd the RUNNING system partition and keep using the shell**
  (maple TEST 37b): writing the raw image underneath the mounted `/system`
  corrupts the live libc (`CANNOT LINK EXECUTABLE: could not load library
  "libc.so" ... empty/missing DT_HASH`) — every subsequent command dies.
  Push the image, verify md5 on-partition, then reboot IMMEDIATELY without
  running anything else. (Verified: `4b8539ac…` 5 GB, `dd` 38 s, md5
  on-partition, `adb reboot` straight after.)
- **Check QRTR in kallsyms before planning any QMI userspace** (maple
  TEST 37): `grep -c qrtr /proc/kallsyms` with positive controls
  (`binder`, `msm_ipc_router` must hit) — 0 means no modern QMI daemon
  (cnss-daemon, pd-mapper, rmt_storage, tftp_server) can ever run,
  chroot or not. Legacy IPC_ROUTER (`msm_ipc_router` thread,
  `sysmon-qmi` reaching ADSP SSCTL) and SMD FastRPC
  (`/dev/adsprpc-smd`) do work — constrain daemon designs to those.
- **Read the running kernel, not the defconfig** (maple TEST 36):
  `/proc/config.gz` is ground truth (`4.4.302-whatawurst+` has
  `CONFIG_QCA_CLD_WLAN=y` built-in and the full tavil/WSA/QDSP6 set,
  while the lineage-17.1 defconfig still says `=m`). Board comments
  corrected from BCM43xx to WCN3990/cherokee the same way (BT tlv names
  on the bluetooth partition agree).
- **A new audio HAL must fill the whole ops table, not just open/write**
  (maple TEST 36): mediaserver SIGSEGVs at `AudioFlinger.cpp:1349`
  calling `dev->init_check` when a HAL leaves it NULL (tombstone:
  `AudioPolicyCompatClient::loadHwModule` → libaudioflinger
  `loadHwModule_l` → pc 0). Assign every pointer the framework can
  reach — device close/init_check/volumes/mutes/mode/mic/params/
  input_buffer_size/streams/dump, both stream commons
  (rate/buffer/channels/format/standby/dump/params/effects), out
  (latency/volume/write/render_position/write_timestamp) and in
  (gain/read/frames_lost). Unused knobs return `-ENOSYS` or neutral
  values (mirror `hardware/libhardware/modules/audio/audio_hw.c`, the
  generic stub). Diagnose via `/data/tombstones` + `addr2line` against
  `out/.../symbols/...` (cooper 32-vs-40-byte lesson, same tools).

- **`PRODUCT_COPY_FILES` cannot ship empty directories** (maple TEST 41,
  2026-10-17): staging a tree with `find . -type f` (the `stage.py` idiom)
  silently drops every empty dir (`dev`, `proc`, `sys`, `data/...`). Any
  design that `mkdir`s mountpoints inside a read-only partition at runtime
  (chroot + bind-mount layouts) then fails `EROFS`, its binds fail
  `ENOENT`, the daemon restart-loops and a one-shot hw-vs-software GL probe
  latches software for the life of the process (single-digit fps until SF
  restarts). Fix: build mountpoints on a writable tmpfs at runtime —
  `hwchroot` assembles glrenderd's chroot under `/dev/.glr` and binds the
  real entries onto it; nothing inside `<root>` is ever written.
- **Staged binaries silently revert to whatever the staging dir holds**
  (maple TEST 41; still the rule for `maple-hyb/` + `maple-bin/`, see
  [libhyb](../../advanced/software/libhyb.md)): a rebuilt daemon was
  live-pushed to the device but the
  staging dir was never refreshed, so the next system image shipped the old
  binary beside the new guest libs — a cross-build protocol mismatch that
  crashed the daemon ~3x in 5 min (FORTIFY destroyed-mutex, SIGSEGV in
  `glBindTexture`/`TexImage`). Refresh staging in the same change as the
  rebuild, verify deployed identity by hash, and have the new build print a
  marker (a stats/log field) so "which binary is running" is one
  `dmesg | grep`.
- **KitKat init service-restart facts** (maple TEST 41, straight from
  `system/core/init/init.c`): a crashed service only restarts after
  `time_started + 5` (`restart_service_if_needed`, ~line 418), so a
  crash-loop has a ≥5 s cycle; `setprop ctl.restart <svc>`
  (`service_restart`, ~line 400) means stop+start when running, plain start
  when not, no-op while restarting. Restarts are invisible to property
  triggers (4.4 init has no state notifications), so a component that must
  react to another service's restart needs an in-band marker file.
- **Init triggers do not wait for the services they start** (maple
  TEST 41): `start fbpresentd` (or any service) in post-fs-data returns
  once the process is forked, and init proceeds to the next trigger
  (`on boot` →
  `class_start core`) immediately — surfaceflinger's process appeared
  milliseconds behind the started service's at maple's boot (caught by a
  probe that wrongly assumed "SF running" meant "the daemon restarted").
  Never infer service states from process presence during early triggers.
- **Cross-era property writes exec the host's `setprop`** (maple TEST 41):
  a helper built static with a modern NDK still runs on the old release,
  but its property client expects the post-8.x `/dev/__properties__`
  layout, not KitKat's single-file area — hwchroot `fork`+`exec`s the
  host's `/system/bin/setprop` for `ctl.restart` instead (verified on
  device). Same trick applies to any modern-side helper that must poke an
  ancient release's init.

## libhyb lessons (maple, 2026-10-18; built, on-device verification pending)

The in-process replacement for glrenderd — Adreno 540 driver + QTI
gralloc1 from a Lineage 22.1 closure loaded into KitKat processes. Full
write-up: [libhyb](../software/libhyb.md); adoption notes (which Taimen
patches were taken, skipped or queued):
[taimen-kitkat-mapping.md](../taimen-kitkat-mapping.md); device log:
[maple quirks](../../devices/maple/quirks.md) TEST 49. The reusable
lessons:

- **A newer 32-bit vendor driver can be called in-process on an old
  Android.** The C-level EGL/GLES ABI is stable across the gap; what
  needs bridging is (a) bionic symbols the newer libraries import and
  (b) buffer formats. Ship a loader + a hooks table and the driver's real
  entry points are plain function calls — no socket, no readback, no
  chroot, and the "one flipper" problem collapses to "the gralloc HAL
  owns fb0".
- **Decide the old/new boundary name by name.** The loader resolves a
  fixed host list (`libc`, `libm`, `libdl`, `liblog`, `libcutils`,
  `libz`) to the old system's own libraries and stages everything else
  from the closure. That is what keeps one copy of the allocator,
  signal handling and logging in the process — and it means the *new*
  libraries' `ALOG*` output rides the old liblog, so an old-side logging
  fix (the `/dev/kmsg` fallback above) lights up the driver's logs too.
- **Discover the closure with `readelf -d` from the real roots, then add
  a second pass for runtime `dlopen`s** (`eglSubDriverAndroid`,
  `libq3dtools`, the gralloc module): the linker graph alone misses what
  a HAL loads by name. Keep staging deterministic (the tools do it) and
  print what was staged and what was rejected, or the next person
  re-does the walk by hand.
- **Re-derive kernel-facing constants from the target's own kernel
  headers**, never from a sibling port's copy: the atomic-commit pipe
  encoding (`pipe_ndx`) and the `msm_mdp*.h` struct layouts came from
  this kernel's `include/uapi/linux/` and were then vendored into the
  device tree so the userspace build and the kernel can't drift.
- **A 64-bit-only kernel path needs an out-of-tree binary** — KitKat's
  build system makes 32-bit binaries only, and the 32-bit compat path of
  at least one MDSS ioctl is fatal (Taimen's port; verify-me on maple).
  Build the presenter with the NDK (`-static`, aarch64) and stage it
  under `vendor/sony/<device>-bin/` **outside the device-tree rsync
  path**, so `sync-device-tree.sh --delete` cannot eat it.
- **`LOGI` is not a KitKat macro.** Device-tree code fails at *link*
  with `undefined reference to 'LOGI'` because the unexpanded name became
  an implicit function call; use `ALOGI`/`ALOGE`
  (`cutils/log.h` → `log/log.h`), as `hardware/` does.
- **Removing files from `PRODUCT_*` does not remove them from the out
  tree.** `mkuserimg.sh` packs whatever `out/target/product/<device>/system`
  holds; a dropped `PRODUCT_COPY_FILES`/`PRODUCT_PACKAGES` entry leaves
  the old file (root-owned, from a container build) in the image. After
  retiring the glrenderd design the first libhyb image still shipped the
  whole 25 MB `/system/glr/` chroot, the emulation GL libraries and
  `/sbin/glrenderd.sh`. Delete the stale paths from the out tree (or the
  whole `system/` dir) and rebuild the images whenever a component is
  removed — and never trust "I deleted the patch" for what is in the
  image (maple TEST 49).

## Container gotchas (verified)

- Trusty image needs `lib32stdc++6 lib32z1` (KitKat's 32-bit prebuilt
  host tools die without them), a `python` → `python2.7` symlink, and
  the JDK-vendor-check patch (Oracle JDK 6 is uninstallable;
  `patches/kitkat/build-jdk-openjdk6.patch`).
- `repo sync` MUST be `repo init --depth=1` + `repo sync -c --no-tags`
  or it fetches all of AOSP history (100+ GB and still growing).
- `TARGET_PREBUILT_KERNEL := vendor/<vendor>/<device>/kernel` (kernel
  dumped from the running device via TWRP, kept out of git like all
  blobs) so plain `make bootimage` packs it with the built ramdisk —
  no kernel rebuild needed for userspace bringup.

## Source & toolchain (vs Donut)

- **Branch:** `repo init -u https://android.googlesource.com/platform/manifest
  -b android-4.4.4_r2` → `aosp-kitkat/` (11 GB+; gitignored). Confirm the
  exact `_r` rev against the build-numbers table before locking scripts.
- **64-bit host required** (Gingerbread+ rule; Donut's i386 chroot does NOT
  apply). Recipe to build: Ubuntu **14.04 (Trusty)** era container/chroot +
  **JDK 6** + Make 3.81 + Python 2.7. KitKat carries prebuilt OpenJDK bits
  in `prebuilts/jdk/` for some steps, but a separate JDK 6 install is still
  the documented path for 2.3–4.4. (Donut: Hardy + JDK 5; KitKat: Trusty +
  JDK 6.)
- **No ARM64 in KitKat.** `TARGET_ARCH := arm64` + `TARGET_2ND_ARCH` are a
  Lollipop-5.0 invention. MSM8998 bringup starts 32-bit (`armeabi-v7a`).
  See `docs/devices/maple/quirks.md` blocker 1 — spike this before anything else.
- Host GCC: Trusty-era GCC 4.6/4.8 is the safe baseline (same lesson as
  Donut's GCC-16-vs-`apriori` failure, one generation newer).

## KitKat build-system deltas (vs Donut gotchas)

- `lunch full_maple-eng` product discovery is the same shape
  (`AndroidProducts.mk` → `full_maple.mk`), but KitKat product files use the
  newer `inherit-product` + `PRODUCT_COPY_FILES` split — audit against
  `build/target/product/generic.mk` in the synced tree, not from memory.
- KitKat **has** `run_program`, ext4 `format()`, 3+-arg `mount`, and
  `write_raw_image` semantics Donut lacked — the cooper updater backports
  do NOT carry over. Re-derive the updater-script from KitKat's
  `bootable/recovery/updater` + TWRP 3.x expectations (API 19-era).
- `TARGET_PROVIDES_INIT_RC` pattern still applies; KitKat init language is
  richer than Donut's (more triggers/flags) — re-audit every cooper-era
  assumption (`import`, `wait`, `exec` semantics all changed since 1.6).
- `preloaded-classes` / provisioning / keymap lessons from cooper carry
  over in spirit but must be re-measured: KitKat's Zygote preload list,
  `Settings.Secure`, and `.kl` flag set all differ from Donut's.
- mkbootimg: KitKat predates `Image.gz-dtb`; expect a backport patch in
  `patches/kitkat/` (precedent: `patches/donut/mkbootimg-pagesize.patch`).
- SELinux is enforcing-era in KitKat (it was absent/permissive in Donut).
  First bringup likely needs permissive + policy audit against the 4.4
  kernel's security hooks.

## KitKat userspace on a 4.4 kernel (vs Donut on 2.6.35)

- Same problem class as cooper (new kernel, ancient userspace), one
  generation up: audit ueventd coldboot/node creation, cgroups, netd/vold
  against 4.4 uevents, firmware-loader paths, and `lowmemorykiller` tuning.
- vold/adbd/rild crash-loop triage from cooper applies: guard NULL
  uevent fields, split-pipe adbd diagnosis if pty misbehaves, comment out
  `ril-daemon` until an RIL lib exists for the SoC.

## Container gotchas (to fill in once built)

- (Empty — log Trusty-apt/JDK6/debootstrap issues here as they are hit,
  the way `docs/advanced/porting-notes/donut.md` logs Hardy ones.)
- **Measuring the panel pipeline on these ports (fb0 dumps, alpha, sleep)**
  (maple TEST 34/35, each trap cost real time): the fb is RGBA and it is
  normal for pixels to carry alpha 0 — an all-zero frame is *transparent
  black* and an RGBA PNG of it renders as **white** in most viewers; decode
  fb dumps with `format=rgb24` (ffmpeg) before concluding anything. A
  sleeping screen keeps the last BLANK frame (zeros) in fb0. And a "frozen
  white/black screen" with an idle-but-alive UI process is usually just the
  screen asleep: these KitKat ports cap the screen-off timeout with the
  keyguard window's `userActivityTimeout` (10 s), and **touch input does not
  wake the screen** — a test harness must check `mScreenOn` and wake via a
  power/menu key, or it will spend an hour "testing" a dark display. Also:
  `screencap` can return a constant image on a
  framebuffer-presented stack (use fb0
  dumps), and `input keyevent 26` is a toggle, not "turn on".

- **SERVREG_LOC (QMI 0x40) can be served from a 32-bit userspace over the
  legacy IPC Router socket API** (maple TEST 40, 2026-10-17): even though
  the stock 64-bit `pd-mapper` (QRTR client) cannot run on this kernel,
  the kernel's `service-locator` client (`get_service_location` →
  `qmi_svc_event_notifier_register(0x40, 1, 1)`) only needs *some* server
  bound on `AF_MSM_IPC` (family 27, `SOCK_DGRAM`) with the name
  `{service=0x40, instance=BUILD_INSTANCE_ID(1,1)=0x0101}`. A small static
  musl binary (`devices/maple/kitkat/pdserve/pdserve.c`) does this: it
  binds that name, receives the kernel's `GET_DOMAIN_LIST` request
  (msg 0x21) and answers with the domain list transcribed from the modem
  partition's `.jsn` files. The wire format is the standard QMI TLV
  encoding — see `drivers/soc/qcom/service-locator-private.h` for the exact
  `elem_info` arrays (`qmi_servreg_loc_get_domain_list_resp_msg_v01_ei`)
  and `lib/qmi_encdec.c` for the TLV/string encoding rules (top-level
  strings are bare bytes; nested strings are `[u8 len][bytes]`). This
  unblocks the PD-restart chain (icnss, audio_pdr, slimbus) and lets the
  modem's TMS query the AP's domain list. Caveat: the modem firmware on
  this port still crashes every ~41 s (err-fatal interrupt, no SMEM reason),
  so end-to-end Wi-Fi is not yet validated — the crash is independent of
  the locator and likely needs a keepalive (rild) that this port lacks.
