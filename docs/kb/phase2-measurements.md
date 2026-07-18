# Phase 2 measurements — RAM footprint and serial/watchdog

Measured from the instrumented Flycast (`patches/flycast-instrument.diff`) over
three merged passes: attract (`capture-attract.log`), extended demo
(`capture-demo-extended.log`), and a **hands-on play pass reaching a game-over**
(`capture-play.log`). **Coverage note:** the play pass barely moved the numbers
— it added 5 new DMA requests and did **not** raise any region's peak (main-RAM
high-water, VRAM, and ARAM scans are all unchanged from attract+demo). That the
peaks held across real gameplay including a game-over strengthens these figures
from "first conservative read" toward "representative." The one residual gap is
the un-streamed top ~12 MB of the cart (see `cart-streaming-map.md`).

## RAM footprint vs Dreamcast capacity

Two independent measures per region:
- **DMA high-water** = `max(dest+len)` over cart DMAs landing in a region —
  trustworthy (actual asset placement).
- **WATERMARK scan** = highest non-zero byte (backwards scan) — a *conservative
  upper bound*; stale/uninitialized non-zero data high in a region inflates it.

| Region | DMA high-water | Scan peak | DC capacity | Reading |
|---|---|---|---|---|
| **Main RAM** | `0x0cb378e0` → **11.2 MB** above base | **~32 MB** (`0x01fff60b`) | 16 MB | Assets fit 16 MB (11.2 MB). The WATERMARK scan hit at `0x01fff60b` (≈ 1 KB below the top of Naomi's 32 MB) was **stale/uninitialized data, NOT a real high-address stack** — confirmed by Phase 3 disassembly + dynamic SP logging (`docs/kb/boot-binary.md` §3): init sets r15 to `0x8c00f000`/`0x8c00f400` (pool-resolved by `DumpEntryChain.java`); dynamic SP range across both captures is `0x8c00e6e8`–`0x8c00ef28` (~58–62 KB above RAM base). **SP is nowhere near 32 MB. Main RAM is safe on DC 16 MB; no SP relocation needed in Phase 4.** |
| **Video RAM** | — (uploaded via PVR/TA, not cart DMA) | **9.2 MB** (`0x0093e738`) | 8 MB | **Exceeds 8 MB by ~1.2 MB.** Texture/framebuffer cuts likely needed in Phase 5 — unless the scan over-reports; confirm with a real play pass and a tighter measure. |
| **Sound RAM** | — (AICA DMA) | **8.0 MB** (`0x00800000` = top of Naomi ARAM) | 2 MB | **Inconclusive.** The scan pegged at the exact top of the 8 MB Naomi ARAM — the backwards-scan artifact (a stale non-zero top byte). Real usage unknown; sound RAM (8→2 MB, 4×) is the region most likely to need cuts. Needs a better measure (interpreter-mode write-tracking, spec fallback) in Phase 5. |

**Verdict for Phase 5:** main-RAM *asset* placement fits DC comfortably; the
Phase 3 stack question is resolved — SP lives at `0x8c00e–f xxx`, main RAM is
safe, no SP relocation needed (`docs/kb/boot-binary.md` §3). VRAM looks ~1 MB
over 8 MB → plan for texture reduction. Sound RAM is unmeasured by this method
→ needs a targeted measurement. This refines `naomi-vs-dreamcast.md §1` (turns
the RAM-size deltas from assumption toward measurement). The play-pass top-up is
now done (peaks held); what remains is the sound/VRAM confirmations in Phase 5.

## Serial / watchdog (resolves §8-4)

- **Serial/network pokes captured: 0** across all runs. The game does not write
  the `NAOMI_COMM_*` serial/network registers → **no serial shim needed** in
  Phase 4.
- **Watchdog:** no dedicated `NAOMI_COMM` register; any watchdog access would
  appear as an unknown-register write in Flycast's NAOMI debug log — **not
  observed**. Low risk; no watchdog shim indicated so far.
