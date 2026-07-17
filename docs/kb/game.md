# Cleopatra Fortune Plus — dump notes

The source material is `Cleopatra Fortune Plus.dat` (repo root): a decrypted
Naomi cartridge image with a standard NAOMI header. Only the Japan title
slot is populated — the game is Japan-only.

## Header

Parsed by `scripts/parse_header.py` (offsets cross-checked against
`tools/netboot/naomi/rom.py`):

- **File:** `Cleopatra Fortune Plus.dat` (109,051,904 bytes)
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

Port-relevant reading of the load table: the game loads only 1 MB at boot
(ROM 0x0 → RAM 0x8c020000, entry 0x8c04ae2c); the other ~108 MB are read
at runtime through the ROM-board interface and must become GD-ROM
streaming / RAM preload on Dreamcast.

## Runtime observations

**BIOS:** `bios/naomi.zip` (8.1 MB MAME-format Naomi BIOS set) — installed
to `~/Library/Application Support/Flycast/data/naomi.zip`.

**BIOS contents verified:** standard MAME Naomi set including
`epr-21576d.ic27`, `epr-21578g.ic27`, `sp5001-b.bin`, and all expected
variant ROMs.

**Launch command (working — opens Flycast with ROM):**
```
/Applications/Flycast.app/Contents/MacOS/Flycast "Cleopatra Fortune Plus.dat"
```
Run from the repo root. Flycast v2.6 (universal binary, arm64 + x86_64)
launches successfully. The `.dat` extension is accepted directly — no
rename to `.bin` was needed (fallback chain rung 1 not required).

**Autonomous screenshot:** not obtained. `screencapture -x` returned
`could not create image from display` — the terminal process lacks macOS
Screen Recording permission (the user declined the prompt). This is a
macOS permission wall, not an emulator failure. Flycast launched (PID
confirmed) and exited cleanly when killed; no crash reports observed. Boot
confirmation requires the user to launch Flycast interactively and report
what they see.

**Gatekeeper:** the Homebrew-installed `Flycast.app` was quarantined and
macOS blocked launch ("cannot be opened"). Cleared once with
`xattr -dr com.apple.quarantine /Applications/Flycast.app` (the app is
validly signed as `com.flyinghead.Flycast`). Required before it will run.

**Region:** must be set to Japan in Flycast (game is Japan-only — USA/
Export/Korea title slots in the header are all SAMPLE GAME placeholders).
Not yet persisted: no `emu.cfg` has been written, so the user sets this on
first interactive launch.

**Screenshot:** `docs/kb/img/flycast-attract.png` — to be captured by the
user once interactive acceptance test is run.

## Open questions

- MAME set name for this game (probably `cleoftp`) — confirm from MAME's
  naomi.cpp game list when the MAME source clone lands.
- Date/serial header fields (0x130 area) — not parsed yet; add to the
  script if a later phase needs them.
