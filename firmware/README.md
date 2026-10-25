# 固件(ESP32-S3 / ESP-IDF)

**状态**:阶段 1 骨架 + 不发射的射频探测;固件 **0.2.0**,协议 **VER=1**。控制通道与 `HELLO`/`CAPS`/`PING` 已在 **921600** 下验证;三模块探测仍未通过,原因指向现场 MOSI 接线。完整说明见 [docs/03-firmware.md](../docs/03-firmware.md)。

## 控制通道

| 项 | 值 |
|---|---|
| 物理通路 | CH343P USB-UART 桥(VID:PID `1a86:55d3`)→ `/dev/ttyACM0` → UART0 |
| 引脚 / 参数 | TX=GPIO43、RX=GPIO44;**921600** 8N1,无硬件流控 |
| 控制台 / 日志 | 关闭;UART0 由协议独占 |
| 复位 | DTR→IO0、RTS→EN:打开串口会复位设备一次 |

另一个 Type-C 口的原生 USB 在本板从未枚举成功,不是控制通道。

## 构建

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd firmware
pio run -e esp32s3
```

产物 `.pio/build/esp32s3/firmware.bin`。工具链:PlatformIO `espressif32` 55.03.311 + **ESP-IDF 5.5.5**;目标 N16R8(16 MB Flash,8 MB 八线 PSRAM 本批不启用)。`sdkconfig.esp32s3` 是生成物;改过 `sdkconfig.defaults` 后先 `rm -f sdkconfig.esp32s3` 再编译。

## 烧录

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd firmware
pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0
```

## 自检

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd firmware
python3 ../tools/bringup_test.py --port /dev/ttyACM0
```

脚本打开端口后按 DTR→IO0、RTS→EN 复位一次,打印启动横幅,再发 `HELLO` / `GET_CAPS` / `PING×3`;不发 `TX_REQUEST`,不碰 `RF_KILL`。`--raw` 逐帧打印,`--no-reset` 跳过复位。

**波特率**:横幅与协议帧都是 **921600**;复位后 ROM bootloader 自己的几行仍是 **115200**,用 921600 打开会看到一小段乱码,属正常。

## 启动横幅字段

| 行 | 含义 |
|---|---|
| `# RF-Lab fw=… proto=… device=…` | 固件版本、协议版本、设备 ID(如 `RFLAB-7AA8`) |
| `# carrier=CH343P->UART0 baud=921600` | 控制通道与波特率 |
| `# wire MISO 14: pd=… pu=…` | MISO 空闲电平(悬空/被拉高/被拉低) |
| `# RF_KILL=asserted … pairing:ok` | 门控处于 KILL;配对初始化结果 |
| `# probe M1 si4463 …` 等三行 | 三槽芯片 ID 与 `present` |
| `# line M1/M2 …`、`# spi M1/M2 …` | 选中后 MISO 是否有驱动、SPI 采样是否随上下拉变化 |
| `# loop MOSI13->MISO14 …` | 13–14 短接回环自检;PASS = ESP32 侧 SPI 正常 |
| `# rssi …` / `# ready` | 仅 M2 探测通过时有 RSSI;最后一行表示启动完成 |

## 当前探测结果与下一步

三槽 `present=false`(M1 `part=0xFFFF`、M2/M3 读数无效);13–14 回环 PASS,说明 ESP32 侧 SPI2/GPIO 正常,故障点在 GPIO13 之后的 MOSI 线、面包板接触或模块 pin 6。复测步骤与判读见 [docs/06-test-plan.md](../docs/06-test-plan.md)。

## 已知缺口

1. `pairing_secret` 明文存 NVS,NVS/flash 加密未开启。
2. 无 CRC/重传,UART 位错只能靠上层探活兜底。
3. `RF_KILL` 无软件切换字段,App 无法查询或释放门控。
4. 配对只下发一次 secret,丢包后无恢复路径;换绑按键(GPIO38)尚未消费。

## 文档

- [固件说明](../docs/03-firmware.md) · [硬件](../docs/02-hardware.md) · [协议](../docs/04-protocol.md) · [测试计划](../docs/06-test-plan.md) · [产品](../docs/01-product.md)
- 源码:[`src/`](src/);协议 schema:[`protocol/schemas/`](../protocol/schemas/)。
