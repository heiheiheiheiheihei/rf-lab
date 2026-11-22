#!/usr/bin/env python3
"""RF-Lab 上机自检(串口 + 设备内 HTTP/SSE/POST),版本无关。

不再硬编码 `fw=0.6.0` / `stored=5`:固件版本、build/ui、码库条数都从设备自己的
`INFO` 横幅与 `LIST`/`LIST LONG` 读,再互相交叉校验。这样在 0.7.1/13 条码的
设备上不会因为「数字变了」误报,而真正的漂移(fw 不一致、条数不一致)仍会 FAIL。

默认只做无射频动作的检查(INFO/LIST/LIST LONG/HELP/STATE/DISP/BLE/WEBTEST)。
加 `--tx` 才真的 SEND/LONG/STOP(对自有接收器/学习遥控器做验证);
`--tx-repeat N` 只是把这一组动作重复 N 次并记录 hits/attempts,**不替你判定 RF-01
的 9/10**——判读仍写在 docs/evidence 里。

    python3 tools/device_selftest.py --port /dev/ttyACM0 --tx
    python3 tools/device_selftest.py --port /dev/ttyACM0 --json
    python3 tools/device_selftest.py --port /dev/ttyACM0 --tx --tx-repeat 10 \
        --json --log /tmp/selftest.json.log

退出码:0 = 无 FAIL(允许 SKIP);1 = 有 FAIL;2 = 串口/pyserial 错误。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time

try:  # 解析/单测不需要 pyserial;只有打开串口时才要求。
    import serial
except ImportError:  # pragma: no cover - 依赖缺失路径
    serial = None


# pyserial 缺失时仍能 import 本模块做解析/单测(只有 Dev 打开串口才报错)。
SERIAL_ERRORS: tuple[type[BaseException], ...] = (
    (serial.SerialException, OSError) if serial is not None else (OSError,)
)

PASS, FAIL, SKIP = "pass", "fail", "skip"

# --- 设备输出格式(唯一来源:firmware/src/main.c cmd_info/cmd_list/cmd_disp/cmd_help) ---
RE_BANNER = re.compile(r"#\s*RF-Lab\s+fw=(\S+)")
RE_BUILD = re.compile(r"#\s*build=(\S+)\s+ui=(\S+)")
RE_CODEBOOK = re.compile(r"#\s*codebook\s+ver=(\d+)\s+count=(\d+)/(\d+)")
RE_STORED = re.compile(r"\bOK stored=(\d+)/(\d+)")
RE_NET = re.compile(r"#\s*net\s+mode=(\S+)")
RE_OLED = re.compile(r"#\s*oled\s+[^\n]*?present=(yes|no)")
RE_BLE = re.compile(r"#\s*ble\s+on=(on|off)\s+adv=(yes|no)")
RE_OTA = re.compile(r"#\s*ota\s+slot=(-?\d+)\s+next=(-?\d+)\s+state=(\S+)")
RE_DISP = re.compile(r"OK disp=(on|off) present=(yes|no) test=(on|off)")
RE_LIST = re.compile(r"^\s*(\d+)\s+(315M|433M)\s+([0-9A-Fa-f]{8})\s*$", re.M)
RE_LIST_LONG = re.compile(
    r'^\s*(\d+)\s+(315M|433M)\s+([0-9A-Fa-f]{8})\s+'
    r'name="([^"]*)"\s+group="([^"]*)"\s+sym="([^"]*)"((?:\s+(?:manual|edited))*)\s*$',
    re.M,
)
RE_FB = re.compile(r"#\s*fb\s+(\d+)x(\d+)")
RE_SEND_OK = re.compile(r"\(0xF0,\s*(\d+)ms\)")


def first_line(text: str, fallback: str = "") -> str:
    for line in text.splitlines():
        line = line.strip()
        if line and not line.startswith("$"):
            return line
    return fallback


def parse_boot(text: str) -> dict:
    """启动横幅:fw / build / ui / oled 初始化结果。"""
    d: dict = {}
    m = RE_BANNER.search(text)
    if m:
        d["fw"] = m.group(1)
    m = RE_BUILD.search(text)
    if m:
        d["build"], d["ui"] = m.group(1), m.group(2)
    m = re.search(r"#\s*oled\s+[^\n]*:\s*(\S+)", text)
    if m:
        d["oled_status"] = m.group(1)
    if "oled unavailable" in text:
        d["oled_degraded"] = True
    return d


def parse_info(text: str) -> dict:
    """`INFO` 输出:fw / build / ui / 码库条数 / 网络 / OLED / BLE / OTA 槽。"""
    d: dict = {}
    m = RE_BANNER.search(text)
    if m:
        d["fw"] = m.group(1)
    m = RE_BUILD.search(text)
    if m:
        d["build"], d["ui"] = m.group(1), m.group(2)
    m = RE_CODEBOOK.search(text)
    if m:
        d["codebook_version"] = int(m.group(1))
        d["stored"] = int(m.group(2))
        d["max"] = int(m.group(3))
    else:  # 0.6.x 没有 codebook 行,退回 OK stored=
        m = RE_STORED.search(text)
        if m:
            d["stored"], d["max"] = int(m.group(1)), int(m.group(2))
    m = RE_NET.search(text)
    if m:
        d["net_mode"] = m.group(1)
    m = RE_OLED.search(text)
    if m:
        d["oled_present"] = m.group(1) == "yes"
    m = RE_BLE.search(text)
    if m:
        d["ble_on"] = m.group(1) == "on"
        d["ble_adv"] = m.group(2) == "yes"
    m = RE_OTA.search(text)
    if m:
        d["ota_slot"], d["ota_next"], d["ota_state"] = int(m.group(1)), int(m.group(2)), m.group(3)
    return d


def parse_state(text: str) -> dict | None:
    """`STATE` 的 JSON 快照;找不到/解析失败返回 None。"""
    m = re.search(r"\{.*\}", text, re.S)
    if not m:
        return None
    try:
        obj = json.loads(m.group(0))
    except json.JSONDecodeError:
        return None
    return obj if isinstance(obj, dict) else None


def parse_list(text: str) -> list[dict]:
    """`LIST`:每行 `id band code`。空库文案不产生记录。"""
    return [
        {"id": int(m.group(1)), "band": m.group(2), "code": m.group(3).upper()}
        for m in RE_LIST.finditer(text)
    ]


def parse_list_long(text: str) -> list[dict]:
    """`LIST LONG`:id band code name group sym [manual] [edited]。"""
    out = []
    for m in RE_LIST_LONG.finditer(text):
        flags = m.group(7).split()
        out.append({
            "id": int(m.group(1)),
            "band": m.group(2),
            "code": m.group(3).upper(),
            "name": m.group(4),
            "group": m.group(5),
            "sym": m.group(6),
            "flags": flags,
        })
    return out


def parse_disp(text: str) -> dict:
    d: dict = {}
    m = RE_DISP.search(text)
    if m:
        d["disp"] = m.group(1)
        d["present"] = m.group(2) == "yes"
        d["test"] = m.group(3) == "on"
    return d


def summarize(checks: list[dict]) -> dict:
    counts = {PASS: 0, FAIL: 0, SKIP: 0}
    for c in checks:
        counts[c["status"]] = counts.get(c["status"], 0) + 1
    return {
        "total": len(checks),
        "passed": counts[PASS],
        "failed": counts[FAIL],
        "skipped": counts[SKIP],
    }


def analyze(boot_text: str, info_text: str, state_text: str, list_text: str,
            long_text: str = "", help_text: str = "") -> tuple[dict, list[dict]]:
    """纯解析 + 交叉一致性检查(不碰串口,主机可测)。

    返回 (device, checks)。device 是 INFO 里读到的设备事实,checks 是
    [(name, status, detail), ...] 形式的一致性结论。
    """
    boot = parse_boot(boot_text)
    info = parse_info(info_text)
    state = parse_state(state_text)
    codes = parse_list(list_text)
    codes_long = parse_list_long(long_text) if long_text else []
    checks: list[dict] = []

    def add(name: str, status: str, detail: str = "") -> None:
        checks.append({"name": name, "status": status, "detail": detail})

    def ok(name: str, cond: bool, detail: str = "") -> None:
        add(name, PASS if cond else FAIL, detail)

    # 1) 启动横幅。USB-Serial-JTAG 复位会重枚举,横幅可能读不到:记 SKIP 而不是假红。
    if not boot_text.strip():
        ok("boot banner", False, "RESET produced no output")
    elif "fw" in boot:
        ok("boot banner", True, f"fw={boot['fw']}")
    else:
        add("boot banner", SKIP, "banner not captured (USB re-enumeration?); fw read from INFO")

    # 2) INFO 是版本/条数的唯一来源。
    ok("INFO fw", "fw" in info, f"fw={info.get('fw', '?')}")
    ok("INFO build/ui", bool(info.get("build") and info.get("ui")),
       f"build={info.get('build', '?')} ui={info.get('ui', '?')}")

    if "fw" in boot and "fw" in info:
        ok("fw consistent boot/info", boot["fw"] == info["fw"],
           f"boot={boot['fw']} info={info['fw']}")
    else:
        add("fw consistent boot/info", SKIP, "no boot banner to compare")

    stored, max_codes = info.get("stored"), info.get("max")
    ok("INFO codebook count", stored is not None,
       f"stored={stored if stored is not None else '?'}/{max_codes if max_codes else '?'}")
    if stored is not None and max_codes:
        ok("codebook within capacity", 0 <= stored <= max_codes, f"{stored}/{max_codes}")

    if info.get("oled_present") is True:
        ok("OLED present", True, "0x3C")
    elif info.get("oled_present") is False:
        add("OLED present", SKIP, "absent at 0x3C; degraded mode is expected (RF-06)")
    else:
        add("OLED present", SKIP, "INFO has no present= field")

    if "ble_adv" in info:
        ok("BLE advertising", info["ble_adv"], f"on={info.get('ble_on')} adv={info['ble_adv']}")
    else:
        add("BLE advertising", SKIP, "INFO has no adv= field")

    # 3) LIST / LIST LONG:条数必须和 INFO 一致(这才是版本无关的断言)。
    if stored is None:
        add("LIST count matches INFO", SKIP, "INFO count unknown")
    else:
        ok("LIST count matches INFO", len(codes) == stored,
           f"LIST={len(codes)} INFO={stored}")
    if long_text:
        ok("LIST LONG count matches INFO", len(codes_long) == len(codes),
           f"LIST LONG={len(codes_long)} LIST={len(codes)}")
        ids_ok = [c["id"] for c in codes_long] == list(range(1, len(codes_long) + 1))
        ok("LIST LONG ids 1..N", ids_ok, f"ids={[c['id'] for c in codes_long][:5]}...")
    else:
        add("LIST LONG", SKIP, "no LIST LONG output")

    # 4) HELP:证明当前固件的命令面(0.7.1 起有 LIST LONG/NAME/GROUP/SYM/EDIT)。
    if help_text:
        required = ["LIST LONG", "NAME ", "GROUP ", "SYM ", "EDIT ", "MODE ", "WEBTEST"]
        missing = [t for t in required if t not in help_text]
        ok("HELP 0.7.1 command surface", not missing,
           f"missing={missing}" if missing else "LIST LONG/NAME/GROUP/SYM/EDIT/MODE/WEBTEST")
    else:
        add("HELP 0.7.1 command surface", SKIP, "no HELP output")

    # 5) STATE JSON 与 INFO 必须一致:两条独立路径说同一件事。
    if state is None:
        ok("STATE JSON", False, "no JSON object in STATE output")
    else:
        ok("STATE JSON", True,
           f"fw={state.get('fw')} count={state.get('store', {}).get('count')}")
        ok("STATE fw matches INFO", state.get("fw") == info.get("fw"),
           f"STATE={state.get('fw')} INFO={info.get('fw')}")
        ok("STATE count matches INFO", state.get("store", {}).get("count") == stored,
           f"STATE={state.get('store', {}).get('count')} INFO={stored}")

    device = {
        "fw": info.get("fw"),
        "build": info.get("build"),
        "ui": info.get("ui"),
        "codebook_version": info.get("codebook_version"),
        "stored": stored,
        "max": max_codes,
        "net_mode": info.get("net_mode"),
        "oled_present": info.get("oled_present"),
        "ble_on": info.get("ble_on"),
        "ble_adv": info.get("ble_adv"),
        "ota_slot": info.get("ota_slot"),
    }
    return device, checks


class Report:
    """收集 PASS/FAIL/SKIP;--json 时只留数据,不写人类可读行。"""

    def __init__(self, quiet: bool = False):
        self.checks: list[dict] = []
        self.quiet = quiet

    def add(self, name: str, status: str, detail: str = "") -> None:
        self.checks.append({"name": name, "status": status, "detail": detail})
        if self.quiet:
            return
        line = f"{status.upper():4s}  {name}"
        if detail and status != PASS:
            line += f"  [{detail}]"
        print(line)

    def ok(self, name: str, cond: bool, detail: str = "") -> None:
        self.add(name, PASS if cond else FAIL, detail)

    def skip(self, name: str, detail: str = "") -> None:
        self.add(name, SKIP, detail)

    def summary(self) -> dict:
        return summarize(self.checks)

    def failed(self) -> bool:
        return any(c["status"] == FAIL for c in self.checks)


class Dev:
    def __init__(self, port: str, baud: int, log_path: str | None):
        self.port = port
        self.baud = baud
        self.log_path = log_path
        self.reopened = False
        self.s = self._open(deadline=5.0)
        time.sleep(0.3)
        self._write(b"\r\n")
        time.sleep(0.3)
        self.s.reset_input_buffer()

    # --- 串口底层 ---
    def _open(self, deadline: float):
        if serial is None:
            raise SystemExit("device_selftest: 需要 pyserial(python3 -m pip install pyserial)")
        end = time.time() + deadline
        while True:
            try:
                return serial.Serial(self.port, self.baud, timeout=0.2)
            except SERIAL_ERRORS as e:  # 设备可能正在重启/重枚举
                if time.time() >= end:
                    raise
                time.sleep(0.2)

    def _reopen(self) -> bool:
        try:
            self.s.close()
        except SERIAL_ERRORS:
            pass
        try:
            self.s = self._open(deadline=10.0)
        except SERIAL_ERRORS:
            return False
        self.reopened = True
        time.sleep(0.2)
        return True

    def _write(self, data: bytes) -> None:
        try:
            self.s.write(data)
        except SERIAL_ERRORS:
            if not self._reopen():
                raise
            self.s.write(data)

    def _read_for(self, seconds: float) -> str:
        buf = b""
        end = time.time() + seconds
        while time.time() < end:
            try:
                chunk = self.s.read(self.s.in_waiting or 1)
            except SERIAL_ERRORS:
                if not self._reopen():
                    raise
                continue
            if chunk:
                buf += chunk
            elif not os.path.exists(self.port) and self._reopen():
                continue
        return buf.decode("utf-8", "replace")

    # --- 命令 ---
    def _log(self, header: str, text: str) -> None:
        if not self.log_path:
            return
        with open(self.log_path, "a", encoding="utf-8") as f:
            f.write(f"\n$ {header}\n{text}")

    def cmd(self, line: str, wait: float) -> str:
        self._write((line + "\r\n").encode())
        text = self._read_for(wait)
        self._log(line, text)
        return text

    def reset_boot(self, wait: float = 3.0) -> str:
        self._write(b"RESET\r\n")
        text = self._read_for(wait)
        self._log("RESET", text)
        return text

    def close(self) -> None:
        try:
            self.s.close()
        except SERIAL_ERRORS:
            pass


def wait_net_info(d: "Dev") -> str:
    """INFO 里 mode=connecting 时最多等 20s(STA 优先,15s 内自动开 AP)。"""
    info = d.cmd("INFO", 1.5)
    for _ in range(20):
        mode = parse_info(info).get("net_mode")
        if mode != "connecting":
            break
        time.sleep(1)
        info = d.cmd("INFO", 1.0)
    return info


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="RF-Lab 上机自检(版本无关;默认不发 RF)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="示例:\n"
               "  device_selftest.py --port /dev/ttyACM0            # 只读检查\n"
               "  device_selftest.py --port /dev/ttyACM0 --json     # 可解析 JSON\n"
               "  device_selftest.py --port /dev/ttyACM0 --tx --tx-repeat 10  # RF-01 采集\n",
    )
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--tx", action="store_true", help="真发 SEND/LONG/STOP(需要接收器观察)")
    ap.add_argument("--tx-repeat", type=int, default=1,
                    help="--tx 时每个动作重复次数(RF-01/RF-02 用;默认 1)")
    ap.add_argument("--json", action="store_true", help="stdout 只输出可解析 JSON")
    ap.add_argument("--log", help="把串口原始对话追加到这个文件")
    args = ap.parse_args(argv)
    if args.tx_repeat < 1:
        ap.error("--tx-repeat 必须 >= 1")

    if args.log:
        # 追加而不是截断:--log 常指向 docs/evidence/ 的转录,不能一次误跑清空旧记录。
        with open(args.log, "a", encoding="utf-8") as f:
            f.write("\n# RF-Lab on-device self-test (tools/device_selftest.py)\n")
            f.write(f"# {time.strftime('%Y-%m-%d %H:%M:%S %z')} "
                    f"port={args.port} baud={args.baud} tx={int(args.tx)} "
                    f"repeat={args.tx_repeat}\n")

    report = Report(quiet=args.json)
    device: dict = {}
    tx_stats: dict = {}
    error: str | None = None
    try:
        d = Dev(args.port, args.baud, args.log)
        try:
            boot = d.reset_boot(3.0)
            info_text = wait_net_info(d)
            list_text = d.cmd("LIST", 1.0)
            long_text = d.cmd("LIST LONG", 1.5)
            help_text = d.cmd("HELP", 1.0)
            state_text = d.cmd("STATE", 1.0)

            device, checks = analyze(boot, info_text, state_text, list_text,
                                     long_text, help_text)
            for c in checks:
                report.add(c["name"], c["status"], c["detail"])

            # 网络:STA 优先/AP 回退,两种模式都算 up;只有 connecting 才算没起来。
            mode = device.get("net_mode")
            report.ok("network up", mode in ("ap", "sta", "apsta"), f"mode={mode or '?'}")
            net_up = mode in ("ap", "sta", "apsta")

            # OLED:屏不在时按 RF-06 降级记 SKIP,但 DISP SHOW 仍应工作(读的是帧缓冲)。
            disp_text = d.cmd("DISP", 0.8)
            disp = parse_disp(disp_text)
            if device.get("oled_present") is False:
                report.skip("OLED on (DISP)", "no panel at 0x3C; degraded mode")
                report.skip("DISP TEST", "no panel at 0x3C; degraded mode")
            else:
                report.ok("OLED on (DISP)", disp.get("disp") == "on" and disp.get("present") is True,
                          first_line(disp_text))
                test = d.cmd("DISP TEST 1", 1.5)
                report.ok("DISP TEST", "OK disp=test" in test, first_line(test))
                d.cmd("DISP ON", 1.0)
            show = d.cmd("DISP SHOW", 1.5)
            fb = RE_FB.search(show)
            report.ok("DISP SHOW framebuffer", bool(fb) and f"OK fb {fb.group(1)}x{fb.group(2)}" in show,
                      f"{fb.group(1)}x{fb.group(2)}" if fb else first_line(show))

            # BLE 开关闭环(不改变持久配置,只切广播)。
            ble_off = d.cmd("BLE OFF", 0.8)
            off_adv = parse_info(d.cmd("INFO", 1.0)).get("ble_adv")
            report.ok("BLE OFF", "OK ble=off" in ble_off and off_adv is False,
                      f"adv={off_adv}")
            ble_on = d.cmd("BLE ON", 0.8)
            on_adv = parse_info(d.cmd("INFO", 1.0)).get("ble_adv")
            report.ok("BLE ON", "OK ble=on" in ble_on and on_adv is True,
                      f"adv={on_adv}")

            # 设备内 HTTP/SSE/POST(不依赖本机网络,由固件自己请求自己)。
            if not net_up:
                report.skip("WEBTEST", "net not up; HTTP/SSE not reachable")
            else:
                webtest = d.cmd("WEBTEST", 12.0)
                report.ok("HTTP /api/hello 200",
                          re.search(r"200 http://\S+/api/hello", webtest) is not None,
                          first_line(webtest))
                report.ok("HTTP /api/state 200",
                          re.search(r"200 http://\S+/api/state", webtest) is not None)
                report.ok("SSE /api/events 200",
                          re.search(r"200 http://\S+/api/events", webtest) is not None)
                report.ok("POST /api/cmd 202", "POST 202" in webtest)
                report.ok("POST executed", "OK disp=test 1s" in webtest)

            # 射频:只有 --tx 才碰;默认只记录 SKIP。
            if args.tx:
                n = args.tx_repeat
                send_hits, send_ms = 0, []
                long_hits, stop_hits = 0, 0
                for _ in range(n):
                    send = d.cmd("SEND 1", 3.0)
                    m = RE_SEND_OK.search(send)
                    if "OK send id=1" in send and m:
                        send_hits += 1
                        send_ms.append(int(m.group(1)))
                    long_ = d.cmd("LONG 1", 1.5)
                    if "OK long id=1" in long_ and "0xFE" in long_:
                        long_hits += 1
                    stop_ = d.cmd("STOP 1", 2.0)
                    if "OK stop id=1" in stop_ and "0xFF" in stop_:
                        stop_hits += 1
                report.ok("SEND F0", send_hits == n,
                          f"{send_hits}/{n} F0; ms={send_ms}")
                report.ok("LONG 0xFE", long_hits == n, f"{long_hits}/{n}")
                report.ok("STOP 0xFF", stop_hits == n, f"{stop_hits}/{n}")
                tx_stats = {
                    "repeat": n,
                    "send": {"hits": send_hits, "attempts": n, "ms": send_ms},
                    "long": {"hits": long_hits, "attempts": n},
                    "stop": {"hits": stop_hits, "attempts": n},
                }
            else:
                report.skip("SEND/LONG/STOP", "no --tx (no RF sent)")
        finally:
            d.close()
    except SERIAL_ERRORS as e:  # pragma: no cover - 需要硬件复现
        error = f"{type(e).__name__}: {e}"
        if not args.json:
            print(f"ERROR  {error}", file=sys.stderr)
    except SystemExit as e:  # pyserial 缺失
        error = str(e)
        if not args.json:
            print(f"ERROR  {error}", file=sys.stderr)

    summary = report.summary()
    if args.json:
        payload = {
            "tool": "device_selftest",
            "schema": 1,
            "generated_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
            "port": args.port,
            "baud": args.baud,
            "tx": args.tx,
            "log": args.log,
            "device": device,
            "checks": report.checks,
            "summary": summary,
            "ok": error is None and summary["failed"] == 0,
        }
        if tx_stats:
            payload["tx_stats"] = tx_stats
        if error:
            payload["error"] = error
        print(json.dumps(payload, ensure_ascii=False, indent=2))
    else:
        line = f"\n{summary['passed']}/{summary['passed'] + summary['failed']} checks passed"
        if summary["skipped"]:
            line += f", {summary['skipped']} skipped"
        print(line)
        failed = [c["name"] for c in report.checks if c["status"] == FAIL]
        if failed:
            print("failed:", ", ".join(failed))

    if error is not None:
        return 2
    return 0 if summary["failed"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
