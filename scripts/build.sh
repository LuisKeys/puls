#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# CLT SDK may be newer than the Xcode linker; prefer the Xcode SDK when present.
if [ -z "${SDKROOT:-}" ] && [ "$(uname)" = Darwin ]; then
  for s in /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk; do [ -d "$s" ] && export SDKROOT="$s"; done
fi
# The native (NanoVG) UI has no resources: on Linux the CLAP is a single file, not the old WebView bundle directory.
if [ "$(uname)" = Linux ] && [ -d bin/puls.clap ]; then
  echo "bin/puls.clap is a directory left by the WebView build; remove it first" >&2
  exit 1
fi
make -C plugin -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
