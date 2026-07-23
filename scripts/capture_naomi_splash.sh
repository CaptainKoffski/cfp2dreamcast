#!/bin/sh
REPO=/Users/captainkoffski/AntigravityProjects/cleopatra
BIN="$REPO/tools/flycast-src/build/Flycast.app/Contents/MacOS/Flycast"
PNG="$CLAUDE_JOB_DIR/tmp/naomi_boot.png"
LOG="$CLAUDE_JOB_DIR/tmp/naomi_boot.log"
rm -f naomi_boot_s*.png
for try in 1 2 3 4; do
  pkill -9 -f "flycast-src.*Flycast" 2>/dev/null; sleep 10
  FLYCAST_SHOT="$PNG" FLYCAST_SHOT_EVERY=15 \
    "$BIN" "$REPO/Cleopatra Fortune Plus.dat" -config config:rend.vsync=no > "$LOG" 2>&1 &
  FPID=$!
  sleep 8
  grep -q "Verify Failed" "$LOG" && { echo "try$try flake"; continue; }
  break
done
i=0
while [ $i -lt 45 ]; do
  i=$((i+1)); sleep 1
  cp "$PNG" "naomi_boot_s$i.png" 2>/dev/null
done
pkill -9 -f "flycast-src.*Flycast" 2>/dev/null
md5 naomi_boot_s*.png | awk '{print $NF, $4}' | sort -k2 | uniq -f1 | sort -V
