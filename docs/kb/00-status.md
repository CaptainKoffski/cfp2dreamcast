# Project status

**Updated:** 2026-07-20 (Phase 4 complete)

## What this is

Static binary conversion of *Cleopatra Fortune Plus* (Sega Naomi) to Sega
Dreamcast, Atomiswave-port style: patch the Naomi-specific touchpoints in
the game binary (cart reads → GD-ROM loads, JVS input → controllers,
EEPROM/coin logic → shims) and boot it from a GDI via a custom loader.
Replacement pieces are structured as a small reusable library.
Spec: `docs/superpowers/specs/2026-07-17-phase1-foundation-design.md`.

## Decisions

- Target: real Dreamcast hardware (GDEMU-class ODE). Emulators are dev
  tools, not the goal.
- Personal project first, possible community release later — pipeline must
  stay reproducible and documented.
- A generic trap-based "Naomi runtime" was rejected: Naomi's cart interface
  shares hardware addresses with the DC's GD-ROM drive.

## Phases

1. **Foundation — DONE 2026-07-18** (repo, knowledge base, tooling, boot verification)
2. **Instrumented analysis — DONE 2026-07-18** (cart-streaming map, RAM/serial measurements, input map via instrumented Flycast)
3. **Reverse engineering — DONE 2026-07-18** (Ghidra headless + interpreter-mode dynamic analysis; entry chain, SP verdict, cart-read fn, input fn, EEPROM fn, BIOS verdict — see `docs/kb/boot-binary.md`)
4. **Conversion — DONE 2026-07-20** (loader + freestanding shim + 28-patch
   table → bootable GDI; boots, runs attract, playable 1P+2P with free-play —
   all Flycast-confirmed; see `phase4-conversion.md`)
5. **Real-hardware testing & fit — NEXT** (run `build/cleo.gdi` on a real
   Dreamcast via a GDEMU-class ODE; watch items: GD-ROM PIO→DMA latency,
   VRAM ~9.2 MB > DC 8 MB texture fit, sound-RAM fit, cart-stream cache
   coherency on real cache)

## Phase 1 checklist

- [x] Repo scaffolding, CLAUDE.md, this doc
- [x] game.md — parsed ROM header
- [x] naomi-vs-dreamcast.md — architecture delta
- [x] atomiswave-method.md — AW conversion playbook
- [x] Tools installed: Flycast, Ghidra, entrypoint sanity check
- [x] Game boots & plays in Flycast (user-confirmed 2026-07-18: attract mode, free-play + coin, test menu)
- [x] Exit audit + fresh-agent test (passed 2026-07-18 — clean-context agent identified project, state, next step from CLAUDE.md + this doc alone)

## Next step

Phase 5 — real-hardware testing. The port is functionally complete and
Flycast-confirmed (M1–M4, free-play, 2P). The user runs `build/cleo.gdi` on a
real Dreamcast via a GDEMU-class SD-card ODE (build + run guide:
`phase4-conversion.md` §"Running on real hardware"). Test order on HW:

1. **Graphics** — cart-streamed assets render (the C1 cache-coherency fix,
   Task 20, must hold on real cache; Flycast has none, so it cannot confirm).
2. **Streaming** — frame hitches / audio underruns on stage loads (I1: GD-ROM
   PIO latency; the Phase-5 GD-DMA upgrade is the mitigation).
3. **Controller input** — real Maple `GetCondition` (I2: relies on KOS's
   one-time Maple HW setup persisting through handoff).

Then fit: VRAM ~9.2 MB > DC 8 MB (likely texture cuts), sound-RAM fit.

## Key facts so far

- ROM: `Cleopatra Fortune Plus.dat`, ~109 MB decrypted Naomi cart image,
  standard NAOMI header intact.
- The game loads only 1 MB at boot: ROM offset 0x0 → RAM 0x8c020000,
  entrypoint 0x8c04ae2c (header load table).
- The rest of the cart is read at runtime via the ROM-board interface —
  on DC this must become GD-ROM streaming / RAM preload.

### Phase 4 findings (see `phase4-conversion.md`)

Shipped deliverable = **KOS loader (`1ST_READ.BIN`) + freestanding SH-4 shim
(`0x8cfc0000`) + 28-patch table + GDI**. All milestones **Flycast-confirmed**
(real hardware untested — Phase 5):

- **M1 boot, M2 stream, M3 attract, M4 input, free-play, 2P** — all reached in
  Flycast (attract runs, playable 1P+2P, FREE PLAY on-screen).
- **Loader:** reads the 1 MB game image from the GDI (`CART_FAD 47198`), applies
  the 28 old-byte-verified patches, places the shim + two Naomi BIOS-data
  slices, zeros the register mirrors + shim `.bss`, `dcache_purge` + handoff to
  `0x8c04ae2c`.
- **28 patches** (`scripts/build_patch_table.py`): cart/G1 register-mirror (13),
  Naomi BIOS-data pointer redirects (2), async-Maple MIE service (maple-base
  mirror + 2 fn-ptr slots), config-time JVS-enum service (7), forced I/O-spec
  check (1 `insn16`), sync EEPROM-read hook (1), cart-wait hook (1).
- **Divergence from the Phase 3 plan** (documented as the real findings): the
  plan assumed the input fn-ptr path was the whole story. Reality — the game
  reads input/EEPROM/enum through an **async-Maple MIE engine** (config-time +
  per-frame transports); it **needs the Naomi BIOS code/data supplied** in RAM
  (the "no BIOS shim" verdict was wrong); and the **I/O-board enumeration must
  report node-count ≥ 1** before the game emits its per-frame input poll. The
  shim services all three.
- **Real-HW correctness:** the C1 cache-coherency bug (cart-stream dest left in
  D-cache) is **fixed** (Task 20 — P2-uncached dest); invisible in Flycast (no
  real cache), would corrupt graphics on hardware.

### Phase 3 findings (see `boot-binary.md`)

- **Entry chain:** trampoline `0x8c04ae2c` → init `0x8c021000` (resolved by `DumpEntryChain.java`).
- **SP / main RAM:** stack at `0x8c00e6e8`–`0x8c00ef28`; Phase 2 WATERMARK hit near 32 MB was stale data; **main RAM safe on DC 16 MB, no SP relocation needed**.
- **Cart-read fn:** `FUN_8c03bd08` (`0x8c03bd08`–`0x8c03bd4d`) — runtime DMA trigger (computed SB_GDST store); static candidate `FUN_8c08063c` is a separate config-time builder; **Phase 4 patches `FUN_8c03bd08`**.
- **Input fn:** Maple store routine `0x8c0315ce` + `FUN_8c03c2c6`; **Phase 4 shims BOTH to DC `GetCondition`**. (Phase 3 counted 369×/7× on sub-0x15 only — Phase 4 Task 4 showed the steady-state poll is sub 0x33 from `FUN_8c03c2c6`, 23,762× in Phase 3's own capture-pc.log; `boot-binary.md` §5 addendum.)
- **EEPROM fn:** same two Maple sites (sub `0x01`/`0x03`); **Phase 4 forces free-play defaults**.
- **BIOS verdict:** BIOSREF=0 + BIOSEXEC=0 across both captures; **no BIOS-call shim needed**.

### Phase 2 findings (see `cart-streaming-map.md`, `phase2-measurements.md`, `input-map.md`)

- **Cart streaming map:** 388 unique DMA `(cart offset, length, dest)` triples
  captured (attract + demo + play-to-game-over), cart span
  `0x800000`..`0x609c000`; streams almost entirely by DMA (1 PIO seek). Top
  ~12 MB of cart never streamed (known gap). Feeds the Phase 4 GD-ROM reissue.
- **RAM verdict:** main-RAM asset placement 11.2 MB (fits DC 16 MB). The scan
  hit near the top of Naomi's 32 MB was **stale data, not a real stack** —
  Phase 3 pinned the SP low in RAM (`0x8c00e6e8`..`0x8c00ef28` during play; see
  `boot-binary.md` §3), so **main RAM is safe on DC's 16 MB with no SP
  relocation**. VRAM ~9.2 MB (over DC 8 MB → likely texture cuts in Phase 5).
  Sound RAM inconclusive (scan artifact).
- **Input map:** all 7 gameplay controls confirmed to single JVS bits
  (Start 0x8000, Up/Down/Left/Right 0x2000/1000/0800/0400, B1/B2 0x0200/0100).
- **Serial/watchdog:** 0 pokes → no serial or watchdog shim needed.
