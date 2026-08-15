# Load-screen dancer animation — design

**Date:** 2026-08-03. **Status:** approved design, not yet implemented.

## Goal

The ~5 s post-handoff load screen (black + progress bar, SHIM_LOADBAR — HW
verdicts 2026-08-01/02) works but reads as bare. Show the game's own dancing
Cleopatra background animation above the bar, extracted **from the cart's real
sprite data** (user decision 2026-08-03: authentic assets, not screen
captures), so the load screen feels like part of the game.

Two deliverables:

- **Part A** — a committed extraction pipeline (`scripts/`) that locates,
  decodes, and bakes the dancer frames from the gitignored cart image at
  build time. Ships method, never bytes (house rule 5).
- **Part B** — shim playback: blit the baked frames into the live scanout FB
  during the load window, same idiom as `loadbar_paint`.

## Established facts the design stands on

1. **Live-scanout blitting during load is proven.** `loadbar_paint`
   (`shims/src/util.c:99`) repaints per tick into the flipping FB base
   (FB_R_SOF1), blackout-while-blanked → unblank ordering, cable-dependent
   row. HW-verified both cables (00-status.md, loadbar rounds 1–3 +
   composite reopen).
2. **Scanned lines differ by cable.** The game's TV mode scans FB lines
   0..236 only; VGA scans 0..477 (CLEO-SPG A/B, 2026-08-02). Everything
   painted must be cable-positioned.
3. **The game's scanout pixel format is NOT the loader's RGB565.** HW photo
   2026-08-01: RGB565 splash washed out, 0x39e7 gray showed olive —
   consistent with RGB0555. The bar dodged this with black/white-only; a
   color animation cannot. Must be pinned by measurement (see A4).
4. **~180 KB of proven-safe RAM is free** in the shim carve-out:
   `SHIM_BASE`+0x13100 (above MAPLE_MIRROR) to RAM top 0x8d000000
   (`shims/include/shim_iface.h`; carve-out V2-verified clean, game write
   watermark 15.5 MB).
5. **A load-window-only paint gate exists.** The SHIM_LOADBAR 10 MiB byte
   countdown (`cart.c` PB_TOTAL/pb_left) expires before the title presents;
   painting latched off by it can never draw on a visible game frame.
6. **A ~60 Hz tick + calibrated timebase exist through the whole window.**
   `shim_maple_steady` pumps every engine tick; TCNT0 with runtime
   TCR0.TPSC prescaler read (round-17 pad-cache fix) gives real-time pacing.
7. **Twiddle decode is validated** (`make_gdi.py patch_gdtex`, order
   confirmed from Flycast `core/rend/texconv.cpp`), and the instrumented
   Flycast fork (CLEO-SPG, CLEO-VRAMDUMP, cartlog) is the measurement rig.
8. **Naomi textures are DC PVR textures** (same CLX2 GPU) — twiddled,
   16bpp or palettized 4/8bpp, possibly VQ. No foreign format expected.

## Part A — extraction pipeline (build-time)

**A1. Dump candidate textures.** Run the game in Flycast with texture
dumping during attract (the dancer loops constantly there). Stock Flycast
has a texture-dump facility for the texture-pack workflow; if our build
doesn't expose it on this path, the fork grows a small dump hook in the
texture cache (precedent: CLEO-VRAMDUMP). Output: PNG per unique texture +
its format metadata (dimensions, pixel format, palette, hash).

**A2. Identify the dancer frames.** Eyeball the dump set, list the frame
textures in animation order (order/timing observed in Flycast — frame
advance per vblank count). Deliverable: a frame manifest (texture hash →
sequence position, per-frame tick duration).

**A3. Locate frames in the cart.** Signature-search the .dat for each
dumped texture's raw bytes (the cart-streaming map narrows the haystack; the
attract preload region is fully mapped). Deliverable: authoritative
(cart offset, length, format, palette offset) per frame, recorded in the
manifest — the manifest is committed; it contains offsets and hashes, not
copyrighted bytes.

**A4. Pin the scanout format.** Instrumented Flycast logs FB_R_CTRL at game
video takeover, both cable classes (one-line addition to CLEO-SPG if not
already captured). Expected: fb_depth = 0555 both classes; whatever it
reads is the bake target.

**A5. Bake.** `scripts/extract_dancer.py` (stdlib-only, like bmp2rgb565):
reads the cart at manifest offsets, detwiddles, VQ-decodes if needed,
applies palette → emits (a) PNGs for eyeball verification, (b) a packed
blob: 8bpp indexed frames + one palette in the A4 scanout format + a tiny
header (w, h, frame count, per-frame ticks). Downscale/crop at bake time if
the native frames blow the RAM budget (fact 4): budget = ~180 KB ⇒ e.g.
10+ frames at 128×128 8bpp. Blob is objcopy-embedded into the shim like
splash/bios_data — build-time product of the gitignored cart, never
committed.

## Part B — shim playback

- **Placement:** blob linked into the shim carve-out above MAPLE_MIRROR;
  `shim_iface.h` gains the region define; `test_shim_iface` overlap asserts
  extended. Budget assert: blob ≤ carve-out free space, checked at build.
- **`loadanim_paint()` (util.c):** called from the steady tick alongside the
  existing pump. TCNT0-paced (prescaler-aware) to the A2-measured cadence;
  advances the frame index, blits the current frame above the bar. Per
  pixel: palette lookup, index 0 = transparent (skip — load screen is
  black). Row cable-dependent like `yb` (animation + bar both inside lines
  0..236 on TV; bar stays at its HW-proven rows). Repaint-into-live-base
  every call (flip-buffer convergence, loadbar precedent). Blackout/unblank
  ordering unchanged — `loadbar_paint`'s virgin latch already owns it.
- **Gate:** same window as the bar — paints on every steady tick from the
  moment `shim_vid_init` runs (when the empty bar appears today) until the
  10 MiB countdown expires, then off for good. `SHIM_LOADANIM` (shim_iface.h, default 1)
  compiles it out; SHIM_LOADBAR=0 with SHIM_LOADANIM=1 is not a supported
  combination (the anim reuses the bar's latch).
- **Perf:** ~16 K uncached VRAM writes per anim frame at 128×128, load
  window only — bounded by the same reasoning as the bar ("cost irrelevant
  during load"); zero gameplay cost by the gate. Per-tick FB writes are
  Flycast-present-safe (rounds 13–16 precedent); no new register probing
  beyond TCNT0/TCR0 reads already proven in the pad cache.

## Known unknowns (branches, not blockers)

- Dancer may be **multi-sprite composited** → bake composites the parts into
  flat frames (build-time, free).
- Textures may be **VQ-compressed** → decoder in extract_dancer.py (~20
  lines; 2 KB codebook lookup).
- Animation may use **palette cycling** instead of frame textures → bake
  expands cycles into flat frames.
- Frames may be **large** (true full-screen bg) → crop to the dancer region
  and/or downscale at bake time to fit 180 KB.
- If the game re-blanks video mid-load on some path, the anim (like the bar)
  simply isn't visible until unblank — no regression vs today.

## Verification

1. Bake round-trip: extract_dancer.py PNGs eyeballed; blob header/size
   sanity + region overlap asserts in `make test`.
2. Flycast: CLEO-VRAMDUMP byte-check of a load-window snapshot — frame
   pixels present at the computed rows on both cable classes (the bar's
   verification precedent; Flycast cannot render FB writes, the dump proves
   them). Attract screenshot check: no present regression.
3. Real HW (house rule 1): photo rounds on **both** VGA and composite —
   splash → animated dancer + bar → title, correct colors (A4 validated on
   real scanout), no blink, gameplay unaffected.
