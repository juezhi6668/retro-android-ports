#!/usr/bin/env bash
# Sync a device tree from devices/<codename>/<version> into the AOSP checkout
# at $AOSP_DIR (default: <repo>/aosp-donut), where the build system expects it
# as vendor/<vendor>/<codename>.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AOSP="${AOSP_DIR:-$ROOT/aosp-donut}"

case "${1:-}" in
    cooper) VENDOR=samsung; DEVICE=cooper ;;
    *)
        echo "usage: $0 <codename> [version]   (known: cooper)"
        exit 1
        ;;
esac
VERSION="${2:-donut}"

SRC="$ROOT/devices/$DEVICE/$VERSION"
DST="$AOSP/vendor/$VENDOR/$DEVICE"

[ -d "$SRC" ] || { echo "no such device tree: $SRC" >&2; exit 1; }
[ -d "$AOSP" ] || { echo "no AOSP checkout at $AOSP (set AOSP_DIR)" >&2; exit 1; }

rsync -a --delete "$SRC/" "$DST/"
echo "synced $SRC -> $DST"
