#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
接线图生成器 —— docs/02-hardware.md §3 的可执行镜像。

目的:让「引脚分配」只有一处**人工**权威(docs/02-hardware.md §3),本文件的
PINS 是它的镜像;接线图 SVG/PNG 与 Markdown 接线说明全部从 PINS 派生。改引脚时
先改 docs §3,再改 PINS,最后重跑本脚本。

本脚本的自检(全部在 --check 里执行,失败退出 1):
  1. 数据自检:占用/余量/实装根数与文档一致、无重复分配、无 strapping、模块的
     CS/中断/使能/SDN/CE 都能在 PINS 里找到、预算分类求和正确;
  2. 几何自检:面板内文本两两不相交(按保守字宽估算);
  3. 面板自检:每个面板的内容包围盒必须落在面板矩形内(不溢出);
  4. 生成物自检:把 SVG/README **重新生成到临时目录**,与仓库里已提交的
     `hardware/wiring/wiring-diagram.svg` / `README.md` **逐字节比对**,
     不一致即"生成物过期",退出 1。
     (PNG 是同一个 SVG 的栅格派生物,像素受渲染器/字体影响,不做字节比对;
      重跑生成器时会顺带重渲染。)

用法:
    python3 tools/gen_wiring_diagram.py            # 生成(覆盖 SVG/README/PNG)
    python3 tools/gen_wiring_diagram.py --check    # 只校验,不写文件(提交前/CI)
    python3 tools/gen_wiring_diagram.py --out-dir DIR   # 生成到别的目录(跳过生成物比对)

退出码:0 成功 / 1 自检失败 / 2 参数错误
依赖:仅 Python3 标准库(渲染 PNG 时可选调用系统里的 rsvg-convert / libreoffice / inkscape)。
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unicodedata

# ============================================================================
# 单一数据源(镜像)
# 人工权威:docs/02-hardware.md §3(引脚分配)
# 纪律:改这里之前先改 docs/02-hardware.md §3。§3.1 的模块针序也在 MODULES 里复述,
#       供面板③/README §3 输出"模块脚号"列,防止现场只按位置插模块。
# ============================================================================

# (GPIO, 方向, 信号, 分组, 备注) —— 计入 21 根占用
PINS = [
    # 共享 SPI2 总线:只有 GPIO12 是 IO_MUX 的 FSPICLK 默认脚;13/14 走 GPIO 矩阵
    (12, "out", "SPI2 FSPICLK → SCK",  "spi", "IO_MUX 直连(FSPICLK 默认脚)"),
    (13, "out", "SPI2 FSPID → MOSI",   "spi", "GPIO 矩阵路由(非默认脚)"),
    (14, "in",  "SPI2 FSPIQ → MISO",   "spi", "GPIO 矩阵路由(非默认脚)"),
    # M1 · AS10-M4463D-SMA(Si4463)
    (15, "out", "M1 CS",   "m1", "AS10 pin5 NSEL,低有效"),
    (18, "in",  "M1 nIRQ", "m1", "AS10 pin9;Si4463 nIRQ(不是 GDO0)"),
    (9,  "OD",  "M1 SDN",  "m1", "开漏输出 + 外部 100k 上拉 3.3V;POR 脉冲:释放(高)≥100µs → 拉低"),
    (4,  "out", "M1 EN",   "m1", "LDO-A 使能,高有效;100k 下拉"),
    # M2 · AS07-M1101D-SMA(CC1101)
    (16, "out", "M2 CS",   "m2", "AS07 pin4 CSN,低有效"),
    (21, "in",  "M2 GDO0", "m2", "AS07 pin3;CC1101 GDO0"),
    (5,  "out", "M2 EN",   "m2", "LDO-B 使能,高有效;100k 下拉"),
    # M3 · AS01-ML01(nRF24L01P + RFX2401C)
    (17, "out", "M3 CS",   "m3", "AS01 pin4 CSN,低有效"),
    (47, "in",  "M3 IRQ",  "m3", "AS01 pin8;nRF24 IRQ"),
    (6,  "out", "M3 EN",   "m3", "LDO-C 使能,高有效;100k 下拉;上电默认低"),
    # 发射硬件门控链(安全相关):AND(GPIO7,GPIO8) 输出直接接 AS01 pin3 CE
    (7,  "out", "2.4G 总闸", "gate", "高=软件允许;上电默认低;AND 输入(不新增 CE 脚)"),
    (8,  "out", "RF KILL",   "gate", "低有效;外部 100k 下拉;AND 输入"),
    # 系统
    (1,  "in",  "电池分压 ADC", "sys", "ADC1_CH0,经 1:2 分压"),
    (2,  "in",  "NTC 温度",     "sys", "ADC1_CH1,10kΩ 分压"),
    (48, "out", "板载 WS2812",  "sys", "✅ 已核实 GPIO48;单线可寻址,占用 RMT"),
    # OLED(I²C,4 针版)—— ⏸ 下一批:引脚预留,本批不装、不写驱动
    (40, "io",  "OLED SDA", "oled", "⏸ 下一批(I²C 预留,本批不装)"),
    (41, "io",  "OLED SCL", "oled", "⏸ 下一批;与 JTAG(39–42)冲突,本产品已放弃 JTAG"),
    # 配对按键
    (38, "in",  "配对按键", "btn", "内部上拉,低有效;外并 100nF 去抖"),
]

# 控制通道 / 不可用脚:复用已有外设,**不新增占用**
# (GPIO, 方向, 信号, 分组, 备注);分组只用于面板① 的配色与说明
CONTROL_PINS = [
    (43, "—", "UART0 TX", "ctrl", "板载 CH343P(RX)→ COM 口;当前唯一控制通道"),
    (44, "—", "UART0 RX", "ctrl", "板载 CH343P(TX)→ COM 口;当前唯一控制通道"),
    (19, "—", "原生 USB D−", "na", "⛔ 从未枚举成功:不可用、非控制通道"),
    (20, "—", "原生 USB D+", "na", "⛔ 同上;JTAG 随之失去,勿设计成 OTG 控制口"),
]

POOL = [1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21,
        38, 39, 40, 41, 42, 47, 48]          # 25 根可用
STRAPPING = [0, 3, 45, 46]                    # 不使用
# 不可用:SPI0/1 Flash, OPI PSRAM, UART0(被 CH343P 占用), USB
UNAVAILABLE = [26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 43, 44]

# 预算(必须与 docs/02-hardware.md §2/§4、README §2 一致):
#   占用 21 = 射频 15 + 系统 3 + OLED 预留 2 + 按键 1
#   可用池 25 = 45 GPIO − 16 不可用 − 4 strapping
#   余量 +4;本批实装 19(OLED 2 未装)⇒ 当前 +6
EXPECT_OCCUPIED = 21
EXPECT_MARGIN = 4
EXPECT_INSTALLED = 19
EXPECT_CURRENT_MARGIN = 6
EXPECT_GROUP_COUNTS = {"spi": 3, "m1": 4, "m2": 3, "m3": 3,
                       "gate": 2, "sys": 3, "oled": 2, "btn": 1}

GROUPS = {
    "spi":  ("共享 SPI2 总线", "#2563eb"),
    "m1":   ("M1 · Si4463",   "#1d4ed8"),
    "m2":   ("M2 · CC1101",   "#047857"),
    "m3":   ("M3 · nRF24+PA", "#c2410c"),
    "gate": ("发射门控",      "#b91c1c"),
    "oled": ("OLED I²C ⏸",   "#0e7490"),
    "sys":  ("系统",          "#475569"),
    "btn":  ("按键",          "#a16207"),
}
CONTROL_GROUPS = {
    "ctrl": ("控制通道", "#7c3aed"),
    "na":   ("不可用",   "#dc2626"),
}

# 每个射频模块的专线 + 官方针序里的对应脚(防止"按位置插"错位)
# cs/irq/en 是 ESP32 侧 GPIO;sdn 只有 M1 有;ce 不是 GPIO,是 AND 门输出。
MODULES = [
    dict(slot=1, name="AS10-M4463D-SMA", chip="Si4463", cs=15, cs_pin="NSEL=5",
         irq=18, irq_name="nIRQ", irq_pin="IRQ=9", sdn=9, sdn_pin="SDN=2",
         en=4, rail="LDO-A", ce=None, ce_pin="—",
         band="425–525MHz", pwr="20dBm", col="#1d4ed8",
         func="433MHz OOK 遥控收发(唯一)"),
    dict(slot=2, name="AS07-M1101D-SMA", chip="CC1101", cs=16, cs_pin="CSN=4",
         irq=21, irq_name="GDO0", irq_pin="GDO0=3", sdn=None, sdn_pin="—",
         en=5, rail="LDO-B", ce=None, ce_pin="—",
         band="387–464MHz", pwr="10dBm", col="#047857",
         func="Sub-GHz 通用收发"),
    dict(slot=3, name="AS01-ML01", chip="nRF24L01P+RFX2401C", cs=17, cs_pin="CSN=4",
         irq=47, irq_name="IRQ", irq_pin="IRQ=8", sdn=None, sdn_pin="—",
         en=6, rail="LDO-C(2.4G)", ce="AND(7,8)", ce_pin="CE=3",
         band="2400–2525MHz", pwr="约 20dBm", col="#c2410c",
         func="2.4GHz 嗅探/发射"),
]

# 接线前必查(面板⑦ 与 README §8 共用同一份,避免两处文案漂移)
CHECKS = [
    ("M3 的 CE 必须由 AND(GPIO7,GPIO8) 驱动", "直连 GPIO 或悬空会让 RF_KILL 形同虚设"),
    ("KILL(GPIO8)必须外部 100k 下拉", "上电/复位期间可能意外允许发射"),
    ("LDO EN 4/5/6 各加 100k 下拉", "EN 浮空可能让模块意外上电"),
    ("AS10 SDN 接 GPIO9(开漏)+ 100k 上拉 3.3V", "OD 高=高阻,旧接 GND 不短路但也拉不高;POR 缺失 ⇒ SPI 不响应、读数全 0xFF"),
    ("AS10 上电做 POR 脉冲", "释放(OD 高)≥100µs → 拉低 → 等 ≥10ms,再初始化 SPI"),
    ("OLED ⏸ 下一批:40/41 预留(与 JTAG 冲突)", "本批不装;已放弃 JTAG,挪用了下一批就要改板"),
    ("OLED 必须 I²C 4 针版(下一批)", "SPI 版(4–7 根)几乎吃光 +4 的全部余量"),
    ("OLED 不挂 M3 键控轨(下一批)", "否则关模块时屏幕一起灭"),
    ("原生 USB 19/20 不可用", "本板从未枚举成功;不要设计成控制通道/OTG"),
    ("控制口 = 板载 COM(CH343P)数据线", "充电线无 D+/D−,插上不通信"),
    ("M3 天线拉远 + 正交极化", "20dBm 会压掉本机 Wi-Fi/BLE"),
    ("每模块独立 LDO + 就近 100µF", "µs 级电流阶跃导致复位或频谱劣化"),
    ("三根天线不可并联 / 合路", "失配 + 违背 C-02"),
    ("strapping 0/3/45/46 一律不用", "上电模式误判"),
]

CJK = "Noto Sans CJK SC, WenQuanYi Zen Hei, Source Han Sans SC, sans-serif"
MONO = "Noto Sans Mono CJK SC, DejaVu Sans Mono, monospace"
CANVAS_W = 1240


def esc(t):
    return t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def text_w(s, size):
    """保守估算文本宽度(全角 1.0em、ASCII 0.55em、常用符号 0.7em)。"""
    w = 0.0
    for ch in s:
        if unicodedata.east_asian_width(ch) in ("W", "F"):
            w += 1.0
        elif ch in " ·—→":
            w += 0.7
        else:
            w += 0.55
    return w * size


class Svg:
    """按「面板」累积元素,再统一排版。

    面板只描述自己的内容高度,纵向位置由 render() 累加计算 —— 面板变长时下面的
    面板自动下移。同时记录每个面板内容的包围盒,供 --check 证明"内容不溢出面板"。
    """

    def __init__(self):
        self.chrome = []
        self.panels = []
        self._cur = []
        self._bbox = None

    def _expand(self, x0, y0, x1, y1):
        if self._bbox is None:
            self._bbox = [x0, y0, x1, y1]
        else:
            b = self._bbox
            b[0] = min(b[0], x0)
            b[1] = min(b[1], y0)
            b[2] = max(b[2], x1)
            b[3] = max(b[3], y1)

    # ---- 面板内绘制(坐标相对面板左上角)----
    def rect(self, x, y, w, h, fill="#fff", stroke="#cbd5e1", rx=8, sw=1.2, dash=None):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        self._cur.append('<rect x="%g" y="%g" width="%g" height="%g" rx="%g" fill="%s" '
                         'stroke="%s" stroke-width="%g"%s/>' % (x, y, w, h, rx, fill, stroke, sw, d))
        self._expand(x, y, x + w, y + h)

    def text(self, x, y, s, size=13, fill="#0f172a", anchor="start",
             weight="normal", family=None):
        self._cur.append('<text x="%g" y="%g" font-family="%s" font-size="%g" fill="%s" '
                         'text-anchor="%s" font-weight="%s">%s</text>'
                         % (x, y, family or CJK, size, fill, anchor, weight, esc(s)))
        w = text_w(s, size)
        if anchor == "start":
            x0 = x
        elif anchor == "end":
            x0 = x - w
        else:
            x0 = x - w / 2
        self._expand(x0, y - size * 0.80, x0 + w, y + size * 0.25)

    def line(self, x1, y1, x2, y2, stroke="#94a3b8", sw=1.6, dash=None):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        self._cur.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="%s" '
                         'stroke-width="%g" stroke-linecap="round"%s/>'
                         % (x1, y1, x2, y2, stroke, sw, d))
        self._expand(min(x1, x2), min(y1, y2), max(x1, x2), max(y1, y2))

    def poly(self, pts, stroke="#94a3b8", sw=1.8, fill="none"):
        d = " ".join("%g,%g" % pt for pt in pts)
        self._cur.append('<polyline points="%s" fill="%s" stroke="%s" stroke-width="%g" '
                         'stroke-linejoin="round" stroke-linecap="round"/>' % (d, fill, stroke, sw))
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        self._expand(min(xs), min(ys), max(xs), max(ys))

    def circle(self, cx, cy, r, fill="#94a3b8"):
        self._cur.append('<circle cx="%g" cy="%g" r="%g" fill="%s"/>' % (cx, cy, r, fill))
        self._expand(cx - r, cy - r, cx + r, cy + r)

    def chip(self, x, y, w, h, label, sub=None, fill="#f8fafc", stroke="#94a3b8",
             tcol="#0f172a", size=12.5):
        self.rect(x, y, w, h, fill=fill, stroke=stroke, rx=6)
        cy = y + h / 2
        self.text(x + w / 2, cy - (7 if sub else -4), label, size=size,
                  weight="bold", anchor="middle", fill=tcol)
        if sub:
            self.text(x + w / 2, cy + 12, sub, size=10.5, anchor="middle",
                      fill="#475569", family=MONO)

    # ---- 面板 ----
    def panel(self, title, height, builder, sub=None, accent="#334155", width=None):
        """builder(s) 里用相对坐标绘制;height 是本面板的内容高度。"""
        self._cur = []
        self._bbox = None
        self._cur_w = width or (CANVAS_W - 80)
        builder(self)
        self.panels.append(dict(title=title, sub=sub, accent=accent,
                                h=height, w=self._cur_w, body=list(self._cur),
                                bbox=list(self._bbox) if self._bbox else None))

    def render(self):
        total = 152
        for p in self.panels:
            total += p["h"] + 16
        total += 20
        out = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
               'viewBox="0 0 %d %d">' % (CANVAS_W, total, CANVAS_W, total),
               '<rect width="%d" height="%d" fill="#f8fafc"/>' % (CANVAS_W, total)]
        out.extend(self.chrome)
        y = 152
        for p in self.panels:
            x, w, h = 40, p["w"], p["h"]
            out.append('<rect x="%g" y="%g" width="%g" height="%g" rx="10" fill="#fff" '
                       'stroke="#e2e8f0" stroke-width="1.2"/>' % (x, y, w, h))
            out.append('<path d="M %g %g h %g a 10 10 0 0 1 10 10 v 24 h -%g '
                       'a 10 10 0 0 1 -10 -10 z" fill="#f1f5f9" stroke="#e2e8f0" '
                       'stroke-width="1.2"/>' % (x + 10, y, w - 20, w))
            out.append('<rect x="%g" y="%g" width="4" height="34" rx="2" fill="%s"/>'
                       % (x, y, p["accent"]))
            out.append('<text x="%g" y="%g" font-family="%s" font-size="15" fill="#0f172a" '
                       'font-weight="bold">%s</text>' % (x + 16, y + 22, CJK, esc(p["title"])))
            if p["sub"]:
                out.append('<text x="%g" y="%g" font-family="%s" font-size="11.5" '
                           'fill="#64748b" text-anchor="end">%s</text>'
                           % (x + w - 16, y + 22, CJK, esc(p["sub"])))
            out.append('<g transform="translate(%g,%g)">' % (x, y))
            out.extend(p["body"])
            out.append('</g>')
            y += h + 16
        out.append('</svg>')
        return "\n".join(out) + "\n"


def build_svg():
    s = Svg()
    s.chrome.append(
        '<text x="40" y="46" font-family="%s" font-size="26" font-weight="bold" '
        'fill="#0f172a">RF 扫频 / 捕获设备 — 接线图</text>' % CJK)
    s.chrome.append(
        '<text x="40" y="72" font-family="%s" font-size="13.5" fill="#475569">'
        'ESP32-S3N16R8 + 三射频模块 + CH343P→UART0 有线控制通道 · OLED 预留(下一批) · '
        '三模块全部经免焊排针/母座(约束 C-03)</text>' % CJK)
    s.chrome.append('<rect x="40" y="88" width="1160" height="46" rx="8" '
                    'fill="#fffbeb" stroke="#fcd34d"/>')
    s.chrome.append(
        '<text x="56" y="107" font-family="%s" font-size="12.5" fill="#92400e">'
        '⚠️ 本图是布线的可执行镜像,不是最终板图;引脚以 '
        'docs/02-hardware.md §3 为准。</text>' % CJK)
    s.chrome.append(
        '<text x="56" y="125" font-family="%s" font-size="12.5" fill="#92400e">'
        '改动纪律:①改 docs/02-hardware.md §3 → ②改 tools/gen_wiring_diagram.py 的 PINS → '
        '③重跑生成器(--check 会逐字节比对生成物,过期即失败)。</text>' % CJK)

    # ---------------- ① 引脚分配 ----------------
    ROWH = 22.0
    n_rows = len(PINS) + len(GROUPS) + 1 + len(CONTROL_PINS)   # 组标题 + 控制块标题+行
    p1_h = 80 + n_rows * ROWH + 98

    def b1(s):
        cols = [24, 232, 368, 440, 600]
        for i, htxt in enumerate(["GPIO", "信号 / 网络", "方向", "归属", "备注"]):
            s.text(cols[i], 58, htxt, size=11.5, weight="bold", fill="#64748b")
        y = 80
        order = ["spi", "m1", "m2", "m3", "gate", "sys", "oled", "btn"]
        for gkey in order:
            gname, gcol = GROUPS[gkey]
            items = [p for p in PINS if p[3] == gkey]
            s.rect(12, y - 15, 1136, ROWH, fill="#f1f5f9", rx=4)
            s.rect(12, y - 15, 3.5, ROWH, fill=gcol, rx=1.5)
            s.text(26, y, gname, size=11.5, weight="bold", fill=gcol)
            s.text(1138, y, "%d 根" % len(items), size=11, fill="#64748b", anchor="end")
            y += ROWH
            for pin, direction, sig, _g, note in items:
                s.rect(cols[0], y - 13.5, 44, 17, fill=gcol, rx=3.5)
                s.text(cols[0] + 22, y - 1, str(pin), size=11.5, fill="#fff",
                       anchor="middle", weight="bold")
                s.text(cols[1], y, sig, size=12, family=MONO)
                s.text(cols[2], y, direction, size=11, fill="#64748b", family=MONO)
                s.text(cols[3], y, gname, size=11, fill=gcol)
                s.text(cols[4], y, note, size=11, fill="#475569")
                y += ROWH
        # 控制通道 / 不可用脚(不新增占用)
        s.rect(12, y - 15, 1136, ROWH * (1 + len(CONTROL_PINS)), fill="#faf5ff",
               stroke="#e9d5ff", rx=4)
        s.text(26, y, "控制通道 / 不可用脚(复用,不新增占用;见 docs/02-hardware.md §2/§3.3)",
               size=11.5, weight="bold", fill=CONTROL_GROUPS["ctrl"][1])
        y += ROWH
        for pin, direction, sig, cg, note in CONTROL_PINS:
            cname, ccol = CONTROL_GROUPS[cg]
            s.rect(cols[0], y - 13.5, 44, 17, fill=ccol, rx=3.5)
            s.text(cols[0] + 22, y - 1, str(pin), size=11.5, fill="#fff",
                   anchor="middle", weight="bold")
            s.text(cols[1], y, sig, size=12, family=MONO)
            s.text(cols[2], y, direction, size=11, fill="#64748b", family=MONO)
            s.text(cols[3], y, cname, size=11, fill=ccol)
            s.text(cols[4], y, note, size=11, fill="#475569")
            y += ROWH
        yb = y + 6
        s.rect(12, yb, 1136, 84, fill="#eff6ff", stroke="#bfdbfe", rx=6)
        s.text(26, yb + 18,
               "占用 21 = 射频 15(SCK/MOSI/MISO + CS×3 + 中断×3 + EN×3 + SDN + 总闸 + KILL)",
               size=11.5, fill="#1e40af")
        s.text(26, yb + 36,
               "+ 系统 3(电池 ADC / NTC / 板载 WS2812)+ OLED 2(⏸ 下一批,预留)+ 按键 1;"
               "控制通道 43/44 复用 UART0、USB 19/20 不可用,均计 0。",
               size=11.5, fill="#1e40af")
        s.text(26, yb + 54,
               "可用池 25 = 45 GPIO − 16 不可用 − 4 strapping;余量 +4(含 OLED 预留);"
               "本批实装 19 根 ⇒ 当前 +6。",
               size=11.5, fill="#64748b")
        s.text(26, yb + 72,
               "不足以再加一条 SPI 主机(需 3 根且只能走 GPIO Matrix)"
               "—— 这是「单总线 + 3 CS」的成立边界。",
               size=11.5, fill="#64748b")
    s.panel("① ESP32-S3N16R8 引脚分配", p1_h, b1,
            sub="可用池 25 / 占用 21 / 余量 +4 · 实装 19 ⇒ 当前 +6")

    # ---------------- ② SPI 总线 ----------------
    def b2(s):
        s.text(30, 48,
               "三条 SPI 网络(SCK / MOSI / MISO)各自独立、分别并联到 3 个模块;"
               "● = 同网络连接点,线交叉且无 ● = 不连接。",
               size=10.5, fill="#475569")
        bx, by, bw, bh = 30, 72, 148, 108
        s.chip(bx, by, bw, bh, "ESP32-S3", "SPI2_HOST", "#eff6ff", "#93c5fd", "#1d4ed8")
        rails = [("SCK", 12, "#2563eb", 100),
                 ("MOSI", 13, "#047857", 124),
                 ("MISO", 14, "#b45309", 148)]
        for name, pin, col, ry in rails:
            s.line(bx + bw, ry, 1040, ry, col, 2.2)
            s.text(bx + bw + 10, ry - 6, "%s ← GPIO%d" % (name, pin),
                   size=10, fill=col, family=MONO)
        mws, my, mh = 280, 240, 170
        mxs = [110, 470, 830]
        taps = [("SCK", 70, "#2563eb", 100),
                ("MOSI", 140, "#047857", 124),
                ("MISO", 210, "#b45309", 148)]
        for i, m in enumerate(MODULES):
            mx = mxs[i]
            s.rect(mx, my, mws, mh, fill="#f8fafc", stroke=m["col"], rx=6)
            s.rect(mx, my, 5, mh, fill=m["col"], rx=2)
            for name, dx, col, ry in taps:
                tx = mx + dx
                s.line(tx, ry, tx, my, col, 1.8)
                s.circle(tx, ry, 3.2, col)
                s.text(tx, my + 16, name, size=9.5, fill=col, anchor="middle", family=MONO)
            s.text(mx + 14, my + 50, "槽位%d %s" % (m["slot"], m["name"]),
                   size=11, weight="bold")
            s.text(mx + 14, my + 72, m["chip"], size=10, fill="#64748b", family=MONO)
            s.text(mx + 14, my + 100, "SCK=12  MOSI=13  MISO=14",
                   size=10, fill="#475569", family=MONO)
            s.text(mx + 14, my + 122, "CS=%d  IRQ=%d  EN=%d" % (m["cs"], m["irq"], m["en"]),
                   size=10, fill="#475569", family=MONO)
            s.text(mx + 14, my + 144, "→ %s" % m["rail"], size=10, fill=m["col"], family=MONO)
        # 片选:三条独立线,从 ESP32 直达各模块,不经过 SPI 总线
        cy0 = 460
        s.chip(30, cy0, 148, 90, "ESP32-S3", "同一 SPI2_HOST",
               "#eff6ff", "#93c5fd", "#1d4ed8")
        for i, m in enumerate(MODULES):
            yy = cy0 + 20 + i * 26
            center = mxs[i] + 140
            s.line(178, yy, center, yy, m["col"], 1.8)
            s.line(center, yy, center, my + mh, m["col"], 1.8)
            s.text(190, yy - 5, "GPIO%d → M%d CS(%s)" % (m["cs"], m["slot"], m["cs_pin"]),
                   size=9.5, fill=m["col"], family=MONO)
        s.text(30, cy0 + 116,
               "三根 CS 各自独立(GPIO15/16/17 → M1/M2/M3),与 SPI 三线没有任何共点;"
               "同一时刻只有一根 CS 有效。",
               size=10.5, fill="#475569")
        s.text(30, cy0 + 134,
               "禁止在 CS 有效期间插入日志 / JSON / NVS 写(否则错帧);射频 SPI 访问由 rf_hal 串行化。",
               size=10.5, fill="#475569")
    s.panel("② 共享 SPI2 总线(方案 B:单总线 + 3 CS)", 620, b2,
            sub="省 6 根引脚", accent="#2563eb", width=1160)

    # ---------------- ③ 模块接线 ----------------
    p3_rows = 3
    p3_note_start = 80 + p3_rows * 36 + 30
    p3_notes = [
        "M3(AS01)没有独立 EN 脚:模块电源由 LDO-C(GPIO6)控制;CE(pin3)由 AND(GPIO7,GPIO8)驱动。",
        "CE=0 ⇒ nRF24 处于 power-down、PA 不工作。KILL 保持低时,M3 仍可由 GPIO6 上电做 SPI 只读探测"
        "(CE 低,不发射);M3 收发必须 GPIO7=1 且 KILL 释放。",
        "AS10 SDN=GPIO9(开漏)+ 外部 100k 上拉 3.3V;POR 脉冲:释放(OD 高)≥100µs → 拉低 → 等 ≥10ms。开漏高=高阻,旧接法把 SDN 接 GND 也不短路(但拉不高、须改接 GPIO9)。",
        "每模块只用 1 根中断:Si4463 nIRQ / CC1101 GDO0 / nRF24 IRQ;其余中断脚可空。",
        "天线:三支独立 SMA 座,禁止并联或用合路器(失配 + 违背 C-02)。",
    ]
    p3_h = p3_note_start + len(p3_notes) * 20 + 16

    def b3(s):
        cols = [24, 214, 384, 566, 656, 772, 908, 1032]
        heads = ["槽位 / 模块", "芯片", "模块脚号(官方针序)", "CS", "中断",
                 "SDN / CE", "电源(EN→LDO)", "VCC / GND"]
        for x, htxt in zip(cols, heads):
            s.text(x, 58, htxt, size=10.5, weight="bold", fill="#64748b")
        ty = 80
        for m in MODULES:
            s.rect(14, ty - 18, 1132, 30, fill="#f8fafc", stroke="#e2e8f0", rx=5)
            s.rect(14, ty - 18, 5, 30, fill=m["col"], rx=2)
            s.text(cols[0], ty, "槽位%d %s" % (m["slot"], m["name"]), size=11, weight="bold")
            s.text(cols[1], ty, m["chip"], size=9.5, fill="#64748b", family=MONO)
            pins = "%s · %s" % (m["cs_pin"], m["irq_pin"])
            if m["sdn_pin"] != "—":
                pins += " · " + m["sdn_pin"]
            if m["ce_pin"] != "—":
                pins += " · " + m["ce_pin"]
            s.text(cols[2], ty, pins, size=9.5, fill="#475569", family=MONO)
            s.text(cols[3], ty, str(m["cs"]), size=11, fill=m["col"], family=MONO, weight="bold")
            s.text(cols[4], ty, "%d %s" % (m["irq"], m["irq_name"]), size=10,
                   fill=m["col"], family=MONO)
            if m["sdn"] is not None:
                s.text(cols[5], ty, "SDN=%d(OD,100k↑)" % m["sdn"], size=10,
                       fill=m["col"], family=MONO)
            elif m["ce"] is not None:
                s.text(cols[5], ty, "CE=%s" % m["ce"], size=10, fill=m["col"], family=MONO)
            else:
                s.text(cols[5], ty, "—", size=10, fill="#94a3b8", family=MONO)
            s.text(cols[6], ty, "%d → %s" % (m["en"], m["rail"]), size=10,
                   fill=m["col"], family=MONO)
            s.text(cols[7], ty, "3.3V / GND", size=10, fill="#64748b", family=MONO)
            ty += 36
        y = p3_note_start
        for note in p3_notes:
            s.text(24, y, note, size=10, fill="#475569")
            y += 20
    s.panel("③ 射频模块接线(每模块 3 根共享线 + 独立 CS/中断/SDN/CE)", p3_h, b3,
            sub="全部免焊排针 / 母座", accent="#c2410c", width=1160)

    # ---------------- ④ 控制通道 ----------------
    def b4(s):
        ux, uy = 24, 70
        bw, bh, gap = 190, 66, 24
        boxes = [
            ("手机 / 主机", "USB 主机", "#eff6ff", "#93c5fd", "#1d4ed8"),
            ("Type-C COM 口", "丝印 COM", "#faf5ff", "#c4b5fd", "#6d28d9"),
            ("CH343P", "VID:PID 1a86:55d3", "#faf5ff", "#a78bfa", "#5b21b6"),
            ("UART0", "TX=43 / RX=44", "#f0fdf4", "#86efac", "#047857"),
            ("ESP32-S3", "控制流", "#eff6ff", "#93c5fd", "#1d4ed8"),
        ]
        x = ux
        for label, sub, fill, stroke, tcol in boxes:
            s.chip(x, uy, bw, bh, label, sub, fill, stroke, tcol, 11.5)
            if x > ux:
                s.line(x - gap, uy + bh / 2, x, uy + bh / 2, "#7c3aed", 2)
            x += bw + gap
        lines = [
            "控制通道 = 开发板板载 CH343P(COM 口)→ UART0 43/44;载板无需另做 USB 控制口。",
            "DTR/RTS 接了自动复位:打开 COM 口即复位设备一次,App 必须处理(固件无法规避)。",
            "⛔ 原生 USB 19/20:本板从未枚举成功,不可用、非控制通道;JTAG 随之失去,不要设计成 OTG 控制口。",
            "console / 调试日志走 UART0(与协议帧同一物理口):量产固件必须关闭非协议输出,否则 App 会读到日志。",
        ]
        for i, t in enumerate(lines):
            s.text(24, uy + bh + 40 + i * 22, t, size=10.5,
                   fill="#b91c1c" if t.startswith("⛔") else "#475569")
    s.panel("④ 有线控制通道(板载 CH343P → UART0)", 280, b4,
            sub="不新增占用 · 不是 USB-OTG", accent="#7c3aed", width=1160)

    # ---------------- ⑤ 外设 ----------------
    def b5(s):
        ox = 24
        s.chip(ox, 52, 240, 62, "OLED · SSD1306 ⏸", "0.96寸 128×64 · 4 针 I²C · 下一批",
               "#f8fafc", "#94a3b8", "#64748b", 11.5)
        for i, (t, col) in enumerate([
            ("SDA → GPIO 40    SCL → GPIO 41", "#0e7490"),
            ("4.7kΩ 上拉到 3.3V(模块常已自带上拉)", "#475569"),
            ("供电取主 3.3V —— 不挂 M3 键控轨", "#b91c1c"),
            ("必须 I²C 4 针版;SPI 7 针版会吃光余量", "#b91c1c"),
            ("⚠️ 40/41 与 JTAG(39–42)冲突:原生 USB/JTAG 不可用,已放弃 JTAG", "#b45309"),
        ]):
            s.text(ox + 6, 140 + i * 17, t, size=9.5, fill=col)
        s.line(ox, 236, ox + 560, 236, "#e2e8f0", 1)
        rows = [
            ("配对按键 SW1", "GPIO 38(内部上拉,低有效,并 100nF 去抖)", "#a16207"),
            ("板载 WS2812", "GPIO 48 · 开发板自带(✅ 已核实;蜂鸣器不存在)", "#475569"),
            ("电源灯 / TX/RX 灯", "不受固件控制,不计入本地状态指示", "#475569"),
        ]
        for i, (lbl, val, col) in enumerate(rows):
            yy = 258 + i * 19
            s.text(ox + 6, yy, lbl, size=10.5, weight="bold", fill=col)
            s.text(ox + 130, yy, val, size=10, fill="#475569")
    s.panel("⑤ 外设:OLED(⏸ 下一批)/ 按键 / 指示", 320, b5, accent="#0e7490", width=1160)

    # ---------------- ⑥ 电源 / 门控 ----------------
    def b6(s):
        gx = 24
        s.text(gx, 44, "3.3V 主轨 → 主控 / 按键 / LED(OLED 下一批)", size=10, fill="#b91c1c")
        s.chip(gx, 64, 132, 46, "USB-C 5V", "或 18650", "#fef2f2", "#fca5a5", "#b91c1c", 11)
        s.line(gx + 66, 110, gx + 66, 130, "#b91c1c", 2.4)
        s.chip(gx, 130, 132, 46, "DC-DC 3.3V", "≥2A", "#fef2f2", "#fca5a5", "#b91c1c", 11)
        rail_x = 250
        s.line(gx + 132, 153, rail_x, 153, "#b91c1c", 2.4)
        lx, lw = 280, 200
        for i, m in enumerate(MODULES):
            ly = 64 + i * 62
            s.chip(lx, ly, lw, 52, "%s 3.3V" % m["rail"], "EN←GPIO%d · 100k↓" % m["en"],
                   "#fff", m["col"], m["col"], 10.5)
            s.poly([(rail_x, 153), (rail_x, ly + 26), (lx, ly + 26)], "#b91c1c", 1.6)
            s.text(lx + lw + 10, ly + 22, "→ %s VCC(3.0–3.3V)" % m["name"],
                   size=9.5, fill=m["col"])
            extra = "100µF + 100nF×2" + (" + 470µF + π 滤波" if m["slot"] == 3 else "")
            s.text(lx + lw + 10, ly + 37, extra, size=9, fill="#64748b")
        # AND 门 + CE:标准 AND 符号,GPIO7/GPIO8 两个输入,输出直达 AS01 pin3
        ax, ay = 650, 64
        s.rect(ax, ay, 480, 240, fill="#fef2f2", stroke="#fca5a5", rx=8)
        s.text(ax + 240, ay + 22, "发射硬件门控(安全)", size=11.5, weight="bold",
               anchor="middle", fill="#b91c1c")
        s.text(ax + 14, ay + 48, "GPIO7 = 2.4G 总闸(高=软件允许;上电默认低)",
               size=9.5, fill="#b91c1c", family=MONO)
        s.text(ax + 14, ay + 64, "GPIO8 = RF_KILL(低有效;外部 100k 下拉)",
               size=9.5, fill="#b91c1c", family=MONO)
        g2x, g2y = ax + 200, ay + 84
        s.text(g2x - 34, g2y + 11, "GPIO7", size=9.5, fill="#b91c1c",
               anchor="end", family=MONO)
        s.text(g2x - 34, g2y + 27, "GPIO8", size=9.5, fill="#b91c1c",
               anchor="end", family=MONO)
        s.line(g2x - 30, g2y + 8, g2x, g2y + 8, "#b91c1c", 1.8)
        s.line(g2x - 30, g2y + 24, g2x, g2y + 24, "#b91c1c", 1.8)
        s.poly([(g2x, g2y), (g2x + 26, g2y), (g2x + 40, g2y + 8), (g2x + 44, g2y + 16),
                (g2x + 40, g2y + 24), (g2x + 26, g2y + 32), (g2x, g2y + 32), (g2x, g2y)],
               "#b91c1c", 1.8, "#fff")
        s.text(g2x + 16, g2y + 20, "&", size=10, anchor="middle", fill="#b91c1c")
        s.line(g2x + 44, g2y + 16, g2x + 84, g2y + 16, "#b91c1c", 2.2)
        s.text(g2x + 88, g2y + 20, "→ AS01 pin3 CE", size=10, weight="bold", fill="#b91c1c")
        for i, t in enumerate([
            "CE=0 ⇒ nRF24 处于 power-down、PA 不工作。",
            "KILL 保持低时,M3 仍可由 GPIO6 上电做 SPI 只读探测(CE 低,不发射)。",
            "M3 收发必须 GPIO7=1 且 KILL 释放;不新增 CE GPIO。",
        ]):
            s.text(ax + 14, ay + 150 + i * 20, t, size=9.5, fill="#b91c1c")
        s.text(gx, 360,
               "模块 VCC 标称 3.3V:经低 dropout LDO 实测 3.0–3.3V;三模块均在 1.8–3.6V 内工作。",
               size=10.5, fill="#475569")
        s.text(gx, 378,
               "退耦电容紧贴模块 VCC(<5mm);地回流短而宽。关闭 LDO-C(GPIO6=0)可断 M3 模块电源。",
               size=10.5, fill="#475569")
    s.panel("⑥ 电源树与发射门控链", 400, b6,
            sub="每模块独立 LDO + 就近储能;AND(7,8)→CE", accent="#b91c1c", width=1160)

    # ---------------- ⑦ 必查 ----------------
    checks = CHECKS
    p7_h = 58 + len(checks) * 28 + 10

    def b7(s):
        yy = 58
        for i, (t, sub) in enumerate(checks):
            s.circle(20, yy - 4, 7.5, "#b45309")
            s.text(20, yy - 1, str(i + 1), size=9.5, fill="#fff", anchor="middle", weight="bold")
            s.text(36, yy - 1, t, size=10.5, weight="bold")
            s.text(36, yy + 13, sub, size=9.5, fill="#64748b")
            yy += 28
    s.panel("⑦ 接线前必查(踩错就冒烟或失效)", p7_h, b7, accent="#b45309", width=1160)

    return s


def build_markdown():
    L = []
    A = L.append
    A("# 接线图与引脚分配")
    A("")
    A("状态:`draft` · 最后更新:2026-10-31 · 上游依据:"
      "[`docs/02-hardware.md`](../../docs/02-hardware.md) §3、"
      "[`docs/01-product.md`](../../docs/01-product.md) §8")
    A("")
    A("> ⚠️ **本文件与 [`wiring-diagram.svg`](wiring-diagram.svg) 均由 "
      "[`tools/gen_wiring_diagram.py`](../../tools/gen_wiring_diagram.py) 生成,不要手工编辑。**")
    A("> 引脚分配的人工权威是 `docs/02-hardware.md` §3,生成器的 `PINS` 是它的镜像;"
      "要改引脚:**先改 `docs/02-hardware.md` §3 → 再改生成器的 `PINS` → 重跑生成器**。")
    A("> `--check` 会:① 重算占用/余量/实装根数并核对预算;② 检查文本不压字、面板内容不溢出;"
      "③ 把 SVG/README 重新生成到临时目录并与仓库文件**逐字节比对**,不一致即报"
      "「生成物过期」退出 1。但**文档表与 `PINS` 之间仍需人工同步**,自检不能替你读懂表格。")
    A("")
    A("![接线图](wiring-diagram.svg)")
    A("")
    A("> 两种格式同一张图:**[`wiring-diagram.svg`](wiring-diagram.svg)** 是权威矢量图(可无损放大、可在浏览器或"
      "绘图软件里打开与标注);**[`wiring-diagram.png`](wiring-diagram.png)** 是同一张图的位图渲染"
      "(手机浏览器与文件管理器对 SVG 支持不稳,现场看图用 PNG)。两者都是生成物。")
    A("")
    A("---")
    A("")
    A("## 1. 引脚分配(计入引脚预算)")
    A("")
    A("| GPIO | 信号 / 网络 | 方向 | 归属 | 备注 |")
    A("|---|---|---|---|---|")
    order = ["spi", "m1", "m2", "m3", "gate", "sys", "oled", "btn"]
    for gkey in order:
        for pin, direction, sig, _g, note in [p for p in PINS if p[3] == gkey]:
            A("| **%d** | `%s` | %s | %s | %s |"
              % (pin, sig, direction, GROUPS[gkey][0], note))
    A("")
    A("### 控制通道 / 不可用脚(**不新增占用**)")
    A("")
    A("| GPIO | 信号 | 方向 | 备注 |")
    A("|---|---|---|---|")
    for pin, direction, sig, _g, note in CONTROL_PINS:
        A("| **%d** | `%s` | %s | %s |" % (pin, sig, direction, note))
    A("")
    A("43/44 是 UART0,经开发板板载 **CH343P**(COM 口,VID:PID `1a86:55d3`)成为控制通道;"
      "19/20 是 ESP32-S3 原生 USB,本板**从未枚举成功**,不可用、不作为控制通道(JTAG 随之失去)。"
      "两者都不计入占用。")
    A("")
    A("---")
    A("")
    A("## 2. 引脚预算核算")
    A("")
    A("| 类别 | 根数 | GPIO |")
    A("|---|---|---|")
    A("| 射频侧 | 15 | `4,5,6,7,8,9,12,13,14,15,16,17,18,21,47` |")
    A("| 系统 | 3 | `1,2,48`(48 = 板载 WS2812,✅ 已核实);39 因蜂鸣器不存在而释放 |")
    A("| OLED I²C | **(2)** | `40,41` —— ⏸ **下一批**(引脚已预留,不计入本批实装的 19 根) |")
    A("| 配对按键 | 1 | `38`(2026-09-21 由 48 改来:48 实为指示灯) |")
    A("| 控制通道 | **0** | `43,44` 复用 UART0(CH343P);`19,20` 原生 USB 不可用 |")
    A("| **合计占用** | **21** | 含 OLED 预留 2 根 |")
    A("| **可用池** | **25** | 29 个候选(45 GPIO − 16 不可用:26–37、19/20、43/44 = 29)− 4 strapping(`0/3/45/46`)= **25** |")
    A("| **余量** | **+4** | 本批实装 19 根 ⇒ 当前 **+6** |")
    A("")
    A("**余量的边界**:+4 根够用,但**不足以再加一条 SPI 主机**(需 3 根,且只能走 GPIO Matrix,"
      "时钟质量更差)。这正是「单 SPI2 总线 + 3 CS」(§4)成立的原因。")
    A("")
    A("---")
    A("")
    A("## 3. 三模块接线")
    A("")
    A("共享三线(SCK/MOSI/MISO),各自独立 **CS / 中断 / 使能**;M1 另有 **SDN**,"
      "M3 的 **CE** 由 AND 门驱动。模块官方针序见 "
      "[`docs/02-hardware.md`](../../docs/02-hardware.md) §3.1 与 "
      "[`as10-pin-card.png`](as10-pin-card.png)。")
    A("")
    A("| 槽位 | 模块 | 芯片 | 模块脚号(官方针序) | 共享 | CS | 中断 | SDN / CE | 电源 | VCC/GND |")
    A("|---|---|---|---|---|---|---|---|---|---|")
    for m in MODULES:
        pins = "%s · %s" % (m["cs_pin"], m["irq_pin"])
        if m["sdn_pin"] != "—":
            pins += " · " + m["sdn_pin"]
        if m["ce_pin"] != "—":
            pins += " · " + m["ce_pin"]
        if m["sdn"] is not None:
            extra = "SDN=**%d**(开漏)+ 外部 100k↑" % m["sdn"]
        elif m["ce"] is not None:
            extra = "CE=**%s**(pin3)" % m["ce"]
        else:
            extra = "—"
        A("| %d | %s | %s | `%s` | `SCK 12 / MOSI 13 / MISO 14` | **%d** | **%d**(%s) | %s | EN=%d → %s | 3.3V / GND |"
          % (m["slot"], m["name"], m["chip"], pins, m["cs"], m["irq"], m["irq_name"],
             extra, m["en"], m["rail"]))
    A("")
    A("- **AS10(M1)SDN 由 GPIO9 控制(开漏)**:GPIO9 为**开漏输出** + 外部 **100kΩ 上拉到 3.3V**(上电默认 shutdown);"
      "POR 脉冲:**释放(上拉为高)≥100µs → 驱动低 → 等 ≥10ms**,再初始化 SPI。"
      "开漏高=高阻,即使旧现场仍把 SDN 接 GND 也**不会短路**(但拉不高、必须改接 GPIO9)。")
    A("- **AS01(M3)没有独立 EN 脚**:模块电源由 LDO-C(EN=GPIO6)控制;**CE(pin3)由 "
      "AND(GPIO7,GPIO8) 输出直接驱动**。`CE=0 ⇒ nRF24 power-down、PA 不工作`;"
      "KILL 保持低时,M3 仍可由 GPIO6 上电做 SPI 只读探测(CE 低,不发射);"
      "M3 收发必须 GPIO7=1 **且** KILL 释放。**不新增 CE GPIO**。")
    A("- 每模块只用 1 根中断:Si4463 `nIRQ` / CC1101 `GDO0` / nRF24 `IRQ`;"
      "其余中断脚按芯片可空(Si4463 GPIO0–3;CC1101 GDO1/GDO2)。**M1 的中断叫 nIRQ,不是 GDO0。**")
    A("- **天线:三支独立 SMA 座,禁止并联或用合路器**(失配 + 违背 C-02)。")
    A("")
    A("---")
    A("")
    A("## 4. 共享 SPI2 总线")
    A("")
    A("```")
    A("ESP32-S3 SPI2_HOST          三条独立 SPI 网络,分别并联到 3 个模块")
    A("    SCK  12 ────┬───────────┬───────────┬────► M1 / M2 / M3 SCK")
    A("    MOSI 13 ────┼───────────┼───────────┼────► M1 / M2 / M3 MOSI")
    A("    MISO 14 ────┴───────────┴───────────┴────► M1 / M2 / M3 MISO")
    A("")
    A("    三根 CS 各自独立,与上面三线没有任何共点:")
    A("    CS 15 ───────────────────────────────────► M1 NSEL(AS10 pin5)")
    A("    CS 16 ───────────────────────────────────► M2 CSN(AS07 pin4)")
    A("    CS 17 ───────────────────────────────────► M3 CSN(AS01 pin4)")
    A("```")
    A("")
    A("- **IO_MUX**:只有 **SCK(12)是 `FSPICLK` 默认脚**,走 IO_MUX;**MOSI(13)/MISO(14)不是**"
      "(S3 的默认是 `FSPID=GPIO11`、`FSPIQ=GPIO13`、`FSPIWP=GPIO14`),必须经 **GPIO 矩阵**路由。"
      "1MHz 下矩阵损失可忽略;提高时钟前须实测波形。")
    A("- 由 `rf_hal` 串行化;**禁止在 CS 有效期间插入日志 / JSON / NVS 写**(否则错帧)。")
    A("- ⚠️ 协议上报的 `bus` 是**整数 `2`**(`SPI2_HOST`),**不是字符串 `\"spi2\"`** —— "
      "schema 为 `integer` 且 `additionalProperties:false`,上报字符串会被开发期校验判非法。")
    A("")
    A("---")
    A("")
    A("## 5. 有线控制通道(CH343P → UART0)")
    A("")
    A("```")
    A("手机(USB 主机)──► 开发板 Type-C「COM」口 ──► CH343P(1a86:55d3)──► UART0")
    A("                                                        GPIO43 = TX / GPIO44 = RX")
    A("                                                        ⇄ ESP32-S3 控制流(921600)")
    A("")
    A("原生 USB(GPIO19 D− / GPIO20 D+):本板从未枚举成功 ──► 不可用、不是控制通道")
    A("```")
    A("")
    A("- **控制通道就是那块开发板自带的 CH343P(COM 口),载板无需另做 USB 控制口**;"
      "插上后主机出现 CDC-ACM 设备 `1a86:55d3 → /dev/ttyACM0`。")
    A("- CH343P 的 DTR/RTS 接了自动复位:**打开 COM 口会让设备复位一次**,App 必须处理。")
    A("- ⛔ **原生 USB 19/20 不可用**:本板实测从未枚举成功;**JTAG 随之失去**(39–42 已放弃)。"
      "不要把它设计成 OTG / 控制通道,也不要承诺保留 JTAG。")
    A("- console / 调试日志走 UART0(与协议帧同一物理口):量产固件**必须关闭非协议输出**,"
      "否则 App 会在控制通道读到日志。")
    A("- 设备**自行供电**,不依赖手机 OTG 供电(手机过流保护会导致掉电重启)。")
    A("")
    A("---")
    A("")
    A("## 6. 电源树与发射门控链")
    A("")
    A("```")
    A("USB-C 5V / 18650                                            ┌── 100µF+100nF×2 ─► M1 VCC")
    A("     │                                                      │")
    A("     ▼                                                      │")
    A(" DC-DC 降压(3.3V,≥2A) ──┬── 3.3V 主轨 ─► 主控 / 按键 / LED")
    A("                        │")
    A("                        ├── LDO-A(EN←GPIO4,100k↓) ──────────┤")
    A("                        ├── LDO-B(EN←GPIO5,100k↓) ── 100µF+100nF×2 ─► M2 VCC")
    A("                        └── LDO-C(EN←GPIO6,100k↓) ── 100µF+100nF×2+470µF+π 滤波 ─► M3 VCC")
    A("")
    A("GPIO7(2.4G 总闸,上电默认低) ──┐")
    A("                             ├──► AND 门 ──► AS01 pin3 CE")
    A("GPIO8(RF_KILL,低有效,100k↓) ─┘")
    A("```")
    A("")
    A("- **布尔式**:`M3 CE = GPIO7 ∧ GPIO8`(单门 AND 输出直接接 AS01 pin3);"
      "**M3 模块电源**则单独由 `LDO-C EN = GPIO6` 软件使能。两个门控对象不同,不存在双驱动。")
    A("- **CE=0 ⇒ nRF24 处于 power-down、PA 不工作**;"
      "KILL 低时 CE 被 AND 门钳在 0。")
    A("- **KILL 保持低时 M3 仍可被探测**:GPIO6 给 LDO-C 上电、CE 保持低,"
      "nRF24 处于 power-down,SPI 只读探测安全、不会发射;M3 收发必须 GPIO7=1 且 KILL 释放。")
    A("- **KILL 必须外部下拉**、**LDO EN(4/5/6)各加 100kΩ 下拉**:上电、复位、MCU 崩溃期间 "
      "EN/KILL 浮空会被读成高 ⇒ 模块意外上电或意外允许发射。")
    A("- 模块 VCC **标称 3.3V**:DC-DC 3.3V 经低 dropout LDO,实测 **3.0–3.3V**;"
      "三模块均可在 **1.8–3.6V** 内工作,实测偏低不是故障。")
    A("- ⚠️ **`RF_KILL` 在协议契约里没有定义** —— 它只存在于硬件文档 §3.3/§4(GPIO8)"
      "与固件文档(启动顺序与已知缺口)。App 无法查询门控状态;建议补进契约的 `STATUS`/`CAPS`。")
    A("- 每模块独立 LDO + 就近储能:20dBm 的 µs 级电流阶跃靠 LDO 环路来不及。"
      "M3 不工作时可关 LDO-C 轨(GPIO6=0)以省电。")
    A("")
    A("---")
    A("")
    A("## 7. 外设")
    A("")
    A("| 外设 | 引脚 | 接线要点 |")
    A("|---|---|---|")
    A("| OLED SSD1306 0.96\" 128×64 ⏸ **下一批不装** | SDA 40 / SCL 41(预留) | **必须 I²C 4 针版**;4.7kΩ 上拉 3.3V;供电取主 3.3V,**不挂 M3 键控轨**。**本批把 40/41 留空,不要挪用**;⚠️ 40/41 与 JTAG(39–42)冲突,本产品原生 USB/JTAG 不可用、已放弃 JTAG |")
    A("| 配对按键 SW1 | 38 | 内部上拉,低有效;外并 100nF 去抖;**不可用 BOOT 键复用**。⚠️ 2026-09-21 由 48 改来 |")
    A("| 板载 WS2812 | **48** | ✅ **已核实**:单线**可寻址** RGB LED,不是普通 GPIO 灯 ⇒ 驱动占 RMT;⚠️ **停更即保持最后颜色**,故 `KILL` 必须由固件**显式写黑**;⚠️ 山寨板可能**未焊接 2812 信号** |")
    A("| 电池分压 | 1 | ADC1_CH0,1:2 分压 |")
    A("| NTC 温度 | 2 | ADC1_CH1,10kΩ 分压 |")
    A("")
    A("### 7.1 使用与存储")
    A("")
    A("- **USB 线分两种,别买错**:充电线常只有电源两根线,无 D+/D−,插上不通信。"
      "需**数据线**;手机侧接 **OTG 转接头/线**(手机作 USB 主机)。")
    A("- 本图交付物是 **SVG 矢量图 + PNG 位图**,可直接在浏览器打开、打印或导入绘图软件标注。")
    A("- AS10 模块针序/认 pin1 见 [`as10-pin-card.png`](as10-pin-card.png)(现场引脚定位卡)。")
    A("")
    A("---")
    A("")
    A("## 8. 接线前必查")
    A("")
    A("| # | 检查项 | 踩错的后果 |")
    A("|---|---|---|")
    for i, (t, sub) in enumerate(CHECKS):
        A("| %d | %s | %s |" % (i + 1, t, sub))
    A("")
    return "\n".join(L) + "\n"


def self_check():
    errs = []
    seen = {}
    for pin, _d, sig, grp, _n in PINS:
        if pin in seen:
            errs.append("GPIO %d 被重复分配:%s / %s" % (pin, seen[pin], sig))
        seen[pin] = sig
        if grp not in GROUPS:
            errs.append("未知分组 '%s'(信号 %s)" % (grp, sig))
        if pin in STRAPPING:
            errs.append("GPIO %d 是 strapping 脚,本设计不得使用(%s)" % (pin, sig))
        if pin not in POOL:
            errs.append("GPIO %d 不在可用池内(%s)" % (pin, sig))
    for pin, _d, sig, grp, _n in CONTROL_PINS:
        if pin in seen:
            errs.append("GPIO %d 同时出现在控制/不可用块与常规分配:%s" % (pin, sig))
        if grp not in CONTROL_GROUPS:
            errs.append("控制脚 %s 的分组 '%s' 未知" % (sig, grp))
        if pin in STRAPPING:
            errs.append("GPIO %d 是 strapping 脚" % pin)
    # 预算分类求和
    for gkey, want in EXPECT_GROUP_COUNTS.items():
        got = len([p for p in PINS if p[3] == gkey])
        if got != want:
            errs.append("分组 '%s' 根数 %d ≠ 预期 %d" % (gkey, got, want))
    occupied = len(PINS)
    margin = len(POOL) - occupied
    installed = occupied - EXPECT_GROUP_COUNTS["oled"]
    current_margin = len(POOL) - installed
    if occupied != EXPECT_OCCUPIED:
        errs.append("占用根数 %d ≠ docs/02-hardware.md §3 的 %d" % (occupied, EXPECT_OCCUPIED))
    if margin != EXPECT_MARGIN:
        errs.append("余量 %d ≠ docs/02-hardware.md §3 的 +%d" % (margin, EXPECT_MARGIN))
    if installed != EXPECT_INSTALLED:
        errs.append("实装根数 %d ≠ 文档的 %d" % (installed, EXPECT_INSTALLED))
    if current_margin != EXPECT_CURRENT_MARGIN:
        errs.append("当前余量 +%d ≠ 文档的 +%d" % (current_margin, EXPECT_CURRENT_MARGIN))
    # 池与不可用集合的完整性:占用 + 未用池 + 不可用 + strapping 应覆盖 0–48
    all_gpio = set(range(0, 22)) | set(range(26, 49))
    accounted = (set(POOL) | set(STRAPPING) | set(UNAVAILABLE) |
                 {p[0] for p in CONTROL_PINS})
    missing = sorted(all_gpio - accounted)
    if missing:
        errs.append("以下 GPIO 既不在池、也不在不可用/strapping: %s" % missing)
    # 模块专线必须能在 PINS 中找到;CE/SDN 的映射必须与 ARCH 一致
    for m in MODULES:
        for k in ("cs", "irq", "en"):
            if m[k] not in seen:
                errs.append("模块 %s 的 %s=GPIO%d 不在 PINS 中" % (m["name"], k, m[k]))
        if m["slot"] == 1:
            if m["sdn"] != 9 or 9 not in seen:
                errs.append("M1 的 SDN 必须是 GPIO9 且计入 PINS")
            if m["ce"] is not None:
                errs.append("M1 不应有 CE")
        else:
            if m["sdn"] is not None:
                errs.append("槽位%d 不应有 SDN(只有 M1 的 AS10 有 SDN)" % m["slot"])
        if m["slot"] == 3:
            if m["ce"] != "AND(7,8)":
                errs.append("M3 的 CE 必须是 AND(7,8)")
            if 7 not in seen or 8 not in seen:
                errs.append("M3 的 CE=AND(7,8) 输入 GPIO7/GPIO8 不在 PINS 中")
        elif m["ce"] is not None:
            errs.append("槽位%d 不应有 CE" % m["slot"])
        if m["irq_name"] and m["slot"] == 1 and m["irq_name"] != "nIRQ":
            errs.append("M1 的中断名必须是 nIRQ(不是 GDO0)")
    for gkey in GROUPS:
        if not [p for p in PINS if p[3] == gkey]:
            errs.append("分组 '%s' 没有任何引脚" % gkey)
    return errs, occupied, margin, installed, current_margin


def find_text_collisions(svg_text):
    """几何自检:算出每个 <text> 的包围盒,报告真实相交。

    为什么需要:把图缩到能一眼看全时,10px 的中文字根本看不清有没有压字。
    所以「无压字」必须由几何计算来证明,不能靠看图。
    字宽用 text_w() 保守估算;返回 [(重叠宽, 重叠高, 文本A, 文本B), ...]。
    """
    groups = re.findall(r'<g transform="translate\(([-\d.]+),([-\d.]+)\)">(.*?)</g>',
                        svg_text, re.S)
    boxes = []
    for gx, gy, body in groups:
        ox, oy = float(gx), float(gy)
        for m in re.finditer(r'<text x="([-\d.]+)" y="([-\d.]+)"[^>]*font-size="([\d.]+)"'
                             r'[^>]*text-anchor="(\w+)"[^>]*>(.*?)</text>', body):
            x, y, size, anc = (float(m.group(1)), float(m.group(2)),
                               float(m.group(3)), m.group(4))
            s = (m.group(5).replace('&amp;', '&').replace('&lt;', '<').replace('&gt;', '>'))
            w = text_w(s, size)
            x0 = ox + x if anc == 'start' else (ox + x - w if anc == 'end' else ox + x - w / 2)
            boxes.append((x0, oy + y - size * 0.80, w, size * 1.05, s, ox, oy))
    out = []
    for i in range(len(boxes)):
        for j in range(i + 1, len(boxes)):
            a, b = boxes[i], boxes[j]
            if (a[5], a[6]) != (b[5], b[6]):
                continue                      # 不同面板之间不比较
            ow = min(a[0] + a[2], b[0] + b[2]) - max(a[0], b[0])
            oh = min(a[1] + a[3], b[1] + b[3]) - max(a[1], b[1])
            if ow > 1.5 and oh > 1.5:
                out.append((round(ow, 1), round(oh, 1), a[4], b[4]))
    return out


def find_panel_overflow(svg):
    """面板自检:每个面板的内容包围盒必须 ≤ 面板矩形(容忍 0.5px 描边)。"""
    errs = []
    for i, p in enumerate(svg.panels, start=1):
        b = p.get("bbox")
        if b is None:
            errs.append("面板%d「%s」没有任何内容" % (i, p["title"]))
            continue
        if b[0] < -0.5 or b[1] < -0.5 or b[2] > p["w"] + 0.5 or b[3] > p["h"] + 0.5:
            errs.append("面板%d「%s」内容溢出:bbox=(%.1f,%.1f,%.1f,%.1f),面板 %.0f×%.0f"
                        % (i, p["title"], b[0], b[1], b[2], b[3], p["w"], p["h"]))
    return errs


def _regenerate_to_tmp(svg_text, md_text):
    tmp = tempfile.mkdtemp(prefix="rf-lab-wiring-")
    try:
        svg_path = os.path.join(tmp, "wiring-diagram.svg")
        md_path = os.path.join(tmp, "README.md")
        with open(svg_path, "w", encoding="utf-8") as f:
            f.write(svg_text)
        with open(md_path, "w", encoding="utf-8") as f:
            f.write(md_text)
        return tmp, svg_path, md_path
    except Exception:
        shutil.rmtree(tmp, ignore_errors=True)
        raise


def check_committed_artifacts(root, svg_text, md_text):
    """把生成物重写到临时目录,与仓库里已提交的文件逐字节比对。"""
    errs = []
    out_dir = os.path.join(root, "hardware", "wiring")
    tmp, svg_tmp, md_tmp = _regenerate_to_tmp(svg_text, md_text)
    try:
        for rel, tmp_path in (("wiring-diagram.svg", svg_tmp), ("README.md", md_tmp)):
            committed = os.path.join(out_dir, rel)
            if not os.path.exists(committed):
                errs.append("缺少已提交的生成物 hardware/wiring/%s" % rel)
                continue
            with open(tmp_path, "rb") as f:
                new = f.read()
            with open(committed, "rb") as f:
                old = f.read()
            if new != old:
                errs.append("生成物过期:hardware/wiring/%s 与生成器输出不一致"
                            "(重跑 python3 tools/gen_wiring_diagram.py)" % rel)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return errs


def render_png(out_dir, svg_path):
    """尝试把 SVG 渲染成 PNG(可选增强;无渲染器时只提示,不失败)。"""
    png_path = os.path.join(out_dir, "wiring-diagram.png")
    for exe, cmd in (
        ("rsvg-convert", ["rsvg-convert", "-o", png_path, svg_path]),
        ("libreoffice", ["libreoffice", "--headless", "--convert-to",
                         "png:writer_png_Export", "--outdir", out_dir, svg_path]),
        ("inkscape", ["inkscape", svg_path, "--export-filename=" + png_path]),
    ):
        if not shutil.which(exe):
            continue
        try:
            subprocess.run(cmd, check=True, timeout=300,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print("✓ 已写入 %s(由 %s 渲染;生成物,可重建)" % (
                os.path.relpath(png_path, out_dir), exe))
        except Exception as e:  # 渲染失败不影响 SVG 交付
            print("· PNG 渲染跳过(%s):%s" % (exe, e))
        return
    print("· 未找到 SVG 渲染器,跳过 PNG(不影响 SVG 与 Markdown 交付)")


def main():
    ap = argparse.ArgumentParser(description="生成接线图(SVG + Markdown),docs/02 §3 的可执行镜像")
    ap.add_argument("--check", action="store_true", help="只做自检,不写文件")
    ap.add_argument("--out-dir", default=None, help="生成到指定目录(跳过已提交生成物比对)")
    args = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = args.out_dir or os.path.join(root, "hardware", "wiring")

    errs, occupied, margin, installed, current_margin = self_check()
    if errs:
        print("数据自检失败:", file=sys.stderr)
        for e in errs:
            print("  ✗ " + e, file=sys.stderr)
        return 1
    print("✓ 数据自检通过:占用 %d / 可用池 %d / 余量 +%d / 实装 %d ⇒ 当前 +%d"
          "(与 docs/02-hardware.md §3 一致)"
          % (occupied, len(POOL), margin, installed, current_margin))

    svg_obj = build_svg()
    svg_text = svg_obj.render()
    md_text = build_markdown()

    # 几何自检:面板不溢出 + 文本不压字
    overflow = find_panel_overflow(svg_obj)
    if overflow:
        print("✗ 面板自检失败:%d 处内容溢出" % len(overflow), file=sys.stderr)
        for e in overflow:
            print("   " + e, file=sys.stderr)
        print("   ⇒ 调整面板高度/坐标后再提交。", file=sys.stderr)
        return 1
    print("✓ 面板自检通过:%d 个面板内容均不溢出" % len(svg_obj.panels))

    cols = find_text_collisions(svg_text)
    if cols:
        print("✗ 几何自检失败:发现 %d 处文本相交" % len(cols), file=sys.stderr)
        for ow, oh, s1, s2 in sorted(cols, key=lambda t: -(t[0] * t[1]))[:15]:
            print("   %.0f×%.0fpx  «%s» × «%s»" % (ow, oh, s1[:32], s2[:32]),
                  file=sys.stderr)
        print("   ⇒ 调整布局后再提交。字宽是保守估算,轻微相交也可能只是估宽偏大。",
              file=sys.stderr)
        return 1
    print("✓ 几何自检通过:%d 个文本元素无相交" % svg_text.count("<text"))

    if args.check:
        if args.out_dir:
            print("· 指定了 --out-dir,跳过已提交生成物比对")
        else:
            drift = check_committed_artifacts(root, svg_text, md_text)
            if drift:
                print("✗ 生成物自检失败:", file=sys.stderr)
                for e in drift:
                    print("   " + e, file=sys.stderr)
                return 1
            print("✓ 生成物自检通过:重新生成的 SVG/README 与仓库文件逐字节一致"
                  "(PNG 为栅格派生物,不做字节比对)")
        return 0

    os.makedirs(out_dir, exist_ok=True)
    svg_path = os.path.join(out_dir, "wiring-diagram.svg")
    md_path = os.path.join(out_dir, "README.md")
    with open(svg_path, "w", encoding="utf-8") as f:
        f.write(svg_text)
    with open(md_path, "w", encoding="utf-8") as f:
        f.write(md_text)
    print("✓ 已写入 %s" % os.path.relpath(svg_path, root))
    print("✓ 已写入 %s" % os.path.relpath(md_path, root))
    render_png(out_dir, svg_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
