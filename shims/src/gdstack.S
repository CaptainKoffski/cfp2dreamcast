/* gdc_call(r4=cmd, r5=param, r6=superfn, r7=func) -> r0
 *
 * Calls the live GD syscall vector [0x8c0000bc] on a PRIVATE stack in shim
 * RAM (GD_STACK_TOP, shim_iface.h). Why: DreamShell isoldr's syscall emu
 * runs its whole read path (FatFs + SPI bit-bang + a coroutine that parks
 * continuation frames) on the CALLER's stack. The game's Naomi stack is
 * ~2 KB (floor 0x8c00e6e8, boot-binary.md §3) -- deep enough for the real
 * BIOS's shallow GD driver (18 green GDEMU rounds), not for isoldr; on a
 * serial-SD boot the emu + any game ISR that fires mid-read dive below the
 * game stack into live game data. Swapping r15 here puts that depth in shim
 * RAM for every backend; the real BIOS doesn't care what SP it runs on.
 *
 * ABI: r8-r14 are callee-saved by the C target either way; only pr and r15
 * need manual care. Not reentrant (single static SP save) -- fine: every
 * GDC call sits sequentially in the game main thread, nothing else in the
 * shim calls GD. The vector is re-read per call (live deref, matching the
 * old GDC macro). */
#include "shim_iface.h"

    .section .text
    .globl  _gdc_call
    .align  2
_gdc_call:
    sts.l   pr, @-r15
    mov.l   .Lsp_save_k, r0
    mov.l   r15, @r0            /* stash caller SP (after pr push) */
    mov.l   .Lstack_top_k, r15  /* switch to the private GD stack */
    mov.l   .Lvec_k, r0
    mov.l   @r0, r0             /* live syscall vector */
    jsr     @r0
    nop
    mov.l   .Lsp_save_k, r1
    mov.l   @r1, r15            /* back to the caller stack */
    lds.l   @r15+, pr
    rts
    nop

    .align  2
.Lsp_save_k:   .long _gdc_saved_sp
.Lstack_top_k: .long GD_STACK_TOP
.Lvec_k:       .long 0x8c0000bc

    .section .data
    .globl  _gdc_saved_sp
    .align  2
_gdc_saved_sp: .long 0
