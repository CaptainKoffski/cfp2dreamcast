# Naomi vs Dreamcast — architecture delta

Foundation reference for the port. The Naomi and the Dreamcast are the same
SH-4 machine with the same Holly/PowerVR2 GPU and AICA sound; the deltas are
in RAM sizes, how the game gets its data (cart ROM board vs GD-ROM), how it
reads inputs (JVS via the MIE vs Maple controllers), and what the BIOS leaves
behind. This doc catalogs those deltas so Phase 4 knows exactly which
touchpoints to patch.

Every hardware claim below carries a citation. Source-code cites are
`file:line` into the sparse MAME clone at `tools/mame/src/mame/sega/` (cloned
commit `59e7c0b9c76305458dc5df7817e30346af7a505d`) or into
`tools/netboot/docs/naomi.md` (DragonMinded's Naomi RE notes, commit
`6ccbfdd`). MAME/emulator source outranks wikis; where only a wiki/web source
exists it is cited by URL. Unresolved items are in §8, never guessed.

## 1. Summary table

| Component | Naomi | Dreamcast | Port impact |
|---|---|---|---|
| **CPU** | Hitachi SH-4 (HD6417091) @ 200 MHz | Hitachi SH-4 @ 200 MHz | None. Identical ISA and clock — game code runs unmodified. `naomi.cpp:62` (chip), `naomi.cpp:1205` (`#define CPU_CLOCK (200000000)`); DC clock: [dreamcast.wiki hardware](https://dreamcast.wiki/Hardware_overview) |
| **GPU** | PowerVR2 / CLX2 "Holly" @ 100 MHz | PowerVR2 / CLX2 "Holly" @ 100 MHz | None. Same TA/ISP/DAC register block, mapped identically (`0x5f7c00`, `0x5f8000`). `naomi.cpp:63`, `naomi.cpp:1301-1302`; `dccons.cpp:153-154` |
| **Sound** | Yamaha AICA (315-6232) + ARM7 @ 45 MHz | Yamaha AICA + ARM7 @ 45 MHz | None. Same SPU; AICA reg block at `0x00700000` on both. `naomi.cpp:74`, `naomi.cpp:1304`; `dccons.cpp:156` |
| **Main RAM** | 32 MB @ `0x0c000000-0x0dffffff` | 16 MB @ `0x0c000000-0x0cffffff` (upper 16 MB are mirrors) | **Half the RAM.** Game may assume 32 MB. Phase 2 measured asset placement at **11.2 MB** — fits 16 MB — but a scan finds data ~1 KB below the top of 32 MB (likely a high-address stack) that must be pinned down in Phase 3. See `docs/kb/phase2-measurements.md`. `naomi.cpp:1319`; `dccons.cpp:170-173` |
| **Video RAM** | 16 MB texture/frame RAM | 8 MB texture RAM | **Half the VRAM.** Naomi map exposes 16 MB texture area (`0x04000000-0x04ffffff` = 16 MB). DC has 8 MB. Asset/texture budget likely needs cuts (Phase 5). `naomi.cpp:1312`; DC: [dreamcast.wiki](https://dreamcast.wiki/Hardware_overview) |
| **Sound RAM** | 8 MB @ `0x00800000-0x00ffffff` | 2 MB @ `0x00800000-0x009fffff` | **Quarter the sound RAM.** Audio-sample budget must shrink. `naomi.cpp:1306`; `dccons.cpp:158` |
| **Game storage** | ROM cartridge on the G1 bus (this game: 109 MB image) | GD-ROM disc (~1 GB usable) | **The central port problem.** Cart streamed via ROM-board registers; on DC becomes GD-ROM streaming / RAM preload. See §2, §3. `naomibd.cpp:8-24`; GD-ROM cap: [Wikipedia GD-ROM](https://en.wikipedia.org/wiki/GD-ROM) |
| **Input** | JVS I/O boards → MIE (315-6146) → Maple bus | Maple controllers direct | Game reads inputs via MIE maple command `0x86`/`0x15`; must be shimmed to Maple controller reads. See §4. `mie.cpp:8`, `mie.cpp:47-56`; `tools/netboot/docs/naomi.md:190-196` |
| **Settings storage** | 93C46 serial EEPROM (128 bytes) via MIE + game SRAM | flashrom (128 KB) via BIOS syscall `0x8c0000b8` | Naomi EEPROM read/coin/free-play logic must be shimmed; no coin concept on DC. See §5. `naomi.cpp:2472`, `naomi.cpp:2488`; DC flash: `dccons.cpp:145-146`, [mc.pp.se syscalls](https://mc.pp.se/dc/syscalls.html) |
| **Boot/BIOS** | Naomi BIOS parses cart header, DMAs load entries, jumps to entrypoint | BootROM loads IP.BIN → 1ST_READ.BIN, jumps in | Replace with custom loader that mimics the Naomi header load table. See §6. `tools/netboot/docs/naomi.md:6-7`; [dreamcast.wiki boot](https://dreamcast.wiki/Boot_process) |

## 2. Memory maps

Both machines are SH-4 in the same 29-bit external address layout, split into
"areas". The differences are almost entirely RAM *sizes* and what lives on the
G1 bus (area 0, `0x005f7xxx`). Addresses below are physical (P0/cached region
`0x0c...`); the SH-4 uncached mirror at `0xa0000000` applies to both
(`tools/netboot/docs/naomi.md:127`).

**Naomi** (`naomi.cpp:1289-1336`):

| Range | Contents | Size |
|---|---|---|
| `0x00000000-0x001fffff` | BIOS ROM | 2 MB |
| `0x00200000-0x00207fff` | battery-backed SRAM (high scores etc.) | 32 KB |
| `0x005f6800-0x005f69ff` | Holly system control (CH2/sort DMA) | — |
| `0x005f6c00-0x005f6cff` | Maple bus controller (`0x5f6c04` DMA buffer, etc.) | — |
| `0x005f7000-0x005f70ff` | **cart ROM-board registers (G1)** — see §3 | — |
| `0x005f7400-0x005f74ff` | G1 bus control / GD-ROM DMA channel (SB_GD*) | — |
| `0x005f7c00-0x005f9fff` | PowerVR2 DMA + TA registers | — |
| `0x00700000-0x00707fff` | AICA registers | — |
| `0x00710000-0x0071000f` | AICA RTC | — |
| `0x00800000-0x00ffffff` | **sound RAM** | 8 MB |
| `0x04000000-0x04ffffff` | texture/video RAM (64-bit access) | 16 MB |
| `0x0c000000-0x0dffffff` | **main RAM** | 32 MB |
| `0x10000000-0x13ffffff` | TA FIFO / texture direct path | — |

**Dreamcast** (`dccons.cpp:145-173`):

| Range | Contents | Size |
|---|---|---|
| `0x00000000-0x001fffff` | BootROM (BIOS) | 2 MB |
| `0x00200000-0x0021ffff` | **flashrom** (settings, region, RTC drift) | 128 KB |
| `0x005f6800-0x005f69ff` | Holly system control | — |
| `0x005f6c00-0x005f6cff` | Maple bus controller | — |
| `0x005f7000-0x005f701f` | **GD-ROM drive, ATA cs1** | — |
| `0x005f7080-0x005f709f` | **GD-ROM drive, ATA cs0** | — |
| `0x005f7400-0x005f74ff` | G1 bus control / GD-ROM DMA channel | — |
| `0x005f7c00-0x005f9fff` | PowerVR2 DMA + TA registers | — |
| `0x00700000-0x00707fff` | AICA registers | — |
| `0x00800000-0x009fffff` | **sound RAM** | 2 MB |
| `0x04000000-0x04ffffff` | texture/video RAM | 8 MB (16 MB range, half populated) |
| `0x0c000000-0x0cffffff` | **main RAM** | 16 MB (`0x0d/0e/0f...` are mirrors) |

**Where the game's data appears.** On Naomi the 109 MB cart is *not*
memory-mapped — it is reached through the ROM-board register window at
`0x005f7000` and DMA'd into main RAM (§3). On DC the equivalent bulk store is
the GD-ROM, reached through the ATA drive registers at `0x005f7000`/`0x005f7080`
and the same G1 DMA channel at `0x005f7400`. So the *bytes* land in the same
main RAM the same way (G1 DMA to `0x0c...`), but the *source device* and its
control registers differ — and, critically, they overlap in address space
(§3).

The key RAM-size deltas driving Phase 5: main RAM 32→16 MB, VRAM 16→8 MB,
sound RAM 8→2 MB. The boot binary loads only 1 MB (ROM `0x0` → RAM
`0x8c020000`, entry `0x8c04ae2c`; see `docs/kb/game.md`), so the boot code
itself fits trivially; the risk is the runtime working set, which Phase 2 must
measure.

## 3. Cartridge interface (G1 bus) — the central port problem

**The Naomi cart is not memory-mapped.** This is the single most important fact
for the whole port. Games do not `mov.l` from a cart address; they program a
small register set on the G1 bus and either PIO-read a data port or fire a DMA.
The definitive statement: *"Even though the Naomi has ROM cartridges, it does
not access them through memory mapped interface. Instead, it uses GD-ROM
registers much like the Dreamcast. So, parts of the ROM must be DMA'd over
using the GD-ROM interface hardware before executing."*
(`tools/netboot/docs/naomi.md:7`).

### The `0x005f70xx` ROM-board register set

From ElSemi's documentation baked into MAME (`naomibd.cpp:8-24`) and the handler
implementations (`naomibd.cpp:63-199`):

| Register | Address | Meaning |
|---|---|---|
| `NAOMI_ROM_OFFSETH` | `0x5f7000` | high 16 bits of cart read offset (`naomibd.cpp:10,133-137`) |
| `NAOMI_ROM_OFFSETL` | `0x5f7004` | low 16 bits of cart read offset (`naomibd.cpp:11,139-143`) |
| `NAOMI_ROM_DATA` | `0x5f7008` | **PIO data port**: each 16-bit read returns 2 bytes and auto-advances the offset (`naomibd.cpp:12,145-161`) |
| `NAOMI_DMA_OFFSETH/L` | `0x5f700c/10` | cart source offset for DMA (`naomibd.cpp:13-14,171-181`) |
| `NAOMI_DMA_COUNT` | `0x5f7014` | DMA length **in units of 0x20 (32) bytes** (`naomibd.cpp:15,183-186`) |
| `NAOMI_BOARDID_WRITE/READ` | `0x5f7078/7c` | serial EEPROM on the cart (game-board X76F100, not the main-board 93C46) (`naomibd.cpp:18-19,188-199`) |

**PIO path** (`naomibd.cpp:145-161`): set an offset in `ROM_OFFSETH/L`, then
read `ROM_DATA` repeatedly; each read returns 2 bytes and, if bit 31 of the
offset is set (auto-advance), bumps the offset by 2. This is how a game reads
small, scattered structures.

**DMA path** (`naomibd.cpp:105-131` + `naomig1.cpp:117-149`): set the cart
source in `DMA_OFFSETH/L`, set `DMA_COUNT` (in 32-byte units), then **trigger a
GD-ROM DMA request**. The trigger is *not* a cart-specific register — it is the
shared G1 GD-ROM DMA channel (`naomibd.cpp:24`: *"Then trigger a GDROM DMA
request"*). That channel is at `0x005f7400`: `SB_GDSTAR` (dest address,
`naomig1.cpp:9`), `SB_GDLEN` (length, `:10`), `SB_GDDIR` (direction, `:11`),
`SB_GDEN` (enable, `:12`), `SB_GDST` (start, `:13`). Writing `1` to `SB_GDST`
with `SB_GDEN` set kicks off the transfer, which copies cart bytes straight
into main RAM at `SB_GDSTAR` and raises `DMA_GDROM_IRQ` on completion
(`naomig1.cpp:117-149,58-64`). The high bits of the DMA offset select mode:
bit 31 = auto-advance, bit 30 = enable/decrypt select
(`naomibd.cpp:33-47`). A typical streaming read is: fill `DMA_OFFSET` (with the
`0xa0000000` prefix for auto-advance + 8 MB mode, `naomibd.cpp:60`), set
`DMA_COUNT`, arm `SB_GDEN`, write `SB_GDST`, wait for the IRQ.

### How this game streams its cart

The boot binary loads 1 MB and the remaining ~108 MB are pulled at runtime
(`docs/kb/game.md`). Given the interface above, the game almost certainly issues
DMA reads (offset in `DMA_OFFSET`, length in `DMA_COUNT`, kicked via
`SB_GDST`) to stream assets into main RAM as levels/screens demand. Phase 2's
instrumentation should log writes to `0x5f7000-0x5f7014` and `0x5f7400-0x5f74ff`
to capture the exact offset/length/dest triples the game requests — those become
the GD-ROM read requests on DC. (The precise per-request pattern for *this*
binary is an open question, §8-1, resolved by Phase 2, not guessable here.)

### The address-space collision (why the trap approach was rejected)

On the **Dreamcast**, `0x005f7000-0x005f701f` and `0x005f7080-0x005f709f` are the
**GD-ROM drive's own ATA registers** (`dccons.cpp:149-150`: `ata_interface_device`
cs1/cs0). On the **Naomi**, that same `0x005f7000-0x005f70ff` window is the
**cart ROM-board register set** (`naomi.cpp:1297` → `naomibd.cpp` handlers).
**Same addresses, different devices.** Worse, the DMA trigger both platforms use
is *literally the same* G1 GD-ROM DMA channel at `0x005f7400` (`naomi.cpp:1299`
and `dccons.cpp:151` both map `0x5f7400-0x5f74ff` to G1 control).

That overlap is why a generic trap-based "Naomi runtime" was rejected (see
`docs/kb/00-status.md`): a trap on `0x5f70xx` cannot cleanly tell "this is a
Naomi cart-offset write, redirect me to GD-ROM" apart from legitimate DC
GD-ROM/ATA traffic, because on real DC hardware those addresses *are* the
GD-ROM. Instead the plan is a static binary patch: rewrite the game's cart-read
call sites to call a loader shim that issues real GD-ROM reads, rather than
trapping the address bus. §6 covers the loader side.

## 4. Input path

**Naomi.** Physical controls arrive over a JVS (Jamma Video Standard) serial
chain from external I/O boards. The chain terminates at the **MIE** (Sega
315-6146, a Z80-based MCU) which bridges JVS to the Maple bus
(`mie.cpp:8` — *"MAPLE-JVS bridge Z80-based MCU"*; JVS UART handling at
`mie.cpp:47-56`). The MIE is Maple device `0x20` (`tools/netboot/docs/naomi.md:142`).

**How game code actually sees inputs.** Not by reading a JVS register directly.
The game talks to the MIE over the Maple bus using MIE command `0x86` with
subcommand `0x15` ("query controls"); the MIE replies with a `0x87`/`0x16`
response, 0xE words long, containing the button bitmap and analog values,
including Test/Service (`tools/netboot/docs/naomi.md:190-196`). So input is
*polled on demand via a Maple DMA transaction*, not maintained in a fixed
BIOS-owned structure that the game reads passively. (Whether *this* game wraps
that in its own per-frame poll and where it stashes the result is §8-2.)

**Dreamcast.** Same Maple bus, same Maple DMA controller at `0x005f6c00`
(`naomi.cpp:1296` == `dccons.cpp:148`), but the devices are standard Dreamcast
controllers, not an MIE/JVS bridge. A DC controller answers the standard Maple
`GetCondition` request with its own button/analog format
([mc.pp.se maple bus](https://mc.pp.se/dc/maplebus.html), cited within
`tools/netboot/docs/naomi.md:142`).

**Port impact.** The Maple *transport* is identical, so the DMA plumbing works
unchanged. What differs is the device address and the command/response format:
the game issues `0x86`/`0x15` to MIE device `0x20` and decodes a JVS-style
bitmap; on DC it must issue `GetCondition` to a controller and decode the DC
format. Phase 4 shims the game's input-read function to translate. The exact
JVS bit → button mapping for this game is not documented in MAME at bit
granularity (`tools/netboot/docs/naomi.md:192` says it must be found
empirically) → §8-2.

## 5. EEPROM & settings

**Naomi.** Two separate EEPROMs matter:

1. **Main-board 93C46 serial EEPROM, 128 bytes** — system + game settings
   (coin/credit config, free-play, cabinet orientation, attract sound, plus
   the game's own settings blob). It hangs off the MIE in the same physical
   spot as the DIP switches and is accessed over Maple with MIE command `0x86`,
   subcommands `0x01`/`0x03` (read) and `0x0b` (write)
   (`tools/netboot/docs/naomi.md:131-168`; device instantiated
   `naomi.cpp:2472` `EEPROM_93C46_16BIT` "main_eeprom" and `naomi.cpp:2488`
   `EEPROM_93C46_8BIT` "mie_eeprom"). Layout: two CRC-protected copies each of a
   16-byte system section and a variable-length game section; the game stamps
   its 4-byte serial (header `0x134`) so the BIOS knows to wipe settings when
   the cart changes (`tools/netboot/docs/naomi.md:170-188`).
2. **Cart-board X76F100 secure serial EEPROM** — holds the cartridge serial for
   the BIOS's anti-piracy check, accessed via `BOARDID` registers
   `0x5f7078/7c` (`naomibd.cpp:18-19,188-199`; device `naomi.cpp:2490`
   `X76F100`). Not settings storage; a copy-protection ID.

Game settings persistence at runtime: the game loads the EEPROM via the Maple
commands above, CRC-checks it, and if the serial/CRC don't match, re-initializes
from defaults baked into the cart header (the per-region EEPROM default table at
header `0x1e0`) (`tools/netboot/docs/naomi.md:36-45,198-206`). High scores are
*not* in the EEPROM — they go in the 32 KB battery SRAM at `0x00200000`
(`naomi.cpp:1294`; `tools/netboot/docs/naomi.md:129`).

**Dreamcast.** No 93C46, no cart EEPROM, no coin logic. Persistent settings live
in the **flashrom** (128 KB at `0x00200000-0x0021ffff`, `dccons.cpp:145-146`),
read/written through BIOS syscall vector `0x8c0000b8`
(FLASHROM_INFO/READ/WRITE, [mc.pp.se syscalls](https://mc.pp.se/dc/syscalls.html)).
Region byte at flash offset `0x1a002` (`dccons.cpp:50`). User save data goes to
VMU (a Maple device), not flash.

**Port impact.** Every Naomi EEPROM/coin/free-play touchpoint is meaningless on
DC and must be shimmed. Phase 4 replaces the game's "read EEPROM → parse
settings struct" path with forced sane defaults (free-play, attract as desired),
exactly the technique DragonMinded documents for forcing settings
(`tools/netboot/docs/naomi.md:198-227` — locate the common
EEPROM-parse function, replace the byte-load instructions with immediate loads).
If the game wants persistent settings/high-scores on DC, they can be redirected
to flashrom or VMU, but the minimum viable port just forces defaults.

## 6. BIOS & boot

### Naomi boot sequence

The Naomi BIOS boots a cart like this
(`tools/netboot/docs/naomi.md:6-7,47-56,80-82`):

1. Read the ROM header (first `0x500` bytes) through the simulated GD-ROM
   interface.
2. Read the **main load table** at header `0x360` — up to eight 12-byte entries,
   each `(ROM offset, RAM load address, length)`, terminated by offset
   `0xFFFFFFFF` (`tools/netboot/docs/naomi.md:47-50`).
3. DMA each load entry from cart to main RAM.
4. Jump to the **entrypoint** at header `0x420`, which must lie within a loaded
   region (`tools/netboot/docs/naomi.md:55`).

For *this* game the table is a single entry: ROM `0x0` → RAM `0x8c020000`,
length `0x100000` (1 MB), entrypoint `0x8c04ae2c` (`docs/kb/game.md`). So the
BIOS lays 1 MB at `0x8c020000` and jumps to `0x8c04ae2c`; everything past that
1 MB the game streams itself via §3.

### What the Naomi BIOS leaves resident (matters most for Phase 4)

After the jump, the game runs on bare metal. Typical Naomi startup, per RE:
jump to the SH-4 uncached mirror, enable I-cache/OC via CCR, `memset` working
RAM to 0, init FPU registers, then start loading assets and reading EEPROM
(`tools/netboot/docs/naomi.md:82`). Key point for the port: **the Naomi BIOS
does not install a rich resident syscall layer the game depends on.** The game
does its own hardware setup and talks to the G1 cart, Maple/MIE, and AICA
directly (all the §3–§5 register pokes are the game's own code, not BIOS
calls). The BIOS's resident job is essentially: get the cart header, DMA the
load entries, jump. The battery SRAM (`0x00200000`) and the EEPROM contents are
the only persistent state it hands off.

This is *good news* for the port: because the game is self-contained after the
entrypoint and doesn't call into a Naomi BIOS ABI, we don't have to reimplement
a Naomi syscall table — we only have to (a) reproduce the header-driven load
(put 1 MB at `0x8c020000`, jump to `0x8c04ae2c`) and (b) service the game's own
direct hardware pokes (redirect cart DMA to GD-ROM, MIE input to Maple, EEPROM
to forced defaults).

**RESOLVED Phase 3** (`docs/kb/boot-binary.md` §7): the game's boot binary
makes no calls into Naomi BIOS ROM after the entrypoint. Static
`ScanBiosTargets.java` found zero flow references into BIOS ROM (`BIOSREF=0`);
dynamic logging found zero BIOS-range executions across both captures
(`BIOSEXEC=0`, `no_bios_exec` PASS). Six pool constants in the BIOS address
range are exception-vector offsets (VBR setup) and two potential BIOS data-read
pointers — not call targets. No BIOS-call shim is needed in Phase 4.

### Dreamcast boot sequence

DC BootROM reads the GD-ROM, loads **IP.BIN** (the disc bootstrap /
license/metadata) to `0x8c008000`, which in turn loads **1ST_READ.BIN** (the
game main binary) to `0x8c010000` and jumps in
([dreamcast.wiki boot](https://dreamcast.wiki/Boot_process)). 1ST_READ.BIN is
scrambled by an obfuscation formula as copy protection (same source). DC leaves
a **resident syscall layer** in low RAM `0x8c000000-0x8c007fff`: indirect
vectors at `0x8c0000b0` (SYSINFO), `0x8c0000b4` (ROM font), `0x8c0000b8`
(flashrom), `0x8c0000bc` (misc + GD-ROM), function selected by r7 (and r6 for
the GD-ROM super-vector) ([mc.pp.se syscalls](https://mc.pp.se/dc/syscalls.html)).

**Port impact.** The custom loader must occupy the DC's 1ST_READ.BIN role: it
boots via IP.BIN, then reproduces the Naomi header load — copy the game's 1 MB
image to `0x8c020000` and jump to `0x8c04ae2c` — after installing the §3/§4/§5
shims (GD-ROM read service for cart DMA, Maple input for MIE, forced settings).
The loader can freely *use* the DC BIOS GD-ROM syscall at `0x8c0000bc` to do the
actual disc reads. Note the address separation is clean: the Naomi game loads at
`0x8c020000`, well clear of the DC syscall region (`0x8c000000-0x8c007fff`),
IP.BIN (`0x8c008000`), and the default 1ST_READ.BIN slot (`0x8c010000`) — so
the Naomi image can be placed at its native `0x8c020000` without colliding with
DC boot structures.

## 7. Timers, RTC, misc deltas

- **RTC.** Both have the AICA RTC at `0x00710000` (`naomi.cpp:1305` ==
  `dccons.cpp:157`). Same device; no delta. DC additionally prompts the user to
  set the clock when the flash-stored timestamp is stale
  (`dccons.cpp:36`) — a DC-BIOS behavior, irrelevant once the game is running.
- **Watchdog.** The Naomi main board carries a Fujitsu MB3773 power-supply
  monitor *with watchdog timer* (`naomi.cpp:164`); the earlier board revision
  has an MB3771 without watchdog (`naomi.cpp:163`). If the game kicks the
  watchdog, that path is a no-op on DC and should just be neutralized. Not yet
  confirmed the game uses it → §8-4.
- **DIMM board.** Absent from our setup. A Net-DIMM/GD-ROM DIMM board would
  interpose its own register set at `0x5f703c-0x5f704c` (`naomigd.cpp:53-106`)
  and its own firmware to feed the cart-DMA path. Our source is a plain
  decrypted cart image, so the DIMM board is out of scope; we do not emulate or
  patch it. (It *is* why the interface is "GD-ROM-shaped" in the first place —
  on real DIMM setups the Naomi BIOS DMAs from the DIMM the same way it would
  from a cart.)
- **Serial / network.** Naomi has a MIE-driven RS422/RS232 serial port (CN8)
  and optional ARCNET/Ethernet for linked cabinets (`naomi.cpp:172-178`,
  `naomigd.cpp:48-49`). A single-player fortune/puzzle game is very unlikely to
  use these; if it probes them the shim returns "not present." Not confirmed →
  low-risk, folded into §8-4.
- **DIP switches.** Naomi reads 4 DIP switches (frequency 15/31 kHz, etc.) via
  the MIE (`tools/netboot/naomi/README.md` frequencies/orientations). On DC
  these don't exist; the loader forces 31 kHz / horizontal-or-vertical to match
  what the game's header requires (header `0x42a`/`0x42b`,
  `tools/netboot/docs/naomi.md:68-73`).
- **Cart decryption.** M1/M2/M4 cart types support on-the-fly
  decrypt/decompress selected by DMA-offset mode bits (`naomibd.cpp:38-53`).
  **Our dump is already decrypted** (`docs/kb/game.md`), so the loader reads
  plaintext and ignores these mode bits — do *not* set the decrypt bit (bit 30)
  when reproducing reads.

## 8. Open questions

1. **Exact cart-streaming request pattern for this binary.** §3 establishes the
   *mechanism* (DMA offset/count → `SB_GDST` trigger, or PIO via `ROM_DATA`),
   but not the specific `(cart offset, length, dest RAM)` triples this game
   issues at runtime, nor whether it favors PIO or DMA. *Tried:* read the MAME
   ROM-board handlers (`naomibd.cpp`) and the header load table (`game.md`);
   these give the interface, not the game's usage. *Resolves in:* Phase 2 —
   instrument the emulator to log all writes to `0x5f7000-0x5f7014` and
   `0x5f7400-0x5f74ff` while playing. Must not be guessed; Phase 4 patches
   depend on the real triples.
   **RESOLVED Phase 2** — see `docs/kb/cart-streaming-map.md` /
   `cart-streaming-map.csv`: 388 unique DMA `(cart offset, length, dest)`
   triples captured over attract + demo + a hands-on play pass to game-over;
   cart span `0x800000`..`0x609c000`; game streams almost entirely by DMA
   (1 PIO seek). All three parser self-checks pass. Known gap: top ~12 MB of
   cart never streamed.
2. **JVS/MIE input bit → button mapping for this game.** The MIE `0x86`/`0x15`
   response is a 0xE-word bitmap but the per-bit meaning isn't documented at bit
   granularity. *Tried:* `tools/netboot/docs/naomi.md:190-196` (documents the
   command, explicitly says the bit map must be found empirically) and MIE
   handlers in `mie.cpp`. *Resolves in:* Phase 2/3 — log MIE responses while
   pressing known inputs, or disassemble the game's input decoder (Phase 3).
   **RESOLVED Phase 2** (7 gameplay controls) — see `docs/kb/input-map.md`:
   each control pressed alone flipped exactly its predicted JVS bit
   (Start 0x8000, Up 0x2000, Down 0x1000, Left 0x0800, Right 0x0400,
   B1 0x0200, B2 0x0100; active-high, idle 0x0000). Coin/Test live in the JVS
   system byte outside the logged word — Phase 4 handles them separately.
3. **Does the boot binary ever call into Naomi BIOS ROM after the entrypoint?**
   §6 argues the game is self-contained (RE describes the *common* startup as
   BIOS-independent), but this hasn't been proven for *this* binary. *Tried:*
   `tools/netboot/docs/naomi.md:80-82` (general pattern only). *Resolves in:*
   Phase 3 — check the disassembly for any `jsr`/`jmp` targeting
   `0x00000000-0x001fffff` (BIOS ROM) after `0x8c04ae2c`. If it does call BIOS,
   those routines must be reimplemented in the loader.
   **RESOLVED Phase 3** — see `docs/kb/boot-binary.md` §7: `BIOSREF=0`
   (Ghidra `ScanBiosTargets.java` found zero flow references into BIOS ROM) +
   `BIOSEXEC=0` (dynamic `no_bios_exec` PASS across both captures). No BIOS-call
   dependency, statically or dynamically. Six pool words in BIOS address range
   (`POOLBIOS=6`) are SH-4 exception-vector constants (game's own VBR setup) and
   two P2-uncached BIOS-ROM data pointers — NOT call targets; low-risk Phase 4
   watch item only. **No BIOS shim needed in Phase 4.**
4. **Watchdog / serial / network usage.** Unknown whether the game kicks the
   MB3773 watchdog or probes the serial/ARCNET hardware. *Tried:* board notes in
   `naomi.cpp:163-164,172-178`; no game-specific evidence. *Resolves in:*
   Phase 2 (watch for pokes to those registers) / Phase 3. Low risk — shims can
   no-op these — but flagged so Phase 4 doesn't miss a hang-on-watchdog.
   **RESOLVED Phase 2** — see `docs/kb/phase2-measurements.md`: 0 writes to any
   `NAOMI_COMM_*` serial/network register across all captures, and no watchdog
   register access observed → no serial or watchdog shim indicated.
5. **Precise DC VRAM population (8 vs 16 MB usable).** The MAME DC map declares
   the `0x04000000-0x04ffffff` texture range as 16 MB of address space
   (`dccons.cpp:166`) but retail DC ships 8 MB
   ([dreamcast.wiki](https://dreamcast.wiki/Hardware_overview)); the MAME range
   is address space, not populated RAM. *Tried:* compared MAME map vs published
   DC spec — they describe different things (window vs chips). Treated as 8 MB
   populated for budgeting (§1, §5), to be validated on real hardware in Phase 5.
