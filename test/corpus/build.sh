#!/bin/sh
# Generates well-formed archives in test/corpus/. Requires the `rar` tool.
# Malformed / hand-crafted archives live in test/fixtures/ and are checked in.

set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

if ! command -v rar >/dev/null 2>&1; then
  echo "corpus/build.sh: 'rar' not found in PATH; skipping corpus regeneration" >&2
  exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# --- simple.rar: three plain files ------------------------------------------
(
  cd "$WORK"
  mkdir simple
  printf 'hello world\n' > simple/hello.txt
  printf 'abcdefghij' > simple/ten.bin
  mkdir simple/sub
  printf 'nested contents\n' > simple/sub/nested.txt
  rm -f "$HERE/simple.rar"
  (cd simple && rar a -r -ma5 -idq "$HERE/simple.rar" . >/dev/null)
)

# NOTE: We don't generate a RAR4 (-ma4) archive here. rar 7.x dropped
# RAR4 writing support, so a legacy RAR4 corpus entry would require
# either an older `rar` binary or a checked-in fixture. The RAR4 header
# CRC code path is therefore only exercised against user-supplied
# archives; our regression tests cover RAR5 only.

# --- solid.rar: a solid RAR5 archive ----------------------------------------
(
  cd "$WORK"
  mkdir solid
  # a few small files that should compress well when solid
  for i in 1 2 3 4 5; do
    printf 'the quick brown fox jumps over the lazy dog %d\n' "$i" > "solid/file${i}.txt"
  done
  rm -f "$HERE/solid.rar"
  (cd solid && rar a -s -ma5 -idq "$HERE/solid.rar" . >/dev/null)
)

# --- solid-long.rar: longer solid chain (8 files) ---------------------------
# Exercises deeper predecessor traversal than solid.rar (5 files). Picks up
# bugs that only manifest after N parts of state reuse.
(
  cd "$WORK"
  mkdir solid_long
  for i in 1 2 3 4 5 6 7 8; do
    printf 'the quick brown fox jumps over the lazy dog %d\n' "$i" > "solid_long/file${i}.txt"
  done
  rm -f "$HERE/solid-long.rar"
  (cd solid_long && rar a -s -ma5 -idq "$HERE/solid-long.rar" . >/dev/null)
)

# --- solid-mixed.rar: solid archive mixing stored and compressed entries ----
# `-msbin` forces files with .bin extension to be stored; the rest are
# compressed. Crosses both code paths in one chain so a decoder state-leak
# between stored and compressed entries would corrupt output.
(
  cd "$WORK"
  mkdir solid_mixed
  for i in 1 2 3 4 5; do
    printf 'the quick brown fox jumps over the lazy dog %d\n' "$i" > "solid_mixed/file${i}.txt"
    printf 'binary payload %d: xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n' "$i" > "solid_mixed/file${i}.bin"
  done
  rm -f "$HERE/solid-mixed.rar"
  (cd solid_mixed && rar a -s -ma5 -msbin -idq "$HERE/solid-mixed.rar" . >/dev/null)
)

# --- encrypted-data.rar: file data encrypted, headers visible ---------------
(
  cd "$WORK"
  mkdir encdata
  printf 'secret contents\n' > encdata/secret.txt
  rm -f "$HERE/encrypted-data.rar"
  (cd encdata && rar a -ma5 -ppasswd -idq "$HERE/encrypted-data.rar" . >/dev/null)
)

# --- encrypted-hdr.rar: headers encrypted too -------------------------------
(
  cd "$WORK"
  mkdir enchdr
  printf 'top secret\n' > enchdr/secret.txt
  rm -f "$HERE/encrypted-hdr.rar"
  (cd enchdr && rar a -ma5 -hppasswd -idq "$HERE/encrypted-hdr.rar" . >/dev/null)
)

# --- links.rar: symlink entry -----------------------------------------------
(
  cd "$WORK"
  mkdir links
  printf 'target\n' > links/target.txt
  ln -s target.txt links/link.txt
  rm -f "$HERE/links.rar"
  (cd links && rar a -ol -ma5 -idq "$HERE/links.rar" . >/dev/null)
)

echo "corpus regenerated in $HERE"
