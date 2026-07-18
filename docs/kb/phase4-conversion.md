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
