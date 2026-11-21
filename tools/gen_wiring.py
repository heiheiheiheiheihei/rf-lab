#!/usr/bin/env python3
"""Generate the wiring contract: C header, docs tables and abstract SVG plates.

Single source: hardware/wiring.json
Outputs:
  firmware/src/wiring_config.h   pin/baud macros used by main.c
  hardware/README.md             block between WIRING:START/END
  docs/00-mvp.md                 block between WIRING:START/END
  web/art/hw-assembly.svg        wide abstract wiring plate
  web/art/hw-assembly-m.svg      portrait abstract wiring plate
  web/art/hw-core.svg            abstract CORE-C3 pin map
  web/art/hw-rf.svg              abstract YS-RF34DL pin map

Run:  python3 tools/gen_wiring.py
Check: python3 tools/gen_wiring.py --check
"""
from __future__ import annotations

import argparse
import html
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "hardware" / "wiring.json"
HEADER = ROOT / "firmware" / "src" / "wiring_config.h"
MAIN_C = ROOT / "firmware" / "src" / "main.c"
HW_README = ROOT / "hardware" / "README.md"
MVP = ROOT / "docs" / "00-mvp.md"
ART = ROOT / "web" / "art"

# ---- palette ----------------------------------------------------------------
INK = "#070906"
BASE = "#0b0e08"
PANEL = "#0d120b"
PANEL2 = "#11160d"
LINE = "#303823"
LINE2 = "#46522f"
AMBER = "#ffb000"
AMBER_HI = "#ffd24a"
BONE = "#ece6d4"
KHAKI = "#b4ae96"
MUTED = "#817d6b"
GREEN = "#9bf05c"
RED = "#ff5d50"
TEAL = "#173029"
COPPER = "#c98a4b"

MONO = "'JetBrains Mono','Cascadia Mono','DejaVu Sans Mono',monospace"
DISP = "'Saira Condensed','Roboto Condensed','Arial Narrow',sans-serif"

DIR_ZH = {
    "power": "电源",
    "ground": "地",
    "esp32->module": "ESP32 → 模块",
    "module->esp32": "模块 → ESP32",
}
DIR_SHORT = {
    "power": "POWER",
    "ground": "GND",
    "esp32->module": "→",
    "module->esp32": "←",
}
WIRE = {"amber": AMBER, "khaki": KHAKI, "green": GREEN}


def esc(s: object) -> str:
    return html.escape(str(s), quote=True)


def load() -> dict:
    return json.loads(SRC.read_text(encoding="utf-8"))


def validate(d: dict) -> None:
    pins = d["pins"]
    expect_pins = {
        "console_tx": 21, "console_rx": 20,
        "module_tx": 3, "module_rx": 4,
        "oled_sda": 5, "oled_scl": 6,
    }
    if pins != expect_pins:
        raise SystemExit(f"pins mismatch: {pins!r} != {expect_pins!r}")
    if d.get("uart_port") != 1:
        raise SystemExit("uart_port must be 1 (UART1)")

    by_net = {c["net"]: c for c in d["connections"]}
    expected = {
        "3V3": ("3V3", "VCC", "power"),
        "GND": ("GND", "GND", "ground"),
        "UART1_TX": ("GPIO3", "RXD", "esp32->module"),
        "UART1_RX": ("GPIO4", "TXD", "module->esp32"),
    }
    if set(by_net) != set(expected):
        raise SystemExit(f"nets {sorted(by_net)} != {sorted(expected)}")
    for net, (e, m, direction) in expected.items():
        c = by_net[net]
        got = (c["esp32"], c["module"], c["direction"])
        if got != (e, m, direction):
            raise SystemExit(f"{net}: {got} != {(e, m, direction)}")
    if by_net["3V3"]["module"] != "VCC" or "3.3V" not in by_net["3V3"]["note"]:
        raise SystemExit("3V3 row must feed VCC and warn 3.3V only")
    if "5V" not in by_net["3V3"]["note"]:
        raise SystemExit("3V3 row must mention 5V")
    if "ground" not in by_net["GND"]["note"].lower():
        raise SystemExit("GND row must mention common ground")

    reserved = " ".join(r["pins"] for r in d["reserved"])
    for used in ("3V3", "GND", "GPIO3", "GPIO4"):
        if used in reserved:
            raise SystemExit(f"used pin {used} is also listed reserved")
    for gpio in ("5", "6", "18", "19", "20", "21", "8", "9", "2"):
        if f"GPIO{gpio}" not in reserved:
            raise SystemExit(f"reserved list missing GPIO{gpio}")

    if d["constants"]["module_baud"] != 9600:
        raise SystemExit("module baud must be 9600")
    if d["constants"]["oled_i2c_hz"] != 400000:
        raise SystemExit("OLED I2C must be 400000")


def check_main_c() -> None:
    src = MAIN_C.read_text(encoding="utf-8")
    if '#include "wiring_config.h"' not in src:
        raise SystemExit("main.c must include wiring_config.h")
    for macro in ("PIN_CONSOLE_TX", "PIN_CONSOLE_RX", "PIN_MODULE_TX", "PIN_MODULE_RX",
                  "PIN_OLED_SDA", "PIN_OLED_SCL", "CONSOLE_BAUD", "MODULE_BAUD_DEFAULT",
                  "OLED_I2C_HZ"):
        if re.search(rf"^#define\s+{macro}\b", src, re.M):
            raise SystemExit(f"main.c still defines {macro}; use wiring_config.h")
    if not re.search(r"^#define\s+MODULE_UART\s+UART_NUM_1\b", src, re.M):
        raise SystemExit("main.c must keep MODULE_UART UART_NUM_1")
    # The macros must be used in the correct order: ESP32 TX -> module RX,
    # ESP32 RX <- module TX. A swapped call would still compile.
    if not re.search(r"uart_set_pin\(MODULE_UART,\s*PIN_MODULE_TX,\s*PIN_MODULE_RX", src):
        raise SystemExit("main.c must call uart_set_pin(MODULE_UART, PIN_MODULE_TX, PIN_MODULE_RX, ...)")
    if not re.search(r"gpio_set_pull_mode\(PIN_MODULE_RX", src):
        raise SystemExit("main.c must pull up PIN_MODULE_RX")
    if not re.search(r"oled_init\(PIN_OLED_SDA,\s*PIN_OLED_SCL", src):
        raise SystemExit("main.c must call oled_init(PIN_OLED_SDA, PIN_OLED_SCL, ...)")
    if not re.search(r"uart_set_pin\(UART_NUM_0,\s*PIN_CONSOLE_TX,\s*PIN_CONSOLE_RX", src):
        raise SystemExit("main.c must call uart_set_pin(UART_NUM_0, PIN_CONSOLE_TX, PIN_CONSOLE_RX, ...)")


def header_text(d: dict) -> str:
    p, c = d["pins"], d["constants"]
    return f'''/* AUTO-GENERATED by tools/gen_wiring.py from hardware/wiring.json.
 * Do not edit by hand. Pin changes go in the JSON, then run:
 *   python3 tools/gen_wiring.py
 */
#pragma once

/* console: native USB + UART0 fallback (GPIO20/21) */
#define CONSOLE_BAUD {c["console_baud"]}
#define PIN_CONSOLE_TX {p["console_tx"]}
#define PIN_CONSOLE_RX {p["console_rx"]}

/* YS-RF34DL: UART1, 9600 8N1, crossed TX/RX.
 * GPIO3 = ESP32 TX -> module RXD; GPIO4 = ESP32 RX <- module TXD. */
#define MODULE_BAUD_DEFAULT {c["module_baud"]}
#define PIN_MODULE_TX {p["module_tx"]}
#define PIN_MODULE_RX {p["module_rx"]}

/* on-board 0.42" SSD1306 72x40, I2C address 0x3C */
#define PIN_OLED_SDA {p["oled_sda"]}
#define PIN_OLED_SCL {p["oled_scl"]}
#define OLED_I2C_HZ {c["oled_i2c_hz"]}
'''


def table_block(d: dict, compact: bool = False) -> str:
    rows = []
    for c in d["connections"]:
        if compact:
            rows.append(f"| {c['module']} | `{c['esp32']}` | {c.get('note_zh', c['note'])} |")
        else:
            rows.append(
                f"| **{c['module']}** | `{c['esp32']}` | {DIR_ZH[c['direction']]} | {c.get('note_zh', c['note'])} |"
            )
    if compact:
        head = "| 模块脚 | 接到 ESP32-C3 | 说明 |\n|---|---|---|"
    else:
        head = "| 模块引脚 | 接到 ESP32-C3 | 方向 | 说明 |\n|---|---|---|---|"
    return "<!-- WIRING:START -->\n<!-- generated from hardware/wiring.json; do not edit -->\n" + head + "\n" + "\n".join(rows) + "\n<!-- WIRING:END -->"


def replace_block(text: str, block: str, path: pathlib.Path) -> str:
    start = "<!-- WIRING:START -->"
    end = "<!-- WIRING:END -->"
    a = text.find(start)
    b = text.find(end)
    if a < 0 or b < 0 or b < a:
        raise SystemExit(f"{path}: missing WIRING markers")
    return text[:a] + block + text[b + len(end):]


# ---- SVG helpers ------------------------------------------------------------
def svg_open(w: int, h: int, uid: str, title: str, desc: str) -> str:
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" role="img" aria-labelledby="{uid}-title {uid}-desc">
<title id="{uid}-title">{esc(title)}</title>
<desc id="{uid}-desc">{esc(desc)}</desc>
<defs>
  <pattern id="{uid}-grid" width="28" height="28" patternUnits="userSpaceOnUse">
    <path d="M28 0H0V28" fill="none" stroke="{AMBER}" stroke-opacity=".05"/>
  </pattern>
  <marker id="{uid}-arr-green" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">
    <path d="M0 0L10 5L0 10Z" fill="{GREEN}"/>
  </marker>
  <marker id="{uid}-arr-bone" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">
    <path d="M0 0L10 5L0 10Z" fill="{BONE}"/>
  </marker>
</defs>
<style>
  .w-mono{{font-family:{MONO}}}
  .w-disp{{font-family:{DISP}}}
  .w-cjk{{font-family:"Noto Sans SC","PingFang SC","Microsoft YaHei",sans-serif}}
</style>
<rect width="{w}" height="{h}" fill="{INK}"/>
<rect width="{w}" height="{h}" fill="url(#{uid}-grid)"/>
<rect x="1" y="1" width="{w-2}" height="{h-2}" fill="none" stroke="{LINE}"/>
<g fill="none" stroke="{AMBER}" stroke-opacity=".55">
  <path d="M16 38V16H38"/><path d="M{w-38} 16H{w-16}V38"/>
  <path d="M16 {h-38}V{h-16}H38"/><path d="M{w-38} {h-16}H{w-16}V{h-38}"/>
</g>
'''


def svg_close(w: int, h: int, note: str = "GENERATED FROM hardware/wiring.json / DO NOT EDIT") -> str:
    return f'''<line x1="28" y1="{h-38}" x2="{w-28}" y2="{h-38}" stroke="{LINE}"/>
<text x="30" y="{h-18}" class="w-mono" font-size="6.4" fill="{MUTED}" letter-spacing=".4">{esc(note)}</text>
<text x="{w-30}" y="{h-18}" text-anchor="end" class="w-mono" font-size="6.4" fill="{MUTED}" letter-spacing=".4">WIRING // {esc('v1')} // 2026-11-21</text>
</svg>'''


def txt(x, y, s, size=9, fill=KHAKI, cls="w-mono", anchor="start", weight="normal", ls="0", opacity="1"):
    return (f'<text x="{x}" y="{y}" class="{cls}" font-size="{size}" fill="{fill}" '
            f'text-anchor="{anchor}" font-weight="{weight}" letter-spacing="{ls}" opacity="{opacity}">{esc(s)}</text>')


def block(x, y, w, h, title, sub, right, accent=AMBER):
    return f'''<g>
  <rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{PANEL}" stroke="{LINE2}"/>
  <rect x="{x}" y="{y}" width="{w}" height="42" fill="{PANEL2}" stroke="{LINE}"/>
  <rect x="{x}" y="{y}" width="3" height="{h}" fill="{accent}" opacity=".75"/>
  {txt(x+14,y+24,title,20,BONE,"w-disp","start","700",".5")}
  {txt(x+14,y+36,sub,7.2,KHAKI,"w-mono","start","normal",".5")}
  {txt(x+w-12,y+16,right,6.4,MUTED,"w-mono","end","normal","1")}
</g>'''


def pin(x, y, side, label, role, color=AMBER, used=True, label_dy=None, role_dy=None):
    """Pin circle on a block edge; label sits inside the block."""
    if side == "right":
        lx, anchor, tx = x-10, "end", x-20
        line = f'<line x1="{x-16}" y1="{y}" x2="{x}" y2="{y}" stroke="{color}" stroke-opacity=".55"/>'
    elif side == "left":
        lx, anchor, tx = x+10, "start", x+22
        line = f'<line x1="{x+16}" y1="{y}" x2="{x}" y2="{y}" stroke="{color}" stroke-opacity=".55"/>'
    elif side == "bottom":
        lx, anchor, tx = x, "middle", x
        line = f'<line x1="{x}" y1="{y-16}" x2="{x}" y2="{y}" stroke="{color}" stroke-opacity=".55"/>'
    else:  # top
        lx, anchor, tx = x, "middle", x
        line = f'<line x1="{x}" y1="{y+16}" x2="{x}" y2="{y}" stroke="{color}" stroke-opacity=".55"/>'
    fill = color if used else PANEL2
    s = line
    s += f'<circle cx="{x}" cy="{y}" r="4.2" fill="{fill}" stroke="{color}" stroke-width="1.2"/>'
    if side in ("right", "left"):
        s += txt(tx, y+2.6, label, 8.2, color, "w-mono", anchor, "700", ".2")
        s += txt(tx, y-9, role, 6.2, MUTED, "w-mono", anchor, "normal", ".2")
    else:
        ly = y + (label_dy if label_dy is not None else (-12 if side == "bottom" else 14))
        ry = y + (role_dy if role_dy is not None else (-22 if side == "bottom" else 24))
        s += txt(tx, ly, label, 8.2, color, "w-mono", anchor, "700", ".2")
        s += txt(tx, ry, role, 6.2, MUTED, "w-mono", anchor, "normal", ".2")
    return s


def wire(x1, x2, y, color, direction):
    s = f'<line x1="{x1}" y1="{y}" x2="{x2}" y2="{y}" stroke="{INK}" stroke-width="8"/>'
    s += f'<line x1="{x1}" y1="{y}" x2="{x2}" y2="{y}" stroke="{color}" stroke-width="2.6"/>'
    s += f'<line x1="{x1}" y1="{y-1}" x2="{x2}" y2="{y-1}" stroke="#ffffff" stroke-opacity=".16" stroke-width=".7"/>'
    if direction == "esp32->module":
        s += f'<path d="M{x2-11} {y-5}L{x2} {y}L{x2-11} {y+5}Z" fill="{color}"/>'
    elif direction == "module->esp32":
        s += f'<path d="M{x1+11} {y-5}L{x1} {y}L{x1+11} {y+5}Z" fill="{color}"/>'
    return s


def wire_v(x, y1, y2, color, direction):
    s = f'<line x1="{x}" y1="{y1}" x2="{x}" y2="{y2}" stroke="{INK}" stroke-width="8"/>'
    s += f'<line x1="{x}" y1="{y1}" x2="{x}" y2="{y2}" stroke="{color}" stroke-width="2.6"/>'
    if direction == "esp32->module":
        s += f'<path d="M{x-5} {y2-11}L{x} {y2}L{x+5} {y2-11}Z" fill="{color}"/>'
    elif direction == "module->esp32":
        s += f'<path d="M{x-5} {y1+11}L{x} {y1}L{x+5} {y1+11}Z" fill="{color}"/>'
    return s


def table(d, x, y, w, row_h=26, head_h=22, size=8.4):
    cols = [x+10, x+150, x+300, x+440, x+620]
    s = f'<rect x="{x}" y="{y}" width="{w}" height="{head_h+4*row_h}" fill="{BASE}" stroke="{LINE}"/>'
    s += f'<rect x="{x}" y="{y}" width="{w}" height="{head_h}" fill="{PANEL2}" stroke="{LINE}"/>'
    heads = ["ESP32-C3", "NET", "YS-RF34DL", "DIR", "NOTE"]
    for cx, hh in zip(cols, heads):
        s += txt(cx, y+15, hh, 6.8, MUTED, "w-mono", "start", "normal", "1")
    for i, c in enumerate(d["connections"]):
        ry = y + head_h + i*row_h
        s += f'<line x1="{x}" y1="{ry}" x2="{x+w}" y2="{ry}" stroke="{LINE}" stroke-opacity=".55"/>'
        s += txt(cols[0], ry+17, c["esp32"], size, BONE if c["direction"] != "power" else AMBER, "w-mono", "start", "700")
        s += txt(cols[1], ry+17, c["net"], 7.4, MUTED, "w-mono", "start", "normal", ".3")
        s += txt(cols[2], ry+17, c["module"], size, BONE, "w-mono", "start", "700")
        s += txt(cols[3], ry+17, DIR_SHORT[c["direction"]], 9.5, GREEN if c["direction"].startswith(("esp32", "module")) else KHAKI, "w-mono", "start", "700")
        s += txt(cols[4], ry+17, c["note"], 7.2, KHAKI, "w-mono", "start")
    return s


def table_compact(d, x, y, w, row_h=24, head_h=20, size=8.0):
    cols = [x+10, x+160, x+300, x+390]
    s = f'<rect x="{x}" y="{y}" width="{w}" height="{head_h+4*row_h}" fill="{BASE}" stroke="{LINE}"/>'
    s += f'<rect x="{x}" y="{y}" width="{w}" height="{head_h}" fill="{PANEL2}" stroke="{LINE}"/>'
    heads = ["ESP32-C3", "YS-RF34DL", "DIR", "NOTE"]
    for cx, hh in zip(cols, heads):
        s += txt(cx, y+14, hh, 6.6, MUTED, "w-mono", "start", "normal", "1")
    for i, c in enumerate(d["connections"]):
        ry = y + head_h + i*row_h
        s += f'<line x1="{x}" y1="{ry}" x2="{x+w}" y2="{ry}" stroke="{LINE}" stroke-opacity=".55"/>'
        s += txt(cols[0], ry+16, c["esp32"], size, BONE if c["direction"] != "power" else AMBER, "w-mono", "start", "700")
        s += txt(cols[1], ry+16, c["module"], size, BONE, "w-mono", "start", "700")
        s += txt(cols[2], ry+16, DIR_SHORT[c["direction"]], 9.2, GREEN if c["direction"].startswith(("esp32", "module")) else KHAKI, "w-mono", "start", "700")
        s += txt(cols[3], ry+16, c["note"], 7.0, KHAKI, "w-mono", "start")
    return s


# ---- wide plate -------------------------------------------------------------
def render_wide(d: dict) -> str:
    w, h = 1200, 760
    uid = "w"
    s = svg_open(w, h, uid, "RF-LAB abstract wiring diagram: ESP32-C3 to YS-RF34DL",
                 "Abstract four-wire wiring diagram generated from hardware/wiring.json. ESP32-C3 3V3/GND/GPIO3/GPIO4 to YS-RF34DL VCC/GND/RXD/TXD.")
    s += txt(30, 32, "RF-LAB // WIRING PLATE", 7.2, MUTED, "w-mono", "start", "normal", "1.8")
    s += txt(w-30, 32, "FIG. 02 / WIRING", 7.2, AMBER, "w-mono", "end", "normal", "1.6")
    s += f'<line x1="30" y1="44" x2="{w-30}" y2="44" stroke="{LINE}"/>'
    s += txt(40, 90, "FOUR WIRES. ONE CHAIN.", 40, BONE, "w-disp", "start", "700", ".4")
    s += txt(41, 110, "CORE-C3 ↔ YS-RF34DL  //  UART1 9600 8N1  //  3.3V LOGIC", 8, KHAKI, "w-mono", "start", "normal", ".5")
    s += txt(w-40, 88, "● LIVE // 0.6.0", 7, GREEN, "w-mono", "end", "normal", "1.2")
    s += txt(w-40, 102, "GENERATED FROM hardware/wiring.json", 6.4, MUTED, "w-mono", "end", "normal", ".6")
    s += f'<line x1="30" y1="128" x2="{w-30}" y2="128" stroke="{LINE}"/>'

    # blocks
    s += block(46, 160, 440, 340, d["board"]["name"], f'{d["board"]["mcu"]}  //  {d["board"]["radio"]}', "MCU")
    s += block(714, 160, 440, 340, d["module"]["name"], f'{d["module"]["bands"]}  //  UART {d["module"]["uart"]}', "RF BAY 01", GREEN)
    # esp32 internals
    s += f'<rect x="76" y="226" width="220" height="86" fill="{TEAL}" stroke="{LINE2}"/>'
    s += txt(88, 248, d["board"]["mcu"], 17, BONE, "w-disp", "start", "700", ".5")
    s += txt(88, 263, d["board"]["radio"], 7.2, KHAKI, "w-mono")
    s += txt(88, 278, "USB " + d["board"]["usb"], 6.4, MUTED, "w-mono")
    s += f'<rect x="76" y="330" width="155" height="58" fill="{PANEL2}" stroke="{LINE}"/>'
    s += txt(88, 350, "OLED", 9, AMBER, "w-mono", "start", "700", ".8")
    s += txt(88, 364, "0.42\" 72x40", 7, BONE, "w-mono")
    s += txt(88, 376, "I2C 5/6 / 0x3C", 6.2, MUTED, "w-mono")
    s += f'<rect x="246" y="330" width="110" height="58" fill="{PANEL2}" stroke="{LINE}"/>'
    s += txt(258, 350, "USB-C", 9, AMBER, "w-mono", "start", "700", ".8")
    s += txt(258, 364, "5V IN", 7, BONE, "w-mono")
    s += txt(258, 376, "native USB", 6.2, MUTED, "w-mono")
    s += f'<rect x="76" y="404" width="280" height="28" fill="{AMBER}" fill-opacity=".04" stroke="{AMBER}" stroke-opacity=".35"/>'
    s += txt(88, 422, "3.3V ONLY  //  NEVER 5V INTO GPIO3/4", 7.2, AMBER, "w-mono", "start", "700", ".5")

    # module internals
    s += f'<rect x="748" y="226" width="170" height="86" fill="{TEAL}" stroke="{LINE2}"/>'
    s += txt(760, 248, d["module"]["mcu"], 17, BONE, "w-disp", "start", "700", ".5")
    s += txt(760, 263, "fixed-code codec", 7.2, KHAKI, "w-mono")
    s += txt(760, 278, d["module"]["bands"], 6.4, MUTED, "w-mono")
    s += f'<rect x="748" y="330" width="140" height="58" fill="{PANEL2}" stroke="{LINE}"/>'
    s += txt(760, 350, "RF", 9, GREEN, "w-mono", "start", "700", "1")
    s += txt(760, 364, "315 / 433", 7, BONE, "w-mono")
    s += txt(760, 376, "≤10 dBm", 6.2, MUTED, "w-mono")
    # antenna
    s += f'<g stroke="{COPPER}" stroke-width="2" fill="none"><path d="M1040 300V220"/><path d="M1040 220L1028 206M1040 220L1052 206"/></g>'
    for r in (26, 42, 58):
        s += f'<path d="M{1040+r} 300A{r} {r} 0 0 1 {1040} {300-r}" fill="none" stroke="{AMBER}" stroke-opacity=".28" stroke-dasharray="2 5"/>'
    s += txt(1078, 292, "ANT", 8, AMBER, "w-mono", "start", "700", "1")
    s += txt(1078, 304, d["module"]["antenna"], 6.4, KHAKI, "w-mono")

    # pins and wires
    ys = [264, 314, 364, 414]
    for c, y in zip(d["connections"], ys):
        color = WIRE[c["wire"]]
        s += pin(486, y, "right", c["esp32"], c["esp32_role"], color)
        s += pin(714, y, "left", c["module"], c["module_role"], color)
        s += wire(486, 714, y, color, c["direction"])
        label = f'{c["esp32"]}  →  {c["module"]}' if c["direction"] == "esp32->module" else (
                f'{c["esp32"]}  ←  {c["module"]}' if c["direction"] == "module->esp32" else
                f'{c["esp32"]}  →  {c["module"]}')
        s += txt(600, y-8, label, 8.6, color, "w-mono", "middle", "700", ".2")
        note = "3.3V ONLY" if c["direction"] == "power" else ("COMMON GROUND" if c["direction"] == "ground" else c["esp32_role"] + " // " + c["module_role"])
        s += txt(600, y+13, note, 6.2, MUTED if c["direction"] not in ("power",) else AMBER, "w-mono", "middle", "normal", ".3")

    s += txt(46, 540, "WIRING TABLE // 4 NETS", 7, AMBER, "w-mono", "start", "700", "1.4")
    s += txt(46, 552, "Reference table. Pin numbers come from firmware/src/wiring_config.h.", 6.4, MUTED, "w-mono")
    s += table(d, 46, 566, 1108, 26, 22, 8.4)
    s += svg_close(w, h)
    return s


# ---- portrait plate ---------------------------------------------------------
def render_portrait(d: dict) -> str:
    w, h = 720, 1200
    uid = "wm"
    s = svg_open(w, h, uid, "RF-LAB abstract wiring diagram, portrait",
                 "Portrait four-wire wiring diagram generated from hardware/wiring.json.")
    s += txt(24, 30, "RF-LAB // WIRING", 7, MUTED, "w-mono", "start", "normal", "1.8")
    s += txt(w-24, 30, "FIG. 02-M", 7, AMBER, "w-mono", "end", "normal", "1.4")
    s += f'<line x1="24" y1="42" x2="{w-24}" y2="42" stroke="{LINE}"/>'
    s += txt(30, 80, "FOUR WIRES.", 34, BONE, "w-disp", "start", "700", ".4")
    s += txt(30, 112, "ONE CHAIN.", 34, BONE, "w-disp", "start", "700", ".4")
    s += txt(30, 132, "CORE-C3 ↔ YS-RF34DL  //  3.3V UART", 7.6, KHAKI, "w-mono", "start", "normal", ".5")
    s += txt(w-30, 80, "● LIVE", 6.8, GREEN, "w-mono", "end", "normal", "1")
    s += txt(w-30, 94, "0.6.0", 6.2, MUTED, "w-mono", "end", "normal", ".8")
    s += f'<line x1="24" y1="146" x2="{w-24}" y2="146" stroke="{LINE}"/>'

    # ESP32 block
    s += block(24, 166, 672, 310, d["board"]["name"], f'{d["board"]["mcu"]}  //  {d["board"]["radio"]}', "MCU")
    s += f'<rect x="48" y="226" width="230" height="78" fill="{TEAL}" stroke="{LINE2}"/>'
    s += txt(60, 248, d["board"]["mcu"], 15, BONE, "w-disp", "start", "700", ".4")
    s += txt(60, 263, d["board"]["radio"], 6.8, KHAKI, "w-mono")
    s += txt(60, 276, "USB " + d["board"]["usb"], 6.2, MUTED, "w-mono")
    s += f'<rect x="300" y="226" width="150" height="78" fill="{PANEL2}" stroke="{LINE}"/>'
    s += txt(312, 246, "OLED", 8, AMBER, "w-mono", "start", "700", ".8")
    s += txt(312, 260, "0.42\" 72x40", 6.6, BONE, "w-mono")
    s += txt(312, 273, "I2C 5/6 / 0x3C", 6, MUTED, "w-mono")
    s += f'<rect x="470" y="226" width="202" height="78" fill="{PANEL2}" stroke="{LINE}"/>'
    s += txt(482, 246, "USB-C", 8, AMBER, "w-mono", "start", "700", ".8")
    s += txt(482, 260, "native USB", 6.6, BONE, "w-mono")
    s += txt(482, 273, "5V IN", 6, MUTED, "w-mono")
    s += f'<rect x="48" y="326" width="624" height="26" fill="{AMBER}" fill-opacity=".04" stroke="{AMBER}" stroke-opacity=".35"/>'
    s += txt(60, 343, "3.3V ONLY  //  NEVER 5V INTO GPIO3/4  //  COMMON GROUND", 7, AMBER, "w-mono", "start", "700", ".4")

    # module block
    s += block(24, 690, 672, 300, d["module"]["name"], f'{d["module"]["bands"]}  //  UART {d["module"]["uart"]}', "RF BAY 01", GREEN)
    s += f'<rect x="48" y="770" width="180" height="78" fill="{TEAL}" stroke="{LINE2}"/>'
    s += txt(60, 792, d["module"]["mcu"], 15, BONE, "w-disp", "start", "700", ".4")
    s += txt(60, 807, "fixed-code codec", 6.8, KHAKI, "w-mono")
    s += txt(60, 820, d["module"]["bands"], 6.2, MUTED, "w-mono")
    s += f'<rect x="248" y="770" width="130" height="78" fill="{PANEL2}" stroke="{LINE}"/>'
    s += txt(260, 792, "RF", 9, GREEN, "w-mono", "start", "700", "1")
    s += txt(260, 807, "315 / 433", 6.6, BONE, "w-mono")
    s += txt(260, 820, "≤10 dBm", 6, MUTED, "w-mono")
    # antenna
    s += f'<g stroke="{COPPER}" stroke-width="2" fill="none"><path d="M560 862V780"/><path d="M560 780L548 766M560 780L572 766"/></g>'
    for r in (24, 38, 52):
        s += f'<path d="M{560+r} 862A{r} {r} 0 0 1 {560} {862-r}" fill="none" stroke="{AMBER}" stroke-opacity=".28" stroke-dasharray="2 5"/>'
    s += txt(600, 854, "ANT", 7.6, AMBER, "w-mono", "start", "700", "1")
    s += txt(600, 866, d["module"]["antenna"], 6.2, KHAKI, "w-mono")

    # pins: ESP32 bottom, module top
    xs = [120, 270, 420, 570]
    for c, x in zip(d["connections"], xs):
        color = WIRE[c["wire"]]
        s += pin(x, 470, "bottom", c["esp32"], c["esp32_role"], color)
        s += pin(x, 690, "top", c["module"], c["module_role"], color, label_dy=50, role_dy=62)
        s += wire_v(x, 470, 690, color, c["direction"])
        label = f'{c["esp32"]} → {c["module"]}' if c["direction"] == "esp32->module" else (
                f'{c["esp32"]} ← {c["module"]}' if c["direction"] == "module->esp32" else
                f'{c["esp32"]} → {c["module"]}')
        s += txt(x, 540, label, 8, color, "w-mono", "middle", "700", ".1")
        note = "3.3V ONLY" if c["direction"] == "power" else ("COMMON" if c["direction"] == "ground" else c["esp32_role"])
        s += txt(x, 553, note, 5.8, MUTED if c["direction"] != "power" else AMBER, "w-mono", "middle")

    s += txt(24, 1032, "WIRING TABLE", 7, AMBER, "w-mono", "start", "700", "1.4")
    s += table_compact(d, 24, 1042, 672, 24, 20, 8.2)
    s += svg_close(w, h)
    return s


# ---- detail plates ----------------------------------------------------------
def render_core(d: dict) -> str:
    w, h = 720, 760
    uid = "wc"
    s = svg_open(w, h, uid, "CORE-C3 abstract pin map",
                 "Abstract ESP32-C3 pin map generated from hardware/wiring.json.")
    s += txt(24, 30, "RF-LAB // PIN MAP", 7, MUTED, "w-mono", "start", "normal", "1.8")
    s += txt(w-24, 30, "FIG. A / CORE-C3", 7, AMBER, "w-mono", "end", "normal", "1.3")
    s += f'<line x1="24" y1="42" x2="{w-24}" y2="42" stroke="{LINE}"/>'
    s += txt(30, 84, "CORE-C3", 34, BONE, "w-disp", "start", "700", ".4")
    s += txt(30, 104, "ESP32-C3 / 0.42\" OLED / USB-C // only the pins that matter", 7.4, KHAKI, "w-mono")
    s += txt(w-30, 84, "● LIVE", 6.8, GREEN, "w-mono", "end", "normal", "1")
    s += f'<line x1="24" y1="118" x2="{w-24}" y2="118" stroke="{LINE}"/>'

    s += block(24, 138, 400, 430, d["board"]["name"], f'{d["board"]["mcu"]}  //  {d["board"]["radio"]}', "MCU")
    s += f'<rect x="48" y="198" width="210" height="82" fill="{TEAL}" stroke="{LINE2}"/>'
    s += txt(60, 220, d["board"]["mcu"], 16, BONE, "w-disp", "start", "700", ".4")
    s += txt(60, 235, d["board"]["radio"], 6.8, KHAKI, "w-mono")
    s += txt(60, 248, "USB " + d["board"]["usb"], 6.2, MUTED, "w-mono")
    s += f'<rect x="48" y="300" width="160" height="54" fill="{PANEL2}" stroke="{LINE}"/>'
    s += txt(60, 320, "OLED", 8, AMBER, "w-mono", "start", "700", ".8")
    s += txt(60, 334, "0.42\" 72x40", 6.6, BONE, "w-mono")
    s += txt(60, 346, "I2C 5/6 / 0x3C", 6, MUTED, "w-mono")
    s += f'<rect x="220" y="300" width="112" height="54" fill="{PANEL2}" stroke="{LINE}"/>'
    s += txt(232, 320, "USB-C", 8, AMBER, "w-mono", "start", "700", ".8")
    s += txt(232, 334, "5V IN", 6.6, BONE, "w-mono")
    s += txt(232, 346, "native USB", 6, MUTED, "w-mono")
    s += f'<rect x="48" y="374" width="300" height="26" fill="{AMBER}" fill-opacity=".04" stroke="{AMBER}" stroke-opacity=".35"/>'
    s += txt(60, 391, "3.3V ONLY  //  NEVER 5V INTO GPIO3/4", 6.8, AMBER, "w-mono", "start", "700", ".4")
    s += txt(48, 414, "USED BY THIS PROJECT // RIGHT EDGE", 6.6, GREEN, "w-mono", "start", "700", "1")
    s += txt(48, 430, "UART1 9600 8N1  //  TX/RX crossed at the module", 6.2, MUTED, "w-mono")
    s += txt(48, 444, "GPIO5/6 stay on the on-board OLED; do not reuse.", 6.2, MUTED, "w-mono")
    ys = [470, 500, 530, 560]
    for c, y in zip(d["connections"], ys):
        color = WIRE[c["wire"]]
        s += pin(424, y, "right", c["esp32"], c["esp32_role"], color)

    # reserved panel
    s += f'<rect x="448" y="138" width="248" height="430" fill="{PANEL}" stroke="{LINE}"/>'
    s += txt(460, 160, "RESERVED // 不可挪用", 7, RED, "w-mono", "start", "700", "1")
    for i, r in enumerate(d["reserved"]):
        y = 184 + i*66
        s += f'<line x1="460" y1="{y-12}" x2="684" y2="{y-12}" stroke="{LINE}" stroke-opacity=".5"/>'
        s += txt(460, y, r["pins"], 7.6, BONE, "w-mono", "start", "700", ".2")
        s += txt(460, y+12, r["role"], 6.2, KHAKI, "w-mono")
        s += txt(460, y+23, r["why"], 5.8, MUTED, "w-mono")

    s += txt(24, 596, "PIN TABLE", 7, AMBER, "w-mono", "start", "700", "1.4")
    s += table_compact(d, 24, 606, 672, 24, 20, 8.2)
    s += svg_close(w, h)
    return s


def render_rf(d: dict) -> str:
    w, h = 720, 760
    uid = "wr"
    s = svg_open(w, h, uid, "YS-RF34DL abstract pin map",
                 "Abstract YS-RF34DL module pin map generated from hardware/wiring.json.")
    s += txt(24, 30, "RF-LAB // PIN MAP", 7, MUTED, "w-mono", "start", "normal", "1.8")
    s += txt(w-24, 30, "FIG. B / RF BAY 01", 7, GREEN, "w-mono", "end", "normal", "1.3")
    s += f'<line x1="24" y1="42" x2="{w-24}" y2="42" stroke="{LINE}"/>'
    s += txt(30, 84, d["module"]["name"], 34, BONE, "w-disp", "start", "700", ".4")
    s += txt(30, 104, f'{d["module"]["bands"]} / {d["module"]["uart"]} / 4-wire UART', 7.4, KHAKI, "w-mono")
    s += txt(w-30, 84, "● LIVE", 6.8, GREEN, "w-mono", "end", "normal", "1")
    s += f'<line x1="24" y1="118" x2="{w-24}" y2="118" stroke="{LINE}"/>'

    s += block(24, 138, 400, 430, d["module"]["name"], f'{d["module"]["bands"]}  //  {d["module"]["mcu"]}', "RF BAY 01", GREEN)
    s += f'<rect x="70" y="198" width="170" height="82" fill="{TEAL}" stroke="{LINE2}"/>'
    s += txt(82, 220, d["module"]["mcu"], 16, BONE, "w-disp", "start", "700", ".4")
    s += txt(82, 235, "fixed-code codec", 6.8, KHAKI, "w-mono")
    s += txt(82, 248, d["module"]["bands"], 6.2, MUTED, "w-mono")
    s += f'<rect x="70" y="300" width="130" height="54" fill="{PANEL2}" stroke="{LINE}"/>'
    s += txt(82, 320, "RF", 9, GREEN, "w-mono", "start", "700", "1")
    s += txt(82, 334, "315 / 433", 6.6, BONE, "w-mono")
    s += txt(82, 346, "≤10 dBm", 6, MUTED, "w-mono")
    s += f'<rect x="218" y="300" width="150" height="54" fill="{PANEL2}" stroke="{LINE}"/>'
    s += txt(230, 320, "UART", 8, AMBER, "w-mono", "start", "700", ".8")
    s += txt(230, 334, d["module"]["uart"], 6.6, BONE, "w-mono")
    s += txt(230, 346, "TTL 3.3V", 6, MUTED, "w-mono")
    s += f'<rect x="48" y="374" width="320" height="26" fill="{AMBER}" fill-opacity=".04" stroke="{AMBER}" stroke-opacity=".35"/>'
    s += txt(60, 391, "★ 3.3V ONLY  //  VCC + TXD/RXD", 6.8, AMBER, "w-mono", "start", "700", ".4")
    s += txt(48, 430, "USED BY THIS PROJECT", 6.6, GREEN, "w-mono", "start", "700", "1.2")
    ys = [458, 494, 530, 566]
    for c, y in zip(d["connections"], ys):
        color = WIRE[c["wire"]]
        s += pin(48, y, "left", c["module"], c["module_role"], color)

    # antenna panel
    s += f'<rect x="448" y="138" width="248" height="430" fill="{PANEL}" stroke="{LINE}"/>'
    s += txt(460, 160, "ANTENNA", 7, AMBER, "w-mono", "start", "700", "1")
    s += f'<g stroke="{COPPER}" stroke-width="2.4" fill="none"><path d="M570 380V250"/><path d="M570 250L554 232M570 250L586 232"/></g>'
    for r in (40, 62, 84):
        s += f'<path d="M{570+r} 380A{r} {r} 0 0 1 {570} {380-r}" fill="none" stroke="{AMBER}" stroke-opacity=".3" stroke-dasharray="2 6"/>'
    s += txt(460, 420, d["module"]["antenna"], 7.6, BONE, "w-mono")
    s += txt(460, 436, "vertical / away from metal", 6.2, MUTED, "w-mono")
    s += txt(460, 470, "BANDS", 6.6, AMBER, "w-mono", "start", "700", "1")
    s += txt(460, 488, d["module"]["bands"], 8, BONE, "w-mono")
    s += txt(460, 520, "UART", 6.6, AMBER, "w-mono", "start", "700", "1")
    s += txt(460, 538, d["module"]["uart"], 8, BONE, "w-mono")

    s += txt(24, 596, "PIN TABLE", 7, AMBER, "w-mono", "start", "700", "1.4")
    s += table_compact(d, 24, 606, 672, 24, 20, 8.2)
    s += svg_close(w, h)
    return s


# ---- main -------------------------------------------------------------------
def generate(d: dict) -> dict[pathlib.Path, str]:
    return {
        HEADER: header_text(d),
        ART / "hw-assembly.svg": render_wide(d),
        ART / "hw-assembly-m.svg": render_portrait(d),
        ART / "hw-core.svg": render_core(d),
        ART / "hw-rf.svg": render_rf(d),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    d = load()
    validate(d)
    check_main_c()

    files = generate(d)
    ok = True
    for path, content in files.items():
        if args.check:
            if not path.exists():
                print(f"MISSING {path.relative_to(ROOT)}")
                ok = False
            elif path.read_text(encoding="utf-8") != content:
                print(f"STALE   {path.relative_to(ROOT)}")
                ok = False
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")

    blocks = {
        HW_README: table_block(d, compact=False),
        MVP: table_block(d, compact=True),
    }
    for path, block in blocks.items():
        text = path.read_text(encoding="utf-8")
        expected = replace_block(text, block, path)
        if args.check:
            if expected != text:
                print(f"STALE   {path.relative_to(ROOT)} [WIRING block]")
                ok = False
        else:
            path.write_text(expected, encoding="utf-8")

    if args.check and not ok:
        print("wiring check failed; run python3 tools/gen_wiring.py")
        return 1
    print("wiring: 4 nets / 4 wires; header + 4 SVGs + 2 doc tables OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
