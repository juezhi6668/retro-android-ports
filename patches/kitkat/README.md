# patches/kitkat

Changes this port makes to a pristine AOSP KitKat (`android-4.4.4_r2`)
checkout. Step-by-step build:
[`docs/advanced/build/kitkat.md`](../../docs/advanced/build/kitkat.md);
field lessons:
[`docs/advanced/porting-notes/kitkat.md`](../../docs/advanced/porting-notes/kitkat.md).
Same layout as [`patches/donut/`](../donut/README.md): each
`.patch` is a `git diff` against the named repo project, applied from that
project's root:

```sh
cd aosp-kitkat/system/core && git apply ../../patches/kitkat/<name>.patch
```

The graphics stack itself needs **no AOSP patches**: the Adreno 540 driver
runs in-process via [libhyb](../../docs/advanced/software/libhyb.md),
whose loader/EGL/gralloc/fbpresentd sources live in the device tree
(`devices/maple/kitkat/`) with the staged closure under
`vendor/sony/maple-hyb` and the NDK-built `fbpresentd` under
`vendor/sony/maple-bin`.

| Patch | Project | What |
| --- | --- | --- |
| `build-jdk-openjdk6.patch` | `build` | `core/main.mk`: accept OpenJDK 6 (Oracle JDK 6 uninstallable in 2026); version check below still enforces 1.6.x |
| `bionic-binder-uapi-64bit.patch` | `bionic` | `libc/kernel/common/linux/binder.h` as the 64-bit binder abi (protocol 8), the only one the 4.4 kernel speaks |
| `frameworks-native-binder-64bit.patch` | `frameworks/native` | libbinder and servicemanager on that abi, every pointer and offset the kernel sees is 64 bits. replaces `binder-uapi-64bit.patch`, reset both projects before applying |
| `system-core-liblog-kmsg-fallback.patch` | `system/core` | `liblog/logd_write.c`: the 4.4 kernel has no logger driver (`/dev/log`), so every `ALOG*` is written to `/dev/kmsg` as a `<level>Tag[pid]: msg` line (`dmesg`). This is also where the newer 32-bit libraries loaded by libhyb log from. Taimen port. **The node has to be writable by the caller**: the kernel brings `/dev/kmsg` up `0600 root`, and liblog silently drops every line when its `open()` fails EACCES — the device tree's `init.rc` chmods it to `0622` (see the porting notes) |
| `system-core-liblog-kmsg-terminator.patch` | `system/core` | always terminate a kmsg record with `\n` — without it the kernel joins the next message onto the previous line. Apply **after** the fallback patch. Taimen port |
| `system-core-elapsedrealtime-boottime.patch` | `system/core` | `libutils` `elapsedRealtime()` from `CLOCK_BOOTTIME` (KitKat's `/dev/alarm` does not exist on this kernel) + `init/util.c` reads the full 4 KiB kernel cmdline (maple's was truncated: `slot_suffix`, `serialno` were lost). Taimen port |
| `system-core-fsmgr-ext4-project-quota.patch` | `system/core` | `fs_mgr` clears `RO_COMPAT_PROJECT` from the `/data` superblock before mounting — a newer ROM (Lineage on slot B) may have set the project-quota feature, which the 4.4 kernel refuses; skipped with `metadata_csum` (would need a checksum rewrite). Taimen port |
| `dalvik-zygote-capset-v3.patch` | `dalvik` | Zygote's `setCapabilities` uses `capset` ABI v3 (64-bit masks) so `system_server` keeps `CAP_WAKE_ALARM`/`CAP_BLOCK_SUSPEND` (bits 35/36) — the v1 ABI truncates them. Pairs with the time patch below. Taimen port |
| `frameworks-base-time-without-dev-alarm.patch` | `frameworks/base` | `AlarmManagerService` runs on `*_ALARM` timerfds and sets the clock with `settimeofday` instead of KitKat's `/dev/alarm`; also `SystemClock`, `ZygoteInit` cap list and the `rtc-restore` service hook. Taimen port |
| `frameworks-base-disable-atlas.patch` | `frameworks/base` | backport Lollipop's `config.disable_atlas`: the asset atlas assumes GPU rendering and kills `system_server` when it can't build one (the fallback rendering path). Taimen port |
| `frameworks-base-hwui-gles2-cap.patch` | `frameworks/base` | `libs/hwui/Extensions.cpp`: cap the reported GL version at ES 2.0 — KitKat's hwui is only tested against ES2 behaviour; the newer driver advertises 3.2. Taimen port |
| `frameworks-base-input-dispatch-freeze-thaw.patch` | `frameworks/base` | `services/input/InputDispatcher.cpp/.h`: lift a dispatch freeze that outlives 10 s (WMS freezes around window transitions; a transition that never finishes left the dispatcher asleep with the UI accepting no touch) |
| `frameworks-base-keyguard-scrim-no-input.patch` | `frameworks/base` | `policy/.../KeyguardServiceDelegate.java`: never show the crash scrim, and make it `FLAG_NOT_TOUCHABLE`/`FLAG_NOT_FOCUSABLE` — a dead keyguard must not eat every input while the last frame stays on screen (kept after the glrenderd era: still a cheap safety net) |
| `frameworks-base-systemui-wallpaper-canvas-fallback.patch` | `frameworks/base` | `packages/SystemUI/.../ImageWallpaper.java`: fall back to the canvas wallpaper instead of throwing when EGL has no ES2 config (software-only GLES; the crash looped systemui and stranded a zombie KeyguardScrim eating all touch) |
| `hardware-libhardware-gralloc-fb-stride.patch` | `hardware/libhardware` | `modules/gralloc/gralloc.cpp`: report the kernel's `line_length` stride for `HW_FB` buffers (mdss pads 1080 RGBA lines to 1088; generic align-4 reported 1080 and every row sheared into a parallelogram). Still used by the `gralloc.default` fallback; `gralloc.maple` reports the QTI stride itself |
| `hardware-libhardware-legacy-wifi-socket.patch` | `hardware/libhardware_legacy` | `wifi/wifi.c`: the supplicant control socket is `/dev/socket/wpa_<iface>`, so try that filesystem path first and only then KitKat's `/data/system/wpa_supplicant/` directory and the abstract `@android:wpa_<iface>` name — MIUI's supplicant binds nothing but the filesystem socket, and its WifiNative prefixes every command with `IFNAME=wlan0 `, which only the global interface parses. `wifi/Android.mk` also compiles a new `wifi/softap_qc_stub.c`: MIUI/QC's `libandroid_runtime` imports `wifi_connect_to_softapd()` from this library, so the prebuilt-ROM library swap in `miui5-chiron-pack.sh` only links with the stub present (it returns -1; softapd is not part of this tree) |
| `packages-apps-settings-kernel-version.patch` | `packages/apps/Settings` | `DeviceInfoSettings`: accept any compiler in the `/proc/version` regex — maple's kernel is clang-built, so About phone showed "Unknown" with the GCC-only pattern. Taimen port |
| _(more expected)_ | — | init/vold/adbd/HAL hardening once measured on maple (cf. `patches/donut/` precedent). Note: the predicted mkbootimg `Image.gz-dtb`/base-0 backport was NOT needed (verified geometry) |

Notes:

- The glrenderd-era patches (`device-generic-goldfish-glrenderd`,
  `frameworks-native-egl-glrenderd`,
  `frameworks-native-glconsumer-glgeterror-bound`, `sdk-glrenderd`,
  `system-core-glrenderd-fs-config`) and the `files/` overlay they needed
  (`RenderdStream.{cpp,h}`) were **deleted 2026-10-18** when the graphics
  stack moved to libhyb: stock KitKat's loader (`checkGlesEmulationStatus`)
  is already what the EGL patch restored, and the emulation GL libraries
  are no longer built at all. The removed patches live in git history and
  are still relevant to the poplar port, which keeps the old design.
- Patches track the exact tree that produces the boot-verified maple zip;
  they are plain diffs (no commit metadata) so they survive re-syncs.
- Do NOT commit blobs or full AOSP trees; `aosp-kitkat/` is gitignored.
