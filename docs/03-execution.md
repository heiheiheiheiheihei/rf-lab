# RF-Lab 执行交接(2026-11-27)

> **用途**:另起会话时先读这一页,再按链接读规格。
> **状态、测试债务、版本计划只以 [`02-plan.md`](02-plan.md) 为准**;本页只留锁定决策、当前队列、启动提示和禁止事项。
> 仓库 `/home/myf/DSH/Dev/rf-lab` · `master c7ae2a3`(已推送)· 对外固件/APK **0.7.2**

## 0. 一句话

**先关闭 0.7.2 的 RF-01/RF-02(空口发射),再跑 BLE/OTA/uConsole 端到端;0.7.3 与 0.8.0 的新能力等 gate。**
0.8.0 M1a 只是把 `SEND/LONG/STOP` 套进 radio HAL,行为零变化,**不代表空口已恢复**。

## 1. 已锁定决策(不再讨论)

| 决策 | 锁定值 | 后果 |
|---|---|---|
| 0.7.3 物理确认 | **BOOT(GPIO9)按住 2s** | 1.0 外壳必须给 BOOT 留可达 |
| 0.7.3 固件源 | **GitHub Pages `/firmware/`** | 浏览器拉 manifest/bin;设备只收 HTTP 上传;不可达时手动 fallback |
| 0.7.3 发版 | **独立 0.7.3** | 0.7.2 不加新功能 |
| 下载/校验 | 浏览器下载,**设备算 SHA-256** | 设备页是 HTTP,不是 secure context,不用 `crypto.subtle` |
| 授权 | BOOT 物理确认 → token;token 绑客户端 IPv4 | HTTP/BLE 不能单独 arm;`POST /api/cmd OTA` 仍 serial-only |
| 范围 | Web UI only | Android APP 仍 BLE-only;不做 BLE OTA/APK 更新;uConsole 不加更新入口 |
| 签名 | 0.7.3 不做 | SHA-256 不是签名;没有 secure boot;detached signature 另案 |

完整行为契约:[`01-ui.md`](01-ui.md) §3.4.1;版本/里程碑:[`02-plan.md`](02-plan.md) §5.2。

**0.8.0 前置决策(2026-11-24,细节见 [`adr/`](adr/README.md))**

| 决策 | 锁定值 | 后果 |
|---|---|---|
| 许可 | 本仓库保持非 GPL;**清洁室重写** | 不拷 MYNOVA/Flipper 代码;事实从 [`04-ref-mynova.md`](04-ref-mynova.md) 取 |
| CC1101 范围 | 0.8.0 只做**原始 RX/TX** | 不做协议解码、不做 `.sub`、不做 `LONG/STOP` |
| RAW 存储 | **RAM 会话(4,096 段/16KB)+ 浏览器 `.sub`** | 码库与分区表零改动;raw 不进 NVS |
| `.sub` | 0.8.x **只读导入**;Key 可存(v3),RAW 只进会话 | 写回放 1.0 |
| HA 实体 | 快捷位 + `send_last`/`stop` + 传感器 | 全码库实体默认关 |

## 2. 当前队列

| 顺序 | 任务 | 执行 | 通过标准 |
|---|---|---|---|
| 1 | 冷启动 + 焊点/电压 + `SEND/LONG/STOP` ×10 + 接收器观察 | U | ≥9/10 回 `F0`;`LONG` 持续、`STOP` 停止;记录失败曲线 |
| 2 | BLE `SEND`、错误 PIN、`NAME/GROUP/SYM/EDIT` 重启持久化 | U | 回显 `OK ... (0xF0)`;5 次错误断开;重启后码库保留 |
| 3 | OTA 浏览器上传/崩溃回滚/AP;uConsole;AP 浏览器;STA 掉线 | U | 每项一条 evidence;失败也写 |
| 4 | CI-03 文档一致性脚本(required checks 已于 11-27 配置) | A | 版本/体积/条数/链接过期时失败 |
| 5 | 0.7.3 M1(arm/status/BOOT/SHA-256/IP 绑定) | A+U | 0.7.2 gate 关闭后 |
| 6 | 0.8.0 M1b 接收解析迁入 backend;M2 背板 rev A | A+U | M1b 无;M2 需背板/ADR-002 |

**不要**在本页维护状态:设备版本、测试计数、已关闭项都写在 [`02-plan.md`](02-plan.md) §2–§3。

## 3. 新会话启动提示(可直接粘贴)

```text
仓库:/home/myf/DSH/Dev/rf-lab
先按顺序读:AGENTS.md → docs/02-plan.md → docs/03-execution.md → docs/01-ui.md §3.4.1。
当前 master c7ae2a3(已推送),对外固件/APK 0.7.2。
设备已 OTA 到 build=e0f155a,但空口 SEND 未复测;RF-01/RF-02 仍是 P0。
现在只做 0.7.2 的验证收尾,不开始 0.7.3/0.8.0 的新能力。
发 RF 前先确认用户在场;不要改 docs/evidence 的旧值。
先给改动计划,再动手;完成后分别报告 Build/Host/Device/Unverified。
```

## 4. 禁止事项

1. 在 RF-01/RF-02 复测前,不要把「发射」写成已验证;11-15 的 `F0` 只是历史证据。
2. 在 0.7.2 gate 关闭前,不写 0.7.3/0.8.0 的新能力代码;M1a 是纯重构,不是例外先例。
3. 不要让 HTTP/BLE 在没有 BOOT 确认时 arm;不要把 SHA-256 写成签名/secure boot。
4. 不要把 1.1MB 固件提交进 git;`site/firmware/` 由 CI 注入。
5. 不要改 `docs/evidence/` 旧值;新结果写新文件。
6. 不要把「能探测到 CC1101」写成「能收发」;能力矩阵先写死。
7. 推送前按 `AGENTS.md` 约束 13 检查版本/Release;纯文档推送不重复打 tag。

## 5. 文档地图

| 文档 | 用途 |
|---|---|
| [`AGENTS.md`](../AGENTS.md) | 唯一入口、硬约束、路由、验证命令 |
| [`02-plan.md`](02-plan.md) | 测试债务 + 版本计划(状态唯一所有者) |
| [`01-ui.md`](01-ui.md) §3.4.1 | 一键更新行为契约 |
| [`00-mvp.md`](00-mvp.md) | 射频/命令规格与 AC |
| [`04-ref-mynova.md`](04-ref-mynova.md) | 外部参考台账(非规格/非计划) |
| [`adr/`](adr/README.md) | 架构决策(为什么这样定) |
| [`evidence/`](evidence/) | 实测事实(不改旧值) |
| [`../README.md`](../README.md) | 对外状态摘要 |
