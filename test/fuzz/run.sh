#!/bin/sh
# Build a fuzz harness (if needed), seed it, and hand off to libFuzzer.
#
# Usage:
#   ./run.sh <target> [libfuzzer-args...]
#
# Examples:
#   ./run.sh fuzz_open_mem                                 # runs forever
#   ./run.sh fuzz_open_mem -runs=100000 -max_total_time=60 # 60s smoke

set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

if [ $# -lt 1 ]; then
  echo "usage: $0 <target> [libfuzzer-args...]" >&2
  echo "targets: fuzz_open_mem fuzz_filename_stat fuzz_extract_mem fuzz_extract_solid_mem" >&2
  exit 2
fi

target="$1"
shift

case "$target" in
  fuzz_open_mem|fuzz_filename_stat|fuzz_extract_mem|fuzz_extract_solid_mem) ;;
  *) echo "$0: unknown target '$target'" >&2; exit 2 ;;
esac

if ! command -v clang >/dev/null 2>&1; then
  echo "$0: clang not in PATH; libFuzzer needs clang" >&2
  exit 1
fi

make "$target"
make seed

mkdir -p "work/$target"

exec "./$target" "work/$target" seed \
  -print_final_stats=1 \
  -timeout=25 \
  "$@"
