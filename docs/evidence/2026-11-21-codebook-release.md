# 2026-11-21 · v0.7.1 发版:固件 0.7.1 + APK 0.7.1

状态:**CI ✅ · Release ✅ · Pages ✅** · 真机 0.7.1 APK 安装/自更新未验证。

## Release

| 项 | 值 |
|---|---|
| tag | `v0.7.1`(annotated) |
| commit | `a7b4bf3` |
| Release | <https://github.com/heiheiheiheiheihei/rf-lab/releases/tag/v0.7.1> |
| CI run | [44918804371](https://github.com/heiheiheiheiheihei/rf-lab/actions/runs/44918804371),Android release APK,1m46s,success |
| Pages | [44918796317](https://github.com/heiheiheiheiheihei/rf-lab/actions/runs/44918796317),success |

## 资产(从 Release 下载后复核)

```text
rf-lab-android.apk    1,050,176 B
rf-lab-0.7.1.apk      1,050,176 B
SHA256SUMS.txt              172 B
sha256  59f763ba9d1be61bcf0b45b24987ead0a42066b57cd7a6de6cf559f314a4a2d1
```

`aapt dump badging`:`dev.rflab.terminal` **versionCode 3 / versionName 0.7.1**,
minSdk 26,targetSdk 35。

`apksigner verify`:v2 scheme **true**,v1/v3 false(与 0.7.0 同一策略)。

APK 内 `assets/index.html` sha256 `ed65b7c5…`,与仓库 `web/dist/index.html` 一致
(CI 的 “Check generated Web assets” 通过)。

> 本机 23:16 的 release 构建 sha256 为 `035c5916…`,与 CI 资产不同但**同样大小/版本**;
> 以 Release 的 `59f763ba…` 为准(CI 干净构建,本地构建带 dirty 元数据)。

## Pages

```text
https://heiheiheiheiheihei.github.io/rf-lab/                  200  45,279 B
https://heiheiheiheiheihei.github.io/rf-lab/assets/terminal.html 200 129,449 B
```

## 仍未验证

- 在真机上安装 **0.7.1 APK**(0.7.0 已装并连过 BLE;0.7.1 未装)。
- 0.7.0 → 0.7.1 自更新弹窗/下载/系统安装器。
- BLE 下码库 `NAME/GROUP/SYM/EDIT` 写入(链路/码表显示已由
  [`2026-11-21-ble-framing.md`](2026-11-21-ble-framing.md) 覆盖)。
- 官方 0.7.1 APK 与固件 0.7.1 的整机组合回归。
