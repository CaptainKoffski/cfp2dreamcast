# Project status

**Updated:** 2026-07-18 (Phase 2 complete)

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
3. **Reverse engineering — NEXT** (Ghidra on the 1 MB boot binary)
4. Conversion (loader + shims + patches → bootable GDI)
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

Brainstorm and spec Phase 3 (reverse engineering): Ghidra on the 1 MB boot
binary — confirm no BIOS `jsr` after the entrypoint (§8-3), and locate the
cart-read and input-decode functions the Phase 4 patches target,
cross-referenced against the Phase 2 cart-streaming map and input map.

## Key facts so far

- ROM: `Cleopatra Fortune Plus.dat`, ~109 MB decrypted Naomi cart image,
  standard NAOMI header intact.
- The game loads only 1 MB at boot: ROM offset 0x0 → RAM 0x8c020000,
  entrypoint 0x8c04ae2c (header load table).
- The rest of the cart is read at runtime via the ROM-board interface —
  on DC this must become GD-ROM streaming / RAM preload.

### Phase 2 findings (see `cart-streaming-map.md`, `phase2-measurements.md`, `input-map.md`)

- **Cart streaming map:** 388 unique DMA `(cart offset, length, dest)` triples
  captured (attract + demo + play-to-game-over), cart span
  `0x800000`..`0x609c000`; streams almost entirely by DMA (1 PIO seek). Top
  ~12 MB of cart never streamed (known gap). Feeds the Phase 4 GD-ROM reissue.
- **RAM verdict:** main-RAM asset placement 11.2 MB (fits DC 16 MB), but a scan
  hit near the top of Naomi's 32 MB (likely a high-address stack) is a **Phase 3
  question** before main RAM is declared safe. VRAM ~9.2 MB (over DC 8 MB → likely
  texture cuts in Phase 5). Sound RAM inconclusive (scan artifact).
- **Input map:** all 7 gameplay controls confirmed to single JVS bits
  (Start 0x8000, Up/Down/Left/Right 0x2000/1000/0800/0400, B1/B2 0x0200/0100).
- **Serial/watchdog:** 0 pokes → no serial or watchdog shim needed.
