#!/usr/bin/env bash
# Fetch the third-party libraries the N64 build compiles from source, at the
# versions it was tested with. Sources are used unmodified; the console's
# configuration lives in src/n64/sdl2 and src/n64/text.
#
#   tools/fetch-n64-deps.sh [dest=/opt/n64-src]
#
# Then configure with -DN64_DEPS_SOURCE_DIR=<dest> if not the default.
set -euo pipefail
DEST="${1:-/opt/n64-src}"
mkdir -p "$DEST"
fetch() {   # fetch <dir> <git-url> <tag>
    if [ -d "$DEST/$1/.git" ]; then echo "$1: present"; return; fi
    git -c advice.detachedHead=false clone -q --depth 1 -b "$3" "$2" "$DEST/$1"
    echo "$1: $3"
}
fetch SDL       https://github.com/libsdl-org/SDL.git               release-2.30.9
fetch SDL_image https://github.com/libsdl-org/SDL_image.git         release-2.8.8
fetch SDL_mixer https://github.com/libsdl-org/SDL_mixer.git         release-2.8.0
fetch libwebp   https://github.com/webmproject/libwebp.git          v1.4.0
fetch freetype  https://gitlab.freedesktop.org/freetype/freetype.git VER-2-13-3
fetch harfbuzz  https://github.com/harfbuzz/harfbuzz.git            10.1.0
fetch pixman    https://gitlab.freedesktop.org/pixman/pixman.git    pixman-0.44.2
fetch cairo     https://gitlab.freedesktop.org/cairo/cairo.git      1.18.2
