#!/bin/sh
# Minimize a crashing input and save it as a regression fixture.
#
# Usage:
#   ./minimize.sh <target> <crash-file> [name-hint]
#
# The minimized input lands in test/fixtures/fuzz/<target>_<hash>.rar
# and an empty .note file is created for a short human description.

set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
FIXDIR="$REPO/test/fixtures/fuzz"

if [ $# -lt 2 ]; then
  echo "usage: $0 <target> <crash-file> [name-hint]" >&2
  exit 2
fi

target="$1"
crash="$2"
hint="${3:-}"

if [ ! -f "$HERE/$target" ]; then
  echo "$0: $target binary missing; run 'make $target' first" >&2
  exit 1
fi
if [ ! -f "$crash" ]; then
  echo "$0: crash file '$crash' not found" >&2
  exit 1
fi

mkdir -p "$FIXDIR"
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

cp "$crash" "$tmp"

echo "==> minimizing $crash via libFuzzer"
"$HERE/$target" -minimize_crash=1 -runs=50000 -exact_artifact_path="$tmp" "$tmp" \
  >/dev/null 2>&1 || true

# Short hash so different crashes with the same target don't collide.
hash="$(sha256sum "$tmp" | cut -c1-10)"
base="${target}_${hash}"
[ -n "$hint" ] && base="${target}_${hash}_${hint}"
out="$FIXDIR/${base}.rar"
note="$FIXDIR/${base}.note"

cp "$tmp" "$out"
: > "$note"

echo "==> saved $(ls -la "$out" | awk '{print $5}') bytes to $out"
echo "==> note file: $note  (add a one-line description)"
echo
echo "Next steps:"
echo "  1. Edit $note with a short description of what this fixture exposes."
echo "  2. Add { \"$target\", \"test/fixtures/fuzz/${base}.rar\" } to the"
echo "     fuzz_regressions[] table in test/runner.c."
echo "  3. Run: make -C test test-asan"
