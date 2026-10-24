# MIUI 4.1.2 (`mione_plus`) on Xiaomi Mi 6

Porting the Mi-One-Plus MIUI V5 OTA (Android **4.1.2**, SDK 16,
`ro.build.version.incremental=4.11.28`) onto `sagit`. Two images are
involved and **neither has been booted yet**: a repacked `system.img` and a
`boot.img` built from the KitKat port's 4.4.247 kernel + ramdisk. Hardware
deltas and the same-topic notes live in [quirks.md](quirks.md); the working
KitKat port this borrows from is [README.md](README.md).

## Source ROM

`miui_Mioneplus_4.11.28_ebcfee352a_4.1.zip` (165 MB) is an OTA, not a
factory image: it carries `system/` and `data/` only, **no boot.img and no
recovery**. 1426 files, 32-bit `armeabi-v7a`, `MSM8660_SURF`, MIUI V5
(framework.jar + MIUI's own `framework_ext.jar`, `framework-miui-res.apk`).

The zip stores FAT-style entries (`2.0 fat`, `-rw----`, no symlinks), so
the updater-script is the only source of permissions and of the 62
symlinks (`bin/sh -> mksh`, `bin/* -> toolbox`, `fonts/DroidSans* ->
Roboto*`, `lib/libwiperjni.so -> libwiperjni_v01.so`, ...). Extracting it
on Windows and packing from NTFS loses all of them; unpack on ext4 and
apply `config/mione-symlinks.txt`.

## system.img

Packed on WSL ext4 straight from the zip, 1 GiB ext4 (`mke2fs -d`,
`^has_journal`, label `system`), uid/gid/mode from the OTA's
`set_perm*` calls. The stock `C:\download\sagit\config\system_fs_options`
is a `mkfs.erofs` recipe and **cannot be used**: the 4.4.247 kernel has no
EROFS (`/proc/filesystems` on the device lists none).

The ROM's own MSM8660 blobs are useless here, so the image carries the
MSM8998 closure taken from the KitKat build
(`out/target/product/sagit/system/`): `vendor/firmware/{a530,a540,adsp,
cpe_9335,cpe_9340,modem,mba,wlanmdsp,tfa98xx}`, `vendor/firmware/wlan/
qca_cld/`, and `vendor/etc/acdbdata/Forte/`. The 18 MSM8660 files
(`etc/firmware/*`, `vendor/firmware/fw_bcm*`) are dropped. Reproducible
in one shot: `config/mione-pack.sh` (zip → ext4 image, no Windows copy
involved).

## boot.img

Built by `scripts/build-sagit-jb-boot.py`: the KitKat port's
`out/target/product/sagit/ramdisk.img` (4.4 `init`, `adbd`, `ueventd`,
`init.rc` — all already boot this phone) with two overlays from
`devices/sagit/jb/ramdisk/`:

| File | Why it must differ |
| --- | --- |
| `init.environ.rc` | the 4.4 build's `BOOTCLASSPATH` lists core-libart / framework2 / webviewchromium / okhttp / telephony-common / mms-common, none of which exist in this ROM. JB needs its own list, plus MIUI's `framework_ext.jar` |
| `init.sagit.rc` | drops `fbpresentd`/`pdserve`/`rtc-restore`/`bootlog`/`modem_hold` (binaries or firmware only the KitKat tree has), gives `sdcard` Jelly Bean's positional arguments, creates the 4.1 storage layout (`/storage/sdcard0`, `/sdcard`, `/mnt/sdcard`) |

Kernel, cmdline, base and page size are unchanged from
`devices/sagit/kitkat/BoardConfig.mk` (4.4.247 `Image.gz-dtb`, patched DTB).

Note that JB `vold` reads `/system/etc/vold.fstab` (the ROM ships
`dev_mount sdcard /storage/sdcard0 auto /devices/platform/msm_sdcc.3/...`),
not `/fstab.<hardware>` — so the ramdisk `fstab.sagit` is inert here.

## Open gaps (nothing below is verified on hardware)

- **Display / media / camera HALs are msm8660.** `libgralloc`,
  `libhwcomposer`, `libcamera`, `libaudio` in the ROM target MSM8660; the
  KitKat port's working closure is libhyb built against 4.4 HAL headers,
  and the 4.1 framework's `hw_get_module`/audio interfaces differ. Expect
  `surfaceflinger` to come up only after a 4.1-compatible display path
  exists; that is the first thing to attack after init.
- **No `/dev/log`.** The 4.4.247 kernel has no `CONFIG_LOGGER` and JB
  `liblog` has no `/dev/kmsg` fallback (the KitKat port patched that in).
  Debugging is `dmesg` + `/data/misc` snapshots only.
- **WiFi / BT / RIL** in the ROM are MSM8660 stacks (brcm, msm_sdcc);
  none of them drive msm8998's qca_cld3 / WCN.
- SELinux runs permissive with 4.4 policy loaded (unlabeled 4.1 files).

## Flashing

`system.img` and `boot.img` are plain ext4/boot images; both go in via
TWRP `dd` with md5 readback (same recipe as the KitKat port). Reformat
`userdata` and `cache` first — 4.1 will not accept a KitKat-era `/data`.
