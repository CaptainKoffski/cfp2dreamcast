# Can a game damage the drive? — GD-EMU vs. real GD-ROM

Operational note for hardware testing: whether running our converted disc (or
any game) can physically harm the storage device it boots from. Two very
different devices, two different answers. Every hardware claim carries a
citation; where only a wiki carries a claim it is flagged as such.

**Bottom line:**

- **GD-EMU (and any SD-card ODE): no.** Solid-state, read-only during play.
  No software path — ours or anyone's — damages it. Worst case is a crash or
  a failed boot, which a power-cycle clears.
- **Real GD-ROM drive: no *direct* damage, but it is mechanical and wears.**
  No game command can overdrive the laser or overspeed the motor, but every
  boot spends finite mechanical life on the Dreamcast's single most
  failure-prone part.

## 1. GD-EMU / SD-card ODE — solid-state, cannot be damaged by software

A GDEMU-class ODE (how the converted disc is loaded onto real Dreamcast
hardware — see `00-status.md`, `port-playbook.md` Phase 5) is an SD card plus
a microcontroller that answers the Dreamcast's GD-ROM bus commands. The
mechanical failure modes of an optical drive simply do not exist:

- **No laser, spindle motor, or sled** — nothing for software to wear out or
  drive into a mechanical stop. "Seeking" is just the MCU returning data.
- **Reads only, so effectively zero flash wear.** The Dreamcast never writes
  game data back to the GD-ROM; settings/saves go to the console flash and the
  VMU, not the disc (`naomi-vs-dreamcast.md` §5, flash syscalls cited there;
  our own VMU-safety tripwires exist precisely because writes go elsewhere —
  `port-playbook.md` Phase 6). NAND/SD flash cells are degraded by the
  program/erase cycle of a *write*; read operations do not consume endurance
  ([Kingston, eMMC life-cycle](https://www.kingston.com/en/blog/embedded-and-industrial/emmc-lifecycle);
  [TechTarget, write endurance](https://www.techtarget.com/searchstorage/definition/write-endurance)).
  A play session issues no writes, so it costs the SD card nothing.
- **No power or thermal lever.** The ODE answers a fixed command set at bus
  speed regardless of what the game asks; a game cannot make it draw more
  current or overheat.

The GD-ROM protocol the ODE emulates is a *read-only* memory interface — the
emulated command set is sector/TOC/status reads, not disc writes (Flycast's
implementation, [`core/hw/gdrom`](https://github.com/flyinghead/flycast/tree/master/core/hw/gdrom)).
So a malformed GDI — including a bad conversion of ours — is a **boot problem,
never a hardware risk**.

What actually kills these units is physical/electrical, never a game:
counterfeit boards with bad regulators, ESD, cold solder joints, a
dirty/undervolted PSU, or pulling the SD card mid-read.

## 2. Real GD-ROM drive — mechanical, no direct damage, but it wears

The opposite of the ODE: a spinning optical drive with a laser pickup,
reading in CAV mode at up to 12× ([Wikipedia, GD-ROM](https://en.wikipedia.org/wiki/GD-ROM)).
It is analog, mechanical, and already the Dreamcast's weakest link.

**No software lever for catastrophic damage:**

- **Laser current is a hardware loop, not a game register.** Optical drives
  run automatic power control (APC): a photodiode feedback loop holds the beam
  at constant output and *raises* drive current on its own as the diode ages —
  the game never commands laser power
  ([TI, APC for laser diodes, SLOA360](https://www.ti.com/lit/pdf/sloa360);
  [ProPhotonix, automatic power control](https://www.prophotonix.com/blog/tech-note-automatic-power-control/)).
- **Spindle speed is the drive controller's, not the game's** — CAV servo
  tracks read radius; there is no "spin faster" command a game can abuse
  ([Wikipedia, GD-ROM](https://en.wikipedia.org/wiki/GD-ROM)).

**But wear is real, unlike the ODE:**

- **Seek/stream load is mechanical work.** Heavy disc access keeps the sled
  actuator, focus/tracking coils, and spindle busy. Drives are built for this,
  but pathological access patterns and long runtime hours are cumulative wear —
  aging, not a one-session break.
- **The drive is the known weak point.** GD-ROM laser pickups are widely
  reported as a leading Dreamcast failure, attributed to the GD-ROM format
  pushing off-the-shelf CD optics harder than they were designed for (wiki-level
  claim, primary source not found: [Sega Fandom, GD-ROM](https://sega.fandom.com/wiki/GD-ROM)).
  Independent of any single game, an aging diode weakens and APC compensates
  until it can no longer hold output — end of life. A game only adds hours.
- **Burn quality matters if testing off physical media.** Marginal CD-R burns
  cause read retries and re-seeks — more mechanical work per byte, still not
  sudden damage. The old "burned discs kill the drive" line is mostly myth; the
  real variable is burn quality, not the format.

## 3. Takeaway

No game — ours or any other — can inflict sudden physical damage on either
device. The only real difference is wear cost: a boot from a solid-state ODE
consumes nothing, while a boot from the real optical drive spends a slice of
finite mechanical life on the console's most failure-prone part. That is a fact
about the two media, not a testing risk — the converted disc is safe to run on
both.
