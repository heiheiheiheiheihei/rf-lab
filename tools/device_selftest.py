#!/usr/bin/env python3
"""RF-Lab 0.6.0 上机自检(串口 + 设备内 HTTP/SSE/POST)。

默认只做无射频动作的检查(INFO/STATE/WEBTEST/BLE/DISP);
加 --tx 才真的 SEND/LONG/STOP(对自有接收器/学习遥控器做验证)。

    python3 tools/device_selftest.py --port /dev/ttyACM0 --tx \
        --log docs/evidence/2026-11-15-ui-serial.log
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time

import serial


class Dev:
    def __init__(self, port: str, baud: int, log_path: str | None):
        self.s = serial.Serial(port, baud, timeout=0.2)
        self.log_path = log_path
        self.chunks: list[str] = []
        time.sleep(0.3)
        self.s.write(b"\r\n")
        time.sleep(0.3)
        self.s.reset_input_buffer()

    def cmd(self, line: str, wait: float) -> str:
        self.s.write((line + "\r\n").encode())
        buf = b""
        t = time.time()
        while time.time() - t < wait:
            buf += self.s.read(8192)
        text = buf.decode("utf-8", "replace")
        self.chunks.append(f"$ {line}\n{text}")
        if self.log_path:
            with open(self.log_path, "a", encoding="utf-8") as f:
                f.write(f"\n$ {line}\n{text}")
        return text

    def reset_boot(self, wait: float = 2.5) -> str:
        self.s.write(b"RESET\r\n")
        time.sleep(wait)
        text = self.s.read(16384).decode("utf-8", "replace")
        self.chunks.append(f"$ RESET\n{text}")
        if self.log_path:
            with open(self.log_path, "a", encoding="utf-8") as f:
                f.write(f"\n$ RESET\n{text}")
        return text

    def close(self):
        self.s.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--tx", action="store_true", help="run SEND/LONG/STOP (real RF)")
    ap.add_argument("--log", help="append transcript to this file")
    args = ap.parse_args()

    if args.log:
        with open(args.log, "w", encoding="utf-8") as f:
            f.write("# RF-Lab 0.6.0 on-device self-test\n")
            f.write(f"# {time.strftime('%Y-%m-%d %H:%M:%S %z')}\n")

    d = Dev(args.port, args.baud, args.log)
    checks = []

    def check(name: str, ok: bool, detail: str = ""):
        checks.append((name, ok, detail))
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  [{detail}]" if detail and not ok else ""))

    boot = d.reset_boot(3.0)
    check("boot 0.6.0", "fw=0.6.0" in boot)
    check("oled present", "present=yes" in boot or "oled 72x40" in boot)

    # 不碰 WiFi 凭据;AP/STA 两种模式都应能自检。
    info = d.cmd("INFO", 1.5)
    for _ in range(20):
        if "mode=connecting" not in info:
            break
        time.sleep(1)
        info = d.cmd("INFO", 1.0)
    check("stored 5", "stored=5/32" in info)
    check("ble advertising", "adv=yes" in info)
    check("network up", any(m in info for m in ("mode=ap ", "mode=sta ", "mode=apsta ")))

    state_raw = d.cmd("STATE", 1.0)
    m = re.search(r"\{.*\}", state_raw, re.S)
    state_ok = False
    if m:
        try:
            obj = json.loads(m.group(0))
            state_ok = obj.get("fw") == "0.6.0" and obj.get("store", {}).get("count") == 5
            state_detail = f"fw={obj.get('fw')} count={obj.get('store', {}).get('count')}"
        except json.JSONDecodeError as e:
            state_detail = str(e)
    else:
        state_detail = "no JSON"
    check("STATE JSON", state_ok, state_detail)

    disp = d.cmd("DISP", 0.8)
    check("OLED on", "disp=on" in disp and "present=yes" in disp)
    test = d.cmd("DISP TEST 1", 1.5)
    check("DISP TEST", "OK disp=test 1s" in test)
    d.cmd("DISP ON", 1.0)

    ble_off = d.cmd("BLE OFF", 0.8)
    info_off = d.cmd("INFO", 1.0)
    check("BLE OFF", "OK ble=off" in ble_off and "adv=no" in info_off)
    ble_on = d.cmd("BLE ON", 0.8)
    info_on = d.cmd("INFO", 1.0)
    check("BLE ON", "OK ble=on" in ble_on and "adv=yes" in info_on)

    webtest = d.cmd("WEBTEST", 12.0)
    check("HTTP /api/hello 200", re.search(r"200 http://\S+/api/hello", webtest) is not None)
    check("HTTP /api/state 200", re.search(r"200 http://\S+/api/state", webtest) is not None)
    check("SSE /api/events 200", re.search(r"200 http://\S+/api/events", webtest) is not None)
    check("POST /api/cmd 202", "POST 202" in webtest)
    check("POST executed", "OK disp=test 1s" in webtest)

    if args.tx:
        send = d.cmd("SEND 1", 3.0)
        check("SEND F0 ~850ms", "OK send id=1" in send and "0xF0" in send,
              re.search(r"\(0xF0, (\d+)ms\)", send).group(0) if re.search(r"\(0xF0, (\d+)ms\)", send) else "")
        long = d.cmd("LONG 1", 1.5)
        check("LONG FE", "OK long id=1" in long and "0xFE" in long)
        stop = d.cmd("STOP 1", 2.0)
        check("STOP FF", "OK stop id=1" in stop and "0xFF" in stop)
    else:
        print("SKIP  SEND/LONG/STOP (no --tx)")

    d.close()
    failed = [c for c in checks if not c[1]]
    print(f"\n{len(checks) - len(failed)}/{len(checks)} checks passed")
    if failed:
        print("failed:", ", ".join(c[0] for c in failed))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
