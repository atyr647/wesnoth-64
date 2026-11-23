#!/usr/bin/env bash
# Prepare the SummerCart64 SD card contents for the N64 port.
#
# The game reads its data from the SD card exactly as the PC game reads it
# from disk: directory "wesnoth" on the card holds an unmodified copy of the
# data tree (data/, images/, sounds/, fonts/). User data (preferences, saves)
# goes to "wesnoth-userdata", created on first start.
#
#   tools/n64-make-sd.sh dir  <mount-point-of-sd-card>
#       copy the data tree onto a mounted card (FAT32 or exFAT)
#   tools/n64-make-sd.sh image <file.img> [size-MiB]
#       build a FAT32 disk image (for emulators: SC64_SD_IMAGE=<file.img>)
#
# Optional: WESNOTH_SD_EXCLUDE="data/campaigns/Foo data/core/music" leaves
# those paths out (smaller test images); the default copies everything.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODE="${1:?usage: $0 dir <mount-point> | image <file.img> [size-MiB]}"
DEST="${2:?destination}"
TREE=(data images sounds fonts)

stage() {   # stage <dir>: wesnoth/ tree with exclusions applied (hard links)
    local out="$1"
    rm -rf "$out"; mkdir -p "$out/wesnoth"
    for t in "${TREE[@]}"; do cp -al "$ROOT/$t" "$out/wesnoth/$t"; done
    for x in ${WESNOTH_SD_EXCLUDE:-}; do rm -rf "$out/wesnoth/$x"; done
    find "$out/wesnoth" -name '.*' -prune -exec rm -rf {} +
}

case "$MODE" in
dir)
    [ -d "$DEST" ] || { echo "not a directory: $DEST" >&2; exit 1; }
    tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
    stage "$tmp/sd"
    cp -r "$tmp/sd/wesnoth" "$DEST/"
    mkdir -p "$DEST/wesnoth-userdata"
    echo "copied the data tree to $DEST/wesnoth"
    ;;
image)
    command -v mkfs.fat >/dev/null && command -v mcopy >/dev/null ||
        { echo "needs dosfstools (mkfs.fat) and mtools (mcopy)" >&2; exit 1; }
    tmp="$(mktemp -d "$(dirname "$DEST")/.sdstage.XXXX")"; trap 'rm -rf "$tmp"' EXIT
    stage "$tmp/sd"
    used=$(du -sm "$tmp/sd/wesnoth" | cut -f1)
    size="${3:-$(( used + used / 4 + 64 ))}"
    rm -f "$DEST"
    truncate -s "${size}M" "$DEST"
    mkfs.fat -F 32 -n WESNOTH "$DEST" >/dev/null
    MTOOLS_SKIP_CHECK=1 mcopy -s -Q -i "$DEST" "$tmp/sd/wesnoth" ::/
    MTOOLS_SKIP_CHECK=1 mmd -i "$DEST" ::/wesnoth-userdata
    echo "wrote $DEST (${size} MiB, data ${used} MiB)"
    ;;
*)
    echo "unknown mode: $MODE" >&2; exit 1 ;;
esac
