# RF-Lab 测试债务与开发计划(2026-11-24)

状态:**现行计划** · 基线 `master be22aeb` + 0.8.0 M1a 工作区(未提交)· 对外固件/APK `0.7.2` · 设备 `RF-Lab-1B59`
单一所有者:**未完成测试登记 + 版本计划**。验收口径仍由 [`00-mvp.md`](00-mvp.md) §7 与
[`01-ui.md`](01-ui.md) §8 定义;实测事实只写进 [`evidence/`](evidence/);本文件只回答
「还差什么、谁做、怎么算过、先做哪个」。

## 0. 结论(先看)

1. **现役链路的最大风险仍是「证据与当前设备状态脱节」。**
   2026-11-21 真机出现「ESP→模块命令方向 0 应答」;11-22 只读巡检 `MODE single` 已回
   `F2`、`acks` 0→1,但**没有发 `SEND`**,空口发射仍未复测。设备已 OTA 到
   `fw=0.7.2 build=e0f155a`,码库 21/64;详见
   [`evidence/2026-11-22-release-0.7.2.md`](evidence/2026-11-22-release-0.7.2.md)。

2. **自动化基线已绿,但 merge 仍不守门。**
   主机 C 测试 **11,312**(含 `SAN=1`)+ TUI 12、APP 36 checks、固件构建、站点检查都有,`ci.yml` 在 `master` 绿;但 `master` 没有 required checks,失败不会阻断合并。

3. **上机缺口集中在 4 类:**
   ① 射频空口(`SEND/LONG/STOP`、距离、幻码);
   ② BLE/APP 端到端(`SEND`、错误 PIN、写入持久化、Android 15);
   ③ OTA 失败路径(崩溃回滚、浏览器上传、AP 路径、真断电);
   ④ uConsole 真机、AP 模式外部浏览器、STA 运行中掉线。

4. **计划顺序:0.7.2「验证闭合」→ 0.7.3「UI 一键固件更新」→ 0.8.0「模块抽象/背板」。**
   0.8.0 M1a(仅 HAL 重构,行为零变化)已在 11-24 先行落地,**不改变 gate**;
   0.7.2 的 P0 关闭前,不做 0.7.3/0.8.0 的新能力。

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

## 2. 当前基线(2026-11-24)

| 项 | 结果 | 证据 |
|---|---|---|
| Git | `master be22aeb`;0.8.0 M1a 在工作区未提交 | `git status` |
| 主机测试 | 协议 76 + 码库 368 + 事件环 9,427 + OTA 41 + radio 114 + OLED/UI 1,286 = **11,312**;另有 TUI 12、`device_selftest` 18;APP 自更新 36 | `./tools/host_test.sh` / `./tools/app_update_test.sh` |
| Sanitizer | `SAN=1 ./tools/host_test.sh` 全过 | 2026-11-24 已验 |
| 固件构建 | v0.7.2 tag:1,126,928 B / 2,031,616 B(55.5%),RAM 27.5%;工作区 M1a:1,154,240 B(56.8%),RAM 27.5% | `cd firmware && pio run -e esp32c3` |
| 官网静态验收 | `SITE OK`;1 条 WARN:`terminal.html` **127,587 B** > 120,000 B 告警线 | `python3 tools/site_check.py` |
| 设备现状 | `/dev/ttyACM0`,**`fw=0.7.2 build=e0f155a`**,`ui=d0824ba5`,STA `192.168.5.125`;码库 **21/64** | [`2026-11-22-release-0.7.2.md`](evidence/2026-11-22-release-0.7.2.md) |
| 设备自检 | `device_selftest --json`:**26 pass / 0 fail / 1 skip**(skip = 未加 `--tx`,未发 RF) | 同上 |
| 设备 UART 命令方向 | `MODE single` → `ACK 0xF2`,`acks` 0→1 | 同上 |
| 未执行 | `SEND/LONG/STOP`、BLE `SEND`、OTA 浏览器/回滚、uConsole、AP 浏览器 | — |

> **不要把这张表当发布验收。** 它只说明「主机是绿的、设备是活的」;
> 空口、BLE、OTA、uConsole 都还没有本次证据。

## 3. 未完成测试登记

### 3.1 P0 — 先关闭,否则不开始新功能

| ID | 待验证 | 当前缺口 | 通过标准 | 执行 |
|---|---|---|---|---|
| **RF-01** | 空口发射恢复 | 11-21 命令方向 0 应答;11-22 `MODE` 已回 `F2`,但没发 `SEND` | 冷启动后 `SEND 1` ×10 ≥9 次回 `F0`(~850ms);学习遥控器能复解码;失败次数/起始时间记录 | **U** |
| **RF-02** | `LONG` 持续 / `STOP` 释放 | 只有 `0xFE/0xFF` 状态,没有接收器观察 | `LONG` 期间接收器持续动作;`STOP` 后停止;重复 3 次 | **U** |

> TST-01(自检版本无关)与 REL-01(tag 构建 + Release 固件资产)已于 2026-11-22 关闭,
> 证据见 [`2026-11-22-device-selftest.md`](evidence/2026-11-22-device-selftest.md) 与
> [`2026-11-22-release-0.7.2.md`](evidence/2026-11-22-release-0.7.2.md)。

### 3.2 P1 — 0.7.2 验证/回归必须覆盖

#### 射频与设备

| ID | 待验证 | 当前缺口 | 通过标准 | 执行 |
|---|---|---|---|---|
| RF-03 | 433M 幻码 / 自动学码误收 | 越界帧过滤已进 0.7.2,`DEDUP` 后 21/64;24h 静态复测未做 | 24h 静态不新增;按键只增真实码;若无法过滤,至少 UI/串口明确告警 | U+A |
| RF-04 | 距离 15–50m | 未测 | 室内 15–50m 可收发;记录墙体/天线/供电条件 | U |
| RF-05 | 真断电 NVS | 只验证过复位/重烧,没拔 USB | 拔电 10s 再上电,码库/Wi-Fi/BLE PIN 保留(条数以测试前 `LIST` 为准) | U |
| RF-06 | 无屏降级 | 屏焊死,未上机 | 0x3C 不应答时仍启动、串口/HTTP 可用;`DISP SHOW` 可脱屏复核 | U |
| RF-07 | 72 字节 stray | 2026-11-15 教学期间一次,未复现 | 复现则解释来源;不能解释就保留告警并记录条件 | U |

> **RF-03 现状**:4B payload = `地址2/地址1/按键/osc`,身份不含 osc;地址/按键为 0 的
> 越界帧不学习并计入 `bad`。设备执行 `DEDUP` 后 `removed=11 count=21`,`bad=0`(未接收);
> 24h 静态复测仍未做。

#### BLE / Android

| ID | 待验证 | 当前缺口 | 通过标准 | 执行 |
|---|---|---|---|---|
| BLE-01 | BLE `SEND` | 已验连接/AUTH/`DISP TEST`/码表,未验 ARM→SEND | 手机 BLE 发 `SEND 1`,回显 `OK ... (0xF0)`,UI `tx` 事件刷新 | U |
| BLE-02 | 错误 PIN / 断线重连 | 未验错误 PIN 5 次锁定与重连 | 连续 5 次错误断开;正确 PIN 可重连;暂停广播按 `01-ui.md` §4 现状记录(当前实现是断开后重新广播,10s 暂停窗口未单独做) | U |
| BLE-03 | BLE 写入重启持久化 | 11-22 只确认写入与 UI 同步 | `NAME/GROUP/SYM/EDIT` 后重启,`LIST`/`/api/codes` 仍保留 | U |
| BLE-04 | BLE codes 快照完整性 | 2026-11-22 APP 码库空白(信封漏 `}` 已修);64 条大快照未验 | 手机 BLE 收到完整 `{"t":"codes","items":[...]}`;21/64 条都能显示;重连后仍刷新 | U |
| APP-01 | Android 15 edge-to-edge | 需要真机 | 状态栏/导航栏不遮内容;三视口无横向溢出 | U |
| APP-02 | 旧固件 0.6.x + 新 APK | 代码路径有,未真机组合 | 编辑入口禁用并提示;`LIST/SEND` 仍可用 | U |
| APP-03 | 自更新失败路径 | 真机只验成功路径 | 坏 SHA-256 / 签名不符 / 未允许安装 / 取消,均失败关闭且不装 | U |
| APP-04 | 官网 APK 下载安装 | 只从 Release 装过,未点官网 `latest/download` | 手机浏览器点官网按钮能下载并安装同版本 | U |
| APP-05 | 固件/APK 0.7.2 整机组合 | 设备已是 tag 构建,但 0.7.2 APK 未装 | 发布固件 + 发布 APK 同机跑 BLE/码库/发射回归 | U |

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
| WEB-01 | 新皮肤全量上机 | 11-21 只覆盖码库页;全量皮肤与 `HW` 模态未回归 | 设备 LAN 页面 1440/1280/390 无溢出/console error;`HW` 模态正常 | U |
| WEB-02 | AP 模式外部浏览器 | 本机无 Wi-Fi 客户端 | 手机连 `RF-Lab-XXXX`,浏览器开 `192.168.4.1` 可操作 | U |
| WEB-03 | STA 运行中掉线回退 | 只有失败启动→AP 回退,没有运行中掉线 | 运行中关路由器/拔 AP,15s 内开 AP,页面可恢复 | U |
| WEB-04 | Pages 线上交互回归 | 部署后未在线点过 | 线上 ARM/SEND/LONG/STOP/模拟学习可用;零 `/api`、零外部请求 | U+A |
| WEB-05 | Safari / Firefox | 只在 Chromium 159 验过 | iframe + `postMessage` + SSE shim 在 Safari/Firefox 可用 | U |
| WEB-06 | 低端手机帧率 / 读屏 | 未测 | 低端机可操作;`aria-live`/tab 顺序可用 | U |
| UC-01 | uConsole 真机 | 只有主机 12 项 + PTY 布局 | 1280×720 字体/中文宽度可读;`Ctrl+Alt+R` 打开;HTTP 与串口模式都能操作 | U |

#### 工程化 / 发布

| ID | 待验证 | 当前缺口 | 通过标准 | 执行 |
|---|---|---|---|---|
| CI-03 | 文档一致性 | README/site/AGENTS 有旧版本号、旧测试计数 | 脚本校验版本/体积/条数/链接;过期值失败 | A |
| CI-05 | required checks | push/PR 已跑 host/SAN/firmware/site,但 `master` 无 branch protection | 配 required checks,失败阻断合并 | A |

> CI 已绿(host 含 `SAN=1`、static、`pio run -e esp32c3`),见
> [`evidence/2026-11-22-ci-gate.md`](evidence/2026-11-22-ci-gate.md);唯一缺口是 required checks。

### 3.3 P2 — 覆盖率与硬化

| ID | 项 | 说明 | 执行 |
|---|---|---|---|
| COV-01 | BLE 分帧纯 C 单测 | 把按 `\n` 重组/分片逻辑从 NimBLE 回调里抽出,主机测 `MTU-3` 分片、半包、粘包 | A |
| COV-02 | `web_server` / `net` / OTA 流主机测试 | 目前只测 `rf34dl/codebook/link/ota_ticket/ui`;路由、状态机、上传流没有主机测试 | A |
| COV-03 | 64 条满库真机 | 主机覆盖 64 条最大载荷;真机 JSON/BLE 快照未压满 | U |
| COV-04 | 模块 VCC–GND / 冷启动失效曲线 | 11-21 只做到「重焊后短暂恢复」;没量电压、没做失效曲线、没换模块对照 | U |
| DOC-01 | `gen_wiring.py` 版本字面量 | SVG 里仍写 `● LIVE // 0.6.0`;应改为读固件版本或去掉版本号 | A |
| DOC-02 | 官网 demo 体积 | `terminal.html` 127,587 B 触发 120KB WARN;要么提高阈值并说明,要么压页面 | A |
| SEC-01 | BLE PIN 轮换 | 固件只生成/打印 PIN,无重置入口;证据日志曾含一次 PIN(已脱敏,历史提交仍可能保留) | 串口 `PIN RESET` 重新生成并持久化;文档说明 PIN 是应用层门 | A |
| WATCH-05 | 手表 APP 自更新 | 真机点 UPDATE:发现新版→下载→SHA-256/包名/版本/签名校验→系统确认安装;断网/取消失败关闭 | U |

> 手表 WATCH-01/03/04 的 46mm 部分已在 2026-11-22 关闭(OW20W1 / API 27);
> 剩余 WATCH-05。0.1.4 为 pre-release;`PackageInstaller` 失败后改走 `ACTION_VIEW`,
> 系统安装界面未验。见 [`evidence/2026-11-22-watch-update.md`](evidence/2026-11-22-watch-update.md)。

### 3.4 明确不做(不是测试债务)

滚动码克隆/重放、0.7.x 的原始 OOK 波形记录(0.8.0 CC1101 只做原始 RX/TX,见 ADR-001)、
扫频/RSSI/频谱、BLE 链路加密、secure boot/固件签名、云账户/公网访问、静默安装、应用商店分发。
这些是范围边界,不要用「未验证」包装成待办。

## 4. 关键风险与判断

1. **P0:把 11-15 的发射证据当成当前状态。**
   11-15 的 `F0` 是真的,但 11-21 模块命令方向失效也是真的;11-22 只证明 UART 恢复。
   **在 `SEND` 复测前,对外只能说「历史证据存在,当前待复测」。**

2. **P1:CI 绿但 merge 不守门。**
   主机 11k checks 只在人记得跑、或 PR 跑过之后靠自觉;`master` 没有 required checks,
   失败不会阻断合并。配置 required checks 比再写 1k 个断言更优先。

3. **P1:模块故障可能是硬件/供电,不要用软件重试掩盖。**
   11-21 的现象是「整段 0 字节」,不是「迟到」;`ACK_WAIT_MS` 加大没有用。
   先量 VCC–GND、RXD 电平、焊点,再做冷启动失效曲线和换模块对照。

4. **P2:0.8.0 的四个模块不是同一类能力。**
   YS-RF34DL 是固定码编解码模块;CC1101 是原始收发器;NRF24L01 是 2.4G 包链路;
   SI4463/LR1121 是另一类。**不能把它们都写成「支持 315/433 固定码」**。
   0.8.0 必须先有「能力矩阵」,UI 按能力启用/禁用。

## 5. 开发计划

> 以下为规划假设:时间不含器件到货、打样、返修等待;**以 gate 为准,不以日历为准。**

### 5.1 0.7.2 — 验证闭合(收尾)

**状态**:0.7.2 已作为代码/兼容版本发布(tag `v0.7.2`,APK + `firmware.bin` + `SHA256SUMS`);
测试工具、CI、发布工程已落地。**只剩射频空口与端到端两类上机项**。

| 剩余 | 内容 | 执行 | 产出 |
|---|---|---|---|
| 射频 | 冷启动;焊点/电压;`SEND/LONG/STOP` ×10;接收器观察;距离 | U | 新 evidence + 失败次数表 |
| 端到端 | BLE `SEND`/错误 PIN/持久化;OTA 浏览器/回滚/AP;uConsole;AP 浏览器 | U | 每项一条 evidence |
| 文档/仓库 | README/site/AGENTS 版本一致;required checks | A | CI/文档更新 |

**0.7.2 退出条件(gate):**

- RF-01/RF-02 关闭,或有用户签字的「明确不做」理由;
- P1 中 BLE/OTA/WEB/UC 的阻塞项关闭;其余列为已知限制;
- 设备运行 `build=` 不含 `dirty` 的 tag 构建(已满足);
- CI 在 master 上绿(已满足);required checks 已配置(待);
- README/site/AGENTS 的版本、条数、体积与证据一致;
- 24h 静态无新增 433M 幻码(或已有明确告警/过滤)。

### 5.2 0.7.3 — UI 一键固件更新(建议 2–3 周)

**目标**:设备页里完成「检查最新版 → BOOT 物理确认 → 浏览器下载 → 设备校验 SHA-256 →
写入非运行槽 → 重启 → 显示新 build」,不再手工找 `.bin`/抄 token。完整行为契约见
[`01-ui.md`](01-ui.md) §3.4.1;手动「镜像 + token」上传保留。

**依赖**:0.7.2 gate 通过;双槽 OTA 的浏览器上传/回滚用例先有结果;Pages CORS 已核
([`evidence/2026-11-22-pages-cors.log`](evidence/2026-11-22-pages-cors.log))。

**不在范围**:Android APP 的 BLE OTA、设备自己拉 GitHub、固件签名/secure boot、APK 更新。

**已拍板(2026-11-22)**:物理确认 = **BOOT**;固件源 = **GitHub Pages**;0.7.3 **独立发版**。

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

> 接口、manifest 校验、arm 契约与测试项见 [`01-ui.md`](01-ui.md) §3.4.1;本文件只留里程碑、依赖与 gate。

#### 5.2.3 风险 / 开放决策

1. **BOOT 可达性**:当前裸板可用;1.0 外壳必须给 BOOT 留可达(开孔/硅胶键)。
   若 1.0 结构做不到,单独立项硬件方案。
2. **Pages 可达性**:GitHub Pages 在部分网络不可达;失败必须回退手动上传,不能假装成功。
3. **无签名(未决)**:SHA-256 只保证字节一致,不保证来源;Pages/仓库被篡改仍可能下发恶意固件。
   是否在 0.8.x/1.0 加 detached signature,单独 ADR。
4. **AP 模式无外网**:浏览器查不到 manifest;只能手动文件或先接 LAN。
5. **内存/超时**:1.1MB 下载 + 上传;低端手机与慢 Wi-Fi 要设 8s 检查/300s 上传超时并给出可重试文案。
6. **Pages 触发**:release 完成后用 `workflow_run` 重新部署;任何部署路径都必须先
   hydrate 最新 Release 的固件,不能让 master 部署把 `/firmware/` 冲掉。

**0.7.3 退出条件:** UI-26/UI-27 全过;真机完成 happy path + 坏 SHA + 中断 + 旧固件拒绝;
Pages `/firmware/manifest.json` 与 `.bin` 均 200 且 CORS `*`;设备运行 tag 构建;
`/api/state`/SSE/串口无 token;AP/离线时手动 fallback 可用。

### 5.3 0.8.0 — 模块抽象层 + SPI 背板(建议 4–6 周)

**先设计,后写驱动。** 0.8.0 的难点不是「多写四个驱动」,是能力模型和硬件边界。

#### 5.3.1 三份 ADR/规格

| 决策 | 要回答的问题 |
|---|---|
| ADR-001 射频抽象接口 | `probe/init/learn_poll/send/long/stop/capabilities` 的最小接口;错误如何上报;哪些能力是模块可选 |
| ADR-002 舱位/供电/引脚 | SPI 共享 + 每舱 CS;是否需要每舱电源开关;3.3V LDO/去耦;C3 GPIO 预算;天线隔离 |
| ADR-003 码库模型 | 继续用 `band` 还是升级为 `module/profile/params`;YS 固定码与 CC1101 原始码如何共存;迁移路径 |

**状态(2026-11-24)**:[ADR-001](adr/ADR-001-radio-hal.md)(射频 HAL/干净室)与
[ADR-003](adr/ADR-003-codebook-raw.md)(码库/RAW 存储)已写;ADR-002 依赖背板 rev A,由硬件线定稿后写。
**已锁**:0.8.0 码库与分区表零改动;CC1101 只做原始 RX/TX;raw 只存 RAM(4,096 段)+ 浏览器 `.sub`;
`.sub` 导入排在 0.8.x 且只读。台账与依据见 [`04-ref-mynova.md`](04-ref-mynova.md)。

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
| M1 | `radio` HAL + YS backend 重构(**M1a 发射 ✅ / M1b 接收**) | 0.7.2 的 host/device 回归全过,行为零变化(金样本) |
| M2 | 背板 rev A + 舱位探测 | `BAY`/`PROBE` 能识别空舱/YS/CC1101;供电/发热/引脚记录 |
| M3 | CC1101 backend(原始 RX/TX) | 主机 mock + 真机收发;与 Wi-Fi/BLE 共存;Flash/RAM 预算达标 |
| M4 | NRF24L01 backend(2.4G 包) | 点对点包收发;UI 只显示包能力,不冒充固定码 |
| M5 | SI4463/LR1121(0.8.x) | 每个模块单独验收,不打包宣称 |

**进展(2026-11-24)**

- **M1a 完成(仅主机/构建)**:新增 `radio.c`(能力位/状态/错误)+ `radio_ys.c`(组帧/F0/FE/FF),
  `SEND/LONG/STOP` 已改走 HAL;帧与旧 `rf34dl_build_tx` 逐字节一致(单测直接对比)。
  主机 **11,312 checks**(新增 radio 114);固件 **1,154,240 B / 2,031,616 B(56.8%)**、
  RAM **90,140 B(27.5%)**。证据:[`evidence/2026-11-24-m1a-radio-hal.md`](evidence/2026-11-24-m1a-radio-hal.md)。
  **真机未复测**——空口行为仍以本文件 §3.1 的 RF-01/RF-02 为准。
- **M1b 未做**:`rf34dl_parser_t` 仍在 `main.c`,YS 能力位暂只有 `FIXED_TX|LONG`;
  迁完才置 `FIXED_RX` 并启用 `learn_poll`。
- M2 依赖背板 rev A;M3 未开始。

#### 5.3.4 0.8.0 测试策略

- **Host**:每个驱动的寄存器序列/状态机用 mock HAL 单测;YS 金样本回归;错误路径(空舱、ID 读错、SPI 超时)。
- **Device**:`PROBE`/`BAY` 自检;传导/近场收发(衰减器 + 假负载);接收器/学习遥控器观察;距离。
- **共存**:Wi-Fi + BLE + 2.4G 模块同时开;记录丢包/重连/功耗。
- **资源**:每加一个驱动记录 Flash/RAM;保持 OTA 槽 <70%,不把 1.94MB 用满。
- **UI**:能力矩阵驱动按钮禁用;空舱/不支持模块有明确文案;官网仍标「路线图」直到 Device 证据。

#### 5.3.5 外部参考吸收(MYNOVA RFC V2)

M1a 已落地;M2/M3 的驱动代码等背板与 0.7.2 gate。外部参考只做**判断**,台账与证据见
[`04-ref-mynova.md`](04-ref-mynova.md):

- **可拿**:CC1101 异步收发/4 档预设/RMT 分段接收/3 档灵敏度/长间隔回放/.sub 语法/HA 发现机制/
  主机测试 seam/打包脚本;全部按 TI 数据手册与公开格式**重写**,不拷 GPL 代码。
- **不拿**:上游 54,762 行里的 UI/游戏/音频/对讲;无 SHA-256 的 OTA;200 个 HA 实体;
  含凭据的备份;零鉴权的固件上传。
- **存储**:0.8.0 的 RAW 先做「RAM 内录制 + 浏览器上传/下载」,不动分区表;
  持久化到数据分区是 0.9+/1.0 的决定,需要与背板 USB 重烧合并。
- **首版主机测试**(M3):预设寄存器逐项回读;RX 进入 0x0D / TX 进入 0x13;PATABLE 两字节与
  FREND0;长段拆分与奇数原子补位;双帧才确认;取消/超时能退出。`.sub` round-trip 与 HA 发现
  放在 0.8.x/0.9.0。

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
| Discovery | 快捷位 + `send_last`/`stop` 按钮 + 码库数/RSSI/uptime/电池传感器;**全码库实体不做默认**(开关另行评估) |
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

- 版本只在固件/APK 有改动时发;纯文档/工具/CI 不重复打 tag。已发布的 `0.7.2` 不重发。
- 0.7.3 起每个 Release 至少附:APK(如改动)、`firmware.bin`、`firmware-manifest.json`、
  `SHA256SUMS.txt`、`build=`/`ui=`、CI run。0.7.2 已附 APK + `firmware.bin` + `SHA256SUMS`(无 manifest)。
- 0.7.3 起 `versionName`、tag、APK、固件、manifest 五处必须一致;官网状态板在发版后同步。
- Pages 部署时从**最新 Release**注入 `site/firmware/`,不把二进制提交进 git;
  master 的 Pages 重新部署不得把固件目录冲掉。
- 证据只写新文件,不改旧值;失败也写,失败比「没跑」有价值。

## 6. 执行顺序(当前)

| 顺序 | 任务 | 执行 | 依赖 |
|---|---|---|---|
| 1 | 冷启动 + 焊点/电压 + `SEND/LONG/STOP` ×10 + 接收器观察 | U | 自有接收器/学习遥控器 |
| 2 | BLE `SEND`、错误 PIN、写入重启持久化 | U | 手机 + 设备 |
| 3 | OTA 浏览器上传/崩溃回滚/AP;uConsole;AP 浏览器;STA 掉线 | U | OTA 镜像/token、路由器 |
| 4 | required checks + README/site/AGENTS 版本一致性 | A | 无 |
| 5 | 关闭 0.7.2 gate 后,再开 0.7.3 M1 后端(arm/BOOT/SHA-256) | A+U | 1–4 |
| 6 | 0.8.0 M1b 接收解析迁入 backend;M2 背板 rev A | A+U | M1b 无;M2 需背板 rev A |

**并行原则:** 需要硬件的(U)和纯主机的(A)分开跑;A 不阻塞 U,A 也不替 U 下结论。
**停止线:** 第 1 项没有结果前,不接受「空口发射已恢复」;0.7.2 gate 关闭前,不开始 0.7.3/0.8.0 的新能力。

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
