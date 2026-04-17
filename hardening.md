# dmc_unrar Hardening Plan

This document tracks the work needed to make `dmc_unrar` safer and more
predictable for app use with arbitrary RAR archives.

## Status (2026-04-16)

A first critical-fixes pass has landed. Completed:

- Phase 1 baseline: golden corpus + differential harness + Makefile + CI
  (see `test/` and `.github/workflows/ci.yml`).
- All Phase 2 items below (the six "Known Issues From Initial Review").
- Phase 3 subset: memory-reader seek validation, allocation overflow in
  default wrappers, block-span overflow check before seeks, RAR4 header
  CRC validation (gated by `DMC_UNRAR_DISABLE_HEADER_CRC_CHECK`).
- Phase 5 subset: `dmc_unrar_extract_file_to_path()` now rejects unsafe
  archive filenames (traversal, absolute, UNC, drive prefix) by default
  and returns `DMC_UNRAR_FILE_UNSAFE_PATH`. Path extraction is also
  atomic now (temp file + rename), so failed extractions never leave
  half-written output. The legacy behavior is still available through
  `dmc_unrar_extract_file_to_path_unsafe()`.

Still outstanding (tracked below): Phase 0 (formal support contract),
most of Phase 3 (configurable caps, RAR5 header CRC coverage, VLQ
overlong-encoding hardening beyond what already exists), Phase 4 (RAR5
solid archives produced by `rar` 7.x currently fail extraction after the
first file), Phase 6 (resource limits), Phase 7 (fuzzing), and Phase 8
(documentation pass).


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

- [ ] Add C tests for public APIs: open, list, stat, filename, extract-to-mem,
      extract-to-heap, extract-to-file, and callback extraction.
- [ ] Add a golden archive corpus generated locally with `rar` and validated
      with `unrar`.
- [ ] Include archives for simple RAR5, multi-file RAR5, solid RAR5, encrypted
      file data, encrypted headers, traversal names, corrupt CRC, truncated
      archive, links, and volumes.
- [ ] Add differential extraction tests against `unrar`.
- [ ] Add compile checks for the advertised C89/GNU89 warning profiles.
- [ ] Add ASan/UBSan test runs.
- [ ] Add a static analyzer run.

Acceptance criteria:

- [ ] Current known failures are captured as failing tests or expected-failure
      tests.
- [ ] The test suite can be run with a single documented command.

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

- [ ] ASan passes for corrupt CRC heap extraction.
- [ ] Encrypted RAR5 headers fail with an explicit unsupported-encryption
      error.
- [ ] Callback extraction behavior matches the public documentation.

## Phase 3: Harden Archive Parsing

- [ ] Add checked integer arithmetic helpers for addition, multiplication, and
      stream offset calculations.
- [ ] Add allocation overflow checks around all `items * size` calculations.
- [ ] Validate memory-reader and sub-reader seek targets: no negative offsets,
      no offset overflow, and no invalid offset past stream bounds.
- [ ] Validate block spans before seeking: `start_pos + header_size +
      data_size` must not overflow and must not exceed stream size.
- [ ] Validate RAR5 variable-length integers and reject overlong encodings or
      shifts beyond 63 bits.
- [ ] Validate RAR4 header CRCs where available.
- [ ] Validate RAR5 header CRCs where available.
- [ ] Add configurable caps for file count, block count, filename length,
      comment size, filter count, filter stack depth, metadata allocation, and
      total archive metadata.

Acceptance criteria:

- [ ] Truncated archives fail cleanly under ASan/UBSan.
- [ ] Malformed block sizes fail cleanly without invalid reads or seeks.
- [ ] Malformed archives cannot trigger unbounded memory allocation.

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

- [ ] Add or document a safe extraction wrapper for app use.
- [ ] Normalize separators before validation.
- [ ] Reject absolute paths.
- [ ] Reject `..` path components.
- [ ] Reject Windows drive prefixes.
- [ ] Reject UNC-style paths.
- [ ] Reject invalid UTF-8 names if the app requires UTF-8.
- [ ] Reject or sanitize reserved platform names where relevant.
- [ ] Prevent overwriting existing files unless explicitly allowed.
- [ ] Extract to a temporary file, validate CRC, then rename into place.
- [ ] Never create symlinks, hardlinks, devices, FIFOs, or sockets.

Acceptance criteria:

- [ ] Malicious archive names cannot write outside the extraction root on
      Linux, macOS, or Windows.
- [ ] Partial failed extraction does not leave files that look successfully
      extracted.

## Phase 6: Add Resource Limits

- [ ] Add a configurable maximum file count.
- [ ] Add a configurable maximum single-file uncompressed size.
- [ ] Add a configurable maximum total uncompressed size.
- [ ] Add a configurable maximum compression ratio.
- [ ] Add a configurable maximum dictionary size.
- [ ] Add a configurable maximum PPMd memory size.
- [ ] Add callback-driven cancellation or another app-controlled timeout
      mechanism.
- [ ] Add a list-only mode that avoids allocating decompressor state.

Acceptance criteria:

- [ ] Archive bombs fail with deterministic limit errors.
- [ ] Limit failures do not leave partial trusted output.
- [ ] Limits are documented and test-covered.

## Phase 7: Fuzzing

- [ ] Add a libFuzzer target for `dmc_unrar_archive_open_mem()`.
- [ ] Add a libFuzzer target that extracts the first supported file from small
      archives.
- [ ] Seed fuzzing with the golden archive corpus.
- [ ] Run fuzzing with ASan and UBSan.
- [ ] Convert every discovered crash or hang into a regression test.

Acceptance criteria:

- [ ] Initial fuzzing finds no unfixed crashes over a documented run duration.
- [ ] Fuzz regressions run in CI or in a documented local hardening command.

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

