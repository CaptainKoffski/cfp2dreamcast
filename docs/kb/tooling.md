# Tooling

Every tool used by this project: exact install steps, version, usage.
The environment must be rebuildable from scratch from this file.
`tools/` (gitignored) holds third-party clones and generated binaries.

### netboot (DragonMinded) — format reference

- Install: `git clone --depth 1 https://github.com/DragonMinded/netboot.git tools/netboot`
- Cloned commit: 6ccbfdd000e705678ee422ebe972c2fccce40693
- Use: `tools/netboot/naomi/rom.py` is the authoritative Naomi header
  format reference; also contains patching utilities useful in Phase 4.
  `tools/netboot/docs/naomi.md` is DragonMinded's Naomi RE writeup (memory
  map, EEPROM/MIE/JVS access, header load table) — a primary source for
  `naomi-vs-dreamcast.md`.

### Flycast — 2.6

- Install: `brew install --cask flycast` (deprecated cask, still installs; fallback: https://github.com/flyinghead/flycast/releases)
- **Gatekeeper:** after install, macOS may block launch ("Flycast cannot be opened").
  Clear the quarantine once: `xattr -dr com.apple.quarantine /Applications/Flycast.app`
  (app is validly signed as `com.flyinghead.Flycast`).
- Run (use an ABSOLUTE ROM path — a relative path fails with
  `Cannot stat ...` because Flycast's CWD is not the repo root):
  `/Applications/Flycast.app/Contents/MacOS/Flycast "/Users/captainkoffski/AntigravityProjects/cleopatra/Cleopatra Fortune Plus.dat"`
- Emulates both Naomi and Dreamcast. Open source (github.com/flyinghead/flycast);
  Phase 2 instruments a source build — this is the release build.
- **BIOS path (working):** `~/Library/Application Support/Flycast/data/naomi.zip`
  Source: `bios/naomi.zip` in the repo. Copy with:
  `cp bios/naomi.zip ~/Library/Application\ Support/Flycast/data/`
- **ROM format:** `.dat` is accepted directly (no rename to `.bin` needed).
- **Settings to apply on first launch** (recommended; not yet confirmed persisted):
  - System → Region: **Japan** (game is Japan-only)
  - Platform: auto-detected as Naomi
- **Control mapping (confirmed by user, Flycast v2.6, keyboard as Naomi P1):**

  | Action | Key |
  |--------|-----|
  | Start | Enter |
  | Up / Down / Left / Right | Arrow keys (digital directions, not thumbstick) |
  | Button 1 (rotate CCW / select) | X |
  | Button 2 (rotate CW) | C |

  Coin insert and Test/Service both work (verified in coin mode and the
  operator menu); their specific keys were not enumerated. Gameplay uses
  only Start + 4 directions + 2 buttons.

### Flycast — source build (Phase 2 instrumentation)

The instrumented build that logs this game's cart streaming, RAM watermarks, JVS
input, and serial pokes. Distinct from the release Flycast above.

- **Clone:** `tools/flycast-src/` (gitignored), pinned at commit
  `f09d1f22ef8d199b8b7a2395d0b46774e08a58c2`.
- **Instrumentation:** `patches/flycast-instrument.diff` +
  `patches/flycast-syphon-build-fix.diff` — apply per `patches/README.md`
  (submodule init first; the Syphon patch applies *inside* the submodule).
- **Build prereqs (macOS/arm64, this box):**
  - Standalone **CMake 3.31.6** (Kitware universal binary). **NOT** Homebrew
    cmake 4.x — 4.x breaks this Flycast commit at generate (cmrc/OBJC).
  - Full Xcode reachable via `export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`
    (cmake's OBJC ABI detection needs `xcodebuild`; CommandLineTools alone fails).
  - No extra `brew install` needed (zlib/png already present).
- **Configure + build:**
  ```sh
  cd tools/flycast-src
  git submodule update --init --recursive          # slow; re-run if it times out
  git submodule update --init --force --recursive   # force-populate if a prior clone left trees empty
  # (apply the two patches here — see patches/README.md)
  export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
  CM=<path>/cmake-3.31.6-macos-universal/CMake.app/Contents/bin/cmake
  ZLIB_TBD="$DEVELOPER_DIR/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/lib/libz.tbd"
  "$CM" -B build -DCMAKE_BUILD_TYPE=Release \
        -DUSE_BREAKPAD=OFF -DUSE_VULKAN=OFF \
        -DCMAKE_OSX_ARCHITECTURES=arm64 -DZLIB_LIBRARY="$ZLIB_TBD"
  "$CM" --build build -j"$(sysctl -n hw.ncpu)"      # resumable; re-run same cmd if a call times out
  ```
  Flags: `USE_BREAKPAD=OFF` (dump_syms wants full-Xcode; crash reporting irrelevant),
  `USE_VULKAN=OFF` (no MoltenVK on this box; OpenGL renderer is fine),
  `OSX_ARCHITECTURES=arm64` (M1, single-arch halves build time),
  `ZLIB_LIBRARY=<real libz.tbd>` (upstream's `-lz` doesn't resolve here).
- **Output:** `tools/flycast-src/build/Flycast.app/Contents/MacOS/Flycast` (Mach-O arm64).
- **Capturing:** use `scripts/capture.sh <attract|play|input> [seconds]`. It sets
  `FLYCAST_CARTLOG=<repo>/capture-<pass>.log` (the cartlog helper writes there,
  flushed per line) and handles two macOS gotchas:
  - `-config config:rend.vsync=no` — required, or the emu thread deadlocks past
    boot when the window is unfocused (transient flag; `emu.cfg` untouched).
  - `defaults write com.flyinghead.Flycast ApplePersistenceIgnoreState -bool YES`
    — a killed/crashed run makes macOS show a "reopen windows?" modal on the
    *next* launch that silently blocks boot (process alive at ~0% CPU, guest
    never runs, zero cartlog). This was the mysterious "post-sleep launch fails"
    blocker; a reboot does **not** fix it — this key does.
- **Phase 3 interpreter-mode capture:** to log every guest PC/SP (required for
  `CARTDMAPC`/`MAPLEPC`/`BIOSEXEC` lines), the dynarec must be off:
  add `Dynarec.Enabled=no` under `[config]` in `emu.cfg` (or set it in the
  Flycast GUI: Settings → General → CPU → Interpreter). Re-enable after
  capture (interpreter is ~10× slower). Capture command: `scripts/capture.sh pc [seconds]`.
- **Log line formats** (parsed by `scripts/parse_cart_log.py`):
  ```
  CARTDMA src=%08x dest=%08x len=%x      # cart→RAM DMA: cart byte offset, phys RAM dest, bytes
  CARTPIO offset=%08x                     # PIO seek (ROM_DATA port)
  WATERMARK region=%s used=%x size=%x     # region in {main,vram,aram}; highest non-zero byte+1
  JVSREPORT buttons=%04x                  # P1 JVS word (active-high: set bit = pressed)
  SERIALPOKE addr=%08x data=%08x          # write to a NAOMI_COMM_* serial/network register
  CARTDMAPC pc=%08x sp=%08x              # Phase 3: guest PC at SB_GDST store + stack pointer
  MAPLEPC cmd=86 sub=%02x pc=%08x        # Phase 3: guest PC at Maple DMA store (cmd 0x86)
  BIOSEXEC pc=%08x                        # Phase 3: any guest insn in BIOS ROM range (phys 0x0–0x1fffff)
  ```

### Ghidra — 12.1.2 (20260605)

- Install: `brew install --cask ghidra` unavailable in Homebrew; direct download used:
  `curl -L https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.2_build/ghidra_12.1.2_PUBLIC_20260605.zip`
  extracted to `tools/ghidra_12.1.2_PUBLIC/` (gitignored).
- Java: `brew install openjdk` (formula, no sudo needed) → OpenJDK 26.0.1.
  `export PATH="/opt/homebrew/opt/openjdk/bin:$PATH"` before running analyzeHeadless.
- GHIDRA_HOME: `tools/ghidra_12.1.2_PUBLIC`
- Headless: `"$GHIDRA_HOME/support/analyzeHeadless"` — see
  `scripts/ghidra/DisasmEntry.java` for the working import invocation
  (processor `SuperH4:LE:32:default`, BinaryLoader, base 0x8c020000).
- Note: Ghidra 12 dropped Jython headless support (a `.py` post-script would need
  PyGhidra / CPython + JPype). `DisasmEntry.java` is the headless script; write any
  future headless scripts in Java, not Jython.
- Verified: entrypoint 0x8c04ae2c disassembles to plausible SH-4 (5-instruction
  dispatch trampoline: `mov.l`, `mov #0`, `mov.l`, `jmp @r1`, delay-slot `mov.l`).
  Entry is a boot stub that loads the real start address from a literal pool and jumps.
- **Phase 3 harness (`scripts/ghidra/run.sh`):** re-runnable wrapper. Two subcommands:
  ```sh
  scripts/ghidra/run.sh import              # import tools/boot.bin, full auto-analysis (once)
  scripts/ghidra/run.sh script NAME.java    # run scripts/ghidra/NAME.java (-noanalysis)
  ```
  Project dir: `tools/ghidra-proj/` (gitignored). Scripts: `FindMmioXrefs.java`,
  `ScanBiosTargets.java`, `DumpEntryChain.java`, `WhichFunc.java`.
  `tools/boot.bin` = first 1 MB of `Cleopatra Fortune Plus.dat` (gitignored).

### KallistiOS (KOS) + sh-elf toolchain

Dreamcast SDK for Phase 4 (loader, shims). Provides `kos-cc` and the KOS
environment every Phase 4 build task sources.

- **Clone:** `git clone --recursive https://github.com/KallistiOS/KallistiOS.git tools/kos`
  (gitignored). Cloned commit: `705c862957b2f6091a6ce4784943744daecc3e2b`.
  Note: this revision has no submodules (`--recursive` is a harmless no-op).
- **Prefix (requires sudo, one-time):**
  ```sh
  sudo mkdir -p /opt/toolchains/dc && sudo chown "$(whoami)" /opt/toolchains/dc
  ```
  Everything after this is sudo-free. Do NOT relocate — KOS defaults and all
  Phase 4 tasks assume `/opt/toolchains/dc`.
- **Homebrew prereqs:**
  `brew install gmp mpfr libmpc gettext texinfo wget libelf jpeg-turbo libpng`
  (versions used: gmp 6.3.0, mpfr 4.2.2, libmpc 1.4.1, gettext 1.0,
  texinfo 7.3, wget 1.25.0, libelf 0.8.13_1, jpeg-turbo 3.2.0, libpng 1.6.58).
  jpeg-turbo/libpng are needed by the KOS `dcbumpgen` util, not the toolchain.
- **Toolchain build — the builder is `utils/kos-chain`, NOT `utils/dc-chain`**
  (upstream renamed/reworked dc-chain; config is now `Makefile.cfg` copied from
  a per-platform sample, not `config/config.mk.stable.sample`):
  ```sh
  cd tools/kos/utils/kos-chain
  cp Makefile.dreamcast.cfg Makefile.cfg   # stable profile, prefix /opt/toolchains/dc/sh-elf
  make                                     # LONG (~20 min on M1); resumable — re-run same cmd on timeout
  ```
  Built: binutils 2.45.1, GCC 15.2.0 (2-pass, c/c++/objc), newlib 4.6.0.20260123.
  - **Flake hit:** pass-1 GCC died once with
    `fatal error: libgcc_tm.h: No such file or directory` — a parallel-make
    race on a generated header. Recovery: just re-run the same `make`; it
    resumed and completed. If it recurs, set `makejobs=1` in `Makefile.cfg`.
  - **Harmless noise** in the log: `clang++: error: unsupported option
    '-print-multi-os-directory'` — GCC configure probing the host compiler.
- **arm-eabi (AICA) toolchain: deliberately NOT built.** Optional in this KOS
  revision — `kernel/arch/dreamcast/sound/arm/Makefile` falls back to the
  shipped `stream.drv.prebuilt` when `DC_ARM_CC` is absent (confirmed used in
  our build log). Build it only if a custom AICA driver is ever needed
  (`cp Makefile.aica.cfg Makefile.cfg && make` in kos-chain).
- **KOS environment + library build:**
  ```sh
  cd tools/kos
  cp doc/environ.sh.sample environ.sh
  # edit environ.sh: KOS_BASE="/Users/captainkoffski/AntigravityProjects/cleopatra/tools/kos"
  # (absolute path; all other settings left at defaults — KOS_SUBARCH stays
  #  commented = "pristine" Dreamcast; the loader targets a stock DC)
  source environ.sh
  export CPATH=/opt/homebrew/include LIBRARY_PATH=/opt/homebrew/lib  # see below
  make
  ```
  - **macOS/arm64 deviation:** without CPATH/LIBRARY_PATH the build dies at
    `utils/dcbumpgen` with `jpeglib.h: file not found` — its Makefile
    hardcodes `-I/usr/local/include` (Intel-mac Homebrew path; arm64 brew is
    `/opt/homebrew`). The two exports fix it; no file edits needed.
  - Output: `tools/kos/lib/dreamcast/libkallisti.a` (5.8 MB).
- **Verify:**
  - `/opt/toolchains/dc/sh-elf/bin/sh-elf-gcc --version` → `sh-elf-gcc (GCC) 15.2.0`
  - hello example: `source environ.sh && cd examples/dreamcast/hello && make`
    → `hello.elf` (ELF 32-bit LSB, Renesas SH, statically linked).
- **Every later build shell must `source tools/kos/environ.sh` first** (gives
  `kos-cc`, `KOS_BASE`, flags).

### MAME source (reference only — never built, never run)

- Install: `git clone --depth 1 --filter=blob:none --sparse https://github.com/mamedev/mame.git tools/mame`
  then `git -C tools/mame sparse-checkout set src/mame/sega`
- Cloned commit: 59e7c0b9c76305458dc5df7817e30346af7a505d
- Use: `src/mame/sega/naomi.cpp` top comment = Naomi hardware bible;
  register handlers document the cart/G1 interface. MAME romsets/builds are
  deliberately out of scope (our dump is decrypted; MAME wants originals).
