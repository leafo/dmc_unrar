/* Phase 5 tail smoke tests.
 *
 * Compiled three times with different DMC_UNRAR_* defines forced on, each
 * paired with a PHASE5_MODE_* selector. See test/Makefile for the three
 * invocations. Exits 0 on pass, nonzero with a message on fail.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../dmc_unrar.c"

#if defined(PHASE5_MODE_UTF8)

static int check(const char *name, bool expect_safe) {
	bool got = dmc_unrar_filename_is_safe(name);
	if (got != expect_safe) {
		fprintf(stderr, "FAIL: dmc_unrar_filename_is_safe(%s) = %d, expected %d\n",
		        name, (int)got, (int)expect_safe);
		return 1;
	}
	return 0;
}

int main(void) {
	int fail = 0;
	/* Valid UTF-8 names still pass. */
	fail |= check("foo.txt", true);
	fail |= check("sub/nested.txt", true);
	/* Valid multi-byte UTF-8 (snowman, U+2603 = E2 98 83). */
	fail |= check("snow\xe2\x98\x83.txt", true);
	/* Lone continuation byte. */
	fail |= check("bad\x80name", false);
	/* Truncated two-byte sequence (leading byte, missing continuation). */
	fail |= check("bad\xc2", false);
	/* Invalid five-byte sequence (F8 is not a valid lead byte). */
	fail |= check("bad\xf8\x80\x80\x80\x80", false);
	if (fail == 0)
		printf("ok: reject non-UTF-8 paths\n");
	return fail;
}

#elif defined(PHASE5_MODE_RESERVED)

static int check(const char *name, bool expect_safe) {
	bool got = dmc_unrar_filename_is_safe(name);
	if (got != expect_safe) {
		fprintf(stderr, "FAIL: dmc_unrar_filename_is_safe(%s) = %d, expected %d\n",
		        name, (int)got, (int)expect_safe);
		return 1;
	}
	return 0;
}

int main(void) {
	int fail = 0;
	/* Bare reserved names. */
	fail |= check("CON", false);
	fail |= check("PRN", false);
	fail |= check("AUX", false);
	fail |= check("NUL", false);
	fail |= check("COM1", false);
	fail |= check("COM9", false);
	fail |= check("LPT1", false);
	fail |= check("LPT9", false);
	/* With extensions -- still reserved per Windows semantics. */
	fail |= check("CON.txt", false);
	fail |= check("nul.log", false);
	fail |= check("com2.bin", false);
	/* Nested under a subdir -- still reserved. */
	fail |= check("sub/CON", false);
	fail |= check("a/b/LPT3.dat", false);
	/* Not reserved: stem differs. */
	fail |= check("conflict.txt", true);
	fail |= check("nullish", true);
	fail |= check("com0.bin", true);
	fail |= check("com10.bin", true);
	fail |= check("lpt.txt", true);
	fail |= check("CONsole.txt", true);
	/* Not reserved: dotfile with reserved tail. */
	fail |= check(".conrc", true);
	/* Windows-illegal component syntax. */
	fail |= check("trailing-dot.", false);
	fail |= check("trailing-space ", false);
	fail |= check("sub/trailing-dot.", false);
	fail |= check("sub/trailing-space ", false);
	fail |= check("has<lt.txt", false);
	fail |= check("has>gt.txt", false);
	fail |= check("has:colon.txt", false);
	fail |= check("sub/stream:name", false);
	fail |= check("has\"quote.txt", false);
	fail |= check("has|pipe.txt", false);
	fail |= check("has?question.txt", false);
	fail |= check("has*star.txt", false);
	fail |= check("has\x1f" "control.txt", false);
	/* Still legal on Windows. */
	fail |= check("space inside.txt", true);
	fail |= check("many.dots.inside.txt", true);
	if (fail == 0)
		printf("ok: Windows unsafe-name rejection\n");
	return fail;
}

#elif defined(PHASE5_MODE_OVERWRITE)

static int write_text_file(const char *path, const char *text) {
	FILE *f = fopen(path, "wb");
	size_t len = strlen(text);
	if (!f) {
		fprintf(stderr, "FAIL: fopen(%s)\n", path);
		return 1;
	}
	if (fwrite(text, 1, len, f) != len) {
		fprintf(stderr, "FAIL: fwrite(%s)\n", path);
		fclose(f);
		return 1;
	}
	if (fclose(f) != 0) {
		fprintf(stderr, "FAIL: fclose(%s)\n", path);
		return 1;
	}
	return 0;
}

static int expect_text_file(const char *path, const char *text) {
	char buf[128];
	FILE *f = fopen(path, "rb");
	size_t want = strlen(text);
	size_t got;
	if (!f) {
		fprintf(stderr, "FAIL: fopen(%s)\n", path);
		return 1;
	}
	got = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	if (got != want || memcmp(buf, text, want) != 0) {
		fprintf(stderr, "FAIL: %s contents changed\n", path);
		return 1;
	}
	return 0;
}

int main(void) {
	const char *out = "phase5-overwrite.out";
	const char *stale_tmp0 = "phase5-overwrite.out.dmcunrar.tmp.00000000";
	const char *stale_tmp1 = "phase5-overwrite.out.dmcunrar.tmp.00000001";
	const char *race_tmp = "phase5-race.tmp";
	const char *race_out = "phase5-race.out";
	dmc_unrar_archive a;
	dmc_unrar_size_t idx, n, i, written = 0;
	dmc_unrar_return r;
	struct stat st;
	off_t first_size;
	mode_t old_umask = 0;
	bool restore_umask = false;
	int fail = 0;

	(void)unlink(out);
	(void)unlink(stale_tmp0);
	(void)unlink(stale_tmp1);
	(void)unlink(race_tmp);
	(void)unlink(race_out);

	if (dmc_unrar_archive_init(&a) != DMC_UNRAR_OK) {
		fprintf(stderr, "FAIL: archive_init\n");
		return 1;
	}
	if (dmc_unrar_archive_open_path(&a, "test/corpus/simple.rar") != DMC_UNRAR_OK) {
		fprintf(stderr, "FAIL: archive_open_path\n");
		dmc_unrar_archive_close(&a);
		return 1;
	}

	n = dmc_unrar_get_file_count(&a);
	idx = (dmc_unrar_size_t)-1;
	for (i = 0; i < n; i++) {
		if (!dmc_unrar_file_is_directory(&a, i) &&
		    dmc_unrar_file_is_supported(&a, i) == DMC_UNRAR_OK) {
			idx = i;
			break;
		}
	}
	if (idx == (dmc_unrar_size_t)-1) {
		fprintf(stderr, "FAIL: no extractable entry\n");
		dmc_unrar_archive_close(&a);
		return 1;
	}

#ifndef _WIN32
	old_umask = umask(022);
	restore_umask = true;
#endif

	/* A stale first-choice temp sibling must not be overwritten. */
	if (write_text_file(stale_tmp0, "stale-temp") != 0) {
		fail = 1;
		goto done;
	}

	/* First extract must succeed by using a different exclusive temp. */
	r = dmc_unrar_extract_file_to_path(&a, idx, out, &written, true);
	if (r != DMC_UNRAR_OK) {
		fprintf(stderr, "FAIL: first extract = %d (%s)\n", (int)r, dmc_unrar_strerror(r));
		fail = 1;
		goto done;
	}
	if (stat(out, &st) != 0) {
		fprintf(stderr, "FAIL: first extract did not produce %s\n", out);
		fail = 1;
		goto done;
	}
#ifndef _WIN32
	if ((st.st_mode & 0777) != 0644) {
		fprintf(stderr, "FAIL: extracted mode = %03o, expected %03o\n",
		        (unsigned)(st.st_mode & 0777), (unsigned)0644);
		fail = 1;
		goto done;
	}
#endif
	first_size = st.st_size;
	if (expect_text_file(stale_tmp0, "stale-temp") != 0) {
		fail = 1;
		goto done;
	}

	/* Second extract must fail with DMC_UNRAR_FILE_EXISTS and leave the
	   original file untouched (same size, no sibling temp). */
	r = dmc_unrar_extract_file_to_path(&a, idx, out, &written, true);
	if (r != DMC_UNRAR_FILE_EXISTS) {
		fprintf(stderr, "FAIL: second extract = %d (%s), expected DMC_UNRAR_FILE_EXISTS\n",
		        (int)r, dmc_unrar_strerror(r));
		fail = 1;
		goto done;
	}
	if (stat(out, &st) != 0 || st.st_size != first_size) {
		fprintf(stderr, "FAIL: target file disturbed after refused overwrite\n");
		fail = 1;
		goto done;
	}

	/* The temp sibling used for the successful extract must not linger. */
	if (stat(stale_tmp1, &st) == 0) {
		fprintf(stderr, "FAIL: leftover temp file %s\n", stale_tmp1);
		(void)unlink(stale_tmp1);
		fail = 1;
		goto done;
	}

	/* The _unsafe variant bypasses path-safety checks, but
	   DMC_UNRAR_REJECT_OVERWRITE fires inside the shared impl because
	   it concerns the caller-provided target path, not the archive
	   name. Confirm _unsafe also rejects the overwrite. */
	r = dmc_unrar_extract_file_to_path_unsafe(&a, idx, out, &written, true);
	if (r != DMC_UNRAR_FILE_EXISTS) {
		fprintf(stderr, "FAIL: _unsafe second extract = %d (%s), expected DMC_UNRAR_FILE_EXISTS\n",
		        (int)r, dmc_unrar_strerror(r));
		fail = 1;
		goto done;
	}

	/* If the final target appears after temp creation, no-replace publish
	   must return FILE_EXISTS and leave both files untouched. */
	if (write_text_file(race_tmp, "temp") != 0 ||
	    write_text_file(race_out, "target") != 0) {
		fail = 1;
		goto done;
	}
	r = dmc_unrar_publish_temp_file(race_tmp, race_out);
	if (r != DMC_UNRAR_FILE_EXISTS) {
		fprintf(stderr, "FAIL: publish race = %d (%s), expected DMC_UNRAR_FILE_EXISTS\n",
		        (int)r, dmc_unrar_strerror(r));
		fail = 1;
		goto done;
	}
	if (expect_text_file(race_tmp, "temp") != 0 ||
	    expect_text_file(race_out, "target") != 0) {
		fail = 1;
		goto done;
	}

	if (fail == 0)
		printf("ok: reject overwrite\n");
done:
	(void)unlink(out);
	(void)unlink(stale_tmp0);
	(void)unlink(stale_tmp1);
	(void)unlink(race_tmp);
	(void)unlink(race_out);
	if (restore_umask)
		(void)umask(old_umask);
	dmc_unrar_archive_close(&a);
	return fail;
}

#else
#error "define one of PHASE5_MODE_UTF8, PHASE5_MODE_RESERVED, PHASE5_MODE_OVERWRITE"
#endif
