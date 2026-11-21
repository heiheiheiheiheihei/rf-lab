#!/usr/bin/env python3
"""Serve web/dist/index.html with a deterministic mock API for UI screenshots.

This is a layout harness, not a device simulator: it only exists so the embedded
single-page UI can be rendered without hardware. It is never part of the firmware.

    python3 tools/ui_mock_server.py --port 8765
    # then open http://127.0.0.1:8765/ (Chromium; no external requests)

The data mirrors the 2026-11-15 screenshot fixture: 5 codes, 5/32 store, 37
frames, 15 ACKs, STA LAN address. SSE emits one learn + one tx event so the
event log is not empty in screenshots.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = pathlib.Path(__file__).resolve().parent.parent
HTML = ROOT / "web" / "dist" / "index.html"
STATE = {
    "fw": "0.7.1", "proto": 1, "uptime_ms": 3725000,
    "net": {
        "mode": "sta", "ap_on": 0, "has_creds": 1,
        "ap_ssid": "RF-Lab-1B59", "ap_ip": "192.168.4.1",
        "sta_connected": True, "sta_ssid": "SSID-A",
        "sta_ip": "192.168.5.117", "rssi": -57,
    },
    "ble": {"on": 1, "conn": 0, "name": "RF-Lab-1B59", "pin_set": 1},
    "module": {"baud": 9600, "rx_bytes": 296, "frames": 37, "acks": 15,
               "dups": 3, "stray": 0, "resync": 0},
    "store": {"count": 5, "max": 32, "ver": 2},
    "oled": {"present": 1, "on": 1, "flush": 43, "err": 0, "max_us": 8747},
    "tx": {"active": 0, "kind": "idle", "id": 0, "ok": 0, "ms": 0},
    "last": {"kind": "rx", "id": 2, "band": "315M", "code": "5555034C", "ms": 123},
}
CODES = [
    {"id": 1, "band": "315M", "code": "5555034C", "name": "客厅大门", "group": "家门", "sym": "🚪"},
    {"id": 2, "band": "315M", "code": "5555033C", "name": "车库卷帘", "group": "车库", "sym": "🚗"},
    {"id": 3, "band": "315M", "code": "5555034B", "name": "玄关灯", "group": "家门", "sym": "💡"},
    {"id": 4, "band": "433M", "code": "A1B2C3D4", "name": "后院插座", "group": "户外", "sym": "⚡", "manual": 1},
    {"id": 5, "band": "433M", "code": "5555033C", "name": "未知遥控", "group": "", "sym": "", "edited": 1},
]
HELLO = {"fw": "0.7.1", "proto": 1, "codebook": 2, "name": "RF-Lab-1B59",
         "ap_ssid": "RF-Lab-1B59", "ip": "192.168.4.1", "auth": 1}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_args) -> None:  # keep the harness quiet
        pass

    def send_json(self, obj, status: int = 200) -> None:
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html"):
            if not HTML.exists():
                self.send_json({"error": "run python3 tools/build_web.py first"}, 503)
                return
            body = HTML.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif path == "/favicon.ico":
            self.send_response(204)
            self.end_headers()
        elif path == "/api/hello":
            self.send_json(HELLO)
        elif path == "/api/state":
            self.send_json(STATE)
        elif path == "/api/codes":
            self.send_json(CODES)
        elif path == "/api/net":
            self.send_json(STATE["net"])
        elif path == "/api/events":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.end_headers()

            def event(name: str, obj: dict) -> None:
                self.wfile.write(
                    f"event: {name}\ndata: {json.dumps(obj, ensure_ascii=False)}\n\n".encode("utf-8")
                )
                self.wfile.flush()

            try:
                event("hello", {"t": "hello", "fw": "0.7.1", "proto": 1, "codebook": 2, "name": "RF-Lab-1B59", "auth": 1})
                event("state", {"t": "state", "seq": 1, "data": STATE})
                event("codes", {"t": "codes", "seq": 2, "items": CODES})
                time.sleep(0.15)
                event("event", {
                    "t": "event", "seq": 3, "kind": "learn", "id": 6,
                    "band": "315M", "code": "5555034C",
                    "line": "LEARN id=6 band=315M code=5555034C",
                })
                event("event", {
                    "t": "event", "seq": 4, "kind": "tx", "tx": "send", "id": 1,
                    "band": "315M", "code": "5555034C", "ok": 1, "ms": 852,
                    "line": "OK tx id=1 kind=send band=315M code=5555034C ok=1 ms=852",
                })
                event("log", {"t": "log", "seq": 5, "line": "OK mode=continuous"})
                while True:
                    time.sleep(10)
                    self.wfile.write(b": ping\n\n")
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass
        else:
            self.send_json({"error": "not found"}, 404)

    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length", "0") or 0)
        if length:
            self.rfile.read(length)
        self.send_json({"ok": True, "queued": True}, 202)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    if not HTML.exists():
        raise SystemExit("web/dist/index.html missing; run python3 tools/build_web.py")
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"RF-LAB mock UI: http://{args.host}:{args.port}/  (Ctrl+C to stop)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
