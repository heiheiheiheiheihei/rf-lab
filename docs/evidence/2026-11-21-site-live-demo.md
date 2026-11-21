# 2026-11-21 官网交互终端 — 真实 UI 内嵌 + 本地模拟传输

状态:2026-11-21 · 固件 **0.6.0(仅 Web 资源 + CSS 动效)** · 板未上电
范围:`site/index.html`、`site/app.js`、`site/style.css`、`site/assets/terminal.html`(生成物)、
`site/assets/terminal-demo.js`、`tools/build_web.py`、`docs/01-ui.md` §5.2 / UI-17。
规格:[`../01-ui.md`](../01-ui.md) §5.2 · 同源视觉:[`2026-11-21-ui-unify.md`](2026-11-21-ui-unify.md)

> 部署结果见 §8;§5 的“未部署”是推送前的状态。

> **不是上机截图。** 官网终端区跑的是**真实的 Web 单页 UI**,但网络层由本地 shim 接管;
> 设备没有重新烧录,页面上的 `SIM` 不代表真机在线。

## 1. 结论

| 项 | 结果 | 证据等级 |
|---|---|---|
| 官网终端区 | 不再放截图;iframe 载入 `assets/terminal.html`(由同一份 `web/dist` 生成);21/21 项交互全过 | ✅ 浏览器 |
| 交互 | ARM、SEND、LONG、STOP、ERASE、模拟 LEARN、RESET、WEB/APP/UCONSOLE 切换 | ✅ 浏览器 |
| 网络 | 页面共 **5** 个同源请求;`/api/*` **0** 次;外部请求 **0** 次 | ✅ 浏览器 |
| 移动 390×844 | 默认 APP 视图;12/12;无横向溢出、无 console error | ✅ 浏览器 |
| `file://` | 直接打开 `site/index.html` 可用;postMessage 学习事件生效 | ✅ 浏览器 |
| reduced-motion | 页面/iframe 的扫描、频谱、雷达动效全部关闭 | ✅ 浏览器 |
| Web 资源 | 60,433 B → gzip **16,745 B**;官网 demo HTML 60,513 B + shim 12,986 B | ✅ 构建 |
| 固件 | image **1,069,052 B**,RAM 55,732 B,`factory=0x200000` | ✅ 构建 |
| Android | APK **1,280,617 B**,assets 与 `web/dist` 同哈希 | ✅ 构建 |
| 主机测试 | 协议 76 + 事件环 53 + OLED/UI 1,286 + TUI 12;`site_check` 通过 | ✅ 主机 |
| 上机 | 未重新烧录,设备浏览器/真实 LAN 未复核 | ⏳ 未验证 |
| Pages | 未重新部署;本地 `site_check`/浏览器通过 | ⏳ 未部署 |

## 2. 怎么做

- **不复制第二份 UI**:`tools/build_web.py` 把 `web/dist/index.html` 原样写成
  `site/assets/terminal.html`,只在应用脚本前插入一行
  `<script src="terminal-demo.js"></script>`;同时加 `noindex`。
- **本地传输**:`terminal-demo.js` 在真实应用启动前覆盖 `fetch` 与 `EventSource`:
  - `GET /api/hello|state|codes|net` → 本地状态机;
  - `POST /api/cmd` → 按既有事件格式回放 `tx/learn/log`(SEND 760ms 后 `ok=1 ms=852`);
  - SSE `hello/state/codes/event` → 同一个事件对象;
  - 父页只通过 `postMessage` 发 `learn`/`reset`,并接收 `ready/state/event` 更新事件流。
- **三视口**:WEB 1440×900、APP 390×844、UCONSOLE 1280×720;切换只改 iframe
  的宽高与 `scale`,页面不重载;手机默认 APP。
- **动效**:官网新增计数动画、demo 扫描/呼吸边框/加载条、CRT 扫描线、按钮高光、
  模块卡扫描、频谱条生长、feed 光标;Web 单页新增日志滑入、ARM 脉冲、TX 红脉冲、
  TX 频谱加速、状态点脉冲。全部 CSS,`prefers-reduced-motion` 关闭。
- **移除**:`site/assets/terminal-web/mobile/uconsole.webp` 与灯箱;官网终端区不再引用截图。
- **未改**:`web/app.js` 的传输/命令分支、`SEND/LONG/STOP` 时序、HTTP/SSE/BLE/元素 ID。
  Web 侧只改了 `web/app.css` 的动效层。

## 3. 复现

```bash
cd /home/myf/DSH/Dev/rf-lab
python3 tools/build_web.py            # 生成 dist / 固件 gzip / Android assets / site demo
node --check site/app.js
node --check site/assets/terminal-demo.js
python3 tools/site_check.py

python3 -m http.server 8088 -d site
# 浏览器打开 http://127.0.0.1:8088/#terminal
# 检查:点击 ARM → SEND;码库行尾 LONG/STOP/ERASE;「模拟遥控器」→ 码库 +1;
#       RESET → 回到 5 条;WEB/APP/UCONSOLE 切换;Network 面板无 /api 请求。
```

自动化验收使用 Chromium 159 + CDP:1440×1250 / 390×844 / 1440×900;
`Runtime.evaluate` 在 iframe 内点击,`Network.requestWillBeSent` 审计请求,
`Emulation.setEmulatedMedia` 检查 reduced-motion。

另用 `Input.dispatchMouseEvent` 在 **缩放后的 iframe** 上按真实坐标点击 ARM
(`scale≈0.51`):状态从 `DISARMED` 变为 `ARMED`,`body.armed=true`,
说明交互不是只靠脚本调用。

## 4. 截图

| 视口 | 文件 | 检查 |
|---|---|---|
| 1440×1250 | [`site-demo-2026-11-21-desktop.png`](site-demo-2026-11-21-desktop.png) | 终端区、6 条码、事件流、控制条;0 横向溢出 |
| 390×844 | [`site-demo-2026-11-21-mobile.png`](site-demo-2026-11-21-mobile.png) | 默认 APP;终端 iframe 304×658;0 横向溢出 |
| 1440×900 | [`site-demo-2026-11-21-standalone.png`](site-demo-2026-11-21-standalone.png) | 「全屏体验」独立页;底部 SIM 工具条 |

截图是**浏览器 mock**,不是设备截图;页面上的 `SIM` 标识保留。

## 5. 未验证 / 边界

1. **真机**:没有烧录;设备浏览器、OLED、BLE、真实 LAN 未复核。
2. **Pages**:`site/` 未重新部署;线上仍是旧版静态页,部署后需复核
   `/rf-lab/`、`/rf-lab/assets/terminal.html`、`/rf-lab/assets/terminal-demo.js` 均 200。
3. **跨浏览器**:只在 Chromium 159 验证;未测 Safari/Firefox 的 `iframe` + `postMessage`
   与 `clip-path` 动效。
4. **性能**:iframe 内跑完整单页 + Canvas;桌面 mock 无异常,低端手机未测帧率。
5. **可访问性**:iframe `title`、tab 键盘切换、`aria-live` 已设;读屏实际体验未测。

## 6. 取舍

- **Key Judgment**:用 **iframe + 传输 shim** 跑真实单页,而不是复制一份 demo UI 或继续放截图。
  截图会过期;复制 UI 会漂移;iframe 让官网展示的永远是 `web/` 的真实 DOM/CSS/JS。
- **不把 demo 逻辑放进 `web/app.js`**:shim 在 iframe 外挂,固件包不增加 demo 分支;
  `SEND/LONG/STOP` 的真实时序仍只存在于固件。
- **Avoided Misfix**:
  - ❌ 手写第二份终端 HTML/CSS:改一处忘一处,截图/演示再次漂移。
  - ❌ 在 `web/app.js` 加 `demo` 传输分支:固件体积与行为风险换一点演示便利。
  - ❌ 直接把 iframe 指向 `web/dist/index.html`:Pages 只发布 `site/`,线上 404;
    → 由 `build_web.py` 预生成到 `site/assets/`。
  - ❌ 把 SIM 演示写成“真机在线”:页面与文案都明确 `SIM/本地模拟/无网络`。


## 8. 部署补记(2026-11-21)

推送 `master` 后 GitHub Actions `pages.yml` 成功:

- commit `798a6ee`(ui: unify terminal with site and add live demo)
- run `44874345132` / job `136755615973`,结论 **success**
- 线上检查(2026-11-21):`/rf-lab/`、`/rf-lab/assets/terminal.html`、
  `/rf-lab/assets/terminal-demo.js`、`/rf-lab/style.css`、`/rf-lab/app.js` 均 **200**;
  首页包含 `assets/terminal.html`,demo 页包含 `terminal-demo.js`。
- 仍未做:线上浏览器交互回归、真机烧录、Safari/Firefox、低端手机帧率。
