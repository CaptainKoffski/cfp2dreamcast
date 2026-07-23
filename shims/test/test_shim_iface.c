/* Host-compilable self-test for shim_iface.h's pure address-arithmetic macros
 * AND the shim-home region map (run by `make test` -- final review wired it in;
 * it previously existed but no target executed it).
 * (The freestanding half is checked by the cross-build + the shim.ld ASSERT,
 * which bounds code+data+bss size; the region-map invariants below are pure
 * macros the linker never sees, so they are enforced HERE.) */
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

    /* Region map: ordered, disjoint, and clear of the KOS stack (final review:
     * a length bump like BIOS_DATA_60000_LEN 0x7000->0x7100 would silently
     * overlap the next slice; nothing else checks these). Loader main.c
     * memcpys the two BIOS slices back-to-back from one blob, so slice A must
     * END at or before slice B's start. */
    assert(SHIM_BASE + SHIM_CODE_MAX <= SHIM_ERR);                    /* code+bss | err */
    assert(SHIM_ERR + 16 <= G1_MIRROR);                               /* err | G1 mirror */
    assert(G1_MIRROR + 0x800 <= MAPLE_TX);                            /* mirror | maple tx */
    assert(MAPLE_TX + 0x40 <= MAPLE_RX);                              /* tx | rx */
    assert(MAPLE_RX + 132 <= SHIM_BOUNCE);                            /* rx (DEVINFO reply 132B) | bounce */
    assert(SHIM_BOUNCE + 2048 <= BIOS_DATA_60000);                    /* bounce | BIOS slice A */
    assert(BIOS_DATA_60000 + BIOS_DATA_60000_LEN <= BIOS_DATA_1FFD00);/* slice A | slice B */
    assert(BIOS_DATA_1FFD00 + BIOS_DATA_1FFD00_LEN <= MAPLE_MIRROR);  /* slice B | maple mirror */
    assert(MAPLE_MIRROR + MAPLE_MIRROR_LEN <= 0x8cff0000u);           /* map top | KOS stack bottom */
    printf("shim_iface host self-test: OK (incl. region map)\n");
    return 0;
}
