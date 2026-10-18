#!/usr/bin/env python3
"""RF-Lab USB 上电自检客户端(阶段 1)

用途:设备烧好固件后,在**主机侧**用一条命令验证 USB 控制通道是否真的能用:
    握手(HELLO/HELLO_ACK)→ 取能力(GET_CAPS/CAPS)→ RTT(PING/PONG)

为什么不用 pyserial:本机没有 pip,不能装第三方包。这里只用标准库
(os/termios/select)直接操作 tty,USB CDC 不依赖波特率。

⚠️ 本脚本**不修改设备任何状态**:不发 TX_REQUEST,也不碰 RF_KILL。

用法:
    python3 tools/bringup_test.py                 # 自动找 /dev/ttyACM* /dev/ttyUSB*
    python3 tools/bringup_test.py --port /dev/ttyACM0
    python3 tools/bringup_test.py --raw           # 打印收到的每个帧

协议权威:docs/30-protocol/010-device-app-protocol.md §2/§3.1/§4
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import select
import struct
import sys
import termios
import time

MAGIC = 0x52
VER = 0x01
HDR_LEN = 6
MAX_PAYLOAD = 16384

MSG_HELLO = 0x01
MSG_GET_CAPS = 0x02
MSG_PING = 0x03
MSG_WARN = 0x7E
MSG_ERROR = 0x7F
MSG_HELLO_ACK = 0x81
MSG_CAPS = 0x82
MSG_PONG = 0x83

NAMES = {
    MSG_HELLO: "HELLO",
    MSG_GET_CAPS: "GET_CAPS",
    MSG_PING: "PING",
    MSG_WARN: "WARN",
    MSG_ERROR: "ERROR",
    MSG_HELLO_ACK: "HELLO_ACK",
    MSG_CAPS: "CAPS",
    MSG_PONG: "PONG",
}


class Device:
    """帧协议的收发端。逐字节状态机,任何噪声都能靠 MAGIC 重新同步。"""

    def __init__(self, port: str, verbose: bool = False) -> None:
        self.verbose = verbose
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self._raw_mode()
        self._buf = bytearray()
        self._st = 0
        self._type = 0
        self._seq = 0
        self._len = 0
        self._got = 0
        self._payload = bytearray()
        self.logs: list[str] = []
        self.seq = 0

    def _raw_mode(self) -> None:
        a = termios.tcgetattr(self.fd)
        a[0] = 0  # iflag: 关掉 CR/LF 转换
        a[1] = 0  # oflag
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        a[3] = 0  # lflag: 关掉行缓冲/回显
        a[4] = termios.B115200
        a[5] = termios.B115200
        a[6][termios.VMIN] = 0
        a[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, a)

    def close(self) -> None:
        try:
            os.close(self.fd)
        except OSError:
            pass

    # --- 发送 -------------------------------------------------------
    def send(self, mtype: int, obj: dict | None) -> int:
        self.seq = (self.seq + 1) & 0xFF
        payload = b"" if obj is None else json.dumps(obj, separators=(",", ":")).encode()
        frame = struct.pack("<BBBBH", MAGIC, VER, mtype, self.seq, len(payload)) + payload
        os.write(self.fd, frame)
        return self.seq

    # --- 接收 -------------------------------------------------------
    def _feed(self, byte: int) -> tuple[int, int, bytes] | None:
        if self._st == 0:
            self._st = 1 if byte == MAGIC else 0
        elif self._st == 1:
            if byte != VER:
                self._st = 0
            else:
                self._st = 2
        elif self._st == 2:
            self._type = byte
            self._st = 3
        elif self._st == 3:
            self._seq = byte
            self._st = 4
        elif self._st == 4:
            self._len = byte
            self._st = 5
        elif self._st == 5:
            self._len |= byte << 8
            self._got = 0
            self._payload = bytearray()
            if self._len > MAX_PAYLOAD:
                self._st = 0
            else:
                self._st = 6 if self._len else 0
                if self._len == 0:
                    return (self._type, self._seq, b"")
        elif self._st == 6:
            self._payload.append(byte)
            self._got += 1
            if self._got >= self._len:
                self._st = 0
                return (self._type, self._seq, bytes(self._payload))
        return None

    def recv(self, timeout: float = 1.0) -> list[tuple[int, int, bytes]]:
        """收帧。超时返回已收到的(可能是空列表)。日志行以 '#' 开头。"""
        out: list[tuple[int, int, bytes]] = []
        deadline = time.monotonic() + timeout
        while True:
            remain = deadline - time.monotonic()
            if remain <= 0:
                break
            r, _, _ = select.select([self.fd], [], [], remain)
            if not r:
                continue
            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:
                continue
            if not chunk:
                break
            for b in chunk:
                # 日志文本行与帧共用一条流:整行以 '#' 开头就单独记录。
                if b in (0x0A, 0x0D):
                    if self._buf:
                        line = bytes(self._buf).decode("utf-8", "replace")
                        if line.startswith("#"):
                            self.logs.append(line)
                        self._buf.clear()
                    if self._st == 0:
                        continue
                if self._st == 0 and b != MAGIC:
                    self._buf.append(b)
                    continue
                got = self._feed(b)
                if got is not None:
                    out.append(got)
        return out

    def wait_for(self, want: int, timeout: float = 3.0) -> tuple[int, bytes] | None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for mtype, seq, payload in self.recv(timeout=0.2):
                if self.verbose:
                    print(f"    ← {NAMES.get(mtype, hex(mtype))} seq={seq} {payload[:160]!r}")
                if mtype == want:
                    return seq, payload
        return None


def _decode(payload: bytes) -> dict:
    try:
        return json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return {}


def autodetect() -> str | None:
    for pat in ("/dev/ttyACM*", "/dev/ttyUSB*"):
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[0]
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="串口设备,默认自动找 /dev/ttyACM* 或 /dev/ttyUSB*")
    ap.add_argument("--raw", action="store_true", help="打印每一帧")
    ap.add_argument("--client-id", default="bringup-test", help="HELLO.client_id")
    args = ap.parse_args()

    port = args.port or autodetect()
    if port is None:
        print("FAIL: 找不到串口设备(/dev/ttyACM* 或 /dev/ttyUSB*)")
        print("      ⇒ 设备没有枚举成功。先解决 USB 链路,再谈协议。")
        print("      自查:lsusb 里是否出现 303a:xxxx(ESP32-S3 USB Serial/JTAG)")
        return 2

    print(f"端口: {port}")
    dev = Device(port, verbose=args.raw)
    # 丢掉开窗噪声
    dev.recv(timeout=0.3)

    fails: list[str] = []

    # --- 1. HELLO ---------------------------------------------------
    print("[1/3] HELLO …")
    seq = dev.send(MSG_HELLO, {
        "app_version": "bringup-test",
        "proto_min": 1,
        "proto_max": 1,
        "client_id": args.client_id,
        "pairing": True,
    })
    got = dev.wait_for(MSG_HELLO_ACK, timeout=3.0)
    if got is None:
        fails.append("HELLO 无应答")
        print("  FAIL: 3s 内没有 HELLO_ACK")
    else:
        ack = _decode(got[1])
        print(f"  OK: fw={ack.get('fw_version')} proto={ack.get('proto')} "
              f"device={ack.get('device_id')} chip={ack.get('chip')} "
              f"uptime={ack.get('uptime_ms')}ms seq_base={ack.get('last_accepted_seq')}")
        if ack.get("pair_ok") is False:
            print(f"  配对失败: pair_error={ack.get('pair_error')}")
            fails.append(f"配对失败({ack.get('pair_error')})")
        elif ack.get("first_bond"):
            print("  首次配对成功(secret 已下发,请勿打印到日志里)")
        if ack.get("proto") != 1:
            fails.append(f"设备 proto={ack.get('proto')} 与帧头 VER=1 不一致")

    # --- 2. GET_CAPS ------------------------------------------------
    print("[2/3] GET_CAPS …")
    dev.send(MSG_GET_CAPS, {})
    got = dev.wait_for(MSG_CAPS, timeout=3.0)
    if got is None:
        fails.append("GET_CAPS 无应答")
        print("  FAIL: 3s 内没有 CAPS")
    else:
        caps = _decode(got[1])
        print(f"  OK: transports={caps.get('transports')} "
              f"active={caps.get('active_transport')}")
        for m in caps.get("modules", []):
            print(f"      槽{m.get('slot')} {m.get('chip'):<10} present={m.get('present')} "
                  f"bands={m.get('bands')} tx_max={m.get('tx_max_dbm')}dBm")
        pol = caps.get("policy", {})
        lim = caps.get("limits", {})
        print(f"      policy: whitelist={pol.get('tx_whitelist')} "
              f"cap={pol.get('tx_power_cap_dbm')}dBm token={pol.get('tx_requires_token')}")
        print(f"      limits: usb_frame_max={lim.get('usb_frame_max')} "
              f"max_edges={lim.get('max_edges')} rate={lim.get('stream_rate_hz')}Hz "
              f"arm_timeout={lim.get('arm_timeout_ms')}ms")
        if lim.get("usb_frame_max") != MAX_PAYLOAD:
            fails.append("limits.usb_frame_max 与固件 RFLAB_USB_FRAME_MAX 不一致")

    # --- 3. PING ----------------------------------------------------
    print("[3/3] PING ×3 …")
    rtts = []
    for _ in range(3):
        t0 = time.monotonic()
        dev.send(MSG_PING, {"t_ms": int(time.time() * 1000)})
        got = dev.wait_for(MSG_PONG, timeout=2.0)
        if got is None:
            fails.append("PING 无应答")
            break
        rtts.append((time.monotonic() - t0) * 1000.0)
        time.sleep(0.05)
    if rtts:
        print(f"  OK: RTT = " + ", ".join(f"{r:.1f}ms" for r in rtts))

    if dev.logs:
        print("设备启动日志:")
        for line in dev.logs[:8]:
            print(f"  {line}")

    dev.close()

    print()
    if fails:
        print("结果: FAIL — " + "; ".join(fails))
        return 1
    print("结果: PASS — USB 控制通道可用")
    return 0


if __name__ == "__main__":
    sys.exit(main())
