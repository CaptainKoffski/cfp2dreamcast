# Experience reuse — design spec

**Date:** 2026-07-26
**Status:** approved (design), pending implementation
**Project:** capturing the *Cleopatra Fortune Plus* (Naomi → Dreamcast) port
experience for reuse in future console-port projects. See `CLAUDE.md`,
`docs/kb/00-status.md`.

## Purpose

The Cleopatra port produced a large body of hard-won knowledge — a working
method, deep on-hardware debugging war stories, a reproducible toolchain, and
a spec→plan→implement working cadence. All of it currently lives as a
*project-specific record* in `docs/kb/` and `docs/superpowers/`, keyed to this
repo and this game. None of it is reachable from the next port's repo, and the
highest-value lessons are buried in prose inside a 28 KB running status log.

This spec captures that experience as **portable, forward-looking assets** so
port #2 (a specific next cart, already in mind) goes smoother with fewer
re-learned mistakes.

## Why not just reuse the existing docs

The KB is a *record* — chronological, game-specific, in-repo, and it
faithfully preserves dead ends. A reuse asset must be the opposite:
procedural, game-agnostic, reachable from a fresh repo, and with the war
stories surfaced as first-class rules rather than buried mid-paragraph. Two
different jobs. The reuse assets are *distilled from* the KB (mostly an
extraction pass over `atomiswave-method.md`, `naomi-vs-dreamcast.md`, and the
gotchas mined from `00-status.md`), not a rewrite of it. The KB stays as-is.

## Decisions (user-approved in brainstorm)

1. **Reuse targets:** more console ports (the port method), the war-story
   lessons (the gotchas), and how we work together (the collaboration
   cadence). Explicitly *not* a generic low-level-RE toolkit.
2. **Working-style rules go global** — to `~/.claude/CLAUDE.md`, phrased as
   general principles so they carry to every project, not only ports.
3. **Port #2 is real**, but the user chose a **playbook doc** as the reuse
   asset now and **deferred the port skill** — it needs more thought and may
   be authored later (possibly via an agent) from this doc as source material.
4. **Tooling extraction is deferred** to a separate session the user will
   drive; recorded here as a handoff, not built now.
5. **Copyrighted dumps are never shared** — ROM `.dat`, `bios/naomi.zip`,
   donor GDIs stay gitignored. A community release ships method + tools +
   patches, never ROMs.
6. **The playbook documents the superpowers process as the proven backbone**
   and recommends it for port #2 — `brainstorming` → `writing-plans` →
   `executing-plans`, plus `systematic-debugging` and
   `verification-before-completion`, mapped per phase. It documents and
   recommends; it does not mandate or automate (no skill). The Cleopatra port
   ran exactly this way — every phase has a spec and a plan under
   `docs/superpowers/` — which is a large part of why it worked.

## §1 Asset — the playbook doc (`docs/kb/port-playbook.md`)

A single human-readable doc: the distilled, forward-looking "how to do the
next Naomi/AW → Dreamcast port." It lives in this repo's KB; the user brings
it to port #2 (or a later agent distills a skill from it). It leans on the
existing reference docs rather than re-deriving them.

Contents:

1. **The ordered method** — the five-phase arc, procedural not chronological:
   Foundation → Instrumented analysis → Reverse engineering → Conversion →
   Hardware test, with what each phase produces and its go/no-go checkpoint.
2. **The core mechanism** — patch the arcade touchpoints (cart→GD-ROM,
   JVS→controllers, EEPROM→shims) and boot from a GDI via a custom loader;
   plus the project-level decisions that generalize (real hardware is the
   goal; emulators are dev tools; a trap-based generic runtime fails because
   Naomi cart registers and the DC GD-ROM ATA registers share addresses).
3. **Gotchas** — the war stories as first-class traps, surfaced from the
   `00-status.md` running log where they are currently buried:
   - Emulator masks real hardware — Flycast-green is not a boot; benign/HLE
     emulator behavior hides real-HW spins and skips init ladders.
   - Control-test with a known-good disc before theorizing about your own
     artifact (the Dolphin-Blue boot test).
   - macOS AppleDouble sidecars (`._*`) silently poison disc/data folders;
     `dot_clean` (or master on Linux).
   - Boot binary must live in the last data track; max-clone a
     proven-bootable donor and keep your delta to one track.
   - IP.BIN/bootstrap traps (makeip's hardcoded device-info + CD-R
     bootstrap); use a donor IP.BIN.
   - Sector size is 2048, not 2352.
   - Unpatched hardware-register literals hide inside vendor-BIOS library
     thunks; grep every touchpoint address, including the indirectly reached.
   - Build on-screen observability early (breadcrumb HUD, heartbeats, PC
     sampler, hex dumps read off the TV) when no debugger can attach.
4. **What mattered vs. red herrings** — retrospective judgment: for
   "does it boot at all," exhaust structural/disc-mastering explanations
   (control test!) before deep-diving the game binary; the costly red
   herrings (2352 sector guess, the uncached-descriptor-walk fix) were in the
   binary, the real blockers were structural (mastering) and deep (BIOS
   EEPROM bit-bang).
5. **The working cadence** — that the superpowers spec→plan→execute→verify
   loop (per phase, per decision #6) was the backbone, and the phase→skill
   map for reuse.
6. **Pointers** — to the deeper reference docs (`atomiswave-method.md`,
   `naomi-vs-dreamcast.md`, `tooling.md`, `boot-binary.md`) and to §3's
   deferred tooling handoff.

## §2 Asset — global working-style rules

Five lines appended to `~/.claude/CLAUDE.md`, phrased as general principles
with the port as the origin example so they don't jar in unrelated projects:

1. **Verify on the real target.** A passing proxy — emulator, mock, staging —
   is not proof it works in production/on-hardware.
2. **Control-test when stuck on "does it work at all."** Run a known-good
   reference through the same path before theorizing about your own artifact.
3. **Every hardware/behavioral claim carries a citation; primary source
   (emulator/kernel source) outranks wikis.** (promoted from the project rule)
4. **Record every tool install** — version, flags, steps — so the pipeline
   stays reproducible. (promoted from the project rule)
5. **Never commit or upload copyrighted dumps** (ROMs, BIOS, disc images);
   keep them gitignored. (hard line)

## §3 Deferred — tooling (separate session, recorded for handoff)

Not built this session. Triaged into four buckets so the later session starts
warm:

1. **Upstream tools used as-is** (Ghidra 12.1.2, stock Flycast 2.6, makeip,
   cdrtools, KOS/sh-elf) — do *not* store or fork the bytes. The shareable
   artifact is the recipe, and `docs/kb/tooling.md` already is it. `tools/`
   stays gitignored. No action.
2. **Instrumented Flycast** (`tools/flycast-src`, the Phase 2 instrumentation)
   — the one thing worth a fork: a modification nobody can trivially
   reconstruct and the actual enabler of the RE phase. Publish as a fork with
   the instrumentation commits, or (more drift-proof) a patch/diff + build
   recipe. Never the built binary.
3. **Own reusable code** — the game-agnostic core of `loader/`, `shims/` (the
   "small reusable library"), `scripts/ghidra`, and the GDI-mastering scripts.
   Extract into a standalone `naomi-dc-port-kit` repo that port #2 depends on.
   The Cleopatra-specific parts (the 33-patch table, cart-streaming map) stay
   in this repo.
4. **Copyrighted assets** — never shared (see decision 5).

## Definition of done (this session)

- `docs/kb/port-playbook.md` exists: the ordered method, the gotchas surfaced
  from the status log, the red-herring retrospective, the working cadence, and
  pointers — accurate against the KB it was distilled from.
- The five rules are written to `~/.claude/CLAUDE.md` (created; did not exist).
- This spec is committed as the handoff record for the deferred tooling and
  the deferred skill.

## Alternatives considered

- **Port skill (auto-triggering).** The higher-leverage asset — self-contained,
  fires in a fresh repo, mandates the superpowers backbone. **Deferred**, not
  rejected: the user wants more thought and may author it later (from this
  playbook as source material), possibly via an agent.
- **Skill only, no global rules.** Would keep the working-style lessons
  port-scoped. Rejected — the user wants them to apply to every project.
- **Project memory for the working-style rules.** Rejected: the memory dir is
  keyed to this repo's path and does not follow to the next repo; only
  `~/.claude/CLAUDE.md` is cross-project.
- **Blanket-fork all tooling.** Rejected — upstream tools need a recipe not a
  byte dump, own code belongs in a kit repo not a fork, and copyrighted dumps
  never leave. Only the instrumented Flycast warrants a fork.
