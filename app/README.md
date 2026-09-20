# 手机 App(占位)

**本目录当前是占位,不含任何实现。** 设计阶段只产出设计文档。

- App 设计见 [`../docs/40-app/010-mobile-app-design.md`](../docs/40-app/010-mobile-app-design.md)
- AI 链路见 [`../docs/50-ai/010-deepseek-analysis.md`](../docs/50-ai/010-deepseek-analysis.md)
- 协议契约见 [`../docs/30-protocol/010-device-app-protocol.md`](../docs/30-protocol/010-device-app-protocol.md)

## 关键决策待办

载体选型(PWA / Flutter / RN)受一个硬事实影响:**iOS 全平台不支持 Web Bluetooth**。
若必须覆盖 iPhone 的 BLE 近距控制,则纯 PWA 不可行。详见 App 设计文档的载体选型章节。

**本机环境**:Flutter/Dart 未安装;Node v22 + npm 已装。

## 安全红线

- DeepSeek API Key **只存于 App**,绝不写入设备或本仓库
- 发射必须经用户二次确认 + 短时 HMAC 令牌,禁止自动发射
