#!/usr/bin/env python3
"""RF-Lab 主机命令行 —— 没有 OLED/按键,这就是设备的 UI。

用法:
    python3 tools/rf34dl.py --port /dev/ttyACM0                 # 交互模式
    python3 tools/rf34dl.py --port /dev/ttyACM0 info list
    python3 tools/rf34dl.py --port /dev/ttyACM0 learn 15        # 听 15s,按遥控器
    python3 tools/rf34dl.py --port /dev/ttyACM0 send 1

交互模式下直接输固件命令(HELP / LIST / SEND 1 ...),输入 learn N 听 N 秒。
默认 115200 8N1;ESP32-C3 原生 USB-Serial-JTAG 不需要驱动。
"""

import argparse
import sys
import time

import serial


def drain(port, seconds, out=sys.stdout):
    """读 seconds 秒,边读边写出去(解码事件是异步的,不能只读一次)。"""
    t0 = time.time()
    got = 0
    while time.time() - t0 < seconds:
        data = port.read(512)
        if data:
            got += len(data)
            out.write(data.decode("utf-8", "replace"))
            out.flush()
    return got


def send(port, line, wait=1.0, out=sys.stdout):
    port.write((line + "\r\n").encode())
    return drain(port, wait, out)


def learn(port, seconds, out=sys.stdout):
    out.write(f"# listening {seconds:.0f}s — press a button on the remote\n")
    out.flush()
    return drain(port, seconds, out)


def repl(port, wait, out=sys.stdout):
    out.write("# RF-Lab console. Type HELP; 'learn N' to listen; 'quit' to exit.\n")
    out.flush()
    drain(port, 0.5, out)
    while True:
        try:
            line = input("rf> ").strip()
        except (EOFError, KeyboardInterrupt):
            out.write("\n")
            return 0
        if not line:
            continue
        low = line.lower()
        if low in ("quit", "exit", "q"):
            return 0
        if low.startswith("learn"):
            parts = line.split()
            secs = float(parts[1]) if len(parts) > 1 else 10.0
            learn(port, secs, out)
            continue
        send(port, line, wait, out)


def main():
    ap = argparse.ArgumentParser(description="RF-Lab 主机命令行")
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--wait", type=float, default=1.0,
                    help="单条命令后读多久(秒),默认 1.0")
    ap.add_argument("commands", nargs="*",
                    help="固件命令;留空进入交互模式。learn N 走特殊路径")
    args = ap.parse_args()

    try:
        port = serial.Serial(args.port, args.baud, timeout=0.05)
    except serial.SerialException as e:
        print(f"cannot open {args.port}: {e}", file=sys.stderr)
        print("提示:ls /dev/ttyACM*;Linux 下加 dialout 组权限", file=sys.stderr)
        return 2

    try:
        time.sleep(0.3)
        port.reset_input_buffer()

        if not args.commands:
            return repl(port, args.wait)

        for cmd in args.commands:
            low = cmd.lower()
            if low.startswith("learn"):
                parts = cmd.split()
                secs = float(parts[1]) if len(parts) > 1 else 10.0
                learn(port, secs)
            else:
                send(port, cmd, args.wait)
        return 0
    finally:
        port.close()


if __name__ == "__main__":
    sys.exit(main())
