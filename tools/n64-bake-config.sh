#!/bin/bash
# Build tools/n64-bake/bake_config against an unmodified upstream Wesnoth build
# and run it, producing the baked WML image the N64 ROM links in
# (-DN64_FROZEN_IMAGE=<out>; see src/n64/frozen/n64_frozen_format.hpp).
#
#   tools/n64-bake-config.sh <upstream source dir> <upstream build dir> <out.bin>
#
# The upstream build is a normal CMake + Ninja build of the same commit this
# port is based on; its compile and link flags are taken from build.ninja.
set -euo pipefail
SRC=$(realpath "$1"); BUILD=$(realpath "$2"); OUT=$(realpath -m "$3")
HERE=$(cd "$(dirname "$0")" && pwd)
field() { awk -v t="$1" -v f="  $2 = " '$0 ~ t {on=1} on && index($0, f) == 1 {print substr($0, length(f) + 1); exit}' "$BUILD/build.ninja"; }
DEFS=$(field '^build src/CMakeFiles/wesnoth.dir/wesnoth.cpp.o:' DEFINES)
INCS=$(field '^build src/CMakeFiles/wesnoth.dir/wesnoth.cpp.o:' INCLUDES)
LIBS=$(field '^build wesnoth:' LINK_LIBRARIES)
TOOL="$BUILD/n64_bake_config"
if [ ! -x "$TOOL" ] || [ "$HERE/n64-bake/bake_config.cpp" -nt "$TOOL" ]; then
    (cd "$BUILD" && eval g++ -std=c++17 -O2 -fPIE $DEFS $INCS -c "$HERE/n64-bake/bake_config.cpp" -o n64_bake_config.o \
        && eval g++ -fPIE -pie n64_bake_config.o $LIBS -o "$TOOL")
fi
cd "$SRC" && "$TOOL" "$SRC" "$OUT"
rm -rf "$OUT.userdata"
