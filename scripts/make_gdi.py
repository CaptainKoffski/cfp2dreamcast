#!/usr/bin/env python3
"""Master the Phase 4 GDI: 3 tracks + .gdi. Layout (docs/superpowers/specs/
2026-07-18-phase4-conversion-design.md §1): track3 = [ISO region: IP.BIN +
1ST_READ.BIN, fixed ISO_SECTORS] + [cart image at CART_LBA] . Output is
ROM-derived -> build/ is gitignored."""
import argparse, pathlib, subprocess, sys

SECTOR = 2048
TRACK3_LBA = 45000
ISO_SECTORS = 2048              # 4 MB reserved for IP.BIN + loader FS
CART_LBA = TRACK3_LBA + ISO_SECTORS      # 47048; FAD = 47198 (shim_iface.h)
CART_SIZE = 0x6800000           # 109,051,904 bytes; docs/kb/game.md — assert, don't trust
                                 # (brief said 0x6D00000/114294784 - a MiB/MB units slip;
                                 # 109 decimal MB != 109 binary MiB. Actual file size wins.)

def run(cmd): subprocess.run(cmd, check=True)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default="Cleopatra Fortune Plus.dat")
    ap.add_argument("--loader", default="build/1ST_READ.BIN")
    ap.add_argument("--ip", default="build/IP.BIN")
    ap.add_argument("--out", default="build")
    a = ap.parse_args()
    out = pathlib.Path(a.out); out.mkdir(exist_ok=True)
    rom = pathlib.Path(a.rom).read_bytes()
    assert len(rom) == CART_SIZE, f"cart size {len(rom):#x} != {CART_SIZE:#x}"

    # ISO region: mkisofs with LBA offset so in-FS extents match disc LBAs
    fsdir = out / "fs"; fsdir.mkdir(exist_ok=True)
    (fsdir / "1ST_READ.BIN").write_bytes(pathlib.Path(a.loader).read_bytes())
    iso = out / "iso_part.bin"
    run(["mkisofs", "-C", f"0,{TRACK3_LBA}", "-V", "CLEOPATRA", "-G", a.ip,
         "-l", "-o", str(iso), str(fsdir)])
    iso_b = iso.read_bytes()
    assert len(iso_b) <= ISO_SECTORS * SECTOR, "ISO region overflow: raise ISO_SECTORS (and CART_LBA/CART_FAD in shim_iface.h)"

    t3 = out / "track03.bin"
    with open(t3, "wb") as f:
        f.write(iso_b); f.write(b"\0" * (ISO_SECTORS * SECTOR - len(iso_b)))
        f.write(rom)
        pad = (-len(rom)) % SECTOR
        f.write(b"\0" * pad)
    (out / "track01.bin").write_bytes(b"\0" * (300 * SECTOR))
    (out / "track02.raw").write_bytes(b"\0" * (300 * 2352))
    (out / "cleo.gdi").write_text(
        "3\n"
        "1 0 4 2048 track01.bin 0\n"
        "2 450 0 2352 track02.raw 0\n"
        f"3 {TRACK3_LBA} 4 2048 track03.bin 0\n")
    print(f"OK cleo.gdi  cart at LBA {CART_LBA} (FAD {CART_LBA+150}), "
          f"track03 {t3.stat().st_size//SECTOR} sectors")

if __name__ == "__main__":
    main()
