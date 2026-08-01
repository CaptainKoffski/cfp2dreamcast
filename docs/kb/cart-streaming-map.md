# Cart streaming map (Phase 2 capture)

The runtime cart reads *Cleopatra Fortune Plus* issues, captured from the
instrumented Flycast (`patches/flycast-instrument.diff`). Machine-readable rows
are in `cart-streaming-map.csv` (`cart_offset,length,dest,mode`). Each `DMA`
row is a `(cart byte offset, length, physical RAM dest)` request that Phase 4
must reissue as a GD-ROM read; `mode=PIO` rows are small scattered seeks via
the ROM_DATA port (length not tracked).

## Coverage (iterative — see `00-status.md`)

- **Captured:** three passes, merged (the parser dedups on merge):
  1. attract-mode loop (`capture-attract.log`),
  2. an extended unattended demo-mode run (`capture-demo-extended.log`, ~overnight),
  3. a **hands-on play pass reaching a game-over** (`capture-play.log`,
     user-driven — real gameplay across multiple stages).
- **Play pass added only 5 new unique DMA requests** over attract+demo (383 →
  388) — i.e. demo mode already exercised nearly all of gameplay's asset
  streaming; real play mostly re-reads the same assets. The 5 new reads land at
  cart `0x044e2000`..`0x04b84000` (~68–75 MB), staged to the same buffer
  (`0x0ca378e0`) — stage assets the demo doesn't show.
- **Known remaining gap:** the **top ~12 MB of the cart**
  (`0x0609c000`..`0x06d00000`, ~96.6→109 MB) was never streamed by any pass —
  could be padding, unreferenced data, or content past where play reached. Top
  up the same way (replay with logging on; the CSV dedups on merge) if Phase 4
  needs it.
- **Boot load** (from the header, not the runtime log): cart `0x0` → RAM
  `0x8c020000`, `0x100000` bytes (`docs/kb/game.md`).

## What we have

- **388 unique DMA requests.**
- **Cart offset range touched:** `0x00800000`..`0x0609c000` — i.e. runtime
  streaming spans ~8 MB to ~96 MB into the 109 MB cart. Notably the lowest
  runtime DMA source is `0x800000` (8 MB); nothing between the 1 MB boot image
  and 8 MB is DMA-streamed in this capture.
- **1 PIO seek** (offset `0x450` region) — PIO is barely used; the game streams
  almost entirely by DMA, as expected for bulk assets.
- **Verification — all three parser self-checks PASS:**
  - `dest_in_ram`: every DMA destination in main RAM `0x0c000000`-`0x0dffffff`.
  - `len_aligned_32`: every DMA length is a whole number of `0x20`-byte units.
  - `beyond_boot_read`: reads well past the 1 MB boot region (proves runtime
    streaming, not just boot).

Parser summary (`scripts/parse_cart_log.py capture-attract.log
capture-demo-extended.log capture-play.log`):

```
DMA requests (unique): 388
PIO seeks (unique): 1
cart offset range: 0x00800000..0x0609c000
main-RAM DMA high-water (dest+len): 0x0cb378e0 (11.2 MB above base) vs DC 16 MB
WATERMARK main: 0x01fff60b (32.0 MB — see phase2-measurements.md)
WATERMARK vram: 0x0093e738 (9.2 MB — stale BIOS-framebuffer content, see phase2-measurements.md)
WATERMARK aram: 0x00800000 (8.0 MB — scan artifact, see phase2-measurements.md)
serial/network pokes: 0
CHECK dest_in_ram: PASS
CHECK len_aligned_32: PASS
CHECK beyond_boot_read: PASS
```

This resolves `naomi-vs-dreamcast.md §8-1` (the cart-streaming request pattern)
for the captured coverage; RAM/serial findings are in
`docs/kb/phase2-measurements.md`, input bits in `docs/kb/input-map.md`.
