#!/bin/sh
# Re-runnable Ghidra headless harness for the Phase 3 boot-binary analysis.
# Usage:
#   scripts/ghidra/run.sh import              # import tools/boot.bin + full auto-analysis
#   scripts/ghidra/run.sh script NAME.java    # run scripts/ghidra/NAME.java on the imported program
#
# ROM content: tools/boot.bin and tools/ghidra-proj are gitignored (tools/). Never commit.
set -e
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
GHIDRA_HOME="${GHIDRA_HOME:-$REPO/tools/ghidra_12.1.2_PUBLIC}"
PROJ="$REPO/tools/ghidra-proj"
NAME=cleo3
BOOT="$REPO/tools/boot.bin"
HL="$GHIDRA_HOME/support/analyzeHeadless"

# openjdk from brew (see tooling.md) — Ghidra needs Java 21+ on PATH.
export PATH="/opt/homebrew/opt/openjdk/bin:$PATH"

if [ ! -x "$HL" ]; then echo "ERROR: analyzeHeadless not found: $HL" >&2; exit 1; fi
if [ ! -f "$BOOT" ]; then echo "ERROR: boot slice not found: $BOOT" >&2; exit 1; fi
mkdir -p "$PROJ"

case "${1:-}" in
  import)
    # No -noanalysis => full SH-4 auto-analysis (follows jmp @rN via literal pools).
    "$HL" "$PROJ" "$NAME" -import "$BOOT" -overwrite \
      -processor "SuperH4:LE:32:default" \
      -loader BinaryLoader -loader-baseAddr 0x8c020000
    ;;
  script)
    [ -n "${2:-}" ] || { echo "usage: $0 script NAME.java" >&2; exit 1; }
    "$HL" "$PROJ" "$NAME" -process boot.bin -noanalysis \
      -scriptPath "$REPO/scripts/ghidra" -postScript "$2"
    ;;
  *) echo "usage: $0 import | script NAME.java" >&2; exit 1 ;;
esac
