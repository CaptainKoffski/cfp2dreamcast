#!/usr/bin/env python3
"""Extract the JVS I/O-board enumeration MIE replies from a working Naomi-mode
capture, for the input shim's boot handshake (Phase 4 Task 14 / M3).

The game enumerates the JVS I/O board at boot: it transmits each JVS command
(F1 set-address, 0x10 read-ID, 0x11 cmd-rev, 0x12 JVS-rev, 0x13 comm-rev,
0x14 features) via MIE sub-0x17, then reads the response via MIE sub-0x15.
capture-attract.log holds the exact replies the real MIE sent (device->host),
which the game accepted. We harvest one blob per JVS command; the shim replays
the matching blob keyed on the transmitted JVS command byte.

The board-ID reply (JVS 0x10) is 0x17 words = 96 bytes but the capture's MIERESP
dump is truncated to 0x40 (64) bytes, so its ID string + checksum tail are cut
off. We reconstruct it: the truncated prefix is spliced with the full board-ID
string (verbatim from Flycast maple_jvs.cpp get_id(), which produced the very
bytes in the capture -- the captured 33-char prefix matches, an anchor check),
then the JVS checksum is recomputed with the same algorithm the fully-captured
replies validate against.

Outputs build/mie_jvsXX.bin (gitignored, ROM-derived). Re-runnable; self-checks.
Usage: python3 scripts/extract_jvs_replies.py [capture-attract.log]
"""
import sys, os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "capture-attract.log")
OUT = os.path.join(REPO, "build")

# Full board-ID string, verbatim from Flycast's default Naomi JVS board
# (tools/flycast-src/core/hw/maple/maple_jvs.cpp:1105/1184 get_id()). This is
# the exact string the emulator streamed into the capture we harvest from.
BOARD_ID = b"SEGA ENTERPRISES,LTD.;I/O BD JVS;837-13551 ;Ver1.00;98/10"

# JVS-command -> a unique substring of its sub-0x15 reply's data= hex field.
# (E0 sync @byte0x1a; body = 01 01 <cmd-specific> <checksum>.)
SIGS = {
    0xf1: "e000040101050b",          # set-address ack (report 5)
    0x10: "e0003d01015345474120",    # read-ID: "SEGA " prefix, JVS len 0x3d
    0x11: "e00004010111",            # command format rev 1.1
    0x12: "e00004010120",            # JVS rev 2.0
    0x13: "e00004010110",            # communication rev 1.0
    0x14: "e00014010101020d",        # slave features (function list)
}

def jvs_checksum(b, e0, csum_idx):
    """JVS checksum = sum of bytes after the E0 sync up to (not incl.) the
    checksum, & 0xff. Matches Flycast calc_crc / shims/src/jvs.c."""
    return sum(b[e0 + 1:csum_idx]) & 0xff

def find_reply(cmd):
    sig = SIGS[cmd]
    with open(LOG) as f:
        for line in f:
            if line.startswith("MIERESP sub=15") and sig in line:
                return bytes.fromhex(line.split("data=")[1].strip())
    raise SystemExit(f"no sub-15 reply matching JVS cmd {cmd:#x} (sig {sig})")

def natural(b):
    """Trim a captured 64-byte reply to its true length: (word_count+1)*4,
    where word_count is maple header byte 3."""
    return bytes(b[: (b[3] + 1) * 4])

os.makedirs(OUT, exist_ok=True)
made = {}
for cmd in SIGS:
    raw = find_reply(cmd)
    e0 = raw.index(0xe0)                    # JVS sync (byte 0x1a in practice)
    jvs_len = raw[e0 + 2]                   # len byte follows E0, node
    csum_idx = (e0 + 2) + jvs_len          # checksum is the last body byte
    if cmd == 0x10:
        # Reconstruct the truncated board-ID reply.
        assert raw[e0 + 5: e0 + 5 + 33] == BOARD_ID[:33], "board-ID prefix mismatch"
        total = (raw[3] + 1) * 4           # 0x17 words -> 96 bytes
        b = bytearray(total)
        b[: e0 + 5] = raw[: e0 + 5]        # header .. status,report (E0 node len 01 01)
        b[e0 + 5: e0 + 5 + len(BOARD_ID)] = BOARD_ID
        b[e0 + 5 + len(BOARD_ID)] = 0x00   # JVS_OUT(0) string terminator
        assert csum_idx == e0 + 5 + len(BOARD_ID) + 1, "board-ID length model wrong"
        b[csum_idx] = jvs_checksum(b, e0, csum_idx)
        blob = bytes(b)
    else:
        blob = natural(raw)
        assert blob[csum_idx] == jvs_checksum(raw, e0, csum_idx), \
            f"checksum mismatch cmd {cmd:#x}"  # validates the checksum model
    path = os.path.join(OUT, f"mie_jvs{cmd:02x}.bin")
    with open(path, "wb") as f:
        f.write(blob)
    made[cmd] = (len(blob), blob[csum_idx])

for cmd, (n, cs) in made.items():
    print(f"mie_jvs{cmd:02x}.bin  {n:3d} bytes  checksum={cs:#04x}")
print("OK: wrote", len(made), "enumeration reply blobs to build/")
