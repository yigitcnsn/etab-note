#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
make build/noteos

adb push build/noteos /data/noteos/noteos.new
adb shell "mv /data/noteos/noteos.new /data/noteos/noteos && chmod +x /data/noteos/noteos && killall noteos"
echo "Deployed and restarted /data/noteos/noteos"
