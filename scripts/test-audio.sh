#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$project_root/build}"
audio_binary="${PULSEAUDIO_BINARY:-pulseaudio}"
runtime_dir="$(mktemp -d -t frameforge-audio.XXXXXXXX)"
audio_pid=""
cleanup() {
  if [[ -n "$audio_pid" ]]; then
    kill "$audio_pid" 2>/dev/null || true
    wait "$audio_pid" 2>/dev/null || true
  fi
  rm -rf -- "$runtime_dir"
}
trap cleanup EXIT
mkdir "$runtime_dir/state" "$runtime_dir/config"
extra=()
if [[ -n "${PULSEAUDIO_MODULE_PATH:-}" ]]; then
  extra+=("--dl-search-path=$PULSEAUDIO_MODULE_PATH")
fi
PULSE_RUNTIME_PATH="$runtime_dir" PULSE_STATE_PATH="$runtime_dir/state" XDG_CONFIG_HOME="$runtime_dir/config" \
  "$audio_binary" -n --daemonize=no --exit-idle-time=-1 --use-pid-file=no --disable-shm=yes \
  "${extra[@]}" \
  --load="module-native-protocol-unix socket=$runtime_dir/native auth-anonymous=1" \
  --load='module-null-sink sink_name=frameforge_test rate=48000 channels=2' > "$runtime_dir/server.log" 2>&1 &
audio_pid=$!
for attempt in {1..50}; do
  if [[ -S "$runtime_dir/native" ]]; then break; fi
  if ! kill -0 "$audio_pid" 2>/dev/null; then cat "$runtime_dir/server.log"; exit 1; fi
  sleep 0.1
done
PULSE_SERVER="unix:$runtime_dir/native" FRAMEFORGE_TEST_AUDIO=1 G_DEBUG=fatal-criticals \
  "$build_dir/playback_tests" audioDevicePlayback
