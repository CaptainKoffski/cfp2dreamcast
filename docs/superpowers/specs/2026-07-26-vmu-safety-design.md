# VMU-safety tripwire — design

**Date:** 2026-07-26. **Status:** approved scope (both layers), pending spec review.

## Goal

A Naomi game has no VMU concept, so the port must never write a VMU — the
worst realistic failure is corrupting a user's saves (never a brick, see
`docs/kb/naomi-vs-dreamcast.md` §"Hardware safety"). Add two deterministic,
re-runnable checks that keep this true through future patch churn and future
ports: a dynamic end-to-end test (primary) and a static literal scan (covers
paths the dynamic run never executes).

## Threat model (why the surface is small)

A VMU is only reachable via Maple-bus frames — flash writes are
`MDCF_BlockWrite` (cmd 12) with the storage function (0x2); no DC BIOS syscall
writes VMUs ([mc.pp.se syscalls](https://mc.pp.se/dc/syscalls.html)). In this
port:

- The game engine's sole live maple-base pool word `0x8c030fec` is repointed
  to the shim's RAM mirror (patch table; `docs/kb/phase4-conversion.md`
  §patch-sites), so engine Maple traffic never reaches hardware.
- The shim puts exactly two commands on the real wire: DEVICE REQUEST (1) and
  GETCOND (9), both read-only, both to main-device addresses 0x20/0x60
  (`shims/src/maple.c:50,85`) — never to sub-devices, where VMUs live.
- The KOS loader phase and the game are both covered by the dynamic test
  below, so no per-component claim is load-bearing.

## Layer 1 — dynamic VMU-canary test (`make test-vmu` / `make test-vmu-play`)

One Flycast run of `build/disc.gdi`, in one of two modes of the same script
(same launch pattern as `scripts/capture.sh` / `docs/kb/tooling.md` Task 18:
abs path, `ApplePersistenceIgnoreState`, transient `rend.vsync=no`, stale
instances killed first):

- **`attract` (default, `make test-vmu`):** unattended background run through
  boot + attract, auto-killed after ~90 s. CI-style, deterministic.
- **`play` (`make test-vmu-play`):** foreground, headed — the tester plays
  the game for as long as they like (settings, 2P, game over, high-score
  screens: the longer and wider the session, the more paths the canary
  observes). The script blocks until the tester quits the emulator, then
  runs the **identical** seed/hash assertions. This is the recommended
  pre-release mode, since it shrinks the "exercised paths only" caveat that
  bounds every dynamic layer. Same `capture.sh play`-pass precedent
  (foreground + vsync=no is the proven combination from Phase 2/3 sessions).

**Oracle (Flycast source, instrumented tree in repo):**

- A VMU flash write is flushed to the backing `vmu_save_*.bin` file
  immediately (`tools/flycast-src/core/hw/maple/maple_devs.cpp:679-707`,
  `MDCF_BlockWrite` → `fwrite`).
- At startup Flycast rewrites a VMU file **only** if missing or all-zero
  (auto-format: `OnSetup` sum check → `initializeVmu()` → `fullSave()`,
  `maple_devs.cpp:436-474`). A non-zero file is otherwise never touched.

**Protocol** (`scripts/test_vmu_untouched.sh`):

1. Fresh temp dir; seed `vmu_save_A1.bin`, `A2`, `B2` with 128 KB of `0xA5`
   (canaries) and `vmu_save_B1.bin` with 128 KB of zeros (control).
2. Launch with transient, non-mutating CLI config (same `-config` mechanism
   capture.sh uses; `emu.cfg` untouched):
   `config:Dreamcast.VMUPath=<tempdir>`, `config:PerGameVmu=no`,
   `config:UsePhysicalVmuMemory=no`, `config:rend.vsync=no`,
   `input:device1=0`, `input:device1.1=1`, `input:device2=0`,
   `input:device2.1=1` (cfg keys/sections:
   `tools/flycast-src/core/cfg/option.cpp:145,201-215,234,238`).
3. `attract`: kill after the run window. `play`: wait for the tester to
   quit. Then hash all four files.
4. **PASS iff** every canary is byte-identical **and** the control file
   changed (auto-format proves the VMUPath override, VMU attachment, and the
   hash logic are actually wired — the control-test rule applied to a
   negative result, in the same run).

Failure modes it catches: any flash write from loader, KOS, shim, game code,
or a missed live MMIO literal — on any path the run exercises.

## Layer 2 — static Maple-literal scan (in fast `make test`)

`scripts/test_maple_literals.py`: scan **every executable byte source on the
disc** for aligned u32 literals in the Maple MMIO block —
`(v & 0x1fffff00) == 0x005f6c00` covers the P0/P1/P2 mirrors of
`0x5f6c00–0x5f6cff`. **PASS iff** the hit set equals the recorded baseline
exactly (offset + value). Same failure class as the 19 unpatched G1
`0x5f7xxx` literals that caused the HW round-10 stall, but for Maple, where a
live literal would put game-controlled frames on the real bus.

**Executable surfaces on the final disc, and how each is covered:**

| Surface | Static coverage | Dynamic coverage |
|---|---|---|
| Full cart image (109 MB `.dat` — boot 1 MB **and** all streamed content) | **scanned** (find-trick, ~1 s) | exercised paths only |
| Naomi BIOS library slices (`build/bios_data.bin`, executable via game thunks incl. unpatched watch-item `0x8c0803a4`) | **scanned** | exercised paths only |
| Loader (our `main.c`/`handoff.S`) | `nm main.o handoff.o` must show **zero** vmu/maple references (measured: 0) | fully — the loader's whole life is the test's boot |
| KOS library code in `1ST_READ.BIN` (links `_vmu_block_write` etc. via default init) | own-objects assert above proves nothing of ours calls it; KOS drivers write only on request (KOS kernel source) | canary run covers KOS init + runtime |
| Shim (`shims/src`) | **excluded by design** — the one authorized Maple user; TX limited to DEVICE REQUEST + GETCOND to main devices (`maple.c:50,85`) | canary run |
| Donor IP.BIN bootstrap (tracks 1–3, runs pre-loader) | donor-verbatim, hash-pinned by `make_gdi` all-files sentinel | runs in every canary boot |

**Baseline (measured 2026-07-26):**

- **Cart: 80 hits = exactly 4 × the boot image's 20**, mirrored at
  `0x200000` stride across `0x0–0x800000` (the region below the first
  streamed offset; matches the Phase 2 streaming map's `0x800000` floor).
  The 20: 1 engine maple-base pool word `0x8c030fec` (repointed to shim
  mirror by the patch table), 10 in the settings/EEPROM BIOS-library region
  `0x8c0803xx–0x8c080exx` (entry thunks stubbed), 9 at `0x8c0a3830–0x8c0a3fcc`
  (second embedded maple-driver copy, never observed executing in Phase 2/3
  MAPLEPC captures).
- **Streamed region (`≥ 0x800000`, ~101 MB): ZERO hits** — asserted as its
  own named invariant ("no maple literal ever streams in"), which closes the
  overlay-code concern for pool-literal SH-4 code.
- **`bios_data.bin`: 1 hit** — `0xa05f6c18` (SB_MDST) at slice offset
  `0x14d4`. Reachable in principle via the unpatched thunk; content-benign
  (MDST kick would resend the shim's last read-only frame), baselined and
  watched like every other entry.

Full (offset, value) lists live in the script as baseline constants.
Contract: any new/changed/moved hit fails the build; classify it (Ghidra
`FindMmioXrefs.java` for xrefs, patch it or prove it dead) before updating
the baseline. For future ports the script is reusable as-is: point it at the
new cart, start from an empty baseline, classify every hit.

**Residual risk (stated, not closed statically):** SH-4 code that *computes*
a Maple register address without a pool literal is invisible to this scan —
only the dynamic canary (exercised paths) covers it. The game engine's own
Maple access goes through the one repointed base pointer, so a computed-
address writer would be wholly new code never observed in any capture.

## Wiring

- Root `Makefile`: `test` gains the static scan (host-fast, no emulator;
  needs the ROM at repo root + `build/bios_data.bin` + loader objects, all
  produced by a normal `make disc`); new `test-vmu` (attract) and
  `test-vmu-play` (headed, tester-driven) targets run the canary script
  (needs ROM, built disc, Flycast — same prerequisites as `make disc`).
- `docs/kb/tooling.md` + `docs/kb/00-status.md`: short entries; playbook
  (`docs/kb/port-playbook.md`) gets the "run both before release" step.

## Non-goals

- VMU LCD/clock/buzzer writes: don't touch the flash file, can't corrupt
  data — out of scope (and the shim never sends them anyway).
- Real-hardware proof: the canary test is an emulator oracle; the real-target
  evidence stays "the user's VMU saves survive real play sessions". The
  static scan is target-independent.
- No runtime guard in the shim: its two TX frames are compile-time constants;
  a runtime assert would compare constants to constants.

## Files touched

`scripts/test_vmu_untouched.sh` (new), `scripts/test_maple_literals.py`
(new), `Makefile` (2 lines), KB docs (short notes).
