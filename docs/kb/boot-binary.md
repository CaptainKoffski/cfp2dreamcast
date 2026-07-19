# Boot-binary analysis — Phase 3

Reverse engineering of the Cleopatra Fortune Plus 1 MB boot binary
(`0x8c020000`–`0x8c11ffff`), establishing the five patch targets for Phase 4
and closing the open questions from Phase 2.

---

## 1. Method

**Static (Ghidra 12.1.2):** `scripts/ghidra/run.sh import` — imports
`tools/boot.bin` into project `cleo3` with `BinaryLoader`, base `0x8c020000`,
processor `SuperH4:LE:32:default`, full auto-analysis. Post-import scripts run
via `scripts/ghidra/run.sh script NAME.java` (`-noanalysis`):

| Script | Role |
|---|---|
| `FindMmioXrefs.java` | Walks every MMIO range; emits `XREF src=… reg=…` for each pool-literal MMIO reference |
| `ScanBiosTargets.java` | Checks every flow reference and 32-bit pool word for BIOS-range targets (phys `0x0`–`0x1fffff`) |
| `DumpEntryChain.java` | Follows the trampoline at the header entrypoint; resolves pool-jump targets |
| `WhichFunc.java` | Maps a list of addresses to `(fn entry, body range)` |

**Dynamic (Flycast interpreter + BIOSEXEC guard):** `patches/flycast-instrument.diff`
extends the guest-PC / SP log with:
- `CARTDMAPC pc= sp=` — PC at SB_GDST store (cart DMA trigger)
- `MAPLEPC cmd=86 sub= pc=` — PC at Maple DMA store
- `BIOSEXEC pc=` — any guest instruction executed while PC is in phys
  `0x0`–`0x1fffff` (BIOS ROM); guarded to the dynarec-off interpreter path
  (`Dynarec.Enabled=no` in Flycast config) so every instruction is visible

The `+2 store offset` caveat: the hook fires on the *instruction after* the
triggering store, because the SH-4 has issued the store by the time the hook
runs. For the cart DMA site the logged PC `0x8c03bd28` is the `bsr` immediately
after the `mov.l r4,@r1` at `0x8c03bd26`. For the input site the logged PC
`0x8c03161e` is the return address one instruction past the `rts` delay slot at
`0x8c03161c`.

**Captures:** two full passes (boot/attract smoke + user-played pass to
game-over) stored in `capture-pc.log`. Parsed by
`scripts/parse_cart_log.py` with five cross-checks:

| Cross-check | Result |
|---|---|
| `no_bios_exec` | PASS — zero BIOSEXEC lines |
| `dma_pc_in_cart_fn` | PASS — all 460 cart-DMA PCs inside `0x8c03bd08–0x8c03bd4d` |
| `input_pc_in_input_fn` | PASS — all input-poll PCs inside declared ranges |
| `eeprom_seen` | PASS — MIE sub `0x01`/`0x03` observed and inside declared ranges |
| `sp_consistent` | PASS — all logged SPs cluster in `0x8c00e6e8`–`0x8c00ef28` (< 1 MB span) |

---

## 2. Entry chain

The Naomi header entrypoint is **`0x8c04ae2c`** (`docs/kb/game.md`).

`DumpEntryChain.java` resolves:

```
0x8c04ae2c  trampoline (5 instructions): mov.l @(4,PC),r1; mov #0,r4;
            mov.l @(8,PC),r14; jmp @r1; mov.l @(4,PC),r5
            pool literal → r1 = 0x8c021000
0x8c021000  real init function (init sets CCR, zeroes RAM, configures stack)
```

The trampoline loads the real start address from a literal pool and branches
there. `DumpEntryChain.java` pool-jump resolver confirmed `0x8c021000` as init.

---

## 3. Stack-pointer verdict — CLOSES the Phase 2 main-RAM question

### Static (DumpEntryChain.java / Ghidra disassembly)

Init (`0x8c021000`) writes r15 twice:
- `mov r0,r15` at `0x8c02108c` with `r0=0x8c00f400` (loaded from pool at
  `0x8c0210a4`) — first SP set during CCR/cache init.
- A second `mov.l @(…),r15` at `0x8c021104` loads `0x8c00f000` — definitive
  working SP.

Both targets are ~58–62 KB above the `0x8c000000` RAM base.

The earlier `0x8c00f400` write as seen in P2 (uncached) alias `0xac00f400` is
the CCR-setup phase writing to the uncached mirror; not the definitive SP.

### Dynamic (capture-pc.log, `sp_consistent` PASS)

Stack pointer range across all logged cart-DMA events (460 `sp=` samples
spanning boot + play — SP is sampled at each `CARTDMAPC`, not every
instruction): **`0x8c00e6e8`–`0x8c00ef28`** — entirely within the first
62 KB of main RAM. (`sp_consistent` proves the stack is *stable* to within
1 MB; the *low* location is these observed values plus the static r15 setup
in §3, not the check alone.)

### Verdict

**The Phase 2 WATERMARK scan hit of `0x01fff60b` (≈ 1 KB below the top of
Naomi's 32 MB) was stale/uninitialized data, NOT a real high-address stack.**
The SP never comes near 32 MB. Static and dynamic agree: the stack lives at
`0x8c00e–f xxx`, well within DC's 16 MB (`0x0c000000`–`0x0cffffff`).

**Phase 4 implication: no SP relocation needed. Main RAM is safe as-is on DC
16 MB.** This closes the `phase2-measurements.md` "Phase 3 question."

---

## 4. Cart-read function — PRIMARY Phase 4 patch target

### Headline finding: static and dynamic disagree; dynamic wins

**Runtime trigger:** `FUN_8c03bd08` (`0x8c03bd08`–`0x8c03bd4d`)
— confirmed by dynamic PC logging (sole cart-DMA PC: `0x8c03bd28`, 460 events,
`dma_pc_in_cart_fn` PASS; `scripts/parse_cart_log.py`).

**Static candidate:** `FUN_8c08063c` (`0x8c08063c`–`0x8c080749`)
— found by `FindMmioXrefs.java`'s pool-literal G1/GD-ROM register scan.

**Why they differ:** `FUN_8c03bd08` writes SB_GDST via a *computed address*
(`[r14+0x58] + 0x418 = phys 0x005f7418`), not a pool literal. `FindMmioXrefs`
scans pool literals only and therefore never sees it. The two functions are in
entirely separate call chains (`WhichFunc.java`):

- `FUN_8c03bd08` is called from `FUN_8c03b81a` (at `0x8c03b8ec`); its callees
  are `FUN_8c03bc12` and `FUN_8c03bbe8` (both in the `0x8c03bxxx`
  cart-stream cluster).
- `FUN_8c08063c` is called only from `FUN_8c081aee` (at `0x8c081b26`) — a
  config-time DMA-parameter/region builder that writes G1 registers via
  absolute pool literals (`0x005f7480`, `0x005f7484`, `0x005f7490`,
  `0x005f74a4`, `0x005f74b8` = GDSTAR/GDLEN/GDDIR/GDEN base config) inside a
  region-code switch. No call, jump, or fall-through connects the two clusters.

### Disassembly of FUN_8c03bd08 (key instructions)

```
0x8c03bd08  mov.l r14,@-r15
0x8c03bd0a  mov r4,r14              ; r14 = DMA descriptor object
0x8c03bd0c  mov #0x1,r4
0x8c03bd16  mov.l r4,@(r0,r14)     ; [r14+0x7c] = 1  (status flag)
0x8c03bd1a  mov.l @(r0,r14),r2     ; r2 = [r14+0x58]  (G1 reg base ~0x005f7000)
0x8c03bd1c  add r3,r2              ; r2 = base + 0x414
0x8c03bd1e  mov.l r4,@r2           ; *(base+0x414) = 1  (SB_GDEN)
0x8c03bd20  mov.l @(r0,r14),r1     ; r1 = [r14+0x58]
0x8c03bd22  mov.w 0x8c03bdca,r2    ; r2 = 0x418  (pool)
0x8c03bd24  add r2,r1              ; r1 = base + 0x418
0x8c03bd26  mov.l r4,@r1           ; *(base+0x418) = 1  ← SB_GDST STORE (DMA trigger)
0x8c03bd28  bsr 0x8c03bc12         ; ← LOGGED PC (hook fires here, after the store)
```

Pool constants from `WhichFunc.java`/disassembly: `0x8c03bdc8 = 0x414`,
`0x8c03bdca = 0x418`. Base `[r14+0x58] + 0x418` = phys `0x005f7418` = SB_GDST
(`naomig1.cpp:13`).

### Phase 4 implication

The Phase 4 cart-read intercept belongs on **`FUN_8c03bd08`** (the runtime
trigger, specifically the `mov.l r4,@r1` store at `0x8c03bd26`). Patching
`FUN_8c08063c` would intercept only config-time DMA-parameter setup, missing
every per-frame streaming read. Lesson: pure static analysis would have
mis-targeted Phase 4; dynamic PC logging was necessary.

`FUN_8c08063c` may still be relevant to Phase 4 for translating the initial
GDSTAR/GDLEN programming, but the streaming-read intercept hook belongs on
`FUN_8c03bd08`.

---

## 5. Input-decode function — Phase 4 shim target

**Per-frame input poll** is handled by the Maple store routine at **`0x8c0315ce`**
(`0x8c0315ce`–`0x8c03161d`).

The routine is a Ghidra gap (no function created) because it is reached only
via function-pointer callback: `FUN_8c027584` loads its address from a
pointer table at `0x8c0275da`/`0x8c0275e0` and dispatches via `jsr @r3`
(`WhichFunc.java`: DATA/PARAM refs from `0x8c0275da`/`0x8c0275e0`). It builds
the Maple command frame (store-queue writes), issues the Maple DMA-start store
at `0x8c031618` (`mov.l r1,@r0`), then `rts` at `0x8c03161a` + delay slot at
`0x8c03161c`. The logged PC `0x8c03161e` is the return address (one byte past
the delay slot); the check range is extended to `0x8c03161f` to cover it.

A minor second site `FUN_8c03c2c6` (`0x8c03c2c6`–`0x8c03c4a1`) handles 7×
events vs 369× for the primary. Both use MIE command `0x86` subcommand `0x15`
(query controls; `tools/netboot/docs/naomi.md:190-196`).

The JVS bit map is established in `docs/kb/input-map.md`:
Start `0x8000`, Up `0x2000`, Down `0x1000`, Left `0x0800`, Right `0x0400`,
B1 `0x0200`, B2 `0x0100` (active-high).

`input_pc_in_input_fn` PASS (`scripts/parse_cart_log.py`).

**Phase 4 implication:** shim `0x8c0315ce` (primary) to issue a DC Maple
`GetCondition` request and translate the DC controller bitmap to the JVS layout
the game expects. The minor site `FUN_8c03c2c6` may need the same shim.

> **Addendum 2026-07-18 (Phase 4 Task 4) — primary/secondary inversion.**
> The 369×/7× counts above count only **sub `0x15`** traffic (the parser's
> input check filters on sub 0x15) and that framing is misleading for the
> steady state. Both captures show the per-frame input poll is actually
> **sub `0x33` issued from `FUN_8c03c2c6`**:
> Phase 3 interpreter-exact `capture-pc.log` has **23,762** `MAPLEPC sub=33
> pc=8c03c3e4` lines (inside `FUN_8c03c2c6` `0x8c03c2c6`–`0x8c03c4a1`) vs
> 369 `sub=15` at the `0x8c0315ce` site; the Phase 4 600 s attract capture
> (`capture-attract.log`, dynarec ON, block-granular PCs) has **34,991**
> `sub=33 pc=8c03c3d6` — same function — vs 376 `sub=15` total, all in the
> boot phase. So: **`0x8c0315ce` = boot-phase site (subs 0x15/0x27 + one-off
> 0x01/0x03 EEPROM), `FUN_8c03c2c6` = steady-state per-frame input poll
> (sub 0x33)**. The old counts stand as what they measured (sub-0x15 only);
> the "primary/minor" labels do not. **Task 5 must disassemble BOTH sites**,
> and the input shim must serve sub 0x33 (see `phase4-conversion.md` §V4
> for reply templates and the per-sub site table).

---

## 6. EEPROM/settings-parse function — Phase 4 shim target

**EEPROM access shares the two Maple sites** from §5 — the same logged PCs
(`0x8c03161e` primary, `0x8c03c3e4` secondary; these are the *post-store*
return PCs — the routine entries are `0x8c0315ce` and `0x8c03c2c6`) carry both
sub `0x15` (input) and sub `0x01`/`0x03` (EEPROM read) traffic. There is no
PC-level distinction; Phase 4 must differentiate by subcommand.

MIE subcommand observation (both captures, `eeprom_seen` PASS):
- `0x01`/`0x03` (EEPROM read): 2× at each site at boot
- `0x0b` (EEPROM write): not seen in captures (writes may only occur on first
  boot or settings change)

The static config-time MIE command builders (`FUN_8c0809b2`
`0x8c0809b2`–`0x8c080cff` and `FUN_8c080d18` `0x8c080d18`–`0x8c080ec3`,
chain `FUN_8c04ae50` → `FUN_8c080d18` → `FUN_8c0809b2`;
`WhichFunc.java` + `FindMmioXrefs.java`) are a separate call chain — the same
static/dynamic split as the cart DMA case.

**Phase 4 implication:** at the runtime Maple path (`0x8c0315ce`) intercept
sub `0x01`/`0x03` and return forced free-play defaults, skipping the real
EEPROM entirely.

---

## 7. BIOS-call verdict — RESOLVES naomi-vs-dreamcast.md §8-3

### Static: ScanBiosTargets.java

`BIOSREF = 0` — Ghidra followed every resolvable call/jump in the program;
none have a flow destination in BIOS ROM (`phys 0x0`–`0x1fffff`).

`POOLBIOS = 6` — six defined 32-bit pool words hold BIOS-VA-shaped values:

| Address | Value | Interpretation |
|---|---|---|
| `0x8c02e9f0` | `0x80000200` | SH-4 VBR + 0x200 (general exception vector; game's own exception setup) |
| `0x8c04afbc` | `0x80000038` | SH-4 VBR + 0x038 (TLB-miss exception vector; VBR setup) |
| `0x8c04b37c` | `0x80000038` | Same TLB-miss vector constant |
| `0x8c0804d4` | `0xa0060000` | P2-uncached pointer to phys `0x60000` (inside BIOS ROM) — possible BIOS data read (e.g. font/ID); NOT a call |
| `0x8c080e94` | `0x80000300` | SH-4 VBR + 0x300 (interrupt vector; VBR setup) |
| `0x8c0814d0` | `0xa01ffd00` | P2-uncached pointer to phys `0x1ffd00` (inside BIOS ROM) — possible BIOS data read; NOT a call |

The three `0x800003xx` constants are exception-vector offsets for the game's
own VBR programming — not calls into BIOS code. The two `0xa0060000`/`0xa01ffd00`
pool words are P2-uncached pointers into BIOS ROM that *may* be data reads (font
table, device ID); these are NOT flow targets. Flagged as a low-risk Phase 4
watch item: if the game dereferences them at runtime, the loader may need to
supply that BIOS ROM data.

### Dynamic: BIOSEXEC

`BIOSEXEC = 0` across both captures (`no_bios_exec` PASS,
`scripts/parse_cart_log.py`). No guest instruction was executed in the BIOS
ROM range after the entrypoint.

### Verdict

**No BIOS-call dependency, statically or dynamically.**

Honest caveat: the static scan can miss a computed (non-pool) branch target
whose address is built at runtime from arithmetic (as seen for the cart DMA
case). The dynamic scan covers only executed paths. Both methods agree: no
BIOS calls observed. BIOSREF=0 + BIOSEXEC=0 are the definitive evidence.

**Phase 4 implication: no BIOS-call shim needed.** The `0xa0060000` /
`0xa01ffd00` pool constants are a low-priority watch item; if Phase 4 testing
shows a fault at those addresses, the loader must provide a stub mapping to
that BIOS ROM data.

---

## 8. Reproduction

### Ghidra import (once)

```sh
scripts/ghidra/run.sh import
```

(Requires `tools/boot.bin` — extract 1 MB at offset 0 from the cart image.)

### Static scripts

```sh
scripts/ghidra/run.sh script FindMmioXrefs.java 2>&1 | grep XREF | wc -l   # expect > 0
scripts/ghidra/run.sh script ScanBiosTargets.java 2>&1 | grep -E "RESULT|BIOSREF|POOLBIOS"
scripts/ghidra/run.sh script DumpEntryChain.java 2>&1 | grep -E "pool|init"
scripts/ghidra/run.sh script WhichFunc.java 2>&1 | grep -E "FUN_|body"
```

### Parser self-tests

```sh
python3 scripts/test_parse_cart_log.py
# Expected: 12 passed, 0 failed
```

### Dynamic cross-checks (requires capture-pc.log)

Capture with: `Dynarec.Enabled=no` in Flycast config, then
`scripts/capture.sh pc [seconds]`.

```sh
python3 scripts/parse_cart_log.py capture-pc.log \
  --cart-fn 8c03bd08-8c03bd4d \
  --input-fn 8c0315ce-8c03161f,8c03c2c6-8c03c4a1 \
  --eeprom-fn 8c0315ce-8c03161f,8c03c2c6-8c03c4a1
```

Expected output — the 5 Phase 3 checks below, plus the 3 Phase 2 checks
(`dest_in_ram`, `len_aligned_32`, `beyond_boot_read`), all `PASS`:
```
CHECK no_bios_exec: PASS
CHECK dma_pc_in_cart_fn: PASS
CHECK input_pc_in_input_fn: PASS
CHECK eeprom_seen: PASS
CHECK sp_consistent: PASS
```

Verified 2026-07-18 on `capture-pc.log` (460 cart DMAs, 264 unique triples;
cart span `0x800000`–`0x609c000`; SP range `0x8c00e6e8`–`0x8c00ef28`;
BIOSEXEC=0; BIOSREF=0, POOLBIOS=6).
