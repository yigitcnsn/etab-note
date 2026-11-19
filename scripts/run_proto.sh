#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
make push

echo "Pausing Android UI and launching Bare Note OS prototype..."
echo "Press Vol+ on tablet to clear, Vol- or Power to exit and return to Android."
adb shell "su -c 'stop; sleep 1; /data/local/tmp/proto; start'"
