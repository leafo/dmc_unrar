# dmc_unrar Hardening Plan

This document tracks the work needed to make `dmc_unrar` safer and more
predictable for app use with arbitrary RAR archives.

## Status (2026-04-17)

A first critical-fixes pass and a round of fuzz-driven hardening have
landed. Completed:

- Phase 1 baseline: golden corpus + differential harness + Makefile + CI
  (see `test/` and `.github/workflows/ci.yml`).
- All Phase 2 items below (the six "Known Issues From Initial Review").
- Phase 3 subset: memory-reader seek validation, allocation overflow in
  default wrappers, block-span overflow check before seeks, RAR4 header
  CRC validation (gated by `DMC_UNRAR_DISABLE_HEADER_CRC_CHECK`), RAR5
  file-header extra-field walker overflow/monotonicity checks plus a
  `DMC_UNRAR_RAR5_NAME_MAX_LENGTH` cap, a boundary check on
  `name_offset + name_size` that runs unconditionally (including when
  `extra_size == 0`) so `dmc_unrar_get_filename()` can't return bytes
  read from beyond the file header, and per-record bounds checks in
  the extra-field walker so a zero-length or oversize record can't
  cause the type varint to be read from outside the declared record
  (and, at the tail of the extra area, outside the block header).
- Phase 7 infrastructure: three libFuzzer harnesses
  (`open_mem` / `filename_stat` / `extract_mem`), seed corpus, crash
  minimization script, and a regression hook in `runner.c`. Fuzzing
  is out-of-band (local only, not in CI); fuzz-found bug fixtures
  live under `test/fixtures/fuzz/`.
- Phase 7 findings (nine bugs fixed so far, the most recent an ASan
  heap-OOB-read in `dmc_unrar_filters_x86_filter` caused by
  `i <= length - 5` underflowing on length-4 inputs; all four x86
  filter callers had a too-loose `< 4` guard, now `< 5`, plus
  internal belt-and-braces guards in both the x86 and ARM filters):
  RAR5 header-parse hang,
  RAR5 metadata-walker hang, LZSS out-of-window back-reference
  assert, unbounded LZSS dictionary allocation (up to 4 GiB driven
  by a RAR5 header field), zero-width / uninitialized Huffman
  decode path that drove an assert in `dmc_unrar_bs_peek_uint32`,
  a zero-length RAR5 filter that drove an assert in
  `dmc_unrar_rar50_decompress`, a short-fill filter-input slot
  where the RAR5 block decoder returned OK with an underrun, and a
  pair surfaced by the same 152-byte input — a signed-int shift UB
  in `dmc_unrar_rar50_read_block_header` (uint8_t promoted to int
  before a 24-bit left shift) and a current-offset-beyond-filter
  assert in `dmc_unrar_rar50_decompress`. The latter three
  filter-path asserts got the same runtime-check treatment in the
  symmetric rar30 path. All eight fixtures run under ASan in
  `test_fuzz_regressions`.
- Phase 6 caps: alongside `DMC_UNRAR_MAX_DICT_SIZE` (LZSS window,
  default 256 MiB), the library now enforces
  `DMC_UNRAR_MAX_FILE_COUNT` (default 1,000,000),
  `DMC_UNRAR_MAX_FILE_SIZE` (declared uncompressed per entry, default
  16 GiB), `DMC_UNRAR_MAX_TOTAL_SIZE` (cumulative declared
  uncompressed, default 256 GiB),
  `DMC_UNRAR_MAX_COMPRESSION_RATIO` (per-entry
  uncompressed/compressed ratio, default 1000), and
  `DMC_UNRAR_MAX_PPMD_SIZE_MB` (archive-declared PPMd heap, default
  32). File-count, file-size, total-size, and ratio caps fire at
  open/list time in the RAR4 and RAR5 block-collection loops, after
  a real file header is parsed (archive-sub / service / comment
  metadata re-parses don't touch the running total). The PPMd cap
  fires when the decompressor reads the per-block heap hint. Ratio
  check uses overflow-checked multiplication, not floor division,
  so values just above the cap can't sneak through.
  A cooperative cancel callback (`dmc_unrar_cancel` on the archive
  struct) is also in place: the library polls it at the top of the
  RAR4/RAR5 block-collect loops, inside the extract driver, around
  skipped solid predecessors / solid tail draining, and in the main
  decompressor loops. A `false` return unwinds with
  `DMC_UNRAR_USER_CANCEL`. This gives callers an app-level timeout /
  user-cancel hook without requiring the library to force-stop a
  runaway read or user callback. Cancellation is cooperative: it is
  observed at library poll points and cannot interrupt an IO callback
  or extraction callback that is already running.
  A list-only mode that avoids allocating decompressor state is
  still outstanding.
- Phase 5 subset: `dmc_unrar_extract_file_to_path()` now rejects unsafe
  archive filenames (traversal, absolute, UNC, drive prefix) by default
  and returns `DMC_UNRAR_FILE_UNSAFE_PATH`. Path extraction is also
  atomic now (temp file + rename), so failed extractions never leave
  half-written output. The legacy behavior is still available through
  `dmc_unrar_extract_file_to_path_unsafe()`.

Still outstanding (tracked below):

- Phase 0: formal support contract.
- Phase 1 tail: static analyzer pass, volumes/RAR4 corpus coverage.
- Phase 3 tail: configurable caps, RAR5 header CRC coverage audit, RAR5
  VLQ overlong-encoding targeted test, broader `items * size` audit,
  sub-reader seek audit.
- Phase 4: RAR5 solid archives produced by `rar` 7.x currently fail
  extraction after the first file.
- Phase 5 tail: UTF-8 enforcement in the safety check, Windows-reserved-
  name rejection, opt-in overwrite protection.
- Phase 6 tail: a list-only mode that avoids allocating decompressor
  state. The archive-bomb caps (file count, per-file size, total
  size, compression ratio, PPMd heap) and the cooperative cancel
  callback all landed in previous rounds.
- Phase 7 tail: add a fork/timeout isolation layer to `runner.c` so
  hang/crash-class fixtures can land in the regression table pre-fix,
  extend fuzzing beyond 60-second smoke runs once the Phase-6 caps
  are all in.
- Phase 8: documentation pass.

CI currently runs on Ubuntu x86_64 and arm64 (`ubuntu-latest` +
`ubuntu-24.04-arm`).


## Goals

- Make every unsupported RAR feature fail explicitly.
- Make malformed archives fail cleanly without crashes, hangs, invalid memory
  access, or unbounded allocation.
- Preserve extraction correctness against a reference implementation.
- Provide an app-safe extraction path that prevents filesystem escape.
- Keep the library's support contract and limitations documented.

## Phase 0: Define The Support Contract

- [ ] Decide the RAR feature set the app needs.
- [ ] Classify supported features, such as RAR5 non-solid, RAR5 solid, RAR4,
      stored files, compressed files, comments, filters, and PPMd.
- [ ] Classify unsupported features, such as encrypted files, encrypted
      headers, multi-volume archives, symlinks, hardlinks, devices, FIFOs,
      sockets, and generic RARVM filters.
- [ ] Decide whether the app will use extraction to memory, callbacks, file
      streams, filesystem paths, or a wrapper around those APIs.
- [ ] Define the expected return code for every unsupported feature.

Acceptance criteria:

- [ ] Unsupported features return specific errors.
- [ ] Unsupported features never look like empty archives, partial success, or
      silent extraction success.

## Phase 1: Build A Safety Baseline

- [x] Add C tests for public APIs: open, list, stat, filename, extract-to-mem,
      extract-to-heap, extract-to-file, and callback extraction.
      (`test/runner.c`)
- [x] Add a golden archive corpus generated locally with `rar` and validated
      with `unrar`. (`test/corpus/build.sh`)
- [x] Include archives for simple RAR5, solid RAR5, encrypted file data,
      encrypted headers, corrupt CRC, truncated archive, and links.
      Traversal names are covered as a unit test of the safety helper
      instead of a corpus archive (modern `rar` refuses to emit such
      names). Volumes and a RAR4 sample are still missing — `rar` 7.x
      dropped RAR4 writing support, so a checked-in fixture would be
      needed.
- [x] Add differential extraction tests against `unrar`. (`test/diff.sh`,
      currently limited to `simple.rar` — `solid.rar` is skipped because
      of the known Phase 4 RAR5-solid regression.)
- [x] Add compile checks for the advertised C89/GNU89 warning profiles.
      (`make -C test c89`)
- [x] Add ASan/UBSan test runs. (`make -C test test-asan / test-ubsan`)
- [ ] Add a static analyzer run.

Acceptance criteria:

- [x] Current known failures are captured as failing tests or expected-failure
      tests. (Regression tests for each Phase 2 bug were added alongside
      the fixes.)
- [x] The test suite can be run with a single documented command.
      (`make -C test test`; CI runs the full matrix on x86_64 and arm64.)

## Phase 2: Immediate Correctness And Safety Fixes

- [x] Fix `dmc_unrar_extract_file_to_heap()` to free `heap_buffer`, not the
      caller's `void **buffer`, on extraction failure.
- [x] Add a regression test for corrupt CRC with
      `dmc_unrar_extract_file_to_heap(..., validate_crc=true)`.
- [x] Fix the callback extraction contract: support `buffer == NULL` as
      documented (internal buffer is allocated on demand).
- [x] Fix `dmc_unrar_filters_init_stack()` so it checks the allocated stack
      pointer rather than the filters pointer.
- [x] Detect RAR5 encrypted-header archives and return
      `DMC_UNRAR_ARCHIVE_UNSUPPORTED_ENCRYPTED`.
- [x] Ensure failed `dmc_unrar_extract_file_to_path()` calls do not leave
      trusted-looking partial output. Extraction now writes to a sibling
      temp file and atomically renames on success.

Acceptance criteria:

- [x] ASan passes for corrupt CRC heap extraction.
- [x] Encrypted RAR5 headers fail with an explicit unsupported-encryption
      error.
- [x] Callback extraction behavior matches the public documentation.

## Phase 3: Harden Archive Parsing

- [ ] Add checked integer arithmetic helpers for addition, multiplication, and
      stream offset calculations. (One-off helpers added ad hoc
      -- `dmc_unrar_size_mul_ok`, `dmc_unrar_block_end_pos` -- but no
      general set of helpers yet.)
- [x] Add allocation overflow checks around `items * size` in the default
      allocator wrappers. A broader audit of every internal call site is
      still outstanding.
- [x] Validate memory-reader seek targets: no negative offsets, no offset
      overflow, and no invalid offset past stream bounds. (Sub-reader
      currently delegates to the parent and inherits those checks; a
      dedicated pass on the sub-reader path would still be worthwhile.)
- [x] Validate block spans before seeking: `start_pos + header_size +
      data_size` must not overflow and must not exceed stream size.
      (Wired into both RAR4 and RAR5 collect loops.)
- [ ] Validate RAR5 variable-length integers and reject overlong encodings or
      shifts beyond 63 bits. (Existing decoder already bounds the bit
      position to 70 before returning; a targeted audit + test has not
      been done.)
- [x] Validate RAR4 header CRCs where available. (Enforced by default;
      opt-out via `DMC_UNRAR_DISABLE_HEADER_CRC_CHECK=1`.)
- [ ] Validate RAR5 header CRCs where available. (Library reports
      `DMC_UNRAR_50_BLOCK_CHECKSUM_NO_MATCH` from some code paths; full
      coverage not audited.)
- [ ] Add configurable caps for file count, block count, filename length,
      comment size, filter count, filter stack depth, metadata allocation, and
      total archive metadata.

Acceptance criteria:

- [x] Truncated archives fail cleanly under ASan/UBSan.
      (`truncated_does_not_crash` regression test.)
- [~] Malformed block sizes fail cleanly without invalid reads or seeks.
      (Block-span overflow now caught; broader fuzzing-driven coverage
      is Phase 7.)
- [ ] Malformed archives cannot trigger unbounded memory allocation.
      (Still depends on the caps above.)

## Phase 4: Fix Extraction Correctness

- [ ] Debug RAR5 solid extraction state reuse.
- [ ] Add tests for RAR5 solid extraction in sequential order.
- [ ] Add tests for RAR5 solid extraction out of order.
- [ ] Add tests for RAR5 solid archives generated by current `rar`.
- [ ] Verify CRC calculation covers exactly the bytes emitted to the caller.
- [ ] Decide and test how early callback cancellation affects solid extraction
      state.
- [ ] Add regression tests for every extraction correctness bug found.

Acceptance criteria:

- [ ] Generated RAR5 solid archives extract byte-identically to `unrar`.
- [ ] Sequential and out-of-order solid extraction both behave predictably.
- [ ] CRC validation catches corrupted output without crashing.

## Phase 5: Provide App-Safe Filesystem Extraction

- [x] Add or document a safe extraction wrapper for app use.
      `dmc_unrar_extract_file_to_path()` is now safe by default and
      returns `DMC_UNRAR_FILE_UNSAFE_PATH` for risky archive names.
      `dmc_unrar_extract_file_to_path_unsafe()` restores the legacy
      behavior.
- [x] Normalize separators before validation. (`get_filename()` already
      normalizes to forward slashes; the safety check also rejects any
      surviving backslash.)
- [x] Reject absolute paths.
- [x] Reject `..` path components.
- [x] Reject Windows drive prefixes.
- [x] Reject UNC-style paths.
- [ ] Reject invalid UTF-8 names if the app requires UTF-8. (Library
      provides `dmc_unrar_unicode_is_valid_utf8()` but the safe-path
      check does not enforce it yet.)
- [ ] Reject or sanitize reserved platform names where relevant.
      (`CON`, `PRN`, `AUX`, `NUL`, `COMx`, `LPTx` on Windows.)
- [ ] Prevent overwriting existing files unless explicitly allowed.
      (Current rename step uses `MOVEFILE_REPLACE_EXISTING` / POSIX
      rename semantics — it will overwrite.)
- [x] Extract to a temporary file, validate CRC, then rename into place.
- [x] Never create symlinks, hardlinks, devices, FIFOs, or sockets.
      (Library refuses link entries via `DMC_UNRAR_FILE_UNSUPPORTED_LINK`
      and only ever writes through stdio / WIN32 file handles.)

Acceptance criteria:

- [~] Malicious archive names cannot write outside the extraction root on
      Linux, macOS, or Windows. (Unit-tested on Linux; Windows-specific
      patterns — drive + UNC — are rejected by the same helper but not
      integration-tested on a Windows runner.)
- [x] Partial failed extraction does not leave files that look successfully
      extracted. (Atomic temp + rename; `path_extract_no_partial_on_fail`
      regression test.)

## Phase 6: Add Resource Limits

- [x] Add a configurable maximum file count.
      (`DMC_UNRAR_MAX_FILE_COUNT`, default 1,000,000.)
- [x] Add a configurable maximum single-file uncompressed size.
      (`DMC_UNRAR_MAX_FILE_SIZE`, default 16 GiB.)
- [x] Add a configurable maximum total uncompressed size.
      (`DMC_UNRAR_MAX_TOTAL_SIZE`, default 256 GiB.)
- [x] Add a configurable maximum compression ratio.
      (`DMC_UNRAR_MAX_COMPRESSION_RATIO`, default 1000:1; only
      enforced on entries with non-zero compressed data so
      symlinks are unaffected.)
- [x] Add a configurable maximum dictionary size.
      (`DMC_UNRAR_MAX_DICT_SIZE`, default 256 MiB.)
- [x] Add a configurable maximum PPMd memory size.
      (`DMC_UNRAR_MAX_PPMD_SIZE_MB`, default 32 MiB, only enforced
      when the archive explicitly declares a heap size.)
- [x] Add callback-driven cancellation or another app-controlled timeout
      mechanism. (`dmc_unrar_cancel` on the archive struct; polled at
      the top of the RAR4/RAR5 block-collect loops, inside the extract
      driver, around skipped solid predecessors / solid tail draining,
      and in the main decompressor loops; returning `false` unwinds
      with `DMC_UNRAR_USER_CANCEL`.)
- [ ] Add a list-only mode that avoids allocating decompressor state.

Acceptance criteria:

- [~] Archive bombs fail with deterministic limit errors. All caps
      return `DMC_UNRAR_INVALID_DATA` deterministically at
      open/extract time; the cap smoke tests
      (`make -C test test-caps`) exercise the file-count, per-file,
      and total-size caps.
- [x] Limit failures do not leave partial trusted output. Caps fire
      before extraction starts (open-time caps) or before the
      decompressor writes any output byte (PPMd cap).
- [~] Limits are documented and test-covered. Defines are documented
      inline; test-caps covers three of the five caps.

## Phase 7: Fuzzing

- [x] Add a libFuzzer target for `dmc_unrar_archive_open_mem()`.
      (`test/fuzz/fuzz_open_mem.c`)
- [x] Add a libFuzzer target that extracts the first supported file from small
      archives. (`test/fuzz/fuzz_extract_mem.c`; also a metadata-only
      harness `fuzz_filename_stat.c`.)
- [x] Seed fuzzing with the golden archive corpus. (`make -C test/fuzz seed`
      hard-links `test/corpus/*.rar` and `test/fixtures/*.rar` into
      `test/fuzz/seed/`.)
- [x] Run fuzzing with ASan and UBSan. (Harnesses compile with
      `-fsanitize=fuzzer,address,undefined`.)
- [ ] Convert every discovered crash or hang into a regression test.
      Infra in place (`test/fuzz/minimize.sh` +
      `fuzz_regressions[]` table in `runner.c`). Per the fixtures
      README, fixtures land pre-fix for repro preservation but only
      get wired into the regression table once the underlying bug is
      fixed — the main suite has no fork/timeout isolation yet.

Acceptance criteria:

- [~] Initial fuzzing finds no unfixed crashes over a documented run
      duration. Initial 30-second smoke runs surfaced three bugs
      (two RAR5 header/metadata hangs and one RAR5 LZSS out-of-window
      back-reference assert). Fixtures are in `test/fixtures/fuzz/`
      with `.note` files; fixes are Phase-7 follow-up work.
- [ ] Fuzz regressions run in CI. (Out-of-band only by design; see
      `test/fuzz/README.md`. Regression replay of fixed fuzz bugs
      runs in CI via the normal `runner.c` suite.)

## Phase 8: Documentation And Integration Decision

- [ ] Document supported RAR versions and features.
- [ ] Document unsupported features and expected return codes.
- [ ] Document caller responsibilities for path safety.
- [ ] Document resource-limit behavior.
- [ ] Document thread-safety assumptions.
- [ ] Document the GPLv2-or-later license implications for app integration.
- [ ] Decide whether the app should embed this library, isolate it in a helper
      process, or use a different backend.

Acceptance criteria:

- [ ] App engineers can make an informed integration decision from the docs.
- [ ] The documented support matrix matches test coverage.

## Known Issues From Initial Review

- [x] `dmc_unrar_extract_file_to_heap()` invalid free on extraction failure.
- [ ] RAR5 solid archives generated by `rar 7.12` list correctly but corrupt or
      fail extraction after the first file.
- [x] RAR5 encrypted-header archives can be treated as empty archives instead
      of returning an unsupported-encryption error.
- [x] Raw archive names can contain traversal paths such as `../a.txt`
      (safe-by-default path extraction now rejects these).
- [x] Callback extraction rejects `buffer == NULL` despite documentation saying
      the library can allocate an internal buffer.
- [x] Default allocation wrappers do not check multiplication overflow.
- [x] Memory-reader seeks do not validate negative or out-of-range offsets.
- [x] RAR4 block-header CRC validation is marked TODO.
- [x] Some parser size calculations and block-span seeks need overflow and
      bounds validation (block-span end-position now checked; broader parser
      hardening still pending).

## Suggested Future Agent Split

- Agent A: test harness, golden corpus, and differential `unrar` tests.
- Agent B: immediate safety fixes and public API behavior.
- Agent C: parser hardening and bounds checks.
- Agent D: RAR5 solid extraction investigation and fixes.
- Agent E: safe filesystem extraction wrapper and documentation.
