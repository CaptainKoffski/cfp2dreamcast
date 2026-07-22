#include "shim_iface.h"
#include <stddef.h>
void scif_puts(const char *); void scif_puthex(unsigned int);
/* Freestanding -Os lets GCC lower sized array copies/inits to memcpy/memset
 * calls, so the runtime must supply them. xmemcpy stays for explicit callers. */
void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *dd = d; const unsigned char *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}
void *memset(void *d, int c, size_t n) {
    unsigned char *dd = d;
    while (n--) *dd++ = (unsigned char)c;
    return d;
}
void *xmemcpy(void *d, const void *s, unsigned int n) { return memcpy(d, s, n); }
/* Real-HW breadcrumb HUD: paint a small block near the top of the LIVE scanout
 * framebuffer (base read from FB_R_SOF1) and clear the VO_CONTROL blank bit --
 * the game blanks video during init and unblanks only after its first rendered
 * frame, so on a hang the screen stays black with zero information. Each
 * milestone paints once; which blocks appear names the phase that hung. */
/* Round-17: HUD compiled out by default. On real HW every painted pixel is a
 * slow uncached VRAM bus write -- the full HUD (marks + classifiers + 6 hex
 * rows per trigger + 4 per poll) burns milliseconds of the 16.7 ms frame,
 * invisible in Flycast (instant memory). Prime suspect for the 2P-only
 * slowdown (heavier frames, no headroom left). Flip to 1 for stall hunts;
 * shim_die's fatal paints stay unconditional. */
#ifndef SHIM_HUD
#define SHIM_HUD 0
#endif

void shim_mark(unsigned int slot, unsigned short color) {
#if !SHIM_HUD
    (void)slot; (void)color; return;
#endif
    *(volatile unsigned int *)0xa05f80e8 &= ~8u;            /* unblank video */
    unsigned int base = *(volatile unsigned int *)0xa05f8050 & 0x00fffffcu;
    volatile unsigned short *fb =
        (volatile unsigned short *)(0xa5000000u + base);
    if (slot >= 16u)                       /* slots 16+ = second row, below row 1 */
        fb += 12u * 640u + (slot - 16u) * 24u;
    else
        fb += slot * 24u;
    for (unsigned int y = 0; y < 8; y++)
        for (unsigned int x = 0; x < 16; x++)
            fb[y * 640u + x] = color;
}

/* On-screen hex printer (real-HW forensics): 8 nibbles, 3x5 font scaled x2,
 * drawn into the live scanout FB. Readable from a photo of the TV. */
static const unsigned char hexfont[16][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},
    {5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,2,2,2},
    {7,5,7,5,7},{7,5,7,1,7},{2,5,7,5,5},{6,5,6,5,6},
    {7,4,4,4,7},{6,5,5,5,6},{7,4,7,4,7},{7,4,7,4,4},
};
static void hex_paint(unsigned int x, unsigned int y, unsigned int val) {
    *(volatile unsigned int *)0xa05f80e8 &= ~8u;            /* unblank video */
    unsigned int base = *(volatile unsigned int *)0xa05f8050 & 0x00fffffcu;
    volatile unsigned short *fb = (volatile unsigned short *)(0xa5000000u + base);
    for (unsigned int d = 0; d < 8; d++) {
        unsigned int nib = (val >> ((7u - d) * 4u)) & 0xfu;
        for (unsigned int r = 0; r < 5; r++)
            for (unsigned int c = 0; c < 3; c++) {
                /* cyan digits: white merged with the loader's bfont text on TV */
                unsigned short px = ((hexfont[nib][r] >> (2u - c)) & 1u) ? 0x07ff : 0x0000;
                unsigned int px_x = x + d * 10u + c * 2u, px_y = y + r * 2u;
                fb[px_y * 640u + px_x]        = px;
                fb[px_y * 640u + px_x + 1u]   = px;
                fb[(px_y + 1u) * 640u + px_x] = px;
                fb[(px_y + 1u) * 640u + px_x + 1u] = px;
            }
    }
}

void shim_hex(unsigned int x, unsigned int y, unsigned int val) {
#if !SHIM_HUD
    (void)x; (void)y; (void)val; return;
#endif
    hex_paint(x, y, val);
}

void shim_die(unsigned int code, unsigned int a, unsigned int b) {
    volatile unsigned int *e = P2(SHIM_ERR);
    e[1] = a; e[2] = b; e[3] = 0xdeadcafe; e[0] = code;
    scif_puts("SHIMERR code="); scif_puthex(code);
    scif_puts(" a="); scif_puthex(a); scif_puts(" b="); scif_puthex(b); scif_puts("\n");
    /* Paint VRAM so real HW shows the failure instead of a silent black hang
     * (serial is invisible there). The visible framebuffer sits inside the
     * first 1 MB of VRAM regardless of scanout base. RGB565 pairs:
     * 2=yellow (cart-service bad dest)  3=magenta (unknown maple frame)
     * 4=red (GD read error)  5=blue (GD poll hang)  else cyan. */
    unsigned int px = 0x07ff07ff;
    if (code == 2) px = 0xffe0ffe0;
    else if (code == 3) px = 0xf81ff81f;
    else if (code == 4) px = 0xf800f800;
    else if (code == 5) px = 0x001f001f;
    volatile unsigned int *v = (volatile unsigned int *)0xa5000000;
    for (unsigned int i = 0; i < 0x100000 / 4; i++) v[i] = px;
    /* Round-9 lesson: a mute color is half a diagnosis. Paint code/a/b as hex
     * ON the fill (cyan digits read fine on every fill color) so the TV shows
     * e.g. the failing FAD and error code, not just "red". */
    hex_paint(20, 100, code);        /* unconditional: fatal screens stay verbose */
    hex_paint(20, 114, a);
    hex_paint(20, 128, b);
    for (;;) ;
}
