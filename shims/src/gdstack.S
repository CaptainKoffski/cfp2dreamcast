/* gdc_call(r4=cmd, r5=param, r6=superfn, r7=func) -> r0
 *
 * Calls the live GD syscall vector [0x8c0000bc] with the caller's execution
 * environment SANITIZED, for backends that -- unlike the real BIOS GD
 * driver -- assume a KOS-like world (DreamShell isoldr):
 *
 * 1. PRIVATE STACK: isoldr runs its whole read path (FatFs + SPI bit-bang +
 *    a coroutine that parks continuation frames) on the CALLER's stack. The
 *    game's Naomi stack is ~2 KB (floor 0x8c00e6e8, boot-binary.md §3) --
 *    deep enough for the real BIOS's shallow driver (18 green GDEMU
 *    rounds), not for isoldr. Swap r15 to GD_STACK_TOP (shim_iface.h).
 *
 * 2. FPU QUARANTINE: isoldr links KOS memcpy.S, whose big-block path
 *    (kos/src/memcpy.S:499-560 in the dreamshell tree) clobbers dr0-dr14
 *    AND xd0-xd14 -- both FPU banks, only fr12-15 saved -- and switches
 *    FPSCR to paired precision assuming the KOS baseline. The real BIOS GD
 *    driver is integer-only, so GDEMU never exposed this. Save the game's
 *    complete FPU context, run the syscall under the KOS-default FPSCR the
 *    emu was written against, restore everything on the way out.
 *    Assumes SR.FD=0 at the call site (the game is an FPU user and calls
 *    from its main thread; with FD=1 isoldr's own memcpy would trap first).
 *
 * ABI: r8-r14 are callee-saved by the C target either way; pr/r15/FPU are
 * handled manually here. Not reentrant (single static SP save) -- fine:
 * every GDC call sits sequentially in the game main thread, nothing else in
 * the shim calls GD. The vector is re-read per call (live deref, matching
 * the old GDC macro). */
#include "shim_iface.h"

    .section .text
    .globl  _gdc_call
    .align  2
_gdc_call:
    sts.l   pr, @-r15
    mov.l   .Lsp_save_k, r0
    mov.l   r15, @r0            /* stash caller SP (after pr push) */
    mov.l   .Lstack_top_k, r15  /* switch to the private GD stack */

    sts.l   fpscr, @-r15        /* game's FPSCR (incl. FR bank-select bit) */
    sts.l   fpul, @-r15
    mov.l   .Lfpscr_kos_k, r1
    lds     r1, fpscr           /* SZ=0 PR=0 FR=0: single moves, bank 0 front */
    fmov.s  fr0,  @-r15         /* physical bank 0 */
    fmov.s  fr1,  @-r15
    fmov.s  fr2,  @-r15
    fmov.s  fr3,  @-r15
    fmov.s  fr4,  @-r15
    fmov.s  fr5,  @-r15
    fmov.s  fr6,  @-r15
    fmov.s  fr7,  @-r15
    fmov.s  fr8,  @-r15
    fmov.s  fr9,  @-r15
    fmov.s  fr10, @-r15
    fmov.s  fr11, @-r15
    fmov.s  fr12, @-r15
    fmov.s  fr13, @-r15
    fmov.s  fr14, @-r15
    fmov.s  fr15, @-r15
    frchg                       /* front = physical bank 1 */
    fmov.s  fr0,  @-r15
    fmov.s  fr1,  @-r15
    fmov.s  fr2,  @-r15
    fmov.s  fr3,  @-r15
    fmov.s  fr4,  @-r15
    fmov.s  fr5,  @-r15
    fmov.s  fr6,  @-r15
    fmov.s  fr7,  @-r15
    fmov.s  fr8,  @-r15
    fmov.s  fr9,  @-r15
    fmov.s  fr10, @-r15
    fmov.s  fr11, @-r15
    fmov.s  fr12, @-r15
    fmov.s  fr13, @-r15
    fmov.s  fr14, @-r15
    fmov.s  fr15, @-r15
    frchg                       /* back to bank 0 for the call */

    /* MMU off DURING the syscall, game's MMUCR restored after (round 13).
     * The old permanent clear ("games assume AT=0 forever") was wrong for
     * this game: it runs AT=1 and maps RAM through the store-queue window
     * via UTLB entries (SQ-mapper 0x8c0311a4 -- gated on MMUCR.AT).  The
     * permanent clear left AT=0 after the last load syscall, so the
     * load->title transition's SQ-emitted display-list closers fell back to
     * QACR mapping (area 0 -- writes vanish, no fault): buffer stayed zero,
     * the transfer queue pinned waiting for the modifier-list-end event =
     * the 18-round "hang at bar 100%".  Clearing only AROUND the call keeps
     * isoldr in the AT=0 world it needs (rounds 2-5) while the game's
     * SQ translation survives; a plain 0-store leaves TI=0 so the game's
     * UTLB entries are NOT invalidated and AT restore fully revives them. */
    /* ...but ONLY for isoldr (2026-08-18 flycast 0.6.0 regression). The
     * real BIOS driver never needed AT=0 -- 18 GDEMU-green rounds and the
     * whole 0.5.0 era called it with the game's AT=1 intact -- and on a
     * BIOS-style boot this game runs MMU-ON from EARLY boot (round 14), so
     * an unconditional clear/restore toggles MMUCR.AT twice per syscall.
     * Real silicon shrugs; flycast wipes its entire dynarec block cache on
     * every AT flip (ccn.cpp CCN_MMUCR_write: AT change -> ResetCache) --
     * thousands of full retranslations across the boot preload = the
     * "load bar crawls / never starts / Windows breakpoint" 0.6.0 reports
     * on vanilla flycast with a real BIOS. Backend probe: isoldr redirects
     * [0x8c0000bc] into its own resident image (gdc_syscall.s gdc_redir,
     * Memory placement >= 0x8c010000); the real BIOS and flycast's reios
     * keep their GD entry inside the 64 KB syscall work RAM below that.
     * Probe per call (cheap), decision latched for the restore side. */
    mov.l   .Lvec_k, r1
    mov.l   @r1, r1             /* GD entry the vector holds */
    mov.l   .Lphysmask_k, r2
    and     r2, r1
    mov.l   .Lbios_top_k, r2
    cmp/hs  r2, r1              /* T = RAM offset >= 0x10000 -> isoldr
                                 * (real BIOS: 0x8c001000, reios: 0x8c001006
                                 * -- measured, MMUCRWR vecbc watch) */
    movt    r3
    mov.l   .Lmmu_gate_k, r2
    mov.l   r3, @r2
    tst     r3, r3
    bt      .Lmmu_skip_clear
    mov.l   .Lmmucr_k, r1
    mov.l   @r1, r2             /* game's MMUCR (AT=1: SQ-via-TLB) */
    mov.l   .Lmmucr_save_k, r3
    mov.l   r2, @r3
    mov     #0, r2
    mov.l   r2, @r1
.Lmmu_skip_clear:

    mov.l   .Lvec_k, r0
    mov.l   @r0, r0             /* live syscall vector */
    jsr     @r0
    nop

    mov.l   .Lmmu_gate_k, r1    /* r0 = result -- keep it */
    mov.l   @r1, r1
    tst     r1, r1
    bt      .Lmmu_skip_restore  /* non-isoldr: MMUCR never touched */
    mov.l   .Lmmucr_save_k, r3  /* restore the game's MMUCR */
    mov.l   @r3, r2
    mov.l   .Lmmucr_k, r1
    mov.l   r2, @r1
.Lmmu_skip_restore:

    mov.l   .Lfpscr_kos_k, r1   /* known move width/bank for the restore */
    lds     r1, fpscr
    frchg                       /* front = physical bank 1 */
    fmov.s  @r15+, fr15
    fmov.s  @r15+, fr14
    fmov.s  @r15+, fr13
    fmov.s  @r15+, fr12
    fmov.s  @r15+, fr11
    fmov.s  @r15+, fr10
    fmov.s  @r15+, fr9
    fmov.s  @r15+, fr8
    fmov.s  @r15+, fr7
    fmov.s  @r15+, fr6
    fmov.s  @r15+, fr5
    fmov.s  @r15+, fr4
    fmov.s  @r15+, fr3
    fmov.s  @r15+, fr2
    fmov.s  @r15+, fr1
    fmov.s  @r15+, fr0
    frchg                       /* front = physical bank 0 */
    fmov.s  @r15+, fr15
    fmov.s  @r15+, fr14
    fmov.s  @r15+, fr13
    fmov.s  @r15+, fr12
    fmov.s  @r15+, fr11
    fmov.s  @r15+, fr10
    fmov.s  @r15+, fr9
    fmov.s  @r15+, fr8
    fmov.s  @r15+, fr7
    fmov.s  @r15+, fr6
    fmov.s  @r15+, fr5
    fmov.s  @r15+, fr4
    fmov.s  @r15+, fr3
    fmov.s  @r15+, fr2
    fmov.s  @r15+, fr1
    fmov.s  @r15+, fr0
    lds.l   @r15+, fpul
    lds.l   @r15+, fpscr        /* game's original FPSCR (restores its FR too) */

    mov.l   .Lsp_save_k, r1
    mov.l   @r1, r15            /* back to the caller stack */
    lds.l   @r15+, pr
    rts
    nop

    .align  2
.Lsp_save_k:    .long _gdc_saved_sp
.Lstack_top_k:  .long GD_STACK_TOP
.Lvec_k:        .long 0x8c0000bc
.Lmmucr_k:      .long 0xff000010
.Lmmucr_save_k: .long _gdc_saved_mmucr
.Lmmu_gate_k:   .long _gdc_mmu_gate
.Lphysmask_k:   .long 0x00ffffff    /* RAM offset (16 MB), drops the 0x0c base */
.Lbios_top_k:   .long 0x00010000
.Lfpscr_kos_k:  .long 0x00040001    /* KOS default: DN=1, RM=chop, SZ=PR=FR=0 */

    .section .data
    .globl  _gdc_saved_sp
    .align  2
_gdc_saved_sp:  .long 0
_gdc_saved_mmucr: .long 0
_gdc_mmu_gate:  .long 0             /* 1 = isoldr backend: AT=0 window applied */
