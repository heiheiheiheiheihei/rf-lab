# RF-Lab Android 0.7.0 — 自动检查更新

本次只动 Android 端：装一次之后，APP 自己查新版本，不再需要每次手动回来下载。

## 新功能

- **自动检查**：冷启动/回前台时查 GitHub Releases（6 小时节流，失败静默）。
- **一键更新**：弹窗 → 下载 → 校验 → 交给系统安装器，用户在系统弹窗确认。
- **忽略此版本**：该版本不再提示，下一个版本重新提示。
- **三道校验后才安装**：Release 的 `SHA256SUMS.txt`、包名/`versionCode`/`versionName` 必须前进、
  签名证书必须与已装 APP 一致；系统安装时还会再验一次签名。

## 下载

| 文件 | 说明 |
|---|---|
| `rf-lab-android.apk` | 稳定文件名，官网按钮指向 `releases/latest/download/rf-lab-android.apk` |
| `rf-lab-0.7.0.apk` | 版本归档副本 |
| `SHA256SUMS.txt` | 上述 APK 的 SHA-256（APP 自更新也读这个文件） |

要求 **Android 8.0（API 26）及以上**。APK 使用本项目的 release 密钥签名（APK Signature Scheme v2），证书 SHA-256：

```
B8:CC:EA:6C:1B:2F:BA:85:BD:BF:59:DA:DA:0D:21:BF:98:7F:60:DD:97:70:3C:0C:88:21:BB:63:D8:AB:0D:EA
```

> 与之前侧载的 debug 签名 APK **签名不同**，覆盖安装会失败；先卸载旧版再装。
> 0.6.0（release 签名）可以直接覆盖安装到 0.7.0；0.6.0 本身没有自更新，需手动装一次 0.7.0。

## 安装与连接

1. 下载 APK，允许「安装未知来源」后安装。
2. 首次打开授予蓝牙权限（Android 12+ 为 `BLUETOOTH_SCAN` / `BLUETOOTH_CONNECT`；≤11 为蓝牙 + 精确定位）。
3. 设备上电，APP 扫描 `RF-Lab-*`；PIN 见设备 OLED 开机页 5 秒或串口 `PIN`。
4. 控制链路只走 BLE；APP 唯一用 HTTP 的地方是自更新（只读 GitHub Releases，不连设备）。

## 已验证 / 未验证

| 项 | 状态 |
|---|---|
| `assembleRelease` + release 签名 | ✅ 构建机验证（1,027,080 B，`apksigner verify` v2 通过） |
| 包名/版本/权限/Web 资源内嵌 | ✅ `aapt2 dump badging`：`2 / 0.7.0`，INTERNET + REQUEST_INSTALL_PACKAGES |
| 自更新逻辑（版本比较 + SUMS 查表） | ✅ 主机测试 36 checks；真实 Release 资产校验一致 |
| 真机：更新弹窗/下载/系统安装器 | ⏳ **未验证**（构建环境无 Android 手机） |
| 真机安装与启动 | ⏳ **未验证**（构建环境无 Android 手机） |
| 真机 BLE 扫描/连接/鉴权/发射 | ⏳ **未验证**（构建环境无蓝牙适配器） |
| Android 15 edge-to-edge 视觉 | ⏳ 未验证 |

构建成功不等于链路验证。设备端 BLE 只有 4 位应用层 PIN，Web 的 `ARM` 只防误触，都不是安全边界。

## 安全边界

固定码可被复制：只对自有设备测试；确认当地频段与功率法规后再发射。
