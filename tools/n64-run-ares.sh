#!/usr/bin/env bash
# Boot a .z64 headless on ares (built by Pak's tools/build_ares.sh) and grab a
# screenshot, so a ROM can be smoke-tested without a display or a console.
#
#   tools/n64-run-ares.sh <rom.z64> <screenshot.png> [seconds=15] [log=ares.log]
#
# Recipe from Pak's tcl/tools/ares_test.tcl: Xvfb + Mesa software GL, the
# Nintendo 64 core, no file prompt, audio muted. Prints the mean brightness of
# the frame (0 = black screen, which is also what a ROM that never boots shows).
set -euo pipefail
ROM="${1:?rom.z64}"; SHOT="${2:?screenshot.png}"; SECS="${3:-15}"; LOG="${4:-ares.log}"
ARES="${ARES:-/opt/pak-ares/bin/ares}"
D=$((100 + RANDOM % 100))
Xvfb ":$D" -screen 0 1280x960x24 -ac -nolisten tcp >/dev/null 2>&1 &
XPID=$!
trap 'kill -9 $APID $XPID 2>/dev/null || true; rm -f /tmp/.X$D-lock' EXIT
sleep 2
DISPLAY=":$D" LIBGL_ALWAYS_SOFTWARE=1 "$ARES" --system "Nintendo 64" \
    --no-file-prompt --fullscreen --setting Audio/Mute=true "$ROM" >"$LOG" 2>&1 &
APID=$!
sleep "$SECS"
DISPLAY=":$D" import -window root "$SHOT"
convert "$SHOT" -format "mean brightness: %[fx:mean]\n" info:
kill -0 "$APID" 2>/dev/null && echo "ares still running after ${SECS}s" || echo "ares exited early (see $LOG)"
