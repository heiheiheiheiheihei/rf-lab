# 2026-11-22 OPPO Watch 2 自更新验证

结论:**下载 + 校验链路已在 OW20W1 真机跑通;系统安装界面未跑通**。
`PackageInstaller` 会话在该 ColorOS 上返回 `STATUS_FAILURE(-1)`,`InstallStart`
用 `file://` 也是启动后立即结束;`ACTION_VIEW` + `content://` 的 0.1.4 路径
在设备断开前未完成验证。0.1.4 因此标记为 **pre-release**,官网仍指向 0.1.0。

## 环境

| 项 | 值 |
|---|---|
| 手表 | OPPO Watch 2 46mm `OW20W1`,ColorOS Watch 2.0 / Android 8.1 (API 27),402×476 @320 |
| 网络 | 手表经手机蓝牙共享网络(`COMPANION_PROXY`,`Sysproxy Connected`) |
| 构建 | JDK 17 / SDK 35 / Gradle 8.11.1 |
| 签名 | 独立 watch key(证书 `ED:C2:A5:…:94:A7`),v2 |
| 安装包 | 本地测试 0.1.1(code 2)、0.1.3(code 4)、0.1.4(code 5) |

## 主机侧

```bash
./watch/check.sh
# BUILD SUCCESSFUL;testDebugUnitTest + lintDebug + assembleDebug
# RELEASE OK;apksigner verify --verbose -> Verifies
# HOST OK
```

新增 JVM 测试:`AppVersionTest`(tag 解析/数值比较)、`UpdateFeedTest`
(只选 `watch-v*` 正式版、必须带 APK+SUMS、`./` 前缀、大小上限)。
`docs/02-plan.md` 登记 WATCH-05。

## 真机:能查、能下、能校验

```bash
# 手表 shell 直连 GitHub API(经手机蓝牙网络)
curl -sS -m 12 https://api.github.com/zen      # 200, 3.6s
# github.com 的 release 下载地址在同一链路:
curl -L -m 25 .../releases/download/watch-v0.1.2/rf-lab-watch.apk
# curl: (28) Operation timed out ... SSL handshake timed out
```

因此下载改走 GitHub API 的 asset 端点(`Accept: application/octet-stream`):

```bash
curl -L -m 30 -H "Accept: application/octet-stream" \
  .../releases/assets/778969737
# 200 58200 15.2s
```

APP 内点击 SYSTEM → `UPDATE`:

1. 0.1.1 已安装、feed 只有 0.1.0 时显示 `UP TO DATE`;
2. 0.1.3 发布后再次点 `UPDATE`,行内进度 `0.1.3 · 0% → 100%`;
3. 校验阶段暴露真实 bug:`SHA256SUMS.txt` 由
   `sha256sum ./*.apk` 生成,文件名带 `./`,`UpdateFeed.hashFor` 精确比较失败,
   logcat:`IOException: SHA256SUMS has no rf-lab-watch.apk`。
   修复为同时接受 `name` / `./name` / `*name`,工作流改为 `sha256sum *.apk`;
4. 修复后重试:下载 100% → `INSTALL…` → `CONFIRM…`。

## 真机:安装被 ROM 挡住

`PackageInstaller` 会话路径:

```text
UPDATE -> 100% -> INSTALL… -> CONFIRM…
20:31:52 W/RF-Lab.Watch: install failed: -1 null
```

`PackageInstaller.EXTRA_STATUS = STATUS_FAILURE(-1)`,无系统安装界面;
`dumpsys package dev.rflab.watch` 仍是 `versionName=0.1.1`,未发生覆盖安装。
当时 `appops get dev.rflab.watch REQUEST_INSTALL_PACKAGES` = `allow`,
`settings get secure install_non_market_apps` = `1`。

直接 `ACTION_VIEW` 测试:

```bash
adb shell am start -a android.intent.action.VIEW \
  -t application/vnd.android.package-archive \
  -d file:///sdcard/Download/rf-lab-watch-v1.apk
# ActivityManager: START ... cmp=com.android.packageinstaller/.InstallStart
# 随后 dumpsys activity: mLastPausedActivity: .../.InstallStart t-1 f(立即结束)
# 焦点回到 launcher,无安装确认界面
```

`com.android.packageinstaller/.PackageInstallerActivity` 存在但未导出,
唯一导出的入口是 `InstallStart`;ColorOS 该 ROM 上它是秒退的。

## 0.1.4 的替代路径(待验)

0.1.3 之后改为:

- `ApkProvider`:只读、只暴露 `cache/update/*.apk`,manifest `exported=false`;
- `ACTION_VIEW` + `FLAG_GRANT_READ_URI_PERMISSION`;
- 返回 APP 时比较已装 `versionName` 与目标版本,显示 `UPDATED` / `CANCELLED`。

0.1.4 的下载/校验与 0.1.3 相同(已验);**系统安装界面在设备断开前未验证**,
因此 0.1.4 在 GitHub 上标记为 pre-release,不作为稳定版推荐。

## 当前发布状态

| 版本 | 状态 |
|---|---|
| 0.1.0 | 稳定公开;官网/README 下载入口;无自更新 |
| 0.1.3 | 已删除(安装会话确认失败) |
| 0.1.4 | pre-release;下载/校验已验,安装界面待验 |

## 复现 / 继续验证

```bash
adb install -r /tmp/watch-013-provider.apk   # 0.1.3 + ApkProvider 测试包
# 手表 SYSTEM -> UPDATE -> 0.1.4 -> 观察是否出现系统安装界面
adb logcat -s RF-Lab.Watch.Update:V
adb shell dumpsys package dev.rflab.watch | grep versionName
```

若安装界面仍不出现,结论是该 ColorOS 版本不支持表内安装,应回滚自更新功能,
保留手动 `adb install -r`。
