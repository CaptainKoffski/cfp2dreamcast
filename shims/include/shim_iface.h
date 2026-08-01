/* Single source of truth for Phase 4 addresses. Consumed by shim (freestanding),
 * loader (KOS), and scripts/build_patch_table.py (parses the #defines). */
#ifndef SHIM_IFACE_H
#define SHIM_IFACE_H

#define SHIM_BASE       0x8cfc0000  /* spec §1 RAM map; V2-verified clean */
#define SHIM_CODE_MAX   0x00008000  /* 32 KB code+rodata budget */

/* Fixed data blocks (offsets from SHIM_BASE, all accessed via P2) */
#define SHIM_ERR        (SHIM_BASE + 0x8000)  /* u32[4]: code, a, b, magic */
#define G1_MIRROR       (SHIM_BASE + 0x8800)  /* 0x800 bytes: fake 0x5f7000-0x5f77ff */
#define MAPLE_TX        (SHIM_BASE + 0x9000)  /* 32-byte aligned maple descriptor+frame */
#define MAPLE_RX        (SHIM_BASE + 0x9040)
#define SHIM_BOUNCE     (SHIM_BASE + 0xa000)  /* 2048-byte sector bounce (ends +0xa800) */

/* Naomi BIOS-ROM data the game reads (absent on DC); loader places copies here,
 * two pool patches repoint the game's P2 read pointers at them. Contiguous:
 * 0x60000 block ends exactly at 0x1ffd00 block. Ends +0x12070, < RAM top. */
#define BIOS_DATA_60000      (SHIM_BASE + 0xb000)   /* FUN_8c0803a4 verify+copy library */
#define BIOS_DATA_60000_LEN  0x7000
#define BIOS_DATA_1FFD00     (SHIM_BASE + 0x12000)  /* FUN_8c081438 copyright-string auth */
#define BIOS_DATA_1FFD00_LEN 0x70

/* Task 14f: async-Maple engine register mirror. The runtime MIE engine
 * (FUN_8c03c2c6) reads/writes the maple register window via the sole live
 * maple-base pool word 0x8c030fec (value 0xa05f6c00); build_patch_table repoints
 * that word here, so the engine's SB_MDSTAR (+0x04)/SB_MDEN (+0x14)/SB_MDST
 * (+0x18) accesses land in shim RAM instead of real maple regs -- no real
 * controller DMA fires from the game path, and shim_maple_steady services the
 * transaction. Above BIOS_DATA_1FFD00 (ends +0x12070), below RAM top, above the
 * game write watermark (15.5 MB). Accessed uncached (P2), matching the game's
 * 0xa0-prefixed view. */
#define MAPLE_MIRROR         (SHIM_BASE + 0x13000)
#define MAPLE_MIRROR_LEN     0x100

#define STAGING_ADDR    0x8cd00000
#define GAME_LOAD_ADDR  0x8c020000
#define GAME_LEN        0x00100000
#define GAME_ENTRY      0x8c04ae2c
#define CART_FAD        451878      /* = CART_LBA 451728 + 150: cart follows the
                                     * 1728-sector boot region in track 4 (B5
                                     * max-clone layout; was 47198 in track 3) */
#define CART_SIZE       0x06800000  /* 109,051,904 bytes; verified against ROM at Task 2 */

#define P2ADDR(a)       ((a) | 0xa0000000)
#ifndef HOST_TEST
#define P2(a)           ((volatile unsigned int *)P2ADDR(a))
#endif

/* HW load-time measurement toggle. Single source so cart.c/main.c/util.c agree
 * (a per-file mismatch would link-error on ls_stampA). 1 = paint live counters
 * to the framebuffer: cart bytes/reads/GD-ms (streaming) + the post-handoff init
 * timeline (which stage burns the pre-stream black screen). 0 = compiled out
 * (release). Per-read/per-frame VRAM paint blacks Flycast's present -- HW-only,
 * like SHIM_HUD. */
#ifndef SHIM_LOADSTAT
#define SHIM_LOADSTAT 0        /* 1 = on-screen load-time counters (HW profiling) */
#endif

/* Boot-preload progress bar (release UX): the post-handoff black screen is ~5 s
 * of asset streaming + game init with zero feedback. shim_cart_service paints a
 * bar into the live scanout FB per cart stream, ONLY until the boot-preload
 * byte total is reached -- painting then stops for good, so stage-boundary
 * streams during play never draw over a visible game frame. Invisible in
 * Flycast (FB writes don't reach its rendered output -- same as the splash and
 * HUD); the TV shows it. 0 = compiled out. */
#ifndef SHIM_LOADBAR
#define SHIM_LOADBAR 1
#endif

/* GD-ROM cart streaming via G1-DMA (CD_CMD_DMAREAD) instead of polled PIO -- the
 * deferred I1 "GD-DMA upgrade". Whole-sector, 32-byte-aligned body reads go by
 * DMA (the bulk); partial head/tail and any unaligned body stay PIO. 1 = DMA
 * (this build, under test), 0 = the proven polled-PIO path (instant fallback if
 * HW regresses). HW-only: Flycast's virtual drive returns instantly for both PIO
 * and DMA, so it can neither exercise nor confirm this path. */
#ifndef SHIM_GD_DMA
#define SHIM_GD_DMA 1
#endif

#endif
