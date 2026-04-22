# dmc_unrar Hardening for Wharf Integration

Plan of attack for landing a wharfd-ready dmc_unrar. Scope is narrowed to
the API surface wharfd actually uses and the threat model of untrusted
user-uploaded archives processed inside a sandboxed child process.

## How wharfd uses the library

The scan worker processes each uploaded archive in a child `wharfd scan`
subprocess. The Go binding (`dmcunrar-go`) drives the C API via:

- `dmc_unrar_archive_init`
- `dmc_unrar_archive_open` (with caller-supplied read/seek callbacks —
  reads route back into a Go `io.ReaderAt`)
- `dmc_unrar_archive_close`
- `dmc_unrar_get_file_count`
- `dmc_unrar_get_filename` (double call: size probe, then fill)
- `dmc_unrar_unicode_make_valid_utf8`
- `dmc_unrar_get_file_stat` (reads `.uncompressed_size` directly)
- `dmc_unrar_file_is_directory`
- `dmc_unrar_file_is_supported` (errors map to
  `DMC_UNRAR_ARCHIVE_UNSUPPORTED_ENCRYPTED` /
  `DMC_UNRAR_FILE_UNSUPPORTED_ENCRYPTED` for encrypted-archive handling)
- `dmc_unrar_extract_file_with_callback` (always with
  `validate_crc=true`, writes via a Go callback into a savior sink)
- `dmc_unrar_strerror`

Wharfd does **not** use:

- Path-based APIs (`dmc_unrar_extract_file_to_path` / `_unsafe`).
- Heap extraction (`dmc_unrar_extract_file_to_heap`).
- Memory extraction (`dmc_unrar_extract_file_to_mem`).

Anything gated solely on those three API families is out of scope.

## Threat model

- Input archives are untrusted and may be adversarial (archive bombs,
  malformed headers, truncation, fuzz-derived edge cases).
- Each scan runs in a sandboxed subprocess with a supervisor-enforced
  `ctx` and 24-hour hard timeout. Crashes become `EnvelopeFatal` or
  `killed by signal`; wharfd recovers, but loses diagnostic fidelity.
- Silent-correctness bugs are worse than crashes: the sandbox doesn't
  catch wrong output. CRC coverage gaps and solid-extraction
  mis-emission fall into this class.
- Hangs burn child CPU until the supervisor kills the process. A clean
  `DMC_UNRAR_*` return is strictly better than a SIGKILL.
- The Go binding passes `validate_crc=true` on every extraction and
  expects that flag to mean "the bytes the caller saw were validated."

## Already in place

Self-contained summary of the state to build on.

### Test and CI baseline
- Golden corpus under `test/corpus/` (simple RAR5, solid RAR5,
  encrypted data, encrypted headers, corrupt CRC, truncated archive,
  links).
- Differential extraction test against system `unrar` for `simple.rar`
  and `solid.rar` (`test/diff.sh`).
- ASan and UBSan runs (`make -C test test-asan`, `test-ubsan`).
- C89/GNU89 compile checks (`make -C test c89`).
- CI on Ubuntu x86_64 and arm64.
- Single-command suite: `make -C test test`.

### Correctness and safety fixes
- `dmc_unrar_extract_file_to_heap()` invalid-free fixed.
- Callback extraction honors `buffer == NULL` per docs.
- `dmc_unrar_filters_init_stack()` checks the correct pointer.
- RAR5 encrypted-header archives return
  `DMC_UNRAR_ARCHIVE_UNSUPPORTED_ENCRYPTED` instead of looking empty.
- `dmc_unrar_extract_file_to_path()` writes to a sibling temp file and
  publishes atomically on success. (Irrelevant to wharfd directly, but
  keeps the library self-consistent.)

### Parser hardening
- Memory-reader seek validation (negative offsets, overflow, past-end).
- Allocator wrappers check `items * size` overflow.
- Block-span overflow check: `start_pos + header_size + data_size` must
  neither overflow nor exceed stream size; wired into both RAR4 and
  RAR5 collect loops.
- RAR4 header CRC validation enforced (opt-out via
  `DMC_UNRAR_DISABLE_HEADER_CRC_CHECK`).
- RAR5 file-header extra-field walker: overflow and monotonicity
  checks, `DMC_UNRAR_RAR5_NAME_MAX_LENGTH` cap, boundary check on
  `name_offset + name_size` that runs even when `extra_size == 0`, and
  per-record bounds checks so a zero-length or oversize record can't
  drive an out-of-record varint read.

### Solid RAR5 correctness
- State-reuse bug fixed; tests cover sequential and out-of-order
  extraction.
- Solid packed-input offsets cached at open time (no per-extract chain
  walk).
- `rar` 7.x `solid.rar` extracts byte-identically to `unrar`.

### Resource caps (archive-bomb mitigation)
All fire deterministically with `DMC_UNRAR_INVALID_DATA`:

| Define | Default | Scope |
|---|---|---|
| `DMC_UNRAR_MAX_FILE_COUNT` | 1,000,000 | Declared entries |
| `DMC_UNRAR_MAX_FILE_SIZE` | 16 GiB | Declared uncompressed per entry |
| `DMC_UNRAR_MAX_TOTAL_SIZE` | 256 GiB | Cumulative declared uncompressed |
| `DMC_UNRAR_MAX_COMPRESSION_RATIO` | 1000:1 | Per-entry ratio; stored entries always pass |
| `DMC_UNRAR_MAX_DICT_SIZE` | 256 MiB | LZSS sliding window |
| `DMC_UNRAR_MAX_PPMD_SIZE_MB` | 32 MiB | Archive-declared PPMd heap |

Count/size/total/ratio caps fire inside the RAR4 and RAR5
block-collect loops after a real file header is parsed (metadata
re-parses don't double-count). PPMd cap fires when the decoder reads
the per-block heap hint. Ratio uses overflow-checked multiplication.

### Cooperative cancellation
- `dmc_unrar_cancel` callback on the archive struct. Polled at the top
  of the RAR4/RAR5 block-collect loops, inside the extract driver,
  around skipped solid predecessors and solid tail draining, and in
  the main decompressor loops. Returning `false` unwinds with
  `DMC_UNRAR_USER_CANCEL`. Cooperative — cannot interrupt an in-flight
  IO or extract callback.

### Fuzzing infrastructure
- Four libFuzzer harnesses: `fuzz_open_mem` (parser / block walker),
  `fuzz_filename_stat` (metadata iteration), `fuzz_extract_mem`
  (decompressor + CRC on first supported entry), and
  `fuzz_extract_solid_mem` (solid / state-reuse path).
- Seed corpus from `test/corpus/` and `test/fixtures/`, auto-populated
  by `test/fuzz/run.sh`.
- `test/fuzz/minimize.sh` for crash reduction into
  `test/fixtures/fuzz/`.
- Regression table in `runner.c` (`fuzz_regressions[]`); fixtures
  under `test/fixtures/fuzz/` run under ASan in
  `test_fuzz_regressions`.
- Workflow, per-target usage, and the two-phase fixture flow (land
  repro, wire into regression table post-fix) are documented in
  `test/fuzz/README.md`.
- Nine fuzz-found bugs fixed so far: RAR5 header-parse hang, RAR5
  metadata-walker hang, LZSS out-of-window back-reference assert,
  unbounded LZSS dictionary allocation (up to 4 GiB), zero-width /
  uninitialized Huffman decode assert, zero-length RAR5 filter
  assert, short-fill filter-input slot RAR5 block-decoder underrun,
  signed-int shift UB in `dmc_unrar_rar50_read_block_header`, x86
  filter heap-OOB-read (length-4 input). Symmetric rar30 path got
  the same runtime-check treatment.

## Outstanding work — must land before wharfd integration

Tasks are ordered roughly by expected risk-reduction for wharfd.

### 1. Parser bounds audit
Known hot path; untrusted input; remaining coverage gaps are concrete
crash vectors.

- [ ] Broader `items * size` audit at **every internal allocation
      call site**, not just the default allocator wrappers. Grep for
      allocation patterns, document which sites already use the
      checked helper, convert the rest.
- [ ] Dedicated sub-reader seek audit. Today it delegates to the
      parent and inherits the memory-reader checks — verify every
      code path honors that assumption and add a targeted test.
- [ ] Introduce a uniform checked-arithmetic helper family
      (`dmc_unrar_size_add_ok`, `dmc_unrar_size_mul_ok`, offset
      helpers). One-offs exist; consolidating them makes future
      audits tractable and prevents helper drift.
- [ ] RAR5 VLQ overlong-encoding targeted test. The decoder bounds
      bit position to 70, but there's no fuzz-independent unit test
      covering overlong encodings and shifts approaching 63 bits.
- [ ] RAR5 header CRC coverage audit. Some paths report
      `DMC_UNRAR_50_BLOCK_CHECKSUM_NO_MATCH`; confirm no header type
      silently bypasses the check.

### 2. Extraction correctness
Directly affects the `validate_crc=true` contract wharfd relies on.

- [ ] CRC-boundary audit: verify the CRC calculation covers exactly
      the bytes handed to the extract callback. Particular concern:
      solid chain transitions and filter outputs. Silent gaps here
      mean wharfd reports "validated" content that wasn't validated.
- [ ] Define and test the behavior of `dmc_unrar_cancel` fired
      mid-solid-extract. Wharfd will wire this to `ctx.Done()`;
      supervisor shutdown mid-chain must not corrupt the archive
      state for a subsequent extraction call or leave allocations
      dangling.
- [ ] Broader solid fixture coverage: varied chain length, mixed
      stored/compressed, filter-heavy archives. `solid.rar` alone is
      too narrow for a diff test to generalize.

### 3. Fuzz feedback loop
Enables pre-fix regression capture and catches whatever the audits
miss. Workflow: `./run.sh <target> -max_total_time=N` to drive a
harness, `./minimize.sh <target> crash-<hash>` to shrink and stash a
repro under `test/fixtures/fuzz/`, then add an entry to
`fuzz_regressions[]` in `runner.c` once the bug is fixed. Full
details in `test/fuzz/README.md`.

- [ ] Fork/timeout isolation in `runner.c` so hang-class and
      crash-class fixtures can sit in the regression table **before**
      the underlying bug is fixed, not just after. Today those
      fixtures live in `test/fixtures/fuzz/` disconnected from CI.
- [ ] Extend fuzzing past the current 60-second smoke runs. The
      resource caps are in, so the old unbounded-allocation timeouts
      no longer dominate the budget. Run hours, not seconds —
      `fuzz_open_mem` and `fuzz_filename_stat` first (cheapest,
      find parser bugs fastest), then `fuzz_extract_mem` and
      `fuzz_extract_solid_mem` for decoder coverage.
- [ ] Convert every newly discovered crash or hang into a regression
      test via the existing pipeline.
- [ ] Extend `make -C test test-caps` to cover the three caps it
      currently doesn't exercise (compression ratio, dictionary size,
      PPMd heap). Fuzz-discovered caps should also land as fixtures.

#### Wharfd-shaped harnesses (new)
The existing `_mem` harnesses drive the library via its built-in
memory reader and its heap-extract API. Wharfd uses neither:
it opens archives via caller-supplied `read_func`/`seek_func`
callbacks pointing into a Go `io.ReaderAt`, and it extracts via
`dmc_unrar_extract_file_with_callback` with its own write callback.
Bugs along those exact code paths (opaque handling, short reads,
callback return-value propagation, buffer-pointer swap semantics,
mid-extract cancel) won't be surfaced by the current harnesses.

- [ ] `fuzz_open_cb`: open an archive through `dmc_unrar_archive_init`
      + caller-supplied `read_func`/`seek_func` plus
      `dmc_unrar_archive_open` (not `_open_mem`). Drive the IO
      callbacks from the fuzz input and exercise the caller-callback
      error paths the `_mem` harness bypasses.
- [ ] `fuzz_extract_cb`: drive `dmc_unrar_extract_file_with_callback`
      over *every* supported entry (not just the first), with
      `validate_crc=true`. Vary the extract callback's behavior —
      returning `false` at random offsets, legitimately short writes
      — to exercise short-fill / underrun paths and verify callback
      error propagation matches what wharfd will see. This is the
      closest possible match to wharfd's actual runtime shape.
- [ ] `fuzz_cancel`: once the cancel hook is live, exercise
      `dmc_unrar_cancel` returning `false` at randomized poll points
      during open/list/extract, with follow-on calls to confirm
      `DMC_UNRAR_USER_CANCEL` unwinds cleanly and leaves no dangling
      allocator / solid-chain state.

## Nice-to-have (does not block integration)

- Static analyzer pass (clang-analyzer or similar). Cheap, may find
  latent issues the fuzzer hasn't reached.
- RAR4 corpus fixture and RAR4 diff-test. `rar` 7.x can't emit RAR4,
  so this requires a checked-in archive from an older tool. Wharfd
  sees RAR4 in the upload firehose.

## Explicitly out of scope

- **List-only mode that avoids allocating decompressor state.**
  Wharfd always extracts after listing; saving decompressor state
  would require dual code paths with no benefit.
- **Anything related to `dmc_unrar_extract_file_to_path*`.** Wharfd
  writes through savior's `FolderSink`; path safety belongs at that
  layer and the relevant work inside dmc_unrar is already complete.
- **Heap and memory extraction APIs.** Unused by wharfd.
- **Formal support-contract document and integration-decision doc.**
  The integration decision is "yes, via dmcunrar-go in a sandboxed
  child." Documenting that is future polish.

## Go-binding follow-ups (in `dmcunrar-go`, tracked here for visibility)

These don't live in this repo but must land together with the C-side
changes for wharfd to realize the benefits.

- [ ] Expose `dmc_unrar_cancel` through `dmcunrar.Archive` and wire
      it to a caller-supplied `ctx`. Without this, `scanworker.Scan`'s
      `ctx.Done()` cannot interrupt a running extraction and the
      24-hour supervisor timeout is the only backstop.
- [ ] Re-verify the `efCallbackGo` contract against current library
      behavior. The callback today writes `uncompressed_size` bytes
      from `*buffer` unconditionally and ignores `*buffer_size` /
      `*err`. The fuzz-driven filter-input changes may have tightened
      what the library expects from the callback on short-fill paths.
- [ ] Add `DMC_UNRAR_USER_CANCEL` to the `ErrorCode` mirror in
      `glue.go` and surface it through `dmcunrar.Error`. Classify it
      as retriable in wharfd's envelope logic (supervisor shutdown,
      not a bad archive).
- [ ] Decide wharfd-tuned cap values and pass them via `#cgo CFLAGS`
      (`-DDMC_UNRAR_MAX_TOTAL_SIZE=...`, etc.). Upstream defaults are
      generous; wharfd's legitimate-upload envelope is far smaller.
- [ ] Audit `savior.FolderSink` (separate repo) for path traversal
      before shipping. `boar/rarextractor` feeds
      `dmc_unrar_get_filename()` output straight into
      `savior.Entry.CanonicalPath`; the sink's `filepath.Join` is not
      traversal-safe. Sandbox limits blast radius but the extract
      tree can still be wrong.

## Suggested order of attack

1. Parser bounds audit (items 1.1–1.5). Pure defensive work on code
   paths every archive hits.
2. CRC-boundary audit and solid-cancel semantics. Seal the correctness
   story before exposing cancellation to the supervisor.
3. Fork/timeout runner isolation + extended fuzz runs. Let the fuzzer
   validate the audit work and find anything missed.
4. Go-binding wiring: cancel hook, callback re-verification, error
   mapping, cap CFLAGS.
5. Nice-to-haves and the savior traversal audit can ride along or
   land post-integration.
