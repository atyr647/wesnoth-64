#!/usr/bin/env bash
# Fix libgcc's DWARF unwinder for the N64 (o64 ABI, 64-bit kernel addressing).
#
# libdragon runs the VR4300 with SR.KX=1, so kernel addresses must be held in
# registers sign-extended (0xFFFFFFFF8xxxxxxx). In o64 a pointer is 32 bits and
# a register 64; libgcc's unwinder converts register values it keeps "by value"
# with (_Unwind_Word)(_Unwind_Internal_Ptr)x -- a zero extension. A landing pad
# then receives e.g. the exception object as 0x0000000080153e98, and the first
# load through it faults (seen in _Unwind_Resume after a cleanup).
#
# This rebuilds unwind-dw2.o from the toolchain's own GCC sources with those
# conversions sign-extending, and replaces it in the installed libgcc.a.
#
#   tools/patch-n64-libgcc.sh [N64_INST=/opt/n64_toolchain]
# Needs the toolchain build tree (tools/build_n64_toolchain.sh keeps it in
# $N64_INST/build).
set -euo pipefail
N64_INST="${1:-${N64_INST:-/opt/n64_toolchain}}"
BUILD="$N64_INST/build/build-gcc/mips64-elf/libgcc"
SRC="$(ls -d "$N64_INST"/build/gcc-*/libgcc | head -1)"
LIBGCC="$("$N64_INST/bin/mips64-elf-gcc" -print-libgcc-file-name)"
[ -f "$BUILD/Makefile" ] || { echo "toolchain build tree not found: $BUILD" >&2; exit 1; }
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cp "$SRC/unwind-dw2.c" "$WORK/unwind-dw2.c"
python3 - "$WORK/unwind-dw2.c" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
SX = "(_Unwind_Word) (_Unwind_Sword) (__INTPTR_TYPE__)"
fixes = [
    # register values kept "by value" in the context
    ("  return (_Unwind_Word) (_Unwind_Internal_Ptr) val;",
     "  return " + SX + " (_Unwind_Internal_Ptr) val; /* N64: sign-extend */"),
    ("\t      w = (_Unwind_Internal_Ptr) t;",
     "\t      w = " + SX + " (_Unwind_Internal_Ptr) t; /* N64: sign-extend */"),
    # the stack pointer restored for the landing pad (_Unwind_SetSpColumn)
    ("      tmp_sp->word = (_Unwind_Ptr) cfa;",
     "      tmp_sp->word = " + SX + " (_Unwind_Ptr) cfa; /* N64: sign-extend */"),
    # addresses computed by DWARF expressions
    ("\t  result = (_Unwind_Word) (_Unwind_Ptr) read_pointer (op_ptr);",
     "\t  result = " + SX + " (_Unwind_Ptr) read_pointer (op_ptr); /* N64: sign-extend */"),
    ("\t\tresult = (_Unwind_Ptr) read_pointer (ptr);",
     "\t\tresult = " + SX + " (_Unwind_Ptr) read_pointer (ptr); /* N64: sign-extend */"),
]
for a, b in fixes:
    if b in s: continue
    assert a in s, a
    s = s.replace(a, b)
open(p, "w").write(s)
PY
# Same command libgcc's Makefile uses, with the patched source.
CMD="$(cd "$BUILD" && make -n unwind-dw2.o -W ../../../$(basename "$(dirname "$SRC")")/libgcc/unwind-dw2.c 2>/dev/null | grep -- '-o unwind-dw2.o' | head -1)"
[ -n "$CMD" ] || { echo "could not get the unwind-dw2.o compile command" >&2; exit 1; }
CMD="${CMD//-o unwind-dw2.o/-o $WORK/unwind-dw2.o}"
CMD="${CMD//-MF unwind-dw2.dep/-MF $WORK/unwind-dw2.dep}"
CMD="${CMD//..\/..\/..\/$(basename "$(dirname "$SRC")")\/libgcc\/unwind-dw2.c/$WORK/unwind-dw2.c}"
(cd "$BUILD" && eval "$CMD")
"$N64_INST/bin/mips64-elf-ar" r "$LIBGCC" "$WORK/unwind-dw2.o"
echo "patched $LIBGCC (unwind-dw2.o sign-extends register values)"
