# Task 1 Report (Phase 2): Build the instrumented Flycast

Status: **DONE** — build succeeds, all five instrumentation log points verified firing, and the
smoke gate (CARTDMA > 0 AND JVSREPORT > 0, plus beyond-boot cart reads) is fully met.

(Note: this file previously held a stale Phase 1 scaffolding report; overwritten with the Phase 2
instrumented-Flycast report per the current task brief.)

## What changed, per file (real line numbers in the pinned source)

Source tree: `tools/flycast-src` @ `f09d1f22ef8d199b8b7a2395d0b46774e08a58c2` (gitignored; edits captured
in `patches/flycast-instrument.diff`).

### Instrumentation (the actual task)
- **`core/hw/naomi/cartlog.h`** (new, 4 lines): `void cartlog(const char*, ...)` — verbatim from brief.
- **`core/hw/naomi/cartlog.cpp`** (new, 23 lines): writes to `$FLYCAST_CARTLOG` (default
  `flycast-cartlog.txt`), truncate-once, flush-per-line — verbatim from brief.
- **`core/hw/naomi/CMakeLists.txt`** (+2): added `cartlog.cpp` / `cartlog.h` at the top of the
  `target_sources(${PROJECT_NAME} PRIVATE ...)` list.
- **`core/hw/naomi/naomi_cart.h`**:
  - base `class Cartridge` — added `virtual u32 GetDmaSrcOffset() const { return 0; }` right after
    `virtual void* GetDmaPtr(u32 &size) = 0;` (was line 66).
  - `class NaomiCartridge` — added `u32 GetDmaSrcOffset() const override { return DmaOffset & 0x1fffffff; }`
    right after its `GetDmaPtr` override (was line 89; `DmaOffset` declared at line 104).
- **`core/hw/naomi/naomi.cpp`**:
  - includes (after `#include "oslib/i18n.h"`, was line 37): added `cartlog.h`, `hw/pvr/pvr_mem.h`
    (vram), `hw/aica/aica_if.h` (aica::aica_ram). `hw/sh4/sh4_mem.h` (mem_b) and `types.h` (the
    RAM_SIZE/VRAM_SIZE/ARAM_SIZE macros) were already included.
  - `cartlog_high()` + `cartlog_watermarks()` file-statics inserted just above `Naomi_DmaStart`
    (was line 152). **Deviation from brief, deliberate:** the brief's watermark snippet used
    `RAM`/`ERAM_SIZE`, but in this source those symbols are `elan::RAM` / `elan::ERAM_SIZE`
    (Elan coprocessor RAM, `hw/pvr/elan.h`), NOT main system RAM. `mem_watch.cpp`'s "main" region is
    `mem_b` sized by `RAM_SIZE`. I used the correct main-RAM symbols: `region=main` -> `mem_b`/`RAM_SIZE`,
    `region=vram` -> `vram`/`VRAM_SIZE`, `region=aram` -> `aica::aica_ram`/`ARAM_SIZE`. Verified at
    runtime: `WATERMARK region=main ... size=2000000` (32 MB), `vram size=1000000` (16 MB),
    `aram size=800000` (8 MB) — correct Naomi region sizes.
  - CARTDMA + WATERMARK-every-64th inserted in the `CurrentCartridge` branch of `Naomi_DmaStart`,
    right after the existing `DEBUG_LOG(NAOMI, "NAOMI-DMA start ...")` (was line 167) — verbatim.
  - SERIALPOKE inserted in `WriteMem_naomi` after the null-cartridge guard and before the m3comm
    branch (guard was lines 104-108, m3comm branch line 109) — verbatim.
- **`core/hw/naomi/naomi_cart.cpp`**:
  - added `#include "cartlog.h"` after `#include <memory>`.
  - CARTPIO inserted in `NaomiCartridge::WriteMem`, `NAOMI_ROM_OFFSETL_addr` case, after
    `RomPioOffset |= data;` (was line 1017) — verbatim.
- **`core/hw/maple/maple_jvs.cpp`** — **NOT `maple_devs.cpp`. Corrected the brief's file/line.**
  The brief pointed at `maple_devs.cpp:195` (`w16(getButtonState(pjs));`), but that site is the
  **Dreamcast Maple controller** (`struct maple_sega_controller`) — it does NOT fire for a Naomi
  arcade game. Naomi input is JVS, handled in `maple_jvs.cpp`. The real per-frame digital-input
  report is the JVS command `0x20` ("Read digital input") handler, where `read_digital_in(buttons, inputs)`
  fills the per-player button words. I added `#include "hw/naomi/cartlog.h"` (after the existing
  `#include "hw/naomi/naomi_cart.h"`) and, right after `read_digital_in(buttons, inputs);` (~line 2227),
  `cartlog("JVSREPORT buttons=%04x\n", inputs[0] & 0xffff);` — P1's 16-bit JVS word, active-low.
  Confirmed at runtime: 1218 reports in 30 s (~60/s = one per frame). The `maple_devs.cpp` line would
  have produced 0 for this game.

### Local build fixes (NOT instrumentation — needed to build on this machine; also in the patch)
- **`CMakeLists.txt`**:
  - after `project(flycast)`: `if(APPLE AND NOT LIBRETRO) enable_language(OBJC) endif()`. Without it,
    `CMAKE_OBJC_COMPILE_OBJECT` stays unset (cmake emits "Error required internal CMake variable not set")
    and SDL2's `.m` sources never compile (`ar: ... .m.o: No such file`). SDL2's subproject compiles
    `.m` files but only declares `project(SDL2 C)` and never enables OBJC, relying on the parent.
  - MoltenVK POST_BUILD copy guarded with `if(USE_VULKAN AND EXISTS "$ENV{VULKAN_SDK}/lib/libMoltenVK.dylib")`.
    Unguarded it tried to copy `/lib/libMoltenVK.dylib` (VULKAN_SDK unset) and failed the link's
    post-build, deleting the just-built binary.
- **`core/deps/Syphon/CMakeLists.txt`** (submodule): `target_precompile_headers(Syphon PUBLIC ...)` ->
  `PRIVATE`. PUBLIC propagated Syphon's ObjC prefix-header PCH to the flycast target, whose `.mm`
  (ObjC++) sources then failed with "Objective-C was disabled in precompiled file". PRIVATE keeps
  Syphon's PCH (and its `SYPHONLOG` macro) internal.

## Working build recipe (record for tooling.md — later task owns tooling.md)

Prereqs discovered:
- `brew install cmake` installed cmake **4.4.0**, which is incompatible with this Flycast commit
  (breaks at generate with cmrc/OBJC). Used a standalone **CMake 3.31.6** universal binary instead
  (downloaded to the session scratchpad from Kitware releases; not installed system-wide). A later
  task should pin a cmake 3.x for tooling.md.
- Full **Xcode** is installed at `/Applications/Xcode.app` but `xcode-select` points at
  CommandLineTools. `xcodebuild` (needed by cmake OBJC ABI detection and breakpad's dump_syms) is
  reached by exporting `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer` (no sudo).
- No other `brew install` was needed (zlib/png already present via Homebrew).

```bash
cd tools/flycast-src
git submodule update --init --recursive            # slow (network); resumable, re-run if it times out.
# Some submodule *working trees* were left empty by an interrupted prior clone; force-populate:
git submodule update --init --force --recursive     # (oboe is Android-only; its "fatal" is benign on macOS)

export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
CM=<path-to>/cmake-3.31.6-macos-universal/CMake.app/Contents/bin/cmake   # NOT brew cmake 4.x
ZLIB_TBD="$DEVELOPER_DIR/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/lib/libz.tbd"

"$CM" -B build -DCMAKE_BUILD_TYPE=Release \
      -DUSE_BREAKPAD=OFF -DUSE_VULKAN=OFF \
      -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DZLIB_LIBRARY="$ZLIB_TBD"
"$CM" --build build -j"$(sysctl -n hw.ncpu)"        # resumable; re-run same cmd if a 10-min call times out
# -> build/Flycast.app/Contents/MacOS/Flycast  (Mach-O arm64, 13 MB)
```
Flags rationale: `USE_BREAKPAD=OFF` (dump_syms needs full-Xcode via xcodebuild; crash reporting is
irrelevant); `USE_VULKAN=OFF` (no MoltenVK/Vulkan SDK on this box; OpenGL renderer is fine and matches
`pvr.rend=0`); `OSX_ARCHITECTURES=arm64` (single-arch, this is an M1; halves build time vs the default
`x86_64;arm64` universal); `ZLIB_LIBRARY=<real libz.tbd>` (the upstream `set(ZLIB_LIBRARY "-lz")`
Xcode workaround is treated as a bogus make target under the Unix Makefiles generator).

## Smoke test (Step 9)

Command (Step 9 verbatim, 30 s window) run against `Cleopatra Fortune Plus.dat` with BIOS at
`~/Library/Application Support/Flycast/data/naomi.zip`, `Dreamcast.Region=0` (Japan) in emu.cfg.

**Critical runtime finding:** launched non-interactively (background/`open`), the emu thread deadlocks
in `sh4_sched_tick -> cResetEvent::Wait()` — it parks waiting for a vblank/render-sync signal the
un-focused GUI never delivers; only ~3 JVSREPORTs (boot handshake) appear and frame emulation never
advances (CARTDMA stays 0). Setting **`rend.vsync = no`** in emu.cfg decouples the render thread from
the display refresh and breaks the deadlock; the game then runs at full speed and streams cart assets.
(`ThreadedRendering` off made it worse — stalls even earlier.) I reverted emu.cfg to the user's
original after testing; a real capture run needs `rend.vsync = no` (or an interactive foreground window).

Results with `rend.vsync = no`, 30 s:

| log point   | count | notes                                                            |
|-------------|-------|------------------------------------------------------------------|
| CARTDMA     | 150   | src offsets 0x00800000 .. ~0x05ff1000 (~96 MB into cart)         |
| JVSREPORT   | 1218  | ~60/s = one per frame; grows continuously (271 @10s, 622 @20s)   |
| CARTPIO     | 4     |                                                                  |
| WATERMARK   | 9     | main used=0xc00080/32MB, vram=0x93e738/16MB, aram=0x800000/8MB   |
| SERIALPOKE  | 0     | no serial/network reg writes this offline boot (expected)        |
| beyond-boot cart reads (src >= 0x100000) | **150** | ALL 150 are beyond the 1 MB boot window |

Note: the Step 9 beyond-boot one-liner uses `awk ... strtonum(...)`, a **gawk** function absent from
macOS's default awk — it errors and prints 0. Re-counted portably (`printf "%d" 0x$hex` + compare):
**150** beyond-boot reads. Gate (CARTDMA>0 AND JVSREPORT>0 AND >=1 beyond-boot): **PASS**.

## Patch (Step 10)
`patches/flycast-instrument.diff` — **233 lines**. Contains the 8 top-level file edits plus the Syphon
submodule hunk (marked separately, applies inside `core/deps/Syphon/`, since git diff on the parent
only shows a `-dirty` submodule pointer). Top-level hunk validated via `git apply --reverse --check`.

## Concerns
1. **cmake 4.4.0 (from `brew install cmake`) does not work** with this Flycast commit; a standalone
   3.31.6 was used. tooling.md should pin a cmake 3.x.
2. **Capture requires `rend.vsync = no`** (or interactive foreground) or the emu thread deadlocks and
   nothing beyond boot is logged. The later user-driven capture task must set this.
3. Build fixes (breakpad/vulkan off, arm64-only, real libz, enable_language(OBJC), MoltenVK guard,
   Syphon PCH PRIVATE) live in gitignored `tools/` and in the diff. They are build-environment fixes,
   not instrumentation — none touch the logged code paths. `DEVELOPER_DIR` must point at Xcode.app.
4. The brief's JVS site (`maple_devs.cpp:195`) was wrong for this game; used the real JVS path in
   `maple_jvs.cpp`. Task 2's parser only needs the `JVSREPORT buttons=%04x` line format, which is
   unchanged, so this is transparent downstream.

## Task 1 fix — patch reproducibility

### What was split

`patches/flycast-instrument.diff` previously embedded the Syphon submodule hunk after a prose
comment separator (`# --- core/deps/Syphon submodule ... ---`), with incorrect path prefix
(`a/CMakeLists.txt`) that made `git apply` from the Flycast root fail or misapply it.

Split into two cleanly self-applying patches:

- `patches/flycast-instrument.diff` — main Flycast tree only (8 files; generated with
  `git -C tools/flycast-src diff HEAD -- <explicit file list>`). Content is identical to the
  original main-tree hunks; the Syphon prose+hunk is removed entirely.
- `patches/flycast-syphon-build-fix.diff` — Syphon submodule only (1 file: `CMakeLists.txt`);
  generated with `git -C tools/flycast-src/core/deps/Syphon diff` so paths are correct for
  applying inside the submodule.
- `patches/README.md` — documents apply order and exact commands.

### Verification commands and output

```
$ git -C tools/flycast-src apply --check --reverse "$PWD/patches/flycast-instrument.diff"
(no output)
EXIT: 0

$ git -C tools/flycast-src/core/deps/Syphon apply --check --reverse \
      "/Users/captainkoffski/AntigravityProjects/cleopatra/patches/flycast-syphon-build-fix.diff"
(no output)
EXIT: 0
```

Both reverse-checks pass (exit 0, no output), confirming the patches exactly describe the current
working-tree state. Forward stash/pop was not attempted to avoid risking a dirty tree; reverse-check
is sufficient evidence of patch integrity.

### Files changed

- `patches/flycast-instrument.diff` — rewritten (Syphon section removed)
- `patches/flycast-syphon-build-fix.diff` — new
- `patches/README.md` — new
