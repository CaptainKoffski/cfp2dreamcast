/* GD-ROM via DC BIOS syscall vector 0x8c0000bc. Polling only, no IRQs.
 * PIOREAD, not DMAREAD: no G1-DMA side effects in game context (the game's own
 * G1 regs are mirrored; the BIOS PIO path uses the ATA task-file regs instead).
 *
 * Constants + ABI cross-checked against KOS (tools/kos):
 *   vector 0x8c0000bc = VEC_MISC_GDROM  (kernel/arch/dreamcast/hardware/syscalls.c:26)
 *   call ABI syscall(r4,r5,r6=superfn,r7=func); SUPER_FUNC_GDROM=0 (syscalls.c:55,
 *     macros :98-116) -> r6=0 always here
 *   func r7: SEND_COMMAND=0, CHECK_COMMAND=1, EXEC_SERVER=2 (syscalls.c:62-64)
 *   CMD_PIOREAD=16 (include/dc/syscalls.h:256)
 *   param block {start_sec,num_sec,buffer,is_test} (include/dc/syscalls.h:290-295)
 *   status: FAILED=-1, NOT_FOUND=0, PROCESSING=1, COMPLETED=2, STREAMING=3,
 *     BUSY=4 (include/dc/syscalls.h:439-444). done = COMPLETED only; NOT_FOUND
 *     and <=FAILED are errors -- matches KOS's read path (cdrom.c:124-132 done
 *     predicate + :197-198 which maps a bare NOT_FOUND to ERR_NO_ACTIVE).
 *     safe: COMPLETE precedes any NOT_FOUND on both flycast and real BIOS.
 * ponytail: PIO read speed is fine under emulation; DMAREAD is the Phase 5
 * upgrade path if real-hardware streaming stutters (it would then need a
 * dcache_inval on the dest, see cart.c cart_read). */
#include "shim_iface.h"
typedef unsigned int u32;
typedef int (*gdc_t)(u32, u32, u32, u32);

#define GDC       ((gdc_t)(*(volatile u32 *)0x8c0000bc))
#define CMD_PIOREAD   16
#define GD_SEND       0   /* r7 func codes (superfn r6 = 0) */
#define GD_CHECK      1
#define GD_EXEC       2   /* exec-server / drive main loop */
#define GD_NOT_FOUND  0
#define GD_COMPLETED  2
#define GD_FAILED    -1

int gd_read_sectors(void *dst, u32 fad, u32 n) {
    u32 param[4], stat[4];
    param[0] = fad; param[1] = n; param[2] = (u32)dst; param[3] = 0;
    int req = GDC((u32)CMD_PIOREAD, (u32)param, 0, GD_SEND);
    if (req <= 0) return -1;                 /* send failed / bad handle */
    for (;;) {
        GDC(0, 0, 0, GD_EXEC);               /* pump the drive state machine */
        int s = GDC((u32)req, (u32)stat, 0, GD_CHECK);
        if (s == GD_COMPLETED) return 0;
        if (s == GD_NOT_FOUND || s <= GD_FAILED) return -2;  /* not busy, no completion */
        /* PROCESSING(1)/STREAMING(3)/BUSY(4): keep polling */
    }
}
