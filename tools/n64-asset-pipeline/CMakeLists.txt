cmake_minimum_required(VERSION 3.21)
project(wesnoth-n64-asset-pipeline CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# This tool runs on the HOST (PC/Mac/Linux), not on N64.
# It reads Wesnoth's data/ directory and produces a packed ROM filesystem.

find_package(PNG REQUIRED)    # libpng  — read .png sprite files
find_package(ZLIB REQUIRED)   # zlib    — decompress existing assets

# Optional: Vorbis for OGG → ADPCM conversion
find_package(Vorbis QUIET)
find_package(OggVorbis QUIET)

add_executable(wesnoth-n64-pack
    src/main.cpp
    src/wml_compiler.cpp
    src/image_converter.cpp
    src/audio_converter.cpp
    src/glyph_sheet.cpp
    src/romfs_builder.cpp
    src/lua_precompiler.cpp
)

target_include_directories(wesnoth-n64-pack PRIVATE
    include/
    ../../src/         # access to Wesnoth serialization headers
)

target_link_libraries(wesnoth-n64-pack PRIVATE
    PNG::PNG
    ZLIB::ZLIB
)

if(Vorbis_FOUND)
    target_link_libraries(wesnoth-n64-pack PRIVATE Vorbis::vorbis Vorbis::vorbisfile)
    target_compile_definitions(wesnoth-n64-pack PRIVATE HAVE_VORBIS)
endif()

install(TARGETS wesnoth-n64-pack DESTINATION bin)
