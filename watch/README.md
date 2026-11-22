# RF-Lab Watch — OPPO Watch 2 客户端

> 一句话:**把手机 APP 的“选码 → ARM → 发射”压缩成手表上的一屏 + 两次触摸**;
> 只走 BLE,复用 `RF-Lab-*` 的既有 GATT / 行协议,不改固件、不改手机 APP。

**公开下载:** <https://github.com/heiheiheiheiheihei/rf-lab/releases/download/watch-v0.1.0/rf-lab-watch.apk>
· `rf-lab-watch.apk` / `rf-lab-watch-0.1.0.apk` / `SHA256SUMS.txt` · 版本 0.1.0 · 独立 release key。

## 1. 结论与范围

| 项 | 决定 |
|---|---|
| 目标设备 | OPPO Watch 2(46mm 402×476 / 42mm 372×430,326 PPI) |
| 系统假设 | ColorOS Watch 2.0,Android 8.1(API 27);minSdk 26 / targetSdk 28 |
| 形态 | 单 Activity + 自绘 View,无 WebView、无 AndroidX、无网络权限 |
| 传输 | BLE GATT,与手机 APP / Web 相同的 Service/RX/TX 和 `\n` 行协议 |
| 主要页面 | LINK(扫描/连接)→ PIN(4 位数字键盘)→ CONTROL(单码卡片 + SEND/HOLD) |
| 次级页面 | CODEBOOK 列表浮层、SYSTEM 菜单、PIN 键盘 |
| 自更新 | SYSTEM → UPDATE 手动触发;GitHub Release → SHA-256/包名/版本/签名校验 → 系统安装器确认 |
| 不做的 | 码库编辑/新建/删除、Wi-Fi 配网、固件 OTA、后台自更新、静默安装、通知、云账户 |

**为什么是“单码卡片”而不是码库表格**:手表可用宽度约 180–200dp,一张表放不下
band/code/name/动作四类信息;横向滑动和长按都难发现。单码卡片 + 上下滑动换码 +
大按钮,让最常用路径(选码 → 发射)保持两次触摸。

## 2. 目标设备事实与假设

| 事实 | 值 | 来源 |
|---|---|---|
| 46mm 屏幕 | 1.91" AMOLED,402×476,326 PPI | [OPPO 官方规格](https://www.oppo.com/cn/accessories/oppo-watch-2/specs/46mm/) |
| 42mm 屏幕 | 1.75" AMOLED,372×430,326 PPI | [OPPO 官方规格](https://www.oppo.com/cn/accessories/oppo-watch-2/specs/42mm/) |
| 平台 | ColorOS Watch 2.0,基于 Android 8.1;Snapdragon Wear 4100 + Apollo 4s;1GB/8GB | 发布资料(见 §10 引用) |
| ADB | 设置 → 其他设置 → 关于手表 → 连点版本号 → 开发者选项 → ADB 调试 | 本机 OW20W1 已验证;菜单路径以实机为准 |

> **关键风险**:OPPO Watch 2 的 ADB 入口/连接方式(USB 调试、WLAN 调试或 OTG)随
> 固件版本不同,部分 OPPO Watch 只开 OTG。**没有 `adb devices` 看到设备之前,
> 不把“可安装/可调试”写成已验证。**

## 3. 交互设计

### 3.1 屏幕与布局

设计基准 **200×232dp**,同时保证 **180×208dp**(42mm)不裁切。左右安全边距 8dp,
上下 6dp;所有可点区域 ≥ 40dp,主按钮 46dp。

```text
CONTROL(200×232dp)                 LINK
┌──────────────────────────┐       ┌──────────────────────────┐
│ ≡  RF-LAB        [SAFE]  │  28   │    RF-LAB        W/0.1   │
│ ● RF-Lab-AB12      ONLINE│  18   │  LINK RF-LAB             │
│ ┌──────────────────────┐ │       │  BLE · 4-DIGIT PIN       │
│ │ 03/09          315M  │ │       │  [ SCAN / STOP ]         │
│ │ 客厅大门              │ │  92   │  LAST RF-Lab-AB12        │
│ │ 5555033C        ▲▼   │ │       │  ─ RF-Lab-AB12   -57 dBm │
│ └──────────────────────┘ │       │  ─ RF-Lab-CD34   -71 dBm │
│ OK TX #3 · 852ms         │  18   │  PIN: OLED boot / serial │
│ [   SEND   ][   HOLD   ] │  46   └──────────────────────────┘
└──────────────────────────┘
```

| 区域 | 行为 |
|---|---|
| `≡` | 打开 SYSTEM 菜单:2×2 网格(RECONNECT / OLED 10s / REFRESH / UPDATE)+ 整行 FORGET |
| `SAFE / ARMED` | 点击切换;ARM 只在本次连接有效,断线/退出即复位 |
| 码卡 | 上/下滑换码(循环);点击打开 CODEBOOK 列表 |
| `SEND` | 单次发射;SAFE 时只提示先 ARM,不发命令 |
| `HOLD` | **按住 350ms 才发 `LONG`,松开发 `STOP`**;这是长发唯一入口 |
| 状态行 | 最近一条 `event/log`;`OK` 绿、`WARN/ERR` 红/琥珀,不静默 |
| `STOP` | 仅在“设备端已 active 但不是本机按住”时出现;整行红色 |

### 3.2 操作路径

| 目标 | 路径 | 触摸数 |
|---|---|---|
| 首次连接 | SCAN → 点设备 → 输 4 位 PIN | 1 + 1 + 4 |
| 再次连接 | 打开 APP 自动连上次设备,存过 PIN 则自动 AUTH | 0 |
| 换码 | 码卡上/下滑,或点卡 → 列表选 | 1 |
| 单次发射 | 点 `SAFE` → 点 `SEND` | 2 |
| 长发 | 按住 `HOLD` 350ms,松开即停 | 1 次按住 |
| 停止异常长发 | 重连后自动发 `STOP`;或点红色 `STOP` | 1 |
| 更新 APP | SYSTEM → `UPDATE` → 下载/校验 → 系统确认安装 | 2 + 系统确认 |

### 3.3 PIN 键盘

- 4 个方点,输入第 4 位自动提交 `AUTH <pin>`,不额外点 OK。
- 键盘 `1..9 / ⌫ / 0 / CLR`;每键至少 40×34dp。
- 错误 PIN 显示设备返回的 `left`;连续 5 次错误设备会断开,APP 回到 LINK 并提示锁定。
- PIN 默认按设备地址保存在 APP 私有 `SharedPreferences`,可在菜单 `FORGET` 清除。
  **这是便利,不是加密**;设备端仍是应用层 PIN,链路不加密。

## 4. 屏幕适配

| 项 | 规则 |
|---|---|
| 尺寸 | 按 `dp` 计算,不写死 px;`onSizeChanged` 重算所有命中矩形 |
| 高度 | 顶部栏/链路条/状态行/按钮固定;码卡吃掉剩余高度(42mm 约 68dp,46mm 约 92dp) |
| 宽度 | 双按钮均分;文本 `ellipsize`;中文名最多 1 行 |
| 字体 | 数据/码用 `MONOSPACE`;名称用系统 sans;字号 7–18sp |
| 圆角 | 视觉保持方角(0–2dp);内容避开手表玻璃圆角(8dp 边距) |
| 系统栏 | 全屏沉浸;不依赖导航栏/状态栏 |
| 刷新 | 扫描/按住/长发时 30fps 自绘;其余按需 `invalidate`,不做常驻动画 |

## 5. BLE 契约(与手机 APP 相同,不复制时序)

| 项 | 值 |
|---|---|
| 广播名 | `RF-Lab-XXXX` |
| Service | `5a3d0001-7c1f-4b8e-9a55-52464c414231` |
| RX(写) | `5a3d0002-7c1f-4b8e-9a55-52464c414231` |
| TX(通知) | `5a3d0003-7c1f-4b8e-9a55-52464c414231` |
| MTU | 请求 247;写/通知按 `MTU-3` 分片;设备端行长上限 256B |
| 行协议 | UTF-8,`\n` 结束;连接后设备发 `{"t":"hello",...}` |
| 鉴权 | `AUTH <4位PIN>`;成功 `{"t":"auth","ok":1}`;失败带 `left` |
| 控制 | `SEND <id>` / `LONG <id>` / `STOP <id>`;`DISP TEST 10`;`LIST LONG` |
| 状态 | `state.tx.active/kind/id/ok/ms`、`store.count`;`codes` 全量快照 |
| 事件 | `event.kind=tx/learn`;`log`;`error` |

**只提交命令行,不在手表里复制 `SEND/LONG/STOP` 的时序**;射频行为仍由固件唯一拥有。
手表不做 `NAME/EDIT/NEW/ERASE`,这些留在手机/Web。

## 6. 安全与失败关闭

| 场景 | 行为 |
|---|---|
| 未 ARM 点 SEND | 不发命令;状态行提示 `TAP SAFE → ARMED`,SEND 保持禁用 |
| 按住 HOLD 后松手/滑出/来电/退到后台 | 立即发 `STOP <id>`;`onPause` 也兜底 |
| 长发中断线 | 进入红色 `LINK LOST · TX MAY CONTINUE`;自动重连,鉴权成功后**先发 STOP** |
| 5 次错误 PIN | 设备断开;APP 清空输入、回 LINK,提示锁定,不自动重试 |
| 连接超时 | 10s 未完成 GATT/通知订阅 → 主动断开,回 LINK |
| 无 BLE 权限/蓝牙关/定位关(API≤30) | 显示原因和可点的修复入口,不假装在扫描 |
| 退出/返回 | 若 TX active 先 `STOP`,250ms 后断开;不静默把长发留在设备上 |
| PIN 保存 | APP 私有目录明文;菜单 `FORGET` 清除;**不是密钥库/加密** |

**ARM 只是防误触**,不是权限门;BLE 链路无加密,抓包可见明文命令。这两条与
`docs/01-ui.md` §4 一致,不要在 UI 或文档里升级成“安全认证”。

## 7. 代码结构

| 文件 | 职责 |
|---|---|
| `WatchActivity.java` | 唯一状态源:连接、鉴权、ARM、选码、TX、生命周期兜底 |
| `WatchView.java` | 自绘 UI + 命中测试 + 浮层/动画;不持有协议状态 |
| `BleClient.java` | 扫描/连接/GATT/MTU/CCCD/写队列/按 `\n` 重组 |
| `Protocol.java` | `hello/auth/codes/state/event/log` 解析与选择逻辑(可主机测试) |
| `Prefs.java` | 上次设备、PIN、选中码;`FORGET` 清空 |
| `app/src/test/...` | 纯 JVM 协议/选择测试,不需要手表 |

## 8. 构建与主机测试

工具链与手机 APP 相同(JDK 17 / Android SDK 35 / Gradle 8.11.1):

```bash
export JAVA_HOME="$HOME/.local/opt/jdk-17"
export ANDROID_HOME="$HOME/.local/opt/android-sdk"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$PATH"

printf 'sdk.dir=%s\n' "$ANDROID_HOME" > watch/local.properties
./watch/check.sh          # test + lint + debug;有 watch/keystore.properties 时再验 release 签名
```

产物:`watch/app/build/outputs/apk/debug/app-debug.apk`。

### 8.1 Release 签名(独立 watch key)

手表使用**独立于手机 APK**的 release key,不复用 `dev.rflab.terminal` 的密钥。
密钥和口令只在仓库外:

```text
~/.local/share/rf-lab/watch/rf-lab-watch-release.jks
~/.config/rf-lab/watch-keystore.env
watch/keystore.properties   # gitignored, 600
```

```bash
set -a; . "$HOME/.config/rf-lab/watch-keystore.env"; set +a
cd watch && ../android/gradlew assembleRelease
"$ANDROID_HOME/build-tools/35.0.0/apksigner" verify --print-certs \
  app/build/outputs/apk/release/app-release.apk
```

| 项 | 值 |
|---|---|
| alias | `rflab-watch`(RSA 4096,PKCS12,10000 天) |
| 证书 SHA-256 | `ED:C2:A5:C1:0E:68:C3:35:A9:D7:7B:BD:44:C6:C1:34:18:A8:08:7B:EE:DD:D8:47:BE:05:51:3B:A4:90:94:A7` |
| 签名方案 | APK Signature Scheme v2(Android 8.1 支持) |
| 产物 | `watch/app/build/outputs/apk/release/app-release.apk` |
| 公开 Release | [watch-v0.1.0](https://github.com/heiheiheiheiheihei/rf-lab/releases/tag/watch-v0.1.0) · sha256 `739afa…` |

**debug → release 不能覆盖安装**:签名不同,必须先 `adb uninstall dev.rflab.watch`,
会清掉记住的 PIN/设备/选中码。首装 release 后重新配对即可。

静态检查:

```bash
"$ANDROID_HOME/build-tools/35.0.0/aapt2" dump badging \
  watch/app/build/outputs/apk/debug/app-debug.apk
```

### 8.2 APP 自更新(0.1.2 起)

更新是**手动**的:SYSTEM → `UPDATE`,没有后台轮询,也不会静默安装。

```text
UPDATE
  → GET api.github.com/repos/heiheiheiheiheihei/rf-lab/releases?per_page=30
     (APK/SUMS 走同一个 API 的 asset 端点,Accept: application/octet-stream)
  → 只留 draft=false / prerelease=false / tag 匹配 watch-vX.Y.Z
     且同时带 rf-lab-watch.apk + SHA256SUMS.txt 的 Release,取最高版本
  → 下载 APK(≤20 MB)→ 大小、SHA-256、包名、versionCode/versionName
  → 签名证书必须与已装 APP 一致
  → ACTION_VIEW(content://)+只读 provider → 系统安装界面 → 安装
```

| 项 | 约定 |
|---|---|
| 网络 | 仅 HTTPS;仓库与资产名写死,不接受服务端下发的下载地址 |
| 下载路径 | 用 GitHub API asset 端点(302 到 release CDN)。实测经手机蓝牙代理时 `github.com` 的 TLS 握手会超时,`api.github.com` 正常 |
| 触发 | 只由 SYSTEM → `UPDATE` 触发;无定时器、无开机自启 |
| 版本门 | `versionCode` 和 `versionName` 都必须严格前进,同版本重装会被拒绝 |
| 完整性 | 资产大小 + `SHA256SUMS.txt` 的 SHA-256;不一致立即失败 |
| 签名 | 证书 SHA-256 必须与已装包一致;系统安装器还会再验一次 |
| 安装 | `ACTION_VIEW` + 只读 `content://` provider,由系统安装界面确认;首次需允许“安装未知应用”(API 26+)。本机实测 `PackageInstaller` 会话在该 ROM 上直接返回 `STATUS_FAILURE`,故不用会话路径 |
| 失败 | 状态行短码:`NO NET` / `RATE LIMIT` / `BAD HASH` / `BAD SIGN` / `NOT NEWER` / `BAD APK` / `INSTALL FAILED`;详情 `adb logcat -s RF-Lab.Watch.Update` |

**未授权安装来源**时 APK 仍会下载并校验,APP 打开系统“安装未知应用”页,状态行提示
`ALLOW INSTALL SOURCE`;授权后回到 APP 再点一次 `UPDATE`。

**边界**:自更新只替换手表 APK,不更新设备固件,也不改变 BLE PIN/码库;它没有
secure boot 或签名链,只保证“新包与已装包同证书”。手表必须能自己上网(通常连 Wi-Fi);
没有网络时不会回退到手机代理。

**2026-11-22 真机状态**:`PackageInstaller` 会话在 OW20W1 上直接返回
`STATUS_FAILURE`,无安装界面;改用 `ACTION_VIEW` + 只读 `content://` provider 后,
系统安装界面尚未完成验证(设备断开)。0.1.4 因此标记为 **pre-release**,官网仍指向
0.1.0;重新接上手表后先验证安装界面,再决定是否转为稳定版。

## 9. ADB 上机步骤(设备接入后执行)

手表侧准备(本机 `OW20W1` 已通过 ADB 连接;具体菜单路径以实机固件为准):

1. 设置 → 其他设置 → 关于手表 → 连点“版本号”7 次,开开发者模式;
2. 其他设置 → 开发者选项 → 打开“ADB 调试”/“USB 调试”;
3. 若只有“WLAN 调试”:手表与电脑同一网段,`adb connect <手表IP>:5555`;
4. 插线/连接后,手表弹出授权框时选“允许”。

> 部分 OPPO Watch 固件只开 OTG 或隐藏 ADB 入口;若 `adb devices` 一直为空,
> 先记录手表型号/系统版本,不要反复重装 APK。若显示 `no permissions`,是主机 udev
> 问题(本机已为 `18d1`/`22d9` 加规则);若是 `unauthorized`,在手表上点“允许”。

```bash
adb devices -l
adb shell getprop ro.build.version.sdk
adb shell wm size
adb shell wm density
# 公开包(推荐)
adb install -r rf-lab-watch.apk
# 或本地构建产物
adb install -r watch/app/build/outputs/apk/release/app-release.apk
adb shell am start -n dev.rflab.watch/.WatchActivity
adb exec-out screencap -p > /tmp/rflab-watch.png
```

调试开关(仅 debuggable 包):

```bash
# 无 BLE 环境预览(假数据,不连设备;仅 debuggable 包)
adb shell am start -n dev.rflab.watch/.WatchActivity --ez demo true
adb shell am start -n dev.rflab.watch/.WatchActivity --ez demo true --ez demo_pin true
adb shell am start -n dev.rflab.watch/.WatchActivity --ez demo true --ez demo_link true
adb shell am start -n dev.rflab.watch/.WatchActivity --ez demo true --ez demo_many true
adb shell am start -n dev.rflab.watch/.WatchActivity --ez demo true --ez demo_tx true
```

验收顺序:

1. `adb devices` + API/分辨率/密度记录;
2. 安装/启动/截图,确认 402×476 或 372×430 无裁切;
3. 扫描到 `RF-Lab-*`,连接,错误 PIN / 正确 PIN;
4. `SEND` 一次,设备 OLED 或串口出现 `OK tx ... (0xF0)`;
5. 按住 `HOLD` → `LONG`,松开 → `STOP`;观察设备 `tx.active`;
6. 长发时关屏/退出/断链,确认 STOP 或红色断链告警;
7. 记录到 `docs/evidence/YYYY-MM-DD-watch.md`。

## 10. 验收与发布范围

| ID | 项 | 通过标准 | 状态 |
|---|---|---|---|
| WATCH-01 | 构建/签名 | debug 可构建;release 用独立 key,v2 验签通过 | ✅ debug 103,784B;公开资产 48,244B/sha256 `739afa…`;v2;Lint 0 |
| WATCH-02 | 主机测试 | 协议解析/选择逻辑全过 | ✅ 2026-11-22,`ProtocolTest` 9/9 |
| WATCH-03 | 安装/启动 | 手表 `adb install` 成功;冷启动无崩溃 | ✅ 2026-11-22,OW20W1 / API 27 |
| WATCH-04 | 屏幕适配 | 46mm 402×476 无裁切;按钮命中正确 | ✅ 46mm 真机 OW20W1 |
| WATCH-05 | BLE 控制 | 扫描/连接/AUTH/`SEND`/`LONG`/`STOP` 与设备回显一致 | ✅ 46mm 真机:32 码、`OK TX`、`STOP`;错误 PIN `4 LEFT` |
| WATCH-06 | 失败关闭 | 错误 PIN、退后台 STOP 均不发飞 | ✅ 错误 PIN `4 LEFT`;退后台 `STOP #1` |
| WATCH-07 | APP 自更新 | 真机点 UPDATE:发现新版→下载→四项校验→系统确认→版本前进;取消/断网失败关闭 | ⏳ 0.1.4 下载/校验真机已验;系统安装界面未验(设备断开);0.1.4 为 pre-release |

构建/主机/模拟器证据:[`docs/evidence/2026-11-22-watch.md`](../docs/evidence/2026-11-22-watch.md)。
真机与 release 签名/公开包证据:[`docs/evidence/2026-11-22-watch-release.md`](../docs/evidence/2026-11-22-watch-release.md)。

**发布范围**:0.1.0 面向 46mm `OW20W1`;长时间长发/息屏省电策略、32 条逐条发射
不属于本次发布门槛。

外部资料:
- [OPPO Watch 2 46mm 规格](https://www.oppo.com/cn/accessories/oppo-watch-2/specs/46mm/)
- [OPPO Watch 2 42mm 规格](https://www.oppo.com/cn/accessories/oppo-watch-2/specs/42mm/)
- [OPPO Watch 2 发布报道(Android 8.1 / Wear 4100)](https://m.chinaz.com/2021/0529/1257582.shtml)
- [Wear OS Wi-Fi 调试(通用 ADB 流程)](https://developer.android.com/training/wearables/get-started/debug-wifi)

## 11. 关键判断与避免的误修

| 判断 | 为什么 | 避免的误修 |
|---|---|---|
| 单码卡片,不做码库表格 | 180–200dp 宽放不下四列;手表操作要一次看懂 | 把手机 WebView 直接塞进手表,再靠缩放/滚动 |
| 按住 HOLD 才发长发,松开即停 | 长发没有天然超时;手表易误触/离开 | 做一个“持久 LONG”按钮,再把 STOP 藏进二级页 |
| PIN 自动提交 + 记住 | 4 位数字在手表上输入成本高 | 每次连接都让用户在小键盘上敲 4 位 |
| ARM 保留但只做防误触 | 与 Web/手机语义一致;避免口袋误发 | 为了少一次点击去掉 ARM,或把它写成安全门 |
| 不复制 `SEND/LONG/STOP` 时序 | 固件是射频行为的唯一所有者 | 在手表里自己拼 RF 帧/重试时序 |
| 不做后台 Service | 手表续航/系统限制;长发必须在前台可控 | 用前台服务保活,结果用户看不见发射状态 |
