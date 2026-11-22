#!/usr/bin/env python3
"""device_selftest 的主机单测:解析 + 一致性判定,不需要串口/设备。

fixture 直接取自真机记录(docs/evidence/2026-11-22-device-audit.md):
0.7.1 / build=511afca+dirty / 码库 13 条。核心断言是 TST-01 的要求——
在这台设备上不再因为「不是 0.6.0、不是 5 条」误报;真正的漂移仍要 FAIL。
"""

import contextlib
import io
import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import device_selftest as ds  # noqa: E402

# --- 真机 fixture(docs/evidence/2026-11-22-device-audit.md §2.1 / §2.2) ---
BOOT_071 = """\
# RF-Lab fw=0.7.1 (ESP32-C3 + YS-RF34DL)
# module UART1 9600 8N1 TX=GPIO3 RX=GPIO4
# console uart0:on usb:on 115200 8N1
# oled 72x40 @0x3C i2c400000 sda=GPIO5 scl=GPIO6: ok
# stored 13 code(s)
# build=511afca+dirty ui=85a1a9b4 ota_slot=0 pending_verify=0
"""

INFO_071 = """\
# RF-Lab fw=0.7.1 (ESP32-C3 + YS-RF34DL)
# build=511afca+dirty ui=85a1a9b4
# ota slot=0 next=-1 state=idle pct=0 pending=0
# module UART1 9600 8N1 TX=GPIO3 RX=GPIO4
# console uart0:on usb:on 115200 8N1
# oled 72x40 @0x3C i2c400000 sda=5 scl=6 present=yes on=yes flush=121 err=0 last_us=8719 max_us=9643
# net mode=sta ap_on=0 has_creds=1 ap=RF-Lab-1B59 ap_ip=192.168.4.1 clients=0 sta=Mark sta_ip=192.168.5.125 rssi=-37
# ble on=on adv=yes conn=no name=RF-Lab-1B59 pin=set
# codebook ver=2 count=13/32 migrated=no
OK stored=13/32 rx_bytes=928 frames=116 acks=0 dups=0 stray=0 resync=0
"""

LIST_071 = """\
 1  315M 5555034C
 2  315M 5555033C
 3  315M 5555034B
 4  433M 5555034B
 5  433M 5555033C
 6  433M 1555304B
 7  433M 00000092
 8  315M 5555034D
 9  433M 5555034D
10  433M 00000093
11  433M 00000094
12  433M 00000095
13  433M 00000053
"""

# LIST LONG 未在 11-22 巡检里原样留存:按固件 cmd_list_long 的格式从上面的真实 LIST
# 派生(仅第 1 条带 name/group/sym,其余为固件默认的 "-");条数必须与 INFO 一致。
LIST_LONG_071 = "".join(
    (f' 1  315M 5555034C osc=0x3C  name="Gate A" group="Home" sym="A"\n'
     if c["id"] == 1 else
     f'{c["id"]:2d}  {c["band"]} {c["code"]} osc=0x{int(c["code"][-2:], 16):02X}  '
     f'name="-" group="-" sym="-"\n')
    for c in ds.parse_list(LIST_071)
)

# 只用于测 flags 解析(manual/edited 是固件 0.7.1 的 LIST LONG 后缀)。
LIST_LONG_FLAGS = """\
 1  315M 5555034C osc=0x3C  name="Gate A" group="Home" sym="A" manual
 2  315M 5555033C osc=0x3C  name="-" group="-" sym="-"
 3  433M 00000093 osc=0x93  name="-" group="-" sym="-" edited
"""

HELP_071 = """\
commands (115200 8N1):
  HELP                  this list
  INFO                  fw / module baud / counters
  LIST                  stored codes: id band code
  LIST LONG             id band code name group sym flags
  SEND <id>             transmit once (frame dur=03)
  LONG <id>             start continuous transmit (dur=FE)
  STOP <id>             stop continuous transmit (dur=FF)
  ERASE <id|all>        delete one code / all codes (ids shift)
  NEW <band> <hex> [name]  add a manual code (315/433, 8 hex)
  NAME <id> [text]      set/clear UTF-8 name (max 24 bytes)
  SYM <id> [text]       set/clear symbol (max 8 bytes)
  GROUP <id> [text]     set/clear single group (max 16 bytes)
  EDIT <id> band|code <v>  edit raw band / 4-byte payload
  MODE single|continuous  module decode output mode
  DISP ON|OFF|TEST [s]|SHOW  OLED panel / test pattern / ASCII dump
  BLE [on|off]          BLE advertising / status
  WEBTEST               self-check HTTP /api/hello + /api/events
  RESET                 restart ESP32
"""

STATE_071 = json.dumps({
    "fw": "0.7.1", "proto": 1, "uptime_ms": 31840000,
    "net": {"mode": "sta", "ap_on": 0},
    "ble": {"on": 1, "conn": 0},
    "module": {"baud": 9600, "rx_bytes": 928},
    "store": {"count": 13, "max": 32, "ver": 2},
    "oled": {"present": 1, "on": 1},
    "ota": {"armed": 0, "pct": 0, "phase": "idle", "slot": 0, "next": -1, "pending": 0},
    "tx": {"active": 0, "kind": "idle", "id": 0, "ok": 0, "ms": 0},
    "last": {"kind": "learn", "id": 13, "band": "433M", "code": "00000053", "ms": 100},
})

DISP_OK = "OK disp=on present=yes test=off flush=121 err=0\n"
DISP_TEST_OK = "OK disp=test 1s (DISP ON to leave test)\n"
DISP_SHOW_OK = ("# fb 72x40 '#'=on (screen content, not read back from glass)\n"
                + ("." * 72 + "\n") * 40
                + "OK fb 72x40\n")
WEBTEST_OK = """\
OK webtest 200 http://192.168.5.125/api/hello -> {"fw":"0.7.1"}
OK webtest 200 http://192.168.5.125/api/state -> {"fw":"0.7.1"}
OK webtest 200 http://192.168.5.125/api/events -> ok
OK webtest POST 202 /api/cmd -> DISP TEST 1
OK disp=test 1s (DISP ON to leave test)
"""
SEND_OK = "OK send id=1 band=315M code=5555034C (0xF0, 852ms)\n"
LONG_OK = "OK long id=1 band=315M code=5555034C (0xFE, 40ms)\n"
STOP_OK = "OK stop id=1 band=315M code=5555034C (0xFF, 38ms)\n"


class FakeDev:
    """替代真串口:按命令行返回固定输出。"""

    table = {
        "INFO": INFO_071, "LIST": LIST_071, "LIST LONG": LIST_LONG_071,
        "HELP": HELP_071, "STATE": STATE_071,
        "DISP": DISP_OK, "DISP TEST 1": DISP_TEST_OK,
        "DISP ON": "OK disp=on\n", "DISP SHOW": DISP_SHOW_OK,
        "BLE OFF": "OK ble=off (advertising stopped, current link dropped)\n",
        "BLE ON": "OK ble=on\n", "WEBTEST": WEBTEST_OK,
        "SEND 1": SEND_OK, "LONG 1": LONG_OK, "STOP 1": STOP_OK,
    }

    def __init__(self, *args, **kwargs):
        self.reopened = False
        self.calls = []
        self.ble_adv = True

    def reset_boot(self, wait=3.0):
        self.calls.append("RESET")
        return BOOT_071

    def cmd(self, line, wait):
        self.calls.append(line)
        if line == "INFO":  # BLE OFF/ON 必须真的改变 adv,否则闭环检查没有意义
            return INFO_071.replace("adv=yes", f"adv={'yes' if self.ble_adv else 'no'}")
        if line == "BLE OFF":
            self.ble_adv = False
        elif line == "BLE ON":
            self.ble_adv = True
        return self.table.get(line, "")

    def close(self):
        pass


class TestParsers(unittest.TestCase):
    def test_info_071(self):
        info = ds.parse_info(INFO_071)
        self.assertEqual(info["fw"], "0.7.1")
        self.assertEqual((info["build"], info["ui"]), ("511afca+dirty", "85a1a9b4"))
        self.assertEqual((info["stored"], info["max"], info["codebook_version"]), (13, 32, 2))
        self.assertEqual(info["net_mode"], "sta")
        self.assertIs(info["oled_present"], True)
        self.assertIs(info["ble_adv"], True)
        self.assertEqual(info["ota_slot"], 0)

    def test_info_old_firmware_falls_back_to_stored(self):
        # 0.6.x 没有 codebook 行:只靠 OK stored= 也要读出条数,不能写死。
        info = ds.parse_info("OK stored=5/32 rx_bytes=1 frames=2 acks=0\n")
        self.assertEqual((info["stored"], info["max"]), (5, 32))
        self.assertNotIn("codebook_version", info)

    def test_state_rejects_non_json(self):
        self.assertIsNone(ds.parse_state("ERR nothing"))
        self.assertEqual(ds.parse_state(STATE_071)["fw"], "0.7.1")

    def test_list_and_empty_text(self):
        self.assertEqual(len(ds.parse_list(LIST_071)), 13)
        self.assertEqual(ds.parse_list("(empty; press a remote button and watch for LEARN)\n"), [])
        self.assertEqual(ds.parse_list(LIST_071)[9]["code"], "00000093")

    def test_list_long_flags(self):
        recs = ds.parse_list_long(LIST_LONG_FLAGS)
        self.assertEqual(len(recs), 3)
        self.assertEqual(recs[0]["name"], "Gate A")
        self.assertEqual(recs[0]["flags"], ["manual"])
        self.assertEqual(recs[2]["flags"], ["edited"])
        self.assertEqual(recs[0]["osc"], 0x3C)

        # 旧固件(0.7.1)的 LIST LONG 没有 osc=,仍要能解析。
        old = ds.parse_list_long(' 1  315M 5555034C  name="Gate" group="-" sym="-"\n')
        self.assertEqual(len(old), 1)
        self.assertIsNone(old[0]["osc"])

    def test_summarize_skip_is_not_failure(self):
        with mock.patch.object(ds, "serial", None):
            rep = ds.Report(quiet=True)
            rep.ok("a", True)
            rep.skip("b", "no hardware")
            rep.ok("c", False)
            s = rep.summary()
        self.assertEqual((s["total"], s["passed"], s["failed"], s["skipped"]), (3, 1, 1, 1))
        self.assertTrue(rep.failed())


class TestConsistency(unittest.TestCase):
    def test_audit_transcript_has_no_false_red(self):
        """TST-01:0.7.1 / 13 条码,所有一致性检查必须是 PASS(不是 0.6.0/5 条)。"""
        _dev, checks = ds.analyze(BOOT_071, INFO_071, STATE_071, LIST_071,
                                  LIST_LONG_071, HELP_071)
        failed = [c for c in checks if c["status"] == ds.FAIL]
        self.assertEqual(failed, [], f"unexpected FAIL: {failed}")
        names = [c["name"] for c in checks if c["status"] == ds.PASS]
        self.assertIn("LIST count matches INFO", names)
        self.assertIn("fw consistent boot/info", names)
        self.assertIn("HELP 0.7.1 command surface", names)

    def test_state_fw_mismatch_fails(self):
        state = STATE_071.replace('"fw": "0.7.1"', '"fw": "0.6.0"')
        _dev, checks = ds.analyze(BOOT_071, INFO_071, state, LIST_071,
                                  LIST_LONG_071, HELP_071)
        failed = {c["name"] for c in checks if c["status"] == ds.FAIL}
        self.assertIn("STATE fw matches INFO", failed)

    def test_count_mismatch_fails(self):
        short_list = "\n".join(LIST_071.splitlines()[:-1])
        _dev, checks = ds.analyze(BOOT_071, INFO_071, STATE_071, short_list,
                                  LIST_LONG_071, HELP_071)
        failed = {c["name"] for c in checks if c["status"] == ds.FAIL}
        self.assertIn("LIST count matches INFO", failed)

    def test_missing_state_json_fails(self):
        _dev, checks = ds.analyze(BOOT_071, INFO_071, "", LIST_071,
                                  LIST_LONG_071, HELP_071)
        failed = {c["name"] for c in checks if c["status"] == ds.FAIL}
        self.assertIn("STATE JSON", failed)

    def test_boot_banner_lost_is_skip_not_fail(self):
        _dev, checks = ds.analyze("", INFO_071, STATE_071, LIST_071,
                                  LIST_LONG_071, HELP_071)
        by_name = {c["name"]: c["status"] for c in checks}
        self.assertEqual(by_name["boot banner"], ds.FAIL)  # 完全没输出才算红
        _dev, checks = ds.analyze("# stored 13 code(s)\n", INFO_071, STATE_071,
                                  LIST_071, LIST_LONG_071, HELP_071)
        by_name = {c["name"]: c["status"] for c in checks}
        self.assertEqual(by_name["boot banner"], ds.SKIP)


class TestEndToEndJson(unittest.TestCase):
    def run_main(self, argv):
        out = io.StringIO()
        with mock.patch.object(ds, "Dev", FakeDev), contextlib.redirect_stdout(out):
            code = ds.main(argv)
        return code, out.getvalue()

    def test_json_full_pass_on_071(self):
        code, text = self.run_main(["--port", "fake", "--json"])
        self.assertEqual(code, 0)
        payload = json.loads(text)
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["summary"]["failed"], 0)
        self.assertEqual(payload["device"]["fw"], "0.7.1")
        self.assertEqual(payload["device"]["stored"], 13)
        self.assertNotIn("SKIP", text)  # JSON 里 status 是小写 pass/fail/skip
        # 除「没加 --tx」这一项外全部 pass:不加 --tx 就不该有 FAIL。
        not_pass = [(c["name"], c["status"]) for c in payload["checks"]
                    if c["status"] != "pass"]
        self.assertEqual(not_pass, [("SEND/LONG/STOP", "skip")])

    def test_no_tx_by_default(self):
        code, text = self.run_main(["--port", "fake", "--json"])
        self.assertEqual(code, 0)
        payload = json.loads(text)
        self.assertNotIn("tx_stats", payload)
        skipped = [c["name"] for c in payload["checks"] if c["status"] == "skip"]
        self.assertIn("SEND/LONG/STOP", skipped)

    def test_tx_repeat_reports_hits(self):
        code, text = self.run_main(["--port", "fake", "--json", "--tx", "--tx-repeat", "3"])
        self.assertEqual(code, 0)
        payload = json.loads(text)
        self.assertEqual(payload["tx_stats"]["send"]["hits"], 3)
        self.assertEqual(payload["tx_stats"]["send"]["attempts"], 3)
        self.assertEqual(payload["tx_stats"]["send"]["ms"], [852, 852, 852])
        self.assertEqual(payload["tx_stats"]["long"]["hits"], 3)

    def test_tx_without_f0_fails(self):
        class NoAckDev(FakeDev):
            table = dict(FakeDev.table, **{
                "SEND 1": "WARN send id=1 band=315M code=5555034C no 0xF0 within 1500ms\n"})

        out = io.StringIO()
        with mock.patch.object(ds, "Dev", NoAckDev), contextlib.redirect_stdout(out):
            code = ds.main(["--port", "fake", "--json", "--tx"])
        payload = json.loads(out.getvalue())
        self.assertEqual(code, 1)
        self.assertFalse(payload["ok"])
        self.assertEqual(payload["tx_stats"]["send"]["hits"], 0)
        failed = {c["name"] for c in payload["checks"] if c["status"] == "fail"}
        self.assertIn("SEND F0", failed)

    def test_log_appends_instead_of_truncating(self):
        """--log 常指向 evidence 转录:重复跑必须追加,不能清空旧记录。"""
        with tempfile.TemporaryDirectory() as td:
            log = pathlib.Path(td) / "selftest.log"
            for _ in range(2):
                code, _text = self.run_main(["--port", "fake", "--json", "--log", str(log)])
                self.assertEqual(code, 0)
            text = log.read_text(encoding="utf-8")
        self.assertEqual(text.count("# RF-Lab on-device self-test"), 2)
        self.assertIn("port=fake baud=115200 tx=0 repeat=1", text)

    def test_human_summary_shape(self):
        code, text = self.run_main(["--port", "fake"])
        self.assertEqual(code, 0)
        self.assertIn("checks passed", text)
        self.assertIn("PASS  boot banner", text)


class TestNoPyserial(unittest.TestCase):
    def test_import_and_analyze_work_without_pyserial(self):
        with mock.patch.object(ds, "serial", None):
            _dev, checks = ds.analyze(BOOT_071, INFO_071, STATE_071, LIST_071,
                                      LIST_LONG_071, HELP_071)
            self.assertFalse([c for c in checks if c["status"] == ds.FAIL])
            with self.assertRaises(SystemExit):
                ds.Dev("/dev/null", 115200, None)


if __name__ == "__main__":
    unittest.main()
