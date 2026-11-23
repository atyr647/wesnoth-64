# n64_sdl2.cmake -- build SDL 2 and SDL_image for the Nintendo 64.
#
# Unmodified upstream sources (fetched by tools/fetch-n64-deps.sh, default
# /opt/n64-src) plus the platform files in src/n64/sdl2:
#   SDL_config.h, SDL_n64video.c (video + input), SDL_n64timer.c
#
# Provides:
#   n64_sdl2, n64_sdl2_image      static libraries
#   N64_SDL2_INCLUDE_DIR          directory containing SDL2/SDL.h, SDL2/SDL_image.h

set(N64_DEPS_SOURCE_DIR "/opt/n64-src" CACHE PATH "Where tools/fetch-n64-deps.sh put SDL and SDL_image")
set(N64_SDL_DIR "${N64_DEPS_SOURCE_DIR}/SDL")
set(N64_SDL_IMAGE_DIR "${N64_DEPS_SOURCE_DIR}/SDL_image")
if(NOT EXISTS "${N64_SDL_DIR}/include/SDL.h" OR NOT EXISTS "${N64_SDL_IMAGE_DIR}/include/SDL_image.h")
    message(FATAL_ERROR "SDL sources not found in ${N64_DEPS_SOURCE_DIR}; run tools/fetch-n64-deps.sh")
endif()

# Public headers as <SDL2/...>, with the N64 SDL_config.h.
set(N64_SDL2_INCLUDE_DIR "${CMAKE_BINARY_DIR}/sdl2-include")
file(GLOB _sdl_headers "${N64_SDL_DIR}/include/*.h")
list(FILTER _sdl_headers EXCLUDE REGEX "/SDL_config.*\\.h$")
file(COPY ${_sdl_headers} "${N64_SDL_IMAGE_DIR}/include/SDL_image.h"
     DESTINATION "${N64_SDL2_INCLUDE_DIR}/SDL2")
configure_file("${CMAKE_SOURCE_DIR}/src/n64/sdl2/SDL_config.h"
               "${N64_SDL2_INCLUDE_DIR}/SDL2/SDL_config.h" COPYONLY)
file(WRITE "${N64_SDL2_INCLUDE_DIR}/SDL2/SDL_revision.h"
     "#define SDL_REVISION \"SDL-2.30.9 (N64)\"\n#define SDL_REVISION_NUMBER 0\n")

set(_S "${N64_SDL_DIR}/src")
set(N64_SDL2_SOURCES
    ${_S}/SDL.c ${_S}/SDL_assert.c ${_S}/SDL_dataqueue.c ${_S}/SDL_error.c
    ${_S}/SDL_guid.c ${_S}/SDL_hints.c ${_S}/SDL_list.c ${_S}/SDL_log.c ${_S}/SDL_utils.c
    ${_S}/atomic/SDL_atomic.c ${_S}/atomic/SDL_spinlock.c
    ${_S}/audio/SDL_audio.c ${_S}/audio/SDL_audiocvt.c ${_S}/audio/SDL_audiotypecvt.c
    ${_S}/audio/SDL_mixer.c ${_S}/audio/SDL_wave.c
    ${_S}/cpuinfo/SDL_cpuinfo.c
    ${_S}/file/SDL_rwops.c
    ${_S}/filesystem/dummy/SDL_sysfilesystem.c
    ${_S}/haptic/SDL_haptic.c ${_S}/haptic/dummy/SDL_syshaptic.c
    ${_S}/joystick/SDL_joystick.c ${_S}/joystick/SDL_gamecontroller.c
    ${_S}/joystick/controller_type.c ${_S}/joystick/SDL_steam_virtual_gamepad.c
    ${_S}/joystick/dummy/SDL_sysjoystick.c
    ${_S}/loadso/dummy/SDL_sysloadso.c
    ${_S}/locale/SDL_locale.c ${_S}/locale/dummy/SDL_syslocale.c
    ${_S}/misc/SDL_url.c ${_S}/misc/dummy/SDL_sysurl.c
    ${_S}/power/SDL_power.c
    ${_S}/sensor/SDL_sensor.c ${_S}/sensor/dummy/SDL_dummysensor.c
    ${_S}/thread/SDL_thread.c
    ${_S}/thread/generic/SDL_syscond.c ${_S}/thread/generic/SDL_sysmutex.c
    ${_S}/thread/generic/SDL_syssem.c ${_S}/thread/generic/SDL_systhread.c
    ${_S}/thread/generic/SDL_systls.c
    ${_S}/render/SDL_render.c ${_S}/render/SDL_yuv_sw.c
    ${_S}/video/yuv2rgb/yuv_rgb_std.c
    ${CMAKE_SOURCE_DIR}/src/n64/sdl2/SDL_n64video.c
    ${CMAKE_SOURCE_DIR}/src/n64/sdl2/SDL_n64timer.c
    ${CMAKE_SOURCE_DIR}/src/n64/sdl2/SDL_n64render.c
    ${CMAKE_SOURCE_DIR}/src/n64/sdl2/SDL_n64audio.c
)
file(GLOB _sdl_glob
    ${_S}/events/*.c ${_S}/stdlib/*.c ${_S}/render/software/*.c ${_S}/video/*.c)
list(FILTER _sdl_glob EXCLUDE REGEX "/(SDL_egl|SDL_vulkan_utils|SDL_mslibc|imKStoUCS)\\.c$")
list(APPEND N64_SDL2_SOURCES ${_sdl_glob})

set(N64_SDL_COMPILE_OPTIONS -O2 -mlong-calls -fno-strict-aliasing -w)

add_library(n64_sdl2 STATIC EXCLUDE_FROM_ALL ${N64_SDL2_SOURCES})
target_include_directories(n64_sdl2 PRIVATE
    "${N64_SDL2_INCLUDE_DIR}/SDL2" "${N64_SDL_DIR}/src")
target_include_directories(n64_sdl2 INTERFACE "${N64_SDL2_INCLUDE_DIR}")
target_compile_definitions(n64_sdl2 PRIVATE USING_GENERATED_CONFIG_H DYNAPI_NEEDS_DLOPEN)
target_compile_options(n64_sdl2 PRIVATE ${N64_SDL_COMPILE_OPTIONS})

# libwebp (decoder + demux): Wesnoth ships many images as WebP.
set(N64_WEBP_DIR "${N64_DEPS_SOURCE_DIR}/libwebp")
file(GLOB _webp_srcs "${N64_WEBP_DIR}/src/dec/*.c" "${N64_WEBP_DIR}/src/dsp/*.c"
                     "${N64_WEBP_DIR}/src/utils/*.c" "${N64_WEBP_DIR}/src/demux/*.c")
add_library(n64_webp STATIC EXCLUDE_FROM_ALL ${_webp_srcs})
target_include_directories(n64_webp PUBLIC "${N64_WEBP_DIR}/src")
target_include_directories(n64_webp PRIVATE "${N64_WEBP_DIR}")
target_compile_definitions(n64_webp PRIVATE malloc=n64_vmalloc calloc=n64_vcalloc
                                          realloc=n64_vrealloc free=n64_vfree)
target_compile_options(n64_webp PRIVATE ${N64_SDL_COMPILE_OPTIONS})

# SDL_image: PNG and JPEG through its bundled stb_image, WebP through libwebp
# (src/n64/sdl2/IMG_webp_n64.c: scaled decoding of oversized images); PNG
# saving through its bundled miniz.
set(_I "${N64_SDL_IMAGE_DIR}/src")
add_library(n64_sdl2_image STATIC EXCLUDE_FROM_ALL
    ${_I}/IMG.c ${_I}/IMG_bmp.c ${_I}/IMG_gif.c ${_I}/IMG_jpg.c ${_I}/IMG_png.c
    ${_I}/IMG_stb.c ${_I}/IMG_tga.c ${CMAKE_SOURCE_DIR}/src/n64/sdl2/IMG_webp_n64.c ${_I}/IMG_xcf.c ${_I}/IMG_xpm.c
    ${_I}/IMG_xv.c ${_I}/IMG_pnm.c ${_I}/IMG_pcx.c ${_I}/IMG_lbm.c ${_I}/IMG_qoi.c
    ${_I}/IMG_svg.c ${_I}/IMG_tif.c ${_I}/IMG_avif.c ${_I}/IMG_jxl.c)
target_include_directories(n64_sdl2_image PRIVATE "${N64_SDL2_INCLUDE_DIR}/SDL2")
target_include_directories(n64_sdl2_image INTERFACE "${N64_SDL2_INCLUDE_DIR}")
target_compile_definitions(n64_sdl2_image PRIVATE
    LOAD_BMP LOAD_GIF LOAD_JPG LOAD_PNG LOAD_TGA LOAD_XPM LOAD_PNM LOAD_QOI LOAD_WEBP
    USE_STBIMAGE SDL_IMAGE_SAVE_PNG=1 SDL_IMAGE_SAVE_JPG=1)
target_compile_options(n64_sdl2_image PRIVATE ${N64_SDL_COMPILE_OPTIONS})
target_link_libraries(n64_sdl2_image PUBLIC n64_sdl2 n64_webp)

# ---------------------------------------------------------------------------
# wesnoth_n64_sdltest: small ROM exercising the SDL port (renderer, target
# textures, blending, SDL_image, cursor, timers, input). Not part of `all`.
function(n64_embed_file var file out)
    file(READ "${file}" hex HEX)
    string(LENGTH "${hex}" n)
    math(EXPR n "${n} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
    file(WRITE "${out}" "const unsigned char ${var}[] = {${hex}};\nconst unsigned ${var}_len = ${n};\n")
endfunction()
n64_embed_file(test_cursor_png "${CMAKE_SOURCE_DIR}/images/cursors/normal.png" "${CMAKE_BINARY_DIR}/sdltest_cursor.c")
n64_embed_file(test_unit_png "${CMAKE_SOURCE_DIR}/data/core/images/units/human-loyalists/bowman.png" "${CMAKE_BINARY_DIR}/sdltest_unit.c")
n64_embed_file(test_tile_png "${CMAKE_SOURCE_DIR}/data/core/images/terrain/grass/green.png" "${CMAKE_BINARY_DIR}/sdltest_tile.c")
add_executable(wesnoth_n64_sdltest EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/src/n64/sdl2/test/n64_sdl_test.c
    ${CMAKE_BINARY_DIR}/sdltest_cursor.c ${CMAKE_BINARY_DIR}/sdltest_unit.c ${CMAKE_BINARY_DIR}/sdltest_tile.c)
target_compile_options(wesnoth_n64_sdltest PRIVATE -O2)
target_link_options(wesnoth_n64_sdltest PRIVATE
    -T ${CMAKE_BINARY_DIR}/wesnoth_n64.ld -Wl,--wrap=__do_global_ctors -Wl,--gc-sections)
target_link_libraries(wesnoth_n64_sdltest PRIVATE n64_sdl2_image n64_sdl2
    ${N64_INST}/mips64-elf/lib/libdragon.a ${N64_INST}/mips64-elf/lib/libdragonsys.a m c gcc)
n64_create_rom(wesnoth_n64_sdltest)

# SDL_mixer: WAV and Ogg Vorbis (its bundled stb_vorbis), the formats
# Wesnoth's sounds and music use.
set(N64_SDL_MIXER_DIR "${N64_DEPS_SOURCE_DIR}/SDL_mixer")
if(EXISTS "${N64_SDL_MIXER_DIR}/include/SDL_mixer.h")
    set(_M "${N64_SDL_MIXER_DIR}/src")
    file(COPY "${N64_SDL_MIXER_DIR}/include/SDL_mixer.h" DESTINATION "${N64_SDL2_INCLUDE_DIR}/SDL2")
    add_library(n64_sdl2_mixer STATIC EXCLUDE_FROM_ALL
        ${_M}/mixer.c ${_M}/music.c ${_M}/utils.c ${_M}/effect_position.c
        ${_M}/effect_stereoreverse.c ${_M}/effects_internal.c
        ${_M}/codecs/load_aiff.c ${_M}/codecs/load_voc.c ${_M}/codecs/music_wav.c
        ${_M}/codecs/music_ogg_stb.c ${_M}/codecs/mp3utils.c)
    target_include_directories(n64_sdl2_mixer PRIVATE "${N64_SDL2_INCLUDE_DIR}/SDL2" "${_M}" "${_M}/codecs")
    target_include_directories(n64_sdl2_mixer INTERFACE "${N64_SDL2_INCLUDE_DIR}")
    target_compile_definitions(n64_sdl2_mixer PRIVATE MUSIC_WAV MUSIC_OGG OGG_USE_STB)
    target_compile_options(n64_sdl2_mixer PRIVATE ${N64_SDL_COMPILE_OPTIONS})
    target_link_libraries(n64_sdl2_mixer PUBLIC n64_sdl2)
endif()
