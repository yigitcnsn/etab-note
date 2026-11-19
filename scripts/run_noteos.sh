#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
make push

echo "Pausing Android UI and launching E-Tab 5 Note OS v1..."
echo "Use NOTES -> EXIT inside the app to return to Android cleanly."
adb shell "su -c 'stop; sleep 1; /data/local/tmp/noteos; start'"
