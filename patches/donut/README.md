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
| `system-core.patch` | `system/core` | init: cooper STL volume-scan bringup (`do_insmod_inner`); adb: split-pipe shell (no `socketpair()` on this bionic) |
| `frameworks-base.patch` | `frameworks/base` | `preloaded-classes`: drop proprietary `com.google.common.*`/`masf.*` (zygote crash-loop fix); `DatabaseHelper`: seed `device_provisioned` (HOME key fix) |
| `bootable-recovery.patch` | `bootable/recovery` | updater: accept API 3, raw block `write_raw_image` fallback, `run_program` backport |
| `hardware-libhardware_legacy.patch` | `hardware/libhardware_legacy` | `wifi.c`: AR6003/ath6kl driver loading (`WIFI_DRIVER_*`) |
| `external-wpa_supplicant.patch` | `external/wpa_supplicant` | wext/cfg80211-compat tweaks for the ath6kl stack |
| `packages-providers-CalendarProvider.patch` | `packages/providers/CalendarProvider` | tests link the `vendor/googlelogin` stub |
| `mkbootimg-pagesize.patch` | `system/core` | mkbootimg: `--pagesize` + 8K padding buffer (2048 hardcodes corrupt 4096 output) |
| `files/vendor/googlelogin/` | new tree `vendor/googlelogin` | tiny stub for proprietary `com.google.android.googlelogin`, needed by `CalendarProvider/tests` (copy into `aosp-donut/vendor/`) |

Notes:
- `files/` is a straight copy-overlay rooted at the AOSP root:
  `cp -a files/vendor aosp-donut/vendor/`.
- Patches track the exact tree that produced the boot-verified cooper zip;
  they are plain diffs (no commit metadata) so they survive re-syncs.
