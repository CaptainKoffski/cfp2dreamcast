# Boot-load indicator — design

**Date:** 2026-07-26. **Status:** approved, not yet implemented.

## Goal

On real hardware there is a black screen between the loader's NAOMI splash
and the game's company-logo screens: the game sets the VO_CONTROL blank bit
early in init and unblanks only after its first rendered frame, and on HW the
init + boot cart preload (GD-ROM PIO through the shim) under that blank takes
long enough to read as "hung". Give the player continuous visual confirmation
the game is loading: keep the NAOMI splash visible through the window and
animate three small cycling dots on it. The black screen disappears entirely
— which also matches arcade behavior (the Naomi BIOS screen stays up while
the cart checks run; there is no black gap on a real Naomi).

## Mechanism (all pieces already exist)

Three established facts carry the design (`docs/kb/00-status.md` Phase 5
rounds; `shims/src/util.c:17`):

1. **The black window is only a blank bit.** The game never clears the
   framebuffer during init — during the HW stall hunts the shim HUD cleared
   VO_CONTROL bit 3 and the loader's text stayed readable on the TV through
   this exact window. The loader's NAOMI splash is still in VRAM the whole
   time.
2. **`shim_maple_steady` ticks ~60 Hz through the whole window** (ISR-driven
   engine pump; the HUD heartbeats ran on it during every stall round) — a
   free animation clock.
3. **`shim_vid_init` (patch #34 hook) fires at the exact video takeover** —
   the moment the window ends and the game starts presenting.

## Design

- `shim_boot_anim()` in `shims/src/util.c` (~30 lines), called once per tick
  from `shim_maple_steady()` (`shims/src/main.c`, one line). No loader
  changes; **patch count stays 34** (pure shim code, no new patch-table
  entries).
- Gate: `static u32 anim_live = 1` — non-zero `.data` init per house style
  (the loader does not zero shim `.bss`). `shim_vid_init` sets it to 0 before
  tail-calling the SDK display init, so nothing ever paints over
  game-rendered frames, and later stage-boundary cart reads never trigger it
  (no cart-path hook).
- Every 16th tick (~3.75 Hz at 60 fps): clear VO_CONTROL blank bit
  (`0xa05f80e8` bit 3 — splash becomes/stays visible), read the live scanout
  base from FB_R_SOF1 (`0xa05f8050`, same idiom as `hex_paint`), paint three
  10×10 RGB565 squares centered near the bottom (~y=430, inside TV overscan
  safe area), lit dot = `(tick/16) % 3`.
- Perf: ~300 uncached VRAM writes per step, boot window only — zero gameplay
  cost (the round-17 HUD lesson does not apply post-takeover). Per-tick VRAM
  paints + the VO_CONTROL RMW are Flycast-present-safe (HUD precedent,
  rounds 13–16); only CPU control-register probing was ever harmful
  (round-12 bisect).

## Error handling / known edges

- If the game ever re-blanked per frame, the dots would win only ~4×/s and
  flicker; HUD-era evidence says the blank is set once at init.
- If video takeover ever preceded the tail of the preload, the dots stop
  early and the residue is black — same as today, no regression.
- `FB_R_SOF1` is read live per paint, so a mid-window scanout-base move
  cannot misplace the paints (same guarantee the HUD relied on).

## Verification

1. `make test` green (34 patches, old-byte checks unchanged).
2. Flycast boot→attract screenshot check: no regression (the dots themselves
   cannot appear in Flycast screenshots — framebuffer writes never do, same
   as the splash and HUD; the TV shows them).
3. Real-HW look (house rule 1): NAOMI splash stays up after the swirl/license,
   dots cycle until the Taito/company logos appear.
