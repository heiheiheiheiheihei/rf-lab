#!/usr/bin/env python3
"""Wi-Fi 失败回退上机自检。

验证:
  1. WIFI CLEAR 后是 AP-only,Web 可用;
  2. 配置一个不存在的 SSID → connecting → 15s 后 AP FALLBACK(apsta,AP 可用);
  3. 回退后用 AP 的 WEBTEST 仍能 200;
  4. WIFI CLEAR 回到 AP-only。

真实 STA 连接成功（局域网访问）需要现场有可用 AP,本脚本不伪造。

⚠️ 本脚本会写入一个不存在的 SSID 并在最后 WIFI CLEAR,会清除设备已保存的 STA 凭据;
不要在已配好网的设备上随手运行。
"""

from __future__ import annotations

import argparse
import sys
import time

from device_selftest import Dev


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--log", default="docs/evidence/2026-11-15-wifi.log")
    args = ap.parse_args()

    with open(args.log, "w", encoding="utf-8") as f:
        f.write("# RF-Lab 0.6.0 Wi-Fi fallback self-test\n")
        f.write(f"# {time.strftime('%Y-%m-%d %H:%M:%S %z')}\n")

    d = Dev(args.port, args.baud, args.log)
    checks = []

    def check(name, ok, detail=""):
        checks.append((name, ok))
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  [{detail}]" if detail and not ok else ""))

    clear = d.cmd("WIFI CLEAR", 1.5)
    check("WIFI CLEAR", "OK wifi cleared" in clear)
    info = d.cmd("INFO", 1.5)
    check("AP-only after clear", "mode=ap " in info and "ap_on=1" in info)

    ssid = f"__rflab_missing_{int(time.time()) % 10000}__"
    setr = d.cmd(f"WIFI {ssid} wrongpass", 1.5)
    check("WIFI SET accepted", "OK wifi" in setr)
    info = d.cmd("INFO", 1.0)
    check("connecting/APSTA state", "ap_on=1" in info and "sta=-" in info)

    print("# waiting 18s for 15s connect timeout / AP fallback ...")
    time.sleep(18)
    info = d.cmd("INFO", 2.0)
    check("AP config failure keeps AP", "mode=apsta " in info and "ap_on=1" in info and "sta=-" in info)
    check("AP IP still 192.168.4.1", "ap_ip=192.168.4.1" in info)

    webtest = d.cmd("WEBTEST", 12.0)
    check("Web via AP while reconnecting", "200 http://" in webtest and "webtest POST 202" in webtest)

    # 关键的启动路径:有凭据时先 STA-only(AP 关),15s 失败后自动开 AP。
    setr = d.cmd(f"WIFI {ssid} wrongpass", 1.5)
    check("WIFI SET before reboot", "OK wifi" in setr)
    boot = d.reset_boot(3.0)
    check("boot starts STA-only", "mode=connecting" in boot or "STA connecting" in boot)
    info = d.cmd("INFO", 1.0)
    check("AP off while connecting", "mode=connecting" in info and "ap_on=0" in info)

    print("# waiting 18s for boot connect timeout / AP fallback ...")
    time.sleep(18)
    after = d.cmd("INFO", 2.0)
    check("boot timeout -> AP fallback", "mode=apsta" in after and "ap_on=1" in after)
    check("fallback serial notice", "AP FALLBACK" in after)
    webtest = d.cmd("WEBTEST", 12.0)
    check("Web via AP after boot fallback", "200 http://" in webtest)

    clear = d.cmd("WIFI CLEAR", 1.5)
    check("WIFI CLEAR final", "OK wifi cleared" in clear)
    info = d.cmd("INFO", 1.5)
    check("AP-only final", "mode=ap " in info and "ap_on=1" in info)

    d.close()
    failed = [c for c in checks if not c[1]]
    print(f"\n{len(checks) - len(failed)}/{len(checks)} checks passed")
    if failed:
        print("failed:", ", ".join(c[0] for c in failed))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
