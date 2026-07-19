#!/usr/bin/env python3
"""Generate build/patch_table.h from patch definitions + tools/boot.bin +
shims/build/shim.map + shims/include/shim_iface.h.

Definitions reference shim entry points by name (resolved from shim.map) and the
G1 register-mirror block (resolved from shim_iface.h). Original bytes are read
from boot.bin at generation time and asserted against the value the KB claims is
there -> a wrong address/value fails the build BEFORE it can corrupt the game
image at boot. Output embeds original ROM bytes, so build/ is gitignored.

Patch kinds (all little-endian, addresses are game VAs 0x8c02xxxx..):
  pool(addr, expect, value)   u32 config-time literal: assert cur==expect, write value
  ptr (addr, expect, target)  u32 fn-pointer slot: assert cur==expect, write target
  hook(fn, target)            overwrite fn entry with a 6-byte SH-4 thunk +
                              pooled .long target: mov.l @(disp,PC),r0; jmp @r0; nop

Sources (all cited in docs/kb/phase4-conversion.md):
  §V3          -> cart completion-wait hook (FUN_8c03bc12)
  §patch-sites -> 13 cart/G1 mirror repoints (#1 descriptor base + #2-13 literals)
  §input-ABI   -> boot + steady MIE fn-pointer slot swaps
"""
import pathlib, re, struct, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BASE = 0x8C020000                                 # boot.bin offset = addr - BASE
boot = (ROOT / "tools/boot.bin").read_bytes()
assert len(boot) == 0x100000, "tools/boot.bin missing/wrong size (need 1 MB)"

# ---- shim entry points from shim.map ------------------------------------
# Symbols carry a leading _ (__USER_LABEL_PREFIX__=_); look up with or without.
symtab = {}
for line in (ROOT / "shims/build/shim.map").read_text().splitlines():
    p = line.split()
    if len(p) == 3:
        symtab[p[2]] = int(p[0], 16)

def sym(name, p2=False):
    v = symtab.get(name)
    if v is None:
        v = symtab.get("_" + name)          # C name -> asm label _name
    if v is None:
        sys.exit(f"symbol {name!r} not in shim.map (rebuild: make -C shims)")
    return (v | 0xA0000000) if p2 else v     # code entries run cached (P1): p2=False

# ---- G1 mirror block from shim_iface.h ----------------------------------
# G1_MIRROR = (SHIM_BASE + 0x8800), accessed via P2 (P2ADDR = a | 0xa0000000).
# Parsed (not hardcoded) so a base/offset move in shim_iface.h is tracked; a
# naive "#define NAME 0xhex" regex would silently drop the expression-valued
# G1_MIRROR, so match the SHIM_BASE + 0xNNNN form explicitly.
iface = (ROOT / "shims/include/shim_iface.h").read_text()
SHIM_BASE = int(re.search(r"#define\s+SHIM_BASE\s+(0x[0-9a-fA-F]+)", iface).group(1), 16)
G1_OFF = int(re.search(r"#define\s+G1_MIRROR\s+\(SHIM_BASE\s*\+\s*(0x[0-9a-fA-F]+)\)",
                       iface).group(1), 16)
MIRROR_P2 = (SHIM_BASE + G1_OFF) | 0xA0000000     # 0xacfc8800

def _sb_off(name):     # "#define NAME (SHIM_BASE + 0xNNNN)" -> abs address
    m = re.search(rf"#define\s+{name}\s+\(SHIM_BASE\s*\+\s*(0x[0-9a-fA-F]+)\)", iface)
    return SHIM_BASE + int(m.group(1), 16)
BIOS_60000_P2  = _sb_off("BIOS_DATA_60000")  | 0xA0000000     # 0xacfcb000
BIOS_1FFD00_P2 = _sb_off("BIOS_DATA_1FFD00") | 0xA0000000     # 0xacfd2000
MAPLE_MIRROR_P2 = _sb_off("MAPLE_MIRROR")    | 0xA0000000     # 0xacfd3000 (Task 14f)

def rd(addr, n):
    off = addr - BASE
    assert 0 <= off <= len(boot) - n, hex(addr)
    return boot[off:off + n]

patches = []   # (addr, old-bytes, new-bytes, comment)

def pool(addr, expect, value, comment=""):
    old = rd(addr, 4)
    got = struct.unpack("<I", old)[0]
    assert got == expect, f"pool @{addr:#x}: found {got:#010x}, expected {expect:#010x}"
    patches.append((addr, old, struct.pack("<I", value), comment))

def ptr(addr, expect, target, comment=""):
    old = rd(addr, 4)
    got = struct.unpack("<I", old)[0]
    assert got == expect, f"ptr @{addr:#x}: found {got:#010x}, expected {expect:#010x}"
    patches.append((addr, old, struct.pack("<I", target), comment))

def insn16(addr, expect, value, comment=""):
    """16-bit SH-4 instruction patch with old-opcode verification."""
    old = rd(addr, 2)
    got = struct.unpack("<H", old)[0]
    assert got == expect, f"insn16 @{addr:#x}: found {got:#06x}, expected {expect:#06x}"
    patches.append((addr, old, struct.pack("<H", value), comment))

def hook(fn, target, comment=""):
    # SH-4 entry thunk: mov.l @(disp,PC),r0 ; jmp @r0 ; nop ; [nop pad] ; .long target
    #   mov.l @(disp,PC),r0 = 0xD000|disp ; EA = disp*4 + (PC&~3) + 4  (PC=fn)
    #   jmp @r0 = 0x402B ; nop = 0x0009 (also the jmp delay slot)
    slot = (fn + 6 + 3) & ~3            # 4-align the pooled .long
    pad = slot - (fn + 6)
    disp = (slot - ((fn & ~3) + 4)) // 4
    assert 0 <= disp <= 255, hex(fn)
    code = struct.pack("<HHH", 0xD000 | disp, 0x402B, 0x0009)
    code += struct.pack("<H", 0x0009) * (pad // 2) + struct.pack("<I", target)
    patches.append((fn, rd(fn, len(code)), code, comment))

def _selftest():
    # Money path: the ROM-verify is worthless unless a wrong expectation raises.
    n = len(patches)
    try:
        ptr(0x8C02DA74, 0xDEADBEEF, 0)         # real value is 0xa05f7000
    except AssertionError:
        assert len(patches) == n, "selftest polluted the patch list"
        return
    sys.exit("SELFTEST FAILED: bad expectation did not raise")

# ---- definitions --------------------------------------------------------
_selftest()

# §V3: cart-DMA completion-wait -- entry hook (reached by `bsr 0x8c03bc12` @0x8c03bd28,
# no pointer to it, so a thunk on the entry is the correct kind).
hook(0x8C03BC12, sym("shim_cart_service"), "V3 cart-DMA wait -> shim_cart_service")

# §patch-sites #1: descriptor-base source (covers all runtime streaming; base is
# read as [desc+0x58] then offset, so every base-relative reg access follows).
pool(0x8C02DA74, 0xA05F7000, MIRROR_P2 + 0x000, "#1 desc base 0x5f7000 -> mirror")

# §patch-sites #2-13: config-time absolute pool literals (each -> mirror+offset).
pool(0x8C08071C, 0xA05F74B8, MIRROR_P2 + 0x4B8, "#2 GDEN cfg 0x5f74b8 -> mirror")
pool(0x8C080720, 0xA05F7480, MIRROR_P2 + 0x480, "#3 GDSTAR 0x5f7480 -> mirror")
pool(0x8C080724, 0xA05F7484, MIRROR_P2 + 0x484, "#4 GDLEN 0x5f7484 -> mirror")
pool(0x8C080728, 0xA05F7490, MIRROR_P2 + 0x490, "#5 GDDIR 0x5f7490 -> mirror")
pool(0x8C08072C, 0xA05F74A4, MIRROR_P2 + 0x4A4, "#6 0x5f74a4 -> mirror")
pool(0x8C0807D8, 0xA05F7418, MIRROR_P2 + 0x418, "#7 SB_GDST 0x5f7418 -> mirror")
pool(0x8C0808E4, 0xA05F7418, MIRROR_P2 + 0x418, "#8 SB_GDST 0x5f7418 -> mirror")
pool(0x8C080904, 0xA05F700C, MIRROR_P2 + 0x00C, "#9 cart 0x5f700c -> mirror")
pool(0x8C080E3C, 0xA05F700C, MIRROR_P2 + 0x00C, "#10 cart 0x5f700c -> mirror")
pool(0x8C081D24, 0xA05F7418, MIRROR_P2 + 0x418, "#11 SB_GDST 0x5f7418 -> mirror")
pool(0x8C081E90, 0xA05F7418, MIRROR_P2 + 0x418, "#12 SB_GDST 0x5f7418 -> mirror")
pool(0x8C081FF8, 0xA05F7418, MIRROR_P2 + 0x418, "#13 SB_GDST 0x5f7418 -> mirror")

# §M2 BIOS-data: redirect the two Naomi BIOS-ROM data pointers (absent on DC) to
# the loader's shim-home RAM copies. Kept P2 uncached to match original access.
pool(0x8C0804D4, 0xA0060000, BIOS_60000_P2,  "#14 BIOS 0x60000 lib -> shim-home copy")
pool(0x8C0814D0, 0xA01FFD00, BIOS_1FFD00_P2, "#15 BIOS 0x1ffd00 str -> shim-home copy")

# §input-ABI: MIE fn-pointer slots (game does jsr @rN, rN loaded from the pool
# word) -> swap the slot to the shim entry (not an entry hook: the swap is the
# whole redirect).
# §Task 14d: the BOOT slot 0x8c027618 (=0x8c0315ce) is NOT hooked. That pool feeds
# the GENERIC transaction dispatcher FUN_8c027584 (160+ callers), whose builder
# 0x8c0315ce serves ALL of its (r10&0x20)==0 transactions -- not only MIE input.
# On DC the boot JVS enumeration uses the raw-maple path (Task 14/14b: shim_maple_boot
# fires 0x before the I/O check), and AFTER the forced I/O check the first call through
# this path is a NON-MIE transaction (frame_header cmd=0xf6/reci=0x04, recv=0xc8000000,
# float payload) -- so hooking it made the shim mis-treat a generic frame as MIE and
# shim_die(3). Behavioural proof (instrumented DC interpreter): hooked+die halts at 149
# cart reads; unhooked (real builder runs) AND tolerate-in-shim BOTH reach 180 reads and
# the IDENTICAL downstream crash (EXC epc=0c10004c evn=180) -- i.e. this frame is a red
# herring, and letting the real builder handle it is both correct and carries the game
# further. The real M3 blocker is downstream (deferred Task-14b async-MIE; see
# phase4-conversion.md §Task 14d). shim_maple_boot stays in the shim as the documented
# boot-MIE ABI, ready to re-hook if a MIE-only call site is ever isolated.
# §Task 14f: async-Maple MIE service (input + EEPROM transport). Mirror the sole
# live maple-base pool word so the steady engine FUN_8c03c2c6 drives shim RAM (no
# real controller DMA), and wrapper-hook BOTH fn-ptr slots that dispatch it to
# shim_maple_steady (Mode A + Mode B; DC takes Mode B). shim_maple_steady calls
# the REAL FUN_8c03c2c6 first (pump must run -- 14b), then walks the descriptor it
# programmed into mirror_SB_MDSTAR, synthesizes each MIE reply into its recv addr
# via maple_reply + baked blobs (sub 0x03 -> eeprom.bin free-play), and clears
# mirror_SB_MDST so the engine's cross-frame poll sees completion. See
# .superpowers/sdd/task-14e-completion-mechanism.md + phase4-conversion.md §14f.
pool(0x8C030FEC, 0xA05F6C00, MAPLE_MIRROR_P2, "#16 Task14f maple base 0x5f6c00 -> mirror (async-MIE)")
ptr(0x8C02ED6C, 0x8C03C2C6, sym("shim_maple_steady"), "Task14f STEADY slot A -> shim_maple_steady")
ptr(0x8C02EE88, 0x8C03C2C6, sym("shim_maple_steady"), "Task14f STEADY slot B -> shim_maple_steady")
# §Task 14c: FORCE the I/O-board-detected check to pass (M3 unblock).
# Dynamic evidence (instrumented DC-mode interpreter on build/cleo.gdi): the current
# 18-patch build registers the I/O board as PRESENT (conn flag 0x8c1c9774 = 1) but its
# JVS feature/ID enumeration returns garbage on DC, so the feature-id 0x8c1ca474 = 0 ->
# the spec-compat result 0x8c0d541c is set to 1 ("no id"), i.e. "DOES NOT FULFILL THE
# GAME SPECS." The per-frame I/O-status handler FUN_8c07a22a (called only from the boot
# scene loop FUN_8c04ae50 @0x8c04b176) reads that result at 0x8c07a262:
#     8c07a262 mov.l 0x8c07a2d8,r0   ; r0 = &spec_result (0x8c0d541c)
#     8c07a264 mov.l @r0,r1          ; r1 = spec_result
#     8c07a266 tst  r1,r1            ; T = (spec == 0 == OK)
#     8c07a268 bt   0x8c07a302       ; OK -> build normal display (FUN_8c07c144) & proceed
# spec!=0 falls through to the error-draw path. Force the OK branch always taken by
# replacing `tst r1,r1` (0x2118) with `sett` (0x0018, T:=1) -> the handler treats the
# board's specs as fulfilled every frame, exactly the "force board present/OK" the M3
# strategy calls for. Minimal (2 bytes), old-opcode-verified, and FUN_8c07a22a has a
# single caller so no other path is affected.
insn16(0x8C07A266, 0x2118, 0x0018, "Task14c: I/O spec-check OK (tst r1,r1 -> sett)")

# §Task 15c: service the CONFIG-TIME JVS enumeration -> node-count [0x8c1ca474]>=1
# -> board struct [0x8c1ca47c] populated -> engine emits sub-0x33 (input poll, M4).
# CORRECTS the Task-15b premise: the node-count probe FUN_8c082bc4 (+ parser
# FUN_8c082c98 + per-node builder FUN_8c082aa4, all from the commit FUN_8c082fd8)
# do NOT use raw-maple absolute literals nor the dead Z80 path FUN_8c080d18/
# FUN_8c0809b2; they transmit via FUN_8c081562 / receive via FUN_8c081626, funneling
# through FUN_8c03000c/FUN_8c02f158 on the shared engine struct *0x8c0e8410 whose
# base [+0x10f4]=0xa05f6c00 is already mirrored (patch #16). Node-count still 0
# (capture-14f.log: 61/61 IOCHK specs=1) because those queued frames aren't serviced
# at the probe's synchronous read time. Fix parallels 14f but at the config layer:
# repoint the 7 pool words that hold FUN_8c081562 (TX) / FUN_8c081626 (RX) -- used
# ONLY by the enum cluster 0x8c082aa4..0x8c082e4c (boot.bin scan: 4 TX + 3 RX words,
# no other holders) -- to shim_cfg_tx / shim_cfg_rx, which latch the JVS command and
# replay the captured Naomi enum blobs (mie_jvsf1/10..14) at +0x15. Reproduces the
# exact Naomi 1-board enumeration -> node-count=1, specs=0. See main.c shim_cfg_rx
# + docs/kb/phase4-conversion.md §Task 15c. Old-byte asserts: all 7 = their fn addr.
for _w in (0x8C082BB0, 0x8C082C8C, 0x8C082D10, 0x8C082E4C):
    ptr(_w, 0x8C081562, sym("shim_cfg_tx"), "Task15c: config JVS TX -> shim_cfg_tx")
for _w in (0x8C082BB8, 0x8C082C94, 0x8C082D18):
    ptr(_w, 0x8C081626, sym("shim_cfg_rx"), "Task15c: config JVS RX -> shim_cfg_rx")

# §Task 16 (M5): free-play sticks. The settings validator FUN_8c080094 reads the
# 93C46 via FUN_8c080f50, which issues the read through the shared async engine
# (FUN_8c03000c/FUN_8c02f158); on DC that reply lands a frame late -- AFTER the
# validator reads its buffers -> both system CRC copies mismatch garbage -> the
# game re-inits the system section to coin-mode defaults (observed EE WR x16,
# coin=0x00) and discards our free-play. Same synchronous-config-read gap as Task
# 15c (which only covered the JVS-enum wrappers FUN_8c081562/1626, not this raw
# read). FIX: hook FUN_8c080f50 -> shim_ee_read, which fills the validator's three
# output buffers DIRECTLY from the baked free-play image, so it sees valid free-
# play (both CRC = 0x50cb) -> result 0, no re-init. See main.c shim_ee_read +
# .superpowers/sdd/task-16-freeplay-report.md. Guard the buffer addresses the shim
# hardcodes: assert FUN_8c080f50's copy-dest + validator pool words are unmoved.
for _pw, _exp in ((0x8C08107C, 0x8C1C954C), (0x8C081080, 0x8C1C9528),
                  (0x8C081084, 0x8C1C953A), (0x8C080184, 0x8C1C9528),
                  (0x8C080188, 0x8C1C953A),
                  # runtime settings struct base. Task18 (M5 fix): main.c pins the
                  # REAL free-play flag @base+0xc=0x8c1c9790 (drives the credit
                  # display + decrement gate FUN_8c081efc@0x8c081f48), superseding
                  # Task16's ineffective coin-byte pin @base+0x10=0x8c1c9794:
                  (0x8C081D14, 0x8C1C9784)):
    _got = struct.unpack("<I", rd(_pw, 4))[0]
    assert _got == _exp, f"Task16 EEPROM buffer pool @{_pw:#x}: {_got:#x} != {_exp:#x}"
hook(0x8C080F50, sym("shim_ee_read"), "Task16: EEPROM read -> shim_ee_read (sync free-play)")

# NOTE (Task 14b history, RESOLVED by Task 14f above): FUN_8c03c2c6 is reached via
# BOTH pool[0x8c02ed6c] (Mode A) and pool[0x8c02ee88] (Mode B, DC takes this); Task
# 14 swapped only the first, and swapping the second to shim_maple_entry regressed
# boot to 0 cart reads because that entry replaces the whole builder and SKIPS the
# pump FUN_8c03c1c2. Task 14f fixes it the right way: mirror the maple base
# (0x8c030fec) so no real DMA fires, and wrapper-hook both slots to shim_maple_steady
# which CALLS the real FUN_8c03c2c6 (pump runs) then services the mirrored
# transaction. shim_maple_entry/shim_maple_boot are retained as documented boot-MIE
# ABI but are no longer hooked. See task-14b-report + task-14e-completion-mechanism.

# ---- emit ---------------------------------------------------------------
out = ["/* GENERATED by scripts/build_patch_table.py - do not edit, do not commit",
       " * (embeds original ROM bytes). */",
       "typedef struct { unsigned int addr, len;",
       "  const unsigned char *old, *neu; const char *what; } patch_t;"]
for i, (addr, old, new, c) in enumerate(patches):
    out.append(f"static const unsigned char p{i}o[] = {{{','.join(map(hex, old))}}};")
    out.append(f"static const unsigned char p{i}n[] = {{{','.join(map(hex, new))}}};")
rows = ",\n".join(
    f'  {{0x{a:08x}u, {len(o)}u, p{i}o, p{i}n, "{c}"}}'
    for i, (a, o, n, c) in enumerate(patches))
out.append(f"static const patch_t cleo_patches[] = {{\n{rows}\n}};")
out.append(f"enum {{ CLEO_NPATCHES = {len(patches)} }};")
(ROOT / "build").mkdir(exist_ok=True)
(ROOT / "build/patch_table.h").write_text("\n".join(out) + "\n")
print(f"OK patch_table.h: {len(patches)} patches "
      f"(2 hook, 16 pool, 9 ptr, 1 insn16; Task 14f async-MIE + 15c config-JVS-enum + 16 free-play EEPROM); "
      f"MIRROR_P2={MIRROR_P2:#010x} MAPLE_MIRROR_P2={MAPLE_MIRROR_P2:#010x} "
      f"BIOS_60000_P2={BIOS_60000_P2:#010x} BIOS_1FFD00_P2={BIOS_1FFD00_P2:#010x}")
