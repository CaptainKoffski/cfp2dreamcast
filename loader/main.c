#include <kos.h>
#include "shim_iface.h"
#include "patch_table.h"        /* generated (build/), gitignored */

extern uint8 shim_bin[];        /* objcopy-embedded shim.bin, see Makefile */
extern uint8 shim_bin_end[];
extern void handoff(uint32 src, uint32 dst, uint32 len, uint32 entry);
extern uint8 handoff_end[];     /* end-of-stub label in handoff.S (stub is PIC) */

#define GAME_SECTORS   (GAME_LEN / 2048)   /* 512 */

/* PIC handoff stub runs here: outside the game copy target [0x8c020000,0x8c120000)
 * (else it overwrites itself mid-copy), above the loader + KOS heap, below the KOS
 * stack top (0x8d000000), just past staging-end. The runtime SP is printed at start
 * so the collision assumption is verified, not assumed. */
#define HANDOFF_SCRATCH  0x8ce00000u

static void halt(const char *msg) {
    dbglog(DBG_INFO, "%s", msg);
    for(;;) thd_sleep(1000);
}

static int apply_patches(uint8 *img) {
    for (unsigned i = 0; i < CLEO_NPATCHES; i++) {
        const patch_t *p = &cleo_patches[i];
        uint8 *at = img + (p->addr - GAME_LOAD_ADDR);
        if (memcmp(at, p->old, p->len)) {
            dbglog(DBG_INFO, "PATCH MISMATCH %s @%08lx\n", p->what, (unsigned long)p->addr);
            return -1;
        }
        memcpy(at, p->neu, p->len);
        dbglog(DBG_INFO, "patched %s @%08lx (%lu)\n", p->what,
               (unsigned long)p->addr, (unsigned long)p->len);
    }
    return 0;
}

int main(void) {
    dbglog(DBG_INFO, "CLEO LOADER M2\n");

    /* KOS-stack-collision probe: &probe ~= current SP. Shim region tops out at
     * SHIM_BOUNCE+2048 = SHIM_BASE+0xa800; the loader only writes up to
     * G1_MIRROR+0x800 (SHIM_BASE+0x9000). Safe iff SP stays clear of both. */
    volatile int probe = 0;
    dbglog(DBG_INFO, "SP~%08lx memtop=%08lx shim=%08x..%08x scratch=%08x\n",
           (unsigned long)&probe, (unsigned long)_arch_mem_top,
           (unsigned)SHIM_BASE, (unsigned)(SHIM_BASE + 0xa800),
           (unsigned)HANDOFF_SCRATCH);

    cdrom_reinit();             /* inits the GD subsystem the shim's BIOS syscalls reuse */
    uint8 *stage = (uint8 *)STAGING_ADDR;
    if (cdrom_read_sectors(stage, CART_FAD, GAME_SECTORS) != ERR_OK)
        halt("read fail\n");
    if (memcmp(stage, "NAOMI", 5))
        halt("bad image\n");

    if (apply_patches(stage))   /* verify old bytes then patch; abort on mismatch */
        halt("patch abort\n");

    uint32 shim_len = (uint32)(shim_bin_end - shim_bin);
    memcpy((void *)SHIM_BASE, shim_bin, shim_len);

    /* Zero the G1 mirror block (uncached P2 -- how the patched game + shim access it)
     * so config-time SB_GDST pollers don't spin on stale RAM before the first DMA. */
    volatile uint32 *mir = (volatile uint32 *)P2ADDR(G1_MIRROR);
    for (unsigned i = 0; i < 0x800 / 4; i++) mir[i] = 0;

    /* Relocate the PIC handoff stub out of the copy target and flush it to RAM. */
    uint32 ho_len = (uint32)((uint8 *)handoff_end - (uint8 *)handoff);
    memcpy((void *)HANDOFF_SCRATCH, (void *)handoff, ho_len);

    /* Write-back the CPU stores (patched image, shim code, stub) to RAM: handoff
     * reads staging via P2 and the game/shim read the shim region freshly cached. */
    dcache_purge_range(STAGING_ADDR, GAME_LEN);
    dcache_purge_range(SHIM_BASE, shim_len);
    dcache_purge_range(HANDOFF_SCRATCH, ho_len);

    dbglog(DBG_INFO, "jumping to %08x\n", GAME_ENTRY);
    irq_disable();

    void (*ho)(uint32, uint32, uint32, uint32) =
        (void *)P2ADDR(HANDOFF_SCRATCH);           /* run the stub uncached */
    ho(P2ADDR(STAGING_ADDR), P2ADDR(GAME_LOAD_ADDR), GAME_LEN, GAME_ENTRY);
    return 0; /* unreachable */
}
