# 现场证据

不是日志存档,是**结论的可复核依据**。换模块/换板子后如果结论变了,先检查这里的原始数据还成不成立。

| 文件 | 采集方式 | 支撑的结论 |
|---|---|---|
| [`2026-11-14-9600-capture.log`](2026-11-14-9600-capture.log) | 固件 0.3.0 的串口桥(ESP32-S3,UART1 9600,GPIO4 RX)在 180s 内把模块输出逐帧打印 | 模块主动输出 **8 字节 `FD … DF` 帧**;一次按键连发多帧(16 字节 = 两帧);码 `5555033C` / `5555034B`,频段 `F3`(315M) |
| [`2026-11-14-9600-baud.log`](2026-11-14-9600-baud.log) | 同一固件的 `PULSE` 命令用 GPIO 边沿中断测 RX 引脚脉宽 | 最短脉宽 **104µs** ⇒ **9600 8N1**(115200 读到的是 0x00 乱码) |
| [`2026-11-15-bringup.md`](2026-11-15-bringup.md) | 新硬件首跑(ESP32-C3 + 3V3 直连),逐条命令 + 观察 + 失败与误报记录 | 学码/存码/发射/改波特率可用;`F0`≈0.85s、`F1`≈0.1s;学习遥控器学到同码 |
| [`2026-11-15-oled.md`](2026-11-15-oled.md) | 0.5.0 上机:`DISP SHOW` 40 行 ASCII + 反解文本、`MON` 时间戳抓包、目视验收 | OLED 几何正确;学码/发射/长发/停止页随事件更新;**长发 `0xFE` ~25Hz / 停止 `0xFF`**,修正 0.4.0 的"无应答"结论;两条 433M 记录未解释 |
| [`2026-11-15-ui.md`](2026-11-15-ui.md) | 0.6.0 构建/主机/上机自检(18/18)、HTTP/SSE/POST、BLE 广播、APK/TUI、截图索引 | Web/BLE/Android/TUI 的交付边界与未验证项 |
| [`2026-11-16-ui-refresh.md`](2026-11-16-ui-refresh.md) | Web 资源 refresh;`build_web` + `pio run` + `assembleDebug` + mock API 布局 | 新皮肤构建事实;截图是 mock,新皮肤**未上机** |
| [`2026-11-21-pages.md`](2026-11-21-pages.md) | 仓库转 public + GitHub Actions Pages + URL/资源 200 | 官方入口切到 GitHub Pages;设备/固件无变更 |
| [`2026-11-21-ui-unify.md`](2026-11-21-ui-unify.md) | `build_web` + `pio run` + `assembleDebug` + mock API CDP 布局/交互 | `web/` 与官网同源 token/组件;gzip 16,418B;新皮肤**未上机** |
| [`2026-11-21-site-live-demo.md`](2026-11-21-site-live-demo.md) | 官网 iframe 真 UI + 本地传输 shim;CDP 交互/网络审计 | 终端区不再放截图;21/21 交互、0 `/api`、0 外部;gzip 16,745B;**未上机**;Pages 已部署 |
| [`2026-11-15-ui-serial.log`](2026-11-15-ui-serial.log) | `tools/device_selftest.py --tx` 串口全文 | 18 项上机检查的原始输出 |
| [`ui-desktop-1440.png`](ui-desktop-1440.png) 等 | Playwright + mock API | 0.6.0 旧皮肤桌面/uConsole/手机布局;不是设备截图 |
| [`ui-2026-11-16-desktop.png`](ui-2026-11-16-desktop.png) 等 | [`tools/ui_mock_server.py`](../tools/ui_mock_server.py) + Chromium | 2026-11-16 品牌/频谱带布局;不是设备截图;新皮肤未上机 |
| [`ui-2026-11-16b-desktop.png`](ui-2026-11-16b-desktop.png) 等 | 同上,字标改为英文连字符 `RF-LAB` | 历史展示用图;仍是 mock,新皮肤未上机 |
| [`ui-2026-11-21-desktop.png`](ui-2026-11-21-desktop.png) 等 | 同上 + CDP 固定视口 1440×900 / 1280×720 / 390×844 | 设备端 mock 对照图;官网 §03 已改用交互 demo;新皮肤未上机 |
| [`site-demo-2026-11-21-desktop.png`](site-demo-2026-11-21-desktop.png) 等 | 官网页面 + CDP:1440×1250 / 390×844 / 独立 demo 1440×900 | 官网终端区真实 UI + 本地模拟;不是设备截图 |
| [`2026-11-15-wifi.md`](2026-11-15-wifi.md) | `tools/wifi_selftest.py` 15/15 + 串口全文 | 有凭据 STA-only 启动(AP 关)→15s 超时 AP 回退;AP 内配网/`WIFI CLEAR` |
| [`2026-11-15-wifi.log`](2026-11-15-wifi.log) | 串口全文 | STA 失败、AP 回退、回退后 Web 200 的原始输出 |
| [`2026-11-15-wifi-lan.md`](2026-11-15-wifi-lan.md) | 主机 LAN ping/curl/SSE/POST + 真实 Chromium | 设备接入 `SSID-A`,DHCP `192.168.5.117`;重启后 `mode=sta ap_on=0`(AP 关) |
| [`2026-11-15-wifi-lan.log`](2026-11-15-wifi-lan.log) | 串口 + 主机命令原始输出 | 真实 LAN 200/202 与浏览器 200 |
| [`ui-lan-live.png`](ui-lan-live.png) | 真实设备 Web 页面(Chromium,1280×720) | 顶栏 `NET LAN 192.168.5.117`,设置页 `LAN MODE`;非 mock |
| [`2026-11-15-sta-base.log`](2026-11-15-sta-base.log) | `device_selftest.py --tx`(STA 模式) | 18/18;不碰已保存的 STA 凭据 |

前两条来自**旧硬件方案**(ESP32-S3 + 同一块模块)。硬件换了,但模块与抓包内容不变,所以证据继续有效。

> **修正(2026-11-15,0.5.0)**:`2026-11-15-bringup.md` §6 的"长发/停止之后模块没有回任何字节"
> 已被推翻。实际是 `0xFE` 心跳(长发,~25Hz)与 `0xFF`(停止),见 `2026-11-15-oled.md` §4。

## 复现条件

- 抓包时的模块 VCC 接法**没有记录**(旧文档只写了「先按 3.3V 接」);现行方案按 3.3V,但证据本身不能证明 3.3V 已可用。
- 遥控器按键时模块 LED 短暂熄灭,串口随即输出帧。
- 日志里 `[ 27556]` 是固件启动后的毫秒时间戳,不是绝对时间。

## 失效条件(出现任一条就要重新抓)

1. 换成不同型号/批次的编解码模块。
2. 模块波特率被 `BAUD` 命令改过且没恢复 9600。
3. 帧头/帧尾或第三字节含义与规格 §5 不符。
