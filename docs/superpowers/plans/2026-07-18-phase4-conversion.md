# Phase 4 — Conversion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a GDI that boots in Flycast's Dreamcast profile and runs *Cleopatra Fortune Plus* playable to game-over: KOS stage-1 loader, freestanding shims at `0x8cfc0000`, patch table, GDI mastering.

**Architecture:** Per the approved spec (`docs/superpowers/specs/2026-07-18-phase4-conversion-design.md`): the full cart image rides track 3 at a fixed LBA; a KOS loader reads the 1 MB boot image to a staging buffer, applies a build-time patch table, drops the shim blob at `0x8cfc0000`, and jumps to `0x8c04ae2c`. The cart-DMA intercept is a **register mirror**: the pool words feeding the game's G1/cart register base are repointed to a shim-owned block, and the completion-wait site calls the shim, which serves the read from GD-ROM via BIOS syscalls. Input/EEPROM are a Maple-routine pointer swap with fake MIE replies.

**Tech Stack:** KallistiOS (sh-elf-gcc toolchain) for loader + freestanding shim; Python 3 stdlib for mastering/patch-gen; Ghidra 12.1.2 headless Java scripts (existing `scripts/ghidra/run.sh` harness); instrumented Flycast (existing `patches/flycast-instrument.diff` recipe) for V2/V4 captures; host `cc` for pure-function unit tests.

## Global Constraints

- **ROM content never committed, never uploaded.** Gitignored: `tools/`, `build/` (tracks, `cleo.gdi`, `patch_table.h` — it embeds original ROM bytes), `shims/data/` (baked EEPROM, MIE templates). Committed patch *definitions* contain only addresses and our replacement bytes.
- **Every hardware/address claim in the KB carries a citation** (script output, `file:line` into `tools/mame/…`, `tools/netboot/…`, or `tools/kos/…`). Emulator/KOS source outranks wikis.
- **Ghidra scripts are Java** (`scripts/ghidra/`, run via `scripts/ghidra/run.sh script NAME.java [args]`), re-runnable headlessly.
- **Fixed address plan** (from the spec; shared header `shims/include/shim_iface.h` is the single source of truth):
  - `SHIM_BASE 0x8cfc0000` (256 KB to end of RAM), `STAGING_ADDR 0x8cd00000`
  - game image: cart `0x0` → `0x8c020000`, len `0x100000`, entry `0x8c04ae2c`
  - disc: track 3 at LBA 45000; ISO region fixed at `ISO_SECTORS = 2048` (4 MB); `CART_LBA = 47048`; `CART_FAD = CART_LBA + 150 = 47198`; `CART_SIZE = 0x6d00000`
- **Shim code is freestanding** (`-ffreestanding -nostdlib`): no KOS calls, no libc; MMIO and shim-home state accessed through P2 (`| 0xa0000000`) pointers.
- **Flycast DC-profile test command** (release app, absolute path):
  `/Applications/Flycast.app/Contents/MacOS/Flycast "$PWD/build/cleo.gdi"`
  Serial output: enable Flycast's serial console (`Debug.SerialConsoleEnabled=yes` in `emu.cfg`; exact key verified against Flycast source in Task 2 and recorded in `tooling.md`).
- **Numbers taken from analysis tasks** (V1–V5 etc.) are marked `/* from KB phase4-conversion.md §… */` in code; the producing task records them in `docs/kb/phase4-conversion.md` before the consuming task builds.

---

## File Structure

- `scripts/make_gdi.py` (create) — masters `build/cleo.gdi` (+3 track files) from loader binary, IP.BIN, and the gitignored `.dat`.
- `scripts/build_patch_table.py` (create) — patch definitions (data) + generator: reads `tools/boot.bin` + `shims/build/shim.map`, emits `build/patch_table.h` with `(addr, old, new)` triples; assembles entry-hook thunks and pool-word edits.
- `scripts/check_triples.py` (create) — regression oracle: every `cart-streaming-map.csv` triple servable from the mastered layout.
- `scripts/ghidra/DisasmRange.java` (create), `scripts/ghidra/ListPoolWords.java` (create) — range disassembler and pool-word scanner (used by V1, V5, store classification).
- `scripts/ghidra/run.sh` (modify) — forward extra args to post-scripts.
- `patches/flycast-instrument.diff` (modify) — `SHIMWATCH` write-watch + `MIERESP` response dumps.
- `scripts/parse_cart_log.py`, `scripts/test_parse_cart_log.py` (modify) — new line types, `shim_home_clean` check, MIE template dumper.
- `shims/` (create) — `Makefile`, `shim.ld`, `include/shim_iface.h`, `src/{main,gd,cart,maple,jvs,scif,util}.c`, `test/test_host.c`, `data/` (gitignored).
- `loader/` (create) — `Makefile`, `main.c`, `handoff.S`, `ip.txt`.
- `docs/kb/phase4-conversion.md` (create) — V1–V5 verdicts, store classification table, input ABI, build/run pipeline.
- `docs/kb/tooling.md`, `docs/kb/00-status.md`, `.gitignore` (modify).

---

## Task 1: KOS toolchain

**Files:**
- Modify: `docs/kb/tooling.md`, `.gitignore`

**Interfaces:**
- Produces: `sh-elf-gcc` at `/opt/toolchains/dc/sh-elf/bin/`, KOS environment via `source tools/kos/environ.sh`, `kos-cc` for the loader. Tasks 2, 9–13 need this.

- [ ] **Step 1: Install prerequisites and clone KOS**

```bash
brew install gmp mpfr libmpc gettext texinfo wget
git clone --recursive https://github.com/KallistiOS/KallistiOS.git tools/kos
sudo mkdir -p /opt/toolchains/dc && sudo chown "$(whoami)" /opt/toolchains/dc
```

- [ ] **Step 2: Build the dc-chain toolchain** (long — resumable; re-run on timeout)

```bash
cd tools/kos/utils/dc-chain
cp config/config.mk.stable.sample config.mk
make
```
Expected: `sh-elf-gcc` and `arm-eabi-gcc` under `/opt/toolchains/dc/`. Verify: `/opt/toolchains/dc/sh-elf/bin/sh-elf-gcc --version` prints a GCC version.

- [ ] **Step 3: Build KOS + hello example**

```bash
cd tools/kos
cp doc/environ.sh.sample environ.sh
# edit environ.sh: KOS_BASE="$(pwd)" (absolute repo path to tools/kos); leave defaults otherwise
source environ.sh && make
cd examples/dreamcast/hello && make
```
Expected: `hello.elf` exists.

- [ ] **Step 4: Record in tooling.md, gitignore build dirs, commit**

Add a `### KallistiOS` section to `docs/kb/tooling.md` (clone commit hash from `git -C tools/kos rev-parse HEAD`, the exact commands above, and any macOS deviation actually hit). Append to `.gitignore`:

```
build/
shims/data/
```

```bash
git add docs/kb/tooling.md .gitignore
git commit -m "Phase 4 Task 1: KOS toolchain installed and recorded"
```

---

## Task 2: GDI mastering + hello loader — **Milestone M1**

**Files:**
- Create: `scripts/make_gdi.py`, `loader/Makefile`, `loader/main.c`, `loader/ip.txt`
- Modify: `docs/kb/tooling.md` (makeip, cdrtools, Flycast serial-console key)

**Interfaces:**
- Consumes: KOS (Task 1).
- Produces: `scripts/make_gdi.py --rom "Cleopatra Fortune Plus.dat" --loader build/1ST_READ.BIN --out build/` → `build/cleo.gdi`; layout constants `ISO_SECTORS=2048`, `CART_LBA=47048`, `CART_FAD=47198`. `loader/main.c` grows in Task 13 — this task's version proves boot + disc math.

- [ ] **Step 1: Install makeip and cdrtools, record in tooling.md**

```bash
brew install cdrtools          # provides mkisofs
git clone https://github.com/sizious/makeip tools/makeip
cd tools/makeip/src && make    # produces tools/makeip/src/makeip
```

- [ ] **Step 2: Write `loader/ip.txt` and the hello loader**

`loader/ip.txt`:
```
Hardware ID   : SEGA SEGAKATANA
Maker ID      : SEGA ENTERPRISES
Device Info   : 0000 CD-ROM1/1
Area Symbols  : J
Peripherals   : E000F10
Product No    : T00000N
Version       : V1.000
Release Date  : 20260718
Boot Filename : 1ST_READ.BIN
SW Maker Name : CLEO PORT PROJECT
Game Title    : CLEOPATRA FORTUNE PLUS
```

`loader/main.c` (v1 — M1 smoke: boot, read cart sector 0, verify NAOMI header magic):
```c
#include <kos.h>

#define CART_FAD 47198          /* track3 LBA 45000 + 150 FAD bias + ISO_SECTORS 2048 */

static uint8 sec[2048] __attribute__((aligned(32)));

int main(void) {
    dbglog(DBG_INFO, "CLEO LOADER M1\n");
    cdrom_reinit();
    int r = cdrom_read_sectors(sec, CART_FAD, 1);
    dbglog(DBG_INFO, "read fad=%d -> %d\n", CART_FAD, r);
    if (r == ERR_OK && !memcmp(sec, "NAOMI", 5))
        dbglog(DBG_INFO, "M1 OK: NAOMI header found at cart+0\n");
    else
        dbglog(DBG_INFO, "M1 FAIL: r=%d first bytes %02x %02x %02x %02x %02x\n",
               r, sec[0], sec[1], sec[2], sec[3], sec[4]);
    for(;;) thd_sleep(1000);
    return 0;
}
```

`loader/Makefile`:
```makefile
# Requires: source tools/kos/environ.sh
TARGET = loader.elf
OBJS = main.o
all: ../build/1ST_READ.BIN
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)
../build/1ST_READ.BIN: $(TARGET)
	mkdir -p ../build
	sh-elf-objcopy -R .stack -O binary $(TARGET) $@
clean:
	rm -f $(TARGET) $(OBJS) ../build/1ST_READ.BIN
```

- [ ] **Step 3: Write `scripts/make_gdi.py`**

```python
#!/usr/bin/env python3
"""Master the Phase 4 GDI: 3 tracks + .gdi. Layout (docs/superpowers/specs/
2026-07-18-phase4-conversion-design.md §1): track3 = [ISO region: IP.BIN +
1ST_READ.BIN, fixed ISO_SECTORS] + [cart image at CART_LBA] . Output is
ROM-derived -> build/ is gitignored."""
import argparse, pathlib, subprocess, sys

SECTOR = 2048
TRACK3_LBA = 45000
ISO_SECTORS = 2048              # 4 MB reserved for IP.BIN + loader FS
CART_LBA = TRACK3_LBA + ISO_SECTORS      # 47048; FAD = 47198 (shim_iface.h)
CART_SIZE = 0x6D00000           # docs/kb/game.md — assert, don't trust

def run(cmd): subprocess.run(cmd, check=True)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default="Cleopatra Fortune Plus.dat")
    ap.add_argument("--loader", default="build/1ST_READ.BIN")
    ap.add_argument("--ip", default="build/IP.BIN")
    ap.add_argument("--out", default="build")
    a = ap.parse_args()
    out = pathlib.Path(a.out); out.mkdir(exist_ok=True)
    rom = pathlib.Path(a.rom).read_bytes()
    assert len(rom) == CART_SIZE, f"cart size {len(rom):#x} != {CART_SIZE:#x}"

    # ISO region: mkisofs with LBA offset so in-FS extents match disc LBAs
    fsdir = out / "fs"; fsdir.mkdir(exist_ok=True)
    (fsdir / "1ST_READ.BIN").write_bytes(pathlib.Path(a.loader).read_bytes())
    iso = out / "iso_part.bin"
    run(["mkisofs", "-C", f"0,{TRACK3_LBA}", "-V", "CLEOPATRA", "-G", a.ip,
         "-l", "-o", str(iso), str(fsdir)])
    iso_b = iso.read_bytes()
    assert len(iso_b) <= ISO_SECTORS * SECTOR, "ISO region overflow: raise ISO_SECTORS (and CART_LBA/CART_FAD in shim_iface.h)"

    t3 = out / "track03.bin"
    with open(t3, "wb") as f:
        f.write(iso_b); f.write(b"\0" * (ISO_SECTORS * SECTOR - len(iso_b)))
        f.write(rom)
        pad = (-len(rom)) % SECTOR
        f.write(b"\0" * pad)
    (out / "track01.bin").write_bytes(b"\0" * (300 * SECTOR))
    (out / "track02.raw").write_bytes(b"\0" * (300 * 2352))
    (out / "cleo.gdi").write_text(
        "3\n"
        "1 0 4 2048 track01.bin 0\n"
        "2 450 0 2352 track02.raw 0\n"
        f"3 {TRACK3_LBA} 4 2048 track03.bin 0\n")
    print(f"OK cleo.gdi  cart at LBA {CART_LBA} (FAD {CART_LBA+150}), "
          f"track03 {t3.stat().st_size//SECTOR} sectors")

if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Build IP.BIN + loader + GDI**

```bash
source tools/kos/environ.sh
make -C loader
tools/makeip/src/makeip loader/ip.txt build/IP.BIN
python3 scripts/make_gdi.py
```
Expected: `OK cleo.gdi  cart at LBA 47048 (FAD 47198), …`.

- [ ] **Step 5: M1 boot test in Flycast DC profile**

Enable the serial console: check the exact config key in Flycast source (`grep -ri serialconsole tools/flycast-src/core/cfg/` — expected `Debug.SerialConsoleEnabled`), set it in `~/Library/Application Support/Flycast/emu.cfg`, record the key in `tooling.md`. Then:

```bash
/Applications/Flycast.app/Contents/MacOS/Flycast "$PWD/build/cleo.gdi"
```
Expected in the serial/console log: `CLEO LOADER M1` then `M1 OK: NAOMI header found at cart+0`. If `M1 FAIL` with wrong bytes, the FAD bias is off — dump the read bytes, locate the `NAOMI` magic by trial FAD ±150/±16, fix `CART_FAD` in **both** `loader/main.c` and (Task 9) `shim_iface.h`, and record the verified value in `docs/kb/phase4-conversion.md`.

- [ ] **Step 6: Commit**

```bash
git add scripts/make_gdi.py loader/ docs/kb/tooling.md
git commit -m "Phase 4 Task 2 (M1): GDI mastering + hello loader boots in Flycast DC profile"
```

---

## Task 3: V1 — init memset range (syscalls vs raw ATA gate)

**Files:**
- Create: `scripts/ghidra/DisasmRange.java`
- Modify: `scripts/ghidra/run.sh`, create `docs/kb/phase4-conversion.md`

**Interfaces:**
- Produces: `run.sh script DisasmRange.java <start> <end>` (used again by Tasks 5, 6); KB §V1 verdict: does init's RAM-zeroing cover `0x8c000000–0x8c007fff`?

- [ ] **Step 1: Let run.sh forward script args**

In `scripts/ghidra/run.sh`, change the `script)` case:
```bash
  script)
    [ -n "${2:-}" ] || { echo "usage: $0 script NAME.java [args...]" >&2; exit 1; }
    SCRIPT="$2"; shift 2
    "$HL" "$PROJ" "$NAME" -process boot.bin -noanalysis \
      -scriptPath "$REPO/scripts/ghidra" -postScript "$SCRIPT" "$@"
    ;;
```

- [ ] **Step 2: Write `scripts/ghidra/DisasmRange.java`**

```java
// Disassembly listing for an address range. Usage:
//   scripts/ghidra/run.sh script DisasmRange.java 0x8c021000 0x8c021200
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;

public class DisasmRange extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Address start = toAddr(Long.decode(a[0]));
        Address end = toAddr(Long.decode(a[1]));
        Instruction ins = getInstructionAt(start);
        if (ins == null) ins = getInstructionAfter(start);
        while (ins != null && ins.getAddress().compareTo(end) <= 0) {
            println(String.format("%s  %s", ins.getAddress(), ins));
            ins = ins.getInstructionAfter();
        }
    }
}
```

- [ ] **Step 3: Read the init function's zeroing loop**

```bash
scripts/ghidra/run.sh script DisasmRange.java 0x8c021000 0x8c021200 2>&1 | grep -E '^8c02'
```
Find the memset/zero loop (`boot-binary.md` §2: init "zeroes RAM"): identify the start/end bound registers, chase their pool constants (visible in the listing as `mov.l @(disp,PC)` — dump the pool words with more `DisasmRange` calls if needed). Record in `docs/kb/phase4-conversion.md`:

```markdown
# Phase 4 — conversion build notes & analysis results

## V1 — init RAM-zero range
- Zero loop at 0x8c021xxx: clears 0x________ .. 0x________ (pool words at 0x________).
- Verdict: syscall area 0x8c000000–0x8c007fff [SURVIVES | IS WIPED].
- Decision: shim disc access = [BIOS GD syscalls | raw ATA driver].
```

**Gate:** if the syscall area is wiped, STOP — revise the plan (add a raw-ATA-driver task replacing Task 10's `gd.c` internals; spec §2 names this fallback) before proceeding. Also verify the zero range does not touch `0x8cfc0000+` (shim home) — if it does, the same stop-and-revise applies (shim relocation or a memset-bound patch).

- [ ] **Step 4: Commit**

```bash
git add scripts/ghidra/DisasmRange.java scripts/ghidra/run.sh docs/kb/phase4-conversion.md
git commit -m "Phase 4 Task 3 (V1): init memset range verdict"
```

---

## Task 4: V2 + V4 dynamic captures (shim-home write-watch, MIE response dumps)

**Files:**
- Modify: `patches/flycast-instrument.diff`, `scripts/parse_cart_log.py`, `scripts/test_parse_cart_log.py`
- Modify: `docs/kb/phase4-conversion.md`

**Interfaces:**
- Consumes: instrumented Flycast build recipe (`tooling.md`), `scripts/capture.sh`.
- Produces: KB §V2 (shim home clean verdict); `build/mie_sub15.bin`, `build/mie_sub01.bin`, `build/mie_sub03.bin` (response templates, gitignored) + KB §V4 (response buffer address). Tasks 5, 11 consume these.

- [ ] **Step 1: Extend the instrumentation**

In the instrumented Flycast tree (rebuild per `tooling.md` recipe), extend the same write-trace path that emits `WATERMARK`/`CARTDMA` (helper `cartlog_printf`, see existing diff):

```cpp
// SHIMWATCH: any game write into the planned shim home (phys 0x0cfc0000+)
if (paddr >= 0x0cfc0000 && paddr <= 0x0cffffff)
    cartlog_printf("SHIMWATCH addr=%08x\n", paddr);
```

At the point where the existing `MAPLEPC` hook parses the MIE frame (it already extracts cmd/sub and knows the receive address from the transfer descriptor's second word): after the emulated MIE has produced the response, dump it:

```cpp
// MIERESP: raw MIE response for template harvesting (cmd 0x86 transactions)
cartlog_printf("MIERESP sub=%02x addr=%08x data=", sub, recv_addr);
for (int i = 0; i < 0x40; i++)
    cartlog_printf("%02x", ReadMem8(recv_addr + i));
cartlog_printf("\n");
```

Regenerate `patches/flycast-instrument.diff` from the working tree (same procedure as Phases 2/3), rebuild.

- [ ] **Step 2: Extend the parser + tests**

`scripts/parse_cart_log.py`: parse both line types; add check `shim_home_clean` (PASS iff zero `SHIMWATCH` lines); add `--dump-mie <dir>` writing `mie_subXX.bin` (first occurrence per sub, decoded hex payload) and printing each sub's `addr=`.

`scripts/test_parse_cart_log.py`: add cases — a log with one `SHIMWATCH` fails `shim_home_clean`; a `MIERESP sub=15 addr=0c012345 data=8f16…` line round-trips to bytes and reports the address. Run:

```bash
python3 scripts/test_parse_cart_log.py
```
Expected: all tests pass (12 existing + new).

- [ ] **Step 3: Capture (Naomi mode, interpreter not required — no PC needed)**

```bash
scripts/capture.sh play 600     # play a couple of stages, per Phase 2/3 procedure
python3 scripts/parse_cart_log.py capture-play.log --dump-mie build/
```
Expected: `CHECK shim_home_clean: PASS`; `mie_sub15.bin`, `mie_sub01.bin`, `mie_sub03.bin` written; a single consistent `addr=` per sub. Record in KB §V2 (clean verdict — if FAIL, list offending addresses and STOP: shim home must move; revise `shim_iface.h` choice before Task 9) and §V4 (response buffer address, template file names, first-bytes summary).

- [ ] **Step 4: Commit**

```bash
git add patches/flycast-instrument.diff scripts/parse_cart_log.py scripts/test_parse_cart_log.py docs/kb/phase4-conversion.md
git commit -m "Phase 4 Task 4 (V2+V4): shim-home write-watch clean; MIE response templates captured"
```

---

## Task 5: Input-path ABI (static) — the input-shim contract

**Files:**
- Modify: `docs/kb/phase4-conversion.md`

**Interfaces:**
- Consumes: `DisasmRange.java` (Task 3), V4 response-buffer address (Task 4).
- Produces: KB §input-ABI with **exact values** Task 11 compiles in: `MIE_REQ_SUB_ADDR` (where the routine takes the subcommand from — argument register or memory), `MIE_RESP_BUF` (must equal V4's), `BTN_OFF` (offset of the JVS button word inside the `0x87`/`0x16` reply), `EE_OFF` (offset of EEPROM payload in the `0x01`/`0x03` reply), and the completion signal the caller checks after the routine returns.

- [ ] **Step 1: Disassemble the dispatcher and routine**

```bash
scripts/ghidra/run.sh script DisasmRange.java 0x8c027584 0x8c027600 2>&1 | grep -E '^8c02'
scripts/ghidra/run.sh script DisasmRange.java 0x8c0315ce 0x8c031620 2>&1 | grep -E '^8c03'
```
Read: (a) what the dispatcher (`FUN_8c027584`, pointer table `0x8c0275da`/`0x8c0275e0` — `boot-binary.md` §5) passes in r4–r7; (b) where `0x8c0315ce` reads the MIE subcommand it puts in the frame (store-queue writes); (c) which address it programs as Maple receive buffer — must match V4's `addr=`; (d) what the caller polls after `rts` (memory flag / ISTNRM bit — follow the code after the `jsr @r3` dispatch site).

- [ ] **Step 2: Locate the button word and EEPROM payload in the templates**

Diff `build/mie_sub15.bin` against the idle JVS word (`input-map.md`: idle `0x0000`, active-high) and the known reply shape (`0x87`/`0x16` header, 0xE words — `tools/netboot/docs/naomi.md:190-196`): identify `BTN_OFF`. For `mie_sub01/03.bin`, find the 128-byte EEPROM block (`EE_OFF`) — it must contain the same system-settings bytes twice (CRC-protected duplicate copies, `naomi-vs-dreamcast.md` §5).

- [ ] **Step 3: Record the contract in KB §input-ABI and commit**

All five values with the disassembly/citation for each.

```bash
git add docs/kb/phase4-conversion.md
git commit -m "Phase 4 Task 5: input-shim ABI pinned (req source, resp buffer, offsets, completion)"
```

---

## Task 6: Store classification + V3 completion-wait verdict — the patch-site table

**Files:**
- Create: `scripts/ghidra/ListPoolWords.java`
- Modify: `docs/kb/phase4-conversion.md`

**Interfaces:**
- Produces: KB §patch-sites — the complete list Task 12 turns into patch definitions: (a) every pool word holding a cart/G1 register address (value + address + referencing function), classified `descriptor-base` (value `0x…5f7000`) vs `absolute` (nonzero offset); (b) KB §V3 — the completion-wait mechanism and the chosen intercept (expected: entry-hook on `FUN_8c03bc12`).

- [ ] **Step 1: Write `scripts/ghidra/ListPoolWords.java`**

```java
// Scan all 4-aligned 32-bit words whose value (masked to 29-bit phys) falls in
// [lo, hi); print each with its referencing instructions and their functions.
// Usage: run.sh script ListPoolWords.java 0x005f7000 0x005f7800
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;

public class ListPoolWords extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        long lo = Long.decode(a[0]), hi = Long.decode(a[1]);
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            Address addr = b.getStart();
            while (addr.compareTo(b.getEnd()) < 0) {
                if ((addr.getOffset() & 3) == 0) {
                    long v = 0xFFFFFFFFL & currentProgram.getMemory().getInt(addr);
                    long phys = v & 0x1FFFFFFFL;
                    if (phys >= lo && phys < hi) {
                        StringBuilder sb = new StringBuilder(
                            String.format("POOLWORD addr=%s val=%08x refs=", addr, v));
                        for (Reference r : getReferencesTo(addr)) {
                            Function f = getFunctionContaining(r.getFromAddress());
                            sb.append(String.format("%s(%s) ", r.getFromAddress(),
                                f == null ? "?" : f.getName()));
                        }
                        println(sb.toString());
                    }
                }
                addr = addr.add(4);
            }
        }
    }
}
```

- [ ] **Step 2: Run it over the cart/G1 window; classify**

```bash
scripts/ghidra/run.sh script ListPoolWords.java 0x005f7000 0x005f7800 2>&1 | grep POOLWORD
```
Expected: the descriptor-base word(s) (value `0x005f7000` or `0xa05f7000` — fed to the streaming cluster's `[r14+0x58]`, `boot-binary.md` §4) plus the absolute config words seen in Phase 3 (`0x005f7480/84/90/a4/b8` in `FUN_8c08063c`'s chain). For each, record in KB §patch-sites: pool-word address, value, referencing functions, and the mirror patch (`descriptor-base → MIRROR|P2`, `absolute 0x5f7yyy → MIRROR+0xyyy|P2`). Cross-check with `DisasmRange` where a reference looks unrelated to streaming — any pool word also used by non-cart code is flagged and excluded (needs an instruction-level patch instead; record it explicitly).

- [ ] **Step 3: V3 — read the wait path**

```bash
scripts/ghidra/run.sh script DisasmRange.java 0x8c03bc12 0x8c03bd08 2>&1 | grep -E '^8c03'
```
`FUN_8c03bc12` is called immediately after the `SB_GDST` trigger (`boot-binary.md` §4). Determine what it polls: descriptor-base-relative reads (→ lands in the mirror once repointed — shim controls it), `SB_GDST` directly, or ISTNRM (`0x5f6900`). Record KB §V3 with the verdict and the chosen intercept:
- **Default plan:** entry-hook `FUN_8c03bc12` → `shim_cart_service` (serve read + return "done"); works for every polling variant because the original wait never runs.
- Only if the wait is an *interrupt* wait (sleep-until-IRQ, no poll loop): record the exact wait instruction and plan a branch-over patch alongside the entry hook — and note it for Task 12.

- [ ] **Step 4: Commit**

```bash
git add scripts/ghidra/ListPoolWords.java docs/kb/phase4-conversion.md
git commit -m "Phase 4 Task 6: cart/G1 pool-word classification + V3 wait verdict"
```

---

## Task 7: V5 — battery-SRAM references

**Files:**
- Modify: `docs/kb/phase4-conversion.md`

**Interfaces:**
- Consumes: `ListPoolWords.java` (Task 6).
- Produces: KB §V5 — where the game touches `0x00200000–0x0021ffff`, and the verdict on tolerating flashrom garbage there (spec §3 out-of-scope assumption).

- [ ] **Step 1: Scan and read**

```bash
scripts/ghidra/run.sh script ListPoolWords.java 0x00200000 0x00220000 2>&1 | grep POOLWORD
```
For each hit, `DisasmRange` the referencing function far enough to see the pattern (expected: read block → CRC check → on mismatch, re-init defaults in RAM — mirroring the EEPROM path). Record KB §V5: reference list + verdict. **Gate:** only if the code shows an unguarded dependency (e.g. spins until an SRAM byte matches) does score handling enter Phase 4 — then add a patch-site note (redirect the SRAM base pool word to a shim-home RAM mirror, same mechanic as the G1 mirror) to KB §patch-sites for Task 12.

- [ ] **Step 2: Commit**

```bash
git add docs/kb/phase4-conversion.md
git commit -m "Phase 4 Task 7 (V5): battery-SRAM reference scan + verdict"
```

---

## Task 8: Baked free-play EEPROM harvest

**Files:**
- Create: `shims/data/eeprom.bin` (gitignored)
- Modify: `docs/kb/phase4-conversion.md`

**Interfaces:**
- Produces: `shims/data/eeprom.bin` — exactly 128 bytes, valid CRCs, free-play on. Task 11 embeds it.

- [ ] **Step 1: Configure free-play in the Naomi test menu (user at the controls)**

Run release Flycast with the `.dat` (per `tooling.md`), enter the test menu, set COIN SETTING → FREE PLAY, exit saving. This is a user checkpoint — ask the user to do it and confirm (they've driven the test menu in Phase 1).

- [ ] **Step 2: Harvest the EEPROM file**

```bash
find ~/Library/Application\ Support/Flycast \( -iname '*eeprom*' -o -iname '*.nvmem*' \) 2>/dev/null | head
mkdir -p shims/data
cp "<found eeprom file>" shims/data/eeprom.raw
python3 - <<'EOF'
data = open("shims/data/eeprom.raw","rb").read()
assert len(data) >= 128, len(data)
open("shims/data/eeprom.bin","wb").write(data[:128])
print("first 16:", data[:16].hex())
EOF
```
Expected: 128-byte `eeprom.bin`; the two CRC-protected copies visible as repeated 16-byte system blocks (`naomi-vs-dreamcast.md` §5). Cross-check against Task 4's `mie_sub01.bin` payload at `EE_OFF` — after the free-play change they should differ only in the settings bytes+CRC. Record the file's provenance and first-bytes summary in KB (bytes themselves stay gitignored).

- [ ] **Step 3: Commit (KB only)**

```bash
git add docs/kb/phase4-conversion.md
git commit -m "Phase 4 Task 8: free-play EEPROM harvested (gitignored data)"
```

---

## Task 9: Shim skeleton — layout, linker script, SCIF debug, error spin

**Files:**
- Create: `shims/Makefile`, `shims/shim.ld`, `shims/include/shim_iface.h`, `shims/src/main.c`, `shims/src/scif.c`, `shims/src/util.c`

**Interfaces:**
- Produces: `shims/build/shim.bin` + `shims/build/shim.map`; `shim_iface.h` — the address contract consumed by the loader (Task 13) and patch generator (Task 12); functions `shim_die(code,a,b)`, `scif_puts/puthex`, `xmemcpy` used by Tasks 10–11. Entry symbols exported: `shim_cart_service`, `shim_maple_entry` (stubs here; real bodies Tasks 10–11).

- [ ] **Step 1: Write `shims/include/shim_iface.h`**

```c
/* Single source of truth for Phase 4 addresses. Consumed by shim (freestanding),
 * loader (KOS), and scripts/build_patch_table.py (parses the #defines). */
#ifndef SHIM_IFACE_H
#define SHIM_IFACE_H

#define SHIM_BASE       0x8cfc0000  /* spec §1 RAM map; V2-verified clean */
#define SHIM_CODE_MAX   0x00008000  /* 32 KB code+rodata budget */

/* Fixed data blocks (offsets from SHIM_BASE, all accessed via P2) */
#define SHIM_ERR        (SHIM_BASE + 0x8000)  /* u32[4]: code, a, b, magic */
#define G1_MIRROR       (SHIM_BASE + 0x8800)  /* 0x800 bytes: fake 0x5f7000-0x5f77ff */
#define MAPLE_TX        (SHIM_BASE + 0x9000)  /* 32-byte aligned maple descriptor+frame */
#define MAPLE_RX        (SHIM_BASE + 0x9040)
#define SHIM_BOUNCE     (SHIM_BASE + 0xa000)  /* 2048-byte sector bounce */

#define STAGING_ADDR    0x8cd00000
#define GAME_LOAD_ADDR  0x8c020000
#define GAME_LEN        0x00100000
#define GAME_ENTRY      0x8c04ae2c
#define CART_FAD        47198       /* verified at M1 (Task 2) */
#define CART_SIZE       0x06d00000

#define P2ADDR(a)       ((a) | 0xa0000000)
#ifndef HOST_TEST
#define P2(a)           ((volatile unsigned int *)P2ADDR(a))
#endif

#endif
```

- [ ] **Step 2: Write skeleton sources**

`shims/src/scif.c`:
```c
/* SCIF debug out. Baud/pin state inherited from the KOS boot (dbgio scif). */
typedef volatile unsigned short vu16; typedef volatile unsigned char vu8;
#define SCFSR2  (*(vu16 *)0xffe80010)
#define SCFTDR2 (*(vu8  *)0xffe8000c)
void scif_putc(char c) {
    while (!(SCFSR2 & 0x20)) ;      /* TDFE */
    SCFTDR2 = (unsigned char)c;
    SCFSR2 &= (unsigned short)~0x60;/* clear TDFE|TEND */
}
void scif_puts(const char *s) { while (*s) { if (*s=='\n') scif_putc('\r'); scif_putc(*s++); } }
void scif_puthex(unsigned int v) {
    for (int i = 28; i >= 0; i -= 4) scif_putc("0123456789abcdef"[(v >> i) & 15]);
}
```

`shims/src/util.c`:
```c
#include "shim_iface.h"
void scif_puts(const char *); void scif_puthex(unsigned int);
void *xmemcpy(void *d, const void *s, unsigned int n) {
    unsigned char *dd = d; const unsigned char *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}
void shim_die(unsigned int code, unsigned int a, unsigned int b) {
    volatile unsigned int *e = P2(SHIM_ERR);
    e[1] = a; e[2] = b; e[3] = 0xdeadcafe; e[0] = code;
    scif_puts("SHIMERR code="); scif_puthex(code);
    scif_puts(" a="); scif_puthex(a); scif_puts(" b="); scif_puthex(b); scif_puts("\n");
    for (;;) ;
}
```

`shims/src/main.c` (stub entries — replaced by Tasks 10/11):
```c
#include "shim_iface.h"
void shim_die(unsigned int, unsigned int, unsigned int);
void shim_cart_service(void) { shim_die(0x10, 0, 0); }  /* Task 10 */
void shim_maple_entry(void)  { shim_die(0x11, 0, 0); }  /* Task 11 */
```

`shims/shim.ld`:
```
OUTPUT_ARCH(sh)
ENTRY(shim_cart_service)
SECTIONS {
  . = 0x8cfc0000;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
  ASSERT(. <= 0x8cfc8000, "shim exceeds SHIM_CODE_MAX")
}
```

`shims/Makefile`:
```makefile
CC = /opt/toolchains/dc/sh-elf/bin/sh-elf-gcc
NM = /opt/toolchains/dc/sh-elf/bin/sh-elf-nm
OBJCOPY = /opt/toolchains/dc/sh-elf/bin/sh-elf-objcopy
CFLAGS = -ml -m4-single-only -ffreestanding -nostdlib -Os -Wall -Iinclude
SRCS = src/main.c src/scif.c src/util.c
B = build

all: $(B)/shim.bin $(B)/shim.map
$(B)/shim.elf: $(SRCS) shim.ld include/shim_iface.h
	mkdir -p $(B)
	$(CC) $(CFLAGS) -Wl,-T,shim.ld -Wl,-Map,$(B)/shim.map.tmp -o $@ $(SRCS)
$(B)/shim.bin: $(B)/shim.elf
	$(OBJCOPY) -O binary $< $@
$(B)/shim.map: $(B)/shim.elf
	$(NM) $< > $@
clean:
	rm -rf $(B)
```

- [ ] **Step 3: Build and verify layout**

```bash
make -C shims
grep -E 'shim_cart_service|shim_maple_entry' shims/build/shim.map
```
Expected: both symbols at `0x8cfc0xxx`, `shim.bin` a few hundred bytes.

- [ ] **Step 4: Commit**

```bash
git add shims/
git commit -m "Phase 4 Task 9: shim skeleton (layout header, linker script, SCIF, die)"
```

---

## Task 10: Cart-read shim (GD syscalls + head/body/tail) with host-tested split math

**Files:**
- Create: `shims/src/gd.c`, `shims/src/cart.c`, `shims/test/test_host.c`
- Modify: `shims/src/main.c` (drop cart stub), `shims/Makefile` (add sources + `test` target)

**Interfaces:**
- Consumes: `shim_iface.h`, `shim_die`, `xmemcpy` (Task 9); V1 verdict = syscalls (Task 3); mirror layout (values arrive via Task 6 patches at runtime).
- Produces: `void shim_cart_service(void)` — the entry Task 12 hooks onto `FUN_8c03bc12`; `int gd_read_sectors(void *dst, unsigned int fad, unsigned int n)`; pure `void cart_split(u32 off, u32 len, split_t *s)` (host-tested).

- [ ] **Step 1: Write the failing host test**

`shims/test/test_host.c`:
```c
/* Host-side test of the pure split math. Build: cc -DHOST_TEST. */
#include <assert.h>
#include <stdio.h>
#include "../include/shim_iface.h"
#include "../src/cart.c"     /* pure part only (guards out SH-4 code) */

int main(void) {
    split_t s;
    /* aligned, exact sectors: no head/tail */
    cart_split(0, 4096, &s);
    assert(s.head_take == 0 && s.body_sect == 2 && s.body_fad == 0 && s.tail_take == 0);
    /* unaligned start, within one sector */
    cart_split(100, 50, &s);
    assert(s.head_fad == 0 && s.head_skip == 100 && s.head_take == 50);
    assert(s.body_sect == 0 && s.tail_take == 0);
    /* unaligned start crossing into full sectors + tail */
    cart_split(2048 + 32, 2048 * 3, &s);
    assert(s.head_fad == 1 && s.head_skip == 32 && s.head_take == 2048 - 32);
    assert(s.body_fad == 2 && s.body_sect == 2);
    assert(s.tail_fad == 4 && s.tail_take == 32);
    /* head fills to boundary exactly, then body only */
    cart_split(2048 - 64, 64 + 2048, &s);
    assert(s.head_take == 64 && s.body_sect == 1 && s.tail_take == 0);
    printf("PASS test_host cart_split\n");
    return 0;
}
```

- [ ] **Step 2: Run it to see it fail**

```bash
cc -DHOST_TEST -o /tmp/test_host shims/test/test_host.c && /tmp/test_host
```
Expected: FAIL to compile — `cart.c`/`split_t` don't exist yet.

- [ ] **Step 3: Write `shims/src/cart.c` and `shims/src/gd.c`**

`shims/src/cart.c`:
```c
#include "shim_iface.h"
typedef unsigned int u32;

typedef struct {
    u32 head_fad, head_skip, head_take;   /* fads are cart-relative sector indices */
    u32 body_fad, body_sect;
    u32 tail_fad, tail_take;
} split_t;

/* Pure: decompose (byte offset, byte len) into partial-head / whole-sector
 * body / partial-tail. Compiled on host for the unit test. */
void cart_split(u32 off, u32 len, split_t *s) {
    u32 sec = off / 2048, skip = off % 2048;
    s->head_fad = s->head_skip = s->head_take = 0;
    s->body_fad = s->body_sect = 0;
    s->tail_fad = s->tail_take = 0;
    if (skip) {
        u32 take = 2048 - skip; if (take > len) take = len;
        s->head_fad = sec; s->head_skip = skip; s->head_take = take;
        len -= take; sec++;
    }
    s->body_fad = sec;
    s->body_sect = len / 2048;
    sec += s->body_sect; len %= 2048;
    if (len) { s->tail_fad = sec; s->tail_take = len; }
}

#ifndef HOST_TEST
void shim_die(u32, u32, u32);
void *xmemcpy(void *, const void *, u32);
int gd_read_sectors(void *dst, u32 fad, u32 n);
void scif_puts(const char *); void scif_puthex(u32);

static void gd_or_die(void *dst, u32 rel_fad, u32 n) {
    int r = gd_read_sectors(dst, CART_FAD + rel_fad, n);
    if (r < 0) shim_die(4, rel_fad, (u32)r);
}

void cart_read(u32 off, u32 len, u32 dest_phys) {
    split_t s;
    unsigned char *dst = (unsigned char *)(dest_phys | 0x80000000); /* P1 */
    unsigned char *bounce = (unsigned char *)SHIM_BOUNCE;
    cart_split(off, len, &s);
    if (s.head_take) {
        gd_or_die(bounce, s.head_fad, 1);
        xmemcpy(dst, bounce + s.head_skip, s.head_take);
        dst += s.head_take;
    }
    if (s.body_sect) {
        gd_or_die(dst, s.body_fad, s.body_sect);
        dst += s.body_sect * 2048;
    }
    if (s.tail_take) {
        gd_or_die(bounce, s.tail_fad, 1);
        xmemcpy(dst, bounce, s.tail_take);
    }
}

/* Entry hooked onto the game's DMA-completion wait (KB §V3, patch via Task 12).
 * Reads the mirrored register values the game already wrote (KB §patch-sites). */
void shim_cart_service(void) {
    volatile u32 *m = P2(G1_MIRROR);
    u32 off = (((m[0x0c/4] & 0xffff) << 16) | (m[0x10/4] & 0xffff)) & 0x0fffffff;
    u32 len = m[0x408/4];               /* SB_GDLEN mirror (bytes) */
    u32 cnt = m[0x14/4] * 32;           /* DMA_COUNT mirror (0x20 units) */
    if (!len) len = cnt;
    else if (cnt && cnt != len) shim_die(1, cnt, len);
    u32 dest = m[0x404/4];              /* SB_GDSTAR mirror (phys dest) */
    if (!len || off + len > CART_SIZE || (dest & 0x1f000000) != 0x0c000000)
        shim_die(2, off, len ? dest : 0);
    scif_puts("CART off="); scif_puthex(off);
    scif_puts(" len="); scif_puthex(len);
    scif_puts(" dst="); scif_puthex(dest); scif_puts("\n");
    cart_read(off, len, dest);
    m[0x418/4] = 0;                     /* SB_GDST mirror reads "done" */
}
#endif /* !HOST_TEST */
```

`shims/src/gd.c`:
```c
/* GD-ROM via DC BIOS syscall vector 0x8c0000bc (mc.pp.se/dc/syscalls.html;
 * constants cross-checked against tools/kos/kernel/arch/dreamcast/hardware/
 * cdrom.c — cite exact lines in KB when verified). Polling only, no IRQs.
 * PIOREAD (16), not DMAREAD: no G1-DMA side effects in game context.
 * ponytail: PIO read speed is fine under emulation; DMAREAD is the Phase 5
 * upgrade path if real-hardware streaming stutters. */
#include "shim_iface.h"
typedef unsigned int u32;
typedef int (*gdc_t)(u32, u32, u32, u32);

#define GDC       ((gdc_t)(*(volatile u32 *)0x8c0000bc))
#define CMD_PIOREAD   16
#define CMD_INIT      24
#define GD_SEND       0   /* r7 function codes, superfn r6 = 0 */
#define GD_CHECK      1
#define GD_MAINLOOP   2

int gd_read_sectors(void *dst, u32 fad, u32 n) {
    u32 param[4], stat[4];
    param[0] = fad; param[1] = n; param[2] = (u32)dst; param[3] = 0;
    int req = GDC((u32)CMD_PIOREAD, (u32)param, 0, GD_SEND);
    if (req <= 0) return -1;
    for (;;) {
        GDC(0, 0, 0, GD_MAINLOOP);
        int s = GDC((u32)req, (u32)stat, 0, GD_CHECK);
        if (s == 2) return 0;               /* COMPLETED */
        if (s == -1 || s == 3) return -2;   /* ABORTED/error */
    }
}
```

Update `shims/src/main.c` (remove the cart stub, keep the maple stub):
```c
#include "shim_iface.h"
void shim_die(unsigned int, unsigned int, unsigned int);
void shim_maple_entry(void)  { shim_die(0x11, 0, 0); }  /* Task 11 */
```

`shims/Makefile`: add `src/gd.c src/cart.c` to `SRCS` and a host-test target:
```makefile
test:
	mkdir -p $(B)
	cc -DHOST_TEST -Iinclude -o $(B)/test_host test/test_host.c && $(B)/test_host
```
(The `-Iinclude` matters: `cart.c`'s `#include "shim_iface.h"` resolves through it on the host build. Step 2's one-off `cc` needs `-Ishims/include` for the same reason when run from the repo root.)

- [ ] **Step 4: Run tests + target build**

```bash
make -C shims test && make -C shims
grep shim_cart_service shims/build/shim.map
```
Expected: `PASS test_host cart_split`; clean cross-build; symbol present. Verify the syscall constants against `tools/kos/kernel/arch/dreamcast/hardware/cdrom.c` (`CMD_PIOREAD`, status values) and add the `file:line` cite to the `gd.c` header comment.

- [ ] **Step 5: Commit**

```bash
git add shims/
git commit -m "Phase 4 Task 10: cart-read shim (GD syscalls, host-tested split math)"
```

---

## Task 11: Input + EEPROM shim (Maple GetCondition, fake MIE replies)

**Files:**
- Create: `shims/src/maple.c`, `shims/src/jvs.c`
- Modify: `shims/src/main.c` (real `shim_maple_entry`), `shims/test/test_host.c` (JVS translate cases), `shims/Makefile` (sources + data embedding)

**Interfaces:**
- Consumes: KB §input-ABI values (Task 5): `MIE_REQ_SUB_ADDR`, `MIE_RESP_BUF`, `BTN_OFF`, `EE_OFF`, completion signal; templates `build/mie_sub15.bin` etc. (Task 4); `shims/data/eeprom.bin` (Task 8).
- Produces: `void shim_maple_entry(void)` — Task 12 pointer-swaps `0x8c0275da`/`0x8c0275e0` slots and entry-hooks `FUN_8c03c2c6` to it; pure `unsigned short dc_to_jvs(unsigned short)` (host-tested).

- [ ] **Step 1: Add failing JVS-translate cases to the host test**

Append to `shims/test/test_host.c` `main()` (before the final print):
```c
    /* dc_to_jvs: DC condition is ACTIVE-LOW (0=pressed); JVS active-high.
       DC bits (maple GetCondition, controller): 1=B 2=A 3=Start 4=Up 5=Down 6=Left 7=Right */
    assert(dc_to_jvs(0xffff) == 0x0000);              /* nothing pressed */
    assert(dc_to_jvs((unsigned short)~(1u << 3)) == 0x8000);   /* Start */
    assert(dc_to_jvs((unsigned short)~(1u << 4)) == 0x2000);   /* Up */
    assert(dc_to_jvs((unsigned short)~(1u << 5)) == 0x1000);   /* Down */
    assert(dc_to_jvs((unsigned short)~(1u << 6)) == 0x0800);   /* Left */
    assert(dc_to_jvs((unsigned short)~(1u << 7)) == 0x0400);   /* Right */
    assert(dc_to_jvs((unsigned short)~(1u << 2)) == 0x0200);   /* A -> B1 */
    assert(dc_to_jvs((unsigned short)~(1u << 1)) == 0x0100);   /* B -> B2 */
    assert(dc_to_jvs((unsigned short)~((1u<<3)|(1u<<4))) == 0xa000); /* chord */
```
Include `../src/jvs.c` next to the `cart.c` include. Run `make -C shims test` — expected: compile FAIL (`jvs.c` missing).

- [ ] **Step 2: Write `shims/src/jvs.c`** (pure)

```c
/* DC controller condition -> JVS button word (docs/kb/input-map.md).
 * DC buttons active-low (mc.pp.se/dc/controller.html); JVS active-high. */
unsigned short dc_to_jvs(unsigned short dc) {
    unsigned short b = (unsigned short)~dc, j = 0;
    if (b & 0x0008) j |= 0x8000;  /* Start */
    if (b & 0x0010) j |= 0x2000;  /* Up    */
    if (b & 0x0020) j |= 0x1000;  /* Down  */
    if (b & 0x0040) j |= 0x0800;  /* Left  */
    if (b & 0x0080) j |= 0x0400;  /* Right */
    if (b & 0x0004) j |= 0x0200;  /* A -> B1 */
    if (b & 0x0002) j |= 0x0100;  /* B -> B2 */
    return j;
}
```
Run `make -C shims test` — expected: `PASS`.

- [ ] **Step 3: Write `shims/src/maple.c` and the real entry**

`shims/src/maple.c`:
```c
/* Maple GetCondition to port-A controller, polled. Registers per
 * mc.pp.se/dc/maplebus.html + KOS kernel/arch/dreamcast/hardware/maple/
 * (verify names/lines when writing the KB note). All buffers in shim home,
 * accessed via P2 - no cache maintenance needed. */
#include "shim_iface.h"
typedef unsigned int u32;

#define SB_MDSTAR (*(volatile u32 *)0xa05f6c04)
#define SB_MDTSEL (*(volatile u32 *)0xa05f6c10)
#define SB_MDEN   (*(volatile u32 *)0xa05f6c14)
#define SB_MDST   (*(volatile u32 *)0xa05f6c18)

/* returns DC button word (active-low), or 0xffff if no/failed reply */
unsigned short maple_getcond(void) {
    volatile u32 *tx = P2(MAPLE_TX);
    volatile u32 *rx = P2(MAPLE_RX);
    rx[0] = 0;
    tx[0] = 0x80000000 | (0 << 16) | 1;          /* last | port A | 1 extra word */
    tx[1] = MAPLE_RX & 0x1fffffff;               /* receive addr (phys) */
    tx[2] = (1u << 24) | (0x00 << 16) | (0x20 << 8) | 9; /* len|src host A|dst A-main|GETCOND */
    tx[3] = 0x01000000;                          /* FUNC_CONTROLLER */
    SB_MDTSEL = 0;
    SB_MDSTAR = MAPLE_TX & 0x1fffffff;
    SB_MDEN = 1;
    SB_MDST = 1;
    while (SB_MDST & 1) ;
    if ((rx[0] & 0xff) != 8) return 0xffff;      /* not DATA_TRANSFER */
    return (unsigned short)(rx[2] & 0xffff);     /* cond.buttons, active-low */
}
```

`shims/src/main.c` becomes the dispatcher (values marked `KB` come from `docs/kb/phase4-conversion.md` §input-ABI — fill the actual numbers recorded there):
```c
#include "shim_iface.h"
typedef unsigned int u32;
void shim_die(u32, u32, u32);
void *xmemcpy(void *, const void *, u32);
unsigned short maple_getcond(void);
unsigned short dc_to_jvs(unsigned short);

/* Templates + EEPROM embedded at build (Makefile xxd rules) */
extern const unsigned char mie_sub15[], mie_sub01[], eeprom_img[];
extern const unsigned int mie_sub15_len, mie_sub01_len;

#define MIE_REQ_SUB_ADDR  0x0 /* KB §input-ABI (Task 5) */
#define MIE_RESP_BUF      0x0 /* KB §input-ABI == V4 addr */
#define BTN_OFF           0x0 /* KB §input-ABI */
#define EE_OFF            0x0 /* KB §input-ABI */

void shim_maple_entry(void) {
    u32 sub = *(volatile unsigned char *)MIE_REQ_SUB_ADDR;
    unsigned char *rx = (unsigned char *)P2ADDR(MIE_RESP_BUF);
    switch (sub) {
    case 0x15: {                          /* input poll */
        xmemcpy(rx, mie_sub15, mie_sub15_len);
        unsigned short j = dc_to_jvs(maple_getcond());
        rx[BTN_OFF]     = (unsigned char)(j & 0xff);   /* byte order per template; */
        rx[BTN_OFF + 1] = (unsigned char)(j >> 8);     /* flip if M4 shows swapped */
        break;
    }
    case 0x01: case 0x03:                 /* EEPROM read */
        xmemcpy(rx, mie_sub01, mie_sub01_len);
        xmemcpy(rx + EE_OFF, eeprom_img, 128);
        break;
    case 0x0b:                            /* EEPROM write: ack, drop */
        xmemcpy(rx, mie_sub01, mie_sub01_len);
        break;
    default:
        shim_die(3, sub, 0);
    }
    /* completion signal per KB §input-ABI (e.g. status word write) goes here */
}
```
If Task 5 found the subcommand arrives in a register argument instead of memory, give `shim_maple_entry` the matching signature (`void shim_maple_entry(u32 r4, u32 r5)` — SH-4 args land in r4–r7 in order) and read it from there; record which in the KB.

`shims/Makefile` — embed data (append):
```makefile
$(B)/mie_sub15.c: ../build/mie_sub15.bin
	xxd -n mie_sub15 -i $< > $@
$(B)/mie_sub01.c: ../build/mie_sub01.bin
	xxd -n mie_sub01 -i $< > $@
$(B)/eeprom_img.c: data/eeprom.bin
	xxd -n eeprom_img -i $< > $@
SRCS += src/maple.c src/jvs.c $(B)/mie_sub15.c $(B)/mie_sub01.c $(B)/eeprom_img.c
```
(`xxd -n NAME` pins the array symbol to `NAME`/`NAME_len`, matching the externs in `main.c`; `xxd -i` alone would derive names from the file path.)

- [ ] **Step 4: Fill the KB constants, build, verify**

Replace the four `0x0 /* KB … */` defines with the real values from `docs/kb/phase4-conversion.md`. Then:
```bash
make -C shims test && make -C shims
grep -E 'shim_maple_entry|maple_getcond' shims/build/shim.map
```
Expected: host tests PASS, cross-build clean, symbols present, `shim.bin` still under 32 KB.

- [ ] **Step 5: Commit**

```bash
git add shims/
git commit -m "Phase 4 Task 11: input+EEPROM shim (GetCondition, fake MIE replies, baked EEPROM)"
```

---

## Task 12: Patch table generator

**Files:**
- Create: `scripts/build_patch_table.py`

**Interfaces:**
- Consumes: `tools/boot.bin` (originals), `shims/build/shim.map` (symbol addresses), KB §patch-sites + §V3 (addresses for the definitions).
- Produces: `build/patch_table.h` (gitignored — embeds original ROM bytes) with `patch_t cleo_patches[]`; consumed by the loader (Task 13). Patch kinds: `pool` (u32 replace), `ptr` (u32 replace with expectation), `hook` (6-byte thunk + pool slot).

- [ ] **Step 1: Write the generator**

```python
#!/usr/bin/env python3
"""Generate build/patch_table.h from patch definitions + tools/boot.bin +
shims/build/shim.map. Definitions reference shim symbols by name; original
bytes are read from boot.bin (ROM-derived -> output is gitignored).

Patch kinds:
  pool(addr, value)                 replace the u32 pool word at addr
  pool(addr, sym=..., p2=True)      ... with a shim symbol address (P2-ORed)
  ptr(addr, expect, sym)            u32 slot swap, asserts current == expect
  hook(fn, sym)                     overwrite fn entry with: mov.l @(disp,PC),r0;
                                    jmp @r0; nop; [pad]; .long target
Addresses are game VAs (0x8c02xxxx..); boot.bin offset = addr - 0x8c020000."""
import pathlib, struct, sys

BASE = 0x8C020000
boot = pathlib.Path("tools/boot.bin").read_bytes()
assert len(boot) == 0x100000, "tools/boot.bin missing/wrong size"

symtab = {}
for line in pathlib.Path("shims/build/shim.map").read_text().splitlines():
    parts = line.split()
    if len(parts) == 3:
        symtab[parts[2]] = int(parts[0], 16)

def sym(name, p2=True):
    v = symtab[name]
    return v | 0xA0000000 if p2 else v

def rd(addr, n):
    off = addr - BASE
    assert 0 <= off <= len(boot) - n, hex(addr)
    return boot[off:off + n]

patches = []   # (addr, old bytes, new bytes, comment)

def pool(addr, value, comment=""):
    patches.append((addr, rd(addr, 4), struct.pack("<I", value), comment))

def ptr(addr, expect, target, comment=""):
    old = rd(addr, 4)
    assert struct.unpack("<I", old)[0] == expect, \
        f"ptr @{addr:#x}: found {old.hex()}, expected {expect:#x}"
    patches.append((addr, old, struct.pack("<I", target), comment))

def hook(fn, target, comment=""):
    # thunk: mov.l @(disp,PC),r0 ; jmp @r0 ; nop ; [2-byte pad if needed] ; .long target
    slot = (fn + 6 + 3) & ~3
    pad = slot - (fn + 6)
    disp = (slot - ((fn & ~3) + 4)) // 4
    assert 0 <= disp <= 255, hex(fn)
    code = struct.pack("<HHH", 0xD000 | disp, 0x402B, 0x0009)
    code += b"\x09\x00" * (pad // 2) + struct.pack("<I", target)
    patches.append((fn, rd(fn, len(code)), code, comment))

# ---- definitions (addresses from docs/kb/phase4-conversion.md) ----------
# §patch-sites: descriptor-base + absolute G1 pool words -> mirror. EXAMPLES,
# replace with the recorded list:
#   pool(0x8c0xxxxx, sym("g1_mirror_blk"), "descriptor base 0xa05f7000 -> mirror")
#   pool(0x8c0xxxxx, sym("g1_mirror_blk") + 0x480, "abs 0x5f7480 -> mirror+0x480")
# §V3: completion-wait hook:
#   hook(0x8c03bc12, sym("shim_cart_service", p2=False), "wait -> cart service")
# §input-ABI (boot-binary.md §5): pointer-table swap + secondary entry hook:
#   ptr(0x8c0275dc, 0x8c0315ce, sym("shim_maple_entry", p2=False), "maple ptr swap")
#   hook(0x8c03c2c6, sym("shim_maple_entry", p2=False), "secondary maple site")
# --------------------------------------------------------------------------

out = ["/* GENERATED by scripts/build_patch_table.py - do not edit, do not commit",
       " * (embeds original ROM bytes). */",
       "typedef struct { unsigned int addr, len;",
       "  const unsigned char *old, *neu; const char *what; } patch_t;"]
for i, (addr, old, new, c) in enumerate(patches):
    out.append(f"static const unsigned char p{i}o[] = {{{','.join(map(hex, old))}}};")
    out.append(f"static const unsigned char p{i}n[] = {{{','.join(map(hex, new))}}};")
rows = ",\n".join(
    f'  {{0x{a:08x}u, {len(o)}u, p{i}o, p{i}n, "{c}"}}'
    for i, (a, o, n, c) in enumerate(patches))
out.append(f"static const patch_t cleo_patches[] = {{\n{rows}\n}};")
out.append(f"enum {{ CLEO_NPATCHES = {len(patches)} }};")
pathlib.Path("build").mkdir(exist_ok=True)
pathlib.Path("build/patch_table.h").write_text("\n".join(out) + "\n")
print(f"OK patch_table.h: {len(patches)} patches")
```

- [ ] **Step 2: Fill the real definitions and generate**

Replace the example block with the actual entries from KB §patch-sites (Task 6), §V3, and §input-ABI (Task 5 — the two pointer-table slot addresses that hold `0x8c0315ce`; find their exact u32-aligned addresses in the Task 5 disassembly). Run:

```bash
python3 scripts/build_patch_table.py
```
Expected: `OK patch_table.h: N patches` with no assertion failure — every `ptr` expectation matching proves the addresses are right against the real ROM. An assertion failure means a wrong KB address: fix the KB first, then the defs (never force).

- [ ] **Step 3: Commit**

```bash
git add scripts/build_patch_table.py
git commit -m "Phase 4 Task 12: patch table generator (pool/ptr/hook kinds, ROM-verified)"
```

---

## Task 13: Full loader — patch, place, jump — **Milestone M2**

**Files:**
- Modify: `loader/main.c`, `loader/Makefile`
- Create: `loader/handoff.S`

**Interfaces:**
- Consumes: `build/patch_table.h` (Task 12), `shims/build/shim.bin` (Tasks 9–11), `shim_iface.h` constants, mastering (Task 2).
- Produces: the bootable chain — M2 = first `CART off=… len=… dst=…` line from the shim on serial (game reached its first streamed read through the whole pipeline).

- [ ] **Step 1: Write `loader/handoff.S`**

```asm
! handoff(src_p2 r4, dst_p2 r5, len r6, entry r7)
! Copies the patched game image into place and jumps. Runs from its P2 alias
! (caller converts the address), so nothing executing is in a cache line.
    .globl _handoff
    .align 2
_handoff:
    mov     r6,r0
    shlr2   r0              ! words
1:  mov.l   @r4+,r1
    mov.l   r1,@r5
    add     #4,r5
    dt      r0
    bf      1b
    mov.l   ccr_a,r1        ! invalidate+enable both caches, copy-back P1
    mov.l   ccr_v,r2
    mov.l   r2,@r1
    nop; nop; nop; nop; nop; nop; nop; nop
    jmp     @r7
    nop
    .align 2
ccr_a:  .long 0xff00001c
ccr_v:  .long 0x0000090d    ! OCE|CB|OCI|ICE|ICI - cross-check KOS
                            ! kernel/arch/dreamcast/kernel/startup.s value
```

- [ ] **Step 2: Extend `loader/main.c`**

```c
#include <kos.h>
#include "shim_iface.h"
#include "patch_table.h"        /* generated, gitignored */

extern uint8 shim_bin[];        /* objcopy-embedded, see Makefile */
extern uint8 shim_bin_end[];
extern void handoff(uint32 src, uint32 dst, uint32 len, uint32 entry);

#define GAME_SECTORS (GAME_LEN / 2048)

static int apply_patches(uint8 *img) {
    for (unsigned i = 0; i < CLEO_NPATCHES; i++) {
        const patch_t *p = &cleo_patches[i];
        uint8 *at = img + (p->addr - GAME_LOAD_ADDR);
        if (memcmp(at, p->old, p->len)) {
            dbglog(DBG_INFO, "PATCH MISMATCH %s @%08lx\n", p->what, (unsigned long)p->addr);
            return -1;
        }
        memcpy(at, p->neu, p->len);
        dbglog(DBG_INFO, "patched %s @%08lx (%lu)\n", p->what,
               (unsigned long)p->addr, (unsigned long)p->len);
    }
    return 0;
}

int main(void) {
    dbglog(DBG_INFO, "CLEO LOADER M2\n");
    cdrom_reinit();
    uint8 *stage = (uint8 *)STAGING_ADDR;
    if (cdrom_read_sectors(stage, CART_FAD, GAME_SECTORS) != ERR_OK)
        { dbglog(DBG_INFO, "read fail\n"); for(;;) thd_sleep(1000); }
    if (memcmp(stage, "NAOMI", 5))
        { dbglog(DBG_INFO, "bad image\n"); for(;;) thd_sleep(1000); }
    memcpy((void *)SHIM_BASE, shim_bin, shim_bin_end - shim_bin);
    if (apply_patches(stage)) for(;;) thd_sleep(1000);
    dbglog(DBG_INFO, "jumping to %08x\n", GAME_ENTRY);
    dcache_flush_range(STAGING_ADDR, GAME_LEN);
    dcache_flush_range(SHIM_BASE, shim_bin_end - shim_bin);
    irq_disable();
    void (*ho)(uint32, uint32, uint32, uint32) =
        (void *)(((uint32)&handoff) | 0xa0000000);
    ho(P2ADDR(STAGING_ADDR), 0xac020000 /* P2 alias of GAME_LOAD_ADDR */,
       GAME_LEN, GAME_ENTRY);
    return 0; /* unreachable */
}
```

`loader/Makefile` additions:
```makefile
OBJS = main.o handoff.o shim_blob.o
CFLAGS += -I../shims/include -I../build
shim_blob.o: ../shims/build/shim.bin
	sh-elf-objcopy -I binary -O elf32-sh -B sh4 \
	  --redefine-sym _binary____shims_build_shim_bin_start=shim_bin \
	  --redefine-sym _binary____shims_build_shim_bin_end=shim_bin_end \
	  $< $@
```
(Check the exact generated symbol names with `sh-elf-nm shim_blob.o` and fix the `--redefine-sym` spellings to match.)

- [ ] **Step 3: Build the full chain + master**

```bash
source tools/kos/environ.sh
make -C shims && python3 scripts/build_patch_table.py && make -C loader
python3 scripts/make_gdi.py
```
Expected: all clean; `cleo.gdi` rebuilt.

- [ ] **Step 4: M2 run**

```bash
/Applications/Flycast.app/Contents/MacOS/Flycast "$PWD/build/cleo.gdi"
```
Expected serial: `CLEO LOADER M2`, N× `patched …`, `jumping to 8c04ae2c`, then — the game boots far enough to stream — `CART off=00800000 len=… dst=…` lines (first runtime DMA per `cart-streaming-map.md` starts at `0x800000`) or a `SHIMERR` with a readable code. Either is M2: the pipeline runs end to end. Debug loop for a silent hang: check `SHIM_ERR` words and the game-visible RAM in Flycast's debugger; the systematic-debugging skill applies.

- [ ] **Step 5: Commit**

```bash
git add loader/
git commit -m "Phase 4 Task 13 (M2): full loader - patch, place, handoff; first shim output"
```

---

## Task 14: Attract mode — **Milestone M3** + regression oracle

**Files:**
- Create: `scripts/check_triples.py`
- Modify: `scripts/build_patch_table.py` (only if M3 debugging changes definitions), `docs/kb/phase4-conversion.md`

**Interfaces:**
- Consumes: everything through Task 13.
- Produces: attract mode running in Flycast DC profile; `check_triples.py` green.

- [ ] **Step 1: Write the oracle**

```python
#!/usr/bin/env python3
"""Every Phase 2 captured triple must be servable from the mastered layout."""
import csv, sys
CART_SIZE = 0x6D00000
bad = 0
with open("docs/kb/cart-streaming-map.csv") as f:
    for row in csv.DictReader(f):
        if row["mode"] != "DMA":
            continue
        off, ln, dest = (int(row[k], 16) for k in ("cart_offset", "length", "dest"))
        if off + ln > CART_SIZE:
            print(f"UNSERVABLE off={off:#x} len={ln:#x}"); bad += 1
        if not (0x0C000000 <= dest and dest + ln <= 0x0D000000):
            print(f"BAD DEST dest={dest:#x} len={ln:#x}"); bad += 1
print("CHECK triples_servable:", "FAIL" if bad else "PASS")
sys.exit(1 if bad else 0)
```
Run: `python3 scripts/check_triples.py` → `CHECK triples_servable: PASS`. (Column names must match the CSV header — adjust to the actual header on first run.)

- [ ] **Step 2: M3 — attract mode**

Run the GDI; let it sit through boot into attract. Expected: title/attract renders and loops; serial shows a steady stream of `CART …` lines whose offsets appear in `cart-streaming-map.csv`. Known-acceptable: texture glitches (VRAM 9.2 > 8 MB — spec §5 accepted risk). Failure modes and where to look: `SHIMERR 1/2` = mirror values inconsistent → re-check §patch-sites classification (a store site was missed — rerun Task 6 Step 2 including a `DisasmRange` of the reporting PC); hang with no `CART` lines = wait-site verdict wrong → revisit KB §V3. Use systematic-debugging; record any new patch definition in the KB before adding it to the generator.

- [ ] **Step 3: Record M3 + commit**

Note the M3 result (and any definition changes) in `docs/kb/phase4-conversion.md`.
```bash
git add scripts/check_triples.py scripts/build_patch_table.py docs/kb/phase4-conversion.md
git commit -m "Phase 4 Task 14 (M3): attract mode runs from GDI; triple oracle green"
```

---

## Task 15: Input + EEPROM live — **Milestones M4, M5**

**Files:**
- Modify: `shims/src/main.c` (byte-order/completion fixes only, if M4 testing demands), `docs/kb/phase4-conversion.md`

**Interfaces:**
- Consumes: Tasks 11–14.
- Produces: game controllable with the DC controller (M4); free-play, no error screens, no coin prompt (M5).

- [ ] **Step 1: M4 — controls**

Map keyboard→DC controller in Flycast (Settings → Controls, DC profile). Run the GDI, press Start on the title. Expected: game responds; all 7 controls work in play (Start, 4 directions, rotate = A/B). If a button reads inverted/swapped: the JVS word byte order at `BTN_OFF` is flipped — swap the two stores in `shim_maple_entry` (the marked comment); if *all* input is dead, the completion signal from KB §input-ABI is wrong — re-read the caller's post-`jsr` polling (Task 5 Step 1 commands) and fix the signal write.

- [ ] **Step 2: M5 — settings**

From a cold boot: no EEPROM error screen, no coin counter — attract says FREE PLAY, Start enters the game directly. If the game shows a settings-initialization screen, the baked image's CRC/serial doesn't match what the game expects: re-check Task 8's harvest against `mie_sub01.bin`'s payload (`EE_OFF`) — game serial lives in the game section (`naomi-vs-dreamcast.md` §5) — and re-harvest after a Naomi-mode boot of the *same* ROM.

- [ ] **Step 3: Record + commit**

```bash
git add shims/ docs/kb/phase4-conversion.md
git commit -m "Phase 4 Task 15 (M4+M5): DC controller input + free-play EEPROM live"
```

---

## Task 16: M6 — play to game-over; KB closeout — **Phase 4 done**

**Files:**
- Modify: `docs/kb/phase4-conversion.md`, `docs/kb/00-status.md`

**Interfaces:**
- Consumes: everything.
- Produces: Phase 4 exit criteria (spec) all checked; status advanced to Phase 5.

- [ ] **Step 1: M6 — user checkpoint**

Ask the user to play the GDI build in Flycast DC profile to a game-over: controls sane, difficulty screens/bonus stages stream correctly, no hangs. This is the spec's definition of done — user confirmation required, same as Phase 1's boot check.

- [ ] **Step 2: KB closeout**

`docs/kb/phase4-conversion.md`: complete the build/run pipeline section — the one-command chain from `.dat` to `cleo.gdi`:
```bash
source tools/kos/environ.sh && make -C shims && \
python3 scripts/build_patch_table.py && make -C loader && \
python3 scripts/make_gdi.py && python3 scripts/check_triples.py
```
plus V1–V5 verdicts (all recorded en route), the final patch list, and known cosmetic issues (VRAM/audio) handed to Phase 5.

`docs/kb/00-status.md`: Phase 4 → DONE with a findings block (patch count, V verdicts, M6 confirmation date); Phase 5 → NEXT (hardware/GDEMU, VRAM/sound fitting, latency, score persistence nicety); update "Next step".

- [ ] **Step 3: Verify exit criteria and commit**

Check against the spec's exit criteria: M1–M6 demonstrated (M6 user-confirmed), V1–V5 in KB with evidence, pipeline reproducible (run the one-command chain from clean `build/`), status advanced. Run the full verification set:
```bash
python3 scripts/test_parse_cart_log.py && make -C shims test && python3 scripts/check_triples.py
```
Expected: all PASS.
```bash
git add docs/kb/
git commit -m "Phase 4 Task 16 (M6): playable to game-over in Flycast DC profile - Phase 4 complete"
```
