# Input map (Phase 2)

The JVS digital-input word as this game reads it, from the instrumented Flycast
`JVSREPORT` log (the flycast fork logs `inputs[0] & 0xffff` —
Player 1's low 16-bit JVS word, once per frame).

**Polarity: active-HIGH.** The idle word (nothing pressed) is `0x0000`
throughout the attract/demo capture (4794/4794 idle reports); a pressed control
*sets* its bit. (The instrumentation source comment says "active-low" — that was
an assumption and is **wrong**; the captured idle value proves active-high. The
comment is cosmetic — the logged values are raw and correct.)

## Bit layout (CONFIRMED by the input pass)

The JVS word is assembled in `tools/flycast-src/core/hw/maple/maple_jvs.cpp`
`read_digital_in` by mapping each internal `NAOMI_*_KEY` bit through the board's
`cur_mapping[]`. For a standard Naomi control panel this produces the standard
JVS layout below. Each control was pressed alone in the input pass
(`capture-input.log`); every one flipped exactly its predicted single bit:

| Control | JVS bit | Pressed word | Confirmed |
|---|---|---|---|
| Start | 15 | `0x8000` | ✓ observed |
| Service | 14 | `0x4000` | not pressed (system byte, not needed for gameplay) |
| Up | 13 | `0x2000` | ✓ observed |
| Down | 12 | `0x1000` | ✓ observed |
| Left | 11 | `0x0800` | ✓ observed |
| Right | 10 | `0x0400` | ✓ observed |
| Button 1 (rotate CCW / select) | 9 | `0x0200` | ✓ observed |
| Button 2 (rotate CW) | 8 | `0x0100` | ✓ observed |

The 7 gameplay controls were captured in press order as
`8000, 2000, 1000, 0800, 0400, 0200, 0100` — each a clean single-bit word,
matching the derived layout exactly. Reproduce: run `scripts/capture.sh input`,
press each control alone, then
`grep '^JVSREPORT' capture-input.log | awk '{print $2}' | awk '!seen[$0]++'`.

**Coin and Test** fall in the JVS *system* byte, outside the logged low-16 word
(`NAOMI_COIN_KEY`/`NAOMI_TEST_KEY` are bits 19/18) — they will not appear in
`JVSREPORT`. They are not needed for gameplay and can be handled separately in
Phase 4.

## Two-player mode (Phase 4 scope note)

The game has a **2-player mode** (user-confirmed). Flycast binds only Player 1
to the keyboard by default, so P2 was not exercised. P2 uses the identical JVS
button layout on the *second* player word (`inputs[1]`); the current
instrumentation logs P1 (`inputs[0]`) only. **Whether to wire up two Dreamcast
Maple controllers for 2-player is a Phase 4 scope decision** — the minimum
viable port is single-player. Recorded here so it is not forgotten.

This resolves `naomi-vs-dreamcast.md §8-2` for the 7 gameplay controls —
bit layout derived from source and confirmed empirically by single-press capture.
