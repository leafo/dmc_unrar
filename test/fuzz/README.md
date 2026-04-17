# Fuzzing dmc_unrar

This directory contains libFuzzer harnesses that drive the library's
public API with arbitrary bytes. Fuzzing is **out-of-band** — it does
not run in CI. Developers run it locally; every crash becomes a
regression fixture under `../fixtures/fuzz/` that the main test suite
replays under ASan on every PR.

## Requirements

- `clang` (libFuzzer ships as part of the clang runtime).
- No other packages; the harnesses `#include "dmc_unrar.c"` directly,
  exactly like `test/runner.c`.

## Targets

| Target                | Exercises                                                        |
|-----------------------|------------------------------------------------------------------|
| `fuzz_open_mem`       | Parser / block walker / header decoders (open + close).          |
| `fuzz_filename_stat`  | Metadata iteration: `get_file_stat`, `get_filename`, `is_*`.     |
| `fuzz_extract_mem`    | Decompressors + CRC path: extracts the first supported entry.    |

`fuzz_open_mem` is usually the best place to start — it's the fastest
and finds parser bugs quickly.

## Quick start

```sh
# One-off 60-second smoke run.
./run.sh fuzz_open_mem -runs=100000 -max_total_time=60

# Longer run, default forever; interrupt with ^C.
./run.sh fuzz_open_mem

# Same, but against the extractor (slower, finds different bugs).
./run.sh fuzz_extract_mem -max_total_time=600
```

`run.sh` builds the target if needed, populates `seed/` from the
existing `test/corpus/*.rar` + `test/fixtures/*.rar` archives, creates
a per-target working corpus under `work/<target>/`, and hands off to
libFuzzer.

## When libFuzzer finds a crash

libFuzzer writes a `crash-<hash>` file in CWD and aborts. To promote
it into a regression fixture:

```sh
./minimize.sh fuzz_open_mem crash-0123abcd [optional-hint]
```

The script shrinks the input with libFuzzer's built-in minimizer, saves
the minimized bytes to `test/fixtures/fuzz/fuzz_open_mem_<hash>.rar`,
and creates an empty `.note` file for a one-line description.

The fixture file alone is enough — committing it preserves the repro
input so the bug isn't lost.

**Wiring it into the regression suite is a separate step, done only
after the bug is fixed.** The main `runner.c` suite has no per-test
isolation (no fork, no timeout), so a committed fixture that still
crashes or hangs would abort or freeze `make test`. Once the
underlying bug is fixed, add an entry to the `fuzz_regressions[]`
table in `test/runner.c`:

```c
static const struct { const char *target; const char *path; }
fuzz_regressions[] = {
    { "fuzz_open_mem", "test/fixtures/fuzz/fuzz_open_mem_0123abcde.rar" },
    { NULL, NULL }
};
```

The fixture then protects the fix from regression — if the bug comes
back, ASan catches it and the test goes red.

See `test/fixtures/fuzz/README.md` for the two-phase flow and a
running list of fixtures awaiting a fix.

## Investigating a crash

Every harness is an ordinary ELF. To reproduce under a debugger:

```sh
gdb --args ./fuzz_open_mem crash-0123abcd
```

Pass a specific input as the first positional argument to replay
without fuzzing.

## Dealing with ASan OOMs and timeouts

- `-rss_limit_mb=2048` if the default 2 GiB limit trips on your box.
- `-timeout=60` to raise the per-input timeout past the default 25 s
  (useful for `fuzz_extract_mem` on PPMd/solid-looking inputs).

## Known limitations

- No coverage-driven dictionary for the RAR format yet — adding one
  would help the fuzzer find new code paths faster.
- No `-fsanitize-coverage=...` reports wired up; rely on libFuzzer's
  own stats (`-print_final_stats=1`, which `run.sh` passes by default).
- No CI integration. The regression suite catches known crashes on
  every PR, but new crashes are only found by hand-run fuzzing.
