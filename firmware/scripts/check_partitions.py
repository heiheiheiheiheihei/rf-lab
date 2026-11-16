#!/usr/bin/env python3
"""Guard against the 1MB-default partition table trap.

PlatformIO can silently use its own 1MB `partitions.bin` even when the IDF
sdkconfig selects a custom table; the 0.6.0 image (~1.06MB) then boot-loops.
This post-build action decodes the generated table and fails the build if the
factory app slot is not the expected 2MB.
"""

Import("env")

import struct
from pathlib import Path

EXPECTED = 0x200000


def check_partitions(source, target, env):
    path = Path(env.subst("$BUILD_DIR")) / "partitions.bin"
    if not path.exists():
        return
    data = path.read_bytes()
    for i in range(0, min(len(data), 0xC00), 32):
        e = data[i:i + 32]
        if len(e) < 32 or e[0:2] != b"\xaa\x50":
            continue
        name = e[12:28].split(b"\0")[0].decode(errors="replace")
        typ = e[2]
        offset = struct.unpack("<I", e[4:8])[0]
        size = struct.unpack("<I", e[8:12])[0]
        if typ == 0:  # app
            print(f"[partition] {name} off=0x{offset:x} size=0x{size:x}")
            if size != EXPECTED:
                raise SystemExit(
                    f"[partition] ERROR: app slot is 0x{size:x}, expected 0x{EXPECTED:x}. "
                    "Check board_build.partitions and flash bootloader+partition table.")
    if not data:
        raise SystemExit("[partition] ERROR: empty partitions.bin")


env.AddPostAction("buildprog", check_partitions)
