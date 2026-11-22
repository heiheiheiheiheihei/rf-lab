#!/usr/bin/env bash
# Host-side gate for the OPPO Watch 2 client. No watch required.
# Build + JVM protocol tests + APK metadata. Exits non-zero on any failure.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export JAVA_HOME="${JAVA_HOME:-$HOME/.local/opt/jdk-17}"
export ANDROID_HOME="${ANDROID_HOME:-$HOME/.local/opt/android-sdk}"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$PATH"

if [[ ! -x "$JAVA_HOME/bin/java" ]]; then
  echo "ERR: JAVA_HOME has no java: $JAVA_HOME" >&2
  exit 1
fi
if [[ ! -d "$ANDROID_HOME/platforms/android-35" ]]; then
  echo "ERR: Android platform 35 missing under $ANDROID_HOME" >&2
  exit 1
fi

printf 'sdk.dir=%s\n' "$ANDROID_HOME" > "$ROOT/watch/local.properties"

"$ROOT/android/gradlew" -p "$ROOT/watch" testDebugUnitTest lintDebug assembleDebug --console=plain

APK="$ROOT/watch/app/build/outputs/apk/debug/app-debug.apk"
BT="$ANDROID_HOME/build-tools/35.0.0"
[[ -x "$BT/aapt2" ]] || BT="$ANDROID_HOME/build-tools/$(ls "$ANDROID_HOME/build-tools" | sort -V | tail -1)"
[[ -x "$BT/aapt2" ]] || { echo "ERR: aapt2 not found" >&2; exit 1; }

echo
echo "BUILD OK"
ls -l "$APK"
sha256sum "$APK"
echo
"$BT/aapt2" dump badging "$APK" \
  | grep -E "^(package:|minSdkVersion|targetSdkVersion|application-label|launchable-activity)" || true

if [[ -f "$ROOT/watch/keystore.properties" ]]; then
  "$ROOT/android/gradlew" -p "$ROOT/watch" assembleRelease --console=plain
  RAPK="$ROOT/watch/app/build/outputs/apk/release/app-release.apk"
  "$BT/apksigner" verify --verbose --print-certs "$RAPK" | grep -q "Verifies"
  echo
  echo "RELEASE OK (independent watch key)"
  ls -l "$RAPK"
  sha256sum "$RAPK"
fi

echo
echo "HOST OK: protocol tests + APK build"
