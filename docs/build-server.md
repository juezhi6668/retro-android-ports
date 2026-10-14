# Build server runbook (maple/KitKat)

Machine: `androd-buildtest`, Ubuntu 26.04, 6 cores / 15 GB RAM / ~390 GB
disk. Reachable over Tailscale as `BUILD_USER@BUILD_HOST` (see your own
notes for the current address — it has moved before; `tailscale status`
locally shows peer state, "offline, last seen ..." means wait, not debug).

## Layout on the server

- `~/aosp-kitkat/` — KitKat tree (`repo init --depth=1 -u
  https://android.googlesource.com/platform/manifest -b android-4.4.4_r2`,
  then `repo sync -c --no-tags -j6`). Shallow + current-only ≈ 14 GB.
  Full-history sync balloons past 150 GB — never run bare `repo sync`.
- `~/retro-devices/kitkat/` — copy of `devices/maple/kitkat/` (scp'd).
  Staged into the tree with
  `cp ~/retro-devices/kitkat/* ~/aosp-kitkat/vendor/sony/maple/`
  (the repo's `sync-device-tree.sh` equivalent; script assumes local tree).
- `~/retro-containers/kitkat/Dockerfile` — copy of
  `containers/kitkat/Dockerfile`. Rebuild after edits:
  `docker build -f ~/retro-containers/kitkat/Dockerfile -t retro-kitkat:4.4.4 ...`
- `~/aosp-kitkat/vendor/sony/maple/kernel` — TWRP-dumped `Image.gz-dtb`
  (16.7 MB, NOT in git). Re-dump via
  `adb pull /dev/block/bootdevice/by-name/boot` + `abootimg -x` if lost.
- `~/aosp-kitkat/build-maple.sh` (`lunch full_maple-eng && make -j6`),
  `build-sys.sh` (`make systemimage`), `build-boot.sh` (`make bootimage`),
  `run-*.sh` wrappers (`docker run --rm -v ~/aosp-kitkat:/srv/aosp-kitkat
  -v ~/ccache-kitkat:/srv/ccache-kitkat ...`, sudo needs the password on
  stdin — see files).
- Logs: `~/build-maple.log`, `~/build-sys.log`, `~/build-boot.log`,
  `~/repo-sync*.log`.

## Background launches (the channel hangs — verify separately)

`setsid nohup ... &` still holds the SSH channel open; launch with a
short client timeout, IGNORE the timeout, then verify with a fresh call
(`pgrep -af '[m]ain\.py'`, log tails, `du`).

## Footguns hit already (do not repeat)

- `pkill -f`/`pgrep -f` patterns match YOUR OWN `bash -c` wrapper
  (it contains the pattern text) — always use the bracket trick
  (`[m]ain\.py`) or kill by exact PID. We killed our own shell twice.
- Two concurrent `repo sync` on one tree duplicate packfiles and poison
  each other (`reference already exists`, corrupt `base.git` stubs).
  One sync at a time; repair with
  `rm -rf .repo/projects/.../X.git .repo/project-objects/.../X.git`
  + re-sync that project.
- `sudo` resets `~` to `/root` — always use absolute `/home/BUILD_USER/`
  paths under sudo.
- `out/` is root-owned (container builds as root): `chmod`/`sudo` for
  reads, `strings`, `scp`.
- Docker `--rm` destroys exit codes; tail the log files instead.
- KitKat `make` regenerates `system.img` SPARSE — always `simg2img`
  before `dd` (use the container's `out/host/linux-x86/bin/simg2img`;
  it is 32-bit and won't run on the Ubuntu 26.04 host).
- `make servicemanager systemimage`-style targets speed up iterations;
  full `make -j6` after UAPI/bionic changes (~1 h, touches everything).
- `TARGET_NO_KERNEL` is NOT yet set — set it (plus formalize the three
  patch sets) once server access returns; see maple.md pending list.

## Flash workflow (phone side, from the Linux workstation)

Phone in TWRP (`adb devices` → `recovery`). Images live in repo `dumps/`
(gitignored):
- `dd if=<raw-system> of=/dev/block/bootdevice/by-name/system bs=4M`
  (5 GB raw via simg2img locally; push to `/data/media`, NOT `/sdcard`
  which refuses creates; `adb shell` stdin pipes fail — push files).
- `dd if=<boot.img> of=/dev/block/bootdevice/by-name/boot bs=4M`
  + read-back md5 verify before every reboot.
- Read state back: `/data/misc/watch*.txt`, `/data/misc/sm-debug.txt`,
  `/data/dalvik-cache` count, `/data/tombstones`, `/sys/fs/pstore/*`
  (mount pstore in TWRP if empty), `/data/system/packages.xml`.
- The phone auto-returns to TWRP 15 min after boot (autowinch service);
  after that lands, `adb reboot` starts the next cycle.
