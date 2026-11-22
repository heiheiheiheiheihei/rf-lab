# 2026-11-22 码库按模块协议重构:osc 不进身份,越界帧不学

状态:`measured(主机 + 构建)` · **未上机**(设备被并发会话占用) · **未发 RF**
依据:用户提供的模块串口协议(2026-11-22):
9600 8N1,8 字节 `FD <F3|F4> 03 <地址2> <地址1> <按键> <osc> DF`,
地址/按键 `0x01..0xFF`,osc `0x30..0xF0`(建议 `0x60`)。

## 1. 判断

| 结论 | 依据 | 影响 |
|---|---|---|
| **osc 不是身份** | 第 7 字节是「震荡参数/脉宽」,与信号强度/解码质量有关;示例 `FD F4 03 12 34 05 60 DF` | 去重/查重只看 `band + 地址2 + 地址1 + 按键`;同键不同 osc 是同一条码 |
| **「大量重复码」得到解释** | 现有 32 条里 11 条只是 osc 不同:`555503 3C/4B/4D`(315/433)、`155530 46/4B`、`855630 4A/94/AD`、`D0C701 40/41/45/46/47` | 32 条 = **21 个身份**;`DEDUP` 可释放 11 槽,不需要「按 bit 距离猜」 |
| **`00000093/94/95/53` 是越界帧** | 地址1/地址2/按键都为 0,不在模块手册范围 | 学习时直接丢弃并计数,不再占满码库(RF-03 的直接线索) |
| **不需要「发送时轮流发不同参数」** | osc 是脉冲参数,不是第二个键;轮流发等于把同一个键连发多次 | `SEND/LONG/STOP` 仍原样回放 4B;要换 osc 用 `EDIT <id> code <8hex>` |

**边界**:这是**模块手册的 4B 字段语义**,不是解析 EV1527/Princeton 的 RF 位时序;
码库仍原样存/发 4 字节。若某些遥控器的地址字节确实为 0,会被当作越界帧丢弃——
用 `INFO`/`STATE` 的 `bad` 计数观察;需要放宽时改 `codebook_code_valid()` 一处。

## 2. 代码改动

| 文件 | 改动 |
|---|---|
| `firmware/src/codebook.h/.c` | 身份改为 `band + code[0..2]`;`codebook_code_valid()`(地址/按键非 0、osc `0x30..0xF0`);`codebook_same_identity()`;`set_code/set_band` 用 `find_except()` 允许「只改 osc」 |
| `firmware/src/main.c` | 学习前校验越界帧(计数 `bad`,不学);`DEDUP` 命令(按身份合并,保留最早一条);`LIST LONG` 增加 `osc=0xNN`;`INFO`/`STATE` 增加 `bad`;`HELP` 与 `NEW/EDIT` 的 dup 提示 |
| `firmware/test/host/test_codebook.c` | 测试改为协议语义:同身份不同 osc = DUP、换频段 = 新身份、只改 osc 允许、越界拒绝;218 → **235** checks |
| `tools/device_selftest.py` / 测试 | `LIST LONG` 解析可选 `osc=0xNN`(兼容没有该字段的 0.7.1);18 项单测仍过 |

示例:

```text
$ NEW 433M 5555034B
ERR new: dup (same band+addr+button; EDIT <id> code <8hex> to change osc)
$ LIST LONG
 1  315M 5555033C osc=0x3C  name="Gate A" group="Home" sym="A" manual
$ DEDUP
OK dedup removed=11 count=21
```

## 3. Build

```bash
cd firmware && pio run -e esp32c3   # 本地用 dev 平台(临时),未烧录
```

- `[SUCCESS]`,镜像 **1,099,760 B / 2,031,616 B(54.1%)**(比之前 +1,120 B)。
- 工作树是 dirty 的,`build=` 会带 `+dirty`;**这个镜像没有上机,也不对应 tag**。
- CI(esp32c3 7.1.3)未在本轮推送后运行;本改动尚未提交/推送。

## 4. Host

```text
./tools/host_test.sh        # 协议 76 + 码库 235 + 事件环 9418 + OTA 41 + OLED/UI 1286 + TUI 12 + selftest 18,0 failure
SAN=1 ./tools/host_test.sh  # 同上,ASan/UBSan 0 failure
```

## 5. Device

**未执行**。设备在 15:37 被并发会话刷成 `eaed71d+dirty / ui=6f8b9a42 / ota_slot=1`;
本会话没有再动板子,也没有发 `SEND/LONG/STOP`。
`DEDUP` 对真实 32 条的效果(预期 21 条)也**没有上机验证**。

> **后续(2026-11-22 16:52)**:本改动已提交 `200cc4e` 并全量烧录到设备:
> `DEDUP` 实测 `removed=11 count=21`、`osc` 只改不新建、越界 `NEW` 被拒、自检 26/0/1。
> 见 [`2026-11-22-protocol-device-test.md`](2026-11-22-protocol-device-test.md)。

## 6. Unverified

1. 新固件上机:学习去重、`DEDUP`、`bad` 计数、`LIST LONG` osc。
2. 越界帧过滤在真实噪声环境下的误杀率(地址字节为 0 的合法遥控器)。
3. 空口 `SEND/LONG/STOP`(RF-01/02)。
4. CI 首跑(本改动未推送);0.7.2 发版。
5. 本改动**未提交**;`02-plan`/`03-execution`/`01-ui`/`AGENTS` 里混有并发 watch 会话的未提交改动,未替你分开提交。
