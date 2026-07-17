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
- **Settings to apply on first launch:**
  - System → Region: **Japan** (game is Japan-only)
  - Platform: auto-detected as Naomi
- **Control mapping (to be set/confirmed by user in GUI — Settings → Controls):**
  Map keyboard as Naomi player-1 device:

  | Action         | Recommended key |
  |----------------|----------------|
  | Up / Down / Left / Right | Arrow keys |
  | Button 1       | Z |
  | Button 2       | X |
  | Start          | Enter |
  | Coin (Insert)  | 5 |
  | Test / Service | F2 |

  *These are Flycast v2.6 defaults for Naomi; confirm in the Controls UI
  and update this table with the actual assigned keys.*

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

### MAME source (reference only — never built, never run)

- Install: `git clone --depth 1 --filter=blob:none --sparse https://github.com/mamedev/mame.git tools/mame`
  then `git -C tools/mame sparse-checkout set src/mame/sega`
- Cloned commit: 59e7c0b9c76305458dc5df7817e30346af7a505d
- Use: `src/mame/sega/naomi.cpp` top comment = Naomi hardware bible;
  register handlers document the cart/G1 interface. MAME romsets/builds are
  deliberately out of scope (our dump is decrypted; MAME wants originals).
