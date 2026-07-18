# Phase 4 — conversion build notes & analysis results

Analysis results feeding the Phase 4 static-conversion patches. Every bound
below is cited to an instruction address in the boot binary
(`tools/boot.bin`, imported at base `0x8c020000`) and, where the bound is a
literal, to the 32-bit pool word that holds it. Pool words were read directly
from `tools/boot.bin` (file offset = addr − `0x8c020000`, little-endian) and,
where a pool word is itself an in-image pointer, dereferenced one level.

---

## V1 — init RAM-write range (BIOS-GD-syscalls vs raw-ATA gate)

**Question (task brief / spec §2):** the DC port jumps into init at
`0x8c021000` (via trampoline `0x8c04ae2c`, `boot-binary.md` §2). init "zeroes
RAM" (`boot-binary.md` §2). The planned disc-access shim calls the DC BIOS
GD-ROM syscall, whose **vector table + work area live in `0x8c000000–0x8c007fff`**
(GD-ROM vector pointer at `0x8c0000bc`, see citations). If init's zeroing
covers that area, the shim must instead carry a raw ATA driver (STOP-and-revise
gate). Also check the planned shim home `0x8cfc0000–0x8cffffff`.

### Method

`scripts/ghidra/run.sh script DisasmRange.java <start> <end>` (new this task;
listing over an address range on the already-auto-analysed program). init and
its callees were disassembled, every clear/copy/fill loop identified, and each
loop's bound registers chased to their pool-literal / pointer-table constants.

### init structure — this is a compiler crt0, not a hand memset

init (`0x8c021000`) is a standard SH-4 C-runtime startup:

- `0x8c021000` MMIO reg store `*(0xa05f811c)=0xff` (Holly reg; not RAM) then
  two cache-init `jsr`s run in P2-uncached space (`0x8c02100c`→`0xac0210c4`,
  `0x8c021042`→`0xac0210b8`; CCR/cache ops, not RAM fills).
- `0x8c02104c` `jsr @r3` → **`FUN_0x8c094a68`** with `r4=0xac00fc00, r5=0,
  r6=0x400` — a byte `memset(dest,val,len)` (loop body `0x8c094a72`–`0x8c094a7a`,
  `mov.b r5,@r0`). **Zeroes phys `0x8c00fc00`–`0x8c00ffff`** (1 KB, via the
  uncached mirror `0xac00fc00`).
- `0x8c02108c` `mov r0,r15` sets SP = `0x8c00f400` (pool `0x8c0210a4`);
  `0x8c021090` `ldc r0,VBR` sets VBR = `0x8c00f400` (pool `0x8c0210b0`).
- `0x8c021104` `mov.l @r0,r15` sets the definitive SP = `*[0x8c0c44a4]` =
  `0x8c00f000` (pool `0x8c021118` holds the pointer `0x8c0c44a4`). Reconciles
  `boot-binary.md` §3.
- `0x8c021108` `jmp @r0` → `0x8c021150` (pool `0x8c02111c`), the data/bss init.
- `0x8c021176` `bsr 0x8c0211e2` (block G) then `0x8c02117a` `bsr 0x8c021188`
  (blocks C–F).
- `0x8c02122c`+ are `.init_array`/ctor walkers (`jsr @r2` over a function-pointer
  table); they run application constructors — out of V1 scope.

### Every RAM-writing loop/call in init

| # | Site (loop body) | Kind | Range written | Bound evidence |
|---|---|---|---|---|
| memset | `FUN_0x8c094a68` `0x8c094a72` | **zero** (byte) | `0x8c00fc00`–`0x8c00ffff` (1 KB) | args at `0x8c02104c`: r4=pool`0x8c021060`=`0xac00fc00`, r6=pool`0x8c021054`=`0x400`, r5=0 |
| G | `0x8c0211f8` | copy (non-zero code stubs) | `0x8c000000`–`0x8c00001f` (32 B) | dest=pool`0x8c02133c`=`0x8c000000`, end=pool`0x8c021340`=`0x8c000020`; src table `0x8c0a20f8` |
| G′ | stores `0x8c021218`, `0x8c02121e`, `0x8c021222` | conditional non-zero word stores | `0x8c000010`, `0x8c000018`, `0x8c00001c` (3 words) | vals `0x002b003b`/`0x402b9401`/`0x0100e501` (pools `0x8c02134c`/`0x8c021354`/`0x8c02135c`) → dests pools `0x8c021350`=`0x8c000010`, `0x8c021358`=`0x8c000018`, `0x8c021360`=`0x8c00001c` |
| A | `0x8c02115a` | fill `0x41474553` ("SEGA") | `0x8c00c000`–`0x8c00efff` (12 KB) | start=pool`0x8c0212fc`=`0x8c00c000`, fill=pool`0x8c0212f8`=`0x41474553`, end=`*[pool 0x8c0212f4=0x8c0c44a4]`=`0x8c00f000` |
| B | `0x8c02116c` | fill `0x41474553` ("SEGA") | `0x8c1f3480`–`0x8c1f349f` (32 B) | start=`*[pool 0x8c021304=0x8c0a1fcc]`=`0x8c1f3480`, end=`*[pool 0x8c021300=0x8c0a1fd0]`=`0x8c1f34a0` |
| E | `0x8c0211bc` | **zero** (byte) | `0x8c0daf80`–`0x8c0fd8df` (~138 KB) | dest=`*[pool 0x8c021324=0x8c0a2028]`=`0x8c0daf80`, end=`*[pool 0x8c021320=0x8c0a2034]`=`0x8c0fd8e0` |
| C | `0x8c021192` | **zero** (word) | `0x8c0fd8e0`–`0x8c1f347f` (~981 KB) | start=`*[pool 0x8c021310=0x8c0a1fbc]`=`0x8c0fd8e0`, end=`*[pool 0x8c02130c=0x8c0a1fc8]`=`0x8c1f3480` |
| D | `0x8c0211a8` | byte copy | **empty** (dest==end==`0x8c0daf80`) | dest=`*[0x8c0a1fac]`=`0x8c0daf80`, end=`*[0x8c0a1fb8]`=`0x8c0daf80` |
| F | `0x8c0211d2` | byte copy | **empty** (dest==end==`0x8c0daf80`) | dest=`*[0x8c0a2018]`=`0x8c0daf80`, end=`*[0x8c0a2024]`=`0x8c0daf80` |

Notes:
- **E + C are contiguous** → the BSS clear is one region **`0x8c0daf80`–`0x8c1f347f`**
  (~1.12 MB), byte-clear up to `0x8c0fd8e0` then word-clear to the end.
- D and F (the `.data` ROM→RAM copies) are no-ops: the game runs from the same
  RAM image it was loaded into, so `.data` is already in place. Nothing to relocate.
- Block A is stack painting: it fills the stack window `0x8c00c000`–`0x8c00f000`
  (top = the SP `0x8c00f000`) with the pattern "SEGA" — a high-water-mark canary.
  Contains the observed runtime stack `0x8c00e6e8`–`0x8c00ef28` (`boot-binary.md` §3).
- Block G copies 8 words that decode as tiny SH-4 handler stubs
  (`0x0009`=nop, `0x002b`=rte, `0x000b`=rts, `0xaffd`=bra) to `0x8c000000`. It is
  a **non-zero code write, not a zeroing loop**, and the game's VBR is
  `0x8c00f400`, so these are not the game's exception vectors — just 32 bytes the
  game parks at the base of RAM.
- Row G′: immediately after the copy loop, three more word stores overwrite
  three of those stub slots — but only conditionally. The guard at
  `0x8c02120c`–`0x8c021210` loads `*[0x8c004000]` (ptr from pool `0x8c021348`)
  and compares it to `0x000b003b` (pool `0x8c021344`); on mismatch,
  `bf 0x8c021224` skips all three stores. Note the guard **reads** `0x8c004000`
  — inside the syscall window — so on DC the branch outcome depends on whatever
  the BIOS left there; the stores may or may not fire. Either way all three
  dests stay within `0x8c000000`–`0x8c00001f`.
- This table is the complete inventory of **crt0's own** RAM writes (init
  through the `rts` at `0x8c021228`, plus the `FUN_0x8c094a68` call). The
  `.init_array` walkers at `0x8c02122c`+ run application constructors via
  function-pointer tables — out of V1 scope (they are ordinary game code, not
  startup zeroing).

### Verdict — syscall area `0x8c000000–0x8c007fff`

DC BIOS GD-ROM syscall infrastructure (authoritative addresses):
- Vector pointers `0x8c0000b0`–`0x8c0000e0`; **GD-ROM vector = `0x8c0000bc`**
  (`VEC_MISC_GDROM = MEM_AREA_P1_BASE | 0x0C0000BC`,
  `tools/kos/kernel/arch/dreamcast/hardware/syscalls.c:26`; identical in
  `tools/flycast-src/core/reios/reios.cpp:39` `dc_bios_syscall_gd 0x8C0000BC`).
- On flycast's HLE BIOS the vectors are patched to point at syscall stubs at
  `0x8c001000`–`0x8c001008` (`reios.cpp:648-654`, `setup_syscall(0x8C001006,
  dc_bios_syscall_gd)`).

**None of init's zeroing touches the syscall area.** The zero ranges are
`0x8c00fc00`–`0x8c00ffff` (memset), `0x8c0daf80`–`0x8c1f347f` (BSS) — all far
**above** `0x8c007fff`. The lowest zero byte is `0x8c00fc00` (memset), well clear.

**→ Syscall area SURVIVES the zeroing. GATE NOT TRIPPED.** The BIOS GD-syscall
plan stands; no raw ATA driver is forced by init.

Caveat (watch item, not a gate trip): init writes **32 non-zero bytes to
`0x8c000000`–`0x8c00001f`**, inside the declared syscall window, from **four
write sites**: the block G loop store (`0x8c0211fe`) plus the three conditional
stores (`0x8c021218`, `0x8c02121e`, `0x8c021222` — row G′). All four stay
*below* the syscall vector table (`0x8c0000b0`+) and below the HLE syscall
stubs (`0x8c001000`+), so the GD-ROM vector `0x8c0000bc` and its handler are
untouched — on flycast/reios these writes are harmless (that region holds
nothing the syscalls use). They could not be proven harmless on **real** BIOS
statically: the DC boot reserves the low 64 KB (KOS loads at `0x8c010000`), and
whether the real BIOS gdrom driver keeps state in `0x8c000000`–`0x8c00001f`
cannot be resolved without disassembling the real BIOS. If real-hardware
testing later shows a GD syscall fault right after init, revisit these writes;
any neutralizing patch must cover **all four store sites** (patching only the
loop store at `0x8c0211fe` would leave the three conditional stores live) —
e.g. redirect the dest pools `0x8c02133c`/`0x8c021340` and
`0x8c021350`/`0x8c021358`/`0x8c021360`, or branch over
`0x8c0211f2`–`0x8c021222`. Still a few-word patch, not a driver rewrite.

### Verdict — shim home `0x8cfc0000–0x8cffffff`

The last byte init writes is `0x8c1f349f` (block B; its end bound `0x8c1f34a0`
is exclusive). Every init write is ≤ `0x8c1f349f`, over 14 MB below `0x8cfc0000`.

**→ Shim home SAFE — init never touches `0x8cfc0000+`.**

### Decision

- **Shim disc access = BIOS GD-ROM syscalls** (vector `0x8c0000bc`). Raw ATA
  driver NOT required.
- **Shim home `0x8cfc0000+` needs no relocation and no memset-bound patch.**
- Low-priority watch item: init's 32-byte code-stub writes at `0x8c000000`
  (four store sites: `0x8c0211fe`, `0x8c021218`, `0x8c02121e`, `0x8c021222`) —
  re-check only if a real-BIOS GD syscall faults immediately post-init; any
  patch must cover all four sites.

### Reproduction

```sh
# init + crt0 body
scripts/ghidra/run.sh script DisasmRange.java 0x8c021000 0x8c021200 2>&1 | grep 'DisasmRange.java> 8c021'
scripts/ghidra/run.sh script DisasmRange.java 0x8c0211e2 0x8c0212f4 2>&1 | grep 'DisasmRange.java> 8c021'
# memset helper called at 0x8c02104c
scripts/ghidra/run.sh script DisasmRange.java 0x8c094a68 0x8c094ab0 2>&1 | grep 'DisasmRange.java> 8c094'
```

Pool/pointer resolution + gate self-check (asserts the bounds this section
claims — fails loudly if any pool word moves):

```sh
python3 scripts/test_v1_ranges.py    # -> OK: all V1 bounds verified
```

DC syscall vector citations:
```sh
grep -n 'VEC_MISC_GDROM\|VEC_SYSINFO' tools/kos/kernel/arch/dreamcast/hardware/syscalls.c
grep -n 'dc_bios_syscall_gd\|setup_syscall(0x8C0010' tools/flycast-src/core/reios/reios.cpp
```

---

## V2 — shim-home write-watch (dynamic, whole-run)

**Question (task brief):** V1 proved *init* never touches the planned shim home
phys `0x0cfc0000`–`0x0cffffff` (P1 `0x8cfc0000+`). Does the *running game* ever
write there? If yes, the shim home must move.

### Method

Instrumented Flycast (`patches/flycast-instrument.diff`,
`core/hw/naomi/naomi.cpp` `cartlog_shimwatch()`): a content scan of mem_b
offsets `0x00fc0000`–`0x00ffffff`, sampled at the same every-64th-cart-DMA
cadence as the `WATERMARK` scan, emitting `SHIMWATCH addr=` on the first
non-zero byte found. A **content scan, not a write-intercept**, because the
arm64 dynarec's fast path (`core/rec-ARM64/rec_arm64.cpp`
`GenWriteMemoryFast`/`GenWriteMemoryImmediate`) stores straight into host RAM,
bypassing every C-level write function — a `WriteMem` hook would miss most
game writes with the dynarec on. mem_b is zeroed on hard reset
(`core/hw/sh4/sh4_mem.cpp` `mem_Reset` → `mem_b.zero()`), so any non-zero byte
must have been written during the run. Parser check: `shim_home_clean`
(PASS iff zero `SHIMWATCH` lines).

### Capture

`scripts/capture.sh attract 600` — one unattended 600 s Naomi-mode run
(dynarec ON), **coverage = boot + attract + demo mode (unattended)**; no human
play this pass. Demo mode exercises gameplay (Phase 2: demo covered ~99% of
streaming; hands-on play added only 5 DMAs). Log `capture-attract.log`
(repo root, untracked): 108,648 lines — 865 CARTDMA, 35,759 MIERESP,
35,758 MAPLEPC, 35,355 JVSREPORT, 42 WATERMARK (= 14 scan samples × 3
regions), **0 SHIMWATCH**.

### Verdict

**`CHECK shim_home_clean: PASS` — zero SHIMWATCH lines across the run.**
The game never left a non-zero byte in `0x0cfc0000`–`0x0cffffff`
(scanned 14 times through boot → attract → demo). Shim home stands;
no relocation of `shim_iface.h`'s choice needed.

Margin note (honest caveat, not a trip): this run's main-RAM watermark
reached `0x00f80040` (15.5 MB — higher than Phase 2's 11.2 MB DMA high-water;
the game does write above the asset region), which is only 0x3ffc0 (~256 KB)
below the shim window base `0x00fc0000`. The window itself stayed all-zero.
Sampling caveat: a write that was fully re-zeroed between two 64-DMA samples
would evade the scan — same accepted trade-off as the WATERMARK scan.

### Reproduction

```sh
scripts/capture.sh attract 600
python3 scripts/parse_cart_log.py capture-attract.log   # -> CHECK shim_home_clean: PASS
```

---

## V3 — cart-DMA completion-wait mechanism

**Question (spec §4 V3):** does the game wait for the G1 cart DMA by *polling*
a register or by *sleeping on an IRQ*? Poll → the register-mirror handles it
for free (the poll lands in the mirror once the descriptor base is repointed);
IRQ/sleep → the patch table needs an extra branch-over. Candidate wait:
`FUN_8c03bc12`, called immediately after the `SB_GDST` trigger
(`boot-binary.md` §4).

### Method

`scripts/ghidra/run.sh script DisasmRange.java 0x8c03bc12 0x8c03bd08`; the
loop-called function pointer and the base-relative offsets were resolved to
their pool words (16-bit `.word` / 32-bit `.long`, read from `tools/boot.bin`,
file offset = VA − `0x8c020000`).

### FUN_8c03bc12 is a base-relative poll loop (verdict: POLL, not IRQ)

`FUN_8c03bc12(r4=flag, r5=descriptor)` (`0x8c03bc12`–`0x8c03bc72`):

```
8c03bc20  LOOP: mov.l @(0x70,r14),r2 ; r2 = [desc+0x70]  (mode flag)
8c03bc24        tst r2,r2
8c03bc26        bf   0x8c03bc50       ; mode!=0 → skip yield
8c03bc2e        jsr  @r11             ; r11 = *0x8c03bd00 = FUN_8c09dfe4  (task yield)
8c03bc32        mov  #0x58,r0
8c03bc36        mov.l @(r0,r14),r3    ; r3 = [desc+0x58]  = G1 base (0xa05f7000)
8c03bc3a        add  r5,r3            ; r5 = *0x8c03bcf6 = 0x0418  → base+0x418 = SB_GDST
8c03bc3c        mov.l @r3,r2          ; r2 = *(base+0x418)  ← READ SB_GDST
8c03bc3e        tst  r2,r13           ; r13 = 1  → test bit0
8c03bc40        bf   0x8c03bc58       ; bit0 SET (busy) → 0x8c03bc58 → loops back to 0x8c03bc20
                ; bit0 CLEAR (done): read base+0x4f8 (*0x8c03bcfa) → [desc+0x5c], return
```

- The exit condition is `*(base+0x418) & 1 == 0` (SB_GDST bit0 clear = "DMA no
  longer in progress"). `base = [desc+0x58]` = the descriptor register base
  (`0xa05f7000`, see cart-patch-sites). This is a **descriptor-base-relative
  read** → it lands in the shim mirror once the base is repointed.
- Pool offsets (`tools/boot.bin`): `0x8c03bcf6 = 0x0418` (SB_GDST),
  `0x8c03bcf8 = 0x0414` (SB_GDEN), `0x8c03bcfa = 0x04f8` (G1 status, → desc+0x5c),
  `0x8c03bcfc = 0x0084`. All are offsets added to `base`, so **every register
  access in the wait is base-relative** — none is an absolute pool literal.
- `*0x8c03bd00 = 0x8c09dfe4` is **not** a sleep/IRQ primitive: `FUN_8c09dfe4`
  (`0x8c09dfe4`+) saves `PR/MACH/MACL/r14..r11` to a context block at
  `*0x8c09e0b4` — a **cooperative task-yield / context switch**. So the loop is
  a *yield-between-polls busy-wait*, not an interrupt wait. The exit still
  depends 100 % on the register read at `0x8c03bc3c`.
- The sibling arm helper `FUN_8c03bbe8` (`0x8c03bbe8`, called before the
  trigger) performs the same base-relative poll: writes `base+0x414`
  (SB_GDEN) = 0 and spins on `base+0x418` bit0 (`0x8c03bc02`–`0x8c03bc0a`).
- `*0x8c03bd04 = 0xdeaddead` is a poison sentinel compared in the post-DMA
  handler `FUN_8c03bc74` — not part of the wait.

### Verdict + chosen intercept

**POLL, base-relative.** No interrupt/sleep wait exists, so **no branch-over
patch is required**. Because `FUN_8c03bc12`, `FUN_8c03bbe8`, and the runtime
trigger all read/write `SB_GDST`/`SB_GDEN` as `base+0x418`/`base+0x414` with
`base = [desc+0x58]`, repointing the descriptor base (cart-patch-sites, patch
#1) makes the whole wait land in the mirror. The shim keeps **`mirror+0x418`
bit0 = 0** (DMA idle/done) after each synchronous service, and the first poll
iteration exits.

- **Chosen default (belt-and-suspenders): entry-hook `FUN_8c03bc12` →
  `shim_cart_service`** (serve the read, ensure completion visible, return).
  Works for any polling variant and removes the cooperative yield from the hot
  path. Recorded for Task 12.
- Even without the hook, the mirror alone satisfies the wait (poll reads
  `mirror+0x418` = 0). The hook is the safety margin, not a necessity.

Every config-time `SB_GDST` reader uses the *same* wait-for-clear test
(`tst r2,r2; bf` loops while `SB_GDST != 0`: `FUN_8c08074a`, `FUN_8c080868`,
`FUN_8c081c76`, `FUN_8c081d68/d86`, `FUN_8c081efc`), so a single invariant —
**mirror `+0x418` reads 0 when idle** — satisfies both the runtime and the
config-time pollers.

### Reproduction

```sh
scripts/ghidra/run.sh script DisasmRange.java 0x8c03bbe8 0x8c03bd06 2>&1 | grep 'DisasmRange.java> 8c03'
scripts/ghidra/run.sh script DisasmRange.java 0x8c09dfe4 0x8c09e002 2>&1 | grep 'DisasmRange.java> 8c09'
# pool offsets: 0x8c03bcf6=0418 0x8c03bcf8=0414 0x8c03bcfa=04f8 0x8c03bd00=8c09dfe4
```

---

## cart-patch-sites — cart/G1 register-mirror patch list (Task 6)

The exact list Task 12 turns into patch definitions for the **register-mirror**
cart-DMA shim (spec §3). Every game store/load to the Naomi cart/G1 registers
(`0x5f7000`–`0x5f7014` cart offset/count, `0x5f7400`–`0x5f74ff` G1 DMA channel)
must be repointed to a shim-owned **mirror block** so nothing hits the DC's
real GD-ROM ATA registers at the same addresses (`naomi-vs-dreamcast.md` §3
collision). Reads of the mirror are served by the shim; the `SB_GDST` trigger
becomes a shim call (Task 12).

### The mirror block

A ≥ `0x500`-byte (round to `0x800`) block in shim home
(`0x8cfc0000`–`0x8cffffff`, V2-verified), laid out so **`MIRROR + 0xYYY`
stands in for register `0x5f7YYY`**. Repoint values use the **P2-uncached**
alias (`0xA0000000 | mirror_phys`, written `MIRROR_P2` below) to match the
game's original `0xa05f7xxx` accesses and to keep shim/game views coherent
without cache flushes. Offsets actually used span `0x00c`–`0x4b8`.

### Method

`scripts/ghidra/ListPoolWords.java` (new this task) raw-scans every 4-aligned
32-bit word whose value masked to 29-bit phys lands in `[0x5f7000, 0x5f7800)`
and prints its referencing instructions + functions; cross-checked against the
Phase-3 operand+data scanner `FindMmioXrefs.java` (identical 13-site active
set) and against `getReferencesTo` per word. Each active pool word was then
confirmed via `DisasmRange` to be used **only** by cart/G1 config/streaming
code (no unrelated sharer).

```sh
scripts/ghidra/run.sh script ListPoolWords.java 0x005f7000 0x005f7800 2>&1 | grep POOLWORD
scripts/ghidra/run.sh script FindMmioXrefs.java 2>&1 | grep -E 'block=(cart|g1dma)'
```

### Patch #1 — descriptor-base source (PRIMARY; covers ALL runtime streaming)

The runtime trigger `FUN_8c03bd08` and its wait/arm helpers read the G1
register base from `[desc+0x58]` and add offsets (`boot-binary.md` §4). That
base is set **once, at descriptor construction**, from a single pool word:

- **Getter** `FUN_8c02d9a6` (`0x8c02d9a6`): `mov.l 0x8c02da74,r0; rts` — returns
  `*0x8c02da74 = 0xa05f7000`. Pool word `0x8c02da74` is referenced **only** by
  this getter (`getReferencesTo` → 1 ref).
- **Constructor** `FUN_8c02dd20`: `0x8c02dd2c bsr 0x8c02d9a6` (getter → r0),
  then `0x8c02dd30 mov #0x58,r1; add r14,r1; 0x8c02dd38 mov.l r0,@r1` →
  **`*(desc+0x58) = 0xa05f7000`**. `FUN_8c02dd20` (via `FindRefs`) is the
  getter's sole caller and the sole writer of `+0x58` (all other `+0x58`
  accesses in the `0x8c03bxxx` cluster are reads).

| pool word | value | ref | rewrite |
|---|---|---|---|
| `0x8c02da74` | `0xa05f7000` | `FUN_8c02d9a6` (only) | → `MIRROR_P2 + 0x000` |

**Consumers auto-covered by patch #1** (all read `base=[desc+0x58]`, add a const):
`FUN_8c03bd08` writes `base+0x414` (SB_GDEN, `0x8c03bd1e`) and `base+0x418`
(SB_GDST trigger, `0x8c03bd26`); `FUN_8c03bc12`/`FUN_8c03bbe8` poll
`base+0x418`; `FUN_8c03b81a` writes `base+0x404/0x408/0x40c/0x4b8`
(`0x8c03b88e`–`0x8c03b8ea`, offsets `0x8c03b984..0x8c03b98a`). **Because these
are `base + const`, once `base = MIRROR_P2` every one lands in the mirror
automatically — confirmed.** (The `SB_GDST`/`SB_GDEN` computed writes the task
flags in `FUN_8c03bd08` therefore need no separate patch.) This is the whole
per-frame streaming path (dynamically: all 460 cart DMAs, `boot-binary.md` §4).

### Patches #2–#13 — config-time absolute pool literals

Config/region-setup code programs the G1 channel via **absolute** pool literals
(no descriptor). Each would hit real ATA on DC; each pool word is exclusive to
its function (cross-checked). `MIRROR_P2 + 0xYYY` = repoint target.

| # | pool word | value | reg | function(s) @ load site | access | rewrite |
|---|---|---|---|---|---|---|
| 2 | `0x8c08071c` | `0xa05f74b8` | GDEN cfg `74b8` | `FUN_8c08063c` @`0x8c08063c` | WRITE | `MIRROR_P2+0x4b8` |
| 3 | `0x8c080720` | `0xa05f7480` | GDSTAR `7480` | `FUN_8c08063c` @`0x8c080642` | WRITE | `MIRROR_P2+0x480` |
| 4 | `0x8c080724` | `0xa05f7484` | GDLEN `7484` | `FUN_8c08063c` @`0x8c080648` | WRITE | `MIRROR_P2+0x484` |
| 5 | `0x8c080728` | `0xa05f7490` | GDDIR `7490` | `FUN_8c08063c` @`0x8c080656` | WRITE | `MIRROR_P2+0x490` |
| 6 | `0x8c08072c` | `0xa05f74a4` | `74a4` | `FUN_8c08063c` @`0x8c08065a` | WRITE | `MIRROR_P2+0x4a4` |
| 7 | `0x8c0807d8` | `0xa05f7418` | SB_GDST `7418` | `FUN_8c08074a` @`0x8c08074a` | READ | `MIRROR_P2+0x418` |
| 8 | `0x8c0808e4` | `0xa05f7418` | SB_GDST `7418` | `FUN_8c080868` @`0x8c080868` | READ | `MIRROR_P2+0x418` |
| 9 | `0x8c080904` | `0xa05f700c` | cart `700c` | `FUN_8c080868` @`0x8c080874` | WRITE | `MIRROR_P2+0x00c` |
| 10 | `0x8c080e3c` | `0xa05f700c` | cart `700c` | `FUN_8c080d18` @`0x8c080d3c` | WRITE | `MIRROR_P2+0x00c` |
| 11 | `0x8c081d24` | `0xa05f7418` | SB_GDST `7418` | `FUN_8c081c76` @`0x8c081c78`, `FUN_8c081aee` @`0x8c081b90` | READ (poll) | `MIRROR_P2+0x418` |
| 12 | `0x8c081e90` | `0xa05f7418` | SB_GDST `7418` | `FUN_8c081d68` @`0x8c081d6c`, `FUN_8c081d86` @`0x8c081d86` | READ (poll) | `MIRROR_P2+0x418` |
| 13 | `0x8c081ff8` | `0xa05f7418` | SB_GDST `7418` | `FUN_8c081efc` @`0x8c081fac` | READ (poll) | `MIRROR_P2+0x418` |

**Computed-offset writes inside `FUN_8c08063c` are auto-covered by #2–#6.** The
function loads the pool literals as *bases* then reaches adjacent registers by
constant arithmetic: `add #-0x30`/`add #0xc` off r2 (from `74b8`) → `7488`,
`7494`; `add #0xc` off r3 (from `7480`) → `748c`; `add #0x10` off r7 (from
`7490`) → `74a0` (`0x8c08064e`–`0x8c080748`). Since the deltas are constants
and the mirror preserves relative layout, repointing the 5 base literals
redirects the computed writes too — no extra patch.

**Config-time `SB_GDST` reads (#7,#8,#11,#12,#13) share the wait-for-clear
semantics of V3** (`tst r2,r2; bf` loops while `SB_GDST != 0`). With the shim
holding `mirror+0x418 = 0` when idle, `FUN_8c08074a` returns "idle",
`FUN_8c080868` proceeds, and the `0x8c081xxx` pollers exit immediately — no
hang. These config-time sites were **not** observed in the dynamic capture
(the `CARTDMAPC` hook logs `SB_GDST` *stores* only, and all 460 were inside
`FUN_8c03bd08`); they are repointed defensively because they are live,
statically-reachable code that would read real GD-ROM ATA on DC.

### Flagged — NOT mirror-repointable (findings)

1. **Generic hardware-register tables** (`0x8c0a39a0`+ and `0x8c0a3fb8`+). These
   are data tables listing DMA-start / Holly registers across **all** channels,
   not cart/G1-exclusive pool literals: e.g. `0x8c0a39a0..` = `{reg, 0}` pairs
   `0x5f6808, 0x5f6820, 0x5f6c18 (SB_MDST maple), 0x5f7418 (SB_GDST), 0x5f7818
   (AICA), 0x5f7838}`; `0x8c0a3fb8..` = a run `0x5f6800/04/10/14, 0x5f6c04/10,
   0x5f7404/08/40c, 0x5f7800/04/08/0c/10`. They hold the G1 regs `0x5f7418`
   (`0x8c0a39b8`) and `0x5f7404/08/40c` (`0x8c0a3fd0/d4/d8`) **among unrelated
   Maple/AICA/PVR regs**, so the G1 entries cannot be blindly repointed without
   desyncing any routine that walks the whole table. `getReferencesTo` returns
   **zero** for every entry (and the neighborhood), so no analyzed code indexes
   them — they are either dead or reached only by a computed-access routine
   Ghidra did not resolve. **If** M2/M3 testing shows a fault touching real
   GD-ROM around a multi-channel DMA quiesce/reset, this needs an
   *instruction-level* patch on that routine, **not** a mirror repoint. LOW
   risk: unreferenced + the dynamic capture (boot→attract→demo→play) never
   triggered a cart DMA outside `FUN_8c03bd08`.

2. **Dead pool slots** holding cart/G1 addresses but with **zero** references
   (confirmed `FindRefs`; not even Ghidra-defined data — raw literal-pool
   filler): `0x8c080620` (`7418`), `0x8c080628` (`703c`), `0x8c080630`
   (`7014`), `0x8c0807e0` (`7000`), `0x8c0807e4` (`7004`), `0x8c0807f0`
   (`7008`), `0x8c0808e8` (`7004`), `0x8c0808f0` (`7000`), `0x8c0808f4`
   (`7014`), `0x8c0808f8` (`7010`), `0x8c080908` (`7404`), `0x8c08090c`
   (`7408`), `0x8c080910` (`740c`), `0x8c081eb4` (`7068`), `0x8c0821c8`
   (`7418`). Not active programming sites → **no patch**; listed for
   completeness so a future re-scan does not re-flag them.

### Summary for Task 12

**13 mirror repoints:** patch #1 (descriptor base `0x8c02da74`) covers the
entire runtime streaming path (trigger + arm + wait, all base-relative);
patches #2–#13 cover the config-time absolute literals. Plus the V3 entry-hook
on `FUN_8c03bc12` (default). No branch-over patch needed (V3 = poll, not IRQ).
Two flagged findings (generic reg tables; dead slots) need no mirror action now.

### Reproduction

```sh
scripts/ghidra/run.sh script ListPoolWords.java 0x005f7000 0x005f7800 2>&1 | grep POOLWORD
scripts/ghidra/run.sh script DisasmRange.java 0x8c02dd20 0x8c02dd3a 2>&1 | grep 'DisasmRange.java> 8c02'  # +0x58 base store
scripts/ghidra/run.sh script DisasmRange.java 0x8c08063c 0x8c080760 2>&1 | grep 'DisasmRange.java> 8c08'  # FUN_8c08063c config writes
# pool values read from tools/boot.bin (offset = VA-0x8c020000, LE):
#   0x8c02da74=a05f7000  0x8c08071c=a05f74b8 ... 0x8c081ff8=a05f7418
```

---

## V4 — MIE response templates + response buffer address

**Question (task brief):** the input/EEPROM shim replaces the MIE (Maple cmd
0x86) transactions. It needs (a) byte-exact reply templates to write, and
(b) where to write them.

### Method

Instrumented Flycast, `core/hw/maple/maple_if.cpp` `maple_DoDma()` (MP_Start
case): after `pDevice->RawDma()` produces the reply and after the `swap_msb`
byte-order fixup — i.e. byte-identical to what the emulator then copies to
guest RAM — log `MIERESP sub=%02x addr=%08x data=<64 bytes hex>` for every
cmd-0x86 transaction. `addr` is the Maple transfer descriptor's second word
(the receive/response address, `header_2`), `sub` is the JVS subcommand
(first payload byte after the frame header). **Direction: these are the MIE's
reply frames (device → host), NOT the game's request** — the dump is taken
from the out-buffer after the reply is built, and its first byte 0x87
(MDRS_JVSReply) confirms it. Extracted with
`python3 scripts/parse_cart_log.py capture-attract.log --dump-mie build/`
(first occurrence per sub → `build/mie_subXX.bin`, ROM-derived, gitignored).

### Results (same 600 s boot+attract+demo capture as V2)

Subcommands captured: 0x01, 0x03, 0x13, 0x15, 0x17, 0x21, 0x27, 0x31, 0x33,
0xff. Templates written: `build/mie_sub01.bin` … `mie_subff.bin` (64 bytes
each, zero-padded past the true reply length).

First bytes (little-endian reply header `[resp][sender][reci][n_words]`,
resp 0x87 = MDRS_JVSReply):

| sub | first 16 bytes | reads as |
|---|---|---|
| 0x01 | `87002001 02000000 …` | 1-word ack, payload `02 00 00 00` (EEPROM ready/status) |
| 0x03 | `87002020 50cb1042 45532009 101a0101` | 32-word EEPROM read: two identical 16-byte halves (`50cb…1111` ×2) — the classic Naomi dual-copy EEPROM image |
| 0x15 | `87002009 16ffffff 00ffffff 00000000` | 9-word **boot JVS-handshake reply** (F1 set-address ack; JVS body `e0 00 04 01 01 05 0b`; the `ff` bytes are maple sub-header placeholders and **byte 0x20 = 0x0b is the JVS checksum, not a button word** — see §input-ABI) |
| 0x33 | `87002005 32ffffff 00ffffff 00000000` | cold/empty receive (subresp `0x32` = no JVS data yet; the steady-state has-data 0x33 frame is `0x0f` words — see §input-ABI) |

Counts: sub 0x15 ×376 (all in the first boot phase), sub 0x33 ×34,991 (the
per-frame steady-state poll — **the input shim must serve sub 0x33, not just
0x15**), sub 0x27 ×360, sub 0x01/0x03 ×2 each (boot-time EEPROM), rest <20.

### Issuing sites per sub (MAPLEPC cross-reference)

This capture ran dynarec ON, so its `MAPLEPC pc=` values are block-granular
(Sh4cntx.pc updates at block boundaries), not instruction-exact; Phase 3's
interpreter capture `capture-pc.log` is the instruction-exact reference.
Both agree on the function attribution:

| sub | this capture (dynarec, block PC) | Phase 3 `capture-pc.log` (interpreter, exact PC) | issuing site |
|---|---|---|---|
| 0x33 | **34,991× pc=8c03c3d6** (100%) | **23,762× pc=8c03c3e4** (100%) | **`FUN_8c03c2c6`** (`0x8c03c2c6`–`0x8c03c4a1`) |
| 0x15 | 359× pc=0c0227a8, 7× 0c0315ca, 7× 8c03c3d6, rest 1–2× | 369× pc=0c03161e, 7× 8c03c3e4 | `0x8c0315ce` routine (369) + `FUN_8c03c2c6` (7) |
| 0x27 | 359× pc=0c02283c | 360× pc=0c03161e | `0x8c0315ce` routine |
| 0x01/0x03 | 1× 0c031570 + 1× 8c03c3d6 each | 1× 0c03161e + 1× 8c03c3e4 each | both sites, boot only |
| 0x13/0x17/0x21/0x31 | ≤9× each, split across both | ≤9× each, split across both | both sites, boot only |

(The dynarec-run sub-15/27 PCs `0c0227a8`/`0c02283c` are caller-side block
entries — `FUN_8c027584` dispatches `0x8c0315ce` as a fn-ptr callback,
`boot-binary.md` §5 — of the same interpreter-exact site `0c03161e`. Both
`8c03c3d6` and `8c03c3e4` fall inside `FUN_8c03c2c6`.)

**Primary/secondary inversion (supersedes the framing in `boot-binary.md` §5;
dated addendum added there):** Phase 3's "primary 369× / minor 7×" counted
only sub-0x15 traffic. The steady-state per-frame input poll is **sub 0x33
from `FUN_8c03c2c6`** (34,991× this capture; 23,762× in Phase 3's own log);
the `0x8c0315ce` site carries the **boot phase** (subs 0x15/0x27, one-off
EEPROM 0x01/0x03). **Task 5 must disassemble BOTH sites**, and the input shim
must answer sub 0x33 with the `mie_sub33.bin` template shape.

### Response buffer address

**Not a single constant.** Three receive buffers observed:

| buffer (phys) | used by |
|---|---|
| `0x0c296220` | boot-phase transactions: sub 0x15 (369/376), 0x01, 0x03, 0x13, 0x17, 0x21, **0x27 (all 360)**, 0x31 |
| `0x0c0fd8e0` / `0x0c1038e0` | steady-state sub 0x33 stream, strictly alternating (17,495 / 17,496× — a double buffer); also the minority boot occurrences of 0x15/0x17 etc. |
| `0x0c1c99a0` | the single sub 0xff (reset broadcast) |

→ **The input shim must take the response address from the Maple transfer
descriptor's second word per-transaction** (as the real hardware does), not
hardcode one. `0x0c296220` is the observed boot/EEPROM buffer;
`0x0c0fd8e0`/`0x0c1038e0` is the per-frame input double buffer. These are
inside the game's own RAM image — no conflict with the shim home.

### Reproduction

```sh
python3 scripts/parse_cart_log.py capture-attract.log --dump-mie build/
grep -oE '^MIERESP sub=[0-9a-f]+ addr=[0-9a-f]+' capture-attract.log | sort | uniq -c
```

---

## input-ABI — the input-shim contract (Task 5)

The exact ABI Task 11 compiles into the input shim. Two MIE Maple sites are
characterized. Every claim is cited to an instruction address or template byte.
Pool words were read from `tools/boot.bin` (file offset = VA − `0x8c020000`,
little-endian; a pool that is an in-image pointer is dereferenced one level).
Reply templates are `build/mie_subXX.bin` (V4). Full evidence chain:
`.superpowers/sdd/task-5-report.md`.

### Headline

| Item | Boot site `0x8c0315ce` | Steady site `FUN_8c03c2c6` |
|---|---|---|
| Reached via | fn-ptr `pool[0x8c027618]=0x8c0315ce`, `jsr @r3` @`0x8c0275ee` | `jsr @r1`, `pool[0x8c02ed6c]=0x8c03c2c6` @`0x8c02ed1c` |
| Subs carried | 0x15,0x27,0x01,0x03,0x13,0x17,0x21,0x31,0xff (boot) | **0x33 per-frame** + one-off boot subs |
| Subcommand src | descriptor word3 low byte = `[cmdblk+0xc]`, cmdblk=`*0x8c0e6400` | descriptor word3 (input slot's frame) |
| Reply-addr src | descriptor word1 = `[cmdblk+0x4]` (= `0x0c296220`) | word1 = `FUN_8c030fba([base+0x10a8+idx*4])` → `[desc+0x04]` @`0x8c03c3d6` (double buf `0x0c0fd8e0`/`0x0c1038e0`) |
| Completion | maple DMA-done (SB_MDST reads 0); reply at recvaddr | `[desc+0x18]` bit0 pending flag; next call returns -1 while set |
| BTN_OFF | **0x20/0x21** P1 word big-endian (P2 @0x22/0x23; JVS checksum @0x3a recomputed) | same |
| EE_OFF | **4** (128-B EEPROM after 1-word header) | n/a |

Both routines build the **same 8-word Maple transfer descriptor** (word0
control, word1 recv-addr, word2 frame-header `cmd 0x86`/`dev 0x20`, word3
subcommand, word4-7 payload). One shared reply-synthesis body serves both;
the per-site differences are the entry hook **and the completion action**
(see contract box).

### Crown jewel — 0x33 vs 0x15 vs 0x27

From the Flycast MIE handler `tools/flycast-src/core/hw/maple/maple_jvs.cpp`
(`MIEImpl::handle_86_subcommand`), which produces the exact byte stream the game
parses:
- **0x15** = *Receive JVS data* (`:1807`) — return the latest completed scan.
- **0x27** = *Transmit with repeat* (`:1854`) — kick a JVS scan (boot two-step:
  0x27 then 0x15).
- **0x33** = *Receive then transmit with repeat* (`:1878`) — return the latest
  scan **and** kick the next in one transaction: the self-sustaining
  steady-state per-frame poll.

⇒ **0x33 is "read latest input + queue next", not "kick".** The shim must do a
**real DC `GetCondition` on every 0x33 (once per frame)** and translate to the
JVS word at reply offset 0x20. No caching needed (0x33 = ~1×/frame). The
`build/mie_sub33.bin` template is the *cold-start empty* variant (subresp 0x32,
no data — captured before any scan completed); the steady-state 0x33 reply is a
has-data reply (subresp 0x16) with the button word at 0x20, identical to 0x15.

### Site A — boot builder `0x8c0315ce` (`0x8c0315ce`–`0x8c03161c`)

**Linkage.** Called as a function pointer from dispatcher `FUN_8c027584`:
`0x8c0275da mov.l 0x8c027618,r5` (`r5 = pool[0x8c027618] = 0x8c0315ce` — the
exact u32 slot; companion param `pool[0x8c027614]=0x00080028`; slot copied at
`0x8c0276c8`), pushed `0x8c0275e0`, popped to r3 `0x8c0275ea`, `jsr @r3`
`0x8c0275ee`. Chosen when `(r10 & 0x20)==0` (`bt`@`0x8c0275b8`,
`pool[0x8c0275cc]=0x20`); sibling builder `pool[0x8c0275d4]=0x8c031640` for the
0x20 case. **Arg r4** = `pool[0x8c027624]=0x8c0e62c8` or
`pool[0x8c027628]=0x8c0a27f4` → descriptor payload words 4-7; r5–r7 unused.

**Descriptor** at `buf = *(*0x8c0e6404)` (`pool[0x8c031620]=0x8c0e6404`;
`+0x20` then eight `mov.l r,@-r5`), with `cmdblk = *0x8c0e6400`
(`pool[0x8c031624]=0x8c0e6400`):

| word | value | site |
|---|---|---|
| w0 `+0x00` control | `[cmdblk+0x00]` | `0x8c03160c-0e` |
| w1 `+0x04` **recv addr** | `[cmdblk+0x04]` (=`0x0c296220`) | `0x8c031606-08` |
| w2 `+0x08` frame hdr | `([cmdblk+0x08] & 0x03efffff) \| 0x20000000` | `0x8c0315fa-0602` (pools `0x8c031628`/`0x8c03162c`) |
| w3 `+0x0c` **subcommand** (low byte) | `[cmdblk+0x0c]` | `0x8c0315f2-f4` |
| w4-7 `+0x10..1c` payload | `[arg0+0x00..0x0c]` | `0x8c0315d0-ee` |

**Completion.** Routine does no Maple MMIO (`mov.l r1,@r0`@`0x8c031618` targets
RAM `pool[0x8c031630]=0x8c0e6670`). The real DMA is the shared maple driver
(pool-literal register writes at `0x8c080e74-90`: `0xa05f6c14` SB_MDEN,
`0xa05f6c04` buffer, `0xa05f6c18` SB_MDST — same static/dynamic split as cart
DMA, §boot-binary §4). Completion = SB_MDST reads 0
(`tools/netboot/docs/naomi.md:138`); the caller polls before reading the reply
at `[cmdblk+0x4]`.

### Site B — steady builder `FUN_8c03c2c6` (`0x8c03c2c6`–`0x8c03c4a1`)

`base = *0x8c0e8410` (`pool[0x8c03c300]`). Per-frame **async** poll.

**Call site.** `0x8c02ed1a mov.l 0x8c02ed6c,r1` (`pool[0x8c02ed6c]=0x8c03c2c6`),
`0x8c02ed1c jsr @r1`, then `mov r0,r12; cmp/pz r12; bf 0x8c02ed70` — caller
treats **return ≥ 0 as OK**, < 0 as retry / reuse-last-frame. Returns `-3`
not-ready (`0x8c03c2e2`), `-1` pending (`0x8c03c312`), `-2` lock (`0x8c03c326`),
`0` success (`0x8c03c490`). No meaningful register args (reads `base`).

**Flow.** (1) `[base+0x0fc0]==1`? else -3 (`0x8c03c2d6-da`, `hwpool
0x8c03c2e8=0x0fc0`). (2) `desc=[base+0x10f4]` (`hwpool 0x8c03c42a=0x10f4`); if
`[desc+0x18]&1` → -1 (`0x8c03c30a-0c`) — **`[desc+0x18]` bit0 = completion
flag**. (3) `tas.b @([pool 0x8c03c444=0x8c03c864]=0x8c03c868)` else -2. (4) frame
build via pump `bsr 0x8c03c1c2`@`0x8c03c396`. (5) **recv addr**:
`FUN_8c030fba([base+0x10a8 + idx*4])` (`idx=[base+0x10b8]`) → `[desc+0x04]`
@`0x8c03c3d6`; `FUN_8c030fba = ptr & 0x0fffffff` (`pool[0x8c030fe8]`, P1→phys);
idx toggled `xor`@`0x8c03c3f6` → alternating `0x0c0fd8e0`/`0x0c1038e0`. (6)
`mov.l r12,@(0x18,r2)`@`0x8c03c3e2` **sets `[desc+0x18]=1`** (pending); return 0.
(pc `8c03c3d6`/`8c03c3e4` = the V4/Phase-3 logged PCs.)

**Completion (async).** The maple completion path (pump / maple-end IRQ) clears
`[desc+0x18]` and copies the reply into game input state; the next frame's call
reads it. **Shim (synchronous): on return, `recvaddr`(=`[desc+0x04]`) must hold
a valid reply and `[desc+0x18]` bit0 must be 0**, else the routine returns -1
forever.

### Shared descriptor (Naomi/DC maple, `tools/netboot/docs/naomi.md:135-142,151`)

```
word0  transfer control (end bit | length)
word1  RECV ADDRESS      <- reply DMA'd here     (shim writes here)
word2  frame header      cmd 0x86, recipient 0x20 = MIE
word3  payload[0]        low byte = SUBCOMMAND   (shim dispatches on this)
word4+ payload[1..]      JVS command bytes (transmit subs)
```

### Reply offsets + per-sub actions

**BTN_OFF = 0x20** (P1 button-word hi byte; the word is big-endian at
0x20/0x21, P2 at 0x22/0x23; **JVS checksum at 0x3a — must be recomputed
whenever a button byte changes**).

*Authoritative frame — the steady-state has-data reply itself.*
`capture-attract.log` carries **34,990 byte-identical** sub-0x33 has-data
replies (all inputs idle); decoded byte-for-byte against the Flycast emitter
that produced them (`tools/flycast-src/core/hw/maple/maple_jvs.cpp`):

| off | bytes (observed) | meaning | emitter |
|---|---|---|---|
| 0x00 | `87 00 20 0f` | maple header, 0x0f words | `reply()` `:1716` |
| 0x04 | `16` | subresp 0x16 = has JVS data | `:1717` |
| 0x05 | `ff ff ff` | placeholder | `:1719-1721` |
| 0x08 | `00 ff ff ff` | `w32(0xffffff00)` | `:1722` |
| 0x0c | `00 ×8` | `w32(0) ×2` | `:1723-1724` |
| 0x14 | `00 00 8e` | 0, channel, sense line | `:1732-1737` |
| 0x17 | `01 00 21` | node 1, status ok, out_len 0x21 | `:1663-1665` |
| 0x1a | `e0 00 1e` | JVS sync, master node, len 0x1e | `:2084-2086`, len fill `:2474` |
| 0x1d | `01` | overall status | `:2220` |
| 0x1e | `01` | report — cmd 0x20 digital read | `:2227` |
| 0x1f | `00` | TEST byte | `:2232` |
| **0x20** | `00 00` | **P1 buttons hi, lo** (`inputs[0]>>8`, `inputs[0]`) | `:2237`, `:2241` |
| 0x22 | `00 00` | P2 buttons hi, lo | same loop, player 2 |
| 0x24 | `01` + `00 ×4` | report + 2 coin slots (cmd 0x21) | `:2248-2267` |
| 0x29 | `01` + `80 00 ×8` | report + 8 analog ch, idle 0x8000 (cmd 0x22) | `:2273`, `:2370-2371` |
| 0x3a | `22` | **JVS checksum** | `:2476-2480` |

Checksum formula (`:2476-2478`, `calc_crc` sums `buffer_out[1..]`, i.e.
everything after the `E0` sync): `[0x3a] = (Σ bytes [0x1b..0x39]) & 0xff`.
Verified on the observed frame: constant bytes sum to `0x22` = the logged
checksum. The three report blocks also decode the game's boot-stored JVS
repeat request (sub 0x13, stored len 7): `20 02 02 | 21 02 | 22 08` —
digital 2 players × 2 bytes, coins 2 slots, analog 8 channels.

⇒ **BTN_OFF = 0x20/0x21, P1 word big-endian** (`JVS_OUT(inputs[player]>>8)`
then `JVS_OUT(inputs[player])`, `:2237`/`:2241`); P2 = 0x22/0x23. Active-high,
idle `0x0000` (`input-map.md`); this game's 7 controls are all bits 8-15 ⇒
presses land in byte 0x20 (P1) / 0x22 (P2). Same frame for steady 0x15 and
0x33 replies (361 identical 0x15 frames in the same capture).

**Template-file corrections (supersedes earlier annotations).**
`build/mie_sub15.bin` is a **boot JVS-handshake reply, not a digital read**:
its JVS body `e0 00 04 01 01 05 0b` is the F1 set-address ack (`JVS_OUT(5)`
`:2093`) and **its byte 0x20 = 0x0b is the JVS checksum**
(`(0x00+0x04+0x01+0x01+0x05)&0xff`), not a button word. Other boot 0x15
replies in the capture carry the board-ID string
`SEGA ENTERPRISES,LTD.;I/O BD JVS;` (cmd 0x10, `:2097-2102`).
`build/mie_sub33.bin` is the cold/empty variant (subresp 0x32, no completed
scan yet, `:1711-1713`) — its `00` at 0x20 is padding, not an idle button
word. **Neither file is a valid button-read template**; the shim's 0x15/0x33
template is the 34,990× frame above.

*Caveats:* (a) the game's parser load of `recvaddr+0x20` was not pinned to an
instruction (generic 24-slot maple engine `FUN_8c03c1c2`); BTN_OFF is
source-derived from the emitter whose byte stream the game demonstrably
parses correctly. (b) No capture contains a pressed button inside a MIERESP
frame (the attract capture is 100% idle; the Phase-2 press captures predate
MIERESP logging), so the bit placement at 0x20 rests on `input-map.md` +
`:2237`, not an observed pressed frame. **M4 gate: press a control and
confirm byte recvaddr+0x20 flips.**

**EE_OFF = 4.** `0x03` reply = 1-word header + 128-B EEPROM (`:1925-1929`);
`build/mie_sub03.bin` header `87 00 20 20` (0x20 words) then EEPROM at 0x04-0x83;
two identical 18-B system copies at 0x04 and 0x16 (dual CRC copy,
`naomi-vs-dreamcast.md` §5 / `naomi.md:174-181`). Coin byte at reply 0x0d & 0x1f
= `0x1a` = **free-play** (`naomi.md:180`). The template is already valid
free-play + correct serial → shim may **replay `mie_sub03.bin` verbatim**
(and `mie_sub01.bin` for 0x01), or bake fresh via `naomi/eeprom.py`.

| sub | meaning (maple_jvs.cpp) | shim action |
|---|---|---|
| 0x15, 0x33 | Receive (+kick) JVS input | **real GetCondition translate** → has-data template, P1 word big-endian @0x20/0x21, checksum recomputed @0x3a |
| 0x03 | EEPROM read 128 B (`:1920`) | **baked free-play EEPROM** @+4 (or replay `mie_sub03.bin`) |
| 0x01 | status / schedule EEPROM read | replay `mie_sub01.bin` (ready ACK) |
| 0x27 | kick scan (`:1854`) | verbatim ACK `8700 2001 26 00 8e00` |
| 0x17 | transmit no-repeat (`:1820`) | verbatim ACK `8700 2001 18 00 8e00` |
| 0x21 | transmit repeat (`:1840`) | verbatim ACK `8700 2001 18 00 8e00` |
| 0x13 | store repeated request (`:1793`) | verbatim ACK `8700 2001 14 00 0800` |
| 0x31 | DIP switches (`:1933`) | verbatim `8700 2005 32 ffffff 00 ff f9 ff` |
| 0xff | broadcast reset / dev req | verbatim ACK `8700 2000` |

Only 0x15/0x33 carry live input; the rest are fixed/near-fixed ACKs replayed
verbatim (the shim synthesizes input directly, so a real JVS scan `0x27` is a
no-op ACK).

### Shim contract (boxed — Task 11 implements this)

```
Entry: two hooks — 0x8c0315ce (boot) and 0x8c03c2c6 (steady) — OR one hook on
       the shared SB_MDST store (0xa05f6c18). Both hand off a finished 8-word
       maple descriptor; read:  recvaddr = word1 ;  sub = word3 & 0xff.
Dispatch on sub:
  0x15,0x33 -> real DC GetCondition (per frame); translate DC pad -> JVS bits
               (Start 0x8000, Up 0x2000, Down 0x1000, Left 0x0800, Right 0x0400,
                B1 0x0200, B2 0x0100). Copy the baked 0x3b-byte has-data
               template (the 34,990x frame in the BTN_OFF table; maple header
               87 00 20 0f) to recvaddr, then:
                 [0x20..0x21] = P1 JVS word big-endian ([0x22..0x23] = P2);
                 [0x3a] = (sum of bytes [0x1b..0x39]) & 0xff   (JVS checksum;
                          for this template all variable bytes are 0, so
                          = (0x22 + [0x1f] + [0x20]+[0x21]+[0x22]+[0x23]) & 0xff).
               Do NOT zero-fill inside the frame — bytes 0x04..0x3a are
               structural (maple sub-header, E0 sync @0x1a, len @0x1c,
               status/report bytes, coin/analog blocks); only 0x3b.. is padding.
  0x03      -> baked 128-B free-play EEPROM @recvaddr+4 (or replay mie_sub03.bin).
  0x01      -> replay mie_sub01.bin (status/ready ACK).
  0x13,0x17,0x21,0x27,0x31,0xff -> replay mie_subXX.bin verbatim.
Completion state on return (DIFFERS per site):
  - both:   reply bytes present at recvaddr (= descriptor word1).
  - steady: clear [desc+0x18] bit0 ,  desc = [ *0x8c0e8410 + 0x10f4 ].
  - boot:   leave maple DMA-done observable (SB_MDST reads 0); reply at
            [ *0x8c0e6400 + 0x4 ].  NOTE: the boot-side completion poll is
            inferred from the shared maple driver's SB_MDST semantics
            (naomi.md:138), NOT pinned to a caller instruction — verify in M4.
ONE shim body serves BOTH sites (same descriptor + reply format); per-site
differences = the entry hook AND the completion action above. 0x33 is
per-frame, so do a real GetCondition on every 0x33 — no caching.
M4 gate: BTN_OFF is source-derived (maple_jvs.cpp:2237) — confirm live by
pressing a control and watching recvaddr+0x20 flip.
```

### Reproduction

```sh
scripts/ghidra/run.sh script DisasmRange.java 0x8c027584 0x8c027612   # dispatcher
scripts/ghidra/run.sh script DisasmRange.java 0x8c0315ce 0x8c03161c   # boot builder
scripts/ghidra/run.sh script DisasmRange.java 0x8c03c2c6 0x8c03c4a1   # steady builder
scripts/ghidra/run.sh script DisasmRange.java 0x8c02ecf0 0x8c02ed2c   # steady call site
xxd build/mie_sub15.bin ; xxd build/mie_sub33.bin ; xxd build/mie_sub03.bin
# the authoritative has-data frame (34,990 identical occurrences):
grep '^MIERESP sub=33' capture-attract.log | sed 's/.*data=//' | sort | uniq -c
```
