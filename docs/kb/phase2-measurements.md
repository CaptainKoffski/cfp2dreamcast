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
| **Sound RAM** | — (AICA DMA) | ~~8.0 MB~~ → **2.0 MB** (write-truth, Phase 5) | 2 MB | **RESOLVED — fits, no cuts.** The old backwards *content* scan pegged at the exact 8 MB top — a stale/BIOS byte it cannot tell from a real write. Write-truth remeasure (zero ARAM at game handoff, then high-water + 256 KB histogram of genuine post-handoff writes — `naomi.cpp cartlog_aram_profile`) shows the game writes **only `0x000000-0x1fffff` = exactly 2 MB, 0 bytes above 2 MB** across boot+attract+demo (7 snapshots / 433 cart DMAs, `capture-aram-fit.log`). The full 2 MB loads once at boot as a fixed bank (histogram buckets 8-31 = the 2-8 MB range stay all-zero). The game already targets a ≤2 MB sound config → **sound fits DC's 2 MB.** |

**Verdict for Phase 5:** main-RAM *asset* placement fits DC comfortably; the
Phase 3 stack question is resolved — SP lives at `0x8c00e–f xxx`, main RAM is
safe, no SP relocation needed (`docs/kb/boot-binary.md` §3). VRAM looks ~1 MB
over 8 MB → plan for texture reduction. **Sound RAM: RESOLVED in Phase 5** by a
write-truth remeasure (see the ARAM row above) — the game uses exactly 2 MB and
fits DC with no sample cuts. This refines `naomi-vs-dreamcast.md §1` (turns the
RAM-size deltas from assumption toward measurement). The play-pass top-up is now
done (peaks held); the one remaining fit-check is VRAM.

## Sound RAM — write-truth measurement (Phase 5)

The Phase-2 WATERMARK is a *backwards content scan*: it reports the highest
non-zero byte, so a stale or BIOS-written byte high in the region inflates it —
which is exactly why ARAM read "8 MB / inconclusive." A content scan cannot
distinguish a real game write from residue. The fix is to measure **writes**, not
content:

1. Zero ARAM once at game handoff (the first cart DMA — cart DMAs land in *main*
   RAM; sound reaches ARAM only later via G2/AICA DMA, so nothing is lost). This
   wipes any BIOS/reset residue, including the mystery top byte.
2. Afterward, any non-zero ARAM byte is a genuine game/AICA sound write. Report
   the true high-water, non-zero counts below/above DC's 2 MB line, and a 256 KB
   per-bucket histogram (so a lone stray write is distinguishable from dense use).

Instrumentation: `cartlog_aram_profile()` + the handoff-zero in `Naomi_DmaStart`
(`patches/flycast-instrument.diff`, `core/hw/naomi/naomi.cpp`). Run: launch the
instrumented Flycast on the ROM with `FLYCAST_CARTLOG` set, let boot+attract+demo
run. Evidence: `capture-aram-fit.log`.

Result — high-water pinned at **`0x200000` (exactly 2 MB)** and **`nz_above2m=0`**
in every one of 7 snapshots over 433 cart DMAs; histogram buckets 0-7 (0-2 MB)
full/near-full, buckets 8-31 (2-8 MB) all zero. The full 2 MB is present from the
first post-handoff snapshot → a single fixed sound bank loaded at boot, not
incremental per-stage loading. **Verdict: the game's sound data fits DC's 2 MB
ARAM exactly (100% full, 0 bytes over) — no sample reduction needed.**

Confirmed on hands-on gameplay (2026-08-01): a second capture with a real
stage-play round (`capture-aram-fit-gameplay.log`, 603 cart DMAs / 10 snapshots —
gameplay added ~140 cart DMAs of in-game asset streaming over the attract-only
run) held the high-water at exactly `0x200000` with `nz_above2m=0` in every
snapshot; buckets 8-31 (2-8 MB) stayed all-zero throughout piece drops, line
clears, combos, and stage transitions. Nothing loads a sound bank past 2 MB.
**Fully closed — the game fits DC's 2 MB ARAM in both attract and hands-on play.**

Why no gameplay scenario can change this (incl. 2P both-perfect simultaneously):
the 2 MB bank is loaded once at boot and playing a sound keys one of the AICA's
64 hardware voice channels to read an *already-resident* sample — simultaneous
playback spends **voices, not ARAM bytes**, so the footprint is scenario- and
mode-independent. The only ceiling many concurrent sounds can hit is voice-count
exhaustion (>64 voices → the game's driver steals a voice), which is a
game-design behaviour identical on Naomi and DC (same AICA, same samples), not a
port regression. The conversion touches GD/boot/video/JVS, never the AICA path.
Note too that the game uses only 2 MB even though Naomi offers 8 MB — it is
self-constrained to a 2 MB sound budget, so it never exceeds a ceiling DC lacks.

## Serial / watchdog (resolves §8-4)

- **Serial/network pokes captured: 0** across all runs. The game does not write
  the `NAOMI_COMM_*` serial/network registers → **no serial shim needed** in
  Phase 4.
- **Watchdog:** no dedicated `NAOMI_COMM` register; any watchdog access would
  appear as an unknown-register write in Flycast's NAOMI debug log — **not
  observed**. Low risk; no watchdog shim indicated so far.
