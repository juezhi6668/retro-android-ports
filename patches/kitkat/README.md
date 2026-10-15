# patches/kitkat

Changes this port makes to a pristine AOSP KitKat (`android-4.4.4_r2`)
checkout. Same layout as [`patches/donut/`](../donut/README.md): each
`.patch` is a `git diff` against the named repo project, applied from that
project's root:

```sh
cd aosp-kitkat/system/core && git apply ../../patches/kitkat/<name>.patch
```

| Patch | Project | What |
| --- | --- | --- |
| `build-jdk-openjdk6.patch` | `build` | `core/main.mk`: accept OpenJDK 6 (Oracle JDK 6 uninstallable in 2026); version check below still enforces 1.6.x |
| `bionic-binder-uapi-64bit.patch` | `bionic` | `libc/kernel/common/linux/binder.h` as the 64-bit binder abi (protocol 8), the only one the 4.4 kernel speaks |
| `frameworks-native-binder-64bit.patch` | `frameworks/native` | libbinder and servicemanager on that abi, every pointer and offset the kernel sees is 64 bits. replaces `binder-uapi-64bit.patch`, reset both projects before applying |
| `device-generic-goldfish-glrenderd.patch` | `device/generic/goldfish` | the emulator gl libs dial glrenderd's abstract socket instead of a qemu pipe, gralloc builds as `gralloc.maple` and falls back to `gralloc.default` without glrenderd, gles1 encoder fix for image specification while `TEXTURE_EXTERNAL_OES` is enabled |
| `frameworks-native-egl-glrenderd.patch` | `frameworks/native` | egl loader picks the emulation libs when glrenderd answers, libagl otherwise. replaces `egl-use-software-renderer.patch` |
| `sdk-glrenderd.patch` | `sdk` | glrenderd's host renderer, alpha twins for opaque configs (adreno won't copy an opaque pbuffer into rgba) and headless posting |
| `system-core-glrenderd-fs-config.patch` | `system/core` | 0755 for what gets exec'd in glrenderd's chroot, make_ext4fs takes modes from fs_config |
| `hardware-libhardware-gralloc-fb-stride.patch` | `hardware/libhardware` | `modules/gralloc/gralloc.cpp`: report the kernel's `line_length` stride for `HW_FB` buffers (mdss pads 1080 RGBA lines to 1088; generic align-4 reported 1080 and every row sheared into a parallelogram) |
| `frameworks-base-systemui-wallpaper-canvas-fallback.patch` | `frameworks/base` | `packages/SystemUI/.../ImageWallpaper.java`: fall back to the canvas wallpaper instead of throwing when EGL has no ES2 config (software-only GLES without glrenderd; the crash looped systemui and stranded a zombie KeyguardScrim eating all touch) |
| _(more expected)_ | — | init/vold/adbd/HAL hardening once measured on maple (cf. `patches/donut/` precedent). Note: the predicted mkbootimg `Image.gz-dtb`/base-0 backport was NOT needed (verified geometry) |

Notes:

- `files/` (when created) is a straight copy-overlay rooted at the AOSP
  root: `cp -a files/* aosp-kitkat/`.
- Patches track the exact tree that produces the boot-verified maple zip;
  they are plain diffs (no commit metadata) so they survive re-syncs.
- Do NOT commit blobs or full AOSP trees; `aosp-kitkat/` is gitignored.
