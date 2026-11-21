#!/usr/bin/env bash
# 主机测试:APP 自更新的纯逻辑(版本比较 + SHA256SUMS 查表)。
#
# 只编译两个纯 Java 文件,不需要 Android SDK / Gradle / 模拟器:
#   android/app/src/main/java/dev/rflab/terminal/AppVersion.java
#   android/app/src/main/java/dev/rflab/terminal/Sums.java
#   android/app/src/test/java/dev/rflab/terminal/UpdateLogicTest.java
#
# 弹不弹「发现新版本」只看这里的比较结果,所以它进主机测试,而不是等真机。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAIN="$ROOT/android/app/src/main/java/dev/rflab/terminal"
TEST="$ROOT/android/app/src/test/java/dev/rflab/terminal/UpdateLogicTest.java"

BIN=""
for candidate in "${JAVA_HOME:+$JAVA_HOME/bin}" "$HOME/.local/opt/jdk-17/bin"; do
    if [ -n "$candidate" ] && [ -x "$candidate/javac" ] && [ -x "$candidate/java" ]; then
        BIN="$candidate"
        break
    fi
done
if [ -z "$BIN" ] && command -v javac >/dev/null 2>&1 && command -v java >/dev/null 2>&1; then
    BIN="$(dirname "$(command -v javac)")"
fi
if [ -z "$BIN" ]; then
    echo "app_update_test: 找不到 javac(设置 JAVA_HOME 或装 JDK 17)" >&2
    exit 1
fi

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

"$BIN/javac" -encoding UTF-8 -d "$OUT" \
    "$MAIN/AppVersion.java" "$MAIN/Sums.java" "$TEST"
"$BIN/java" -cp "$OUT" dev.rflab.terminal.UpdateLogicTest
