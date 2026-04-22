#!/bin/sh
# Differential extraction test: for each well-formed corpus archive,
# extract via our library and via system `unrar`, then byte-compare
# every file produced.

set -e

REPO="$(cd "$(dirname "$0")/.." && pwd)"
CORPUS="$REPO/test/corpus"

if ! command -v unrar >/dev/null 2>&1; then
  echo "diff.sh: 'unrar' not in PATH; skipping differential test" >&2
  exit 0
fi

# We need a tiny extractor binary. Rather than building another C file,
# reuse example.c which ships with the library and already supports 'e'.
EX="$REPO/test/example_unrar"
cc -std=gnu89 -Wall -Wextra -Wno-long-long -I"$REPO" \
   "$REPO/example.c" -o "$EX"

# Archives we can meaningfully diff.
# Skipped on purpose:
#   - encrypted-*.rar / links.rar: the library correctly refuses these.
ARCHIVES="simple.rar solid.rar solid-long.rar solid-mixed.rar x86filter.rar solid-filter.rar"

fail=0

for arc in $ARCHIVES; do
  src="$CORPUS/$arc"
  if [ ! -f "$src" ]; then
    echo "diff.sh: missing $src (run make corpus)" >&2
    continue
  fi
  ours="$(mktemp -d)"
  theirs="$(mktemp -d)"
  trap 'rm -rf "$ours" "$theirs"' INT TERM

  echo "== $arc =="
  # example.c strips directory prefixes via get_filename_no_directory(),
  # so match that shape by extracting without paths on the reference side.
  (cd "$ours"   && "$EX" e "$src" >/dev/null)
  (cd "$theirs" && unrar e -inul -o+ "$src" >/dev/null)

  if diff -r "$ours" "$theirs" >/dev/null 2>&1; then
    echo "   identical"
  else
    echo "   MISMATCH"
    diff -r "$ours" "$theirs" || true
    fail=1
  fi
  rm -rf "$ours" "$theirs"
done

if [ "$fail" -ne 0 ]; then
  exit 1
fi
echo "diff: all archives identical"
