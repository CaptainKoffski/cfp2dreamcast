#include <kos.h>
#include <arch/timer.h>         /* arch_timer_gettime() -- load-stage timing */
#include "shim_iface.h"
#include "patch_table.h"        /* generated (build/), gitignored */

extern uint8 shim_bin[];        /* objcopy-embedded shim.bin, see Makefile */
extern uint8 shim_bin_end[];
extern uint8 bios_data[];       /* objcopy-embedded [0x7000 @0x60000][0x70 @0x1ffd00] */
extern void handoff(uint32 src, uint32 dst, uint32 len, uint32 entry);
extern uint8 handoff_end[];     /* end-of-stub label in handoff.S (stub is PIC) */

#define GAME_SECTORS   (GAME_LEN / 2048)   /* 512 */

/* PIC handoff stub runs here: outside the game copy target [0x8c020000,0x8c120000)
 * (else it overwrites itself mid-copy), above the loader + KOS heap, below the KOS
 * stack top (0x8d000000), just past staging-end. The runtime SP is printed at start
 * so the collision assumption is verified, not assumed. */
#define HANDOFF_SCRATCH  0x8ce00000u

/* Real-HW visibility: serial is invisible on a TV, so every stage is drawn to
 * the framebuffer (KOS init already set 640x480). A stuck screen names the
 * stage that hung; halt() turns the screen red with the reason. */

/* Boot-proven since round B5 -> the stage text is now hidden behind the Naomi
 * BIOS splash (LOADER_QUIET=1). Flip to 0 for the on-TV breadcrumbs if boot
 * ever regresses; halt() failure screens stay verbose either way. */
#define LOADER_QUIET 1
extern uint8 splash_bin[];      /* objcopy-embedded 640x480 RGB565 (Makefile) */

static int say_row = 0;
static void say(const char *s) {
    dbglog(DBG_INFO, "%s\n", s);
    if (LOADER_QUIET) return;
    bfont_draw_str(vram_s + (40 + say_row * 26) * 640 + 20, 640, 1, s);
    say_row++;
}

static void halt(const char *msg) {
    dbglog(DBG_INFO, "%s", msg);
    for (int i = 0; i < 640 * 480; i++) vram_s[i] = 0xf800;   /* red */
    bfont_draw_str(vram_s + 100 * 640 + 20, 640, 1, msg);
    for(;;) thd_sleep(1000);
}

/* ---- load-stage timing (LOADER_TIMING) ---------------------------------
 * Answers "where does the black-screen load time actually go" on REAL HW --
 * Flycast can't: its virtual GD-ROM returns instantly (the 1 MB cart read is
 * PIO, cdrom.c CD_CMD_PIOREAD) and its serial drains instantly (the patch
 * stage's real cost is its 34 dbglog lines at 115200 baud, ~87us/char, not
 * the memcpy). Each mark is one arch_timer_gettime() (80ns tick) + two stores
 * -- captured into RAM, NO serial -- so the instrument doesn't pollute the
 * serial cost it measures. The breakdown is painted to the framebuffer (read
 * off the TV, like every prior HUD) and paused, not printed to serial.
 * Compile-time flag like LOADER_QUIET; set 0 to remove entirely. */
#define LOADER_TIMING 1
#if LOADER_TIMING
static uint64 tmk_us[16];
static const char *tmk_lbl[16];
static int tmk_n;
static void tmark(const char *lbl) {
    struct timespec t = arch_timer_gettime();
    if (tmk_n >= 16) return;
    tmk_us[tmk_n]  = (uint64)t.tv_sec * 1000000ull + (uint64)t.tv_nsec / 1000ull;
    tmk_lbl[tmk_n] = lbl;
    tmk_n++;
}
static void tmark_dump(void) {
    char line[48];
    for (int i = 1; i < tmk_n; i++) {
        uint64 d = tmk_us[i] - tmk_us[i - 1];
        snprintf(line, sizeof line, "%-16s %4lu.%03lu ms", tmk_lbl[i],
                 (unsigned long)(d / 1000), (unsigned long)(d % 1000));
        dbglog(DBG_INFO, "TIMING %s\n", line);
        bfont_draw_str(vram_s + (180 + i * 26) * 640 + 20, 640, 1, line);
    }
    uint64 tot = tmk_us[tmk_n - 1] - tmk_us[0];
    snprintf(line, sizeof line, "%-16s %4lu.%03lu ms", "TOTAL",
             (unsigned long)(tot / 1000), (unsigned long)(tot % 1000));
    dbglog(DBG_INFO, "TIMING %s\n", line);
    bfont_draw_str(vram_s + (180 + tmk_n * 26) * 640 + 20, 640, 1, line);
    thd_sleep(8000);   /* read/photograph window; whole block gated by LOADER_TIMING */
}
#else
#define tmark(x)     ((void)0)
#define tmark_dump() ((void)0)
#endif

/* Rehearse the shim's exact GD path (BIOS syscall vector 0x8c0000bc, polled
 * PIO — see shims/src/gd.c) before handing the game to it. Flycast HLEs these
 * calls statelessly; the real BIOS runs a state machine in low RAM, so this is
 * the first place a real-HW divergence would show. Bounded poll: a hang here
 * halts with its own message instead of a silent black screen. */
typedef int (*gdc_t)(uint32, uint32, uint32, uint32);
static int sysrd_test(void *dst, uint32 fad, uint32 n) {
    gdc_t gdc = (gdc_t)(*(volatile uint32 *)0x8c0000bc);
    uint32 param[4] = { fad, n, (uint32)dst, 0 };
    uint32 stat[4];
    int req = gdc(16 /*CMD_PIOREAD*/, (uint32)param, 0, 0 /*SEND*/);
    if (req <= 0) return -1;
    for (uint32 guard = 0; guard < 100000000u; guard++) {
        gdc(0, 0, 0, 2 /*EXEC*/);
        int s = gdc((uint32)req, (uint32)stat, 0, 1 /*CHECK*/);
        if (s == 2 /*COMPLETED*/) return 0;
        if (s == 0 /*NOT_FOUND*/ || s <= -1 /*FAILED*/) return -2;
    }
    return -3;   /* poll never completed */
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
    tmark("start");
    dbglog(DBG_INFO, "CLEO LOADER M2\n");

    /* KOS-stack-collision probe: &probe ~= current SP. The loader writes shim
     * home through MAPLE_MIRROR end = SHIM_BASE+0x13100 (0x8cfd3100) -- shim
     * code/data, mirrors, BIOS slices. Safe iff SP (KOS stack bottom ~0x8cff0000,
     * top 0x8d000000) stays clear of that. (Final review: this comment used to
     * quote the pre-BIOS-slice bound SHIM_BASE+0xa800, 35 KB short.) */
    volatile int probe = 0;
    dbglog(DBG_INFO, "SP~%08lx memtop=%08lx shim=%08x..%08x scratch=%08x\n",
           (unsigned long)&probe, (unsigned long)_arch_mem_top,
           (unsigned)SHIM_BASE, (unsigned)(SHIM_BASE + 0xa800),
           (unsigned)HANDOFF_SCRATCH);

    /* Naomi BIOS splash (arcade boot feel): shown for the whole load. On the
     * real Naomi the BIOS draws this screen, not the game -- our conversion
     * bypasses that BIOS, so the loader stands in for it. Single memcpy:
     * blob is prepared as raw RGB565 in framebuffer layout. */
    if (LOADER_QUIET)
        memcpy(vram_s, splash_bin, 640 * 480 * 2);
    tmark("splash");

    say("CLEO LOADER M2");
    cdrom_reinit();             /* inits the GD subsystem the shim's BIOS syscalls reuse */
    say("GD init OK");
    tmark("gd init");
    uint8 *stage = (uint8 *)STAGING_ADDR;
    if (cdrom_read_sectors(stage, CART_FAD, GAME_SECTORS) != ERR_OK)
        halt("KOS GD READ FAIL");
    tmark("read 1MB PIO");      /* the dominant term -- pure cdrom_read_sectors */
    if (memcmp(stage, "NAOMI", 5)) {
        dbglog(DBG_INFO, "bad image: %02x %02x %02x %02x %02x %02x %02x %02x @FAD %d\n",
               stage[0], stage[1], stage[2], stage[3],
               stage[4], stage[5], stage[6], stage[7], CART_FAD);
        halt("BAD IMAGE (KOS READ)");
    }
    say("cart read OK (KOS)");

    static uint8 sysbuf[2048] __attribute__((aligned(32)));
    int sr = sysrd_test(sysbuf, CART_FAD, 1);
    if (sr == -3) halt("SYSCALL GD POLL HANG");
    if (sr != 0)  halt("SYSCALL GD READ FAIL");
    if (memcmp(sysbuf, "NAOMI", 5)) halt("BAD IMAGE (SYSCALL READ)");
    say("cart read OK (BIOS syscall)");
    tmark("syscall rd");

    if (apply_patches(stage))   /* verify old bytes then patch; abort on mismatch */
        halt("PATCH ABORT");
    say("patches OK");
    tmark("patches+log");       /* memcpy is us; this bucket is the 34 dbglog serial lines */

    uint32 shim_len = (uint32)(shim_bin_end - shim_bin);
    memcpy((void *)SHIM_BASE, shim_bin, shim_len);
    /* Zero the shim's .bss (NOBITS -- absent from shim.bin, so uninitialised on
     * real DC where RAM boots as garbage; Flycast happens to zero RAM, masking
     * it). The shim has zero-init statics (main.c cfg_seen) that MUST start 0.
     * .bss sits between end-of-loaded-data and the code budget (shim.ld ASSERT
     * . <= SHIM_BASE+SHIM_CODE_MAX), so zeroing that gap covers it exactly. */
    memset((void *)(SHIM_BASE + shim_len), 0, SHIM_CODE_MAX - shim_len);

    /* Place the two Naomi BIOS-ROM slices the game reads via patched P2 pointers
     * (0x60000 verify+copy library, 0x1ffd00 copyright-string auth). Both absent
     * on DC -> without this the config init fails and boot hangs downstream. */
    memcpy((void *)BIOS_DATA_60000,  bios_data,                       BIOS_DATA_60000_LEN);
    memcpy((void *)BIOS_DATA_1FFD00, bios_data + BIOS_DATA_60000_LEN, BIOS_DATA_1FFD00_LEN);
    dbglog(DBG_INFO, "bios-data placed %08x/%x %08x/%x\n",
           (unsigned)BIOS_DATA_60000, (unsigned)BIOS_DATA_60000_LEN,
           (unsigned)BIOS_DATA_1FFD00, (unsigned)BIOS_DATA_1FFD00_LEN);

    /* Zero the G1 mirror block (uncached P2 -- how the patched game + shim access it)
     * so config-time SB_GDST pollers don't spin on stale RAM before the first DMA. */
    volatile uint32 *mir = (volatile uint32 *)P2ADDR(G1_MIRROR);
    for (unsigned i = 0; i < 0x800 / 4; i++) mir[i] = 0;

    /* Task 14f: zero the async-Maple register mirror so the steady engine's first
     * cross-frame SB_MDST poll (0x8c03c30a) sees "not busy" (bit0=0) and triggers,
     * instead of spinning forever on stale RAM. Uncached P2, matching the game's
     * repointed 0xa0-prefixed access -- no cache to purge. */
    volatile uint32 *mmir = (volatile uint32 *)P2ADDR(MAPLE_MIRROR);
    for (unsigned i = 0; i < MAPLE_MIRROR_LEN / 4; i++) mmir[i] = 0;

    /* Relocate the PIC handoff stub out of the copy target and flush it to RAM. */
    uint32 ho_len = (uint32)((uint8 *)handoff_end - (uint8 *)handoff);
    memcpy((void *)HANDOFF_SCRATCH, (void *)handoff, ho_len);

    /* Write-back the CPU stores (patched image, shim code, stub) to RAM: handoff
     * reads staging via P2 and the game/shim read the shim region freshly cached. */
    dcache_purge_range(STAGING_ADDR, GAME_LEN);
    dcache_purge_range(SHIM_BASE, SHIM_CODE_MAX);   /* code+data + zeroed .bss */
    dcache_purge_range(HANDOFF_SCRATCH, ho_len);
    /* Flush our BIOS-data writes: the game reads them via P2 uncached. */
    dcache_purge_range(BIOS_DATA_60000,  BIOS_DATA_60000_LEN);
    dcache_purge_range(BIOS_DATA_1FFD00, BIOS_DATA_1FFD00_LEN);
    tmark("place+purge");       /* shim/bios/mirror memcpys + 1MB uncached copy + purges */

    say("shim + BIOS data placed");
    dbglog(DBG_INFO, "jumping to %08x\n", GAME_ENTRY);
    say("HANDOFF -> game");
    tmark("handoff prep");
    tmark_dump();               /* paint breakdown + pause (LOADER_TIMING only) */
    irq_disable();
    /* Deliberately NO TMU stop / ARM reset here: the game reads TCNT0 for its
     * delay loops relying on the BIOS-left-running timer (12 TCNT0 literals;
     * a stopped TMU froze init between the EEPROM phase and JVS enum on real
     * HW), and it holds/reloads/releases the AICA ARM itself as its first
     * action (Flycast CLEO-ARMRST trace) -- KOS's state matches Naomi's here. */

    /* MMU OFF for the game. KOS runs with the MMU configured; a Naomi game
     * assumes MMUCR.AT=0 forever -- it has NO TLB handlers and reads settings
     * via P0-mirror pointers (0x0c01f100/30). On real HW those accesses raised
     * eternal TLB-miss restarts (EXPEVT=0x040 read off the TV, main thread
     * pinned at 0x8c081224) while every P1/P2 path kept working -- invisible
     * in Flycast (no MMU emulation on this path). */
    *(volatile uint32 *)0xff000010 = 0;            /* MMUCR: AT=0, TLB invalid */

    void (*ho)(uint32, uint32, uint32, uint32) =
        (void *)P2ADDR(HANDOFF_SCRATCH);           /* run the stub uncached */
    ho(P2ADDR(STAGING_ADDR), P2ADDR(GAME_LOAD_ADDR), GAME_LEN, GAME_ENTRY);
    return 0; /* unreachable */
}
