#!/bin/sh
# Capture wrapper for Phase 2/3 instrumented Flycast sessions.
# Usage: scripts/capture.sh <attract|play|input|pc> [seconds]
#
# attract  -- background launch, auto-kill after [seconds] (default 90).
#             Covers title -> demo -> how-to-play -> high scores loop.
# play     -- foreground launch; user plays, closes window when done.
# input    -- foreground launch; user exercises each control, closes window.
# pc       -- foreground launch; forces interpreter (Dynarec.Enabled=no) for
#             instruction-exact guest PC/SP in CARTDMAPC/MAPLEPC/BIOSEXEC lines.
#
# Writes: capture-<pass>.log in the repo root.
# vsync is forced off via transient CLI flag (-config config:rend.vsync=no)
# so the emu thread does not deadlock when the window is unfocused.
# This is non-mutating: emu.cfg is never touched.

set -e

PASS="${1:-}"
SECS="${2:-90}"

if [ -z "$PASS" ] || { [ "$PASS" != "attract" ] && [ "$PASS" != "play" ] && [ "$PASS" != "input" ] && [ "$PASS" != "pc" ]; }; then
    echo "usage: $0 <attract|play|input|pc> [seconds]" >&2
    exit 1
fi

# Repo root = directory containing this script's parent
REPO="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$REPO/tools/flycast-src/build/Flycast.app/Contents/MacOS/Flycast"
ROM="$REPO/Cleopatra Fortune Plus.dat"
LOG="$REPO/capture-${PASS}.log"

if [ ! -x "$BIN" ]; then
    echo "ERROR: instrumented binary not found: $BIN" >&2
    exit 1
fi
if [ ! -f "$ROM" ]; then
    echo "ERROR: ROM not found: $ROM" >&2
    exit 1
fi

echo "Pass: $PASS  Log: $LOG"

# ponytail: a killed/crashed run makes macOS show a modal "reopen windows?" on the
# NEXT launch, which blocks boot forever (process alive, guest never runs — 0 cartlog
# lines). This was the mysterious "post-sleep launch fails" blocker. Suppress it so
# captures are launch-safe regardless of how the prior run ended.
defaults write com.flyinghead.Flycast ApplePersistenceIgnoreState -bool YES 2>/dev/null || true
defaults write com.flyinghead.Flycast NSQuitAlwaysKeepsWindows -bool false 2>/dev/null || true

# ponytail: transient vsync=no prevents emu-thread deadlock on unfocused window (Task 1 finding).
# -config is a built-in Flycast flag; value is not written to emu.cfg.
# ponytail: Phase 3 pc-pass forces the interpreter core for instruction-exact guest PC/SP.
EXTRA=""
if [ "$PASS" = "pc" ]; then EXTRA="-config config:Dynarec.Enabled=no"; fi
FLYCAST_CARTLOG="$LOG" "$BIN" -config config:rend.vsync=no $EXTRA "$ROM" &
FLYPID=$!

if [ "$PASS" = "attract" ]; then
    echo "Auto-kill in ${SECS}s (PID $FLYPID) ..."
    sleep "$SECS"
    kill "$FLYPID" 2>/dev/null
    wait "$FLYPID" 2>/dev/null || true
    echo "Attract capture done. Log: $LOG"
else
    # play / input: foreground wait — user closes the window
    echo "Foreground session (PID $FLYPID). Play, then close the window."
    wait "$FLYPID"
    echo "${PASS} capture done. Log: $LOG"
fi
