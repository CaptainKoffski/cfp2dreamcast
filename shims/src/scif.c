/* SCIF debug out. Baud/pin state inherited from the KOS boot (dbgio scif). */
typedef volatile unsigned short vu16; typedef volatile unsigned char vu8;
#define SCFSR2  (*(vu16 *)0xffe80010)
#define SCFTDR2 (*(vu8  *)0xffe8000c)
void scif_putc(char c) {
    while (!(SCFSR2 & 0x20)) ;      /* TDFE */
    SCFTDR2 = (unsigned char)c;
    SCFSR2 &= (unsigned short)~0x60;/* clear TDFE|TEND */
}
void scif_puts(const char *s) { while (*s) { if (*s=='\n') scif_putc('\r'); scif_putc(*s++); } }
void scif_puthex(unsigned int v) {
    for (int i = 28; i >= 0; i -= 4) scif_putc("0123456789abcdef"[(v >> i) & 15]);
}
