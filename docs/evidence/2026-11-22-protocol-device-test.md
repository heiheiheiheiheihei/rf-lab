# 2026-11-22 协议固件上机:身份去重 + DEDUP + 越界拒绝

状态:`measured` · **真机串口实测** · **未发 RF**
提交:`200cc4e feat(codebook): osc is not identity; dedupe on addr+button, drop invalid frames`
设备:`/dev/ttyACM0` · 板 `RF-Lab-1B59`(ESP32-C3 + YS-RF34DL)
依据:[`00-mvp.md`](../00-mvp.md) §5.2 的模块协议(2026-11-22 确认)。

## 1. Build

```bash
# 提交后、干净 firmware/src 上构建;因本机未装 7.1.3 framework,临时用已装 dev 平台
./tools/flash.sh /dev/ttyACM0
```

| 项 | 值 |
|---|---|
| 提交 | `200cc4e`(`build=` 无 `+dirty`) |
| 镜像 | **1,099,744 B**;sha256 `01565d35dc6dd4792de670b223271b721ba2f101e4c681e838cb912bc4be2574` |
| 烧录 | bootloader + 双 OTA 分区表 + app → `ota_0`,擦 `otadata`;NVS 保留 |
| 设备自报 | `fw=0.7.1` `build=200cc4e` `ui=85a1a9b4` `ota slot=0` `pending=0` |

> 设备跑的是 `200cc4e`(master 提交,不是 tag);本地镜像是 dev 平台构建,
> CI 的 7.1.3 构建要等这轮改动推送后才验证(本文件 §5)。NVS 未擦,码库保留 32 条。

## 2. Host

见 [`2026-11-22-codebook-protocol.md`](2026-11-22-codebook-protocol.md) §4:
`host_test.sh` **11,068**(码库 **235**)+ TUI 12 + selftest 18,`SAN=1` 全过。

## 3. Device(原始转录)

转录:[`2026-11-22-protocol-device.log`](2026-11-22-protocol-device.log)(**已脱敏**)
自检:[`2026-11-22-protocol-selftest.log`](2026-11-22-protocol-selftest.log) /
[`.json`](2026-11-22-protocol-selftest.json)

| 步骤 | 结果 |
|---|---|
| `INFO`(烧录后) | `build=200cc4e`、`ota slot=0`、`codebook count=32/32`、`bad=0` |
| `LIST LONG` | 每条显示 `osc=0xNN`,如 `1 315M 5555033C osc=0x3C name="-" …` |
| `DEDUP`(第一次) | **`OK dedup removed=11 count=21`** —— 与预测一致(32→21) |
| `DEDUP`(第二次) | `OK dedup removed=0 count=21`(幂等) |
| `NEW 315M 12121260` | `OK new id=22 …` |
| `EDIT 22 code 12121270` | **`OK edit id=22 code=12121270`**;`LIST LONG` 显示 `osc=0x70 … manual edited`(只改 osc 不新建) |
| `ERASE 22` | `OK erased id=22 (21 left)` |
| `NEW 433M D0C70146` | `ERR new: dup (same band+addr+button; EDIT <id> code <8hex> to change osc)` |
| `NEW 433M D0C70160` | 同上 **DUP** —— 同身份、只换 osc 被正确拒绝 |
| `NEW 433M 00000093` | **`ERR new: code`** —— 地址/按键为 0,越界拒绝 |
| 自检 `device_selftest --json` | **26 pass / 0 fail / 1 skip**,`ok=true`;**`stored=21/32`** |

`INFO`(DEDUP 之后节选):

```text
# RF-Lab fw=0.7.1 (ESP32-C3 + YS-RF34DL)
# build=200cc4e ui=85a1a9b4
# codebook ver=2 count=21/32 migrated=no
OK stored=21/32 rx_bytes=0 frames=0 acks=0 dups=0 bad=0 stray=0 resync=0
```

> 30 分钟前那批 32 条码并不需要「轮发不同 osc」;保留每个键最早一条后剩 21 条,
> 11 条纯 osc 重复被合并。`bad=0` 只是因为本轮**没有接收**任何帧,不能据此判断
> 噪声过滤效果。

## 4. 未验证

1. **空口**:未加 `--tx`,`SEND/LONG/STOP` 未执行;RF-01/02 仍待测。
2. **越界帧实时过滤**:没有在接收路径制造噪声/坏帧,`bad` 计数与 1 秒限速日志未实测。
3. **带名称的 DEDUP**:现有 32 条 name/group/sym 全是 `-`;合并时保留「最早一条」的
   元数据,这一路径没有用真实命名数据验证。
4. **CI / 发版**:本提交是否推送、是否走 0.7.2 由用户决定;CI 的 7.1.3 构建未在此提交上跑过。
5. **设备与 tag**:设备跑 master 提交 `200cc4e`,不是 tag;REL-01 仍未关闭。
