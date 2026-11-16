#!/usr/bin/env bash
# 主机单测:只编纯 C 源码(rf34dl / oled_gfx / ui),不碰 ESP-IDF,秒级完成。
#   ./tools/host_test.sh          # 普通编译
#   SAN=1 ./tools/host_test.sh    # 加 ASan/UBSan
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"

CC="${CC:-cc}"
CFLAGS=(-std=c11 -Wall -Wextra -Werror -O2 -I"$ROOT/firmware/src")
if [[ "${SAN:-0}" == "1" ]]; then
    CFLAGS+=(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
fi

echo "== rf34dl:协议层(真模块抓包回归、重同步、ACK、fuzz)"
"$CC" "${CFLAGS[@]}" \
    "$ROOT/firmware/src/rf34dl.c" \
    "$ROOT/firmware/test/host/test_rf34dl.c" \
    -o "$TMP/test_rf34dl"
"$TMP/test_rf34dl"

echo "== link:事件环回绕 / 多消费者 / JSON 转义"
"$CC" "${CFLAGS[@]}" \
    "$ROOT/firmware/src/link.c" \
    "$ROOT/firmware/test/host/test_link.c" \
    -o "$TMP/test_link"
"$TMP/test_link"

echo "== web:单页构建 / 内嵌 gzip / 无外部请求"
python3 "$ROOT/tools/build_web.py" --check
if command -v node >/dev/null 2>&1; then
    node --check "$ROOT/web/app.js"
fi
python3 - "$ROOT/firmware/partitions.csv" <<'PY'
import csv, sys
rows = [r for r in csv.reader(open(sys.argv[1])) if r and not r[0].startswith('#')]
app = [r for r in rows if len(r) > 5 and r[0].strip() == 'factory']
size = int(app[0][4].strip(), 0) if app else 0
if size != 0x200000:
    raise SystemExit(f'factory app slot 0x{size:x} != 0x200000')
print(f'partitions.csv: factory 0x{size:x} OK')
PY

echo "== tui:解析 + 键位分发(不启动 curses)"
python3 -m unittest discover -s "$ROOT/tools" -p 'test_tui.py' -v

echo "== oled:帧缓冲/字库/状态页/面板几何"
"$CC" "${CFLAGS[@]}" \
    "$ROOT/firmware/src/rf34dl.c" \
    "$ROOT/firmware/src/oled_gfx.c" \
    "$ROOT/firmware/src/ui.c" \
    "$ROOT/firmware/test/host/test_ui.c" \
    -o "$TMP/test_ui"
"$TMP/test_ui"
