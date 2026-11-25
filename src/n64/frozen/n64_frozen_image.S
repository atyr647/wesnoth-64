/*
 * The baked WML image (tools/n64-bake), in the ROM's paged read-only data:
 * its pages are read from the cartridge on demand and never go to swap.
 */
    .section .rodata.n64_frozen_image, "a"
    .balign 16
    .globl n64_frozen_image_start
n64_frozen_image_start:
    .incbin N64_FROZEN_IMAGE
    .globl n64_frozen_image_end
n64_frozen_image_end:
    .balign 16
