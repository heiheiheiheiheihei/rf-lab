# 2026-11-22 — OPPO Watch 2 真机验证(RF-Lab Watch)

> **结论:46mm OW20W1 上安装、启动、BLE 扫描/连接/AUTH、32 条码库、ARM→SEND、
> HOLD→LONG→STOP、错误 PIN、退后台 STOP、OLED TEST 均通过。** 未接射频接收器,
> 只证明设备回 `ACK 0xF0` / BLE 状态,不证明空口被外部接收器收到。
> 之后的独立 release key 签名/重装见
> [`2026-11-22-watch-release.md`](2026-11-22-watch-release.md)。

## Device

| 项 | 值 |
|---|---|
| 型号 | `OW20W1`(OPPO Watch 2 46mm) |
| 系统 | Android 8.1.0 / API 27,ColorOS Watch |
| 屏幕 | 402×476 @ 320dpi(实机 `wm size` / `wm density`) |
| 序列号 | `a6c55854` |
| 安装 | `adb install -r watch/app/build/outputs/apk/debug/app-debug.apk` → `Success` |
| 包 | `dev.rflab.watch` 0.1.0(versionCode 1),minSdk 26 / targetSdk 28 |
| 首次安装 / 最后更新 | 2026-11-22 15:34:43 / 15:42:42 |
| 权限 | `ACCESS_FINE_LOCATION: granted=true`;蓝牙为安装时权限 |

ADB 连接前主机 `adb devices` 报 `no permissions`;设备是 Qualcomm `18d1:4ee2`
(`SDM429W-QRD`)。已加 udev 规则(设备 owner 为当前用户)并授权 USB 调试。
**这是主机侧修复,不改变 APK 权限模型。**

## BLE

| 步 | 结果 | 证据 |
|---|---|---|
| 扫描 | 找到 `RF-Lab-1B59`,-51…-53 dBm | [01-link-found](2026-11-22-watch-device/01-link-found.png) |
| 连接 | 进入 PAIR 键盘,设备名 `RF-Lab-1B59` | [02-pin](2026-11-22-watch-device/02-pin.png) |
| 错误 PIN | 输入 `0000` → 红字 `WRONG PIN · 4 LEFT` | [14-wrong-pin](2026-11-22-watch-device/14-wrong-pin.png) |
| 正确 PIN | 自动提交 → `ONLINE`,32 条码,`CODE 01 / 315M / 5555033C` | [03-control-safe](2026-11-22-watch-device/03-control-safe.png) |
| 重连 | 设备/PIN 记忆生效;冷启动自动连接并 AUTH,无需再输 PIN | [13-auto-connect](2026-11-22-watch-device/13-auto-connect.png) |
| 码库 | 32 条全量快照,列表可滚动 | [08-codebook](2026-11-22-watch-device/08-codebook.png) |
| OLED | 菜单 `OLED TEST 10s` → `OK disp=test 10s` | [12-oled-test](2026-11-22-watch-device/12-oled-test.png) |

**PIN 不在此记录**;来自串口 `PIN`(设备 OLED 开机也显示 5s)。BLE 仍是应用层 PIN,
不是链路加密。

## 发射

| 动作 | 手表显示 | 设备侧观察 |
|---|---|---|
| ARM | `ARMED` / SEND 可用 | — |
| SEND #1 | `OK TX #1 · 654ms`(另一次 `740ms`) | 串口 `ACK 0xF0 TX_OK`、`OK send id=1 band=315M code=5555033C (0xF0, 687ms)` |
| HOLD #1 | 按住后 `HOLDING #1 · RELEASE TO STOP`;SEND 变 `BUSY` | — |
| 松开 | `STOP #1 · 34ms`(另一次 `39ms`) | 设备状态回到非 active |
| 退后台 | 按住时按 HOME,回前台 `STOP #1 · 58ms`,无残留 STOP 按钮 | 退后台路径发 STOP |

截图:

- [04-armed](2026-11-22-watch-device/04-armed.png)
- [05-send-ok](2026-11-22-watch-device/05-send-ok.png)
- [06-hold](2026-11-22-watch-device/06-hold.png)
- [07-stop](2026-11-22-watch-device/07-stop.png)
- [15-pause-stop](2026-11-22-watch-device/15-pause-stop.png)

> 串口原始 `OK send` 是在同一 BLE 命令路径上抓到的;打开串口会拉 DTR/RTS 复位 C3,
> 所以后续改用设备回传的 BLE 状态/日志,不再在发射时打开串口。

## 断链与空态

- 菜单 `DISCONNECT` 后回 LINK,显示 `DISCONNECTED`、上次设备 `LAST RF-Lab-1B59`、
  空态 `NO DEVICE · TAP LAST TO RECONNECT`,无文字重叠。
  [10-link-empty](2026-11-22-watch-device/10-link-empty.png)
- 点 LAST 重连自动 AUTH,回 CONTROL,ARM 复位为 SAFE。
  [11-reconnect](2026-11-22-watch-device/11-reconnect.png)
- 菜单布局在 402×476 实机完整显示。
  [09-system](2026-11-22-watch-device/09-system.png)

## Build / Host(最终包)

```bash
./watch/check.sh
```

| 项 | 值 |
|---|---|
| APK | 103,784 B |
| sha256 | `7ca5452c72cd24a5bef9f1fe3f30009d8ec22f99c9aac22aed1cb148e574596a` |
| JVM 测试 | `ProtocolTest` 9/9 |
| Lint | 0 error / 0 warning |

## 未验证

- **断链长发**:按住 HOLD 时 BLE 断开 → 红色 `TX MAY CONTINUE` + 自动重连后 STOP;
  本机未稳定复现(串口复位会同时打断触摸注入),未写成通过;
- **5 次错误 PIN 锁定**:只测了 1 次错误(`4 LEFT`),未打满 5 次;
- **42mm 实机**:本机是 46mm OW20W1;42mm 只有模拟器 372×430;
- **外部接收器**:没有接收器/学习遥控器观察空口,只有设备 `ACK 0xF0` 与状态;
- 长时间长发、息屏/省电策略、连续 32 条逐条发射、OLED `DISP OFF` 后恢复;
- PIN 明文保存在 APP 私有目录的安全接受度(功能上已验证,安全边界未评审)。
