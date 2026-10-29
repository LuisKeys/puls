#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [ -z "${SDKROOT:-}" ] && [ "$(uname)" = Darwin ]; then
  s=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
  [ -d "$s" ] && export SDKROOT="$s"
fi
mkdir -p build
cmake -S tests -B build/tests -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
clang++ -std=c++20 -Iexternal/dpf/distrho/src tests/ClapLoadTest.cpp -o build/clap_load_test
./build/clap_load_test bin/puls.clap
if command -v clap-validator >/dev/null; then
  clap-validator validate bin/puls.clap
else
  echo "clap-validator not installed: validation PENDING"
fi
