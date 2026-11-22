# 2026-11-22 固件烧录:master `dd71bf6` → 设备

状态:`measured` · **USB 全量烧录(`tools/flash.sh`)** · **未发 RF**
设备:`/dev/ttyACM0` · 板 `RF-Lab-1B59`(ESP32-C3 + YS-RF34DL)
目的:把设备从 `511afca+dirty` 换成**可追溯的 HEAD 构建**,给 REL-01
([`../02-plan.md`](../02-plan.md) §3.1)留下 `build=` / `ui=` / sha256。

> 本次**没有执行 `SEND` / `LONG` / `STOP`**,没有发射任何 315/433 信号;
> 空口发射是否恢复仍按 RF-01 单独验。也没有走 OTA(本次是 USB 全量)。

## 1. 命令

```bash
./tools/flash.sh /dev/ttyACM0
```

脚本先 `pio run -e esp32c3`(含 `gen_wiring.py --check`、`build_web.py`、
`check_partitions.py`),再写 `0x0` bootloader + `0x8000` 双 OTA 分区表 +
`0x20000` app,最后擦 `otadata`(0x10000/0x2000)。NVS 0x9000 不碰。

## 2. 烧录结果

| 项 | 值 |
|---|---|
| 构建结论 | `pio run -e esp32c3` 成功;镜像 **1,098,608 B** |
| sha256 | `43e9863ef03633c4393be9891136a90cc87edd42fef925274eea4b4349213679` |
| 写入 | `0x0` bootloader / `0x8000` partitions / `0x20000` app(压缩 705,749 B,8.9 s,988 kbit/s) |
| 校验 | `Hash of data verified.` |
| otadata | 擦除成功(空白表 → 启动 `ota_0`) |

## 3. 烧录前后对比

| 项 | 烧录前 | 烧录后 |
|---|---|---|
| `build=` | `511afca+dirty` | **`dd71bf6`**(工作树 `web/`/`firmware/src/` 干净,无 `+dirty`) |
| `ui=` | `85a1a9b4` | `85a1a9b4`(UI 未变) |
| `fw=` | 0.7.1 | 0.7.1 |
| 运行槽 | `ota slot=0` | `ota slot=0`,`next=-1`,`pending=0` |
| 码库 | 13/32 | **13/32**(NVS 保留) |
| Wi-Fi | `mode=sta 192.168.5.125 rssi=-39` | `mode=sta 192.168.5.125 rssi=-40`(凭据保留,自动回连) |
| BLE | `adv=yes pin=set` | `adv=yes pin=set` |
| OLED | `present=yes err=0` | `present=yes err=0`(`flush=2`) |
| UART 命令方向 | `acks=1`(11-22 巡检) | `MODE single` → **`ACK 0xF2 OUTPUT_SINGLE_OK`**,`acks` 0→1 |

烧录后 `INFO` 节选:

```text
# RF-Lab fw=0.7.1 (ESP32-C3 + YS-RF34DL)
# build=dd71bf6 ui=85a1a9b4
# ota slot=0 next=-1 state=idle pct=0 pending=0
# module UART1 9600 8N1 TX=GPIO3 RX=GPIO4
# oled 72x40 @0x3C i2c400000 sda=5 scl=6 present=yes on=yes flush=2 err=0 last_us=8723 max_us=8772
# net mode=sta ap_on=0 has_creds=1 ap=RF-Lab-1B59 ap_ip=192.168.4.1 clients=0 sta=Mark sta_ip=192.168.5.125 rssi=-40
# ble on=on adv=yes conn=no name=RF-Lab-1B59 pin=set
# codebook ver=2 count=13/32 migrated=no
```

```text
> MODE single
ACK 0xF2 OUTPUT_SINGLE_OK
OK mode=single
OK stored=13/32 rx_bytes=1 frames=0 acks=1 dups=0 stray=0 resync=0
```

## 4. 边界

1. `dd71bf6` 是 **master HEAD**(`v0.7.1-4-gdd71bf6`),不是 `v0.7.1` tag 本身;
   Release 里仍**没有** `firmware.bin` + `SHA256SUMS.txt` 资产,REL-01 只关掉了
   「设备跑 dirty 构建」这一半,「发布固件可下载」仍未关。
2. 本次只证明**构建、烧录、启动、NVS 保留、UART 命令方向**;`SEND/LONG/STOP`
   空口、BLE `SEND`、OTA、uConsole 均未复测,不能写成「已通过」。
3. `MODE single` 会把模块输出模式设为 single(与巡检时一致);原为 continuous
   才需手动切回。
