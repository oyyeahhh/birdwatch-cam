#!/bin/bash
# push_web.sh - install/update the BirdWatch local UI on a camera's SD card
# over Wi-Fi, via the firmware's /api/upload endpoint. No card reader needed.
#
# Usage:  tools/push_web.sh <camera-ip>
# Example: tools/push_web.sh 192.168.1.182
set -euo pipefail

IP="${1:-}"
if [ -z "$IP" ]; then echo "usage: $0 <camera-ip>"; exit 1; fi

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

echo "Pushing web UI to camera at $IP ..."

up() {  # up <localfile> <sd-path>
  local code
  code=$(curl -s -m 60 -o /dev/null -w "%{http_code}" \
    -F "file=@$1" "http://$IP/api/upload?path=$2")
  if [ "$code" = "200" ]; then echo "  ok   $2"; else echo "  FAIL($code) $2"; return 1; fi
}

up web/local/index.html /web/index.html

ok=0; fail=0
for f in web/assets/illustrations/*.png; do
  if up "$f" "/web/assets/illustrations/$(basename "$f")"; then ok=$((ok+1)); else fail=$((fail+1)); fi
done

echo "Done: index.html + $ok illustrations uploaded, $fail failed."
echo "Open http://$IP/ to see the app served from the camera."
