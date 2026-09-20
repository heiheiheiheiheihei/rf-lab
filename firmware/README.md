# 固件(占位)

**本目录当前是占位,不含任何实现。** 设计阶段只产出设计文档。

- 框架已定:**ESP-IDF**(ESP32-S3N16R8)
- 架构设计见 [`../docs/20-firmware/010-firmware-architecture.md`](../docs/20-firmware/010-firmware-architecture.md)
- 协议契约见 [`../docs/30-protocol/010-device-app-protocol.md`](../docs/30-protocol/010-device-app-protocol.md)

## 规划的目录结构(实现阶段启用)

```
firmware/
├── CMakeLists.txt
├── sdkconfig.defaults
├── boards/                 板级定义:引脚分配、模块槽位映射
├── components/             ESP-IDF 组件(依赖单向,不得循环)
│   ├── rf_hal/             SPI / GPIO 抽象
│   ├── rf_driver_si4463/
│   ├── rf_driver_cc1101/
│   ├── rf_driver_nrf24/
│   ├── rf_capture/         RMT 时序捕获
│   ├── rf_scan/            扫频状态机
│   ├── rf_tx/              发射 + 策略门禁
│   ├── features/           特征提取(纯逻辑,可在 PC 上测)
│   ├── proto/              帧编解码 + 分片(纯逻辑,可在 PC 上测)
│   ├── transport_ws/
│   ├── transport_ble/
│   ├── store/              信号存储
│   └── audit/              发射审计
└── main/
```

## 构建(实现阶段)

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd firmware && pio run -e esp32s3
```

> 本机 PlatformIO 已装但不在 PATH;`espressif32` 平台与 `framework-espidf` 已下载。
