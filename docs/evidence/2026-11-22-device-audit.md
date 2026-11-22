# 2026-11-22 只读巡检:设备固件 / 码库 / UART 命令方向

状态:`measured` · **只读查询 + 一条非射频配置命令 `MODE single`** · **未发 RF**
设备:`/dev/ttyACM0` 115200 8N1 · 板 `RF-Lab-1B59`(ESP32-C3 + YS-RF34DL)
目的:确认 [`2026-11-21-send-no-ack.md`](2026-11-21-send-no-ack.md) 之后,当前设备是否仍处于
「ESP→模块命令方向 0 应答」;为 0.7.2 的测试计划提供当前基线。

> 本次**没有执行 `SEND` / `LONG` / `STOP`**,没有发射任何 315/433 信号。
> 因此本文只能证明 UART 命令方向,不证明空口发射可用。
>
> 本次执行了 `MODE single`(UART,不发射),它会把模块输出模式设为 single;
> 若原为 continuous,需要手动 `MODE continuous` 切回。

## 1. 查询

```bash
# 只读:INFO / LIST / STATE
# 非射频配置:MODE single(只走 UART,不发射)
```

设备启动后约 31,840s(8.8h)时查询。

## 2. 结果

### 2.1 INFO(节选)

```text
# RF-Lab fw=0.7.1 (ESP32-C3 + YS-RF34DL)
# build=511afca+dirty ui=85a1a9b4
# ota slot=0 next=-1 state=idle pct=0 pending=0
# module UART1 9600 8N1 TX=GPIO3 RX=GPIO4
# oled 72x40 @0x3C i2c400000 sda=5 scl=6 present=yes on=yes flush=121 err=0 last_us=8719 max_us=9643
# net mode=sta ap_on=0 has_creds=1 ap=RF-Lab-1B59 ap_ip=192.168.4.1 clients=0 sta=Mark sta_ip=192.168.5.125 rssi=-37
# ble on=on adv=yes conn=no name=RF-Lab-1B59 pin=set
# codebook ver=2 count=13/32 migrated=no
OK stored=13/32 rx_bytes=928 frames=116 acks=0 dups=0 stray=0 resync=0
```

### 2.2 LIST

```text
 1  315M 5555034C
 2  315M 5555033C
 3  315M 5555034B
 4  433M 5555034B
 5  433M 5555033C
 6  433M 1555304B
 7  433M 00000092
 8  315M 5555034D
 9  433M 5555034D
10  433M 00000093
11  433M 00000094
12  433M 00000095
13  433M 00000053
```

### 2.3 命令方向探针

```text
$ MODE single
ACK 0xF2 OUTPUT_SINGLE_OK
OK mode=single
```

随后 `INFO`:

```text
OK stored=13/32 rx_bytes=929 frames=116 acks=1 dups=0 stray=0 resync=0
```

## 3. 判读

| 结论 | 依据 | 边界 |
|---|---|---|
| ESP→模块 UART 命令方向**当前可用** | `MODE single` 收到 `0xF2`,`acks` 0→1 | 只证明 UART,不证明 RF 发射 |
| 2026-11-21 的「命令方向 0 应答」**不是当前状态** | 同上;可能是冷启动/重焊后恢复,也可能是间歇故障 | 不能据此判定已修好 |
| 空口发射**仍未验证** | 本次刻意未发 `SEND/LONG/STOP` | 需要自有接收器/学习遥控器观察 |
| 设备固件**不是 v0.7.1 tag 构建** | `build=511afca+dirty`;v0.7.1 tag 指向 `a7b4bf3` | dirty 构建的通过不能写成发布固件通过 |
| 码库 13 条,不是 README 记录的 9 条 | `LIST` 10–13 为 `433M 00000093/94/95/53` | 与 2026-11-15 的两条 433M 异常同族;是否自动学码误收待查 |

## 4. 复现

```bash
python3 - <<'PY'
import serial, time
s = serial.Serial('/dev/ttyACM0', 115200, timeout=0.3)
time.sleep(0.3); s.write(b'\r\n'); time.sleep(0.3); s.reset_input_buffer()
for cmd in ('INFO', 'LIST', 'STATE'):
    s.write((cmd + '\r\n').encode()); time.sleep(1.0)
    print(s.read(65536).decode('utf-8', 'replace'))
# 只探命令方向,不发射:
s.write(b'MODE single\r\n'); time.sleep(1.5)
print(s.read(4096).decode('utf-8', 'replace'))
s.close()
PY
```

## 5. 未验证 / 下一步

1. **P0**:在自有接收器/学习遥控器旁执行 `SEND 1` / `LONG 1` / `STOP 1`,确认 `F0/FE/FF`
   与接收器动作;冷启动后连续 10 次 `SEND`,记录失败曲线。
2. **P0**:用 v0.7.1 tag(`a7b4bf3`)重新构建并烧录,确认 `build=` 不再带 `dirty`,
   再复测上述射频项。
3. **P1**:确认 10–13 号 433M 码的来源;若是模块/环境噪声被自动学码收进来,
   需要产品层过滤或至少可见告警。
4. BLE `SEND`、OTA、uConsole 未在本次覆盖。
