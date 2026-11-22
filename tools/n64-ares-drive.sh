#!/usr/bin/env bash
# Drive a ROM on headless ares with scripted controller input and screenshots.
#
#   tools/n64-ares-drive.sh <rom.z64> <script> <out-dir>
#
# Script lines (one per line, '#' comments):
#   wait <seconds>        sleep
#   tap <button> [secs]   press and release (default 0.15 s)
#   hold <button> <secs>  hold
#   shot <name>           screenshot to <out-dir>/<name>.png
# Buttons: up down left right a b z start l r cup cdown cleft cright
#
# ares reads the keyboard from the X server, so xdotool key events drive it.
# Bindings (ares keyboard device 0x1, key = index in ruby/input/keyboard/xlib.cpp):
#   arrows = D-pad   x = A   z = B   c = Z   Return = Start   q = L   w = R
#   i/j/k/l = C-up/C-left/C-down/C-right
set -euo pipefail
ROM="${1:?rom.z64}"; SCRIPT="${2:?script}"; OUT="${3:?out-dir}"
ARES="${ARES:-/opt/pak-ares/bin/ares}"
mkdir -p "$OUT"
declare -A KEY=([up]=Up [down]=Down [left]=Left [right]=Right [a]=x [b]=z [z]=c
                [start]=Return [l]=q [r]=w [cup]=i [cleft]=j [cdown]=k [cright]=l)
P="Nintendo64/Input/Controller.Port.1/Gamepad"
D=$((200 + RANDOM % 100))
Xvfb ":$D" -screen 0 1280x960x24 -ac -nolisten tcp >/dev/null 2>&1 &
XPID=$!
trap 'kill -9 $APID $XPID 2>/dev/null || true; rm -f /tmp/.X$D-lock' EXIT
sleep 2
DISPLAY=":$D" LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy "$ARES" --system "Nintendo 64" \
  --no-file-prompt --fullscreen --setting Audio/Mute=true \
  --setting "$P/Up=0x1/0/84;;"    --setting "$P/Down=0x1/0/85;;" \
  --setting "$P/Left=0x1/0/86;;"  --setting "$P/Right=0x1/0/87;;" \
  --setting "$P/A=0x1/0/58;;"     --setting "$P/B=0x1/0/60;;" \
  --setting "$P/Z=0x1/0/37;;"     --setting "$P/Start=0x1/0/89;;" \
  --setting "$P/L=0x1/0/51;;"     --setting "$P/R=0x1/0/57;;" \
  --setting "$P/C-Up=0x1/0/43;;"  --setting "$P/C-Left=0x1/0/44;;" \
  --setting "$P/C-Down=0x1/0/45;;" --setting "$P/C-Right=0x1/0/46;;" \
  "$ROM" >"$OUT/ares.log" 2>&1 &
APID=$!
export DISPLAY=":$D"
while read -r cmd a b _; do
  case "$cmd" in
    ''|'#'*) ;;
    wait) sleep "$a" ;;
    tap)  xdotool keydown "${KEY[$a]}"; sleep "${b:-0.15}"; xdotool keyup "${KEY[$a]}"; sleep 0.15 ;;
    hold) xdotool keydown "${KEY[$a]}"; sleep "$b"; xdotool keyup "${KEY[$a]}"; sleep 0.15 ;;
    shot) import -window root "$OUT/$a.png"; echo "shot $a: $(convert "$OUT/$a.png" -format 'mean %[fx:mean]' info:)" ;;
    *) echo "unknown command: $cmd" >&2 ;;
  esac
done < "$SCRIPT"
