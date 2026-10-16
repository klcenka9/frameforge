#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -d "$project_root/.sdk/root/usr/lib/x86_64-linux-gnu" ]]; then
  export LD_LIBRARY_PATH="$project_root/.sdk/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
exec "$project_root/build/frameforge" "$@"
