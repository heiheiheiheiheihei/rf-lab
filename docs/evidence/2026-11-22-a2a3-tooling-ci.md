# 2026-11-22 Phase A A2/A3:device_selftest 版本无关化 + CI 骨架

状态:`measured(仅主机)` · **未上机、未发 RF、未改设备** · 本轮改动未提交
仓库:`/home/myf/DSH/Dev/rf-lab` · `HEAD dd71bf6`(计划基线 `e5a3fbd` 之上的纯文档提交)
目的:执行 [`03-execution.md`](../03-execution.md) Phase A 的 **A2**(修测试工具)与
**A3**(加 CI 守门),对应 [`02-plan.md`](../02-plan.md) §3.1 **TST-01**、§3.2 **CI-01/CI-02**。

> 本文只证明「主机侧工具与 CI 定义可用」。**没有**在真机上跑过新自检,
> 也**没有**在 GitHub 上跑过新 workflow;两项都在 §5 Unverified 里。

## 1. Build

```bash
cd firmware && ~/.platformio/penv/bin/pio run -e esp32c3   # pio 不在 PATH,用本地 penv
```

```text
RAM:   22.4% (used 73540 bytes from 327680 bytes)
Flash: 54.0% (used 1097968 bytes from 2031616 bytes)
[partition] image 1098608 B / slot 2031616 B (54.1%)
========================= [SUCCESS] Took 71.83 seconds =========================
```

| 项 | 值 |
|---|---|
| 产物 | `firmware/.pio/build/esp32c3/firmware.bin` **1,098,608 B** |
| sha256 | `e52a5312016689a8a741693209144b4489160d50178361e29eaba098aa2ca1df` |
| 槽 | 2,031,616 B(54.1%) |
| 说明 | 本轮**没有改固件源码**;工作树含 A2/A3 改动,所以此镜像不对应 tag,不作为发布物 |

> 该 `.bin` 已在 08:58 被 `tools/flash.sh` 的构建覆盖(当前文件 sha256
> `43e9863ef03633c4393be9891136a90cc87edd42fef925274eea4b4349213679`,见
> [`2026-11-22-firmware-flash.md`](2026-11-22-firmware-flash.md));上表是 08:47 那次构建的值。
> 同源两次构建 sha256 不同(app descriptor 带构建时间),不能拿它当可重建证明。

## 2. Host(全部通过)

| 命令 | 结果 |
|---|---|
| `./tools/host_test.sh` | 协议 76 + 码库 218 + 事件环 9,418 + OTA 41 + OLED/UI 1,286 = **11,051**,0 failure;wiring/web/partitions 检查 OK;TUI **12** OK;selftest **18** OK |
| `SAN=1 ./tools/host_test.sh` | 同上,ASan/UBSan 0 failure |
| `python3 -m unittest discover -s tools -p 'test_device_selftest.py' -v` | **Ran 18 tests … OK** |
| `node --check web/app.js` / `site/app.js` / `site/assets/terminal-demo.js` | OK |
| `python3 tools/gen_wiring.py --check` | `4 nets / 4 wires; header + 4 SVGs + 2 doc tables OK` |
| `python3 tools/build_web.py --check` | `ui 85a1a9b4`;gzip 30,068 B(23.2%);与源一致 |
| `python3 tools/site_check.py` | `SITE OK`(1 条既有 WARN:`terminal.html` 127,490 B) |
| `python3 tools/device_selftest.py --help` | 列出 `--tx` / `--tx-repeat` / `--json` / `--log` |
| `python3 tools/device_selftest.py --port /dev/rflab-does-not-exist --json` | 5s 重试后 **exit 2**,stdout 为 JSON:`ok=false` + `error` |

### 2.1 A2 的关键回归(取自真机记录,不是编的)

fixture 直接抄 [`2026-11-22-device-audit.md`](2026-11-22-device-audit.md) §2.1/§2.2 的
真实 `INFO`(`fw=0.7.1`、`build=511afca+dirty`、`ui=85a1a9b4`、`count=13/32`)与 13 行 `LIST`:

- `analyze()` 对该 transcript 的所有一致性检查均为 **PASS**,无 `fw=0.6.0`/`stored=5` 断言;
- 反向用例:STATE fw 改成 `0.6.0`、LIST 少一行、STATE 非 JSON → 对应检查 **FAIL**;
- 启动横幅因 USB 重枚举丢失 → `SKIP`(不是假红);`RESET` 完全无输出 → `FAIL`;
- 无 pyserial 时 `import`/解析照常,只有打开串口才报依赖错误。

`LIST LONG` 原文未在巡检里留存,fixture 按 `cmd_list_long` 格式从真实 `LIST` 派生
(仅第 1 条带 name/group/sym);`manual/edited` 后缀另用一条小样本测解析。

### 2.2 判读

- **根因**:旧脚本把「当前版本 = 0.6.0、条数 = 5」写成常量,设备一升级就制造**假红**,
  失败原因在脚本而不在设备(即 `02-plan.md` §4.3)。
- **Key judgment**:版本/条数只从设备读,再让两条独立路径互证 —— `LIST` vs `INFO`,
  `STATE` vs `INFO`。**不**引入「当前应该是什么版本」的第二套常量。
- **Avoided misfix**:没有把断言放宽成「只打印不断言」或「永远 PASS」;不一致仍然 FAIL,
  否则工具会从假红变成假绿,比现在更糟。
- **SKIP 语义**:OLED 不在 → `SKIP`(与 RF-06 无屏降级一致),既不是 PASS 也不是 FAIL。

## 3. CI(A3)

新增 [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml):`push master` / `pull_request` /
`workflow_dispatch` 三个触发,三个 job:

| job | 内容 |
|---|---|
| `host` | `host_test.sh` + `SAN=1 host_test.sh` + JDK 17 + `app_update_test.sh` |
| `static` | py 语法 + `device_selftest --help` + 3×`node --check` + `gen_wiring --check` + `build_web --check` + `site_check` |
| `firmware` | `setup-python 3.12` + `pip install "platformio>=6.1,<7"` + 缓存 `~/.platformio` + `pio run -e esp32c3` |

- YAML 已用 `yaml.safe_load` 解析通过(3 jobs / 3 triggers);本机无 `actionlint`。
- **未推送、未在 GitHub 上运行**;`master` 当前**没有分支保护**
  (`gh api .../branches/master/protection` → `Branch not protected`),
  所以「失败阻断合并」还需要在仓库设置里把 CI 设为 required status check(账号有 admin,未擅自改)。

## 4. 改动文件

| 文件 | 改动 |
|---|---|
| `tools/device_selftest.py` | 版本无关(INFO/LIST/STATE 交叉校验)、`--json`、`--tx-repeat`、串口重枚举重试、HELP/LIST LONG/DISP SHOW 只读检查;`--tx` 门保留;`--log` 改为追加(不截断证据转录) |
| `tools/test_device_selftest.py` | 新增:18 项主机单测(真机 transcript 回归 + 反向用例 + 假设备端到端 JSON) |
| `tools/host_test.sh` | 新单测挂进主机测试(一处入口) |
| `.github/workflows/ci.yml` | 新增 CI 守门 |
| `AGENTS.md` | 路由表加「CI 守门」;`host_test.sh` 注释补 selftest 17 |
| `docs/03-execution.md` | 验收命令补 `--json` / `--tx-repeat` 可选参数 |

## 5. Unverified(明确没做)

1. ~~真机运行新自检~~ **2026-11-22 13:46 已上机**(0.7.1/32 条,26 pass / 0 fail / 1 skip,
   未发 RF):见 [`2026-11-22-device-selftest.md`](2026-11-22-device-selftest.md);
   本文的离线 transcript 仍保留为可重复的主机回归。
2. **CI 实际运行**:workflow 未推送,首次绿/红灯未知;`platform = espressif32` 未锁版本,
   CI 首次可能拉到与本地不同的平台包。
3. **失败阻断合并**:需要仓库分支保护/required checks,未配置。
4. **`app_update_test.sh` 本机未跑**:本机无 JDK;该步骤只在 CI `host` job 里覆盖。
5. `tools/wifi_selftest.py` 的日志头仍写 `0.6.0`(只是注释、没有断言),本轮未动。
