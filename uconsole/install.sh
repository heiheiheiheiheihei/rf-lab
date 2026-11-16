#!/usr/bin/env bash
# 把 RF-Lab TUI 安装到当前用户(uConsole / 任意 Linux)。
# 不 sudo、不改系统文件、不自动编辑 labwc 配置。
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BIN="$HOME/.local/bin"
APP="$HOME/.local/share/applications"
SHARE="$HOME/.local/share/rflab"

mkdir -p "$BIN" "$APP" "$SHARE"
install -m 0644 "$ROOT/tools/rflab_tui.py" "$SHARE/rflab_tui.py"
install -m 0755 "$HERE/rflab-tui" "$BIN/rflab-tui"
install -m 0644 "$HERE/rflab.desktop" "$APP/rflab.desktop"

echo "installed:"
echo "  $BIN/rflab-tui"
echo "  $APP/rflab.desktop"
echo "  $SHARE/rflab_tui.py"
echo
echo "run:  rflab-tui --url http://192.168.4.1"
echo "      rflab-tui --port /dev/ttyACM0"
echo "keybind: 把 $HERE/labwc-keybind.xml 里的 <keybind> 段加入 ~/.config/labwc/rc.xml"
