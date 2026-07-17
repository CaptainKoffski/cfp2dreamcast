#!/usr/bin/env python3
"""Parse a decrypted Naomi cart image header; print markdown to stdout.

Field offsets cross-checked against DragonMinded's netboot naomi/rom.py
(tools/netboot), the battle-tested reference for this format.
"""
import struct
import sys


def cstr(b):
    return b.decode("ascii", "replace").rstrip("\x00 ")


def print_entries(hdr, off, label):
    print(f"- **{label} load entries:**")
    for i in range(8):
        rom, ram, n = struct.unpack_from("<III", hdr, off + 12 * i)
        # ponytail: also stop on an all-zero slot; netboot stops only on
        # 0xFFFFFFFF (rom.py:497). Safe here — a real entry has n != 0.
        if rom == 0xFFFFFFFF or (rom == 0 and n == 0):
            break
        print(f"  - ROM 0x{rom:08x} -> RAM 0x{ram:08x}, 0x{n:x} bytes")


def main(path):
    with open(path, "rb") as f:
        hdr = f.read(0x500)
        f.seek(0, 2)
        size = f.tell()
    magic = cstr(hdr[0:0x10])
    assert magic == "NAOMI", f"not a Naomi image: magic={magic!r}"
    print(f"- **File:** `{path}` ({size:,} bytes)")
    print(f"- **Magic:** `{magic}`")
    print(f"- **Publisher:** {cstr(hdr[0x10:0x30])}")
    regions = ["Japan", "USA", "Export", "Korea", "Australia",
               "Reserved1", "Reserved2", "Reserved3"]
    for i, name in enumerate(regions):
        print(f"- **Title ({name}):** {cstr(hdr[0x30 + 0x20 * i:0x50 + 0x20 * i])}")
    print_entries(hdr, 0x360, "Main")
    print_entries(hdr, 0x3C0, "Test")
    main_ep, test_ep = struct.unpack_from("<II", hdr, 0x420)
    print(f"- **Entrypoint (main):** 0x{main_ep:08x}")
    print(f"- **Entrypoint (test):** 0x{test_ep:08x}")


if __name__ == "__main__":
    main(sys.argv[1])
