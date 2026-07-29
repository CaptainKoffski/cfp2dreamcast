# Ghidra analysis, reconstructible

`boot.xml` is the **Program XML export of the `cleo3` Ghidra analysis** of the
game's `boot.bin` — 1645 functions, 271 named symbols, 22,415 typed data items.
It carries the *analysis layer only* (function boundaries + names + types), not
the copyrighted image. Anyone with their own dump can reconstruct the full
Ghidra project from it, deterministically — no auto-analysis, no version drift.

## Why this format

The disassembly is a deterministic function of the bytes; the *understanding*
(names, function boundaries, types) is the human work worth preserving. Ghidra's
own Program XML exporter serializes exactly that layer into `boot.xml` and puts
the raw image in a separate `boot.bytes` file. We ship the XML, never the bytes.
Round-tripping through Ghidra's own importer means no hand-rolled parser and no
restore-drift — the tool that wrote it reads it back.

- `boot.xml` — the analysis (committed). References a companion image file named
  `boot.bytes`, so the image you supply in step 2 below must be named exactly that.
- `boot.bytes` — byte-identical to `boot.bin`, i.e. the copyrighted image.
  **gitignored (`*.bytes`), never commit.**
- `scripts/ghidra/ExportToXML.java` — headless script that generated `boot.xml`
  (regen below), run through the existing `scripts/ghidra/run.sh` harness.

## Reconstruct the project (you need your own ROM)

Tested on Ghidra 12.1.2, JDK 26, macOS.

1. Slice the SH-4 program out of your own cart image — it's the first 1 MiB,
   nothing else needed:
   ```sh
   dd if="Cleopatra Fortune Plus.dat" of=docs/kb/ghidra/boot.bytes bs=1M count=1
   ```
   Why only 1 MiB: the `.dat` is the whole ~104 MB Naomi cartridge (program +
   graphics/audio/tables). Only the first 1 MiB is the SH-4 code that runs at
   `0x8c020000` — the region this analysis covers (see `../boot-binary.md`). The
   rest is streamed assets, not code, and irrelevant here. The file must be named
   `boot.bytes` (the XML references it by that name) and be the same dump — the
   XML memory image expects byte-for-byte identical contents.
2. (already done by step 1 — `boot.bytes` now sits next to `boot.xml`, and is
   gitignored so it never gets committed.)
3. Import — GUI: **File → Import File → `boot.xml`** (loader: *XML Input
   Format*). Or headless:
   ```sh
   export JAVA_HOME=$(/usr/libexec/java_home)   # a JDK Ghidra accepts (21+; 26 works)
   tools/ghidra_12.1.2_PUBLIC/support/analyzeHeadless \
     <project-dir> <project-name> -import docs/kb/ghidra/boot.xml -noanalysis
   ```

You get the exact project: 1645 functions with the original names and
boundaries, all data typing. **Do not run auto-analysis** — the XML already
carries the finished analysis; re-analyzing would only add the analyzer's
guesses on top.

### Verified

Exported from `cleo3`, imported into a fresh project, dumped both function
tables (entry+name) and diffed: **identical, all 1645.** Defined data
22,415 = 22,415. Symbols 9065 vs 9067 (2 auto-labels not recreated) and 16
per-function comment sets throw a benign `NullPointerException` during import —
a known SuperH4 quirk in Ghidra's `FunctionsXmlMgr`, cosmetic only; functions,
names, and types all land.

## Regenerate `boot.xml` (when the analysis advances)

Through the existing harness (needs your local `cleo3` project + `boot.bin`):

```sh
scripts/ghidra/run.sh script ExportToXML.java "$PWD/docs/kb/ghidra/boot.xml"
```
Output to a file named `boot.xml` so the companion reference stays
`boot.bytes` (it derives from the output filename). This also writes
`docs/kb/ghidra/boot.bytes` — leave or delete it, it's gitignored either way.

## Full private backup

`boot.xml` is the clean, shareable layer. For a complete private backup
(including comments/bookmarks and the exact DB state), export the whole project
as a **Ghidra Archive** (`File → Archive Current Project`, `.gar`) — but it
embeds the image, so keep it offline / in a private store, never here.
