#include "shim_iface.h"
void scif_puts(const char *); void scif_puthex(unsigned int);
void *xmemcpy(void *d, const void *s, unsigned int n) {
    unsigned char *dd = d; const unsigned char *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}
void shim_die(unsigned int code, unsigned int a, unsigned int b) {
    volatile unsigned int *e = P2(SHIM_ERR);
    e[1] = a; e[2] = b; e[3] = 0xdeadcafe; e[0] = code;
    scif_puts("SHIMERR code="); scif_puthex(code);
    scif_puts(" a="); scif_puthex(a); scif_puts(" b="); scif_puthex(b); scif_puts("\n");
    for (;;) ;
}
