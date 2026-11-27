# RF-Lab Android APP

同一份 `web/index.html` 跑在 WebView 里;原生层只做 BLE 权限、扫描、GATT 与
NDJSON 行转发。**控制链路是 BLE,不走 HTTP**;唯一的 HTTP 用途是 APP 自更新(只读 GitHub
Releases,不连设备)。Web 版才走 Wi-Fi。

**下载(最新 Release):** <https://github.com/heiheiheiheiheihei/rf-lab/releases/latest/download/rf-lab-android.apk>
(官网导航栏与首屏按钮指向同一 URL;`releases/latest` 永远取最新版。)

> 装上新版后,APP 自己也会查更新(见「APP 自更新」),不需要每次手动回来下载。

## 构建

工具链(本机已验证的版本):

| 件 | 版本 | 路径 |
|---|---|---|
| JDK | Temurin 17.0.21 | `~/.local/opt/jdk-17` |
| Android SDK | platform 35 / build-tools 35.0.0 / 34.0.0 | `~/.local/opt/android-sdk` |
| Gradle | 8.11.1 | `~/.local/opt/gradle-8.11.1` |
| AGP | 8.7.3 | Gradle 依赖 |

```bash
export JAVA_HOME="$HOME/.local/opt/jdk-17"
export ANDROID_HOME="$HOME/.local/opt/android-sdk"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$PATH"

# local.properties 写 sdk.dir(不入库)
printf 'sdk.dir=%s\n' "$ANDROID_HOME" > android/local.properties

# Web UI 先生成到 assets/index.html(与固件同一份)
python3 tools/build_web.py

cd android
./gradlew assembleRelease
```

> 首次 `./gradlew` 需要下载 Gradle 8.11.1;直连失败时按本机约定加 SOCKS 代理:
> `JAVA_TOOL_OPTIONS="-DsocksProxyHost=127.0.0.1 -DsocksProxyPort=10808" ./gradlew assembleRelease`。
> 也可以直接用本机 Gradle 8.11.1:`gradle assembleRelease`。

产物:`android/app/build/outputs/apk/release/app-release.apk`(0.7.2:`1,050,272B`,versionCode 4;0.7.1 为 1,050,176B/versionCode 3)。

### release 签名

`app/build.gradle.kts` 从两处读取签名凭据,**都不入库**:

1. `android/keystore.properties`(本地,已 gitignore);
2. 环境变量 `RFLAB_KEYSTORE_PATH` / `RFLAB_KEYSTORE_PASSWORD` / `RFLAB_KEY_ALIAS` /
   `RFLAB_KEY_PASSWORD`(CI 用 GitHub Secrets 注入)。

两处都没有时 `assembleRelease` 仍可构建,但得到的是**未签名 APK,不可安装**——发布流水线
会显式跑 `apksigner verify` 并失败。

本机凭据位置(仓库外,600):

```
~/.local/share/rf-lab/android/rf-lab-release.jks
~/.config/rf-lab/android-keystore.env
```

> ⚠️ **密钥是更新链的根**:APP 自更新与手动安装都靠它做同一身份;丢失后已安装的版本
> 永远无法覆盖升级,只能换包名/让用户卸载。务必离线备份这两个文件。
> 证书 SHA-256 见 `RELEASE_NOTES.md`。

发布由 tag 触发(`.github/workflows/android-release.yml`):推 `v*` 标签 → 校验标签与
`versionName` 一致 → 构建、验签、
上传 `rf-lab-android.apk`(稳定名)+ `rf-lab-<version>.apk` + `SHA256SUMS.txt`。
仓库 Secrets 需配置:`ANDROID_KEYSTORE_BASE64`(keystore 的 base64)、
`ANDROID_KEYSTORE_PASSWORD`、`ANDROID_KEY_ALIAS`、`ANDROID_KEY_PASSWORD`。
发版前先 bump `versionCode`(必须递增)和 `versionName`(必须等于 tag 去掉 `v`),否则工作流会拒绝发布。
`versionCode` 同时是 APP 自更新的降级闸:新包必须比已装的大,否则拒装。

安装:

```bash
adb install -r android/app/build/outputs/apk/release/app-release.apk
# 或把 APK 传到手机,允许"安装未知来源"
```

debug 包仍可用 `./gradlew assembleDebug` 构建(`app-debug.apk`),但与 release 签名不同,
不能互相覆盖安装。

首次打开会请求蓝牙权限(Android 12+ 为 `BLUETOOTH_SCAN`/`BLUETOOTH_CONNECT`;
≤11 为蓝牙 + 精确定位)。APP 自动扫描 `RF-Lab-*`,点设备连接;PIN 看设备 OLED
开机页 5s 或串口 `PIN`。

### APP 自更新(0.7.0 起;0.7.1 沿用)

APP 不再等用户手动回来下载:每次冷启动/回前台,节流 6 小时查一次 GitHub Releases,
有新版本就弹「一键更新 / 忽略此版本 / 稍后」。

| 步 | 做什么 | 失败时 |
|---|---|---|
| 检查 | `GET api.github.com/repos/<repo>/releases?per_page=30`;只认 draft/prerelease 之外、带 `rf-lab-android.apk` 资产的 Release,取版本号最高的 | 静默(没网/限流不打扰),下次回前台再试 |
| 比较 | tag → `versionName`,数字段比较;`0.7.0 > 0.7.0-rc1`,`0.10.0 > 0.9.0` | — |
| 提示 | `发现新版本` 弹窗:体积 + Release 说明 | — |
| 忽略 | 记 `SharedPreferences` 里的版本号,同版本不再提示,下一版重新提示 | — |
| 下载 | HTTPS 到 `cache/update/`,上限 40MB,进度百分比 | 弹「更新失败」+ 原因 |
| 校验 | ① Release 的 `SHA256SUMS.txt` ② 包名 + `versionCode`/`versionName` 必须前进 ③ 签名证书与已装 APP 一致 | **失败关闭**,不安装 |
| 安装 | `PackageInstaller` 会话 → 系统确认框(需允许「安装未知应用」) | 弹「安装失败」+ 系统消息 |

边界(写死):

- **不是静默更新**:系统确认框必须由用户点;不申请无障碍、不 root。
- **不是云推送**:每次检查是 APP 主动拉公开 Release,仓库里没有推送服务。
- **下载前先要权限**:Android 8+ 若未允许「安装未知应用」,先跳设置页,回来自动续下载。
- **忽略不跨版本**:忽略只对当前版本号生效。
- 代码:`Updater.java`(网络/校验/安装)、`AppVersion.java`(比较)、`Sums.java`(查表);
  主机测试 `./tools/app_update_test.sh`(36 checks,不需要 SDK/模拟器)。
- 证据与未验证项:[`../docs/evidence/2026-11-21-app-update.md`](../docs/evidence/2026-11-21-app-update.md) ·
  [`../docs/evidence/2026-11-22-app-ble.md`](../docs/evidence/2026-11-22-app-ble.md)。

## 结构

| 文件 | 职责 |
|---|---|
| `MainActivity.java` | WebView、`WebViewAssetLoader`、`RFBridge`(JS↔原生)、权限、更新弹窗 |
| `Updater.java` | 查 GitHub Release、下载、SHA-256/签名自检、`PackageInstaller` 会话 |
| `AppVersion.java` / `Sums.java` | 版本比较 / `SHA256SUMS.txt` 查表(纯 Java,主机可测) |
| `BleClient.java` | 扫描/连接/GATT、MTU、CCCD、写队列、按 `\n` 重组通知 |
| `assets/index.html` | 由 `tools/build_web.py` 从 `web/` 生成,与固件同字节 |
| `res/...` | 深色主题、琥珀准星自适应图标、manifest 权限 |

BLE 参数(与固件一致):

| 项 | 值 |
|---|---|
| Service | `5a3d0001-7c1f-4b8e-9a55-52464c414231` |
| RX(写) | `5a3d0002-7c1f-4b8e-9a55-52464c414231` |
| TX(通知) | `5a3d0003-7c1f-4b8e-9a55-52464c414231` |
| MTU | 请求 247;按 `MTU-3` 分片 |
| 行协议 | UTF-8,`\n` 结束;首条 `AUTH <4位PIN>` |

## 已验证 / 未验证

> 完整登记、优先级与 0.7.2 退出条件见 [`../docs/02-plan.md`](../docs/02-plan.md) §3;
> 本表只保留 Android 视角。

| 项 | 状态 |
|---|---|
| `assembleRelease` + 签名 | ✅ 0.7.2:1,050,272B/versionCode 4/release 密钥 v2;0.7.1:1,050,176B/versionCode 3 |
| 资源内嵌 | ✅ `assets/index.html` 129,449B,与 `web/dist` 一致 |
| Manifest/权限/包名 | ✅ `aapt2 dump badging`: `dev.rflab.terminal` 4/0.7.2,minSdk 26,targetSdk 35,INTERNET + REQUEST_INSTALL_PACKAGES |
| 自更新逻辑(主机) | ✅ `./tools/app_update_test.sh` 36 checks;真实 Release 的 `./name` SUMS 查表与实际哈希一致 |
| 自更新真机链路 | ✅ 2026-11-22:0.7.0→0.7.1 弹窗/下载/系统安装器/覆盖安装成功(用户确认);⏳ 0.7.2 未复测 |
| GitHub Release 发布 | ✅ [v0.7.2](https://github.com/heiheiheiheiheihei/rf-lab/releases/tag/v0.7.2)(1,050,272B + `firmware.bin` + `SHA256SUMS.txt`);v0.7.0/v0.7.1 历史 Release 保留;官网按钮指向 `latest/download` |
| 真机安装/启动 | ✅ 0.7.1 已在真机安装;0.7.0 亦已真机连 BLE;⏳ 0.7.2 真机安装 |
| 真机 BLE 扫描/连接/鉴权/码库 | ✅ 手机连接、AUTH、`DISP TEST 30` 回显、码表 9 条(2026-11-21);✅ BLE `NAME/GROUP/SYM/EDIT` 写入(2026-11-22);⏳ BLE `SEND` |
| Android 15 edge-to-edge 视觉 | ⏳ 需要真机 |

## 已知限制

- release 密钥自签,不上应用商店;个人侧载可以。与历史 debug 包签名不同,不能覆盖安装。
- APP 只走 BLE,不提供 Wi-Fi 配网;配网请用浏览器打开 `http://192.168.4.1`。
- BLE PIN 是应用层鉴权,不加密链路;见 [`../docs/01-ui.md`](../docs/01-ui.md) §4。
- **固件 OTA 不更新 APK**:OTA 只换设备端整包(含设备页面);APP 里的页面是 APK 构建时
  冻结的副本。APP 的更新走 GitHub Release(0.7.0 起可在 APP 内一键更新);0.7.1 起允许
  固件/APK 同号,但两条更新链路仍互不
  覆盖;设备与 APP 的 `build`/`ui` 可在帮助面板或 `/api/hello` 对比。APP 走 BLE,
  不提供 OTA 上传入口(Web 的帮助面板 `U` 才有)。
- 自更新检查需要联网;被墙/限流时静默跳过,不阻塞 BLE 与控制功能。
- 「忽略此版本」只在看到下一个版本后自动解除;要立刻重查,清除 APP 数据即可。
- Android 12+ 的 `neverForLocation` 扫描标志已声明;若某些 ROM 仍过滤 BLE 结果,
  检查系统定位开关/厂商省电策略。
