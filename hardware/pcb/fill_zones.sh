#!/usr/bin/env bash
# Fill zones headlessly.
#
# KiCad 9.0.8's Python ZONE_FILLER segfaults without a PCB_EDIT_FRAME, so we
# drive the real pcbnew GUI under Xvfb: dismiss the first-run library dialog,
# press "B" (fill all zones), save, close.  Run after gen_pcb.py.
set -euo pipefail
board="$(readlink -f "${1:?usage: fill_zones.sh board.kicad_pcb}")"
log="${TMPDIR:-/tmp}/pcbnew-fill.log"

timeout 120 xvfb-run -a bash -c '
    pcbnew "$1" >"$2" 2>&1 &
    pid=$!
    sleep 12
    # first-run "configure global footprint table" dialog
    xdotool key --clearmodifiers Return || true
    sleep 6
    xdotool key --clearmodifiers b
    sleep 5
    xdotool key --clearmodifiers ctrl+s
    sleep 5
    xdotool key --clearmodifiers alt+F4 || true
    sleep 2
    kill $pid 2>/dev/null || true
' _ "$board" "$log"

python3 - "$board" <<'PY'
import sys, pcbnew
b = pcbnew.LoadBoard(sys.argv[1])
zs = list(b.Zones())
if not zs:
    raise SystemExit("no zones found")
for z in zs:
    print(f"zone {z.GetLayerName()}: filled={z.IsFilled()} outlines={z.GetFilledPolysList(z.GetLayer()).OutlineCount()}")
    if not z.IsFilled():
        raise SystemExit("zone not filled")
PY
