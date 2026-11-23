/* ========================================================================
 * n64_paged.ld -- libdragon n64.ld variant for demand-paged programs.
 *
 * Resident (kseg0, loaded by IPL3): interrupt vectors, libdragon, the C/C++
 * runtime libraries, the pager, and ALL writable data (.data/.bss).
 *
 * Paged (kseg2 at 0xC0000000, stored in ROM as a separate blob and loaded on
 * demand by src/n64/pager): every other input's code, read-only data, C++
 * exception tables (.eh_frame, .gcc_except_table).
 *
 * Program code placed in .paged must be compiled with -mlong-calls (jumps
 * between kseg0 and kseg2 cross a 256 MiB region boundary).
 * ======================================================================== */

OUTPUT_FORMAT ("elf32-bigmips", "elf32-bigmips", "elf32-littlemips")
OUTPUT_ARCH (mips)
EXTERN (_start)
ENTRY (_start)

PHDRS
{
    irq PT_LOAD AT ( 0x80000000 );
    main PT_LOAD AT ( 0x80000400 );
    paged PT_LOAD;
}

SECTIONS {
    .intvectors 0x80000000 : {
        . = ALIGN(32);
        KEEP(*(.intvectors))
        __intvectors_end = .;
    } :irq

    .text 0x80000400 : {
        __text_start = .;
        *(.boot)
        . = ALIGN(16);
        "*libdragon*.a:"(.text .text.*)
        "*libdragonsys.a:"(.text .text.*)
        "*libc.a:"(.text .text.*)
        "*libg.a:"(.text .text.*)
        "*libm.a:"(.text .text.*)
        "*libgcc.a:"(.text .text.*)
        "*libstdc++.a:"(.text .text.*)
        "*libsupc++.a:"(.text .text.*)
        *crt*.o(.text .text.*)
        *n64_pager.c.obj(.text .text.*)
        *n64_libgcc.c.obj(.text .text.*)
        *n64_vheap.cpp.obj(.text .text.*)
        *n64_throwlog.cpp.obj(.text .text.*)
        *n64_eh_sext.S.obj(.text .text.*)
        /* libstdc++'s weak template functions, wherever they were emitted
           (generated into the build directory by CMakeLists.txt) */
        INCLUDE n64_resident_weak.ld
        KEEP(*(keep.text.*))
        *(.init)
        *(.fini)
        . = ALIGN(16);
        __text_end  = .;
    } :main

    .jcr : { KEEP (*(.jcr)) } :main

    .rodata : {
        "*libdragon*.a:"(.rdata .rodata .rodata.*)
        "*libdragonsys.a:"(.rdata .rodata .rodata.*)
        "*libc.a:"(.rdata .rodata .rodata.*)
        "*libg.a:"(.rdata .rodata .rodata.*)
        "*libm.a:"(.rdata .rodata .rodata.*)
        "*libgcc.a:"(.rdata .rodata .rodata.*)
        "*libstdc++.a:"(.rdata .rodata .rodata.*)
        "*libsupc++.a:"(.rdata .rodata .rodata.*)
        *crt*.o(.rdata .rodata .rodata.*)
        *n64_pager.c.obj(.rdata .rodata .rodata.*)
        *n64_libgcc.c.obj(.rdata .rodata .rodata.*)
        *n64_vheap.cpp.obj(.rdata .rodata .rodata.*)
        *n64_throwlog.cpp.obj(.rdata .rodata .rodata.*)
        KEEP(*(keep.rodata.*))
        . = ALIGN(8);
    } :main

    . = ALIGN(4);
    /* Same layout as GNU ld's default script. libdragon's n64.ld keeps only
       plain .ctors, so prioritised constructors (.ctors.NNNNN) -- including
       libstdc++'s initialisation of std::cout/std::cerr -- were discarded and
       never ran. libdragon runs this list from the end backwards, so the
       sorted priority constructors (placed last) run first, as intended. */
    .ctors : {
        __CTOR_LIST__ = .;
        KEEP (*crtbegin.o(.ctors))
        KEEP (*crtbegin?.o(.ctors))
        KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o) .ctors))
        KEEP (*(SORT(.ctors.*)))
        KEEP (*(.ctors))
        __CTOR_END__ = .;
    } :main

    . = ALIGN(8);
    .data : {
        __data_start = .;
        *(.data)
        *(.data.*)
        KEEP(*(keep.data.*))
        *(.gnu.linkonce.d.*)
        . = ALIGN(8);
    } :main

    /* Thread-local storage template. libdragon has no TLS runtime; the pager
       sets up one TLS block at boot and emulates rdhwr $29 (see n64_pager.c). */
    .tdata : {
        __tdata_start = .;
        *(.tdata .tdata.* .gnu.linkonce.td.*)
        __tdata_end = .;
    } :main
    .tbss : {
        __tbss_start = .;
        *(.tbss .tbss.* .gnu.linkonce.tb.*)
        *(.tcommon)
        __tbss_end = .;
    } :main

    .sdata : {
        _gp = . + 0x8000;
        *(.sdata)
        *(.sdata.*)
        *(.gnu.linkonce.s.*)
        . = ALIGN(8);
    } :main
    .lit8 : { *(.lit8) . = ALIGN(8); } :main
    .lit4 : { *(.lit4) . = ALIGN(8); } :main

    . = ALIGN(8);
    __data_end = .;
    . = ALIGN(8);
    __rom_end = .;

    .sbss : {
         __bss_start = .;
        *(.sbss)
        *(.sbss.*)
        *(.gnu.linkonce.sb.*)
        *(.scommon)
        *(.scommon.*)
    } :main
    . = ALIGN(8);
    .bss : {
        *(.bss)
        *(.bss*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(8);
         __bss_end = .;
    } :main
    . = ALIGN(8);
    end = .;

    /* ---------------- demand-paged image (ROM-resident) ---------------- */
    .paged 0xC0000000 : {
        __paged_start = .;
        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t.*)
        . = ALIGN(16);
        *(.rdata)
        *(.rodata)
        *(.rodata.*)
        *(.gnu.linkonce.r.*)
        . = ALIGN(16);
        *(.gcc_except_table*)
        . = ALIGN(16);
        __EH_FRAME_BEGIN__ = .;
        KEEP (*(.eh_frame))
        LONG(0)
        . = ALIGN(16384);
        __paged_end = .;
    } :paged

    /DISCARD/ : { *(.eh_frame_hdr) *(.rel.dyn) }
}

/* Resident image: 8 MiB minus stack and low memory, as in libdragon's n64.ld.
   The frame pool and heap come out of what remains. */
ASSERT(end - __text_start <= 8*1024*1024 - 64*1024 - 1024, "resident image too big for N64 memory")
