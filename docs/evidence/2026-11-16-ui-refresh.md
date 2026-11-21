# 2026-11-16 UI refresh — RF-LAB 品牌 + 频谱带(布局 / 构建证据)

状态:2026-11-16 · 固件 **0.6.0(仅 Web 资源 refresh)** · 板 `RF-Lab-1B59`
范围:`web/index.html` + `web/app.css` + `web/app.js`;不改命令、HTTP/SSE、BLE 或射频代码。
旧皮肤证据:[`2026-11-15-ui.md`](2026-11-15-ui.md) · 规格:[`../01-ui.md`](../01-ui.md)

> **这不是新皮肤的上机截图。** 本次只重建了固件/APK 并做 mock API 布局验收;
> 设备没有重新烧录,真实 LAN 图仍是 0.6.0 旧皮肤。不要把下面任何一张 mock 图写成设备截图。

## 1. 结论

| 项 | 结果 | 证据等级 |
|---|---|---|
| Web 资源 | 55,052 B → gzip **15,504 B**(28.2%);无外部请求;`node --check` 通过 | ✅ 主机 |
| 固件构建 | `pio run -e esp32c3` 成功;总 image **1,067,804 B**,RAM **55,732 B**;`factory=0x200000` | ✅ 主机 |
| Android | `assembleDebug` 成功;APK **1,275,236 B**;assets 与 `web/dist` 同哈希 | ✅ 构建 |
| 主机单测 | 协议 76 + 事件环 53 + OLED/UI 1286 + TUI 12,全过 | ✅ 主机 |
| 布局(mock) | 1440×900 / 1280×720 / 390×844;无 console error;`scrollWidth == innerWidth` | ✅ 浏览器(mock) |
| 设备新皮肤 | **未重新烧录、未经设备浏览器验收** | ⏳ 未验证 |

## 2. 改了什么

- 品牌锁定:`RF-LAB` 字标 + `百元射频实验室` 中文副标;顶部 `RF-LAB` 标与
  `LAB 100—999` 系列标识。
- 状态栏新增 `BAY YS-RF34DL`:当前唯一射频舱位,不暗示其他模块已接入。
- 底部新增 96 柱 `RF SPECTRUM // 315 / 433 MHz` 装饰带(纯 CSS 动画 + 本地生成 DOM)。
- 全页新增非常轻的扫描光束;`prefers-reduced-motion` 下关闭。
- **未改**:`SEND/LONG/STOP` 命令、SSE/BLE 传输、所有元素 ID、ARM 语义、API 契约。

布局改动只在 1440/1280 宽度显示频谱带;`max-height:760px` 时隐藏,保证
uConsole 1280×720 的三栏仍完整。移动端品牌区 `min-width:0`,修复长 eyebrow 导致的
14px 横向溢出。

## 3. 复现

```bash
# 资源 / 主机
python3 tools/build_web.py
node --check web/app.js
./tools/host_test.sh

# 固件(构建后由 post script 校验 2MB 分区)
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd firmware && pio run -e esp32c3

# Android
export JAVA_HOME="$HOME/.local/opt/jdk-17"
export ANDROID_HOME="$HOME/.local/opt/android-sdk"
cd android && ./gradlew assembleDebug

# 布局截图:mock API(不是设备)
python3 tools/ui_mock_server.py --port 8765
# 浏览器打开 http://127.0.0.1:8765/,分别用 1440×900 / 1280×720 / 390×844 截图
```

## 4. 截图(mock API)

| 视口 | 文件 | 检查 |
|---|---|---|
| 1440×900 | [`ui-2026-11-16-desktop.png`](ui-2026-11-16-desktop.png) | 品牌/BAY/频谱带;三栏无溢出 |
| 1280×720 | [`ui-2026-11-16-uconsole.png`](ui-2026-11-16-uconsole.png) | uConsole 逻辑分辨率;频谱带按高度隐藏 |
| 390×844 | [`ui-2026-11-16-mobile.png`](ui-2026-11-16-mobile.png) | 单列;`scrollWidth == 390`;按钮/状态栏可读 |

截图由 `tools/ui_mock_server.py` 提供 5 条码、`5/32`、37 frames、15 ACK 和一条
learn/send 事件;和 2026-11-15 的 mock 口径一致。不是上机图。

## 5. 未验证 / 边界

1. **新皮肤上机**:没有重新烧录,设备浏览器/OLED 未复核;旧皮肤真实 LAN 图仍见
   [`ui-lan-live.png`](ui-lan-live.png)。
2. **Android 真机**:只验证 `assembleDebug` 与 assets 一致性;安装/BLE 未测。
3. **uConsole 真机**:1280×720 只看 mock 布局;真机字体/键位未测。
4. **BLE**:没有第二台 BLE 主机;广播状态不等于手机连接。

## 6. 取舍

- **不做 Canvas/Three.js 频谱**:C3 的 2MB app 分区与 Web 资源 <80KB 约束优先;
  96 个 CSS 柱 + 本地 DOM 足够表达“在扫”,不引入 canvas 任务/像素回读。
- **不新增数据接口**:频谱带是装饰,不读 ADC/RSSI,避免把“好看的动画”写成测量值。
- **不升版本号**:协议/命令/页面 ID 未变,只是 Web 资源 refresh;0.6.0 保持,
  APK/firmware 重新构建。
- **避免的误修**:❌ 把新皮肤直接说成“已上机”;❌ 为了频谱带引入外部字体/CDN;
  ❌ 改 `web/app.js` 的传输或命令分支。

## 7. 官网线上(2026-11-16)

官网 `site/` 已直传到 Cloudflare Pages 项目 `rf-lab`:

- 生产地址:<https://rf-lab-388.pages.dev/>
- 部署方式:Direct Upload,只上传 `site/`;没有绑定 Git provider,不需要把 API Token 写进仓库。
- 主机检查:`/`、`/style.css`、`/app.js`、`/assets/terminal-web.webp`、`/robots.txt`、
  `/sitemap.xml` 均 **200**;Chromium 390×844 无 console error,配装器 100/272/988 正常。
- 截图仍是 mock 布局图;线上地址不等于设备上机验证。


## 8. 2026-11-16b 字标修正（英文连字符）

按反馈把品牌字标从 `RF—LAB` 改为 `RF-LAB`（英文 `-`，保留琥珀色）:

| 项 | 2026-11-16 | 2026-11-16b |
|---|---|---|
| Web 单页 | 55,052 B | 55,050 B |
| gzip | 15,504 B | 15,503 B |
| Android assets | 55,052 B | 55,050 B |
| APK | 1,275,236 B | 1,275,234 B |

截图更新为 [`ui-2026-11-16b-desktop.png`](ui-2026-11-16b-desktop.png)、
[`ui-2026-11-16b-uconsole.png`](ui-2026-11-16b-uconsole.png)、
[`ui-2026-11-16b-mobile.png`](ui-2026-11-16b-mobile.png)。仍是 mock 布局图;
命令/API/元素 ID 未变,新皮肤仍未重新烧录上机。

> 2026-11-21:官方入口迁移到 GitHub Pages;旧 Cloudflare 地址保留为历史镜像,
> 见 [`2026-11-21-pages.md`](2026-11-21-pages.md)。
