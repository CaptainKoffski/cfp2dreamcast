#!/usr/bin/env python3
"""Quick PVR texture viewer: decode to BMP and open it (macOS `open`).

For eyeballing whether disc art settled down properly. Accepts a bare
.pvr (GBIX and/or PVRT header) or any binary containing one (e.g. a
patched track03.bin) — it scans for the first PVRT signature.

Stdlib-only on purpose, same as bmp2rgb565.py. Decodes the formats this
project actually uses: ARGB1555/RGB565/ARGB4444 pixels, square-twiddled
(0x01) and raw-rectangle (0x09) layouts. Anything else errors loudly.

Usage: pvrview.py FILE [-o OUT.bmp] [--no-open]
       pvrview.py --selftest
"""
import struct, subprocess, sys

def find_pvrt(b):
    i = b.find(b"PVRT")
    assert i >= 0, "no PVRT signature found"
    return i

def detwiddle_index(x, y):
    # Morton/Z-order, y in the low bit position.
    idx = 0
    for i in range(10):  # up to 1024x1024
        idx |= ((y >> i) & 1) << (2 * i) | ((x >> i) & 1) << (2 * i + 1)
    return idx

def px_to_rgb(v, fmt):
    if fmt == 0:   # ARGB1555 (alpha ignored)
        return ((v >> 10) & 31) << 3, ((v >> 5) & 31) << 3, (v & 31) << 3
    if fmt == 1:   # RGB565
        return ((v >> 11) & 31) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3
    if fmt == 2:   # ARGB4444 (alpha ignored)
        return ((v >> 8) & 15) << 4, ((v >> 4) & 15) << 4, (v & 15) << 4
    raise SystemExit(f"unsupported pixel format 0x{fmt:02x}")

def decode(b):
    """-> (width, height, rows of (r,g,b))"""
    o = find_pvrt(b)
    fmt, typ, w, h = struct.unpack_from("<BBxxHH", b, o + 8)
    data = b[o + 16:]
    assert len(data) >= w * h * 2, "file truncated"
    px = struct.unpack_from(f"<{w * h}H", data)
    if typ == 0x09:            # raw rectangle, row-major
        get = lambda x, y: px[y * w + x]
    elif typ == 0x01:          # square twiddled
        assert w == h, "twiddled but not square"
        get = lambda x, y: px[detwiddle_index(x, y)]
    else:
        raise SystemExit(f"unsupported image type 0x{typ:02x}")
    return w, h, [[px_to_rgb(get(x, y), fmt) for x in range(w)] for y in range(h)]

def write_bmp(path, w, h, rows):
    pad = (-w * 3) % 4
    body = b"".join(
        b"".join(bytes((bl, g, r)) for r, g, bl in rows[y]) + b"\0" * pad
        for y in range(h - 1, -1, -1))  # bottom-up
    hdr = struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54)
    hdr += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(body), 2835, 2835, 0, 0)
    open(path, "wb").write(hdr + body)

def selftest():
    # 2x2 twiddled RGB565: storage order is (0,0),(0,1),(1,0),(1,1)
    pix = [0xF800, 0x07E0, 0x001F, 0xFFFF]  # red, green, blue, white
    b = b"PVRT" + struct.pack("<IBBxxHH", 8 + 8, 1, 1, 2, 2) + struct.pack("<4H", *pix)
    w, h, rows = decode(b)
    assert rows[0][0] == (248, 0, 0) and rows[1][0] == (0, 252, 0)
    assert rows[0][1] == (0, 0, 248) and rows[1][1] == (248, 252, 248)
    print("selftest ok")

def main():
    if "--selftest" in sys.argv:
        return selftest()
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    src = args[0]
    out = sys.argv[sys.argv.index("-o") + 1] if "-o" in sys.argv else src + ".bmp"
    b = open(src, "rb").read()
    w, h, rows = decode(b)
    write_bmp(out, w, h, rows)
    print(f"{src}: {w}x{h} @ offset {find_pvrt(b)} -> {out}")
    if "--no-open" not in sys.argv:
        subprocess.run(["open", out])

if __name__ == "__main__":
    main()
