# 2026-11-22 — RF-Lab Watch release 签名与安装

> 目标:手表使用**独立 release key**(不复用手机 `dev.rflab.terminal` 的 key),
> 构建 v2 签名 APK,替换 debug 包并做一次真机回归。

## Key

| 项 | 值 |
|---|---|
| keystore | `~/.local/share/rf-lab/watch/rf-lab-watch-release.jks`(仓库外,600) |
| 口令文件 | `~/.config/rf-lab/watch-keystore.env`(仓库外,600) |
| alias | `rflab-watch` |
| 算法 | RSA 4096 / PKCS12 / SHA384withRSA / 10000 天 |
| 证书 SHA-256 | `ED:C2:A5:C1:0E:68:C3:35:A9:D7:7B:BD:44:C6:C1:34:18:A8:08:7B:EE:DD:D8:47:BE:05:51:3B:A4:90:94:A7` |
| 与手机 key | **不同**:手机 release key 仍只签 `dev.rflab.terminal` |

`watch/keystore.properties` 为本地构建入口,已 gitignore(600);CI 用
`RFLAB_WATCH_KEYSTORE_PATH` / `RFLAB_WATCH_KEYSTORE_PASSWORD` /
`RFLAB_WATCH_KEY_ALIAS` / `RFLAB_WATCH_KEY_PASSWORD`。仓库内没有密钥/口令。

## Build / Sign

```bash
set -a; . "$HOME/.config/rf-lab/watch-keystore.env"; set +a
cd watch && ../android/gradlew assembleRelease
"$ANDROID_HOME/build-tools/35.0.0/apksigner" verify --print-certs \
  app/build/outputs/apk/release/app-release.apk
```

| 项 | 值 |
|---|---|
| 产物 | `watch/app/build/outputs/apk/release/app-release.apk` |
| 大小 | 48,244 B |
| sha256 | `4bd74ccae418342163d6fe4a0fb23d90dab07e35b5df377786d7fad9b6bb7e26` |
| 包 / 版本 | `dev.rflab.watch` 0.1.0(versionCode 1) |
| 签名 | v1 false / **v2 true** |
| debuggable | 无(`aapt2 dump badging` 无 `application-debuggable`) |

## Install(debug → release)

```text
adb uninstall dev.rflab.watch          → Success
adb install app-release.apk             → Success
adb shell pm grant dev.rflab.watch android.permission.ACCESS_FINE_LOCATION
```

签名不同,**必须卸载 debug**;手表上的 PIN/设备/选中码被清空,首装后重新配对。
`firstInstallTime=2026-11-22 15:57:31`,release 包已安装在 OW20W1。

## Release 真机回归

| 项 | 结果 | 截图 |
|---|---|---|
| 扫描 | 找到 `RF-Lab-1B59` -52 dBm | [control](2026-11-22-watch-release/control.png) |
| PIN / 码库 | 4 位 PIN 后 `ONLINE`,32 条码 | 同上 |
| ARM / SEND | `OK TX #1 · 693ms` | [send-ok](2026-11-22-watch-release/send-ok.png) |
| HOLD | `HOLDING #1 · RELEASE TO STOP`,SEND `BUSY` | [hold](2026-11-22-watch-release/hold.png) |
| 松开 | `STOP #1`,回到 SEND/HOLD | [stop](2026-11-22-watch-release/stop.png) |

## 官网 / 项目介绍

- `README.md` 增加「手腕上的发射键」、四入口表与 0.1.0 真机边界;
- `site/index.html` 新增 `04 手表` 区:真机截图、LINK→PIN→CONTROL、HOLD/LONG/STOP、
  独立 release key、公开 APK 下载入口;导航/状态板/页脚/`W` 键同步;
- 站点新增 `assets/watch-control.webp` / `watch-hold.webp`(由 46mm 真机截图转 WebP)。

```text
python3 tools/site_check.py
SITE OK
ids 62 unique / 62 total
images 5 (local, alt present)
html 43,620 B gzip 13,751 B
```

本地 Chromium 1440×900 / 1024×800 / 390×844 无横向溢出
(`scrollWidth <= innerWidth`);手表区桌面/手机截图:

- [site-watch-desktop](2026-11-22-watch-release/site-watch-desktop.png)
- [site-watch-mobile](2026-11-22-watch-release/site-watch-mobile.png)

**已部署(2026-11-22)**:commit `e359992`;CI
[45001726458](https://github.com/heiheiheiheiheihei/rf-lab/actions/runs/45001726458) 绿,
Pages [45001726431](https://github.com/heiheiheiheiheihei/rf-lab/actions/runs/45001726431) 成功。
线上 `https://heiheiheiheiheihei.github.io/rf-lab/` 返回 200,页面含 `id="watch"`,
`assets/watch-control.webp` / `watch-hold.webp` 均 200。

## 公开 Release

| 项 | 值 |
|---|---|
| tag | [`watch-v0.1.0`](https://github.com/heiheiheiheiheihei/rf-lab/releases/tag/watch-v0.1.0) |
| 资产 | `rf-lab-watch.apk` / `rf-lab-watch-0.1.0.apk` / `SHA256SUMS.txt` |
| 下载 | <https://github.com/heiheiheiheiheihei/rf-lab/releases/download/watch-v0.1.0/rf-lab-watch.apk> |
| 构建 | `.github/workflows/watch-release.yml`(tag 触发,独立 watch key secrets) |
| latest | **明确 false**:不抢手机 APK 的 `releases/latest` |
| 安装 | `adb install -r rf-lab-watch.apk`;debug → release 仍需卸载 debug |

公开包只覆盖 0.1.0 的 46mm 控制路径;码库编辑/OTA 不在范围内。安装包 SHA-256
以 Release 的 `SHA256SUMS.txt` 为准。
