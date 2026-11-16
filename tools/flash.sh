#!/usr/bin/env bash
# Flash the full RF-Lab image (bootloader + 2MB partition table + app).
#
# `pio run -t upload` only writes the app; on a board that still has the 1MB
# default partition table this silently leaves a 1MB slot and the 0.6.0 image
# boot-loops. This script always writes the three pieces together.
#
# NVS (codes, Wi-Fi credentials, BLE PIN) lives at 0x9000 and is NOT touched.
set -euo pipefail

PORT="${1:-/dev/ttyACM0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/firmware/.pio/build/esp32c3"
export PATH="$HOME/.platformio/penv/bin:$PATH"

cd "$ROOT/firmware"
pio run -e esp32c3

python3 - "$BUILD/partitions.bin" <<'PY'
import struct, sys
from pathlib import Path
p = Path(sys.argv[1])
data = p.read_bytes()
for i in range(0, min(len(data), 0xC00), 32):
    e = data[i:i+32]
    if len(e) < 32 or e[0:2] != b'\xaa\x50':
        continue
    name = e[12:28].split(b'\0')[0].decode(errors='replace')
    typ = e[2]
    off, size = struct.unpack('<I', e[4:8])[0], struct.unpack('<I', e[8:12])[0]
    if typ == 0:
        print(f"app partition: {name} off=0x{off:x} size=0x{size:x}")
        if size != 0x200000:
            raise SystemExit(f"refusing to flash: app slot 0x{size:x} != 0x200000")
PY

esptool.py --chip esp32c3 --port "$PORT" --baud 460800 \
    --before default_reset --after hard_reset \
    write_flash \
    0x0 "$BUILD/bootloader.bin" \
    0x8000 "$BUILD/partitions.bin" \
    0x10000 "$BUILD/firmware.bin"

echo
echo "flashed full layout; NVS at 0x9000 preserved."
echo "verify with: python3 tools/rf34dl.py --port $PORT info"
