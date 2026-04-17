#!/bin/sh
# Generates hand-crafted malformed fixtures used by the test suite.
# These derive from test/corpus/simple.rar and are checked in to the
# repo so they don't need `rar` to be present at test time.

set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
CORPUS="$HERE/../corpus"

if [ ! -f "$CORPUS/simple.rar" ]; then
  echo "build_fixtures.sh: $CORPUS/simple.rar missing; run corpus/build.sh first" >&2
  exit 1
fi

# truncated.rar: simple.rar with the last 40 bytes chopped off.
head -c $(( $(wc -c < "$CORPUS/simple.rar") - 40 )) "$CORPUS/simple.rar" > "$HERE/truncated.rar"

# corrupt-data.rar: simple.rar with a data byte flipped so a file's CRC
# validation must fail. We target the last 80 bytes (end-marker / padding
# zone is last ~8 bytes for RAR5; one of the file data bytes lives above
# that). Flipping offset len-60 puts it squarely in the first file's data.
python3 - "$CORPUS/simple.rar" "$HERE/corrupt-data.rar" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
with open(src, 'rb') as f:
    data = bytearray(f.read())
# simple.rar stores files with method -m0 (STORE), so the uncompressed
# bytes appear verbatim in the archive. Locate the string "hello world"
# and flip one byte to invalidate that file's CRC-32.
needle = b"hello world\n"
idx = data.find(needle)
if idx < 0:
    raise SystemExit("corrupt-data: couldn't locate hello world body in simple.rar")
data[idx + 5] ^= 0xFF  # flip the space character
with open(dst, 'wb') as f:
    f.write(bytes(data))
PY

echo "fixtures rebuilt in $HERE"
