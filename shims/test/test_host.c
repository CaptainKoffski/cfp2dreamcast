/* Host-side test of the pure split math. Build: cc -DHOST_TEST. */
#include <assert.h>
#include <stdio.h>
#include "../include/shim_iface.h"
#include "../src/cart.c"     /* pure part only (guards out SH-4 code) */
#include "../src/jvs.c"      /* pure: dc_to_jvs, jvs_checksum, jvs_hasdata */

int main(void) {
    split_t s;
    /* aligned, exact sectors: no head/tail */
    cart_split(0, 4096, &s);
    assert(s.head_take == 0 && s.body_sect == 2 && s.body_fad == 0 && s.tail_take == 0);
    /* unaligned start, within one sector (head only, no body/tail) */
    cart_split(100, 50, &s);
    assert(s.head_fad == 0 && s.head_skip == 100 && s.head_take == 50);
    assert(s.body_sect == 0 && s.tail_take == 0);
    /* unaligned start crossing into full sectors + tail */
    cart_split(2048 + 32, 2048 * 3, &s);
    assert(s.head_fad == 1 && s.head_skip == 32 && s.head_take == 2048 - 32);
    assert(s.body_fad == 2 && s.body_sect == 2);
    assert(s.tail_fad == 4 && s.tail_take == 32);
    /* head fills to boundary exactly, then body only */
    cart_split(2048 - 64, 64 + 2048, &s);
    assert(s.head_take == 64 && s.body_sect == 1 && s.tail_take == 0);
    /* head + tail with NO body (crosses exactly one boundary) */
    cart_split(2048 - 32, 32 + 100, &s);
    assert(s.head_fad == 0 && s.head_take == 32);
    assert(s.body_sect == 0);
    assert(s.tail_fad == 1 && s.tail_take == 100);
    /* zero-length read: no head/body/tail => no I/O issued */
    cart_split(1000, 0, &s);
    assert(s.head_take == 0 && s.body_sect == 0 && s.tail_take == 0);

    /* dc_to_jvs: DC condition is ACTIVE-LOW (0=pressed); JVS is ACTIVE-HIGH.
       DC bits (KOS controller.h CONT_*): 1=B 2=A 3=Start 4=Up 5=Down 6=Left 7=Right.
       JVS word (input-map.md, observed): Start 0x8000 Up 0x2000 Down 0x1000
       Left 0x0800 Right 0x0400 B1 0x0200 B2 0x0100. */
    assert(dc_to_jvs(0xffff) == 0x0000);                             /* nothing pressed */
    assert(dc_to_jvs((unsigned short)~(1u << 3)) == 0x8000);         /* Start */
    assert(dc_to_jvs((unsigned short)~(1u << 4)) == 0x2000);         /* Up */
    assert(dc_to_jvs((unsigned short)~(1u << 5)) == 0x1000);         /* Down */
    assert(dc_to_jvs((unsigned short)~(1u << 6)) == 0x0800);         /* Left */
    assert(dc_to_jvs((unsigned short)~(1u << 7)) == 0x0400);         /* Right */
    assert(dc_to_jvs((unsigned short)~(1u << 2)) == 0x0200);         /* A -> B1 */
    assert(dc_to_jvs((unsigned short)~(1u << 1)) == 0x0100);         /* B -> B2 */
    assert(dc_to_jvs((unsigned short)~((1u<<3)|(1u<<4))) == 0xa000); /* Start+Up chord */

    /* jvs_checksum on the reconstructed golden idle has-data frame = 0x22.
       This pins the whole 64-byte template byte-for-byte (§input-ABI). */
    assert(jvs_hasdata[0x00] == 0x87 && jvs_hasdata[0x03] == 0x0f);  /* maple hdr 87 00 20 0f */
    assert(jvs_hasdata[0x04] == 0x16 && jvs_hasdata[0x1a] == 0xe0);  /* subresp / JVS E0 sync */
    assert(jvs_checksum(jvs_hasdata) == 0x22 && jvs_hasdata[0x3a] == 0x22);

    /* Task 17 (2P): P2 word lands big-endian at 0x22/0x23 and the checksum loop
       (0x1b..0x39) covers those bytes. Golden template has P2 idle; a P2 Start
       (dc_to_jvs -> 0x8000) sets [0x22]=0x80 and bumps the sum by 0x80. */
    assert(jvs_hasdata[0x22] == 0x00 && jvs_hasdata[0x23] == 0x00);   /* P2 idle in template */
    {
        unsigned char f2[64];
        unsigned short j2 = dc_to_jvs((unsigned short)~(1u << 3));    /* P2 Start */
        int i;
        for (i = 0; i < 64; i++) f2[i] = jvs_hasdata[i];
        f2[0x22] = (unsigned char)(j2 >> 8);
        f2[0x23] = (unsigned char)(j2 & 0xff);
        assert(f2[0x22] == 0x80 && f2[0x23] == 0x00);                 /* Start -> 0x8000 big-endian */
        assert(jvs_checksum(f2) == (unsigned char)(0x22 + 0x80));     /* checksum picks up P2 byte */
    }

    printf("PASS test_host cart_split + dc_to_jvs + jvs_checksum + p2\n");
    return 0;
}
