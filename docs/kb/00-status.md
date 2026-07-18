# Project status

**Updated:** 2026-07-18 (Phase 3 complete)

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
4. **Conversion — NEXT** (loader + shims + patches → bootable GDI)
5. Fit & polish (RAM/asset cuts if measurements demand, hardware testing)

## Phase 1 checklist

- [x] Repo scaffolding, CLAUDE.md, this doc
- [x] game.md — parsed ROM header
- [x] naomi-vs-dreamcast.md — architecture delta
- [x] atomiswave-method.md — AW conversion playbook
- [x] Tools installed: Flycast, Ghidra, entrypoint sanity check
- [x] Game boots & plays in Flycast (user-confirmed 2026-07-18: attract mode, free-play + coin, test menu)
- [x] Exit audit + fresh-agent test (passed 2026-07-18 — clean-context agent identified project, state, next step from CLAUDE.md + this doc alone)

## Next step

Brainstorm and spec Phase 4 (conversion): design the loader + shims + patches
that produce a bootable Dreamcast GDI from the Naomi binary. Phase 3 produced
the concrete patch targets:

- **Cart DMA intercept:** patch `FUN_8c03bd08` (`0x8c03bd08`–`0x8c03bd4d`),
  specifically the SB_GDST store at `0x8c03bd26` — redirect to GD-ROM reads.
- **Input shim:** patch BOTH Maple sites — `FUN_8c03c2c6` (steady-state
  per-frame poll, **sub 0x33**, 34,991×/23,762× in the Phase 4/Phase 3
  captures) and the store routine at `0x8c0315ce` (boot phase, subs
  0x15/0x27 + EEPROM) — translate to DC `GetCondition`. (Phase 3's
  "primary 369×/secondary 7×" counted sub-0x15 only; see `boot-binary.md`
  §5 addendum + `phase4-conversion.md` §V4.)
- **EEPROM shim:** intercept sub `0x01`/`0x03` at the same Maple sites;
  return forced free-play defaults.
- **No SP relocation** — stack lives at `0x8c00e–fxxx`; main RAM safe.
- **No BIOS shim** — BIOSREF=0, BIOSEXEC=0; the two BIOS-ROM data-pointer pool
  words (`0xa0060000`, `0xa01ffd00`) are a low-risk watch item only.

## Key facts so far

- ROM: `Cleopatra Fortune Plus.dat`, ~109 MB decrypted Naomi cart image,
  standard NAOMI header intact.
- The game loads only 1 MB at boot: ROM offset 0x0 → RAM 0x8c020000,
  entrypoint 0x8c04ae2c (header load table).
- The rest of the cart is read at runtime via the ROM-board interface —
  on DC this must become GD-ROM streaming / RAM preload.

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
