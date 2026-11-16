# RF-Lab Android APP

同一份 `web/index.html` 跑在 WebView 里;原生层只做 BLE 权限、扫描、GATT 与
NDJSON 行转发。**APP 不发 HTTP**,链路是 BLE;Web 版才走 Wi-Fi。

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
./gradlew assembleDebug
```

> 首次 `./gradlew` 需要下载 Gradle 8.11.1;直连失败时按本机约定加 SOCKS 代理:
> `JAVA_TOOL_OPTIONS="-DsocksProxyHost=127.0.0.1 -DsocksProxyPort=10808" ./gradlew assembleDebug`。
> 也可以直接用本机 Gradle 8.11.1:`gradle assembleDebug`。

产物:`android/app/build/outputs/apk/debug/app-debug.apk`(约 1.2MB,debug 签名)。

安装:

```bash
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
# 或把 APK 传到手机,允许"安装未知来源"
```

首次打开会请求蓝牙权限(Android 12+ 为 `BLUETOOTH_SCAN`/`BLUETOOTH_CONNECT`;
≤11 为蓝牙 + 精确定位)。APP 自动扫描 `RF-Lab-*`,点设备连接;PIN 看设备 OLED
开机页 5s 或串口 `PIN`。

## 结构

| 文件 | 职责 |
|---|---|
| `MainActivity.java` | WebView、`WebViewAssetLoader`、`RFBridge`(JS↔原生)、权限 |
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

| 项 | 状态 |
|---|---|
| `assembleDebug` 构建 | ✅ 2026-11-15,APK 1.2MB,debug 签名 |
| 资源内嵌 | ✅ `assets/index.html` 49,676B,与 `web/dist` 一致 |
| Manifest/权限/包名 | ✅ `aapt2 dump badging` 检查 |
| 真机安装/启动 | ⏳ 需要 Android 手机 |
| 真机 BLE 扫描/连接/鉴权/发射 | ⏳ **未验证**;本机无蓝牙适配器,不能把构建成功写成链路验证 |
| Android 15 edge-to-edge 视觉 | ⏳ 需要真机 |

## 已知限制

- debug 签名,不适合上架;个人侧载可以。
- APP 只走 BLE,不提供 Wi-Fi 配网;配网请用浏览器打开 `http://192.168.4.1`。
- BLE PIN 是应用层鉴权,不加密链路;见 [`../docs/01-ui.md`](../docs/01-ui.md) §4。
- Android 12+ 的 `neverForLocation` 扫描标志已声明;若某些 ROM 仍过滤 BLE 结果,
  检查系统定位开关/厂商省电策略。
