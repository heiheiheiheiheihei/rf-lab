# 顶层架构

状态:`draft` · 最后更新:2026-09-20 · 上游依据:`010-product-definition.md`、`docs/30-protocol/010-device-app-protocol.md`、`docs/adr/0001-frequency-coverage-gap.md`、`docs/adr/0002-ai-analysis-in-app.md`

## 1. 系统全景

本产品由**三层**构成:设备(射频端)、手机 App(控制与智能端)、DeepSeek(分析服务)。

```
┌───────────────────────────────────────────────────────────────────────┐
│                         设备 RF-Lab Device                             │
│                      ESP32-S3N16R8 (16MB/8MB PSRAM)                    │
│                                                                        │
│   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐              │
│   │ 槽位1 SI4463 │   │ 槽位2 CC1101 │   │ 槽位3 nRF24  │              │
│   │ 独立SPI + SMA│   │ 独立SPI + SMA│   │ 独立SPI + SMA│              │
│   └──────┬───────┘   └──────┬───────┘   └──────┬───────┘              │
│          │                  │                  │                       │
│          └──────────────────┼──────────────────┘                       │
│                        GPIO / SPI 矩阵                                 │
│                             │                                          │
│   ┌─────────────────────────┴──────────────────────────┐              │
│   │              固件 (ESP-IDF / FreeRTOS)              │              │
│   │  rf_hal → rf_driver_* → rf_scan / rf_capture / rf_tx│              │
│   │  features 提取 · proto 编解码 · audit 审计          │              │
│   └───────────┬─────────────────────────┬──────────────┘              │
│               │                         │                              │
│         WebSocket (Wi-Fi)          GATT (BLE)                          │
└───────────────┼─────────────────────────┼──────────────────────────────┘
                │                         │
        ┌───────▼─────────────────────────▼───────┐
        │           手机 App (控制 + 智能)         │
        │  · 连接管理(WS / BLE 自动择优)         │
        │  · 扫频/捕获/发射 控制 UI                │
        │  · 本地信号库 · 审计镜像                 │
        │  · 特征去重 · 提示词组装 · 结果解析      │
        └───────────────────┬─────────────────────┘
                            │ HTTPS (仅特征,不传原始码流)
                    ┌───────▼────────┐
                    │    DeepSeek    │
                    │  信号类型/协议  │
                    │  /威胁等级     │
                    └───────┬────────┘
                            │ 结构化 JSON 建议
                            ▼
                    返回 App → 用户确认 → 令牌 → 设备发射
```

## 2. 三个关键架构决策

| 决策 | 内容 | ADR |
|---|---|---|
| **D1** | AI 分析在 App 侧,设备不持有 API Key,只回传特征 | [ADR-0002](../adr/0002-ai-analysis-in-app.md) |
| **D2** | 扫频范围以模块真实能力为准,超范围必须如实裁剪并告知,禁止静默跳过 | [ADR-0001](../adr/0001-frequency-coverage-gap.md) |
| **D3** | 发射默认拒绝:白名单出厂为空,需短时令牌 + 用户二次确认,全量审计 | 协议契约 §4/§7 |

## 3. 数据流

### 3.1 扫频与捕获(上行)

```
App  START_SCAN{f_start,f_stop,step,dwell,threshold}
  └─► 设备 校验频率范围 → 夹取到模块真实 bands(超界回 WARN{band_clipped})
        └─► rf_scan:逐点调谐 + 读 RSSI
              └─► SCAN_HIT{f,rssi,t}   (仅超阈值,限流 50 msg/s)
                    └─► SCAN_DONE{hits, coverage:[真实区间]}

App  CAPTURE_START{f, mod, max_edges, trigger}
  └─► 设备 RMT 捕获跳变序列
        ├─► DATA_CHUNK (二进制 RLE:dt_us u16 + level u8)
        └─► CAPTURE_DONE{edges, features:{...}}
```

### 3.2 智能分析(旁路,不经设备)

```
App 收到 features
  └─► 规范化哈希 → 命中缓存? → 是则复用
        └─► 否 → 组装提示词 → HTTPS → DeepSeek
              └─► 严格 JSON:{signal_type, protocol_guess, confidence,
                            threat_level, threat_reason,
                            matched_protocols[], recommended_actions[], caveats[]}
                    └─► 校验失败 → 纠错重试 1 次 → 仍失败降级为纯文本展示
                          └─► App 展示 + 存入本地分析历史
```

### 3.3 发射(下行,受控)

```
用户看到 AI 建议 → 人工确认(不可跳过)
  └─► App 计算 HMAC 令牌(绑定 seq/f/mod/power,**无时间窗**)
        └─► TX_REQUEST{f, mod, payload_rle, repeats, power_dbm, auth_token}
              └─► 设备四道门禁:①白名单 ②功率上限 ③令牌 ④模块在位
                    ├─ 任一失败 → TX_STATUS{state:"denied"} + 审计记录
                    └─ 全通过 → TX_STATUS{state:"sent", granted_power_dbm} + 审计记录
```

## 4. 职责边界(谁拥有什么)

| 能力 | 设备 | App | DeepSeek |
|---|---|---|---|
| 频率调谐 / RSSI 读取 | ✅ 唯一拥有 | ❌ | ❌ |
| 原始时序捕获 | ✅ 唯一拥有 | ❌ | ❌ |
| 特征提取 | ✅ 唯一拥有 | ❌ | ❌ |
| 扫频范围决策 | ⚠️ 校验与夹取 | ✅ 发起 | ❌ |
| 协议/威胁判定 | ❌ | ⚠️ 展示与缓存 | ✅ 唯一拥有 |
| API Key | ❌ 绝不持有 | ✅ 唯一持有 | — |
| 发射门禁 | ✅ 最终强制 | ⚠️ 计算令牌 | ❌ |
| 审计日志 | ✅ 主记录 | ⚠️ 镜像 | ❌ |
| 信号库 | ✅ 设备本地 | ✅ App 本地 | ❌ |

> 「唯一拥有」表示该能力只有一处实现,不存在第二份。这是防止逻辑分叉的硬约束。

## 5. 通道选择策略

| 场景 | 通道 | 理由 |
|---|---|---|
| 首次配网 | BLE | 不依赖现场网络,免明文广播密码 |
| 近距离调试 | BLE | 免配网,即开即用 |
| 远程操作(设备在车内/背包) | Wi-Fi WebSocket | 距离远、吞吐高,适合持续扫频数据流 |
| 大数据量捕获 | Wi-Fi | BLE 需分片且限流,吞吐不足 |

App 应**自动择优**:同时可达时,近距离优先 BLE;需要高吞吐(连续扫频、长捕获)时优先 Wi-Fi。

## 6. 故障与降级

| 故障 | 行为 |
|---|---|
| 设备 Wi-Fi 断连 | BLE 仍可用(若在范围);App 提示通道降级 |
| 设备掉电重启 | 恢复后回 `IDLE`,需重新 `HELLO`;进行中的捕获丢失(不持久化) |
| 无外网 | AI 分析降级为本地规则库,明确标注低置信度 |
| DeepSeek 超时/报错 | 重试 1 次,仍失败则展示特征原文并提示稍后重试 |
| 模块未插好 | `MODULES` 报 `present:false`;`SELECT_MODULE` 回 `module_absent` |
| 固件看门狗触发 | 射频强制进 idle,回滚到 `READY`,记录复位原因 |

## 7. 非目标(架构级)

- ❌ 无云端服务端,不做设备注册/账号体系/远程穿透
- ❌ 不做设备间互联或中继
- ❌ 设备不持久化 AI 结论(结论属于 App)
- ❌ 不做原始 IQ 采样与上传

## 8. 文档地图

| 主题 | 文档 |
|---|---|
| 产品定义与需求 | `docs/00-overview/010-product-definition.md` |
| 术语表 | `docs/00-overview/030-glossary.md` |
| 顶层架构(本文) | `docs/00-overview/020-architecture.md` |
| 硬件架构与 BOM | `docs/10-hardware/010-system-architecture.md` |
| 固件架构 | `docs/20-firmware/010-firmware-architecture.md` |
| 协议契约 | `docs/30-protocol/010-device-app-protocol.md` |
| 手机 App | `docs/40-app/010-mobile-app-design.md` |
| DeepSeek 分析 | `docs/50-ai/010-deepseek-analysis.md` |
| 频谱合规 | `docs/60-compliance/010-spectrum-compliance.md` |
| 验证计划 | `docs/70-test/010-verification-plan.md` |
| 决策记录 | `docs/adr/` |
