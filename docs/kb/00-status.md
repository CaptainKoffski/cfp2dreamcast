# Project status

**Updated:** 2026-08-15 (DreamShell serial-SD boot investigation live — see
the Phase-5 entry; GDEMU path unaffected. Phase 5: GAME FULLY PLAYABLE ON
REAL HARDWARE —
1P and 2P at full speed, both pads responsive; 2P-slowdown case closed in
round 18; composite/AV 15 kHz output fixed (patch #34) and HW-verified on
both cable types. Boot-time reduction (HW-verified): patch #35 trimmed a ~2 s
hardcoded JVS post-RESET settle and the GD-DMA upgrade (SHIM_GD_DMA, I1) made
cart streaming 2.4× faster — post-handoff black screen cut ~9.5→~5 s.
Fit checks all CLOSED 2026-08-01 — sound RAM exactly 2 MB, VRAM never
writes above 8 MB (write-truth remeasures). Loadbar-on-composite bug fixed
2026-08-02 (bar row now cable-dependent — the game's TV mode scans only FB
lines 0..236) and HW-verified on both cable types. Remaining: release
packaging.)

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
   would show as wrong/missing textures, none reported (watch item since
   CLOSED 2026-08-01: write-truth remeasure proved no overfit ever existed —
   stale BIOS framebuffer content; see the Phase-5 entry below). **Disc identity
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
   screenshot-verified. `make test` green (34 patches). **HW verdict
   (2026-07-26): PASS — user tested both AV composite (RetroTink 4K) and
   VGA on the real Dreamcast, both display properly, no issues found.
   Composite sync case CLOSED.**

   **Boot-time reduction — load-timing instrumentation + JVS post-RESET
   settle (patch #35, 2026-08-01):** the post-handoff black screen was
   profiled on real HW with compile-gated on-screen instrumentation — loader
   stage timers (`LOADER_TIMING`, loader/main.c → framebuffer table) and shim
   load-stat counters (`SHIM_LOADSTAT`, single toggle in shim_iface.h;
   cart.c/main.c/util.c paint live cart bytes/reads/GD-ms + a post-handoff
   init timeline via a fg/bg-parameterized `hex_paint_c`). The loader itself
   is ~607 ms (dominated by the 1 MB PIO cart read 378 ms + 34 patch-log SCIF
   lines 183 ms; dbglog routes to scif — scif_detected()==1, empirically
   baud-limited). The real cost is the ~9.5 s black screen AFTER handoff,
   HW-measured: ~4.5 s GD-PIO asset preload (I1) + **~2.1 s a hardcoded JVS
   post-RESET settle** + ~0.9 s early init + ~0.8 s enum→stream + ~1.5 s
   interleaved. The 2.1 s was pinned by disasm (new
   `scripts/ghidra/Decomp.java` — decompile-by-address + callers): the
   config-JVS node-count probe FUN_8c082bc4 double-RESETs the bus
   (FUN_8c0814e8 ×2) then **busy-waits 0x78 (120) vblanks** via the vsync-wait
   FUN_8c0342c0 before enumerating — a real arcade I/O-board reboot settle,
   dead wait on this port (the I/O board is the instant shim; the MIE-txn
   counter measured exactly 2 transactions crossing the gap = the two RESETs,
   zero during the 120-frame wait). **Patch #35** (insn16 0x8c082bec,
   `mov #0x78,r14` 0xEE78 → `mov #0x4,r14` 0xEE04) cuts 120→4 frames (a small
   settle margin, not 0 — tunable knob). **HW verdict (2026-08-01): black
   screen 9568 → 7638 ms (−1.9 s); JVS-enum milestone 2679 → 748 ms;
   streaming (11.4 MiB / 149 reads / 4.5 s GD), controllers, gameplay, and
   attract all unchanged/normal.** Remaining boot-time lever: the ~4.5 s
   GD-PIO preload → the deferred **GD-DMA upgrade (I1)**. The timing
   instrumentation is compile-gated and currently ON (SHIM_LOADSTAT=1,
   LOADER_TIMING=1) for the GD-DMA profiling to follow — flip both to 0 for
   the release build (removes the counters + the loader debug pause).

   **GD-DMA cart streaming (SHIM_GD_DMA, 2026-08-01) — I1 addressed:** the
   post-handoff cart asset preload streamed via polled PIO (~2.5 MiB/s, ~4.5 s);
   the deferred I1 "GD-DMA upgrade" swaps the whole-sector body reads to
   `CD_CMD_DMAREAD` (gd.c `gd_read_sectors_dma`: 32-byte-aligned PHYSICAL dest,
   OCBI dcache-invalidate, same polled EXEC/CHECK completion + retry/reinit
   hardening; partial head/tail + unaligned bodies stay PIO). KOS ABI
   cross-checked (CD_CMD_DMAREAD=17, physical dest, 32-B align). **The blocker
   was the exact "G1-DMA side effect" the original PIO-only choice avoided:**
   real GD-DMA fires the GD-DMA-complete interrupt (SB_ISTNRM bit 14), and the
   game — which runs its cart DMA mirrored + polled — still has its Naomi-legacy
   handler armed and mishandles it. Symptom (HW): the DMA data landed correctly
   (a dest canary + no poll-hang, confirmed via SHIM_LOADSTAT death-screen
   diagnostics) but the game hung after ~14 reads with the frame loop alive.
   **Fix (HW-confirmed): mask GD-DMA (bit 14) from all three ASIC IRQ levels
   (IRQD/IRQB/IRQ9 @ 0xa05f6910/20/30) + ack SB_ISTNRM bit 14 per read** — the
   game never needs a real GD-DMA IRQ (it polls the mirror); vblank etc.
   untouched. **HW verdict (2026-08-01): GD-time 4526 → 1874 ms (2.4×,
   ~6.1 MiB/s), post-handoff black screen ~7.6 → ~5 s; attract, 1P and 2P all
   play correctly.** SHIM_GD_DMA defaults 1 (proven upgrade); 0 = instant PIO
   fallback. Combined with patch #35, the post-handoff black screen went
   ~9.5 → ~5 s. Possible follow-up: async (hook the DMA-kick, not the wait) to
   overlap the transfer with the game's decompression — diminishing returns
   (the shim hooks the completion-wait, by when the game is already blocking).

   **Boot progress bar (SHIM_LOADBAR, 2026-08-01):** release-UX answer to the
   remaining ~5 s black screen (speed levers exhausted: streaming is GD-DMA at
   ~6.1 MiB/s, the rest is game init; async overlap already assessed
   diminishing-returns). `shim_cart_service` paints a 320×6 px bar (util.c
   `loadbar_paint`, same live-scanout idiom as `hex_paint_c`) per cart stream
   until a 10 MiB countdown expires — deliberately UNDER the measured
   ~11.4 MiB boot preload so painting stops for good BEFORE the title
   presents; stage-boundary streams during play can never draw on a visible
   frame (the round-12/17 paint hazards don't apply: paints are load-time
   only). 10 MiB = 320 px << 15, so the fill is a shift, no libgcc divide.
   Flycast attract screenshot-verified (no present regression; the bar itself
   is an FB write Flycast doesn't render — same as splash/HUD, TV shows it).
   `make test` green. SHIM_LOADBAR=0 compiles it out.
   HW round 1 (photo, 2026-08-01): bar visible and working, but the unblank
   exposed the stale loader splash in the scanout FB, washed out, and the
   0x39e7 dark-gray track showed light OLIVE — the game scans the FB in a
   different pixel format than the loader's RGB565 splash bytes. Fix:
   BLACK/WHITE ONLY (0x0000/0xffff are the only colors identical in every
   FB format — no detection needed); first paint blacks out the whole
   stale-splash FB (provably just splash residue: it was what the screen
   showed) → clean splash → black + white-outlined-bar transition.
   HW round 2 (photo): bar looks good, but a splash BLINK flashed between
   the init black and the bar — the first paint unblanked video BEFORE the
   1–2-frame FB clear, so the live scanout showed the stale splash for
   exactly the clear's duration. Fix: blackout + outline run while still
   blanked; unblank moved to after the paint — HW round 3: blink gone,
   user calls the game good. Round-3 polish: `shim_vid_init` now also
   paints the EMPTY bar (loadbar_paint(0)) right after the SDK video init
   it wraps, so the outline appears at video takeover instead of at the
   first cart stream — closes the remaining ~1 s splash→bar solid-black
   gap (re-entry safe: the one-shot blackout is loadbar_paint's own virgin
   latch; if the game re-blanks between vid-init and streaming the outline
   just stays hidden until the first stream — no worse than before).
   Flycast attract screenshot-verified for all four bar versions (no
   present regression; the bar itself is an FB write Flycast doesn't
   render — same as splash/HUD, TV shows it). **HW verdict (2026-08-01):
   works fine, user-confirmed — boot UX case CLOSED** (splash → outlined
   bar → fill climbs → title; no blink, no gap).

   **Loadbar on composite — REOPENED and fixed (2026-08-02):** user HW
   report — bar perfect on VGA, **solid black on composite** (all four
   2026-08-01 HW rounds were VGA-only; Flycast can't render the bar's FB
   writes, so composite+bar had never actually been seen). Root cause via
   instrumented-Flycast A/B (Cable=3 vs 0; CLEO-SPG grew `FB_R_SIZE`/
   `FB_R_SOF1/2` logging): at takeover the game's class-0 TV mode programs
   `FB_R_SIZE=0x0013b13f` → ysize=236, modulus=1, `SOF1==SOF2` — a
   **240-line arcade picture: only FB lines 0..236 are ever scanned** (VGA:
   `0x0017753f` → lines 0..477). The bar lived at lines 417–428 — never
   scanned on TV cables; the screen showed only the shim's own blackout.
   (The loader's KOS 480i is a true 480-line weave — modulus 0x141,
   SOF2=SOF1+1280 — which is why the *splash* shows fine on composite; the
   240-line scanout is the game's own TV mode, not patch #34's.) Fix
   (util.c `loadbar_paint`): bar row is cable-dependent — `yb =
   shim_cable_is_vga() ? 417 : 200` (rows 200–211 keep ~10% bottom margin
   in the 237-line field); outline now repaints every call, not one-shot,
   because the game flips the scanout base between two buffers each vblank
   even during load (CLEO-SPG: SOF1 0xfd000↔0x4fd000) and a one-shot
   outline lands in a single flip buffer. Verified byte-exact in Flycast
   via new `CLEO-VRAMDUMP` (VRAM snapshot at each shim unblank, decoded
   through `pvr_map32`'s 32↔64-bit bank swizzle): composite run — outline
   at rows 200/211 x158–482 + growing fill at the live scanout base,
   nothing at 417–428; VGA run — bar byte-identical at the HW-proven
   417–428, nothing at 200–211. `make test` green. **HW verdict
   (2026-08-02): PASS — user sees the bar on both composite and VGA, game
   plays fine. Composite-loadbar case CLOSED.**

   **DreamShell serial-SD boot — OPEN (2026-08-15, round 2 deployed):**
   testers without an ODE run the GDI via DreamShell isoldr + a serial-port
   SD dongle — a boot path this port had never targeted (official target:
   GDEMU-class ODE). Round 1: with the old serial-debug builds the tester
   got a solid red screen — the dongle's data link IS the SCIF pins, so our
   debug TX corrupted SD reads mid-boot; the serial kill-switch (0e490ed)
   cured that class. Current symptom: Naomi splash + bar outline appear
   (= loader phase + handoff + game init all fine), then the bar never
   fills = the FIRST post-handoff cart stream never completes. Identical at
   isoldr Memory 0x8cfe8000 (high preset) and 0x8cf80000 (custom, in the
   gap above the game's 15.5 MB write watermark), async off, DMA/CDDA off,
   several-minute waits — so NOT a loader-placement trample.
   Evidence (primary source: `sega-dreamcast/dreamshell` master,
   `firmware/isoldr/loader/`, cloned+read 2026-08-15):
   - isoldr redirects GD syscall vectors 0x8c0000bc/c0 to `gdc_redir`
     inside its own image (`gdc_syscall.s`) and serves reads from FAT/SD
     over SCIF SPI (`dev/sd/spi.c`, bit-bang under irq_disable);
     CMD_DMAREAD is emulated incl. dcache_purge of the dest
     (`syscalls.c` data_transfer) — command support + cache coherency are
     NOT the problem; SD builds are -DNO_SD_INIT (no TMU-dependent init on
     the read path).
   - isoldr's server lock is a byte hardcoded at **0x8c00002d**
     (`gdc_syscall.s` gdc_lock — the real BIOS's own GD lock location).
     Both lock-wedge variants DISPROVEN by a new instrumented-Flycast RAM
     watch (fork `addrspace.cpp` LOWRAMWR, first 0x100 bytes of RAM;
     `capture-lowram.log`, DC-mode interpreter run, 200+ streams): the
     game NEVER writes the first 0x100 bytes (every writer pc is BIOS
     code 0x8c000xxx–0x8c00bxxx), and the lock is balanced and =0 at
     handoff.
   - **Leading hypothesis — caller-stack depth:** isoldr runs its whole
     emu (FatFs + SPI + the gdcExitToGame coroutine, which parks/resumes
     continuation frames) on the CALLER's stack. Loader phase = KOS's big
     stack → works (splash, 1 MB image, patches all fine). Game phase =
     the Naomi stack, floor 0x8c00e6e8, ~2 KB observed (boot-binary §3),
     with game ISRs nesting on the live SP between the irq_disabled SPI
     bursts. The real BIOS GD driver is shallow enough to fit (18 green
     GDEMU rounds); isoldr's is not — the dive below the game stack lands
     in live game data, and a wedged ISR/frame mid-read = zero bar ticks,
     frozen screen, placement-independent. Exactly the observed symptom.
   Deployed (round 2, `make test` green, Flycast attract
   screenshot-verified): **SHIM_GD_STACK=1 (default)** — `gdstack.S`
   trampoline runs every GD syscall on a private 16 KB shim stack at
   SHIM_BASE+0x14000..+0x18000 (region asserts extended; harmless on a
   real BIOS, which doesn't care what SP it borrows — fix candidate for
   the whole caller-stack class). **SHIM_GD_DIAG (default 0)** — gd.c
   paints live GD state at rows y=120/134: [send result | last CHECK
   status] and [poll heartbeat | fad]; one TV photo classifies
   send-refused vs stuck-processing vs syscall-never-returned. Tester
   round 2: same DreamShell settings (Memory 0x8cfe8000, async/DMA/CDDA
   off) with the new disc; if still hung, rebuild with
   `make -C shims clean && make DEFS='-DSHIM_GD_DIAG=1'` and photograph
   the two rows. Expectation note: serial SD moves ~150 KB/s — a healthy
   boot preload (11.4 MiB) fills the bar over ~1.5–2 minutes.
   **Round 3 verdict (tester diag photo, 2026-08-15): GD PATH NOW HEALTHY —
   the wall moved into the game.** Screen read req=7, status=2 (COMPLETED),
   heartbeat=0 (completed on first poll), fad=0x6F536. Decode: fad rel
   0x1010 = cart byte 0x808000 = boot-preload **stream #2** (the stream
   program mined from the round-2 capture's MIRRORWR lines: 231 streams;
   #1 = 2 KB table → 0x8c0e6a00, #2.. = 2 KB compressed chunks through a
   fixed window at 0x0c115960). So with the private stack, sends are
   accepted and reads COMPLETE cleanly — the freeze is between stream #2
   returning and stream #3 being requested, i.e. inside the game's own
   decompressor consuming chunk #1. GDEMU-proven code freezing on data ⇒
   the delivered bytes are suspect. **Root-cause candidate found in isoldr
   source: KOS memcpy.S FPU carnage.** isoldr links KOS's optimized
   `memcpy.S`; its big-block path (`kos/src/memcpy.S:499-560`) copies via
   `fmov` pairs through dr0-dr14 AND xd0-xd14 — BOTH FPU banks, saving
   only fr12-15 — and switches FPSCR to paired precision assuming KOS's
   baseline. The real BIOS GD driver is integer-only (why 18 GDEMU rounds
   never saw this); under isoldr, every big serve-path copy runs with —
   and tramples — whatever FPU state the Naomi game holds live across its
   patched cart-wait call. Loader phase was immune (KOS's own FPSCR/regs).
   **Round 4 (deployed, `make test` green, Flycast attract-verified):
   gdc_call grew an FPU QUARANTINE** — saves the game's complete FPU
   context (both banks + FPSCR + FPUL), runs the syscall under KOS-default
   FPSCR 0x00040001, restores everything (gdstack.S; ~136 B on the private
   GD stack, negligible vs a serial read). SHIM_GD_DIAG also grew a
   data-truth instrument: per-stream position-sensitive checksum
   (rotl1-xor) of the delivered bytes painted at (220,120) + a phase cell
   at (220,134) (0xA|n requested → 0xB|n delivered → 0xE|n done). Expected
   checksums (from the ROM): stream1 0x5AB35E19, s2 0xC5A1AA96,
   s3 0x74267D2E, s4 0xCEC68B8C, s5 0xEADC93AF, s6 0x858FFB64,
   s7 0xA902CD97, s8 0x1F742ED0. Tester round 4: diag build, same
   settings — full boot = FPU case closed (ship with SHIM_GD_DIAG=0);
   still frozen = photo gives phase + on-screen vs expected checksum,
   separating wire corruption from a game-internal stall.
   **Round 4 verdict (tester diag photo): DATA IS BYTE-PERFECT, the wedge
   is a GD SEND that never returns.** Screen: ck=0x5AB35E19 = stream #1's
   exact expected checksum (wire corruption disproven; FPU quarantine kept
   as hardening), phase=0xA0000002 (stream #2 requested, never delivered),
   req still 7 with fad already stream #2's — so stream #2's gdcReqCmd (a
   loop-free function) never came back: execution lost inside isoldr, one
   read after it worked. Round-4.5 write-truth measurements (fork watch
   GAMEWR/BANDWR, DC-mode interpreter, unsaturated through 231 streams):
   the game's init runs a top-of-RAM stack 0x8cffd000–0x8d000000 (floor
   0x8cffdf54) and clears 0x8c00c100+, but **never writes
   0x8cf80000–0x8cfc0000 nor 0x8cfd8000–0x8cffd000 once running** (the
   heavy pre-handoff writes there are the real-BIOS boot UI/bootstrap,
   absent under DreamShell). isoldr facts (source): image budget 32 KB +
   1 KB params (isoldr.h:26); **HEAP_MODE_AUTO — the UI default — puts the
   heap at 0x8c001100 for any placement above APP_BIN_ADDR**
   (malloc.c internal_malloc_init_auto) — i.e. in the low BIOS work RAM
   whose game-phase trample is HW-proven (round 11 wedged the real BIOS GD
   state exactly there). Prime suspect for high-placement failures with an
   intact image: isoldr's heap-resident FS state dying in low RAM between
   stream #1 and #2. **Round 5 prescription (no rebuild — settings only):
   ISO Loader Memory = 0x8cf80000 AND Heap = 0x8cf90000** — image and heap
   both inside the measured game-clean 256 KB band below the shim. If it
   boots: ship (diag off). If it freezes with the same signature: both
   surfaces exonerated, next dig is the isoldr coroutine/park mechanics
   under our call pattern.
   **Round 5 verdict: STILL FROZEN, identical numbers, with image AND heap
   in the measured-clean band** — every placement/trample surface is now
   conclusively exonerated (image, heap, lock byte, vector, data, caller
   stack, FPU). Round 6 (deployed): stop inferring, read the wedged
   instruction directly — the vblank ISR provably survives these wedges
   (screen stays lit), so the **SHIM_PROBES SPC sampler from the MMU saga
   is revived** (main.c; painter switched to the unconditional hex_paint —
   shim_hex is SHIM_HUD-gated — and y68-right upgraded MMUCR→EXPEVT:
   0x040/0x060 = TLB miss r/w, 0x0e0/0x100 = address error r/w). Rows:
   y68 = SPC (the wedged thread's PC) | EXPEVT, y82 = TEA | VBR. gd.c grew
   a syscall-phase tracer (row y148): left = syscall IN FLIGHT
   (0xAAAA000n), right = last RETURNED (0xAAAA800n); n: 1=read SEND,
   2=EXEC, 3=CHECK, 4=init SEND, 5=init poll, 6=sysinit — recovery ladder
   now instrumented too. Address decode with Memory=0x8cf80000: SPC in
   0x8cf8xxxx = inside isoldr (offset = position in the isoldr loader
   binary), 0x8c0010xx-0x8c00bxxx = BIOS syscall RAM, 0x8c020000-0x8c120000
   = game image, ≥0x8c120000 = game streamed/decompressed code, 0x8cfc0xxx
   = shim. Tester build: `make -C shims clean &&
   make DEFS='-DSHIM_GD_DIAG=1 -DSHIM_PROBES=1'` (probe paints kill
   Flycast's present — round-12 — so this build is verified boots+streams
   via cartlog, not screenshots; HW-only diagnostics as in probe rounds
   4-11). Same DreamShell settings (Memory 0x8cf80000, Heap 0x8cf90000).
   **Round 6 verdict (tester photo, 2026-08-15): BREAKTHROUGH ×2.** (1) The
   probe build BOOTED THROUGH the old wedge — the bar filled completely,
   ~150 streams served (req id 0x9A, phase E|0x96, CHECK in-flight/returned
   pair matched, EXPEVT=0x020 stale-reset = zero exceptions all run). The
   only semantic delta vs the round-5 freeze is the probe block's per-tick
   MMUCR clear — the round-9 medicine, accidentally reintroduced. (The SD
   isoldr never touches MMUCR — mmu_disable/restore compile only for the CD
   device build — so the exact re-enabler remains unidentified; DreamShell
   itself runs MMU-on.) (2) The NEXT wall showed itself: post-preload the
   main thread spins at SPC≈0x8c032e68 = FUN_8c032e00, a 3-priority
   32-slot × 0x40-byte command-ring ALLOCATOR that returns NULL on pool-full
   — the classic queue-full/consumer-dead spin, precisely when the title
   BGM would start. Consumer = the AICA ARM sound driver. Root cause: a
   BIOS boot hands the game a HALTED ARM (upload driver, then release);
   KOS never quiesces the SPU in normal init (spu_disable only in
   arch_abort, init.c:428) and neither does isoldr — so under DreamShell
   the game boots with DreamShell's own sound driver still RUNNING on the
   ARM, overwrites all 2 MB ARAM under it, the ARM crashes, the rings never
   drain, pool pins full. **Round 7 (deployed, make test green, Flycast
   attract screenshot-verified incl. game sound init): loader calls
   spu_disable() before GD init** (BIOS-equivalent ARM state on every boot
   path; idempotent on GDEMU) **+ gdc_call clears MMUCR after every
   syscall** (the medicine's Flycast-safe mainline home — per-syscall, not
   per-tick, so no round-12 present hazard). Tester round 7: rebuild with
   `make -C shims clean && make DEFS='-DSHIM_GD_DIAG=1'` (diag cells kept,
   probes OFF — this is also the controlled test that the per-syscall
   MMUCR clear suffices without the per-tick one), same DreamShell
   settings. Expected: bar fills ~1.5-2 min → title WITH SOUND → attract.
   If it freezes at the bar again: per-syscall clear insufficient → next
   build gates a per-tick clear behind a DreamShell detect. If it freezes
   post-bar silently: AICA theory needs the SPC probes back.
   **Round 7 verdict: REGRESSION — spu_disable() froze the game's EARLIEST
   init** (bar 0%, and with probes on NOT ONE cyan row painted = the vblank
   engine never started; gdc_call never ran, so the loader's spu_disable is
   the only live change). RE decode of the game's sound boot (Ghidra;
   FUN_8c02a4f4): a full SEGA-SDK **"SDRV"** driver boot — validates the
   blob magic 0x56524453 "SDRV" (version window 2.50–2.xx) from the 1 MB
   image, then ARM halt (FUN_8c038948, RMW 0xa0702c00|1) → ARAM ops →
   blob copy → ARM run (FUN_8c0389b8, &~1) → **three blocking waits**: the
   heartbeat word (ARAM addr = [[0x8c0e6898]+0x98]+0x18, reader
   FUN_8c03a3a4) must leave magic "DMPD" (0x44504d44), then count past 10,
   then a state field must read "EXEC" (0x43455845). This boot provably
   TOLERATES a foreign driver running at entry (round 6 booted through with
   DreamShell's driver live) but NOT our long-pre-held ARM +
   spu_reset_chans state — mechanism unresolved; spu_disable REVERTED
   empirically. The round-6 post-preload allocator pin therefore happened
   with a driver that HAD passed its boot handshake — whether the ARM died
   later or the SH4-side pump stopped is exactly what round 8 measures.
   **Round 8 (deployed, make test green, mainline attract re-verified, diag
   build boots 132 streams in Flycast): spu_disable reverted + new
   SHIM_PROBES liveness cell** — y96: [0x8c0e6898] sound ctx | ARM driver
   heartbeat (ticking = alive; frozen = dead; 0xBADxxxxx = handshake chain
   not valid yet). Tester round 8:
   `make -C shims clean && make DEFS='-DSHIM_GD_DIAG=1 -DSHIM_PROBES=1'`,
   same DreamShell settings. Expected: round-6-like boot (bar fills) →
   post-preload pin → photo of y96 decides ARM-dead vs pump-dead.
   **Round 8 tester result:** bar fills → same post-preload pin, bottom
   diag IDENTICAL to round 6 (req 0x9A, fad 0x7A331, phase AAAA0003 /
   AAAA8003, checksum 2FB0FC09, E0000096; SPC cycling 8C0xxxxx = the
   FUN_8c032e00 allocator spin) — the wall is stable and reproducible.
   New cells: TEA=0, EXPEVT=0x20 (manual-reset leftover, benign),
   VBR=8C00F400. y96: ctx=**8C0F3B80** (valid; init flag=1 → the game's
   SDRV boot COMPLETED under DreamShell), hb=**BAD10001** — which is a
   PROBE BUG, not a dead driver: [ctx+0x98] holds an **ARAM OFFSET**
   (0x0001xxxx ≈ 64 KB in), not an absolute pointer. Primary source:
   FUN_8c03a3a4 passes [ctx+0x98]+0x18 to ARAM-word-reader FUN_8c039688,
   whose literal pool holds bound 0x00200000 (2 MB ARAM) and base
   0xa0800000 (boot.bin file offs 0x196cc/0x196d0). Round 8's probe
   demanded a 0x008xxxxx pointer and painted BAD1 over a healthy offset.
   **Round-7 verdict RETRACTED:** tester reports both round-7 builds were
   made WITHOUT `make -C shims clean` — stale shim objects explain the
   missing digits (and possibly the 0% bar, if the stale objects predated
   the MMU medicine). "spu_disable froze earliest init" is unproven; it
   stays reverted anyway since round 8 proves the game's SDRV boot
   completes over DreamShell's live driver, so the quiesce buys nothing.
   **Build hygiene (tester-confirmed foot-gun, bit twice): the shim
   Makefile does not track CFLAGS — ALWAYS `make -C shims clean` when
   DEFS change.**
   **Round 9 (deployed): heartbeat probe fixed** — reads
   0xa0800000 + [ctx+0x98] + 0x18 with the game's own validity checks
   (offset < 0x200000, 4-aligned). Tester round 9: same build command +
   settings as round 8; photo of the y96 RIGHT cell at the pin, twice a
   few seconds apart — CHANGING value = ARM driver alive → fix targets
   the SH4-side sound pump; FROZEN value = driver died post-handshake →
   fix targets ARM/ARAM state.
   **Round 9 tester result: ARM DRIVER ALIVE.** Heartbeat 0x5B20 →
   0x6AF9 across two photos seconds apart (~1 kHz count); spin PC samples
   8C033C5A/5E; rest identical to rounds 6/8. The wall is SH4-side.
   **RE of the SH4 side (Ghidra, boot.bin):** the "sound-command rings"
   are the SDK's generic DMA-TRANSFER-REQUEST queue — head 0x8c0fb8e0,
   3 priorities × 32 slots × 0x40 B (ring offsets +0x40/+0x840/+0x1040;
   per-ring write idx/read idx/count at +0x2c..+0x3c; slot: +0 seq,
   +4 flags (bit3 = channel B), +8 mode 0-7, +0xc len, +0x10 src,
   +0x14 dst, +0x1c expected-event mask, +0x1e arrived-event mask,
   +0x28 state, +0x30 chunk count, +0x34/+0x38 chunk strides).
   Spin site FUN_8c033c50 polls FUN_8c032fe0(last-seq [0x8c0faaa4])
   until the seq leaves all rings (0x101 = gone, 0x100 = queued).
   Allocator FUN_8c032e00; pump FUN_8c033400 (one in-flight per ring at
   +0x20/+0x24/+0x28, channel slots +0x18/+0x1c); transport FUN_8c033160
   programs, per slot mode: SH4 DMAC ch2 in DDT (SAR2 0xffa00020,
   DMATCR2=len>>5, CHCR2=0x12c1, DMAOR=0x8201) + Holly ch2-DMA
   (SB_C2DSTAT 0x5f6800/LEN/ST) or PVR-DMA (SB_PDSTAP 0x5f7c00 block,
   SB_PDAPRO unlock 0x6702007f; init also sets SB_LMMODE0=0,
   SB_LMMODE1=1). Slots are freed ONLY by the DMA-end interrupt
   callback 0x8c0473c0 (registered via FUN_8c03e5a0 in FUN_8c0400e0 for
   event IDs 0x11-0x13,0x15-0x19 + 0x1b/0x1c→0x8c047668): each event
   ORs its bit into arrived (+0x1e); five consecutive IDs 0x15-0x19 get
   bits 0x10,8,4,2,1 = ISTNRM bits 15-19 (AICA/Ext1/Ext2/Dev/ch2-DMA
   end), 0x11=PVR-DMA end (bit 11); slot completes when
   arrived ⊇ expected (+0x1c); mode-4 multi-chunk slots reprogram the
   DMA inside the ISR. So ONE undelivered DMA-end interrupt (or one
   wedged/blocked DMA) under DreamShell pins the rings full forever
   while vblank + ARM keep running — exactly the observed state.
   **Round 10 (deployed): DMA/interrupt autopsy rows** below the GD
   diag: y162 ISTNRM | DMAOR<<16.C2DST<<8.PDST<<4.ADST; y176 DMATCR2 |
   CHCR2; y190 in-flight slot ptr | expected<<16|arrived (no slot: 0 |
   0xC0.c0.c1.c2 ring counts). Same build command + settings. Decode:
   ISTNRM DMA-end bit pending forever + engines idle → interrupt
   delivery broken (mask/hook); C2DST or PDST stuck 1 / DMATCR2 > 0 →
   engine wedged (suspects: DMAOR AE/NMIF, SB_PDAPRO/G2APRO protection
   under DreamShell); all idle + arrived≠expected → completion event
   consumed/lost before the game's dispatcher saw it.
   **Round 10 tester result = ROOT CAUSE.** ISTNRM=0x10 (transient
   vblank only), DMAOR=0x8201 healthy, C2DST/PDST/ADST all 0,
   DMATCR2=0, CHCR2=0x12C0 (DE/TE cleared by the game's own ID-0x12
   ack) → the DMA hardware COMPLETED. In-flight slot 0x8C0FB960
   (prio 0) pinned with **expected=0x8002, arrived=0x8001**. Decoding
   via the SDK's event-ack table (0x8c0cefb8, found through dispatcher
   0x8c0402a0; each ID 0x10-0x1f → [reg, RW1C ack value]): ID 0x12 =
   ISTNRM bit 19 ch2-DMA end + CHCR2 &= ~3 (matches the observed
   0x12C0); ID 0x13 = bit 6 TA/YUV transfer end (cb bit 0x8000);
   **ID 0x18 = bit 8 End-of-Transfer OPAQUE-MODIFIER list (cb bit
   0x2); ID 0x19 = bit 7 End-of-Transfer OPAQUE list (cb bit 0x1)**
   (bit names: flycast holly_intc.h:21-23). So the pinned transfer is
   the game's first TILE-ACCELERATOR display-list DMA — NOT sound (the
   'sound-command ring' label from rounds 6-9 is retracted; the queue
   is the SDK's generic transfer manager, and the sound path was
   exonerated by the ticking ARM heartbeat). The game submitted an
   opaque-MODIFIER list and waited for bit 8; the TA raised bit 7
   (OPAQUE) instead. Mechanism (flycast ta_vtx.cpp startList):
   `if (CurrentList != ListType_None) return true` — **a TA list left
   OPEN makes the TA ignore the new list-start and finish the STALE
   list type**. DreamShell renders its UI with the TA and isoldr never
   resets it; the GDEMU path goes through the BIOS boot which does.
   The game (written for BIOS-fresh Naomi hardware) inherits an open
   opaque list → wrong end-of-list event → transfer queue pins full →
   the 100%-bar hang. Every symptom back to round 1 fits.
   **Round 11 (deployed): THE FIX** — loader handoff (after
   irq_disable, next to the MMUCR clear) pulses PVR SOFTRESET
   0x005f8008 = 3 → 0 (TA bit0 + render pipeline bit1; SDRAM bit2
   untouched; flycast pvr_regs.cpp:146 `data & 1 → ta_vtx_SoftReset()`)
   and clears stale latched Holly events (ISTNRM/ISTERR = RW1C,
   0xffffffff). Loader never uses the TA (framebuffer splash only).
   Probes kept ON for the confirmation run. Tester round 11: same
   build command + settings. Expected: full boot → title WITH sound →
   attract/game. If it still pins: photograph the same probe rows
   (y162/y176/y190) — the expected/arrived pair will say what changed.
   **Round 11 tester result: NO CHANGE** — identical readings
   (expected=8002/arrived=8001). Theory falsified; Flycast baseline
   (fork logging: C2D DMA w/ first source word, TAREG/TAEND, PVR reg
   writes) explains why: **the game pulses SOFTRESET=1→0 +
   TA_LIST_INIT ITSELF every frame** — the handoff reset was
   redundant. Healthy per-frame choreography: C2D opaque
   (w0=80000000, ~0x3460 B) → STARTRENDER → modifier (w0=81000000,
   **len 0x40** = one global + one EOL) → translucent (82000000,
   ~0x720) → transmod (83000000, 0x40); the two 0x40-byte modifier
   lists are CPU-written moments before queueing. **Leading
   hypothesis: stale bytes delivered to the TA** — a stale ZERO first
   word decodes as End-Of-List with ListType=0, which makes the TA
   raise OPAQUE end (flycast ta.cpp: EOL with cl==7 →
   cl=pcw.ListType) = exactly arrived 8001, twice-opaque, no
   modifier-end, first frame only. Cache-policy audit: KOS
   CCR_DEFAULT (tools/kos dc/cache.h:42) includes **CCR_CB = P1
   copy-back**, and the game's boot stub (0x8c0210f4, mask 0x89af |
   0x800) READ-MODIFY-WRITES CCR — it inherits the boot
   environment's cache policy wholesale. BUT our KOS loader runs on
   both paths (arch_main → cache_write_ccr(CCR_DEFAULT)), so CCR
   should be path-identical — needs the live HW reading to close.
   **Round 12 (deployed): pinned-slot autopsy** — y162 ISTNRM | slot
   ptr; y176 mode<<24|flags<<16|chunks | expected<<16|arrived; y190
   src | dst; **y204 PCW@src read via P1 (cache) | via P2 (RAM
   truth)** — divergent cells = dirty-line/writeback proof, both
   zero = the game never built the list, both 81000000 = data fine →
   TA-side; y218 CCR | TA_ALLOC_CTRL. Same build command + settings;
   photo of all five rows at the pin.

   **Round 12 HW result (2026-08-16):** ISTNRM 00000010 (vblank latch only) |
   slot 8C0FB960; mode/flags/chunks 0009AAC0 | expected/arrived **8002 8001**
   (same pin); src **8CB80000** | dst 10000000 (TA FIFO); **PCW@src P1
   00000000 | P2 00000000** — cache and RAM agree, both zero → **stale-cache
   writeback theory DEAD** (no dirty-line divergence); CCR 00000105 =
   ICE|CB|OCE normal; TA_ALLOC_CTRL 00121213.

   **Round 12 reinterpretation — the real anatomy (emulator capture
   `baseline12.log`, fork instrumentation C2D/TAEND/TAREG/PVRW):**
   - The 4-list "healthy choreography" frames (opaque 3460 / modifier 40
     `w0=81000000` / translucent 720 / transmod 40) are the **load screen
     itself** rendering (~550 frames), not gameplay. Last STARTRENDER at the
     bar-full moment; then a long render-silent load stretch (bar frozen at
     100% — exactly what the tester sees); then the **load→title transition**:
     SOFTRESET pulse ×2 + TA_ALLOC_CTRL=00121213 (PT list now enabled —
     also appears in healthy runs, NOT a divergence) + LIST_INIT ×2, then two
     zero-content closer DMAs: `0c0cf240` len 0x20 and **`0cb80000` len 0x40 —
     the exact pinned slot src** (P1 form 8CB80000).
   - The pinned transfer is the **title arena's modifier-list closer**, shape
     [global][EOL] = 0x40. Queued typed as modifier (expected 8002) but its
     **content was never built** — a zero first word decodes as EOL ListType=0
     → TA raises OPAQUE end (arrived 8001) → pin. Slot len bookkeeping says
     0x40, so the emitter's write-pointer advanced while the stores are
     missing ⇒ init-vs-submit divergence, not a partial write.
   - **Flycast reproduces the hang deterministically in interpreter mode**
     (`baseline12.log`, re-confirmed `v4-unfixed.log`: transition C2D
     `src=0cb80000 len=40 w0=00000000`, then zero TAEND/STARTRENDER forever,
     maple polls only) — never noticed before because the emulator health
     metric ("off=418 streams ≥ 130") sits upstream of the transition.
     Mid-investigation "nondeterminism" was two launch artifacts, both now in
     `tooling.md`: `-config` flags AFTER the disc path are silently dropped
     (runs were dynarec, which sails through), and the
     `build/[GDI] .../disc.gdi` release-set copy was a stale Aug-11 build
     (plain `make` refreshes only `build/disc.gdi` + tracks) — every
     "passing identical re-run" was actually the old disc.
   - Title frame anatomy in a passing run: opaque global via PIO + zero-EOL
     closer `0c0cf240` 0x20; modifier `0cb80000` [818c0002][EOL] 0x40;
     translucent `0cc80000` 0xa40; transmod `0ce80000` 0x40; PT `0cf80000`
     [848c0002][EOL] 0x40 — single-buffered (same srcs every frame), content
     written by **store-queue bursts immediately before each frame's
     submission** (SQWR lines ~20 log entries before the C2D; CLOSERWR=0 —
     never plain stores).

   **Round 13 — ROOT CAUSE FOUND AND FIXED (2026-08-16, emulator-only).**
   Fork instrumentation (fork commits `2a22e6681`, `8bccf2b49`): `CLOSERWR`
   (CPU stores into `0c0cf240`/`0cb80000`), `CTRLWR` (control watch on
   load-screen closer `0cb54540`), `SQWR` (same ranges in
   `WriteMemBlock_nommu_sq` — SQ flushes bypass addrspace::writet), slot-write
   ring dump at the transition C2D, `MMUCRWR` (MMUCR write timeline), and
   `FLYCAST_GDSLOW=<N>` (GD rate divider; N=12 ≈ 150 KB/s ≈ serial dongle).

   **Evidence chain:** SQWR lines print area-3 destinations, which in
   Flycast's SQ model is only reachable via `sqWrite<true>` — **the
   MMU-translated path** (flycast `core/hw/sh4/storeq.cpp`: nommu area-3
   goes through fast paths that bypass `WriteMemBlock_nommu_sq`) ⇒ **the
   game runs MMU-on and maps RAM through the SQ window via UTLB entries**.
   Game-side confirmation in boot.bin: SQ-mapper `0x8c0311a4` reads MMUCR,
   tests AT, and **skips the whole mapping when AT=0**; loops TLB page
   loader `0x8c03b1c8` over 0xE0000000-based 1 MB pages onto RAM (pools at
   file 0x11298/0x1b2bc: 0xff000010, page masks). MMUCR write timeline
   (MMUCRWR): BIOS clears ×2 → our loader handoff clear (`pc=8c0103d6`) →
   **the game's enable `val=00040005` (AT|TI|URB) at `pc=8c03b1c0`**, right
   at its TLB loader.

   **Root cause — our own shim forced AT=0, two writers:**
   1. `shims/src/gdstack.S` — permanent post-syscall `MMUCR=0` ("games
      assume AT=0 forever" — wrong for this game).
   2. `shims/src/main.c` probe block — the round-8 "medicine": a per-tick
      `MMUCR=0` from ISR context (diag builds; `MMUCRWR pc=8cfc05ba`).
   With AT forced 0 past the game's enable, the SQ bursts fall back to QACR
   mapping (area 0 — writes vanish, no fault): the closer buffer stays
   zero, the TA raises opaque-end instead of modifier-end (a zero EOL PCW →
   ListType 0), the slot pins on expected 8002 / arrived 8001. Round-12's
   P1==P2==0 autopsy = the bytes never reached RAM from either view. And
   the round-8 signature was the SAME root cause's other mask: an AT=0
   window while the SQ-mapper ran → no TLB entries → later SQ PREF with
   AT=1 fault-restarts forever (EXPEVT=0x040 eternal TLB-miss pin).
   GDEMU worked because the verified build was MAIN branch (no sanitizer,
   no probes).

   **Fix (repo commits `11b4c79` + probe-clear removal):** (a) gdc_call now
   SAVES the game's MMUCR on entry, holds it 0 for the duration of the
   isoldr call (the rounds-2-5 requirement — now also covering the FIRST
   call, which the old clear-after never protected), and RESTORES it on
   exit; the 0-store keeps TI=0 so the game's UTLB entries survive (SH7091:
   MMUCR.TI=1 is the only TLB-invalidate trigger; flycast ccn.cpp
   `CCN_MMUCR_write` models the same). (b) The per-tick clear is deleted —
   MMUCR is owned by the game. `make test` green.

   **Ablation (interpreter, fresh discs, `build/disc.gdi`):**
   | build | transition buffer | result |
   |---|---|---|
   | unfixed round-12 (`v4-unfixed.log`) | `w0=00000000` | PIN (STARTRENDER frozen at 541) |
   | gdstack fix only (`v3-fixed.log`) | `w0=00000000` | PIN — per-tick clear alone is fatal |
   | both fixes (`v5-bothfixes.log`) | `w0=818c0002` | **TITLE RUNS** — STARTRENDER 541→4252+, all 5 lists close per frame |

   V5's MMUCR timeline confirms the fix live: gdc_call clear/restore pairs
   (`pc=8cfc1714`/`8cfc1724`), restores carrying the game's `val=00040001`
   (AT=1 preserved; the 0x40005 enable at `pc=8c03b1c8` reads back with TI
   self-cleared), zero per-tick writers. **Round 13 HW test: rebuild with
   the same diag command, same DreamShell settings — expected outcome is
   the game booting into the title. If it still pins, photograph the same
   five cyan probe rows.**

   **Round 13 HW result (2026-08-16): NEW earlier pin — bar 0%, and it
   decoded the entire rounds-1..13 saga.** Photo: y68 `SPC=8c081224 |
   EXPEVT=00000040`, y82 `TEA=10667424 | VBR=8c00f400`, y96 `0 | BAD00000`
   (sound driver not up — early boot), y162 `ISTNRM=10 | slot=00000000`,
   y176-left `C0000000` (ring counts 0/0/0), y218-right
   `TA_ALLOC_CTRL=202` (load-phase alloc); rows y190/y204/CCR correctly
   slot-gated off; no white-on-blue (GD-diag rows overdrawn/pre-stream).
   The round-8 eternal TLB-miss pin is back at the same insn — but with the
   MMU model complete, TEA finally identified the disease:

   **Round 14 — the bar-0% pin was NEVER an EE-library spin; it is a wild
   TABLE INDEX read (fixed, patch #36).** Chain, each link
   disassembly-verified: parser `FUN_8c0811f2` copies a static 32-byte
   table (`0x8c0c0c84`) to its frame and reads `SP + arg2*4` at
   `0x8c081224` (`mov.l @r6,r1`) — arg2 is its third argument.
   Orchestrator `FUN_8c081aee` passes arg2 = `[0x8c1c9770]` when gate
   `[0x8c1c9768]` != 0; the gate is set by `FUN_8c081438` = a **Naomi-BIOS
   fingerprint check** (112 bytes at `0xa01ffd00` vs index-obfuscated
   table `0x8c0d7ed9`) — which patch #15 makes PASS in every environment
   (required: task-13 proved gate=0 dead-ends boot). Wrapper
   `FUN_8c081bf0` fills `[0x8c1c9770]` via READ thunk `FUN_8c080484`
   (fn-table `[[0x8c1c9764]]` slot +40 — the un-stubbed sixth sibling of
   the five round-2/3 write-path stubs) immediately before the parse. On
   the tester's DC that lib read returns without writing; the word keeps
   **DreamShell boot residue** (round 13: `0x10667424`-yielding junk;
   round 8: `0x58c1fc94` — different DS sessions, different residue), and
   the parse reads a wild unmapped address. Two masks, now unified:
   AT=1 (game's design) → eternal TLB-miss restart, no handler at
   VBR+0x400 (rounds 1-8, 13, bar 0%); forced AT=0 (rounds 9-12
   "medicine") → wild read returns junk harmlessly but the transition's
   SQ world dies later (bar 100%). Flycast and GDEMU were green the whole
   time because THEIR residue at `0x8c1c9770` is zero (flycast zeroes RAM
   at boot; the DC BIOS boot leaves the word benign) — i.e. the
   proven-green worlds already run this path with **index 0**.

   **Second discovery burying the "MMU enabled only at transition" model:
   on real hardware the game runs MMU-ON from EARLY BOOT.** Call graph
   (Ghidra): boot scene loop `FUN_8c04ae50` → `FUN_8c04ab70` →
   `FUN_8c0258ec` → `FUN_8c030ff0` (MMUCR clear `FUN_8c03b1c0` → TLB
   loader/enable `FUN_8c03b1c8` → SQ-mapper `FUN_8c0311a0`, which
   re-invokes the loader 3×) — runs before the settings flow. The HW
   photo proves it live: EXPEVT=0x040 requires AT=1, and the
   orchestrator's own P0 write to `0x0c01f100` (insn `8c081b3a`,
   *before* the parse call) retired — the game's wired UTLB entries were
   in place. Flycast's HLE boot never enters this scene path (its enable
   appears only at the transition, `v5-bothfixes.log` line 95595) — the
   emulator is structurally blind to the early real-HW MMU phase; only
   TEA on a TV could catch this one.

   **Fix (deployed):** `shim_ee_idx_read` (`shims/src/main.c`) — stub
   `FUN_8c080484` in the exact idiom of its five stubbed siblings:
   `*(u32*)r4 = 0; return 0` (deterministic index 0 = the flycast/GDEMU
   proven-green value; r0 ignored at the `8c081c04` call site). Patch #36
   `hook(0x8C080484, 0x7FFC, shim_ee_idx_read)` — 12-byte entry thunk
   fits the 14-byte body, next fn `0x8c080492` untouched. Round-13's two
   MMUCR fixes stay (gdstack save/clear/restore around syscalls; no
   per-tick writers): they are what makes the early AT=1 world—and the
   transition—survive. `make test` green; HUD slot 14 cyan = the read
   thunk was reached (breadcrumb for the next photo). **If round 14 still
   pins with EXPEVT=0x040 at a NEW SPC/TEA: same residue class, next
   un-stubbed lib output — the prepared escalation is a shim-resident
   identity-mapping TLB-miss handler at VBR+0x400 (1 MB pages, C=0, D=1,
   SH=1; the game's `MMUCR.URB=1` confines ldtlb replacement to entries
   0-1, so wired entries are safe by construction).**

   **Round 14 HW result (2026-08-16): THE 14-ROUND BOOT PIN IS DEAD — the
   bar fills, the transition happens, the game RUNS. Remaining defect:
   post-transition display is black.** Photo decode: SPC cycling in
   `8c02xxxx` (caught `8c023b12` = the documented healthy scene-pump
   range), EXPEVT=`020` stale reset (ZERO exceptions, TEA=0), sound ctx
   `8c0f3b80` with the ARM heartbeat counting (first time ever on
   serial), GD req# climbing with CHECK=2 COMPLETED at FAD `0x7916a`,
   cart-stream counter in the thousands with the A→B→E phase marker
   cycling (checksums varying = real data), TA_ALLOC_CTRL=`00121213` =
   title-phase, transfer rings drained, no slot pinned. Tester observed:
   monitor lost sync ~1 s (the transition's SPG mode switch — CLEO-SPG
   `FB_R_CTRL`/`VO_CONTROL` writes in the flycast log), then black with
   the probe text **flickering at flip rate** (= frames ARE being
   presented; their content is black) and possibly interlace-soft.

   **Emulator cross-section (r14 shot run + r14b no-stub control with a
   `SETWR` fork watch on `0x0c1c9764-88`/`0x0c1c94c0-d0`):**
   - The same stub build shows the REAL TITLE (PRESS START / FREEPLAY)
     ~10 s after the transition (`r14-post10.png`) — then the flycast
     DISPLAY goes black too (`r14-post30/60/90.png` = pixel-identical to
     the tester's photo) **while the TA logs keep carrying rich geometry**
     (translucent lens a40/3240/3060 to end-of-run). The flycast black is
     the KNOWN probes-kill-present artifact (per-tick paints put flycast
     into raw-FB present; round-12 bisect) — flycast's display channel is
     unusable as evidence in probe builds; its TA/C2D log is the truth
     channel, and it says the game draws the attract fine.
   - `SETWR` findings that correct round 14's residue story: flycast's
     HLE BIOS leaves memtest garbage (NOT zeros), and **the game itself
     memclears the settings region at boot (`pc=8c021192`)** — residue is
     irrelevant in every world. The EE lib context is installed at
     `0x8c018000` (`[0x8c1c9764]=ac018000`, set at `pc=8c0803b2`), and
     slot+40 **does write the index: 1 byte `0x28`** (`pc=0c0183ae` — the
     lib runs from its RAM home via P0). So the tester's wild index was
     WRITTEN by the lib — its EEPROM bit-bang against a real DC's G1 regs
     produces session-dependent noise (`0x2119xxxx` r13, `0x3330xxxx` r8)
     — not leftover memory. The stub's deterministic 0 and the lib's 0x28
     both title-green in flycast (0x28 itself indexes past the 32-B table
     into mapped stack — the "green" worlds were never reading the table
     either). Stub stays at 0; revisit only if a settings-semantics
     divergence ever surfaces.

   **Round 15 instrument (deployed): video-output register autopsy.**
   The round-12 transfer-queue probe rows (proven healthy) are replaced
   with PVR output state — one photo decides where the black comes from:
   y162 `FB_R_SOF1 | FB_W_SOF1`, y176 `FB_R_SOF2 | FB_W_SOF2`, y190
   `FB_R_CTRL | FB_W_CTRL`, y204 `VO_CONTROL | SPG_CONTROL`, y218
   `SPG_STATUS | ISP_BACKGND_T`. Reading guide: R_SOF pair = what scanout
   shows, W_SOF = where renders land — if the pairs never intersect, the
   game presents buffers it never renders (video-init divergence:
   isoldr-left BIOS work-RAM video/cable flags vs BIOS-fresh values is
   the prime suspect class); `VO_CONTROL` bit3 = blank (probes visible ⇒
   not blanked), `SPG_CONTROL` bit4 = interlace (the "unfocused/flicker"
   report), `FB_R_CTRL` bit0 = fb enable, `ISP_BACKGND_T` = background
   plane tag (garbage here = black tiles despite geometry).

   **Round 15 HW result (2026-08-16): the video registers confessed —
   scanout and render never meet.** Photo (game otherwise healthy: SPC
   `8c0239da` in the pump range, EXPEVT stale, sound heartbeat ticking,
   streams flowing at FAD `0x7916a`): `FB_R_SOF1/2 = 000b2000/000b2500`
   (one buffer, interlaced fields, STATIC) vs `FB_W_SOF1 = 004b2000`,
   `FB_W_SOF2 = 00600000` (loader leftover); `FB_R_CTRL=1`,
   `FB_W_CTRL=b`, `VO_CONTROL=00160000` (not blanked),
   `SPG_CONTROL=00000150` (interlace NTSC — the flicker/softness),
   `SPG_STATUS=3c17`, `ISP_BACKGND_T=01160590`. No sound audible despite
   the ARM heartbeat — consistent with the attract never presenting its
   first frame.

   **Flycast dynamic cross-section (fork watches `SOFWR` + `IMLWR`,
   commit bd29ce0b7):**
   - Healthy flip: the game's per-frame STARTRENDER fn `0x8c041ce0`
     (disasm-verified: programs REGION/PARAM base, ISP_BACKGND, FB_W_CTRL,
     FB_W_SOF1 [gated on `[0x8c0e84e4]==0`], then `STARTRENDER=1`; guard
     `jsr 0x8c048300`/arg test early-outs `0xEEEE` when the previous
     render is unacknowledged) alternates `FB_W_SOF1`
     `0xb2000↔0x4b2000` every frame (`pc=8c03df06 pr=8c041d7a`). The HW
     photo = that alternation parked after one render into `0x4b2000`
     with scanout pinned on never-rendered `0xb2000` — and the probe
     text surviving in the scan buffer proves no rendered pixels ever
     land there. The early-out guard + parked flip = the render-complete
     acknowledgment never arrives on HW.
   - Green-world Holly masks (title era): `IML2NRM=00001008` (vblank-in
     + maple-DMA-end), `IML4NRM=0007b000` (DMA-end bits, after the
     shim's documented bit-14 clear at `pc=8cfc14cc`),
     `IML6NRM=00280fec` — **render-done + all five list-end bits live on
     level 6**, built bit-by-bit by the game's own Holly registrar
     (`pr=8c0404xx` = the FUN_8c0400e0 family) via helpers
     `8c02aad4/8c02aac2`.
   - Flycast's HLE BIOS *zeroes and restores* the IML masks around
     syscalls (`pc=8c00c9xx` reios). Prime suspect for the HW black:
     **isoldr's syscall emulation does the same but restores KOS-world
     masks (or zeros) instead of the game's** — every streaming read
     would wipe `IML6`, the render-done IRQ never reaches the game's
     handler, the busy flag never clears, every later frame early-outs
     `0xEEEE`, flip parked, screen black. Same disease shape as the
     round-13 MMUCR clobber. If confirmed, the fix is the same shape
     too: save/restore the three IML masks in `gdc_call` around each
     isoldr syscall (or re-arm per tick).

   **Round 16 instrument (deployed): Holly-mask probe rows** appended
   below the video rows: y232 `IML2NRM | IML4NRM`, y246 `IML6NRM |
   ISTNRM`. Expected photo readings: masks matching the green values
   above ⇒ theory dead, look at ISTNRM latches instead (render-done bit
   2 stuck set = handler starved at the SH4 level; never set = render
   never completes); `IML6NRM=0` or a KOS-looking value ⇒ isoldr mask
   clobber confirmed ⇒ ship the gdc_call mask save/restore.

   **Round 16 HW result (2026-08-16): mask theory DEAD, and the verdict
   sharpened.** Tester (VGA cable throughout the serial saga):
   `IML2NRM=00000000, IML4NRM=0007B000, IML6NRM=00280FEC,
   ISTNRM=00000010` (ISTNRM active during load, settles at 0x10 when the
   bar completes). IML4/IML6 match the green world EXACTLY — render-done
   + list-end enables armed on HW. IML2=0 vs green 0x1008 is
   BIOS-internal (flycast reios' own vblank/maple level-2 hooks; absent
   under isoldr by design — isoldr is driven synchronously via
   gdc_call). The decisive datum: **render-done (bit 2) and every
   list-end bit NEVER LATCH in ISTNRM** — the interrupt plumbing is fine
   and idle; the PVR CORE is never completing (almost certainly never
   receiving) a render after the first one. Combined with round 15
   (`FB_W_SOF1` parked at `0x4b2000` after one write, scanout static):
   the frame director issues at most one render and its completion path
   never advances.

   **Round 17 instrument (deployed): frame-director state probe.** The
   sole caller of the STARTRENDER fn is FUN_8c036220 (the frame
   director): on issue it sets `[0x8c0eb72c]`=active render ctx,
   `[0x8c0eb728]`=1 (render pending), `ctx+0x14`=5 ("rendering"), and a
   +10-frame deadline `[actx+0x24]` (pools file 0x16550/0x16554; guard
   FUN_8c048300 is a ctx-validity walk over the registry `0x8c0faaa8`
   stride 0x8c, NOT a busy-wait). Mask rows y232/y246 replaced:
   y232 `active ctx | render-pending`, y246 `ctx->state(+0x14) |
   ISTNRM-OR-accumulator since boot` (bit2/7-10/21 catches transient
   latches; ffffffff state = no valid ctx). Reading guide:
   pending=1 + state=5 frozen ⇒ a render WAS issued and never completed
   (PVR CORE level — suspect render inputs/region array under
   DreamShell); pending=0 + ctx=0 ⇒ the director was never invoked for a
   second frame (stall upstream, in the scene loop's frame pacing);
   ist_seen bit2 set ⇒ completion FIRED but the state machine missed it
   (software race — look at the callback registration).
   **Green-world reference (flycast r17 black-phase capture,
   `r17-post32.png`):** active ctx=`8C0EA578` (static bss — should be
   identical on HW), pending=`0`, state=`8` (resting; 5 only transient),
   ist_seen=`00009038` (bits 3/4/5/12/15 — note: even green never
   catches bit 2/list-ends in the accumulator; acked sub-tick, so a
   missing bit 2 on HW is NOT evidence against completion). Bonus:
   `SPG_CONTROL=0x150` in the green world too — interlaced NTSC is the
   game's own chosen output mode; the tester's soft/flickery VGA picture
   is cosmetic (possible later polish: force a progressive mode), not
   part of the black-screen defect.

   **Phase-5 closing items:** graphics/stage-load spot-checks
   during normal play (user reports none so far; sound-RAM fit CLOSED —
   see below). **Pre-publication
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
   name it if it ever bites. **VRAM fit CLOSED (2026-08-01):** write-truth remeasure in Flycast (same
   method as sound RAM below — zero VRAM at game handoff, then profile genuine
   post-handoff writes + snapshot the TA/FB layout registers;
   `cartlog_vram_profile`, the flycast fork) shows the game's
   VRAM writes peak at `0x7cd7d5` (**7.8 MB, 0 bytes at/above 8 MB** in every
   snapshot; `capture-vram-fit-attract.log`, 433 cart DMAs / 7 snapshots;
   confirmed on a hands-on gameplay round `capture-vram-fit-gameplay.log`,
   490 cart DMAs / 8 snapshots — `nz_above8m` stayed 0 through live play), and
   the game's own TA/FB layout double-buffers entirely below `0x800000`. The
   old ~9.2 MB was the Naomi BIOS boot screen in the BIOS framebuffer at
   `0x800000` — stale content the never-cleared scan counted. **Fits DC's
   8 MB, no texture cuts**; corroborated by 18 clean HW rounds (no
   wrong/missing textures). See `docs/kb/phase2-measurements.md` §Video RAM.
   No fit watch items remain.
   **Sound-RAM fit CLOSED (2026-08-01):** write-truth remeasure in Flycast
   (zero ARAM at game handoff, then high-water + 256 KB histogram of genuine
   post-handoff writes — `cartlog_aram_profile`, the flycast fork)
   shows the game writes only ARAM `0x0-0x1fffff` = **exactly 2 MB, 0 bytes above**,
   loaded once at boot as a fixed bank; confirmed on a hands-on gameplay round
   (`capture-aram-fit-gameplay.log`, 603 cart DMAs / 10 snapshots — high-water
   never left `0x200000` through drops/clears/combos/stage changes). Fits DC's
   2 MB, no sample cuts — the old
   "8 MB / inconclusive" was the backwards content scan fooled by a stale byte.
   See `docs/kb/phase2-measurements.md`.
   (GD-ROM PIO→DMA latency = I1 CLOSED by the SHIM_GD_DMA upgrade above).

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

Fit checks: **all CLOSED (2026-08-01).** Sound RAM fits DC's 2 MB exactly;
VRAM fits DC's 8 MB (both by write-truth remeasure — the old 9.2 MB VRAM
figure was stale BIOS framebuffer content; see `phase2-measurements.md`).

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
  relocation**. VRAM **fits DC's 8 MB** — write-truth remeasure (2026-08-01):
  game writes peak at 7.8 MB with 0 bytes at/above 8 MB; the old ~9.2 MB scan
  figure was stale BIOS-framebuffer content at `0x800000`.
  Sound RAM **fits DC's 2 MB** — write-truth remeasure (2026-08-01) shows the
  game uses exactly 2 MB, 0 bytes above; the earlier "inconclusive (scan
  artifact)" is resolved.
- **Input map:** all 7 gameplay controls confirmed to single JVS bits
  (Start 0x8000, Up/Down/Left/Right 0x2000/1000/0800/0400, B1/B2 0x0200/0100).
- **Serial/watchdog:** 0 pokes → no serial or watchdog shim needed.
