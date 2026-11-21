#!/usr/bin/env bash
# 主机单测:只编纯 C 源码(rf34dl / link / ota_ticket / oled_gfx / ui),不碰 ESP-IDF,秒级完成。
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

echo "== ota_ticket:token 格式 / 过期 / 错误次数 / 一次性"
"$CC" "${CFLAGS[@]}" \
    "$ROOT/firmware/src/ota_ticket.c" \
    "$ROOT/firmware/test/host/test_ota_ticket.c" \
    -o "$TMP/test_ota_ticket"
"$TMP/test_ota_ticket"

echo "== wiring:JSON -> 代码头 / 表 / SVG 一致性"
python3 "$ROOT/tools/gen_wiring.py" --check

echo "== web:单页构建 / 内嵌 gzip / 无外部请求"
python3 "$ROOT/tools/build_web.py" --check
if command -v node >/dev/null 2>&1; then
    node --check "$ROOT/web/app.js"
fi
python3 - "$ROOT/firmware/partitions.csv" <<'PY'
import csv, sys
rows = [r for r in csv.reader(open(sys.argv[1])) if r and not r[0].startswith('#')]
parts = {r[0].strip(): (int(r[3].strip(), 0), int(r[4].strip(), 0))
         for r in rows if len(r) >= 5}
if parts.get('otadata') != (0x10000, 0x2000):
    raise SystemExit(f"otadata {parts.get('otadata')} != (0x10000, 0x2000)")
if 'factory' in parts:
    raise SystemExit('factory partition present; dual-OTA layout must not keep it')
apps = {n: v for n, v in parts.items() if n in ('ota_0', 'ota_1')}
if set(apps) != {'ota_0', 'ota_1'}:
    raise SystemExit(f'app slots {sorted(apps)} != ota_0/ota_1')
for name, (off, size) in apps.items():
    if size < 0x1E0000:
        raise SystemExit(f'{name} size 0x{size:x} < 0x1E0000')
print('partitions.csv: otadata + ota_0/ota_1 dual-OTA OK')
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
