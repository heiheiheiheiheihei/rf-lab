# RF-Lab Android 0.7.1 — 码库：名称 / 符号 / 分组 / 原始参数

本次 APP 与固件同为 **0.7.1**。APP 内置页面换成码库 v2；设备端仍需 OTA 0.7.1，
否则 APP 会禁用编辑并提示升级（`hello.codebook` 能力位）。

## 新功能

- **名称 / 符号**：每条码可设 UTF-8 名称（≤24B，约 8 个汉字）与符号/emoji（≤8B）。
- **单分组**：每条码一个分组（≤16B）；码库顶部按分组筛选，未分组单独一档。
- **搜索**：按名称 / hex / 分组 / id 过滤。
- **新建自定义码**：`+ NEW` 指定 band 315/433 + 8 位 hex，可直接命名/分组。
- **编辑原始参数**：`EDIT` 改 band 与 4B payload；弹层显示 `FD F3/F4 03 … DF` 原始帧预览，
  重复码/非法 hex 拒绝，学习码改 band/code 需确认。
- **删除**：行内两次确认或编辑弹层删除；`ERASE` 后 id 前移是既有行为。

> 设备固件 0.7.1 同时修复 BLE 下行 `state/codes` 漏 `\n` 的 framing 问题；APP 端不新增解析逻辑，
> 但仍需设备 OTA 到 0.7.1 才能在 BLE 下稳定收到码库快照。

## 兼容

| APP | 设备固件 | 行为 |
|---|---|---|
| 0.7.1 | 0.7.1 | 全功能 |
| 0.7.1 | 0.6.x | 码库编辑禁用，提示先 OTA 设备 |
| 0.7.0 | 0.7.1 | 旧页面可用，忽略新字段，无编辑器 |

- 页面是构建时打入 APK 的 `assets/index.html`，**不会随设备固件自动更新**；
  APP 自更新只更新 APP，设备固件仍要走 OTA/串口。
- APP 版本 `versionCode 3` / `versionName 0.7.1`；签名与 0.7.0 相同（release 证书），
  可由 0.7.0 覆盖安装或自更新。

## 下载

| 文件 | 说明 |
|---|---|
| `rf-lab-android.apk` | 稳定文件名，官网按钮指向 `releases/latest/download/rf-lab-android.apk` |
| `rf-lab-0.7.1.apk` | 版本归档副本 |
| `SHA256SUMS.txt` | 上述 APK 的 SHA-256（APP 自更新也读这个文件） |

要求 **Android 8.0（API 26）及以上**。APK 使用本项目的 release 密钥签名（APK Signature Scheme v2），证书 SHA-256：

```
B8:CC:EA:6C:1B:2F:BA:85:BD:BF:59:DA:DA:0D:21:BF:98:7F:60:DD:97:70:3C:0C:88:21:BB:63:D8:AB:0D:EA
```

## 已验证 / 未验证

| 项 | 状态 |
|---|---|
| `assembleRelease` + 签名 | ✅ 2026-11-21,1,050,176B,versionCode 3,v2 签名,`apksigner verify` 通过 |
| Manifest/权限/包名 | ✅ `aapt2 dump badging`: `dev.rflab.terminal` 3/0.7.1,minSdk 26,targetSdk 35 |
| 页面资源 | ✅ 与固件同一次 `build_web.py` 产出;`assets/index.html` 与 `web/dist` 逐字节一致 |
| 码库 UI | ✅ 主机 Chromium 1440/390;真机浏览器同页;✅ 真机 BLE `NAME/GROUP/SYM/EDIT` 写入(2026-11-22) |
| 真机安装 / 自更新 | ✅ 2026-11-22:0.7.1 安装、0.7.0→0.7.1 弹窗/下载/系统安装器/覆盖安装成功(用户确认);⏳ BLE `SEND` 未单独验 |

> 0.7.0（release 签名）可直接覆盖安装到 0.7.1；0.6.0 没有自更新，需手动装一次。

## 历史

- **0.7.0**：APP 自更新（查 GitHub Release、一键/忽略、SHA-256/包名/版本/签名自检、系统安装器）。
