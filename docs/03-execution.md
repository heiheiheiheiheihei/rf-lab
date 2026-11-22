# RF-Lab 执行交接(2026-11-22)

> **用途**:另起会话时先读这一页,再按链接读规格。
> **状态以 [`02-plan.md`](02-plan.md) 为准**;本页只锁「现在做什么、不做什么、怎么算过」。
> 仓库 `/home/myf/DSH/Dev/rf-lab` · 当前 `HEAD c8e93d9` · 固件/APK **0.7.2**

## 0. 一句话

**先 0.7.2「验证闭合」(P0 + 测试工具/CI),再 0.7.3「UI 一键固件更新」
(BOOT + GitHub Pages + 独立发版),最后 0.8.0「模块抽象/背板」。**
0.7.2 的 P0 没关闭前,**不写 0.7.3/0.8.0 的代码**。

## 1. 已锁定决策(不再讨论)

| 决策 | 锁定值 | 后果 |
|---|---|---|
| 0.7.3 物理确认 | **BOOT(GPIO9)按住 2s** | 不做 OLED 6 位 code 备选;1.0 外壳必须给 BOOT 留可达 |
| 0.7.3 固件源 | **GitHub Pages `/firmware/`** | 浏览器拉 manifest/bin;设备只收 HTTP 上传;不可达时手动 fallback |
| 0.7.3 发版 | **独立 0.7.3** | 0.7.2 不加新功能;0.7.3 只做一键更新 |
| 下载/校验 | 浏览器下载,**设备算 SHA-256** | 设备页是 HTTP,不是 secure context,不用 `crypto.subtle` |
| 授权 | BOOT 物理确认 → token;token 绑客户端 IPv4 | HTTP/BLE 不能单独 arm;`POST /api/cmd OTA` 仍 serial-only |
| 范围 | Web UI only | Android APP 仍 BLE-only;不做 BLE OTA/APK 更新;uConsole 不加更新入口 |
| 签名 | 0.7.3 不做 | SHA-256 不是签名;没有 secure boot;detached signature 另案 |

完整行为契约:[`01-ui.md`](01-ui.md) §3.4.1;版本/里程碑:[`02-plan.md`](02-plan.md) §5.2。

## 2. 当前状态(2026-11-22 实测)

| 项 | 状态 |
|---|---|
| Git | 本地 `200cc4e`(协议去重,未推送);`origin/master` = `89ad522`(CI 绿) |
| 主机 | `host_test.sh` **11,210 checks** + TUI **12** + selftest **18**;`app_update_test.sh` **36 checks**;全过,`SAN=1` 也过 |
| 固件构建 | **1,100,416 B / 2,031,616 B 槽(54.1%)**;RAM **88,692 B / 327,680 B(27.1%)** |
| 官网 | `site_check.py` **SITE OK**;1 条 WARN:`terminal.html` 127,490 B > 120KB |
| 设备 | `/dev/ttyACM0`,已全量刷 **`build=f75173f` / `ui=d0824ba5` / `ota_slot=0`**;码库 21/64,heap≈34.7KB;自检 26/0/1,见 [容量上机](evidence/2026-11-22-capacity-64.md) |
| 设备 UART | `MODE single` 已回 `ACK 0xF2`,`acks` 0→1;**当前模块输出模式可能已是 single** |
| 码库 | 13:46 为 **32/32**;16:52 按协议 `DEDUP` 后 **21/64**(11 条只是 osc 不同,已合并);`bad=0`(本轮未接收),见 [协议上机](evidence/2026-11-22-protocol-device-test.md) |
| 空口发射 | **未复测**:最近一次上机(13:46 自检)未加 `--tx`,没有发 `SEND/LONG/STOP` |
| CI | `master` `479d6db` 全绿(run 45007429077:host+SAN / static / 7.1.3 固件);仍无 required checks |
| Android | 本机无 JDK/SDK,APK 只能靠 CI;v0.7.1 Release 资产 1,050,176 B |

关键证据:

- [`evidence/2026-11-22-device-audit.md`](evidence/2026-11-22-device-audit.md)
- [`evidence/2026-11-22-firmware-flash.md`](evidence/2026-11-22-firmware-flash.md)
- [`evidence/2026-11-22-device-selftest.md`](evidence/2026-11-22-device-selftest.md)
- [`evidence/2026-11-22-ble-codes-envelope.md`](evidence/2026-11-22-ble-codes-envelope.md)
- [`evidence/2026-11-22-pages-cors.log`](evidence/2026-11-22-pages-cors.log)
- [`evidence/2026-11-21-send-no-ack.md`](evidence/2026-11-21-send-no-ack.md)

## 3. 执行顺序

### Phase A — 0.7.2 验证闭合(先做,不添加新功能)

| 任务 | 执行 | 内容 | 通过标准 |
|---|---|---|---|
| **A1** | 用户(上机) | 冷启动;查 VCC/GND/RXD 焊点/电压;`SEND 1` ×10;`LONG 1`;`STOP 1`;接收器/学习遥控器观察 | ≥9/10 次 `F0`(~850ms);`LONG` 接收器持续动作;`STOP` 停止;记录失败曲线 |
| **A2** ✅ | Agent(主机) | 修 `tools/device_selftest.py`:版本无关 + `--json` + 保留 `--tx` 门 | **已上机(2026-11-22 13:46):26 pass / 0 fail / 1 skip**,见 [`evidence/2026-11-22-device-selftest.md`](evidence/2026-11-22-device-selftest.md) |
| **A3** ✅ | Agent(主机) | 新增 `.github/workflows/ci.yml`:push/PR 跑 host(含 `SAN=1`)+ `pio run -e esp32c3` + `gen_wiring --check` + `build_web --check` + `site_check` + `node --check` + `app_update_test` | **已首跑绿(`d447494`)**;仅剩「配置 required checks」这一仓库设置未做 |
| **A4** | 用户(上机) | BLE `SEND`;错误 PIN 5 次/重连;`NAME/GROUP/SYM/EDIT` 后重启;OTA Web 浏览器上传 / 崩溃回滚 / AP 路径 | 每项一条 evidence;失败也写 |
| **A5** | 用户(上机) | uConsole 真机;AP 模式外部浏览器;STA 运行中掉线;Android 15 edge-to-edge;旧固件 0.6.x + 新 APK | 按 `02-plan.md` §3.2 的通过标准 |
| **A6** | Agent + 用户 | 用**当前发布 tag**(`v0.7.1`;若 0.7.2 有固件改动则 `v0.7.2`)重建/重烧;Release 附 `firmware.bin` + `SHA256SUMS.txt`(仅在发版时);写证据 | `build=` 不含 `dirty`;设备运行 tag 构建;Release 资产可核验 |

> 0.7.2 只有**固件改动**才 bump/发版;纯工具/CI/文档改动不重复打 tag。
> 若没有固件改动,设备重烧 `v0.7.1` tag 构建即可满足“可追溯”要求。

**0.7.2 退出条件(gate):**

- P0 四项(RF-01/02、TST-01、REL-01)关闭,或有用户签字的「明确不做」理由;
- 设备运行 `build=` 不含 `dirty` 的 tag 构建;`device_selftest` 在 0.7.2 上 N/N;
- CI 在 master 上绿;README/site/AGENTS 的版本、条数、体积与证据一致;
- 24h 静态无新增 433M 幻码(或已有明确告警/过滤)。

### Phase B — 0.7.3 UI 一键固件更新(Phase A gate 之后)

| 里程碑 | 内容 | 退出条件 |
|---|---|---|
| **B1 后端** | `POST /api/ota/arm`(body `size/sha256/version`)、`GET /api/ota/arm/status`、`/cancel`;BOOT(GPIO9)2s;token 绑 IPv4;OLED `OTA WAIT/ARMED`;`X-RFLAB-SHA256` | 不按 BOOT 无 token;按 2s 有 token;错 IP 401;host 测试过 |
| **B2 UI** | `CHECK LATEST` / `UPDATE`;Pages `manifest.json?t=`;下载/上传进度;失败文案;手动 fallback;APP/BLE 禁用 | 真机 LAN 全流程;console 无错 |
| **B3 发布** | `tools/firmware_manifest.py`;tag 构建固件;Release 附 `firmware.bin` + manifest + SUMS;Pages 注入 `site/firmware/` | Pages `/firmware/*` 200 且 CORS `*` |
| **B4 端到端** | 正常、坏 SHA、旧固件、AP/离线、中断、断电、回滚、NVS | `UI-26`/`UI-27` 全过,证据落 `docs/evidence/` |
| **B5 文档** | README/site/验收/发布说明 | 版本、体积、状态一致 |

**0.7.3 退出条件:** `UI-26`/`UI-27` 全过;真机 happy path + 坏 SHA + 中断 + 旧固件拒绝;
`/api/state`/SSE/串口无 token;AP/离线手动 fallback 可用;设备运行 tag 构建。

### Phase C — 0.8.0(0.7.3 gate 之后,先 ADR 不写驱动)

只做三份 ADR/规格:

1. **ADR-001 射频抽象接口**:`probe/init/learn_poll/send/long/stop/capabilities`。
2. **ADR-002 舱位/供电/引脚**:SPI 共享、每舱 CS、3.3V LDO、C3 GPIO 预算、天线隔离。
3. **ADR-003 码库模型**:`band` vs `module/profile/params`;YS 固定码与 CC1101 原始码共存/迁移。

**0.8.0 代码从 YS backend 重构开始,行为零变化;之后才是背板 + CC1101。**
不要把 CC1101 的「原始 RX/TX」写成「支持 315/433 固定码」。

## 4. 新会话启动提示(可直接粘贴)

```text
仓库:/home/myf/DSH/Dev/rf-lab
先按顺序读:AGENTS.md → docs/02-plan.md → docs/03-execution.md → docs/01-ui.md §3.4.1。
当前 HEAD dd71bf6,固件/APK 0.7.1;设备已于 11-22 重烧为 dd71bf6(master HEAD),空口 SEND 未复测。
现在只做 Phase A 的 A2/A3(不需要硬件):修 device_selftest + 加 CI。
不要开始 0.7.3/0.8.0 代码;不要发 RF;不要改 docs/evidence 的旧文件。
先给改动计划,再动手;完成后分别报告 Build/Host/Device/Unverified。
```

## 5. 验收命令与证据

```bash
./tools/host_test.sh                 # 协议 76 + 码库 368 + 事件环 9427 + OTA 41 + OLED/UI 1286 + TUI 12
SAN=1 ./tools/host_test.sh           # ASan/UBSan
./tools/app_update_test.sh           # 36 checks
python3 tools/gen_wiring.py --check
python3 tools/build_web.py --check
node --check web/app.js
node --check site/app.js
node --check site/assets/terminal-demo.js
python3 tools/site_check.py
cd firmware && pio run -e esp32c3    # 构建
python3 tools/device_selftest.py --port /dev/ttyACM0 --tx   # 上机;--json / --tx-repeat N 可选
python3 tools/wifi_selftest.py --port /dev/ttyACM0
```

每个上机任务写 `docs/evidence/YYYY-MM-DD-<topic>.md`:

```text
## Build
命令 / 产物 / 大小 / sha256:

## Host
命令 / checks / 失败数:

## Device
固件 build= / ui=:
设备 / 手机 / 浏览器:
命令与原始输出:
观察(接收器/学习遥控器/屏幕):
重复次数与失败:

## Unverified
明确没做的项:
```

**Build 绿 ≠ Device 绿**;没有原始输出的「已确认」不算证据。

## 6. 禁止事项

1. 在 RF-01/RF-02 复测前,不要把「发射」写成已验证;11-15 的 `F0` 只是历史证据。
2. 在 0.7.2 P0 关闭前,不写 0.7.3/0.8.0 代码。
3. 不要给 0.7.3 加 OLED code 备选;已拍板 BOOT。
4. 不要让 HTTP/BLE 在没有 BOOT 确认时 arm;不要把 SHA-256 写成签名/secure boot。
5. 不要把 1.1MB 固件提交进 git;`site/firmware/` 由 CI 注入。
6. 不要改 `docs/evidence/` 旧值;新结果写新文件。
7. 不要把「能探测到 CC1101」写成「能收发」;能力矩阵先写死。
8. 推送前按 `AGENTS.md` 约束 13 检查版本/Release;纯文档推送不重复打 tag。

## 7. 文档地图

| 文档 | 用途 |
|---|---|
| [`AGENTS.md`](../AGENTS.md) | 唯一入口、硬约束、路由 |
| [`02-plan.md`](02-plan.md) | 测试债务 + 版本计划(状态唯一所有者) |
| [`01-ui.md`](01-ui.md) §3.4.1 | 一键更新行为契约 |
| [`00-mvp.md`](00-mvp.md) | 射频/命令规格与 AC |
| [`evidence/`](evidence/) | 实测事实(不改旧值) |
| [`../README.md`](../README.md) | 对外状态摘要 |
| `firmware/` / `android/` / `uconsole/` README | 组件构建/排障 |
