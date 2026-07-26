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

## Layer 1 — dynamic VMU-canary test (`make test-vmu`)

One headless Flycast run of `build/disc.gdi` through boot + attract (~90 s,
same launch pattern as `scripts/capture.sh` / `docs/kb/tooling.md` Task 18:
abs path, `ApplePersistenceIgnoreState`, transient `rend.vsync=no`, stale
instances killed first).

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
3. Kill after the run window; hash all four files.
4. **PASS iff** every canary is byte-identical **and** the control file
   changed (auto-format proves the VMUPath override, VMU attachment, and the
   hash logic are actually wired — the control-test rule applied to a
   negative result, in the same run).

Failure modes it catches: any flash write from loader, KOS, shim, game code,
or a missed live MMIO literal — on any path the run exercises.

## Layer 2 — static Maple-literal scan (in fast `make test`)

`scripts/test_maple_literals.py`: scan `tools/boot.bin` (1 MB game boot
image, base `0x8c020000`) for aligned u32 literals in the Maple MMIO block —
`(v & 0x1fffff00) == 0x005f6c00` covers the P0/P1/P2 mirrors of
`0x5f6c00–0x5f6cff`. **PASS iff** the hit set equals the recorded baseline
exactly (address + value). Same failure class as the 19 unpatched G1
`0x5f7xxx` literals that caused the HW round-10 stall, but for Maple, where a
live literal would put game-controlled frames on the real bus.

**Baseline (measured 2026-07-26, 20 hits):**

| Region | Hits | Classification |
|---|---|---|
| `0x8c030fec` = `0xa05f6c00` | 1 | engine maple base — **repointed to shim mirror** by the patch table |
| `0x8c080a00–0x8c080e90` | 10 | settings/EEPROM BIOS-library region; entry thunks stubbed (33-patch table); no live maple xrefs on any captured path |
| `0x8c0a3830–0x8c0a3fcc` | 9 | second embedded maple-driver copy; never observed executing in Phase 2/3 instrumented captures (MAPLEPC) |

Full 20-entry (address, value) list lives in the script as the baseline
constant. Contract: any new/changed/moved hit fails the build; classify it
(Ghidra `FindMmioXrefs.java` for xrefs, patch it or prove it dead) before
updating the baseline. For future ports the script is reusable as-is: point
it at the new boot image, start from an empty baseline, classify every hit.

## Wiring

- Root `Makefile`: `test` gains the static scan (host-fast, no emulator);
  new `test-vmu` target runs the canary script (needs ROM, built disc,
  Flycast — same prerequisites as `make disc`).
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
