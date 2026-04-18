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
| `fuzz_extract_mem_73a7f1b48a_bspeek.rar` | Huffman decode on an uninitialized / zero-width table, 335 bytes. |
| `fuzz_extract_mem_34d0acd798_filter0.rar` | RAR5 zero-length filter assert in `dmc_unrar_rar50_decompress`, 195 bytes. |
| `fuzz_extract_mem_3e8c033a03_filterunderrun.rar` | RAR5 short-fill from `rar50_decompress_block` into a filter input slot (`filter_offset != filter_length` at dmc_unrar.c:8014), 161 bytes. |
| `fuzz_extract_mem_a30f3c5c86_filteroffset.rar` | RAR5 block-size shift UBSan (`uint8_t << 24` at dmc_unrar.c:8152) + filter-offset assert (decoder advanced past the archive's declared filter position, dmc_unrar.c:8004), 152 bytes. |
| `fuzz_extract_mem_bbd9d9a307_x86filter.rar` | Heap OOB read in `dmc_unrar_filters_x86_filter` (`i <= length - 5` unsigned-underflow when length < 5; caller guards said `< 4` but the loop reads 5 bytes per window), 205 bytes. |

First three surfaced on the initial 30-second smoke runs; the OOM was
found on the first post-fix 60-second smoke run, followed by the
zero-width Huffman table crash on the next extract smoke run. The
zero-length filter crash surfaced on the 60-second smoke run taken
right after the Phase 6 caps landed. The filter-underrun crash
surfaced on the first 10-minute post-Phase-6 smoke run; the
block-size-shift + filter-offset pair surfaced on the second; the
x86-filter OOB surfaced on the third (20-minute focused extract-only
run). All nine now return cleanly and run under ASan in
`test_fuzz_regressions`.

## Awaiting fix

None currently.

New fuzz findings land as new fixtures here; until a fork/alarm
isolation layer is added to `runner.c`, crash-/hang-class fixtures
stay out of the regression table — see the "Two phases" section.
