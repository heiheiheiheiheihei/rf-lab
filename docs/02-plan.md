# RF-Lab 测试债务与开发计划(2026-11-22)

状态:**现行计划** · 基线 `HEAD dd71bf6` · 固件/APK `0.7.1` · 设备 `RF-Lab-1B59`
单一所有者:**未完成测试登记 + 版本计划**。验收口径仍由 [`00-mvp.md`](00-mvp.md) §7 与
[`01-ui.md`](01-ui.md) §8 定义;实测事实只写进 [`evidence/`](evidence/);本文件只回答
「还差什么、谁做、怎么算过、先做哪个」。

## 0. 结论(先看)

1. **现役链路的最大风险不是功能缺失,是「证据与当前设备状态脱节」。**
   2026-11-21 真机出现「ESP→模块命令方向 0 应答」;2026-11-22 只读巡检 `MODE single`
   已回 `F2`、`acks` 0→1,但**没有发 `SEND`**,空口发射仍未复测。设备原先跑的是
   `511afca+dirty`;2026-11-22 已用 `tools/flash.sh` 重烧为 **`dd71bf6`**(master HEAD,
   不是 `v0.7.1` tag),码库 13 条,不是 README 记录的 9 条。
   详见 [`evidence/2026-11-22-device-audit.md`](evidence/2026-11-22-device-audit.md) 与
   [`evidence/2026-11-22-firmware-flash.md`](evidence/2026-11-22-firmware-flash.md)。

2. **自动化基线是绿的,但没有人替它守门。**
   2026-11-22 复跑:主机 **11,051 + 36** checks 全过(含 `SAN=1`);固件构建
   **1,098,608 B / 2,031,616 B 槽(54.1%)**,RAM **73,540 B(22.4%)**;官网 `SITE OK`。
   但 `.github/workflows/` 只有「打 tag 发 APK」和「推 master 发 Pages」,
   **push/PR 不跑主机测试、不构建固件**。

3. **上机缺口集中在 5 类:**
   ① 射频空口(`SEND/LONG/STOP`、距离、幻码);
   ② BLE/APP 端到端(`SEND`、错误 PIN、写入持久化、Android 15);
   ③ OTA 失败路径(崩溃回滚、浏览器上传、AP 路径、真断电);
   ④ uConsole 真机;
   ⑤ AP 模式外部浏览器 / STA 运行中掉线。

4. **计划顺序:0.7.2「验证闭合」→ 0.7.3「UI 一键固件更新」→ 0.8.0「模块抽象/背板」。**
   0.7.2 不引入新射频能力,只做测试工具、CI、文档对齐和失败路径复测;
   0.7.3 才加「查最新版 → BOOT 确认 → 浏览器下载/设备校验/上传」的 UI 一键更新;
   0.8.0 建议把四个模块拆成 `0.8.0`(YS + CC1101)→ `0.8.x`(NRF24/SI4463/LR1121),
   不要一次全上。**在 0.7.2 的 P0 关闭前,不开始 0.7.3/0.8.0 的代码。**

## 1. 口径

| 词 | 含义 |
|---|---|
| **Build** | 只证明能编译/打包,不证明硬件行为 |
| **Host** | 主机单测/静态检查,不碰射频、不碰真机 |
| **Device** | 真机串口/HTTP/BLE/浏览器实测,必须记录固件 `build=`/`ui=` |
| **Unverified** | 没有可复现证据;不得写成「通过」 |
| **P0** | 阻断「现役链路可用」这一对外结论 |
| **P1** | 阻断「0.7.2 可发布/可回归」或会造成假绿/假状态 |
| **P2** | 覆盖率/工程化/体验补强,不阻断发布 |

「完成」至少需要:命令 + 产物/输出 + 证据文件;**Build 绿不能替代 Device 绿**。
射频项必须带接收器或学习遥控器的观察;BLE 项必须写手机型号/Android 版本与 PIN 结果;
UI 项必须写视口、浏览器与 console 结果。

## 2. 当前基线(2026-11-22 实测)

| 项 | 结果 | 证据 |
|---|---|---|
| Git | `dd71bf6` `master`;A2/A3 与上机证据未提交 | `git status` |
| 主机测试 | 协议 76 + 码库 218 + 事件环 9,418 + OTA 41 + OLED/UI 1,286 + TUI 12 = **11,051**;+ `device_selftest` **18**;APP 自更新 **36** | `./tools/host_test.sh` / `./tools/app_update_test.sh` |
| Sanitizer | `SAN=1 ./tools/host_test.sh` 全过 | 本次复跑 |
| 固件构建 | **1,098,608 B / 2,031,616 B(54.1%)**;RAM **73,540 B / 327,680 B(22.4%)**;70.6s | `cd firmware && pio run -e esp32c3` |
| 官网静态验收 | `SITE OK`;1 条 WARN:`terminal.html` **127,490 B** > 120,000 B 告警线 | `python3 tools/site_check.py` |
| Android 本机构建 | **不可复现**:本机没有 JDK / Android SDK;只能依赖 CI 或补工具链 | `java`/`adb` 未找到 |
| 设备现状 | `/dev/ttyACM0`,fw 0.7.1,`build=dd71bf6 ui=85a1a9b4`,STA `192.168.5.125`;码库 **13/32**(08:59 烧录)→ **32/32**(13:46 自检,全部自动学码,无 manual/edited);14:20 烧 IP 修复 `c4c9378` | [`2026-11-22-firmware-flash.md`](evidence/2026-11-22-firmware-flash.md) · [`2026-11-22-device-selftest.md`](evidence/2026-11-22-device-selftest.md) |
| 设备自检 | `device_selftest --json`: **26 pass / 0 fail / 1 skip**(skip = 未加 `--tx`,未发 RF);`RESET`/`LIST`/`LIST LONG`/`HELP`/`DISP`/`BLE`/`WEBTEST` 全过 | [`2026-11-22-device-selftest.md`](evidence/2026-11-22-device-selftest.md) |
| 设备 UART 命令方向 | `MODE single` → `ACK 0xF2`,`acks` 0→1 | 同上 |
| 未执行 | `SEND/LONG/STOP`(未发 RF)、BLE、OTA、uConsole、AP 浏览器 | — |

> **不要把这张表当发布验收。** 它只说明「主机是绿的、设备是活的」;
> 空口、BLE、OTA、uConsole 都还没有本次证据。

## 3. 未完成测试登记

### 3.1 P0 — 先关闭,否则不开始新功能

| ID | 待验证 | 当前缺口 | 通过标准 | 执行 |
|---|---|---|---|---|
| **RF-01** | 空口发射恢复 | 11-21 命令方向 0 应答;11-22 `MODE` 已回 `F2`,但没发 `SEND` | 冷启动后 `SEND 1` ×10 ≥9 次回 `F0`(~850ms);学习遥控器能复解码;失败次数/起始时间记录 | **U** |
| **RF-02** | `LONG` 持续 / `STOP` 释放 | 只有 `0xFE/0xFF` 状态,没有接收器观察 | `LONG` 期间接收器持续动作;`STOP` 后停止;重复 3 次 | **U** |
| **TST-01** | 上机自检脚本已过期 | `device_selftest.py` 硬编码 `fw=0.6.0`、`stored=5`;当前 0.7.1/13 条会误报 | 从 `INFO` 读版本/条数;覆盖 0.7.1 命令;输出 PASS/FAIL + 可解析 JSON | **A** |
| **REL-01** | 设备构建可追溯 | 设备已重烧为 `dd71bf6`(master HEAD,非 tag),sha256 已记录;**Release 仍只发 APK,没发固件二进制** | 用 tag 重建/重烧;记录 `build=`/`ui=`/sha256;Release 附 `firmware.bin` + `SHA256SUMS.txt` | **A+U** |

> **TST-01 已于 2026-11-22 13:46 上机关闭**:版本/条数改为从 `INFO`/`LIST`/`STATE` 交叉校验,
> 在 0.7.1 / 32 条真机上 `--json` 输出 **26 pass / 0 fail / 1 skip**、退出码 0(`SEND` 未跑)。
> 见 [`evidence/2026-11-22-device-selftest.md`](evidence/2026-11-22-device-selftest.md)。
> 仍开放:RF-01 / RF-02(空口)、REL-01(tag + Release 固件资产)。

### 3.2 P1 — 0.7.2 发布/回归必须覆盖

#### 射频与设备

| ID | 待验证 | 当前缺口 | 通过标准 | 执行 |
|---|---|---|---|---|
| RF-03 | 433M 幻码 / 自动学码误收 | 13 条码中 10–13 是 `433M 00000093/94/95/53`;11-15 已有两条同族异常 | 24h 静态不新增;按键只增真实码;若无法过滤,至少 UI/串口明确告警 | U+A |
| RF-04 | 距离 15–50m | 未测 | 室内 15–50m 可收发;记录墙体/天线/供电条件 | U |
| RF-05 | 真断电 NVS | 只验证过复位/重烧,没拔 USB | 拔电 10s 再上电,码库/Wi-Fi/BLE PIN 保留(条数以测试前 `LIST` 为准) | U |
| RF-06 | 无屏降级 | 屏焊死,未上机 | 0x3C 不应答时仍启动、串口/HTTP 可用;`DISP SHOW` 可脱屏复核 | U |
| RF-07 | 72 字节 stray | 2026-11-15 教学期间一次,未复现 | 复现则解释来源;不能解释就保留告警并记录条件 | U |

#### BLE / Android

| ID | 待验证 | 当前缺口 | 通过标准 | 执行 |
|---|---|---|---|---|
| BLE-01 | BLE `SEND` | 已验连接/AUTH/`DISP TEST`/码表,未验 ARM→SEND | 手机 BLE 发 `SEND 1`,回显 `OK ... (0xF0)`,UI `tx` 事件刷新 | U |
| BLE-02 | 错误 PIN / 断线重连 | 未验错误 PIN 5 次锁定与重连 | 连续 5 次错误断开;正确 PIN 可重连;暂停广播按 `01-ui.md` §4 现状记录(当前实现是断开后重新广播,10s 暂停窗口未单独做) | U |
| BLE-03 | BLE 写入重启持久化 | 11-22 只确认写入与 UI 同步 | `NAME/GROUP/SYM/EDIT` 后重启,`LIST`/`/api/codes` 仍保留 | U |
| APP-01 | Android 15 edge-to-edge | 需要真机 | 状态栏/导航栏不遮内容;三视口无横向溢出 | U |
| APP-02 | 旧固件 0.6.x + 新 APK | 代码路径有,未真机组合 | 编辑入口禁用并提示;`LIST/SEND` 仍可用 | U |
| APP-03 | 自更新失败路径 | 真机只验成功路径 | 坏 SHA-256 / 签名不符 / 未允许安装 / 取消,均失败关闭且不装 | U |
| APP-04 | 官网 APK 下载安装 | 只从 Release 装过,未点官网 `latest/download` | 手机浏览器点官网按钮能下载并安装同版本 | U |
| APP-05 | 固件/APK 0.7.1 整机组合 | 设备是 dirty 构建,不是发布固件 | 发布固件 + 发布 APK 同机跑 BLE/码库/发射回归 | U |

#### OTA

| ID | 待验证 | 当前缺口 | 通过标准 | 执行 |
|---|---|---|---|---|
| OTA-01 | 崩溃回滚 | 只有代码/编译,未构造崩溃镜像 | 新镜像启动即崩溃 → bootloader 回旧槽;串口/页面可见旧 `ui=` | A+U |
| OTA-02 | 浏览器上传 | `U` 面板已构建,只用 `ota.py`/curl 验过 | 真实浏览器选 `.bin` + token 上传,进度到 100%,重启后 `ui=` 正确 | U |
| OTA-03 | AP 模式 OTA | 只在 STA `192.168.5.125` 验过 | `192.168.4.1` AP 下同样 200/重启/保留 NVS | U |
| OTA-04 | 写 flash 时真断电 | 只测过 TCP 断流 | 写入中拔电,重启仍运行旧槽,不砖 | U |

#### Web / 官网 / uConsole

| ID | 待验证 | 当前缺口 | 通过标准 | 执行 |
|---|---|---|---|---|
| WEB-01 | 0.7.1 新皮肤上机 | 11-21「新皮肤未上机」;0.7.1 码库真机截图部分覆盖 | 设备 LAN 页面 1440/1280/390 无溢出/console error;`HW` 模态正常 | U |
| WEB-02 | AP 模式外部浏览器 | 本机无 Wi-Fi 客户端 | 手机连 `RF-Lab-XXXX`,浏览器开 `192.168.4.1` 可操作 | U |
| WEB-03 | STA 运行中掉线回退 | 只有失败启动→AP 回退,没有运行中掉线 | 运行中关路由器/拔 AP,15s 内开 AP,页面可恢复 | U |
| WEB-04 | Pages 线上交互回归 | 部署后未在线点过 | 线上 ARM/SEND/LONG/STOP/模拟学习可用;零 `/api`、零外部请求 | U+A |
| WEB-05 | Safari / Firefox | 只在 Chromium 159 验过 | iframe + `postMessage` + SSE shim 在 Safari/Firefox 可用 | U |
| WEB-06 | 低端手机帧率 / 读屏 | 未测 | 低端机可操作;`aria-live`/tab 顺序可用 | U |
| UC-01 | uConsole 真机 | 只有主机 12 项 + PTY 布局 | 1280×720 字体/中文宽度可读;`Ctrl+Alt+R` 打开;HTTP 与串口模式都能操作 | U |

#### 工程化 / 发布

| ID | 待验证 | 当前缺口 | 通过标准 | 执行 |
|---|---|---|---|---|
| CI-01 | push/PR 守门 | 只有 tag 发 APK、master 发 Pages | push/PR 自动跑 host + firmware build + web/wiring/site 检查;失败阻断合并 | A |
| CI-02 | Sanitizer 进 CI | 只在本地手动 `SAN=1` | CI 跑 ASan/UBSan,清零 | A |
| CI-03 | 文档一致性 | README/site/AGENTS 有 0.6.0、9 条、旧测试计数 | 脚本校验版本/体积/条数/链接;过期值失败 | A |
| CI-04 | Release 固件资产 | Release 只有 APK | 附 `firmware.bin` + sha256 + `build=`;与 tag 一致 | A |

### 3.3 P2 — 覆盖率与硬化

| ID | 项 | 说明 | 执行 |
|---|---|---|---|
| COV-01 | BLE 分帧纯 C 单测 | 把按 `\n` 重组/分片逻辑从 NimBLE 回调里抽出,主机测 `MTU-3` 分片、半包、粘包 | A |
| COV-02 | `web_server` / `net` / OTA 流主机测试 | 目前只测 `rf34dl/codebook/link/ota_ticket/ui`;路由、状态机、上传流没有主机测试 | A |
| COV-03 | 32 条满库真机 | 主机覆盖 32 条最大载荷;真机 JSON/BLE 快照未压满 | U |
| COV-04 | 模块 VCC–GND / 冷启动失效曲线 | 11-21 只做到「重焊后短暂恢复」;没量电压、没做失效曲线、没换模块对照 | U |
| COV-05 | nRF Connect 逐帧 `0A` | 行为已由手机端到端覆盖,属可选核对 | U |
| DOC-01 | `gen_wiring.py` 版本字面量 | SVG 里仍写 `● LIVE // 0.6.0`;应改为读固件版本或去掉版本号 | A |
| DOC-02 | 官网 demo 体积 | `terminal.html` 127,490 B 触发 120KB WARN;要么提高阈值并说明,要么压页面 | A |
| ENV-01 | Android 本机构建 | 当前无 JDK/SDK,APK 只能靠 CI;补工具链或明确「CI-only」 | A+U |
| WATCH-01 | 手表 APK 构建/安装 | 构建过只证明编译;需在真机确认 ColorOS 允许侧载、启动不崩 | A+U |
| WATCH-02 | 46/42mm 屏幕适配 | 402×476 / 372×430 两档无裁切,按钮命中区域正确 | U |
| WATCH-03 | 手表 BLE 控制 | 扫描/连接/AUTH/`SEND`/`LONG`/`STOP` 与设备回显一致;错误 PIN 失败关闭 | U |
| WATCH-04 | 长发失败关闭 | 松手/退后台/断链均 STOP;断链后重连先 STOP,不把长发留在设备 | U |

2026-11-22 真机进展(OW20W1 / API 27,证据
[`evidence/2026-11-22-watch-device.md`](evidence/2026-11-22-watch-device.md)):
WATCH-03/04/05 的 46mm 部分已关闭;WATCH-06 只关了错误 PIN 与退后台 STOP,
断链长发自动 STOP 仍未复现;42mm 实机、5 次 PIN 锁定、外部接收器观察仍开放。

### 3.4 明确不做(不是测试债务)

滚动码克隆/重放、0.7.x 的原始 OOK 波形记录(0.8.0 CC1101 的原始 RX/TX 是否解锁由 ADR-001 决定)、
扫频/RSSI/频谱、BLE 链路加密、secure boot/固件签名、云账户/公网访问、静默安装、应用商店分发。
这些是范围边界,不要用「未验证」包装成待办。

## 4. 关键风险与判断

1. **P0:把 11-15 的发射证据当成当前状态。**
   11-15 的 `F0` 是真的,但 11-21 模块命令方向失效也是真的;11-22 只证明 UART 恢复。
   **在 `SEND` 复测前,对外只能说「历史证据存在,当前待复测」。**

2. **P0:发布物与上机物不是同一个构建。**
   设备原先跑 `511afca+dirty`,tag 是 `a7b4bf3`;11-22 已重烧为 `dd71bf6`(master HEAD),
   但 Release 又只有 APK。结果是「0.7.1 上机通过」无法对应到一个可下载、可校验的固件。
   **先发带 `firmware.bin` + `SHA256SUMS.txt` 的固件产物,再谈发布验收。**

3. **P1:测试脚本本身会制造假红/假绿。**
   旧 `device_selftest.py` 按 0.6.0/5 条断言,跑在 0.7.1 上必然失败且原因不在设备。
   **2026-11-22 已改为版本无关并上机 26/0/1**,见 [`evidence/2026-11-22-device-selftest.md`](evidence/2026-11-22-device-selftest.md)。
   测试工具必须先版本无关化,否则后续上机记录不可比。

4. **P1:没有 CI 门。**
   主机 11k checks 只在人记得跑的时候跑;固件/网页/接线任何一处漂移都可能进 master。
   这比再写 1k 个断言更优先。

5. **P1:模块故障可能是硬件/供电,不要用软件重试掩盖。**
   11-21 的现象是「整段 0 字节」,不是「迟到」;`ACK_WAIT_MS` 加大没有用。
   先量 VCC–GND、RXD 电平、焊点,再做冷启动失效曲线和换模块对照。

6. **P2:0.8.0 的四个模块不是同一类能力。**
   YS-RF34DL 是固定码编解码模块;CC1101 是原始收发器;NRF24L01 是 2.4G 包链路;
   SI4463/LR1121 是另一类。**不能把它们都写成「支持 315/433 固定码」**。
   0.8.0 必须先有「能力矩阵」,UI 按能力启用/禁用。

## 5. 开发计划

> 以下为规划假设:时间不含器件到货、打样、返修等待;**以 gate 为准,不以日历为准。**

### 5.1 0.7.2 — 验证闭合(建议 1–2 周,2 个上机窗口)

**目标:不增加新射频能力,让「能用的」和「没跑的」在文档与证据里一致。**

| 工作流 | 内容 | 执行 | 产出 |
|---|---|---|---|
| A. 射频/硬件 | 冷启动 + 焊点/电压检查;`SEND/LONG/STOP` ×10;失效曲线;换模块对照;距离 | U | 新 evidence + 失败次数表 |
| B. 测试工具 | `device_selftest` 版本无关化、码库检查、`--json`;BLE 分帧抽取单测 | A | 可重复的上机自检 |
| C. CI | host(含 SAN)+ firmware build + `gen_wiring --check` + `build_web --check` + `site_check` + JS check | A | `.github/workflows/ci.yml` |
| D. 端到端 | BLE `SEND`、错误 PIN、写入持久化;OTA 浏览器/回滚/AP;uConsole;AP 浏览器 | U | 每项一条 evidence |
| E. 发布工程 | tag 构建重烧;Release 附 `firmware.bin` + SHA256SUMS;APK 只在有改动时 bump | A+U | v0.7.2 Release(如固件有改动) |

**0.7.2 退出条件(gate):**

- P0 四项全部关闭,或有用户签字的「明确不做」理由;
- P1 中 RF/BLE/OTA/WEB/UC 的阻塞项关闭;其余列为已知限制;
- 设备运行 `build=` 不含 `dirty` 的 tag 构建;`device_selftest` 在 0.7.2 上 N/N;
- CI 在 master 上绿;README/site/AGENTS 的版本、条数、体积与证据一致;
- 24h 静态无新增 433M 幻码(或已有明确告警/过滤)。

> 只改文档/测试工具/CI 的提交不 bump `versionName`;一旦改固件,就按「推送即发版」走 0.7.2。

> **2026-11-22 bugfix 例外(用户确认)**:[`c4c9378`](evidence/2026-11-22-net-note-ip-fix.md) 只修
> 「串口网络事件把 IPv4 截成 12 字符」的日志问题,不涉及射频/协议/分区/NVS,直接进 `master`,
> **不 bump `versionName`、不占 0.7.2**;设备已烧该提交并复测(`192.168.5.125`)。
> 0.7.2 仍只用于验证闭合。同类例外只适用于「纯日志、无行为变化」的修复,需逐次确认,
> 不得推广到射频/存储/OTA 行为。

### 5.2 0.7.3 — UI 一键固件更新(建议 2–3 周)

**目标**:设备页里完成「检查最新版 → BOOT 物理确认 → 浏览器下载 → 设备校验 SHA-256 →
写入非运行槽 → 重启 → 显示新 build」,不再手工找 `.bin`/抄 token。完整行为契约见
[`01-ui.md`](01-ui.md) §3.4.1;手动「镜像 + token」上传保留。

**依赖**:0.7.2 gate 通过;双槽 OTA 的浏览器上传/回滚用例先有结果;Pages CORS 已核
([`evidence/2026-11-22-pages-cors.log`](evidence/2026-11-22-pages-cors.log))。

**不在范围**:Android APP 的 BLE OTA、设备自己拉 GitHub、固件签名/secure boot、APK 更新。

**已拍板(2026-11-22)**:物理确认 = **BOOT**;固件源 = **GitHub Pages**;0.7.3 **独立发版**。
以下按此执行,不再把 OLED code、设备自拉、并入 0.7.2 作为并列备选。

#### 5.2.1 架构决策

| 决策 | 结论 | 理由 |
|---|---|---|
| 谁下载固件 | **浏览器**从 GitHub Pages 拉 | Pages `ACAO: *`;设备不需要 TLS/SNTP/CA bundle |
| 谁校验 | **设备**边写边算 SHA-256 | 设备页是 HTTP,不是 secure context,`crypto.subtle` 不可用;设备才是写入方 |
| 谁授权 | **BOOT(GPIO9)按住 2s** | 保留「远程不能单独 arm」;HTTP/BLE 不直接给 token |
| 固件放哪 | Pages `/firmware/manifest.json` + `/firmware/rf-lab-firmware.bin` | 稳定 URL、CORS 可用;Release 资产仅作归档/手动下载 |
| Pages 怎么来的 | CI 从最新 Release 取固件,部署时注入 `site/firmware/` | 不把 1.1MB 二进制提交进 git;master 的 Pages 部署也不会把固件冲掉 |

#### 5.2.2 里程碑

| 里程碑 | 内容 | 退出条件 |
|---|---|---|
| M1 后端 | `POST /api/ota/arm`、`GET /api/ota/arm/status`、`/cancel`;BOOT 检测;token 绑 IPv4;OLED `OTA WAIT/ARMED`;`X-RFLAB-SHA256` | 主机单测 + 真机:不按 BOOT 无 token;按住 2s 有 token;错 IP 401 |
| M2 UI | `CHECK LATEST` / `UPDATE`、manifest 校验、下载/上传进度、失败文案、手动 fallback、APP/BLE 禁用 | 本地 Chromium + 真机 LAN 全流程;console 无错 |
| M3 发布 | 构建固件、生成 `firmware-manifest.json`、Release 资产、Pages 注入 | tag Release 有固件+manifest+SUMS;Pages `/firmware/*` 200 且 CORS `*` |
| M4 端到端 | 正常升级、坏 SHA、旧固件、AP/离线、中断、断电、回滚、NVS | UI-26/UI-27 全过,证据落 `docs/evidence/` |
| M5 文档 | README/site 状态板、`01-ui` 验收、发布说明 | 版本/体积/状态一致 |

#### 5.2.3 接口与数据

- `manifest.json` 字段与校验规则见 `01-ui.md` §3.4.1;新增 `tools/firmware_manifest.py`
  (`--bin/--version/--build/--ui/--min-from/--out`,`--check` 供 CI)。
- `POST /api/ota/arm`(body `{size,sha256,version}`)→ `202 state=wait_button`;
  `GET /api/ota/arm/status` → token(仅同 IP);`POST /api/ota/arm/cancel`;
  `POST /api/ota` 新增 `X-RFLAB-SHA256`,并校验 size/sha 与 arm 记录一致。
- token 仍 8 hex/10 分钟/一次性;新增客户端 IPv4 绑定;`/api/state`/SSE/串口不出现 token。
- `min_from` 不满足 → 拒绝并提示 USB `flash.sh`;`version <= 当前` → 不提供降级/同版重装。
- CI 必须校验 **tag == `firmware/platformio.ini` 的 `RFLAB_FW_VERSION` == Android `versionName`
  == manifest `version`**,任何一处不一致直接失败,避免「Release 里装出旧版」。
- 新增纯 C `sha256.c/h`(不依赖 mbedTLS,主机可测);`ota.c` 流式更新,`ota_finish` 比较
  期望值,失败 `ota_abort`。已知向量 + 1MB 分块 + 中断用例进 host tests。
- **线程模型**:HTTP task 只投递 arm 请求/读取结果,不在 HTTP task 直接刷 OLED 或调
  `ota_arm`;arm 状态、BOOT 轮询、`OTA OFF` 由主循环单一所有者,避免与上传/命令竞争。
- **Pages 注入**:`pages.yml` 在 master push 与 release workflow 完成后,从最新 Release
  下载 `rf-lab-firmware.bin` + `firmware-manifest.json` 到 `site/firmware/` 再部署;
  不把 1.1MB 二进制提交进 git,也不让 master 部署把 `/firmware/` 冲掉。
- 本地/官网 demo:`tools/ui_mock_server.py` 提供 mock manifest + 假固件;
  `terminal-demo.js` 拦截 manifest 请求,标 `SIM`,不真联网。

**文件影响(预估)**:

| 文件 | 改动 |
|---|---|
| `web/index.html` / `web/app.js` / `web/app.css` | `U` 面板加 `CHECK LATEST`/`UPDATE`/状态;`rflab-ota-base` meta;下载/上传/错误文案;APP 禁用 |
| `firmware/src/sha256.c` / `.h` | 新增纯 C SHA-256(主机可测) |
| `firmware/src/ota.c` / `.h` | 流式 hash、expected size/sha、完成时比较 |
| `firmware/src/ota_ticket.c` / `.h` | arm code/等待态、IPv4 绑定、限速、token 生命周期 |
| `firmware/src/web_server.c` | `/api/ota/arm`、`/status`、`/cancel`;`/api/ota` 新头 |
| `firmware/src/main.c` / `ui.c` | BOOT(GPIO9)2s 检测、OLED `OTA WAIT/ARMED`、主循环所有权 |
| `tools/firmware_manifest.py` | 新增:生成/校验 manifest(bin sha256/size/build/ui/min_from) |
| `tools/ota.py` / `tools/ui_mock_server.py` | CLI 带 `X-RFLAB-SHA256`;mock manifest/bin |
| `site/assets/terminal-demo.js` | 拦截 `manifest.json`,SIM 回放,保持零真实网络 |
| `.github/workflows/android-release.yml` / `pages.yml` | 构建固件、Release 资产、Pages 注入 |
| `tools/site_check.py` | 允许可选 `site/firmware/`;不把注入物当外部资源 |

#### 5.2.4 测试

- **Host**:manifest 校验(schema/版本/file/size/sha/min_from);SHA-256 已知向量/半包/中断;
  arm 状态机/限速/IP 绑定/token 不出现在 state;`firmware_manifest.py --check`。
- **Device**:不按 BOOT / 按 2s / 30s 超时 / `OTA OFF`;错 IP;坏 SHA;size 不符;上传中断;
  写 flash 拔电;AP 模式;升级后 NVS(13 条码/Wi-Fi/PIN)保留;`build` 变化。
- **Browser**:Chromium、Android Chrome、Safari、Firefox;Pages CORS;下载/上传进度;
  离线/AP;APP WebView 显示不支持;官网 iframe demo 拦截 manifest 且零真实网络。
- **CI**:tag 构建固件;生成 manifest;`SHA256SUMS.txt` 含固件;Pages 注入后
  `/firmware/manifest.json` 200 + CORS;`site_check.py` 允许可选 `site/firmware/`。

#### 5.2.5 风险 / 开放决策

1. **BOOT 可达性(已拍板用 BOOT)**:当前裸板可用;1.0 外壳必须给 BOOT 留可达
   (开孔/硅胶键)。若 1.0 结构做不到,再单独立项硬件方案;0.7.3 不改成 OLED code。
2. **Pages 可达性(已接受 Pages)**:GitHub Pages 在部分网络不可达;失败必须回退手动上传,
   不能假装成功,也不因此改回「设备自拉」。
3. **无签名(未决)**:SHA-256 只保证字节一致,不保证来源;Pages/仓库被篡改仍可能下发恶意固件。
   是否在 0.8.x/1.0 加 detached signature,单独 ADR。
4. **AP 模式无外网**:浏览器查不到 manifest;只能手动文件或先接 LAN。
5. **内存/超时**:1.1MB 下载 + 上传;低端手机与慢 Wi-Fi 要设 8s 检查/300s 上传超时并给出可重试文案。
6. **Pages 触发**:优先用 `workflow_run`(release 完成后)重新部署 Pages;若 GitHub 对
   `GITHUB_TOKEN` 的递归事件有限制,则在 release workflow 里加一个 Pages job,或发版后手动
   `workflow_dispatch`。无论哪条路,master 的 Pages 部署都必须先 hydrate 最新 Release 的固件。

**0.7.3 退出条件:** UI-26/UI-27 全过;真机完成 happy path + 坏 SHA + 中断 + 旧固件拒绝;
Pages `/firmware/manifest.json` 与 `.bin` 均 200 且 CORS `*`;设备运行 tag 构建;
`/api/state`/SSE/串口无 token;AP/离线时手动 fallback 可用。

### 5.3 0.8.0 — 模块抽象层 + SPI 背板(建议 4–6 周)

**先设计,后写驱动。** 0.8.0 的难点不是「多写四个驱动」,是能力模型和硬件边界。

#### 5.3.1 先落三份 ADR/规格(不写代码)

| 决策 | 要回答的问题 |
|---|---|
| ADR-001 射频抽象接口 | `probe/init/learn_poll/send/long/stop/capabilities` 的最小接口;错误如何上报;哪些能力是模块可选 |
| ADR-002 舱位/供电/引脚 | SPI 共享 + 每舱 CS;是否需要每舱电源开关;3.3V LDO/去耦;C3 GPIO 预算;天线隔离 |
| ADR-003 码库模型 | 继续用 `band` 还是升级为 `module/profile/params`;YS 固定码与 CC1101 原始码如何共存;迁移路径 |

#### 5.3.2 能力矩阵(先写死,避免官网越权)

| 模块 | 频段/形态 | 固定码学习 | 固定码发射 | 原始 RX/TX | `LONG/STOP` |
|---|---|---|---|---|---|
| YS-RF34DL | 315/433 | ✅ 现役 | ✅ 现役 | ❌ | ✅ |
| CC1101 | 300–928 MHz | 需软件解码,首版不承诺 | 需软件编码,首版不承诺 | ✅ 首版目标 | ❌ |
| NRF24L01+ | 2.4G 包链路 | ❌ | ❌ | 包收发 | ❌ |
| SI4463 | 119–1050 MHz | 另案 | 另案 | 另案 | ❌ |
| LR1121 | Sub-GHz + 2.4G | 另案 | 另案 | 另案 | ❌ |

> 「原始 RX/TX」不等于「能复制固定码」。EV1527/Princeton 的位解码是另一件事,
> 要不要做、做到什么程度,由 ADR-001/003 决定;不在 0.8.0 默认承诺里。

#### 5.3.3 里程碑

| 里程碑 | 内容 | 退出条件 |
|---|---|---|
| M1 | `radio` HAL + YS backend 重构 | 0.7.2 的 host/device 回归全过,行为零变化(金样本) |
| M2 | 背板 rev A + 舱位探测 | `BAY`/`PROBE` 能识别空舱/YS/CC1101;供电/发热/引脚记录 |
| M3 | CC1101 backend(原始 RX/TX) | 主机 mock + 真机收发;与 Wi-Fi/BLE 共存;Flash/RAM 预算达标 |
| M4 | NRF24L01 backend(2.4G 包) | 点对点包收发;UI 只显示包能力,不冒充固定码 |
| M5 | SI4463/LR1121(0.8.x) | 每个模块单独验收,不打包宣称 |

#### 5.3.4 0.8.0 测试策略

- **Host**:每个驱动的寄存器序列/状态机用 mock HAL 单测;YS 金样本回归;错误路径(空舱、ID 读错、SPI 超时)。
- **Device**:`PROBE`/`BAY` 自检;传导/近场收发(衰减器 + 假负载);接收器/学习遥控器观察;距离。
- **共存**:Wi-Fi + BLE + 2.4G 模块同时开;记录丢包/重连/功耗。
- **资源**:每加一个驱动记录 Flash/RAM;保持 OTA 槽 <70%,不把 1.94MB 用满。
- **UI**:能力矩阵驱动按钮禁用;空舱/不支持模块有明确文案;官网仍标「路线图」直到 Device 证据。

**0.8.0 退出条件:** 至少一个新增模块完成「Device 可用 + 可重复测试」;YS 零回归;
能力矩阵与 UI/官网一致;没有把「能探测到芯片」写成「能收发」。

### 5.4 0.9.0 — 本地 MQTT + Home Assistant Discovery(建议 2–3 周)

**目标:把已有的 HTTP/SSE + 命令行接进本地自动化,不复制射频逻辑。**

| 项 | 计划 |
|---|---|
| 传输 | `esp-mqtt` over Wi-Fi;只连用户自己的局域网 Broker;不做云 |
| 配置 | Web 设置页填 Broker URI/用户名/密码/base topic;存 NVS;`MQTT TEST` 串口命令 |
| Topic | `rflab/<node>/state`(retained JSON)、`/event`、`/cmd`(subscribe)、`/availability`(LWT) |
| 命令 | `/cmd` payload 仍是既有命令行(`SEND 1`),不新增第二套语义 |
| Discovery | HA MQTT Discovery:最近码/码库数/RSSI/uptime + `send_last`/`stop` 按钮 |
| 失败 | Broker 断开不清空本地 UI,自动重连;离线不影响串口/BLE/HTTP |
| 安全 | Broker 只开局域网;ACL 按 topic 限权;文档写明「MQTT 不是鉴权替代品」 |
| 测试 | Mosquitto 本地集成;retained/LWT/QoS0-1;断线重连;HA 自动发现;命令回执 |

**退出条件:** 真实 LAN 上 HA 自动出现实体;MQTT `SEND` 能让接收器动作;Broker 关掉后
本地 Web/BLE/串口仍可用;没有公网/云依赖。

### 5.5 1.0 — 产品化(建议 6–8 周)

外壳/结构、BOM 与替代料、装配/测试夹具、RF/EMC 预扫、法规与标签、10 台试产、
72h 老化、跌落/热、说明书与支持矩阵。**1.0 的门槛是「可重复制造 + 可重复测试」,
不是再添一个模块。**

### 5.6 版本与发布规则

- `0.7.2` / `0.7.3` 只在固件有改动时发;纯文档/工具/CI 不重复打 tag。
- 每个 Release 至少附:APK(如改动)、`firmware.bin`、`firmware-manifest.json`、
  `SHA256SUMS.txt`、`build=`/`ui=`、CI run。
- `versionName`、tag、APK、固件、manifest 五处必须一致;官网状态板在发版后同步。
- Pages 部署时从**最新 Release**注入 `site/firmware/`,不把二进制提交进 git;
  master 的 Pages 重新部署不得把固件目录冲掉。
- 证据只写新文件,不改旧值;失败也写,失败比「没跑」有价值。

## 6. 执行顺序(未来两周 + 0.7.3)

| 顺序 | 任务 | 执行 | 依赖 |
|---|---|---|---|
| 1 | 冷启动 + 焊点/电压 + `SEND/LONG/STOP` ×10 + 接收器观察 | U | 自有接收器/学习遥控器 |
| 2 | `device_selftest` 版本无关化 + `--json` | A | 无 |
| 3 | CI 骨架(host/SAN/firmware/web/site) | A | 无 |
| 4 | BLE `SEND`、错误 PIN、写入重启持久化 | U | 手机 + 设备 |
| 5 | OTA 浏览器上传 / 崩溃回滚 / AP 路径 | U+A | OTA 镜像/token |
| 6 | uConsole 真机、AP 浏览器、STA 掉线 | U | uConsole/手机/路由器 |
| 7 | tag 构建重烧 + Release 固件资产 + 0.7.2 证据 | A+U | 1–6 |
| 8 | 0.7.3 M1 后端:arm/status/BOOT/SHA-256/IP 绑定 | A+U | 0.7.2 gate |
| 9 | 0.7.3 M2 UI:CHECK/UPDATE/失败文案/手动 fallback | A | 8 |
| 10 | 0.7.3 M3–M4:Release+Pages 注入、真机 happy/failure/rollback | A+U | 9 |
| 11 | 0.8.0 三份 ADR 评审 | A+U | 0.7.3 gate |

**并行原则:** 需要硬件的(U)和纯主机的(A)分开跑;A 不阻塞 U,A 也不替 U 下结论。
**停止线:** 第 1 项没有结果前,不开始 0.7.3/0.8.0 的代码;第 3 项没有绿前,不接受「CI 已覆盖/回归已自动化」。

## 7. 验收与证据模板

每个上机任务至少留一个 `docs/evidence/YYYY-MM-DD-<topic>.md`,包含:

```text
## Build
命令:
产物/大小/sha256:

## Host
命令:
checks/失败数:

## Device
固件 build= / ui=:
设备/手机/浏览器:
命令与原始输出:
观察(接收器/学习遥控器/屏幕):
重复次数与失败:

## Unverified
明确没做的项:
```

射频项的通过标准写「次数/比例/条件」,不写「正常」;UI 项写视口/浏览器/console;
BLE 项写手机型号/Android 版本/PIN 结果。**没有原始输出的「已确认」不算证据。**

## 8. 文档关系

| 文档 | 角色 |
|---|---|
| 本文件 | **测试债务 + 版本计划**的唯一所有者 |
| [`00-mvp.md`](00-mvp.md) | 射频/命令规格与 AC-1..AC-26 |
| [`01-ui.md`](01-ui.md) | UI/BLE/OTA/APP 规格与 UI-1..UI-27(含 §3.4.1 一键更新规划) |
| [`evidence/`](evidence/) | 实测事实;新结果写新文件 |
| [`../README.md`](../README.md) | 对外状态摘要;必须链接本文件,不另维护第二份清单 |
| `firmware/` / `android/` / `uconsole/` README | 组件构建/排障;未验证项只留摘要并链接本文件 |

> 新增测试项先加到本文件,再改组件 README;不要在四处各写一份「未验证」。
