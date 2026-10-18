# patches/donut

Changes this port makes to a pristine AOSP Donut (`android-1.6_r2`) checkout.
Step-by-step build: [`docs/advanced/build/donut.md`](../../docs/advanced/build/donut.md);
field lessons: [`docs/advanced/porting-notes/donut.md`](../../docs/advanced/porting-notes/donut.md).
Each `.patch` is a `git diff` against the named repo project, applied from
that project's root:

```sh
cd aosp-donut/system/core && git apply ../../patches/donut/system-core.patch
```

| Patch | Project | What |
| --- | --- | --- |
| `system-core.patch` | `system/core` | init: cooper STL volume-scan bringup (`do_insmod_inner`), maple bring-up progress log on `/cache` (`init.c`/`init.h`/`util.c` — `progress_init()` mknods the cache node itself before ueventd exists; `ashmem` mknod fallback in `property_service.c`); adb: split-pipe shell (no `socketpair()` on this bionic) and the `shell:`/`reboot:` service-argument lifetime fix (`strdup`) — Donut passed a stack array to the service thread, so `adb shell <cmd>` exec'd a bare `/system/bin/sh` that waited on stdin forever |
| `frameworks-base.patch` | `frameworks/base` | `preloaded-classes`: drop proprietary `com.google.common.*`/`masf.*` (zygote crash-loop fix); `DatabaseHelper`: seed `device_provisioned` (HOME key fix) |
| `bootable-recovery.patch` | `bootable/recovery` | updater: accept API 3, raw block `write_raw_image` fallback, `run_program` backport |
| `hardware-libhardware_legacy.patch` | `hardware/libhardware_legacy` | `wifi.c`: AR6003/ath6kl driver loading (`WIFI_DRIVER_*`) |
| `external-wpa_supplicant.patch` | `external/wpa_supplicant` | wext/cfg80211-compat tweaks for the ath6kl stack |
| `packages-providers-CalendarProvider.patch` | `packages/providers/CalendarProvider` | tests link the `vendor/googlelogin` stub |
| `mkbootimg-pagesize.patch` | `system/core` | mkbootimg: `--pagesize` + 8K padding buffer (2048 hardcodes corrupt 4096 output) |
| `bionic-binder-uapi-64bit.patch` | `bionic` | 64-bit binder UAPI (protocol 8): `binder_size_t`/`binder_uintptr_t` = 64-bit, 24-byte `flat_binder_object`, 48-byte `binder_write_read` (ioctl `0xc0306201`), `binder_handle_cookie` — maple's 4.4 kernel has no 32-bit protocol. Ported from the KitKat port's `patches/kitkat/bionic-binder-uapi-64bit.patch` |
| `frameworks-base-binder-64bit.patch` | `frameworks/base` | userspace half of the same ABI change: `Parcel` (`mObjects` 64-bit offsets, `writePointer`/`readPointer`), `IPCThreadState` (pointer payloads on `BR_ACQUIRE`/`BC_*_DONE`/`BC_FREE_BUFFER`/death notifications, 64-byte `binder_transaction_data`), C `servicemanager` (`binder_txn`/`binder_object`/`binder_io` mirror the 64-bit ABI) |
| `system-core-adbd-ffs.patch` | `system/core` | adbd FunctionFS transport for the configfs-only 4.4 kernel (no `/dev/android_adb`): backported from KitKat's `usb_linux_client.c`; `adb.c` picks USB when `/dev/usb-ffs/adb` exists. Blob headers use **this kernel's ancient magic values** (descriptors `1`, strings `2`), not the modern `0x502`/`0x503` |
| `system-core-devices-major-minor.patch` | `system/core` | ueventd: drop the 8:8 major/minor limit and build the glibc 32-bit `dev_t` encoding — without it every modern block node (`sde46` system, `sda15` data, major 259) is silently skipped and all mounts fail; plus maple bring-up `progress()` lines for node creation and the coldboot steps |
| `frameworks-policies-keyguard-touch-unlock.patch` | `frameworks/policies/base` | `LockScreen`: unlock on a tap (`onTouchEvent` → `mCallback.goToUnlockScreen()`, the same callback the MENU key uses) — maple has no MENU key and Donut's non-secure keyguard has no other unlock input; a secure keyguard still routes through its own `UnlockScreen` |
| `frameworks-policies-orientation.patch` | `frameworks/policies/base` | `PhoneWindowManager`: the flip-cover hall sensor (bu520x1nvx) reports SW_LID, and Donut treated "lid open" as a sliding-keyboard reveal -> forced landscape (rotation 90) at boot. Guarded the lid-open->landscape branch with `!KEYBOARD_ALWAYS_HIDDEN` and set `KEYBOARD_ALWAYS_HIDDEN = true` (maple has no sliding keyboard), so the default orientation is portrait (1080x1920). Fixes "stuck in landscape" on this hardware |
| `frameworks-base-keyguard-touch-unlock.patch` | `frameworks/base` | `framework-res`: keyguard hint strings now read "Tap to unlock." / "Tap to unlock or place emergency call." (Donut's originals told the user to press a MENU key this phone does not have) |
| `build-kcm-file-mode.patch` | `build` | host tool `kcm`: creates the `.kcm.bin` files mode 0660 (`root:root`), so app processes (uid `app_*`) cannot read them; `KeyCharacterMap::load()` then returns NULL and libui SIGSEGVs (fault addr 4) in any app that loads a key character map (Settings, Launcher). Fixed to 0644. Maple also normalizes the mode in `build-maple-donut-image.py` as belt-and-suspenders |
| `files/vendor/googlelogin/` | new tree `vendor/googlelogin` | tiny stub for proprietary `com.google.android.googlelogin`, needed by `CalendarProvider/tests` (copy into `aosp-donut/vendor/`) |

Notes:
- `system-core-adbd-ffs.patch` hard-codes FunctionFS magic values `1`/`2`
  because whatawurst's msm8998 4.4 tree kept the ancient enum in
  `include/uapi/linux/usb/functionfs.h` (`DESCRIPTORS=1`, `STRINGS=2`,
  `V2=3`); writing `0x502` makes `__ffs_data_got_descs` return `-EINVAL`
  before it parses anything (no `ffs_do_descs` log — the only visible
  symptom is adbd looping `write descriptors errno=22`). Re-check the
  target kernel's uapi header before moving this file to another tree —
  see maple [`quirks.md` TEST 42](../../docs/devices/maple/quirks.md).
- `files/` is a straight copy-overlay rooted at the AOSP root:
  `cp -a files/vendor aosp-donut/vendor/`.
- The working tree's `adb/usb_linux_client.c` carries TEMP bring-up
  diagnostics (`ffs_dbg()` appending to `/cache/adbd-ffs.log`) that
  `system-core-adbd-ffs.patch` deliberately does not contain. Remove them
  before regenerating that patch (`git diff > …`), and keep the patch's
  comment wording for the magic values.
- Patches track the exact tree that produced the boot-verified cooper zip
  (and, for the four maple patches, the boot-verified maple image); they are
  plain diffs (no commit metadata) so they survive re-syncs.
- The four maple patches (binder 64-bit ×2, adbd FFS, ueventd major/minor)
  are additive: cooper keeps the 32-bit paths it always had.
- The keyguard-unlock pair is one topic on two projects, maple-only:
  `frameworks-policies-keyguard-touch-unlock.patch` (apply from
  `aosp-donut/frameworks/policies/base`) unlocks the non-secure keyguard on
  a tap, and `frameworks-base-keyguard-touch-unlock.patch` (apply from
  `aosp-donut/frameworks/base`) retitles the hint strings. Background:
  maple [`quirks.md` TEST 44](../../docs/devices/maple/quirks.md).
