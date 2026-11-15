# 固件(ESP32-C3 / ESP-IDF 5.5.5)

**状态**:0.4.0 · 可构建、协议层有主机单测;**未上机验证**(见文末「未验证」)。

目标板:ESP32-C3 0.42" OLED(4MB Flash,原生 USB)。板载 OLED 本版不驱动,GPIO5/6 仍按已占用处理。

## 结构

| 文件 | 职责 |
|---|---|
| [`src/rf34dl.h`](src/rf34dl.h) / [`src/rf34dl.c`](src/rf34dl.c) | **纯协议层**:8 字节帧的组帧/解帧/应答识别。不依赖 ESP-IDF,可在主机上单测 |
| [`src/main.c`](src/main.c) | 控制台(USB + UART0)、NVS 存码、模块 UART1 收发、命令分发 |
| [`test/host/test_rf34dl.c`](test/host/test_rf34dl.c) | 主机单测:真实抓包回归、重同步、ACK、fuzz |

命令表与行为规格在 [`docs/00-mvp.md`](../docs/00-mvp.md) §6(**唯一权威**,这里不重复)。

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
./tools/host_test.sh        # 69 项
SAN=1 ./tools/host_test.sh  # 加 ASan/UBSan
```

只编 `rf34dl.c` + 测试,不碰 ESP-IDF,秒级。含 **2026-11-14 真模块抓包**(16 字节两帧)的回归。

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

## 一次完整流程

```text
# RF-Lab fw=0.4.0 (ESP32-C3 + YS-RF34DL)      ← 上电横幅
# stored 0 code(s)
LEARN id=1 band=315M code=5555033C             ← 按一下原遥控器(自动入库)
OK tx id=1 band=315M code=5555033C             ← SEND 1,收到模块 F0
```

## 未验证(上机前不许说「能用」)

1. `F0` 是否真在每次发射后回、`F1/F2/F3` 是否真回。
2. 3.3V 供电下的**发射功率与距离**(解码已实测,发射没有)。
3. 长发 `FE` / 停止 `FF` 的实际效果与应答。
4. 模块在 3.3V 的长期稳定性(手册引脚定义下限 4.5V)。

## 已知缺口

1. 波特率改错要手动 `RECOVER`(尽力而为,不是原子操作)。
2. 存储上限 32 条;满了只打印 `store full`,不淘汰旧码。
3. 协议本身无校验和;UART 位错靠 `INFO` 的 `stray`/`resync` 计数暴露。
4. 无发射硬门控(旧方案 `RF_KILL` 随三模块方案一起取消)。

## 排障

| 现象 | 先查 |
|---|---|
| 串口无横幅 | 线是不是数据线;`ls /dev/ttyACM*`;`dmesg \| tail` |
| 有横幅但按遥控器无 `LEARN` | 模块 VCC/TXD 电压;`MON ON` 看有没有原始字节;`INFO` 看 `rx_bytes` |
| 有 `rx_bytes` 但没有帧 | 波特率不是 9600 → `BAUD 9600` / `RECOVER`;TXD/RXD 是否交叉 |
| `SEND` 只出 `WARN … no F0` | 模块收不到命令:查 GPIO3→RXD;或该模块本来不回 F0(待实测) |
