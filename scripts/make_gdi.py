#!/usr/bin/env python3
"""Master the GDI as a self-booting GD-ROM — B5 "max-clone" layout.

Five real-HW boot attempts failed identically (no SEGA license screen) while
Dolphin Blue — the megavolt85 Atomiswave→DC fan port this project mirrors —
boots on the same ODE, card and GDMENUCardManager flow. Each round of
converging on its format (IP.BIN TOC B1, GD-ROM1/1 device info + donor IP B3,
boot binary in the last track B4) fixed a genuine defect yet still failed, so
this build stops converging and CLONES: every byte the BIOS or ODE could
inspect *is* Dolphin Blue's byte.

  track01.iso / track02.raw / track03.iso / disc.gdi
      = the donor's files VERBATIM (track03 = donor IP.BIN + donor filesystem
        + donor game data, exact size; disc.gdi byte-identical).
  track04.iso
      = [our loader, zero-padded to the donor's 3,538,944-byte boot region]
        [our 109 MB cart image]
      The donor FS says 1ST_READ.BIN = 3,538,016 B @ LBA 450000, so the
      bootstrap loads our loader + zero padding — the KOS binary at 0x8c010000
      runs the same either way. The cart follows at LBA 451728 = CART_FAD
      451878 - 150 (shims/include/shim_iface.h — the ONLY code change).

The only deltas from a disc proven to boot on the user's hardware are
track04's bytes/size (pure payload, read only after boot) and the IP.BIN
metadata fields in track03 sector 0 (brand_ip: title/serial/company/date +
correct CRC — cosmetic, parsed for display by BIOS/menus; bootstrap code,
TOC and filesystem stay donor-verbatim). Real-HW verified 2026-07-23.

History (git): the previous version of this script synthesized IP.BIN
(makeip + TOC patch + GD fix) and its own mkisofs filesystem — kept there,
not here, until real HW proves which parts the BIOS actually tolerates.
"""
import argparse, pathlib, re, shutil, subprocess, sys

# Final review: bare asserts are this script's only guards -- die loudly if
# they were stripped (same pattern as build_patch_table.py's selftest).
try:
    assert False
    sys.exit("make_gdi.py: asserts are stripped (PYTHONOPTIMIZE?) -- refusing")
except AssertionError:
    pass

SECTOR = 2048

# IP.BIN identity (track03 sector 0). The donor track03 stays byte-verbatim
# EXCEPT these pure-metadata fields — bootstrap code, TOC and filesystem are
# untouched, so the proven-boot property holds. Serial: unique fake — letters
# in the digit block collide with nothing: real JP serials are T-<digits>M and
# the AW fan ports use megavolt85's sequential T0001M..T00xxM series (user's
# call 2026-07-23: no shared serial with the real Altron DC Cleopatra Fortune
# T-16603M, no auto-cover mismatch; custom art is assigned manually).
# Company: "SEGA LC-T-99" = the license-code string every megavolt85 AW port
# carries at 0x70 (read from the Dolphin Blue + Sushi Bar donors) — the
# fan-port family convention, displays natively in menu tools.
IP_PRODUCT = "T-CFP001M"         # 0x40, 10 bytes
IP_VERSION = "V1.000"            # 0x4a, 6 bytes
IP_DATE    = "20260723"          # 0x50, 16 bytes
IP_COMPANY = "SEGA LC-T-99"      # 0x70, 16 bytes
IP_TITLE   = "CLEOPATRA FORTUNE PLUS"   # 0x80, 128 bytes


def ip_crc16(data: bytes) -> int:
    """Sega IP.BIN device-info CRC (CRC-16/CCITT-FALSE) over product+version
    (0x40..0x4f). Verified against a real disc: ChuChu Rocket US
    "MK-51049  V1.007" -> 743C, matching its header byte-for-byte. (The AW
    fan ports carry a stale value here and still boot -> BIOS ignores it;
    we write the correct one anyway.)"""
    n = 0xFFFF
    for b in data:
        n ^= b << 8
        for _ in range(8):
            n = ((n << 1) ^ 0x1021) & 0xFFFF if n & 0x8000 else (n << 1) & 0xFFFF
    return n


def brand_ip(track03: pathlib.Path):
    # ljust never truncates and bytearray slice-assign silently GROWS, so an
    # over-long hand-edited field would shift every later header byte (final
    # review) -- guard the exact widths.
    assert len(IP_PRODUCT) <= 10 and len(IP_VERSION) <= 6, "serial/version too long"
    assert len(IP_DATE) <= 16 and len(IP_COMPANY) <= 16, "date/company too long"
    assert len(IP_TITLE) <= 128, "title too long"
    with open(track03, "r+b") as f:
        hdr = bytearray(f.read(256))
        hdr[0x40:0x4A] = IP_PRODUCT.ljust(10).encode()
        hdr[0x4A:0x50] = IP_VERSION.ljust(6).encode()
        hdr[0x50:0x60] = IP_DATE.ljust(16).encode()
        hdr[0x70:0x80] = IP_COMPANY.ljust(16).encode()
        hdr[0x80:0x100] = IP_TITLE.ljust(128).encode()
        hdr[0x20:0x24] = b"%04X" % ip_crc16(bytes(hdr[0x40:0x50]))
        f.seek(0)
        f.write(hdr)
DONOR_7Z = pathlib.Path("[GDI] Dolphin Blue.7z")   # repo root, gitignored
DONOR_SUB = "Dolphin Blue"
DONOR_FILES = ("track01.iso", "track02.raw", "track03.iso", "track04.iso", "disc.gdi")
BOOT_REGION = 3538944           # donor track04 size = its FS 1ST_READ region
BOOT_FILE_SIZE = 3538016        # donor FS dir-record size for 1ST_READ.BIN
CART_LBA = 450000 + BOOT_REGION // SECTOR   # 451728; FAD 451878 = CART_FAD
CART_SIZE = 0x6800000           # 109,051,904 B; docs/kb/game.md

# Cross-check against the shim's compiled-in constants (final review: this
# derivation and shim_iface.h's CART_FAD were previously independent -- a donor
# swap or header edit could master the cart at one FAD while the shim streams
# from another, with no error at any build stage).
_iface = pathlib.Path("shims/include/shim_iface.h").read_text()
_fad = int(re.search(r"#define\s+CART_FAD\s+(\d+)", _iface).group(1))
assert CART_LBA + 150 == _fad, \
    f"cart FAD mismatch: mastering at {CART_LBA + 150}, shim streams from {_fad}"
_csz = int(re.search(r"#define\s+CART_SIZE\s+(0x[0-9a-fA-F]+)", _iface).group(1), 16)
assert _csz == CART_SIZE, f"CART_SIZE mismatch: gdi {CART_SIZE:#x} vs shim {_csz:#x}"


def run(cmd): subprocess.run(cmd, check=True)


def donor_tracks(out: pathlib.Path) -> pathlib.Path:
    """Extract the donor image from the AW-port 7z (cached in build/donor/)."""
    dest = out / "donor" / DONOR_SUB
    # all-files sentinel (final review): a single-file sentinel let an
    # interrupted extraction serve partial donor tracks forever
    if not all((dest / f).exists() for f in DONOR_FILES):
        assert DONOR_7Z.exists(), f"donor archive missing: {DONOR_7Z}"
        sz = shutil.which("7zz") or "/opt/homebrew/bin/7zz"
        run([sz, "x", "-y", f"-o{out / 'donor'}"] +
            [str(DONOR_7Z)] + [f"{DONOR_SUB}/{f}" for f in DONOR_FILES])
    return dest


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default="Cleopatra Fortune Plus.dat")
    ap.add_argument("--loader", default="build/1ST_READ.BIN")
    ap.add_argument("--out", default="build")
    a = ap.parse_args()
    out = pathlib.Path(a.out); out.mkdir(exist_ok=True)
    donor = donor_tracks(out)

    rom = pathlib.Path(a.rom).read_bytes()
    assert len(rom) == CART_SIZE, f"cart size {len(rom):#x} != {CART_SIZE:#x}"
    ldr = pathlib.Path(a.loader).read_bytes()
    assert len(ldr) <= BOOT_FILE_SIZE, "loader outgrew the donor boot region"

    for f in ("track01.iso", "track02.raw", "track03.iso", "disc.gdi"):
        shutil.copyfile(donor / f, out / f)
    brand_ip(out / "track03.iso")
    with open(out / "track04.iso", "wb") as t4:
        t4.write(ldr)
        t4.write(b"\0" * (BOOT_REGION - len(ldr)))
        t4.write(rom)

    # stale outputs from the pre-B5 layout confuse SD-card deploys — drop them
    for f in ("cleo.gdi", "track01.bin", "track03.bin", "track04.bin"):
        (out / f).unlink(missing_ok=True)

    print(f"OK disc.gdi (B5 max-clone: tracks 1-3 + gdi = donor verbatim; "
          f"track4 = loader + cart at LBA {CART_LBA} / FAD {CART_LBA + 150})")


if __name__ == "__main__":
    main()
