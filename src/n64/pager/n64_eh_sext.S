/*
 * n64_eh_sext.S -- sign-extend the exception pointer handed to the C++
 * runtime by landing pads (resident; linked with --wrap).
 *
 * libdragon runs the VR4300 with 64-bit kernel addressing (SR.KX=1): a kernel
 * pointer must be held sign-extended (0xFFFFFFFF8xxxxxxx). Landing pads can
 * receive the exception object zero-extended from libgcc's unwinder (see
 * tools/patch-n64-libgcc.sh), and every use of it would fault. The two
 * runtime entry points a landing pad passes it to are fixed up here:
 *   _Unwind_Resume     (end of a cleanup)
 *   __cxa_begin_catch  (start of a catch clause)
 * n64_eh_zero_extended counts the calls that needed it.
 */
    .set noreorder
    .set noat
    .text

    .globl n64_eh_zero_extended
    .section .bss.n64_eh_zero_extended,"aw",@nobits
    .align 2
n64_eh_zero_extended:
    .space 4
    .text

.macro SEXT_A0 real
    dsll32  $t0, $a0, 0
    dsra32  $t0, $t0, 0          /* t0 = sign-extended low word of a0 */
    beq     $t0, $a0, 1f
    nop
    lui     $t1, %hi(n64_eh_zero_extended)
    lw      $t2, %lo(n64_eh_zero_extended)($t1)
    addiu   $t2, $t2, 1
    sw      $t2, %lo(n64_eh_zero_extended)($t1)
1:
    move    $a0, $t0
    lui     $t9, %hi(\real)
    addiu   $t9, $t9, %lo(\real)
    jr      $t9
    nop
.endm

    .globl __wrap__Unwind_Resume
    .type __wrap__Unwind_Resume, @function
__wrap__Unwind_Resume:
    SEXT_A0 __real__Unwind_Resume
    .size __wrap__Unwind_Resume, .-__wrap__Unwind_Resume

    .globl __wrap___cxa_begin_catch
    .type __wrap___cxa_begin_catch, @function
__wrap___cxa_begin_catch:
    SEXT_A0 __real___cxa_begin_catch
    .size __wrap___cxa_begin_catch, .-__wrap___cxa_begin_catch
