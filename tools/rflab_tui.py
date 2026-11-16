#!/usr/bin/env python3
"""RF-Lab uConsole 键盘 TUI — 战术终端风格,仅标准库。

连接方式:
    rflab-tui --url http://192.168.4.1      # Wi-Fi / SSH(默认)
    rflab-tui --port /dev/ttyACM0           # USB 串口回退
    rflab-tui --mock                        # 无设备演示/测试

键位:
    ↑/↓ j/k  选码       Enter  SEND        L  LONG
    S  STOP            D  ERASE(按两次)     A  ARM/DISARM
    R  刷新            T  OLED TEST 30s     ?  帮助
    Q  退出

设计口径见 docs/01-ui.md §6。所有业务命令仍走固件既有命令行;
TUI 不做第二套协议。
"""

from __future__ import annotations

import argparse
import curses
import json
import os
import re
import sys
import threading
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from typing import Optional

# ---------------- 色板(与 Web 同一套) ----------------
C_INK = 0
C_AMBER = 1
C_BONE = 2
C_KHAKI = 3
C_MUTED = 4
C_GREEN = 5
C_RED = 6
C_PANEL = 7

PALETTE = {
    C_AMBER: (255, 176, 0),
    C_BONE: (232, 227, 210),
    C_KHAKI: (168, 163, 140),
    C_MUTED: (115, 112, 94),
    C_GREEN: (139, 224, 78),
    C_RED: (255, 90, 78),
    C_PANEL: (60, 69, 41),
}

KEY_ROWS = [
    ("↑/↓ j/k", "select"),
    ("Enter", "send"),
    ("L", "long"),
    ("S", "stop"),
    ("D", "erase x2"),
    ("A", "arm"),
    ("T", "oled test"),
    ("R", "refresh"),
    ("?", "help"),
    ("Q", "quit"),
]

HELP_ROWS = [
    ("↑/↓ or j/k", "选择码 / select code"),
    ("Enter", "单次发射 SEND"),
    ("L", "长发 LONG"),
    ("S", "停止 STOP"),
    ("D", "删除,按两次确认 ERASE"),
    ("A", "ARM / DISARM(发射前必须 ARM)"),
    ("R", "重新拉取状态 / refresh"),
    ("T", "OLED 测试图案 30s"),
    ("Q", "退出 / quit"),
    ("?", "关闭帮助"),
]

LOG_MAX = 240


# ---------------- 纯函数(主机可单测) ----------------

def parse_list_line(line: str) -> Optional[dict]:
    """解析固件 `LIST` 的一行:` 1  315M 5555034C`。"""
    m = re.match(r"^\s*(\d+)\s+(315M|433M)\s+([0-9A-Fa-f]{8})\s*$", line)
    if not m:
        return None
    return {"id": int(m.group(1)), "band": m.group(2), "code": m.group(3).upper()}


def parse_serial_event(line: str) -> Optional[dict]:
    """把串口事件行变成 TUI 日志/状态用的 dict。"""
    line = line.strip()
    if not line:
        return None
    m = re.search(r"LEARN id=(\d+) band=(\S+) code=([0-9A-Fa-f]{8})", line)
    if m:
        return {"kind": "learn", "id": int(m.group(1)), "band": m.group(2),
                "code": m.group(3).upper(), "line": line}
    m = re.search(r"RX id=(\d+) band=(\S+) code=([0-9A-Fa-f]{8})", line)
    if m:
        return {"kind": "rx", "id": int(m.group(1)), "band": m.group(2),
                "code": m.group(3).upper(), "line": line}
    m = re.search(r"OK (tx|long|stop) id=(\d+) .*?\(0x([0-9A-Fa-f]{2}), (\d+)ms\)", line)
    if m:
        return {"kind": "tx", "tx": m.group(1), "id": int(m.group(2)),
                "ok": 1, "ms": int(m.group(4)), "line": line}
    m = re.search(r"OK stored=(\d+)/(\d+) rx_bytes=(\d+) frames=(\d+) acks=(\d+) dups=(\d+) stray=(\d+)", line)
    if m:
        return {"kind": "counters", "store": int(m.group(1)), "max": int(m.group(2)),
                "rx_bytes": int(m.group(3)), "frames": int(m.group(4)),
                "acks": int(m.group(5)), "dups": int(m.group(6)),
                "stray": int(m.group(7)), "line": line}
    m = re.search(r"# net mode=(\S+) ap=(\S+) ip=(\S+) clients=(\d+) sta=(\S+) sta_ip=(\S+) rssi=(-?\d+)", line)
    if m:
        return {"kind": "net", "mode": m.group(1), "ap_ssid": m.group(2),
                "ap_ip": m.group(3), "clients": int(m.group(4)),
                "sta": m.group(5), "sta_ip": m.group(6), "rssi": int(m.group(7)),
                "line": line}
    if line.startswith(("ERR", "WARN")):
        return {"kind": "warn", "line": line}
    if line.startswith("OK "):
        return {"kind": "ok", "line": line}
    return {"kind": "log", "line": line}


def parse_state_json(obj: dict) -> dict:
    """把 HTTP /api/state 压成 TUI 需要的最小字段;缺字段不抛。"""
    net = obj.get("net") or {}
    mod = obj.get("module") or {}
    store = obj.get("store") or {}
    oled = obj.get("oled") or {}
    tx = obj.get("tx") or {}
    last = obj.get("last") or {}
    return {
        "fw": obj.get("fw", "?"),
        "uptime_ms": obj.get("uptime_ms", 0),
        "net_mode": net.get("mode", "ap"),
        "ap_on": bool(net.get("ap_on", True)),
        "has_creds": bool(net.get("has_creds", False)),
        "ap_ssid": net.get("ap_ssid", "?"),
        "ap_ip": net.get("ap_ip", "?"),
        "sta_connected": bool(net.get("sta_connected")),
        "sta_ssid": net.get("sta_ssid", ""),
        "sta_ip": net.get("sta_ip", ""),
        "rssi": net.get("rssi", 0),
        "baud": mod.get("baud", 0),
        "frames": mod.get("frames", 0),
        "acks": mod.get("acks", 0),
        "stray": mod.get("stray", 0),
        "count": store.get("count", 0),
        "max": store.get("max", 32),
        "oled_present": bool(oled.get("present")),
        "oled_on": bool(oled.get("on")),
        "tx_active": bool(tx.get("active")),
        "tx_kind": tx.get("kind", "idle"),
        "tx_id": tx.get("id", 0),
        "tx_ok": tx.get("ok", 0),
        "tx_ms": tx.get("ms", 0),
        "last_kind": last.get("kind", ""),
        "last_id": last.get("id", 0),
        "last_band": last.get("band", ""),
        "last_code": last.get("code", ""),
    }


def parse_codes_json(items) -> list[dict]:
    out = []
    if not isinstance(items, list):
        return out
    for it in items:
        if not isinstance(it, dict):
            continue
        try:
            out.append({"id": int(it["id"]), "band": str(it["band"]),
                        "code": str(it["code"]).upper()})
        except (KeyError, TypeError, ValueError):
            continue
    return out


def format_uptime(ms: int) -> str:
    sec = max(0, int(ms)) // 1000
    d, sec = divmod(sec, 86400)
    h, sec = divmod(sec, 3600)
    m, sec = divmod(sec, 60)
    return (f"{d}D " if d else "") + f"{h:02d}:{m:02d}:{sec:02d}"


# ---------------- 传输 ----------------

class HttpLink:
    name = "HTTP"

    def __init__(self, url: str):
        self.url = url.rstrip("/")
        self.last_error = ""

    def _get(self, path: str, timeout: float = 2.0):
        req = urllib.request.Request(self.url + path, headers={"Cache-Control": "no-store"})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode("utf-8"))

    def poll(self):
        state = parse_state_json(self._get("/api/state"))
        codes = parse_codes_json(self._get("/api/codes"))
        return state, codes

    def send(self, cmd: str):
        body = json.dumps({"cmd": cmd}).encode()
        req = urllib.request.Request(
            self.url + "/api/cmd", data=body, method="POST",
            headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=3.0) as r:
            return r.status

    def close(self):
        pass

    def describe(self) -> str:
        return f"{self.url}"


class SerialLink:
    name = "SERIAL"

    def __init__(self, port: str, baud: int = 115200):
        import serial  # 延迟导入:HTTP/TUI 不依赖 pyserial

        self.port = port
        self.ser = serial.Serial(port, baud, timeout=0.05)
        self.ser.write(b"\r\n")
        time.sleep(0.2)
        self.ser.reset_input_buffer()
        self.codes: list[dict] = []
        self.state = parse_state_json({})
        self._last_list = 0.0
        self._buf = b""

    def poll(self):
        now = time.time()
        if now - self._last_list > 5:
            self.ser.write(b"LIST\r\nINFO\r\n")
            self._last_list = now
        data = self.ser.read(4096)
        if data:
            self._buf += data
            while b"\n" in self._buf:
                raw, self._buf = self._buf.split(b"\n", 1)
                line = raw.decode("utf-8", "replace").strip()
                if not line:
                    continue
                m = re.search(r"# RF-Lab fw=(\S+)", line)
                if m:
                    self.state["fw"] = m.group(1)
                m = re.search(r"# module UART1 (\d+)", line)
                if m:
                    self.state["baud"] = int(m.group(1))
                code = parse_list_line(line)
                if code:
                    self._upsert(code)
                    continue
                ev = parse_serial_event(line)
                if not ev:
                    continue
                k = ev["kind"]
                if k == "learn":
                    self._upsert({"id": ev["id"], "band": ev["band"], "code": ev["code"]})
                    self.state["last_kind"] = "learn"
                    self.state["last_id"] = ev["id"]
                    self.state["last_band"] = ev["band"]
                    self.state["last_code"] = ev["code"]
                elif k == "rx":
                    self.state["last_kind"] = "rx"
                    self.state["last_id"] = ev["id"]
                    self.state["last_band"] = ev["band"]
                    self.state["last_code"] = ev["code"]
                elif k == "tx":
                    self.state["tx_active"] = ev["tx"] == "long" and bool(ev["ok"])
                    self.state["tx_kind"] = ev["tx"]
                elif k == "counters":
                    self.state.update({
                        "count": ev["store"], "max": ev["max"],
                        "frames": ev["frames"], "acks": ev["acks"], "stray": ev["stray"],
                    })
                elif k == "net":
                    self.state.update({
                        "ap_ssid": ev["ap_ssid"], "ap_ip": ev["ap_ip"],
                        "sta_connected": ev["sta"] != "-", "sta_ssid": ev["sta"],
                        "sta_ip": ev["sta_ip"], "rssi": ev["rssi"],
                    })
        return dict(self.state), list(self.codes)

    def _upsert(self, code: dict):
        for i, c in enumerate(self.codes):
            if c["id"] == code["id"]:
                self.codes[i] = code
                return
        self.codes.append(code)
        self.codes.sort(key=lambda c: c["id"])

    def send(self, cmd: str):
        self.ser.write((cmd + "\r\n").encode())
        return 202

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def describe(self) -> str:
        return self.port


class MockLink:
    name = "MOCK"

    def __init__(self):
        self.codes = [
            {"id": 1, "band": "315M", "code": "5555034C"},
            {"id": 2, "band": "315M", "code": "5555033C"},
            {"id": 3, "band": "315M", "code": "5555034B"},
            {"id": 4, "band": "433M", "code": "A1B2C3D4"},
            {"id": 5, "band": "433M", "code": "5555033C"},
        ]
        self.state = parse_state_json({
            "fw": "0.6.0", "uptime_ms": 3725000,
            "net": {"mode": "apsta", "ap_on": True, "has_creds": True,
                    "ap_ssid": "RF-Lab-AB12", "ap_ip": "192.168.4.1",
                    "sta_connected": True, "sta_ssid": "MyHome-5G",
                    "sta_ip": "192.168.5.42", "rssi": -57},
            "module": {"baud": 9600, "frames": 37, "acks": 15, "stray": 0},
            "store": {"count": 5, "max": 32},
            "oled": {"present": True, "on": True},
            "tx": {"active": False, "kind": "idle"},
            "last": {"kind": "learn", "id": 5, "band": "315M", "code": "5555034B"},
        })
        self.sent: list[str] = []

    def poll(self):
        return dict(self.state), list(self.codes)

    def send(self, cmd: str):
        self.sent.append(cmd)
        return 202

    def close(self):
        pass

    def describe(self) -> str:
        return "MOCK / NO DEVICE"


# ---------------- TUI ----------------

@dataclass
class Snapshot:
    state: dict = field(default_factory=lambda: parse_state_json({}))
    codes: list[dict] = field(default_factory=list)
    link_ok: bool = True
    link_error: str = ""


class App:
    def __init__(self, link, refresh_s: float = 1.0):
        self.link = link
        self.refresh_s = refresh_s
        self.lock = threading.Lock()
        self.snap = Snapshot()
        self.logs: list[tuple[str, str, str]] = []  # (time, kind, text)
        self.armed = False
        self.selected = 0
        self.running = True
        self.show_help = False
        self.last_cmd = ""
        self.last_msg = ""
        self.last_msg_at = 0.0
        self.pending_delete: tuple[int, float] | None = None
        self._poll_thread = threading.Thread(target=self._poll_loop, daemon=True)
        self._last_signature = None

    # -------- background --------
    def start(self):
        self._poll_thread.start()

    def stop(self):
        self.running = False
        try:
            self.link.close()
        except Exception:
            pass

    def _poll_loop(self):
        while self.running:
            try:
                state, codes = self.link.poll()
                with self.lock:
                    self.snap.state = state
                    self.snap.codes = codes
                    self.snap.link_ok = True
                    self.snap.link_error = ""
                    if self.selected >= len(codes):
                        self.selected = max(0, len(codes) - 1)
                    self._note_changes(state, codes)
            except Exception as e:
                with self.lock:
                    self.snap.link_ok = False
                    self.snap.link_error = str(e)
            time.sleep(self.refresh_s)

    def _note_changes(self, state: dict, codes: list[dict]):
        sig = (state.get("count"), state.get("last_kind"), state.get("last_id"),
               state.get("last_code"), state.get("tx_active"),
               state.get("tx_ok"), state.get("tx_ms"), state.get("tx_id"))
        if sig == self._last_signature:
            return
        self._last_signature = sig
        k = state.get("last_kind")
        if k in ("learn", "rx") and state.get("last_code"):
            self._log(k, f"{k.upper()} #{state.get('last_id')} "
                          f"{state.get('last_band')} {state.get('last_code')}")
        if state.get("tx_active"):
            self._log("tx", "LONG TX ACTIVE")
        if state.get("tx_ms", 0) > 0 and state.get("tx_id"):
            ok = state.get("tx_ok")
            self._log("ok" if ok else "warn",
                      f"TX {state.get('tx_kind', '?')} #{state.get('tx_id')} "
                      f"{'OK' if ok else 'WARN'} {state.get('tx_ms')}ms")

    def _log(self, kind: str, text: str):
        self.logs.append((time.strftime("%H:%M:%S"), kind, text))
        if len(self.logs) > LOG_MAX:
            del self.logs[: len(self.logs) - LOG_MAX]

    def log(self, kind: str, text: str):
        with self.lock:
            self._log(kind, text)

    # -------- commands --------
    def send(self, cmd: str):
        if cmd.startswith(("SEND", "LONG")) and not self.armed:
            self.message("ARM first: press A", warn=True)
            return
        try:
            self.link.send(cmd)
            self.last_cmd = cmd
            self.log("cmd", "> " + cmd)
            self.message("queued: " + cmd)
        except Exception as e:
            self.message("send failed: " + str(e), warn=True)
            self.log("warn", "ERR " + str(e))

    def message(self, text: str, warn: bool = False):
        self.last_msg = text
        self.last_msg_at = time.time()
        if warn:
            self.log("warn", text)

    # -------- key handling --------
    def handle_key(self, ch: int) -> bool:
        if self.show_help:
            self.show_help = False
            return True
        if ch in (ord("q"), ord("Q")):
            return False
        if ch in (curses.KEY_UP, ord("k")):
            self.move(-1)
        elif ch in (curses.KEY_DOWN, ord("j")):
            self.move(1)
        elif ch in (curses.KEY_ENTER, 10, 13):
            self.send_selected("SEND")
        elif ch in (ord("l"), ord("L")):
            self.send_selected("LONG")
        elif ch in (ord("s"), ord("S")):
            self.stop()
        elif ch in (ord("d"), ord("D")):
            self.erase()
        elif ch in (ord("a"), ord("A")):
            self.armed = not self.armed
            self.message("ARMED" if self.armed else "DISARMED")
        elif ch in (ord("r"), ord("R")):
            self.message("refresh")
        elif ch in (ord("t"), ord("T")):
            self.send("DISP TEST 30")
        elif ch == ord("?"):
            self.show_help = True
        return True

    def current_id(self) -> int:
        with self.lock:
            if not self.snap.codes:
                return 0
            return self.snap.codes[min(self.selected, len(self.snap.codes) - 1)]["id"]

    def send_selected(self, kind: str):
        cid = self.current_id()
        if not cid:
            self.message("NO CODES // 先按遥控器学码", warn=True)
            return
        self.send(f"{kind} {cid}")

    def move(self, delta: int):
        with self.lock:
            n = len(self.snap.codes)
            if n:
                self.selected = (self.selected + delta + n) % n

    def stop(self):
        with self.lock:
            sid = (self.snap.state.get("tx_id") or self.snap.state.get("last_id")
                   or self.current_id())
        if sid:
            self.send("STOP " + str(sid))

    def erase(self):
        cid = self.current_id()
        if not cid:
            return
        now = time.time()
        if self.pending_delete and self.pending_delete[0] == cid and now - self.pending_delete[1] < 3:
            self.pending_delete = None
            self.send("ERASE " + str(cid))
        else:
            self.pending_delete = (cid, now)
            self.message(f"press D again to ERASE #{cid}", warn=True)

    # -------- drawing --------
    def draw(self, stdscr):
        stdscr.erase()
        h, w = stdscr.getmaxyx()
        if h < 16 or w < 64:
            self._draw_tiny(stdscr, h, w)
            stdscr.refresh()
            return

        amber = curses.color_pair(C_AMBER) | curses.A_BOLD
        bone = curses.color_pair(C_BONE)
        khaki = curses.color_pair(C_KHAKI)
        muted = curses.color_pair(C_MUTED)
        green = curses.color_pair(C_GREEN)
        red = curses.color_pair(C_RED)
        panel = curses.color_pair(C_PANEL)

        # header
        with self.lock:
            st = self.snap.state
            link_ok = self.snap.link_ok
        title = "RF-LAB // FIELD TERMINAL"
        clock = time.strftime("%H:%M:%S")
        self._put(stdscr, 0, 1, title, amber)
        self._put(stdscr, 0, max(1, w - len(clock) - 2), clock, amber)
        self._hline(stdscr, 1, 1, w - 2, panel)

        link_txt = f"LINK {self.link.name} {self.link.describe()}"
        self._put(stdscr, 1, 2, link_txt[: w - 40], green if link_ok else red)
        arm = "ARMED" if self.armed else "DISARMED"
        self._put(stdscr, 1, max(2, w - len(arm) - 16), f"ARM {arm}",
                  amber if self.armed else muted)
        self._put(stdscr, 1, max(2, w - 34), f"FW {st.get('fw', '?')}", khaki)

        # body 62/38 split;底部留出事件台(小屏也要能看到日志)
        top = 2
        log_h = max(5, min(9, h // 4))
        bottom = h - log_h - 3
        split = max(30, int(w * 0.58))
        self._box(stdscr, top, 1, bottom - top + 1, split - 2, "CODEBOOK // 码库", amber, panel)
        self._box(stdscr, top, split, bottom - top + 1, w - split - 1,
                  "ACQUISITION // 捕获", amber, panel)

        self._draw_codes(stdscr, top + 2, 3, bottom - top - 3, split - 6, amber, bone, khaki, muted)
        self._draw_hero(stdscr, top + 2, split + 2, bottom - top - 3, w - split - 4,
                        amber, bone, khaki, muted, green, red, st)

        # log(横跨全宽;屏高不够时至少保留 5 行)
        log_top = bottom + 1
        if log_top < h - 2:
            self._hline(stdscr, log_top, 1, w - 2, panel)
            self._put(stdscr, log_top, 2, "EVENT LOG // 事件台", amber)
            self._draw_log(stdscr, log_top + 1, h - 2, w)

        # footer
        self._put(stdscr, h - 2, 1, " " * (w - 2), muted)
        x = 2
        for key, desc in KEY_ROWS:
            if x + len(key) + len(desc) + 4 >= w:
                break
            self._put(stdscr, h - 2, x, key, amber)
            x += len(key) + 1
            self._put(stdscr, h - 2, x, desc + "  ", khaki)
            x += len(desc) + 2

        if self.last_msg and time.time() - self.last_msg_at < 4:
            self._put(stdscr, h - 1, 1, " " * (w - 2), muted)
            self._put(stdscr, h - 1, 2, self.last_msg[: w - 4],
                      red if "ARM" in self.last_msg or "failed" in self.last_msg else amber)

        if self.show_help:
            self._draw_help(stdscr, h, w, amber, bone, khaki, panel)
        stdscr.refresh()

    def _draw_tiny(self, stdscr, h, w):
        self._put(stdscr, 0, 0, "RF-LAB // TERMINAL", curses.color_pair(C_AMBER))
        with self.lock:
            for i, c in enumerate(self.snap.codes[: max(1, h - 4)]):
                self._put(stdscr, i + 1, 0, f"{c['id']:2d} {c['band']} {c['code']}"[:w], curses.color_pair(C_BONE))
        self._put(stdscr, h - 1, 0, "Q quit  ? help", curses.color_pair(C_KHAKI))

    def _draw_codes(self, stdscr, y, x, h, w, amber, bone, khaki, muted):
        with self.lock:
            codes = list(self.snap.codes)
            sel = self.selected
        if not codes:
            self._put(stdscr, y, x, "NO CODES // 按一下原遥控器", muted)
            return
        start = 0
        if sel >= h:
            start = sel - h + 1
        for i, c in enumerate(codes[start:start + h]):
            idx = start + i
            mark = ">" if idx == sel else " "
            line = f"{mark} {c['id']:02d}  {c['band']:>4}  {c['code']}"
            self._put(stdscr, y + i, x, line[:w], amber if idx == sel else bone)
            if idx == sel:
                self._put(stdscr, y + i, x + len(line), " " * max(0, w - len(line)), amber)

    def _draw_hero(self, stdscr, y, x, h, w, amber, bone, khaki, muted, green, red, st):
        with self.lock:
            codes = list(self.snap.codes)
            sel = self.selected
        cur = codes[min(sel, len(codes) - 1)] if codes else None
        code = (cur or {}).get("code", "--------")
        band = (cur or {}).get("band", "--")
        self._put(stdscr, y, x, "LAST SIGNAL // 最近信号", muted)
        self._put(stdscr, y + 1, x, code[:w], amber | curses.A_BOLD)
        self._put(stdscr, y + 2, x, f"{band} // ID {cur['id'] if cur else '--'}", khaki)

        self._put(stdscr, y + 4, x, "TX CONTROL", muted)
        self._put(stdscr, y + 5, x, "[ENTER] SEND   [L] LONG", bone)
        self._put(stdscr, y + 6, x, "[S] STOP       [A] " + ("DISARM" if self.armed else "ARM"), bone)

        if st.get("tx_active"):
            self._put(stdscr, y + 8, x, "LONG TX ACTIVE // 长发中", red)
        else:
            self._put(stdscr, y + 8, x, "STANDBY // 待发", green)

        # telemetry at bottom of hero;小屏只保留两行,避免压到日志框
        if h >= 16:
            ty = y + h - 8
            self._put(stdscr, ty, x, "TELEMETRY", muted)
            self._put(stdscr, ty + 1, x, f"STORE  {st.get('count', 0)}/{st.get('max', 32)}", khaki)
            self._put(stdscr, ty + 2, x, f"FRAMES {st.get('frames', 0)}  ACKS {st.get('acks', 0)}", khaki)
            self._put(stdscr, ty + 3, x, f"STRAY  {st.get('stray', 0)}  BAUD {st.get('baud', 0)}", khaki)
            up = st.get("sta_ip") if st.get("sta_connected") else st.get("ap_ip", "")
            self._put(stdscr, ty + 4, x, f"LINK   {st.get('ap_ssid', '?')} {up}", khaki)
            self._put(stdscr, ty + 5, x, f"UPTIME {format_uptime(st.get('uptime_ms', 0))}", muted)
        else:
            self._put(stdscr, y + 10, x, f"STORE {st.get('count', 0)}/{st.get('max', 32)}  "
                                          f"F{st.get('frames', 0)} A{st.get('acks', 0)}", khaki)

    def _draw_log(self, stdscr, y, h, w):
        with self.lock:
            logs = list(self.logs)[-(h - y):]
        for i, (ts, kind, text) in enumerate(logs):
            color = curses.color_pair(C_KHAKI)
            if kind in ("learn", "rx"):
                color = curses.color_pair(C_BONE)
            elif kind in ("ok", "tx"):
                color = curses.color_pair(C_GREEN)
            elif kind == "warn":
                color = curses.color_pair(C_RED)
            elif kind == "cmd":
                color = curses.color_pair(C_AMBER)
            self._put(stdscr, y + i, 2, f"{ts}  {text}"[: w - 4], color)

    def _draw_help(self, stdscr, h, w, amber, bone, khaki, panel):
        bw = min(78, w - 8)
        bh = min(len(HELP_ROWS) + 5, h - 6)
        y = (h - bh) // 2
        x = (w - bw) // 2
        self._box(stdscr, y, x, bh, bw, "FIELD MANUAL // 键位", amber, panel)
        for i, (key, desc) in enumerate(HELP_ROWS[: bh - 4]):
            self._put(stdscr, y + 2 + i, x + 2, key.ljust(12)[:12], amber)
            self._put(stdscr, y + 2 + i, x + 15, desc[: bw - 17], bone)
        self._put(stdscr, y + bh - 2, x + 2, "any key closes", khaki)

    # -------- low-level --------
    @staticmethod
    def _put(stdscr, y: int, x: int, text: str, attr=0):
        h, w = stdscr.getmaxyx()
        if y < 0 or y >= h or x < 0 or x >= w:
            return
        text = text[: max(0, w - x - 1)]
        try:
            stdscr.addstr(y, x, text, attr)
        except curses.error:
            pass

    @staticmethod
    def _hline(stdscr, y: int, x: int, n: int, attr):
        h, w = stdscr.getmaxyx()
        if y < 0 or y >= h:
            return
        try:
            stdscr.hline(y, x, curses.ACS_HLINE, max(0, min(n, w - x - 1)), attr)
        except curses.error:
            pass

    def _box(self, stdscr, y, x, h, w, title, amber, panel):
        if h < 3 or w < 8:
            return
        hh, ww = stdscr.getmaxyx()
        if y < 0 or x < 0 or y + h > hh or x + w > ww:
            return
        # 直接画在 stdscr 上:newwin 会盖住后续 _put 的内容。
        self._hline(stdscr, y, x + 1, w - 2, panel)
        self._hline(stdscr, y + h - 1, x + 1, w - 2, panel)
        try:
            stdscr.vline(y + 1, x, curses.ACS_VLINE, h - 2, panel)
            stdscr.vline(y + 1, x + w - 1, curses.ACS_VLINE, h - 2, panel)
            stdscr.addch(y, x, curses.ACS_ULCORNER, panel)
            stdscr.addch(y, x + w - 1, curses.ACS_URCORNER, panel)
            stdscr.addch(y + h - 1, x, curses.ACS_LLCORNER, panel)
            stdscr.addch(y + h - 1, x + w - 1, curses.ACS_LRCORNER, panel)
            self._put(stdscr, y, x + 2, " " + title + " ", amber)
        except curses.error:
            pass


def setup_colors():
    curses.start_color()
    try:
        curses.use_default_colors()
        bg = -1
    except curses.error:
        bg = curses.COLOR_BLACK
    for idx, (r, g, b) in PALETTE.items():
        if curses.can_change_color() and curses.COLORS >= 256:
            try:
                curses.init_color(idx + 8, r * 1000 // 255, g * 1000 // 255, b * 1000 // 255)
                curses.init_pair(idx, idx + 8, bg)
                continue
            except curses.error:
                pass
        simple = {
            C_AMBER: curses.COLOR_YELLOW,
            C_BONE: curses.COLOR_WHITE,
            C_KHAKI: curses.COLOR_YELLOW,
            C_MUTED: curses.COLOR_WHITE,
            C_GREEN: curses.COLOR_GREEN,
            C_RED: curses.COLOR_RED,
            C_PANEL: curses.COLOR_YELLOW,
        }[idx]
        curses.init_pair(idx, simple, bg)


def run(stdscr, app: App):
    try:
        curses.curs_set(0)
    except curses.error:
        pass
    stdscr.nodelay(True)
    stdscr.timeout(100)
    setup_colors()
    app.start()
    try:
        while app.running:
            app.draw(stdscr)
            try:
                ch = stdscr.getch()
            except curses.error:
                ch = -1
            if ch == -1:
                continue
            if ch == curses.KEY_RESIZE:
                continue
            if not app.handle_key(ch):
                break
    finally:
        app.stop()


def build_link(args):
    if args.mock:
        return MockLink()
    if args.port:
        return SerialLink(args.port, args.baud)
    return HttpLink(args.url)


def main(argv=None):
    ap = argparse.ArgumentParser(description="RF-Lab uConsole TUI")
    ap.add_argument("--url", default=os.environ.get("RFLAB_URL", "http://192.168.4.1"),
                    help="HTTP base URL (default http://192.168.4.1)")
    ap.add_argument("--port", help="serial port fallback, e.g. /dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--mock", action="store_true", help="no device; demo/test")
    ap.add_argument("--refresh", type=float, default=1.0, help="poll interval seconds")
    ap.add_argument("--dump", action="store_true", help="print one state snapshot and exit")
    args = ap.parse_args(argv)

    try:
        link = build_link(args)
    except Exception as e:
        print(f"cannot open transport: {e}", file=sys.stderr)
        return 2

    if args.dump:
        state, codes = link.poll()
        print(json.dumps({"state": state, "codes": codes}, ensure_ascii=False, indent=2))
        link.close()
        return 0

    app = App(link, refresh_s=args.refresh)
    try:
        curses.wrapper(run, app)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
