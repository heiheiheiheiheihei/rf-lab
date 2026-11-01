#!/usr/bin/env python3
"""USB 热插拔监听 —— 用来回答"这块板到底枚举成了什么"

排查场景:设备插上后内核一直报 `error -71` 却从不出现 `/dev/ttyACM*`。
这时"换了线/换了口气"到底有没有变化,靠肉眼看 lsusb 很容易错过 ——
本脚本按固定间隔轮询并**把每次变化带日期时间戳记进日志**,事后可复盘。

本项目的判读依据(契约 §2.2 / docs/02 §2):
    1a86:55d3  → **WCH CH343P 桥 = 本板的控制口**(默认关注目标)
    303a:xxxx  → Espressif 芯片原生 USB Serial/JTAG(本板从未枚举成功,不可用)
两者都不出现 ⇒ 物理层或 CH343P 驱动问题,与固件无关。

**本脚本只读**:只调用 `lsusb`、glob `/dev/ttyACM*` `/dev/ttyUSB*`、写自己的日志,
不打开串口、不发任何 USB/串口命令、不绑定/解绑驱动。

⚠️ 采样局限(必须如实告知):默认每 0.2s 快照一次。若某设备存在窗口**短于采样间隔**
   且两次快照都恰好错过,本脚本会漏记该次闪烁。要逐事件复盘请用
   `udevadm monitor --udev --kernel`(本工具不自动调用它:保持只读、不依赖 root)。
   看到"目标没出现"时,请把本脚本的采样局限一并考虑,不要把单一日志当唯一证据。

⚠️ `lsusb` 不可用(未安装/PATH 错/权限异常/超时)时,本脚本**如实报错并以非 0 退出**,
   绝不把"工具跑不了"伪装成"总线上没有任何外设"——后者会把排查引向错误的物理层结论。

纯标准库,不需要 root。用法:
    python3 tools/usb_watch.py                     # 默认监听 1800s,日志 /tmp/rflab_usb_watch.log
    python3 tools/usb_watch.py --duration 60
    python3 tools/usb_watch.py --vid-pid 303a:1001 # 改关注目标
"""

from __future__ import annotations

import argparse
import datetime
import glob
import re
import subprocess
import time

# 本板控制口:WCH CH343P(docs/02 §2,契约 §2.2)。可用 --vid-pid 覆盖。
DEFAULT_VID_PID = "1a86:55d3"
NATIVE_USB_VID = "303a"  # Espressif 原生 USB;本板不可用,出现反而是异常

RE_ID = re.compile(r"\bID\s+([0-9a-fA-F]{4}):([0-9a-fA-F]{4})\b")


def parse_vid_pid(line: str) -> str | None:
    m = RE_ID.search(line)
    return f"{m.group(1).lower()}:{m.group(2).lower()}" if m else None


def snapshot() -> tuple[set[str], set[str], str | None]:
    """返回 (usb 设备行集合, tty 集合, 错误信息 or None)。

    ⚠️ 错误必须单独返回:调用方不能把"空集合"当成"总线上没设备"。
    """
    try:
        proc = subprocess.run(["lsusb"], capture_output=True, text=True, timeout=5)
    except FileNotFoundError:
        return set(), set(), "lsusb 未安装(或不在 PATH)"
    except subprocess.TimeoutExpired:
        return set(), set(), "lsusb 超时(>5s)"
    except OSError as exc:
        return set(), set(), f"lsusb 无法执行: {exc}"
    if proc.returncode != 0:
        err = (proc.stderr or proc.stdout).strip() or "无 stderr 输出"
        return set(), set(), f"lsusb 失败(退出码 {proc.returncode}): {err}"
    out = proc.stdout
    usb = {
        ln.strip()
        for ln in out.splitlines()
        if ln.strip() and "root hub" not in ln
    }
    tty = set(glob.glob("/dev/ttyACM*")) | set(glob.glob("/dev/ttyUSB*"))
    return usb, tty, None


def stamp() -> str:
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def annotate(line: str, target: str) -> str:
    """给 lsusb 行加上"这是什么"的标注,避免现场误读。"""
    vidpid = parse_vid_pid(line)
    if vidpid is None:
        return line
    if vidpid == target:
        return f"{line}   ← [目标 {target}] 本板控制口(CH343P)"
    if vidpid.startswith(NATIVE_USB_VID + ":"):
        return f"{line}   ← [原生 USB {NATIVE_USB_VID}:xxxx] 本板应不可用"
    return line


def main() -> int:
    ap = argparse.ArgumentParser(
        description="只读 USB 热插拔监听(默认关注 1a86:55d3 CH343P)")
    ap.add_argument("--log", default="/tmp/rflab_usb_watch.log")
    ap.add_argument("--duration", type=int, default=1800, help="监听秒数(默认 1800)")
    ap.add_argument("--interval", type=float, default=0.2,
                    help="轮询间隔秒(默认 0.2;更小更密但更耗 CPU,短于间隔的闪烁仍可能漏采)")
    ap.add_argument("--vid-pid", default=DEFAULT_VID_PID,
                    help=f"关注目标 VID:PID(默认 {DEFAULT_VID_PID} = CH343P)")
    args = ap.parse_args()

    if args.interval <= 0:
        print("参数错误: --interval 必须 > 0")
        return 2
    target = args.vid_pid.strip().lower()
    if not re.fullmatch(r"[0-9a-f]{4}:[0-9a-f]{4}", target):
        print(f"参数错误: --vid-pid 需要 4 位十六进制 VID:PID,收到 {args.vid_pid!r}")
        return 2

    try:
        log = open(args.log, "a", buffering=1, encoding="utf-8")
    except OSError as exc:
        print(f"FAIL: 打不开日志 {args.log}: {exc}")
        return 2

    def emit(msg: str) -> None:
        line = f"[{stamp()}] {msg}"
        print(line, flush=True)
        log.write(line + "\n")

    def fail(msg: str) -> int:
        emit(f"ERROR: {msg}")
        emit("       ⇒ 本脚本无法监听 USB;请修好 lsusb/权限后重试。"
             "这不代表'总线上没有设备'。")
        log.close()
        return 1

    emit(f"开始监听 {args.duration}s(间隔 {args.interval}s),目标 VID:PID={target},日志 {args.log}")
    prev_usb, prev_tty, err = snapshot()
    if err is not None:
        return fail(err)

    emit("初始状态: lsusb 可用")
    if prev_usb:
        for line in sorted(prev_usb):
            emit(f"  USB 设备: {annotate(line, target)}")
    else:
        emit("  总线上没有任何非根集线器 USB 设备")
    for p in sorted(prev_tty):
        emit(f"  串口: {p}")

    seen_target = any(parse_vid_pid(line) == target for line in prev_usb)
    if seen_target:
        emit(f"目标 {target}: **已枚举**")
    else:
        emit(f"目标 {target}: 未出现"
             f"(采样间隔 {args.interval}s,短事件可能漏采;见 docstring)")

    deadline = time.monotonic() + args.duration
    while time.monotonic() < deadline:
        time.sleep(args.interval)
        usb, tty, err = snapshot()
        if err is not None:
            return fail(f"监听过程中 lsusb 失败: {err}")
        if usb != prev_usb:
            for add in sorted(usb - prev_usb):
                emit(f"USB 出现: {annotate(add, target)}")
            for gone in sorted(prev_usb - usb):
                emit(f"USB 消失: {annotate(gone, target)}")
            prev_usb = usb
            now_seen = any(parse_vid_pid(line) == target for line in usb)
            if now_seen and not seen_target:
                emit(f"目标 {target}: **已出现**")
            elif seen_target and not now_seen:
                emit(f"目标 {target}: **已消失**(重新枚举?)")
            seen_target = now_seen
        if tty != prev_tty:
            for add in sorted(tty - prev_tty):
                emit(f"串口出现: {add}")
            for gone in sorted(prev_tty - tty):
                emit(f"串口消失: {gone}")
            prev_tty = tty

    emit(f"监听结束(目标 {target}: {'出现过' if seen_target else '全程未出现'};"
         f"采样间隔 {args.interval}s,短事件可能漏采)")
    log.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
