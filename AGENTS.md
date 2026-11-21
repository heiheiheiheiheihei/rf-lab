# AGENTS.md — RF-Lab

本文件是仓库的唯一入口。规则与验收口径在 `docs/`,这里只做路由与硬约束。

## 任务 → 文档

| 任务 | 读 |
|---|---|
| 改射频行为、命令语义、硬件/供电 | `docs/00-mvp.md` |
| 改 Web / BLE / Android / uConsole UI | `docs/01-ui.md` |
| 改接线图 / 硬件效果图 / 官网插图 | `hardware/wiring.json`、`tools/gen_wiring.py`、`docs/01-ui.md` §5.3、`site/README.md` |
| 改 WiFi STA/AP 回退、配网 | `docs/01-ui.md` §3.1、`docs/evidence/2026-11-15-wifi.md` |
| 改 OTA / 分区 / 回滚 | `docs/01-ui.md` §3.4、`firmware/README.md`、`firmware/partitions.csv` |
| 看当前状态、上机证据、未验证项 | `README.md`、`docs/evidence/2026-11-15-ui.md` |
| 看/改官网、配装器、价格与文案 | `site/README.md`、`site/index.html`、`site/app.js` |
| 接线、引脚、上电检查 | `hardware/wiring.json`、`hardware/README.md` |
| 固件构建、烧录、排障 | `firmware/README.md` |
| Android APK 构建/权限/BLE 桥 | `android/README.md` |
| uConsole 安装/键位/`Ctrl+Alt+R` | `uconsole/README.md` |

## 硬约束

1. **不改证据**:`docs/evidence/` 是实测记录;数字变了写新行/新文件,不改旧值。
2. **射频与 UI 分离**:所有传输(串口/HTTP/BLE/TUI)只提交既有命令行;不在 C3 上复制
   `SEND/LONG/STOP` 时序。新 UI 能力先改 `docs/01-ui.md`,射频能力先改 `docs/00-mvp.md`。
3. **烧录用 `./tools/flash.sh`**:它写 bootloader + 双 OTA 分区表 + app 到 `ota_0`,
   并擦 `otadata`。只 `pio run -t upload` 会留旧表/写错槽,镜像启动失败(已实测)。
4. **双 OTA 分区保护**:`partitions.csv` 必须是 `otadata` 0x10000/0x2000 +
   `ota_0` 0x20000/0x1F0000 + `ota_1` 0x210000/0x1F0000,**没有 factory**;
   `board_build.partitions` 与 `scripts/check_partitions.py` 是防线,不要删。
   首次从旧单 app 表迁移必须 USB 重烧一次。
5. **NVS 不擦**:码库/Wi-Fi/BLE PIN 在 0x9000;`tools/flash.sh` 与 OTA 都刻意不碰 NVS。
6. **安全边界**:HTTP 无登录(AP 密码/局域网可达性是门槛);BLE 只有应用层 PIN;
   UI 的 ARM 只是防误触。不要把其中任何一条写成"硬门控/加密"。
7. **Wi-Fi 是 STA 优先、AP 回退**:有凭据时启动 STA-only(AP 关),15s 连不上自动开 AP;
   AP 里配网成功保持 AP,`WIFI CLEAR` 回 AP-only。不要改回"AP 常开";也不要声称
   真实 LAN 连接已验证——本机没有 AP,只验证了失败回退路径。
8. **单一 Web 资源源**:`web/index.html` + `web/app.css` + `web/app.js` 由
   `tools/build_web.py` 生成 `web/dist/index.html`、固件 gzip、Android assets,
   以及 `site/assets/terminal.html`(官网交互 demo);改 UI 后先跑
   `python3 tools/build_web.py`,再构建固件/APK。
9. **app 分区余量**:镜像已 ~1.09MB/1.94MB 槽;新增依赖先看 Flash/RAM 报告,不要把槽用满。
10. **官网不越权 / 视觉同源**:`site/` 是静态宣传/规划页;CC1101/NRF24L01/SI4463/LR1121、MQTT/HA
    必须保持“路线图/未实现”标识,不得写成现役能力。官网同时是 UI 视觉基线(`site/style.css`
    的 token 与组件语法):`web/`/固件页只复制视觉、不复制脚本/依赖,细则见 `docs/01-ui.md` §5.1。
    改官网版式不动 `web/` 与固件;设备 UI 变了才重生成 `site/assets/terminal.html`(官网交互 demo)。
11. **OTA 边界**:`POST /api/ota` 默认关闭,只有串口 `OTA` armed 后才接受;token 是应用层门,
    不是签名/加密,没有 secure boot。**设备 OTA 只更新设备,不更新 APK**。APK 自更新是独立的
    客户端路径(0.7.0 起,`android/.../Updater.java`):只读 GitHub Release,过 SHA-256/包名/
    版本/签名自检后交给系统 `PackageInstaller`,由用户确认。**真机自更新未验证**——不要把
    "构建/主机测试通过"写成"已上机自更新";也不要让 APP 自更新去碰设备固件。
12. **接线单一源**:`hardware/wiring.json` 同时生成 `firmware/src/wiring_config.h`、
    两张文档接线表和 `web/art/hw-*.svg`;改引脚只改 JSON,再跑 `python3 tools/gen_wiring.py`。
    `--check` 已进主机测试与固件构建前;不要手改生成物,也不要让图/表/代码各写一套。

## 验证命令

```bash
./tools/host_test.sh                 # 协议 76 + 事件环 53 + OTA 41 + OLED/UI 1286 + TUI 12
node --check web/app.js
python3 tools/gen_wiring.py --check   # JSON -> 代码头/表/SVG
python3 tools/build_web.py --check
node --check site/app.js
node --check site/assets/terminal-demo.js
python3 tools/site_check.py           # 官网静态验收(含 demo 资源)
cd firmware && pio run -e esp32c3    # 构建(含 Web gzip/build 指纹 + 分区校验)
./tools/flash.sh /dev/ttyACM0        # 首次/迁移烧录(需硬件)
python3 tools/ota.py --host <ip> --serial /dev/ttyACM0   # 之后的固件更新
python3 tools/device_selftest.py --port /dev/ttyACM0 --tx
python3 tools/wifi_selftest.py --port /dev/ttyACM0
cd android && ./gradlew assembleDebug   # 或 assembleRelease(需仓库外密钥)
./tools/app_update_test.sh            # APP 自更新:版本比较 + SHA256SUMS 查表(无需 SDK)
```

真机/模拟器未覆盖的项见 `docs/evidence/2026-11-15-ui.md` §6 与
`docs/evidence/2026-11-15-wifi.md` §6;不要把构建成功写成 BLE/Android/uConsole/真实 LAN 验证。
