# Phase 2 — Instrumented Analysis Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an instrumented Flycast that logs this game's runtime cart-streaming requests, RAM/VRAM/sound-RAM peaks, JVS input bits, and serial/watchdog pokes; capture that data by playing; and record it in the knowledge base for Phase 4.

**Architecture:** Patch Flycast's Naomi cart + G1 DMA path (`core/hw/naomi/naomi.cpp`, `naomi_cart.*`) and its JVS report path (`core/hw/maple/maple_devs.cpp`) to emit tagged lines through a tiny shared logging helper that writes to a dedicated file. A Python parser turns those files into a machine-readable cart-streaming map plus RAM/serial verdicts. The user plays capture sessions; attract mode is captured autonomously.

**Tech Stack:** Flycast (C++, CMake) pinned at commit `f09d1f22ef8d199b8b7a2395d0b46774e08a58c2`; Python 3 stdlib for the parser; macOS/arm64 host.

**Spec:** `docs/superpowers/specs/2026-07-18-phase2-instrumented-analysis-design.md`
**Prior context:** `docs/kb/naomi-vs-dreamcast.md` (§3 cart interface, §4 input, §7 misc), `docs/kb/game.md`, `docs/kb/tooling.md`, `docs/kb/00-status.md`.

## Global Constraints

- The ROM (`Cleopatra Fortune Plus.dat`) and BIOS (`bios/naomi.zip`) are NEVER committed or uploaded (gitignored). `tools/` is NEVER committed (gitignored).
- Flycast launches use an ABSOLUTE ROM path: `/Users/captainkoffski/AntigravityProjects/cleopatra/Cleopatra Fortune Plus.dat` (relative paths fail with `Cannot stat`).
- The Flycast source clone lives at `tools/flycast-src/`, pinned at commit `f09d1f22ef8d199b8b7a2395d0b46774e08a58c2`. Its BIOS is at `~/Library/Application Support/Flycast/data/naomi.zip` (already installed in Phase 1).
- The dump is already decrypted: never set the DMA decrypt bit (offset bit 30); record it, do not act on it.
- Guest hardware addresses are fixed by `docs/kb/naomi-vs-dreamcast.md §3/§4` — cart regs `0x5f7000`-`0x5f7014`, G1 DMA channel `0x5f7400` (`SB_GDSTAR/GDLEN/GDDIR/GDEN/GDST`).
- Every hardware claim added to the KB carries a citation; emulator source outranks wikis. Record the Flycast source-build in `docs/kb/tooling.md`.
- No autonomous screen capture (macOS Screen Recording permission was declined by the user). Launching and killing the emulator is fine; visual confirmation comes from the user.
- The instrumentation patch is saved in-repo as `patches/flycast-instrument.diff` so the build is reproducible from a fresh clone.

## Log line formats (the contract every task shares)

The instrumented build emits these lines (unique prefixes so the parser greps them; all written by the `cartlog()` helper to the file named by `$FLYCAST_CARTLOG`, default `flycast-cartlog.txt` in the launch CWD):

```
CARTDMA src=%08x dest=%08x len=%x        # cart→RAM DMA: src=cart byte offset, dest=phys RAM addr, len=bytes
CARTPIO offset=%08x                      # PIO seek: cart byte offset the game set before ROM_DATA reads
WATERMARK region=%s used=%x size=%x      # region in {main,vram,aram}; used=highest non-zero byte+1
JVSREPORT buttons=%04x                   # JVS button word (active-low: a CLEARED bit = pressed)
SERIALPOKE addr=%08x data=%08x           # write to a NAOMI_COMM_* serial/network register
```

Field semantics used by the parser:
- `src` = `DmaOffset & 0x1fffffff` — byte offset into `Cleopatra Fortune Plus.dat`.
- `dest` = `SB_GDSTAR & 0x1FFFFFE0` — physical main-RAM address (`0x0c000000`-`0x0dffffff`).
- `len` = `SB_GDLEN` — a whole number of `0x20`-byte units (bytes).

---

## File Structure

- `tools/flycast-src/core/hw/naomi/cartlog.h` (create) — declares `void cartlog(const char *fmt, ...)`.
- `tools/flycast-src/core/hw/naomi/cartlog.cpp` (create) — the helper: lazy-opens the log file, `fprintf` + flush.
- `tools/flycast-src/core/hw/naomi/CMakeLists.txt` (modify) — add `cartlog.cpp cartlog.h` to sources.
- `tools/flycast-src/core/hw/naomi/naomi.cpp` (modify) — CARTDMA + WATERMARK in the DMA path; SERIALPOKE in `WriteMem_naomi`.
- `tools/flycast-src/core/hw/naomi/naomi_cart.h` (modify) — `GetDmaSrcOffset()` accessor.
- `tools/flycast-src/core/hw/naomi/naomi_cart.cpp` (modify) — CARTPIO in `WriteMem`.
- `tools/flycast-src/core/hw/maple/maple_devs.cpp` (modify) — JVSREPORT in the JVS button path.
- `patches/flycast-instrument.diff` (create) — saved diff of the above.
- `scripts/parse_cart_log.py` (create) — parser + self-checks.
- `scripts/test_parse_cart_log.py` (create) — parser tests.
- `docs/kb/cart-streaming-map.md` + `docs/kb/cart-streaming-map.csv` (create) — the map.
- `docs/kb/input-map.md` (create) — JVS bit → control.
- `docs/kb/phase2-measurements.md` (create) — RAM verdict + serial/watchdog verdict.
- `docs/kb/tooling.md`, `docs/kb/00-status.md`, `docs/kb/naomi-vs-dreamcast.md` (modify) — integrate findings.

---

## Task 1: Build the instrumented Flycast

**Files:**
- Create: `tools/flycast-src/core/hw/naomi/cartlog.h`, `tools/flycast-src/core/hw/naomi/cartlog.cpp`
- Modify: `tools/flycast-src/core/hw/naomi/CMakeLists.txt`, `tools/flycast-src/core/hw/naomi/naomi.cpp`, `tools/flycast-src/core/hw/naomi/naomi_cart.h`, `tools/flycast-src/core/hw/naomi/naomi_cart.cpp`, `tools/flycast-src/core/hw/maple/maple_devs.cpp`
- Create: `patches/flycast-instrument.diff`

**Interfaces:**
- Produces: an instrumented Flycast binary that, when it runs this ROM, writes CARTDMA/CARTPIO/WATERMARK/JVSREPORT/SERIALPOKE lines (formats above) to `$FLYCAST_CARTLOG`. Task 2's parser consumes those exact formats.

- [ ] **Step 1: Initialize submodules and prove the toolchain builds (baseline, unpatched)**

The clone at `tools/flycast-src/` (commit `f09d1f22ef8d199b8b7a2395d0b46774e08a58c2`) has no submodules yet. Flycast bundles its dependencies (SDL, etc.) as submodules.

```bash
cd tools/flycast-src
git submodule update --init --recursive
# Install cmake if missing (Homebrew, no sudo):
command -v cmake >/dev/null || brew install cmake
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Expected: a build completes and produces a runnable app bundle (path is either `build/flycast.app` or `build/Flycast.app` — confirm with `find build -maxdepth 2 -name '*.app'`). If a dependency is missing, `brew install` it and record it in `docs/kb/tooling.md` (Step 8). Record the exact working build command sequence as you go.

- [ ] **Step 2: Create the logging helper `cartlog.h`**

```cpp
// core/hw/naomi/cartlog.h
// Phase 2 instrumentation (Cleopatra Naomi->DC port). Not upstream.
#pragma once
void cartlog(const char *fmt, ...);
```

- [ ] **Step 3: Create the logging helper `cartlog.cpp`**

Writes to `$FLYCAST_CARTLOG` (default `flycast-cartlog.txt` in CWD), truncating once per process, flushing each line so a killed run still yields a complete log.

```cpp
// core/hw/naomi/cartlog.cpp
// Phase 2 instrumentation (Cleopatra Naomi->DC port). Not upstream.
#include "cartlog.h"
#include <cstdio>
#include <cstdlib>
#include <cstdarg>

void cartlog(const char *fmt, ...)
{
	static FILE *f = nullptr;
	if (f == nullptr)
	{
		const char *path = getenv("FLYCAST_CARTLOG");
		f = fopen(path != nullptr ? path : "flycast-cartlog.txt", "w");
		if (f == nullptr)
			return; // ponytail: if the log file won't open, stay silent rather than crash the emu
	}
	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fflush(f);
}
```

- [ ] **Step 4: Register the helper in CMake**

In `core/hw/naomi/CMakeLists.txt`, add `cartlog.cpp` and `cartlog.h` to the target's source list (match the existing list's `target_sources(... PRIVATE ...)` or `add_library` form — the other `.cpp` files in that directory show the exact syntax). Example addition alongside the existing entries:

```cmake
	cartlog.cpp
	cartlog.h
```

- [ ] **Step 5: Add the cart source-offset accessor in `naomi_cart.h`**

The base `Cartridge` class (around line 50) gets a virtual returning 0; `NaomiCartridge` (around line 82, which holds `u32 DmaOffset;` at ~line 104) overrides it.

In `class Cartridge`, add among the public virtuals (near `GetDmaPtr`):

```cpp
	// Phase 2 instrumentation: current cart byte offset for a DMA read (0 if N/A)
	virtual u32 GetDmaSrcOffset() const { return 0; }
```

In `class NaomiCartridge`, add (near the other overrides such as `GetDmaPtr`):

```cpp
	u32 GetDmaSrcOffset() const override { return DmaOffset & 0x1fffffff; }
```

- [ ] **Step 6: Emit CARTDMA + periodic WATERMARK in `naomi.cpp`**

At the top of `core/hw/naomi/naomi.cpp`, add the includes needed for the log helper and the RAM buffers:

```cpp
#include "cartlog.h"
#include "hw/mem/addrspace.h"   // RAM, ERAM_SIZE (confirm this header exports them; grep for `extern.*RAM` / ERAM_SIZE)
#include "hw/pvr/pvr_mem.h"     // vram, VRAM_SIZE
#include "hw/aica/aica_if.h"    // aica::aica_ram, ARAM_SIZE
```

(If `RAM`/`ERAM_SIZE` are declared elsewhere, use that header — `grep -rn "ERAM_SIZE" core/hw` in the source finds the declaring header; `core/hw/mem/mem_watch.cpp` uses `RAM`, `ERAM_SIZE`, `vram`, `VRAM_SIZE`, `aica::aica_ram`, `ARAM_SIZE`, so its includes are the reference set.)

Add a backwards-scan watermark helper as a file-static above `Naomi_DmaStart`:

```cpp
// Phase 2 instrumentation: highest non-zero byte in a buffer (backwards scan,
// stops at the water line). ponytail: over-reports if stale non-zero data sits
// high in the region — a conservative upper bound, fine for the cut decision.
static u32 cartlog_high(const u8 *buf, u32 size)
{
	for (u32 i = size; i-- > 0; )
		if (buf[i] != 0)
			return i + 1;
	return 0;
}

static void cartlog_watermarks()
{
	cartlog("WATERMARK region=main used=%x size=%x\n", cartlog_high(RAM, ERAM_SIZE), ERAM_SIZE);
	cartlog("WATERMARK region=vram used=%x size=%x\n", cartlog_high(&vram[0], VRAM_SIZE), VRAM_SIZE);
	cartlog("WATERMARK region=aram used=%x size=%x\n", cartlog_high(&aica::aica_ram[0], ARAM_SIZE), ARAM_SIZE);
}
```

In `Naomi_DmaStart`, inside the `CurrentCartridge` branch (right after the existing `DEBUG_LOG(NAOMI, "NAOMI-DMA start addr %08X len %x", SB_GDSTAR, SB_GDLEN);` line), add:

```cpp
		cartlog("CARTDMA src=%08x dest=%08x len=%x\n",
				CurrentCartridge->GetDmaSrcOffset(), SB_GDSTAR & 0x1FFFFFE0, SB_GDLEN);
		static u32 cartlog_dma_count = 0;
		if ((cartlog_dma_count++ & 63) == 0)   // ponytail: every 64th DMA; the scan is cheap but not free
			cartlog_watermarks();
```

- [ ] **Step 7: Emit CARTPIO in `naomi_cart.cpp` and SERIALPOKE in `naomi.cpp`**

At the top of `core/hw/naomi/naomi_cart.cpp` add `#include "cartlog.h"`. In `NaomiCartridge::WriteMem`, in the `NAOMI_ROM_OFFSETL_addr` case (around line 1015, after `RomPioOffset |= data;`), add:

```cpp
		cartlog("CARTPIO offset=%08x\n", RomPioOffset & 0x1fffffff);
```

In `core/hw/naomi/naomi.cpp` `WriteMem_naomi`, log serial/network register writes. Add, immediately inside the function before the `m3comm` branch:

```cpp
	if (address >= NAOMI_COMM_CTRL_addr && address <= NAOMI_COMM_STATUS2_addr)
		cartlog("SERIALPOKE addr=%08x data=%08x\n", address, data);
```

- [ ] **Step 8: Emit JVSREPORT in `maple_devs.cpp`**

At the top of `core/hw/maple/maple_devs.cpp` add `#include "hw/naomi/cartlog.h"`. Find the Naomi JVS button-report site — `grep -n "getButtonState" core/hw/maple/maple_devs.cpp`; the report is emitted as `w16(getButtonState(pjs));` (around line 195, inside the JVS/JAMMA device `handle_86_15`/`transmit_data` path). Replace that one call with:

```cpp
			{
				u16 __jvs = getButtonState(pjs);
				cartlog("JVSREPORT buttons=%04x\n", __jvs);   // active-low: cleared bit = pressed
				w16(__jvs);
			}
```

(Confirm `getButtonState` returns the naomi JVS word and `pjs` is in scope at that site; it is the player-joystick struct used two lines above.)

- [ ] **Step 9: Rebuild and run an autonomous boot smoke test**

```bash
cd tools/flycast-src
cmake --build build -j"$(sysctl -n hw.ncpu)"
APP=$(find build -maxdepth 2 -name '*.app' | head -1)
BIN="$APP/Contents/MacOS/$(basename "$APP" .app)"
cd /tmp && FLYCAST_CARTLOG=/tmp/cleopatra-smoke.log "$BIN" \
  "/Users/captainkoffski/AntigravityProjects/cleopatra/Cleopatra Fortune Plus.dat" &
FLYPID=$!; sleep 25; kill "$FLYPID" 2>/dev/null; wait "$FLYPID" 2>/dev/null
echo "=== CARTDMA lines ==="; grep -c '^CARTDMA'   /tmp/cleopatra-smoke.log
echo "=== JVSREPORT lines ==="; grep -c '^JVSREPORT' /tmp/cleopatra-smoke.log
echo "=== beyond-boot cart reads (src >= 0x100000) ==="
grep '^CARTDMA' /tmp/cleopatra-smoke.log | awk -F'src=| dest' '{print strtonum("0x"$2)}' | awk '$1>=1048576' | wc -l
```

Expected: `CARTDMA` count > 0, `JVSREPORT` count > 0, and at least one beyond-boot cart read (attract mode streams demo assets past the boot 1 MB). If CARTDMA is 0, the DMA log point is wrong; if JVSREPORT is 0, the input log point is wrong. (Boot needs BIOS at `~/Library/Application Support/Flycast/data/naomi.zip` and region set as in Phase 1 — the launch may open a window; that is fine, no screenshot is taken.)

- [ ] **Step 10: Save the patch as a reproducible diff**

```bash
mkdir -p patches
git -C tools/flycast-src diff > patches/flycast-instrument.diff
# cartlog.{h,cpp} are new/untracked in the clone — include them so the diff is complete:
git -C tools/flycast-src add -N core/hw/naomi/cartlog.h core/hw/naomi/cartlog.cpp
git -C tools/flycast-src diff > patches/flycast-instrument.diff
wc -l patches/flycast-instrument.diff   # expect a small diff (tens of lines)
```

- [ ] **Step 11: Commit (repo files only — `tools/` is gitignored)**

```bash
git add patches/flycast-instrument.diff
git commit -m "Phase 2 Task 1: instrumented Flycast build (cart/RAM/JVS/serial logging)"
```

---

## Task 2: Cart-log parser + self-checks

**Files:**
- Create: `scripts/parse_cart_log.py`, `scripts/test_parse_cart_log.py`

**Interfaces:**
- Consumes: cartlog files with the line formats from Task 1.
- Produces: `parse(paths: list[str]) -> dict` with keys `dma` (list of `{src,dest,len}` ints, deduped, sorted by src), `pio` (sorted list of int offsets), `watermarks` (`{region: max_used}`), `serial` (list of `{addr,data}`), and `checks` (list of `(name, ok, detail)`); plus `write_csv(result, path)` and `write_summary(result) -> str`. CLI: `python3 scripts/parse_cart_log.py LOG [LOG ...] --csv OUT.csv`.

- [ ] **Step 1: Write the failing tests**

```python
# scripts/test_parse_cart_log.py
import subprocess, sys, textwrap, os
sys.path.insert(0, os.path.dirname(__file__))
import parse_cart_log as p

SAMPLE = textwrap.dedent("""\
    CARTDMA src=00000000 dest=0c020000 len=100000
    JVSREPORT buttons=ffff
    CARTDMA src=00200000 dest=0c400000 len=40000
    CARTDMA src=00200000 dest=0c400000 len=40000
    CARTPIO offset=00000450
    WATERMARK region=main used=a12340 size=2000000
    WATERMARK region=main used=b00000 size=2000000
    SERIALPOKE addr=5f7018 data=00000001
    JVSREPORT buttons=fdff
""")

def test_parse_dedups_and_sorts_dma():
    r = p.parse_text(SAMPLE)
    assert [d["src"] for d in r["dma"]] == [0x0, 0x200000]   # deduped, sorted
    assert r["dma"][1]["len"] == 0x40000

def test_pio_and_serial_and_watermark():
    r = p.parse_text(SAMPLE)
    assert r["pio"] == [0x450]
    assert r["serial"] == [{"addr": 0x5f7018, "data": 0x1}]
    assert r["watermarks"]["main"] == 0xb00000   # max over the two lines

def test_checks_pass_on_valid_sample():
    r = p.parse_text(SAMPLE)
    names = {name: ok for name, ok, _ in r["checks"]}
    assert names["dest_in_ram"] is True
    assert names["len_aligned_32"] is True
    assert names["beyond_boot_read"] is True   # src=0x200000 >= 0x100000

def test_check_flags_misaligned_len():
    bad = "CARTDMA src=00000000 dest=0c020000 len=100001\n"   # not a multiple of 0x20
    r = p.parse_text(bad)
    names = {name: ok for name, ok, _ in r["checks"]}
    assert names["len_aligned_32"] is False

def test_check_flags_dest_out_of_ram():
    bad = "CARTDMA src=00000000 dest=00000000 len=20\n"       # dest not in main RAM
    r = p.parse_text(bad)
    names = {name: ok for name, ok, _ in r["checks"]}
    assert names["dest_in_ram"] is False

def test_cli_writes_csv(tmp_path):
    log = tmp_path / "in.log"; log.write_text(SAMPLE)
    out = tmp_path / "out.csv"
    subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), "parse_cart_log.py"),
                    str(log), "--csv", str(out)], check=True)
    rows = out.read_text().strip().splitlines()
    assert rows[0] == "cart_offset,length,dest,mode"
    assert "0x00000000,0x100000,0x0c020000,DMA" in rows
    assert any(r.endswith(",PIO") for r in rows)
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python3 -m pytest scripts/test_parse_cart_log.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'parse_cart_log'` (or `AttributeError`).

- [ ] **Step 3: Implement the parser**

```python
#!/usr/bin/env python3
"""Parse Phase 2 cartlog files into the cart-streaming map + RAM/serial verdicts.

Line formats (from the instrumented Flycast, see the Phase 2 plan):
  CARTDMA src=%08x dest=%08x len=%x
  CARTPIO offset=%08x
  WATERMARK region=%s used=%x size=%x
  JVSREPORT buttons=%04x         (read by hand for the input map; ignored here)
  SERIALPOKE addr=%08x data=%08x
"""
import re
import sys

MAIN_RAM_LO, MAIN_RAM_HI = 0x0c000000, 0x0e000000   # naomi/DC main RAM area (physical)
BOOT_END = 0x100000                                  # 1 MB boot load; runtime streaming is past this

_DMA = re.compile(r"^CARTDMA src=([0-9a-fA-F]+) dest=([0-9a-fA-F]+) len=([0-9a-fA-F]+)")
_PIO = re.compile(r"^CARTPIO offset=([0-9a-fA-F]+)")
_WM = re.compile(r"^WATERMARK region=(\w+) used=([0-9a-fA-F]+) size=([0-9a-fA-F]+)")
_SER = re.compile(r"^SERIALPOKE addr=([0-9a-fA-F]+) data=([0-9a-fA-F]+)")


def parse_text(text):
    dma_seen, dma = set(), []
    pio = set()
    watermarks = {}
    serial = []
    for line in text.splitlines():
        m = _DMA.match(line)
        if m:
            src, dest, length = (int(g, 16) for g in m.groups())
            key = (src, dest, length)
            if key not in dma_seen:
                dma_seen.add(key)
                dma.append({"src": src, "dest": dest, "len": length})
            continue
        m = _PIO.match(line)
        if m:
            pio.add(int(m.group(1), 16))
            continue
        m = _WM.match(line)
        if m:
            region, used, _size = m.group(1), int(m.group(2), 16), int(m.group(3), 16)
            watermarks[region] = max(watermarks.get(region, 0), used)
            continue
        m = _SER.match(line)
        if m:
            serial.append({"addr": int(m.group(1), 16), "data": int(m.group(2), 16)})
    dma.sort(key=lambda d: (d["src"], d["dest"]))
    return {
        "dma": dma,
        "pio": sorted(pio),
        "watermarks": watermarks,
        "serial": serial,
        "checks": _checks(dma),
    }


def _checks(dma):
    checks = []
    dest_ok = all(MAIN_RAM_LO <= d["dest"] < MAIN_RAM_HI for d in dma) if dma else False
    checks.append(("dest_in_ram", dest_ok,
                   "every DMA dest in main RAM 0x0c000000-0x0dffffff"))
    len_ok = all(d["len"] % 0x20 == 0 for d in dma) if dma else False
    checks.append(("len_aligned_32", len_ok, "every DMA len is a multiple of 0x20"))
    beyond = any(d["src"] >= BOOT_END for d in dma)
    checks.append(("beyond_boot_read", beyond,
                   "at least one cart read with src >= 0x100000 (runtime streaming)"))
    return checks


def parse_files(paths):
    text = "\n".join(open(p, encoding="utf-8", errors="replace").read() for p in paths)
    return parse_text(text)


def write_csv(result, path):
    with open(path, "w", encoding="utf-8") as f:
        f.write("cart_offset,length,dest,mode\n")
        for d in result["dma"]:
            f.write(f"0x{d['src']:08x},0x{d['len']:x},0x{d['dest']:08x},DMA\n")
        for off in result["pio"]:
            f.write(f"0x{off:08x},0x0,0x00000000,PIO\n")


def write_summary(result):
    lines = [f"DMA requests (unique): {len(result['dma'])}",
             f"PIO seeks (unique): {len(result['pio'])}"]
    if result["dma"]:
        lines.append(f"cart offset range: 0x{result['dma'][0]['src']:08x}"
                     f"..0x{max(d['src'] + d['len'] for d in result['dma']):08x}")
        main_hi = max((d["dest"] + d["len"] for d in result["dma"]
                       if MAIN_RAM_LO <= d["dest"] < MAIN_RAM_HI), default=0)
        lines.append(f"main-RAM DMA high-water (dest+len): 0x{main_hi:08x} "
                     f"({main_hi / 1048576:.1f} MB) vs DC 16 MB")
    for region in ("main", "vram", "aram"):
        if region in result["watermarks"]:
            lines.append(f"WATERMARK {region}: 0x{result['watermarks'][region]:08x} "
                         f"({result['watermarks'][region] / 1048576:.1f} MB)")
    lines.append(f"serial/network pokes: {len(result['serial'])}")
    for name, ok, detail in result["checks"]:
        lines.append(f"CHECK {name}: {'PASS' if ok else 'FAIL'} — {detail}")
    return "\n".join(lines)


def main(argv):
    paths, csv_out = [], None
    i = 0
    while i < len(argv):
        if argv[i] == "--csv":
            csv_out = argv[i + 1]; i += 2
        else:
            paths.append(argv[i]); i += 1
    if not paths:
        print("usage: parse_cart_log.py LOG [LOG ...] --csv OUT.csv", file=sys.stderr)
        return 2
    result = parse_files(paths)
    if csv_out:
        write_csv(result, csv_out)
    print(write_summary(result))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python3 -m pytest scripts/test_parse_cart_log.py -v`
Expected: PASS (6 passed). If pytest is unavailable: `python3 scripts/test_parse_cart_log.py` after appending a `pytest.main`-free `assert`-runner is NOT needed — install pytest with `python3 -m pip install pytest` and record it in tooling.md.

- [ ] **Step 5: Commit**

```bash
git add scripts/parse_cart_log.py scripts/test_parse_cart_log.py
git commit -m "Phase 2 Task 2: cartlog parser + self-checks"
```

---

## Task 3: Capture sessions + generate the KB data

**Files:**
- Create: `docs/kb/cart-streaming-map.md`, `docs/kb/cart-streaming-map.csv`, `docs/kb/input-map.md`, `docs/kb/phase2-measurements.md`

**Interfaces:**
- Consumes: the instrumented build (Task 1) and the parser (Task 2).
- Produces: the four KB data files. This task requires the user for the play and input passes (the attract pass is autonomous).

> **Note for the controller:** this task is user-interactive, like the Phase 1 boot confirmation. The subagent prepares the exact launch commands and parser invocation and hands them to the controller; the controller runs the attract pass autonomously, then asks the user to perform the play pass and the input pass, collects the logs, and runs the parser. Do not fabricate capture data — if a pass is not run, record it as a known gap (iterative coverage, per the spec).

- [ ] **Step 1: Attract pass (autonomous)**

```bash
APP=$(find tools/flycast-src/build -maxdepth 2 -name '*.app' | head -1)
BIN="$APP/Contents/MacOS/$(basename "$APP" .app)"
FLYCAST_CARTLOG="$PWD/capture-attract.log" "$BIN" \
  "/Users/captainkoffski/AntigravityProjects/cleopatra/Cleopatra Fortune Plus.dat" &
FLYPID=$!; sleep 90; kill "$FLYPID" 2>/dev/null; wait "$FLYPID" 2>/dev/null
```

Let attract mode loop once (title → demo → how-to-play → high scores). 90 s covers a full attract loop.

- [ ] **Step 2: Play pass (user)**

Ask the user to launch with a fresh log and play the early stages plus a game-over (the iterative depth chosen in the spec):

```bash
FLYCAST_CARTLOG="$PWD/capture-play.log" "$BIN" \
  "/Users/captainkoffski/AntigravityProjects/cleopatra/Cleopatra Fortune Plus.dat"
```

Controls (from `docs/kb/game.md`): Enter = Start, arrows = directions, X = Button 1, C = Button 2.

- [ ] **Step 3: Input pass (user)**

Ask the user to launch with a fresh log and, from attract or the game, press each control once in this order, holding ~1 s with a gap between: Start, Up, Down, Left, Right, Button 1 (X), Button 2 (C), then insert Coin, then Test/Service.

```bash
FLYCAST_CARTLOG="$PWD/capture-input.log" "$BIN" \
  "/Users/captainkoffski/AntigravityProjects/cleopatra/Cleopatra Fortune Plus.dat"
```

- [ ] **Step 4: Generate the cart-streaming map (CSV + summary)**

```bash
python3 scripts/parse_cart_log.py capture-attract.log capture-play.log \
  --csv docs/kb/cart-streaming-map.csv
```

Expected: the printed summary ends with `CHECK dest_in_ram: PASS`, `CHECK len_aligned_32: PASS`, `CHECK beyond_boot_read: PASS`. If any check FAILs, stop and diagnose (a FAIL means the capture or the instrumentation is wrong — do not write the map from bad data).

- [ ] **Step 5: Write `docs/kb/cart-streaming-map.md`**

Human summary wrapping the CSV. Include: the parser summary output (paste the real numbers), which passes contributed (attract + play; note any un-visited content as a known iterative gap), and this reading guide:

```markdown
# Cart streaming map (Phase 2 capture)

The runtime cart reads this game issues, captured from the instrumented
Flycast (`patches/flycast-instrument.diff`). Machine-readable rows are in
`cart-streaming-map.csv` (`cart_offset,length,dest,mode`). Each DMA row is a
`(cart byte offset, length, physical RAM dest)` request that Phase 4 reissues
as a GD-ROM read. `mode=PIO` rows are small scattered seeks via the ROM_DATA
port (length not tracked).

- Boot load (from the header, not the runtime log): cart 0x0 -> RAM
  0x8c020000, 0x100000 bytes (`docs/kb/game.md`).
- Coverage: <attract + play passes; list stages/screens reached>. Un-visited
  content is a known gap — top up by replaying with logging on and re-running
  the parser over the added log (the CSV dedups on re-merge).
- Verification: all three parser self-checks PASS (see summary below).

<paste parser summary here>
```

- [ ] **Step 6: Write `docs/kb/input-map.md` from the input-pass JVS lines**

Read the JVSREPORT lines from `capture-input.log` (`grep '^JVSREPORT' capture-input.log`). Buttons are active-low (a cleared bit = pressed). Cross-reference the bit positions defined in Flycast `core/hw/maple/maple_devs.h:77-98` and confirm each held control cleared the expected bit:

```markdown
# Input map (Phase 2 capture)

JVS button word as this game reads it, confirmed by pressing each control and
watching which bit the instrumented build logged (active-low: cleared = pressed).
Bit positions match Flycast's `NAOMI_*_KEY` constants
(`tools/flycast-src/core/hw/maple/maple_devs.h:77-98`).

| Control | Bit | Confirmed word (pressed) |
|---|---|---|
| Start | 15 | <e.g. 0x7fff> |
| Up | 13 | ... |
| Down | 12 | ... |
| Left | 11 | ... |
| Right | 10 | ... |
| Button 1 (X, rotate CCW / select) | 9 | ... |
| Button 2 (C, rotate CW) | 8 | ... |
| Service/Test | 14 / 18 | ... |
| Coin | 19 | ... |

Note: Test (bit 18) and Coin (bit 19) fall outside the low 16-bit word — they
appear in a separate JVS byte; record the word/byte the build logged for them.
This resolves `naomi-vs-dreamcast.md §8-2` for the 7 gameplay controls.
```

If a control did not flip exactly one expected bit, record the actual observation — do not force it to match.

- [ ] **Step 7: Write `docs/kb/phase2-measurements.md` (RAM + serial verdict)**

```markdown
# Phase 2 measurements — RAM footprint and serial/watchdog

## RAM footprint vs Dreamcast capacity

Method: main-RAM high-water is `max(dest+len)` over cart DMAs landing in main
RAM (trustworthy — actual asset placement); VRAM/sound-RAM figures are the
`WATERMARK` backwards-scan (highest non-zero byte — a conservative upper bound;
stale high data can inflate it). Numbers from the parser summary:

| Region | Measured peak | DC capacity | Cuts needed? |
|---|---|---|---|
| Main RAM | <X MB> | 16 MB | <yes/no/marginal> |
| Video RAM | <X MB> | 8 MB | <yes/no/marginal> |
| Sound RAM | <X MB> | 2 MB | <yes/no/marginal> |

Verdict feeds Phase 5. If a region is marginal against its DC size, the
interpreter-mode store-watermark fallback (spec §"RAM watermark", method 2) can
tighten the number before committing to asset cuts.

## Serial / watchdog (resolves §8-4)

Serial/network pokes captured (`SERIALPOKE` count): <N>. <If 0: the game does
not touch the NAOMI_COMM_* serial/network registers — no shim needed. If >0:
list the addresses and note Phase 4 must no-op them.> Watchdog: no dedicated
NAOMI_COMM register; any watchdog access would surface as an unknown-register
write in Flycast's NAOMI debug log — <observed / not observed>.
```

- [ ] **Step 8: Commit the KB data**

```bash
git add docs/kb/cart-streaming-map.md docs/kb/cart-streaming-map.csv \
        docs/kb/input-map.md docs/kb/phase2-measurements.md
git commit -m "Phase 2 Task 3: cart-streaming map, input map, RAM/serial measurements"
```

---

## Task 4: Integrate findings into the KB and advance status

**Files:**
- Modify: `docs/kb/naomi-vs-dreamcast.md`, `docs/kb/00-status.md`, `docs/kb/tooling.md`

**Interfaces:**
- Consumes: the Task 3 data files.

- [ ] **Step 1: Resolve the open questions in `naomi-vs-dreamcast.md §8`**

For §8-1 (cart streaming), §8-2 (input bits), and §8-4 (watchdog/serial): change each from "Resolves in: Phase 2" to a one-line resolution pointing at the new data file (e.g., "RESOLVED Phase 2 — see `docs/kb/cart-streaming-map.md`"). Leave §8-3 (BIOS calls) and §8-5 (VRAM population) as Phase 3/5. If the measured RAM peaks settle the §1 "must confirm working-set fits" note, update that cell to cite `phase2-measurements.md`.

- [ ] **Step 2: Record the Flycast source-build in `docs/kb/tooling.md`**

Add a "Flycast — source build (Phase 2 instrumentation)" section: the pinned commit `f09d1f22ef8d199b8b7a2395d0b46774e08a58c2`, the `git submodule update --init --recursive` + `cmake -B build -DCMAKE_BUILD_TYPE=Release` + `cmake --build build` recipe (with any `brew install` deps discovered in Task 1), the app-bundle path, the `patches/flycast-instrument.diff` apply step (`git -C tools/flycast-src apply ../../patches/flycast-instrument.diff` from repo root — adjust relative path), the `FLYCAST_CARTLOG` env var, and the log line formats.

- [ ] **Step 3: Advance `docs/kb/00-status.md`**

Mark Phase 2 done (dated), set Phase 3 as NEXT, and update the "Next step" line to: "Brainstorm and spec Phase 3 (reverse engineering): Ghidra on the 1 MB boot binary — confirm no BIOS `jsr` after the entrypoint (§8-3), and locate the cart-read and input-decode functions the Phase 4 patches target, cross-referenced against the Phase 2 cart-streaming map and input map." Add the key Phase 2 facts (cart-map exists, RAM verdict, input map) to "Key facts so far."

- [ ] **Step 4: Commit**

```bash
git add docs/kb/naomi-vs-dreamcast.md docs/kb/00-status.md docs/kb/tooling.md
git commit -m "Phase 2 Task 4: integrate findings, advance status to Phase 3"
```

---

## Self-Review

**Spec coverage:**
- Target 1 (cart streaming map) → Task 1 (CARTDMA/CARTPIO), Task 2 (parser/CSV), Task 3 (map). ✓
- Target 2 (RAM watermark) → Task 1 (WATERMARK + CARTDMA-derived), Task 2 (summary), Task 3 (`phase2-measurements.md`). ✓
- Target 3 (input bit map) → Task 1 (JVSREPORT), Task 3 (`input-map.md`). ✓
- Target 4 (watchdog/serial) → Task 1 (SERIALPOKE), Task 3 (verdict). ✓
- Capture protocol (attract/play/input, iterative) → Task 3 steps 1-3. ✓
- Deliverables (build, diff, parser, KB docs, tooling/status) → Tasks 1-4. ✓
- Verification (dest-in-RAM, 32-aligned, beyond-boot, one-bit input) → Task 2 checks + Task 1 smoke + Task 3 step 6. ✓
- Exit criteria 1-6 → Tasks 2-4. ✓

**Placeholder scan:** the `<...>` angle-bracket spots in Task 3/4 are real capture values that only exist after the user plays — they are intentional fill-from-data fields, not unspecified logic. All code steps carry complete code.

**Type consistency:** `cartlog(const char*, ...)` used identically in all four TUs; `GetDmaSrcOffset()` declared in base, overridden in `NaomiCartridge`; parser keys (`dma`/`pio`/`watermarks`/`serial`/`checks`) match between `parse_text`, `write_csv`, `write_summary`, and the tests; CSV header `cart_offset,length,dest,mode` matches the test assertion.
