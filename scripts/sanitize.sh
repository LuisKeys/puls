#!/usr/bin/env bash
# Runs the headless tests under AddressSanitizer+UBSan and ThreadSanitizer (separate build dirs).
set -euo pipefail
cd "$(dirname "$0")/.."
if [ -z "${SDKROOT:-}" ] && [ "$(uname)" = Darwin ]; then
  s=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
  [ -d "$s" ] && export SDKROOT="$s"
fi
run() { # name flags
  echo "== $1"
  cmake -S tests -B "build/$1" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="$2 -g -fno-omit-frame-pointer" >/dev/null
  cmake --build "build/$1" >/dev/null
  ctest --test-dir "build/$1" --output-on-failure
}
run asan "-fsanitize=address,undefined -fno-sanitize-recover=undefined"
run tsan "-fsanitize=thread"
