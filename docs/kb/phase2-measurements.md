# Phase 2 measurements — RAM footprint and serial/watchdog

Measured from the instrumented Flycast (the flycast fork) over
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
| **Video RAM** | — (uploaded via PVR/TA, not cart DMA) | ~~9.2 MB~~ → **7.8 MB** (write-truth, Phase 5) | 8 MB | **RESOLVED — fits, no cuts.** The old content scan was counting the **Naomi BIOS boot screen**: the BIOS parks its framebuffers at `0x800000`/`0xc00000` and the screen drawn there (content to `0x93e738`) survived as stale bytes. Write-truth remeasure (zero VRAM at game handoff, then profile genuine writes — `naomi.cpp cartlog_vram_profile`) shows the game's own writes peak at `0x7cd7d5` (7.8 MB) with **0 bytes at/above 8 MB** in every snapshot, and the game's TA/FB layout lives entirely below `0x800000` too → **fits DC's 8 MB.** See §Video RAM below. |
| **Sound RAM** | — (AICA DMA) | ~~8.0 MB~~ → **2.0 MB** (write-truth, Phase 5) | 2 MB | **RESOLVED — fits, no cuts.** The old backwards *content* scan pegged at the exact 8 MB top — a stale/BIOS byte it cannot tell from a real write. Write-truth remeasure (zero ARAM at game handoff, then high-water + 256 KB histogram of genuine post-handoff writes — `naomi.cpp cartlog_aram_profile`) shows the game writes **only `0x000000-0x1fffff` = exactly 2 MB, 0 bytes above 2 MB** across boot+attract+demo (7 snapshots / 433 cart DMAs, `capture-aram-fit.log`). The full 2 MB loads once at boot as a fixed bank (histogram buckets 8-31 = the 2-8 MB range stay all-zero). The game already targets a ≤2 MB sound config → **sound fits DC's 2 MB.** |

**Verdict for Phase 5:** all three region fits are now **RESOLVED**. Main-RAM
asset placement fits DC comfortably and the Phase 3 stack question is closed —
SP lives at `0x8c00e–f xxx`, no SP relocation needed (`docs/kb/boot-binary.md`
§3). **Sound RAM: RESOLVED** by write-truth remeasure — exactly 2 MB, fits DC
with no sample cuts. **VRAM: RESOLVED** by the same method — the game never
writes at or above 8 MB; the old 9.2 MB figure was stale BIOS framebuffer
content (see §Video RAM below). This refines `naomi-vs-dreamcast.md §1` (turns
the RAM-size deltas from assumption toward measurement). No fit-checks remain.

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
(the flycast fork, `core/hw/naomi/naomi.cpp`). Run: launch the
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

## Video RAM — write-truth measurement (Phase 5)

Same method and run recipe as the ARAM remeasure above: zero VRAM once at game
handoff (the first cart DMA — texture uploads cannot precede the game's first
asset fetch; only the BIOS boot screen is lost), then every non-zero byte in
Flycast's `vram` array is a genuine post-handoff write. `cartlog_vram_profile()`
additionally snapshots the TA/FB layout registers (`VRAMREGS`) because of a
Flycast blind spot: the TA parses display lists into host-side structures and
rendering happens on the host GPU, so ISP/OL buffers and framebuffer pixels
never appear as vram-array *content* — those regions are covered by *layout*
instead. Instrumentation: the flycast fork
(`core/hw/naomi/naomi.cpp`). Evidence: `capture-vram-fit-attract.log`
(boot + attract + demo incl. auto-played gameplay, 433 cart DMAs / 7 snapshots,
2026-08-01).

Result — content high-water **`0x7cd7d5` (7.8 MB)** and **`nz_above8m=0` in
every snapshot**; histogram buckets 32–63 (the 8–16 MB half) stay all-zero
throughout. Not one genuine game write lands at or above the 8 MB line.

Where the old 9.2 MB came from: the **BIOS** parks its framebuffers at
32-bit-path `0x800000` (field 1) and `0xc00000` (field 2) — the first
`VRAMREGS` snapshot records exactly those values — and the boot screen drawn
there (content to `0x93e738`) sat in VRAM as stale bytes that the never-cleared
Phase-2 content scan counted. BIOS residue, not game data; `0x800000` + ~1.2 MB of boot screen ≈ 9.2 MB is
the whole mystery — the same ~1.2 MB the old table called "over".

The game's own layout stays below 8 MB by construction. `VRAMREGS` shows a
double-buffered ping-pong across two 4 MB banks: TA ISP/OL lists at
`0x0`–`0x0729a0` / `0x400000`–`0x4729a0`, framebuffers at `0x0b2000` /
`0x4b2000` — every register value the game writes is `< 0x800000`.
(`fb_w_sof2` keeps the BIOS's `0xc00000` untouched in all three observed
layouts and its region shows zero content — dead residue; the game never
renders a second field in this mode.)

Address-space note: the content scan indexes the `vram` array, which Flycast
addresses in 64-bit-path (texture) space, while FB/TA registers hold
32-bit-path addresses. `pvr_map32()` (`core/hw/pvr/pvr_mem.cpp:289`)
interleaves bits [22:2] and passes bit 23 through, so any 32-bit-path region
below 8 MB maps to physical cells below 8 MB. Both spaces `< 0x800000` ⇒ the
entire working set fits DC's physical 8 MB.

**Verdict: the game's VRAM working set fits DC's 8 MB — no texture cuts
needed.** Corroborated on the real target: 18 real-hardware rounds on an
8 MB DC with no wrong/missing textures (a genuine overfit would have shown
there — `00-status.md` round-18 note).

Confirmed on hands-on gameplay (2026-08-01): a second capture with a real play
round (`capture-vram-fit-gameplay.log`, 490 cart DMAs / 8 snapshots — gameplay
added ~57 cart DMAs of in-game asset streaming over the attract-only run) held
**`nz_above8m=0` in every snapshot**, peak high-water `0x7adbe0` (7.7 MB,
marginally below the attract peak — different stages stream different texture
sets), and the same sub-8 MB TA/FB double-buffer layout throughout (the only
new `VRAMREGS` line is a mid-flip transitional state of that same ping-pong).
This round also re-held ARAM at exactly `0x200000` with `nz_above2m=0`.
**Fully closed — attract and hands-on play agree.**

## Serial / watchdog (resolves §8-4)

- **Serial/network pokes captured: 0** across all runs. The game does not write
  the `NAOMI_COMM_*` serial/network registers → **no serial shim needed** in
  Phase 4.
- **Watchdog:** no dedicated `NAOMI_COMM` register; any watchdog access would
  appear as an unknown-register write in Flycast's NAOMI debug log — **not
  observed**. Low risk; no watchdog shim indicated so far.
