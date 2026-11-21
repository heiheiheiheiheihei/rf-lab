# 2026-11-21 · Android APP 自更新(0.7.0)

**范围**:APP 冷启动/回前台自动查 GitHub Releases,有新版本弹「一键更新 / 忽略此版本 / 稍后」;
下载过 SHA-256 与签名自检后交给系统安装器;忽略只针对该版本号。
**结论**:构建、签名、权限、主机逻辑测试、真实 Release 数据校验全部通过;
**真机弹窗 / 下载 / 系统安装器 / 覆盖安装仍未验证**(构建机没有 Android 手机)。

> 端到端能真的弹出提示,前提是**装的是 0.7.0,并且 tag 上出现更高版本**。
> 发布 v0.7.0 本身还不会触发(它就是最新版);要等 v0.8.0 才能真机回归。

## 环境

| 件 | 版本 |
|---|---|
| JDK | Temurin 17.0.21(`~/.local/opt/jdk-17`) |
| Android SDK | platform 35 / build-tools 35.0.0 |
| Gradle / AGP | 8.11.1(wrapper)/ 8.7.3 |
| 设备 | 无(构建机无 Android 手机/蓝牙适配器) |

## 改动

| 文件 | 作用 |
|---|---|
| `android/app/src/main/java/dev/rflab/terminal/Updater.java` | 查 Release、下载、SHA-256/签名自检、PackageInstaller 会话 |
| `.../AppVersion.java` | tag → versionName 的比较规则(纯 Java) |
| `.../Sums.java` | `SHA256SUMS.txt` 查表(纯 Java) |
| `.../MainActivity.java` | 6h 节流检查、更新弹窗、进度、未知来源权限跳转、安装结果广播 |
| `app/src/main/AndroidManifest.xml` | `INTERNET` + `REQUEST_INSTALL_PACKAGES` |
| `app/build.gradle.kts` | `versionCode 1→2`、`versionName 0.6.0→0.7.0` |
| `app/src/test/java/dev/rflab/terminal/UpdateLogicTest.java` | 主机测试(36 checks) |
| `tools/app_update_test.sh` | 主机测试入口,只编译纯 Java,不需要 SDK/Gradle |

流程:

```
回前台 ──6h 节流──▶ GET api.github.com/repos/<repo>/releases?per_page=30
                      └─ 只认 draft=false / prerelease=false / 带 rf-lab-android.apk 的最高版本
                            └─ AppVersion.isNewer(最新, 已装) ? 弹窗 : 静默
弹窗 ──一键更新──▶ 未知来源权限? ──否──▶ 跳设置,回来续下
                        └─是──▶ 下载到 cache/update ──▶ SHA256SUMS 比对
                                  ──▶ 包名/versionCode/versionName 前进
                                  ──▶ 签名证书 == 已装 APP ──▶ PackageInstaller 会话
                                  ──▶ 系统确认框 ──▶ 安装完成重启
弹窗 ──忽略此版本──▶ SharedPreferences 记版本号(同版本不再提示;下一版重新提示)
```

## 本机命令与结果

```bash
./tools/app_update_test.sh
# UpdateLogic OK (36 checks)

cd android && ./gradlew assembleRelease --offline
# BUILD SUCCESSFUL in 3s
# 43 actionable tasks: 11 executed, 32 up-to-date
```

产物与签名:

```
app-release.apk            1,027,080 B
sha256                     faad383216f19226bad6083ce661b92053204fe6f53f6c8c4da18a14e7fc5a1b
apksigner verify           Verifies;Verified using v2 scheme: true
cert SHA-256               b8ccea6c1b2fba85bdbf59dada0d21bf987f60dd97703c0c8821bb63d8ab0dea
```

`aapt2 dump badging`(关键行):

```
package: name='dev.rflab.terminal' versionCode='2' versionName='0.7.0'
targetSdkVersion:'35'
uses-permission: BLUETOOTH_SCAN(neverForLocation)/BLUETOOTH_CONNECT
uses-permission: BLUETOOTH / BLUETOOTH_ADMIN / ACCESS_FINE_LOCATION(maxSdk 30)
uses-permission: INTERNET
uses-permission: REQUEST_INSTALL_PACKAGES
```

`dexdump` 确认新类进包:`AppVersion`、`Sums`、`Updater$CheckCallback`、`Updater$Progress` 等;
内嵌 `assets/index.html` 106,274 B,与 `web/dist/index.html` 同哈希(`16ff0cdc…`)。

## 真实 Release 数据校验

把「格式假设」按真实资产打掉 —— `SHA256SUMS.txt` 由
`(cd dist && sha256sum ./*.apk > SHA256SUMS.txt)` 生成,文件名带 `./` 前缀:

```
$ cat SHA256SUMS.txt
e68db13b…c40ffa  ./rf-lab-0.6.0.apk
e68db13b…c40ffa  ./rf-lab-android.apk
```

用仓库里的 `Sums` 解析真实文件,与下载后实算的哈希对照:

```
$ java ... dev.rflab.terminal.Check SHA256SUMS.txt rf-lab-android.apk
e68db13b849e5b314c3964ab15f0f1936c0adfbf35b6330b76887ae635c40ffa
$ sha256sum rf-lab-android.apk
e68db13b849e5b314c3964ab15f0f1936c0adfbf35b6330b76887ae635c40ffa
```

下载的官方 APK 也过签名:`apksigner verify --verbose` → `Verifies`,`v2 true`,
证书 `b8cc…0dea`(与 APP 自身一致,自检不会误判)。

> 这次校验抓到一个真 bug:最初只按纯文件名匹配,真实文件的 `./` 前缀会让
> 「SHA-256 条目不存在」永远命中 —— 真机上表现为下载完成后拒绝安装。
> 改成按 basename 匹配后进主机测试(`UpdateLogicTest`)。

## 验收

| 项 | 结果 |
|---|---|
| `assembleRelease`(release 签名) | ✅ 1,027,080 B,`apksigner verify` v2 通过 |
| 包名 / 版本 / 权限 | ✅ `dev.rflab.terminal` 2 / 0.7.0 / minSdk 26 / targetSdk 35 / INTERNET + REQUEST_INSTALL_PACKAGES |
| 版本比较 + SUMS 解析(主机) | ✅ 36 checks,含真实 `./name` 格式 |
| 真实 Release 资产校验 | ✅ 查表哈希 == 实际哈希;官方 APK 签名通过 |
| 资源/生成物一致性 | ✅ `build_web.py --check`;内嵌页面 106,274 B 同哈希 |
| 既有主机单测回归 | ✅ `./tools/host_test.sh`:76 / 53 / 41 / 1286 checks + TUI 12 全过 |
| 真机:弹窗 / 下载进度 / 设置跳转 | ⏳ **未验证** |
| 真机:系统安装器确认 / 覆盖安装 / 重启后版本号 | ⏳ **未验证** |
| 真机:BLE 功能未被 INTERNET 权限影响 | ⏳ 未回归(代码路径未改) |

## Key Judgment

1. **用 `PackageInstaller` 会话,不用 `file://` + `ACTION_VIEW`**:不需要 FileProvider、
   不把 APK 路径暴露给别的应用,也不触发 `FileUriExposedException`;确认框仍由系统弹。
   结果广播用 `FLAG_MUTABLE` + `setPackage(...)`:Android 14 只对**隐式** Intent 抛异常,
   AOSP 的判据是 `component == null && package == null`,设了包名就是显式。
2. **失败关闭**:缺少 `SHA256SUMS.txt` 条目、大小不符、包名/版本没前进、签名证书不一致,
   任何一条都终止安装并给出可读原因;系统安装时还会再验一次签名,APP 侧自检只是提前失败。
3. **忽略粒度是版本号**:`0.7.0` 被忽略不影响 `0.7.1`;检查失败(没网/限流)静默,不打扰。
4. **按资产挑版本,而不是只看 `releases/latest`**:同一个 tag 可能只发固件/文档;
   只有带 `rf-lab-android.apk` 的正式 Release 才算 APP 版本。
5. **版本号主动 bump 到 0.7.0 / code 2**:CI 要求 tag 与 `versionName` 一致,
   这次改动注定是新的 APP 版本;不 bump 就等于把功能挂在 0.6.0 上、永远发不出去。

## Avoided Misfix

- 不按纯文件名解析 `SHA256SUMS.txt`(真实文件是 `./…`,会永远“缺条目”)。
- 不做静默安装 / 不申请无障碍服务 / 不引导 root —— 侧载更新必须由用户在系统弹窗确认。
- 不在本次把官网 `0.6.0` 文案改成 `0.7.0`:Release 还不存在,页面会指向一个不存在的版本。
- 不把「构建通过 + 主机测试通过」写成「真机自更新已验证」。

## CI 发布结果(v0.7.0)

工作流 run [44914572503](https://github.com/heiheiheiheiheihei/rf-lab/actions/runs/44914572503)
在 `v0.7.0` 上全绿:tag/`versionName` 校验 → `build_web.py --check` → 还原密钥 →
`assembleRelease` → `apksigner verify` → 上传资产 → 建 Release。

Release:<https://github.com/heiheiheiheiheihei/rf-lab/releases/tag/v0.7.0>

| 资产 | 大小 | SHA-256 |
|---|---:|---|
| `rf-lab-android.apk` | 1,027,080 B | `68d9f594…40960d` |
| `rf-lab-0.7.0.apk` | 1,027,080 B | 同上 |
| `SHA256SUMS.txt` | 172 B | — |

CI 产物下载后复核:

```
apksigner verify --verbose : Verifies,v2 true
aapt2 dump badging         : dev.rflab.terminal versionCode='2' versionName='0.7.0'
                             INTERNET / REQUEST_INSTALL_PACKAGES
sha256sum rf-lab-android.apk : 68d9f5945148a36bd0f31967f317637154e923b58d5c0cb041ef821e4840960d
仓库 Sums 解析 SHA256SUMS.txt : 68d9f594…(与实算一致 ⇒ APP 内校验会通过)
```

本地构建与 CI 产物同为 1,027,080 B、同一证书,但字节哈希不同(本地 `faad3832…`):
和 0.6.0 一样来自打包/签名时间戳元数据,**不要**把它当成可复现构建的证明。

发版后已同步官网/状态板的 APK 版本号与体积(`0.7.0 · release 签名 · 1.03 MB`),
固件侧 `0.6.0` 文案与体积未动。`releases/latest/download/rf-lab-android.apk` 现指向 v0.7.0。

## 未验证 / 后续

- 真机回归需要一台 Android 手机:首次弹窗、下载进度、跳「安装未知应用」、系统确认框、
  覆盖安装后 `versionName` 变成新版。
- 已装 0.6.0 的用户没有自更新,必须手动升级一次 0.7.0;之后才进入自更新链路。
- 端到端「弹出提示」还要等 **0.7.0 装机 + 更高版本 tag**:发 v0.8.0(或任意 0.7.1+)后,
  在装了 0.7.0 的真机上回归。
