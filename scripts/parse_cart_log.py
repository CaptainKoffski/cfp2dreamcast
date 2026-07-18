#!/usr/bin/env python3
"""Parse Phase 2/3 cartlog files into the cart-streaming map + RAM/serial verdicts.

Line formats (from the instrumented Flycast, see the Phase 2/3 plan):
  CARTDMA src=%08x dest=%08x len=%x
  CARTDMAPC pc=%08x sp=%08x
  CARTPIO offset=%08x
  MAPLEPC cmd=86 sub=%02x pc=%08x
  BIOSEXEC pc=%08x
  WATERMARK region=%s used=%x size=%x
  JVSREPORT buttons=%04x         (read by hand for the input map; ignored here)
  SERIALPOKE addr=%08x data=%08x
"""
import re
import sys

MAIN_RAM_LO, MAIN_RAM_HI = 0x0c000000, 0x0e000000   # naomi/DC main RAM area (physical)
BOOT_END = 0x100000                                  # 1 MB boot load; runtime streaming is past this

_DMA  = re.compile(r"^CARTDMA src=([0-9a-fA-F]+) dest=([0-9a-fA-F]+) len=([0-9a-fA-F]+)")
_DMAPC = re.compile(r"^CARTDMAPC pc=([0-9a-fA-F]+) sp=([0-9a-fA-F]+)")
_MAPC  = re.compile(r"^MAPLEPC cmd=86 sub=([0-9a-fA-F]+) pc=([0-9a-fA-F]+)")
_BIOS  = re.compile(r"^BIOSEXEC pc=([0-9a-fA-F]+)")
_PIO  = re.compile(r"^CARTPIO offset=([0-9a-fA-F]+)")
_WM   = re.compile(r"^WATERMARK region=(\w+) used=([0-9a-fA-F]+) size=([0-9a-fA-F]+)")
_SER  = re.compile(r"^SERIALPOKE addr=([0-9a-fA-F]+) data=([0-9a-fA-F]+)")


def parse_text(text, cart_fn=None, input_fn=None, eeprom_fn=None):
    dma_seen, dma = set(), []
    pio = set()
    watermarks = {}
    serial = []
    cartdma_pc, maple_pc, bios_exec = [], [], []
    for line in text.splitlines():
        m = _DMA.match(line)
        if m:
            src, dest, length = (int(g, 16) for g in m.groups())
            key = (src, dest, length)
            if key not in dma_seen:
                dma_seen.add(key)
                dma.append({"src": src, "dest": dest, "len": length})
            continue
        m = _DMAPC.match(line)
        if m:
            cartdma_pc.append({"pc": int(m.group(1), 16), "sp": int(m.group(2), 16)})
            continue
        m = _MAPC.match(line)
        if m:
            maple_pc.append({"sub": int(m.group(1), 16), "pc": int(m.group(2), 16)})
            continue
        m = _BIOS.match(line)
        if m:
            bios_exec.append(int(m.group(1), 16))
            continue
        m = _PIO.match(line)
        if m:
            pio.add(int(m.group(1), 16))
            continue
        m = _WM.match(line)
        if m:
            region, used, _sz = m.group(1), int(m.group(2), 16), int(m.group(3), 16)
            watermarks[region] = max(watermarks.get(region, 0), used)
            continue
        m = _SER.match(line)
        if m:
            serial.append({"addr": int(m.group(1), 16), "data": int(m.group(2), 16)})
    dma.sort(key=lambda d: (d["src"], d["dest"]))
    return {
        "dma": dma, "pio": sorted(pio), "watermarks": watermarks, "serial": serial,
        "cartdma_pc": cartdma_pc, "maple_pc": maple_pc, "bios_exec": bios_exec,
        "checks": _checks(dma) + _pc_checks(cartdma_pc, maple_pc, bios_exec,
                                             cart_fn, input_fn, eeprom_fn),
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


def _in(ranges, pc):
    # ranges is a single (lo, hi) tuple or a list of them (a fn may span
    # more than one site — e.g. the two Maple poll routines). pc is "in" if
    # it lands in ANY range. ponytail: a set of tight ranges, never one wide
    # span across unrelated code — that would make the check meaningless.
    if ranges is None:
        return False
    if ranges and isinstance(ranges[0], int):
        ranges = [ranges]
    # Compare on the 29-bit physical address: the SH-4 runs the same code via
    # P0/P1/P2 region mirrors, so a PC logged as 0x0c03161e and a Ghidra range
    # at 0x8c03161e are the same instruction. Mask both sides.
    pc &= 0x1fffffff
    return any((lo & 0x1fffffff) <= pc <= (hi & 0x1fffffff) for lo, hi in ranges)


def _pc_checks(cartdma_pc, maple_pc, bios_exec, cart_fn, input_fn, eeprom_fn):
    checks = []
    checks.append(("no_bios_exec", len(bios_exec) == 0,
                   "zero BIOSEXEC lines (no BIOS call after entry, §8-3)"))
    if cart_fn and cartdma_pc:
        checks.append(("dma_pc_in_cart_fn", all(_in(cart_fn, d["pc"]) for d in cartdma_pc),
                       "every CARTDMAPC pc inside the static cart-read fn"))
    input_pcs = [m["pc"] for m in maple_pc if m["sub"] == 0x15]
    if input_fn and input_pcs:
        checks.append(("input_pc_in_input_fn", all(_in(input_fn, p) for p in input_pcs),
                       "every input-poll (sub 0x15) pc inside the static input fn"))
    eeprom_pcs = [m["pc"] for m in maple_pc if m["sub"] in (0x01, 0x03, 0x0b)]
    seen = len(eeprom_pcs) > 0
    if eeprom_fn:
        seen = seen and all(_in(eeprom_fn, p) for p in eeprom_pcs)
    checks.append(("eeprom_seen", seen,
                   "≥1 EEPROM op (sub 0x01/0x03/0x0B) captured"
                   + (" and inside the static eeprom fn" if eeprom_fn else "")))
    # sp_consistent: all logged SPs within 1 MB of each other (a stable stack region)
    sps = [d["sp"] for d in cartdma_pc]
    sp_ok = (max(sps) - min(sps) <= 0x100000) if sps else True
    checks.append(("sp_consistent", sp_ok,
                   "logged SPs cluster within 1 MB (a single stable stack region)"))
    return checks


def parse_files(paths, **ranges):
    text = "\n".join(open(p, encoding="utf-8", errors="replace").read() for p in paths)
    return parse_text(text, **ranges)


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
        # report bytes used above the RAM base, not the absolute address / 1 MB
        main_used = main_hi - MAIN_RAM_LO if main_hi else 0
        lines.append(f"main-RAM DMA high-water (dest+len): 0x{main_hi:08x} "
                     f"({main_used / 1048576:.1f} MB above base) vs DC 16 MB")
    for region in ("main", "vram", "aram"):
        if region in result["watermarks"]:
            lines.append(f"WATERMARK {region}: 0x{result['watermarks'][region]:08x} "
                         f"({result['watermarks'][region] / 1048576:.1f} MB)")
    lines.append(f"serial/network pokes: {len(result['serial'])}")
    if result.get("cartdma_pc"):
        pcs = sorted(set(d["pc"] for d in result["cartdma_pc"]))
        lines.append(f"cart-DMA call sites (unique pc): {len(pcs)}  "
                     f"e.g. 0x{pcs[0]:08x}")
        sps = sorted(set(d["sp"] for d in result["cartdma_pc"]))
        lines.append(f"stack pointer range: 0x{sps[0]:08x}..0x{sps[-1]:08x}")
    if result.get("maple_pc"):
        subs = sorted(set(m["sub"] for m in result["maple_pc"]))
        lines.append("MIE 0x86 subcommands seen: " + ", ".join(f"0x{s:02x}" for s in subs))
    lines.append(f"BIOSEXEC lines: {len(result.get('bios_exec', []))}")
    for name, ok, detail in result["checks"]:
        lines.append(f"CHECK {name}: {'PASS' if ok else 'FAIL'} — {detail}")
    return "\n".join(lines)


def _range(s):
    # one "LO-HI" range, or a comma-separated set "LO-HI,LO-HI" (a fn that
    # runs from more than one site). Always returns a list of (lo, hi) tuples.
    out = []
    for part in s.split(","):
        lo, hi = part.split("-")
        out.append((int(lo, 16), int(hi, 16)))
    return out


def main(argv):
    paths, csv_out = [], None
    ranges = {}
    i = 0
    while i < len(argv):
        if argv[i] == "--csv":
            csv_out = argv[i + 1]; i += 2
        elif argv[i] == "--cart-fn":
            ranges["cart_fn"] = _range(argv[i + 1]); i += 2
        elif argv[i] == "--input-fn":
            ranges["input_fn"] = _range(argv[i + 1]); i += 2
        elif argv[i] == "--eeprom-fn":
            ranges["eeprom_fn"] = _range(argv[i + 1]); i += 2
        else:
            paths.append(argv[i]); i += 1
    if not paths:
        print("usage: parse_cart_log.py LOG [LOG ...] [--cart-fn LO-HI[,LO-HI]] "
              "[--input-fn LO-HI[,LO-HI]] [--eeprom-fn LO-HI[,LO-HI]] [--csv OUT.csv]",
              file=sys.stderr)
        return 2
    result = parse_files(paths, **ranges)
    if csv_out:
        write_csv(result, csv_out)
    print(write_summary(result))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
