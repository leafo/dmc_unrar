/* Archive-bomb cap smoke test, extraction variant.
 *
 * Companion to cap_test.c for caps that fire at extract time rather than
 * at archive open. Opens an archive (expected to succeed), then iterates
 * supported entries calling dmc_unrar_extract_file_to_heap. Passes when
 * any entry's extract returns DMC_UNRAR_INVALID_DATA. Accepts an
 * open-time rejection too, since caps that happen to catch a breach
 * earlier are still doing their job.
 *
 * Compile-time options:
 *   CAP_TEST_ARCHIVE: path to the fixture, defaults to test/corpus/solid.rar.
 *     Override with -DCAP_TEST_ARCHIVE='"test/corpus/foo.rar"' when a
 *     particular fixture is required to trigger the cap under test.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#include <stdio.h>
#include <stdlib.h>

#include "../dmc_unrar.c"

#ifndef CAP_TEST_ARCHIVE
#define CAP_TEST_ARCHIVE "test/corpus/solid.rar"
#endif

int main(void) {
	dmc_unrar_archive a;
	dmc_unrar_size_t n, i;
	dmc_unrar_return open_rc, extract_rc = DMC_UNRAR_OK;
	int triggered = 0;

	if (dmc_unrar_archive_init(&a) != DMC_UNRAR_OK) {
		fprintf(stderr, "init failed\n");
		return 1;
	}

	open_rc = dmc_unrar_archive_open_path(&a, CAP_TEST_ARCHIVE);
	if (open_rc == DMC_UNRAR_INVALID_DATA) {
		dmc_unrar_archive_close(&a);
		printf("ok: rejected at open with DMC_UNRAR_INVALID_DATA\n");
		return 0;
	}
	if (open_rc != DMC_UNRAR_OK) {
		fprintf(stderr, "open failed: %d (%s)\n",
		        (int)open_rc, dmc_unrar_strerror(open_rc));
		dmc_unrar_archive_close(&a);
		return 1;
	}

	n = dmc_unrar_get_file_count(&a);
	for (i = 0; i < n; i++) {
		void *out = NULL;
		dmc_unrar_size_t out_size = 0;
		if (dmc_unrar_file_is_directory(&a, i))
			continue;
		if (dmc_unrar_file_is_supported(&a, i) != DMC_UNRAR_OK)
			continue;
		extract_rc = dmc_unrar_extract_file_to_heap(&a, i, &out, &out_size, true);
		free(out);
		if (extract_rc == DMC_UNRAR_INVALID_DATA) {
			triggered = 1;
			break;
		}
	}

	dmc_unrar_archive_close(&a);

	if (!triggered) {
		fprintf(stderr, "expected DMC_UNRAR_INVALID_DATA during extract, "
		        "got %d (%s)\n", (int)extract_rc, dmc_unrar_strerror(extract_rc));
		return 1;
	}

	printf("ok: rejected with DMC_UNRAR_INVALID_DATA\n");
	return 0;
}
