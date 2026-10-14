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
| _(more expected)_ | — | mkbootimg `Image.gz-dtb`/base-0 support, init/vold/adbd hardening once measured on maple (cf. `patches/donut/` precedent) |

Notes:

- `files/` (when created) is a straight copy-overlay rooted at the AOSP
  root: `cp -a files/* aosp-kitkat/`.
- Patches track the exact tree that produces the boot-verified maple zip;
  they are plain diffs (no commit metadata) so they survive re-syncs.
- Do NOT commit blobs or full AOSP trees; `aosp-kitkat/` is gitignored.
