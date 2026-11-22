# site/ — RF-LAB 官网 / 配装台

**线上:** <https://heiheiheiheiheihei.github.io/rf-lab/> · **下载:** [Android APK 0.7.1](https://github.com/heiheiheiheiheihei/rf-lab/releases/latest/download/rf-lab-android.apk) · **手表:** [OPPO Watch 2 客户端源码](https://github.com/heiheiheiheiheihei/rf-lab/tree/master/watch) · **部署:** GitHub Pages / Actions(见文末)
**2026-11-21 已上线交互 demo:** `/assets/terminal.html` 与 `/assets/terminal-demo.js` 均 200;真机 BLE 连接/码表已验(2026-11-21);线上交互回归仍未验证。
**2026-11-22 新增手表区:** 46mm OW20W1 真机截图、独立 release key / 0.1.0;
已由 Pages 发布(commit `e359992` / [run 45001726431](https://github.com/heiheiheiheiheihei/rf-lab/actions/runs/45001726431))。

静态单页,把项目从“315/433 固定码工具”讲成“¥100–999 的模块化射频实验室”。
无站点构建、无外部字体、无 CDN、无外部/API 请求;直接打开 `site/index.html` 就能看。
终端区是预生成的交互 demo(`assets/terminal.html`),由 `tools/build_web.py` 产出并提交。

![官网首屏](assets/preview.webp)

## 本地查看

```bash
# 方式 1:直接打开
xdg-open site/index.html

# 方式 2:起一个本地静态服务(推荐,相对链接行为与部署一致)
python3 -m http.server 8088 -d site
# http://127.0.0.1:8088/
```

## 页面结构

| 区块 | 内容 | 交互 |
|---|---|---|
| Hero | RF-LAB / 百元射频实验室、¥100–999 预算包线、5 个舱位示意 | Canvas 频谱、雷达扫描、滚动揭示 |
| 01 配装台 | **硬件链路图**(CORE-C3 + YS-RF34DL,四线)、4 档预设、9 个可选模块、实时物料小计/余量/能力矩阵 | 勾选、预算滑杆、随机配装、削到预算内、复制配装单;点图打开 SVG 原图 |
| 02 模块阵列 | 对数频段覆盖图、模块规格表、**主控/模块两张硬件细节卡**、三条设计判断 | 表格/频段条;点图打开 SVG 原图 |
| 03 战术终端 | **内嵌真实单页 UI**(iframe,`assets/terminal.html`);WEB/APP/UCONSOLE 三视口 | 直接 ARM/SEND/LONG/STOP/ERASE;模拟遥控器/重置;事件流(标 SIM) |
| 04 手表控制 | OPPO Watch 2 实机截图(402×476)、LINK→PIN→CONTROL、HOLD/LONG/STOP | 静态截图 + 源码/规格链接;不提供未发布 APK |
| 05 无线上行 | 本地 MQTT + Home Assistant Discovery 拓扑与 topic 草案 | 强制 `ROADMAP / 未实现` 标识 |
| 06 作战状态 | 构建 / 主机 / 上机分开列;版本线 | 无 |
| Footer | 安全边界、文档入口、来源(含 APK / Releases) | 键盘 `B/T/W/U/R/?` |

## APK 下载入口

| 位置 | 链接 |
|---|---|
| 导航栏 CTA(吸顶) | `releases/latest/download/rf-lab-android.apk` |
| 首屏 CTA 第三颗按钮 | 同上,标 `0.7.1 · ↓` |
| 状态板 Android APK 行 | 文字标 `0.7.1 · release 签名 · 1.05 MB`;下载走导航/页脚 |
| 页脚 SOURCE | 同上 + [Releases](https://github.com/heiheiheiheiheihei/rf-lab/releases/latest) |
| 手表 | [`releases/download/watch-v0.1.0/rf-lab-watch.apk`](https://github.com/heiheiheiheiheihei/rf-lab/releases/download/watch-v0.1.0/rf-lab-watch.apk) · 源码/规格 `watch/README.md` |

用 `releases/latest/download/<固定文件名>` 而不是写死 tag:每次发版覆盖同名资产,官网按钮
无需改动。APK 由 `.github/workflows/android-release.yml` 在 `v*` 标签上构建、验签后上传;
**OPPO Watch 2 是独立 release key / 0.1.0 公开包**,Release 由
`.github/workflows/watch-release.yml` 在 `watch-v*` 标签构建,标为非 latest
(不影响手机 APK 的 `releases/latest`);
**真机安装/自更新/BLE 已验(2026-11-22)**;页面状态板对应行已改为 `●`;uConsole 仍保持 `◐`。
发射行在 2026-11-21 后改为 `◐ 待复测`(UART 命令方向已恢复,`SEND` 未复测);
完整测试登记与 0.7.2 退出条件见 [`../docs/02-plan.md`](../docs/02-plan.md)。
`0.7.3` 的 UI 一键固件更新仍是规划,状态板标 `○ 规划`;已定 **BOOT 确认 + GitHub Pages
固件源 + 独立 0.7.3 发版**,契约见 [`../docs/01-ui.md`](../docs/01-ui.md) §3.4.1。

## 预算与配装

配装器里的价格是**参考物料价**,不含运费与工具;它不是报价单。

| 预设 | 预算上限 | 物料小计 | 组成 |
|---|---:|---:|---|
| 侦察兵 SCOUT | ¥100 | ¥100 | CORE-C3 + YS-RF34DL |
| 突击手 ASSAULT | ¥299 | ¥272 | + CC1101 + NRF24L01 + 背板 + 天线包 |
| 战术组 TACTICAL | ¥599 | ¥589 | + SI4463 + LR1121 + 收纳盒 |
| 全频段 ALL-IN | ¥999 | ¥988 | 全模块 + FIELD DOCK |

改价格只改 `index.html` 里每个 `.mod-card` 的 `data-price`;总价/余量/预设由 `app.js` 计算。
`tools/site_check.py` 会校验 9 个模块、基础价 ¥79、4 档预算不超支、全频段落在 ¥999 内。

## 终端口径 / 截图

终端区**不再放截图**:`site/assets/terminal.html` 是由 `tools/build_web.py` 从同一份
`web/dist/index.html` 生成的交互 demo,`assets/terminal-demo.js` 在应用脚本前接管
`fetch`/`EventSource`,本地回放 `/api/*` 与 SSE,不发网络请求。页面标 `SIM // 本地`,
不是真机在线。

`../docs/evidence/ui-2026-11-21-*.png` 仍是 **mock API 布局验收图**,不是新皮肤上机截图;
真实 LAN 图 `../docs/evidence/ui-lan-live.png` 仍是 0.6.0 旧皮肤。2026-11-21 视觉尚未重新烧录验证。

## 硬件效果图 / 接线图

`hardware/wiring.json` 是唯一源;`tools/gen_wiring.py` 生成代码头、两张文档表与 4 张
抽象 SVG,`tools/build_web.py` 再把 SVG 复制到 `site/assets/` 并内联进设备页:

| 文件 | 用途 | 尺寸 |
|---|---|---|
| `web/art/hw-assembly.svg` | 官网 §01 宽图;设备 UI `HW` 宽屏 | 1200×760 |
| `web/art/hw-assembly-m.svg` | 官网/UI 窄屏竖图 | 720×1200 |
| `web/art/hw-core.svg` | 官网 §02 CORE-C3 引脚图 | 720×760 |
| `web/art/hw-rf.svg` | 官网 §02 YS-RF34DL 引脚图 | 720×760 |

- 图是**抽象功能块**,不是 3D/PCB 写实图;只保留核心元件与 4 根线
  (3V3→VCC、GND→GND、GPIO3→RXD、GPIO4←TXD),以及必须保留的 OLED/USB/strapping 说明。
- 官网 §01 用 `<picture>` 在窄屏切竖图;§02 两张引脚图;点图打开 SVG 原图。
- 设备 UI 顶栏 `HW` 芯片 / `H` 键打开模态;宽屏内联宽图,≤760px 内联竖图;
  `ZOOM` 放大、`FIT` 适应,`ESC` 关闭。只读静态 SVG,不发网络请求、不新增命令。
- 改接线:只改 `hardware/wiring.json` → `python3 tools/gen_wiring.py` → 构建;
  代码头、文档表、SVG 必须一起变。`gen_wiring.py --check` 在主机测试与固件构建前执行。
- 现役只画 CORE-C3 + YS-RF34DL;CC1101 / NRF24L01 / SI4463 / LR1121 不进入这张
  “现役链路”图,避免把路线图写成已接入。

## 现役 / 路线图边界(硬规则)

- **现役**:YS-RF34DL 315/433 固定码学习/发射;Web HTTP+SSE;BLE 广播/真机码表;Android APK 0.7.1;
  OPPO Watch 2 0.1.0(46mm 真机);uConsole TUI 主机测试。上机证据在 `../docs/evidence/`。
- **路线图**:CC1101 / NRF24L01 / SI4463 / LR1121、SPI 背板、MQTT bridge、
  Home Assistant Discovery。页面里必须保持“路线图 / 未实现 / 概念”标识。
- 不要把“配装器可以选”写成“固件已支持”;不要把 MQTT/HA 的规划写成现有能力。
- 只有 YS-RF34DL 链路完成上机验证,其余射频模块连驱动都没有。

这条规则与仓库 `AGENTS.md` 的“不改证据 / 射频与 UI 分离 / 不把构建写成上机”一致。

## 设计取舍

**Key Judgment**:官网与设备端 UI 共用**视觉基线**(`style.css :root` 的 token + 组件语法),
但**代码分离**:设备端 UI 是 16KB 级 gzip 的固件资源,受 2MB 分区与 SSE/内存约束;
官网是宣传页,可以更大、更动效化,但不能反过来把 Three.js/GSAP/CDN 塞进固件,
也不能把官网的滚动/Canvas 逻辑当成终端的一部分。

官网终端区用 **iframe + 本地传输 shim** 直接跑真实单页:既不是截图,也不复制第二份 UI;
shim 只替换 `fetch`/`EventSource`,命令与事件格式仍由 `web/` 决定。

**Avoided Misfix**:
- ❌ 用 CDN 字体/图表库:离线不可用,且与“本地优先”叙事冲突。→ 全部系统字体 + 原生 Canvas。
- ❌ 把 5 个模块都画成已支持:会误导采购。→ 每张卡带“现役/路线图”状态,顶部有采购边界。
- ❌ 官网直接改 `web/index.html`:会破坏 `tools/build_web.py`、固件 gzip、Android assets 的单一来源。
- ❌ 首屏堆满自动播放视频:移动端耗电、不可控。→ 一个 Canvas 频谱 + CSS 雷达,尊重 `prefers-reduced-motion`。
- ❌ 把“可接入 Home Assistant”写成现在时。→ 整段套 `ROADMAP / 0.9.0` 边框与“未实现”文案。
- ❌ 用产品照片或外链图撑首屏:离线会裂图、风格不统一,还可能把路线图模块拍成现役。
  → `web/art/` 矢量重绘,只用项目 token;照片不进仓库。

## 验证

```bash
node --check site/app.js
node --check site/assets/terminal-demo.js
python3 tools/build_web.py            # 同时生成 terminal.html 与 site/assets/hw-*.svg
python3 tools/build_web.py --check    # 校验生成物(含内联 SVG)
python3 tools/site_check.py
```

浏览器自检(Playwright 的 Chromium 即可):

```bash
CHROME="$HOME/.cache/ms-playwright/chromium-1255/chrome-linux64/chrome"
"$CHROME" --headless=new --no-sandbox --disable-gpu --window-size=1440,900 \
  --screenshot=/tmp/rflab-site.png "file://$PWD/site/index.html?static=1"
```

`?static=1` 会跳过 reveal/扫描动效,方便 CLI 截图复现;正常访问不带参数。
人工检查项:1440×900 首屏、390×844 单列、配装器 4 档/超预算/削减、终端三视口、
在终端内 ARM/SEND/LONG/STOP/ERASE、模拟遥控器/重置、`prefers-reduced-motion`、
键盘 `B/T/U/R/?`、无 console error、无外部请求(iframe 的 `/api/*` 只走本地 shim)。
硬件图另查:§01/§02 的 SVG 加载、点图开原图;设备 UI `HW` 打开、`ZOOM/FIT`、`H`/`ESC`。
手表区查:桌面/390px 截图不溢出,两张 `assets/watch-*.webp` 有 alt;`W` 键跳到 `#watch`;
页面与 iframe 都不发起外部网络请求。

## 发布说明

- 官网页脚的文档位已改为页内锚点(`#modules` / `#terminal` / `#status` / `#uplink`),
  避免部署到 Pages 根目录后 `../README.md` 404;完整规格仍在仓库里。
- `og:image` / `canonical` / `sitemap` 已固定为 GitHub Pages 绝对 URL;换域名或换仓库名时同步改 `site/index.html`、`robots.txt`、`sitemap.xml`。
- 页面无 Cookie、无追踪、无分析脚本。
- 改了设备端 UI 后,先跑 `python3 tools/build_web.py`:它会重写 `assets/terminal.html`
  (官网交互 demo);`--check` 会校验生成物。终端区不再有 `terminal-*.webp` 截图。
- 部署前同样跑 `node --check site/app.js`、`node --check site/assets/terminal-demo.js`、
  `python3 tools/site_check.py`。

## GitHub Pages 部署

当前线上地址:<https://heiheiheiheiheihei.github.io/rf-lab/>。仓库已公开,
Pages 使用 **GitHub Actions** 发布,工作流:[`../.github/workflows/pages.yml`](../.github/workflows/pages.yml)。
当前 action:checkout v7 / configure-pages v6 / upload-pages-artifact v5 / deploy-pages v5。

- 触发:`push master` 或 `gh workflow run pages.yml`。
- 产物:工作流把 `site/` 作为 Pages artifact 上传;不需要构建,不需要 token。
- 文档位已改为页内锚点,不依赖 `../README.md`;完整规格在公开仓库里。
- 部署后检查 `/rf-lab/`、`/rf-lab/style.css`、`/rf-lab/app.js`、
  `/rf-lab/assets/terminal.html`、`/rf-lab/assets/terminal-demo.js`、
  `/rf-lab/robots.txt`、`/rf-lab/sitemap.xml` 均 200。
- 重新部署前先跑 `python3 tools/build_web.py` + `node --check site/app.js` +
  `node --check site/assets/terminal-demo.js` + `python3 tools/site_check.py`。
- 旧 Cloudflare Pages 项目 `rf-lab` 保留为历史镜像,不再作为官方入口。
