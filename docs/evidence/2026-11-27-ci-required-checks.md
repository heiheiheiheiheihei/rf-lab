# 2026-11-27:master 推送、required checks 与构建尺寸纠偏

**范围**:仓库/CI 工程,不含射频、BLE、OTA、uConsole。设备本次未接
(`/dev/ttyACM0` 不存在),所以本文没有任何 Device 结论。

## 背景

本地 `master` 领先 `origin/master` 3 个提交,远端停在 `be22aeb`(2026-11-22):

| 提交 | 内容 |
|---|---|
| `c10f451` | C3 底板 PCB 设计(未打样) |
| `f55126f` | 0.8.0 M1a radio HAL(仅主机/构建) |
| `c7ae2a3` | 文档收敛(ADR/plan/ref 台账) |

## 发布约束核对

- `android/app/build.gradle` 的 `versionName=0.7.2`,`versionCode=4`;3 个提交未改
  `android/` 应用代码(只有 `android/README.md`),不 bump 版本。
- `v0.7.2` Release 已存在(固件 `1,126,928 B`、APK `1,050,272 B`),按约束 13
  **纯重构 + 文档不重复打 tag**。

## Build

### 本地固件(干净配置)

```bash
cd firmware
rm -f sdkconfig.esp32c3            # 生成物,.gitignore 已忽略
~/.platformio/penv/bin/pio run -e esp32c3
```

结果:`SUCCESS`;RAM **90,308 B / 327,680 B(27.6%)**;Flash **1,127,738 B /
2,031,616 B(55.5%)**;`check_partitions.py` 报 image **1,128,384 B**。

### 尺寸漂移根因(重要)

同一工作区若保留旧的 `sdkconfig.esp32c3` 再构建,镜像为 **1,154,240 B(56.8%)**、
RAM 90,140 B(27.5%)——与 2026-11-24 M1a 记录的数值一致。差异 **25,856 B** 不是
radio HAL 引入的,而是旧生成配置:

- 旧配置:`CONFIG_LIBC_NEWLIB=y`、`CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_0/1=y`、
  `CONFIG_FREERTOS_IN_IRAM=y` 等;
- 从 `sdkconfig.defaults` 重新生成后:`CONFIG_LIBC_PICOLIBC=y`,protocomm 只保留默认版本;
- 对象清单 diff:旧配置多 `esp_libc/src/flockfile.c`、`newlib_init.c`、`reent_init.c`
  与 `protocomm/security0|1.c`,新配置改为 `picolibc/*` 一组。

**Key Judgment**:以从 `sdkconfig.defaults` 重新生成的干净构建为准,修正文档里的
`1,154,240 B`,而不是把旧生成物当成基线。

**Avoided Misfix**:没有为了保住旧数字去删 `sdkconfig.defaults` 里的证书/组件配置;
也没有把 26 KB 归因于 M1a(CI 与本地干净构建相差仅 **16 B**,M1a 自身约 +1.5 KB)。

### CI 固件

同提交 `c7ae2a3` 的 CI 干净构建:image **1,128,400 B**、RAM 90,308 B(27.6%),
与本地干净构建差 16 B,记录两者,不强行归因。

## Host

```bash
./tools/host_test.sh
```

协议 76 + 码库 368 + 事件环 9,427 + OTA 41 + radio 114 + OLED/UI 1,286 =
**11,312 checks,0 failure**;TUI 12;`device_selftest` 18。

## CI 结果

推送 `be22aeb..c7ae2a3` 触发 run
[46015828224](https://github.com/heiheiheiheiheihei/rf-lab/actions/runs/46015828224):

| job | 结果 | 用时 |
|---|---|---|
| host tests (incl. ASan/UBSan) | ✅ success | 19s |
| web / wiring / site / python | ✅ success | 8s |
| firmware build (esp32c3) | ✅ success | 2m2s |

Pages 部署 run
[46015828148](https://github.com/heiheiheiheiheihei/rf-lab/actions/runs/46015828148):✅ success。

唯一 annotation 是 Node.js 20 deprecation(`actions/cache@v4`、`setup-python@v5`),
不影响结果,留作 P2 升级项。

## 合并守门(CI-05)

`PUT /repos/heiheiheiheiheihei/rf-lab/branches/master/protection`:

| 项 | 值 |
|---|---|
| required checks | `host tests (incl. ASan/UBSan)`、`web / wiring / site / python`、`firmware build (esp32c3)` |
| strict | `true`(分支需与 master 同步) |
| enforce_admins | `false` |
| required PR review | 无 |
| force push / deletion | 均禁止 |

`GET` 回读与写入一致。含义:

- **PR 合并**必须三项全绿;
- **直接 push 仍允许**(未启用 Require a pull request),按约束 13 的发版流程保留;
- 想让 direct push 也过 gate,需要开 Require PR,那会改发版流程,另行拍板。

## Unverified

- **未做失败 PR 实测**:required checks 的实际拒绝效果由 API 配置与 GitHub 语义保证,
  没有构造一个故意失败的 PR 来观察。
- SAN 只在 CI 跑;本地 11-27 未单独跑 `SAN=1`。
- 16 B 的 CI/本地差异没有追到具体 Kconfig 项。
- 设备未接:RF-01/RF-02、BLE、OTA、uConsole 均未触碰。
