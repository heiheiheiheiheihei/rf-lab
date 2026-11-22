# 2026-11-22 上机自检:device_selftest 在 0.7.1 / 32 条设备上 26 pass / 0 fail / 1 skip

状态:`measured` · **真机串口实测** · **未发 RF**(`tx=false`,SEND/LONG/STOP 跳过)
设备:`/dev/ttyACM0`(Espressif USB JTAG/serial,`303a:1001`)· 板 `RF-Lab-1B59`
执行:`python3 tools/device_selftest.py --port /dev/ttyACM0 --json --log docs/evidence/2026-11-22-device-selftest.log`
时间:2026-11-22 13:46(+0800)
目的:关闭 [`02-plan.md`](../02-plan.md) **TST-01** 的上机判据——新自检从 `INFO` 读版本/条数、
输出可解析 JSON,且在**不是 0.6.0/5 条**的真实设备上不误报。

> 自然语言结论:新自检在真机上 **26/26 有效检查全过**(1 项因未加 `--tx` 刻意 SKIP),
> 退出码 0、JSON `ok=true`。**没有发射任何射频**;`RESET` 重启了设备,
> `BLE OFF/ON`、`DISP TEST 1` 均已恢复原状。

## 1. Build

| 项 | 值 |
|---|---|
| 设备自报 | `fw=0.7.1` `build=dd71bf6` `ui=85a1a9b4` `slot=0` `pending=0` |
| 本次是否构建/烧录 | **否**。只跑自检;设备在 08:59 已由 `tools/flash.sh` 烧成 `dd71bf6` |
| 可追溯性 | **仍不是 tag 构建**(`v0.7.1` tag 指向 `a7b4bf3`);`REL-01` 仍未关闭 |
| 本机固件构建 | 见 [`2026-11-22-a2a3-tooling-ci.md`](2026-11-22-a2a3-tooling-ci.md) §1(`1,098,608 B`,sha256 `e52a5312…`) |

`build=dd71bf6` 与当前 `HEAD` 一致(该 `build=` 只覆盖 `web/`、`firmware/src/`、`tools/build_web.py`
三处是否 dirty;全树情况见 §3.3 第 4 条)。设备端无法取镜像 sha256,
因此**不能**说它就是 §1 那个 `.bin`。

## 2. Host

本次没有重新跑主机测试(上一条 evidence 已记录 `host_test.sh` 11,051 + TUI 12 + selftest 18
与 `SAN=1` 全绿);本次上机输出由同一份 `tools/device_selftest.py` 生成。

## 3. Device(本次实测)

```text
$ python3 tools/device_selftest.py --port /dev/ttyACM0 --json --log .../2026-11-22-device-selftest.log
exit = 0
{"summary": {"total": 27, "passed": 26, "failed": 0, "skipped": 1}, "ok": true}
```

原始转录:[`2026-11-22-device-selftest.log`](2026-11-22-device-selftest.log)(**已脱敏**)
JSON:[`2026-11-22-device-selftest.json`](2026-11-22-device-selftest.json)

### 3.1 设备事实(`INFO` / `STATE`)

```text
# RF-Lab fw=0.7.1 (ESP32-C3 + YS-RF34DL)
# build=dd71bf6 ui=85a1a9b4
# oled 72x40 @0x3C ... present=yes on=yes
# net mode=sta ap_on=0 has_creds=1 ... sta=Mark sta_ip=192.168.5.125 rssi=-34
# ble on=on adv=yes conn=no name=RF-Lab-1B59 pin=set
# codebook ver=2 count=32/32 migrated=no
OK stored=32/32 rx_bytes=0 frames=0 acks=0 dups=0 stray=0 resync=0
```

与 [`2026-11-22-device-audit.md`](2026-11-22-device-audit.md)(`build=511afca+dirty`,13/32)
相比,**设备状态已变**:构建变了、码库从 13 条变成 **32/32 满库**。

时间线(只列有记录的点):

| 时间 | 事实 | 来源 |
|---|---|---|
| 08:59 | `tools/flash.sh` 烧 `dd71bf6`;随后 `INFO` 为 **13/32** | [`firmware-flash.md`](2026-11-22-firmware-flash.md) |
| 13:46 | 本次自检,`RESET` 后**第一条** `INFO` 已是 **32/32**;`LIST`=32、`LIST LONG`=32 | 本文 log |
| 14:03–14:06 | 静态观察 180s:仍 32/32、无新增、无 `RX`/`LEARN` 行 | [静态观察](#34-静态观察rf-03-相关) |

所以:这 19 条**不是本次自检写进去的**(自检在 `RESET` 后立即就看到了 32/32,且只做只读命令);
`LIST LONG` 中 **32 条全部没有 `manual`/`edited` 标记**,即都是**自动学码**,
不是 `NEW`/`EDIT` 造的。08:59→13:46 之间是谁按了学习遥控器/做了什么,需要用户确认。

### 3.2 26 项 PASS(JSON 逐项)

| 组 | 检查 |
|---|---|
| 启动/版本 | boot banner、INFO fw、INFO build/ui、boot↔INFO fw 一致 |
| 码库 | INFO count、容量内、`LIST`=32、`LIST LONG`=32、ids 1..N |
| 命令面 | `HELP` 含 `LIST LONG/NAME/GROUP/SYM/EDIT/MODE/WEBTEST` |
| 状态 | `STATE` JSON、STATE↔INFO fw 一致、STATE↔INFO count 一致 |
| 网络 | `mode=sta`;设备自测 `/api/hello` `/api/state` `/api/events` 全 200,`POST /api/cmd` 202 且执行 |
| 屏幕 | `present=yes`、`DISP TEST 1`、`DISP SHOW` 72×40 帧缓冲 |
| BLE | `BLE OFF` → `adv=no`,`BLE ON` → `adv=yes` |

唯一 SKIP:`SEND/LONG/STOP — no --tx (no RF sent)`。

### 3.3 发现(不在 A2/A3 范围,未修)

1. **串口把 12 列 OLED 缓冲当成 IP 原样打印,导致日志里的 IP 少一位。**
   启动后串口出现 `# net LAN OK 192.168.5.12`,但 `INFO`/`STATE`/`/api/hello` 都是
   `192.168.5.125`。根因:`firmware/src/main.c` 的 `s_net_note2[UI_COLS+1]` 只有 13 字节
   (12 字符),`poll_net_note()` 又把这份**已截断**的字符串用 `out("# net %s %s\n", …)` 打到串口。
   OLED 受 12 列限制要截断可以理解,**串口日志不该截断**。
   建议(0.7.2 P2,单独提交):串口用完整 `IPSTR`,OLED 再截断;或把 note 缓冲扩到 16 并只在 UI 侧截断。
   在修之前,凡看到 `192.168.5.12` 的转录都应理解为 `.125` 的截断。
   **已于 2026-11-22 修复并上机复测**:提交 `c4c9378`,启动行恢复为 `192.168.5.125`,
   见 [`2026-11-22-net-note-ip-fix.md`](2026-11-22-net-note-ip-fix.md)。
2. **串口明文打印 AP 密码与 BLE PIN**(`pass=…`、`pin=…`)。AP 密码是文档里的固定默认值
   (`rflab1234`,`RFLAB_AP_PASS` 可改),不算秘密;BLE PIN 是**每台设备随机**的 4 位应用层 PIN。
   本次两处都替换成 `<redacted>`(统一脱敏策略,避免以后把随机 PIN 带进公开仓库);
   后续脚本/人工转录至少要把 `pin=` 脱敏。
3. `RESET` 后设备重新关联 Wi-Fi,首次 `INFO` 仍是 `mode=connecting`,约 2s 内变 `mode=sta`;
   本机自检的等待循环工作正常,未触发 15s AP 回退(符合 STA 优先)。
4. **`INFO` 的 `build=` 与镜像里 ESP-IDF app descriptor 的版本串不一致。**
   设备 `INFO` 报 `build=dd71bf6`(干净),但 08:58 那次构建的 `.bin` 里 app descriptor 是
   `v0.7.1-4-gdd71bf6-dirty`。原因:`RFLAB_BUILD_ID` 只对 `web/`、`firmware/src/`、
   `tools/build_web.py` 判 dirty,而 IDF 的 `PROJECT_VER` 用全树 `git describe --dirty`;
   08:58 时 `tools/`、`docs/` 有未提交改动。**发版前必须全树干净**,否则两个来源会各说各话。
   读取方式(不需要联网):

   ```bash
   python3 - <<'PY'
   from pathlib import Path
   b = Path("firmware/.pio/build/esp32c3/firmware.bin").read_bytes()
   i = b.find(bytes.fromhex("3254cdab"))          # esp_app_desc_t magic
   print([x.decode() for x in b[i:i+128].split(b"\0") if x.isascii() and len(x) > 3][:5])
   PY
   ```

5. **同源重建的 sha256 不会相同。** app descriptor 里带构建时间(`08:58:48` / `Nov 22 2026`),
   08:47 与 08:58 两次构建同为 `dd71bf6`,sha256 分别是 `e52a5312…` 与 `43e9863…`。
   Release 的 `SHA256SUMS.txt` 只能校验「发布的那一个文件」,不能当成可重建证明
   (REL-01/CI-04 的记录口径要写清楚)。

### 3.4 静态观察(RF-03 相关)

```text
$ python3 /tmp/static_watch.py 180        # 只发 INFO/LIST;不 RESET、不发 RF、不写码库
START 2026-11-22T14:03:19+0800 count=32 codes=32
END   2026-11-22T14:06:20+0800 count=32 codes=32
DELTA count=0 over 180s
NEW_CODES: []
```

原始输出:[`2026-11-22-codebook-static-watch.log`](2026-11-22-codebook-static-watch.log)。

**边界(重要)**:观察时码库已经 32/32 **满**。即使模块收到新的 433M 帧,固件也只会打印
`RX ... (store full, not saved)`,不会增长计数;本次窗口内连 `RX`/`LEARN` 行都没有。
因此这 180s **不能**当作「RF-03 静态无幻码」的证据——只能说明「窗口内没有可见接收活动」。
RF-03 的 24h 判据仍未做,且满库状态下需要先腾出空位才有意义。
脚本是本次一次性脚本(未入 `tools/`),逻辑:打开 115200 → `INFO`+`LIST` → sleep N →
`INFO`+`LIST` → 比对 `count`/代码集合/未请求的 `RX`、`LEARN` 行。

## 4. Unverified

1. **空口发射**:未加 `--tx`,`SEND/LONG/STOP` 未执行——`RF-01`/`RF-02` 仍是待测。
2. **tag 构建可追溯**:`build=dd71bf6` 不是 tag;`REL-01` 未关闭。
3. **32 条满库的来源与写路径**:码库 32/32 全部是自动学码,但 08:59→13:46 之间怎么来的未确认;
   也没有验 BLE 写入/JSON 快照(`COV-03` 仍未关闭)。
4. **RF-03 静态判据**:180s 窗口无 `RX`/`LEARN`,但满库会挡住保存,**不能**替代 24h 静态测试。
5. **CI 首跑**:workflow 未推送,见上一条 evidence。
