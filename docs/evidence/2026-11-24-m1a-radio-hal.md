# 2026-11-24 · 0.8.0 M1a:radio 门面 + YS 发射后端

状态:**Host/Build 通过 · Device 未测** · 基线 `HEAD be22aeb` + 工作区改动(uncommitted)
关联:[ADR-001](../adr/ADR-001-radio-hal.md)、[`02-plan.md`](../02-plan.md) §5.3.3

## 1. 范围

把 `SEND/LONG/STOP` 的组帧与应答语义从 `main.c` 抽到 `radio` 门面 + `radio_ys` 后端,
命令输出格式与串口帧保持逐字节一致。**只做发射(M1a)**;接收解析仍在 `main.c`(M1b)。

新增/改动:

| 文件 | 作用 |
|---|---|
| `firmware/src/radio.h` / `radio.c` | 能力位、状态、错误码、门面分发(纯 C) |
| `firmware/src/radio_ys.h` / `radio_ys.c` | YS 组帧(F0/FE/FF)+ 写 UART + 等应答;IO 注入 |
| `firmware/src/main.c` | `radio_setup()` 接线;`do_tx()` 改走门面 |
| `firmware/test/host/test_radio.c` | 114 checks:能力位/状态/BUSY/帧一致/超时 |
| `tools/host_test.sh` | 新增 radio 套件 |

## 2. Host

```bash
./tools/host_test.sh        # 11,312 checks
SAN=1 ./tools/host_test.sh  # 同上,含 ASan/UBSan
```

| 套件 | checks |
|---|---:|
| 协议 rf34dl | 76 |
| 码库 codebook | 368 |
| 事件环 link | 9,427 |
| OTA ticket | 41 |
| **radio(新增)** | **114** |
| OLED/UI | 1,286 |
| TUI / device_selftest | 12 / 18(另跑) |

radio 套件断言:

- 门面:未 init 拒绝、能力位 gating、发送中重入返回 `BUSY`、LONG 状态转换、超时不进入 LONG。
- YS 帧:315/433 × SEND/LONG/STOP 与旧路径 `rf34dl_build_tx` **逐字节相同**;
  `0xF0/0xFE/0xFF` 应答匹配;写不足 8 字节 → `IO` 且不再等;非法 band → `ARG`。
- 原始脉冲/学习接口在 YS 上返回 `UNSUPPORTED`(符合 ADR-001 的 M1a 边界)。

## 3. Build

```bash
cd firmware && pio run -e esp32c3
```

| 项 | 结果 |
|---|---|
| 结果 | `SUCCESS`;`check_partitions` 通过(otadata + 双 OTA,无 factory) |
| 镜像 | **1,154,240 B / 2,031,616 B 槽(56.8%)** |
| RAM | **90,140 B / 327,680 B(27.5%)** |
| 产物 | `firmware/.pio/build/esp32c3/firmware.bin`,sha256 `6489bf494299b5f3b3c81018fd188ff7ca9f8ae23aba721881018555e971d8e0` |

## 4. 未验证(不得写成通过)

- **真机空口**:`SEND` 的 `0xF0` 时序、`LONG` 心跳、`STOP` 的 `0xFF` 均未复测;
  [`02-plan.md`](../02-plan.md) 的 RF-01/RF-02 仍是待办。
- `do_tx()` 的主机不可测部分(UI `sending...` 绘制、事件 JSON)只做了代码审查。
- 与 0.7.2 的 A/B 行为对比:没有保留旧固件二进制做同机对照。
- 构建体积对比不可直接归因:`1,154,240 B` 相对 2026-11-22 的 `1,098,608 B` 含
  其间其它提交与本次 radio 代码,未做逐提交拆分。

## 5. 复现

```bash
cd /home/myf/DSH/Dev/rf-lab
./tools/host_test.sh
SAN=1 ./tools/host_test.sh
cd firmware && pio run -e esp32c3
sha256sum .pio/build/esp32c3/firmware.bin
```
