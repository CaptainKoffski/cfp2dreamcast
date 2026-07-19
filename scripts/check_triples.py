#!/usr/bin/env python3
"""Regression oracle: every Phase 2 captured cart-DMA triple must be servable
from the mastered layout.

For each DMA (cart_offset, length, dest) triple assert:
  - cart_offset + length <= CART_SIZE          (the read fits inside the ROM)
  - 0x0c000000 <= dest and dest+length <= 0x0d000000  (dest is main RAM)

CART_SIZE = 0x6800000 = the real ROM size (Cleopatra Fortune Plus.dat, 109 MB).
CSV columns: cart_offset, length, dest, mode  (non-DMA rows are skipped).

Run:  python3 scripts/check_triples.py            -> CHECK triples_servable: PASS
      python3 scripts/check_triples.py --selftest -> proves the assertion can FAIL
"""
import csv
import os
import sys

CART_SIZE = 0x6800000
RAM_LO = 0x0C000000
RAM_HI = 0x0D000000
CSV = os.path.join(os.path.dirname(__file__), "..", "docs", "kb", "cart-streaming-map.csv")


def check(rows):
    """rows: iterable of dicts with cart_offset/length/dest/mode. Returns bad count (prints each)."""
    bad = 0
    for row in rows:
        if row["mode"] != "DMA":
            continue
        off, ln, dest = (int(row[k], 16) for k in ("cart_offset", "length", "dest"))
        if off + ln > CART_SIZE:
            print(f"UNSERVABLE off={off:#x} len={ln:#x} end={off+ln:#x} > CART_SIZE={CART_SIZE:#x}")
            bad += 1
        if not (RAM_LO <= dest and dest + ln <= RAM_HI):
            print(f"BAD DEST dest={dest:#x} len={ln:#x} end={dest+ln:#x} outside [{RAM_LO:#x},{RAM_HI:#x})")
            bad += 1
    return bad


def selftest():
    # A triple past CART_SIZE and a dest outside RAM must both be flagged.
    bad = check([
        {"cart_offset": hex(CART_SIZE), "length": "0x800", "dest": "0x0c000000", "mode": "DMA"},
        {"cart_offset": "0x800000", "length": "0x800", "dest": "0x8c000000", "mode": "DMA"},
        {"cart_offset": "0x800000", "length": "0x800", "dest": "0x0c000000", "mode": "DMA"},  # good
    ])
    assert bad == 2, f"selftest expected 2 failures, got {bad}"
    print("selftest OK: oracle flags out-of-ROM reads and non-RAM dests")


def main():
    if "--selftest" in sys.argv:
        selftest()
        return
    with open(CSV) as f:
        bad = check(csv.DictReader(f))
    print("CHECK triples_servable:", "FAIL" if bad else "PASS")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
