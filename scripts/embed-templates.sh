#!/bin/sh
# Writes the built-in templates (templates/*.json) as C++ raw string literals, one per file, for state/Templates.cpp.
# Usage: embed-templates.sh <out.inc> <template.json>...
set -eu
out=$1; shift
tmp="$out.tmp"
mkdir -p "$(dirname "$out")"
: > "$tmp"
for f in "$@"; do
  if grep -q ')PULSTPL"' "$f"; then echo "$f: contains the raw string delimiter" >&2; rm -f "$tmp"; exit 1; fi
  { printf 'R"PULSTPL('; cat "$f"; printf ')PULSTPL",\n'; } >> "$tmp"
done
mv "$tmp" "$out"
