# RF-Lab on uConsole

uConsole(ClockworkPi / Debian / labwc,1280×720)上的键盘入口。视觉与
[`uconsole-opt`](../../uconsole-opt) 同一套战术语言:墨底、琥珀、骨白、磷绿。

## 安装

```bash
./uconsole/install.sh
rflab-tui --url http://192.168.4.1     # 设备 AP;或设备 STA IP
rflab-tui --port /dev/ttyACM0          # USB 串口回退
```

安装脚本只写 `~/.local/bin`、`~/.local/share/applications`、`~/.local/share/rflab`,
不 sudo、不改系统文件。

## 键位

| 键 | 动作 |
|---|---|
| `↑/↓`、`j/k` | 选码 |
| `Enter` | SEND(需先 ARM) |
| `L` | LONG(需先 ARM) |
| `S` | STOP |
| `D` | ERASE(按两次确认) |
| `A` | ARM / DISARM |
| `T` | OLED TEST 30s |
| `R` | 刷新 |
| `?` | 帮助 |
| `Q` | 退出 |

## 键位绑定

把 [`labwc-keybind.xml`](labwc-keybind.xml) 里的 `<keybind>` 加入
`~/.config/labwc/rc.xml` 的 `<keyboard>` 节点,即可用 `Ctrl+Alt+R` 打开。
这与 uconsole-opt 的 `Ctrl+Alt` 命令层一致。

## 连接与排障

- 默认 HTTP:`http://192.168.4.1`(设备 AP `RF-Lab-XXXX` / `rflab1234`;仅在回退/配网时开)。
- 设备 STA 连上局域网后,用串口 `INFO` / OLED `LAN OK` 显示的 LAN IP;
  `rflab-tui --url http://<LAN IP>`。TUI 顶部 `LINK` 行会显示 `LAN` / `AP` / `AP FB`。
- 无 Wi-Fi 时用 `--port /dev/ttyACM0`;TUI 每 5s 发 `LIST`/`INFO` 刷新,串口事件
  仍由既有命令行输出。串口模式需要 `pyserial`(与 `tools/rf34dl.py` 相同):
  `python3 -m pip install --user pyserial` 或发行版包 `python3-serial`。
- HTTP 断线时 TUI 顶部 `LINK` 变红并继续重试;已显示的码库不会清空。

## 未验证

TUI 的解析/键位有主机单测(`tools/test_tui.py`),**uConsole 实机 1280×720 的
字体/中文宽度/`Ctrl+Alt+R` 绑定尚未在真机上验收**。首次上机请先 `--mock` 看布局,
再连设备;如果 `labwc` 的 `lxterminal` 不在 PATH,把 `labwc-keybind.xml` 里的命令
换成实际终端(如 `foot`/`xterm`)。
