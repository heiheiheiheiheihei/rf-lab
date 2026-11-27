# RF-LAB · 百元射频实验室

**丰俭由人:¥100 的固定码侦察包 → ¥999 的全频段工作台。**
一块 ESP32-C3,五个可换射频舱位;同一套命令契约延伸到 **Web / Android / uConsole /
OPPO Watch 2**,与官网共用一套视觉基线([`docs/01-ui.md` §5.1](docs/01-ui.md))。
现役链路是 **YS-RF34DL 315/433 固定码**;CC1101 / NRF24L01 / SI4463 / LR1121、
MQTT 与 Home Assistant 是路线图,不是现货能力。

[**官网 / 配装台(线上)**](https://heiheiheiheiheihei.github.io/rf-lab/) · [**下载 Android APK**](https://github.com/heiheiheiheiheihei/rf-lab/releases/latest/download/rf-lab-android.apk) · [**OPPO Watch 2 客户端**](watch/README.md) · [**下载 Watch APK**](https://github.com/heiheiheiheiheihei/rf-lab/releases/download/watch-v0.1.0/rf-lab-watch.apk) · [本地源](site/index.html) · [文档索引](docs/README.md) · [射频规格](docs/00-mvp.md) · [UI 规格](docs/01-ui.md) · [接线](hardware/README.md) · [官网维护说明](site/README.md)

![RF-LAB 官网首屏](site/assets/preview.webp)

## ¥100 → ¥999:先买一块能用的板

| 档位 | 预算 | 组成 | 能力 |
|---|---:|---|---|
| 侦察兵 SCOUT | ¥100 | CORE-C3 + YS-RF34DL | 315/433 固定码学习 / 回放(**现役,已上机**) |
| 突击手 ASSAULT | ¥299 | + CC1101 + NRF24L01 + 背板 + 天线包 | Sub-GHz + 2.4G(路线图) |
| 战术组 TACTICAL | ¥599 | + SI4463 + LR1121 + 收纳盒 | 全频段雏形(路线图) |
| 全频段 ALL-IN | ¥999 | 全模块 + FIELD DOCK | 全舱位 / 现场终端(路线图) |

> 价格为**参考物料价**,不含运费与工具;路线图模块未接入现役固件,不能下单即用。
> 交互式配装器:[`site/index.html`](site/index.html);预算/边界/发布说明:[`site/README.md`](site/README.md)。

### 射频模块

| 模块 | 频段 | 接口 | 角色 | 状态 |
|---|---|---|---|---|
| **YS-RF34DL** | 315 / 433 MHz | UART 9600 | 固定码学习 / 回放 | ✅ **现役,上机验证** |
| CC1101 | 300–928 MHz | SPI | Sub-GHz 通用收发 | ○ 路线图,未接入 |
| NRF24L01+ | 2.4 GHz | SPI | 2.4G 私有链路 | ○ 路线图,未接入 |
| SI4463 | 119–1050 MHz | SPI | 远距 / 窄带 | ○ 路线图,未接入 |
| LR1121 | Sub-GHz + 2.4G | SPI | 双频 LoRa / (G)FSK | ○ 路线图,未接入 |

> 配装器可以勾选路线图模块,那是**采购与规划**,不是固件能力。只有 YS-RF34DL 做过上机验证。

## 现役链路

两块板:**ESP32-C3 OLED** + **YS-RF34DL** 315/433 编解码模块。
按一下原遥控器 → 模块解码 → ESP32 存码 → 板载 OLED 显示状态 → **Web / BLE / 串口 / uConsole**
发射 → 原接收器动作。

![抽象接线图:两块板,四根线](docs/evidence/hw-2026-11-21-wiring.png)

硬件文档与官网视觉是**抽象接线图**(不画 3D/写实 PCB):只保留两块功能块、4 根线和必须正确的引脚;
可选的 PCB 底板另见 [`hardware/pcb/`](hardware/pcb/README.md)(已出 Gerber,未打样)。
唯一源是 [`hardware/wiring.json`](hardware/wiring.json);`tools/gen_wiring.py` 同时生成
`firmware/src/wiring_config.h`、`hardware/README.md` / `docs/00-mvp.md` 的接线表和
4 张 SVG,`gen_wiring.py --check` 在主机测试与固件构建前校验一致性。
官网 §01 用宽图(窄屏切竖图)、§02 放主控/模块引脚图;设备 UI 顶栏 `HW` 芯片(或按 `H`)
打开同一张图,`ZOOM/FIT` 缩放。
**3.3V ONLY**、共地、`GPIO3→RXD` / `GPIO4←TXD` 由接线表保证;图不是比例图,
引脚/供电仍以 [`hardware/README.md`](hardware/README.md) 为准。参考照片不入库。

同一块 C3 现在同时提供:

| 入口 | 形态 | 说明 |
|---|---|---|
| **Web** | LAN IP(STA)或 `http://192.168.4.1/`(AP 回退) | 板载 HTTP + SSE,手机/PC/uConsole 浏览器直接用 |
| **Android** | BLE APK | 同一份单页 UI 跑在 WebView,原生 BLE 桥 |
| **OPPO Watch 2** | 独立 BLE APK | 402×476 单屏;ARM → SEND;按住 HOLD → LONG,松手 STOP;46mm 真机已验证 |
| **uConsole** | 键盘 TUI | `rflab-tui`,HTTP 优先、USB 串口回退,琥珀/橄榄战术风 |
| **串口** | `tools/rf34dl.py` | 0.5.0 既有命令行,行为不变 |

### 手腕上的发射键

OPPO Watch 2 客户端把手机端的「选码 → ARM → SEND」压缩成一屏:码卡上下滑换码,
`SEND` 单发,`HOLD` 按住 350ms 才发 `LONG`、松手立刻 `STOP`;断链/退后台先停。
它只走 BLE,复用同一套 GATT/行协议,**不改固件、不改手机 APP**。

![OPPO Watch 2 实机控制页](site/assets/watch-control.webp)

- 46mm `OW20W1` / Android 8.1 (API 27) / 402×476 @320dpi 真机已验证:安装、扫描、
  PIN、32 码、ARM/SEND、HOLD/LONG/STOP、错误 PIN、退后台 STOP、OLED TEST。
- 独立 **release key** 签名(不与手机 APK 共用);debug → release 需卸载后重装。
- 公开 Release:[**下载 rf-lab-watch.apk**](https://github.com/heiheiheiheiheihei/rf-lab/releases/download/watch-v0.1.0/rf-lab-watch.apk) ·
  [`SHA256SUMS.txt`](https://github.com/heiheiheiheiheihei/rf-lab/releases/download/watch-v0.1.0/SHA256SUMS.txt)
- 规格与安装:[`watch/README.md`](watch/README.md) · 证据:[`docs/evidence/2026-11-22-watch-release.md`](docs/evidence/2026-11-22-watch-release.md)

**Wi-Fi 模式**:有凭据先 `STA-only` 接局域网(AP 关闭);15s 连不上自动打开 AP 回退;
AP 下可以直接用,也可以重新配网或 `WIFI CLEAR`。AP 里配网成功会保持 AP,避免踢掉
正在配网的浏览器;下次启动再回到 STA-only。

- **射频/命令唯一规格**:[`docs/00-mvp.md`](docs/00-mvp.md)
- **跨平台 UI 规格**:[`docs/01-ui.md`](docs/01-ui.md)
- **接线**:[`hardware/README.md`](hardware/README.md)(模块 4 根线;引脚权威)
- **固件**:[`firmware/README.md`](firmware/README.md)(ESP-IDF 5.5.5 / ESP32-C3)
- **Android**:[`android/README.md`](android/README.md) · **uConsole**:[`uconsole/README.md`](uconsole/README.md) · **Watch**:[`watch/README.md`](watch/README.md)

## 官网 / 配装台

线上:<https://heiheiheiheiheihei.github.io/rf-lab/>(GitHub Pages / GitHub Actions 发布 `site/`)。

[`site/index.html`](site/index.html) 是静态单页:4 档预算、9 个模块勾选、实时配装单、
**内嵌真实单页 UI 的交互终端 demo**、OPPO Watch 2 实机区、MQTT/HA 路线图。
无站点构建、无外部字体、无 CDN、无外部/API 请求。

§03 终端区直接跑 `web/` 的真实 UI(iframe):可以 ARM、SEND、LONG、STOP、ERASE,也可以用
「模拟遥控器」学习一条新码;`site/assets/terminal-demo.js` 在 iframe 内接管
`fetch`/`EventSource`,本地回放 `/api/*` 与 SSE,只标 `SIM`,不发网络请求、不代表真机在线。

![配装台:四档预算 + 实时物料小计](site/assets/preview-builder.webp)

```bash
python3 tools/gen_wiring.py --check    # JSON -> 代码头/表/SVG 一致性
python3 tools/build_web.py             # 同步生成 site/assets/terminal.html
python3 -m http.server 8088 -d site    # http://127.0.0.1:8088/
python3 tools/site_check.py            # 静态验收(含 demo 资源)
node --check site/app.js
node --check site/assets/terminal-demo.js
```

## 快速开始

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"

# 1) 构建 + 烧录(必须用 flash.sh:它会一起写 2MB 分区表)
./tools/flash.sh /dev/ttyACM0

# 2) Web:
#    有 STA 凭据:设备先连局域网;成功用串口 INFO / OLED 显示的 LAN IP 访问。
#    无凭据或 15s 连不上:连 AP "RF-Lab-XXXX" / 密码 rflab1234,打开 http://192.168.4.1/
#    AP 设置页填 SSID/PASSWORD 配网;FORGET/AP 清除凭据。
#    串口会打印 AP SSID/IP、BLE PIN;OLED 开机页也显示 PIN / LAN / AP 状态。

# 3) 串口交互(既有)
python3 tools/rf34dl.py --port /dev/ttyACM0
python3 tools/rf34dl.py --port /dev/ttyACM0 learn 15
python3 tools/rf34dl.py --port /dev/ttyACM0 send 1

# 4) uConsole 键盘 TUI
./uconsole/install.sh
rflab-tui --url http://192.168.4.1   # 若已接 LAN,换成 INFO/OLED 的 LAN IP

# 5) Android(已装 JDK/SDK/Gradle 时)
python3 tools/build_web.py
cd android && ./gradlew assembleDebug

# 6) OPPO Watch 2(独立 release key,见 watch/README.md)
set -a; . "$HOME/.config/rf-lab/watch-keystore.env"; set +a
./watch/check.sh
cd watch && ../android/gradlew assembleRelease
# android/app/build/outputs/apk/debug/app-debug.apk
```

主机单测:`./tools/host_test.sh`(协议 76 + 码库 368 + 事件环 9,427 + OTA 41 + radio 114 + OLED/UI 1,286 + TUI 12)。
APP 自更新逻辑主机测试:`./tools/app_update_test.sh`(版本比较 + `SHA256SUMS` 查表,36 checks)。
Wi-Fi 回退上机自检:`python3 tools/wifi_selftest.py --port /dev/ttyACM0`(15 项)。

## 状态

| 项 | 状态 |
|---|---|
| 构建 | ✅ v0.7.2 tag `e0f155a`:**1,126,928B / 2,031,616B(55.5%)**,RAM 90,260B(27.5%);工作区 M1a:1,154,240B(56.8%),未上机 |
| 主机单测 | ✅ C 测试 **11,312** checks(协议 76 + 码库 368 + 事件环 9,427 + OTA 41 + radio 114 + OLED/UI 1,286);另有 TUI 12、selftest 18、APP 自更新 36 |
| 上机:学码 / 存码 / 改波特率 | ✅ 2026-11-15(0.5.0 证据) |
| 上机:发射 `SEND/LONG/STOP` | ⚠️ 2026-11-15 有 `F0/FE/FF`;2026-11-21 模块命令方向 0 应答;11-22 `MODE` 回 `F2`,但 **`SEND` 未复测** |
| 上机:Wi-Fi | ✅ 真实 `SSID-A`→`192.168.5.117`;AP 回退 15/15;AP 配网 / `WIFI CLEAR`;⏳ AP 外部浏览器、STA 运行中掉线 |
| 上机:Web | ✅ HTTP/SSE/POST + 真实 Chromium 无 console error;⏳ 新皮肤全量回归 |
| 上机:BLE | ✅ 真机连接/鉴权/命令回显/码表/状态栏(2026-11-21);⏳ BLE `SEND`、错误 PIN |
| Android APK | ✅ **0.7.2 已发** [Release](https://github.com/heiheiheiheiheihei/rf-lab/releases/tag/v0.7.2);1,050,272B/versionCode 4/v2;✅ 0.7.1 真机安装/自更新;⏳ 0.7.2 真机安装 |
| APP 自更新 | ✅ 36 checks + 真实 Release 资产;✅ 真机弹窗/下载/系统安装器/覆盖安装(0.7.0→0.7.1);⏳ 失败路径 |
| 码库 v2 | ✅ 21/64;迁移/UTF-8/新建/编辑/删除;✅ BLE `NAME/GROUP/SYM/EDIT` 写入;⏳ 写后重启持久化 |
| OTA | ✅ 双槽 A→B→A + 错 token/坏镜像/断流;⏳ 浏览器上传、崩溃回滚、AP、真断电 |
| uConsole TUI | ✅ 主机解析/键位/PTY;⏳ 真机 |
| 官网 | ✅ 交互 demo + Pages 部署;⏳ 线上交互回归 |
| 官网 APK 入口 | ✅ 指向 `releases/latest/download`;⏳ 真机下载安装 |
| UI 一键固件更新(0.7.3) | ○ **未实现**;BOOT 确认 + GitHub Pages + 设备 SHA-256;见 [`docs/01-ui.md`](docs/01-ui.md) §3.4.1 |
| 路线图 | ○ MQTT / HA / 多模块未实现,接口草案见官网 §04 |
| 距离 / 长发时接收器 | ⏳ 未测,见规格 §7 |
| 上机证据 | [`2026-11-22-release-0.7.2.md`](docs/evidence/2026-11-22-release-0.7.2.md) · [`2026-11-22-app-ble.md`](docs/evidence/2026-11-22-app-ble.md) · [`2026-11-21-ota.md`](docs/evidence/2026-11-21-ota.md) · [`2026-11-15-bringup.md`](docs/evidence/2026-11-15-bringup.md) · [`2026-11-15-wifi-lan.md`](docs/evidence/2026-11-15-wifi-lan.md) |

> 完整测试登记、优先级与 gate 见 [`docs/02-plan.md`](docs/02-plan.md) §3;新会话交接见 [`docs/03-execution.md`](docs/03-execution.md)。

## 范围

| 做 | 不做 |
|---|---|
| 315/433 固定码学习 / 解码 / 存 / 发 | 滚动码克隆 / 重放 |
| 单次发射、长发 / 停止 | 原始 OOK 波形记录(0.7.x) |
| 码库命名/分组/手工建码/改 band+4B code | 协议位解析 / 模块 TX 时长编辑 / `.sub` 导入(0.7.x;0.8.x 只读见 ADR-003) |
| 改模块波特率 / 输出模式 | 扫频 / RSSI / 频谱 |
| Wi-Fi STA 接 LAN + AP 回退(HTTP + SSE) | 公网/云账户 |
| 双槽 OTA(Web/串口,整包含页面) | BLE 上传固件 / 云推送 / 固件签名校验 |
| APP 自更新(GitHub Release,一键/忽略) | 静默安装 / 应用商店分发 |
| BLE 外设 + Android APP | BLE 链路加密(只有应用层 PIN) |
| uConsole 键盘 TUI | OLED 菜单/按键交互 |
| 串口 / OLED 状态页(既有,保留) | OLED 中文/图形界面 |
| 官网 / ¥100–999 配装规划(静态) | 把配装器里的路线图模块写成已支持 |
| 自有固定码设备的复制验证 | 对未知设备的"安全"断言 |

> ⚠️ 固定码本身可被复制:测试只对**自有**设备做,结论只对固定码成立。
> Web 的 `ARM` 只防误触,不是发射硬门控;**HTTP 无登录,AP 密码/局域网可达性是门槛**;
> BLE 只有 4 位应用层 PIN,不是强安全边界。

## 版本计划

| 版本 | 目标 | 状态 |
|---|---|---|
| 0.7.3 | UI 一键固件更新:查 Pages manifest、BOOT 确认、下载/校验/上传 | ○ 规划,见 [`docs/01-ui.md`](docs/01-ui.md) §3.4.1 |
| 0.8.0 | 模块抽象层 + SPI 背板;CC1101 首发,NRF24/SI4463/LR1121 分批(0.8.x) | ○ 设计目标;M1a HAL 仅主机/构建 |
| 0.9.0 | 本地 MQTT bridge + Home Assistant Discovery | ○ 设计目标 |
| 1.0 | 多模块外壳 / 小批量 | ○ 设计目标 |

> 官网 §04 的 MQTT / HA 拓扑与 topic 是**接口草案**,不是可售能力。
> 现在只有 HTTP/SSE + BLE/串口;不要提前写“支持 Home Assistant”。

## 截图

官网 §03 的交互终端(真实单页 UI,传输为本地模拟):

![官网交互终端](docs/evidence/site-demo-2026-11-21-desktop.png)

设备 UI 的硬件模态(本地 Chromium mock)与真实 LAN 页面(0.6.0 旧皮肤,非 mock):

![设备 UI 硬件模态](docs/evidence/hw-2026-11-21-ui-desktop.png)
![真实 LAN](docs/evidence/ui-lan-live.png)

更多 mock 布局图在 [`docs/evidence/`](docs/evidence/);截图不是上机证据,上机结论只以 evidence 的 md/log 为准。
