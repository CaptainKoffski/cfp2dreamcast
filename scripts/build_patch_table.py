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
ptr(0x8C027618, 0x8C0315CE, sym("shim_maple_boot"),  "input BOOT slot -> shim_maple_boot")
ptr(0x8C02ED6C, 0x8C03C2C6, sym("shim_maple_entry"), "input STEADY slot -> shim_maple_entry")
# NOTE (Task 14b, REFUTED approach — do NOT re-add without the pump fix):
# FUN_8c03c2c6 (steady JVS builder) is reached via a SECOND fn-pointer pool[0x8c02ee88]
# (Mode B, jsr @0x8c02ed88 in dispatcher FUN_8c02ec08); Task 14 only swapped
# pool[0x8c02ed6c]. In DC mode the game takes Mode B -> real cmd-0x86 maple DMAs to
# MIE@0x20 -> fd0023 garbage (no MIE on DC). BUT swapping pool[0x8c02ee88] ->
# shim_maple_entry REGRESSES boot (0 cart reads vs 147): shim_maple_entry replaces
# the whole builder and SKIPS the callback pump FUN_8c03c1c2 that drives the boot
# state machine. The correct fix intercepts the ASYNC maple engine (mirror the
# maple base 0xa05f6c00 stored by FUN_8c030fc4 + service the engine's completion
# that clears desc+0x18), letting FUN_8c03c2c6 + its pump run. See task-14b-report.

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
      f"(1 hook, 15 pool, 2 ptr); MIRROR_P2={MIRROR_P2:#010x} "
      f"BIOS_60000_P2={BIOS_60000_P2:#010x} BIOS_1FFD00_P2={BIOS_1FFD00_P2:#010x}")
