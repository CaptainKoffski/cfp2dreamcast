# Cleopatra Fortune Plus — Naomi → Dreamcast

A static binary conversion of **Cleopatra Fortune Plus** (Taito, 2003, Sega
Naomi arcade hardware) to the **Sega Dreamcast** — no game source code,
following the techniques of the community Atomiswave→Dreamcast ports, done
with AI heavy-lifting (Claude Code) driving the reverse engineering and
conversion, with a human running the real-hardware test loop.

**Status: fully playable on real hardware.** 1P and 2P at full speed, both
controller ports, free-play, authentic NAOMI boot splash. Verified on a
real Dreamcast with a GDEMU-class SD ODE over VGA, and in Flycast.

As far as we know this is the first community port of a Naomi game to the
Dreamcast (the Atomiswave library was ported by megavolt85, YZB and Sonic3D,
but Atomiswave carts hand the DC a near-native binary; Naomi games need more:
cart streaming, JVS/MIE input, EEPROM and Naomi-BIOS services all have to be
re-provided). The complete investigation — every dead end included — is
documented in `docs/kb/`.

## What this repository contains — and does not

This repo contains **only original work**: the loader, the freestanding SH-4
shim, the patch generator, mastering/capture scripts, and the knowledge base.

It contains **no copyrighted game data**. To build the disc you must supply
your own legally-obtained copies of:

| Input | Path expected | What it is |
|---|---|---|
| Game ROM | `Cleopatra Fortune Plus.dat` (repo root) | decrypted 109 MB Naomi cart image |
| Naomi BIOS | `bios/naomi/epr-21576h.ic27` (extracted from your `bios/naomi.zip`) | Japan bios0; two data slices are embedded at build time, and the boot splash is captured from it |
| Donor disc | `[GDI] Dolphin Blue.7z` (repo root) | the megavolt85 Atomiswave port GDI, used as a proven-bootable disc skeleton (tracks 1–3 + TOC are cloned verbatim; only its IP.BIN metadata fields are re-branded) |

**Do not redistribute built images.** The mastered disc embeds all three
inputs above — Taito's game, Sega's BIOS/boot data, and the donor's data.
Share this source repo, not the output.

## Requirements

Built and tested on macOS (the build uses `sips`, `dot_clean`, BSD `stat`;
Linux would need minor Makefile tweaks). You need:

- **sh-elf toolchain** at `/opt/toolchains/dc` and **KallistiOS** at
  `tools/kos` (with `tools/kos/environ.sh` configured) — setup recipe in
  `docs/kb/tooling.md`
- **python3**, **7zz** (Homebrew p7zip), **git**
- **Instrumented Flycast** at `tools/flycast-src` — clone Flycast at the
  pinned commit and apply the two diffs per `patches/README.md`, then build.
  It adds the capture instrumentation (CARTDMA/MIERESP/… log lines) and a
  headless screenshot hook; needed for the one-time blob harvest below and
  for emulator testing. Also copy `bios/naomi.zip` to
  `~/Library/Application Support/Flycast/data/` so Naomi mode boots.

## Building from a fresh clone

One-time data preparation (all outputs are gitignored, derived from *your*
inputs):

```sh
# 1. Boot image slice the patch generator verifies against (first 1 MB of the cart)
dd if="Cleopatra Fortune Plus.dat" of=tools/boot.bin bs=1M count=1

# 2. Run the game once in instrumented Flycast (Naomi mode) to capture the
#    MIE/JVS traffic the shim replays on DC
scripts/capture.sh attract          # writes capture-attract.log (~90 s run)

# 3. Harvest the reply blobs from the capture
python3 scripts/parse_cart_log.py capture-attract.log --dump-mie build/
python3 scripts/extract_jvs_replies.py capture-attract.log

# 4. Bake the free-play EEPROM image from the captured sub-0x03 reply
#    (recipe + provenance: docs/kb/phase4-conversion.md §V-EEPROM)

# 5. Capture the NAOMI boot splash from your BIOS; pick the full-logo frame
scripts/capture_naomi_splash.sh     # emits naomi_boot_s*.png in the CWD
cp naomi_boot_s6.png loader/splash.png   # frame number may vary

# 6. Optional: disc cover art (what the DC BIOS menu / GDEMU menu display).
#    Drop a 256x256 PNG as 0GDTEX.png at the repo root; the mastering step
#    encodes it to PVR and patches it into the disc. Without it the art
#    stays the donor's (Dolphin Blue). Cover art contributed by stuart2773.
```

Then, and on every rebuild after:

```sh
make            # shim -> patch table -> loader -> mastered GDI in build/
make test       # host-runnable unit tests (split math, JVS, memory map)
make release    # build/[GDI] Cleopatra Fortune Plus.zip for GDMENUCardManager
make deploy     # copy to SD card (CARD=/Volumes/GDEMU/NN) + dot_clean guard
```

`build/disc.gdi` runs directly in Flycast. On real hardware, feed the
release zip to GDMENUCardManager (the disc identifies as `T-CFP001M`,
"CLEOPATRA FORTUNE PLUS").

## How it works (short version)

A KOS-based loader boots from the disc, reads the game's 1 MB boot image,
applies ~33 old-byte-verified patches that repoint every Naomi-specific
touchpoint (cart/G1 registers, Maple/MIE input engine, EEPROM library) at a
freestanding shim placed high in RAM, places two Naomi BIOS data slices the
game insists on reading, disables the MMU (KOS leaves it on; Naomi games
have no TLB handlers), and jumps to the game. At runtime the shim services
cart-DMA requests via GD-ROM BIOS syscalls, answers the game's MIE/JVS
input protocol with real Maple controller reads, and replays captured
EEPROM state for free-play.

The six classes of "works in the emulator, breaks on silicon" divergence
this surfaced — SH-4 D-cache coherency, MMU state at handoff, BIOS GD
syscall state, G1 register semantics, uncached-VRAM write cost, SCIF FIFO
spin cost — are written up in `docs/kb/00-status.md`, which is also the
project's narrative index. `docs/kb/phase4-conversion.md` holds the full
conversion evidence chain; `docs/kb/tooling.md` rebuilds the environment
from scratch.

## Credits

- **megavolt85, YZB, Sonic3D** — the Atomiswave→DC ports that proved the
  approach and provided the donor disc structure.
- **Flycast** — the emulator whose source code answered a hundred hardware
  questions (and, instrumented, produced every capture).
- **KallistiOS** — loader runtime and Maple/GD reference.
- **DragonMinded's netboot tools** — Naomi ROM/EEPROM format documentation.

Not affiliated with or endorsed by Sega, Taito, or Sammy. NAOMI, Dreamcast,
and all game titles are trademarks of their respective owners. This project
is for preservation and interoperability; buy the games and support the
rights holders where possible.
