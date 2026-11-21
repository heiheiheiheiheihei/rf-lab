#!/usr/bin/env python3
"""Guard the dual-OTA partition layout.

PlatformIO can silently use its own 1MB `partitions.bin` even when the IDF
sdkconfig selects a custom table; the 1.08MB image then boot-loops. This
post-build action decodes the generated table and fails the build unless:

  * otadata exists at 0x10000 / 0x2000,
  * exactly two app slots `ota_0` / `ota_1` exist, each >= 0x1E0000,
  * the app image fits in one slot.

The old layout guard checked for a 2MB `factory` partition. OTA needs two
slots instead; keeping the old check would reject every valid OTA build.
"""

Import("env")

import struct
from pathlib import Path

OTADATA = (0x10000, 0x2000)
SLOT_MIN = 0x1E0000
APP_NAMES = ("ota_0", "ota_1")
FLASH_END = 0x400000


def parse(data):
    parts = []
    for i in range(0, min(len(data), 0xC00), 32):
        e = data[i:i + 32]
        if len(e) < 32 or e[0:2] != b"\xaa\x50":
            continue
        name = e[12:28].split(b"\0")[0].decode(errors="replace")
        typ, sub = e[2], e[3]
        offset = struct.unpack("<I", e[4:8])[0]
        size = struct.unpack("<I", e[8:12])[0]
        parts.append((name, typ, sub, offset, size))
    return parts


def check_partitions(source, target, env):
    build = Path(env.subst("$BUILD_DIR"))
    path = build / "partitions.bin"
    if not path.exists():
        return
    data = path.read_bytes()
    if not data:
        raise SystemExit("[partition] ERROR: empty partitions.bin")

    parts = parse(data)
    if not parts:
        raise SystemExit("[partition] ERROR: no partition entries decoded")

    otadata = [p for p in parts if p[0] == "otadata"]
    apps = {p[0]: p for p in parts if p[1] == 0}
    factory = [p for p in parts if p[0] == "factory"]

    for name, typ, sub, off, size in parts:
        print(f"[partition] {name:8s} type={typ} sub={sub} off=0x{off:x} size=0x{size:x}")

    if not otadata:
        raise SystemExit("[partition] ERROR: missing otadata (OTA cannot select a slot)")
    off, size = otadata[0][3], otadata[0][4]
    if (off, size) != OTADATA:
        raise SystemExit(
            f"[partition] ERROR: otadata at 0x{off:x}/0x{size:x}, "
            f"expected 0x{OTADATA[0]:x}/0x{OTADATA[1]:x}")
    if factory:
        raise SystemExit(
            "[partition] ERROR: factory partition present; dual-OTA layout "
            "must not keep a factory slot (otadata chooses ota_0/ota_1)")
    for name in APP_NAMES:
        if name not in apps:
            raise SystemExit(f"[partition] ERROR: missing app slot {name}")
        slot = apps[name]
        if slot[4] < SLOT_MIN:
            raise SystemExit(
                f"[partition] ERROR: {name} slot is 0x{slot[4]:x}, expected >= 0x{SLOT_MIN:x}")
        if slot[3] + slot[4] > FLASH_END:
            raise SystemExit(f"[partition] ERROR: {name} runs past 4MB")
    if set(apps) != set(APP_NAMES):
        raise SystemExit(
            f"[partition] ERROR: app slots {sorted(apps)} != {list(APP_NAMES)}")

    fw = build / "firmware.bin"
    if fw.exists():
        img = fw.stat().st_size
        slot = apps["ota_0"][4]
        print(f"[partition] image {img} B / slot {slot} B ({100 * img / slot:.1f}%)")
        if img > slot:
            raise SystemExit(
                f"[partition] ERROR: image {img} B does not fit in {slot} B slot")


env.AddPostAction("buildprog", check_partitions)
