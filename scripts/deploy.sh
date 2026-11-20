#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
make build/noteos

ADB_ARGS=()
if [ -n "${ANDROID_SERIAL:-}" ]; then
    ADB_ARGS=(-s "$ANDROID_SERIAL")
else
    DEV=$(adb devices | awk 'NR>1 && $2=="device" {print $1; exit}')
    if [ -n "$DEV" ]; then
        ADB_ARGS=(-s "$DEV")
    fi
fi

adb ${ADB_ARGS[@]+"${ADB_ARGS[@]}"} push build/noteos /data/noteos/noteos.new
adb ${ADB_ARGS[@]+"${ADB_ARGS[@]}"} shell "mv /data/noteos/noteos.new /data/noteos/noteos && chmod +x /data/noteos/noteos && killall noteos"
echo "Deployed and restarted /data/noteos/noteos"
