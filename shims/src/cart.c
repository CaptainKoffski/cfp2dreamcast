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

/* dest_phys is a main-RAM phys addr (0x0c......). The game reads streamed cart
 * assets UNCACHED (P2, 0xa0......) or via hardware DMA (PVR/TA/AICA) -- real-DMA
 * semantics, matching the original FUN_8c03bc12 which does NO cache op. So the
 * bytes must land in RAM with nothing stale left in the D-cache. We therefore
 * write the dest through the P2 UNCACHED alias (0xa0......): the xmemcpy head/
 * tail stores AND the gd_read_sectors PIO body stores all go straight to RAM,
 * bypassing the cache, so the game's P2 read (or a downstream DMA) sees current
 * data on real hardware. (Via P1 cached the bytes would sit in D-cache while RAM
 * stayed stale -> garbage graphics on real DC; Flycast has no cache so it masked
 * this.) This mirrors maple_reply + the register mirror, which are all P2.
 * The bounce buffer stays cached (shim home, P1): it is pure CPU scratch -- the
 * BIOS PIO writes it and xmemcpy reads it back through the same cached view, so
 * they are mutually coherent; only the FINAL dest must be uncached. */
void cart_read(u32 off, u32 len, u32 dest_phys) {
    split_t s;
    unsigned char *dst = (unsigned char *)(dest_phys | 0xa0000000); /* P2 uncached */
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
 *   len  = SB_GDLEN(0x7408) bytes (sole length source; DMA_COUNT unwritten here)
 *   dest = SB_GDSTAR(0x7404) phys ; SB_GDST(0x7418) cleared => game's poll exits */
void shim_cart_service(void) {
    volatile u32 *m = P2(G1_MIRROR);
    u32 off = (((m[0x0c/4] & 0xffff) << 16) | (m[0x10/4] & 0xffff)) & 0x0fffffff;
    u32 len = m[0x408/4];               /* SB_GDLEN mirror (bytes), sole length */
    /* ponytail: length is SB_GDLEN; DMA_COUNT (mirror+0x14) is never written by
     * this game's arm path (FUN_8c03b81a) -- cross-check dropped */
    u32 dest = m[0x404/4] & 0x1fffffff; /* SB_GDSTAR mirror; game programs it
                                         * P1-aliased (0x8c..) -- mask P0/P1/P2
                                         * region bits to physical 0x0c.. */
    /* len sanity (>16MB is impossible for a legit cart read) BEFORE the +len
     * comparisons so an absurd SB_GDLEN can't u32-wrap past them. Upper bound is
     * fenced to shim home (SHIM_BASE phys 0x0cfc0000): a DMA reaching there would
     * clobber the running shim / BIOS-data blocks -- last line protecting it. */
    if (!len || len > 0x01000000 || off + len > CART_SIZE ||
        (dest & 0x1f000000) != 0x0c000000 || dest + len > (SHIM_BASE & 0x1fffffff))
        shim_die(2, off, dest);
    scif_puts("CART off="); scif_puthex(off);
    scif_puts(" len="); scif_puthex(len);
    scif_puts(" dst="); scif_puthex(dest); scif_puts("\n");
    cart_read(off, len, dest);
    m[0x418/4] = 0;                     /* SB_GDST mirror reads "done" */
}
#endif /* !HOST_TEST */
