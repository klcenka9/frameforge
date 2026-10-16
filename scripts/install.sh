#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
repo="klcenka9/frameforge"
version="0.1.0"
prefix="${HOME}/.local"
source_dir=""
install_dependencies=true
while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix) [[ $# -ge 2 ]] || { echo 'Missing --prefix value' >&2; exit 2; }; prefix="$2"; shift 2 ;;
    --source) [[ $# -ge 2 ]] || { echo 'Missing --source value' >&2; exit 2; }; source_dir="$2"; shift 2 ;;
    --no-deps) install_dependencies=false; shift ;;
    --help|-h) cat <<'HELP'
Frameforge source installer (Debian / Ubuntu / Linux Mint)
Usage: bash install.sh [--prefix /absolute/path] [--no-deps] [--source /local/checkout]
Default: install distribution build dependencies through sudo apt-get, download
v0.1.0, build as your current user and install under $HOME/.local.
Requires Qt >= 6.4 (target: Mint 22+, Ubuntu 24.04+, Debian 12+).
--no-deps skips apt when dependencies are already installed.
No proprietary drivers or bundled multimedia codecs are installed.
HELP
      exit 0 ;;
    *) echo "Unknown option: $1 (use --help)" >&2; exit 2 ;;
  esac
done
[[ "$prefix" == /* ]] || { echo '--prefix must be an absolute path' >&2; exit 2; }
if [[ $EUID -eq 0 ]]; then
  echo 'Run as your normal desktop user, without sudo. Only apt needs sudo.' >&2
  exit 1
fi
if $install_dependencies; then
  command -v apt-get >/dev/null || { echo 'Install dependencies from README, then use --no-deps.' >&2; exit 1; }
  command -v sudo >/dev/null || { echo 'sudo is required to install build dependencies; otherwise use --no-deps.' >&2; exit 1; }
  echo 'Installing distribution build dependencies (sudo apt-get).'
  sudo apt-get update
  qt_candidate="$(apt-cache policy qt6-base-dev | awk '/Candidate:/ {print $2; exit}')"
  if [[ -z "$qt_candidate" || "$qt_candidate" == '(none)' ]] || ! dpkg --compare-versions "$qt_candidate" ge 6.4; then
    echo 'Qt 6.4+ is required. Use Mint 22+, Ubuntu 24.04+ or Debian 12+, or provide a compatible Qt SDK with --no-deps.' >&2
    exit 1
  fi
  sudo apt-get install -y build-essential cmake qt6-base-dev qt6-multimedia-dev libgl-dev ffmpeg curl ca-certificates
fi
for tool in cmake c++ ffmpeg ffprobe; do
  command -v "$tool" >/dev/null || { echo "Missing dependency: $tool" >&2; exit 1; }
done
work_dir="$(mktemp -d -t frameforge-install.XXXXXXXX)"
trap 'rm -rf -- "$work_dir"' EXIT
if [[ -z "$source_dir" ]]; then
  command -v curl >/dev/null || { echo 'curl is required' >&2; exit 1; }
  echo "Downloading Frameforge v$version source from $repo."
  curl --fail --show-error --location --retry 3 \
    "https://github.com/$repo/archive/refs/tags/v$version.tar.gz" -o "$work_dir/source.tar.gz"
  mkdir "$work_dir/source"
  tar -xzf "$work_dir/source.tar.gz" --strip-components=1 -C "$work_dir/source"
  source_dir="$work_dir/source"
fi
[[ -f "$source_dir/CMakeLists.txt" ]] || { echo 'Source directory has no CMakeLists.txt' >&2; exit 1; }
jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
(( jobs > 4 )) && jobs=4
printf 'Building Frameforge; install prefix: %s\n' "$prefix"
cmake -S "$source_dir" -B "$work_dir/build" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "$work_dir/build" --parallel "$jobs"
cmake --install "$work_dir/build"
printf '\nInstalled. Launch in a graphical session:\n  "%s/bin/frameforge"\n' "$prefix"
echo 'This is an experimental editor. Timeline previews require a background render.'
