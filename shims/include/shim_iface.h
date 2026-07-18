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

#define STAGING_ADDR    0x8cd00000
#define GAME_LOAD_ADDR  0x8c020000
#define GAME_LEN        0x00100000
#define GAME_ENTRY      0x8c04ae2c
#define CART_FAD        47198       /* verified at M1 (Task 2). = CART_LBA 47048 + 150 */
#define CART_SIZE       0x06800000  /* 109,051,904 bytes; verified against ROM at Task 2 */

#define P2ADDR(a)       ((a) | 0xa0000000)
#ifndef HOST_TEST
#define P2(a)           ((volatile unsigned int *)P2ADDR(a))
#endif

#endif
