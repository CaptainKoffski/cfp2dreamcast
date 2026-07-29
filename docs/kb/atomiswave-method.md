# The Atomiswave→Dreamcast method

The Atomiswave (AW) → Dreamcast (DC) conversions by **megavolt85** and **yzb**
are the only successful community precedent for running a Naomi-family arcade
platform natively on a stock Dreamcast. This doc extracts their technique as the
template for our Phase 4. It is a *conversion*, not emulation and not a
recompile: the AW game code is the same SH-4 binary, patched at a handful of
touchpoints and repacked into a bootable GDI.

**Why AW is the right precedent — and where it stops being one.** The
Atomiswave is *closer* to a Dreamcast than the Naomi is: same SH-4, same
Holly/PowerVR2, same AICA, **and the same 16 MB main-RAM size** (see §1). That
last point is the crux of §5: the AW porters never had to solve a RAM-size
problem, and Naomi→DC does. So AW gives us the *shape* of the solution (patch
loader + input + settings, repack to GDI) but not the RAM-fit techniques, which
we must invent.

Citation rules follow `docs/kb/naomi-vs-dreamcast.md`: source-code cites are
`file:line` into the sparse MAME clone at `tools/mame/src/mame/sega/`; the
Atomiswave ROM-board register spec traces to Cah4e3's blog (which MAME itself
cites, `awboard.cpp:8-10`); community technique is cited by author and thread.
Emulator/MAME source outranks forum posts. What could not be established from
public sources is in §6, with what was tried — the AW porters never released
their loader source, so several internals are genuinely unknown, and recording
that is itself a finding.

## 1. Atomiswave hardware vs Dreamcast

The Atomiswave is, per MAME's own header, *"basically just a Sega Dreamcast
using ROM carts"* (`dc_atomiswave.cpp:17`). The `atomiswave_state` derives
directly from `dc_state` (the Dreamcast console driver), not from the Naomi
driver (`dc_atomiswave.h:21`). The deltas the AW porters had to bridge:

| Component | Atomiswave | Dreamcast | Delta / port impact |
|---|---|---|---|
| **CPU** | Hitachi SH-4 (HD6417091) | Hitachi SH-4 | None — identical. `dc_atomiswave.cpp:65` |
| **GPU** | PowerVR2 / Holly, mapped `0x5f7c00`/`0x5f8000` | same | None. `dc_atomiswave.cpp:514-515` |
| **Sound chip** | Yamaha AICA, regs `0x00700000`, RTC `0x00710000` | same | None. `dc_atomiswave.cpp:517-518` |
| **Main RAM** | **16 MB @ `0x0c000000-0x0cffffff`** (`0x0d/0e/0f` are mirrors) | **16 MB, same map** | **None — same size, same address.** This is why AW is *not* a RAM analog for Naomi. `dc_atomiswave.cpp:532-535` |
| **Video/texture RAM** | 8 MB, "half the texture memory, like dreamcast, not naomi" | 8 MB | None. Comment is explicit. `dc_atomiswave.cpp:521,523,540` |
| **Sound RAM** | 8 MB @ `0x00800000-0x00ffffff` | 2 MB @ `0x00800000-0x009fffff` | **AW has 4× the sound RAM.** `dc_atomiswave.cpp:519` vs `dccons.cpp:158` (via naomi-vs-dreamcast.md §2) |
| **Game storage** | ROM cartridge on the G1 bus via a bespoke EPR/MPR **filesystem** register set + on-cart ROMEO decryption | GD-ROM disc | **The port problem.** Cart→GD-ROM streaming. See §3. `awboard.cpp:8-159` |
| **BIOS/flash** | 1–4 Mbit Sammy/Sega flash BIOS (`awflash`), settings init | 2 MB BootROM + IP.BIN/1ST_READ.BIN | Different boot path. See §3 boot. `dc_atomiswave.cpp:505-506,846-855` |
| **Input** | **read as standard Dreamcast controllers over Maple** (`DC_CONTROLLER`) | Dreamcast controllers | Near-identical — see §3 input. `dc_atomiswave.cpp:647,818-840` |
| **Coin / service** | JAMMA coin lines at `0x00600280`; extra I/O and coin counters on the **G2 expansion bus** at `0x00600284-0x0060028c` | none (no coins) | Coin/service logic must be neutralized/shimmed. `dc_atomiswave.cpp:447-500` |
| **Settings/high-scores** | battery-backed **SRAM** `0x00200000-0x0021ffff` (128 KB), `NVRAM` device | flashrom (settings via BIOS syscall) + VMU | AW keeps settings in SRAM; on DC this is injected into the disc image offline. See §3 EEPROM. `dc_atomiswave.cpp:508,812` |

**The headline for §5:** the two rows that would force real engineering on a
Naomi port — main RAM and cart interface — are exactly where AW is *unlike*
Naomi. AW main RAM is already 16 MB at the DC address (`dc_atomiswave.cpp:532`),
so the AW porters did zero RAM relocation. And the AW cart interface
(`awboard.cpp`) is a completely different register set from the Naomi cart
interface (`naomibd.cpp`), so their cart-read shim does not transfer verbatim.

## 2. Known conversions & authors

The conversion scene is small and centered on two people:

- **megavolt85** — primary author; ported the majority of the catalog and does
  the reverse-engineering / patching. Long-time DreamShell developer
  (GitHub `megavolt85`, and the `sega-dreamcast/dreamshell` org). Donations via
  the paypal/vk links in the FAQ. (slivercr, "Atomiswave Conversions FAQ",
  dreamcast-talk.com thread t=13621, opening post, 2020-11-16 —
  <https://www.dreamcast-talk.com/forum/viewtopic.php?t=13621>)
- **yzb** — second author; handles several titles and the CDI variants that
  megavolt85 chose not to do (*"megavolt85 has stated that he will not be
  working on CDI versions but will instead leave that to yzb"*, same FAQ).
- **Sonic3D**, **slivercr** — support roles (bugfix packs / the FAQ itself).

**Scale.** Essentially the *entire dumped Atomiswave library* has been converted
to run on stock Dreamcast — the FAQ lists ~25 titles (Guilty Gear Isuka/X1.5,
Fist of the North Star, Metal Slug 6, KOF XI / Neowave, Neogeo Battle Coliseum,
Samurai Shodown VI, Dolphin Blue, Maximum Speed, Faster Than Speed, Ranger
Mission, Rumble Fish 1/2, Sushi Bar, Sega Clay Challenge, Sports Shooting, etc.)
each with its own release thread, and states *"its implied that all games will
eventually be converted"* (FAQ, "RELEASED GAMES" and "Which games are being
converted?"). This is a mature, near-complete body of work — strong evidence the
method is sound, even though its source is unpublished (§4).

Distribution is as **GDI** disc images (megavolt85) and **CDI** (yzb), plus
serial-port SD builds; collections are mirrored on the Internet Archive
(<https://archive.org/details/Atomiswave-Dreamcast>).

## 3. Technique catalog

The FAQ's one-sentence summary of the whole method, attributed to MetalliC and
quoted by slivercr: *"VERY ROUGHLY, only certain portions of the game must be
retouched in order to allow the Dreamcast to run them natively; -input mapping,
-game loading, -SRAM configurations. megavolt85 … implements these functions and
builds a GDI image compatible with the Dreamcast."* (FAQ, "What's the conversion
process?"). Mapped to our six touchpoint categories:

### Boot / loader
- **AW native boot:** the Sammy/Sega flash BIOS (`awflash`, 1–4 Mbit, mapped at
  `0x0`/`0xa0000000`, `dc_atomiswave.cpp:505-506`) initializes the machine, then
  the game is DMA'd from the cart's EPR-ROM. MAME's `init_atomiswave()` even
  patches out the BIOS startup delay and shows the AW BIOS clears SRAM/"ALL
  BACKUP DATA WAS CLEARED" on first boot (`dc_atomiswave.cpp:873-884`).
- **DC conversion:** megavolt85 replaces the boot/load path with his own **"game
  loading"** code and packs it into a GDI that boots the standard DC way
  (IP.BIN → 1ST_READ). The game binary itself is *"mostly untouched"*; only the
  load mechanism is re-implemented (FAQ, "Is it emulation?": *"no source code is
  being recompiled: the target hardware is the same"*). The loader's job is to
  reproduce what the AW BIOS + cart did: get the game code into RAM and hand off.
- **Gap:** the loader is *not published as source* (§4, §6-1). We know it exists
  and what it must do; we do not have its code.

### The animated boot logo — RE'd from two shipped ports (2026-07-30)

**Finding: the Sammy/Atomiswave opening logo + jingle is the *game's own* code,
not the AW BIOS and not something megavolt85 added.** It ships inside each game
binary via Sammy's SDK and survives the port automatically because DC's PVR +
AICA are the same silicon the SDK targets — megavolt85 gets it for free by *not
removing it*.

Method (reproducible; stdlib only, no copyrighted bytes committed): extract
`1ST_READ.BIN` from the two ports we hold (Dolphin Blue, Sushi Bar — both
megavolt85 GDIs) with a GDI-aware ISO9660 reader (high-density volume base
LBA 45000, PVD at track03 file offset `0x8000`, extents are absolute disc LBAs
so `1ST_READ.BIN` at LBA 450000 lives in track04). Each disc holds only two
files: `0GDTEX.PVR` + one monolithic `1ST_READ.BIN` (loader **and** game in one
blob — DB 3,538,016 B, SB 813,056 B). String-scan both:

```
sx_AwLogo Ver 0.90 Build:May 23 2003 13:14:14   opening-logo module (SystemX SDK)
SystemX Library Version 1.01                    Sammy's Atomiswave SDK
Nindows Library by Y.Ito / S.Uchida ...         its graphics lib
AW_LOGO1.PVR AW_LOGO2.PVR AW_LOGO3.PVR (+.PVP)  layered logo textures + palettes
SammyRogo / SammyRogo.pvp                        Sammy logo (rogo = ロゴ)
SREQ_OPENING_LOGO / MIDI_OPENING_LOGO            opening-logo sound request + jingle
```

Present in **both** games but at **different offsets** (DB `sx_AwLogo` @1069037,
SB @272913) → per-game *linkage* of the SDK module, not a fixed blob pasted in
by the porter. Build-dated 2003 (the AW SDK), long before the 2020 ports —
unambiguously original game code. The module renders the logo PVRs and fires
`MIDI_OPENING_LOGO` through AICA as part of the game's own startup.

**Why our Naomi port cannot inherit this.** The Naomi platform logo lives in the
**BIOS** (§1 "Boot/BIOS"; naomi-vs-dreamcast.md §6), runs *before* the game, and
is never in the cart — our conversion bypasses that BIOS, so there is nothing to
keep. Confirmed against our cart: `Cleopatra Fortune Plus.dat` has **zero**
`AwLogo`/`SammyRogo`/`OPENING_LOGO`/`SystemX` hits; it links "SEGA Ninja2
Library" and carries only its own game assets (`ACTBG*.pvr`).

| | Boot logo lives in | Survives the port? |
|---|---|---|
| Atomiswave | the game (SystemX SDK module) | yes — automatically |
| Naomi | the BIOS (separate from game) | no — must be re-created |

So an animated Naomi boot logo is never free for us: it means capture-and-replay
of the Naomi BIOS logo (frames + jingle) driven from our own loader, with the
sound path being the hard/risky half (the loader has no audio today).

### ROM access redirection (the central mechanism)
The AW cart is **not** memory-mapped; like Naomi it is reached through G1-bus
registers, but with a *different, higher-level* scheme than Naomi — a
**filesystem of 64-byte records**, not raw byte offsets. From Cah4e3's spec baked
into MAME (`awboard.cpp:8-159`) and the `aw_rom_board` handlers:

| Register | Addr | Meaning |
|---|---|---|
| `AW_EPR_OFFSETL/H` | `0x5f7000/04` | 32-bit offset into the 8 MB EPR-ROM (header + main program code). `awboard.cpp:12-32,313-323` |
| `AW_MPR_RECORD_INDEX` | `0x5f700c` | index of a 64-byte filesystem record in MPR-ROM; internal DMA offset = `index<<6`. `awboard.cpp:34-44,325-329` |
| `AW_MPR_FIRST_FILE_INDEX` | `0x5f7010` | record index of the first file, used to resolve relative→absolute file offsets. `awboard.cpp:46-58,331-335` |
| `AW_MPR_FILE_OFFSETL/H` | `0x5f7014/18` | 32-bit relative offset within the MPR file-data sub-area. `awboard.cpp:60-77,337-348` |
| `AW_PIO_DATA` | `0x5f7080` | direct PIO word read/write to ROM board (decryption *not* applied); also flash CFI programming. `awboard.cpp:79-90,297-311` |

Two ROM areas: **EPR-ROM** (≤8 MB, header + code) and **MPR-ROM** (≤128 MB,
data), the latter organized as *"linear file system (array of 64-byte records
with file names, file offsets and other information) and file data sub-area"*
(Cah4e3, <https://cah4e3.wordpress.com/2009/07/26/some-atomiswave-info/>).
Transfers go over the same **G1 GD-ROM DMA channel** the Dreamcast uses
(`aw_rom_board` derives from `naomi_g1_device`, `awboard.h:11`;
`dc_atomiswave.cpp:511-512` maps the G1 submap/amap). **Data is decrypted
on-the-fly by the ROMEO ASIC** using an 8-bit key stored in the cart CPLD; the
key is validated by an 8-bit checksum of decrypted code
(`awboard.cpp:26-31,235-258`, `dma_get_position` decrypts each 32-byte DMA chunk
`awboard.cpp:379-392`; per-game keys e.g. `ROM_PARAMETER ":rom_board:key","c2"`
for `fotns`, `dc_atomiswave.cpp:899`).

**How the DC conversion handles it.** The game code still issues these
`0x5f70xx` reads. megavolt85's *"game loading"* shim redirects them to GD-ROM
reads of a repacked data track. Because the MPR-ROM is already a *filesystem*, the
natural conversion is to unpack it to files and re-serve them — which is exactly
what megavolt85's **`AFS_Tools`** (in `github.com/megavolt85/tools_for_DC`, the
`afs tools` dir) is for: AFS is the same 64-byte-record archive format, so those
tools pack/unpack the AW data into the on-disc archive the loader reads. *(This
mapping AFS↔MPR is inferred from the tool's presence + the identical record
structure, not from published loader source — flagged §6-2.)* The concrete
per-game shim (which call sites are patched, exact GD-ROM read requests) is not
public (§6-1).

### Input mapping
- **AW reads inputs as standard Dreamcast controllers.** MAME is explicit:
  *"Atomiswave - inputs are read as standard Dreamcast controllers"*
  (`dc_atomiswave.cpp:647`), wired with `DC_CONTROLLER` maple devices
  (`dc_atomiswave.cpp:818-840`). The button bit layout (SHOT1-5, START, D-pad,
  SERVICE, TEST) is at `dc_atomiswave.cpp:648-661`.
- **So the Maple transport and even the device type match the DC** — this is the
  *easiest* touchpoint and the biggest contrast with Naomi (which uses JVS/MIE).
  megavolt85 still supplies **hardcoded button-remap functions** per game,
  configurable through the game's own AW **Service Menu** (FAQ, "How do I change
  the control scheme?": *"mappings … are functions hardcoded into the game by
  megavolt85 / yzb and cannot be changed [directly] … the service menu can be
  used to change the control scheme"*).
- **Coin/service is the exception:** *"The coin input uses the G2 bus, i.e., the
  expansion port, to receive commands; it doesn't use the controller inputs"*
  (megavolt85, via FAQ) — matching MAME's coin/counter registers at
  `0x00600280-0x0060028c` on the modem/G2 window (`dc_atomiswave.cpp:441-500`).
  This coin path has no DC equivalent and is left unmapped.

### EEPROM / settings
- **AW stores settings + high scores in battery SRAM** at
  `0x00200000-0x0021ffff` (`NVRAM` device, `dc_atomiswave.cpp:508,812`). There is
  **no 93C46 EEPROM and no cart security EEPROM in the AW game-config path** —
  simpler than Naomi (which has both, per naomi-vs-dreamcast.md §5).
- **DC conversion technique — offline SRAM injection.** Since DC has no battery
  SRAM the game expects and settings can't be edited at runtime, the porter
  **bakes an SRAM image into the GDI**. The FAQ documents the exact procedure:
  configure the game in **demul**'s AW service menu, which writes
  `nvram/ROMNAME.sram`, then `dd` that file into a fixed sector range of the
  data track — e.g. Fist of the North Star:
  `dd if=fotns.sram of=track03.iso bs=2048 seek=57644 count=8` (FAQ, "How do I
  change SRAM settings?", per-game `dd` table). Limitation stated by megavolt85:
  *"Game configuration is stored in SRAM, so can't be changed during runtime.
  Instead SRAM configurations must be injected into the GDI before playing."*
- So the DC loader must present that baked SRAM image where the game reads
  `0x00200000` — effectively forcing fixed settings, the same *outcome* the Naomi
  plan reaches by forcing EEPROM defaults (naomi-vs-dreamcast.md §5).

### Sound
- **No adaptation needed at the chip level.** Same AICA, same register block
  `0x00700000`, same RTC `0x00710000` (`dc_atomiswave.cpp:517-518`). Audio "just
  works" — no source or forum post reports sound-specific patching, consistent
  with the FAQ's *"the hardware is essentially the same."*
- **One capacity caveat:** AW sound RAM is 8 MB (`dc_atomiswave.cpp:519`) vs DC's
  2 MB. In practice AW games that fit their samples in ≤2 MB convert cleanly; the
  scene reports no systemic audio-truncation problem, implying AW titles stayed
  within DC's sound budget (not independently verified per title → §6-3).

### RAM relocation
- **None.** AW main RAM is already 16 MB at `0x0c000000`, the identical DC map
  (`dc_atomiswave.cpp:532-535`). The AW porters relocated nothing and re-fit
  nothing. **This category has no AW technique to borrow** — the honest finding,
  and the whole reason §5 exists.

## 4. Tools & source code

| Item | What it is | Link / cite |
|---|---|---|
| **`megavolt85/tools_for_DC` → `afs tools` (`AFS_Tools`, `AFS_Tools_gui`)** | Pack/unpack tooling for the AFS 64-byte-record archive format — the plausible mechanism for repacking AW MPR-ROM data into the on-disc archive the loader serves. Source in `src/`. | <https://github.com/megavolt85/tools_for_DC> |
| **`sega-dreamcast/dreamshell`** (megavolt85 is a core dev) | DreamShell OS; its `firmware/isoldr/loader/` is the ISO loader family megavolt85 works on. Not AW-specific, but the loader lineage the conversions descend from. | <https://github.com/sega-dreamcast/dreamshell> |
| **demul** (AW emulator) | Used *by end users and by the porter* to generate `ROMNAME.sram` via the AW service menu, for injection into the GDI (§3 EEPROM). | FAQ "How do I change SRAM settings?" |
| **MAME `awboard.cpp` / `dc_atomiswave.cpp`** | The authoritative public spec of the AW ROM-board registers, ROMEO decryption, and memory map. | `tools/mame/src/mame/sega/awboard.cpp`, `dc_atomiswave.cpp` |
| **Cah4e3 blog (2009)** | Original reverse-engineered AW ROM-board register spec; MAME cites it directly. | <https://cah4e3.wordpress.com/2009/07/26/some-atomiswave-info/> |

**The critical gap: there is no released source for the actual per-game
conversion loader/shim.** megavolt85's and yzb's conversion code (the input
remap functions, the cart→GD-ROM redirection, the boot loader) is **not public** —
their GitHub repos are unrelated ports (Doom 64, OpenLara, emulators, controller
adapters) and the DreamShell loader is generic, not the AW patch. The conversions
ship as finished GDI/CDI images only. This is the single biggest limitation of AW
as a copy-able template (§6-1).

## 5. What transfers to Naomi, what doesn't

Naomi→DC differs from AW→DC on the two hardest axes. Naomi-side facts below are
cited to `docs/kb/naomi-vs-dreamcast.md` (not re-derived here).

### Transfers directly (technique + rationale carry over)
- **Overall shape: patch a few touchpoints, repack to a bootable GDI, don't
  recompile.** The whole "conversion not port" model is exactly our Phase 4 plan
  (`docs/kb/00-status.md`). AW proves it works on real DC hardware for a whole
  library.
- **Custom loader occupying the 1ST_READ.BIN role**, reproducing the arcade
  boot's "put code in RAM, jump to entrypoint." AW does this; Naomi needs the
  same (naomi-vs-dreamcast.md §6). The *concept* transfers; the load table is
  different (Naomi header `0x360`, naomi-vs-dreamcast.md §6) but simpler.
- **Forcing fixed settings offline instead of live editing.** AW's baked-SRAM
  injection is the same *strategy* as the Naomi plan's forced-EEPROM-defaults
  (naomi-vs-dreamcast.md §5): decide settings at build time, patch them into the
  image. Directly reusable idea.
- **Sound: no work.** Identical AICA on all three platforms
  (naomi-vs-dreamcast.md §1) — as on AW, expect no sound patching.
- **GD-ROM-is-slower-than-cart is a known risk, already flagged by megavolt85**
  (*"access time and reading speed from a GD-ROM may not be sufficient"*, FAQ).
  Our cart is 109 MB streamed at runtime (naomi-vs-dreamcast.md §3), so this
  warning transfers *and intensifies* — Phase 2/5 must watch GD-ROM read latency.

### Needs adaptation (same category, different mechanism)
- **Cart→storage redirection.** Both are G1-bus register schemes DMA'ing into
  RAM, but the register sets differ entirely: AW = EPR/MPR **filesystem**
  (`AW_MPR_RECORD_INDEX`, file offsets, ROMEO decryption; `awboard.cpp`), Naomi =
  raw `ROM_OFFSETH/L` + `DMA_OFFSET`/`DMA_COUNT` byte offsets (`naomibd.cpp`, via
  naomi-vs-dreamcast.md §3). The AW loader's redirection logic must be
  **rewritten** for Naomi's register set — we can copy the *approach* (trap/patch
  the cart reads, serve from GD-ROM) but not the code. Also: our Naomi dump is
  **already decrypted** (naomi-vs-dreamcast.md §7), so unlike AW there is no
  ROMEO/CPLD-key step to reproduce — simpler on that axis.
- **Input.** AW reads *native DC controllers over Maple* — almost no
  translation. Naomi reads **JVS via the MIE** (MIE command `0x86`/`0x15`,
  naomi-vs-dreamcast.md §4), so our input shim must translate a JVS bitmap to DC
  `GetCondition`, which AW never had to do. The *idea* of a hardcoded remap
  function transfers; the decode work is strictly larger for Naomi.
- **Settings storage.** AW = battery SRAM only; Naomi = 93C46 EEPROM + coin/
  free-play logic + cart security EEPROM (naomi-vs-dreamcast.md §5). Same
  "force it offline" strategy, but more surfaces to neutralize on Naomi.

### Doesn't apply (no AW technique exists to borrow)
- **RAM relocation / RAM-fit.** AW main RAM = 16 MB = DC (`dc_atomiswave.cpp:532`);
  the porters relocated and cut **nothing**. Naomi has **32 MB main RAM, 16 MB
  VRAM, 8 MB sound RAM — 2× / 2× / 4× the DC** (naomi-vs-dreamcast.md §1). Every
  RAM-fit problem in our Phase 5 is *outside* what AW ever solved. **No template
  here — this is net-new engineering.**
- **ROMEO/CPLD decryption reproduction.** AW loaders had to deal with encrypted
  carts (`awboard.cpp:235-258`); our Naomi image is pre-decrypted, so this AW
  step is simply irrelevant to us.
- **Coin-via-G2-bus handling.** AW's coin path is G2-expansion (FAQ); Naomi's is
  JVS/MIE. Neither maps to DC; both get neutralized, but by different code.

## 6. Open questions

1. **The AW conversion loader/shim source is unpublished — exact
   cart→GD-ROM redirection and input-remap code unknown.** *Tried:* enumerated
   all `megavolt85` public repos and the `sega-dreamcast` org via the GitHub API
   (found ports/tools/DreamShell but no AW patch); read the full
   dreamcast-talk.com FAQ (t=13621); GitHub code-search for `atomiswave`
   (auth-gated, no results). The conversions ship as finished GDI/CDI only. So we
   have the *method* (§3) from MAME + author forum posts, but not the reference
   implementation. *Resolves in:* our own Phase 3/4 — we implement the redirection
   ourselves against Naomi's register set; AW gives strategy, not code.
   **Partially resolved 2026-07-30** (§3 "The animated boot logo"): we no longer
   have to guess about *one* part of the loader — the animated boot logo/sound is
   NOT in megavolt85's loader at all; it is the game's own SDK code. The
   cart→GD-ROM redirection and input-remap shim remain unknown from the binaries
   (not chased yet), so this stays open for those parts.
2. **Whether `AFS_Tools` is actually the MPR-ROM repack path, or something
   else.** §3 infers AFS↔MPR from the identical 64-byte-record structure and the
   tool living in megavolt85's `tools_for_DC`, but no post states "the loader
   reads an AFS archive." *Tried:* read `awboard.cpp` FS layout and listed
   `tools_for_DC/afs tools`; did not read `AFS_Tools/src` in depth or find a
   linking forum post. *Resolves in:* low priority — we design our own data
   repack for the Naomi cart regardless; this only matters if we wanted to copy
   AW's exact on-disc format.
3. **Per-title AW sound-RAM fit (8 MB→2 MB).** We assert AW games stayed within
   DC's 2 MB sound RAM because the scene reports no systemic audio problem, but
   did not verify any specific title's sample budget. *Tried:* compared MAME AW
   vs DC sound-RAM sizes (`dc_atomiswave.cpp:519` vs naomi-vs-dreamcast.md §2);
   no per-game measurement. *Resolves in:* not on the Naomi critical path (our
   game's audio budget is measured directly in Phase 2), noted for completeness.
4. **Exact boot/entry handoff the AW loader uses.** We know it re-implements
   "game loading" and boots as a normal GDI (FAQ), and MAME shows the AW BIOS
   role (`dc_atomiswave.cpp:873-884`), but not the addresses/entry the loader
   jumps to. *Tried:* MAME `init_atomiswave` + FAQ; loader source unavailable
   (see #1). *Resolves in:* moot for us — our entry/load table comes from the
   Naomi header (naomi-vs-dreamcast.md §6), not from AW.
   **Update 2026-07-30:** the *animated logo* piece of the boot experience is now
   understood (§3 "The animated boot logo") — it's the game's own SystemX-SDK
   opening-logo module, so it needs no loader support at all on AW. The exact
   RAM addresses/entry the loader jumps to are still unconfirmed, but moot as
   noted.
