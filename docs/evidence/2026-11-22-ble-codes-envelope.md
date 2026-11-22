# 2026-11-22 APP 码库空白:BLE `codes` 信封漏了结尾 `}`

状态:`measured(主机 + 构建 + 设备自检)` · **APP 显示待用户复测** · 未发 RF
提交:`f75173f fix(ble): close the codes envelope; generate it with a tested helper`
设备:`/dev/ttyACM0` · `build=f75173f / ui=d0824ba5` · 码库 21/64

## 1. 根因

容量提到 64 时,为了省一份 12KB 缓冲,把 BLE 的 codes 快照从
`snprintf(s_line, "{\"t\":\"codes\",…\"items\":%s}", json)` 改成了**原地拼**:

```c
memmove(s_codes + h, s_codes, clen + 1);
memcpy(s_codes, hdr, h);
notify_message(s_codes, h + clen);   /* ← 少了结尾 '}' */
```

旧写法 `…%s}` 里的 `}` 在改写时丢了。BLE 通知因此是**非法 JSON**,
APP 的 `safeParse` 整块丢弃 → 码库空白。SSE 路径一直发送 `"}\n\n"`,
所以 Web 页面正常,问题只在 BLE/APP。

**为什么没被测出来**:原来的 `codes` 信封是手写字符串,没有单元测试;
主机测试只覆盖事件环/分帧,不覆盖这个信封。

## 2. 修法与防复发

1. 信封统一由纯函数生成,进主机测试:
   - `link_codes_header(out, cap, seq)` → `{"t":"codes","seq":N,"items":`
   - `link_codes_wrap(buf, cap, off, len, seq)` → 头 + JSON + `}`,
     容量不足返回 0(不返回半截 JSON)。
   位置:[`link.c`](../../firmware/src/link.c) / [`link.h`](../../firmware/src/link.h)。
2. BLE 与 SSE 都改用该函数,**不允许各自手写信封**。
3. 主机测试([`test_link.c`](../../firmware/test/host/test_link.c))断言:
   精确字节、结尾 `}`、NUL、短缓冲拒绝、`link_codes_header` 长度;
   link 检查 9,418 → **9,427**。
4. 兼容契约写进 [`01-ui.md`](../01-ui.md) §3/§4:
   - `state/codes/event/log` 字段**只增不减**,客户端忽略未知字段;
   - `items` 必须是完整 JSON 数组,**信封必须带 `}`**;
   - 容量(`store.max`)是数据不是 schema,32→64 不要求 APP 升级;
   - 破坏性改动必须 bump `hello.proto` 并先写规格。

## 3. Build

```bash
./tools/flash.sh /dev/ttyACM0    # 本地 dev 平台;CI 用 7.1.3
```

| 项 | 值 |
|---|---|
| 提交 | `f75173f`(`build=` 无 `+dirty`) |
| 镜像 | **1,100,416 B**;sha256 `3032e3992a3fc306a560a20fd17607975f14a5b91850cb91e46326c165271950` |
| RAM | 88,692 B / 327,680 B(27.1%) |
| NVS | 保留,21/64 |

## 4. Host / Device

| 项 | 结果 |
|---|---|
| Host | `host_test.sh` **11,210**(协议 76 + 码库 368 + 事件环 9,427 + OTA 41 + OLED/UI 1,286 + TUI 12);`SAN=1` 全过 |
| Device | `build=f75173f`、`ble on=on adv=yes`、`count=21/64`、`heap free≈34.7KB` |
| `device_selftest --json` | **26 pass / 0 fail / 1 skip**(未加 `--tx`) |
| 代码库/HTTP | `/api/codes` 200、`/api/state` 200、SSE 200(本机无法测 BLE 数据面) |

## 5. Unverified

1. **APP 码库显示**:本机没有蓝牙适配器(`bluetoothctl list` 为空),
   无法在主机侧收 BLE 通知;需要手机重连 APP 后确认 21 条码恢复显示。
2. **64 条满库的 BLE 快照**:只验了 21 条(874B);12KB 快照的 BLE 分片/重组未压满。
3. **空口/RF**:本轮未发 `SEND/LONG/STOP`。
4. **APK 侧容错**:APP 对坏 JSON 是静默丢弃(本次因此表现为空白);
   应否在 UI 显示“数据解析失败”是 APP 侧改动,需重打 APK,未做。

## 6. 兼容性检查清单(以后改固件/APP 时用)

- 只加字段,不删/不改既有字段名;`items` 永远是完整数组。
- 任何 JSON 信封改动必须走 `link_codes_*` 并补 host test。
- 容量/条数只能是 `store.max`/数组长度,不写死在 APP。
- 固件与 APK 可独立升级:新固件 + 旧 APP 必须至少能显示码库与状态。
- 破坏性改动 → `hello.proto` + `01-ui.md` 规格 + 新证据,不能悄悄改。
