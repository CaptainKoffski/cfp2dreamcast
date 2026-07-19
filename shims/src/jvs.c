/* Pure JVS translation + the golden has-data reply template. Host-compiled by
 * test/test_host.c and linked into the freestanding shim (no MMIO, no fences). */

/* DC controller GetCondition condition word -> Naomi JVS digital button word.
 * DC buttons are ACTIVE-LOW (KOS controller.h: bit1=B bit2=A bit3=Start bit4=Up
 * bit5=Down bit6=Left bit7=Right); JVS is ACTIVE-HIGH
 * (docs/kb/input-map.md, confirmed by observed presses). */
unsigned short dc_to_jvs(unsigned short dc) {
    unsigned short b = (unsigned short)~dc, j = 0;   /* b: pressed = 1 */
    if (b & (1u << 3)) j |= 0x8000;   /* Start */
    if (b & (1u << 4)) j |= 0x2000;   /* Up    */
    if (b & (1u << 5)) j |= 0x1000;   /* Down  */
    if (b & (1u << 6)) j |= 0x0800;   /* Left  */
    if (b & (1u << 7)) j |= 0x0400;   /* Right */
    if (b & (1u << 2)) j |= 0x0200;   /* A -> Button 1 (rotate CCW / select) */
    if (b & (1u << 1)) j |= 0x0100;   /* B -> Button 2 (rotate CW) */
    return j;
}

/* JVS checksum = (sum of frame bytes [0x1b..0x39]) & 0xff, stored at [0x3a].
 * Mirrors the Flycast emitter's calc_crc (maple_jvs.cpp:2476-2478): the sum runs
 * over everything after the E0 sync. Must be recomputed whenever a button byte
 * changes. */
unsigned char jvs_checksum(const unsigned char *f) {
    unsigned int s = 0;
    int i;
    for (i = 0x1b; i <= 0x39; i++) s += f[i];
    return (unsigned char)s;
}

/* Golden 64-byte has-data JVS digital-read reply (subresp 0x16), reconstructed
 * byte-for-byte from the 34,990x-identical steady-state sub-0x33 frame in
 * docs/kb/phase4-conversion.md §input-ABI (BTN_OFF table). All inputs idle, so
 * the checksum at [0x3a] is 0x22. The shim copies this, writes the P1 JVS word
 * big-endian at [0x20]/[0x21], and recomputes [0x3a]. Bytes [0x04..0x3a] are
 * structural (maple sub-header, E0 sync @0x1a, len @0x1c, status/report bytes,
 * coin @0x24, 8x idle 0x8000 analog @0x2a) -- only [0x3b..] is padding. */
const unsigned char jvs_hasdata[64] = {
    0x87,0x00,0x20,0x0f, 0x16,0xff,0xff,0xff, 0x00,0xff,0xff,0xff, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x8e,0x01, 0x00,0x21,0xe0,0x00, 0x1e,0x01,0x01,0x00,
    0x00,0x00,0x00,0x00, 0x01,0x00,0x00,0x00, 0x00,0x01,0x80,0x00, 0x80,0x00,0x80,0x00,
    0x80,0x00,0x80,0x00, 0x80,0x00,0x80,0x00, 0x80,0x00,0x22,0x00, 0x00,0x00,0x00,0x00,
};
