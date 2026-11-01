#!/usr/bin/env python3
"""RF-Lab 上电自检客户端(阶段 1)

用途:设备烧好固件后,在**主机侧**用一条命令验证控制通道是否真的能用:
    握手(HELLO/HELLO_ACK)→ 取能力(GET_CAPS/CAPS)→ RTT(PING/PONG)

🔴 载体(2026-10-21 实测变更):控制通道是 **CH343P → UART0**,不是原生 USB。
   原生 USB Serial/JTAG 在本板从未枚举成功(2026-10-18:9.4 分钟 435 次 connect、
   0 次成功;2026-10-21 换另一个 Type-C 口 150s 零事件)。
   所以本脚本:① 波特率 921600(这条通路唯一被验证过的速率);
              ② 打开后先按 CH343 的自动复位约定复位一次(DTR→IO0,RTS→EN),
                 保证设备从干净状态启动;③ 发 HELLO 前先清空输入缓冲。

🔴 **配对默认关闭(2026-10-31 变更,重要)**
   固件里"首次配对"会把设备**永久绑定**到 HELLO.client_id,而"换绑"需要设备端
   物理按键——本批固件尚未消费该按键。所以自检脚本**默认发 `pairing:false`**:
     - 只建立会话(固件对 pairing:false 也会置 hello_done),**不写 NVS、不消耗绑定**;
     - 想测配对流程必须显式给 `--pair`,并且**必须同时显式给 `--client-id`**,
       避免把设备永久绑定到某个默认测试 id。`--pair` 成功即永久绑定,换绑需
       擦除 NVS 或等固件实现 GPIO38 按键(契约 §7.1(1))。

⚠️ 本脚本**不修改设备任何状态之外的东西**:不发 TX_REQUEST,也不碰 RF_KILL。
   默认唯一"写入"是打开串口;`--pair` 是唯一会写设备 NVS 的路径。

为什么不用第三方库:本脚本只用标准库(os/termios/select/fcntl/struct)直接操作 tty。

用法:
    python3 tools/bringup_test.py                 # 自动找 /dev/ttyACM* /dev/ttyUSB*(优先 1a86:55d3)
    python3 tools/bringup_test.py --port /dev/ttyACM0
    python3 tools/bringup_test.py --raw           # 打印收到的每一帧(含设备 ERROR/WARN)
    python3 tools/bringup_test.py --no-reset      # 不复位(设备已在跑时用)
    python3 tools/bringup_test.py --pair --client-id my-phone   # 显式请求配对(永久绑定!)

退出码:0 = 三步全过;1 = 有步骤失败/被设备 ERROR 拒绝;2 = 端口/参数/环境问题。

协议权威:docs/04-protocol.md §2/§3.1/§4/§7.1
"""

from __future__ import annotations

import argparse
import fcntl
import glob
import json
import os
import select
import struct
import sys
import termios
import time
from pathlib import Path

MAGIC = 0x52
VER = 0x01
HDR_LEN = 6

# USB 承载单帧 LEN 上限。**单一来源是固件 firmware/src/frame.h 的
# RFLAB_USB_FRAME_MAX**,契约见 docs/04 §2.2.2 / §4 limits.usb_frame_max。
# 本脚本无法 import C 头文件,故在此镜像一份;运行时用 CAPS.limits.usb_frame_max
# 交叉核对(不一致即判 FAIL),避免静默漂移。
MAX_PAYLOAD = 16384
PROTOCOL_BAUD = 921600

# 本项目的板载控制口(契约 §2.2 / docs/02 §2):WCH CH343P。
TARGET_VID_PID = "1a86:55d3"
DEFAULT_CLIENT_ID = "bringup-probe"  # 中性值;绝不用于 --pair

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


def _modem_lines(fd: int, set_bits: int = 0, clear_bits: int = 0) -> None:
    """直接改调制解调器控制线(CH343P 的 DTR/RTS 接在 IO0/EN 上)。"""
    if set_bits:
        fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack("i", set_bits))
    if clear_bits:
        fcntl.ioctl(fd, termios.TIOCMBIC, struct.pack("i", clear_bits))


def baud_supported(baud: int) -> bool:
    """termios 里是否有该波特率的 Bxxx 常量(没有就不能假装成功)。"""
    return hasattr(termios, f"B{baud}")


def usb_ids(port: str) -> str | None:
    """尽力从 sysfs 读串口对应 USB 设备的 VID:PID;读不到返回 None。

    纯只读,只用于"哪个 tty 才是 CH343P"的提示,不参与协议。
    """
    dev = Path("/sys/class/tty") / Path(port).name / "device"
    try:
        node = dev.resolve()
    except OSError:
        return None
    for _ in range(5):
        vid = node / "idVendor"
        pid = node / "idProduct"
        try:
            return f"{vid.read_text().strip()}:{pid.read_text().strip()}"
        except OSError:
            node = node.parent
    return None


def list_ports() -> list[str]:
    return sorted({p for pat in ("/dev/ttyACM*", "/dev/ttyUSB*")
                   for p in glob.glob(pat)})


def autodetect(verbose: bool = False) -> str | None:
    """自动选串口:优先 VID:PID = 1a86:55d3(CH343P);多候选时打印并说明。"""
    hits = list_ports()
    if not hits:
        return None
    annotated = [(p, usb_ids(p)) for p in hits]
    if verbose:
        print("串口候选:")
        for p, ids in annotated:
            mark = "  ← 目标 CH343P" if ids == TARGET_VID_PID else ""
            print(f"  {p}  (VID:PID={ids or '未知'}){mark}")
    target = [p for p, ids in annotated if ids == TARGET_VID_PID]
    if target:
        chosen = target[0]
    else:
        chosen = hits[0]
        if len(hits) > 1:
            print(f"  ⚠️ 找到 {len(hits)} 个串口且没有可识别的 {TARGET_VID_PID};"
                  f"默认取 {chosen}。若不对,请用 --port 指定。")
    if verbose:
        print(f"选用: {chosen}")
    return chosen


class Device:
    """帧协议的收发端。逐字节状态机,任何噪声都能靠 MAGIC 重新同步。"""

    def __init__(self, port: str, verbose: bool = False,
                 baud: int = PROTOCOL_BAUD) -> None:
        self.verbose = verbose
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self._raw_mode(baud)
        self._buf = bytearray()
        self._st = 0
        self._type = 0
        self._seq = 0
        self._len = 0
        self._got = 0
        self._payload = bytearray()
        self.logs: list[str] = []
        # 设备主动发来的 ERROR/WARN:即使调用方在等别的帧也要记下来(M4)。
        self.errors: list[dict] = []
        self.seq = 0

    def _raw_mode(self, baud: int) -> None:
        speed = getattr(termios, f"B{baud}", None)
        if speed is None:
            os.close(self.fd)
            raise ValueError(f"不支持的波特率: {baud}(termios 没有 B{baud})")
        a = termios.tcgetattr(self.fd)
        a[0] = 0  # iflag: 关掉 CR/LF 转换
        a[1] = 0  # oflag
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # 无 HUPCL
        a[3] = 0  # lflag: 关掉行缓冲/回显
        a[4] = speed
        a[5] = speed
        a[6][termios.VMIN] = 0
        a[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, a)

    def reset_into_app(self) -> None:
        """按 CH343 自动复位约定把设备复位进**正常启动**(不进下载模式)。

        DTR → IO0(低 = 进下载), RTS → EN(高 = 复位)。
        保持 DTR 为低电平(IO0 高),脉冲 RTS 一下。
        """
        # IO0 高电平 ⇒ 正常启动:清掉 DTR 位。
        _modem_lines(self.fd, clear_bits=termios.TIOCM_DTR)
        # EN 拉低 150ms 再放开。
        _modem_lines(self.fd, set_bits=termios.TIOCM_RTS)
        time.sleep(0.15)
        _modem_lines(self.fd, clear_bits=termios.TIOCM_RTS)
        # 等固件启动完成(横幅在初始化最后发出,约 0.9s)。
        time.sleep(1.0)
        # 不丢弃这段时间的输出:启动横幅是"设备活着"的唯一证据,收进 logs 里打印。
        self.recv(timeout=0.3)

    def drain(self, timeout: float = 0.2) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            r, _, _ = select.select([self.fd], [], [], 0.02)
            if r:
                try:
                    os.read(self.fd, 4096)
                except OSError:
                    break

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
        """收帧。超时返回已收到的(可能是空列表)。"""
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
                self._handle_byte(b, out)
            # 收到至少一帧就返回:请求/响应模型的 RTT 不能把超时窗口算进去。
            if out:
                return out
        return out

    def _text_byte(self, b: int) -> None:
        """非帧字节:按行收集以 '#' 开头的启动横幅。"""
        if b in (0x0A, 0x0D):
            if self._buf:
                line = bytes(self._buf).decode("utf-8", "replace")
                if line.startswith("#"):
                    self.logs.append(line)
                self._buf.clear()
            return
        self._buf.append(b)
        if len(self._buf) > 256:
            del self._buf[:-256]

    def _handle_byte(self, b: int, out: list) -> None:
        # 同步状态机只认得 MAGIC+VER。⚠️ 失配时必须**重新处理当前字节**:
        # 'R'=0x52 会让 "RF-Lab" 里 'R' 触发同步、'F' 被判为 VER 不符,
        # 若把 'F' 直接丢掉,横幅文本就会缺字(真实踩过)。
        if self._st == 0 and b != MAGIC:
            self._text_byte(b)
            return
        if self._st == 1 and b != VER:
            # 上一个 0x52 只是文本里的 'R',不是帧头:**撤回并重新按文本处理**。
            self._st = 0
            self._text_byte(MAGIC)
            if b == MAGIC:
                self._st = 1
            else:
                self._text_byte(b)
            return
        got = self._feed(b)
        if got is not None:
            out.append(got)

    def _note_error(self, mtype: int, seq: int, payload: bytes) -> None:
        """设备发来的 ERROR/WARN:无论调用方在等什么,都记录下来并打印(M4)。"""
        obj = _decode(payload)
        rec = {
            "name": NAMES.get(mtype, hex(mtype)),
            "type": mtype,
            "seq": seq,
            "code": obj.get("code") if obj else None,
            "msg": obj.get("msg") if obj else None,
            "seq_ref": obj.get("seq_ref") if obj else None,
            "raw": payload[:160],
        }
        self.errors.append(rec)
        detail = (f"code={rec['code']} msg={rec['msg']!r}"
                  if obj is not None else f"载荷不是合法 JSON: {rec['raw']!r}")
        print(f"    ← {rec['name']} seq={seq} {detail}")

    def wait_for(self, want: int, timeout: float = 3.0) -> tuple[int, bytes] | None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for mtype, seq, payload in self.recv(timeout=0.2):
                if self.verbose:
                    print(f"    ← {NAMES.get(mtype, hex(mtype))} seq={seq} {payload[:160]!r}")
                if mtype in (MSG_ERROR, MSG_WARN):
                    self._note_error(mtype, seq, payload)
                if mtype == want:
                    return seq, payload
        return None


def _decode(payload: bytes) -> dict | None:
    """PAYLOAD → dict。空载荷/非法 JSON/非对象一律返回 None(不伪装成 {})。"""
    if not payload:
        return None
    try:
        obj = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None
    return obj if isinstance(obj, dict) else None


def _describe_device_errors(dev: Device, mark: int, fails: list[str],
                            step: str) -> None:
    """把本步骤期间收到的 ERROR 作为失败原因报出来,而不是只报"无应答"。"""
    errs = dev.errors[mark:]
    if not errs:
        return
    parts = []
    for e in errs:
        parts.append(f"{e['name']}{{code={e['code']}, msg={e['msg']}, seq_ref={e['seq_ref']}}}")
    fails.append(f"{step} 被设备拒绝: " + "; ".join(parts))


def main() -> int:
    ap = argparse.ArgumentParser(
        description="RF-Lab 上电自检:HELLO / GET_CAPS / PING(默认不配对)")
    ap.add_argument("--port", help="串口设备,默认自动找 /dev/ttyACM* 或 /dev/ttyUSB*"
                                   "(优先 VID:PID=1a86:55d3 的 CH343P)")
    ap.add_argument("--baud", type=int, default=PROTOCOL_BAUD,
                    help=f"协议波特率(默认 {PROTOCOL_BAUD};必须是 termios 支持的速率)")
    ap.add_argument("--raw", action="store_true", help="打印每一帧(含设备 ERROR/WARN)")
    ap.add_argument("--no-reset", action="store_true",
                    help="不复位设备(设备已在跑、只想发协议帧时用)")
    ap.add_argument("--client-id", default=None,
                    help=f"HELLO.client_id;默认 {DEFAULT_CLIENT_ID!r}(不配对时只是实例标识)")
    ap.add_argument("--pair", action="store_true",
                    help="显式请求配对(pairing:true)。⚠️ 会把设备永久绑定到 --client-id;"
                         "换绑需设备端按键/擦 NVS(本批固件未实现按键消费)。默认关闭")
    args = ap.parse_args()

    if args.pair and not args.client_id:
        ap.error("--pair 必须同时显式给出 --client-id:"
                 "配对会永久绑定,不能用默认测试 id 静默完成")
    client_id = args.client_id or DEFAULT_CLIENT_ID

    if not baud_supported(args.baud):
        print(f"FAIL: 不支持的波特率 {args.baud}(termios 没有 B{args.baud})")
        print("      ⇒ 本通路被验证过的唯一速率是 921600,不要用任意值。")
        return 2

    port = args.port or autodetect(verbose=True)
    if port is None:
        print("FAIL: 找不到串口设备(/dev/ttyACM* 或 /dev/ttyUSB*)")
        print("      ⇒ CH343P 没被枚举。检查:lsusb 里是否有 1a86:55d3")
        print("      自查:lsusb 是否只有 1a86:55d3(原生 USB 303a 在本板不可用)")
        return 2

    print(f"端口: {port}  波特率: {args.baud}  client_id: {client_id}")
    if args.pair:
        print("配对: 开启(--pair)⚠️ 成功即**永久绑定**;换绑需设备端按键/擦 NVS")
    else:
        print("配对: 关闭(默认 pairing:false;只建会话,不写 NVS、不消耗唯一绑定)")
    try:
        dev = Device(port, verbose=args.raw, baud=args.baud)
    except (OSError, ValueError) as exc:
        print(f"FAIL: 打不开串口 {port}: {exc}")
        return 2
    if not args.no_reset:
        print("复位设备到正常启动(DTR→IO0 高 / RTS→EN 脉冲)…")
        dev.reset_into_app()
    else:
        dev.drain(timeout=0.3)

    fails: list[str] = []

    # --- 1. HELLO(带重试:复位后固件可能还没装好 UART0 驱动)----------
    print("[1/3] HELLO …")
    got = None
    for attempt in range(3):
        payload = {
            "app_version": "0.2.0-bringup",
            "proto_min": 1,
            "proto_max": 1,
            "client_id": client_id,
            "pairing": bool(args.pair),
        }
        dev.send(MSG_HELLO, payload)
        mark = len(dev.errors)
        got = dev.wait_for(MSG_HELLO_ACK, timeout=2.0)
        if got is not None:
            break
        if attempt < 2:
            if dev.errors[mark:]:
                # 设备明确回了 ERROR,不要再盲重试;交给下面统一报错。
                break
            print(f"  无应答,重试 {attempt + 1}/2 …")
    if got is None:
        if dev.errors:
            # 设备明确回过 ERROR/WARN:以设备的话为失败原因,不要误报成"固件没起来"。
            _describe_device_errors(dev, 0, fails, "HELLO")
        else:
            fails.append("HELLO 无应答")
            print("  FAIL: 6s 内没有 HELLO_ACK")
            print("        ⇒ 先确认:① 固件是否已烧录;② 设备复位后是否正常启动;")
            print("          ③ 用 921600 打开串口并复位,应能看到启动横幅 '# RF-Lab fw=…'")
            print("             (复位后 ROM bootloader 的几行是 115200,会是乱码,正常)。")
    else:
        ack = _decode(got[1])
        if ack is None:
            fails.append("HELLO_ACK 载荷不是合法 JSON")
            print(f"  FAIL: HELLO_ACK 载荷不是合法 JSON: {got[1][:120]!r}")
        else:
            missing = [k for k in ("fw_version", "proto", "device_id", "chip", "uptime_ms")
                       if k not in ack]
            if missing:
                fails.append("HELLO_ACK 缺少字段: " + ",".join(missing))
            print(f"  OK: fw={ack.get('fw_version')} proto={ack.get('proto')} "
                  f"device={ack.get('device_id')} chip={ack.get('chip')} "
                  f"uptime={ack.get('uptime_ms')}ms seq_base={ack.get('last_accepted_seq')}")
            if ack.get("proto") != 1:
                fails.append(f"设备 proto={ack.get('proto')} 与帧头 VER=1 不一致")

            if args.pair:
                if "pair_ok" not in ack:
                    fails.append("--pair 已请求但 HELLO_ACK 未回 pair_ok")
                    print("  FAIL: --pair 已请求,但设备未回 pair_ok(固件可能未实现配对字段)")
                elif ack.get("pair_ok") is False:
                    pe = ack.get("pair_error")
                    print(f"  FAIL: 配对失败 pair_error={pe!r}")
                    guidance = {
                        "already_bound": "设备已绑定其它 client_id:换绑需设备端按键"
                                         "(本批固件未消费按键)或擦 NVS/重烧;不要反复重试",
                        "rng_failed": "设备 RNG 初始化失败 ⇒ 必须拒绝配对,这是硬件/固件缺陷",
                        "no_display": "本批无 pair_code 搬运通道(BLE 路径);请改用 USB 配对",
                        "bad_code": "本批 USB 不校验 pair_code,出现该值说明实现有误",
                        "ble_only": "本批无 Wi-Fi 链路,该值应为保留值、无活路径",
                    }
                    if pe in guidance:
                        print(f"        ⇒ {guidance[pe]}")
                    fails.append(f"配对失败({pe})")
                elif ack.get("first_bond"):
                    print("  配对成功: 首次绑定完成(pairing_secret 已写入设备 NVS,"
                          "⚠️ 永久绑定;换绑需设备端按键/擦 NVS)")
                else:
                    print("  配对通过: 设备已绑定同一 client_id(未重发 secret)")
            else:
                print("  配对: 未请求(默认 pairing:false),设备绑定状态未改变")
                if "pair_ok" in ack:
                    print("  ⚠️ 设备在 pairing:false 下仍回了 pair_ok,请核对固件是否尊重该标志")

    # --- 2. GET_CAPS ------------------------------------------------
    print("[2/3] GET_CAPS …")
    mark = len(dev.errors)
    dev.send(MSG_GET_CAPS, {})
    got = dev.wait_for(MSG_CAPS, timeout=3.0)
    if got is None:
        _describe_device_errors(dev, mark, fails, "GET_CAPS")
        if not any("GET_CAPS" in f for f in fails):
            fails.append("GET_CAPS 无应答")
        print("  FAIL: 3s 内没有 CAPS")
        print("        ⇒ 若设备刚拒绝过 HELLO,先解决 HELLO;not_ready 表示会话未建立。")
    else:
        caps = _decode(got[1])
        if caps is None:
            fails.append("CAPS 载荷不是合法 JSON")
            print(f"  FAIL: CAPS 载荷不是合法 JSON: {got[1][:120]!r}")
        else:
            print(f"  OK: transports={caps.get('transports')} "
                  f"active={caps.get('active_transport')}")
            for m in caps.get("modules", []):
                print(f"      槽{m.get('slot')} {m.get('chip'):<10} present={m.get('present')} "
                      f"bands={m.get('bands')} tx_max={m.get('tx_max_dbm')}dBm")
            pol = caps.get("policy") or {}
            lim = caps.get("limits") or {}
            print(f"      policy: whitelist={pol.get('tx_whitelist')} "
                  f"cap={pol.get('tx_power_cap_dbm')}dBm token={pol.get('tx_requires_token')}")
            print(f"      limits: usb_frame_max={lim.get('usb_frame_max')} "
                  f"max_edges={lim.get('max_edges')} rate={lim.get('stream_rate_hz')}Hz "
                  f"arm_timeout={lim.get('arm_timeout_ms')}ms")
            usb_max = lim.get("usb_frame_max")
            if usb_max is None:
                fails.append("CAPS.limits.usb_frame_max 缺失")
            elif usb_max != MAX_PAYLOAD:
                fails.append(f"limits.usb_frame_max={usb_max} 与本脚本镜像的 "
                             f"MAX_PAYLOAD={MAX_PAYLOAD} / 固件 RFLAB_USB_FRAME_MAX 不一致")

    # --- 3. PING ----------------------------------------------------
    print("[3/3] PING ×3 …")
    rtts = []
    for _ in range(3):
        t0 = time.monotonic()
        t_ms = int(time.time() * 1000)
        mark = len(dev.errors)
        dev.send(MSG_PING, {"t_ms": t_ms})
        got = dev.wait_for(MSG_PONG, timeout=2.0)
        if got is None:
            _describe_device_errors(dev, mark, fails, "PING")
            if not any("PING" in f for f in fails):
                fails.append("PING 无应答")
            break
        rtts.append((time.monotonic() - t0) * 1000.0)
        pong = _decode(got[1])
        if pong is None:
            fails.append("PONG 载荷不是合法 JSON")
            break
        if pong.get("t_ms") is None:
            print("  ⚠️ PONG 缺少 t_ms(schema 要求);RTT 仍按本机时钟计算")
        elif pong.get("t_ms") != t_ms:
            print(f"  ⚠️ PONG.t_ms={pong.get('t_ms')} 未回显 {t_ms}"
                  f"(契约 §3.1 未强制回显,见 schemas README §7.14)")
        time.sleep(0.05)
    if rtts:
        print(f"  OK: RTT = " + ", ".join(f"{r:.1f}ms" for r in rtts))

    if dev.logs:
        # ⚠️ 不要截断:横幅后半段(#probe/#line/#loop/#rssi)正是射频接线判读的证据,
        #    截成前 8 行会让"模块探测失败"看起来像"什么都没发生"。
        print("启动横幅(设备启动时发出,921600):")
        for line in dev.logs:
            print(f"  {line}")

    dev.close()

    print()
    if fails:
        print("结果: FAIL — " + "; ".join(fails))
        return 1
    print(f"结果: PASS — 控制通道可用(CH343P/UART0 @{args.baud},配对={'开启' if args.pair else '关闭'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
