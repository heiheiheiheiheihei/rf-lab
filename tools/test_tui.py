#!/usr/bin/env python3
"""rflab_tui 的主机单测:纯解析 + 键位分发(不启动 curses)。"""

import json
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import rflab_tui as tui  # noqa: E402


class TestParsers(unittest.TestCase):
    def test_list_line(self):
        self.assertEqual(tui.parse_list_line(" 1  315M 5555034C"),
                         {"id": 1, "band": "315M", "code": "5555034C"})
        self.assertEqual(tui.parse_list_line("32 433M a1b2c3d4"),
                         {"id": 32, "band": "433M", "code": "A1B2C3D4"})
        self.assertIsNone(tui.parse_list_line("(empty; press a remote)"))
        self.assertIsNone(tui.parse_list_line(" 0  315M 1234"))

    def test_serial_events(self):
        ev = tui.parse_serial_event("LEARN id=6 band=433M code=deadbeef")
        self.assertEqual(ev["kind"], "learn")
        self.assertEqual(ev["code"], "DEADBEEF")
        ev = tui.parse_serial_event("OK tx id=1 band=315M code=5555034C (0xF0, 852ms)")
        self.assertEqual(ev["kind"], "tx")
        self.assertEqual(ev["ms"], 852)
        ev = tui.parse_serial_event("OK stored=5/32 rx_bytes=296 frames=37 acks=15 "
                                    "dups=3 stray=0 resync=0")
        self.assertEqual(ev["kind"], "counters")
        self.assertEqual(ev["store"], 5)
        ev = tui.parse_serial_event("# net mode=apsta ap=RF-Lab-1B59 ip=192.168.4.1 "
                                    "clients=1 sta=MyHome sta_ip=192.168.5.42 rssi=-57")
        self.assertEqual(ev["kind"], "net")
        self.assertEqual(ev["sta_ip"], "192.168.5.42")
        self.assertEqual(tui.parse_serial_event("WARN tx ...")["kind"], "warn")
        self.assertEqual(tui.parse_serial_event("OK mode=continuous")["kind"], "ok")

    def test_state_json(self):
        st = tui.parse_state_json({
            "fw": "0.6.0", "uptime_ms": 1234,
            "net": {"mode": "apsta", "ap_on": True, "has_creds": True,
                    "ap_ssid": "RF-Lab", "ap_ip": "192.168.4.1",
                    "sta_connected": True, "sta_ssid": "home",
                    "sta_ip": "192.168.1.2", "rssi": -60},
            "module": {"baud": 9600, "frames": 7, "acks": 3, "stray": 1},
            "store": {"count": 2, "max": 32},
            "oled": {"present": True, "on": True},
            "tx": {"active": True, "kind": "long"},
            "last": {"kind": "learn", "id": 2, "band": "315M", "code": "AABBCCDD"},
        })
        self.assertEqual(st["ap_ssid"], "RF-Lab")
        self.assertEqual(st["net_mode"], "apsta")
        self.assertTrue(st["ap_on"])
        self.assertTrue(st["sta_connected"])
        self.assertEqual(st["frames"], 7)
        self.assertTrue(st["tx_active"])
        self.assertEqual(st["last_code"], "AABBCCDD")
        self.assertEqual(st["count"], 2)

    def test_codes_json(self):
        items = [{"id": 1, "band": "315M", "code": "aabbccdd"},
                 {"id": "bad"}, None, {"id": 2, "band": "433M", "code": "11223344"}]
        out = tui.parse_codes_json(items)
        self.assertEqual(len(out), 2)
        self.assertEqual(out[0]["code"], "AABBCCDD")

    def test_uptime(self):
        self.assertEqual(tui.format_uptime(0), "00:00:00")
        self.assertEqual(tui.format_uptime(3725000), "01:02:05")
        self.assertEqual(tui.format_uptime(90061000), "1D 01:01:01")


class TestAppKeys(unittest.TestCase):
    def setUp(self):
        self.link = tui.MockLink()
        self.app = tui.App(self.link)
        self.app.snap.codes = list(self.link.codes)
        self.app.snap.state = dict(self.link.state)

    def test_arm_required(self):
        self.app.handle_key(10)
        self.assertEqual(self.link.sent, [])
        self.app.handle_key(ord("a"))
        self.assertTrue(self.app.armed)

    def test_empty_codebook(self):
        self.app.snap.codes = []
        self.app.armed = True
        self.app.handle_key(10)
        self.app.handle_key(ord("l"))
        self.assertEqual(self.link.sent, [])

    def test_send_long_stop(self):
        self.app.armed = True
        self.app.handle_key(10)
        self.app.handle_key(ord("l"))
        self.app.handle_key(ord("s"))
        self.assertEqual(self.link.sent[:3], ["SEND 1", "LONG 1", "STOP 5"])

    def test_move_and_select(self):
        self.app.handle_key(ord("j"))
        self.assertEqual(self.app.selected, 1)
        self.app.handle_key(curses_key_down())
        self.assertEqual(self.app.selected, 2)
        self.app.handle_key(ord("k"))
        self.assertEqual(self.app.selected, 1)

    def test_delete_twice(self):
        self.app.handle_key(ord("d"))
        self.assertEqual(self.link.sent, [])
        self.app.handle_key(ord("d"))
        self.assertEqual(self.link.sent, ["ERASE 1"])

    def test_help(self):
        self.app.handle_key(ord("?"))
        self.assertTrue(self.app.show_help)
        self.app.handle_key(ord("x"))
        self.assertFalse(self.app.show_help)

    def test_quit(self):
        self.assertFalse(self.app.handle_key(ord("q")))


def curses_key_down():
    import curses
    return curses.KEY_DOWN


if __name__ == "__main__":
    unittest.main(verbosity=2)
