#!/usr/bin/env python3
"""Generate the RF-Lab C3 carrier PCB (KiCad 9, pcbnew).

Layout intent
-------------
A passive 2-layer carrier that accepts:
  * 01Space ESP32-C3-0.42LCD dev board on two 2.54 mm female headers
    (1x7 + 1x8, row spacing 15.24 mm, rows aligned at the Qwiic end)
  * YS-RF34DL 315/433 MHz module on a 1x4 female header
    (VCC, TXD, RXD, GND -- module TXD -> IO4, module RXD <- IO3)
  * a spare 1x4 male header with the same four signals
  * a 2x8 male header with every remaining C3 signal
  * 4x M2.5 mounting holes and two antenna / strain-relief holes

Everything is through-hole so the board can be ordered bare from JLCPCB
and soldered by hand.  The C3 socket rows are standard 2.54 mm sockets;
no custom footprint is needed.

Coordinates are KiCad board coordinates: origin top-left, +y downwards.
"""

from __future__ import annotations

import json
import os

import pcbnew

MM = pcbnew.FromMM
FPDIR = "/usr/share/kicad/footprints"

BOARD_W = 60.0
BOARD_H = 80.0
CHAMFER = 2.5

# ---------------------------------------------------------------- footprints
# (ref, library dir, footprint name, x, y, rotation deg)
PLACEMENT = [
    ("J1", "Connector_PinSocket_2.54mm", "PinSocket_1x08_P2.54mm_Vertical", 25.24, 3.00, 0),
    ("J2", "Connector_PinSocket_2.54mm", "PinSocket_1x07_P2.54mm_Vertical", 10.00, 5.54, 0),
    ("J5", "Connector_PinHeader_2.54mm", "PinHeader_1x16_P2.54mm_Vertical", 9.00, 32.00, 90),
    ("J3", "Connector_PinSocket_2.54mm", "PinSocket_1x04_P2.54mm_Vertical", 8.00, 42.00, 90),
    ("J4", "Connector_PinHeader_2.54mm", "PinHeader_1x04_P2.54mm_Vertical", 47.00, 42.00, 0),
    ("C1", "Capacitor_SMD", "C_0805_2012Metric", 12.00, 76.50, 0),
    ("C2", "Capacitor_SMD", "C_0805_2012Metric", 16.00, 76.50, 0),
]

HOLES = [
    ("H1", 3.2, 3.2),
    ("H2", 56.8, 3.2),
    ("H3", 3.2, 76.8),
    ("H4", 56.8, 76.8),
]

ANT_HOLES = [
    ("ANT1", 43.5, 55.0),
    ("ANT2", 43.5, 70.0),
]

# ------------------------------------------------------------------- net map
J1_NETS = ["BAT", "5V", "GND", "3V3", "IO20", "IO21", "IO0", "IO1"]
J2_NETS = ["IO10", "IO8", "IO7", "IO6", "IO5", "IO4", "IO3"]
J3_NETS = ["3V3", "IO4", "IO3", "GND"]          # VCC, TXD, RXD, GND
J4_NETS = ["3V3", "GND", "IO3", "IO4"]
# J5 is a single row of 16: J2's fan on the left (1..7), a GND, J1's fan on
# the right (9..15), GND at the end.  This keeps the two fans from crossing.
J5_NETS = [
    "IO3", "IO4", "IO5", "IO6", "IO7", "IO8", "IO10", "GND",
    "IO1", "IO0", "IO21", "IO20", "3V3", "5V", "BAT", "GND",
]
NET_OF_CONN = {"J1": J1_NETS, "J2": J2_NETS, "J3": J3_NETS, "J4": J4_NETS, "J5": J5_NETS}


# ------------------------------------------------------------------- routing
# (net, layer, width_mm, [(x, y), ...])
ROUTES = [
    # power on top, outside the signal fans
    ("5V", "B.Cu", 0.5, [(25.24, 5.54), (42.02, 5.54)]),
    ("5V", "F.Cu", 0.5, [(42.02, 5.54), (42.02, 32.00)]),
    ("BAT", "B.Cu", 0.5, [(25.24, 3.00), (44.56, 3.00)]),
    ("BAT", "F.Cu", 0.5, [(44.56, 3.00), (44.56, 32.00)]),

    # 3V3: J1.4 -> J5.13 -> J3.1 and the caps; branch to J4.1
    ("3V3", "F.Cu", 0.4, [(25.24, 10.62), (39.48, 24.86), (39.48, 35.50)]),
    ("3V3", "F.Cu", 0.4, [(39.48, 35.50), (49.50, 35.50), (49.50, 42.00), (47.00, 42.00)]),
    ("3V3", "F.Cu", 0.4, [(39.48, 35.50), (39.48, 43.50), (8.00, 43.50), (8.00, 42.00)]),
    ("3V3", "F.Cu", 0.4, [(8.00, 43.50), (5.50, 43.50), (5.50, 78.00), (15.05, 78.00), (15.05, 76.50)]),
    ("3V3", "F.Cu", 0.4, [(11.05, 78.00), (11.05, 76.50)]),

    # J1 fan (right side, no crossings)
    ("IO1",  "F.Cu", 0.3, [(25.24, 20.78), (29.32, 24.86), (29.32, 32.00)]),
    ("IO0",  "F.Cu", 0.3, [(25.24, 18.24), (31.86, 24.86), (31.86, 32.00)]),
    ("IO21", "F.Cu", 0.3, [(25.24, 15.70), (34.40, 24.86), (34.40, 32.00)]),
    ("IO20", "F.Cu", 0.3, [(25.24, 13.16), (36.94, 24.86), (36.94, 32.00)]),

    # J2 fan (left side, no crossings)
    ("IO3",  "F.Cu", 0.3, [(10.00, 20.78), (9.00, 20.78), (9.00, 32.00)]),
    ("IO4",  "F.Cu", 0.3, [(10.00, 18.24), (11.54, 19.78), (11.54, 32.00)]),
    ("IO5",  "F.Cu", 0.3, [(10.00, 15.70), (14.08, 19.78), (14.08, 32.00)]),
    ("IO6",  "F.Cu", 0.3, [(10.00, 13.16), (16.62, 19.78), (16.62, 32.00)]),
    ("IO7",  "F.Cu", 0.3, [(10.00, 10.62), (19.16, 19.78), (19.16, 32.00)]),
    ("IO8",  "F.Cu", 0.3, [(10.00, 8.08), (21.70, 19.78), (21.70, 32.00)]),
    # IO10 has to dodge the J1.8 pad (25.24, 20.78)
    ("IO10", "F.Cu", 0.3, [(10.00, 5.54), (23.50, 19.04), (23.50, 30.00), (24.24, 30.74), (24.24, 32.00)]),

    # module socket: IO3/IO4 from the breakout down to J3
    ("IO3", "F.Cu", 0.3, [(9.00, 32.00), (9.00, 40.60), (13.08, 40.60), (13.08, 42.00)]),
    ("IO4", "F.Cu", 0.3, [(11.54, 32.00), (11.54, 34.20)]),
    ("IO4", "B.Cu", 0.25, [(11.54, 34.20), (11.81, 34.47), (11.81, 44.50), (10.54, 45.50), (10.54, 42.00)]),

    # spare 4-pin header: IO3/IO4 run on the bottom layer past the module
    ("IO3", "B.Cu", 0.3, [(13.08, 42.00), (13.08, 44.50), (45.50, 44.50), (45.50, 47.08), (47.00, 47.08)]),
    ("IO4", "B.Cu", 0.3, [(10.54, 42.00), (10.54, 46.50), (44.50, 46.50), (44.50, 49.62), (47.00, 49.62)]),
]

STITCH_VIAS = [
    (6.0, 12.0), (54.0, 12.0), (9.0, 62.0), (54.0, 60.0),
    (30.0, 24.0), (30.0, 40.0), (30.0, 60.0), (20.0, 70.0),
    (50.0, 52.0), (50.0, 58.0),
]

TEXTS = []


def _labels():
    for i, net in enumerate(J1_NETS):
        TEXTS.append((net, 28.3, 3.0 + i * 2.54, 0.85, 0, -1, 0, "F.Silkscreen"))
    for i, net in enumerate(J2_NETS):
        TEXTS.append((net, 6.8, 5.54 + i * 2.54, 0.85, 0, 1, 0, "F.Silkscreen"))
    # J3: pins at y=42, module body hangs down (y>42); labels above the socket
    for i, net in enumerate(["VCC", "TXD", "RXD", "GND"]):
        TEXTS.append((net, 8.0 + i * 2.54, 39.4, 0.8, 0, 0, 0, "F.Silkscreen"))
    # J4: vertical, labels to the right
    for i, net in enumerate(J4_NETS):
        TEXTS.append((net, 50.0, 42.0 + i * 2.54, 0.85, 0, -1, 0, "F.Silkscreen"))
    # J5: one row of 16, labels below the header body, rotated 90 deg
    for i, net in enumerate(J5_NETS):
        x = 9.0 + i * 2.54
        if net in ("IO3", "IO4"):
            x += 0.8          # clear the trace leaving the pad
        elif net == "3V3":
            x += 0.9
        TEXTS.append((net, x, 35.2, 0.8, 90, 1, 0, "F.Silkscreen"))


_labels()

TEXTS += [
    ("J1", 31.2, 1.2, 0.9, 0, 1, 0, "F.Silkscreen"),
    ("J2", 7.4, 2.0, 0.9, 0, 1, 0, "F.Silkscreen"),
    ("J3", 6.4, 40.8, 0.8, 0, 1, 0, "F.Silkscreen"),
    ("J4", 45.6, 41.0, 0.8, 0, 1, 0, "F.Silkscreen"),
    ("J5", 49.4, 31.0, 0.8, 0, -1, 0, "F.Silkscreen"),
    ("RF-LAB C3 CARRIER", 58.0, 60.0, 1.0, 0, 1, 0, "F.Silkscreen"),
    ("v1.0  2026-11", 58.0, 62.2, 0.85, 0, 1, 0, "F.Silkscreen"),
    ("01Space C3-0.42LCD", 58.0, 66.4, 0.9, 0, 1, 0, "F.Silkscreen"),
    ("3.3V ONLY", 46.6, 15.0, 1.3, 0, -1, 0, "F.Silkscreen"),
    ("5V = DAMAGE", 46.6, 17.6, 0.85, 0, -1, 0, "F.Silkscreen"),
    ("TXD -> IO4", 46.6, 20.0, 0.8, 0, -1, 0, "F.Silkscreen"),
    ("RXD <- IO3", 46.6, 21.8, 0.8, 0, -1, 0, "F.Silkscreen"),
    ("MODULE 32x25", 24.0, 73.8, 0.8, 0, 0, 0, "F.Silkscreen"),
    ("ANT", 45.2, 52.5, 0.8, 0, 0, 0, "F.Silkscreen"),
    ("ZIP", 43.5, 72.5, 0.8, 0, 0, 0, "F.Silkscreen"),
]


# ------------------------------------------------------------------ helpers
def add_fp(board, ref, lib, name, x, y, rot):
    fp = pcbnew.FootprintLoad(os.path.join(FPDIR, lib + ".pretty"), name)
    if fp is None:
        raise SystemExit(f"footprint not found: {lib}/{name}")
    fp.SetReference(ref)
    fp.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    fp.SetOrientationDegrees(rot)
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    board.Add(fp)
    return fp


def add_hole(board, ref, fpname, x, y):
    fp = pcbnew.FootprintLoad(os.path.join(FPDIR, "MountingHole.pretty"), fpname)
    fp.SetReference(ref)
    fp.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    board.Add(fp)
    return fp


def add_line(board, x1, y1, x2, y2, layer=pcbnew.Edge_Cuts, width=0.1):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(pcbnew.VECTOR2I(MM(x1), MM(y1)))
    s.SetEnd(pcbnew.VECTOR2I(MM(x2), MM(y2)))
    s.SetLayer(layer)
    s.SetWidth(MM(width))
    board.Add(s)


def add_text(board, text, x, y, size, angle, hjust, vjust, layer_name):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
    t.SetTextThickness(MM(max(0.15, size * 0.15)))
    t.SetTextAngleDegrees(angle)
    t.SetLayer(board.GetLayerID(layer_name))
    t.SetHorizJustify(hjust)
    t.SetVertJustify(vjust)
    board.Add(t)
    return t


def add_zone(board, net, layer, pts, clearance=0.3):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net)
    z.SetLocalClearance(MM(clearance))
    z.SetMinThickness(MM(0.3))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    poly = z.Outline()
    poly.NewOutline()
    for x, y in pts:
        poly.Append(MM(x), MM(y))
    board.Add(z)
    return z



def add_track(board, net, layer_name, width, pts):
    layer = board.GetLayerID(layer_name)
    for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetNet(net)
        t.SetLayer(layer)
        t.SetWidth(MM(width))
        t.SetStart(pcbnew.VECTOR2I(MM(x1), MM(y1)))
        t.SetEnd(pcbnew.VECTOR2I(MM(x2), MM(y2)))
        board.Add(t)


def add_via(board, net, x, y, drill=0.3, size=0.6):
    v = pcbnew.PCB_VIA(board)
    v.SetNet(net)
    v.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    v.SetDrill(MM(drill))
    v.SetWidth(MM(size))
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    board.Add(v)

def rounded_outline(w, h, r):
    return [
        (r, 0), (w - r, 0), (w, r), (w, h - r),
        (w - r, h), (r, h), (0, h - r), (0, r),
    ]


def inset_outline(w, h, r, d):
    return rounded_outline(w - 2 * d, h - 2 * d, max(0.1, r - d))


def check_wiring_source():
    """The four module nets must match hardware/wiring.json (single source)."""
    import json
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "wiring.json")
    with open(src) as f:
        w = json.load(f)
    pins = w["pins"]
    assert pins["module_tx"] == 3, f"module_tx changed: {pins['module_tx']}"
    assert pins["module_rx"] == 4, f"module_rx changed: {pins['module_rx']}"
    assert pins["console_tx"] == 21 and pins["console_rx"] == 20
    assert pins["oled_sda"] == 5 and pins["oled_scl"] == 6


def main():
    check_wiring_source()
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(2)

    ds = board.GetDesignSettings()
    ds.m_MinClearance = MM(0.25)
    ds.m_TrackMinWidth = MM(0.25)
    ds.m_ViasMinSize = MM(0.6)
    ds.m_MinThroughDrill = MM(0.3)

    nets = {}
    for name in ["BAT", "5V", "GND", "3V3"] + [f"IO{i}" for i in (0, 1, 3, 4, 5, 6, 7, 8, 10, 20, 21)]:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        nets[name] = n

    pad_pos = {}
    for ref, lib, name, x, y, rot in PLACEMENT:
        fp = add_fp(board, ref, lib, name, x, y, rot)
        conn_nets = ["3V3", "GND"] if ref.startswith("C") else NET_OF_CONN[ref]
        for pad in fp.Pads():
            num = int(pad.GetNumber())
            if 1 <= num <= len(conn_nets):
                pad.SetNet(nets[conn_nets[num - 1]])
            p = pad.GetPosition()
            pad_pos[f"{ref}.{num}"] = (round(pcbnew.ToMM(p.x), 3), round(pcbnew.ToMM(p.y), 3))

    for ref, x, y in HOLES:
        add_hole(board, ref, "MountingHole_2.7mm_M2.5", x, y)
    for ref, x, y in ANT_HOLES:
        add_hole(board, ref, "MountingHole_2.1mm", x, y)

    pts = rounded_outline(BOARD_W, BOARD_H, CHAMFER)
    for (x1, y1), (x2, y2) in zip(pts, pts[1:] + pts[:1]):
        add_line(board, x1, y1, x2, y2)

    for text, x, y, size, angle, hj, vj, layer in TEXTS:
        add_text(board, text, x, y, size, angle, hj, vj, layer)

    # module body keep-out box (silkscreen)
    for (x1, y1), (x2, y2) in [
        ((6, 44.2), (42, 44.2)), ((42, 44.2), (42, 75.5)),
        ((42, 75.5), (6, 75.5)), ((6, 75.5), (6, 44.2)),
    ]:
        add_line(board, x1, y1, x2, y2, pcbnew.F_SilkS, 0.12)

    # C3 board outline on silkscreen (23.5 x 18, USB edge at top y=0.5)
    for (x1, y1), (x2, y2) in [
        ((8.30, 0.5), (8.30, 24.3)),
        ((8.30, 24.3), (26.90, 24.3)),
        ((26.90, 24.3), (26.90, 0.5)),
    ]:
        add_line(board, x1, y1, x2, y2, pcbnew.F_SilkS, 0.12)


    for net_name, layer_name, width, pts in ROUTES:
        add_track(board, nets[net_name], layer_name, width, pts)
    for x, y in STITCH_VIAS:
        add_via(board, nets["GND"], x, y)
    add_via(board, nets["5V"], 42.02, 5.54)
    add_via(board, nets["BAT"], 44.56, 3.00)
    add_via(board, nets["IO4"], 11.54, 34.20)

    zone_pts = inset_outline(BOARD_W, BOARD_H, CHAMFER, 0.6)
    add_zone(board, nets["GND"], pcbnew.F_Cu, zone_pts)
    add_zone(board, nets["GND"], pcbnew.B_Cu, zone_pts)

    # NOTE: ZONE_FILLER segfaults in this headless KiCad 9 build.  Zones stay
    # unfilled in the saved file; kicad-cli fills them when plotting / DRC.
    board.BuildConnectivity()

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rf-lab-c3-carrier.kicad_pcb")
    pcbnew.SaveBoard(out, board)
    with open(os.path.join(os.path.dirname(out), "pad_positions.json"), "w") as f:
        json.dump(pad_pos, f, indent=1, sort_keys=True)
    print("saved", out)
    print("pads:", len(pad_pos))


if __name__ == "__main__":
    main()
