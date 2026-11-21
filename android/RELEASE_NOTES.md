## 下载

| 文件 | 说明 |
|---|---|
| `rf-lab-android.apk` | 稳定文件名，官网按钮指向 `releases/latest/download/rf-lab-android.apk` |
| `rf-lab-0.6.0.apk` | 版本归档副本 |
| `SHA256SUMS.txt` | 上述 APK 的 SHA-256 |

要求 **Android 8.0（API 26）及以上**。APK 使用本仓库的 release 密钥签名（APK Signature Scheme v2），证书 SHA-256：

```
B8:CC:EA:6C:1B:2F:BA:85:BD:BF:59:DA:DA:0D:21:BF:98:7F:60:DD:97:70:3C:0C:88:21:BB:63:D8:AB:0D:EA
```

> 与之前侧载的 debug 签名 APK **签名不同**，覆盖安装会失败；先卸载旧版再装。

## 安装与连接

1. 下载 APK，允许「安装未知来源」后安装。
2. 首次打开授予蓝牙权限（Android 12+ 为 `BLUETOOTH_SCAN` / `BLUETOOTH_CONNECT`；≤11 为蓝牙 + 精确定位）。
3. 设备上电，APP 扫描 `RF-Lab-*`；PIN 见设备 OLED 开机页 5 秒或串口 `PIN`。
4. APP 只走 BLE，不发 HTTP；Web 版才走 Wi-Fi。

## 已验证 / 未验证

| 项 | 状态 |
|---|---|
| `assembleRelease` + release 签名 | ✅ 构建机验证（`apksigner verify` 通过） |
| 包名/版本/权限/Web 资源内嵌 | ✅ `aapt2 dump badging` 检查 |
| 真机安装与启动 | ⏳ **未验证**（构建环境无 Android 手机） |
| 真机 BLE 扫描/连接/鉴权/发射 | ⏳ **未验证**（构建环境无蓝牙适配器） |
| Android 15 edge-to-edge 视觉 | ⏳ 未验证 |

构建成功不等于链路验证。设备端 BLE 只有 4 位应用层 PIN，Web 的 `ARM` 只防误触，都不是安全边界。

## 安全边界

固定码可被复制：只对自有设备测试；确认当地频段与功率法规后再发射。
