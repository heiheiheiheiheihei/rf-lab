# 固件(ESP32-C3 / ESP-IDF 5.5.5)

**状态**:0.7.1 · 可构建 · 协议/码库/OLED/事件环/TUI/OTA 门票有主机单测 · **2026-11-21 已上机**
(见文末「上机结果」)。0.7.1 在 0.6.0 的 Wi-Fi APSTA + HTTP/SSE + NimBLE 基础上加入码库 v2;
0.6.0 同日加入**双槽 OTA**(Web 上传 + 回滚 + 串口 token);规格见
[`../docs/01-ui.md`](../docs/01-ui.md) §3.4,射频行为仍以 [`../docs/00-mvp.md`](../docs/00-mvp.md) 为准。

目标板:ESP32-C3 0.42" OLED(4MB Flash,原生 USB)。板载 SSD1306 72×40 由本版驱动,
只做状态面板;命令与行为规格在 [`docs/00-mvp.md`](../docs/00-mvp.md) §5–6(**唯一权威**,这里不重复)。

## 结构

| 文件 | 职责 |
|---|---|
| [`src/rf34dl.h`](src/rf34dl.h) / [`src/rf34dl.c`](src/rf34dl.c) | **纯协议层**:8 字节帧组帧/解帧、`F0/F1/F2/F3/FE/FF` 应答识别。不依赖 ESP-IDF |
| [`src/wiring_config.h`](src/wiring_config.h) | **生成物**:引脚/波特率宏,来自 `hardware/wiring.json`;勿手改 |
| [`src/oled_panel.h`](src/oled_panel.h) | 72×40 面板参数(列偏移 28、MUX/COM/IREF、初始化序列),几何的唯一数据源 |
| [`src/oled_gfx.h`](src/oled_gfx.h) / [`src/oled_gfx.c`](src/oled_gfx.c) | **纯帧缓冲**:360B 页式显存、画点/线/框、5×7 ASCII、ASCII dump |
| [`src/font5x7.h`](src/font5x7.h) | 5×7 字库(Adafruit GFX 子集,BSD 声明在文件头) |
| [`src/ui.h`](src/ui.h) / [`src/ui.c`](src/ui.c) | **纯状态映射**:事件 → 5 行 × 12 字符;测试图案 |
| [`src/oled.h`](src/oled.h) / [`src/oled.c`](src/oled.c) | 仅 I2C/SSD1306:probe、初始化、整帧写屏、开关屏、耗时统计 |
| [`src/link.h`](src/link.h) / [`src/link.c`](src/link.c) | 小型事件环 + JSON 转义(纯 C,主机单测) |
| [`src/app.h`](src/app.h) | main 暴露给传输层的命令入队 / 状态快照接口 |
| [`src/net.h`](src/net.h) / [`src/net.c`](src/net.c) | Wi-Fi APSTA、STA 凭据 NVS、IP/RSSI |
| [`src/ota_ticket.h`](src/ota_ticket.h) / [`src/ota_ticket.c`](src/ota_ticket.c) | **纯 C**:8-hex 一次性 token、10 分钟过期、5 次错误锁定(主机单测) |
| [`src/ota.h`](src/ota.h) / [`src/ota.c`](src/ota.c) | 双槽写入、`esp_ota_set_boot_partition`、PENDING_VERIFY 确认、进度 |
| [`src/web_server.h`](src/web_server.h) / [`src/web_server.c`](src/web_server.c) | HTTP 静态页、`/api/*`、SSE(异步 handler)、`POST /api/ota` |
| [`src/ble_link.h`](src/ble_link.h) / [`src/ble_link.c`](src/ble_link.c) | NimBLE GATT(自定义 NUS 风格)、PIN、分片通知 |
| [`src/main.c`](src/main.c) | 控制台、NVS、模块 UART1、命令队列、快照、网络初始化、OLED 调度 |
| [`test/host/test_rf34dl.c`](test/host/test_rf34dl.c) | 协议单测:真实抓包回归、重同步、ACK(含 FE/FF)、fuzz |
| [`test/host/test_ui.c`](test/host/test_ui.c) | 帧缓冲/字库/状态页/面板几何单测 |

## 构建

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd firmware
pio run -e esp32c3
```

构建前会自动跑 `tools/gen_wiring.py --check`(**接线 JSON ↔ 代码头 ↔ 表 ↔ SVG**)
和 `scripts/embed_web.py`(把 `web/` 打成 gzip、EMBED_FILES,并注入
`RFLAB_BUILD_ID` / `RFLAB_UI_HASH`);构建后 `scripts/check_partitions.py` 校验
**otadata + 两个 ≥0x1E0000 的 OTA 槽**,且镜像放得下。

产物 `.pio/build/esp32c3/firmware.bin`。**烧录请用仓库根的 `./tools/flash.sh /dev/ttyACM0`**:
它一次写 bootloader + 双 OTA 分区表 + app 到 `ota_0`,并擦 `otadata`。只 `pio run -t upload`
会留下旧的分区表/槽,镜像启动失败或写错槽(0.6.0 实测踩过)。

**首次从旧的单 app(2MB factory)迁移必须 USB 重烧一次**;分区表不能 OTA 自己。
之后用 `tools/ota.py` 或 Web UI 的 `U` 面板,不动 NVS(码库/Wi-Fi/BLE PIN)。

改了 `sdkconfig.defaults` 先 `rm -f sdkconfig.esp32c3` 再编(它是生成物)。

首次为 C3 构建会下载 **RISC-V 工具链**(`toolchain-riscv32-esp`,约 200MB);本机已完成。

## 主机单测(不接硬件)

```bash
./tools/host_test.sh        # 协议 76 + 码库 368 + 事件环 9418 + OTA 41 + OLED/UI 1286 + TUI 12
SAN=1 ./tools/host_test.sh  # 加 ASan/UBSan
```

只编纯 C 源码,不碰 ESP-IDF,秒级。含 **2026-11-14 真模块抓包**(16 字节两帧)、
**2026-11-15 长发/停止 `FE/FF` 状态**、帧缓冲页式位映射与 SSD1306 几何常量的回归。

## 烧录

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd firmware
pio run -e esp32c3 -t upload --upload-port /dev/ttyACM0
```

原生 USB-Serial-JTAG,不需要驱动。端口不出现时:按住 **BOOT**(GPIO9)插线进下载模式,或换一根**数据线**(充电线没有 D+/D−)。

## OTA(首次 USB 迁移之后)

| 项 | 值 |
|---|---|
| 分区 | `nvs` 0x9000/0x6000 · `phy` 0xf000/0x1000 · `otadata` 0x10000/0x2000 · `ota_0` 0x20000/0x1F0000 · `ota_1` 0x210000/0x1F0000(无 factory) |
| 入口 | 串口 `OTA`(生成 8-hex token,10 分钟有效;OLED 显示 10s) · `OTA OFF` |
| 上传 | `POST /api/ota`,头 `X-RFLAB-OTA: <token>`,`Content-Length` + 原始 `.bin`;成功 200 后约 1.5s 重启 |
| 工具 | `python3 tools/ota.py --host <ip> --serial /dev/ttyACM0`(或 `--token`) |
| Web | 帮助面板 `U` → `FIRMWARE UPDATE`;仅 HTTP 页面可用,APP/BLE 不提供 |
| 回滚 | 新镜像首次启动为 `PENDING_VERIFY`;`main.c` 在 6s 后 `mark valid`,之前崩溃/复位由引导器回滚 |
| 不动 | NVS 0x9000(码库/Wi-Fi/BLE PIN);写的是非运行槽 |

**边界**:HTTP 无登录,所以 OTA 端点默认关闭,只有串口 `OTA` armed 后才接受;
token 是应用层门,不是签名/加密。没有 secure boot。**APP 的页面在 APK 里,OTA 不会更新
APK**;网页端和 APP 的 `build` / `ui` 可在帮助面板或 `/api/hello` 对比。

实测(2026-11-21):镜像 1,086,800B;A(ota_0)→B(ota_1)→A(ota_0)→final(ota_1) 三次 OTA 成功;
错 token 401、随机镜像 500、中途断流后仍运行旧槽;NVS 9 条码与 Wi-Fi 凭据保持。
证据:[`../docs/evidence/2026-11-21-ota.md`](../docs/evidence/2026-11-21-ota.md)。

## 跨平台 UI(0.6.0 起,0.7.1 扩码库)

| 项 | 值 |
|---|---|
| 模式 | `ap` / `connecting` / `sta` / `apsta`;有凭据先 STA-only,15s 失败回 AP |
| AP | `RF-Lab-XXXX`(MAC 末两字节),WPA2 `rflab1234`,`192.168.4.1`;仅回退/配网时开 |
| STA | `WIFI <ssid> <pass>`、Web 设置页或 `/api/wifi`;凭据存 NVS |
| Web | `/`(gzip 单页)、`/api/hello|state|codes|net|events|cmd|wifi|wifi/clear|ota` |
| SSE | `/api/events`;`httpd_req_async_handler_begin` + 每连接任务,最多 2 条 |
| BLE | `RF-Lab-XXXX`;Service `5a3d0001-…`;RX `…0002` / TX `…0003`;MTU 247 |
| BLE PIN | 4 位,NVS `ble_pin`;OLED 开机页 5s;串口 `PIN`(仅串口) |
| 自测 | `WEBTEST` GET `/api/hello|state|events` + POST `/api/cmd`;`wifi_selftest.py` 回退 15/15 |

UI 的 ARM 是防误触,不是硬门控;BLE PIN 是应用层,不是链路加密。

## 控制台

| 项 | 值 |
|---|---|
| 通道 | ① 原生 USB-Serial-JTAG(板载 USB-C)② UART0 GPIO20/21 |
| 参数 | 115200 8N1,无流控 |
| 日志 | IDF 控制台关闭(`CONFIG_ESP_CONSOLE_NONE`),不污染命令行 |

主机侧工具:[`tools/rf34dl.py`](../tools/rf34dl.py)(交互 + 一次性命令 + `learn N` 监听)。

## 板载 OLED

| 项 | 值 |
|---|---|
| 接口 | I2C0,**SDA=GPIO5 / SCL=GPIO6**,0x3C,板载 3.3V |
| 速率 | 400kHz 起;初始化失败自动回退 100kHz |
| 显存 | 72 列 × 5 页 = **360B**;列窗口 28..99 |
| 刷新 | 事件驱动,最多 10Hz;整帧 361B @400kHz **~8.6ms** |
| 统计 | `INFO` 的 `flush/err/last_us/max_us`;失败不阻塞启动,串口行为与 0.4.0 一致 |

命令:

| 命令 | 作用 |
|---|---|
| `DISP ON\|OFF` | 开/关屏(`0xAF/0xAE`,显存保留) |
| `DISP TEST [s]` | 四边框 + 非对称角块 + `RF-Lab/72x40/SSD1306/COL 28`;默认 10s,到期回状态页 |
| `DISP SHOW` | 把当前帧缓冲打成 40 行 × 72 列 `#/.` ASCII(不读玻璃) |

网络 / BLE / 诊断:

| 命令 | 作用 |
|---|---|
| `WIFI [status]\|[ssid pass]\|CLEAR` | 配 STA / 忘掉 STA;凭据存 NVS |
| `BLE [on\|off]` | BLE 广播开关/状态(`adv=yes/no`) |
| `PIN` | 显示 BLE PIN(仅串口) |
| `LIST LONG` | id/band/code + name/group/sym/flags |
| `NEW <band> <hex> [name]` | 手工建码 |
| `NAME/SYM/GROUP <id> [text]` | 设/清名称/符号/分组 |
| `EDIT <id> band\|code <v>` | 改原始频段/4B payload |
| `STATE` | 打印完整 JSON 状态快照 |
| `WEBTEST` | 设备内 GET `/api/hello`、`/api/events`,POST `/api/cmd` |

状态页是 5 行 × 12 字符:标题/频段/码/结果/`F A S` 计数。`LONG` 期间 `0xFE` 心跳
会让 `A` 增长,这正好是模块在长发状态的证据。

## 一次完整流程

```text
# RF-Lab fw=0.7.1 (ESP32-C3 + YS-RF34DL)
# oled 72x40 @0x3C i2c400000 sda=GPIO5 scl=GPIO6: ok
# net mode=ap ap=RF-Lab-1B59 pass=rflab1234 ap_ip=192.168.4.1
# ble name=RF-Lab-1B59 pin=**** (serial PIN; also OLED 5s)
# web http://192.168.4.1/
# stored 3 code(s)                              ← NVS 里的码
LEARN id=4 band=315M code=5555034C               ← 按一下原遥控器
OK tx id=4 band=315M code=5555034C (0xF0, 852ms) ← SEND 4
OK long id=4 … (0xFE, 170ms)                     ← LONG 4;心跳 ~25Hz
OK stop id=4 … (0xFF, 23ms)                      ← STOP 4
```

## 上机结果(2026-11-15)

### 0.7.1 码库 v2

| 项 | 结果 |
|---|---|
| 构建 | Flash 1,098,608B / RAM 73,540B;gzip 30,068B;ui 85a1a9b4 |
| v1→v2 迁移 | 9 条旧码 OTA 后 `LIST` 逐字节一致;`INFO` `codebook ver=2 … migrated=yes`;重启 `migrated=no` |
| 元数据 | HTTP UTF-8 `客厅大门`/`家门`/`🚪`;`/api/codes`、`LIST LONG`、Web 同步;重启保留 |
| 新建/编辑/删除 | 真机 UI 测试码新建、改 band/code、删除;测试后恢复 9 条 |
| 回归 | `LIST` 不变;`SEND 1` F0 814ms |
| 证据 | [`../docs/evidence/2026-11-21-codebook.md`](../docs/evidence/2026-11-21-codebook.md) |

### 0.6.0 跨平台 UI

| 项 | 结果 |
|---|---|
| 构建 | Flash 1,067,804B / RAM 55,732B;2MB 分区保护通过(2026-11-16 UI refresh) |
| 设备内 HTTP | `/api/hello` 200、`/api/state` 200、`/api/events` 200、POST `/api/cmd` 202 |
| 命令回归 | `SEND` 851ms / `LONG` 224ms / `STOP` 51ms |
| BLE | `INFO` `adv=yes`;`BLE OFF/ON` 可停/启广播 |
| 自检 | `tools/device_selftest.py --tx` **18/18** |
| Wi-Fi 回退 | 有凭据 STA-only 启动(AP 关)→15s 超时 AP 回退;15/15 |
| Wi-Fi LAN | ✅ 真实 `SSID-A`→192.168.5.117;ping/HTTP/SSE/POST/浏览器 200;重启 `ap_on=0` |
| 证据 | [`../docs/evidence/2026-11-15-ui.md`](../docs/evidence/2026-11-15-ui.md) · [`../docs/evidence/2026-11-15-wifi.md`](../docs/evidence/2026-11-15-wifi.md) |

### 0.5.0 射频/OLED

| 项 | 结果 |
|---|---|
| 模块在线 | `MODE` → `F2`/`F3`(~0.1s);`BAUD` → `F1`(~0.1s) |
| 学码 | 3 个键 → `5555034C` / `5555033C` / `5555034B`,均 315M |
| NVS | 重烧后 `LIST` 仍有码 |
| 发射 | 15/15 `SEND` 收到 `F0`(**~0.85s**) |
| 空中链路 | 学习型遥控器学到 `5555034B`,再被模块解回同码 |
| 长发/停止 | **不是无应答**:`LONG` 后 `0xFE` ~25Hz;`STOP` 后 `0xFF`,见 `evidence/2026-11-15-oled.md` |
| OLED | 0x3C/400kHz,几何目视正确;学码/发射/长发/停止页随事件更新 |

细节:[`../docs/evidence/2026-11-15-bringup.md`](../docs/evidence/2026-11-15-bringup.md)、
[`../docs/evidence/2026-11-15-oled.md`](../docs/evidence/2026-11-15-oled.md)。

## 未验证

> 完整登记、优先级与 0.7.2 退出条件见 [`../docs/02-plan.md`](../docs/02-plan.md) §3;
> 本清单只保留固件视角。

1. 长发时**原接收器**是否真的持续动作:已有 `0xFE` 心跳,但没看接收器。
2. 距离:室内 15–50m 未测。
3. 真断电(拔 USB)后的保持;目前只验证了跨重烧复位。
4. 无屏(0x3C 不应答)的实机降级路径:屏焊死,未拆;代码路径与主机渲染有,未上机。
5. 两条 `433M` 学码记录(与 315M 码值相同)未解释,见 `evidence/2026-11-15-oled.md` §5;
   2026-11-22 设备上已增至 4 条同族幻码(`00000093/94/95/53`)。
6. 教学期间一次 `stray` 累积 72 字节,未复现(与长发 `0xFE/0xFF` 不是同一现象)。
7. 外部浏览器经 AP:本机无 Wi-Fi 适配器,只做了设备内 HTTP/SSE 自测。
8. BLE `SEND` 未验;连接/鉴权/码表已验(2026-11-21)。
9. uConsole 真机 1280×720 未验收;Android 真机安装/自更新已验(2026-11-22)。
10. STA 接 LAN:✅ 2026-11-15 已用 `SSID-A` 验证(192.168.5.117);见 `evidence/2026-11-15-wifi-lan.md`。
11. STA 运行中掉线回退:代码路径已实现,但没有路由器控制权来触发。

## 已知缺口

1. 波特率改错要手动 `RECOVER`(尽力而为,不是原子操作)。
2. 存储上限 64 条;满了只打印 `store full`,不淘汰旧码。
3. 协议本身无校验和;UART 位错靠 `INFO` 的 `stray`/`resync` 计数暴露。
4. 无发射硬门控(旧方案 `RF_KILL` 随三模块方案一起取消)。
5. 控制台用 USB CDC:端口打开后**第一次写入前读不到历史输出**(驱动缓冲不主动刷新),主机工具需先写一个空行。
6. OLED 只有 5×7 ASCII / 12×5 字符,无中文、无按键交互;写屏同步阻塞 ~8.6ms。
7. BLE 无链路加密,4 位 PIN 只挡随手连接;AP 密码是固件宏,换密码要重编。
8. Web 的 ARM 默认关,但刷新页面/重连后回到 DISARMED;串口仍可直接发。

## 排障

| 现象 | 先查 |
|---|---|
| 串口无横幅 | 线是不是数据线;`ls /dev/ttyACM*`;先发一行再读(见已知缺口 5) |
| 有横幅但按遥控器无 `LEARN` | 模块 VCC/TXD 电压;`MON ON` 看有没有原始字节;`INFO` 看 `rx_bytes` |
| 有 `rx_bytes` 但没有帧 | 波特率不是 9600 → `BAUD 9600` / `RECOVER`;TXD/RXD 是否交叉 |
| `SEND` 出 `WARN … no 0xF0` | 正常发射的 `F0` 约 0.85s;若一直没有,查 GPIO3→RXD |
| `LONG`/`STOP` 出 `WARN … no 0xFE/0xFF` | 检查模块是否真进入长发;`MON ON` 看有没有 `FE` 心跳/`FF` |
| OLED 黑屏 | `DISP ON`;`INFO` 看 `present/err`;`DISP TEST 30`;查 0x3C、GPIO5/6 |
| Web 打不开 | `INFO` 看 `mode`;`sta` 用 LAN IP,`ap/apsta` 连 AP;`WEBTEST` 看设备内 HTTP 是否 200 |
| 配网后连不上 | 15s 后自动回 AP;浏览器在 AP 里看 `WIFI` 提示,或串口 `WIFI status` / `WIFI CLEAR` |
| BLE 扫不到 | `INFO` 看 `adv=yes`;`BLE ON`;PIN 看 OLED 5s 或串口 `PIN` |
| 启动反复重启 | 多半是旧 1MB 分区表;用 `tools/flash.sh` 写全布局 |
| 内网 curl 503 | 本机代理把内网 IP 也代理了;用 `curl --noproxy '*'` 或 `NO_PROXY=<ip>` |
| OLED 画面偏移/镜像 | 改 `src/oled_panel.h` 的列偏移/初始化行,重跑 `DISP TEST`(见 `hardware/README.md` §8) |
