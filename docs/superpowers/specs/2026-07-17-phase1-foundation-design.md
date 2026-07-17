# Phase 1: Foundation — Design

**Date:** 2026-07-17
**Status:** Approved design, pending implementation plan

## Project context

The overall project ports **Cleopatra Fortune Plus** (Sega Naomi, 2D puzzle game) to the Sega Dreamcast without source code, via ROM conversion. Naomi and Dreamcast share the same CPU (SH-4), GPU family (PowerVR2), and sound hardware (AICA), so game code runs natively on both. What differs: Naomi has twice the RAM in every pool (32/16/8 MB main/video/sound vs DC's 16/8/2 MB), reads data from a cartridge instead of GD-ROM, takes input over JVS instead of Maple controllers, stores settings in an arcade EEPROM, and boots through a different BIOS.

The source material is `Cleopatra Fortune Plus.dat` (109 MB, repo root, gitignored): a decrypted Naomi cartridge image with an intact standard header. The header load table shows the game loads only **1 MB** (ROM offset 0 → `0x8c020000`, entrypoint `0x8c04ae2c`) at boot and reads the rest of the cart at runtime through the ROM-board interface — the same runtime-streaming pattern the Atomiswave→Dreamcast community conversions successfully redirected.

### Decisions made during brainstorming

- **Target:** real Dreamcast hardware. Emulators are development tools, not the goal.
- **Test rig:** Dreamcast with GDEMU-class optical drive emulator (GDI on SD card).
- **Audience:** personal first, possible community release later — keep the pipeline reproducible and documented; avoid decisions that would block a future public release.
- **Strategy (Approach 3):** static binary conversion in the proven Atomiswave style — patch each Naomi-specific touchpoint in the game binary (cart reads → GD-ROM loads, JVS input → controller reads, EEPROM/coin logic → shims) with a hand-written loader booting it from disc — but structure the replacement pieces (loader, disc-streaming shim, input mapper, settings shim) as a small reusable support library, so the work compounds for future Naomi ports.
- A generic "Naomi runtime" compatibility layer was rejected: Naomi's cart interface shares hardware addresses with the DC's GD-ROM drive, so transparent trapping is impractical.

### Phase decomposition

Each phase gets its own spec → plan → implementation cycle. Only Phase 1 is designed here.

1. **Foundation** (this spec) — repo, knowledge base, tooling, boot verification.
2. **Instrumented analysis** — run the game with cart-access and RAM logging (likely by modifying Flycast) to map what data it reads, when, and how much main/video/sound RAM it actually uses. Determines whether asset cuts are necessary.
3. **Reverse engineering** — disassemble the 1 MB boot binary; identify cart-read routines, input handling, EEPROM access, BIOS calls.
4. **Conversion** — loader + shims + binary patches; produce a bootable GDI.
5. **Fit & polish** — RAM/asset compaction if measurements demand it, real-hardware testing on GDEMU, load-time tuning.

## Phase 1 goal

By the end of Phase 1: the development machine (macOS) is fully equipped for the phases ahead, the game demonstrably boots and plays in Naomi emulation locally, and the repo contains a knowledge base that lets any future agent session pick up the work without this conversation's context.

## 1. Repository & knowledge base

Git repo in the project folder. `.gitignore` excludes the ROM (`*.dat`), cloned third-party tools (`tools/`), and emulator artifacts. Git holds docs, scripts, and specs only.

| File | Purpose |
|---|---|
| `CLAUDE.md` (root) | ~10 lines: what the project is, where the KB lives, current phase. Auto-read by future agent sessions — the entrypoint required by REQUIREMENTS.md "Data collecting". |
| `docs/kb/00-status.md` | Living "you are here" doc: strategy, phase list, what's done, what's next. Updated at every milestone. |
| `docs/kb/naomi-vs-dreamcast.md` | Architecture delta: RAM sizes and memory maps, cartridge vs GD-ROM interface, JVS vs Maple input, EEPROM, BIOS/boot differences. Researched from MAME and Flycast source plus public documentation, **with citations** so future agents can verify claims rather than trust them. |
| `docs/kb/atomiswave-method.md` | How the Atomiswave ports did it: what they patched and how. The conversion playbook template for Phase 4. |
| `docs/kb/game.md` | Everything about this specific dump: fully parsed header, load table, entrypoint, regions, plus runtime observations once it boots. |
| `docs/kb/tooling.md` | Exact install steps, versions, and run commands for every tool — the environment must be rebuildable from scratch. |

Specs live in `docs/superpowers/specs/`.

## 2. Tooling (macOS)

- **Flycast** (Homebrew release build) — the workhorse: emulates both Naomi and Dreamcast, open source (instrumented by us in Phase 2), has debugging support. **Prerequisite: requires the Naomi BIOS (`naomi.zip`), supplied by the user.**
- **MAME — cloned as source-code reference only.** Its Naomi driver is the best existing documentation of the hardware. The game is not run in MAME: MAME requires original encrypted romsets, not decrypted images like ours. Building a MAME-loadable romset is deferred unless MAME's debugger becomes necessary.
- **Ghidra** — disassembly platform for Phase 3 (built-in SH-4 support). Installed now only to sanity-check that the entrypoint disassembles to plausible SH-4 code.
- **DragonMinded's `netboot` Python tools** — proper Naomi header parsing and manipulation.
- **Deferred to Phase 4:** DC cross-compiler toolchain (KallistiOS/dc-chain) and disc-image builders (mkdcdisc and similar) — installing them now is setup that cannot yet be tested.

## 3. Boot verification

Load the `.dat` in Flycast's Naomi mode. If Flycast rejects the file as-is, the fallback chain is:

1. Rename/repackage the image with the netboot tools into a format Flycast accepts.
2. Read Flycast's ROM-loader source to determine exactly what it expects (open source — available all project long).

**Acceptance:** insert a coin, play one full credit with keyboard controls, and reach the operator test menu. Exact steps and screenshots recorded in the KB (`tooling.md` for how-to, `game.md` for observations: frame rate, glitches, anything odd).

## 4. Exit criteria

Phase 1 is done when:

1. The repo is committed with all five KB docs populated (status, architecture delta, Atomiswave method, game notes, tooling).
2. The game is verified playable in Flycast on the user's Mac per the acceptance test above.
3. The status doc passes a **fresh-agent test**: a new session reading only `CLAUDE.md` + `docs/kb/00-status.md` knows the project state and the next step.

Then Phase 2 (instrumented analysis) gets its own spec.

## Risks & error handling

- **No BIOS:** the phase blocks on boot verification until the user sources `naomi.zip`. KB work proceeds regardless.
- **Flycast won't load the dump:** fallback chain in §3.
- **Conflicting documentation:** emulator source code wins over wikis; both get cited.
