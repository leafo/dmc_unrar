# Fuzz regression fixtures

Minimized inputs from libFuzzer runs (see `test/fuzz/`). Each fixture
has a sibling `.note` file with a one-line human description.

## Two phases per fixture

1. **Found** — an input arrives here as soon as the fuzzer discovers
   a bug. The library still crashes / hangs on it; the fixture is
   checked in so we don't lose the repro, but it is NOT wired into the
   main test suite yet. A fixture that aborts the library would abort
   `make test` with it, and a fixture that hangs would freeze CI.

2. **Fixed** — once the underlying bug is resolved, the fixture is
   added to the `fuzz_regressions[]` table in `test/runner.c`. At
   that point running it is a no-op (the library returns an error
   cleanly), and the regression net catches any future change that
   re-introduces the crash / hang under ASan.

Until a fork- or timeout-based isolation layer is added to
`runner.c`, step 1 and step 2 are explicitly separated — step 2
should only happen after a fix.

## Naming convention

- `<target>_<hash>.rar` — crash / abort / ASan-detected issue.
- `<target>_hang_<hash>.rar` — timeout / infinite loop / CPU pig.

`<target>` is the harness that produced it (`fuzz_open_mem`,
`fuzz_filename_stat`, `fuzz_extract_mem`). `<hash>` is a 10-char
prefix of a content hash so different crashes don't collide.

## Current regressions (fixed, wired into `runner.c`)

| Fixture | What it protects against |
|---------|--------------------------|
| `fuzz_open_mem_5e69ffd712.rar` | RAR5 header-parse hang from unbounded `file->name_size`, 129 bytes. |
| `fuzz_filename_stat_c84ffcca12.rar` | RAR5 metadata-iterate hang in the extra-field walker, 397 bytes. |
| `fuzz_extract_mem_641db2ce06_lzss.rar` | RAR5 LZSS out-of-window back-reference (assert abort), 111 bytes. |
| `fuzz_extract_mem_7fd55f96b7.rar` | Unbounded LZSS dictionary allocation (up to 4 GiB from one 64-bit header field), 335 bytes. |

First three surfaced on the initial 30-second smoke runs; the OOM was
found on the first post-fix 60-second smoke run. All four now return
cleanly and run under ASan in `test_fuzz_regressions`.

## Awaiting fix

| Fixture | What it shows |
|---------|---------------|
| `fuzz_extract_mem_73a7f1b48a_bspeek.rar` | Assert `bit_count > 0` in `dmc_unrar_bs_peek_uint32` — Huffman decode on a zero-width table. 335 B; surfaced by the post-dict-cap smoke run. |

New fuzz findings land as new fixtures here; until a fork/alarm
isolation layer is added to `runner.c`, crash-/hang-class fixtures
stay out of the regression table — see the "Two phases" section.
