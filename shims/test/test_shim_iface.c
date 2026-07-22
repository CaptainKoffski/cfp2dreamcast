/* Host-compilable self-test for shim_iface.h's pure address-arithmetic macros.
 * Run: cc -Wall -Ishims/include shims/test/test_shim_iface.c -o /tmp/t && /tmp/t
 * (Everything else in shims/ is freestanding SH-4 and can't run on the host;
 * that half is checked instead by the cross-build + linker ASSERT + map grep.) */
#include <assert.h>
#include <stdio.h>
#include "shim_iface.h"

int main(void) {
    assert(P2ADDR(0x8cfc0000u) == 0xacfc0000u);      /* P2 = physical | 0xa0000000 */
    assert(P2ADDR(0x00000000u) == 0xa0000000u);
    assert(SHIM_ERR    == 0x8cfc8000u);               /* SHIM_BASE + 0x8000 */
    assert(SHIM_CODE_MAX == 0x8000u);
    assert(CART_SIZE == 0x06800000u);                 /* corrected value, not the brief's 0x06d00000 */
    assert(CART_FAD  == 451878);                      /* CART_LBA 451728 + 150 (track 4, B5) */
    printf("shim_iface host self-test: OK\n");
    return 0;
}
