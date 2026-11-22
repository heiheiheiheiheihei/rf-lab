# RF-Lab Watch v0.1.0

OPPO Watch 2 的 RF-Lab 控制客户端。只走 BLE,复用设备的 GATT / `\n` 行协议,
不改固件、不改手机 APP。

## 下载

- 固定资产:`rf-lab-watch.apk`
- 版本资产:`rf-lab-watch-0.1.0.apk`
- 校验:`SHA256SUMS.txt`

## 能做什么

- 扫描 `RF-Lab-*`、连接、4 位 PIN 鉴权,记住上次设备/PIN 自动重连;
- 32 条码库全量快照;上下滑换码 / 点卡打开码库;
- ARM 防误触 → `SEND` 单发;
- 按住 `HOLD` 350ms 发 `LONG`,松手/滑出/退后台立即 `STOP`;
- OLED TEST 10s。

## 安装

```bash
adb install -r rf-lab-watch.apk
```

- 包名:`dev.rflab.watch`;minSdk 26 / targetSdk 28(目标 ColorOS Watch 2.0 / API 27)。
- 首次启动授予定位权限(Android 8.1 的 BLE 扫描要求)。
- 设备 PIN 看手表 OLED 开机页 5s,或串口 `PIN`。
- 这是**独立 release key** 签名,不与手机 APK 共用;debug 包不能覆盖升级。

## 边界

- 只做控制;码库编辑/新建/删除、Wi-Fi 配网、固件 OTA 仍走手机/Web/串口。
- PIN 是应用层鉴权,BLE 链路不加密;ARM 只是防误触。
- 目标设备为 OPPO Watch 2 46mm(`OW20W1`,402×476 / API 27),已上机验证。
