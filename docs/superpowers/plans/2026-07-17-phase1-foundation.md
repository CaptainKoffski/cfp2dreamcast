# Phase 1: Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Equip the macOS dev machine, verify Cleopatra Fortune Plus boots and plays in Naomi emulation, and build a knowledge base that makes future agent sessions self-sufficient.

**Architecture:** No product code in this phase — the deliverables are a git-tracked knowledge base (`docs/kb/`), one small stdlib-only Python script that parses the Naomi ROM header, installed/verified tooling (Flycast, Ghidra, MAME source, netboot), and a documented, reproducible boot of the game in Flycast.

**Tech Stack:** git, Python 3 stdlib, Homebrew, Flycast (Naomi/DC emulator), Ghidra (SuperH4 processor module), MAME source tree (reference only), DragonMinded netboot (reference/tooling).

**Spec:** `docs/superpowers/specs/2026-07-17-phase1-foundation-design.md`

> **⚠ USER PREREQUISITE — start now:** Task 6 blocks until the user supplies the Naomi BIOS as `bios/naomi.zip` (the MAME-format Naomi BIOS set). Everything else proceeds without it, so start sourcing it while Tasks 1–5 run.

## Global Constraints

- Dev machine is macOS (darwin); Homebrew is assumed installed.
- The ROM `Cleopatra Fortune Plus.dat` stays at repo root; never committed (`*.dat` is gitignored); never uploaded anywhere.
- The Naomi BIOS lives at `bios/naomi.zip`; user-supplied, never committed.
- Every hardware claim in a KB doc carries a citation (source file path, or URL). Emulator/MAME source code outranks wikis; on conflict, record both with citations.
- Every tool install is recorded in `docs/kb/tooling.md`: version, exact install commands, basic usage.
- `tools/` holds third-party clones and generated binaries; it is gitignored.
- Deferred — do NOT install in Phase 1: KallistiOS/dc-chain toolchain, disc-image builders (mkdcdisc etc.), MAME builds or MAME romsets.
- Research questions that can't be answered get recorded under "Open questions" in the relevant KB doc — never guessed.
- Commit at the end of every task.
- Task order: 1 → 2 → {3, 4, 5 in any order} → 6 → 7. Task 6 additionally blocks on the user-supplied BIOS.

---

### Task 1: Repo scaffolding, CLAUDE.md, status doc

**Files:**
- Modify: `.gitignore`
- Create: `CLAUDE.md`
- Create: `docs/kb/00-status.md`

**Interfaces:**
- Consumes: nothing (first task).
- Produces: `docs/kb/00-status.md` containing a "Phase 1 checklist" whose exact line labels later tasks flip from `- [ ]` to `- [x]` (labels are verbatim in the content below — do not reword them).

- [ ] **Step 1: Extend .gitignore**

Append to `.gitignore` (current content: `*.dat`, `tools/`, `.DS_Store`):

```
bios/
.venv/
```

- [ ] **Step 2: Write CLAUDE.md**

```markdown
# Cleopatra Fortune Plus — Naomi → Dreamcast port

Port of the Sega Naomi game *Cleopatra Fortune Plus* to Sega Dreamcast by
static binary conversion (no source code), following the techniques of the
community Atomiswave→Dreamcast ports.

- **Start here:** `docs/kb/00-status.md` — project state, strategy, next step.
- **Knowledge base:** `docs/kb/` — architecture notes, conversion playbook,
  game dump notes, tooling.
- **Specs & plans:** `docs/superpowers/specs/`, `docs/superpowers/plans/`.
- **ROM:** `Cleopatra Fortune Plus.dat` (repo root, gitignored — never commit,
  never upload).
- **BIOS:** `bios/naomi.zip` (user-supplied, gitignored).

Rules: every hardware claim in the KB carries a citation; emulator source
code outranks wikis; record every tool install in `docs/kb/tooling.md`.
```

- [ ] **Step 3: Write docs/kb/00-status.md**

```markdown
# Project status

**Updated:** 2026-07-17 (Phase 1, Task 1)

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

1. **Foundation — IN PROGRESS** (repo, knowledge base, tooling, boot verification)
2. Instrumented analysis (cart-access + RAM logging via modified Flycast)
3. Reverse engineering (Ghidra on the 1 MB boot binary)
4. Conversion (loader + shims + patches → bootable GDI)
5. Fit & polish (RAM/asset cuts if measurements demand, hardware testing)

## Phase 1 checklist

- [x] Repo scaffolding, CLAUDE.md, this doc
- [ ] game.md — parsed ROM header
- [ ] naomi-vs-dreamcast.md — architecture delta
- [ ] atomiswave-method.md — AW conversion playbook
- [ ] Tools installed: Flycast, Ghidra, entrypoint sanity check
- [ ] Game boots & plays in Flycast (blocked on user bios/naomi.zip)
- [ ] Exit audit + fresh-agent test

## Next step

Execute the Phase 1 plan: `docs/superpowers/plans/2026-07-17-phase1-foundation.md`.

## Key facts so far

- ROM: `Cleopatra Fortune Plus.dat`, ~109 MB decrypted Naomi cart image,
  standard NAOMI header intact.
- The game loads only 1 MB at boot: ROM offset 0x0 → RAM 0x8c020000,
  entrypoint 0x8c04ae2c (header load table).
- The rest of the cart is read at runtime via the ROM-board interface —
  on DC this must become GD-ROM streaming / RAM preload.
```

- [ ] **Step 4: Verify and commit**

Run: `git status --short`
Expected: `M .gitignore`, `?? CLAUDE.md`, `?? docs/kb/`

```bash
git add .gitignore CLAUDE.md docs/kb/00-status.md
git commit -m "docs: scaffold knowledge base entrypoint and status doc"
```

---

### Task 2: ROM header parser + game.md + netboot reference

**Files:**
- Create: `scripts/parse_header.py`
- Create: `docs/kb/game.md`
- Create: `docs/kb/tooling.md`
- Create (gitignored clone): `tools/netboot/`
- Modify: `docs/kb/00-status.md`

**Interfaces:**
- Consumes: `Cleopatra Fortune Plus.dat` at repo root.
- Produces: `scripts/parse_header.py` — CLI `python3 scripts/parse_header.py <rom>` printing a markdown bullet list to stdout, exit 0, raises `AssertionError` if the file lacks the `NAOMI` magic. `docs/kb/game.md` with sections `## Header`, `## Runtime observations` (filled by Task 6), `## Open questions`. `docs/kb/tooling.md` with one `### <tool>` section per tool (started here, extended by Tasks 3/5/6).

- [ ] **Step 1: Clone netboot as format reference**

```bash
git clone --depth 1 https://github.com/DragonMinded/netboot.git tools/netboot
ls tools/netboot/naomi/rom.py
```

Expected: clone succeeds; `rom.py` exists. If the layout differs, locate the header parser with `find tools/netboot -name '*.py' | xargs grep -l 'NAOMI'` and use that file as the reference below.

- [ ] **Step 2: Write scripts/parse_header.py**

Stdlib-only on purpose — netboot is the format *reference*, not a runtime dependency.

```python
#!/usr/bin/env python3
"""Parse a decrypted Naomi cart image header; print markdown to stdout.

Field offsets cross-checked against DragonMinded's netboot naomi/rom.py
(tools/netboot), the battle-tested reference for this format.
"""
import struct
import sys


def cstr(b):
    return b.decode("ascii", "replace").rstrip("\x00 ")


def print_entries(hdr, off, label):
    print(f"- **{label} load entries:**")
    for i in range(8):
        rom, ram, n = struct.unpack_from("<III", hdr, off + 12 * i)
        if rom == 0xFFFFFFFF or (rom == 0 and n == 0):
            break
        print(f"  - ROM 0x{rom:08x} -> RAM 0x{ram:08x}, 0x{n:x} bytes")


def main(path):
    with open(path, "rb") as f:
        hdr = f.read(0x500)
        f.seek(0, 2)
        size = f.tell()
    magic = cstr(hdr[0:0x10])
    assert magic == "NAOMI", f"not a Naomi image: magic={magic!r}"
    print(f"- **File:** `{path}` ({size:,} bytes)")
    print(f"- **Magic:** `{magic}`")
    print(f"- **Publisher:** {cstr(hdr[0x10:0x30])}")
    regions = ["Japan", "USA", "Export", "Korea", "Australia",
               "Reserved1", "Reserved2", "Reserved3"]
    for i, name in enumerate(regions):
        print(f"- **Title ({name}):** {cstr(hdr[0x30 + 0x20 * i:0x50 + 0x20 * i])}")
    print_entries(hdr, 0x360, "Main")
    print_entries(hdr, 0x3C0, "Test")
    main_ep, test_ep = struct.unpack_from("<II", hdr, 0x420)
    print(f"- **Entrypoint (main):** 0x{main_ep:08x}")
    print(f"- **Entrypoint (test):** 0x{test_ep:08x}")


if __name__ == "__main__":
    main(sys.argv[1])
```

- [ ] **Step 3: Cross-check offsets against netboot**

Run: `grep -n '0x360\|0x420\|entrypoint\|load' tools/netboot/naomi/rom.py | head -30` and read the surrounding code.
Expected: netboot's offsets for titles (0x30), main load entries (0x360), test load entries (0x3C0), and entrypoints (0x420/0x424) match the script. If netboot disagrees anywhere, **netboot wins** — fix the script and note the correction in `game.md`.

- [ ] **Step 4: Run the parser and verify against known values**

Run: `python3 scripts/parse_header.py "Cleopatra Fortune Plus.dat"`
Expected output (line for line; file size is recorded as measured):

```
- **File:** `Cleopatra Fortune Plus.dat` (<measured size> bytes)
- **Magic:** `NAOMI`
- **Publisher:** SEGA ENTERPRISES,LTD.
- **Title (Japan):** CLEOPATRA FORTUNE PLUS
- **Title (USA):** SAMPLE GAME IN USA--------
- **Title (Export):** SAMPLE GAME IN EXPORT-----
- **Title (Korea):** SAMPLE GAME IN KOREA------
- **Title (Australia):** SAMPLE GAME IN AUSTRALIA--
- **Title (Reserved1):** SAMPLE GAME RESERVED 1
- **Title (Reserved2):** SAMPLE GAME RESERVED 2
- **Title (Reserved3):** SAMPLE GAME RESERVED 3
- **Main load entries:**
  - ROM 0x00000000 -> RAM 0x8c020000, 0x100000 bytes
- **Test load entries:**
  - ROM 0x00000000 -> RAM 0x8c020000, 0x100000 bytes
- **Entrypoint (main):** 0x8c04ae2c
- **Entrypoint (test):** 0x8c04ae36
```

(Reserved2/Reserved3 titles are a best guess from the header pattern — record whatever the script actually prints.) Any other mismatch vs this block = stop and investigate before continuing.

- [ ] **Step 5: Write docs/kb/game.md**

```markdown
# Cleopatra Fortune Plus — dump notes

The source material is `Cleopatra Fortune Plus.dat` (repo root): a decrypted
Naomi cartridge image with a standard NAOMI header. Only the Japan title
slot is populated — the game is Japan-only.

## Header

Parsed by `scripts/parse_header.py` (offsets cross-checked against
`tools/netboot/naomi/rom.py`):

<paste the verified output of Step 4 here>

Port-relevant reading of the load table: the game loads only 1 MB at boot
(ROM 0x0 → RAM 0x8c020000, entry 0x8c04ae2c); the other ~108 MB are read
at runtime through the ROM-board interface and must become GD-ROM
streaming / RAM preload on Dreamcast.

## Runtime observations

Filled in when the game first boots (Phase 1, boot verification task).

## Open questions

- MAME set name for this game (probably `cleoftp`) — confirm from MAME's
  naomi.cpp game list when the MAME source clone lands.
- Date/serial header fields (0x130 area) — not parsed yet; add to the
  script if a later phase needs them.
```

- [ ] **Step 6: Start docs/kb/tooling.md**

```markdown
# Tooling

Every tool used by this project: exact install steps, version, usage.
The environment must be rebuildable from scratch from this file.
`tools/` (gitignored) holds third-party clones and generated binaries.

### netboot (DragonMinded) — format reference

- Install: `git clone --depth 1 https://github.com/DragonMinded/netboot.git tools/netboot`
- Cloned commit: <output of `git -C tools/netboot rev-parse HEAD`>
- Use: `tools/netboot/naomi/rom.py` is the authoritative Naomi header
  format reference; also contains patching utilities useful in Phase 4.
```

- [ ] **Step 7: Flip the status checkbox**

In `docs/kb/00-status.md` change `- [ ] game.md — parsed ROM header` to `- [x] game.md — parsed ROM header` and bump the `**Updated:**` line.

- [ ] **Step 8: Commit**

```bash
git add scripts/parse_header.py docs/kb/game.md docs/kb/tooling.md docs/kb/00-status.md
git commit -m "feat: Naomi header parser + game dump notes"
```

---

### Task 3: MAME source reference + naomi-vs-dreamcast.md

**Files:**
- Create (gitignored clone): `tools/mame/` (sparse: `src/mame/sega/` only)
- Create: `docs/kb/naomi-vs-dreamcast.md`
- Modify: `docs/kb/tooling.md`, `docs/kb/00-status.md`

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: `docs/kb/naomi-vs-dreamcast.md` with the eight sections listed in Step 3 — the architecture-delta doc consumed by every later phase.

- [ ] **Step 1: Sparse-clone MAME's Sega driver directory**

The full MAME tree is gigabytes; we need one directory.

```bash
git clone --depth 1 --filter=blob:none --sparse https://github.com/mamedev/mame.git tools/mame
git -C tools/mame sparse-checkout set src/mame/sega
ls tools/mame/src/mame/sega/ | grep -ci naomi
```

Expected: clone succeeds; final count ≥ 3 (naomi driver files present). Identify the full relevant file list with `ls tools/mame/src/mame/sega/ | grep -iE 'naomi|dc|mie|atomiswave|jvs'`.

- [ ] **Step 2: Read the primary sources**

- `tools/mame/src/mame/sega/naomi.cpp` — the top-of-file comment block is the single best public description of the Naomi hardware (memory map, ROM board, JVS, EEPROM, BIOS behavior). Read it fully.
- Cart interface details: `grep -rn '5f70' tools/mame/src/mame/sega/*.cpp *.h` and read the register handlers (ROM board offset/data registers, DMA).
- DC side for comparison: `dc.cpp` and related machine files in the same directory.
- Web (cite URLs): segaretro.org Sega NAOMI page, dreamcast.wiki, DragonMinded netboot docs/README (`tools/netboot/`), searches like "NAOMI memory map", "NAOMI BIOS syscalls", "NAOMI programming manual". Use WebSearch/WebFetch.

- [ ] **Step 3: Write docs/kb/naomi-vs-dreamcast.md**

Required sections — every claim cited (`file:line` for source code, URL for web); anything unresolved goes to Open questions rather than being guessed:

```markdown
# Naomi vs Dreamcast — architecture delta

## 1. Summary table
<component | Naomi | Dreamcast | port impact — one row each for CPU, GPU,
sound, main RAM, video RAM, sound RAM, game storage, input, settings
storage, boot/BIOS>

## 2. Memory maps
<main/video/sound RAM base addresses, sizes, mirrors on both machines;
where cart data appears on Naomi vs where GD-ROM appears on DC>

## 3. Cartridge interface (G1 bus)
<the 0x5f70xx register set: offset/data registers, PIO vs DMA reads, how a
game typically streams cart data; note the address-space collision with
the DC GD-ROM drive — this is why the trap approach was rejected>

## 4. Input path
<JVS chain on Naomi and how game code actually sees inputs (BIOS-maintained
structures? direct polling?) vs Maple controller reads on DC>

## 5. EEPROM & settings
<Naomi 93C46 serial EEPROM + game settings storage vs DC flashrom>

## 6. BIOS & boot
<Naomi BIOS boot sequence: header parsing, load entries, jump to
entrypoint; what the BIOS leaves resident in RAM (syscall vectors at
0x8c0000xx?) vs DC boot (IP.BIN, 1ST_READ.BIN) and DC BIOS syscalls>

## 7. Timers, RTC, misc deltas
<anything else that differs and could bite: RTC, serial, DIMM board
presence, watchdog>

## 8. Open questions
<numbered list of unresolved items, each with what was tried>
```

- [ ] **Step 4: Verify section completeness**

Run: `grep -c '^## ' docs/kb/naomi-vs-dreamcast.md`
Expected: `8`. Then skim each section confirming it has ≥ 1 citation (source path or URL); sections without citations are not done.

- [ ] **Step 5: Record MAME clone in tooling.md, flip status checkbox**

Append to `docs/kb/tooling.md`:

```markdown
### MAME source (reference only — never built, never run)

- Install: `git clone --depth 1 --filter=blob:none --sparse https://github.com/mamedev/mame.git tools/mame`
  then `git -C tools/mame sparse-checkout set src/mame/sega`
- Cloned commit: <output of `git -C tools/mame rev-parse HEAD`>
- Use: `src/mame/sega/naomi.cpp` top comment = Naomi hardware bible;
  register handlers document the cart/G1 interface. MAME romsets/builds are
  deliberately out of scope (our dump is decrypted; MAME wants originals).
```

In `docs/kb/00-status.md`: flip `- [ ] naomi-vs-dreamcast.md — architecture delta` to `- [x]`, bump `**Updated:**`.

- [ ] **Step 6: Commit**

```bash
git add docs/kb/naomi-vs-dreamcast.md docs/kb/tooling.md docs/kb/00-status.md
git commit -m "docs: Naomi vs Dreamcast architecture delta"
```

---

### Task 4: atomiswave-method.md — the conversion playbook

**Files:**
- Create: `docs/kb/atomiswave-method.md`
- Modify: `docs/kb/00-status.md`

**Interfaces:**
- Consumes: `tools/mame/src/mame/sega/` from Task 3 (for Atomiswave hardware files; if executing before Task 3, do Task 3 Step 1 first).
- Produces: `docs/kb/atomiswave-method.md` with the six sections in Step 2 — the Phase 4 conversion playbook template.

- [ ] **Step 1: Research the Atomiswave→DC conversion scene**

- MAME: `ls tools/mame/src/mame/sega/ | grep -i atomiswave`, read the Atomiswave hardware file(s) — AW is a DC variant; note exactly how it differs (RAM sizes, ROM mapping, input hardware).
- WebSearch (cite every URL used): `megavolt85 atomiswave dreamcast conversion`, `site:dreamcast-talk.com atomiswave port`, `github atomiswave dreamcast converter`, `atomiswave cdi conversion how it works`. The dreamcast-talk.com threads by megavolt85 (the main author of these ports) and any GitHub repos with converter source are the primary sources.
- If converter source code is found on GitHub, skim it and cite specific files — that's stronger evidence than forum posts.

- [ ] **Step 2: Write docs/kb/atomiswave-method.md**

Required sections, same citation rules as Task 3:

```markdown
# The Atomiswave→Dreamcast method

## 1. Atomiswave hardware vs Dreamcast
<the delta AW porters had to bridge, from MAME source: RAM, ROM mapping,
input, settings — table like naomi-vs-dreamcast.md's summary>

## 2. Known conversions & authors
<who ported what; scale of the effort; links>

## 3. Technique catalog
<per touchpoint — boot/loader, ROM access redirection, input mapping,
EEPROM/settings, sound, RAM relocation: what the AW ports patched and how,
as concretely as sources allow>

## 4. Tools & source code
<links to any released converter tools/source, what each does>

## 5. What transfers to Naomi, what doesn't
<AW→DC had near-identical RAM; Naomi→DC has a 2× RAM gap everywhere plus a
different cart interface — spell out which techniques carry over directly,
which need adaptation, which don't apply>

## 6. Open questions
<numbered, with what was tried>
```

- [ ] **Step 3: Verify section completeness**

Run: `grep -c '^## ' docs/kb/atomiswave-method.md`
Expected: `6`. Skim: each section cited, section 3 covers all six touchpoint categories (or records why one is unknown).

- [ ] **Step 4: Flip status checkbox and commit**

In `docs/kb/00-status.md`: flip `- [ ] atomiswave-method.md — AW conversion playbook` to `- [x]`, bump `**Updated:**`.

```bash
git add docs/kb/atomiswave-method.md docs/kb/00-status.md
git commit -m "docs: Atomiswave conversion playbook"
```

---

### Task 5: Install Flycast + Ghidra, entrypoint disassembly sanity check

**Files:**
- Create: `scripts/ghidra/disasm_entry.py`
- Create (gitignored): `tools/boot.bin`, `tools/ghidra-proj/`
- Modify: `docs/kb/tooling.md`, `docs/kb/00-status.md`

**Interfaces:**
- Consumes: entrypoint/load facts from `docs/kb/game.md` (Task 2).
- Produces: installed `/Applications/Flycast.app` (consumed by Task 6); working Ghidra headless + SuperH4 setup and `scripts/ghidra/disasm_entry.py` (the Phase 3 platform); `tools/boot.bin` (first 1 MB of the ROM).

- [ ] **Step 1: Install Flycast**

```bash
brew install --cask flycast
ls /Applications/Flycast.app/Contents/MacOS/Flycast
brew list --cask --versions flycast
```

Expected: binary exists; version prints. Fallback if the cask doesn't exist: download the macOS build from https://github.com/flyinghead/flycast/releases, move `Flycast.app` to `/Applications`, record the actual method used.

- [ ] **Step 2: Install Ghidra (+ Java if needed)**

```bash
java -version 2>&1 | head -1   # if this reports java 17+, skip temurin
brew install --cask temurin
brew install --cask ghidra
ls -d "$(brew --prefix)"/Caskroom/ghidra/*/ghidra_*_PUBLIC
```

Expected: a ghidra home directory prints. Record versions.

- [ ] **Step 3: Extract the 1 MB boot binary**

```bash
dd if="Cleopatra Fortune Plus.dat" of=tools/boot.bin bs=1m count=1
```

Expected: `1+0 records in`, `1+0 records out`, 1048576 bytes.

- [ ] **Step 4: Write scripts/ghidra/disasm_entry.py**

Ghidra post-script (Jython). The header says load base 0x8c020000, entry 0x8c04ae2c → the entry is at file offset 0x2ae2c, inside boot.bin.

```python
# Ghidra post-script: disassemble at the Naomi header entrypoint and print
# the first 32 instructions. Sanity-checks that SuperH4:LE:32 decodes the
# boot binary imported at base 0x8c020000.
from ghidra.app.cmd.disassemble import DisassembleCommand

ENTRY = 0x8c04ae2c
addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(ENTRY)
DisassembleCommand(addr, None, True).applyTo(currentProgram)
ins = currentProgram.getListing().getInstructionAt(addr)
n = 0
while ins is not None and n < 32:
    println("%s  %s" % (ins.getAddress(), ins))
    ins = ins.getNext()
    n += 1
if n == 0:
    println("FAIL: no instructions decoded at 0x%08x" % ENTRY)
```

- [ ] **Step 5: Run the headless sanity check**

```bash
GHIDRA_HOME=$(ls -d "$(brew --prefix)"/Caskroom/ghidra/*/ghidra_*_PUBLIC | head -1)
mkdir -p tools/ghidra-proj
"$GHIDRA_HOME/support/analyzeHeadless" tools/ghidra-proj cleo \
  -import tools/boot.bin -overwrite \
  -processor "SuperH4:LE:32:default" \
  -loader BinaryLoader -loader-baseAddr 0x8c020000 \
  -noanalysis -scriptPath scripts/ghidra -postScript disasm_entry.py
```

Expected: 32 lines of the form `8c04aexx  <sh-4 instruction>` with plausible SH-4 mnemonics (`mov`, `mov.l`, `sts.l`, `bra`, `jsr`, `nop`, …) and no `FAIL:` line. A typical SH-4 function starts by pushing registers (`mov.l r14,@-r15` / `sts.l pr,@-r15`). Garbage signs — all-identical words, undecodable bytes — mean the base address or offset math is wrong: stop and re-derive from `game.md` before proceeding.

- [ ] **Step 6: Record installs in tooling.md, flip status checkbox**

Append to `docs/kb/tooling.md` (fill measured versions):

```markdown
### Flycast — <version>

- Install: `brew install --cask flycast`
- Run: `open -a Flycast` or `/Applications/Flycast.app/Contents/MacOS/Flycast "<rom path>"`
- Emulates both Naomi and Dreamcast. Open source (github.com/flyinghead/flycast);
  Phase 2 instruments a source build — this is the release build.

### Ghidra — <version>

- Install: `brew install --cask temurin ghidra` (temurin only if no java 17+)
- Headless: `"$GHIDRA_HOME/support/analyzeHeadless"` — see
  `scripts/ghidra/disasm_entry.py` for the working import invocation
  (processor `SuperH4:LE:32:default`, BinaryLoader, base 0x8c020000).
- Verified: entrypoint 0x8c04ae2c disassembles to plausible SH-4.
```

In `docs/kb/00-status.md`: flip `- [ ] Tools installed: Flycast, Ghidra, entrypoint sanity check` to `- [x]`, bump `**Updated:**`.

- [ ] **Step 7: Commit**

```bash
git add scripts/ghidra/disasm_entry.py docs/kb/tooling.md docs/kb/00-status.md
git commit -m "feat: Ghidra entrypoint sanity check; install Flycast+Ghidra"
```

---

### Task 6: Boot verification in Flycast (user-gated)

**Files:**
- Create: `docs/kb/img/` (screenshots, committed)
- Modify: `docs/kb/game.md` (Runtime observations), `docs/kb/tooling.md`, `docs/kb/00-status.md`

**Interfaces:**
- Consumes: Flycast install (Task 5); **user-supplied `bios/naomi.zip`** — blocks until present.
- Produces: verified, documented, repeatable boot procedure; first runtime observations in `game.md`.

- [ ] **Step 1: USER ACTION (blocking) — supply the BIOS**

The user places the MAME-format Naomi BIOS set at `bios/naomi.zip`. Verify: `ls -l bios/naomi.zip`.

- [ ] **Step 2: Install the BIOS where Flycast finds it**

First attempt:

```bash
mkdir -p ~/Library/Application\ Support/Flycast/data
cp bios/naomi.zip ~/Library/Application\ Support/Flycast/data/
```

Launch Flycast; if it still reports a missing Naomi BIOS, find the expected data path in Flycast's Settings UI (or https://github.com/flyinghead/flycast/wiki) and move the file there. Record the working path in `tooling.md`.

- [ ] **Step 3: Load the game**

```bash
/Applications/Flycast.app/Contents/MacOS/Flycast "$(pwd)/Cleopatra Fortune Plus.dat"
```

Fallback chain if the `.dat` is rejected, in order — record which rung worked:
1. `cp "Cleopatra Fortune Plus.dat" tools/cleoftp.bin` and load the `.bin`.
2. Add the repo root as a content directory in Flycast settings and launch from the in-app game list.
3. Read the loader source to see exactly what it accepts: `git clone --depth 1 https://github.com/flyinghead/flycast.git tools/flycast`, then read `tools/flycast/core/hw/naomi/naomi_cart.cpp`; adapt (and record the finding in `tooling.md` — Phase 2 needs this clone anyway).

Expected: Flycast boots the Naomi BIOS and the game reaches its attract mode. In-emulator settings that may need setting: region **Japan** (the game is Japan-only), platform auto-detected as Naomi.

- [ ] **Step 4: Map controls and record them**

In Flycast: Settings → Controls → map the keyboard as a Naomi player-1 device: joystick directions, at least buttons 1–2, Start, **Coin**, and the **Test/Service** buttons (needed for the operator menu). Flycast's default key assignments vary by version — record the actual working keys in `tooling.md` under the Flycast section.

- [ ] **Step 5: Capture boot evidence**

Screenshot the attract mode (macOS: `Cmd+Shift+4`, window mode) → save as `docs/kb/img/flycast-attract.png`.

- [ ] **Step 6: USER ACTION — acceptance test**

The user: inserts a coin, plays one full credit with the mapped keys, then opens the operator test menu via the Test button and confirms the settings screens respond. While playing, note anything odd (frame drops, missing audio, glitches). Report observations back.

- [ ] **Step 7: Write up results**

- `docs/kb/game.md` → `## Runtime observations`: BIOS used, boot path (which fallback rung), frame rate impression, audio OK/not, test menu contents seen, any glitches, screenshot reference.
- `docs/kb/tooling.md` → Flycast section: BIOS path, exact launch command, control mapping, any settings changed.
- `docs/kb/00-status.md`: flip `- [ ] Game boots & plays in Flycast (blocked on user bios/naomi.zip)` to `- [x]`, bump `**Updated:**`.

- [ ] **Step 8: Commit**

```bash
git add docs/kb/game.md docs/kb/tooling.md docs/kb/00-status.md docs/kb/img/
git commit -m "docs: verified Naomi boot in Flycast + runtime observations"
```

---

### Task 7: Exit audit + fresh-agent test

**Files:**
- Modify: `docs/kb/00-status.md` (and any doc the audit finds lacking)

**Interfaces:**
- Consumes: everything produced by Tasks 1–6.
- Produces: Phase 1 declared complete in the status doc; next step points at the Phase 2 spec.

- [ ] **Step 1: Audit against the spec's exit criteria**

Check each; fix gaps before proceeding:
1. Five KB docs exist and are populated: `00-status.md`, `naomi-vs-dreamcast.md`, `atomiswave-method.md`, `game.md`, `tooling.md` — none empty, research docs carry citations (`grep -L 'http\|src/mame\|netboot' docs/kb/*.md` should list at most `00-status.md`).
2. Boot verification done (Task 6 checkbox flipped, screenshot exists).
3. Working tree clean: `git status --short` → empty.

- [ ] **Step 2: Fresh-agent test**

Dispatch a subagent (Agent tool, general-purpose) with exactly this prompt:

> Read ONLY these two files: `/Users/captainkoffski/AntigravityProjects/cleopatra/CLAUDE.md` and `/Users/captainkoffski/AntigravityProjects/cleopatra/docs/kb/00-status.md`. Do not read anything else. Then answer: (a) What is this project and what strategy does it use? (b) What phase is it in and what has been completed? (c) What is the next concrete step? Answer from the files only.

Pass = all three answers match reality. Fail = fix the docs (that's the bug — not the agent), re-dispatch, repeat until pass.

- [ ] **Step 3: Declare Phase 1 complete**

In `docs/kb/00-status.md`: flip `- [ ] Exit audit + fresh-agent test` to `- [x]`; change the Phases list line 1 to `1. **Foundation — DONE <date>**` and line 2 to `2. **Instrumented analysis — NEXT** ...`; replace the `## Next step` body with: `Brainstorm and spec Phase 2 (instrumented analysis): cart-access and RAM logging via a modified Flycast source build.`

- [ ] **Step 4: Commit**

```bash
git add docs/kb/00-status.md
git commit -m "docs: Phase 1 complete — exit audit and fresh-agent test passed"
```
