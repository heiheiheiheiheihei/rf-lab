# 2026-11-21 · Android release APK 发布 + 官网下载入口

**范围**:把 Android 端从「本机 debug 构建」推进到「签名的 GitHub Release + 官网可下载」。
**结论**:本机构建/验签/包信息通过;Release 由 tag 触发的 Actions 发布;官网按钮指向
`releases/latest/download/rf-lab-android.apk`。**真机安装与 BLE 链路仍未验证**(构建环境
无 Android 手机、无蓝牙适配器)。

## 环境

| 件 | 版本 |
|---|---|
| JDK | Temurin 17.0.21(`~/.local/opt/jdk-17`) |
| Android SDK | platform 35 / build-tools 35.0.0(`~/.local/opt/android-sdk`) |
| Gradle | 8.11.1(wrapper) |
| AGP | 8.7.3 |

## 本机命令与结果

```bash
python3 tools/build_web.py            # 生成 web/dist、固件 gzip、Android assets、官网 demo
cd android && ./gradlew assembleRelease
```

```
BUILD SUCCESSFUL in 1m 25s
43 actionable tasks: 43 executed
```

产物与签名:

```
android/app/build/outputs/apk/release/app-release.apk   967,996 B
sha256 4d5bb53b2ccce47e78c29e45f3bfff954479c0ba9366042b33de8ca41f714ded
apksigner verify: Verifies
  Verified using v2 scheme: true
  Signer #1 DN: CN=RF-Lab, OU=Android, O=RF-Lab, L=Local, ST=Local, C=CN
  Signer #1 certificate SHA-256:
  B8:CC:EA:6C:1B:2F:BA:85:BD:BF:59:DA:DA:0D:21:BF:98:7F:60:DD:97:70:3C:0C:88:21:BB:63:D8:AB:0D:EA
```

包信息(`aapt2 dump badging`):

```
package: name='dev.rflab.terminal' versionCode='1' versionName='0.6.0'
minSdkVersion:'26'  targetSdkVersion:'35'
uses-permission: BLUETOOTH_SCAN (neverForLocation), BLUETOOTH_CONNECT,
                BLUETOOTH (maxSdk 30), BLUETOOTH_ADMIN (maxSdk 30),
                ACCESS_FINE_LOCATION (maxSdk 30)
```

内嵌 Web 资源:

```
unzip -p app-release.apk assets/index.html | sha256sum
3633dff5478b962d7374521fb6046f7c4c72a7f4962b13a91a66f2e684fb6928
web/dist/index.html              同哈希
android/app/src/main/assets/     同哈希
```

## 签名与发布

- release 密钥:**仓库外**、新建。`~/.local/share/rf-lab/android/rf-lab-release.jks`(600)
  与 `~/.config/rf-lab/android-keystore.env`(600);`android/keystore.properties` 600 且
  已被 `.gitignore` 忽略(`*.jks` 同样忽略)。
- CI:`.github/workflows/android-release.yml`,`push` 标签 `v*` 触发;从 Secrets 还原
  keystore → `assembleRelease` → `apksigner verify` → 上传
  `rf-lab-android.apk`、`rf-lab-<version>.apk`、`SHA256SUMS.txt` → 幂等创建/更新 Release。
- 仓库 Secrets:`ANDROID_KEYSTORE_BASE64`、`ANDROID_KEYSTORE_PASSWORD`、
  `ANDROID_KEY_ALIAS`、`ANDROID_KEY_PASSWORD`(只写名字,不落值)。

## 官网改动

- 导航栏 CTA 改为「下载 APK」;首屏新增第三颗按钮「下载 Android APK / 0.6.0 · ↓」;
  页脚 SOURCE 增加 APK / Releases。
- 状态板 Android 行:`◐ 已构建 / 1.28 MB debug` → `◐ 已发布 / 0.6.0 · release 签名 · 0.97 MB`;
  保持 `◐` 与「真机未测」,不写成 `●`。
- 链接用 `releases/latest/download/rf-lab-android.apk`(固定资产名),不写死 tag。

## 验收

| 项 | 结果 |
|---|---|
| `build_web.py --check` | ✅ gzip 16,745 B,三处资源同哈希 |
| `assembleRelease` | ✅ 967,996 B |
| `apksigner verify` | ✅ v2 签名,证书 SHA-256 如上 |
| `aapt2 dump badging` | ✅ 包名/版本/权限/minSdk 26/targetSdk 35 |
| 内嵌 `assets/index.html` | ✅ 60,433 B,与 `web/dist` 同哈希 |
| `site_check.py` + `node --check` | ✅ SITE OK / JS OK |
| Release 资产与官网按钮 200 | ✅ 见下方 CI / 线上核验 |
| 真机安装 / 启动 | ⏳ 未验证 |
| 真机 BLE 扫描/连接/鉴权/发射 | ⏳ 未验证 |
| Android 15 edge-to-edge | ⏳ 未验证 |

## Key Judgment

1. **新建 release 密钥,而不是直接发 debug 包**:debug keystore 由构建机生成、随时可能被清掉,
   无法承诺后续升级;release 密钥虽然带来保管责任,但给了版本线一个稳定身份。
2. **固定资产名 + `latest/download`**:官网不随版本改链接,发版只覆盖同名资产。
3. **CI 验证签名而不是只构建**:`assembleRelease` 在没有密钥时会产出未签名 APK 且仍然
   "BUILD SUCCESSFUL";流水线用 `apksigner verify` 把这种假成功挡在发布前。

## Avoided Misfix

- 不把 keystore 提交进仓库换 CI 方便——那等于把更新链公开。
- 不因为官网按钮 `200` 就写成"已上机验证";Release 只证明可分发,不证明 BLE 链路。
- 不给 Release 写"兼容旧 debug 包"——签名不同,已在下载说明里要求先卸载。
