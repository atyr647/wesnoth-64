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
    set(N64_INST "/opt/libdragon" CACHE PATH "libdragon install prefix")
endif()

set(TOOLCHAIN_PREFIX "${N64_INST}/bin/mips64-elf-")

set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_AR           "${TOOLCHAIN_PREFIX}ar")
set(CMAKE_RANLIB       "${TOOLCHAIN_PREFIX}ranlib")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_PREFIX}objcopy")
set(CMAKE_SIZE         "${TOOLCHAIN_PREFIX}size")

# N64 / VR4300 CPU flags
# -march=vr4300   : Target the VR4300 (MIPS III subset actually used by N64)
# -mabi=32        : 32-bit ABI (libdragon uses 32-bit even on 64-bit MIPS)
# -mfix-vr4300    : Apply errata workarounds for the VR4300
# -G 0            : Don't use the global pointer optimisation (unsafe with ROM addressing)
# -ffunction-sections -fdata-sections : Allow linker to gc unused code
set(N64_COMMON_FLAGS
    "-march=vr4300 -mabi=32 -mfix-vr4300 -G 0 -ffunction-sections -fdata-sections -fno-exceptions"
)
set(N64_INCLUDE_FLAGS "-I${N64_INST}/include -I${N64_INST}/mips64-elf/include")

set(CMAKE_C_FLAGS_INIT   "${N64_COMMON_FLAGS} ${N64_INCLUDE_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${N64_COMMON_FLAGS} ${N64_INCLUDE_FLAGS} -std=c++17 -fno-rtti")

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

# Helper: convert ELF to N64 ROM image
function(n64_create_rom target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${N64_INST}/bin/elf2rom
                "$<TARGET_FILE:${target}>"
                "$<TARGET_FILE_DIR:${target}>/${target}.n64"
        COMMENT "Creating N64 ROM: ${target}.n64"
    )
endfunction()
