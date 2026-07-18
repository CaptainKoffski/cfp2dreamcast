#include <kos.h>

#define CART_FAD 47198          /* track3 LBA 45000 + 150 FAD bias + ISO_SECTORS 2048 */

static uint8 sec[2048] __attribute__((aligned(32)));

int main(void) {
    dbglog(DBG_INFO, "CLEO LOADER M1\n");
    cdrom_reinit();
    int r = cdrom_read_sectors(sec, CART_FAD, 1);
    dbglog(DBG_INFO, "read fad=%d -> %d\n", CART_FAD, r);
    if (r == ERR_OK && !memcmp(sec, "NAOMI", 5))
        dbglog(DBG_INFO, "M1 OK: NAOMI header found at cart+0\n");
    else
        dbglog(DBG_INFO, "M1 FAIL: r=%d first bytes %02x %02x %02x %02x %02x\n",
               r, sec[0], sec[1], sec[2], sec[3], sec[4]);
    for(;;) thd_sleep(1000);
    return 0;
}
