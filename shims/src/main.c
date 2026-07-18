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

#define SB_MDST (*(volatile u32 *)0xa05f6c18)
#define GW(a)   (*(volatile u32 *)((a) | 0x80000000u))  /* cached word: game control state */
#define GB(a)   (*(volatile u8  *)((a) | 0x80000000u))  /* cached byte */

/* Shared reply synthesizer for both MIE sites. recvaddr is a game main-RAM phys
 * address; the reply is written UNCACHED (P2) because it stands in for a Maple
 * DMA-to-RAM write -- the game's reply reader treats recvaddr as a DMA buffer
 * (reads it uncached / post-invalidate), so an uncached store is what it sees. */
static void maple_reply(u32 sub, u32 recvaddr) {
    void *rx = (void *)P2ADDR(recvaddr);
    switch (sub) {
    case 0x15: case 0x33: {                 /* receive latest JVS digital input */
        unsigned short j = dc_to_jvs(maple_getcond());
        u8 f[64];
        xmemcpy(f, jvs_hasdata, 64);
        f[0x20] = (u8)(j >> 8);             /* BTN_OFF: P1 word big-endian (hi) */
        f[0x21] = (u8)(j & 0xff);           /*          (lo; this game: 0)      */
        f[0x3a] = jvs_checksum(f);          /* recompute JVS checksum @0x3a      */
        xmemcpy(rx, f, 64);
        break;
    }
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

/* Boot MIE builder (0x8c0315ce, reached via fn-ptr pool[0x8c027618]). Sub +
 * recvaddr are read from the command block *0x8c0e6400 (word3 low byte = sub,
 * word1 = recvaddr). Completion: leave the Maple DMA observably done, i.e.
 * SB_MDST reads 0. [KB §input-ABI site A -- boot completion is M4-gated.] */
void shim_maple_boot(void) {
    u32 cmdblk = GW(0x8c0e6400);
    u32 sub    = GB(cmdblk + 0x0c);
    u32 recv   = GW(cmdblk + 0x04);
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
