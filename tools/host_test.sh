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

echo "== oled:帧缓冲/字库/状态页/面板几何"
"$CC" "${CFLAGS[@]}" \
    "$ROOT/firmware/src/rf34dl.c" \
    "$ROOT/firmware/src/oled_gfx.c" \
    "$ROOT/firmware/src/ui.c" \
    "$ROOT/firmware/test/host/test_ui.c" \
    -o "$TMP/test_ui"
"$TMP/test_ui"
