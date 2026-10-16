#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
xvfb_binary="${XVFB_BINARY:-Xvfb}"
"$xvfb_binary" :98 -screen 0 1440x900x24 -nolisten tcp -ac > "$project_root/build/xvfb-test.log" 2>&1 &
xvfb_pid=$!
trap 'kill "$xvfb_pid" 2>/dev/null || true' EXIT
for attempt in {1..40}; do
  if [[ -S /tmp/.X11-unix/X98 ]]; then break; fi
  sleep 0.1
done
DISPLAY=:98 QT_QPA_PLATFORM=xcb QT_FFMPEG_DECODING_HW_DEVICE_TYPES=, PULSE_SERVER=unix:/tmp/frameforge-no-pulse XDG_CACHE_HOME="$project_root/build/cache" "$project_root/build/desktop_tests" -o "$project_root/build/x11-test.txt",txt
