# Boot-Load Indicator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the black screen between the loader's NAOMI splash and the game's company logos with the still-visible splash plus three cycling "loading" dots, stopped at the exact moment the game takes over video.

**Architecture:** Pure shim code (spec: `docs/superpowers/specs/2026-07-26-boot-load-indicator-design.md`). A new `shim_boot_anim()` in `shims/src/util.c` unblanks video (VO_CONTROL bit 3 — the game's "black screen" is only this blank bit; the splash never leaves the framebuffer) and paints three RGB565 dots into the live scanout FB every 16th tick. It is ticked per frame by `shim_maple_steady()` (the engine pump, proven alive through the whole window) and gated off by `shim_vid_init()` (the patch #34 hook that fires at video takeover).

**Tech Stack:** Freestanding SH-4 C (shim, `-Os`, no libc/libgcc), GNU make, Flycast (instrumented, `tools/flycast-src`), real Dreamcast + GDEMU for final verdict.

## Global Constraints

- **Patch count stays 34** — no new patch-table entries, no loader changes.
- Shim statics MUST have non-zero initializers (`.data`) — the loader copies `.data` but does NOT zero `.bss` (house style, see `pending_jvs` in `shims/src/main.c`).
- No integer division/modulo in shim code paths (freestanding, no libgcc — use counters, not `%`).
- Nothing may paint after video takeover: every paint is behind the `anim_live` gate cleared in `shim_vid_init`.
- Never commit/upload ROM, BIOS, or disc images (`build/` is gitignored).
- Build requires the sh-elf toolchain at `/opt/toolchains/dc`, KOS at `tools/kos`, ROM at repo root.
- No host-testable pure logic is added (paint loop is MMIO-bound, dot cycling is a 2-line counter); per repo pattern (only `cart_split` has host tests) the automated gate is `make` + `make test`, behavior verified in Flycast + on real HW.

---

### Task 1: `shim_boot_anim` + takeover gate + per-tick call

**Files:**
- Modify: `shims/src/util.c` (insert after `shim_cable_is_vga`, before the `shim_vid_init` comment block, ~line 97; plus one line inside `shim_vid_init`)
- Modify: `shims/src/main.c` (declaration near line 8, call inside `shim_maple_steady` after the free-play stamp ~line 345)

**Interfaces:**
- Consumes: `shim_maple_steady()` per-frame tick (existing), `shim_vid_init(mode,b,c,d)` takeover hook (existing, patch #34).
- Produces: `void shim_boot_anim(void)` — self-gating; safe to call every tick forever.

- [ ] **Step 1: Add the animation function to `shims/src/util.c`**

Insert after the closing brace of `shim_cable_is_vga` (line 96), before the "Composite/RGB sync fix" comment block:

```c
/* Boot-load indicator (spec: docs/superpowers/specs/2026-07-26-boot-load-
 * indicator-design.md). The game blanks video for the whole init + boot
 * preload window; on real HW the GD-ROM PIO preload makes that a long black
 * screen that reads as a hang. The loader's NAOMI splash is still in the
 * framebuffer (the game never clears it -- HUD-era evidence: the loader text
 * stayed readable through this exact window), so: unblank and cycle three
 * dots on the splash. Ticked per frame from shim_maple_steady (engine pump,
 * alive through the whole window); shim_vid_init below clears the gate at
 * the exact video takeover, so a paint can never land on a game frame. */
static unsigned int anim_live = 1;      /* .data non-zero init per house style */
void shim_boot_anim(void) {
    static unsigned int tick = 4;       /* .data; ~60 Hz caller -> step every 16 */
    static unsigned int dot  = 3;       /* .data; cycles 0..2 (first step wraps) */
    if (!anim_live || (++tick & 15u) != 0)      /* ~3.75 Hz animation step */
        return;
    if (++dot >= 3u) dot = 0;                   /* no % : freestanding, no libgcc */
    *(volatile unsigned int *)0xa05f80e8 &= ~8u;            /* unblank video */
    unsigned int base = *(volatile unsigned int *)0xa05f8050 & 0x00fffffcu;
    volatile unsigned short *fb =
        (volatile unsigned short *)(0xa5000000u + base);
    for (unsigned int d = 0; d < 3; d++) {
        /* lit dot NAOMI-orange, idle dots light gray; 10x10 px each, centered
         * near the bottom (x 295..344, y 430..439 -- inside TV overscan) */
        unsigned short px = (d == dot) ? 0xfc60 : 0xc618;
        volatile unsigned short *p = fb + 430u * 640u + 295u + d * 20u;
        for (unsigned int y = 0; y < 10; y++)
            for (unsigned int x = 0; x < 10; x++)
                p[y * 640u + x] = px;
    }
}
```

- [ ] **Step 2: Clear the gate at video takeover in `shim_vid_init` (`shims/src/util.c`)**

Add one line at the top of the existing `shim_vid_init` body:

```c
int shim_vid_init(unsigned int mode, unsigned int b, unsigned int c, unsigned int d) {
    anim_live = 0;                 /* video takeover: boot-load dots off for good */
    if (!shim_cable_is_vga())
        mode &= ~3u;               /* class 1 (VGA 31k) -> class 0 (NTSC 480i) */
    return ((int (*)(unsigned int, unsigned int, unsigned int, unsigned int))
            0x8c034020)(mode, b, c, d);
}
```

- [ ] **Step 3: Declare and call it in `shims/src/main.c`**

Add to the extern block at the top (after line 8, `int  shim_cable_is_vga(void);`):

```c
void shim_boot_anim(void);                        /* util.c: boot-window loading dots */
```

Add the call in `shim_maple_steady`, immediately after the free-play stamp
(`*(volatile u32 *)0x8c1c9790 = 1;`, line ~345) and before the engine call:

```c
    shim_boot_anim();                              /* boot-window loading dots */
```

- [ ] **Step 4: Build the disc**

Run (repo root): `make`
Expected: shim builds, loader builds (patch table regenerates from shim.map), `make_gdi.py` masters `build/disc.gdi` — no errors. If the link fails with an undefined `__udivsi3`-style symbol, a division snuck in (Global Constraints) — fix, don't add libgcc.

- [ ] **Step 5: Run the test suite**

Run: `make test`
Expected: shims host tests pass (`test_host`, `test_shim_iface`), `test_maple_literals.py` all `OK` lines (the new code touches only video regs `0xa05f80e8`/`0xa05f8050`, which the maple scan — `0x5f6cxx` block only — cannot match; the shim is excluded from that scan anyway).

- [ ] **Step 6: Commit**

```bash
git add shims/src/util.c shims/src/main.c
git commit -m "Boot-load indicator: cycling dots over the persistent NAOMI splash (no new patches)"
```

---

### Task 2: Flycast regression check (boot → attract still presents)

The dots themselves CANNOT appear in Flycast screenshots — framebuffer writes never do (same as the splash and HUD; the TV shows them). This task only proves no regression: the disc still boots and attract still presents. The round-13→16 precedent shows per-tick VRAM paints + the VO_CONTROL RMW are Flycast-present-safe (the HUD did exactly this); only CPU control-register probing ever killed the present (round-12 bisect).

**Files:**
- None modified. Screenshot goes to the session scratchpad, not the repo.

**Interfaces:**
- Consumes: `build/disc.gdi` from Task 1's `make`.

- [ ] **Step 1: Launch the disc in Flycast (background, vsync off, stale-instance + relaunch-modal guards — the `test_vmu_untouched.sh` launch pattern)**

```bash
cd /Users/captainkoffski/AntigravityProjects/cleopatra
pkill -9 -f "flycast-src.*Flycast" 2>/dev/null || true
defaults write com.flyinghead.Flycast ApplePersistenceIgnoreState -bool YES
defaults write com.flyinghead.Flycast NSQuitAlwaysKeepsWindows -bool false
tools/flycast-src/build/Flycast.app/Contents/MacOS/Flycast \
  -config config:rend.vsync=no "$PWD/build/disc.gdi" &
```

- [ ] **Step 2: Wait ~75 s (boot + into attract), screenshot, quit**

```bash
sleep 75
screencapture -x "$TMPDIR/boot-anim-attract.png"   # any session temp dir works; do not commit it
osascript -e 'quit app "Flycast"'
```

- [ ] **Step 3: Verify the screenshot**

Read `boot-anim-attract.png`.
Expected: game content presented (title / attract / demo footage / high-score loop — anything the game renders). FAIL = black Flycast window (a video-present regression: re-check that no paint or register access escaped the `anim_live` gate or landed outside the 16th-tick branch).

---

### Task 3: Status doc + deploy for the real-HW verdict

**Files:**
- Modify: `docs/kb/00-status.md` (Phase-5 narrative, after the composite/AV paragraph)

**Interfaces:**
- Consumes: verified build from Tasks 1–2.

- [ ] **Step 1: Add a status paragraph**

Insert after the "Composite/AV sync fix" paragraph in `docs/kb/00-status.md`:

```markdown
   **Boot-load indicator (2026-07-26):** the post-splash black screen (game
   blanks video through init + boot preload; long on real HW via GD-ROM PIO)
   now shows the loader's NAOMI splash (still in the FB -- the "black" was
   only the VO_CONTROL blank bit) with three cycling dots: `shim_boot_anim`
   (util.c), ticked per frame from shim_maple_steady, gated off in
   shim_vid_init at the exact video takeover. Zero new patches (still 34).
   Spec: `docs/superpowers/specs/2026-07-26-boot-load-indicator-design.md`.
   Flycast: attract regression-checked (dots invisible there -- FB writes,
   same as splash/HUD; TV shows them). HW verdict: PENDING.
```

- [ ] **Step 2: Commit**

```bash
git add docs/kb/00-status.md
git commit -m "Status: boot-load indicator implemented, HW verdict pending"
```

- [ ] **Step 3: Deploy to the GDEMU card (user-gated)**

If `/Volumes/GDEMU/09` is mounted: `make deploy` (expected: files copied, `dot_clean` run, "deployed to /Volumes/GDEMU/09", no AppleDouble junk). Otherwise ask the user to insert the card and run `make deploy`.

- [ ] **Step 4: Hand off for the HW look (house rule 1 — verify on the real target)**

Ask the user to boot the real Dreamcast and confirm: after swirl → SEGA license, the NAOMI splash appears and STAYS on screen with three dots cycling (~4/s) near the bottom until the company logos appear; the logos and game are otherwise unchanged. On PASS/FAIL, update the `HW verdict: PENDING` line in `docs/kb/00-status.md` and commit (`Record HW verdict: boot-load indicator PASS/FAIL`).
