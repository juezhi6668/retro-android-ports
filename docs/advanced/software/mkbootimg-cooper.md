# mkbootimg-cooper — pagesize-patched Donut mkbootimg

`mkbootimg-cooper` is a patched copy of AOSP Donut's `mkbootimg` that can
pack cooper's boot image correctly: it adds `--pagesize` and enlarges the
internal padding buffer from 2 KB to 8 KB.

Files:

- `tools/mkbootimg-cooper.c` — patched source (checked in)
- `tools/mkbootimg-cooper` — compiled host binary (checked in)
- `tools/mkbootimg-donut-orig` — pristine Donut compile, kept to prove the
  original source builds on a modern GCC
- `patches/donut/mkbootimg-pagesize.patch` — the same change as a diff
  against `system/core/mkbootimg/mkbootimg.c`

## Why it is needed

- Donut's `mkbootimg` **hardcodes pagesize 2048** and has no `--pagesize`
  option at all; cooper's boot layout needs **4096**
  (`BOARD_KERNEL_BASE 0x13600000`, pagesize 4096, boot partition 8 MB).
- It also pads with a fixed **2048-byte** buffer: mkbootimg zero-fills
  from the header to that boundary, so any larger pagesize silently
  produces a corrupt image. The patch uses an 8 KB buffer.

## Build

Compiled from the synced Donut tree by
`scripts/build-cooper-donut-skeleton.py` (which also builds the pristine
binary and asserts the patched one accepts `--pagesize`). Manual build:

```sh
gcc -O2 -Wall -I aosp-donut/system/core/include \
    -I aosp-donut/system/core/mkbootimg \
    -o tools/mkbootimg-cooper tools/mkbootimg-cooper.c \
    aosp-donut/system/core/libmincrypt/sha.c
```

## Usage

`scripts/build-cooper-donut-full.py` invokes it as:

```sh
tools/mkbootimg-cooper --kernel <kernel> --ramdisk <ramdisk> \
    --base 0x13600000 --pagesize 4096 --board cooper -o boot.img
```

Then the packager asserts the header: `ANDROID!` magic, pagesize 4096,
kernel load address `base + 0x8000`, board name `cooper`. Do the same when
packing by hand — and verify every flash by readback, because BML writes
can fail silently (see [cooper quirks](../../devices/cooper/quirks.md)).

## Not needed for KitKat

The predicted equivalent for maple (`Image.gz-dtb`, base 0) turned out
unnecessary: KitKat's mkbootimg handles both natively, verified from the
built boot.img geometry. The Donut-era workaround stays Donut-specific;
verify before building a feared fix ([KitKat notes](../porting-notes/kitkat.md)).
