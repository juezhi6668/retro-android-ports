#!/usr/bin/env bash
# Sync a device tree from devices/<codename>/<version> into the AOSP checkout
# at $AOSP_DIR (default: <repo>/aosp-donut), where the build system expects it
# as vendor/<vendor>/<codename>.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AOSP="${AOSP_DIR:-$ROOT/aosp-donut}"

case "${1:-}" in
    cooper) VENDOR=samsung; DEVICE=cooper ;;
    maple) VENDOR=sony; DEVICE=maple ;;
    poplar) VENDOR=sony; DEVICE=poplar ;;
    sagit) VENDOR=xiaomi; DEVICE=sagit ;;
    *)
        echo "usage: $0 <codename> [version]   (known: cooper, maple, poplar, sagit)"
        exit 1
        ;;
esac
VERSION="${2:-}"
if [ -z "$VERSION" ]; then
    case "$DEVICE" in
        cooper) VERSION=donut ;;
        maple|poplar) VERSION=kitkat ;;
        sagit) VERSION=kitkat ;;
    esac
fi

SRC="$ROOT/devices/$DEVICE/$VERSION"
DST="$AOSP/vendor/$VENDOR/$DEVICE"

[ -d "$SRC" ] || { echo "no such device tree: $SRC" >&2; exit 1; }
[ -d "$AOSP" ] || { echo "no AOSP checkout at $AOSP (set AOSP_DIR)" >&2; exit 1; }

mkdir -p "$DST"
# `kernel` is the gitignored dumped kernel blob staged inside the synced path (§4 of the
# build docs: vendor/<vendor>/<device>/kernel, TARGET_PREBUILT_KERNEL). A plain --delete
# silently removes it and the next `make bootimage` dies with "No rule to make target
# 'vendor/<vendor>/<device>/kernel'" — hit 2026-10-19, kitkat tree.
rsync -a --delete --exclude kernel "$SRC/" "$DST/"
echo "synced $SRC -> $DST"
