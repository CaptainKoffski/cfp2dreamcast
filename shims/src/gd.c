/* GD-ROM via DC BIOS syscall vector 0x8c0000bc. Polling only, no IRQs.
 * PIOREAD, not DMAREAD: no G1-DMA side effects in game context (the game's own
 * G1 regs are mirrored; the BIOS PIO path uses the ATA task-file regs instead).
 *
 * Constants + ABI cross-checked against KOS (tools/kos):
 *   vector 0x8c0000bc = VEC_MISC_GDROM  (kernel/arch/dreamcast/hardware/syscalls.c:26)
 *   call ABI syscall(r4,r5,r6=superfn,r7=func); SUPER_FUNC_GDROM=0 (syscalls.c:55,
 *     macros :98-116) -> r6=0 always here
 *   func r7: SEND_COMMAND=0, CHECK_COMMAND=1, EXEC_SERVER=2 (syscalls.c:62-64)
 *   CMD_PIOREAD=16 (include/dc/syscalls.h:256)
 *   param block {start_sec,num_sec,buffer,is_test} (include/dc/syscalls.h:290-295)
 *   status: FAILED=-1, NOT_FOUND=0, PROCESSING=1, COMPLETED=2, STREAMING=3,
 *     BUSY=4 (include/dc/syscalls.h:439-444). done = COMPLETED only; NOT_FOUND
 *     and <=FAILED are errors -- matches KOS's read path (cdrom.c:124-132 done
 *     predicate + :197-198 which maps a bare NOT_FOUND to ERR_NO_ACTIVE).
 *     safe: COMPLETE precedes any NOT_FOUND on both flycast and real BIOS.
 * ponytail: PIO read speed is fine under emulation; DMAREAD is the Phase 5
 * upgrade path if real-hardware streaming stutters (it would then need a
 * dcache_inval on the dest, see cart.c cart_read). */
#include "shim_iface.h"
typedef unsigned int u32;
typedef int (*gdc_t)(u32, u32, u32, u32);

#define GDC       ((gdc_t)(*(volatile u32 *)0x8c0000bc))
#define CMD_PIOREAD   16
#define CMD_DMAREAD   17  /* G1-DMA read (SHIM_GD_DMA); KOS CD_CMD_DMAREAD */
#define CMD_INIT      24  /* drive re-init (KOS cdrom_reinit path) */
#define GD_SEND       0   /* r7 func codes (superfn r6 = 0) */
#define GD_CHECK      1
#define GD_EXEC       2   /* exec-server / drive main loop */
#define GD_NOT_FOUND  0
#define GD_COMPLETED  2
#define GD_FAILED    -1

void shim_die(u32, u32, u32);

/* Raw CHECK status word of the last hard failure -- painted by cart.c's death
 * screen so the TV shows the BIOS's actual verdict, not our -2. .data nonzero
 * init per house style. */
u32 gd_last_err = 0xcafe0000;

/* HW round 10: the first in-game stream fails with CHECK error on a sector the
 * loader-context rehearsal read fine -- the game's boot path (running further
 * than ever post-MMU-fix) has plausibly poked an unmirrored 0x5f7xxx literal,
 * i.e. the REAL GD drive's registers on a DC. KOS's driver recovers exactly
 * this class by re-initializing the drive and retrying; do the same between
 * attempts. If the stray poke is one-time (boot path), one reinit heals the
 * drive for the rest of the session. */
static void gd_init_drive(void) {
    u32 stat[4], guard = 0;
    int req = GDC((u32)CMD_INIT, 0, 0, GD_SEND);
    if (req <= 0) return;
    for (;;) {
        GDC(0, 0, 0, GD_EXEC);
        int s = GDC((u32)req, (u32)stat, 0, GD_CHECK);
        if (s == GD_COMPLETED || s <= GD_FAILED) return;
        if (s == GD_NOT_FOUND && guard > 1000000u) return;
        if (++guard > 100000000u) shim_die(5, 0xdead1217, 0);
    }
}

/* HW round 11: death screen showed b=0xcafe0000 (sentinel untouched) = every
 * attempt died at SEND (req<=0) -- the BIOS refuses to even enqueue. Root
 * cause fits the RAM collision: the DC BIOS keeps its GD-syscall state in low
 * work RAM (0x8c000xxx) which a Naomi game rightfully claims for itself (its
 * VBR table is at 0x8c00f400, stack at 0x8c00exxx) -- the game tramples the
 * BIOS state, the queue plays dead. Recovery: syscall r7=3 = gdGdcInitSystem
 * (KOS syscalls.c FUNC_GDROM_INIT) rebuilds that state from scratch; callable
 * even with the queue wedged (it's a direct entry, not a queued command). */
#define GD_SYSINIT 3
static void gd_sys_reinit(void) { GDC(0, 0, 0, GD_SYSINIT); }

int gd_read_sectors(void *dst, u32 fad, u32 n) {
    /* Real-HW round 9: first in-game stream died red with an instant error.
     * Two hardening changes vs the loader-rehearsed happy path:
     *   - NOT_FOUND is tolerated while polling (up to 1M pumps): in game
     *     context the BIOS command queue may briefly report not-found before
     *     the server picks the command up; KOS's driver retries, we froze.
     *     (COMPLETED is still observed first when the read succeeds -- we
     *     poll every iteration, so completion cannot be missed.)
     *   - the whole read is retried 3x before reporting failure (KOS
     *     re-inits+retries on real drives; reads are idempotent). */
    for (u32 attempt = 0; attempt < 4; attempt++) {
        if (attempt) { gd_sys_reinit(); gd_init_drive(); }  /* rebuild state, then drive */
        u32 param[4], stat[4], guard = 0;
        param[0] = fad; param[1] = n; param[2] = (u32)dst; param[3] = 0;
        int req = GDC((u32)CMD_PIOREAD, (u32)param, 0, GD_SEND);
        if (req <= 0) { gd_last_err = 0xcafe0002u; continue; }  /* send refused: recover+retry */
        for (;;) {
            GDC(0, 0, 0, GD_EXEC);           /* pump the drive state machine */
            int s = GDC((u32)req, (u32)stat, 0, GD_CHECK);
            if (s == GD_COMPLETED) return 0;
            if (s <= GD_FAILED) { gd_last_err = stat[0]; break; } /* hard error: retry */
            if (s == GD_NOT_FOUND && guard > 1000000u) { gd_last_err = 0xcafe0001; break; }
            /* PROCESSING(1)/STREAMING(3)/BUSY(4)/early NOT_FOUND: keep polling.
             * Real-HW hang detector: ~100M pumps >> any PIO read; Flycast
             * completes in a few. Fires blue instead of a silent black hang. */
            if (++guard > 100000000u) shim_die(5, fad, n);
        }
    }
    return -2;
}

#if SHIM_GD_DMA
/* Invalidate the D-cache over [phys, phys+len) (SH-4 cache line = 32 B) BEFORE a
 * DMA read: OCBI discards any lines (incl. dirty) so (a) no stale write-back can
 * land on top of the incoming DMA, and (b) later reads fetch fresh from RAM. The
 * shim is freestanding (no KOS dcache_inval_range), so hit the P1 cached alias of
 * the physical dest directly. Range is line-aligned (callers DMA only 32-aligned
 * dests, whole 2048-B sectors). */
static void dcache_inval(u32 phys, u32 len) {
    u32 a = (phys & 0x1fffffffu) | 0x80000000u, end = a + len;
    for (a &= ~31u; a < end; a += 32u)
        __asm__ volatile ("ocbi @%0" :: "r"(a));
}

/* DMA variant of gd_read_sectors: CD_CMD_DMAREAD to a 32-byte-aligned PHYSICAL
 * dest -- the G1-DMA engine writes RAM directly, no per-word CPU cost. Same
 * polled EXEC/CHECK completion + retry/reinit hardening as the PIO path (the
 * shim drives GD without IRQs, unlike KOS's semaphore path). The BIOS uses the
 * REAL G1 regs 0x5f74xx; the game only ever touches the mirror (the 13 pool
 * patches), so no collision. */
int gd_read_sectors_dma(u32 phys, u32 fad, u32 n) {
    phys &= 0x1fffffffu;                    /* physical for the G1-DMA engine */
    /* Mask the real GD-DMA-complete IRQ (SB_ISTNRM bit 14) from all three ASIC
     * IRQ levels (IRQD/IRQB/IRQ9 @ 0xa05f6910/20/30). The game runs its cart DMA
     * mirrored + polled and never needs a real GD-DMA interrupt, but its Naomi-
     * legacy handler is still armed -- our real G1-DMA fires it and the game
     * mishandles it -> hang after a few reads (exactly the "G1-DMA side effect"
     * this path was written to avoid). HW-CONFIRMED: masking bit 14 is what makes
     * DMA cart streaming work. Clear only bit 14 (vblank etc. untouched);
     * idempotent per call in case the game re-arms it. */
    *(volatile u32 *)0xa05f6910 &= ~(1u << 14);
    *(volatile u32 *)0xa05f6920 &= ~(1u << 14);
    *(volatile u32 *)0xa05f6930 &= ~(1u << 14);
    for (u32 attempt = 0; attempt < 4; attempt++) {
        if (attempt) { gd_sys_reinit(); gd_init_drive(); }
        dcache_inval(phys, n * 2048u);
        u32 param[4], stat[4], guard = 0;
        param[0] = fad; param[1] = n; param[2] = phys; param[3] = 0;
        int req = GDC((u32)CMD_DMAREAD, (u32)param, 0, GD_SEND);
        if (req <= 0) { gd_last_err = 0xcafe0002u; continue; }
        for (;;) {
            GDC(0, 0, 0, GD_EXEC);          /* pump the drive + DMA state machine */
            int s = GDC((u32)req, (u32)stat, 0, GD_CHECK);
            if (s == GD_COMPLETED) {
                *(volatile u32 *)0xa05f6900 = (1u << 14);   /* ack the GD-DMA status */
                return 0;
            }
            if (s <= GD_FAILED) { gd_last_err = stat[0]; break; }
            if (s == GD_NOT_FOUND && guard > 1000000u) { gd_last_err = 0xcafe0001; break; }
            if (++guard > 100000000u) shim_die(5, fad, n);
        }
    }
    return -2;
}
#endif /* SHIM_GD_DMA */
