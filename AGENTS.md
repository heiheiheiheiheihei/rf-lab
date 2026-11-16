# AGENTS.md — RF-Lab

本文件是仓库的唯一入口。规则与验收口径在 `docs/`,这里只做路由与硬约束。

## 任务 → 文档

| 任务 | 读 |
|---|---|
| 改射频行为、命令语义、硬件/供电 | `docs/00-mvp.md` |
| 改 Web / BLE / Android / uConsole UI | `docs/01-ui.md` |
| 改 WiFi STA/AP 回退、配网 | `docs/01-ui.md` §3.1、`docs/evidence/2026-11-15-wifi.md` |
| 看当前状态、上机证据、未验证项 | `README.md`、`docs/evidence/2026-11-15-ui.md` |
| 看/改官网、配装器、价格与文案 | `site/README.md`、`site/index.html`、`site/app.js` |
| 接线、引脚、上电检查 | `hardware/README.md` |
| 固件构建、烧录、排障 | `firmware/README.md` |
| Android APK 构建/权限/BLE 桥 | `android/README.md` |
| uConsole 安装/键位/`Ctrl+Alt+R` | `uconsole/README.md` |

## 硬约束

1. **不改证据**:`docs/evidence/` 是实测记录;数字变了写新行/新文件,不改旧值。
2. **射频与 UI 分离**:所有传输(串口/HTTP/BLE/TUI)只提交既有命令行;不在 C3 上复制
   `SEND/LONG/STOP` 时序。新 UI 能力先改 `docs/01-ui.md`,射频能力先改 `docs/00-mvp.md`。
3. **烧录用 `./tools/flash.sh`**:它写 bootloader + 2MB 分区表 + app。只 `pio run -t upload`
   会留下旧的 1MB 分区表,1.06MB 镜像会启动失败(已实测)。
4. **2MB 分区保护**:`firmware/partitions.csv` 的 factory 必须是 `0x200000`;
   `board_build.partitions` 与 `scripts/check_partitions.py` 是防线,不要删。
5. **NVS 不擦**:码库/Wi-Fi/BLE PIN 在 0x9000;`tools/flash.sh` 刻意不擦 NVS。
6. **安全边界**:HTTP 无登录(AP 密码/局域网可达性是门槛);BLE 只有应用层 PIN;
   UI 的 ARM 只是防误触。不要把其中任何一条写成"硬门控/加密"。
7. **Wi-Fi 是 STA 优先、AP 回退**:有凭据时启动 STA-only(AP 关),15s 连不上自动开 AP;
   AP 里配网成功保持 AP,`WIFI CLEAR` 回 AP-only。不要改回"AP 常开";也不要声称
   真实 LAN 连接已验证——本机没有 AP,只验证了失败回退路径。
8. **单一 Web 资源源**:`web/index.html` + `web/app.css` + `web/app.js` 由
   `tools/build_web.py` 生成 `web/dist/index.html`、固件 gzip、Android assets;
   改 UI 后先跑 `python3 tools/build_web.py`,再构建固件/APK。
9. **app 分区余量**:镜像已 ~1.06MB/2MB;新增依赖先看 Flash/RAM 报告,不要把 2MB 用满。
10. **官网不越权**:`site/` 是静态宣传/规划页;CC1101/NRF24L01/SI4463/LR1121、MQTT/HA
    必须保持“路线图/未实现”标识,不得写成现役能力。改官网不动 `web/` 与固件。

## 验证命令

```bash
./tools/host_test.sh                 # 协议 76 + 事件环 53 + OLED/UI 1286 + TUI 12
node --check web/app.js
python3 tools/build_web.py --check
node --check site/app.js
python3 tools/site_check.py           # 官网静态验收
cd firmware && pio run -e esp32c3    # 构建(含 Web gzip + 分区校验)
./tools/flash.sh /dev/ttyACM0        # 上机(需硬件)
python3 tools/device_selftest.py --port /dev/ttyACM0 --tx
python3 tools/wifi_selftest.py --port /dev/ttyACM0
cd android && ./gradlew assembleDebug
```

真机/模拟器未覆盖的项见 `docs/evidence/2026-11-15-ui.md` §6 与
`docs/evidence/2026-11-15-wifi.md` §6;不要把构建成功写成 BLE/Android/uConsole/真实 LAN 验证。
