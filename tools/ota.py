#!/usr/bin/env python3
"""RF-Lab OTA uploader — arm over USB serial, POST the image over Wi-Fi.

    # device on AP or LAN, USB attached for the one-shot token
    python3 tools/ota.py --host 192.168.4.1 --port /dev/ttyACM0

    # token already printed by the `OTA` serial command
    python3 tools/ota.py --host 192.168.5.117 --token 1A2B3C4D

The device must run a dual-OTA build. The first migration from the old
single-app layout is USB-only (`./tools/flash.sh`); this tool only works after
that. It never erases NVS; a failed upload leaves the running slot untouched.
"""

from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import pathlib
import re
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_IMAGE = ROOT / "firmware" / ".pio" / "build" / "esp32c3" / "firmware.bin"
DIST = ROOT / "web" / "dist" / "index.html"
TOKEN_RE = re.compile(r"token=([0-9A-Fa-f]{8})")


def local_ui_hash() -> str:
    try:
        page = DIST.read_text(encoding="utf-8")
    except OSError:
        return ""
    m = re.search(r'name="rflab-ui" content="([0-9a-f]{8})"', page)
    return m.group(1) if m else ""


def arm_over_serial(port: str) -> str:
    try:
        import serial
    except ImportError:
        raise SystemExit("pyserial missing: pip install pyserial, or pass --token")
    s = serial.Serial(port, 115200, timeout=0.2)
    try:
        time.sleep(0.3)
        s.reset_input_buffer()
        s.write(b"OTA\r\n")
        buf = b""
        t = time.time()
        while time.time() - t < 3.0:
            buf += s.read(4096)
            m = TOKEN_RE.search(buf.decode("utf-8", "replace"))
            if m:
                return m.group(1).upper()
        raise SystemExit("no token from serial; run `OTA` and check the console")
    finally:
        s.close()


def hello(host: str, port: int) -> dict:
    c = http.client.HTTPConnection(host, port, timeout=4)
    try:
        c.request("GET", "/api/hello")
        r = c.getresponse()
        body = r.read()
        if r.status != 200:
            raise RuntimeError(f"HTTP {r.status}")
        return json.loads(body.decode("utf-8"))
    finally:
        c.close()


def upload(host: str, port: int, token: str, image: pathlib.Path) -> dict:
    size = image.stat().st_size
    print(f"image {image} ({size} B) -> http://{host}:{port}/api/ota")
    c = http.client.HTTPConnection(host, port, timeout=30)
    try:
        c.putrequest("POST", "/api/ota")
        c.putheader("Content-Type", "application/octet-stream")
        c.putheader("Content-Length", str(size))
        c.putheader("X-RFLAB-OTA", token)
        c.endheaders()

        sent = 0
        t0 = time.time()
        with image.open("rb") as f:
            while True:
                chunk = f.read(16 * 1024)
                if not chunk:
                    break
                c.send(chunk)
                sent += len(chunk)
                pct = int(sent * 100 / size)
                print(f"\r  upload {pct:3d}%  {sent}/{size} B", end="", flush=True)
        print()
        r = c.getresponse()
        body = r.read().decode("utf-8", "replace")
        print(f"  HTTP {r.status} {body} ({time.time() - t0:.1f}s)")
        try:
            obj = json.loads(body)
        except ValueError:
            obj = {"ok": False, "error": body}
        if r.status != 200 or not obj.get("ok"):
            raise SystemExit(f"OTA failed: {obj.get('error', body)}")
        return obj
    finally:
        c.close()


def wait_for_boot(host: str, port: int, before: dict, want_ui: str) -> bool:
    old_build = before.get("build", "")
    old_ui = before.get("ui", "")
    print("waiting for reboot...")
    deadline = time.time() + 40
    while time.time() < deadline:
        time.sleep(1.0)
        try:
            h = hello(host, port)
        except Exception as exc:  # device rebooting / socket refused
            print(f"\r  offline: {exc}", end="", flush=True)
            continue
        build, ui = h.get("build", ""), h.get("ui", "")
        if build != old_build or ui != old_ui:
            print(f"\n  up: fw={h.get('fw')} build={build} ui={ui}")
            if want_ui and ui and ui != want_ui:
                print(f"  WARN: device ui={ui}, local page={want_ui}")
                return False
            return True
    print("\n  WARN: no version change seen; check serial")
    return False


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.4.1", help="device IP (AP 192.168.4.1 or STA)")
    ap.add_argument("--port", type=int, default=80, help="HTTP port")
    ap.add_argument("--serial", dest="serial_port", help="USB serial to arm (e.g. /dev/ttyACM0)")
    ap.add_argument("--token", help="8-hex token from the serial `OTA` command")
    ap.add_argument("--image", type=pathlib.Path, default=DEFAULT_IMAGE)
    ap.add_argument("--sha256", action="store_true", help="print image sha256 before upload")
    args = ap.parse_args()

    if not args.image.exists():
        raise SystemExit(f"image not found: {args.image}")
    if args.sha256:
        print("sha256", hashlib.sha256(args.image.read_bytes()).hexdigest())

    token = (args.token or "").upper()
    if token and not re.fullmatch(r"[0-9A-F]{8}", token):
        raise SystemExit("--token must be 8 hex characters")
    if not token:
        if not args.serial_port:
            raise SystemExit("need --token or --serial /dev/ttyACM0 (run `OTA` on the console)")
        token = arm_over_serial(args.serial_port)
        print(f"armed: token={token}")

    try:
        before = hello(args.host, args.port)
    except Exception as exc:
        raise SystemExit(
            f"cannot reach http://{args.host}:{args.port}/api/hello: {exc}\n"
            "hint: join the RF-Lab AP (192.168.4.1) or use the STA LAN IP")
    print(f"before: fw={before.get('fw')} build={before.get('build')} ui={before.get('ui')}")

    upload(args.host, args.port, token, args.image)
    ok = wait_for_boot(args.host, args.port, before, local_ui_hash())
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
