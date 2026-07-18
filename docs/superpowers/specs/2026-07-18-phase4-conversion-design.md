# Phase 4 — Conversion: design spec

**Date:** 2026-07-18
**Status:** approved (design), pending implementation plan
**Predecessor:** Phase 3 reverse engineering
(`docs/superpowers/specs/2026-07-18-phase3-reverse-engineering-design.md`)
**Project:** static binary conversion of *Cleopatra Fortune Plus* (Naomi →
Dreamcast). See `CLAUDE.md`, `docs/kb/00-status.md`.

## Purpose

Phases 1–3 produced the method (`docs/kb/atomiswave-method.md`), the runtime
ground truth (`docs/kb/cart-streaming-map.md`, `docs/kb/input-map.md`), and
the exact patch addresses (`docs/kb/boot-binary.md`). Phase 4 spends them:
build the loader, the shims, and the patch set that turn the Naomi cart image
into a bootable Dreamcast GDI.

**Definition of done (user-approved):** the GDI boots in Flycast's
*Dreamcast* profile (not Naomi mode); attract runs; the game is playable to
game-over with a DC controller; free-play is forced. Real-hardware/GDEMU
testing is Phase 5.

## Decisions (user-approved in brainstorm)

1. **Toolchain:** KallistiOS stage-1 loader + freestanding shim blob.
   The loader runs as `1ST_READ.BIN` with KOS alive (GD-ROM reads, easy
   debugging); the shims patched into the game are `-ffreestanding` SH-4
   code with no OS runtime, because nothing of KOS survives the jump to
   `0x8c04ae2c`.
2. **Cart-DMA intercept:** register-mirror shim. Every game store to the
   Naomi cart registers `0x5f7000–0x5f7014` is repointed to a shim-owned
   mirror block; the `SB_GDST` trigger becomes a shim call. No reversing of
   the game's DMA-descriptor struct is required — the parameters are exactly
   the values the game was about to poke into the registers.
3. **Data strategy:** the full 109 MB decrypted cart image rides the GDI at
   a build-time-known LBA; the shim streams from GD-ROM on demand. Preload
   is impossible — the game streams ~88 MB of cart through ~11 MB of RAM
   buffers (overlay pattern, `cart-streaming-map.md`).

Alternatives considered and rejected:

- **Wholesale replacement of the streaming cluster's top function**
  (`FUN_8c03b81a` / descriptor ABI). Fewest patch sites, but hinges on fully
  decoding the descriptor layout; any misread field is silent corruption.
  The mirror approach needs zero struct knowledge. Rejected (kept as a
  fallback if mirror patches prove too numerous).
- **Trap-based Naomi runtime.** Already rejected at project level: the
  Naomi cart registers and the DC GD-ROM ATA registers share addresses
  (`naomi-vs-dreamcast.md` §3), so traps cannot distinguish them on real
  hardware.
- **Fully bare-metal loader / adapting DreamShell isoldr.** More code to
  hand-roll (bare metal) or a large foreign codebase to bend (isoldr) for
  no Phase 4 benefit; KOS is the proven homebrew path for the pre-jump
  stage. Rejected.

## §1 Architecture & disc layout

One GDI, three tracks. Tracks 1–2 are the standard low-density stubs;
track 3 (high density, LBA 45000) carries:

1. **IP.BIN** — standard DC bootstrap.
2. **`1ST_READ.BIN`** — the KOS loader (unscrambled; GD-ROM boots don't
   scramble).
3. **`cart.bin`** — the full decrypted cart image at a **sector-aligned,
   build-time-known LBA**, recorded by the mastering script and baked into
   the shim as a constant.

A Python mastering script (`scripts/`, alongside the existing tooling)
emits the tracks. Runtime address math is trivial:
`LBA = cart_base_lba + cart_offset / 2048`. Cart requests are 32-byte
granular (`len_aligned_32` PASS, `cart-streaming-map.md`) but not sector
aligned, so reads use a head/body/tail split: partial head/tail sectors
through a bounce buffer, whole-sector body straight to the game's
destination when its alignment allows DMA, bounced otherwise.

The ROM rule stands: `cart.bin` and every mastered image are gitignored —
never committed, never uploaded. Only scripts, patches, and shim/loader
source are committed.

### RAM map after handoff

| Range | Contents | Evidence |
|---|---|---|
| `0x8c000000–0x8c007fff` | DC BIOS syscall area — **preserved** | needed for GD syscalls; see V1 |
| `0x8c008000–0x8c00dfff` | dead IP.BIN — free | `naomi-vs-dreamcast.md` §6 |
| `0x8c00e000–0x8c00f400` | game stack | `boot-binary.md` §3 |
| `0x8c010000–0x8c01ffff` | loader — dead after jump | DC 1ST_READ slot |
| `0x8c020000–0x8c11ffff` | game image (1 MB) | header load table, `game.md` |
| `0x8c120000–0x8cb37fff` | game streaming buffers | high-water 11.2 MB, `cart-streaming-map.md` |
| `0x8cfc0000–0x8cffffff` | **shim home** (256 KB): shim code, register mirror, bounce buffer, baked EEPROM, MIE reply template, error word | verified by V2 |

## §2 Loader (KOS stage-1)

1. Boot as `1ST_READ.BIN`. Read cart bytes `0x0–0x100000` (the header load
   table's single entry, `game.md`) into a malloc'd buffer — not directly to
   `0x8c020000`, which overlaps the live KOS heap.
2. Apply the **patch table** to the buffered image. The patch table is a
   build-time artifact (list of `(address, original bytes, new bytes)`)
   generated from committed patch source — every Phase 4 binary change is
   data in one table, none are ad-hoc pokes. Original-byte verification at
   apply time catches a wrong ROM at boot.
3. Copy the **shim blob** (separate freestanding binary, linked at
   `0x8cfc0000`, embedded in the loader image) to shim home.
4. Mask interrupts, quiesce DMA, run a small relocated handoff stub:
   copy buffer → `0x8c020000`, flush caches, jump `0x8c04ae2c`. The game
   does its own VBR/CCR setup (`boot-binary.md` §7), so KOS leftovers are
   moot.

**Shim disc access:** primary path is the DC BIOS GD-ROM syscall vector
(`0x8c0000bc`, polled completion — no interrupts required). Fallback, if
V1 shows the game's init memset wipes `0x8c000000–0x8c007fff` or the
polled-syscall path proves unreliable in game context: a raw ATA polling
driver in the shim (isoldr-style, ~200 lines). The V1 check runs first in
the implementation plan because it picks between these.

## §3 Shims

All three live at shim home and enter via the patch table.

### Cart-read shim (register mirror)

- A Ghidra pass enumerates every store targeting `0x5f7000–0x5f7014`
  (ROM/DMA offset, count) in the streaming cluster (`0x8c03bxxx`,
  `boot-binary.md` §4). Each store is repointed to the mirror block at shim
  home — same value, shim-owned address. Nothing touches the DC's live ATA
  registers at `0x5f70xx` (`naomi-vs-dreamcast.md` §3 collision).
- The `SB_GDST` trigger store at `0x8c03bd26` (inside `FUN_8c03bd08`)
  becomes a call into the shim: read `(cart offset, count, dest)` from the
  mirror, issue the GD-ROM read (head/body/tail), return with completion
  faked. The read is synchronous, so `SB_GDST` reads 0 ("no DMA in
  progress") if the game polls it.
- If V3 shows the game waits on the G1 DMA-done IRQ instead of polling
  (candidate wait: `FUN_8c03bc12`, called immediately after the trigger),
  the patch table additionally short-circuits that wait against the shim's
  done flag.
- The 388-triple streaming map is the **regression oracle, not a
  whitelist**: a build-time check asserts every captured triple is servable
  from the mastered image, and a Flycast-side trace can diff shim reads
  against the map. Uncaptured reads (e.g. the never-streamed top ~12 MB)
  still work — the shim serves any offset in the image.

### Input shim

- The game reaches its Maple poll routine `0x8c0315ce` via a function
  pointer table (`0x8c0275da`/`0x8c0275e0`, `boot-binary.md` §5) — the
  patch table swaps the pointer to the shim routine; no code patch at the
  routine itself. The minor site `FUN_8c03c2c6` (7× per capture) gets an
  entry-point jump to the same shim.
- For MIE sub `0x15` (input query): issue a real Maple `GetCondition` to
  the port-A controller using the same Maple DMA hardware (shim-built
  frame, polled completion), translate DC buttons → the JVS word from
  `input-map.md` (D-pad→D-pad, A→B1 `0x0200`, B→B2 `0x0100`,
  Start→`0x8000`), and write a **byte-exact fake MIE `0x87`/`0x16` reply**
  into the game's response buffer. Reply template + buffer address come
  from V4.
- Test/Service report unpressed; coin bits are moot under free-play.

### EEPROM shim

- Same Maple entry, dispatched by subcommand. Sub `0x01`/`0x03` (read):
  return a **baked 128-byte EEPROM image**, harvested from Flycast
  Naomi-mode nvram after configuring free-play in the test menu — genuine
  CRCs, no defaults-reinit path (the AW baked-settings technique,
  `atomiswave-method.md` §3). Sub `0x0b` (write): acked, dropped.

### Out of scope (deliberate)

High-score persistence. Naomi battery SRAM (`0x00200000`) lands on DC
flashrom; the game's CRC check fails and it re-initializes scores in RAM
each boot. Acceptable for the Flycast-playable bar; V5 confirms the game
tolerates it. VMU/flashrom-backed persistence is a possible Phase 5 nicety.

## §4 Verification items (run early; each decides a design branch)

| # | Check | Tool | Decides |
|---|---|---|---|
| V1 | Init memset range at `0x8c021000` — does it cover `0x8c000000–0x8c007fff`? | Ghidra | BIOS GD syscalls vs raw ATA driver |
| V2 | Write-watch on `0x8cfc0000+` across a full play capture | `flycast-instrument.diff` extension | shim home is safe (else relocate it) |
| V3 | DMA completion mechanism: poll vs IRQ (`FUN_8c03bc12` and callers) | Ghidra | whether the wait-site patch is needed |
| V4 | One real MIE `0x87`/`0x16` response + response buffer address | instrumented Naomi-mode capture | input-shim reply template |
| V5 | References to `0x0020xxxx` (battery SRAM) | Ghidra pool-literal scan | score-CRC-fail tolerance assumption |

## §5 Risks accepted for this phase

- **VRAM 9.2 MB vs DC 8 MB** (`phase2-measurements.md`): possible texture
  corruption in DC mode. Acceptable if the game stays playable; otherwise
  minimal texture triage is pulled forward from Phase 5.
- **Sound RAM fit unmeasured** (Phase 2 scan artifact): garbled audio
  possible; deferred to Phase 5 unless it blocks playability.
- **GD-ROM latency vs cart DMA** (flagged by megavolt85,
  `atomiswave-method.md` §5): synchronous shim reads may cause visible
  hitches on real hardware. In Flycast the virtual drive is fast; the real
  measurement is Phase 5's.
- **Two BIOS-ROM data-pointer pool words** (`0xa0060000`, `0xa01ffd00`,
  `boot-binary.md` §7): low-risk watch item; if the game faults reading
  BIOS data, the loader supplies that data.

## §6 Milestones (each independently testable in Flycast DC profile)

1. **M1** — mastered GDI boots; loader prints hello. Proves mastering +
   KOS stage.
2. **M2** — game image placed, patched, jumped; game runs to its first cart
   read (expected stall). Checkpointed via SCIF serial prints from the
   shim, visible in Flycast's console.
3. **M3** — cart shim live: attract mode runs.
4. **M4** — input shim live: game controllable.
5. **M5** — baked EEPROM live: free-play, no error screens.
6. **M6** — played to game-over. **Phase 4 done.**

## §7 Error handling

Shim-grade and minimal: every shim failure path writes a magic code plus
context (offset/count/subcommand) to a fixed shim-home word and spins.
Visible in Flycast's memory view and serial log. No recovery logic — a
wedged shim with a readable code beats silent corruption.

## Deliverables

- `loader/` — KOS stage-1 loader source (committed).
- `shims/` — freestanding shim blob source + linker script (committed).
- Patch table source + generator (committed; format decided in the plan).
- `scripts/` mastering script emitting the GDI (committed; images
  gitignored).
- `patches/flycast-instrument.diff` — extended for V2/V4 as needed.
- `docs/kb/` updates: status, new `phase4-conversion.md` build/run notes,
  `tooling.md` for any new tool (KOS install recorded there).
- V1–V5 results recorded in the KB with evidence, per the citation rule.

## Scope boundaries

- **In:** loader, shims, patch table, GDI mastering, V1–V5, Flycast DC
  playability.
- **Out — Phase 5:** real-hardware/GDEMU testing, VRAM/sound-RAM fitting,
  texture cuts, GD-ROM latency tuning, score persistence.
- **Out:** any commit/upload of ROM-derived bytes (cart image, boot slice,
  mastered tracks, baked EEPROM if it embeds ROM-derived content — the
  EEPROM bytes are settings data, but ship them gitignored with the images
  to stay clearly on the safe side).

## Exit criteria

1. M1–M6 demonstrated in Flycast DC profile; M6 user-confirmed (play to
   game-over with a DC controller, free-play, no coin/error screens).
2. V1–V5 recorded in the KB with evidence.
3. Full pipeline reproducible from a fresh checkout + gitignored ROM:
   one command chain from `.dat` → GDI (documented in the KB).
4. `00-status.md` advanced to Phase 5 with Phase 4 findings summarized.
