#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# CLT SDK may be newer than the Xcode linker; prefer the Xcode SDK when present.
if [ -z "${SDKROOT:-}" ] && [ "$(uname)" = Darwin ]; then
  for s in /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk; do [ -d "$s" ] && export SDKROOT="$s"; done
fi
(cd ui && npm ci --no-audit --no-fund && npm run build)
make -C plugin -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
