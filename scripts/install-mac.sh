#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
dest="$HOME/Library/Audio/Plug-Ins/CLAP"
mkdir -p "$dest"
rm -rf "$dest/puls.clap"
cp -R bin/puls.clap "$dest/"
echo "installed to $dest/puls.clap"
