# Tooling

Every tool used by this project: exact install steps, version, usage.
The environment must be rebuildable from scratch from this file.
`tools/` (gitignored) holds third-party clones and generated binaries.

### netboot (DragonMinded) — format reference

- Install: `git clone --depth 1 https://github.com/DragonMinded/netboot.git tools/netboot`
- Cloned commit: 6ccbfdd000e705678ee422ebe972c2fccce40693
- Use: `tools/netboot/naomi/rom.py` is the authoritative Naomi header
  format reference; also contains patching utilities useful in Phase 4.
