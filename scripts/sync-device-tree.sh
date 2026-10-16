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
    *)
        echo "usage: $0 <codename> [version]   (known: cooper, maple, poplar)"
        exit 1
        ;;
esac
VERSION="${2:-}"
if [ -z "$VERSION" ]; then
    case "$DEVICE" in
        cooper) VERSION=donut ;;
        maple|poplar) VERSION=kitkat ;;
    esac
fi

SRC="$ROOT/devices/$DEVICE/$VERSION"
DST="$AOSP/vendor/$VENDOR/$DEVICE"

[ -d "$SRC" ] || { echo "no such device tree: $SRC" >&2; exit 1; }
[ -d "$AOSP" ] || { echo "no AOSP checkout at $AOSP (set AOSP_DIR)" >&2; exit 1; }

mkdir -p "$DST"
rsync -a --delete "$SRC/" "$DST/"
echo "synced $SRC -> $DST"
