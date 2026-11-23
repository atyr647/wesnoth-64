# n64_text.cmake -- FreeType, HarfBuzz, pixman and cairo for the Nintendo 64.
#
# Unmodified upstream sources (tools/fetch-n64-deps.sh puts them in
# N64_DEPS_SOURCE_DIR) with the configuration headers in src/n64/text.
# Wesnoth draws text and vector shapes with cairo; its text layout API
# (Pango) is provided by src/n64/pango on top of HarfBuzz and cairo-ft.
#
# Provides static libraries n64_freetype, n64_harfbuzz, n64_pixman, n64_cairo
# and N64_TEXT_INCLUDE_DIRS (public headers of all four).

set(_D "${N64_DEPS_SOURCE_DIR}")
foreach(dep freetype harfbuzz pixman cairo)
    if(NOT EXISTS "${_D}/${dep}")
        message(FATAL_ERROR "${dep} sources not found in ${_D}; run tools/fetch-n64-deps.sh")
    endif()
endforeach()
set(_CFG "${CMAKE_SOURCE_DIR}/src/n64/text")
set(_GEN "${CMAKE_BINARY_DIR}/n64-text-include")

set(N64_TEXT_COMPILE_OPTIONS -mlong-calls -fno-strict-aliasing -w
    $<$<COMPILE_LANGUAGE:C>:-Wno-error=incompatible-pointer-types>)

# --------------------------------------------------------------- FreeType
set(_F "${_D}/freetype/src")
add_library(n64_freetype STATIC EXCLUDE_FROM_ALL
    ${_F}/base/ftsystem.c ${_F}/base/ftinit.c ${_F}/base/ftdebug.c ${_F}/base/ftbase.c
    ${_F}/base/ftbbox.c ${_F}/base/ftbitmap.c ${_F}/base/ftfstype.c ${_F}/base/ftgasp.c
    ${_F}/base/ftglyph.c ${_F}/base/ftmm.c ${_F}/base/ftstroke.c ${_F}/base/ftsynth.c
    ${_F}/base/ftlcdfil.c ${_F}/base/fttype1.c ${_F}/base/ftotval.c ${_F}/base/ftcid.c
    ${_F}/autofit/autofit.c ${_F}/truetype/truetype.c ${_F}/cff/cff.c
    ${_F}/psaux/psaux.c ${_F}/psnames/psnames.c ${_F}/pshinter/pshinter.c
    ${_F}/sfnt/sfnt.c ${_F}/smooth/smooth.c ${_F}/gzip/ftgzip.c)
file(MAKE_DIRECTORY "${_GEN}/freetype-config")
configure_file("${_CFG}/ftmodule.h" "${_GEN}/freetype-config/ftmodule.h" COPYONLY)
target_include_directories(n64_freetype PUBLIC "${_D}/freetype/include")
target_include_directories(n64_freetype PRIVATE "${_GEN}/freetype-config")
target_compile_definitions(n64_freetype PRIVATE FT2_BUILD_LIBRARY
    "FT_CONFIG_MODULES_H=<ftmodule.h>")
target_compile_options(n64_freetype PRIVATE -O2 ${N64_TEXT_COMPILE_OPTIONS})

# --------------------------------------------------------------- HarfBuzz
add_library(n64_harfbuzz STATIC EXCLUDE_FROM_ALL "${_D}/harfbuzz/src/harfbuzz.cc")
target_include_directories(n64_harfbuzz PUBLIC "${_D}/harfbuzz/src")
target_compile_definitions(n64_harfbuzz PRIVATE HAVE_FREETYPE=1 HB_NO_MT=1)
target_compile_options(n64_harfbuzz PRIVATE -Os -fno-exceptions -fno-rtti ${N64_TEXT_COMPILE_OPTIONS})
target_link_libraries(n64_harfbuzz PUBLIC n64_freetype)

# ----------------------------------------------------------------- pixman
set(_P "${_D}/pixman/pixman")
file(READ "${_P}/pixman-version.h.in" _pv)
string(REPLACE "@PIXMAN_VERSION_MAJOR@" "0" _pv "${_pv}")
string(REPLACE "@PIXMAN_VERSION_MINOR@" "44" _pv "${_pv}")
string(REPLACE "@PIXMAN_VERSION_MICRO@" "2" _pv "${_pv}")
file(WRITE "${_GEN}/pixman/pixman-version.h" "${_pv}")
configure_file("${_CFG}/pixman-config.h" "${_GEN}/pixman/pixman-config.h" COPYONLY)
add_library(n64_pixman STATIC EXCLUDE_FROM_ALL
    ${_P}/pixman.c ${_P}/pixman-access.c ${_P}/pixman-access-accessors.c
    ${_P}/pixman-bits-image.c ${_P}/pixman-combine32.c ${_P}/pixman-combine-float.c
    ${_P}/pixman-conical-gradient.c ${_P}/pixman-edge.c ${_P}/pixman-edge-accessors.c
    ${_P}/pixman-fast-path.c ${_P}/pixman-filter.c ${_P}/pixman-glyph.c
    ${_P}/pixman-general.c ${_P}/pixman-gradient-walker.c ${_P}/pixman-image.c
    ${_P}/pixman-implementation.c ${_P}/pixman-linear-gradient.c ${_P}/pixman-matrix.c
    ${_P}/pixman-noop.c ${_P}/pixman-radial-gradient.c ${_P}/pixman-region16.c
    ${_P}/pixman-region32.c ${_P}/pixman-solid-fill.c ${_P}/pixman-timer.c
    ${_P}/pixman-trap.c ${_P}/pixman-utils.c
    ${_P}/pixman-arm.c ${_P}/pixman-mips.c ${_P}/pixman-ppc.c ${_P}/pixman-x86.c ${_P}/pixman-riscv.c)
target_include_directories(n64_pixman PUBLIC "${_P}" "${_GEN}/pixman")
target_compile_definitions(n64_pixman PRIVATE HAVE_CONFIG_H "PIXMAN_CONFIG_H_NAME=\"pixman-config.h\"")
target_compile_options(n64_pixman PRIVATE -O2 ${N64_TEXT_COMPILE_OPTIONS})

# ------------------------------------------------------------------ cairo
set(_C "${_D}/cairo/src")
file(READ "${_D}/cairo/src/meson.build" _cairo_meson)
string(REGEX MATCH "cairo_sources = \\[[^]]*\\]" _cairo_block "${_cairo_meson}")
string(REGEX MATCHALL "'[a-z0-9-]+\\.c'" _cairo_names "${_cairo_block}")
set(_cairo_srcs "")
foreach(n IN LISTS _cairo_names)
    string(REPLACE "'" "" n "${n}")
    list(APPEND _cairo_srcs "${_C}/${n}")
endforeach()
# PDF/PostScript output only (needs zlib): not built.
list(REMOVE_ITEM _cairo_srcs "${_C}/cairo-deflate-stream.c")
# FreeType fonts. No colour (COLR/SVG) glyphs: Wesnoth's fonts have none.
list(APPEND _cairo_srcs ${_C}/cairo-ft-font.c)
configure_file("${_CFG}/cairo-features.h" "${_GEN}/cairo/cairo-features.h" COPYONLY)
configure_file("${_CFG}/cairo-config.h" "${_GEN}/cairo/config.h" COPYONLY)
add_library(n64_cairo STATIC EXCLUDE_FROM_ALL ${_cairo_srcs})
target_include_directories(n64_cairo PUBLIC "${_C}" "${_GEN}/cairo")
target_compile_definitions(n64_cairo PRIVATE HAVE_CONFIG_H CAIRO_COMPILATION)
target_compile_options(n64_cairo PRIVATE -O2 ${N64_TEXT_COMPILE_OPTIONS})
target_link_libraries(n64_cairo PUBLIC n64_pixman n64_freetype)

set(N64_TEXT_INCLUDE_DIRS
    "${_D}/freetype/include" "${_D}/harfbuzz/src" "${_P}" "${_GEN}/pixman" "${_C}" "${_GEN}/cairo")
