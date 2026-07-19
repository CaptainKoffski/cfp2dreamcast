#include "shim_iface.h"
typedef unsigned int u32;
typedef unsigned char u8;
void shim_die(u32, u32, u32);
void *xmemcpy(void *, const void *, u32);
/* shim_cart_service lives in src/cart.c (Task 10) */

unsigned short maple_getcond(void);
unsigned short dc_to_jvs(unsigned short);
unsigned char  jvs_checksum(const unsigned char *);
extern const unsigned char jvs_hasdata[];               /* src/jvs.c */

/* MIE reply templates + free-play EEPROM, embedded at build (Makefile xxd rules;
 * gitignored source blobs). Verbatim ACKs replayed as captured (§input-ABI 4a). */
extern const unsigned char eeprom_img[];
extern const unsigned char mie_sub01[]; extern const u32 mie_sub01_len;
extern const unsigned char mie_sub13[]; extern const u32 mie_sub13_len;
extern const unsigned char mie_sub17[]; extern const u32 mie_sub17_len;
extern const unsigned char mie_sub27[]; extern const u32 mie_sub27_len;
extern const unsigned char mie_sub31[]; extern const u32 mie_sub31_len;
extern const unsigned char mie_subff[]; extern const u32 mie_subff_len;

/* JVS I/O-board enumeration replies (scripts/extract_jvs_replies.py). Keyed on
 * the JVS command the game transmits; the matching one is replayed on the
 * following receive so the board passes enumeration (M3). */
extern const unsigned char mie_jvsf1[]; extern const u32 mie_jvsf1_len;  /* F1 set-addr */
extern const unsigned char mie_jvs10[]; extern const u32 mie_jvs10_len;  /* 10 board ID */
extern const unsigned char mie_jvs11[]; extern const u32 mie_jvs11_len;  /* 11 cmd rev  */
extern const unsigned char mie_jvs12[]; extern const u32 mie_jvs12_len;  /* 12 JVS rev  */
extern const unsigned char mie_jvs13[]; extern const u32 mie_jvs13_len;  /* 13 comm rev */
extern const unsigned char mie_jvs14[]; extern const u32 mie_jvs14_len;  /* 14 features */

/* Last JVS command transmitted (sub 0x17/0x19/0x21) -> selects the enumeration
 * reply the next receive (sub 0x15) returns. 0xff = "not an enumeration command"
 * (digital read). Initialised non-zero so it lands in .data (the loader copies
 * .data but does NOT zero .bss). */
static u8 pending_jvs = 0xff;

#define SB_MDST (*(volatile u32 *)0xa05f6c18)
#define GW(a)   (*(volatile u32 *)((a) | 0x80000000u))  /* cached word: game control state */
#define GB(a)   (*(volatile u8  *)((a) | 0x80000000u))  /* cached byte */

/* Live DC GetCondition -> JVS digital-read has-data frame at recvaddr. */
static void jvs_digital(void *rx) {
    unsigned short j = dc_to_jvs(maple_getcond());
    u8 f[64];
    xmemcpy(f, jvs_hasdata, 64);
    f[0x20] = (u8)(j >> 8);                 /* BTN_OFF: P1 word big-endian (hi) */
    f[0x21] = (u8)(j & 0xff);              /*          (lo; this game: 0)      */
    f[0x3a] = jvs_checksum(f);              /* recompute JVS checksum @0x3a      */
    xmemcpy(rx, f, 64);
}

/* Shared reply synthesizer for both MIE sites. recvaddr is a game main-RAM phys
 * address; the reply is written UNCACHED (P2) because it stands in for a Maple
 * DMA-to-RAM write -- the game's reply reader treats recvaddr as a DMA buffer
 * (reads it uncached / post-invalidate), so an uncached store is what it sees. */
static void maple_reply(u32 sub, u32 recvaddr) {
    void *rx = (void *)P2ADDR(recvaddr);
    switch (sub) {
    case 0x33:                              /* steady per-frame poll: always live */
        jvs_digital(rx);
        break;
    case 0x15:                              /* boot receive: enumeration reply or live */
        switch (pending_jvs) {              /* keyed on the last transmitted JVS cmd */
        case 0xf1: xmemcpy(rx, mie_jvsf1, mie_jvsf1_len); break;
        case 0x10: xmemcpy(rx, mie_jvs10, mie_jvs10_len); break;
        case 0x11: xmemcpy(rx, mie_jvs11, mie_jvs11_len); break;
        case 0x12: xmemcpy(rx, mie_jvs12, mie_jvs12_len); break;
        case 0x13: xmemcpy(rx, mie_jvs13, mie_jvs13_len); break;
        case 0x14: xmemcpy(rx, mie_jvs14, mie_jvs14_len); break;
        default:   jvs_digital(rx);         break;  /* digital read (0x20/0x21/0x22/none) */
        }
        break;
    case 0x03: {                            /* EEPROM read: 1-word hdr + 128 B @ EE_OFF=4 */
        u8 hdr[4] = { 0x87, 0x00, 0x20, 0x20 };   /* 0x20 words */
        xmemcpy(rx, hdr, 4);
        xmemcpy((u8 *)rx + 4, eeprom_img, 128);
        break;
    }
    case 0x01: xmemcpy(rx, mie_sub01, mie_sub01_len); break;   /* EEPROM ready ACK */
    case 0x13: xmemcpy(rx, mie_sub13, mie_sub13_len); break;   /* store repeat req ACK */
    case 0x17: case 0x21:
               xmemcpy(rx, mie_sub17, mie_sub17_len); break;   /* transmit ACK */
    case 0x27: xmemcpy(rx, mie_sub27, mie_sub27_len); break;   /* kick-scan ACK */
    case 0x31: xmemcpy(rx, mie_sub31, mie_sub31_len); break;   /* DIP switches */
    case 0xff: xmemcpy(rx, mie_subff, mie_subff_len); break;   /* broadcast/reset ACK */
    case 0x0b: {                            /* EEPROM write: ack + drop (EEPROM is baked
                                               read-only). Never observed (§V-EEPROM 0x0b:
                                               0x); defensive so a stray write can't hang. */
        u8 ack[8] = { 0x87, 0x00, 0x20, 0x01, 0x0c, 0x00, 0x8e, 0x00 };
        xmemcpy(rx, ack, 8);
        break;
    }
    default:   shim_die(3, sub, recvaddr);
    }
}

/* ponytail: currently UNHOOKED (Task 14d). pool[0x8c027618] feeds the generic
 * dispatcher FUN_8c027584 (160+ callers), not an MIE-only site, so hooking it made
 * the shim shim_die on the first post-check NON-MIE frame (cmd 0xf6, recv 0xc8000000).
 * Kept as the documented boot-MIE ABI + re-hook target once a MIE-only call site is
 * isolated. See scripts/build_patch_table.py §Task 14d + phase4-conversion.md §Task 14d.
 *
 * Boot MIE builder (0x8c0315ce, reached via fn-ptr pool[0x8c027618]). Sub +
 * recvaddr are read from the command block *0x8c0e6400 (word3 low byte = sub,
 * word1 = recvaddr). Completion: leave the Maple DMA observably done, i.e.
 * SB_MDST reads 0. [KB §input-ABI site A -- boot completion is M4-gated.]
 *
 * arg0 = r4 = the transmit payload block the dispatcher passes to the builder
 * (FUN_8c027584 @0x8c0275ee, jsr @r3 with r4 = pool 0x8c0e62c8 / 0x8c0a27f4).
 * On a transmit (sub 0x17/0x19/0x21) the JVS command byte lives at arg0+4
 * (descriptor word5 byte0 -> maple frame byte 12 -> Flycast dma_buffer_in[8],
 * maple_jvs.cpp:1780); we latch it so the following receive (sub 0x15) returns
 * the matching enumeration reply. sub 0x27 (transmit-with-repeat) is only ever
 * the digital-read setup -> latch "not enumeration". */
void shim_maple_boot(u32 arg0) {
    u32 cmdblk = GW(0x8c0e6400);
    u32 sub    = GB(cmdblk + 0x0c);
    u32 recv   = GW(cmdblk + 0x04);
    switch (sub) {
    case 0x17: case 0x19: case 0x21: pending_jvs = GB(arg0 + 4); break;
    case 0x27:                       pending_jvs = 0xff;         break;
    }
    maple_reply(sub, recv);
    SB_MDST = 0;
}

/* Steady per-frame MIE builder (FUN_8c03c2c6, reached via pool[0x8c02ed6c]).
 * Always sub 0x33 (real GetCondition every frame). Reproduces the game's own
 * recvaddr computation from the input double buffer, writes it to descriptor
 * word1, then clears the [desc+0x18] pending bit so the caller sees "done".
 * Caller treats return >= 0 as OK. [KB §input-ABI site B -- M4-gated.] */
int shim_maple_entry(void) {
    u32 base = GW(0x8c0e8410);
    if (GW(base + 0x0fc0) != 1) return -3;         /* input subsystem not ready */
    u32 raw  = GW(base + 0x10b8);                  /* double-buffer index */
    u32 recv = GW(base + 0x10a8 + (raw & 1) * 4) & 0x0fffffff;  /* FUN_8c030fba: P1->phys */
    u32 desc = GW(base + 0x10f4);
    GW(desc + 0x04) = recv;                        /* descriptor word1 = recvaddr */
    GW(base + 0x10b8) = raw ^ 1u;                  /* toggle index (as the game does) */
    maple_reply(0x33, recv);
    GW(desc + 0x18) &= ~1u;                        /* clear pending bit0 = completion */
    return 0;
}
