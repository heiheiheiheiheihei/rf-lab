#!/usr/bin/env python3
"""RF-Lab 射频模块「接入」测试(阶段 2 前置:HIL)

用途:现场按 docs/02-hardware.md §3 接好某一块射频模块后,**一条命令**判定
"这块模块到底被固件认出来了没有",并为每种失败给出接线排查方向。

判据分两层(两层都要看,缺一不可):
  ① 启动横幅里的**只读探测结果**(固件启动时执行,不发射):
       M1: # probe M1 si4463   cts=0xFF part=0x4463 present=1
       M2: # probe M2 cc1101   partnum=0x00 version=0x14 present=1
           + 三行 # rssi(证明调谐与 RX 链路真的在工作,不只是 SPI 通)
       M3: # probe M3 nrf24l01 config=0x08 status=0x.. present=1
  ② 协议面复核:CAPS.modules[].present 必须与横幅一致(否则 CAPS 在撒谎)。

⚠️ 本脚本**不会**发射、不碰 RF_KILL/2.4G 总闸,**也不配对**:HELLO 固定发
    `pairing:false`(只建立会话),不会消耗设备唯一绑定、不写 NVS。
    (历史版本曾默认配对并回退重握手;2026-10-31 起移除——自检工具不应消耗绑定。)
⚠️ 复测前**必须拔掉 GPIO13–14 的回环短接线**,否则整条 SPI 变成回环,
   读数会变成"发出去的字节自己",探测结果不可用。
⚠️ 横幅里的 `# loop ... FAIL(未短接)` 在模块测试时是**正常且期望**的结果;
   若它是 PASS,说明 13–14 短接线还插着,先拔掉再测。

用法:
    python3 tools/rf_module_test.py --expect m1                 # 只接了 M1
    python3 tools/rf_module_test.py --expect m1,m2              # M1+M2(推荐逐块累加)
    python3 tools/rf_module_test.py --expect m1,m2,m3
    python3 tools/rf_module_test.py                             # 只报告,不判定
    python3 tools/rf_module_test.py --port /dev/ttyACM0 --raw-banner

退出码:0 = 所有 --expect 的槽位 PASS(或未指定 --expect 只报告);
        1 = 有期望槽位 FAIL,或 13–14 回环短接线仍在导致结果 INVALID;
        2 = 端口/启动横幅缺失(环境或固件问题)。
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path

from bringup_test import (MSG_CAPS, MSG_GET_CAPS, MSG_HELLO, MSG_HELLO_ACK,
                          Device, _decode, autodetect)

# ---- 期望值 ---------------------------------------------------------------
# 单一来源应为固件 firmware/src/rf_probe.c 的探测判据(本工具不自己发 SPI 命令,
# 只解析固件横幅)。C 侧不是可 import 的常量,故在此镜像一份,并在启动时用
# _firmware_constant_warnings() 对 rf_probe.c 做**只读交叉核对**:固件判据一改,
# 本工具会立刻打印"常量漂移"警告,而不是静默失真。
M1_PART = 0x4463
M2_PARTNUM = 0x00
M2_VERSIONS = (0x14, 0x04)
M3_CONFIG = 0x08


def _firmware_constant_warnings() -> list[str]:
    """尽力从 firmware/src/rf_probe.c 解析期望值并与本工具常量比对(只读)。"""
    src_path = Path(__file__).resolve().parent.parent / "firmware" / "src" / "rf_probe.c"
    try:
        src = src_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return [f"读不到 {src_path},跳过期望值交叉核对(工具常量仍是上面那份)"]
    warns: list[str] = []
    m = re.search(r"part\s*==\s*0x([0-9A-Fa-f]+)", src)
    if m:
        if int(m.group(1), 16) != M1_PART:
            warns.append(f"M1: 工具 M1_PART=0x{M1_PART:04X} ≠ rf_probe.c 的 0x{int(m.group(1),16):04X}")
    else:
        warns.append("M1: rf_probe.c 里找不到 `part == 0x....` 判据,无法交叉核对")
    m2 = re.search(
        r"m->id0\s*==\s*0x([0-9A-Fa-f]+)\s*\).*?m->id1\s*==\s*0x([0-9A-Fa-f]+)"
        r"\s*\|\|\s*m->id1\s*==\s*0x([0-9A-Fa-f]+)", src)
    if m2:
        partnum = int(m2.group(1), 16)
        versions = {int(m2.group(2), 16), int(m2.group(3), 16)}
        if partnum != M2_PARTNUM or versions != set(M2_VERSIONS):
            warns.append(f"M2: 工具 partnum=0x{M2_PARTNUM:02X} versions={M2_VERSIONS} ≠ "
                         f"rf_probe.c 的 partnum=0x{partnum:02X} versions={tuple(sorted(versions))}")
    else:
        warns.append("M2: rf_probe.c 里找不到 partnum/version 判据,无法交叉核对")
    m3 = re.search(r"nrf24_probe.*?m->id0\s*==\s*0x([0-9A-Fa-f]+)", src, re.S)
    if m3:
        if int(m3.group(1), 16) != M3_CONFIG:
            warns.append(f"M3: 工具 M3_CONFIG=0x{M3_CONFIG:02X} ≠ rf_probe.c 的 0x{int(m3.group(1),16):02X}")
    else:
        warns.append("M3: rf_probe.c 里找不到 CONFIG 判据,无法交叉核对")
    return warns

SLOT_NAMES = {1: "M1 Si4463", 2: "M2 CC1101", 3: "M3 nRF24L01"}

RE_WIRE = re.compile(r"# wire MISO 14: pd=(\d) pu=(\d+).*?->\s*(.*)$")
RE_PROBE_M1 = re.compile(
    r"# probe M1 si4463\s+cts=0x([0-9A-Fa-f]+)\s+part=0x([0-9A-Fa-f]+)\s+present=(\d)")
RE_PROBE_M2 = re.compile(
    r"# probe M2 cc1101\s+partnum=0x([0-9A-Fa-f]+)\s+version=0x([0-9A-Fa-f]+)\s+present=(\d)")
RE_PROBE_M3 = re.compile(
    r"# probe M3 nrf24l01 config=0x([0-9A-Fa-f]+)\s+status=0x([0-9A-Fa-f]+)\s+present=(\d)")
RE_LINE = re.compile(r"# line\s+M([12]) cs_pu=(\d) cs_pd=(\d)\s*->\s*(.*)$")
RE_SPI = re.compile(
    r"# spi\s+M([12]) pullup=0x([0-9A-Fa-f]+) pulldown=0x([0-9A-Fa-f]+)\s*->\s*(.*)$")
RE_RSSI = re.compile(r"# rssi ([\d.]+)MHz = (-?\d+) dBm \(raw=0x([0-9A-Fa-f]+)\)")
RE_LOOP = re.compile(r"# loop\s+MOSI13->MISO14 hi=(\d) lo=(\d)\s*->\s*(.*)$")


class Result:
    def __init__(self) -> None:
        self.wire: dict | None = None
        self.probe: dict[int, dict] = {}
        self.line: dict[int, dict] = {}
        self.spi: dict[int, dict] = {}
        self.rssi: list[tuple[float, int, int]] = []
        self.loop: dict | None = None
        self.ready = False

    def parse(self, logs: list[str]) -> None:
        for ln in logs:
            m = RE_WIRE.search(ln)
            if m:
                self.wire = {"pd": int(m.group(1)), "pu": int(m.group(2)),
                             "verdict": m.group(3).strip()}
                continue
            m = RE_PROBE_M1.search(ln)
            if m:
                cts, part, present = (int(m.group(1), 16), int(m.group(2), 16),
                                      int(m.group(3)))
                self.probe[1] = {"cts": cts, "part": part, "present": present}
                continue
            m = RE_PROBE_M2.search(ln)
            if m:
                self.probe[2] = {"partnum": int(m.group(1), 16),
                                 "version": int(m.group(2), 16),
                                 "present": int(m.group(3))}
                continue
            m = RE_PROBE_M3.search(ln)
            if m:
                self.probe[3] = {"config": int(m.group(1), 16),
                                 "status": int(m.group(2), 16),
                                 "present": int(m.group(3))}
                continue
            m = RE_LINE.search(ln)
            if m:
                self.line[int(m.group(1))] = {"cs_pu": int(m.group(2)),
                                              "cs_pd": int(m.group(3)),
                                              "verdict": m.group(4).strip()}
                continue
            m = RE_SPI.search(ln)
            if m:
                self.spi[int(m.group(1))] = {"up": int(m.group(2), 16),
                                             "down": int(m.group(3), 16),
                                             "verdict": m.group(4).strip()}
                continue
            m = RE_RSSI.search(ln)
            if m:
                self.rssi.append((float(m.group(1)), int(m.group(2)),
                                  int(m.group(3), 16)))
                continue
            m = RE_LOOP.search(ln)
            if m:
                self.loop = {"hi": int(m.group(1)), "lo": int(m.group(2)),
                             "verdict": m.group(3).strip()}
                continue
            if ln.strip() == "# ready":
                self.ready = True


def capture_banner(dev: Device, verbose: bool) -> list[str]:
    """复位一次并收全启动横幅(全部以 '#' 开头的行)。"""
    dev.reset_into_app()
    # 横幅在初始化末尾(约 0.9s)发出;再收一段时间,防止慢启动漏行。
    deadline = time.monotonic() + 1.2
    while time.monotonic() < deadline:
        dev.recv(timeout=0.2)
        if any("ready" in s for s in dev.logs):
            break
    if verbose:
        for i, line in enumerate(dev.logs):
            print(f"  [{i:02d}] {line}")
    return list(dev.logs)


def handshake(dev: Device, client_id: str) -> tuple[dict | None, dict | None]:
    """HELLO + GET_CAPS,用于复核 CAPS.modules[].present。

    ⚠️ **本工具不配对**:HELLO 固定发 `pairing:false`。固件对 pairing:false 也会
      置 hello_done(它只表示"不请求配对"),所以 GET_CAPS 可用;同时**不会**
      消耗设备唯一绑定、不会因 `already_bound` 让后续请求回 `ERROR{not_ready}`。
      历史版本先发 pairing:true 再回退到 false(仍可能消耗绑定),已删除。
    """
    ack = None
    for _ in range(3):
        dev.send(MSG_HELLO, {"app_version": "module-test", "proto_min": 1,
                             "proto_max": 1, "client_id": client_id,
                             "pairing": False})
        got = dev.wait_for(MSG_HELLO_ACK, timeout=2.0)
        if got is not None:
            ack = _decode(got[1])
            if ack is None:
                print("  WARN: HELLO_ACK 载荷不是合法 JSON")
            break
    caps = None
    if ack is not None:
        dev.send(MSG_GET_CAPS, {})
        got = dev.wait_for(MSG_CAPS, timeout=3.0)
        if got is not None:
            caps = _decode(got[1])
            if caps is None:
                print("  WARN: CAPS 载荷不是合法 JSON")
    return ack, caps


# ---- 判读 -----------------------------------------------------------------
def judge(slot: int, r: Result, caps: dict | None) -> tuple[bool, list[str], list[str]]:
    """返回 (pass, 证据行, 失败提示)。caps 用于一致性复核。"""
    ev: list[str] = []
    hints: list[str] = []
    p = r.probe.get(slot, {})
    line = r.line.get(slot, {})
    passed = False

    if slot == 1:
        cts = p.get("cts")
        part = p.get("part")
        present = p.get("present")
        ev.append(f"probe: cts=0x{cts:02X} part=0x{part:04X} present={present}"
                  if cts is not None else "probe: 横幅里没有 M1 探测行(固件版本不匹配?)")
        if line:
            ev.append(f"line:  cs_pu={line['cs_pu']} cs_pd={line['cs_pd']} -> {line['verdict']}")
        passed = (cts == 0xFF and part == M1_PART and present == 1)
        if not passed:
            if cts is None:
                hints.append("横幅缺少 M1 行:确认烧的是当前固件(0.2.0),并看串口是否只收到一半横幅。")
            elif cts != 0xFF:
                hints.append("CTS 不是 0xFF = Si4463 没进入命令就绪态。按顺序查:"
                             "①模块 VCC–GND 实测 3.3V(万用表,别信标称);②pin2 SDN 必须接 GND;"
                             "③NSEL(pin5)→GPIO15、SCK(pin8)→GPIO12、MOSI(pin6)→GPIO13;"
                             "④模块 pin6 焊点/杜邦线通断(这是上次的重点怀疑对象)。")
            else:
                hints.append("CTS 正常但 PART 不是 0x4463 = 命令/响应能走通但 ID 不对:"
                             "重点查 MOSI(pin6→GPIO13)是否接到了相邻脚,或 pin 计数错位。")
            if line and line["cs_pu"] != line["cs_pd"]:
                hints.append("line 判为 FLOATING:选中 M1 后 MISO 仍无驱动 ⇒ 先查 VCC/GND 与 NSEL/MISO 是否真接到模块脚上。")

    elif slot == 2:
        partnum = p.get("partnum")
        version = p.get("version")
        present = p.get("present")
        ev.append(f"probe: partnum=0x{partnum:02X} version=0x{version:02X} present={present}"
                  if partnum is not None else "probe: 横幅里没有 M2 探测行(固件版本不匹配?)")
        if line:
            ev.append(f"line:  cs_pu={line['cs_pu']} cs_pd={line['cs_pd']} -> {line['verdict']}")
        ev.append(f"rssi:  {len(r.rssi)} 行" +
                  (" " + ", ".join(f"{f:.3f}MHz={d}dBm" for f, d, _ in r.rssi)
                   if r.rssi else ""))
        passed = (partnum == M2_PARTNUM and version in M2_VERSIONS
                  and present == 1 and len(r.rssi) >= 3)
        if not passed:
            if partnum is None:
                hints.append("横幅缺少 M2 行:确认固件版本与串口收全。")
            elif partnum == 0xFF:
                hints.append("partnum=0xFF = 一直读不到(状态字节 CHIP_RDYn 始终为 1)。查:"
                             "①VCC/GND 实测 3.3V;②CSN(pin4)→GPIO16、SCK(pin5)→GPIO12、MOSI(pin6)→GPIO13;"
                             "③模块 pin6 焊点/杜邦线;④pin 计数错位(AS07 是**奇数一列、偶数一列**)。")
            else:
                hints.append(f"读数非期望值(partnum=0x{partnum:02X}, version=0x{version:02X}):"
                             "多半是 MOSI 接错脚,或读到的是相邻脚/回环数据。")
            if line and line["cs_pu"] != line["cs_pd"]:
                hints.append("line 判为 FLOATING:选中 M2 后 MISO 无驱动 ⇒ 优先查 VCC/GND、CSN、MISO。")
            if present == 1 and len(r.rssi) < 3:
                hints.append("ID 已识别但 RSSI 采样不足 3 点:调谐/RX 状态机未走完,"
                             "查 SPI 稳定性(线长/接触)或晶振起振。")

    else:
        config = p.get("config")
        status = p.get("status")
        present = p.get("present")
        ev.append(f"probe: config=0x{config:02X} status=0x{status:02X} present={present}"
                  if config is not None else "probe: 横幅里没有 M3 探测行(固件版本不匹配?)")
        passed = (config == M3_CONFIG and present == 1)
        if not passed:
            if config is None:
                hints.append("横幅缺少 M3 行:确认固件版本与串口收全。")
            else:
                hints.append("config 应等于复位值 0x08。查:①VCC/GND 实测 3.3V;"
                             "②CSN(pin4)→GPIO17、SCK(pin5)→GPIO12、MOSI(pin6)→GPIO13;"
                             "③CE(pin3)本阶段接 GND(保证 power-down,不发射);"
                             "④模块 pin6 焊点/杜邦线;⑤pin 计数错位(AS01 也是**奇数一列、偶数一列**)。")

    # 协议面一致性
    if caps is not None and p.get("present") is not None:
        caps_slot = next((m for m in caps.get("modules", []) if m.get("slot") == slot), None)
        if caps_slot is None:
            ev.append("CAPS: 缺少该槽位")
        else:
            ev.append(f"CAPS: present={caps_slot.get('present')} "
                      f"chip={caps_slot.get('chip')} bands={caps_slot.get('bands')}")
            if bool(caps_slot.get("present")) != bool(p.get("present")):
                hints.append("CAPS.present 与启动横幅不一致 = 固件内部状态不同步(缺陷,不是接线问题)。")
    return passed, ev, hints


def main() -> int:
    ap = argparse.ArgumentParser(description="RF-Lab 射频模块接入测试(HIL,只读探测)")
    ap.add_argument("--port", help="默认自动找 /dev/ttyACM* /dev/ttyUSB*")
    ap.add_argument("--expect", default="",
                    help="本次应通过的槽位,逗号分隔: m1,m2,m3;省略=只报告不判定")
    ap.add_argument("--raw-banner", action="store_true", help="打印全部启动横幅行")
    ap.add_argument("--client-id", default="module-test",
                    help="HELLO.client_id(仅作实例标识;本工具 pairing:false,永不配对,"
                         "默认 module-test)")
    args = ap.parse_args()

    expect = {s.strip().lower() for s in args.expect.split(",") if s.strip()}
    bad = expect - {"m1", "m2", "m3"}
    if bad:
        print(f"参数错误: --expect 只接受 m1/m2/m3,收到 {sorted(bad)}")
        return 2
    expect_slots = {int(s[1]) for s in expect}

    port = args.port or autodetect()
    if port is None:
        print("FAIL: 找不到串口设备(/dev/ttyACM* 或 /dev/ttyUSB*)")
        print("      ⇒ CH343P 没枚举。先 `lsusb | grep 1a86`,再查数据线/控制口。")
        return 2

    print("== RF-Lab 射频模块接入测试(HIL · 只读探测,不发射,不配对)==")
    print(f"端口: {port}   期望槽位: {args.expect or '(只报告)'}")
    for warn in _firmware_constant_warnings():
        print(f"⚠️ 常量漂移: {warn}")
    print("⚠️ 前提:GPIO13–14 回环短接线必须已拔掉;模块 VCC–GND 必须实测 3.3V。")

    dev = Device(port)
    try:
        print("\n[1/3] 复位并抓取启动横幅 …")
        logs = capture_banner(dev, verbose=args.raw_banner)
        if not logs:
            print("FAIL: 复位后 1.2s 内没有收到任何 '#' 启动行。")
            print("      ⇒ 设备没正常启动/没烧固件/串口选错。先用 bringup_test.py --no-reset 看原始字节。")
            return 2

        dbg = [ln for ln in logs if ln.startswith("# dbg")]
        if dbg:
            print("\n  诊断固件输出:")
            for ln in dbg:
                print(f"    {ln}")

        res = Result()
        res.parse(logs)

        print("\n[2/3] 协议面复核(HELLO / GET_CAPS)…")
        ack, caps = handshake(dev, args.client_id)
        if ack is None:
            print("  WARN: HELLO 无应答,跳过 CAPS 复核(横幅判读仍有效)")
        else:
            print(f"  HELLO_ACK: fw={ack.get('fw_version')} proto={ack.get('proto')} "
                  f"device={ack.get('device_id')}")
            if caps is None:
                print("  WARN: GET_CAPS 无应答")
            else:
                mods = ", ".join(
                    f"槽{m.get('slot')}({m.get('chip')}) present={m.get('present')}"
                    for m in caps.get("modules", []))
                print(f"  CAPS: {mods}")

        print("\n[3/3] 判读 …")
        if res.wire:
            w = res.wire
            print(f"  MISO 空闲电平: pd={w['pd']} pu={w['pu']} -> {w['verdict']}")
        if res.loop:
            lp = res.loop
            loop_ok = lp["hi"] == 1 and lp["lo"] == 0
            note = ("PASS ⇒ 13–14 短接线还插着,**必须先拔掉**再测模块(回环会污染 SPI)"
                    if loop_ok else "FAIL(未短接) ⇒ 这正是模块测试的期望状态")
            print(f"  MOSI13→MISO14 回环: hi={lp['hi']} lo={lp['lo']} -> {note}")
        print()

        all_ok = True
        for slot in (1, 2, 3):
            judged = slot in expect_slots
            passed, ev, hints = judge(slot, res, caps)
            tag = "PASS" if passed else ("FAIL" if judged else "未期望")
            # L9:区分"ID 没认出来"与"ID 认出来了但 RSSI 采样不完整"(退出码同为 1)
            if slot == 2 and judged and not passed:
                p2 = res.probe.get(2, {})
                if (p2.get("partnum") == M2_PARTNUM
                        and p2.get("version") in M2_VERSIONS
                        and p2.get("present") == 1 and len(res.rssi) < 3):
                    tag = "FAIL(ID 已识别 / RSSI 不完整)"
            print(f"  槽{slot} {SLOT_NAMES[slot]}: {tag}")
            for e in ev:
                print(f"      {e}")
            if judged and not passed:
                all_ok = False
                for h in hints:
                    print(f"      ⇒ {h}")
            elif passed and not judged:
                print("      (该槽也有模块被识别;如属预期可忽略)")
            print()

        # 横幅里若 13–14 回环 PASS,则探测读数全部不可信
        if res.loop and res.loop["hi"] == 1 and res.loop["lo"] == 0:
            print("结果: INVALID — 13–14 短接线仍在,拔掉后重新测试。")
            return 1
        if not res.ready:
            print("WARN: 没看到 '# ready' 行,设备可能没走到帧循环。")

        if not expect_slots:
            print("结果: 报告完成(未指定 --expect,不作 PASS/FAIL 判定)。")
            return 0
        if all_ok:
            print("结果: PASS — 期望槽位全部被固件识别。")
            return 0
        print("结果: FAIL — 有期望槽位未被识别,按上面 ⇒ 提示排查接线。")
        return 1
    finally:
        dev.close()


if __name__ == "__main__":
    sys.exit(main())
