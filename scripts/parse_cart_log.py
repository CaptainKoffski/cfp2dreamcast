#!/usr/bin/env python3
"""Parse Phase 2 cartlog files into the cart-streaming map + RAM/serial verdicts.

Line formats (from the instrumented Flycast, see the Phase 2 plan):
  CARTDMA src=%08x dest=%08x len=%x
  CARTPIO offset=%08x
  WATERMARK region=%s used=%x size=%x
  JVSREPORT buttons=%04x         (read by hand for the input map; ignored here)
  SERIALPOKE addr=%08x data=%08x
"""
import re
import sys

MAIN_RAM_LO, MAIN_RAM_HI = 0x0c000000, 0x0e000000   # naomi/DC main RAM area (physical)
BOOT_END = 0x100000                                  # 1 MB boot load; runtime streaming is past this

_DMA = re.compile(r"^CARTDMA src=([0-9a-fA-F]+) dest=([0-9a-fA-F]+) len=([0-9a-fA-F]+)")
_PIO = re.compile(r"^CARTPIO offset=([0-9a-fA-F]+)")
_WM = re.compile(r"^WATERMARK region=(\w+) used=([0-9a-fA-F]+) size=([0-9a-fA-F]+)")
_SER = re.compile(r"^SERIALPOKE addr=([0-9a-fA-F]+) data=([0-9a-fA-F]+)")


def parse_text(text):
    dma_seen, dma = set(), []
    pio = set()
    watermarks = {}
    serial = []
    for line in text.splitlines():
        m = _DMA.match(line)
        if m:
            src, dest, length = (int(g, 16) for g in m.groups())
            key = (src, dest, length)
            if key not in dma_seen:
                dma_seen.add(key)
                dma.append({"src": src, "dest": dest, "len": length})
            continue
        m = _PIO.match(line)
        if m:
            pio.add(int(m.group(1), 16))
            continue
        m = _WM.match(line)
        if m:
            region, used, _size = m.group(1), int(m.group(2), 16), int(m.group(3), 16)
            watermarks[region] = max(watermarks.get(region, 0), used)
            continue
        m = _SER.match(line)
        if m:
            serial.append({"addr": int(m.group(1), 16), "data": int(m.group(2), 16)})
    dma.sort(key=lambda d: (d["src"], d["dest"]))
    return {
        "dma": dma,
        "pio": sorted(pio),
        "watermarks": watermarks,
        "serial": serial,
        "checks": _checks(dma),
    }


def _checks(dma):
    checks = []
    dest_ok = all(MAIN_RAM_LO <= d["dest"] < MAIN_RAM_HI for d in dma) if dma else False
    checks.append(("dest_in_ram", dest_ok,
                   "every DMA dest in main RAM 0x0c000000-0x0dffffff"))
    len_ok = all(d["len"] % 0x20 == 0 for d in dma) if dma else False
    checks.append(("len_aligned_32", len_ok, "every DMA len is a multiple of 0x20"))
    beyond = any(d["src"] >= BOOT_END for d in dma)
    checks.append(("beyond_boot_read", beyond,
                   "at least one cart read with src >= 0x100000 (runtime streaming)"))
    return checks


def parse_files(paths):
    text = "\n".join(open(p, encoding="utf-8", errors="replace").read() for p in paths)
    return parse_text(text)


def write_csv(result, path):
    with open(path, "w", encoding="utf-8") as f:
        f.write("cart_offset,length,dest,mode\n")
        for d in result["dma"]:
            f.write(f"0x{d['src']:08x},0x{d['len']:x},0x{d['dest']:08x},DMA\n")
        for off in result["pio"]:
            f.write(f"0x{off:08x},0x0,0x00000000,PIO\n")


def write_summary(result):
    lines = [f"DMA requests (unique): {len(result['dma'])}",
             f"PIO seeks (unique): {len(result['pio'])}"]
    if result["dma"]:
        lines.append(f"cart offset range: 0x{result['dma'][0]['src']:08x}"
                     f"..0x{max(d['src'] + d['len'] for d in result['dma']):08x}")
        main_hi = max((d["dest"] + d["len"] for d in result["dma"]
                       if MAIN_RAM_LO <= d["dest"] < MAIN_RAM_HI), default=0)
        lines.append(f"main-RAM DMA high-water (dest+len): 0x{main_hi:08x} "
                     f"({main_hi / 1048576:.1f} MB) vs DC 16 MB")
    for region in ("main", "vram", "aram"):
        if region in result["watermarks"]:
            lines.append(f"WATERMARK {region}: 0x{result['watermarks'][region]:08x} "
                         f"({result['watermarks'][region] / 1048576:.1f} MB)")
    lines.append(f"serial/network pokes: {len(result['serial'])}")
    for name, ok, detail in result["checks"]:
        lines.append(f"CHECK {name}: {'PASS' if ok else 'FAIL'} — {detail}")
    return "\n".join(lines)


def main(argv):
    paths, csv_out = [], None
    i = 0
    while i < len(argv):
        if argv[i] == "--csv":
            csv_out = argv[i + 1]; i += 2
        else:
            paths.append(argv[i]); i += 1
    if not paths:
        print("usage: parse_cart_log.py LOG [LOG ...] --csv OUT.csv", file=sys.stderr)
        return 2
    result = parse_files(paths)
    if csv_out:
        write_csv(result, csv_out)
    print(write_summary(result))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
