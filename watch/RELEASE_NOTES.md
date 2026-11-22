# RF-Lab Watch v0.1.2

OPPO Watch 2 的 RF-Lab 控制客户端。只走 BLE,复用设备的 GATT / `\n` 行协议,
不改固件、不改手机 APP。

## 新增

- **手表端 APP 自更新**:SYSTEM → `UPDATE`,检查 GitHub Release → 下载 APK →
  校验 SHA-256 / 包名 / 版本 / 签名 → 交给系统安装器确认。手动触发,不后台轮询,
  不静默安装。
- SYSTEM 菜单改为 2×2 网格 + 整行 FORGET,五个入口仍保持大点击区。
- **LINK 页也能打开 SYSTEM**,未连接 RF-Lab 设备时仍可检查更新。
- 修复 2×2 菜单改造后“行号≠动作号”导致点 UPDATE 触发 FORGET 的问题。

## 下载

- 固定资产:`rf-lab-watch.apk`
- 版本资产:`rf-lab-watch-0.1.2.apk`
- 校验:`SHA256SUMS.txt`

## 安装 / 升级

```bash
adb install -r rf-lab-watch.apk
```

- 0.1.0 用户:**0.1.0 没有自更新**,需要手动装一次 0.1.2。
- 0.1.2 起:手表上 SYSTEM → `UPDATE`;下载完成后由系统弹安装确认。
- 手表需要自己能上网(本机用“通过蓝牙使用手机的网络”已验证;Wi-Fi 亦可)。
- 包名:`dev.rflab.watch`;minSdk 26 / targetSdk 28(目标 ColorOS Watch 2.0 / API 27)。
- 这是**独立 release key** 签名,不与手机 APK 共用;debug 包不能覆盖升级。

## 边界

- 只做控制与 APP 自更新;码库编辑/新建/删除、Wi-Fi 配网、固件 OTA 仍走手机/Web/串口。
- 自更新只更新手表 APK,不更新设备固件;没有 secure boot/签名链,只保证新包与
  已装包同证书。
- PIN 是应用层鉴权,BLE 链路不加密;ARM 只是防误触。
- 目标设备为 OPPO Watch 2 46mm(`OW20W1`,402×476 / API 27),已上机验证。
