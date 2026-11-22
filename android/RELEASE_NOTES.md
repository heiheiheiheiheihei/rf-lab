# RF-Lab 0.7.2 — 协议去重 / 容量 64 / APP 码库修复

本次 APP 与固件同为 **0.7.2**。APP 内置页面仍与固件同源；设备固件走 OTA/串口，
APP 走自更新，两条链路互不影响。

> **RF 边界**：0.7.2 是代码/兼容版本，`SEND/LONG/STOP` 本轮**没有**复测，
> RF-01/02 仍未关闭；不要据此宣称空口发射已通过。

## 新功能 / 修复

- **4B 码字段按模块协议解析**：`地址2 / 地址1 / 按键 / osc`。
  身份 = `band + 地址2 + 地址1 + 按键`，**osc 不参与去重**；同键不同 osc 不再新增记录。
- **`DEDUP`**：合并历史重复（同身份、只差 osc）；设备上实测 32 → 21 条。
- **越界帧过滤**：地址/按键为 0 或 osc 不在 `0x30..0xF0` 的帧不学习，计入 `bad`；
  这解释了 2026-11-22 的 `433M 00000093/94/95/53` 一族。
- **容量 32 → 64**：旧 32 条 blob 自动兼容读取；NVS 不动。128/96 因静态 RAM 不足
  会在设备上失败，64 是当前架构的实际上限（`heap free ≈ 34 KB`）。
- **APP 码库空白修复**：BLE `codes` 信封漏了结尾 `}`，APP 解析失败。
  信封改由主机测试过的 `link_codes_wrap()` 统一生成；SSE 与 BLE 共用。
- **串口网络日志修复**：`# net LAN OK 192.168.5.125` 不再被 OLED 的 12 列缓冲截成 `.12`。
- **CI 守门**：push/PR 跑主机（含 ASan/UBSan）+ 7.1.3 固件构建 + 静态检查；
  Release 同时附 `firmware.bin` 与 `SHA256SUMS.txt`。
- **上机自检**：`device_selftest.py` 版本无关，支持 `--json` / `--tx-repeat`。

## 兼容

| APP | 设备固件 | 行为 |
|---|---|---|
| 0.7.2 | 0.7.2 | 全功能；码库上限 64 |
| 0.7.1 | 0.7.2 | 可显示/编辑码库；容量按 `store.max` 显示；无 `DEDUP` 按钮 |
| 0.7.2 | 0.7.1 | 可显示/编辑；码库上限仍 32；`DEDUP` 不可用 |
| 0.6.x | 0.7.2 | 无码库编辑，提示升级 |

- 下行信封 `t/seq/items` 是兼容契约：字段只增不减，容量是数据不是 schema；
  旧 APP 不因固件升级而失效。
- 页面是构建时打入 APK 的 `assets/index.html`，不会随设备固件自动更新。

## 下载

| 文件 | 说明 |
|---|---|
| `rf-lab-android.apk` | 稳定文件名，官网按钮指向 `releases/latest/download/rf-lab-android.apk` |
| `rf-lab-0.7.2.apk` | 版本归档副本 |
| `rf-lab-firmware.bin` | 设备固件（ESP32-C3，`fw=0.7.2`），用于 OTA/手动上传 |
| `SHA256SUMS.txt` | APK + 固件的 SHA-256（APP 自更新读 APK 行） |

要求 Android 8.0（API 26）及以上；APK 用 release 密钥签名（v2）。证书 SHA-256 同 0.7.1。

## 已验证 / 未验证

| 项 | 状态 |
|---|---|
| `assembleRelease` + 签名 | 由 tag CI 构建（versionCode 4 / versionName 0.7.2） |
| 固件构建 | ✅ 主机 + CI（espressif32 7.1.3） |
| 设备上机 | ✅ 0.7.2 tag 构建全量烧录；码库 21/64；`device_selftest` 26/0/1 |
| 空口 `SEND/LONG/STOP` | ⏳ 未复测（RF-01/02 仍开放） |
| 64 条满库 BLE 快照 | ⏳ 未压满 |

## 历史

- 0.7.1：码库 v2（名称/符号/分组/原始参数）、BLE 下行 framing。
- 0.7.0：APP 自更新。
