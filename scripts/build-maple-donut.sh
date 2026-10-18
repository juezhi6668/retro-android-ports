#!/usr/bin/env bash
# Build Donut for maple inside the Hardy chroot.
#
#   scripts/build-maple-donut.sh [make-target...]     (default: full build)
#
# What it does:
#   1. syncs devices/maple/donut/ -> $AOSP_DIR/vendor/sony/maple/
#   2. re-stages the out-of-git kernel blob. NOTE: sync-device-tree.sh uses
#      rsync --delete, which REMOVES vendor/sony/maple/kernel (it is not in
#      devices/maple/donut/ by design — Sony/Qualcomm binary). Every sync
#      must be followed by this copy; that is why the build lives in this
#      script.
#   3. runs lunch + make inside the chroot with the AOSP tree bind-mounted.
#
# Env: AOSP_DIR (default <repo>/aosp-donut), CHROOT (default /srv/donut-hardy),
#      CHROOT_MNT (mount point inside the chroot, default /mnt/aosp-donut),
#      KERNEL_BLOB (default <repo>/work/maple-donut/kernel-Image.gz-dtb),
#      JOBS (default nproc).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AOSP="${AOSP_DIR:-$ROOT/aosp-donut}"
CHROOT="${CHROOT:-/srv/donut-hardy}"
CHROOT_MNT="${CHROOT_MNT:-/mnt/aosp-donut}"
KERNEL_BLOB="${KERNEL_BLOB:-$ROOT/work/maple-donut/kernel-Image.gz-dtb}"
JOBS="${JOBS:-$(nproc)}"

TARGETS=("$@")
[ ${#TARGETS[@]} -eq 0 ] && TARGETS=(all)

[ -d "$AOSP" ] || { echo "no AOSP checkout at $AOSP (set AOSP_DIR)" >&2; exit 1; }
[ -f "$KERNEL_BLOB" ] || { echo "missing kernel blob $KERNEL_BLOB" >&2; exit 1; }

AOSP_DIR="$AOSP" "$ROOT/scripts/sync-device-tree.sh" maple donut
mkdir -p "$AOSP/vendor/sony/maple"
cp "$KERNEL_BLOB" "$AOSP/vendor/sony/maple/kernel"
echo "staged kernel: $KERNEL_BLOB -> vendor/sony/maple/kernel"

# The chroot needs the tree visible (bind mount is not persistent).
if ! mountpoint -q "$CHROOT$CHROOT_MNT"; then
    sudo mount --bind "$AOSP" "$CHROOT$CHROOT_MNT"
    echo "bind-mounted $AOSP -> $CHROOT$CHROOT_MNT"
fi

TARGET_CMD="$(printf '%s ' "${TARGETS[@]}")"
[ "$TARGET_CMD" = "all " ] && TARGET_CMD=""
sudo chroot "$CHROOT" /bin/bash -c "
    set -o pipefail
    export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
    cd $CHROOT_MNT
    source build/envsetup.sh >/dev/null 2>&1
    lunch full_maple-eng > /tmp/lunch-maple.log 2>&1
    cat /tmp/lunch-maple.log
    make -j$JOBS $TARGET_CMD 2>&1 | tail -80
"
