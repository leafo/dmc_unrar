/* Test driver for dmc_unrar.
 *
 * Not a framework. Each test is a function returning 0 on pass, non-zero
 * on fail, registered in the tests[] table below.
 *
 * Build with: gcc -std=gnu89 -Wall -Wextra -I.. test/runner.c -o test/runner
 * Or see test/Makefile for sanitizer builds.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "../dmc_unrar.c"

/* --- support plumbing ---------------------------------------------------- */

#define CORPUS(name)   "test/corpus/" name
#define FIXTURE(name)  "test/fixtures/" name

static int g_fail;

#define T_FAIL(fmt, ...) do { \
	fprintf(stderr, "  FAIL: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
	g_fail = 1; \
	return 1; \
} while (0)

#define T_ASSERT(cond) do { \
	if (!(cond)) T_FAIL("assert failed: %s", #cond); \
} while (0)

#define T_ASSERT_EQ(got, want) do { \
	long long _g = (long long)(got), _w = (long long)(want); \
	if (_g != _w) T_FAIL("%s = %lld, expected %lld", #got, _g, _w); \
} while (0)

#define T_ASSERT_RET(got, want) do { \
	dmc_unrar_return _g = (got); dmc_unrar_return _w = (want); \
	if (_g != _w) T_FAIL("%s = %d (%s), expected %d (%s)", #got, \
	                     (int)_g, dmc_unrar_strerror(_g), \
	                     (int)_w, dmc_unrar_strerror(_w)); \
} while (0)

static void *read_whole_file(const char *path, size_t *size_out) {
	FILE *f = fopen(path, "rb");
	void *buf;
	long sz;
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
	sz = ftell(f);
	if (sz < 0) { fclose(f); return NULL; }
	if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
	buf = malloc((size_t)sz);
	if (!buf) { fclose(f); return NULL; }
	if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
	fclose(f);
	*size_out = (size_t)sz;
	return buf;
}

static dmc_unrar_size_t find_file_by_name(dmc_unrar_archive *a, const char *want) {
	dmc_unrar_size_t i, n = dmc_unrar_get_file_count(a);
	char buf[256];
	for (i = 0; i < n; i++) {
		dmc_unrar_size_t sz = dmc_unrar_get_filename(a, i, buf, sizeof(buf));
		if (sz == 0 || sz > sizeof(buf)) continue;
		if (strcmp(buf, want) == 0) return i;
	}
	return (dmc_unrar_size_t)-1;
}

/* --- individual tests ---------------------------------------------------- */

static int test_open_mem_simple(void) {
	size_t sz;
	void *data = read_whole_file(CORPUS("simple.rar"), &sz);
	dmc_unrar_archive a;
	T_ASSERT(data != NULL);

	T_ASSERT_RET(dmc_unrar_archive_init(&a), DMC_UNRAR_OK);
	T_ASSERT_RET(dmc_unrar_archive_open_mem(&a, data, sz), DMC_UNRAR_OK);
	T_ASSERT(dmc_unrar_get_file_count(&a) >= 3);

	dmc_unrar_archive_close(&a);
	free(data);
	return 0;
}

static int test_open_path_simple(void) {
	dmc_unrar_archive a;
	T_ASSERT_RET(dmc_unrar_archive_init(&a), DMC_UNRAR_OK);
	T_ASSERT_RET(dmc_unrar_archive_open_path(&a, CORPUS("simple.rar")), DMC_UNRAR_OK);
	T_ASSERT(dmc_unrar_get_file_count(&a) >= 3);
	dmc_unrar_archive_close(&a);
	return 0;
}

static int test_open_file_simple(void) {
	dmc_unrar_archive a;
	FILE *f = fopen(CORPUS("simple.rar"), "rb");
	T_ASSERT(f != NULL);
	T_ASSERT_RET(dmc_unrar_archive_init(&a), DMC_UNRAR_OK);
	T_ASSERT_RET(dmc_unrar_archive_open_file(&a, f), DMC_UNRAR_OK);
	T_ASSERT(dmc_unrar_get_file_count(&a) >= 3);
	dmc_unrar_archive_close(&a);
	/* The file handle is taken over and closed by archive_close, so no fclose here. */
	return 0;
}

static int test_is_rar(void) {
	size_t sz;
	void *data = read_whole_file(CORPUS("simple.rar"), &sz);
	T_ASSERT(data != NULL);
	T_ASSERT(dmc_unrar_is_rar_mem(data, sz));
	free(data);
	T_ASSERT(dmc_unrar_is_rar_path(CORPUS("simple.rar")));
	T_ASSERT(!dmc_unrar_is_rar_path("/etc/hostname"));
	return 0;
}

static int test_get_filename_and_stat(void) {
	dmc_unrar_archive a;
	dmc_unrar_size_t idx, n, i;
	bool saw_hello = false;
	T_ASSERT_RET(dmc_unrar_archive_init(&a), DMC_UNRAR_OK);
	T_ASSERT_RET(dmc_unrar_archive_open_path(&a, CORPUS("simple.rar")), DMC_UNRAR_OK);
	n = dmc_unrar_get_file_count(&a);
	for (i = 0; i < n; i++) {
		char name[128];
		const dmc_unrar_file *st;
		dmc_unrar_size_t sz = dmc_unrar_get_filename(&a, i, name, sizeof(name));
		T_ASSERT(sz > 0);
		st = dmc_unrar_get_file_stat(&a, i);
		T_ASSERT(st != NULL);
		if (strcmp(name, "hello.txt") == 0) {
			saw_hello = true;
			T_ASSERT_EQ(st->uncompressed_size, 12);
		}
	}
	T_ASSERT(saw_hello);
	idx = find_file_by_name(&a, "hello.txt");
	T_ASSERT(idx != (dmc_unrar_size_t)-1);
	dmc_unrar_archive_close(&a);
	return 0;
}

static int test_extract_to_mem(void) {
	dmc_unrar_archive a;
	dmc_unrar_size_t idx, written = 0;
	char buf[64];
	T_ASSERT_RET(dmc_unrar_archive_init(&a), DMC_UNRAR_OK);
	T_ASSERT_RET(dmc_unrar_archive_open_path(&a, CORPUS("simple.rar")), DMC_UNRAR_OK);
	idx = find_file_by_name(&a, "hello.txt");
	T_ASSERT(idx != (dmc_unrar_size_t)-1);
	T_ASSERT_RET(dmc_unrar_extract_file_to_mem(&a, idx, buf, sizeof(buf), &written, true),
	             DMC_UNRAR_OK);
	T_ASSERT_EQ(written, 12);
	T_ASSERT(memcmp(buf, "hello world\n", 12) == 0);
	dmc_unrar_archive_close(&a);
	return 0;
}

static int test_extract_to_heap_ok(void) {
	dmc_unrar_archive a;
	dmc_unrar_size_t idx, written = 0;
	void *out = NULL;
	T_ASSERT_RET(dmc_unrar_archive_init(&a), DMC_UNRAR_OK);
	T_ASSERT_RET(dmc_unrar_archive_open_path(&a, CORPUS("simple.rar")), DMC_UNRAR_OK);
	idx = find_file_by_name(&a, "hello.txt");
	T_ASSERT(idx != (dmc_unrar_size_t)-1);
	T_ASSERT_RET(dmc_unrar_extract_file_to_heap(&a, idx, &out, &written, true),
	             DMC_UNRAR_OK);
	T_ASSERT(out != NULL);
	T_ASSERT_EQ(written, 12);
	T_ASSERT(memcmp(out, "hello world\n", 12) == 0);
	free(out);
	dmc_unrar_archive_close(&a);
	return 0;
}

/* Regression test for B.1: extract_to_heap on a CRC-failing archive must
 * not double-free or leak. Pre-fix: the failure path frees the caller's
 * `void **buffer` handle (an invalid free). ASan catches that. */
static int test_extract_to_heap_corrupt_crc(void) {
	dmc_unrar_archive a;
	dmc_unrar_size_t idx, written = 0;
	void *out = NULL;
	dmc_unrar_return r;
	T_ASSERT_RET(dmc_unrar_archive_init(&a), DMC_UNRAR_OK);
	T_ASSERT_RET(dmc_unrar_archive_open_path(&a, FIXTURE("corrupt-data.rar")), DMC_UNRAR_OK);
	idx = find_file_by_name(&a, "hello.txt");
	if (idx == (dmc_unrar_size_t)-1) idx = find_file_by_name(&a, "ten.bin");
	if (idx == (dmc_unrar_size_t)-1) idx = find_file_by_name(&a, "sub/nested.txt");
	T_ASSERT(idx != (dmc_unrar_size_t)-1);
	r = dmc_unrar_extract_file_to_heap(&a, idx, &out, &written, true);
	/* Accept CRC32_FAIL (our expected failure) or any other failure that
	 * doesn't corrupt memory. The point of this test is to exercise the
	 * failure free-path under ASan. */
	if (r == DMC_UNRAR_OK) {
		/* Unexpected success — corruption didn't actually invalidate this
		 * file. Free and pass. */
		free(out);
	} else {
		/* On failure the library must leave `out` as NULL (no ownership
		 * transferred to the caller). */
		T_ASSERT_EQ((long long)(uintptr_t)out, 0);
	}
	dmc_unrar_archive_close(&a);
	return 0;
}

/* Regression test for B.3: RAR5 encrypted-header archive must fail open
 * with DMC_UNRAR_ARCHIVE_UNSUPPORTED_ENCRYPTED, not silently look empty. */
static int test_encrypted_header_detected(void) {
	dmc_unrar_archive a;
	dmc_unrar_return r;
	T_ASSERT_RET(dmc_unrar_archive_init(&a), DMC_UNRAR_OK);
	r = dmc_unrar_archive_open_path(&a, CORPUS("encrypted-hdr.rar"));
	T_ASSERT_RET(r, DMC_UNRAR_ARCHIVE_UNSUPPORTED_ENCRYPTED);
	dmc_unrar_archive_close(&a);
	return 0;
}

/* File-data encryption should list fine but file_is_supported should say
 * UNSUPPORTED_ENCRYPTED. */
static int test_encrypted_data_unsupported(void) {
	dmc_unrar_archive a;
	dmc_unrar_size_t i, n;
	bool found_enc = false;
	T_ASSERT_RET(dmc_unrar_archive_init(&a), DMC_UNRAR_OK);
	T_ASSERT_RET(dmc_unrar_archive_open_path(&a, CORPUS("encrypted-data.rar")), DMC_UNRAR_OK);
	n = dmc_unrar_get_file_count(&a);
	for (i = 0; i < n; i++) {
		dmc_unrar_return s = dmc_unrar_file_is_supported(&a, i);
		if (s == DMC_UNRAR_FILE_UNSUPPORTED_ENCRYPTED) found_enc = true;
	}
	T_ASSERT(found_enc);
	dmc_unrar_archive_close(&a);
	return 0;
}

static int test_symlink_unsupported(void) {
	dmc_unrar_archive a;
	dmc_unrar_size_t i, n;
	bool found_link = false;
	T_ASSERT_RET(dmc_unrar_archive_init(&a), DMC_UNRAR_OK);
	T_ASSERT_RET(dmc_unrar_archive_open_path(&a, CORPUS("links.rar")), DMC_UNRAR_OK);
	n = dmc_unrar_get_file_count(&a);
	for (i = 0; i < n; i++) {
		dmc_unrar_return s = dmc_unrar_file_is_supported(&a, i);
		if (s == DMC_UNRAR_FILE_UNSUPPORTED_LINK) found_link = true;
	}
	T_ASSERT(found_link);
	dmc_unrar_archive_close(&a);
	return 0;
}

/* Truncated archive: library must not crash. It may return an error from
 * open or extract. */
static int test_truncated_does_not_crash(void) {
	dmc_unrar_archive a;
	dmc_unrar_size_t i, n;
	dmc_unrar_return r;
	T_ASSERT_RET(dmc_unrar_archive_init(&a), DMC_UNRAR_OK);
	r = dmc_unrar_archive_open_path(&a, FIXTURE("truncated.rar"));
	if (r == DMC_UNRAR_OK) {
		n = dmc_unrar_get_file_count(&a);
		for (i = 0; i < n; i++) {
			char tmp[256];
			dmc_unrar_size_t written = 0;
			(void)dmc_unrar_extract_file_to_mem(&a, i, tmp, sizeof(tmp), &written, false);
		}
	}
	dmc_unrar_archive_close(&a);
	return 0;
}

/* Callback plumbing for the NULL-buffer regression test. */
typedef struct {
	size_t total;
	bool   saw_nonnull_buffer;
} cb_state;

static bool cb_collect(void *opaque, void **buffer, dmc_unrar_size_t *buffer_size,
		dmc_unrar_size_t uncompressed_size, dmc_unrar_return *err) {
	cb_state *s = (cb_state *)opaque;
	(void)buffer_size; (void)err;
	if (*buffer != NULL) s->saw_nonnull_buffer = true;
	s->total += uncompressed_size;
	return true;
}

/* Regression test for B.4: passing buffer == NULL should be supported per
 * the public API docs. The library must allocate its own internal buffer
 * and hand a non-NULL pointer to the callback. */
static int test_callback_null_buffer(void) {
	dmc_unrar_archive a;
	dmc_unrar_size_t idx;
	cb_state s;
	s.total = 0;
	s.saw_nonnull_buffer = false;

	T_ASSERT_RET(dmc_unrar_archive_init(&a), DMC_UNRAR_OK);
	T_ASSERT_RET(dmc_unrar_archive_open_path(&a, CORPUS("simple.rar")), DMC_UNRAR_OK);
	idx = find_file_by_name(&a, "hello.txt");
	T_ASSERT(idx != (dmc_unrar_size_t)-1);

	T_ASSERT_RET(dmc_unrar_extract_file_with_callback(&a, idx, NULL, 4096, NULL,
	                                                  false, &s, cb_collect),
	             DMC_UNRAR_OK);
	T_ASSERT_EQ(s.total, 12);
	T_ASSERT(s.saw_nonnull_buffer);
	dmc_unrar_archive_close(&a);
	return 0;
}

/* Phase D.1: filename_is_safe() helper behavior.
 *
 * These exercise the internal helper directly (it's static; because we
 * include dmc_unrar.c as a translation unit, we can call it here). */
static int test_filename_is_safe_helper(void) {
	/* Safe names */
	T_ASSERT(dmc_unrar_filename_is_safe("foo.txt"));
	T_ASSERT(dmc_unrar_filename_is_safe("sub/nested.txt"));
	T_ASSERT(dmc_unrar_filename_is_safe("a/b/c.txt"));
	/* Empty */
	T_ASSERT(!dmc_unrar_filename_is_safe(""));
	/* Absolute */
	T_ASSERT(!dmc_unrar_filename_is_safe("/etc/passwd"));
	/* Traversal */
	T_ASSERT(!dmc_unrar_filename_is_safe("../etc/passwd"));
	T_ASSERT(!dmc_unrar_filename_is_safe("a/../b"));
	T_ASSERT(!dmc_unrar_filename_is_safe("a/b/.."));
	T_ASSERT(!dmc_unrar_filename_is_safe(".."));
	/* Windows drive */
	T_ASSERT(!dmc_unrar_filename_is_safe("C:/foo"));
	T_ASSERT(!dmc_unrar_filename_is_safe("C:\\foo"));
	/* UNC */
	T_ASSERT(!dmc_unrar_filename_is_safe("//server/share"));
	T_ASSERT(!dmc_unrar_filename_is_safe("\\\\server\\share"));
	/* Substring "..a" is NOT a traversal */
	T_ASSERT(dmc_unrar_filename_is_safe("..a"));
	T_ASSERT(dmc_unrar_filename_is_safe("a.."));
	T_ASSERT(dmc_unrar_filename_is_safe("a/..b"));
	return 0;
}

/* Confirms the safe-default path extraction still succeeds for
 * ordinary archive filenames. */
static int test_extract_to_path_safe_default(void) {
	dmc_unrar_archive a;
	dmc_unrar_size_t idx, written = 0;
	const char *out = "test-extract-safe.out";
	T_ASSERT_RET(dmc_unrar_archive_init(&a), DMC_UNRAR_OK);
	T_ASSERT_RET(dmc_unrar_archive_open_path(&a, CORPUS("simple.rar")), DMC_UNRAR_OK);
	idx = find_file_by_name(&a, "hello.txt");
	T_ASSERT(idx != (dmc_unrar_size_t)-1);
	(void)unlink(out);
	T_ASSERT_RET(dmc_unrar_extract_file_to_path(&a, idx, out, &written, true),
	             DMC_UNRAR_OK);
	T_ASSERT_EQ(written, 12);
	(void)unlink(out);
	dmc_unrar_archive_close(&a);
	return 0;
}

/* The _unsafe variant must bypass the safety check; we use it on an
 * ordinary archive to confirm it still succeeds. (We can't practically
 * exercise the _actually unsafe_ case here since rar refuses to create
 * traversal names — see test_filename_is_safe_helper for unit-level
 * coverage of the rejection logic.) */
static int test_extract_to_path_unsafe_variant(void) {
	dmc_unrar_archive a;
	dmc_unrar_size_t idx, written = 0;
	const char *out = "test-extract-unsafe.out";
	T_ASSERT_RET(dmc_unrar_archive_init(&a), DMC_UNRAR_OK);
	T_ASSERT_RET(dmc_unrar_archive_open_path(&a, CORPUS("simple.rar")), DMC_UNRAR_OK);
	idx = find_file_by_name(&a, "hello.txt");
	T_ASSERT(idx != (dmc_unrar_size_t)-1);
	(void)unlink(out);
	T_ASSERT_RET(dmc_unrar_extract_file_to_path_unsafe(&a, idx, out, &written, true),
	             DMC_UNRAR_OK);
	T_ASSERT_EQ(written, 12);
	(void)unlink(out);
	dmc_unrar_archive_close(&a);
	return 0;
}

/* Phase B.5: failed extract_file_to_path must not leave a half-written
 * file on disk. Simulate by asking the library to extract a file from a
 * corrupt archive with validate_crc=true; the file at the output path
 * must not exist after the failure. */
static int test_path_extract_no_partial_on_fail(void) {
	dmc_unrar_archive a;
	dmc_unrar_size_t idx, written = 0;
	dmc_unrar_return r;
	struct stat st;
	const char *out = "test-partial.out";
	T_ASSERT_RET(dmc_unrar_archive_init(&a), DMC_UNRAR_OK);
	T_ASSERT_RET(dmc_unrar_archive_open_path(&a, FIXTURE("corrupt-data.rar")), DMC_UNRAR_OK);
	/* Pick any file. */
	T_ASSERT(dmc_unrar_get_file_count(&a) > 0);
	/* Find first non-directory. */
	{
		dmc_unrar_size_t i, n = dmc_unrar_get_file_count(&a);
		idx = (dmc_unrar_size_t)-1;
		for (i = 0; i < n; i++) {
			if (!dmc_unrar_file_is_directory(&a, i) &&
			    dmc_unrar_file_is_supported(&a, i) == DMC_UNRAR_OK) {
				idx = i;
				break;
			}
		}
	}
	T_ASSERT(idx != (dmc_unrar_size_t)-1);
	(void)unlink(out);
	r = dmc_unrar_extract_file_to_path(&a, idx, out, &written, true);
	if (r != DMC_UNRAR_OK) {
		/* File must not exist. */
		if (stat(out, &st) == 0) {
			(void)unlink(out);
			T_FAIL("partial output file left after failed extraction");
		}
	} else {
		(void)unlink(out);
	}
	dmc_unrar_archive_close(&a);
	return 0;
}

/* Phase 7 fuzzing regressions. As crashes are discovered by the
 * libFuzzer harnesses under test/fuzz/, minimize.sh saves a fixture
 * under test/fixtures/fuzz/ and we add an entry here. The test only
 * asserts no memory corruption — it does not assert a specific return
 * code — so a fixture can land before the underlying bug is fixed.
 * The ASan build of the suite is what turns a "no crash" result into
 * a real safety check.
 *
 * The `target` field names which harness originally produced the
 * fixture (open / filename / extract); we replay each fixture through
 * the matching API surface. */
static const struct {
	const char *target;
	const char *path;
} fuzz_regressions[] = {
	{ "fuzz_open_mem",      "test/fixtures/fuzz/fuzz_open_mem_5e69ffd712.rar" },
	{ "fuzz_filename_stat", "test/fixtures/fuzz/fuzz_filename_stat_c84ffcca12.rar" },
	{ "fuzz_extract_mem",   "test/fixtures/fuzz/fuzz_extract_mem_641db2ce06_lzss.rar" },
	{ "fuzz_extract_mem",   "test/fixtures/fuzz/fuzz_extract_mem_7fd55f96b7.rar" },
	{ "fuzz_extract_mem",   "test/fixtures/fuzz/fuzz_extract_mem_73a7f1b48a_bspeek.rar" },
	{ "fuzz_extract_mem",   "test/fixtures/fuzz/fuzz_extract_mem_34d0acd798_filter0.rar" },
	{ "fuzz_extract_mem",   "test/fixtures/fuzz/fuzz_extract_mem_3e8c033a03_filterunderrun.rar" },
	{ "fuzz_extract_mem",   "test/fixtures/fuzz/fuzz_extract_mem_a30f3c5c86_filteroffset.rar" },
	{ "fuzz_extract_mem",   "test/fixtures/fuzz/fuzz_extract_mem_bbd9d9a307_x86filter.rar" },
	{ NULL, NULL }
};

static int test_fuzz_regressions(void) {
	size_t i;
	for (i = 0; fuzz_regressions[i].path != NULL; i++) {
		const char *target = fuzz_regressions[i].target;
		const char *path   = fuzz_regressions[i].path;
		dmc_unrar_archive a;

		if (dmc_unrar_archive_init(&a) != DMC_UNRAR_OK)
			continue;
		if (dmc_unrar_archive_open_path(&a, path) != DMC_UNRAR_OK) {
			dmc_unrar_archive_close(&a);
			continue;
		}

		if (target && strcmp(target, "fuzz_filename_stat") == 0) {
			dmc_unrar_size_t n = dmc_unrar_get_file_count(&a), j;
			for (j = 0; j < n; j++) {
				char buf[1024];
				(void)dmc_unrar_get_file_stat(&a, j);
				(void)dmc_unrar_file_is_directory(&a, j);
				(void)dmc_unrar_file_is_supported(&a, j);
				(void)dmc_unrar_get_filename(&a, j, buf, sizeof(buf));
			}
		} else if (target && strcmp(target, "fuzz_extract_mem") == 0) {
			dmc_unrar_size_t n = dmc_unrar_get_file_count(&a), j;
			for (j = 0; j < n; j++) {
				void *out = NULL;
				dmc_unrar_size_t out_size = 0;
				const dmc_unrar_file *stat_;
				if (dmc_unrar_file_is_directory(&a, j)) continue;
				if (dmc_unrar_file_is_supported(&a, j) != DMC_UNRAR_OK) continue;
				stat_ = dmc_unrar_get_file_stat(&a, j);
				if (!stat_ || stat_->uncompressed_size > (16u * 1024u * 1024u)) continue;
				(void)dmc_unrar_extract_file_to_heap(&a, j, &out, &out_size, true);
				free(out);
				break;
			}
		}
		/* Default: open + close, matching fuzz_open_mem. */

		dmc_unrar_archive_close(&a);
	}
	return 0;
}

/* --- test registry ------------------------------------------------------- */

typedef int (*test_fn)(void);
typedef struct { const char *name; test_fn fn; } test_entry;

static const test_entry tests[] = {
	{ "open_mem_simple",              test_open_mem_simple },
	{ "open_path_simple",             test_open_path_simple },
	{ "open_file_simple",             test_open_file_simple },
	{ "is_rar",                       test_is_rar },
	{ "get_filename_and_stat",        test_get_filename_and_stat },
	{ "extract_to_mem",               test_extract_to_mem },
	{ "extract_to_heap_ok",           test_extract_to_heap_ok },
	{ "extract_to_heap_corrupt_crc",  test_extract_to_heap_corrupt_crc },
	{ "encrypted_header_detected",    test_encrypted_header_detected },
	{ "encrypted_data_unsupported",   test_encrypted_data_unsupported },
	{ "symlink_unsupported",          test_symlink_unsupported },
	{ "truncated_does_not_crash",     test_truncated_does_not_crash },
	{ "callback_null_buffer",         test_callback_null_buffer },
	{ "path_extract_no_partial_on_fail", test_path_extract_no_partial_on_fail },
	{ "filename_is_safe_helper",      test_filename_is_safe_helper },
	{ "extract_to_path_safe_default", test_extract_to_path_safe_default },
	{ "extract_to_path_unsafe_variant", test_extract_to_path_unsafe_variant },
	{ "fuzz_regressions",             test_fuzz_regressions },
};

int main(int argc, char **argv) {
	size_t i, n = sizeof(tests) / sizeof(tests[0]);
	int pass = 0, fail = 0;
	const char *filter = (argc > 1) ? argv[1] : NULL;

	for (i = 0; i < n; i++) {
		if (filter && !strstr(tests[i].name, filter)) continue;
		g_fail = 0;
		printf("-- %s\n", tests[i].name);
		if (tests[i].fn() == 0 && !g_fail) {
			pass++;
			printf("   ok\n");
		} else {
			fail++;
		}
	}

	printf("\n%d passed, %d failed\n", pass, fail);
	return fail == 0 ? 0 : 1;
}
