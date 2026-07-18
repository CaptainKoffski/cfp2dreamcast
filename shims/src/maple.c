/* Real DC Maple GetCondition on the port-A main controller, polled (no IRQ).
 * Same Maple DMA hardware the Naomi game already drives; the shim owns the bus
 * once the MIE builders are hooked, so this synchronous one-shot never collides.
 *
 * Frame + register layout verified against KOS
 * (kernel/arch/dreamcast/hardware/maple/maple_queue.c:50-63, dc/maple.h:139-174):
 *   word0 = length | (port<<16) | (last<<31)
 *   word1 = recv_buf phys (& MEM_AREA_CACHE_MASK)
 *   word2 = cmd | (maple_addr(port,unit)<<8) | ((port<<6)<<16) | (length<<24)
 *   word3.. = param words
 *   port A = 0, maple_addr(portA, main) = 0x20, GETCOND = 9,
 *   FUNC_CONTROLLER = 0x01000000, reply resp code DATATRF = 8.
 * TX/RX buffers live in shim home and are accessed via P2 (uncached): the DMA
 * writes RX in RAM, so the CPU must read it uncached to see fresh data. */
#include "shim_iface.h"
typedef unsigned int u32;

#define SB_MDSTAR (*(volatile u32 *)0xa05f6c04)   /* DMA start address */
#define SB_MDTSEL (*(volatile u32 *)0xa05f6c10)   /* trigger select (0 = SW) */
#define SB_MDEN   (*(volatile u32 *)0xa05f6c14)   /* DMA enable */
#define SB_MDST   (*(volatile u32 *)0xa05f6c18)   /* start / status */

/* Returns the DC button word (ACTIVE-LOW), or 0xffff (= all released) if no
 * controller / failed reply. dc_to_jvs(0xffff) == 0, so "no pad" reads as idle. */
unsigned short maple_getcond(void) {
    volatile u32 *tx = P2(MAPLE_TX);
    volatile u32 *rx = P2(MAPLE_RX);
    rx[0] = 0;                                              /* clear old reply header */
    tx[0] = 0x80000000u | (0u << 16) | 1u;                 /* last | port A | 1 param word */
    tx[1] = MAPLE_RX & 0x1fffffff;                         /* recv addr (phys) */
    tx[2] = (1u << 24) | (0u << 16) | (0x20u << 8) | 9u;   /* len | src portA | dst A-main | GETCOND */
    tx[3] = 0x01000000u;                                   /* FUNC_CONTROLLER */
    SB_MDTSEL = 0;
    SB_MDSTAR = MAPLE_TX & 0x1fffffff;
    SB_MDEN = 1;
    SB_MDST = 1;
    while (SB_MDST & 1) ;   /* ponytail: bare poll like gd.c; maple DMA always self-clears (HW timeout) */
    if ((rx[0] & 0xff) != 8) return 0xffff;                /* not DATATRF -> no/failed reply */
    return (unsigned short)(rx[2] & 0xffff);               /* cont_cond.buttons (active-low) */
}
