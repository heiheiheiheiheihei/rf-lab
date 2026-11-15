#!/usr/bin/env bash
# 主机单测:只编 rf34dl.c(纯 C)+ 测试,不碰 ESP-IDF,秒级完成。
#   ./tools/host_test.sh          # 普通编译
#   SAN=1 ./tools/host_test.sh    # 加 ASan/UBSan
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)/rf34dl_test"

CC="${CC:-cc}"
CFLAGS=(-std=c11 -Wall -Wextra -Werror -O2 -I"$ROOT/firmware/src")
if [[ "${SAN:-0}" == "1" ]]; then
    CFLAGS+=(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
fi

"$CC" "${CFLAGS[@]}" \
    "$ROOT/firmware/src/rf34dl.c" \
    "$ROOT/firmware/test/host/test_rf34dl.c" \
    -o "$OUT"

"$OUT"
