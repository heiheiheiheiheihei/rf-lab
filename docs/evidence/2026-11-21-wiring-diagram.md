# 2026-11-21 接线图/硬件效果图 — 抽象生成版(v2)

状态:2026-11-21 · 固件 **0.6.0 + OTA(仅 Web/艺术资源与接线头)** · 板未上电
范围:`hardware/wiring.json`、`tools/gen_wiring.py`、`firmware/src/wiring_config.h`、
`firmware/src/main.c`、`web/art/*.svg`、`site/index.html` / `site/style.css`、
`web/index.html` / `web/app.css` / `web/app.js`、`tools/build_web.py`、`tools/host_test.sh`、
`firmware/scripts/embed_web.py`。
规格:[`../01-ui.md`](../01-ui.md) §5.3 / `UI-18`。

> **v1 的 3D/写实风板子被否掉了**:离真实硬件远,而且图不是从接线表生成的。
> v2 改成**抽象功能块 + 表驱动**:只画核心元件与四根线,图/表/代码头同源。
> **这不是上机截图。** 设备没有重新烧录,真实 LAN 图仍是旧皮肤。

## 1. 结论

| 项 | 结果 | 证据等级 |
|---|---|---|
| 接线源 | `hardware/wiring.json`:4 条 net,方向/说明/保留脚齐全 | ✅ 文件 |
| 代码头 | `firmware/src/wiring_config.h` 由 JSON 生成;`main.c` 只 include,不再手写 `PIN_*` | ✅ 生成 + 构建 |
| 文档表 | `hardware/README.md` §2、`docs/00-mvp.md` §3 由同一 JSON 生成 | ✅ `gen_wiring.py --check` |
| 图 | 4 个抽象 SVG:宽图 1200×760、竖图 720×1200、两张引脚图 720×760 | ✅ 文件 |
| Web 构建 | HTML **106,274 B**,gzip **24,166 B**(22.7%);4 个 SVG 复制到 `site/assets/` | ✅ 主机 |
| 官网静态 | `site_check` 通过;3 张 `<img>` 本地、alt 完整;gzip 12,577 B | ✅ 主机 |
| 主机测试 | 76 + 53 + **41 OTA** + 1,286 + 12,全过;含 wiring check | ✅ 主机 |
| 固件构建 | `pio run -e esp32c3` 成功;Flash **1,086,162 B** / 2,031,616 B 槽,RAM **56,412 B**,镜像 **1,086,800 B**(53.5%) | ✅ 主机 |
| Android | `assembleDebug` 成功;APK **1,326,458 B**;assets 与 `web/dist` 同哈希 `16ff0cdc…` | ✅ 构建 |
| 浏览器 | 1440×900 / 390×844 无横向溢出、0 外部请求、无 console error | ✅ 本地 Chromium |
| UI 交互 | `HW`/`H` 打开;宽屏内联宽图,窄屏内联竖图;`ZOOM/FIT`;`ESC` 关闭 | ✅ 本地 Chromium |
| 上机 | **未重新烧录、未上机验收** | ⏳ 未验证 |

## 2. 为什么改

v1 的问题:

1. 追求“像实物”,但手绘的 USB-C/OLED/弹簧天线只能做到「差不多」,反而暴露不真实;
2. 图是手画的,和接线表/`PIN_*` 宏没有生成关系,存在“图对代码错”的风险;
3. 项目真正要讲的是**四根线怎么接**,不是 PCB 长什么样。

v2 的取舍:

- **抽象**:两块功能块 + 4 根线 + 引脚/方向/说明;不画 PCB/3D,也不画 USB-C 连接器、晶振、弹簧天线的写实细节(只保留功能块/标签)。
- **表驱动**:`hardware/wiring.json` → 代码头 + 两张文档表 + 4 个 SVG。
- **代码一致**:`main.c` 的 `PIN_MODULE_TX/RX`、`PIN_OLED_*`、`PIN_CONSOLE_*` 与
  `CONSOLE_BAUD` / `MODULE_BAUD_DEFAULT` / `OLED_I2C_HZ` 全部来自生成头;
  `gen_wiring.py --check` 在 `host_test.sh` 与固件构建前执行。

## 3. 唯一源与生成链

```text
hardware/wiring.json
  ├─ tools/gen_wiring.py
  │    ├─ firmware/src/wiring_config.h              ← main.c include
  │    ├─ hardware/README.md  (WIRING block)
  │    ├─ docs/00-mvp.md      (WIRING block)
  │    ├─ web/art/hw-assembly.svg      (wide)
  │    ├─ web/art/hw-assembly-m.svg    (portrait)
  │    ├─ web/art/hw-core.svg
  │    └─ web/art/hw-rf.svg
  └─ tools/build_web.py
       ├─ inline wide/portrait into web/index.html HW modal
       └─ copy 4 SVGs to site/assets/
```

校验:

- `gen_wiring.py --check` 校验 JSON ↔ 生成头 ↔ 文档表 ↔ SVG,并确认
  `main.c` 包含 `wiring_config.h`、不再重复定义 `PIN_*`。
- `embed_web.py` 在固件构建前调用 `gen_wiring.py --check`;不一致直接失败。
- `tools/host_test.sh` 的 wiring 段也调用同一检查。

## 4. 复现与构建

```bash
cd /home/myf/DSH/Dev/rf-lab
python3 tools/gen_wiring.py --check
python3 tools/build_web.py
python3 tools/build_web.py --check
node --check web/app.js
python3 tools/site_check.py
./tools/host_test.sh

cd firmware && PATH="$HOME/.platformio/penv/bin:$PATH" pio run -e esp32c3
cd ../android && JAVA_HOME="$HOME/.local/opt/jdk-17" ANDROID_HOME="$HOME/.local/opt/android-sdk" ./gradlew assembleDebug
```

构建数字(2026-11-21,本机):

- 固件:`pio run -e esp32c3` 成功;Flash **1,086,162 B** / 2,031,616 B 槽,RAM **56,412 B**,镜像 **1,086,800 B**(53.5%);`factory` 不存在,`otadata/ota_0/ota_1` 校验通过。
- Android:`assembleDebug` 成功;APK **1,326,458 B**;assets 与 `web/dist` 同哈希 `16ff0cdc…`。

## 5. 接线正确性(生成表)

| 模块引脚 | 接到 ESP32-C3 | 方向 | 说明 |
|---|---|---|---|
| VCC | `3V3` | 电源 | 3.3V ONLY / never 5V |
| GND | `GND` | 地 | common ground required |
| RXD | `GPIO3`(UART1 TX) | ESP32 → 模块 | cross TX → RX |
| TXD | `GPIO4`(UART1 RX) | 模块 → ESP32 | cross RX ← TX |

保留脚:GPIO5/6(板载 OLED I2C)、GPIO18/19(原生 USB)、GPIO20/21(UART0)、
GPIO8/9(LED/BOOT)、GPIO2(strapping)。这些在 `wiring.json.reserved` 里,图/表同源。

## 6. 页面证据

| 视口 | 文件 | 检查 |
|---|---|---|
| 1440×900 官网 | [`hw-2026-11-21-site-desktop.png`](hw-2026-11-21-site-desktop.png) | §01 宽图 + 接线表;无横向溢出 |
| 390×844 官网 | [`hw-2026-11-21-site-mobile.png`](hw-2026-11-21-site-mobile.png) | `<picture>` 切竖图;四根垂直线可读 |
| 1440×900 UI | [`hw-2026-11-21-ui-desktop.png`](hw-2026-11-21-ui-desktop.png) | `HW` 模态;宽图内联 |
| 390×844 UI | [`hw-2026-11-21-ui-mobile.png`](hw-2026-11-21-ui-mobile.png) | 窄屏内联竖图;`ZOOM/FIT` |
| 矢量原图 | [`wiring`](hw-2026-11-21-wiring.png) / [`wiring-m`](hw-2026-11-21-wiring-m.png) / [`core`](hw-2026-11-21-core.png) / [`rf`](hw-2026-11-21-rf.png) | PNG 预览,方便 README/评审 |

补充检查:

- `document.documentElement.scrollWidth == clientWidth` @ 1440/390。
- 0 外部请求、0 console error;图片加载后 `naturalWidth > 0`。
- 模态:打开后 `hidden=false` 且包含 SVG;`ZOOM` 后 `body.is-zoom`、按钮变 `FIT`;
  `ESC` 关闭并复位;`H` 重新打开。
- 竖图/宽图切换由 CSS `@media` 与官网 `<picture>` 完成,不新增 JS 布局逻辑。

## 7. 未验证 / 边界

1. **未上机**:没有重新烧录;OLED、BLE、真实 LAN 未复核。
2. **抽象图不是比例图**:不能据此开孔/做外壳/量天线;接线以 `wiring.json` 为准。
3. **模块批次差异**:实物可能多脚/不同丝印;本项目只用 VCC/GND/RXD/TXD 四线。
4. **路线图模块未画**:CC1101 / NRF24L01 / SI4463 / LR1121 不进入现役图/表。
5. **OTA 并发改动**:工作树同时包含 OTA 改动;本次只新增接线源/生成器/图,未改 OTA 语义。

## 8. Key Judgment / Avoided Misfix

**Key Judgment**:把“接线正确”做成**可执行约束**,而不是靠人看图。JSON 是唯一源,
代码头/文档表/SVG 全由它生成;`--check` 在主机测试和固件构建前跑。这样改错 GPIO、
把 TX/RX 画反、或代码与图不一致,都会在构建前失败。

**Avoided Misfix**:

- ❌ 继续手绘 3D 板子去“像实物”:越像越容易错,且无法证明接线正确。→ 抽象功能块 + 表驱动。
- ❌ 只改图不改代码宏:图对但固件接错。→ 代码头也由 JSON 生成。
- ❌ 只改文档表:表和代码漂移。→ 两张表都从同一 JSON 生成,`--check` 校验。
- ❌ 把 `GPIO3/4` 画成直连 TX/TX:串口必须交叉。→ 生成器和检查都断言
  `GPIO3→RXD`、`GPIO4←TXD`。
- ❌ 把 CC1101 等路线图模块画进现役图:采购误判。→ 只画 CORE-C3 + YS-RF34DL。
- ❌ 为手机再写一套图:两套图会漂移。→ 同一 JSON 生成宽/竖两个版式,内容只来自一处。
