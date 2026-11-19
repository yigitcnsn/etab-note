#!/usr/bin/env bash
# Drop an image file, or your current macOS clipboard (image or text), straight onto the E-Tab 5.
# Usage:
#   ./scripts/drop.sh                  # sends current macOS clipboard (image or text)
#   ./scripts/drop.sh path/to/pic.png  # sends an image file
set -euo pipefail

HOST="${ETAB_HOST:-etab.local}"
URL="http://${HOST}:8080/drop"

if [ $# -ge 1 ] && [ -f "$1" ]; then
    curl -fsS -X POST --data-binary "@$1" "$URL"
    exit 0
fi

TMP_IMG="$(mktemp /tmp/etab_clip.XXXXXX.png)"
trap 'rm -f "$TMP_IMG"' EXIT

# Try grabbing an image from macOS clipboard via osascript
if osascript -e "try
    set img to the clipboard as «class PNGf»
    set f to open for access POSIX file \"$TMP_IMG\" with write permission
    set eof f to 0
    write img to f
    close access f
    return \"ok\"
on error
    return \"no\"
end try" 2>/dev/null | grep -q "^ok$"; then
    curl -fsS -X POST -H "Content-Type: image/png" --data-binary "@$TMP_IMG" "$URL"
    exit 0
fi

# Fallback: send clipboard text
TEXT="$(pbpaste)"
if [ -n "$TEXT" ]; then
    printf "%s" "$TEXT" | curl -fsS -X POST -H "Content-Type: text/plain" --data-binary @- "$URL"
    exit 0
fi

echo "Clipboard is empty." >&2
exit 1
