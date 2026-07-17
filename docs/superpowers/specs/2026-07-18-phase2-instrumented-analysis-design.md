# Phase 2 — Instrumented Analysis: design spec

**Date:** 2026-07-18
**Status:** approved (design), pending implementation plan
**Predecessor:** Phase 1 foundation
(`docs/superpowers/specs/2026-07-17-phase1-foundation-design.md`)
**Project:** static binary conversion of *Cleopatra Fortune Plus* (Naomi →
Dreamcast). See `CLAUDE.md`, `docs/kb/00-status.md`.

## Purpose

Phase 1 established *what* the machines are and *where* they differ. Phase 2
observes *what this specific game actually does at runtime*, by instrumenting
the emulator we already trust to boot it (Flycast) and logging four things
while the game runs. It turns the Phase 1 open questions from "must be
measured" into recorded data that Phase 4 can patch against.

Concretely, Phase 2 resolves these items from
`docs/kb/naomi-vs-dreamcast.md §8`:

- **§8-1** the exact cart-streaming request pattern (offset, length, dest) —
  the requests that become GD-ROM reads on Dreamcast.
- **§8-2** the JVS/MIE input bit → button mapping for this game.
- **§8-4** whether the game touches the watchdog / serial / network hardware.

…and it takes the RAM-size deltas of `§1` (main 32→16 MB, video 16→8 MB,
sound 8→2 MB) from *assumption* to *measurement*: does this game's working set
fit Dreamcast memory, or are asset cuts genuinely required in Phase 5?

Phase 2 captures **behavior, not code**. Disassembling the cart-read and
input-decode functions is Phase 3.

## Approach

**Instrument Flycast (chosen).** Flycast is open source and is the emulator
Phase 1 confirmed boots and plays this game. We build it once from source and
add the smallest possible logging patch — reusing Flycast's existing log
output wherever it already prints what we need, and writing custom logging
only for the gaps. All four capture targets live in that one codebase.

Alternatives considered and rejected:

- **MAME debugger + Lua watchpoints.** Scriptable without a recompile, but
  MAME expects original *encrypted* romsets; our dump is decrypted, so merely
  booting the game in MAME is more work than the rest of Phase 2. Also
  contradicts the standing "MAME romsets/builds out of scope" decision
  (`docs/kb/tooling.md`). Rejected.
- **A from-scratch trace tool.** Reinvents what a patched Flycast provides for
  free. Rejected (YAGNI).

The guest hardware addresses to watch are identical on Naomi and Dreamcast
because they emulate the same SH-4 machine; they are already cited in
`docs/kb/naomi-vs-dreamcast.md §3/§4/§7`. *Where in Flycast's C++ those writes
are handled* is an execution detail the plan's first task locates in the
source; this spec fixes the guest addresses, not the Flycast line numbers.

## The four capture targets

### 1. Cart streaming map (resolves §8-1) — primary deliverable

Log every write to the cart ROM-board register window and the shared G1 DMA
channel, plus PIO reads through the data port
(addresses from `naomi-vs-dreamcast.md §3`):

- ROM-board registers `0x5f7000` `ROM_OFFSETH`, `0x5f7004` `ROM_OFFSETL`,
  `0x5f7008` `ROM_DATA` (PIO read port, auto-advances), `0x5f700c/10`
  `DMA_OFFSETH/L`, `0x5f7014` `DMA_COUNT` (**units of 0x20 bytes**).
- G1 GD-ROM DMA channel `0x5f7400`: `SB_GDSTAR` (dest), `SB_GDLEN`,
  `SB_GDDIR`, `SB_GDEN`, `SB_GDST` (write 1 = start).
- Offset high bits: bit 31 = auto-advance, bit 30 = decrypt select. **Our dump
  is already decrypted; the decrypt bit is expected off and is recorded, not
  acted on.**

From these we reconstruct each request as a
`(cart offset, length, dest RAM address, mode = PIO|DMA)` tuple, in issue
order. These tuples are the reads Phase 4 reissues against the GD-ROM.

Output: `docs/kb/cart-streaming-map.md` (human summary) plus
`docs/kb/cart-streaming-map.csv` (machine-readable, columns
`cart_offset,length,dest,mode` — append-friendly so top-up captures merge and
dedup).

### 2. RAM watermark (drives the Phase 5 asset-cut decision)

Determine the peak *used* extent of each RAM region and compare against
Dreamcast sizes: main RAM `0x0c000000` (fits 16 MB, or assumes Naomi's 32?),
video RAM `0x04000000` (8 vs 16 MB), sound RAM `0x00800000` (2 vs 8 MB).

Method, laziest first:

1. **Derived from target #1 for free.** Every bulk asset load is a DMA whose
   `dest + length` we already log; the max over main-RAM-destined DMAs bounds
   the asset high-water in main RAM. Texture and audio uploads travel through
   the PVR/TA and AICA DMA paths — log those DMA destinations the same way to
   bound video and sound RAM.
2. **Fallback, only if the derived main-RAM figure lands marginal against
   16 MB:** run one measurement pass with Flycast's CPU interpreter enabled
   and a highest-written-address-per-region compare in the guest-store path.
   The interpreter is slow; this pass is a fallback, not the default
   (the dynarec fast path bypasses per-write handlers, so a store hook needs
   the interpreter).

Output: a verdict recorded in the KB — for each region, measured peak vs DC
capacity, and whether cuts are required. Feeds Phase 5.

### 3. Input bit map (resolves §8-2)

Log the MIE input-response bitmap (the reply to Maple command `0x86` /
subcommand `0x15`; `naomi-vs-dreamcast.md §4`). During a dedicated input run,
you press each control in a stated order — Start, Up, Down, Left, Right,
Button 1, Button 2, then Coin, then Test/Service — holding each ~1 s with gaps
between. Exactly one bit changes per held control, so the mapping reads
straight off the sequence; no log markers needed.

Output: `docs/kb/input-map.md` — the 7 gameplay controls plus coin and test,
each mapped to its MIE response bit. (Nine rows: read by hand, no parser.)

### 4. Watchdog / serial probe watch (resolves §8-4)

Log any writes to the MB3773 watchdog, the serial port (CN8), and the
ARCNET/network registers (`naomi-vs-dreamcast.md §7`). A single-player puzzle
game is unlikely to use them, but if it kicks the watchdog, Phase 4 must
no-op that path or the port hangs.

Output: a one-line verdict per device in the KB — touched or not; shim needed
or ignorable.

## Capture protocol (your role)

The instrumented build writes an append-only log file we parse afterward.
Coverage is **iterative** (the chosen depth): a solid first pass now, topped
up later if Phase 4 hits an un-mapped asset.

1. **Attract pass.** Launch, let the attract-mode loop run once fully (title,
   demo play, how-to-play, high scores). This streams a lot of assets with no
   skilled play.
2. **Play pass.** Play a session through the early stages plus a game-over.
3. **Input pass.** A short run pressing each control once, in the stated
   order, for target #3.

Scene tagging is by run ordering and the natural signatures in the data
(scene changes show as bursts of cart DMA); you narrate roughly what you did.
A log-marker hotkey is added only if ordering proves insufficient. Re-running
to top up just means playing more with logging on and merging the CSV.

## Data flow

```
instrumented Flycast ──> append-only log file(s) on disk
        │
        ├─ scripts/parse_cart_log.py ──> docs/kb/cart-streaming-map.{md,csv}
        │                                + RAM watermark verdict (targets 1,2,4)
        └─ (input run, read by hand)  ──> docs/kb/input-map.md   (target 3)
```

One parser (`scripts/parse_cart_log.py`) serves targets 1, 2, and 4 from the
same log: it emits the cart-streaming CSV/summary, computes per-region max
`dest+length` for the RAM watermark, and flags any watchdog/serial writes.
Target 3 is nine rows, read by hand — no parser (YAGNI).

## Deliverables

- **Instrumented Flycast**, built from source at `tools/flycast-src/`
  (gitignored clone), with the logging patch saved in-repo as
  `patches/flycast-instrument.diff` so the build is reproducible from a fresh
  clone.
- `scripts/parse_cart_log.py` — log → cart map CSV/summary + RAM watermark +
  watchdog/serial flags.
- `docs/kb/cart-streaming-map.md` + `.csv`.
- `docs/kb/input-map.md`.
- RAM watermark verdict recorded in the KB (in the cart-streaming-map summary
  or a short `docs/kb/phase2-measurements.md`) and reflected back into
  `naomi-vs-dreamcast.md §1/§8` where it settles an open question.
- `docs/kb/tooling.md` updated with the Flycast source-build recipe (macOS)
  and the patch-apply step.
- `docs/kb/00-status.md` advanced: Phase 2 done, Phase 3 next.

## Verification

Each instrument leaves one runnable check so it cannot silently lie:

- **Cart map sanity:** every logged DMA destination lands inside a real RAM
  range and every `DMA_COUNT` is a whole number of `0x20`-byte units; and we
  observe at least one cart read whose destination or offset lies **beyond the
  1 MB boot region** (proves we are seeing runtime streaming, not just the
  boot load). Encoded as asserts in `parse_cart_log.py`.
- **RAM watermark sanity:** the per-region peak is ≥ the known boot load
  (main RAM must reach at least `0x8c020000 + 0x100000`), else the logger is
  blind to writes.
- **Input pass sanity:** pressing exactly one control changes exactly the
  expected bit(s) in the MIE response — a control that flips zero bits or many
  bits means the decode is wrong.

## Scope boundaries

- **In:** runtime behavior capture — cart requests, RAM peaks, input bits,
  watchdog/serial pokes — via an instrumented Flycast, plus the parser and KB
  writeups.
- **Out — Phase 3:** disassembling the cart-read / input-decode / boot code;
  confirming the binary makes no `jsr` into BIOS ROM (§8-3). Phase 2 records
  *what* the game requests, not *which function* issues it.
- **Out — Phase 5:** actually cutting assets. Phase 2 only measures whether
  cuts are needed.
- Decrypt/decompress handling is irrelevant — the dump is already decrypted;
  the decrypt mode bit is recorded, never set.

## Exit criteria

Phase 2 is done when:

1. `cart-streaming-map.csv` holds the runtime request tuples from an iterative
   capture, passing the cart-map sanity checks.
2. Per-region RAM peaks are recorded with a verdict on whether Dreamcast
   memory sizes require asset cuts (feeds Phase 5).
3. `input-map.md` maps the 7 gameplay controls plus coin and test to MIE bits.
4. The watchdog/serial verdict is recorded (used or not; shim or ignore).
5. The instrumented build is reproducible from `patches/flycast-instrument.diff`
   + the `tooling.md` recipe.
6. `00-status.md` is advanced to Phase 3.
