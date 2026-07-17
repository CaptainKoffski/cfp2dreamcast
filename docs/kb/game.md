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

Filled in when the game first boots (Phase 1, boot verification task).

## Open questions

- MAME set name for this game (probably `cleoftp`) — confirm from MAME's
  naomi.cpp game list when the MAME source clone lands.
- Date/serial header fields (0x130 area) — not parsed yet; add to the
  script if a later phase needs them.
