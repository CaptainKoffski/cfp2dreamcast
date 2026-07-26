# VMU-Safety Tripwire Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deterministic checks that the port never writes a VMU: a static Maple-literal scan over every executable surface on the disc, plus a Flycast VMU-canary test with unattended (attract) and headed (play) modes.

**Architecture:** Layer 2 is a stdlib-only Python script asserting a measured literal baseline over the full cart, the BIOS-library slices, and the loader's own objects. Layers 1/3 are one POSIX-sh script: seed a temp VMU dir (canaries + all-zero control), run `build/disc.gdi` in Flycast with transient CLI config, hash after exit. Spec: `docs/superpowers/specs/2026-07-26-vmu-safety-design.md`.

**Tech Stack:** Python 3 stdlib, POSIX sh, instrumented Flycast at `tools/flycast-src/build/Flycast.app/Contents/MacOS/Flycast`, `shasum`, `nm`, `osascript`.

## Global Constraints

- Python scripts: stdlib only (project pattern, e.g. `scripts/bmp2rgb565.py`).
- Shell scripts: `#!/bin/sh` POSIX, like `scripts/capture.sh`.
- Never commit ROM/BIOS bytes. The baselines hold only file offsets + SH-4 MMIO register addresses (`0xa05f6cxx`) — hardware constants already cited throughout `docs/kb/`, not copyrighted content. Do not embed actual ROM byte runs.
- `emu.cfg` must never be mutated: all Flycast config goes through transient `-config section:key=value` CLI flags (capture.sh precedent).
- Flycast launch gotchas (from `docs/kb/tooling.md`): absolute disc path, `defaults write com.flyinghead.Flycast ApplePersistenceIgnoreState -bool YES`, `-config config:rend.vsync=no`, `pkill -9 -f "flycast-src.*Flycast"` before launching.
- Hardware/behavioral claims in comments carry citations (project CLAUDE.md rule).

---

### Task 1: Static Maple-literal scan (`scripts/test_maple_literals.py`)

**Files:**
- Create: `scripts/test_maple_literals.py`

**Interfaces:**
- Consumes: `Cleopatra Fortune Plus.dat` (repo root), `build/bios_data.bin`, `loader/main.o`, `loader/handoff.o` — all present after a normal `make disc`.
- Produces: executable script, exit 0 = all baselines match, exit 1 = any drift. Task 3 wires it into `make test` as `python3 scripts/test_maple_literals.py`.

- [ ] **Step 1: Write the script** (self-test runs first on every invocation — the detector proves itself on planted data before asserting the negative result on real data):

```python
#!/usr/bin/env python3
"""VMU-safety static tripwire (spec: docs/superpowers/specs/2026-07-26-vmu-safety-design.md).

A VMU is only reachable via Maple-bus frames; game code reaches the Maple
DMA registers through u32 literals in the block 0x5f6c00-0x5f6cff (any
P0/P1/P2 mirror). This scan asserts the set of such literals in every
executable byte source on the disc exactly matches the measured baseline:

  - full cart image (boot 1 MB mirrored 4x below 0x800000 + streamed rest)
  - build/bios_data.bin (Naomi BIOS library slices, executable via thunks)
  - loader/main.o + handoff.o (our loader code: zero vmu/maple references;
    the KOS libs linked into loader.elf legitimately contain both, and are
    covered by the dynamic canary test instead)

The shim (shims/src/maple.c) is excluded by design: it is the one authorized
Maple user, TX limited to DEVICE REQUEST + GETCOND to main devices.

Any new/changed hit fails the build. Classify it FIRST (patch it or prove it
dead -- scripts/ghidra FindMmioXrefs.java gives xrefs), then update the
baseline. Same failure class as the 19 unpatched G1 0x5f7xxx literals of HW
round 10 (docs/kb/00-status.md).
"""
import pathlib, struct, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CART = ROOT / "Cleopatra Fortune Plus.dat"
BIOS_DATA = ROOT / "build" / "bios_data.bin"
LOADER_OBJS = [ROOT / "loader" / "main.o", ROOT / "loader" / "handoff.o"]

STREAM_FLOOR = 0x800000    # first streamed cart offset (docs/kb/cart-streaming-map.md)
MIRROR_STRIDE = 0x200000   # boot region repeats 4x below STREAM_FLOOR (measured 2026-07-26)
BASE_VA = 0x8C020000       # cart offset 0 loads here (docs/kb/game.md)

# (cart offset in mirror 0, literal value). Classification (spec + KB):
#   0x010fec            engine maple base -- repointed to shim mirror by the patch table
#   0x060a00..0x060e90  settings/EEPROM BIOS-library region -- entry thunks stubbed
#   0x083830..0x083fcc  second embedded maple-driver copy -- dead in all Phase 2/3 captures
BOOT_HITS = [
    (0x010FEC, 0xA05F6C00),
    (0x060A00, 0xA05F6C14), (0x060B40, 0xA05F6C04), (0x060BE0, 0xA05F6C04),
    (0x060D0C, 0xA05F6C04), (0x060E74, 0xA05F6C14), (0x060E7C, 0xA05F6C8C),
    (0x060E84, 0xA05F6C80), (0x060E88, 0xA05F6C10), (0x060E8C, 0xA05F6C04),
    (0x060E90, 0xA05F6C18),
    (0x083830, 0xA05F6C04), (0x083838, 0xA05F6C10), (0x083840, 0xA05F6C14),
    (0x083848, 0xA05F6C80), (0x083850, 0xA05F6C8C), (0x083858, 0xA05F6CE8),
    (0x0839B0, 0xA05F6C18), (0x083FC8, 0xA05F6C04), (0x083FCC, 0xA05F6C10),
]
CART_BASELINE = {(m * MIRROR_STRIDE + off, v) for m in range(4) for off, v in BOOT_HITS}
BIOS_DATA_BASELINE = {(0x14D4, 0xA05F6C18)}   # SB_MDST in the 0x60000 library slice

def scan(data):
    """Aligned u32 literals with (v & 0x1fffff00) == 0x005f6c00.
    find()-driven: every candidate contains the byte pair 6c 5f at u32
    bytes 1-2 (LE layout lo,6c,5f,hi) -- ~1 s over the 109 MB cart."""
    hits = set()
    pos = data.find(b"\x6c\x5f")
    while pos != -1:
        off = pos - 1
        if off >= 0 and off % 4 == 0 and off + 4 <= len(data):
            v = struct.unpack_from("<I", data, off)[0]
            if (v & 0x1FFFFF00) == 0x005F6C00:
                hits.add((off, v))
        pos = data.find(b"\x6c\x5f", pos + 1)
    return hits

def selftest():
    planted = b"\0" * 4 + struct.pack("<I", 0xA05F6C18) + b"\0" * 8
    assert scan(planted) == {(4, 0xA05F6C18)}, "self-test: planted literal missed"
    assert scan(b"\0" * 16) == set(), "self-test: false hit on zeros"
    assert scan(b"\0" + planted) == set(), "self-test: unaligned literal must not match"

def check(name, got, want):
    if got == want:
        print(f"OK   {name}: {len(got)} literals match baseline")
        return True
    for off, v in sorted(want - got):
        print(f"FAIL {name}: baseline literal GONE  off 0x{off:07x} = 0x{v:08x}")
    for off, v in sorted(got - want):
        print(f"FAIL {name}: NEW maple literal     off 0x{off:07x} = 0x{v:08x}"
              f"  (VA 0x{BASE_VA + (off % MIRROR_STRIDE):08x} if boot code)")
    print("     classify before touching the baseline (patch or prove dead;"
          " scripts/ghidra FindMmioXrefs.java) -- see the spec")
    return False

def main():
    selftest()
    for p in (CART, BIOS_DATA, *LOADER_OBJS):
        if not p.exists():
            sys.exit(f"missing {p} -- ROM at repo root + a normal 'make disc' first")
    ok = True
    cart_hits = scan(CART.read_bytes())
    ok &= check("cart", cart_hits, CART_BASELINE)
    streamed = {h for h in cart_hits if h[0] >= STREAM_FLOOR}
    if streamed:
        ok = False
        for off, v in sorted(streamed):
            print(f"FAIL streamed region: maple literal off 0x{off:07x} = 0x{v:08x}")
    else:
        print("OK   streamed region (>= 0x800000): zero maple literals")
    ok &= check("bios_data.bin", scan(BIOS_DATA.read_bytes()), BIOS_DATA_BASELINE)
    nm = subprocess.run(["nm", *map(str, LOADER_OBJS)],
                        capture_output=True, text=True, check=True)
    bad = [l for l in nm.stdout.splitlines()
           if "vmu" in l.lower() or "maple" in l.lower()]
    if bad:
        ok = False
        for l in bad:
            print(f"FAIL loader objects reference VMU/Maple: {l}")
    else:
        print("OK   loader main.o/handoff.o: no vmu/maple references")
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it against the real artifacts**

Run: `python3 scripts/test_maple_literals.py; echo "exit=$?"`
Expected: 4 `OK` lines (`cart: 80 literals`, `streamed region ... zero`, `bios_data.bin: 1 literals`, `loader ... no vmu/maple references`), `exit=0`.

- [ ] **Step 3: Control-cycle the detector on real data** — temporarily delete the `(0x083FCC, 0xA05F6C10)` line from `BOOT_HITS`, rerun.

Run: `python3 scripts/test_maple_literals.py; echo "exit=$?"`
Expected: 4 `FAIL cart: NEW maple literal` lines (one per mirror) and `exit=1`. Restore the line, rerun, confirm `exit=0` again.

- [ ] **Step 4: Commit**

```bash
git add scripts/test_maple_literals.py
git commit -m "Static VMU tripwire: maple-literal baseline scan"
```

---

### Task 2: VMU-canary test (`scripts/test_vmu_untouched.sh`, attract + play modes)

**Files:**
- Create: `scripts/test_vmu_untouched.sh` (mode `attract` = Layer 1, mode `play` = Layer 3)

**Interfaces:**
- Consumes: `build/disc.gdi` (from `make disc`), instrumented Flycast binary.
- Produces: `scripts/test_vmu_untouched.sh [attract|play] [secs]`, exit 0 = PASS, 1 = FAIL, 2 = usage. Task 3 wires `attract` into `make test-vmu` and `play` into `make test-vmu-play`.

- [ ] **Step 1: Write the script**

```sh
#!/bin/sh
# VMU-canary test (spec: docs/superpowers/specs/2026-07-26-vmu-safety-design.md).
#
#   attract [secs]  -- unattended: boot + attract, auto-quit after secs (default 90).
#   play            -- headed: tester plays as long as they like, quits Flycast;
#                      the longer/wider the session, the more paths observed.
#
# Oracle (Flycast source, tools/flycast-src):
#   - VMU flash writes hit the backing vmu_save_*.bin immediately
#     (core/hw/maple/maple_devs.cpp:679-707, MDCF_BlockWrite -> fwrite).
#   - Startup rewrites a VMU file ONLY if missing or all-zero (auto-format,
#     maple_devs.cpp:436-474). So: 0xA5 canaries must stay byte-identical,
#     and the all-zero control file MUST change -- proving the VMUPath
#     redirect, VMU attachment, and hash logic are wired (control test).
# The user's real VMU saves are never touched: everything runs in a temp dir
# via transient CLI config (emu.cfg is not mutated -- capture.sh precedent).
set -eu

MODE="${1:-attract}"
SECS="${2:-90}"
case "$MODE" in attract|play) ;; *) echo "usage: $0 [attract|play] [secs]" >&2; exit 2 ;; esac

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$REPO/tools/flycast-src/build/Flycast.app/Contents/MacOS/Flycast"
DISC="$REPO/build/disc.gdi"
[ -x "$BIN" ]  || { echo "ERROR: Flycast not built: $BIN" >&2; exit 1; }
[ -f "$DISC" ] || { echo "ERROR: disc not built (make disc): $DISC" >&2; exit 1; }

# Launch gotchas: stale instance wedges SH4 vmem; macOS relaunch modal blocks
# boot forever (docs/kb/tooling.md).
pkill -9 -f "flycast-src.*Flycast" 2>/dev/null || true
defaults write com.flyinghead.Flycast ApplePersistenceIgnoreState -bool YES 2>/dev/null || true
defaults write com.flyinghead.Flycast NSQuitAlwaysKeepsWindows -bool false 2>/dev/null || true

VMUDIR="$(mktemp -d /tmp/vmu-canary.XXXXXX)"    # no spaces: unquoted in $CFG below
head -c 131072 /dev/zero | tr '\0' '\245' > "$VMUDIR/canary.ref"   # 128 KB of 0xA5
for f in vmu_save_A1.bin vmu_save_A2.bin vmu_save_B2.bin; do
    cp "$VMUDIR/canary.ref" "$VMUDIR/$f"
done
head -c 131072 /dev/zero > "$VMUDIR/vmu_save_B1.bin"   # control: MUST get auto-formatted
REF_SUM="$(shasum -a 256 "$VMUDIR/canary.ref" | cut -d' ' -f1)"
ZERO_SUM="$(shasum -a 256 "$VMUDIR/vmu_save_B1.bin" | cut -d' ' -f1)"

# Transient config (cfg keys: tools/flycast-src/core/cfg/option.cpp:145,201-215,234,238):
# VMU dir redirect, no per-game VMU, no MapleLink physical VMU, vsync off
# (unfocused-window deadlock), controller+VMUs pinned on ports A and B.
CFG="-config config:Dreamcast.VMUPath=$VMUDIR -config config:PerGameVmu=no \
 -config config:UsePhysicalVmuMemory=no -config config:rend.vsync=no \
 -config input:device1=0 -config input:device1.1=1 -config input:device1.2=1 \
 -config input:device2=0 -config input:device2.1=1 -config input:device2.2=1"

echo "Mode: $MODE  VMU dir: $VMUDIR"
if [ "$MODE" = play ]; then
    echo "Play as long as you like; quit Flycast when done."
    "$BIN" $CFG "$DISC" || true
else
    "$BIN" $CFG "$DISC" &
    PID=$!
    echo "Attract run ${SECS}s (PID $PID)..."
    sleep "$SECS"
    # Graceful quit, NOT kill -9: VMU fwrites are stdio-buffered and only
    # guaranteed on-disk after clean fclose (maple_devs.cpp fullSave/BlockWrite
    # have no fflush) -- a SIGKILL could hide a small write = false PASS.
    osascript -e 'quit app "Flycast"' 2>/dev/null || true
    n=0
    while kill -0 "$PID" 2>/dev/null && [ "$n" -lt 20 ]; do sleep 1; n=$((n+1)); done
    if kill -0 "$PID" 2>/dev/null; then
        echo "WARN: graceful quit failed; SIGKILL (a buffered VMU write could be lost)"
        kill -9 "$PID" 2>/dev/null || true
    fi
    wait "$PID" 2>/dev/null || true
fi

FAIL=0
for f in vmu_save_A1.bin vmu_save_A2.bin vmu_save_B2.bin; do
    SUM="$(shasum -a 256 "$VMUDIR/$f" | cut -d' ' -f1)"
    if [ "$SUM" = "$REF_SUM" ]; then
        echo "OK   $f unchanged"
    else
        echo "FAIL $f WAS WRITTEN"
        FAIL=1
    fi
done
B1_SUM="$(shasum -a 256 "$VMUDIR/vmu_save_B1.bin" | cut -d' ' -f1)"
if [ "$B1_SUM" != "$ZERO_SUM" ]; then
    echo "OK   control B1 auto-formatted (harness wired)"
else
    echo "FAIL control B1 unchanged: VMUPath redirect NOT wired -- run proves nothing"
    FAIL=1
fi
if [ "$FAIL" = 0 ]; then
    echo "PASS: no VMU writes"
    rm -rf "$VMUDIR"
else
    echo "FAIL: kept $VMUDIR for forensics"
fi
exit "$FAIL"
```

Then: `chmod +x scripts/test_vmu_untouched.sh`

- [ ] **Step 2: Record that `emu.cfg` is untouchable-state before the run**

Run: `shasum -a 256 ~/Library/Application\ Support/Flycast/emu.cfg`
Expected: a hash line; note it for Step 4.

- [ ] **Step 3: Run attract mode end-to-end**

Run: `scripts/test_vmu_untouched.sh attract 90; echo "exit=$?"`
Expected: `OK` for A1/A2/B2 unchanged, `OK control B1 auto-formatted`, `PASS: no VMU writes`, `exit=0`. (The B1 line is the built-in detector control: the same hash-compare code path proving it notices change. If it FAILs instead, the redirect isn't wired — debug before trusting anything else.)

- [ ] **Step 4: Verify `emu.cfg` unchanged**

Run: `shasum -a 256 ~/Library/Application\ Support/Flycast/emu.cfg`
Expected: identical hash to Step 2.

- [ ] **Step 5: Verify play mode end-to-end by simulating the tester** (launch play mode, then quit Flycast from outside — the script must complete its assertions on its own):

Run:
```sh
scripts/test_vmu_untouched.sh play > /tmp/vmu-play-test.log 2>&1 &
sleep 45 && osascript -e 'quit app "Flycast"'
wait %1; echo "exit=$?"; cat /tmp/vmu-play-test.log
```
Expected: log ends with the same OK/OK/OK/OK/PASS block, `exit=0`.

- [ ] **Step 6: Commit**

```bash
git add scripts/test_vmu_untouched.sh
git commit -m "Dynamic VMU tripwire: canary test, attract + headed play modes"
```

---

### Task 3: Makefile wiring + KB notes

**Files:**
- Modify: `Makefile:23,43-44` (`.PHONY` list and `test` target; append two targets)
- Modify: `docs/kb/00-status.md` (Phase-5 closing items block)
- Modify: `docs/kb/tooling.md` (short harness note)
- Modify: `docs/kb/port-playbook.md` (release-checklist step)

**Interfaces:**
- Consumes: `python3 scripts/test_maple_literals.py` (exit 0/1), `scripts/test_vmu_untouched.sh [attract|play]` (exit 0/1/2) from Tasks 1–2.
- Produces: `make test` (shim tests + static scan), `make test-vmu`, `make test-vmu-play`.

- [ ] **Step 1: Edit `Makefile`** — change the `.PHONY` line and `test` target, append the two new targets:

```makefile
.PHONY: disc release deploy test test-vmu test-vmu-play clean
```

```makefile
test:
	$(MAKE) -C shims test
	python3 scripts/test_maple_literals.py

# VMU-safety canary runs (spec: docs/superpowers/specs/2026-07-26-vmu-safety-design.md):
# test-vmu = unattended 90 s attract; test-vmu-play = headed, tester plays then quits.
test-vmu:
	scripts/test_vmu_untouched.sh attract

test-vmu-play:
	scripts/test_vmu_untouched.sh play
```

- [ ] **Step 2: Run the fast suite**

Run: `make test; echo "exit=$?"`
Expected: shim host tests green as before, then the 4 static-scan `OK` lines, `exit=0`.

- [ ] **Step 3: KB notes.** In `docs/kb/00-status.md`, inside the "Phase-5 closing items" block, append:

```markdown
   **VMU-safety tripwire (2026-07-26):** three deterministic checks that the
   port never writes a VMU (spec:
   `docs/superpowers/specs/2026-07-26-vmu-safety-design.md`): `make test` now
   includes the static maple-literal baseline scan (full cart + BIOS slices +
   loader objects); `make test-vmu` = unattended Flycast canary run (0xA5
   canaries must survive, all-zero control must get auto-formatted);
   `make test-vmu-play` = same assertions after a headed tester-driven
   session (recommended pre-release).
```

In `docs/kb/tooling.md`, after the "Headless framebuffer → PNG screenshot" section, add:

```markdown
#### VMU-canary harness (2026-07-26)

`scripts/test_vmu_untouched.sh [attract|play]` — proves a run wrote no VMU:
seeds a temp dir (0xA5 canaries + all-zero control) and redirects Flycast
there via transient `-config config:Dreamcast.VMUPath=...` (emu.cfg never
touched), then hash-compares after exit. Attract mode quits Flycast
gracefully via `osascript` — VMU fwrites are stdio-buffered and only
guaranteed on disk after clean fclose (`core/hw/maple/maple_devs.cpp`, no
fflush), so `kill -9` could hide a write. Static sibling:
`scripts/test_maple_literals.py` (in `make test`).
```

In `docs/kb/port-playbook.md`, add to the pre-release/verification checklist (match the file's list style at the insertion point):

```markdown
- VMU safety: `make test` (static maple-literal baseline), `make test-vmu`
  (unattended canary), and a `make test-vmu-play` session covering settings,
  2P, game over, and high-score screens. All three must PASS.
```

- [ ] **Step 4: Commit**

```bash
git add Makefile docs/kb/00-status.md docs/kb/tooling.md docs/kb/port-playbook.md
git commit -m "Wire VMU tripwire into make test / test-vmu / test-vmu-play + KB notes"
```

---

## Self-Review Notes

- Spec coverage: Layer 2 all-surfaces scan = Task 1 (cart, streamed-region invariant, bios_data.bin, loader nm assert, shim exclusion documented in docstring); Layer 1 attract = Task 2 steps 1–4; Layer 3 play = Task 2 step 5; wiring + KB = Task 3. Non-goals untouched (no shim runtime guard, no KOS init-flag churn).
- The graceful-quit requirement (buffered stdio fwrite) is load-bearing for attract mode and is encoded in the script + tooling note.
- Baselines embed only offsets and MMIO addresses, no ROM bytes — commit-safe per the no-copyrighted-bytes rule.
