#!/usr/bin/env bash
# Builds, ad-hoc codesigns and zips bin/puls.clap into dist/. Ad-hoc signing is NOT notarization: a copy that
# was downloaded (quarantined) still needs `xattr -dr com.apple.quarantine puls.clap` or an explicit allow.
set -euo pipefail
cd "$(dirname "$0")/.."
scripts/build.sh
bundle=bin/puls.clap
arch=$(lipo -archs "$bundle/Contents/MacOS/puls" | tr ' ' '-')
codesign --force --deep --sign - --timestamp=none "$bundle"
codesign --verify --deep --strict --verbose=2 "$bundle"
mkdir -p dist
zip="dist/puls-macos-$arch.zip"
rm -f "$zip"
ditto -c -k --keepParent "$bundle" "$zip"
(cd dist && shasum -a 256 "$(basename "$zip")" | tee "$(basename "$zip").sha256")
echo "packaged $zip (arch: $arch)"
