# N64 Cross-Compilation Toolchain for libdragon
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/n64-libdragon.cmake ..
#
# Requires:
#   - libdragon installed (https://github.com/DragonMinded/libdragon)
#   - N64_INST environment variable pointing to libdragon install prefix
#     OR set N64_TOOLCHAIN_PREFIX manually below

cmake_minimum_required(VERSION 3.21)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR mips)

# Locate the libdragon toolchain prefix
if(DEFINED ENV{N64_INST})
    set(N64_INST "$ENV{N64_INST}")
else()
    set(N64_INST "/opt/n64_toolchain" CACHE PATH "libdragon install prefix")
endif()

set(TOOLCHAIN_PREFIX "${N64_INST}/bin/mips64-elf-")

set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_AR           "${TOOLCHAIN_PREFIX}ar")
set(CMAKE_RANLIB       "${TOOLCHAIN_PREFIX}ranlib")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_PREFIX}objcopy")
set(CMAKE_SIZE         "${TOOLCHAIN_PREFIX}size")

# N64 / VR4300 CPU flags — must match libdragon's own ABI (o64)
# -march=vr4300   : Target the VR4300 (MIPS III subset actually used by N64)
# -mtune=vr4300   : Tune scheduling for VR4300
# -mabi=o64       : libdragon uses the o64 ABI (32-bit pointers, 64-bit regs)
# -mfix4300       : Apply errata workarounds for the VR4300
# -G 0            : Don't use the global pointer optimisation (unsafe with ROM addressing)
# -ffunction-sections -fdata-sections : Allow linker to gc unused code
set(N64_COMMON_FLAGS
    "-march=vr4300 -mtune=vr4300 -mabi=o64 -mfix4300 -G 0 -ffunction-sections -fdata-sections"
)
set(N64_INCLUDE_FLAGS "-I${N64_INST}/include -I${N64_INST}/mips64-elf/include")

set(CMAKE_C_FLAGS_INIT   "${N64_COMMON_FLAGS} ${N64_INCLUDE_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${N64_COMMON_FLAGS} ${N64_INCLUDE_FLAGS} -std=gnu++20")

# Linker flags — link against libdragon, newlib-nano
set(N64_LIB_PATH "${N64_INST}/mips64-elf/lib")
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-L${N64_LIB_PATH} -ldragon -lc -lm -ldragonsys -Wl,--gc-sections -Wl,-Map=wesnoth_n64.map"
)

# ROM image output
set(CMAKE_EXECUTABLE_SUFFIX ".elf")

# Don't try to test executables on the host
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Find programs on the host system, not the target
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH "${N64_INST}")

# Disable host-only features
set(N64_BUILD TRUE CACHE BOOL "Building for Nintendo 64" FORCE)
set(ENABLE_CAMPAIGN_SERVER OFF CACHE BOOL "" FORCE)
set(ENABLE_NLS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)

# Helper: convert ELF to N64 ROM image using the libdragon pipeline:
#   1. n64sym         — extract symbol table for backtraces
#   2. mips-strip     — produce a stripped copy of the ELF
#   3. n64elfcompress — LZ4-compress the stripped ELF (in-place)
#   4. n64tool        — wrap into a .z64 ROM with libdragon's IPL3 boot,
#                       appending the ROMFS blob (if given) before the symbols
function(n64_create_rom target)
    # Optional second argument: ROMFS blob (wesnoth.wnrf) built by
    # tools/n64-asset-pipeline. n64tool names TOC entries after the file's
    # basename and the runtime finds it with rompak_search_ext(".wnrf").
    set(ROMFS_ARGS "")
    if(ARGC GREATER 1 AND EXISTS "${ARGV1}")
        set(ROMFS_ARGS --align 256 "${ARGV1}")
    endif()
    set(ELF_FILE   "$<TARGET_FILE:${target}>")
    set(ELF_DIR    "$<TARGET_FILE_DIR:${target}>")
    set(SYM_FILE   "${ELF_DIR}/${target}.elf.sym")
    set(STRIPPED   "${ELF_DIR}/${target}.stripped")
    set(ROM_FILE   "${ELF_DIR}/${target}.z64")

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E env "N64_INST=${N64_INST}"
                ${N64_INST}/bin/n64sym ${ELF_FILE} ${SYM_FILE}
        COMMAND ${CMAKE_COMMAND} -E copy ${ELF_FILE} ${STRIPPED}
        COMMAND ${TOOLCHAIN_PREFIX}strip -s ${STRIPPED}
        COMMAND ${CMAKE_COMMAND} -E env "N64_INST=${N64_INST}"
                ${N64_INST}/bin/n64elfcompress -o ${ELF_DIR} -c 1 ${STRIPPED}
        COMMAND ${CMAKE_COMMAND} -E env "N64_INST=${N64_INST}"
                ${N64_INST}/bin/n64tool
                --title "Wesnoth"
                --toc
                --output ${ROM_FILE}
                --align 256 ${STRIPPED}
                ${ROMFS_ARGS}
                --align 8   ${SYM_FILE}
        COMMENT "Creating N64 ROM: ${target}.z64"
        VERBATIM
    )
endfunction()
