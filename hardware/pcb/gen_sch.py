#!/usr/bin/env python3
"""Generate a single-sheet KiCad schematic for the RF-Lab C3 carrier.

The carrier is passive, so the schematic is drawn in "pin + net label" style:
every connector pin gets a short stub and a local label.  Identical labels
form the nets, which keeps the sheet readable while staying ERC-clean.
"""

from __future__ import annotations

import os
import sys
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_pcb  # single source for the connector net lists

J1_NETS = gen_pcb.J1_NETS
J2_NETS = gen_pcb.J2_NETS
J3_NETS = gen_pcb.J3_NETS
J4_NETS = gen_pcb.J4_NETS
J5_NETS = gen_pcb.J5_NETS


def u():
    return str(uuid.uuid4())


def lib_symbol(n: int) -> str:
    """A generic 1xN pin header symbol, pins on the left."""
    top = (n - 1) * 2.54 / 2 + 2.54
    pins = []
    for i in range(n):
        y = (n - 1) * 2.54 / 2 - i * 2.54
        pins.append(
            f'''      (pin passive line (at -5.08 {y:.2f} 0) (length 2.54)
        (name "~" (effects (font (size 1.27 1.27))))
        (number "{i + 1}" (effects (font (size 1.27 1.27))))
      )'''
        )
    return f'''    (symbol "rf-lab:Conn_01x{n:02d}"
      (pin_names (offset 1.016))
      (exclude_from_sim no)
      (in_bom yes)
      (on_board yes)
      (property "Reference" "J" (at 0 {top + 1.27:.2f} 0)
        (effects (font (size 1.27 1.27)))
      )
      (property "Value" "Conn_01x{n:02d}" (at 0 {-top - 1.27:.2f} 0)
        (effects (font (size 1.27 1.27)) (hide yes))
      )
      (property "Footprint" "" (at 0 0 0)
        (effects (font (size 1.27 1.27)) (hide yes))
      )
      (property "Datasheet" "" (at 0 0 0)
        (effects (font (size 1.27 1.27)) (hide yes))
      )
      (property "Description" "2.54 mm pin header" (at 0 0 0)
        (effects (font (size 1.27 1.27)) (hide yes))
      )
      (symbol "Conn_01x{n:02d}_1_1"
        (rectangle (start -2.54 {top:.2f}) (end 2.54 {-top:.2f})
          (stroke (width 0.254) (type default))
          (fill (type background))
        )
{os.linesep.join(pins)}
      )
    )'''


CAP_SYMBOL = '''    (symbol "rf-lab:C"
      (pin_names (offset 0.254))
      (exclude_from_sim no)
      (in_bom yes)
      (on_board yes)
      (property "Reference" "C" (at 0 2.54 0)
        (effects (font (size 1.27 1.27)))
      )
      (property "Value" "C" (at 0 -2.54 0)
        (effects (font (size 1.27 1.27)) (hide yes))
      )
      (property "Footprint" "" (at 0 0 0)
        (effects (font (size 1.27 1.27)) (hide yes))
      )
      (property "Datasheet" "" (at 0 0 0)
        (effects (font (size 1.27 1.27)) (hide yes))
      )
      (symbol "C_0_1"
        (polyline (pts (xy -2.032 0.762) (xy 2.032 0.762))
          (stroke (width 0.508) (type default)) (fill (type none))
        )
        (polyline (pts (xy -2.032 -0.762) (xy 2.032 -0.762))
          (stroke (width 0.508) (type default)) (fill (type none))
        )
      )
      (symbol "C_1_1"
        (pin passive line (at -5.08 0 0) (length 3.048)
          (name "~" (effects (font (size 1.27 1.27))))
          (number "1" (effects (font (size 1.27 1.27))))
        )
        (pin passive line (at 5.08 0 180) (length 3.048)
          (name "~" (effects (font (size 1.27 1.27))))
          (number "2" (effects (font (size 1.27 1.27))))
        )
      )
    )'''


def place_conn(ref: str, n: int, x: float, y: float, rot: int, nets: list[str],
               label_dx: float, justify: str) -> str:
    pin_uuids = "\n".join(
        f'''    (pin "{i + 1}"
      (uuid "{u()}")
    )''' for i in range(n)
    )
    # pin connection points (rotation 0 or 180 only)
    wires = []
    labels = []
    for i in range(n):
        dy = (n - 1) * 2.54 / 2 - i * 2.54
        if rot == 0:
            px, py = x - 5.08, y + dy
        else:
            px, py = x + 5.08, y - dy
        ex = px + label_dx
        wires.append(
            f'''  (wire (pts (xy {px:.2f} {py:.2f}) (xy {ex:.2f} {py:.2f}))
    (stroke (width 0) (type default))
    (uuid "{u()}")
  )'''
        )
        labels.append(
            f'''  (label "{nets[i]}"
    (at {ex:.2f} {py:.2f} 0)
    (effects (font (size 1.27 1.27)) (justify {justify} bottom))
    (uuid "{u()}")
  )'''
        )
    return f'''  (symbol (lib_id "rf-lab:Conn_01x{n:02d}") (at {x:.2f} {y:.2f} {rot}) (unit 1)
    (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no)
    (uuid "{u()}")
    (property "Reference" "{ref}" (at {x:.2f} {y - ((n - 1) * 2.54 / 2 + 3.0):.2f} 0)
      (effects (font (size 1.27 1.27)))
    )
    (property "Value" "Conn_01x{n:02d}" (at {x:.2f} {y + ((n - 1) * 2.54 / 2 + 3.0):.2f} 0)
      (effects (font (size 1.27 1.27)) (hide yes))
    )
    (property "Footprint" "" (at {x:.2f} {y:.2f} 0)
      (effects (font (size 1.27 1.27)) (hide yes))
    )
    (property "Datasheet" "" (at {x:.2f} {y:.2f} 0)
      (effects (font (size 1.27 1.27)) (hide yes))
    )
{pin_uuids}
    (instances
      (project "rf-lab-c3-carrier"
        (path "/"
          (reference "{ref}") (unit 1)
        )
      )
    )
  )
{os.linesep.join(wires)}
{os.linesep.join(labels)}'''


def place_cap(ref: str, value: str, x: float, y: float) -> str:
    return f'''  (symbol (lib_id "rf-lab:C") (at {x:.2f} {y:.2f} 0) (unit 1)
    (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no)
    (uuid "{u()}")
    (property "Reference" "{ref}" (at {x:.2f} {y - 3.81:.2f} 0)
      (effects (font (size 1.27 1.27)))
    )
    (property "Value" "{value}" (at {x:.2f} {y + 3.81:.2f} 0)
      (effects (font (size 1.27 1.27)))
    )
    (property "Footprint" "" (at {x:.2f} {y:.2f} 0)
      (effects (font (size 1.27 1.27)) (hide yes))
    )
    (property "Datasheet" "" (at {x:.2f} {y:.2f} 0)
      (effects (font (size 1.27 1.27)) (hide yes))
    )
    (pin "1" (uuid "{u()}"))
    (pin "2" (uuid "{u()}"))
    (instances
      (project "rf-lab-c3-carrier"
        (path "/"
          (reference "{ref}") (unit 1)
        )
      )
    )
  )
  (wire (pts (xy {x - 5.08:.2f} {y:.2f}) (xy {x - 10.16:.2f} {y:.2f}))
    (stroke (width 0) (type default)) (uuid "{u()}")
  )
  (label "3V3" (at {x - 10.16:.2f} {y:.2f} 0)
    (effects (font (size 1.27 1.27)) (justify right bottom)) (uuid "{u()}")
  )
  (wire (pts (xy {x + 5.08:.2f} {y:.2f}) (xy {x + 10.16:.2f} {y:.2f}))
    (stroke (width 0) (type default)) (uuid "{u()}")
  )
  (label "GND" (at {x + 10.16:.2f} {y:.2f} 0)
    (effects (font (size 1.27 1.27)) (justify left bottom)) (uuid "{u()}")
  )'''


def note(text: str, x: float, y: float, size: float = 1.6) -> str:
    return f'''  (text "{text}"
    (at {x:.2f} {y:.2f} 0)
    (effects (font (size {size:.2f} {size:.2f})) (justify left bottom))
    (uuid "{u()}")
  )'''


def main():
    parts = [
        place_conn("J1", 8, 110.49, 64.77, 0, J1_NETS, -10.16, "right"),
        place_conn("J2", 7, 110.49, 124.46, 0, J2_NETS, -10.16, "right"),
        place_conn("J5", 16, 299.72, 69.85, 180, J5_NETS, 10.16, "left"),
        place_conn("J3", 4, 149.86, 209.55, 0, J3_NETS, -10.16, "right"),
        place_conn("J4", 4, 250.19, 209.55, 0, J4_NETS, -10.16, "right"),
        place_cap("C1", "10u", 149.86, 254.0),
        place_cap("C2", "100n", 185.42, 254.0),
        note("RF-Lab C3 carrier - passive 2-layer carrier, 3.3 V ONLY", 20, 20, 2.0),
        note("J1/J2: 01Space ESP32-C3-0.42LCD sockets (2x 2.54 mm, 15.24 mm rows)", 20, 28),
        note("J3: YS-RF34DL socket - VCC/TXD/RXD/GND; TXD->IO4, RXD<-IO3", 20, 34),
        note("J4: spare 4-pin header, same four signals", 20, 40),
        note("J5: all remaining C3 signals + power, 1x16", 20, 46),
    ]

    sch = f'''(kicad_sch
  (version 20250114)
  (generator "eeschema")
  (generator_version "9.0")
  (uuid "{u()}")
  (paper "A3")
  (title_block
    (title "RF-Lab C3 Carrier")
    (date "2026-11-23")
    (rev "1.0")
    (company "RF-Lab")
    (comment 1 "01Space ESP32-C3-0.42LCD + YS-RF34DL")
    (comment 2 "3.3 V ONLY - never feed 5 V into VCC")
  )
  (lib_symbols
{lib_symbol(4)}
{lib_symbol(7)}
{lib_symbol(8)}
{lib_symbol(16)}
{CAP_SYMBOL}
  )
{os.linesep.join(parts)}
  (sheet_instances
    (path "/" (page "1"))
  )
  (embedded_fonts no)
)
'''
    out = os.path.join(HERE, "rf-lab-c3-carrier.kicad_sch")
    with open(out, "w") as f:
        f.write(sch)
    print("saved", out)


if __name__ == "__main__":
    main()
