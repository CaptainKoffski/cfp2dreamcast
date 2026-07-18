# Phase 3 — Reverse Engineering: design spec

**Date:** 2026-07-18
**Status:** approved (design), pending implementation plan
**Predecessor:** Phase 2 instrumented analysis
(`docs/superpowers/specs/2026-07-18-phase2-instrumented-analysis-design.md`)
**Project:** static binary conversion of *Cleopatra Fortune Plus* (Naomi →
Dreamcast). See `CLAUDE.md`, `docs/kb/00-status.md`.

## Purpose

Phase 2 recorded *what* the game does at runtime (which cart reads, which
input bits, which RAM peaks). Phase 3 finds *which code* does it: the
addresses in the 1 MB boot binary that Phase 4 will patch. Every address is
proven two ways — statically (Ghidra cross-reference) and, where the code
path runs during capture, dynamically (the guest PC logged at the moment the
hardware access happens).

Phase 3 answers five questions:

1. **§8-3 (naomi-vs-dreamcast.md): does the binary ever call into Naomi BIOS
   ROM after the entrypoint?** Expected no; must be proven, because a yes
   means the loader has to reimplement those routines.
2. **Where is the cart-read function?** The code that fills
   `DMA_OFFSETH/L` + `DMA_COUNT` and kicks `SB_GDST` (and the one PIO seek
   path). Phase 4 rewrites its call sites / body to issue GD-ROM reads
   against the Phase 2 streaming map.
3. **Where is the input-decode function?** The code that polls the MIE over
   Maple (`0x86`/`0x15`) and decodes the JVS word Phase 2 mapped
   (`input-map.md`). Phase 4 shims it to read a DC controller.
4. **Is there a high-address stack?** Phase 2's main-RAM scan found non-zero
   data ~1 KB below the top of Naomi's 32 MB. Follow the entry code to the
   `r15` (SP) setup. If SP really sits near 32 MB, Phase 4 must relocate it
   below DC's 16 MB; if it's scan noise, main RAM is declared safe.
5. **Where is the EEPROM/settings-parse function?** The code that requests
   the 93C46 contents over Maple (`0x86`/`0x01`/`0x03`) and parses the
   settings blob. Phase 4 forces free-play defaults there
   (naomi-vs-dreamcast.md §5).

## Approach

**Static Ghidra analysis as the spine, dynamic PC logging as the proof
(chosen).** SH-4 code materializes MMIO addresses through PC-relative
literal pools (`mov.l @(disp,PC)`), so the distinctive register addresses
(`0x5f7000`-`0x5f7014`, `0x5f7400`+, `0x5f6c00`+) are findable as data
constants; Ghidra auto-analysis plus small headless scripts turn those into
candidate functions. A small extension to the Phase 2 instrumented Flycast
then logs the guest PC at each cart-DMA kick and Maple transaction — ground
truth that the statically identified function is the one that actually runs.
Static-only leaves ambiguity (multiple xref sites, dead code) to explode in
Phase 4; dynamic-only can't prove a negative (§8-3) or read the SP setup.
Together each covers the other's blind spot, and all the Phase 2 capture
infrastructure (patch, build recipe, capture wrapper, parser self-check
pattern) is reused as-is.

Alternatives considered and rejected:

- **Pure static Ghidra.** No rebuild, faster start — but candidate addresses
  stay unproven, and a wrong patch address is far more expensive to debug in
  Phase 4 than a rebuild is now. Rejected.
- **Dynamic-first (Ghidra only around logged PCs).** Least Ghidra work, but
  weak for §8-3 (absence of evidence in a finite capture, not proof) and
  useless for the SP-setup read. Rejected.

## The five targets

### 1. BIOS-call verdict (resolves §8-3)

Static: enumerate every call/jump whose resolved target lies in BIOS ROM —
physical `0x00000000-0x001fffff` under any SH-4 mirror (P1 `0x8000_0000`,
P2 `0xa000_0000`, etc.; compare on the 29-bit physical address). SH-4
specifics make this tractable: `bsr`/`bra` are PC-relative ±4 KB and cannot
reach BIOS from `0x8c02...`, so only register-indirect `jsr @rN`/`jmp @rN`
matter, and their targets come from literal pools — scan all pool constants
that resolve into BIOS range and check whether any flows into a branch
register. Dynamic backstop: the instrumented Flycast logs any guest
execution inside BIOS range *after* the first hit of the entrypoint
`0x8c04ae2c` (the BIOS legitimately runs before entry). Expected result:
zero, both ways.

Caveat recorded honestly: a computed (non-pool) branch target could evade
the static scan; the dynamic backstop covers executed paths only. If both
come back clean, §8-3 is resolved as "no BIOS dependency observed statically
or dynamically" — sufficient to proceed, revisited only if Phase 4 hits an
unexplained crash.

### 2. Cart-read function (primary Phase 4 patch target)

Static: find literal-pool references to the cart register block
(`0x5f7000/04/08/0c/10/14`) and the G1 DMA channel registers at `0x5f7400`+
(`SB_GDSTAR/GDLEN/GDDIR/GDEN/GDST` — exact offsets to be taken from the
`naomig1.cpp` cites in `naomi-vs-dreamcast.md §3` during implementation,
not guessed), collect the referencing
functions, and read the winner: expect one low-level "issue cart DMA
(offset, count, dest)" routine plus a thin PIO path. Identify its callers
far enough to name a clean patch boundary (the function Phase 4 replaces
wholesale, vs. patching every call site).

Dynamic: `CARTDMAPC` log line — guest PC + SP captured at the moment of the
DMA kick, for every streaming request in a capture pass. Every logged PC
must fall inside the statically identified function.

### 3. Input-decode function

Static: find the Maple transaction path (references to the Maple block
`0x5f6c00`+ — `SB_MDSTAR`, `SB_MDST`) and the code that builds the MIE
`0x86`/`0x15` request frame / parses the response into the JVS word whose
bits Phase 2 confirmed (`input-map.md`: Start `0x8000` … B2 `0x0100`).
The bit masks themselves are search fodder: code testing `0x2000`/`0x1000`/
`0x0800`/`0x0400` near the Maple response buffer is the decoder.

Dynamic: `MAPLEPC` log line — guest PC at each Maple DMA start, tagged with
the MIE command/subcommand seen in the request frame (`0x15` = input poll,
`0x01`/`0x03` = EEPROM read, `0x0b` = EEPROM write), so one log stream
serves targets 3 and 5.

### 4. Stack-pointer verdict (closes the Phase 2 main-RAM question)

Static: follow the entry chain — `0x8c04ae2c` is a 5-instruction trampoline
(verified in Phase 1, `tooling.md`) that loads the real start from a literal
pool and jumps; walk into the real init and find the instruction sequence
that sets `r15`. Read the value. Verdict: SP < 16 MB above RAM base → main
RAM safe as-is; SP near 32 MB → Phase 4 must relocate SP (a one-constant
patch, noted for the Phase 4 plan).

Dynamic: SP is logged alongside PC in every `CARTDMAPC` line — running
confirmation of where the stack actually lives during play.

### 5. EEPROM/settings-parse function

Static: from the Maple path shared with target 3, isolate the `0x86`/`0x01`/
`0x03` (read) and `0x0b` (write) request builders and the parser that
CRC-checks the two settings copies (layout per `naomi-vs-dreamcast.md §5`).
Locate the function boundary Phase 4 patches to force defaults
(DragonMinded's documented technique: replace the byte loads from the
parsed settings struct with immediate loads).

Dynamic: the tagged `MAPLEPC` lines with subcommand `0x01`/`0x03` — the
EEPROM read happens during boot, so the attract pass captures it.

## Instrumentation extension (dynamic side)

Extends `patches/flycast-instrument.diff` (same helper, same
`FLYCAST_CARTLOG` sink, same capture wrapper `scripts/capture.sh`):

```
CARTDMAPC pc=%08x sp=%08x            # at cart-DMA kick (paired with CARTDMA)
MAPLEPC   cmd=%02x sub=%02x pc=%08x  # at Maple DMA start, MIE frame tagged
BIOSEXEC  pc=%08x                    # guest PC entered BIOS ROM range, post-entry
```

**Interpreter mode for the PC-logging pass.** Under the dynarec, the guest
PC available inside a memory-handler callback is block-granular, not
instruction-exact. The capture pass for Phase 3 runs with Flycast's
interpreter core enabled — slower, but exact PC/SP, and the pass is short
(attract through boot + a brief play segment; the EEPROM read and input
polls all happen within the first minutes). If interpreter-mode PC capture
proves unworkable, fallback is dynarec block-start PC + static confirmation
that the block belongs to the candidate function — recorded as such, not
silently substituted.

One rebuild via the existing `tooling.md` recipe; one capture pass; parsing
via `scripts/parse_cart_log.py` extended with the new line types.

## Static-analysis harness

- The 1 MB boot slice (first `0x100000` bytes of the `.dat`) is extracted to
  a gitignored working file — it is ROM content: **never committed, never
  uploaded**, same rule as the `.dat`.
- Import: Ghidra 12.1.2 headless, `SuperH4:LE:32:default`, BinaryLoader,
  base `0x8c020000` — the invocation `scripts/ghidra/DisasmEntry.java`
  already proved in Phase 1, now with full auto-analysis enabled.
- Analysis scripts are headless **Java** (Ghidra 12 dropped Jython —
  `tooling.md`), committed under `scripts/ghidra/`, re-runnable from scratch:
  one to report MMIO literal-pool xrefs by target register block, one for
  the BIOS-range target scan, one to walk/dump the entry chain and SP setup.
  Output is text to stdout/files that feed the KB writeup — the Ghidra
  project itself stays gitignored (it embeds ROM bytes).

## Cross-check (the self-check layer)

Extends the Phase 2 parser-self-check pattern; encoded as asserts in
`scripts/parse_cart_log.py`:

- `dma_pc_in_cart_fn`: every `CARTDMAPC` PC lies inside the statically
  identified cart-read function's range.
- `input_pc_in_input_fn`: every `MAPLEPC` with sub `0x15` lies inside the
  identified input-poll path.
- `eeprom_seen`: at least one `MAPLEPC` with sub `0x01`/`0x03` (the boot
  EEPROM read) was captured, and it lies in the identified EEPROM path.
- `no_bios_exec`: zero `BIOSEXEC` lines.
- `sp_consistent`: every logged SP lies within the stack region implied by
  the static SP-setup value.

A static/dynamic disagreement is a stop-and-debug event (the
systematic-debugging skill), never papered over — one side is wrong and
Phase 4 depends on knowing which.

## Deliverables

- **`docs/kb/boot-binary.md`** — the annotated map: entry chain, SP setup
  and stack verdict, the five target answers, each function with address
  range, role, evidence (static xref + dynamic PC), and its Phase 4 patch
  implication. Every claim cited by address, per the project's citation rule.
- `naomi-vs-dreamcast.md` — §8-3 marked resolved with the verdict.
- `phase2-measurements.md` — the main-RAM stack question closed out.
- `docs/kb/00-status.md` — Phase 3 done, Phase 4 next; key facts updated.
- `scripts/ghidra/*.java` — the committed, re-runnable analysis scripts.
- `patches/flycast-instrument.diff` — extended with the PC logging.
- `scripts/parse_cart_log.py` — extended with the new line types + checks.
- `docs/kb/tooling.md` — updated only if a new tool/step appears.

## Scope boundaries

- **In:** locating and proving the five targets; the instrumentation and
  scripts to do it; the KB writeup.
- **Out — Phase 4:** any patching, shim writing, or loader work. Phase 3
  names the addresses; Phase 4 changes them.
- **Out — Phase 5:** sound/VRAM upload-path RE and asset-cut planning; the
  ARAM/VRAM measurement gaps stay Phase 5 items.
- **Out:** naming/annotating functions beyond the five targets and their
  immediate callers; full-binary comprehension is not the goal.
- **Out:** the un-streamed top ~12 MB of cart (a Phase 4 top-up concern).

## Exit criteria

Phase 3 is done when:

1. `boot-binary.md` records the five answers with static + dynamic evidence
   for each runtime-reachable target.
2. All five parser cross-checks pass on a captured run.
3. §8-3 and the Phase 2 stack question are marked resolved in their docs.
4. The Ghidra scripts re-run headlessly from a fresh checkout (given the
   gitignored ROM) and reproduce the reported addresses.
5. `00-status.md` is advanced to Phase 4.
