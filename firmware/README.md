# 固件(ESP32-C3 / ESP-IDF 5.5.5)

**状态**:0.5.0 · 可构建 · 协议/OLED 有主机单测 · **2026-11-15 已上机**(见文末「上机结果」)。

目标板:ESP32-C3 0.42" OLED(4MB Flash,原生 USB)。板载 SSD1306 72×40 由本版驱动,
只做状态面板;命令与行为规格在 [`docs/00-mvp.md`](../docs/00-mvp.md) §5–6(**唯一权威**,这里不重复)。

## 结构

| 文件 | 职责 |
|---|---|
| [`src/rf34dl.h`](src/rf34dl.h) / [`src/rf34dl.c`](src/rf34dl.c) | **纯协议层**:8 字节帧组帧/解帧、`F0/F1/F2/F3/FE/FF` 应答识别。不依赖 ESP-IDF |
| [`src/oled_panel.h`](src/oled_panel.h) | 72×40 面板参数(列偏移 28、MUX/COM/IREF、初始化序列),几何的唯一数据源 |
| [`src/oled_gfx.h`](src/oled_gfx.h) / [`src/oled_gfx.c`](src/oled_gfx.c) | **纯帧缓冲**:360B 页式显存、画点/线/框、5×7 ASCII、ASCII dump |
| [`src/font5x7.h`](src/font5x7.h) | 5×7 字库(Adafruit GFX 子集,BSD 声明在文件头) |
| [`src/ui.h`](src/ui.h) / [`src/ui.c`](src/ui.c) | **纯状态映射**:事件 → 5 行 × 12 字符;测试图案 |
| [`src/oled.h`](src/oled.h) / [`src/oled.c`](src/oled.c) | 仅 I2C/SSD1306:probe、初始化、整帧写屏、开关屏、耗时统计 |
| [`src/main.c`](src/main.c) | 控制台、NVS、模块 UART1、命令分发、OLED 调度 |
| [`test/host/test_rf34dl.c`](test/host/test_rf34dl.c) | 协议单测:真实抓包回归、重同步、ACK(含 FE/FF)、fuzz |
| [`test/host/test_ui.c`](test/host/test_ui.c) | 帧缓冲/字库/状态页/面板几何单测 |

## 构建

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd firmware
pio run -e esp32c3
```

产物 `.pio/build/esp32c3/firmware.bin`。改了 `sdkconfig.defaults` 先 `rm -f sdkconfig.esp32c3` 再编(它是生成物)。

首次为 C3 构建会下载 **RISC-V 工具链**(`toolchain-riscv32-esp`,约 200MB);本机已完成。

## 主机单测(不接硬件)

```bash
./tools/host_test.sh        # 协议 76 项 + OLED/UI 1286 项
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

状态页是 5 行 × 12 字符:标题/频段/码/结果/`F A S` 计数。`LONG` 期间 `0xFE` 心跳
会让 `A` 增长,这正好是模块在长发状态的证据。

## 一次完整流程

```text
# RF-Lab fw=0.5.0 (ESP32-C3 + YS-RF34DL)
# oled 72x40 @0x3C i2c400000 sda=GPIO5 scl=GPIO6: ok
# stored 3 code(s)                              ← NVS 里的码
LEARN id=4 band=315M code=5555034C               ← 按一下原遥控器
OK tx id=4 band=315M code=5555034C (0xF0, 852ms) ← SEND 4
OK long id=4 … (0xFE, 170ms)                     ← LONG 4;心跳 ~25Hz
OK stop id=4 … (0xFF, 23ms)                      ← STOP 4
```

## 上机结果(2026-11-15)

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

1. 长发时**原接收器**是否真的持续动作:已有 `0xFE` 心跳,但没看接收器。
2. 距离:室内 15–50m 未测。
3. 真断电(拔 USB)后的保持;目前只验证了跨重烧复位。
4. 无屏(0x3C 不应答)的实机降级路径:屏焊死,未拆;代码路径与主机渲染有,未上机。
5. 两条 `433M` 学码记录(与 315M 码值相同)未解释,见 `evidence/2026-11-15-oled.md` §5。
6. 教学期间一次 `stray` 累积 72 字节,未复现(与长发 `0xFE/0xFF` 不是同一现象)。

## 已知缺口

1. 波特率改错要手动 `RECOVER`(尽力而为,不是原子操作)。
2. 存储上限 32 条;满了只打印 `store full`,不淘汰旧码。
3. 协议本身无校验和;UART 位错靠 `INFO` 的 `stray`/`resync` 计数暴露。
4. 无发射硬门控(旧方案 `RF_KILL` 随三模块方案一起取消)。
5. 控制台用 USB CDC:端口打开后**第一次写入前读不到历史输出**(驱动缓冲不主动刷新),主机工具需先写一个空行。
6. OLED 只有 5×7 ASCII / 12×5 字符,无中文、无按键交互;写屏同步阻塞 ~8.6ms。

## 排障

| 现象 | 先查 |
|---|---|
| 串口无横幅 | 线是不是数据线;`ls /dev/ttyACM*`;先发一行再读(见已知缺口 5) |
| 有横幅但按遥控器无 `LEARN` | 模块 VCC/TXD 电压;`MON ON` 看有没有原始字节;`INFO` 看 `rx_bytes` |
| 有 `rx_bytes` 但没有帧 | 波特率不是 9600 → `BAUD 9600` / `RECOVER`;TXD/RXD 是否交叉 |
| `SEND` 出 `WARN … no 0xF0` | 正常发射的 `F0` 约 0.85s;若一直没有,查 GPIO3→RXD |
| `LONG`/`STOP` 出 `WARN … no 0xFE/0xFF` | 检查模块是否真进入长发;`MON ON` 看有没有 `FE` 心跳/`FF` |
| OLED 黑屏 | `DISP ON`;`INFO` 看 `present/err`;`DISP TEST 30`;查 0x3C、GPIO5/6 |
| OLED 画面偏移/镜像 | 改 `src/oled_panel.h` 的列偏移/初始化行,重跑 `DISP TEST`(见 `hardware/README.md` §8) |
