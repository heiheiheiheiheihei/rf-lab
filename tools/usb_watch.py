#!/usr/bin/env python3
"""USB 热插拔监听 —— 用来回答"这块板到底枚举成了什么"

排查场景:设备插上后内核一直报 `error -71` 却从不出现 `/dev/ttyACM*`。
这时"换了线/换了口气"到底有没有变化,靠肉眼看 lsusb 很容易错过 ——
本脚本按 0.5s 轮询并**把每次变化带时间戳记进日志**,事后可复盘。

判读依据(本项目的两种可能):
    Espressif 0x303a:xxxx  → 芯片原生 USB Serial/JTAG(本设计要走的那条)
    WCH       0x1a86:55d3  → CH343P 桥(UART/下载口)
两者都不出现 ⇒ 物理层问题,与固件无关。

纯标准库,不需要 root。
"""

from __future__ import annotations

import argparse
import datetime
import glob
import subprocess
import time


def snapshot() -> tuple[set[str], set[str]]:
    try:
        out = subprocess.run(
            ["lsusb"], capture_output=True, text=True, timeout=5
        ).stdout
    except (OSError, subprocess.SubprocessError):
        out = ""
    usb = {
        ln.strip()
        for ln in out.splitlines()
        if "root hub" not in ln and ln.strip()
    }
    tty = set(glob.glob("/dev/ttyACM*")) | set(glob.glob("/dev/ttyUSB*"))
    return usb, tty


def stamp() -> str:
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", default="/tmp/rflab_usb_watch.log")
    ap.add_argument("--duration", type=int, default=1800, help="监听秒数")
    args = ap.parse_args()

    log = open(args.log, "a", buffering=1, encoding="utf-8")

    def emit(msg: str) -> None:
        line = f"[{stamp()}] {msg}"
        print(line, flush=True)
        log.write(line + "\n")

    emit(f"开始监听 {args.duration}s,日志 {args.log}")
    prev_usb, prev_tty = snapshot()
    if prev_usb or prev_tty:
        emit(f"初始状态: usb={sorted(prev_usb)} tty={sorted(prev_tty)}")
    else:
        emit("初始状态: 总线上没有任何外设(只有根集线器)")

    deadline = time.monotonic() + args.duration
    while time.monotonic() < deadline:
        time.sleep(0.5)
        usb, tty = snapshot()
        if usb != prev_usb:
            for add in sorted(usb - prev_usb):
                emit(f"USB 出现: {add}")
            for gone in sorted(prev_usb - usb):
                emit(f"USB 消失: {gone}")
            prev_usb = usb
        if tty != prev_tty:
            for add in sorted(tty - prev_tty):
                emit(f"串口出现: {add}")
            for gone in sorted(prev_tty - tty):
                emit(f"串口消失: {gone}")
            prev_tty = tty

    emit("监听结束")
    log.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
