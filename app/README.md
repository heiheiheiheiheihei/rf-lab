# 手机 App(占位,本批不实现)

本目录当前只有说明,不含实现。App 侧设计(载体、连接/配对、UI、AI 链路)见:

- [`../docs/05-app-ai.md`](../docs/05-app-ai.md) — App + DeepSeek 分析
- [`../docs/04-protocol.md`](../docs/04-protocol.md) — 设备 ↔ App 协议契约

## 已定结论

- **仅 Android**,载体为 PWA;**WebUSB 基线 + `web-serial-polyfill`**(不是 Web Serial 基线)。
- **本批唯一控制通道是 USB(CH343P,VID:PID = `1a86:55d3`)**;BLE / Wi-Fi 已移出本批(能力定义保留)。
- 首次配对只走 USB,不需要一次性 `pair_code`。

## 安全红线

- DeepSeek API Key **只存于 App**,绝不写入设备或本仓库。
- 发射必须经用户二次确认 + 短时 HMAC 令牌,禁止自动发射。
