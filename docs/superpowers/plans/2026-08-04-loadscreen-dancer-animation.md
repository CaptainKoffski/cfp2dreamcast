# Load-Screen Dancer Animation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show the game's own dancing-Cleopatra background animation, extracted from the cart's real sprite data, above the existing boot progress bar during the ~5 s load screen.

**Architecture:** Build-time pipeline (Flycast texture dump → locate bytes in the cart → `extract_dancer.py` decodes/bakes an 8bpp-indexed blob in the scanout pixel format) + shim playback (`loadanim_paint` blits frames per steady tick into the live scanout FB, gated by the loadbar's existing boot-preload countdown). Spec: `docs/superpowers/specs/2026-08-03-loadscreen-dancer-animation-design.md`.

**Tech Stack:** C (freestanding SH-4 shim, KOS loader), Python 3 stdlib (extraction), instrumented Flycast fork (`tools/flycast-src`, separate git repo).

## Global Constraints

- **Never commit ROM/BIOS-derived bytes** (house rule 5). `scripts/anim_manifest.json` holds offsets/hashes/format words only. `build/anim.bin` and dump dirs are gitignored build products of the gitignored `Cleopatra Fortune Plus.dat`.
- **Shim is `-nostdlib`:** no libgcc calls. No `/` or `%` by a runtime variable (only shifts and `/2`-style constant powers of two). 32×32 multiply is fine (native `mul.l`).
- **Scanout pixel format is RGB0555** (R bits 14:10, G 9:5, B 4:0, bit 15 ignored) — pinned from `capture-m2-13b`-era CLEO-SPG logs: game takeover writes `FB_R_CTRL = 0x00800001`, fb_depth bits 3:2 = 0. Task 3 re-confirms for the composite (class-0) path.
- **Blob budget:** `ANIM_DATA = SHIM_BASE + 0x13800 = 0x8cfd3800`, `ANIM_MAX = 0x1c000` (114688 B). Hard ceiling: region must end ≤ `0x8cff0000` (KOS loader stack bottom — the loader memcpys the blob while running on that stack; already the region-map ceiling in `test_shim_iface.c:32`). The spec's "~180 KB free" figure ignored the loader stack; 114 KB is the real number.
- **Framebuffer paint idiom** (loadbar precedent, `shims/src/util.c:99`): live base from `FB_R_SOF1` (`0xa05f8050 & 0x00fffffc`), write via `0xa5000000 + base`, repaint every call (flip-buffer convergence), end with VO_CONTROL unblank RMW (`0xa05f80e8 &= ~8`). Per-tick FB_R_SOF1 reads + unblank RMW are Flycast-present-safe (rounds 14–18 precedent: `hex_paint` does both per poll); CPU control-register probing is NOT (round 12) — add none.
- **Patch count stays 35.** Pure shim/loader/build changes; no new patch-table entries.
- **House style:** shim statics that must start nonzero use `.data` init (loader does not skip zeroing: it zeroes the `.bss` gap `SHIM_BASE+shim_len..SHIM_CODE_MAX`, so zero-init statics are also safe).
- **Verification ladder** (house rule 1): `make test` green → Flycast attract screenshot regression → CLEO-VRAMDUMP byte-check → real-HW photo rounds on BOTH cables. Flycast cannot render FB writes (the anim is invisible in its screenshots — same as splash/bar); only VRAMDUMP or the TV proves pixels.
- **Record every tool install / fork change** in `docs/kb/tooling.md` (house rule 4). Fork commits go to `tools/flycast-src` (repo `CaptainKoffski/flycast4naomi2dreamcast`, branch master).

## File Structure

- `tools/flycast-src/core/rend/TexCache.cpp` — fork: CLEO-TEX raw-dump hook (Task 1)
- `scripts/anim_manifest.json` — committed manifest: frame hashes, cart offsets, tcw words, cadence, fb format (Tasks 2–3)
- `scripts/extract_dancer.py` — decode + bake tool, `--selftest` (Task 4)
- `shims/include/shim_iface.h` — `ANIM_DATA`/`ANIM_MAX`/`SHIM_LOADANIM` (Task 5)
- `shims/test/test_shim_iface.c` — region asserts (Task 5)
- `loader/Makefile`, `loader/main.c` — blob build rule + placement (Task 5)
- `shims/src/util.c` — `loadanim_paint` (Task 6)
- `shims/src/cart.c` — export `pb_left` (Task 6)
- `shims/src/main.c` — tick call site (Task 6)
- `docs/kb/00-status.md`, `docs/kb/tooling.md` — records (Tasks 1, 8)

---

### Task 1: Fork CLEO-TEX raw-texture dump hook

**Files:**
- Modify: `tools/flycast-src/core/rend/TexCache.cpp` (the `config::DumpTextures` block near line 683)
- Modify: `docs/kb/tooling.md` (new subsection under the Flycast fork notes)

**Interfaces:**
- Produces: with `rend.DumpTextures = yes` and env `FLYCAST_TEXRAW=<dir>`, every texture-cache upload writes `<dir>/<hash8>.raw` (raw VRAM bytes of the texture) and, for palettized formats, `<dir>/<hash8>.pal` (all 1024 palette-RAM entries, u32 each), plus a log line:
  `CLEO-TEX hash=%08x addr=%08x size=%x tcw=%08x tsp=%08x w=%d h=%d`
  The stock dumper simultaneously writes the decoded `texdump/<game>/<hash8>.png` — same hash, so PNG↔raw pairs match by filename.
- Consumed by: Tasks 2–4 (identification, cart locate, decode cross-check).

**Steps:**

- [ ] **Step 1: Verify Flycast symbol names before writing code**

Run (all expected to hit; adjust the Step 2 code to what grep actually shows):
```bash
cd tools/flycast-src
grep -n "PixelPal4\|PixelPal8" core/hw/pvr/ta_structs.h core/hw/pvr/pvr_regs.h core/types.h 2>/dev/null
grep -n "palette_ram" core/hw/pvr/pvr_regs.h core/hw/pvr/pvr_regs.cpp | head -3
grep -n "TexAddr\|PixelFmt" core/hw/pvr/ta_structs.h | head
grep -n "RamRegion vram\|vram\[" core/hw/pvr/pvr_mem.h core/hw/pvr/pvr_mem.cpp | head -5
grep -n "tsp.TexU\|8 << tsp" core/rend/TexCache.cpp | head -5
```
Expected: `TCW` has `TexAddr` (VRAM address >> 3) and `PixelFmt` (5 = Pal4, 6 = Pal8); `palette_ram` is a global u32 array of 1024; `vram` supports `&vram[addr]`; texture width/height derive as `8 << tsp.TexU` / `8 << tsp.TexV`.

- [ ] **Step 2: Add the CLEO-TEX hook**

In `TexCache.cpp`, inside the existing `if (config::DumpTextures)` block, immediately after the `custom_texture.dumpTexture(...)` call:

```cpp
		// CLEO-TEX: raw texture bytes + metadata for the cart-locator pipeline
		// (cleopatra scripts/extract_dancer.py). The PNG alone is decoded --
		// useless for byte-matching against the cart image.
		{
			u32 texaddr = (tcw.TexAddr << 3) & VRAM_MASK;
			int tw = 8 << tsp.TexU, th = 8 << tsp.TexV;
			NOTICE_LOG(RENDERER, "CLEO-TEX hash=%08x addr=%08x size=%x tcw=%08x tsp=%08x w=%d h=%d",
					texture_hash, texaddr, size, tcw.full, tsp.full, tw, th);
			if (const char *dir = getenv("FLYCAST_TEXRAW"))
			{
				char p[512];
				snprintf(p, sizeof p, "%s/%08x.raw", dir, texture_hash);
				FILE *f = fopen(p, "wb");
				if (f) { fwrite(&vram[texaddr], 1, size, f); fclose(f); }
				if (tcw.PixelFmt == PixelPal4 || tcw.PixelFmt == PixelPal8)
				{
					snprintf(p, sizeof p, "%s/%08x.pal", dir, texture_hash);
					f = fopen(p, "wb");
					if (f) { fwrite(palette_ram, 4, 1024, f); fclose(f); }
				}
			}
		}
```
(If `PixelPal4`/`PixelPal8` enum names differ, use the raw values: `tcw.PixelFmt == 5 || tcw.PixelFmt == 6`.)

- [ ] **Step 3: Build the fork**

Per `docs/kb/tooling.md` (existing build recipe):
```bash
cd tools/flycast-src && cmake --build build -j 8
```
Expected: clean build, `build/Flycast.app/Contents/MacOS/Flycast` updated.

- [ ] **Step 4: Smoke-test the hook**

```bash
mkdir -p /tmp/texraw
# enable dumping in the Flycast config the capture harness uses:
# emu.cfg [config] rend.DumpTextures = yes   (grep tooling.md for the cfg path)
FLYCAST_TEXRAW=/tmp/texraw <flycast> build/cleo.gdi   # run ~30 s of attract, quit
ls /tmp/texraw | head; grep "CLEO-TEX" <logfile> | head
```
Expected: dozens of `.raw` files, matching `CLEO-TEX` log lines, and PNGs in the stock texdump dir. If zero output: check `rend.DumpTextures` actually took (the mipmap interaction at `TexCache.cpp:551` proves the flag is live when on).

- [ ] **Step 5: Commit fork + document**

Fork: `git -C tools/flycast-src commit -am "CLEO-TEX: raw texture + palette dump for cart locator"`.
Main repo: add a `tooling.md` subsection "CLEO-TEX texture dump" recording: the flag (`rend.DumpTextures`), the env var (`FLYCAST_TEXRAW`), output naming, the log format, and the fork commit hash. Commit: `git commit -m "tooling: CLEO-TEX raw texture dump (fork) — dancer extraction input"`.

---

### Task 2: Identify the dancer frames + cadence; draft manifest

**Files:**
- Create: `scripts/anim_manifest.json`
- Modify: `.gitignore` (add `texraw/` if dumps land in-repo; keep dumps out of git)

**Interfaces:**
- Produces: `scripts/anim_manifest.json` (committed). Schema consumed by Tasks 3–4:

```json
{
  "fb_fmt": "0555",
  "ticks_per_frame": 8,
  "note": "dancing-Cleopatra bg loop; offsets into the gitignored .dat",
  "frames": [
    { "parts": [ { "hash": "3fa2c1b7", "cart_off": null, "len": 32768,
                   "tcw": "0x04123456", "tsp": "0x00000000",
                   "w": 128, "h": 128,
                   "crop": [0, 0, 96, 128], "dx": 0, "dy": 0 } ] }
  ],
  "palette": { "hash": null, "cart_off": null, "len": 1024 }
}
```
  - One `frames[]` entry per animation pose, in loop order. `parts[]` allows multi-sprite composites (usually length 1). `crop` (optional, `[x,y,w,h]` in texture pixels) handles atlas textures; `dx`/`dy` place a part in the output frame. `cart_off` stays `null` until Task 3. `palette` present only if the textures are palettized. `ticks_per_frame` = 60 Hz vblank ticks per pose.

**Steps:**

- [ ] **Step 1: Capture a dump run + a 60 fps screen recording of attract**

Run the Task-1 setup for ~60 s of attract (the dancer loops constantly there). Simultaneously screen-record the Flycast window for cadence: `screencapture -v -V 30 /tmp/attract.mov` (stock macOS; if frame-stepping needs ffmpeg, `brew install ffmpeg` and record the install in `tooling.md`).

- [ ] **Step 2: Identify the dancer textures**

Read the dumped PNGs (they are viewable images; filenames are `<hash8>.png`). Select the textures that compose the dancing-Cleopatra background figure. Expect one of: (a) N per-pose textures, (b) one atlas with N poses (use `crop`), (c) pose = several overlaid parts (use multi-`parts`). Record each part's hash/tcw/tsp/w/h from the matching `CLEO-TEX` log lines.

- [ ] **Step 3: Establish loop order + cadence**

From the recording (`ffmpeg -i /tmp/attract.mov -vf fps=60 /tmp/fr/%04d.png` and read the frames, or QuickTime frame-step): write down the pose sequence and how many 60 Hz frames each pose is held. `ticks_per_frame` = that hold count (use the dominant value; per-pose variable timing is NOT supported by the blob — flag it in the manifest note if the real animation is non-uniform and pick the closest uniform rate).

- [ ] **Step 4: Write the draft manifest and sanity-check the budget**

Fill `scripts/anim_manifest.json` (offsets null). Check: `8 + 512 + nframes * out_w * out_h <= 114688` where `out_w/out_h` are the composited frame dimensions (after crop, before any scaling). If over budget, plan the bake-time crop/downscale now (e.g. crop dead margins first, then integer 2:1 downscale) and note it in the manifest `note`.

- [ ] **Step 5: Commit**

```bash
git add scripts/anim_manifest.json .gitignore
git commit -m "anim: dancer frame manifest (hashes, order, cadence) — offsets pending"
```

---

### Task 3: Locate frame bytes in the cart; confirm composite fb_depth

**Files:**
- Modify: `scripts/anim_manifest.json` (fill `cart_off` fields, confirm `fb_fmt`)

**Interfaces:**
- Produces: manifest with every `parts[].cart_off` (and `palette.cart_off` if palettized) filled with a byte offset into `Cleopatra Fortune Plus.dat`, verified by exact byte match; `fb_fmt` confirmed for the composite path.

**Steps:**

- [ ] **Step 1: Locate each `.raw` in the cart**

```python
# one-off, run with python3 - <<'EOF' style; not committed
import json, pathlib
dat = pathlib.Path("Cleopatra Fortune Plus.dat").read_bytes()
m = json.load(open("scripts/anim_manifest.json"))
for fr in m["frames"]:
    for p in fr["parts"]:
        raw = pathlib.Path(f"/tmp/texraw/{p['hash']}.raw").read_bytes()
        off = dat.find(raw)
        print(p["hash"], hex(off) if off >= 0 else "NOT FOUND",
              "unique" if off >= 0 and dat.find(raw, off + 1) < 0 else "")
EOF
```
Expected: every part found, unique. Fill `cart_off` (decimal or `"0x..."` — pick one, extract_dancer.py must parse what you choose; use decimal). If a raw is NOT found: try the first half of the bytes (the tail may be mip levels); if still not found, the game transforms textures between cart and VRAM — STOP and report before inventing a workaround (the pipeline design assumes PVR-ready cart textures).

- [ ] **Step 2: Palette (only if palettized textures)**

Same `find` for the 4 KB `.pal` dump; palettes may live in cart as a smaller slice — search the relevant 1024/64-byte bank (`PalSelect`-indexed) rather than all 4 KB if the full block misses. If the palette is genuinely absent from the cart (built at runtime), save the `.pal` file as gitignored `loader/dancer.pal` + manifest `"palette": {"file": "loader/dancer.pal", ...}` and document regeneration (same category as `loader/splash.png`: harvested input, gitignored, regenerable via Task-1 tooling).

- [ ] **Step 3: Confirm composite-path fb_depth**

```bash
grep -h "FB_R_CTRL" capture-*.log | sort -u   # look for a Cable=3 run first
```
If no composite-cable line exists: one Flycast run with `Dreamcast.Cable = 3` (config edit, tooling.md documents the file), grep the new log for the takeover `FB_R_CTRL` write (the one with `pc=8c......`, value nonzero). Expected: fb_depth bits 3:2 = 0 (RGB0555), same as the VGA path's `0x00800001`. Record the value in the manifest `note`. If it is NOT 0555 on either path, set `fb_fmt` accordingly (extract_dancer.py supports 0555/565) — and if the two paths differ from each other, STOP and report (blob would need two palettes; design decision needed).

- [ ] **Step 4: Commit**

```bash
git add scripts/anim_manifest.json
git commit -m "anim: manifest offsets located in cart; composite fb_depth confirmed"
```

---

### Task 4: extract_dancer.py — decode + bake (TDD)

**Files:**
- Create: `scripts/extract_dancer.py`
- Test: `scripts/extract_dancer.py --selftest` (self-contained, no fixtures on disk)

**Interfaces:**
- Consumes: `scripts/anim_manifest.json` (Task 2/3 schema), the `.dat`, optionally a `.pal` file.
- Produces:
  - CLI: `python3 scripts/extract_dancer.py --manifest scripts/anim_manifest.json --dat "Cleopatra Fortune Plus.dat" --out build/anim.bin [--png-dir build/anim_png]`
  - `build/anim.bin` blob, layout (all little-endian, consumed by Tasks 5–6):
    - `u16 w, h` — frame dimensions in pixels
    - `u16 nframes`
    - `u16 ticks_per_frame`
    - `u16 pal[256]` — scanout-format colors (0555 default; index 0 = transparent, value 0)
    - `nframes × (w*h)` bytes — 8bpp indices, row-major from top-left
  - Hard assert: `len(blob) <= 114688` (= `ANIM_MAX`; keep the literal + comment in the script).
  - `--png-dir`: one PNG per baked frame for eyeballing (index 0 rendered magenta so transparency is visible).

**Steps:**

- [ ] **Step 1: Write the selftest first (failing)**

`--selftest` exercises the pure functions with synthetic data — no ROM needed, safe for `make test`:

```python
def selftest():
    # 8x8 twiddled PAL8 texture: index = x^y pattern, palette = grayscale ARGB8888
    import itertools
    idx = bytes(tw_encode_pal8(8, 8, [[ (x ^ y) for x in range(8)] for y in range(8)]))
    pal = [0xFF000000 | (v << 16) | (v << 8) | v for v in range(256)]
    px = decode_pal8(idx, 8, 8, pal)              # -> rows of (a, r, g, b)
    assert px[0][0] == (255, 0, 0, 0)
    assert px[1][0] == (255, 1, 1, 1)             # (x=0,y=1) -> index 1
    assert pack0555(255, 255, 255) == 0x7fff and pack0555(0, 0, 0) == 0x0000
    assert pack565(255, 255, 255) == 0xffff
    # quantize: <=255 distinct colors survive exactly; alpha==0 -> index 0
    rows = [[(255, 8 * c, 0, 0) for c in range(8)] for _ in range(8)]
    rows[0][0] = (0, 99, 99, 99)                  # transparent pixel
    frames, pal16 = bake([rows], "0555")
    assert frames[0][0] == 0 and pal16[0] == 0
    blob = pack_blob(8, 8, 1, 4, pal16, frames)
    assert blob[:8] == (8).to_bytes(2, "little") * 2 + (1).to_bytes(2, "little") + (4).to_bytes(2, "little")
    assert len(blob) == 8 + 512 + 64
    print("extract_dancer selftest: OK")
```
`tw_encode_pal8` is a selftest-only helper (inverse of the decoder — encode via the same `detwiddle_index`). Run: `python3 scripts/extract_dancer.py --selftest`. Expected: **NameError/failure** (functions not yet written).

- [ ] **Step 2: Implement**

Structure (stdlib-only; reuse `detwiddle_index` by `import pvrview` — same directory):
- `decode_texture(raw, tcw, tsp, pal)` — dispatch on TCW bits (`PixelFmt` = bits 29:27 of `tcw`: 0=1555, 1=565, 2=4444, 5=Pal4, 6=Pal8; `ScanOrder` bit 26 = 1 raw/0 twiddled; `VQ_Comp` bit 30): twiddled 16bpp via `pvrview.detwiddle_index` + `pvrview.px_to_rgb`; Pal4/Pal8 indices detwiddled then looked up in the ARGB8888 palette; VQ: 256×(2×2 texel) u16 codebook first, then twiddled byte indices at quarter resolution. Only implement the branches the manifest's tcw words actually need, `raise SystemExit` loudly on others (pvrview house style).
- `composite(frame_parts)` — apply `crop`, paste at `dx/dy` onto a transparent canvas sized to the manifest's largest frame.
- `bake(frames_argb, fb_fmt)` — collect distinct opaque colors (error if > 255: "add a downscale/crop step, do not silently quantize"); index 0 reserved for alpha==0; palette packed by `pack0555`/`pack565`.
- `pack_blob(w, h, n, tpf, pal16, frames)` — the exact layout above.
- `main()` — argparse, read manifest + .dat slices at `cart_off`, decode → composite → bake → assert size → write blob + PNGs.

- [ ] **Step 3: Selftest green**

`python3 scripts/extract_dancer.py --selftest` → `extract_dancer selftest: OK`.

- [ ] **Step 4: Real run + eyeball**

```bash
python3 scripts/extract_dancer.py --manifest scripts/anim_manifest.json \
  --dat "Cleopatra Fortune Plus.dat" --out build/anim.bin --png-dir build/anim_png
```
Read the PNGs: the dancer poses must look right (colors sane, no twiddle scramble, transparency where background should show). Compare against the Task-1 stock PNG dumps — same figure. Byte-budget printout ≤ 114688.

- [ ] **Step 5: Wire selftest into `make test` + commit**

Top-level `Makefile` test target: add `python3 scripts/extract_dancer.py --selftest` alongside the existing script selftests (grep for `test_shim_iface` in the test recipe to find the list). Run `make test` → green.
```bash
git add scripts/extract_dancer.py Makefile
git commit -m "anim: extract_dancer.py — cart PVR decode + 0555 bake, selftest in make test"
```

---

### Task 5: Region map + loader placement

**Files:**
- Modify: `shims/include/shim_iface.h`
- Modify: `shims/test/test_shim_iface.c`
- Modify: `loader/Makefile`
- Modify: `loader/main.c`

**Interfaces:**
- Consumes: `build/anim.bin` (Task 4 CLI + blob layout).
- Produces: `ANIM_DATA` (0x8cfd3800), `ANIM_MAX` (0x1c000), `SHIM_LOADANIM` (default 1) in `shim_iface.h`; blob placed at `ANIM_DATA` and cache-purged before handoff. Task 6's shim reads the blob at `ANIM_DATA` via cached P1.

**Steps:**

- [ ] **Step 1: Failing region asserts first**

`shims/include/shim_iface.h`, after the `MAPLE_MIRROR` block:
```c
/* Load-screen dancer animation blob (extract_dancer.py; layout: u16 w,h,n,tpf;
 * u16 pal[256]; u8 idx[n][w*h]). Placed by the loader, read by the shim via
 * cached P1 (loader purges). Budget ceiling is the KOS LOADER stack bottom
 * 0x8cff0000 -- the loader memcpys this while running on that stack. */
#define ANIM_DATA       (SHIM_BASE + 0x13800)
#define ANIM_MAX        0x1c000               /* 114688 B; extract_dancer.py asserts too */

/* Dancer animation over the load screen (0 = compiled out). Reuses the
 * loadbar's boot-preload countdown as its lifetime gate, so LOADBAR is a
 * hard prerequisite. */
#ifndef SHIM_LOADANIM
#define SHIM_LOADANIM 1
#endif
#if SHIM_LOADANIM && !SHIM_LOADBAR
#error "SHIM_LOADANIM requires SHIM_LOADBAR (shares its pb_left gate)"
#endif
```
`shims/test/test_shim_iface.c`, extend the region map (replace the current final `MAPLE_MIRROR` ceiling line):
```c
    assert(MAPLE_MIRROR + MAPLE_MIRROR_LEN <= ANIM_DATA);             /* maple mirror | anim blob */
    assert(ANIM_DATA + ANIM_MAX <= 0x8cff0000u);                      /* map top | KOS stack bottom */
```

- [ ] **Step 2: Run `make test` — expect PASS**

(The asserts are true by construction; this step proves they compile and run. A deliberate `ANIM_MAX 0x2c000` dry-run should fail the new ceiling assert — try it, see the abort, revert.)

- [ ] **Step 3: Loader Makefile — blob rule + embed**

`loader/Makefile`: add `anim_blob.o` to `OBJS`, then:
```make
# Dancer animation blob: baked from the gitignored cart by extract_dancer.py
# (manifest is committed; blob is a build product). Size-capped at ANIM_MAX --
# the loader also halt()s, but failing at build beats failing on-console.
../build/anim.bin: ../scripts/extract_dancer.py ../scripts/anim_manifest.json
	cd .. && python3 scripts/extract_dancer.py --manifest scripts/anim_manifest.json \
	  --dat "Cleopatra Fortune Plus.dat" --out build/anim.bin
	@test $$(stat -f%z $@) -le 114688 || { echo "anim.bin over ANIM_MAX"; rm -f $@; exit 1; }
anim_blob.o: ../build/anim.bin
	sh-elf-objcopy -I binary -O elf32-shl -B sh4 \
	  --redefine-sym _binary____build_anim_bin_start=_anim_bin \
	  --redefine-sym _binary____build_anim_bin_end=_anim_bin_end \
	  $< $@
```
Also add `../build/anim.bin` to the `clean` recipe's `rm -f` list.

- [ ] **Step 4: Loader placement**

`loader/main.c`: with the other extern blobs (top of file):
```c
extern uint8 anim_bin[];        /* objcopy-embedded dancer blob (SHIM_LOADANIM) */
extern uint8 anim_bin_end[];
```
After the BIOS-data placement block (after the `bios-data placed` dbglog):
```c
#if SHIM_LOADANIM
    /* Dancer-animation blob for the shim's load-screen paint. Above the maple
     * mirror, below the KOS stack bottom (region asserts in test_shim_iface). */
    uint32 anim_len = (uint32)(anim_bin_end - anim_bin);
    if (anim_len > ANIM_MAX) halt("ANIM BLOB TOO BIG");
    memcpy((void *)ANIM_DATA, anim_bin, anim_len);
    dcache_purge_range(ANIM_DATA, anim_len);
    dbglog(DBG_INFO, "anim placed %08x/%lx\n", (unsigned)ANIM_DATA, (unsigned long)anim_len);
#endif
```
Also update the stale SP-probe comment (`loader/main.c:143`): the loader's highest write is now `ANIM_DATA + ANIM_MAX = 0x8cfef800`, still below the stack bottom.

- [ ] **Step 5: Build + test + commit**

`make` (full disc build) and `make test` — both green.
```bash
git add shims/include/shim_iface.h shims/test/test_shim_iface.c loader/Makefile loader/main.c
git commit -m "anim: region map + loader placement (ANIM_DATA, budget-asserted)"
```

---

### Task 6: Shim playback

**Files:**
- Modify: `shims/src/util.c` (new `loadanim_paint` + arm in `shim_vid_init`)
- Modify: `shims/src/cart.c` (export `pb_left`)
- Modify: `shims/src/main.c` (tick call)

**Interfaces:**
- Consumes: blob at `ANIM_DATA` (Task 4 layout, Task 5 placement); `pb_left` countdown (cart.c); `shim_cable_is_vga()`; the loadbar's virgin-blackout ordering.
- Produces: `void loadanim_paint(void)` — safe to call every steady tick from any context; no-op before video takeover and after the boot-preload countdown expires.

**Steps:**

- [ ] **Step 1: Export the gate**

`shims/src/cart.c:71`: `static u32 pb_left = PB_TOTAL;` → `u32 pb_left = PB_TOTAL;               /* exported: loadanim_paint's lifetime gate */`

- [ ] **Step 2: Add `loadanim_paint` to util.c**

After the `loadbar_paint` block (still inside a new `#if SHIM_LOADANIM` region):
```c
#if SHIM_LOADANIM
/* Dancer animation over the load screen. Blob at ANIM_DATA (loader-placed;
 * layout in shim_iface.h). Painted every steady tick: repaint converges both
 * flip buffers (loadbar precedent); pacing counts ticks (~60 Hz vblank pump --
 * ponytail: tick-count clock, TCNT0 upgrade only if HW shows drift). Palette
 * is pre-baked in the scanout format (RGB0555 -- CLEO-SPG takeover evidence),
 * index 0 = transparent. Per-tick FB_R_SOF1 read + unblank RMW are the
 * hex_paint per-poll idiom, Flycast-present-safe (rounds 14-18); no other
 * register touches. anim_wait holds paints until shim_vid_init has run
 * loadbar_paint(0) (blackout + video takeover); pb_left==0 ends the anim
 * with the bar, before the title ever presents. */
extern u32 pb_left;                    /* cart.c boot-preload countdown */
static u32 anim_wait = 1;              /* .data non-zero init (house style) */
void loadanim_paint(void) {
    if (anim_wait || pb_left == 0) return;
    const unsigned short *hdr = (const unsigned short *)ANIM_DATA;
    unsigned int w = hdr[0], h = hdr[1], n = hdr[2], tpf = hdr[3];
    if (!w || w > 320u || !h || h > 160u || !n || n > 64u || !tpf)
        return;                        /* stale/absent blob: paint nothing */
    static u32 tk, fr;                 /* .bss (loader zeroes) */
    if (++tk >= tpf) { tk = 0; if (++fr >= n) fr = 0; }
    const unsigned char *px = (const unsigned char *)(hdr + 4 + 256) + fr * (w * h);
    unsigned int base = *(volatile unsigned int *)0xa05f8050 & 0x00fffffcu;
    volatile unsigned short *fb = (volatile unsigned short *)(0xa5000000u + base);
    unsigned int yb = shim_cable_is_vga() ? 417u : 200u;   /* bar row (loadbar_paint) */
    unsigned int y0 = yb - 8u - h, x0 = (640u - w) / 2u;   /* centered, 8 px above bar */
    for (unsigned int y = 0; y < h; y++)
        for (unsigned int x = 0; x < w; x++) {
            unsigned char c = px[y * w + x];
            if (c) fb[(y0 + y) * 640u + x0 + x] = ((const unsigned short *)(hdr + 4))[c];
        }
    *(volatile unsigned int *)0xa05f80e8 &= ~8u;           /* keep video unblanked */
}
#endif
```

- [ ] **Step 3: Arm at video takeover**

In `shim_vid_init` (util.c), right after the existing `loadbar_paint(0);` line inside its `#if SHIM_LOADBAR` block:
```c
#if SHIM_LOADANIM
    anim_wait = 0;                     /* blackout done (loadbar virgin latch) -- animate */
#endif
```

- [ ] **Step 4: Call from the steady tick**

`shims/src/main.c`, in `shim_maple_steady` immediately after `la_tick();`:
```c
#if SHIM_LOADANIM
    { extern void loadanim_paint(void); loadanim_paint(); }
#endif
```

- [ ] **Step 5: Build discipline check**

`make` — the shim must still fit `SHIM_CODE_MAX` (shim.ld ASSERT enforces; `loadanim_paint` is ~200 bytes). Verify no libgcc symbols crept in: `sh-elf-nm shims/build/shim.elf | grep -i "udiv\|__mul"` → empty (`(640-w)/2` compiles to a shift; `fr*(w*h)` is native `mul.l`).

- [ ] **Step 6: `make test` + Flycast attract regression**

`make test` green. Then the standard Flycast attract screenshot check (tooling.md capture procedure): boot → attract screenshot must be normal (the anim itself is invisible to Flycast's renderer — this run checks for present regressions from the per-tick paints; the hex_paint per-poll precedent says none).

- [ ] **Step 7: Commit**

```bash
git add shims/src/util.c shims/src/cart.c shims/src/main.c
git commit -m "anim: loadanim_paint — dancer loop above the boot bar, pb_left-gated"
```

---

### Task 7: CLEO-VRAMDUMP verification, both cable classes

**Files:**
- None committed (verification evidence; note results in Task 8's docs)

**Interfaces:**
- Consumes: the built disc, the fork's `FLYCAST_VRAMDUMP=<prefix>` snapshot hook (`tools/flycast-src/core/hw/pvr/pvr_regs.cpp:204` — snapshots on shim unblank writes, logs `sof1/sof2/fb_r_size`).

**Steps:**

- [ ] **Step 1: Composite-class run**

Flycast config `Dreamcast.Cable = 3`, `FLYCAST_VRAMDUMP=/tmp/vd-tv` — boot to title, quit. NOTE: the anim unblanks every tick, so expect MANY snapshots (the loadbar verification saw per-paint dumps); disk is cheap, delete after. Pick a mid-load snapshot (one whose log line's timestamp falls between video takeover and title).

- [ ] **Step 2: Decode + assert pixels**

Decode the snapshot at the logged `sof1` base through the 32↔64-bit bank swizzle exactly as the loadbar verification did (`pvr_map32` — the existing decode helper from the 2026-08-02 composite-loadbar session; grep the fork/scripts for `pvr_map32`). Assert:
- bar outline present at rows 200/211 (existing behavior, unregressed);
- dancer pixels present in rows `[200-8-h, 200-8)` centered at x `(640-w)/2`, values ∈ the baked palette;
- nothing painted at VGA rows 417–428.

- [ ] **Step 3: VGA-class run**

Same with `Dreamcast.Cable = 0`, `/tmp/vd-vga`: bar at 417–428, dancer in `[417-8-h, 417-8)`, nothing at 200–211.

- [ ] **Step 4: Frame-advance sanity**

Compare two snapshots ≥ `ticks_per_frame` ticks apart: the dancer pixel block must differ (animation actually advances). If identical, check `tpf` handling and that snapshots aren't from the same tick.

---

### Task 8: Release build, HW handoff, docs

**Files:**
- Modify: `docs/kb/00-status.md` (Phase-5 entry)
- Modify: `docs/kb/tooling.md` (if any tool/procedure wasn't recorded in Tasks 1–7)

**Steps:**

- [ ] **Step 1: Release-flag audit**

Confirm release toggles: `SHIM_LOADSTAT=0`, `LOADER_TIMING=0`, `SHIM_HUD=0`, `SHIM_TRACE=0`, `LOADER_SERIAL=0`, `SHIM_LOADBAR=1`, `SHIM_LOADANIM=1`, `SHIM_GD_DMA=1`. `make test` + full `make` green.

- [ ] **Step 2: Deploy for HW**

`make deploy` (card copy + dot_clean guard). Hand to the user with the checklist: on BOTH composite and VGA — splash → black + bar + **dancing Cleopatra above the bar, correct colors** → title, no blink, no residue, gameplay unaffected. A color-shifted dancer = fb_fmt wrong for that cable path (Task 3 evidence says 0555 both); a scrambled dancer = decode/twiddle bug (but Task 4 PNGs would have shown it); missing dancer on ONE cable = y-position/scan-lines bug.

- [ ] **Step 3: Record the verdict**

`00-status.md`: add the load-screen-animation entry (design → extraction → HW verdict, cable coverage, final flag states). Commit:
```bash
git add docs/kb/00-status.md docs/kb/tooling.md
git commit -m "kb: load-screen dancer animation — status + HW verdict"
```

---

## Self-review notes

- Spec coverage: A1→Task 1–2, A2→Task 2, A3→Task 3, A4→Task 3 (pre-answered for VGA from existing logs), A5→Task 4, placement/regions→Task 5, playback/gate→Task 6, Flycast verify→Task 7, HW verify→Task 8. Spec's "~180 KB" budget corrected to 114 KB (KOS loader stack ceiling — already the region-map invariant in `test_shim_iface.c:32`).
- The spec's "TCNT0-paced" playback is implemented as tick-count pacing (the steady tick IS the vblank pump, so ticks ≈ 60 Hz; no register reads added — Flycast-present-safer). Marked as a knob in the code comment; upgrade to TCNT0 only if HW shows drift.
- Unknown-branches (VQ, atlas, multi-part, palette-not-in-cart, >255 colors) each have an explicit handling path in Tasks 2–4, with STOP-and-report where a silent workaround would hide a wrong assumption.
