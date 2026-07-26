# Project status

**Updated:** 2026-07-22 (Phase 5: GAME FULLY PLAYABLE ON REAL HARDWARE —
1P and 2P at full speed, both pads responsive; 2P-slowdown case closed in
round 18. Remaining: fit/integrity spot-checks, then release packaging.)

## What this is

Static binary conversion of *Cleopatra Fortune Plus* (Sega Naomi) to Sega
Dreamcast, Atomiswave-port style: patch the Naomi-specific touchpoints in
the game binary (cart reads → GD-ROM loads, JVS input → controllers,
EEPROM/coin logic → shims) and boot it from a GDI via a custom loader.
Replacement pieces are structured as a small reusable library.
Spec: `docs/superpowers/specs/2026-07-17-phase1-foundation-design.md`.

## Decisions

- Target: real Dreamcast hardware (GDEMU-class ODE). Emulators are dev
  tools, not the goal.
- Personal project first, possible community release later — pipeline must
  stay reproducible and documented.
- A generic trap-based "Naomi runtime" was rejected: Naomi's cart interface
  shares hardware addresses with the DC's GD-ROM drive.

## Phases

1. **Foundation — DONE 2026-07-18** (repo, knowledge base, tooling, boot verification)
2. **Instrumented analysis — DONE 2026-07-18** (cart-streaming map, RAM/serial measurements, input map via instrumented Flycast)
3. **Reverse engineering — DONE 2026-07-18** (Ghidra headless + interpreter-mode dynamic analysis; entry chain, SP verdict, cart-read fn, input fn, EEPROM fn, BIOS verdict — see `docs/kb/boot-binary.md`)
4. **Conversion — DONE 2026-07-20** (loader + freestanding shim + 28-patch
   table → bootable GDI; boots, runs attract, playable 1P+2P with free-play —
   all Flycast-confirmed; see `phase4-conversion.md`)
5. **Real-hardware testing & fit — IN PROGRESS** (run `build/disc.gdi` on a
   real Dreamcast via a GDEMU-class ODE). Five HW boots (2026-07-20) failed
   identically (static swirl → menu, no SEGA license), while **Dolphin Blue
   boots on the user's ODE** (decisive control test). Real defects found and
   fixed each round — **B1** empty IP.BIN track-TOC at 0x100; **B3** makeip's
   hardcoded `CD-ROM1/1` device-info + CD-R bootstrap → donor IP.BIN; **B4**
   boot binary must be in the last data track (both AW ports put 1ST_READ.BIN
   at exactly LBA 450000) — yet the disc still failed, so **B5 max-clone**:
   tracks 1–3 + .gdi are the Dolphin Blue donor's bytes **verbatim**, and the
   whole game (loader + cart at `CART_FAD` 451878) lives in track 4, the only
   remaining delta vs a proven-bootable disc. Along the way: sector size 2048
   confirmed fine (2352 was a wrong guess). **BOOT BLOCKER SOLVED (2026-07-20):
   macOS `._disc.gdi` AppleDouble sidecars poisoned every folder we ever wrote
   (GDEMU picked the junk `.gdi`); after `dot_clean`, the B5 disc boots on real
   HW — swirl, SEGA license, bootstrap → our loader, all loader stages pass
   on-screen (incl. BIOS-syscall GD read).** Next wall: **post-license black
   screen**, dissected 2026-07-21 via an on-screen shim HUD (breadcrumb blocks,
   heartbeats, hex dump of live maple descriptor lists read off the TV by the
   user). Chain of findings: game blanks video during init (hangs look black);
   engine alive from ISR context polling ports B/C (GetCondition) while the
   main thread stalls after the EEPROM-read hook; descriptor walk fixed to
   uncached reads (correct but not the bug); the game runs the full MIE init
   ladder on real HW (reset/devreq/GetId/Z80-firmware upload — now serviced
   byte-exact per Naomi-mode capture; Flycast's HLE boot never enters it); and
   the ACTUAL stall: the settings write-back thunks into the **Naomi BIOS
   0x60000 library which bit-bangs the cart-board EEPROM via 19 unpatched
   0x5f7xxx literals** — spins forever on G1 drive status on a real DC (benign
   reads in Flycast masked it). Fix: patch #29 repoints the single-referenced
   kicker pool word 0x8c081d20 → `shim_ee_write_skip` (return 0 = the game's
   own native "nothing to write" path). **HW re-test: stall UNCHANGED** —
   so the shim grew an on-screen **SPC sampler** (`shim_maple_steady` keeps
   running through the stall, so it paints the interrupted main-thread PC as
   hex row y=68; healthy Flycast signature 0x8c0239xx–0x8c023bxx). The user
   read **SPC=0x8c081224, stable, slot-11 never painted**: the main thread
   pins inside settings-decode helper FUN_8c0811f2 — whose tail
   `bsr 0x8c0803f8` is ANOTHER thunk into the same BIOS library (fn-table
   [0x8c0804d0] slot +0x10), reached BEFORE the patched kicker; three more
   table thunks (0x8c080418/426/456) run unconditionally after it. Round-2
   fix (**33 patches**): hook all five THUNK BODIES → return-0 shim stubs
   (covers bsr + every pool word; 0 = native nothing-changed path — slot
   0x10's return feeds the changed-count accumulator @0x8c081b7e, the trio's
   returns are ignored; callers are settings/credit flows only, neutralized
   by the baked free-play image + per-frame stamp). Flycast-green: 290 cart
   streams, CFG enum, attract, input polls; the EE WR ×16 vanish — they were
   issued from inside the stubbed lib calls, provably not needed. HUD: slot
   11 white = kicker, 12 green = slot-0x10 stub, 13 yellow = post-kicker
   trio; hex rows recolored cyan. **HW round 3: STILL no change — and that
   disproves the whole main-thread-stall model.** The decode helper provably
   cannot spin (its one call is a jump-table memcpy, FUN_8c0947bc; its loops
   are bounded) and unpainted slot 12 proves the thread never passed through
   even once — so SPC=0x8c081224 is the PARKED main thread's resume PC: the
   frame handler that preempted it NEVER RETURNS, spinning in a wait loop
   that keeps pumping the engine (which is exactly why the shim HUD stays
   alive). New instrument (deployed, Flycast-green): row y=82 paints
   `__builtin_return_address(0)` of `shim_maple_steady` = the pump's call
   site inside the spinning loop. Healthy Flycast baseline: ra=0x8c02ed8c
   (scene-loop pump site). **HW round 4: ra=0x8c02ed8c — SAME as healthy.**
   Call-graph dissection (Ghidra): the pump site lives in service
   FUN_8c02ec08 → sole caller = per-frame callback FUN_8c02e7d8
   (vblank-registered via FUN_8c02ea14; also called by engine-RESET routine
   FUN_8c02f082, which masks IRQs, busy-waits a delay, reinits). The
   callback carries a WATCHDOG: consecutive-fail counter 0x8c0e6134 (pump
   rc<0 increments, success zeroes) > 60 → engine reset + reset-count
   0x8c0e6138++. Round-5 probes (deployed, Flycast-green): y68-right = the
   callback's caller via saved-PR stack scan (baseline 0x8c02abd8 = vblank
   dispatcher; 0x8c02f0f4 would mean reset-loop), y82 = fail counter |
   reset count (baseline 0|0; a climbing reset count = watchdog doom-cycle
   confirmed → then identify WHICH transaction keeps failing).
   **HW rounds 5–6: engine EXONERATED** (disc=0x8c02abd8 normal dispatcher,
   fail counter ~0, resets 0) — and the pin is hardware-true: code word at
   0x8c081224 INTACT (0x6162), SGR frozen 0x8c00ef84 (main stack),
   [SGR+0x40]=0x8c081b7c (the predicted orchestrator return). Every vblank
   catches the same context on the same intact instruction ⇒ **eternal
   fault-restart loop** (only interruptible mechanism left). Round 7: tried
   skipping the whole orchestrator (hook @0x8c081aee) — **breaks Flycast
   too** (14 cart reads, no input; the game waits on its completion writes)
   → reverted; blunt skips are off the table. Deployed probe (Flycast-green,
   33 patches): y68 = SPC | **EXPEVT** (0xff000024, fault class; Flycast
   baseline 0x020 stale reset = no exceptions), y82 = **TEA** (0xff00000c,
   faulting address) | SGR. **HW round 8 delivered the confession:
   EXPEVT=0x040 = TLB MISS (read), TEA=0x58c1fc94.** A TLB miss is
   architecturally impossible with MMUCR.AT=0 — and a Naomi game assumes
   AT=0 forever (no TLB handlers installed; VBR=0x8c00f400 with an
   unpopulated +0x400 miss vector = eternal fault-restart, exactly the
   observed pin) and freely uses P0-mirror pointers (0x0c01f100/30 in the
   settings pool). Diagnosis: **KOS leaves the MMU configured; the game's
   first P0 access faults forever on real HW; Flycast doesn't emulate this
   (MMUCR=0 there) — the fourth real-HW-only divergence class of the port
   (D-cache, G1 regs, BIOS-GD state, now MMU)**. Fix (deployed,
   Flycast-green): loader writes MMUCR=0 immediately before handoff +
   shim_maple_steady force-clears MMUCR every tick (self-healing: the pinned
   load's retry succeeds the moment translation is off). Probe rows now
   y68 = SPC | MMUCR-before-clear, y82 = TEA | VBR. **HW round 9: THE MMU
   WAS THE DISEASE — thread walked free** (HUD marched: settings phase +
   enum completed like in the emulator) — then died SOLID RED = shim_die(4)
   = GD read error on the first in-game cart stream. Hardening (deployed,
   Flycast-green): gd_read_sectors now tolerates NOT_FOUND during a 1M-pump
   pickup window (game-context BIOS command queue may briefly report
   not-found before the server picks up; KOS's driver retries, we insta-
   died) + retries each read 3× before failing; shim_die now paints
   code/a/b as cyan hex on the fill (a = cart-relative FAD, b = -1 send
   fail / -2 status fail). **HW round 10: still red — 4 | 0x1000 | -2**: the
   FIRST in-game stream (cart byte 0x800000, a sector the loader-context
   rehearsal read fine) gets its command ACCEPTED but completed-with-error,
   through all retries. Working theory: the boot path (running deeper than
   ever post-MMU-fix) pokes an unmirrored 0x5f7xxx literal = the REAL GD
   drive on a DC (the day-one documented hazard) → drive state wrecked
   before the first stream. Deployed (Flycast-green): KOS-style recovery —
   CMD_INIT(24) drive re-init between read attempts (4 tries; one-time
   stray poke ⇒ one reinit heals the session) + gd_last_err (raw CHECK
   status word) painted as `b` on the death screen instead of -2. If red
   persists, `b` now shows the BIOS's actual error verdict and the next
   move is the FindMmioXrefs sweep for unmirrored literals on the
   handoff→first-stream path. **HW round 11: b=0xcafe0000 — the sentinel
   untouched = every attempt died at SEND (req≤0): the BIOS refuses to even
   ENQUEUE commands.** Diagnosis sharpened: not a wrecked drive — a wrecked
   BIOS. The DC BIOS keeps its GD-syscall state in low work RAM
   (0x8c000xxx); a Naomi game rightfully claims that RAM (VBR=0x8c00f400,
   stack 0x8c00exxx are already inside it) and tramples the state block →
   the queue plays dead. Deployed (Flycast-green): on send-refusal the shim
   calls **gdGdcInitSystem (vector 0x8c0000bc, r7=3, KOS FUNC_GDROM_INIT)**
   — a direct entry that rebuilds the BIOS GD state from scratch even with
   the queue wedged — then CMD_INIT, then retries (marker 0xcafe0002 on the
   death screen if even that fails). Known risk: InitSystem writes low RAM
   back — a reverse collision with live game data is possible; the next
   boot arbitrates. Fallback if it loops: a raw ATA/SPI packet driver in
   the shim (no BIOS dependency at all). **HW round 12: gdGdcInitSystem
   WORKED** — on real HW the game now runs its full steady-state loop
   (MIE frames flowing incl. sub-0x33 input polls, cart streaming with
   activity blinker, SPC varying through the main loop) — but black screen,
   no frame presented. USER spotted the same in Flycast (my trials had
   degraded to counter-checking without screenshots — testing gap).
   **Screenshot bisect (local): the shim's per-tick probe block was the
   presentation killer** — even a bare per-tick MMUCR READ from the engine
   tick blacks Flycast's video present (v4_final black vs v5_noguard
   attract; Flycast quirk, mechanism not chased). The five thunk stubs are
   attract-safe (v3 screenshot). **Final config (deployed, screenshot-
   verified attract + PRESS START + FREE PLAY): 33 patches, SHIM_PROBES=0
   (probe block retired, compiled out; flip to 1 only for a new HW stall
   hunt), MMU protection = loader handoff MMUCR=0 only, GD hardening kept
   (retries/CMD_INIT/gdGdcInitSystem — all inert when healthy). **HW round
   13: ATTRACT MODE ON REAL HARDWARE** — the port runs on a real Dreamcast
   (HUD overlay still on, to be flag-gated). Open: Start button dead on HW
   (works in Flycast) — Phase-5 item I2 (real Maple). MDAPRO exonerated
   (KOS window 0x6155404f covers all 16 MB incl. shim buffers). Deployed
   (attract-verified): one-time Maple bus re-assert (KOS values: DMA_PROT
   0x6155404f, SPEED 0xC3500000, TSEL=0, MDEN=1), one retry per failed
   GetCondition, and CHANGE-driven input diagnostics at y96 —
   [P1raw|P2raw] | [port-A reply header] (idle FFFFFFFF; a Start press
   must flip a bit on the left; hdr low byte 8 = DATATRF healthy, 0 = DMA
   never wrote the buffer). Change-driven paints are video-safe (fly26
   precedent); only per-tick register probing kills Flycast present.**
   **HW round 14: port-A GetCondition reply = FFFFFFFF (the Maple
   no-response marker) consistently** — the DMA demonstrably processes our
   descriptor (it fetched the recv address from it), the frame is
   byte-identical to KOS's and the game's own (01406009 for port B), yet
   nothing answers; the same pad navigates openMenu. Deployed
   (attract-verified): SH4 write-buffer read-back barrier before the DMA
   trigger (correct idiom, though a pure race wouldn't be 100%-consistent)
   + **DEVICE REQUEST (cmd 1) wake probe per port at bus init** — every
   normal flow (BIOS/KOS/menu) DEVINFOs before polling and some pads stay
   silent until probed; reply headers painted at y110 (A | B, healthy low
   byte 5). Input diagnostics y96 now repaint per poll (change-gated
   paints were unreadable — the game overdraws every frame).
   **HW round 15: INPUT WORKS — the DEVINFO wake probe was the fix; BOTH
   pads work, game PLAYABLE on real hardware.** New issue: 2P mode "very
   slow" (HW only; 1P normal). Mechanism: the game requests input per
   player → jvs_digital did 2 live Maple transactions per request = 4+ bus
   busy-waits/frame in 2P (instant in Flycast, ~0.5–1 ms each on the wire,
   plus retry+timeout when a pad is re-polled back-to-back). Fix
   (deployed, attract-verified): per-frame pad cache — both ports sampled
   once per engine tick (stamped by steady_beat), all same-frame requests
   served from cache. **HW round 16 verdicts (rate meters read off the
   TV): DISC INNOCENT (cart reads = 0 during gameplay in both modes —
   everything preloads at boundaries); the pad cache was refreshing ~7/s
   not ~60/s (round-16 hardcoded TCNT0=12.5 MHz without reading the
   prescaler → the "clunky controls"); and with bus+disc idle, the
   remaining 2P drag points at the HUD itself — thousands of uncached VRAM
   writes per frame, free in Flycast, milliseconds on real HW.** Round 17
   (deployed, attract-verified): **clean-screen build** — SHIM_HUD=0
   compiles out all marks/hex (shim_die fatal paints stay unconditional;
   flip SHIM_HUD=1 in util.c for future stall hunts), pad-cache window now
   computed from TCR0.TPSC at runtime. **HW round 17: controls fixed
   (prescaler), 1P good (rare hiccups), 2P much better but still not
   smooth.** Round 18 found the last invisible spender: **per-frame SCIF
   serial** — LISTDIAG's "one-shot" gate (wr_left==32) fires every trigger
   forever now that the EEPROM lib is stubbed; a ~75-char line overflows
   the 16-byte FIFO and spin-waits ~5 ms at 115200 baud EVERY frame (free
   in Flycast, instant drain), doubled in 2P; IN-raw printed a line per
   button press (= the rare 1P hiccups). Deployed (attract-verified):
   SHIM_TRACE=0 silences LISTDIAG / IN-raw / CART-off traces (boot-time
   prints kept; flip SHIM_TRACE=1 to restore). **HW round 18 VERDICT
   (2026-07-22): 2P runs at 1P speed, controls responsive — slowdown case
   CLOSED.** Full attribution chain: maple bus transactions (partial) →
   HUD uncached VRAM writes (major, round 17) → per-frame SCIF FIFO spin
   (final, round 18). Residual: brief dips only DURING heavy clear/combo
   animations, load-shaped. Assessed authentic: Naomi and DC share the
   same 200 MHz SH4 + CLX2 GPU, cart reads measured 0 during gameplay
   (round 16), and with HUD+serial compiled out the shim's steady cost is
   ~2 maple transactions per 8 ms pad-cache window — nothing left that
   scales with animation load. Cross-check if ever desired: arcade
   footage of the same multi-row combos. VRAM overfit (9.2 MB > 8 MB)
   demoted from slowdown suspect to graphics-integrity watch item — it
   would show as wrong/missing textures, none reported. **Disc identity
   (2026-07-23):** the B5 clone shipped Dolphin Blue's IP.BIN metadata
   (serial T0006M → GDMENUCardManager auto-assigned the Dolphin Blue
   cover). `make_gdi.py brand_ip()` now stamps title CLEOPATRA FORTUNE
   PLUS + serial **T-CFP001M** (unique fake — letters in the digit block
   collide with no real JP serial and none of megavolt85's sequential
   T00xxM fan-port series; user chose NOT to reuse the real Altron DC
   *Cleopatra Fortune* T-16603M to avoid mismatching with the retail
   game; cover art is assigned manually) + company **SEGA LC-T-99** (the
   string every megavolt85 AW port carries — read from both donors),
   with the correct device-info CRC (algorithm validated against ChuChu
   Rocket's real header; the AW ports carry a stale CRC and boot fine,
   so the BIOS ignores it). Bootstrap/TOC/FS remain donor-verbatim.
   In-disc 0GDTEX.PVR (the disc art the DC BIOS menu and GDEMU's
   on-device menu show) was still Dolphin Blue's — first spotted by an
   outside tester on GDEMU v5.20.5 (**stuart2773**), who contributed CFP
   cover art.
   **Replaced 2026-07-24** (`make_gdi.py patch_gdtex`): the gitignored
   `0GDTEX.png` (256×256, repo root, optional input) is encoded at
   mastering time to the donor's exact PVR format — RGB565
   square-twiddled, GBIX+PVRT header kept verbatim, byte-length
   identical — and overwritten in place in track03, so FS/extents stay
   donor-verbatim (same principle as brand_ip). Twiddle order (y bits
   even, x bits odd) confirmed from Flycast `core/rend/texconv.cpp`
   `twiddle_slow()`; verified by detwiddle round-trip + region hashes
   (only IP.BIN fields and the art pixels differ from donor).
   Top-level **Makefile** added: `make` (full
   disc), `make release` (GDMENUCardManager zip — embeds the ROM, local
   only), `make deploy` (card copy + dot_clean guard). Flycast
   screenshot-verified.

   **Boot splash (2026-07-23):** the CLEO LOADER stage-text screen is
   replaced by the real Naomi BIOS logo (arcade-boot feel, like the AW
   ports' Atomiswave splash — on arcade HW the BIOS draws it, our
   conversion bypasses that BIOS so the loader stands in). Frame
   captured from Flycast running the Naomi BIOS
   (`scripts/capture_naomi_splash.sh` → gitignored `loader/splash.png`,
   BIOS-derived like the ROM inputs), converted at build time via sips +
   `scripts/bmp2rgb565.py` (stdlib-only), objcopy-embedded, single
   memcpy to vram_s. Stage breadcrumbs live behind `LOADER_QUIET` in
   loader/main.c (flip to 0 if boot ever regresses; halt() red screens
   stay verbose). Splash bytes round-trip-verified; Flycast
   attract-verified (the splash itself is a framebuffer write, which
   Flycast screenshots can't capture — same as the HUD; TV shows it).

   **Final code review — DONE 2026-07-23** (three independent reviewers +
   verification pass; no formal Phase-5 plan doc ever existed, phases 1–4
   only). No defects found in the load-bearing paths (GD recovery ladder,
   handoff copy/cache discipline, linker/loader contract, memory map —
   all verified against built artifacts). Fixed from findings: (1) MIE
   sub 0x19 (transmit-with-repeat) was latched as a transmit at both
   latch sites but shim_die'd in maple_reply — now ACKed like 0x17/0x21;
   (2) the descriptor-walk list base + per-frame recv addresses
   (game-controlled) are now range-guarded to the 32 MB RAM window
   before uncached reads / reply writes (spray armor; inert in normal
   play); (3) an EEPROM-write trace block escaped the round-18
   SHIM_TRACE gate; (4) scif_putc TX spin now bounded (protects
   shim_die's death screen); (5) hook() patches now carry first-opcode
   expectations like every other patch kind (all 7 verified as
   prologue/thunk opcodes); (6) patch_table.h Makefile rule gained its
   two missing deps (shim_iface.h, boot.bin); (7) make_gdi cross-checks
   CART_FAD/CART_SIZE against shim_iface.h, guards IP field lengths,
   all-files donor sentinel, assert-strip refusal; (8) bios_data.bin
   size-checked + .DELETE_ON_ERROR; (9) .gitignore closes *.iso +
   naomi_boot*.png (donor extraction / BIOS-splash frames were
   committable); (10) test_shim_iface wired into `make test` + new
   region-map overlap asserts (the map invariants were previously
   unchecked anywhere); (11) stale comments corrected (loader SP-probe
   bounds, test_host checksum claim). Rebuilt, both host tests green,
   Flycast attract-verified, redeployed to card.

   **VMU-safety tripwire (2026-07-26):** three deterministic checks that the
   port never writes a VMU (spec:
   `docs/superpowers/specs/2026-07-26-vmu-safety-design.md`): `make test` now
   includes the static maple-literal baseline scan (full cart + BIOS slices +
   loader objects; 80+1 hits, zero in the streamed region); `make test-vmu` =
   unattended Flycast canary run (0xA5 canaries must survive, all-zero
   control must get auto-formatted — proves the harness is wired);
   `make test-vmu-play` = same assertions after a headed tester-driven
   session (recommended pre-release). All verified green 2026-07-26,
   including a full user play session through `make test-vmu-play`: all
   canaries byte-identical, control auto-formatted — PASS, no VMU writes.

   **Composite/AV sync fix (2026-07-26, patch #34):** user HW report — VGA
   fine; composite (RetroTink 4K) loses sync exactly when the game takes
   over video (SEGA + loader NAOMI-splash screens fine = KOS's cable-correct
   NTSC 480i still active; RT4K shows the classic 31-kHz-on-15-kHz 2x2
   ghost). Dissected with instrumented Flycast (CLEO-SPG logs grew pc/pr;
   new CLEO-WATCH RAM watch in addrspace.cpp writet): the game **hardcodes
   display mode 0x31** (640x480, monitor-class bits 1:0 = 1 = 31 kHz VGA)
   in its init (mode stored @0x8c0e6298 pc=8c02636e, callers
   pr=8c0262ac/8c026274) → sole pool word **0x8c026570** → SDK display init
   FUN_8c034020 (flags @0x8c0e842c) → handler[mode&3] → SPG via accessor
   FUN_8c03df00 (takeover write pc=8c03df06: SPG_CONTROL 0x150→0x100,
   FB_R_CTRL vclk_div 0→1 = the sync killer). The Naomi DIP-1 monitor
   choice never reaches this path — that decision belongs to the Naomi BIOS
   we bypass (control test: flipping the MIE sub-0x31 DIP reply bit0
   changed nothing). The SDK's class-0 handler FUN_8c0409e0 is a complete
   native **NTSC 480i** builder (field consts 0x106/0x204/0x102 = KOS
   DM_640x480_NTSC_IL; class 2 = PAL 0x35f/0x34b, class 1 = VGA). Fix:
   `shim_vid_init` (util.c) reads the real DC cable (PDTRA bits 9:8, KOS
   vid_check_cable idiom, latched once — game never touches PCTRA/PDTRA,
   zero CLEO-GPIO hits) and clears the mode class (0x31→0x30) on non-VGA
   cables, then tail-calls FUN_8c034020; **patch #34** repoints pool word
   0x8c026570 (sole live ref; full-cart scan = boot mirrors only). The MIE
   sub-0x31 DIP reply now also reports DIP-1 = 15 kHz on TV cables
   (test-menu truthfulness; provably not consulted for video). Flycast A/B
   (Dreamcast.Cable=3 vs 0): composite keeps NTSC 480i through takeover (no
   SPG_CONTROL 0x100, vclk_div stays 0), attract screenshot-verified; VGA
   run bit-identical to pre-fix (0x100, vclk_div=1), attract
   screenshot-verified. `make test` green (34 patches). **Awaiting HW
   verdict: composite sync + VGA regression.**

   **Phase-5 closing items:** graphics/stage-load/sound spot-checks
   during normal play (user reports none so far). **Pre-publication
   (2026-07-23):** full git-history audit — CLEAN (no ROM/BIOS/donor
   data ever committed on any branch; disasm excerpts + the §V-EEPROM
   18-byte decode table judged de minimis and kept by user decision).
   Top-level README.md written for the public source release (inputs
   table, fresh-clone build walkthrough incl. the one-time capture
   harvest, no-redistribution notice, credits). Build-order
   foot-gun fixed in loader/Makefile: patch_table.h now regenerates from
   shim.map (a shim rebuild moves symbols; generating the table first shipped
   pointers into moved functions — deterministic self-inflicted wedge, cost
   half a day). Watch item: thunk 0x8c0803a4 (same table, via trampoline
   0x8c081ae8) is NOT on the boot path and left unpatched — the SPC row will
   name it if it ever bites. Remaining
   watch items: GD-ROM PIO→DMA latency, VRAM ~9.2 MB > DC 8 MB texture fit,
   sound-RAM fit.

## Phase 1 checklist

- [x] Repo scaffolding, CLAUDE.md, this doc
- [x] game.md — parsed ROM header
- [x] naomi-vs-dreamcast.md — architecture delta
- [x] atomiswave-method.md — AW conversion playbook
- [x] Tools installed: Flycast, Ghidra, entrypoint sanity check
- [x] Game boots & plays in Flycast (user-confirmed 2026-07-18: attract mode, free-play + coin, test menu)
- [x] Exit audit + fresh-agent test (passed 2026-07-18 — clean-context agent identified project, state, next step from CLAUDE.md + this doc alone)

## Next step

Phase 5 — real-hardware testing. The port is functionally complete and
Flycast-confirmed (M1–M4, free-play, 2P). The user runs `build/cleo.gdi` on a
real Dreamcast via a GDEMU-class SD-card ODE (build + run guide:
`phase4-conversion.md` §"Running on real hardware"). Test order on HW:

1. **Graphics** — cart-streamed assets render (the C1 cache-coherency fix,
   Task 20, must hold on real cache; Flycast has none, so it cannot confirm).
2. **Streaming** — frame hitches / audio underruns on stage loads (I1: GD-ROM
   PIO latency; the Phase-5 GD-DMA upgrade is the mitigation).
3. **Controller input** — real Maple `GetCondition` (I2: relies on KOS's
   one-time Maple HW setup persisting through handoff).

Then fit: VRAM ~9.2 MB > DC 8 MB (likely texture cuts), sound-RAM fit.

## Key facts so far

- ROM: `Cleopatra Fortune Plus.dat`, ~109 MB decrypted Naomi cart image,
  standard NAOMI header intact.
- The game loads only 1 MB at boot: ROM offset 0x0 → RAM 0x8c020000,
  entrypoint 0x8c04ae2c (header load table).
- The rest of the cart is read at runtime via the ROM-board interface —
  on DC this must become GD-ROM streaming / RAM preload.

### Phase 4 findings (see `phase4-conversion.md`)

Shipped deliverable = **KOS loader (`1ST_READ.BIN`) + freestanding SH-4 shim
(`0x8cfc0000`) + 28-patch table + GDI**. All milestones **Flycast-confirmed**
(real hardware untested — Phase 5):

- **M1 boot, M2 stream, M3 attract, M4 input, free-play, 2P** — all reached in
  Flycast (attract runs, playable 1P+2P, FREE PLAY on-screen).
- **Loader:** reads the 1 MB game image from the GDI (`CART_FAD 47198`), applies
  the 28 old-byte-verified patches, places the shim + two Naomi BIOS-data
  slices, zeros the register mirrors + shim `.bss`, `dcache_purge` + handoff to
  `0x8c04ae2c`.
- **28 patches** (`scripts/build_patch_table.py`): cart/G1 register-mirror (13),
  Naomi BIOS-data pointer redirects (2), async-Maple MIE service (maple-base
  mirror + 2 fn-ptr slots), config-time JVS-enum service (7), forced I/O-spec
  check (1 `insn16`), sync EEPROM-read hook (1), cart-wait hook (1).
- **Divergence from the Phase 3 plan** (documented as the real findings): the
  plan assumed the input fn-ptr path was the whole story. Reality — the game
  reads input/EEPROM/enum through an **async-Maple MIE engine** (config-time +
  per-frame transports); it **needs the Naomi BIOS code/data supplied** in RAM
  (the "no BIOS shim" verdict was wrong); and the **I/O-board enumeration must
  report node-count ≥ 1** before the game emits its per-frame input poll. The
  shim services all three.
- **Real-HW correctness:** the C1 cache-coherency bug (cart-stream dest left in
  D-cache) is **fixed** (Task 20 — P2-uncached dest); invisible in Flycast (no
  real cache), would corrupt graphics on hardware.

### Phase 3 findings (see `boot-binary.md`)

- **Entry chain:** trampoline `0x8c04ae2c` → init `0x8c021000` (resolved by `DumpEntryChain.java`).
- **SP / main RAM:** stack at `0x8c00e6e8`–`0x8c00ef28`; Phase 2 WATERMARK hit near 32 MB was stale data; **main RAM safe on DC 16 MB, no SP relocation needed**.
- **Cart-read fn:** `FUN_8c03bd08` (`0x8c03bd08`–`0x8c03bd4d`) — runtime DMA trigger (computed SB_GDST store); static candidate `FUN_8c08063c` is a separate config-time builder; **Phase 4 patches `FUN_8c03bd08`**.
- **Input fn:** Maple store routine `0x8c0315ce` + `FUN_8c03c2c6`; **Phase 4 shims BOTH to DC `GetCondition`**. (Phase 3 counted 369×/7× on sub-0x15 only — Phase 4 Task 4 showed the steady-state poll is sub 0x33 from `FUN_8c03c2c6`, 23,762× in Phase 3's own capture-pc.log; `boot-binary.md` §5 addendum.)
- **EEPROM fn:** same two Maple sites (sub `0x01`/`0x03`); **Phase 4 forces free-play defaults**.
- **BIOS verdict:** BIOSREF=0 + BIOSEXEC=0 across both captures; **no BIOS-call shim needed**.

### Phase 2 findings (see `cart-streaming-map.md`, `phase2-measurements.md`, `input-map.md`)

- **Cart streaming map:** 388 unique DMA `(cart offset, length, dest)` triples
  captured (attract + demo + play-to-game-over), cart span
  `0x800000`..`0x609c000`; streams almost entirely by DMA (1 PIO seek). Top
  ~12 MB of cart never streamed (known gap). Feeds the Phase 4 GD-ROM reissue.
- **RAM verdict:** main-RAM asset placement 11.2 MB (fits DC 16 MB). The scan
  hit near the top of Naomi's 32 MB was **stale data, not a real stack** —
  Phase 3 pinned the SP low in RAM (`0x8c00e6e8`..`0x8c00ef28` during play; see
  `boot-binary.md` §3), so **main RAM is safe on DC's 16 MB with no SP
  relocation**. VRAM ~9.2 MB (over DC 8 MB → likely texture cuts in Phase 5).
  Sound RAM inconclusive (scan artifact).
- **Input map:** all 7 gameplay controls confirmed to single JVS bits
  (Start 0x8000, Up/Down/Left/Right 0x2000/1000/0800/0400, B1/B2 0x0200/0100).
- **Serial/watchdog:** 0 pokes → no serial or watchdog shim needed.
