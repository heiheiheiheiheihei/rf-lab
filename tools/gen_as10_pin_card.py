#!/usr/bin/env python3
"""生成 AS10-M4463D-SMA 引脚定位卡(现场接线用,按官方机械图 6.1/6.2 真实比例绘制)。

为什么需要这张卡:AS10 的 12 个脚是**两排相距 13.5mm 的 1×6**(DIP 式),
不是常见的 2.54mm 紧邻双排;而且 **pin1(GND) 与 pin12(VCC) 在靠近 SMA 的那一端**,
pin6(MOSI)/pin7(MISO) 在最远离 SMA 的那一端。从错的一端开始数会整体镜像,
现象是"模块完全不应答"(VCC 没供上、SDN 被 shutdown)。

关键事实(与官方手册 6.2 一致):
  * 两排各 6 个焊盘,**靠 SMA 端的 pin1 与 pin12 两个焊盘都是方形**(方向标记),
    其余为圆形 —— 不能用"方块 = pin1"来唯一识别,否则可能把 VCC 排的方块当 GND;
  * 两排中心距 13.5mm(板宽 16mm,两侧各 1.25mm);
  * 纵向:靠 SMA 端第一排距板边 8.93mm,远离 SMA 端最后一排距板边 2.37mm,
    行内间距 2.54mm(8.93 + 5×2.54 + 2.37 = 24mm)。

本文件是现场辅助图,不是权威:权威仍是 docs/02-hardware.md §3.1 与官方手册。

用法:
    python3 tools/gen_as10_pin_card.py            # 生成/覆盖 hardware/wiring/as10-pin-card.png
    python3 tools/gen_as10_pin_card.py --check    # 校验几何比例 + 生成物是否过期(退出码 0/1)
"""
import argparse
import io
import os
import sys

from PIL import Image, ImageDraw, ImageFont

W, H = 1600, 1180
OUT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "hardware",
                                    "wiring", "as10-pin-card.png"))

BG = (255, 255, 255)
PCB = (31, 78, 121)
PCB_EDGE = (15, 40, 65)
SMA = (180, 148, 90)
TXT = (17, 24, 39)
MUTED = (75, 85, 99)
RED = (185, 28, 28)
GREEN = (21, 128, 61)
BLUE = (29, 78, 216)
ORANGE = (194, 120, 3)
PURPLE = (126, 34, 206)
BLACK = (17, 24, 39)
GRAY = (130, 140, 155)

FONT = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
FONT_B = "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"

# ---- 真实比例常数(单位:px/mm;--check 会断言绘制值 == 官方值)----
SC = 22.0                     # 22 px / mm
BOARD_W_MM, BOARD_L_MM = 16.0, 24.0
PITCH_MM = 2.54
ROW_SPAN_MM = 13.5            # 两排中心距(官方 6.2)
SMA_MARGIN_MM = 8.93          # 板边(SMA 端)到第一排中心(官方 6.2)
FAR_MARGIN_MM = 2.37          # 板边(远端)到最后一排中心(官方 6.2)


def font(size, bold=False, index=2):
    path = FONT_B if bold else FONT
    try:
        return ImageFont.truetype(path, size, index=index)
    except Exception:
        return ImageFont.truetype(path, size)


def build_card():
    """按真实比例绘制卡片;返回 (PIL Image, layout dict)。"""
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)

    # ---------- 标题 ----------
    d.text((50, 26), "AS10-M4463D-SMA 引脚定位卡(Si4463 / M1)",
           font=font(40, True), fill=TXT)
    d.text((50, 84),
           "按官方手册「6.1 机械尺寸 / 6.2 参考焊盘」真实比例绘制:12 脚 = 两排 1×6,"
           "两排相距 13.5mm(不是 2.54mm 紧邻双排)",
           font=font(21), fill=MUTED)
    d.text((50, 118),
           "pin1(GND)与 pin12(VCC)都在【靠近 SMA 的那一端】;这两端的焊盘都是方形。"
           "MOSI(6)/MISO(7)在【最远离 SMA 的那一端】",
           font=font(21, True), fill=RED)
    d.line((50, 156, W - 50, 156), fill=(226, 232, 240), width=2)

    # ---------- 绘制坐标(真实比例)----------
    SC_L = SC
    mw, mh = BOARD_W_MM * SC_L, BOARD_L_MM * SC_L      # 352 × 528
    mx0, my0 = 470, 340
    mx1, my1 = mx0 + mw, my0 + mh
    PITCH = PITCH_MM * SC_L
    x_left = mx0 + (BOARD_W_MM - ROW_SPAN_MM) / 2 * SC_L     # 1.25mm 边距
    x_right = mx0 + (BOARD_W_MM + ROW_SPAN_MM) / 2 * SC_L
    y_first = my0 + SMA_MARGIN_MM * SC_L
    y_last = y_first + 5 * PITCH
    R = 14

    # SMA 接头(模块上方)
    sma_w, sma_h = 92, 72
    sx0 = mx0 + (mw - sma_w) / 2
    sy0 = my0 - sma_h - 8
    d.rounded_rectangle((sx0, sy0, sx0 + sma_w, sy0 + sma_h), radius=8,
                        fill=SMA, outline=PCB_EDGE, width=2)
    for i in range(6):
        yy = sy0 + 10 + i * 10
        d.line((sx0 + 12, yy, sx0 + sma_w - 12, yy), fill=(120, 96, 55), width=2)
    d.text((sx0 + sma_w + 10, sy0 + 4), "SMA", font=font(26, True), fill=(120, 96, 55))
    d.text((sx0 + sma_w + 10, sy0 + 38), "天线座", font=font(19), fill=(120, 96, 55))

    # 模块本体
    d.rounded_rectangle((mx0, my0, mx1, my1), radius=10, fill=PCB, outline=PCB_EDGE, width=3)
    d.text((mx0 + mw / 2 - 66, my0 + 22), "AS10", font=font(26, True), fill=(220, 230, 240))
    d.text((mx0 + mw / 2 - 82, my0 + 56), "M4463D", font=font(20), fill=(180, 200, 220))

    # 两排孔:左排(12..7)、右排(1..6),pin1/pin12 在 SMA 端(上)
    LEFT = [(12, "VCC", RED), (11, "GPIO0", GRAY), (10, "GPIO1", GRAY),
            (9, "IRQ", GRAY), (8, "SCK", ORANGE), (7, "MISO", GREEN)]
    RIGHT = [(1, "GND", BLACK), (2, "SDN", RED), (3, "GPIO3", GRAY),
             (4, "GPIO2", GRAY), (5, "NSEL", PURPLE), (6, "MOSI", BLUE)]

    def pad(cx, cy, num, col, square):
        if square:
            # 方形焊盘:白底 + 彩色内方块(方向标记)
            d.rectangle((cx - R, cy - R, cx + R, cy + R), fill=(245, 245, 245),
                        outline=col, width=3)
            d.rectangle((cx - R + 6, cy - R + 6, cx + R - 6, cy + R - 6), fill=col)
            d.text((cx, cy), str(num), font=font(15, True), fill=(255, 255, 255),
                   anchor="mm")
        else:
            d.ellipse((cx - R, cy - R, cx + R, cy + R), fill=(245, 245, 245),
                      outline=col, width=3)
            d.text((cx, cy), str(num), font=font(15, True), fill=col, anchor="mm")

    for i, (num, name, col) in enumerate(LEFT):
        cx, cy = x_left, y_first + i * PITCH
        pad(cx, cy, num, col, square=(num == 12))
        d.text((mx0 - 12, cy), name, font=font(23, True), fill=col, anchor="rm")

    for i, (num, name, col) in enumerate(RIGHT):
        cx, cy = x_right, y_first + i * PITCH
        pad(cx, cy, num, col, square=(num == 1))
        d.text((mx1 + 12, cy), name, font=font(23, True), fill=col, anchor="lm")

    # ---------- 端头标注 ----------
    top_y = 205
    d.text((mx0 - 120, top_y - 30),
           "▼ SMA 端:pin1(GND)与 pin12(VCC)在同一端,两排这一端的焊盘都是方形",
           font=font(21, True), fill=RED)
    d.line((mx0 - 120, top_y, mx0 + mw + 150, top_y), fill=RED, width=2)

    bot_y = my1 + 20
    d.line((mx0 - 120, bot_y, mx0 + mw + 150, bot_y), fill=BLUE, width=2)
    d.text((mx0 - 120, bot_y + 6),
           "▲ 远离 SMA 端:pin6(MOSI)与 pin7(MISO)是这一对",
           font=font(21, True), fill=BLUE)

    # 两排间距标注(真实比例:13.5mm)
    mid_x = (x_left + x_right) / 2
    mid_y = y_first + 2.5 * PITCH
    d.line((x_left + R + 6, mid_y, x_right - R - 6, mid_y), fill=(180, 200, 220), width=1)
    d.text((mid_x - 42, mid_y - 26), "13.5mm", font=font(17), fill=(200, 215, 230))

    # ---------- 图例(修正:两排 SMA 端焊盘都是方形)----------
    lx, ly = mx0 - 120, my1 + 56
    d.rectangle((lx, ly, lx + 24, ly + 24), fill=(245, 245, 245), outline=BLACK, width=3)
    d.rectangle((lx + 6, ly + 6, lx + 18, ly + 18), fill=BLACK)
    d.text((lx + 34, ly - 1),
           "= 方形焊盘:方向标记,两排靠 SMA 端各有一个(pin1 与 pin12)",
           font=font(18, True), fill=TXT)
    d.ellipse((lx + 560, ly, lx + 584, ly + 24), fill=(245, 245, 245),
              outline=BLACK, width=3)
    d.text((lx + 594, ly - 1), "= 圆形焊盘", font=font(18, True), fill=TXT)
    d.text((lx, ly + 32),
           "SMA 端 = pin1(GND) 与 pin12(VCC) 所在端;行归属按官方图 5-1/6.1 的左右列,"
           "不要只凭「方块 = pin1」认方向。",
           font=font(19), fill=TXT)

    # ---------- 右侧接线表 ----------
    tx, ty = 1000, 250
    d.text((tx, ty - 46), "M1 接线(ESP32-S3)", font=font(28, True), fill=TXT)
    rows = [
        ("模块脚", "名称", "接到"),
        ("1", "GND", "GND", BLACK),
        ("2", "SDN", "GPIO9(开漏,100k↑)", RED),
        ("5", "NSEL", "GPIO15", PURPLE),
        ("6", "MOSI", "GPIO13", BLUE),
        ("7", "MISO", "GPIO14", GREEN),
        ("8", "SCK", "GPIO12", ORANGE),
        ("9", "IRQ", "GPIO18(可空)", GRAY),
        ("12", "VCC", "3.3V(实测 3.0–3.3V)", RED),
        ("3/4/10/11", "GPIO", "可空", GRAY),
    ]
    rowh = 44
    for j, row in enumerate(rows):
        y = ty + j * rowh
        if j == 0:
            d.rectangle((tx, y, tx + 540, y + rowh - 6), fill=(241, 245, 249))
            d.text((tx + 14, y + 6), row[0], font=font(20, True), fill=TXT)
            d.text((tx + 150, y + 6), row[1], font=font(20, True), fill=TXT)
            d.text((tx + 300, y + 6), row[2], font=font(20, True), fill=TXT)
        else:
            col = row[3]
            d.text((tx + 14, y + 6), row[0], font=font(22, True), fill=col)
            d.text((tx + 150, y + 6), row[1], font=font(22, True), fill=col)
            d.text((tx + 300, y + 6), row[2], font=font(19), fill=TXT)
    by = ty + len(rows) * rowh + 16
    d.rounded_rectangle((tx, by, tx + 540, by + 176), radius=10, fill=(254, 242, 242),
                        outline=RED, width=2)
    d.text((tx + 16, by + 12), "如果从「远离 SMA」那一端开始数(错误接法):",
           font=font(21, True), fill=RED)
    d.text((tx + 16, by + 46), "VCC 会落到 MISO 脚、GND 落到 MOSI 脚,", font=font(19), fill=TXT)
    d.text((tx + 16, by + 74), "SDN 接 GND 或悬空 ⇒ 芯片 shutdown、完全不应答", font=font(19), fill=TXT)
    d.text((tx + 16, by + 102), "= 典型现象:SPI 读 0xFF / part=0xFFFF", font=font(19), fill=TXT)
    d.text((tx + 16, by + 126), "旧接法仍接 GND:开漏高=高阻,不短路(安全),",
           font=font(17), fill=RED)
    d.text((tx + 16, by + 146), "但 SDN 拉不高 ⇒ 必须改接 GPIO9。",
           font=font(17), fill=RED)

    # ---------- 底部说明 ----------
    d.line((50, 995, W - 50, 995), fill=(226, 232, 240), width=2)
    notes = [
        "① 认方向只看【SMA 端】+ 官方左右列:两排靠 SMA 端的焊盘都是方形(不是只有 pin1);"
        "从模块另一面看左右会镜像,仍以 SMA 端为准。",
        "② 本图按真实比例:排距 13.5mm;SMA 端→第一排 8.93mm、远端→最后一排 2.37mm(官方 6.2);行内 2.54mm。",
        "③ SDN 接 GPIO9(开漏)+ 外部 100kΩ 上拉 3.3V;POR 脉冲:释放(OD 高)≥100µs → 拉低 → 等 ≥10ms。"
        "若仍接 GND:开漏不短路但拉不高,须改接 GPIO9。",
        "④ 接完先测 VCC–GND = 3.0–3.3V(三模块均可在 1.8–3.6V 内工作);固件运行后 SDN 应为低,"
        "持续为高 = 模块被 shutdown。",
    ]
    for i, t in enumerate(notes):
        d.text((50, 1014 + i * 30), t, font=font(18), fill=TXT)
    d.text((50, 1150),
           "权威:docs/02-hardware.md §3.1 + 官方手册图 5-1 / 机械图 6.1 / 焊盘图 6.2。本图仅现场辅助。",
           font=font(16), fill=MUTED)

    layout = dict(
        sc=SC_L, x_left=x_left, x_right=x_right, y_first=y_first, y_last=y_last,
        mx0=mx0, my0=my0, mx1=mx1, my1=my1, pitch=PITCH,
    )
    return img, layout


def geometry_errors(layout):
    """断言"画出来的距离 == 标注的距离",防止图与文字再次不一致。"""
    errs = []
    checks = [
        ("两排中心距", (layout["x_right"] - layout["x_left"]) / layout["sc"], ROW_SPAN_MM),
        ("SMA 端→第一排", (layout["y_first"] - layout["my0"]) / layout["sc"], SMA_MARGIN_MM),
        ("远端→最后一排", (layout["my1"] - layout["y_last"]) / layout["sc"], FAR_MARGIN_MM),
        ("行内间距", layout["pitch"] / layout["sc"], PITCH_MM),
        ("两排跨度(5×2.54)", (layout["y_last"] - layout["y_first"]) / layout["sc"], 5 * PITCH_MM),
    ]
    for name, got, want in checks:
        if abs(got - want) > 0.01:
            errs.append("几何不一致:%s 画成 %.3fmm,标注 %.2fmm" % (name, got, want))
    return errs


def main():
    ap = argparse.ArgumentParser(description="生成 AS10 引脚定位卡(真实比例)")
    ap.add_argument("--check", action="store_true", help="只校验几何比例与生成物是否过期")
    args = ap.parse_args()

    img, layout = build_card()
    errs = geometry_errors(layout)
    if errs:
        print("✗ 引脚卡几何自检失败:", file=sys.stderr)
        for e in errs:
            print("  " + e, file=sys.stderr)
        return 1
    print("✓ 引脚卡几何自检通过:两排 13.5mm / SMA 端 8.93mm / 远端 2.37mm / 行内 2.54mm"
          "(与官方 6.2 一致)")

    if args.check:
        if not os.path.exists(OUT):
            print("✗ 生成物缺失:%s(运行 python3 tools/gen_as10_pin_card.py)" % OUT,
                  file=sys.stderr)
            return 1
        buf = io.BytesIO()
        img.save(buf, format="PNG")
        new = buf.getvalue()
        with open(OUT, "rb") as f:
            old = f.read()
        if new != old:
            print("✗ 生成物过期:%s 与脚本输出不一致(重跑 python3 tools/gen_as10_pin_card.py)"
                  % os.path.relpath(OUT, os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                  file=sys.stderr)
            return 1
        print("✓ 生成物自检通过:%s 与脚本输出逐字节一致" % os.path.basename(OUT))
        return 0

    img.save(OUT)
    print("✓ 已写入 %s(%dx%d)" % (os.path.relpath(OUT, os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))), W, H))
    return 0


if __name__ == "__main__":
    sys.exit(main())
