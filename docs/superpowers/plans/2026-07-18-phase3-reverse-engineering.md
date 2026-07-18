# Phase 3 — Reverse Engineering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Locate and prove — statically (Ghidra) and dynamically (guest-PC logging) — the five boot-binary addresses Phase 4 will patch: the cart-read function, the input-decode function, the EEPROM/settings-parse function, the stack-pointer setup, and a verdict that the binary makes no BIOS calls after the entrypoint.

**Architecture:** Ghidra headless (Java scripts) is the static spine: import the 1 MB boot slice with full auto-analysis, then three scripts report MMIO literal-pool cross-references, scan for BIOS-range branch targets, and dump the entry chain + SP setup. The Phase 2 instrumented Flycast is extended with three log lines (`CARTDMAPC`, `MAPLEPC`, `BIOSEXEC`) captured under the interpreter core for exact guest PC/SP, and `scripts/parse_cart_log.py` gains five cross-checks that assert every logged PC lands inside a statically-identified function. Results land in `docs/kb/boot-binary.md`.

**Tech Stack:** Ghidra 12.1.2 headless + Java scripts (`SuperH4:LE:32:default`, BinaryLoader, base `0x8c020000`); Flycast source build (macOS/arm64, existing `tooling.md` recipe); Python 3 stdlib parser.

## Global Constraints

- **ROM content never leaves the machine.** The 1 MB boot slice (`tools/boot.bin`) and the Ghidra project (`tools/ghidra-proj/`) are ROM-derived — already covered by the `tools/` gitignore. Never commit, never upload. Same rule as `*.dat`.
- **Every hardware/address claim in the KB carries a citation** — an address, a `scripts/ghidra` script output, or a `file:line` into `tools/mame/src/mame/sega/` or `tools/netboot/docs/naomi.md`. Emulator/MAME source outranks wikis.
- **Ghidra 12 dropped Jython headless** — all analysis scripts are **Java** in `scripts/ghidra/`, committed and re-runnable.
- **Guest MMIO addresses are physical/29-bit.** Compare BIOS-range membership on `addr & 0x1fffffff < 0x00200000`. Register blocks (from `naomi-vs-dreamcast.md §3/§4`): cart `0x005f7000-0x005f7014`, G1 DMA `0x005f7400+`, Maple `0x005f6c00+`.
- **The dump is already decrypted** — the DMA-offset decrypt bit (bit 30) is recorded, never set; ignore decrypt/decompress paths.
- **Interpreter core for the Phase 3 capture pass** (`-config config:Dynarec.Enabled=no`) — the dynarec's block-granular PC is not instruction-exact. The pass is short (boot + brief play), so interpreter speed is fine.
- **Every non-trivial deliverable leaves one runnable check** (a Ghidra-output assertion or a parser assert) — no silent claims.

---

## File Structure

- `scripts/ghidra/run.sh` (create) — re-runnable headless harness: import-and-analyze `tools/boot.bin` once, then run a named post-script. The reproducibility spine (exit criterion #4).
- `scripts/ghidra/FindMmioXrefs.java` (create) — reports functions referencing each MMIO register block (targets 2, 3, 5 static discovery).
- `scripts/ghidra/ScanBiosTargets.java` (create) — scans for call/jump/literal-pool targets resolving into BIOS ROM range (target 1 static).
- `scripts/ghidra/DumpEntryChain.java` (create) — walks entry trampoline → real init, dumps it and flags every r15 (SP) write with its resolved pool constant (target 4 static).
- `patches/flycast-instrument.diff` (modify) — add `CARTDMAPC`, `MAPLEPC`, `BIOSEXEC` logging + interpreter-mode BIOS guard.
- `scripts/capture.sh` (modify) — add a `pc` pass that forces the interpreter core.
- `scripts/parse_cart_log.py` (modify) — parse the three new line types; add the five cross-checks (they take the static function ranges as arguments).
- `scripts/test_parse_cart_log.py` (modify) — unit-test the new parsing + checks.
- `docs/kb/boot-binary.md` (create) — the annotated map: entry chain, SP verdict, five target answers with static+dynamic evidence, Phase 4 patch implications.
- `docs/kb/naomi-vs-dreamcast.md`, `docs/kb/phase2-measurements.md`, `docs/kb/00-status.md`, `docs/kb/tooling.md` (modify) — mark §8-3 resolved, close the stack question, advance status, record any new tooling step.

---

## Task 1: Ghidra headless harness + full-analysis import

**Files:**
- Create: `scripts/ghidra/run.sh`
- Uses: `tools/boot.bin` (exists, 1 MB), `tools/ghidra_12.1.2_PUBLIC/` (exists), existing `scripts/ghidra/DisasmEntry.java`

**Interfaces:**
- Produces: `scripts/ghidra/run.sh import` (imports+analyzes into `tools/ghidra-proj`, project `cleo3`) and `scripts/ghidra/run.sh script NAME.java` (runs a post-script against the imported program). Every later Ghidra task calls `run.sh script …`.

- [ ] **Step 1: Write the harness**

Create `scripts/ghidra/run.sh`:

```bash
#!/bin/sh
# Re-runnable Ghidra headless harness for the Phase 3 boot-binary analysis.
# Usage:
#   scripts/ghidra/run.sh import              # import tools/boot.bin + full auto-analysis
#   scripts/ghidra/run.sh script NAME.java    # run scripts/ghidra/NAME.java on the imported program
#
# ROM content: tools/boot.bin and tools/ghidra-proj are gitignored (tools/). Never commit.
set -e
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
GHIDRA_HOME="${GHIDRA_HOME:-$REPO/tools/ghidra_12.1.2_PUBLIC}"
PROJ="$REPO/tools/ghidra-proj"
NAME=cleo3
BOOT="$REPO/tools/boot.bin"
HL="$GHIDRA_HOME/support/analyzeHeadless"

# openjdk from brew (see tooling.md) — Ghidra needs Java 21+ on PATH.
export PATH="/opt/homebrew/opt/openjdk/bin:$PATH"

if [ ! -x "$HL" ]; then echo "ERROR: analyzeHeadless not found: $HL" >&2; exit 1; fi
if [ ! -f "$BOOT" ]; then echo "ERROR: boot slice not found: $BOOT" >&2; exit 1; fi
mkdir -p "$PROJ"

case "${1:-}" in
  import)
    # No -noanalysis => full SH-4 auto-analysis (follows jmp @rN via literal pools).
    "$HL" "$PROJ" "$NAME" -import "$BOOT" -overwrite \
      -processor "SuperH4:LE:32:default" \
      -loader BinaryLoader -loader-baseAddr 0x8c020000
    ;;
  script)
    [ -n "${2:-}" ] || { echo "usage: $0 script NAME.java" >&2; exit 1; }
    "$HL" "$PROJ" "$NAME" -process boot.bin -noanalysis \
      -scriptPath "$REPO/scripts/ghidra" -postScript "$2"
    ;;
  *) echo "usage: $0 import | script NAME.java" >&2; exit 1 ;;
esac
```

- [ ] **Step 2: Make executable and run the import**

Run:
```bash
chmod +x scripts/ghidra/run.sh
scripts/ghidra/run.sh import 2>&1 | tail -20
```
Expected: analysis completes without a Java stack trace; a line like `INFO ... Analysis succeeded` and `INFO ... Import succeeded`. A `tools/ghidra-proj/cleo3.rep` directory now exists.

- [ ] **Step 3: Verify the existing sanity script still runs against the analyzed program**

Run:
```bash
scripts/ghidra/run.sh script DisasmEntry.java 2>&1 | grep -E '^8c04ae' | head
```
Expected: several `8c04ae2c …` lines of plausible SH-4 (`mov.l`, `mov`, `jmp @r1`, `nop`) and **no** `FAIL:` line — confirms the harness reaches the imported, analyzed program. If the grep is empty, the import failed: re-check `tools/boot.bin` size (`ls -la` → exactly `0x100000` = 1048576 bytes) before proceeding.

- [ ] **Step 4: Commit**

```bash
git add scripts/ghidra/run.sh
git commit -m "Phase 3 Task 1: Ghidra headless harness + full-analysis import"
```

---

## Task 2: MMIO cross-reference script (cart-read / input / EEPROM discovery)

**Files:**
- Create: `scripts/ghidra/FindMmioXrefs.java`
- Uses: `scripts/ghidra/run.sh` (Task 1)

**Interfaces:**
- Consumes: the analyzed `cleo3` program from Task 1.
- Produces: stdout report grouping, per MMIO register block, every constant found in a literal pool that equals a block address, the instruction that loads it, and the containing function (name + entry address). These function entry addresses are the candidate ranges the writeup (Task 7) and the parser checks (Task 5) consume.

- [ ] **Step 1: Write a check that fails without the script**

The game provably DMAs from the cart (Phase 2: 388 DMA requests), so the G1 DMA registers **must** be referenced by a literal-pool constant somewhere in the binary. Write the check first:

```bash
# Expected to FAIL now (script doesn't exist), PASS after Step 2.
scripts/ghidra/run.sh script FindMmioXrefs.java 2>&1 | grep -E '0x005f74|0x005f70' | head
```
Expected now: empty / error (no such script).

- [ ] **Step 2: Write the script**

Create `scripts/ghidra/FindMmioXrefs.java`:

```java
// Report literal-pool constants that equal a watched MMIO register address,
// the instruction that loads each, and its containing function. These functions
// are the Phase 4 patch candidates: cart-read (cart/G1 blocks), input-decode &
// EEPROM (Maple block). Physical addresses; the game loads them via mov.l @(disp,pc).
//@category Cleopatra
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryAccessException;

public class FindMmioXrefs extends GhidraScript {
    // (label, lo, hi) inclusive physical ranges, 29-bit.
    private static final long[][] BLOCKS = {
        {0x005f7000L, 0x005f7014L}, // cart ROM-board regs
        {0x005f7400L, 0x005f74ffL}, // G1 GD-ROM DMA channel
        {0x005f6c00L, 0x005f6cffL}, // Maple bus controller
    };
    private static final String[] LABELS = {"cart", "g1dma", "maple"};

    @Override
    public void run() throws Exception {
        Listing lst = currentProgram.getListing();
        InstructionIterator it = lst.getInstructions(true);
        int hits = 0;
        while (it.hasNext() && !monitor.isCancelled()) {
            Instruction ins = it.next();
            // SH-4 materializes MMIO addrs as pc-relative pool loads; the resolved
            // constant shows up as a scalar/reference operand Ghidra already computed.
            for (int op = 0; op < ins.getNumOperands(); op++) {
                Long v = operandValue(ins, op);
                if (v == null) continue;
                long phys = v & 0x1fffffffL;
                for (int b = 0; b < BLOCKS.length; b++) {
                    if (phys >= BLOCKS[b][0] && phys <= BLOCKS[b][1]) {
                        Function f = getFunctionContaining(ins.getAddress());
                        println(String.format("XREF block=%s const=0x%08x at=%s fn=%s@%s",
                                LABELS[b], phys, ins.getAddress(),
                                f == null ? "?" : f.getName(),
                                f == null ? "?" : f.getEntryPoint().toString()));
                        hits++;
                    }
                }
            }
        }
        // Also sweep defined data words (pool literals not yet attached to an operand).
        DataIterator di = lst.getDefinedData(true);
        while (di.hasNext() && !monitor.isCancelled()) {
            Data d = di.next();
            Long v = dataWord(d);
            if (v == null) continue;
            long phys = v & 0x1fffffffL;
            for (int b = 0; b < BLOCKS.length; b++)
                if (phys >= BLOCKS[b][0] && phys <= BLOCKS[b][1]) {
                    println(String.format("POOL  block=%s const=0x%08x at=%s",
                            LABELS[b], phys, d.getAddress()));
                    hits++;
                }
        }
        println("TOTAL hits=" + hits);
        if (hits == 0) println("FAIL: no MMIO constants found — check analysis ran");
    }

    private Long operandValue(Instruction ins, int op) {
        Object[] r = ins.getOpObjects(op);
        for (Object o : r) {
            if (o instanceof ghidra.program.model.scalar.Scalar)
                return ((ghidra.program.model.scalar.Scalar) o).getUnsignedValue();
            if (o instanceof Address)
                return ((Address) o).getOffset();
        }
        return null;
    }

    private Long dataWord(Data d) {
        try {
            if (d.getLength() == 4 && d.isDefined()) {
                Object val = d.getValue();
                if (val instanceof ghidra.program.model.scalar.Scalar)
                    return ((ghidra.program.model.scalar.Scalar) val).getUnsignedValue();
                if (val instanceof Address)
                    return ((Address) val).getOffset();
                // fall back to raw bytes (little-endian)
                return (long) (d.getInt(0)) & 0xffffffffL;
            }
        } catch (MemoryAccessException e) { /* ponytail: unreadable word => skip, not fatal */ }
        return null;
    }
}
```

- [ ] **Step 3: Run the check — now it passes**

Run:
```bash
scripts/ghidra/run.sh script FindMmioXrefs.java 2>&1 | tee /tmp/mmio.txt | grep -E 'block=(cart|g1dma)' | head
```
Expected: at least one `XREF`/`POOL` line for `block=g1dma` and/or `block=cart` (the DMA path the game provably uses). Also expect `block=maple` lines (input + EEPROM path). If `block=maple` is absent, note it — the Maple constant may be reached via a base+offset register the auto-analysis didn't fold; that becomes a manual trace in Task 7, not a blocker here.

- [ ] **Step 4: Record the candidate function list**

Run and keep the grouped result for Task 7:
```bash
grep '^XREF' /tmp/mmio.txt | sort -u
```
Expected: a short list of `fn=NAME@0x8c0…` entries. Note the distinct function entry addresses per block — these are the cart-read (targets 2), input+EEPROM (targets 3, 5) candidates.

- [ ] **Step 5: Commit**

```bash
git add scripts/ghidra/FindMmioXrefs.java
git commit -m "Phase 3 Task 2: Ghidra MMIO xref script (cart/G1/Maple candidates)"
```

---

## Task 3: BIOS-target scan + entry-chain / SP dump (static verdicts)

**Files:**
- Create: `scripts/ghidra/ScanBiosTargets.java`, `scripts/ghidra/DumpEntryChain.java`
- Uses: `scripts/ghidra/run.sh` (Task 1)

**Interfaces:**
- Consumes: analyzed `cleo3` program.
- Produces: `ScanBiosTargets` → list of any instruction whose resolved target lands in BIOS ROM (`phys < 0x00200000`), or `NONE`. `DumpEntryChain` → the entry trampoline + first init function disassembly, with every r15-writing instruction flagged and any pc-relative pool constant it loads resolved (the candidate stack-top value).

- [ ] **Step 1: Write `ScanBiosTargets.java`**

Create `scripts/ghidra/ScanBiosTargets.java`:

```java
// Static half of naomi-vs-dreamcast §8-3: does any call/jump/pool constant
// resolve into BIOS ROM (phys 0x0..0x1fffff)? bsr/bra are pc-relative +-4KB and
// can't reach BIOS from 0x8c02xxxx, so only jsr/jmp @rN (target from a pool) and
// stray pool constants in BIOS range matter. Expected result: NONE.
//@category Cleopatra
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class ScanBiosTargets extends GhidraScript {
    private static boolean inBios(long v) { long p = v & 0x1fffffffL; return p < 0x00200000L; }

    @Override
    public void run() throws Exception {
        int hits = 0;
        // (a) resolved flow references (Ghidra follows jmp @rN when the pool value is known)
        ReferenceManager rm = currentProgram.getReferenceManager();
        AddressIterator ai = rm.getReferenceSourceIterator(currentProgram.getMemory(), true);
        while (ai.hasNext() && !monitor.isCancelled()) {
            Address src = ai.next();
            for (Reference ref : rm.getReferencesFrom(src)) {
                if (ref.getReferenceType().isFlow() && inBios(ref.getToAddress().getOffset())) {
                    println(String.format("BIOSREF from=%s to=%s type=%s",
                            src, ref.getToAddress(), ref.getReferenceType()));
                    hits++;
                }
            }
        }
        // (b) any defined 32-bit pool word pointing into BIOS (a would-be call target)
        DataIterator di = currentProgram.getListing().getDefinedData(true);
        while (di.hasNext() && !monitor.isCancelled()) {
            Data d = di.next();
            if (d.getLength() == 4 && d.isDefined()) {
                try {
                    long w = ((long) d.getInt(0)) & 0xffffffffL;
                    if (inBios(w) && (w & 0x1fffffffL) >= 0x1000) { // skip small ints / null
                        println(String.format("POOLBIOS at=%s val=0x%08x", d.getAddress(), w));
                        hits++;
                    }
                } catch (Exception e) { /* skip unreadable */ }
            }
        }
        println(hits == 0 ? "RESULT: NONE — no BIOS-range targets found" : "RESULT: " + hits + " candidate(s) — inspect each");
    }
}
```

- [ ] **Step 2: Run it**

Run:
```bash
scripts/ghidra/run.sh script ScanBiosTargets.java 2>&1 | tail -20
```
Expected: `RESULT: NONE …`, or a small list of candidates. Each `POOLBIOS`/`BIOSREF` line, if any, must be inspected in Task 7 (a pool word in BIOS range could be data coincidence, not a call). Record the raw output; this is the static half of the §8-3 verdict (the dynamic half is `BIOSEXEC` in Task 6).

- [ ] **Step 3: Write `DumpEntryChain.java`**

Create `scripts/ghidra/DumpEntryChain.java`:

```java
// Target 4 static: walk the entry trampoline (0x8c04ae2c) to the real init and
// dump it, flagging every instruction that writes r15 (SP) with the pc-relative
// pool constant it loads (the candidate stack top). The dynamic sp= log (Task 6)
// is authoritative; this corroborates and, if the game sets SP itself, pins it.
//@category Cleopatra
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;

public class DumpEntryChain extends GhidraScript {
    private static final long ENTRY = 0x8c04ae2cL;

    @Override
    public void run() throws Exception {
        Address entry = addr(ENTRY);
        new DisassembleCommand(entry, null, true).applyTo(currentProgram, monitor);
        println("== entry trampoline @ " + entry + " ==");
        Address jumpTarget = dump(entry, 8);

        if (jumpTarget != null) {
            new DisassembleCommand(jumpTarget, null, true).applyTo(currentProgram, monitor);
            println("== init (jmp target) @ " + jumpTarget + " ==");
            dump(jumpTarget, 80);
        } else {
            println("NOTE: could not resolve trampoline jump target automatically — read the trampoline above by hand");
        }
    }

    // Dump n instructions from addr; return the first resolved jmp/branch target seen.
    private Address dump(Address a, int n) {
        Instruction ins = currentProgram.getListing().getInstructionAt(a);
        Address target = null;
        for (int i = 0; ins != null && i < n; i++) {
            String flag = "";
            if (writesR15(ins)) flag = "   <== writes r15 (SP)";
            println(String.format("%s  %-28s%s", ins.getAddress(), ins.toString(), flag));
            if (target == null)
                for (Reference r : ins.getReferencesFrom())
                    if (r.getReferenceType().isJump() || r.getReferenceType().isCall())
                        target = r.getToAddress();
            ins = ins.getNext();
        }
        return target;
    }

    private boolean writesR15(Instruction ins) {
        for (int op = 0; op < ins.getNumOperands(); op++) {
            for (Object o : ins.getOpObjects(op)) {
                if (o instanceof ghidra.program.model.lang.Register
                        && ((ghidra.program.model.lang.Register) o).getName().equalsIgnoreCase("r15"))
                    return true;
            }
        }
        return false;
    }

    private Address addr(long v) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v);
    }
}
```

- [ ] **Step 4: Run it**

Run:
```bash
scripts/ghidra/run.sh script DumpEntryChain.java 2>&1 | tee /tmp/entry.txt | grep -E 'r15|== ' | head
```
Expected: the two section headers and (usually) at least one `<== writes r15 (SP)` line. Read `/tmp/entry.txt` fully: find the r15 setup and the constant it loads. If r15 is loaded from a pool `mov.l @(d,pc),r15`, resolve the pool word — that is the candidate stack top. If no r15 write appears (game inherits BIOS SP), record that: the dynamic `sp=` log becomes the sole authority for target 4. Keep the value/finding for Task 7.

- [ ] **Step 5: Commit**

```bash
git add scripts/ghidra/ScanBiosTargets.java scripts/ghidra/DumpEntryChain.java
git commit -m "Phase 3 Task 3: Ghidra BIOS-target scan + entry-chain/SP dump"
```

---

## Task 4: Extend the Flycast instrumentation with PC/SP logging

**Files:**
- Modify: `patches/flycast-instrument.diff` (the in-repo patch) and the working tree under `tools/flycast-src/`
- Modify: `scripts/capture.sh`
- Rebuild output: `tools/flycast-src/build/Flycast.app/Contents/MacOS/Flycast`

**Interfaces:**
- Produces three new cartlog line formats consumed by Task 5:
  - `CARTDMAPC pc=%08x sp=%08x` — emitted alongside each existing `CARTDMA` line, in `Naomi_DmaStart`.
  - `MAPLEPC cmd=86 sub=%02x pc=%08x` — emitted at the top of `MIEImpl::handle_86_subcommand`, `sub` = `dma_buffer_in[0]`.
  - `BIOSEXEC pc=%08x` — emitted once per distinct BIOS-range PC executed after the entrypoint is first seen (interpreter fetch path).

**Background (verified against `tools/flycast-src`):**
- Guest context: `#include "hw/sh4/sh4_if.h"` gives `Sh4cntx`; `Sh4cntx.pc` and `Sh4cntx.r[15]` are the guest PC and SP. The whole cart-DMA and Maple-DMA sequences run synchronously on the guest store that triggers them, so `Sh4cntx.pc` at the hook = the instruction just after the game's triggering store (a +2 offset — good enough to fall inside the function range).
- `Naomi_DmaStart` in `core/hw/naomi/naomi.cpp` already carries the `CARTDMA` cartlog line (Phase 2).
- `MIEImpl::handle_86_subcommand` in `core/hw/maple/maple_jvs.cpp` dispatches input (`subcode 0x15`), EEPROM read (`0x1`/`0x3`), and EEPROM write (`0x0B`) — one hook there covers targets 3 and 5.
- Interpreter loop: `Sh4Interpreter::Run` → `ReadNexOp()` in `core/hw/sh4/interpr/sh4_interpreter.cpp`; `-config config:Dynarec.Enabled=no` selects it.

- [ ] **Step 1: Add the CARTDMAPC line**

In `tools/flycast-src/core/hw/naomi/naomi.cpp`, the existing Phase 2 block reads:

```cpp
		cartlog("CARTDMA src=%08x dest=%08x len=%x\n",
				CurrentCartridge->GetDmaSrcOffset(), SB_GDSTAR & 0x1FFFFFE0, SB_GDLEN);
```

Add immediately after it:

```cpp
		cartlog("CARTDMAPC pc=%08x sp=%08x\n", Sh4cntx.pc, Sh4cntx.r[15]);   // Phase 3: guest PC/SP at DMA kick
```

Ensure the file includes the SH-4 context header near the other Phase 2 includes:

```cpp
#include "hw/sh4/sh4_if.h"   // Phase 3: Sh4cntx (guest pc/sp)
```

- [ ] **Step 2: Add the MAPLEPC line**

In `tools/flycast-src/core/hw/maple/maple_jvs.cpp`, at the top of `MIEImpl::handle_86_subcommand()` (the version with the full `switch(subcode)` — the one around the EEPROM/JVS cases, not `BaseMIE::handle_86_subcommand`), right after `u32 subcode = dma_buffer_in[0];`:

```cpp
	cartlog("MAPLEPC cmd=86 sub=%02x pc=%08x\n", subcode, Sh4cntx.pc);   // Phase 3: input(0x15)/EEPROM(0x01/03/0B) call site
```

Add the includes at the top of the file (near the existing `#include "hw/naomi/cartlog.h"` if present from Phase 2, else add both):

```cpp
#include "hw/naomi/cartlog.h"   // Phase 2/3 instrumentation
#include "hw/sh4/sh4_if.h"      // Phase 3: Sh4cntx
```

- [ ] **Step 3: Add the BIOSEXEC guard in the interpreter**

In `tools/flycast-src/core/hw/sh4/interpr/sh4_interpreter.cpp`, add the include and a file-scope helper, then a one-line check in the fetch path. At the top with the other includes:

```cpp
#include "hw/naomi/cartlog.h"   // Phase 3 instrumentation
```

Add above `u16 Sh4Interpreter::ReadNexOp()` (or above `Run`):

```cpp
// Phase 3: flag any guest execution inside BIOS ROM (phys < 0x200000) AFTER the
// Naomi entrypoint is first reached. Dynamic half of naomi-vs-dreamcast §8-3.
// ponytail: interpreter-only (this pass forces the interpreter); dynarec won't fire this.
static bool cartlog_entry_seen = false;
static void cartlog_bios_check(u32 pc)
{
	if (pc == 0x8c04ae2c)
		cartlog_entry_seen = true;
	if (cartlog_entry_seen && (pc & 0x1fffffff) < 0x00200000)
	{
		static u32 last = 0xffffffff;
		if (pc != last) { last = pc; cartlog("BIOSEXEC pc=%08x\n", pc); }
	}
}
```

In `ReadNexOp()`, immediately after `u32 addr = ctx->pc;`:

```cpp
	cartlog_bios_check(addr);
```

- [ ] **Step 4: Regenerate the in-repo patch**

The patch must stay reproducible from a clean clone. Regenerate it from the flycast working tree (same base commit as Phase 2):

```bash
cd tools/flycast-src
git diff > "$OLDPWD/patches/flycast-instrument.diff"
cd "$OLDPWD"
git diff --stat patches/flycast-instrument.diff
```
Expected: the diffstat shows `naomi.cpp`, `maple_jvs.cpp`, `sh4_interpreter.cpp`, plus the Phase 2 files still present (the CMake/cartlog/watermark changes are unchanged). Confirm the three new cartlog lines appear in the diff:
```bash
grep -E 'CARTDMAPC|MAPLEPC|BIOSEXEC' patches/flycast-instrument.diff
```
Expected: all three present.

- [ ] **Step 5: Rebuild the instrumented Flycast**

Per `docs/kb/tooling.md` (standalone CMake 3.31.6, `DEVELOPER_DIR` = full Xcode):

```bash
cd tools/flycast-src
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
CM=$(ls -d /opt/homebrew/Caskroom/cmake/*/CMake.app/Contents/bin/cmake 2>/dev/null || echo cmake)
"$CM" --build build -j"$(sysctl -n hw.ncpu)" 2>&1 | tail -15
```
Expected: build reaches `Flycast.app` with no compile error in the three edited files. If `Sh4cntx` is undefined, the `sh4_if.h` include is missing in that file — add it and rebuild (incremental). If the build dir is stale/missing, re-run the full configure line from `tooling.md` first.

- [ ] **Step 6: Add the `pc` capture pass**

In `scripts/capture.sh`, extend the pass validation and the launch. Change the usage guard line:

```sh
if [ -z "$PASS" ] || { [ "$PASS" != "attract" ] && [ "$PASS" != "play" ] && [ "$PASS" != "input" ] && [ "$PASS" != "pc" ]; }; then
    echo "usage: $0 <attract|play|input|pc> [seconds]" >&2
    exit 1
fi
```

Add an interpreter flag only for the `pc` pass. After the `FLYCAST_CARTLOG=... "$BIN" ...` launch is set up, replace the single launch line with:

```sh
# ponytail: Phase 3 pc-pass forces the interpreter core for instruction-exact guest PC/SP.
EXTRA=""
if [ "$PASS" = "pc" ]; then EXTRA="-config config:Dynarec.Enabled=no"; fi
FLYCAST_CARTLOG="$LOG" "$BIN" -config config:rend.vsync=no $EXTRA "$ROM" &
FLYPID=$!
```

And treat `pc` like the foreground passes (user closes the window): change the final `if [ "$PASS" = "attract" ]` branch to keep `attract` auto-kill, and let `play`/`input`/`pc` all fall to the foreground `else` branch (no code change needed there — the `else` already covers any non-`attract` pass).

- [ ] **Step 7: Smoke-test the new lines**

Run a short attract-style capture under the interpreter to confirm the lines emit (boot alone exercises the EEPROM read and the first cart DMAs):

```bash
scripts/capture.sh pc 60 &   # play/close, or let it run; then:
sleep 65; grep -cE 'CARTDMAPC|MAPLEPC' capture-pc.log
```
Expected: non-zero counts for both `CARTDMAPC` and `MAPLEPC`. Zero means a hook didn't fire — stop and debug (systematic-debugging) before the real capture. `BIOSEXEC` is expected **absent** (that's the good result).

- [ ] **Step 8: Commit**

```bash
git add patches/flycast-instrument.diff scripts/capture.sh
git commit -m "Phase 3 Task 4: PC/SP + BIOSEXEC logging, pc capture pass (interpreter)"
```

---

## Task 5: Extend the parser with the new lines + five cross-checks

**Files:**
- Modify: `scripts/parse_cart_log.py`
- Modify: `scripts/test_parse_cart_log.py`

**Interfaces:**
- Consumes: log files with the new `CARTDMAPC` / `MAPLEPC` / `BIOSEXEC` lines (Task 4); static function ranges from Tasks 2–3 (passed as `--cart-fn LO-HI`, `--input-fn LO-HI`, `--eeprom-fn LO-HI`).
- Produces: parsed `pc` data and five checks in the summary — `no_bios_exec`, `dma_pc_in_cart_fn`, `input_pc_in_input_fn`, `eeprom_seen`, `sp_consistent`.

- [ ] **Step 1: Write failing tests**

Add to `scripts/test_parse_cart_log.py`:

```python
def test_parses_pc_lines():
    text = (
        "CARTDMA src=00800000 dest=0c010000 len=20\n"
        "CARTDMAPC pc=8c050100 sp=0cff0000\n"
        "MAPLEPC cmd=86 sub=15 pc=8c060200\n"
        "MAPLEPC cmd=86 sub=03 pc=8c061000\n"
    )
    r = parse_text(text)
    assert r["cartdma_pc"] == [{"pc": 0x8c050100, "sp": 0x0cff0000}]
    assert {"sub": 0x15, "pc": 0x8c060200} in r["maple_pc"]
    assert {"sub": 0x03, "pc": 0x8c061000} in r["maple_pc"]


def test_pc_checks_pass_within_ranges():
    text = (
        "CARTDMAPC pc=8c050100 sp=0cff0000\n"
        "MAPLEPC cmd=86 sub=15 pc=8c060200\n"
        "MAPLEPC cmd=86 sub=03 pc=8c061000\n"
    )
    r = parse_text(text, cart_fn=(0x8c050000, 0x8c050fff),
                   input_fn=(0x8c060000, 0x8c060fff),
                   eeprom_fn=(0x8c061000, 0x8c061fff))
    d = dict((n, ok) for n, ok, _ in r["checks"])
    assert d["no_bios_exec"] is True
    assert d["dma_pc_in_cart_fn"] is True
    assert d["input_pc_in_input_fn"] is True
    assert d["eeprom_seen"] is True
    assert d["sp_consistent"] is True


def test_bios_exec_fails_check():
    r = parse_text("BIOSEXEC pc=00001234\n")
    d = dict((n, ok) for n, ok, _ in r["checks"])
    assert d["no_bios_exec"] is False
```

- [ ] **Step 2: Run — verify they fail**

Run: `python3 scripts/test_parse_cart_log.py`
Expected: FAIL (`parse_text` has no `cartdma_pc` key / no `cart_fn` kwarg).

- [ ] **Step 3: Implement the parsing**

In `scripts/parse_cart_log.py`, add the regexes near the others:

```python
_DMAPC = re.compile(r"^CARTDMAPC pc=([0-9a-fA-F]+) sp=([0-9a-fA-F]+)")
_MAPC = re.compile(r"^MAPLEPC cmd=86 sub=([0-9a-fA-F]+) pc=([0-9a-fA-F]+)")
_BIOS = re.compile(r"^BIOSEXEC pc=([0-9a-fA-F]+)")
```

Change `parse_text` signature and body:

```python
def parse_text(text, cart_fn=None, input_fn=None, eeprom_fn=None):
    dma_seen, dma = set(), []
    pio = set()
    watermarks = {}
    serial = []
    cartdma_pc, maple_pc, bios_exec = [], [], []
    for line in text.splitlines():
        m = _DMA.match(line)
        if m:
            src, dest, length = (int(g, 16) for g in m.groups())
            key = (src, dest, length)
            if key not in dma_seen:
                dma_seen.add(key)
                dma.append({"src": src, "dest": dest, "len": length})
            continue
        m = _DMAPC.match(line)
        if m:
            cartdma_pc.append({"pc": int(m.group(1), 16), "sp": int(m.group(2), 16)})
            continue
        m = _MAPC.match(line)
        if m:
            maple_pc.append({"sub": int(m.group(1), 16), "pc": int(m.group(2), 16)})
            continue
        m = _BIOS.match(line)
        if m:
            bios_exec.append(int(m.group(1), 16))
            continue
        m = _PIO.match(line)
        if m:
            pio.add(int(m.group(1), 16)); continue
        m = _WM.match(line)
        if m:
            region, used, _sz = m.group(1), int(m.group(2), 16), int(m.group(3), 16)
            watermarks[region] = max(watermarks.get(region, 0), used); continue
        m = _SER.match(line)
        if m:
            serial.append({"addr": int(m.group(1), 16), "data": int(m.group(2), 16)})
    dma.sort(key=lambda d: (d["src"], d["dest"]))
    return {
        "dma": dma, "pio": sorted(pio), "watermarks": watermarks, "serial": serial,
        "cartdma_pc": cartdma_pc, "maple_pc": maple_pc, "bios_exec": bios_exec,
        "checks": _checks(dma) + _pc_checks(cartdma_pc, maple_pc, bios_exec,
                                             cart_fn, input_fn, eeprom_fn),
    }
```

Add the new checks function:

```python
def _in(rng, pc):
    return rng is not None and rng[0] <= pc <= rng[1]


def _pc_checks(cartdma_pc, maple_pc, bios_exec, cart_fn, input_fn, eeprom_fn):
    checks = []
    checks.append(("no_bios_exec", len(bios_exec) == 0,
                   "zero BIOSEXEC lines (no BIOS call after entry, §8-3)"))
    if cart_fn and cartdma_pc:
        checks.append(("dma_pc_in_cart_fn", all(_in(cart_fn, d["pc"]) for d in cartdma_pc),
                       "every CARTDMAPC pc inside the static cart-read fn"))
    input_pcs = [m["pc"] for m in maple_pc if m["sub"] == 0x15]
    if input_fn and input_pcs:
        checks.append(("input_pc_in_input_fn", all(_in(input_fn, p) for p in input_pcs),
                       "every input-poll (sub 0x15) pc inside the static input fn"))
    eeprom_pcs = [m["pc"] for m in maple_pc if m["sub"] in (0x01, 0x03, 0x0b)]
    seen = len(eeprom_pcs) > 0
    if eeprom_fn:
        seen = seen and all(_in(eeprom_fn, p) for p in eeprom_pcs)
    checks.append(("eeprom_seen", seen,
                   "≥1 EEPROM op (sub 0x01/0x03/0x0B) captured"
                   + (" and inside the static eeprom fn" if eeprom_fn else "")))
    # sp_consistent: all logged SPs within 1 MB of each other (a stable stack region)
    sps = [d["sp"] for d in cartdma_pc]
    sp_ok = (max(sps) - min(sps) <= 0x100000) if sps else True
    checks.append(("sp_consistent", sp_ok,
                   "logged SPs cluster within 1 MB (a single stable stack region)"))
    return checks
```

- [ ] **Step 4: Run tests — verify pass**

Run: `python3 scripts/test_parse_cart_log.py`
Expected: all tests pass (existing Phase 2 tests + the three new ones).

- [ ] **Step 5: Extend the summary + CLI**

In `write_summary`, before the `CHECK` loop, add the pc counts:

```python
    if result.get("cartdma_pc"):
        pcs = sorted(set(d["pc"] for d in result["cartdma_pc"]))
        lines.append(f"cart-DMA call sites (unique pc): {len(pcs)}  "
                     f"e.g. 0x{pcs[0]:08x}")
        sps = sorted(set(d["sp"] for d in result["cartdma_pc"]))
        lines.append(f"stack pointer range: 0x{sps[0]:08x}..0x{sps[-1]:08x}")
    if result.get("maple_pc"):
        subs = sorted(set(m["sub"] for m in result["maple_pc"]))
        lines.append("MIE 0x86 subcommands seen: " + ", ".join(f"0x{s:02x}" for s in subs))
    lines.append(f"BIOSEXEC lines: {len(result.get('bios_exec', []))}")
```

In `main`, accept the range flags and pass them through:

```python
def _range(s):
    lo, hi = s.split("-")
    return (int(lo, 16), int(hi, 16))
```

Extend the arg loop to recognize `--cart-fn`, `--input-fn`, `--eeprom-fn` (each `LO-HI` hex), collect into a dict, and call `parse_files(paths, **ranges)`. Update `parse_files`:

```python
def parse_files(paths, **ranges):
    text = "\n".join(open(p, encoding="utf-8", errors="replace").read() for p in paths)
    return parse_text(text, **ranges)
```

- [ ] **Step 6: Run the smoke log through the CLI**

Run (uses the Task 4 smoke log; ranges are placeholders until Task 6 has real ones):
```bash
python3 scripts/parse_cart_log.py capture-pc.log
```
Expected: summary now includes `cart-DMA call sites`, `MIE 0x86 subcommands seen`, `BIOSEXEC lines: 0`, and the `CHECK no_bios_exec: PASS` line.

- [ ] **Step 7: Commit**

```bash
git add scripts/parse_cart_log.py scripts/test_parse_cart_log.py
git commit -m "Phase 3 Task 5: parser PC/SP/BIOS lines + five cross-checks"
```

---

## Task 6: Capture pass + close the loop (static ranges ↔ dynamic PCs)

**Files:**
- Produces: `capture-pc.log` (gitignored) + the resolved function ranges recorded for Task 7
- Uses: instrumented Flycast (Task 4), parser (Task 5), Ghidra outputs (Tasks 2–3)

**Interfaces:**
- Consumes: `--cart-fn/--input-fn/--eeprom-fn` ranges derived from the Ghidra candidate functions.
- Produces: a passing cross-check run tying each static function to the dynamic PCs, and the confirmed address ranges for the writeup.

- [ ] **Step 1: Derive function ranges from the Ghidra candidates**

From Task 2's `XREF` list, pick the function referencing the G1 DMA block (cart-read), and the function(s) referencing the Maple block (input / EEPROM). Get each function's full extent with a tiny query — add nothing new; reuse `DumpEntryChain`'s pattern inline by running an ad-hoc listing, or read the entry+max address from the Ghidra GUI/`FindMmioXrefs` `fn=NAME@addr`. Record three ranges as `LO-HI` hex. (If input and EEPROM live in the **same** function — plausible, both are MIE `0x86` — use the same range for both; the checks still hold.)

- [ ] **Step 2: Run the real capture**

```bash
scripts/capture.sh pc
```
Play: let attract run once (streams cart + does the boot EEPROM read), then insert coin and play one game into the early stages, then close the window. This exercises cart DMA (target 2), input polling (target 3), and the EEPROM read already happened at boot (target 5).

- [ ] **Step 3: Run the cross-checks with the real ranges**

```bash
python3 scripts/parse_cart_log.py capture-pc.log \
  --cart-fn <LO-HI> --input-fn <LO-HI> --eeprom-fn <LO-HI>
```
Expected: `CHECK no_bios_exec: PASS`, `CHECK dma_pc_in_cart_fn: PASS`, `CHECK input_pc_in_input_fn: PASS`, `CHECK eeprom_seen: PASS`, `CHECK sp_consistent: PASS`.

- [ ] **Step 4: Resolve any disagreement (do not paper over)**

If any check FAILs, the static and dynamic sides disagree — one is wrong and Phase 4 depends on knowing which. Stop and use **superpowers:systematic-debugging**. Common causes and the right move:
- `dma_pc_in_cart_fn` FAIL → the logged pc is a caller of the DMA helper, not the helper (the +2 store is in a wrapper). Widen the range to the enclosing function or record both call site and helper. This is a finding, not a workaround.
- `eeprom_seen` FAIL (zero EEPROM ops) → the boot EEPROM read wasn't captured; re-run ensuring attract runs from a cold boot.
- `no_bios_exec` FAIL → a real BIOS call exists; capture the `BIOSEXEC pc=` and disassemble that target — §8-3's answer flips to "calls BIOS," a material Phase 4 input.
Record whatever you find; the honest verdict is the deliverable.

- [ ] **Step 5: Confirm the SP verdict**

Compare the dynamic `stack pointer range` line against the static `DumpEntryChain` r15 finding (Task 3). Note the actual SP value(s):
- SP within `0x0c000000`–`0x0cffffff` (< 16 MB above base) → **main RAM safe as-is**; the Phase 2 high scan was noise/stale data.
- SP near `0x0e000000`/32 MB → **Phase 4 must relocate SP** below 16 MB (a one-constant patch); flag it for the Phase 4 plan.
Record the value and the verdict for Task 7.

- [ ] **Step 6: Commit the confirmed ranges as a note**

The log is gitignored; capture the resolved ranges in a short commit message / scratch note so Task 7 can cite them. No code change here — this task's output is the verified ranges + verdicts, written up in Task 7. (If Step 4 required a parser tweak, commit that with a `Phase 3 Task 6:` message.)

---

## Task 7: Write `docs/kb/boot-binary.md` + resolve the open questions

**Files:**
- Create: `docs/kb/boot-binary.md`
- Modify: `docs/kb/naomi-vs-dreamcast.md`, `docs/kb/phase2-measurements.md`, `docs/kb/00-status.md`, `docs/kb/tooling.md`

**Interfaces:**
- Consumes: all prior task outputs (Ghidra reports, capture cross-checks, SP verdict).
- Produces: the Phase 3 deliverable KB and the resolved-question edits.

- [ ] **Step 1: Write `boot-binary.md`**

Create `docs/kb/boot-binary.md` with these sections, each claim cited by address / script output / `file:line`:

1. **Method** — Ghidra harness (`scripts/ghidra/run.sh`, base `0x8c020000`) + interpreter-mode PC/SP capture; how static and dynamic corroborate. Note the +2 store offset and the interpreter-only BIOSEXEC guard.
2. **Entry chain** — `0x8c04ae2c` trampoline → real init (address), from `DumpEntryChain`.
3. **Stack-pointer verdict (closes the Phase 2 main-RAM question)** — the static r15 finding + the dynamic `sp=` range + the verdict (safe / relocate). Cite both.
4. **Cart-read function (target 2)** — address range, role (fills `DMA_OFFSET`/`DMA_COUNT`, kicks `SB_GDST`), evidence: `FindMmioXrefs` g1dma/cart xrefs + `dma_pc_in_cart_fn` PASS. Phase 4 implication: this is the function whose body/call sites reissue GD-ROM reads.
5. **Input-decode function (target 3)** — range, role (MIE `0x86`/`0x15`), evidence: maple xref + `input_pc_in_input_fn` PASS + the `input-map.md` bits. Phase 4 implication: shim to DC `GetCondition`.
6. **EEPROM/settings-parse function (target 5)** — range, role (MIE `0x01`/`0x03`/`0x0B`), evidence: maple xref + `eeprom_seen` PASS. Phase 4 implication: force free-play defaults.
7. **BIOS-call verdict (target 1, §8-3)** — `ScanBiosTargets` result + `no_bios_exec` PASS → "no BIOS dependency observed statically or dynamically," with the honest caveat (static misses computed targets; dynamic covers executed paths only). Or, if a call was found, the target and its implication.
8. **Reproduction** — the exact `run.sh` + `capture.sh pc` + `parse_cart_log.py --*-fn` commands and the ranges, so exit criterion #4 is checkable.

- [ ] **Step 2: Mark §8-3 resolved in `naomi-vs-dreamcast.md`**

In the §8 open-questions list, append to item 3 (the "does the boot binary call BIOS" question) a `**RESOLVED Phase 3**` note mirroring the Phase 2 resolution style, citing `boot-binary.md` and the two checks. Update the §6 caveat sentence likewise.

- [ ] **Step 3: Close the stack question in `phase2-measurements.md`**

In the Main RAM row and the "Verdict for Phase 5" paragraph, replace the "Phase 3 question" wording with the resolved verdict from `boot-binary.md` §3 (safe, or relocate-SP-in-Phase-4), citing it.

- [ ] **Step 4: Advance `00-status.md`**

- Bump `**Updated:**` and the header note to "Phase 3 complete."
- In Phases, mark Phase 3 **DONE** with the date and a one-line result; set Phase 4 **NEXT**.
- Under "Key facts," add the five resolved boot-binary addresses/verdicts (cart-read fn, input fn, eeprom fn, SP verdict, no-BIOS-call) with a pointer to `boot-binary.md`.
- Rewrite "Next step" to point at Phase 4 (brainstorm/spec the loader + shims + patches), noting the concrete patch targets Phase 3 produced.

- [ ] **Step 5: Update `tooling.md` if anything new appeared**

If Task 1's harness or the interpreter-mode capture introduced a step not already in `tooling.md` (e.g. the `run.sh` invocation, the `Dynarec.Enabled=no` flag), add it under the Ghidra / Flycast sections. If nothing new, skip (YAGNI).

- [ ] **Step 6: Final verification — re-run everything from the recorded commands**

Prove exit criteria #2 and #4:
```bash
scripts/ghidra/run.sh script FindMmioXrefs.java 2>&1 | grep -c XREF
scripts/ghidra/run.sh script ScanBiosTargets.java 2>&1 | grep RESULT
python3 scripts/test_parse_cart_log.py && echo "parser tests OK"
python3 scripts/parse_cart_log.py capture-pc.log --cart-fn <LO-HI> --input-fn <LO-HI> --eeprom-fn <LO-HI> | grep CHECK
```
Expected: xref count > 0, `RESULT: NONE` (or the recorded finding), parser tests OK, all five `CHECK … PASS`. Any mismatch with what `boot-binary.md` claims → fix the doc to match the evidence (evidence wins).

- [ ] **Step 7: Commit**

```bash
git add docs/kb/boot-binary.md docs/kb/naomi-vs-dreamcast.md docs/kb/phase2-measurements.md docs/kb/00-status.md docs/kb/tooling.md
git commit -m "Phase 3 Task 7: boot-binary map, resolve §8-3 + stack question, advance to Phase 4"
```

---

## Self-Review

**Spec coverage:**
- §8-3 BIOS-call verdict → Tasks 3 (static `ScanBiosTargets`) + 4/6 (dynamic `BIOSEXEC`/`no_bios_exec`) + 7 (writeup). ✓
- Cart-read function → Tasks 2 (xref) + 6 (`dma_pc_in_cart_fn`) + 7. ✓
- Input-decode function → Tasks 2/4 (`MAPLEPC` sub 0x15) + 6 + 7. ✓
- SP/stack verdict → Tasks 3 (`DumpEntryChain`) + 4/6 (`sp=` log, `sp_consistent`) + 7. ✓
- EEPROM/settings-parse → Tasks 2/4 (`MAPLEPC` sub 0x01/0x03/0x0B) + 6 (`eeprom_seen`) + 7. ✓
- Instrumentation extension (CARTDMAPC/MAPLEPC/BIOSEXEC, interpreter) → Task 4. ✓
- Static harness (Ghidra Java, re-runnable, ROM gitignored) → Task 1. ✓
- Cross-checks (five asserts, disagreement = stop-and-debug) → Task 5 + Task 6 Step 4. ✓
- Deliverables (`boot-binary.md`, doc edits, committed scripts, extended patch/parser) → Task 7 + threaded through. ✓
- Exit criteria #1–5 → Task 7 Steps 1/4/6 + Task 6 checks. ✓

**Placeholder scan:** The `<LO-HI>` ranges in Tasks 6–7 are intentionally derived at runtime from Ghidra output (they can't be known before the analysis runs); every step that uses them says exactly how to obtain them. No other TBD/TODO.

**Type consistency:** `parse_text(text, cart_fn=, input_fn=, eeprom_fn=)` and the result keys `cartdma_pc`/`maple_pc`/`bios_exec` are consistent across Tasks 5 and 6. Log line formats (`CARTDMAPC pc= sp=`, `MAPLEPC cmd=86 sub= pc=`, `BIOSEXEC pc=`) match between the Flycast patch (Task 4) and the parser regexes (Task 5). Check names (`no_bios_exec`, `dma_pc_in_cart_fn`, `input_pc_in_input_fn`, `eeprom_seen`, `sp_consistent`) match between Tasks 5, 6, and 7.
