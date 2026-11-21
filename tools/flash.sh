#!/usr/bin/env bash
# Flash the full RF-Lab image (bootloader + dual-OTA partition table + app).
#
# `pio run -t upload` only writes the app; on a board that still has the old
# 1MB/2MB-factory table this silently boots the wrong slot. This script always
# writes the three pieces together:
#
#   0x0       bootloader
#   0x8000    partition table (otadata + ota_0 + ota_1)
#   0x20000   app -> ota_0
#   0x10000   erase otadata (blank table boots ota_0)
#
# NVS (codes, Wi-Fi credentials, BLE PIN) lives at 0x9000 and is NOT touched.
# This USB flash is required once when migrating from the old single-app
# layout; later updates go through `tools/ota.py` or the Web UI.
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
parts = []
for i in range(0, min(len(data), 0xC00), 32):
    e = data[i:i + 32]
    if len(e) < 32 or e[0:2] != b'\xaa\x50':
        continue
    name = e[12:28].split(b'\0')[0].decode(errors='replace')
    typ = e[2]
    off, size = struct.unpack('<I', e[4:8])[0], struct.unpack('<I', e[8:12])[0]
    parts.append((name, typ, off, size))

by = {name: (typ, off, size) for name, typ, off, size in parts}
if 'otadata' not in by or by['otadata'][1:] != (0x10000, 0x2000):
    raise SystemExit("refusing to flash: otadata must be 0x10000/0x2000")
if 'factory' in by:
    raise SystemExit("refusing to flash: factory partition present; use dual-OTA table")
for name in ('ota_0', 'ota_1'):
    if name not in by:
        raise SystemExit(f"refusing to flash: missing {name}")
    typ, off, size = by[name]
    print(f"app partition: {name} off=0x{off:x} size=0x{size:x}")
    if size < 0x1E0000:
        raise SystemExit(f"refusing to flash: {name} 0x{size:x} < 0x1E0000")
if by['ota_0'][1] != 0x20000:
    raise SystemExit(f"refusing to flash: ota_0 at 0x{by['ota_0'][1]:x}, expected 0x20000")
PY

esptool.py --chip esp32c3 --port "$PORT" --baud 460800 \
    --before default_reset --after hard_reset \
    write_flash \
    0x0 "$BUILD/bootloader.bin" \
    0x8000 "$BUILD/partitions.bin" \
    0x20000 "$BUILD/firmware.bin"

# Blank otadata => bootloader boots ota_0. Do not leave a stale selection.
esptool.py --chip esp32c3 --port "$PORT" --baud 460800 \
    --before default_reset --after hard_reset \
    erase_region 0x10000 0x2000

echo
echo "flashed dual-OTA layout (app -> ota_0); NVS at 0x9000 preserved."
echo "verify with: python3 tools/rf34dl.py --port $PORT info"
echo "next update: python3 tools/ota.py --host <device-ip> --port $PORT"
