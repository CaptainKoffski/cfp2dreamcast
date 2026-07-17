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

### MAME source (reference only — never built, never run)

- Install: `git clone --depth 1 --filter=blob:none --sparse https://github.com/mamedev/mame.git tools/mame`
  then `git -C tools/mame sparse-checkout set src/mame/sega`
- Cloned commit: 59e7c0b9c76305458dc5df7817e30346af7a505d
- Use: `src/mame/sega/naomi.cpp` top comment = Naomi hardware bible;
  register handlers document the cart/G1 interface. MAME romsets/builds are
  deliberately out of scope (our dump is decrypted; MAME wants originals).
