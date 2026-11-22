# 2026-11-22 修复:串口网络事件把完整 IPv4 截成 12 字符

状态:`measured` · **真机复测通过** · **未发 RF**
提交:`c4c9378 fix(firmware): keep full IPv4 in the serial net note`
设备:`/dev/ttyACM0` · 板 `RF-Lab-1B59`(ESP32-C3 + YS-RF34DL)
发现于:[`2026-11-22-device-selftest.md`](2026-11-22-device-selftest.md) §3.3 第 1 条。

## 1. 问题与修法

`main.c` 的 `poll_net_note()` 复用了 OLED 的 12 列缓冲(`s_net_note2[UI_COLS+1]`),
再把这份**已截断**的字符串打到串口。结果:

```text
# net LAN OK 192.168.5.12      ← 实际是 192.168.5.125
```

修法(只动 `firmware/src/main.c`):

- 事件缓冲改为 `NET_NOTE_MAX = 32`(完整 IPv4 足够),串口打印完整字符串;
- OLED 仍由 `ui_note_boot()` 里的 `copy_trunc()` 截到 `UI_COLS`(12 列),显示行为不变。

**Key judgment**:截断属于显示约束,不该发生在串口日志上;两处共用一个 12 字符缓冲是最初的
耦合点。**Avoided misfix**:没有把 `UI_COLS` 改大(会动 OLED 布局),也没有在 `out()`
里另拼 IP(会绕开事件队列,丢失“同一事件同时给屏和串口”的语义)。

## 2. Build

```bash
./tools/flash.sh /dev/ttyACM0
```

`flash.sh` 先 `pio run -e esp32c3`(含 wiring/web/分区检查),再写 bootloader +
双 OTA 分区表 + app 到 `ota_0`,擦 `otadata`;NVS 未动。

| 项 | 值 |
|---|---|
| 提交 | `c4c9378`(工作树 `web/`、`firmware/src/` 干净,`build=` 无 `+dirty`) |
| 镜像 | **1,098,608 B**;sha256 `8d496673e4a90cce9f9da86e25555a4d95b0ea0518e76ffeb4c53982d3248951` |
| 设备自报 | `fw=0.7.1` `build=c4c9378` `ui=85a1a9b4` `slot=0` `pending=0` |
| NVS | 码库 **32/32** 保留;Wi-Fi 回连 `192.168.5.125`;BLE `adv=yes` |

> `c4c9378` 是 **master 提交,不是 tag**;v0.7.1 Release 不包含这个修复,REL-01 仍未关闭。
> 同源两次构建 sha256 不同(app descriptor 带构建时间),见
> [`2026-11-22-device-selftest.md`](2026-11-22-device-selftest.md) §3.3 第 5 条。

## 3. Host

```bash
./tools/host_test.sh        # 11,051 + TUI 12 + selftest 18,0 failure
SAN=1 ./tools/host_test.sh  # 同上,ASan/UBSan 0 failure
```

固件改动只在 `main.c`(ESP-IDF 侧),不改主机测试覆盖的纯 C 模块;两轮全绿。

## 4. Device

复位后的启动转录(完整见
[`2026-11-22-device-selftest-ipfix.log`](2026-11-22-device-selftest-ipfix.log),JSON 见
[`2026-11-22-device-selftest-ipfix.json`](2026-11-22-device-selftest-ipfix.json)):

```text
# build=c4c9378 ui=85a1a9b4 ota_slot=0 pending_verify=0
# net LAN OK 192.168.5.125          ← 修复后:完整 13 字符
# net mode=sta ... sta_ip=192.168.5.125
# codebook ver=2 count=32/32 migrated=no
```

| 检查 | 结果 |
|---|---|
| 完整 IP 行 | **2 次**(两次复位各一次),均 `192.168.5.125`;无 `192.168.5.12` 行 |
| 自检 | `device_selftest --json`:**26 pass / 0 fail / 1 skip**,`ok=true`,`build=c4c9378` |
| skip | `SEND/LONG/STOP — no --tx`(未发 RF) |
| 回归 | `INFO/STATE/LIST/LIST LONG/HELP/DISP/BLE/WEBTEST` 与修复前一致 |

> 第一次跑自检时工具调用被中断(JSON 未落盘),转录仍保留了启动行;第二次完整重跑。
> 日志按 `--log` 的追加语义包含两轮,均能看到完整 IP。

## 5. Unverified

1. 空口 `SEND/LONG/STOP`(没有加 `--tx`),RF-01/RF-02 仍待测。
2. tag 构建 / Release 固件资产:设备跑的是 `c4c9378`(master),不是 tag;REL-01 未关闭。
3. 新 CI workflow 的首次运行(未推送)。
