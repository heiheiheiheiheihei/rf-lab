# RF-Lab · 315/433 固定码遥控 学习 / 复制 / 发射

两块板:**ESP32-C3 OLED** + **YS-RF34DL** 315/433 编解码模块。
按一下原遥控器 → 模块解码 → ESP32 存码 → 串口命令发射 → 原接收器动作。

- **唯一规格**:[`docs/00-mvp.md`](docs/00-mvp.md)
- **接线**:[`hardware/README.md`](hardware/README.md)(4 根线;引脚权威)
- **固件**:[`firmware/`](firmware/README.md)(ESP-IDF 5.5.5 / ESP32-C3)
- **主机 UI**:[`tools/rf34dl.py`](tools/rf34dl.py)(没有 OLED/按键,串口就是 UI)

## 快速开始

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"

# 1) 构建 + 烧录
cd firmware && pio run -e esp32c3 -t upload --upload-port /dev/ttyACM0 && cd ..

# 2) 交互
python3 tools/rf34dl.py --port /dev/ttyACM0

# 3) 或一条龙:按遥控器学码 → 发射
python3 tools/rf34dl.py --port /dev/ttyACM0 learn 15
python3 tools/rf34dl.py --port /dev/ttyACM0 send 1
```

协议层可以脱离硬件单测:`./tools/host_test.sh`。

## 状态

| 项 | 状态 |
|---|---|
| 构建(ESP32-C3) | ✅ 主机 |
| 协议层单测(69 项,含真模块抓包回归) | ✅ 主机 |
| 上机:学码 / 存码 / 发射 / 改波特率 | ✅ 2026-11-15,**空中链路已被学习遥控器验证** |
| 上机:距离 / 长发是否真持续发射 | ⏳ 未测,见规格 §7 |
| 上机证据 | [`docs/evidence/2026-11-15-bringup.md`](docs/evidence/2026-11-15-bringup.md) |

## 范围

| 做 | 不做 |
|---|---|
| 315/433 固定码学习 / 解码 / 存 / 发 | 滚动码克隆 / 重放 |
| 单次发射、长发 / 停止 | 原始 OOK 波形记录 |
| 改模块波特率 / 输出模式 | 扫频 / RSSI / 频谱 |
| 自有固定码设备的复制验证 | Wi-Fi/BLE、板载 OLED 驱动(本版) |

> ⚠️ 固定码本身可被复制:测试只对**自有**设备做,结论只对固定码成立。
