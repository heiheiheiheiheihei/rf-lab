#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
接线图生成器 —— 单一数据源。

目的:让「引脚分配」只有一处定义。docs/10-hardware/010-system-architecture.md §2.3 的
引脚表 → 本文件的 PINS → 生成 SVG 接线图 与 Markdown 接线说明。**三者不必手工对齐**,
因为后两者都是从 PINS 派生的;改一处而忘另一处这种错在本流程里不可能发生。

为什么需要它:接线图最危险的错误不是画错线,而是「图与表各说一套」。脚本把这种错误
变成不可能,并且每次运行都重算引脚账(占用/余量),数字对不上就退出 1。

用法:
    python3 tools/gen_wiring_diagram.py            # 生成(覆盖)
    python3 tools/gen_wiring_diagram.py --check    # 只校验,不写文件(提交前/CI)

退出码:0 成功 / 1 数据自检失败 / 2 参数错误
依赖:仅 Python3 标准库(本机无 pip,不引入任何第三方库)。
"""

import argparse
import os
import re
import sys

# ============================================================================
# 单一数据源
# 依据:docs/10-hardware/010-system-architecture.md §2.3(【建议】,尚未冻结为最终板图)
# 纪律:改这里之前先改 §2.3 文档;文档是权威,本表是它的可执行镜像。
# ============================================================================

# (GPIO, 方向, 信号, 分组, 备注)  —— 这些是**计入 21 根占用**的引脚
PINS = [
    # 共享 SPI2 总线(IO_MUX 直连)
    (12, "out", "SPI2 FSPICLK → SCK",  "spi", "IO_MUX 直连,时序最优"),
    (13, "out", "SPI2 FSPID → MOSI",   "spi", "IO_MUX 直连"),
    (14, "in",  "SPI2 FSPIQ → MISO",   "spi", "IO_MUX 直连"),
    # M1 · AS10-M4463D-SMA(Si4463)
    (15, "out", "M1 CS",   "m1", "片选,低有效"),
    (18, "in",  "M1 GDO0", "m1", "中断输入(GDO1/nIRQ 可留空)"),
    (4,  "out", "M1 EN",   "m1", "LDO-A 使能,高有效"),
    # M2 · AS07-M1101D-SMA(CC1101)
    (16, "out", "M2 CS",   "m2", "片选,低有效"),
    (21, "in",  "M2 GDO0", "m2", "中断输入(GDO2 可留空)"),
    (5,  "out", "M2 EN",   "m2", "LDO-B 使能,高有效"),
    # M3 · AS01-ML01(nRF24L01P + RFX2401C)
    (17, "out", "M3 CS",   "m3", "片选,低有效"),
    (47, "in",  "M3 IRQ",  "m3", "中断输入"),
    (6,  "out", "M3 EN",   "m3", "LDO-C(2.4G 模块轨)使能,高有效"),
    # 发射硬件门控链(安全相关)
    (7,  "out", "2.4G 总闸", "gate", "经 AND 门 → PA 使能(见 ⑥)"),
    (8,  "out", "RF KILL",   "gate", "低有效;外部下拉,上电默认禁止发射"),
    # 系统
    (1,  "in",  "电池分压 ADC", "sys", "ADC1_CH0,经 1:2 分压"),
    (2,  "in",  "NTC 温度",     "sys", "ADC1_CH1,10kΩ 分压"),
    (38, "out", "状态 LED ×2",  "sys", "RMT/LEDC 可驱动 WS2812"),
    (39, "out", "蜂鸣器",       "sys", "PWM 驱动"),
    # OLED(I²C,4 针版)—— ⏸ 2026-09-21 延期到下一批:引脚预留,本批不装、不写驱动
    (40, "io",  "OLED SDA", "oled", "⏸ 下一批(I²C 预留,本批不装)"),
    (41, "io",  "OLED SCL", "oled", "⏸ 下一批(I²C 预留,本批不装)"),
    # 配对按键
    (48, "in",  "配对按键", "btn", "内部上拉,低有效;外并 100nF 去抖"),
]

# USB 复用内建控制器,**不新增占用**(见 §2.3)
USB_PINS = [
    (19, "io", "USB D−", "usb", "USB Serial/JTAG,内部 PHY"),
    (20, "io", "USB D+", "usb", "⚠️ 不可配成 USB-OTG 外设用途"),
]

POOL = [1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21,
        38, 39, 40, 41, 42, 47, 48]          # 25 根可用
STRAPPING = [0, 3, 45, 46]                    # 不使用
# 不可用:SPI0/1 Flash, OPI PSRAM, UART0 调试, USB
UNAVAILABLE = [26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 43, 44]

EXPECT_OCCUPIED = 21
EXPECT_MARGIN = 4

GROUPS = {
    "spi":  ("共享 SPI2 总线", "#2563eb"),
    "m1":   ("M1 · Si4463",   "#1d4ed8"),
    "m2":   ("M2 · CC1101",   "#047857"),
    "m3":   ("M3 · nRF24+PA", "#c2410c"),
    "gate": ("发射门控",      "#b91c1c"),
    "usb":  ("USB 有线通道",  "#7c3aed"),
    "oled": ("OLED I²C ⏸",   "#0e7490"),
    "sys":  ("系统",          "#475569"),
    "btn":  ("按键",          "#a16207"),
}

# 每个射频模块必须接的 6 根:3 共享 + CS + 中断 + EN,另加 VCC/GND
MODULES = [
    dict(slot=1, name="AS10-M4463D-SMA", chip="Si4463", cs=15, irq=18,
         en=4, rail="LDO-A", band="425–525MHz", pwr="20dBm", col="#1d4ed8",
         func="433MHz OOK 遥控收发(唯一)"),
    dict(slot=2, name="AS07-M1101D-SMA", chip="CC1101", cs=16, irq=21,
         en=5, rail="LDO-B", band="387–464MHz", pwr="10dBm", col="#047857",
         func="Sub-GHz 通用收发"),
    dict(slot=3, name="AS01-ML01", chip="nRF24L01P+RFX2401C", cs=17, irq=47,
         en=6, rail="LDO-C(2.4G)", band="2400–2525MHz", pwr="20dBm(PA)", col="#c2410c",
         func="2.4GHz 嗅探/发射"),
]

CJK = "Noto Sans CJK SC, WenQuanYi Zen Hei, Source Han Sans SC, sans-serif"
MONO = "Noto Sans Mono CJK SC, DejaVu Sans Mono, monospace"
CANVAS_W = 1240


def esc(t):
    return t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class Svg:
    """按「面板」累积元素,再统一排版。

    为什么要这样做:如果每个面板各自写死 y 坐标,面板一长就会互相压字。这里让每个
    面板只描述自己的内容高度,纵向位置由 flow() 累加计算 —— 面板变长时下面的面板自动下移。
    """

    def __init__(self):
        self.chrome = []
        self.panels = []

    # ---- 面板内绘制(坐标相对面板左上角)----
    def rect(self, x, y, w, h, fill="#fff", stroke="#cbd5e1", rx=8, sw=1.2, dash=None):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        self._cur.append('<rect x="%g" y="%g" width="%g" height="%g" rx="%g" fill="%s" '
                         'stroke="%s" stroke-width="%g"%s/>' % (x, y, w, h, rx, fill, stroke, sw, d))

    def text(self, x, y, s, size=13, fill="#0f172a", anchor="start",
             weight="normal", family=None):
        self._cur.append('<text x="%g" y="%g" font-family="%s" font-size="%g" fill="%s" '
                         'text-anchor="%s" font-weight="%s">%s</text>'
                         % (x, y, family or CJK, size, fill, anchor, weight, esc(s)))

    def line(self, x1, y1, x2, y2, stroke="#94a3b8", sw=1.6, dash=None):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        self._cur.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="%s" '
                         'stroke-width="%g" stroke-linecap="round"%s/>'
                         % (x1, y1, x2, y2, stroke, sw, d))

    def poly(self, pts, stroke="#94a3b8", sw=1.8, fill="none"):
        d = " ".join("%g,%g" % pt for pt in pts)
        self._cur.append('<polyline points="%s" fill="%s" stroke="%s" stroke-width="%g" '
                         'stroke-linejoin="round" stroke-linecap="round"/>' % (d, fill, stroke, sw))

    def circle(self, cx, cy, r, fill="#94a3b8"):
        self._cur.append('<circle cx="%g" cy="%g" r="%g" fill="%s"/>' % (cx, cy, r, fill))

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
        self._cur_w = width or (CANVAS_W - 80)
        builder(self)
        self.panels.append(dict(title=title, sub=sub, accent=accent,
                                h=height, w=self._cur_w, body=list(self._cur)))

    def render(self):
        # 先算总高
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
        'ESP32-S3N16R8 + 三射频模块 + USB 有线控制通道 · OLED 预留(下一批) · '
        '三模块全部经免焊排针/母座(约束 C-03)</text>' % CJK)
    s.chrome.append('<rect x="40" y="88" width="1160" height="46" rx="8" '
                    'fill="#fffbeb" stroke="#fcd34d"/>')
    s.chrome.append(
        '<text x="56" y="107" font-family="%s" font-size="12.5" fill="#92400e">'
        '⚠️ 本图是【建议】布线的可执行镜像,不是最终板图;引脚以 '
        'docs/10-hardware/010-system-architecture.md §2.3 为准。</text>' % CJK)
    s.chrome.append(
        '<text x="56" y="125" font-family="%s" font-size="12.5" fill="#92400e">'
        '改动纪律:①改 §2.3 文档 → ②改 tools/gen_wiring_diagram.py 的 PINS → '
        '③重跑生成器。三处一致性由脚本强制。</text>' % CJK)

    # ---------------- ① 引脚分配 ----------------
    ROWH = 22.0
    n_rows = len(PINS) + len(GROUPS) - 1 + 2  # 引脚 + 分组标题 + USB 2 行
    p1_h = 80 + n_rows * ROWH + 56

    def b1(s):
        # 列位:信号列要放得下 "SPI2 FSPICLK → SCK"(约 170px),方向列不能被它压住
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
        gcol = GROUPS["usb"][1]
        s.rect(12, y - 15, 1136, ROWH * 2, fill="#faf5ff", stroke="#e9d5ff", rx=4)
        s.text(26, y, "USB 复用内建控制器(不新增占用,见 §2.3)", size=11.5,
               weight="bold", fill=gcol)
        y += ROWH
        for pin, direction, sig, _g, note in USB_PINS:
            s.rect(cols[0], y - 13.5, 44, 17, fill=gcol, rx=3.5)
            s.text(cols[0] + 22, y - 1, str(pin), size=11.5, fill="#fff",
                   anchor="middle", weight="bold")
            s.text(cols[1], y, sig, size=12, family=MONO)
            s.text(cols[2], y, direction, size=11, fill="#64748b", family=MONO)
            s.text(cols[3], y, "USB Serial/JTAG", size=11, fill=gcol)
            s.text(cols[4], y, note, size=11, fill="#475569")
            y += ROWH
        yb = y + 6
        s.rect(12, yb, 1136, 42, fill="#eff6ff", stroke="#bfdbfe", rx=6)
        s.text(26, yb + 18,
               "占用 21 = 射频 14(SCK/MOSI/MISO + CS×3 + 中断×3 + EN×3 + 总闸 + KILL)"
               " + 系统 4 + OLED 2(⏸ 下一批,预留)+ 按键 1;USB 计 0(复用内建控制器)。",
               size=11.5, fill="#1e40af")
        s.text(26, yb + 36,
               "余量 +4(含 OLED 预留)= 本批实装 19 根 ⇒ 当前 +6:够用,但不足以再加一条 SPI 主机"
               "(需 3 根且只能走 GPIO Matrix)—— 这是「单总线 + 3 CS」的成立边界。",
               size=11.5, fill="#64748b")
    s.panel("① ESP32-S3N16R8 引脚分配", p1_h, b1,
            sub="可用池 25 / 占用 21 / 余量 +4")

    # ---------------- ② SPI 总线 + ③ 模块接线 ----------------
    def b2(s):
        bx, by = 30, 74
        s.chip(bx, by, 134, 84, "ESP32-S3", "SPI2_HOST", "#eff6ff", "#93c5fd", "#1d4ed8")
        bus_x = bx + 214
        s.text(bus_x + 8, by - 32, "SCK · MOSI · MISO 并联", size=10,
               fill="#1d4ed8", family=MONO)
        s.text(bus_x - 20, by - 10, "三线并联 · IO_MUX 直连", size=10.5, fill="#64748b")
        for i in range(3):
            s.line(bx + 134, by + 24 + i * 20, bus_x, by + 24 + i * 20, "#2563eb", 2.2)
            s.circle(bus_x, by + 24 + i * 20, 3.4, "#2563eb")
        for i, m in enumerate(MODULES):
            ty = by - 12 + i * 34
            s.line(bus_x, by + 24, bus_x, ty + 13, "#2563eb", 1.4)
            s.line(bus_x, ty + 13, bus_x + 18, ty + 13, m["col"], 1.8)
            s.rect(bus_x + 18, ty, 278, 26, fill="#f8fafc", stroke=m["col"], rx=5)
            s.text(bus_x + 28, ty + 18, "CS %2d → %s" % (m["cs"], m["name"]),
                   size=11, fill=m["col"], family=MONO)
        s.text(bx, 236, "同一时刻只有一个 CS 有效;全部射频 SPI 访问经 rf_hal 串行化,",
               size=11, fill="#475569")
        s.text(bx, 253, "禁止在 CS 有效期间插入日志 / JSON / NVS 写(否则错帧,R-09)。",
               size=11, fill="#475569")
    s.panel("② 共享 SPI2 总线(方案 B:单总线 + 3 CS)", 270, b2,
            sub="省 6 根引脚", accent="#2563eb", width=560)

    def b3(s):
        tx = 22
        s.text(tx, 58, "槽位 / 模块", size=11, weight="bold", fill="#64748b")
        s.text(tx + 200, 58, "芯片", size=11, weight="bold", fill="#64748b")
        for lbl, dx in (("CS", 320), ("中断", 380), ("使能 EN", 440), ("VCC / GND", 540)):
            s.text(tx + dx, 58, lbl, size=11, weight="bold", fill="#64748b")
        ty = 78
        for m in MODULES:
            s.rect(tx - 10, ty - 19, 566, 28, fill="#f8fafc", stroke="#e2e8f0", rx=5)
            s.rect(tx - 10, ty - 19, 4, 28, fill=m["col"], rx=2)
            s.text(tx, ty - 1, "槽位%d %s" % (m["slot"], m["name"]), size=11.5)
            s.text(tx + 200, ty - 1, m["chip"], size=10, fill="#64748b", family=MONO)
            s.text(tx + 320, ty - 1, str(m["cs"]), size=11.5, fill=m["col"],
                   family=MONO, weight="bold")
            s.text(tx + 380, ty - 1, str(m["irq"]), size=11.5, fill=m["col"], family=MONO)
            s.text(tx + 440, ty - 1, "%d · %s" % (m["en"], m["rail"]), size=10.5,
                   fill=m["col"], family=MONO)
            s.text(tx + 540, ty - 1, "3.3V / GND", size=10.5, fill="#64748b", family=MONO)
            ty += 34
        ty += 30
        for line in [
            "GDO1 / GDO2 / nIRQ 可留空 —— 本设计每模块只用 1 根中断线。",
            "天线:三支独立 SMA 座,禁止并联或用合路器(失配 + 违背 C-02)。",
            "⚠️ M3 的 PA 使能必须经 AND 门(见 ⑥),不要直接接到 M3 的 EN。",
        ]:
            s.text(tx, ty, line, size=11,
                   fill="#b91c1c" if line.startswith("⚠️") else "#475569")
            ty += 18
    s.panel("③ 射频模块接线(每模块 3 根 GPIO)", 270, b3,
            sub="全部免焊排针 / 母座", accent="#c2410c", width=584)

    # ---------------- ④ USB + ⑤ 外设 ----------------
    def b4(s):
        ux, uy = 24, 74
        s.chip(ux, uy, 118, 66, "USB-C 母座", "J1 · 16P", "#faf5ff", "#c4b5fd", "#6d28d9", 11.5)
        s.text(ux, uy + 86, "丝印必须标「USB 控制/调试」", size=10, fill="#7c3aed")
        s.text(ux, uy + 102, "设备自行供电,不靠手机 OTG 供电", size=10, fill="#b45309")
        s.chip(ux + 140, uy, 94, 66, "22Ω ×2", "串阻", "#fff", "#c4b5fd", "#6d28d9", 11)
        s.chip(ux + 256, uy, 104, 66, "ESD 阵列", "D1", "#fff", "#c4b5fd", "#6d28d9", 11)
        s.chip(ux + 382, uy, 122, 66, "D+ / D−", "GPIO20 / GPIO19",
               "#faf5ff", "#a78bfa", "#5b21b6", 11)
        for x1, x2 in ((ux + 118, ux + 140), (ux + 234, ux + 256), (ux + 360, ux + 382)):
            s.line(x1, uy + 33, x2, uy + 33, "#7c3aed", 2)
        s.text(ux, uy + 126, "VBUS → 自恢复保险丝 F1 → 5V 主轨;并检测 VBUS 以抑制深睡(USB-R4)。",
               size=10.5, fill="#475569")
        s.text(ux, uy + 142, "D+/D− 走线避开 RF 前端与 SMA 馈点。", size=10.5, fill="#475569")
        for i, t in enumerate([
            "✅ 零额外芯片、零额外引脚,且保留 JTAG 调试。",
            "⚠️ 19/20 与 USB-OTG 外设互斥 —— 不要配成 OTG。",
            "⚠️ 量产固件须关闭 USB console 输出,否则 App 会读到日志(R-14)。",
        ]):
            s.text(24, uy + 168 + i * 17, t, size=10.5,
                   fill="#b91c1c" if t.startswith("⚠️") else "#047857")
    s.panel("④ USB 有线控制通道(ADR-0003)", 268, b4,
            sub="复用内建控制器 · 不新增占用", accent="#7c3aed", width=660)

    def b5(s):
        ox = 24
        s.chip(ox, 52, 200, 62, "OLED · SSD1306 ⏸", "0.96寸 128×64 · 4 针 I²C · 下一批",
               "#f8fafc", "#94a3b8", "#64748b", 11.5)
        for i, (t, col) in enumerate([
            ("SDA → GPIO 40    SCL → GPIO 41", "#0e7490"),
            ("4.7kΩ 上拉到 3.3V(模块常已自带上拉)", "#475569"),
            ("供电取主 3.3V —— 不挂 M3 键控 PA 轨(R-11)", "#b91c1c"),
            ("必须 I²C 4 针版;SPI 7 针版吃掉全部余量(R-10)", "#b91c1c"),
        ]):
            s.text(ox + 6, 132 + i * 16, t, size=9.5, fill=col)
        s.line(ox, 208, ox + 412, 208, "#e2e8f0", 1)
        rows = [
            ("配对按键 SW1", "GPIO 48(内部上拉,低有效,并 100nF 去抖)", "#a16207"),
            ("状态 LED ×2", "GPIO 38(RMT/LEDC,可驱动 WS2812)", "#475569"),
            ("蜂鸣器", "GPIO 39(PWM)", "#475569"),
        ]
        for i, (lbl, val, col) in enumerate(rows):
            yy = 230 + i * 19
            s.text(ox + 6, yy, lbl, size=10.5, weight="bold", fill=col)
            s.text(ox + 108, yy, val, size=10, fill="#475569")
    s.panel("⑤ 外设:OLED(⏸ 下一批)/ 按键 / 指示", 266, b5, accent="#0e7490", width=468)

    # ---------------- ⑥ 电源 / 门控 + ⑦ 必查 ----------------
    def b6(s):
        gx, gy = 24, 78
        s.chip(gx, gy, 116, 46, "USB-C 5V", "或 18650", "#fef2f2", "#fca5a5", "#b91c1c", 11)
        s.chip(gx, gy + 66, 116, 46, "DC-DC 3.3V", "≥2A", "#fef2f2", "#fca5a5", "#b91c1c", 11)
        s.line(gx + 58, gy + 46, gx + 58, gy + 66, "#b91c1c", 2.4)
        # 主轨说明放在顶部空白处 —— 原来放在走线轨上会压住 LDO 盒
        s.text(gx, 44, "3.3V 主轨 → 主控 / 按键 / LED(OLED 下一批)", size=10, fill="#b91c1c")
        main_y = gy + 104
        rail_x = gx + 140
        s.line(gx + 116, main_y, rail_x, main_y, "#b91c1c", 2.4)
        lx = gx + 154
        for i, m in enumerate(MODULES):
            ly = gy - 30 + i * 70
            s.chip(lx, ly, 138, 46, m["rail"], "EN ← GPIO %d" % m["en"],
                   "#fff", m["col"], m["col"], 11)
            s.poly([(rail_x, main_y), (rail_x, ly + 23), (lx, ly + 23)], "#b91c1c", 1.6)
            s.text(lx + 148, ly + 20, "→ %s" % m["name"], size=10.5, fill=m["col"])
            extra = "100µF + 100nF×2" + (" + 470µF + π 滤波" if m["slot"] == 3 else "")
            s.text(lx + 148, ly + 35, extra, size=9.5, fill="#64748b")
        s.text(gx, 292, "退耦电容紧贴模块 VCC(<5mm);地回流短而宽。M3 不工作时彻底断电该轨,而非仅置 idle。",
               size=10.5, fill="#475569")
        # AND 门嵌板放在右侧空白区(原来压住 M3 的走线与去耦说明)
        ax, ay = 736, 226
        s.rect(ax, ay, 280, 132, fill="#fef2f2", stroke="#fca5a5", rx=8)
        s.text(ax + 140, ay + 20, "发射硬件门控(安全)", size=11.5, weight="bold",
               anchor="middle", fill="#b91c1c")
        s.text(ax + 14, ay + 40, "GPIO 7 = 2.4G 总闸", size=9.5, fill="#b91c1c", family=MONO)
        s.text(ax + 14, ay + 54, "GPIO 8 = RF KILL(低有效)", size=9.5, fill="#b91c1c", family=MONO)
        s.poly([(ax + 14, ay + 64), (ax + 58, ay + 64), (ax + 58, ay + 90),
                (ax + 14, ay + 90), (ax + 14, ay + 64)], "#b91c1c", 1.8, "#fff")
        s.text(ax + 36, ay + 81, "AND", size=10, anchor="middle", fill="#b91c1c")
        s.line(ax + 58, ay + 77, ax + 84, ay + 77, "#b91c1c", 2.2)
        s.text(ax + 88, ay + 80, "→ LDO-C 的 EN(2.4G 模块轨)", size=9.5,
               weight="bold", fill="#b91c1c")
        s.text(ax + 14, ay + 106, "固件依据:发射链 [7] 硬件门控 —— 软件两路都置位才可能发射;", size=9, fill="#b91c1c")
        s.text(ax + 14, ay + 120, "KILL 未释放时 PA 轨仍无电,故门控落在 LDO EN 上。", size=9, fill="#b91c1c")
    s.panel("⑥ 电源树与发射门控链", 320, b6,
            sub="每模块独立 LDO + 就近储能", accent="#b91c1c", width=1160)

    def b7(s):
        checks = [
            ("M3 的 PA 使能必须过 AND 门", "直连 EN 会让 RF KILL 形同虚设"),
            ("KILL 必须外部下拉", "上电/复位期间可能意外允许发射"),
            ("OLED ⏸ 下一批,引脚 40/41 预留", "本批不装;挪用了下一批就要改板"),
            ("OLED 必须 I²C 4 针版(下一批)", "SPI 版吃掉全部 4 根余量"),
            ("OLED 不挂 M3 键控 PA 轨(下一批)", "否则关模块时屏幕一起灭"),
            ("USB 19/20 不配成 OTG", "会同时破坏控制通道与 JTAG"),
            ("M3 天线拉远 + 正交极化", "20dBm 会压掉本机 Wi-Fi/BLE"),
            ("每模块独立 LDO + 就近 100µF", "µs 级电流阶跃导致复位"),
            ("三根天线不可并联 / 合路", "失配 + 违背 C-02"),
            ("strapping 0/3/45/46 一律不用", "上电模式误判"),
            ("USB 线必须是数据线", "充电线无 D+/D−,插上不通"),
        ]
        yy = 58
        for i, (t, sub) in enumerate(checks):
            s.circle(20, yy - 4, 7.5, "#b45309")
            s.text(20, yy - 1, str(i + 1), size=9.5, fill="#fff", anchor="middle", weight="bold")
            s.text(36, yy - 1, t, size=10.5, weight="bold")
            s.text(36, yy + 13, sub, size=9.5, fill="#64748b")
            yy += 28
    s.panel("⑦ 接线前必查(踩错就冒烟或失效)", 320, b7, accent="#b45309", width=444)

    return s


def build_markdown():
    L = []
    A = L.append
    A("# 接线图与引脚分配")
    A("")
    A("状态:`draft` · 最后更新:2026-09-20 · 上游依据:"
      "[`docs/10-hardware/010-system-architecture.md`](../../docs/10-hardware/010-system-architecture.md) §2.3、"
      "[`docs/adr/0003-usb-wired-control-channel.md`](../../docs/adr/0003-usb-wired-control-channel.md)")
    A("")
    A("> ⚠️ **本文件与 [`wiring-diagram.svg`](wiring-diagram.svg) 均由 "
      "[`tools/gen_wiring_diagram.py`](../../tools/gen_wiring_diagram.py) 生成,不要手工编辑。**")
    A("> 引脚分配的权威是 `docs/10-hardware/010-system-architecture.md` §2.3;"
      "本文件是它的可执行镜像。要改引脚:**先改 §2.3 文档 → 再改生成器的 `PINS` → 重跑生成器**。")
    A("> 生成器的自检会在「占用根数 ≠ 21」或「同一 GPIO 被分配两次」时退出 1,"
      "所以图、表与文档不可能悄悄漂移。")
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
    A("### 复用内建控制器(**不新增占用**)")
    A("")
    A("| GPIO | 信号 | 方向 | 备注 |")
    A("|---|---|---|---|")
    for pin, direction, sig, _g, note in USB_PINS:
        A("| **%d** | `%s` | %s | %s |" % (pin, sig, direction, note))
    A("")
    A("---")
    A("")
    A("## 2. 引脚预算核算")
    A("")
    A("| 类别 | 根数 | GPIO |")
    A("|---|---|---|")
    A("| 射频侧 | 14 | `4,5,6,7,8,12,13,14,15,16,17,18,21,47` |")
    A("| 系统 | 4 | `1,2,38,39` |")
    A("| OLED I²C | **(2)** | `40,41` —— ⏸ **下一批**(引脚已预留,不计入本批实装的 19 根) |")
    A("| 配对按键 | 1 | `48` |")
    A("| USB 控制通道 | **0** | `19,20` 早已是本用途 |")
    A("| **合计占用** | **21** | |")
    A("| **可用池** | **25** | 27 个候选 − strapping `0/3/45/46` |")
    A("| **余量** | **+4** | |")
    A("")
    A("**余量的边界**:+4 根够用,但**不足以再加一条 SPI 主机**(需 3 根,且只能走 GPIO Matrix,"
      "时钟质量更差)。这正是「单 SPI2 总线 + 3 CS」(§4)成立的原因。")
    A("")
    A("---")
    A("")
    A("## 3. 三模块接线")
    A("")
    A("共享三线,各自独立 CS / 中断 / 使能。")
    A("")
    A("| 槽位 | 模块 | 芯片 | 频率(模块级) | 功率 | 共享 | CS | 中断 | 使能 | VCC/GND |")
    A("|---|---|---|---|---|---|---|---|---|---|")
    for m in MODULES:
        A("| %d | %s | %s | %s | %s | `SCK 12 / MOSI 13 / MISO 14` | **%d** | **%d** | **%d**（%s） | 3.3V / GND |"
          % (m["slot"], m["name"], m["chip"], m["band"], m["pwr"], m["cs"], m["irq"],
             m["en"], m["rail"]))
    A("")
    A("- `GDO1` / `GDO2` / `nIRQ` 可留空 —— 本设计每模块只用 1 根中断线。")
    A("- **天线:三支独立 SMA 座,禁止并联或用合路器**(失配 + 违背 C-02)。")
    A("- ⚠️ **M3 的 PA 使能必须经 AND 门**(见 §6),不要直接接到 M3 的 EN。")
    A("")
    A("---")
    A("")
    A("## 4. 共享 SPI2 总线")
    A("")
    A("```")
    A("ESP32-S3 SPI2_HOST")
    A("    SCK  12 ──┬──────┬──────┬──► M1 / M2 / M3 SCK")
    A("    MOSI 13 ──┼──────┼──────┼──► M1 / M2 / M3 MOSI")
    A("    MISO 14 ──┴──────┴──────┴──► M1 / M2 / M3 MISO")
    A("    CS   15 ────────────────────► M1 CS")
    A("    CS   16 ────────────────────► M2 CS")
    A("    CS   17 ────────────────────► M3 CS")
    A("```")
    A("")
    A("- 三根线走成**一束短线**,全部落 IO_MUX 直连脚,时序最优。")
    A("- 由 `rf_hal` 串行化;**禁止在 CS 有效期间插入日志 / JSON / NVS 写**(否则错帧,R-09)。")
    A("- ⚠️ 协议上报的 `bus` 是**整数 `2`**(`SPI2_HOST`),**不是字符串 `\"spi2\"`** —— "
      "schema 为 `integer` 且 `additionalProperties:false`,上报字符串会被开发期校验判非法。")
    A("")
    A("---")
    A("")
    A("## 5. USB 有线控制通道")
    A("")
    A("```")
    A("USB-C J1 ──D+/D−──┬─ 22Ω ×2 ──┬─ USBLC6-2SC6 ──┬── GPIO20 = D+")
    A("                  │           │   (ESD 阵列)   └── GPIO19 = D−")
    A("                  └─ 走线避开 RF 前端与 SMA 馈点")
    A("VBUS ── 自恢复保险丝 F1 ── 5V 主轨(并检测 VBUS 以抑制深睡)")
    A("```")
    A("")
    A("- ✅ 零额外芯片、零额外引脚、**保留 JTAG 调试**。")
    A("- ⚠️ `19/20` 与 **USB-OTG 外设互斥** —— 不要配成 OTG(会同时破坏控制通道与调试)。")
    A("- ⚠️ 量产固件**必须关闭 USB 上的 console 输出**,否则 App 会在控制通道读到调试日志(USB-R3/R-14)。")
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
    A("                        ├── LDO-A(EN←GPIO4) ─────────────────┤")
    A("                        ├── LDO-B(EN←GPIO5) ── 100µF+100nF×2 ─► M2 VCC")
    A("                        └── LDO-C(EN←GPIO6) ── 100µF+100nF×2+470µF+π 滤波 ─► M3 VCC")
    A("                                                                   │")
    A("GPIO7(2.4G 总闸) ──┐")
    A("                   ├──► AND 门 ──► LDO-C 的 EN(2.4G 模块轨)")
    A("GPIO8(RF KILL,低有效)┘      ▲")
    A("                   外部下拉 ⇒ 上电默认禁止发射")
    A("```")
    A("")
    A("> ⚠️ **图中把 2.4G 模块轨串在 GPIO8 的使能链上,而文档表格只写了 `EN ← GPIO6`**。"
      "两者不矛盾,但必须按本图读:GPIO6 是**软件**使能,AND 输出才是 **LDO-C 的 EN**(见下方依据)。")
    A("")
    A("- **门控的是「PA 轨的 LDO 使能」,不是模块单独的 PA 脚** —— 这一点由固件文档写死,不是我选的:"
      "[`docs/20-firmware/010-firmware-architecture.md`](../../docs/20-firmware/010-firmware-architecture.md) §6 的发射链 `[7] 硬件门控` 写明"
      "「`KILL` 引脚 + 2.4GHz 总闸,**软件两路都置位才可能发射**」,理由是"
      "「即使固件被换成恶意版本,**KILL 未释放时 PA 轨仍无电**」。**PA 轨无电**这个措辞只在「门控 LDO EN」下成立。")
    A("- **为什么 M3 要单独一条门控链**:RFX2401C 是 20dBm PA,会压掉本机 Wi-Fi/BLE(R-01)。"
      "因此使能**必须**是「软件想开」**且**「硬件 KILL 未拉低」的逻辑与 —— 单靠软件置位不足以称为门控。")
    A("- **KILL 必须外部下拉**:上电、复位、MCU 崩溃期间 KILL 悬空会被读成高 ⇒ 意外允许发射。")
    A("- ⚠️ **`RF_KILL` 在协议契约里根本没有定义** —— 它只存在于硬件文档 §2.3(GPIO8)"
      "与固件文档(§6 步骤 [7]、§9 中止路径)。"
      "**这是一处归属缺口**:一个安全相关信号没有任何契约字段或状态可观测,"
      "App 无法查询门控是否处于 `KILL`;OLED 本要显示它,而 OLED 又延期 ⇒ 本批完全不可观测。"
      "建议补进契约的 `STATUS`/`CAPS`,否则「显示门控状态」这条需求没有数据来源。")
    A("- 每模块独立 LDO + 就近储能:20dBm 的 µs 级电流阶跃靠 LDO 环路来不及(§6)。")
    A("- M3 不工作时**彻底断电**该轨,而不是仅置芯片 idle。")
    A("")
    A("---")
    A("")
    A("## 7. 外设")
    A("")
    A("| 外设 | 引脚 | 接线要点 |")
    A("|---|---|---|")
    A("| OLED SSD1306 0.96\" 128×64 ⏸ **下一批不装** | SDA 40 / SCL 41(预留) | **必须 I²C 4 针版**;4.7kΩ 上拉 3.3V;供电取主 3.3V,**不挂 M3 键控轨**。**本批把 40/41 留空,不要挪用** |")
    A("| 配对按键 SW1 | 48 | 内部上拉,低有效;外并 100nF 去抖;**不可用 BOOT 键复用** |")
    A("| 状态 LED ×2 | 38 | RMT/LEDC 可驱动 WS2812 |")
    A("| 蜂鸣器 | 39 | PWM 驱动 |")
    A("| 电池分压 | 1 | ADC1_CH0,1:2 分压 |")
    A("| NTC 温度 | 2 | ADC1_CH1,10kΩ 分压 |")
    A("")
    A("### 7.1 使用与存储")
    A("")
    A("- **USB 线分两种,别买错**:充电线常只有电源两根线,无 D+/D−,插上不通信。"
      "需**数据线**;手机侧接 **OTG 转接头/线**(手机作 USB 主机)。")
    A("- 本图交付物是 **SVG 矢量图**,可直接在浏览器打开、打印或导入绘图软件标注。")
    A("")
    A("---")
    A("")
    A("## 8. 接线前必查")
    A("")
    A("| # | 检查项 | 踩错的后果 |")
    A("|---|---|---|")
    for i, (t, sub) in enumerate([
        ("M3 的 PA 使能必须过 AND 门", "RF KILL 形同虚设,失去硬件发射门控"),
        ("KILL 必须外部下拉", "上电/复位期间可能意外允许发射"),
        ("OLED ⏸ 下一批:40/41 必须留空", "挪用后下一批要改板"),
        ("OLED 必须 I²C 4 针版(下一批)", "吃掉全部 4 根余量,被迫砍功能"),
        ("OLED 不挂 M3 键控 PA 轨(下一批)", "关模块时屏幕一起灭,失去本地状态出口"),
        ("USB 19/20 不配成 OTG", "同时破坏控制通道与 JTAG 调试"),
        ("M3 天线拉远 + 正交极化", "20dBm 发射压掉本机 Wi-Fi/BLE"),
        ("每模块独立 LDO + 就近 100µF", "发射瞬态导致复位或频谱劣化"),
        ("三根天线不可并联 / 合路", "失配 + 违背 C-02"),
        ("strapping 0/3/45/46 一律不用", "上电模式误判"),
        ("USB 线必须是数据线", "插上不通信(只充电)"),
    ]):
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
    for pin, _d, sig, _g, _n in USB_PINS:
        if pin in seen:
            errs.append("GPIO %d 同时出现在 USB 与常规分配:%s" % (pin, sig))
        if pin in STRAPPING:
            errs.append("GPIO %d 是 strapping 脚" % pin)
    occupied = len(PINS)
    margin = len(POOL) - occupied
    if occupied != EXPECT_OCCUPIED:
        errs.append("占用根数 %d ≠ 文档 §2.3 的 %d" % (occupied, EXPECT_OCCUPIED))
    if margin != EXPECT_MARGIN:
        errs.append("余量 %d ≠ 文档 §2.3 的 +%d" % (margin, EXPECT_MARGIN))
    for m in MODULES:
        for k in ("cs", "irq", "en"):
            if m[k] not in seen:
                errs.append("模块 %s 的 %s=GPIO%d 不在 PINS 中" % (m["name"], k, m[k]))
    for gkey in GROUPS:
        if gkey == "usb":
            # USB 走 USB_PINS(复用内建控制器,不计入 21 根),不参与常规分组校验
            if not USB_PINS:
                errs.append("分组 'usb' 没有任何引脚")
            continue
        if not [p for p in PINS if p[3] == gkey]:
            errs.append("分组 '%s' 没有任何引脚" % gkey)
    return errs, occupied, margin


def find_text_collisions(svg_text):
    """几何自检:算出每个 <text> 的包围盒,报告真实相交。

    为什么需要:把图缩到能一眼看全时,10px 的中文字根本看不清有没有压字 ——
    本图第一版就有 12 处相交(表头互压、AND 门压在 M3 走线上),肉眼在缩略图上完全看不出来。
    所以「无压字」必须由几何计算来证明,不能靠看图。

    字宽按经验估:全角 1.0em、ASCII 0.55em、常用符号 0.7em。**偏保守(高估宽度)**,
    宁可误报也不漏报。返回 [(重叠宽, 重叠高, 文本A, 文本B), ...]。
    """
    import unicodedata
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
            w = 0.0
            for ch in s:
                if unicodedata.east_asian_width(ch) in ('W', 'F'):
                    w += 1.0
                elif ch in ' ·—→':
                    w += 0.7
                else:
                    w += 0.55
            w *= size
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


def main():
    ap = argparse.ArgumentParser(description="生成接线图(SVG + Markdown),单一数据源")
    ap.add_argument("--check", action="store_true", help="只做数据自检,不写文件")
    ap.add_argument("--out-dir", default=None)
    args = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = args.out_dir or os.path.join(root, "hardware", "wiring")

    errs, occupied, margin = self_check()
    if errs:
        print("数据自检失败:", file=sys.stderr)
        for e in errs:
            print("  ✗ " + e, file=sys.stderr)
        return 1
    print("✓ 数据自检通过:占用 %d / 可用池 %d / 余量 +%d(与文档 §2.3 一致)"
          % (occupied, len(POOL), margin))

    # 几何自检也纳入 --check,这样改完布局不必再手工验一遍(压字在缩略图上看不出来)
    svg_text = build_svg().render()
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
        return 0

    os.makedirs(out_dir, exist_ok=True)
    svg_path = os.path.join(out_dir, "wiring-diagram.svg")
    md_path = os.path.join(out_dir, "README.md")
    with open(svg_path, "w", encoding="utf-8") as f:
        f.write(svg_text)
    with open(md_path, "w", encoding="utf-8") as f:
        f.write(build_markdown())
    print("✓ 已写入 %s" % os.path.relpath(svg_path, root))
    print("✓ 已写入 %s" % os.path.relpath(md_path, root))

    # 顺带产出 PNG:手机浏览器/文件管理器对 SVG 支持不稳,PNG 是实际可用的交付形态。
    # 这一步是**可选增强** —— 本机没有渲染器时不应该让生成失败,只提示。
    import shutil
    import subprocess
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
                os.path.relpath(png_path, root), exe))
        except Exception as e:  # 渲染失败不影响 SVG 交付
            print("· PNG 渲染跳过(%s):%s" % (exe, e))
        break
    else:
        print("· 未找到 SVG 渲染器,跳过 PNG(不影响 SVG 与 Markdown 交付)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
