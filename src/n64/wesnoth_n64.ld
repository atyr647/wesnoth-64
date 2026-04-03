/**
 * wesnoth_n64.ld
 *
 * N64 linker script for Wesnoth.
 *
 * Memory map (8 MB Expansion Pak):
 *
 *   0x80000000 - 0x80000400   Exception vectors (reserved by libdragon)
 *   0x80000400 - 0x80200000   Game code + read-only data (~2 MB)
 *   0x80200000 - 0x80380000   BSS (zeroed globals: pools, caches) (~1.5 MB)
 *   0x80380000 - 0x80400000   Stack (512 KB, grows downward)
 *   0x80400000 - 0x80500000   Heap (1 MB, for std:: allocations)
 *   0x80500000 - 0x80580000   Lua heap (512 KB, managed by n64_lua_alloc)
 *   0x80580000 - 0x80680000   Texture cache (1 MB)
 *   0x80680000 - 0x80700000   Audio buffers (512 KB)
 *   0x80700000 - 0x80780000   WML parse arena (512 KB)
 *   0x80780000 - 0x807FFFFF   Framebuffers + misc (512 KB)
 *
 * ROM addresses (cartridge PI bus):
 *   0x10000000 - 0x10000040   N64 header (64 bytes)
 *   0x10000040 - 0x10200000   Game code (loaded by IPL)
 *   0x10200000+               ROMFS directory + asset data
 *
 * This script is used when linking with:
 *   -T src/n64/wesnoth_n64.ld
 */

OUTPUT_FORMAT("elf32-bigmips")
OUTPUT_ARCH(mips)
ENTRY(_start)

MEMORY
{
    /* RDRAM segments */
    code  (rwx) : ORIGIN = 0x80000400, LENGTH = 2M - 0x400
    bss   (rw)  : ORIGIN = 0x80200000, LENGTH = 1536K
    stack (rw)  : ORIGIN = 0x80380000, LENGTH = 512K
    heap  (rw)  : ORIGIN = 0x80400000, LENGTH = 1M
}

SECTIONS
{
    /* Code and read-only data */
    .text : {
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.init)
        *(.fini)
    } > code

    /* Initialized data */
    .data : {
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(8);
        __data_end = .;
    } > code

    /* Global constructors/destructors */
    .ctors : {
        __CTOR_LIST__ = .;
        KEEP(*(.ctors))
        __CTOR_END__ = .;
    } > code

    .dtors : {
        __DTOR_LIST__ = .;
        KEEP(*(.dtors))
        __DTOR_END__ = .;
    } > code

    /* BSS (zero-initialized, in RDRAM) */
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > bss

    /* Stack pointer initialised to top of stack region */
    __stack_top = ORIGIN(stack) + LENGTH(stack);

    /* Heap boundaries (for newlib _sbrk) */
    __heap_start = ORIGIN(heap);
    __heap_end   = ORIGIN(heap) + LENGTH(heap);

    /* ROMFS address (must match n64_romfs.hpp ROMFS_DIR_ROM_ADDR) */
    __romfs_addr = 0x10200000;

    /* Discard unneeded sections */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.MIPS.abiflags)
        *(.reginfo)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
