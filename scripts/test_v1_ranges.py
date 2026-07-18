#!/usr/bin/env python3
"""V1 self-check: re-derive init's RAM-write bounds from tools/boot.bin and
assert the values docs/kb/phase4-conversion.md §V1 claims. Fails loudly if any
pool word or pointer-table entry has moved. Skips if boot.bin (gitignored) is
absent.

Run: python3 scripts/test_v1_ranges.py
"""
import os
import sys

BASE = 0x8c020000
BOOT = os.path.join(os.path.dirname(__file__), "..", "tools", "boot.bin")

if not os.path.exists(BOOT):
    print("SKIP: tools/boot.bin absent (gitignored ROM slice)")
    sys.exit(0)

data = open(BOOT, "rb").read()


def rd(addr):
    """32-bit LE word at a VA in the boot image, or None if out of range."""
    off = addr - BASE
    if 0 <= off <= len(data) - 4:
        return int.from_bytes(data[off:off + 4], "little")
    return None


def deref(pool):
    """word@pool, then *word (one indirection, both must be in-image)."""
    ptr = rd(pool)
    return rd(ptr)


SYSCALL_LO, SYSCALL_HI = 0x8c000000, 0x8c007fff      # BIOS syscall vector+work area
SHIM_LO, SHIM_HI = 0x8cfc0000, 0x8cffffff            # planned shim home

# (name, start, end, is_zeroing) -- see phase4-conversion.md §V1 table
loops = [
    ("memset 0x8c00fc00", rd(0x8c021060) & 0x1fffffff | 0x8c000000,
     (rd(0x8c021060) & 0x1fffffff | 0x8c000000) + (rd(0x8c021054) & 0xffff), True),
    ("G code-stub copy", rd(0x8c02133c), rd(0x8c021340), False),
    ("A stack paint",   rd(0x8c0212fc), deref(0x8c0212f4), False),
    ("B marker",        deref(0x8c021304), deref(0x8c021300), False),
    ("E bss byte-zero", deref(0x8c021324), deref(0x8c021320), True),
    ("C bss word-zero", deref(0x8c021310), deref(0x8c02130c), True),
]

# 1. Exact bounds claimed in the KB.
expect = {
    "memset 0x8c00fc00": (0x8c00fc00, 0x8c010000),
    "G code-stub copy":  (0x8c000000, 0x8c000020),
    "A stack paint":     (0x8c00c000, 0x8c00f000),
    "B marker":          (0x8c1f3480, 0x8c1f34a0),
    "E bss byte-zero":   (0x8c0daf80, 0x8c0fd8e0),
    "C bss word-zero":   (0x8c0fd8e0, 0x8c1f3480),
}
for name, start, end, _ in loops:
    es, ee = expect[name]
    assert (start, end) == (es, ee), \
        f"{name}: got {start:#x}-{end:#x}, expected {es:#x}-{ee:#x}"

# 2. E and C are contiguous (single BSS clear).
assert expect["E bss byte-zero"][1] == expect["C bss word-zero"][0]

# 3. Block A fills with "SEGA".
assert rd(0x8c0212f8) == 0x41474553, "block A fill pattern != 'SEGA'"

# 4. GATE: no ZEROING loop overlaps the syscall area or the shim home.
def overlaps(a0, a1, b0, b1):
    return a0 <= b1 and b0 < a1  # [a0,a1) vs [b0,b1]

for name, start, end, is_zero in loops:
    if not is_zero:
        continue
    assert not overlaps(start, end, SYSCALL_LO, SYSCALL_HI), \
        f"GATE TRIPPED: {name} zeroes into syscall area!"
    assert not overlaps(start, end, SHIM_LO, SHIM_HI), \
        f"GATE TRIPPED: {name} zeroes into shim home!"

# 5. Non-gate watch item: block G does write inside the syscall window but stays
#    below the GD-ROM vector at 0x8c0000bc.
gstart, gend = expect["G code-stub copy"]
assert gstart >= SYSCALL_LO and gend <= 0x8c0000bc, "block G assumption changed"

print("OK: all V1 bounds verified; syscall area survives zeroing; shim home safe")
