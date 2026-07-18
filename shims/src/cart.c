#include "shim_iface.h"
typedef unsigned int u32;

typedef struct {
    u32 head_fad, head_skip, head_take;   /* fads are cart-relative sector indices */
    u32 body_fad, body_sect;
    u32 tail_fad, tail_take;
} split_t;

/* Pure: decompose (byte offset, byte len) into partial-head / whole-sector
 * body / partial-tail. Compiled on host for the unit test (test/test_host.c). */
void cart_split(u32 off, u32 len, split_t *s) {
    u32 sec = off / 2048, skip = off % 2048;
    s->head_fad = s->head_skip = s->head_take = 0;
    s->body_fad = s->body_sect = 0;
    s->tail_fad = s->tail_take = 0;
    if (skip) {
        u32 take = 2048 - skip; if (take > len) take = len;
        s->head_fad = sec; s->head_skip = skip; s->head_take = take;
        len -= take; sec++;
    }
    s->body_fad = sec;
    s->body_sect = len / 2048;
    sec += s->body_sect; len %= 2048;
    if (len) { s->tail_fad = sec; s->tail_take = len; }
}

#ifndef HOST_TEST
void shim_die(u32, u32, u32);
void *xmemcpy(void *, const void *, u32);
int gd_read_sectors(void *dst, u32 fad, u32 n);
void scif_puts(const char *); void scif_puthex(u32);

static void gd_or_die(void *dst, u32 rel_fad, u32 n) {
    int r = gd_read_sectors(dst, CART_FAD + rel_fad, n);
    if (r < 0) shim_die(4, rel_fad, (u32)r);
}

/* dest_phys is a main-RAM phys addr (0x0c......); we read it back cached via the
 * P1 alias (0x8c......). PIO copies are CPU stores into that same cached view,
 * so the game (also CPU, cached P1) sees them coherently -- no dcache flush.
 * ponytail: coherent because reader and writer share one cache view; a future
 * DMAREAD upgrade (Phase 5) WOULD need dcache_inval on the dest. */
void cart_read(u32 off, u32 len, u32 dest_phys) {
    split_t s;
    unsigned char *dst = (unsigned char *)(dest_phys | 0x80000000); /* P1 */
    unsigned char *bounce = (unsigned char *)SHIM_BOUNCE;
    cart_split(off, len, &s);
    if (s.head_take) {
        gd_or_die(bounce, s.head_fad, 1);
        xmemcpy(dst, bounce + s.head_skip, s.head_take);
        dst += s.head_take;
    }
    if (s.body_sect) {
        gd_or_die(dst, s.body_fad, s.body_sect);
        dst += s.body_sect * 2048;
    }
    if (s.tail_take) {
        gd_or_die(bounce, s.tail_fad, 1);
        xmemcpy(dst, bounce, s.tail_take);
    }
}

/* Entry hooked onto the game's DMA-completion wait FUN_8c03bc12 (KB §V3, patch
 * via Task 12). Reads the mirrored register values the game already wrote
 * (KB §patch-sites: MIRROR+0xYYY stands in for cart/G1 reg 0x5f7YYY).
 *   off  = DMA_OFFSETH(0x700c)<<16 | DMA_OFFSETL(0x7010), cart-relative bytes
 *   len  = SB_GDLEN(0x7408) bytes ; cnt = DMA_COUNT(0x7014) * 0x20 -- must agree
 *   dest = SB_GDSTAR(0x7404) phys ; SB_GDST(0x7418) cleared => game's poll exits */
void shim_cart_service(void) {
    volatile u32 *m = P2(G1_MIRROR);
    u32 off = (((m[0x0c/4] & 0xffff) << 16) | (m[0x10/4] & 0xffff)) & 0x0fffffff;
    u32 len = m[0x408/4];               /* SB_GDLEN mirror (bytes) */
    u32 cnt = m[0x14/4] * 32;           /* DMA_COUNT mirror (0x20 units) */
    if (!len) len = cnt;
    else if (cnt && cnt != len) shim_die(1, cnt, len);
    u32 dest = m[0x404/4];              /* SB_GDSTAR mirror (phys dest) */
    if (!len || off + len > CART_SIZE || (dest & 0x1f000000) != 0x0c000000)
        shim_die(2, off, len ? dest : 0);
    scif_puts("CART off="); scif_puthex(off);
    scif_puts(" len="); scif_puthex(len);
    scif_puts(" dst="); scif_puthex(dest); scif_puts("\n");
    cart_read(off, len, dest);
    m[0x418/4] = 0;                     /* SB_GDST mirror reads "done" */
}
#endif /* !HOST_TEST */
