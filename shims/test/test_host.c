/* Host-side test of the pure split math. Build: cc -DHOST_TEST. */
#include <assert.h>
#include <stdio.h>
#include "../include/shim_iface.h"
#include "../src/cart.c"     /* pure part only (guards out SH-4 code) */

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
    printf("PASS test_host cart_split\n");
    return 0;
}
