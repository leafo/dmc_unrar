/* Archive-bomb cap smoke test.
 *
 * Compiled multiple times with different -D overrides that each force
 * one of the DMC_UNRAR_MAX_* caps low enough to reject a corpus fixture
 * at open time. Passes when dmc_unrar_archive_open_path returns
 * DMC_UNRAR_INVALID_DATA. See test/Makefile for the -D variants.
 *
 * Compile-time options:
 *   CAP_TEST_ARCHIVE: path to the fixture, defaults to test/corpus/simple.rar.
 *     Override when a particular archive is needed to trigger a given cap.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#include <stdio.h>
#include <stdlib.h>

#include "../dmc_unrar.c"

#ifndef CAP_TEST_ARCHIVE
#define CAP_TEST_ARCHIVE "test/corpus/simple.rar"
#endif

int main(void) {
	dmc_unrar_archive a;
	dmc_unrar_return rc;

	if (dmc_unrar_archive_init(&a) != DMC_UNRAR_OK) {
		fprintf(stderr, "init failed\n");
		return 1;
	}

	rc = dmc_unrar_archive_open_path(&a, CAP_TEST_ARCHIVE);
	dmc_unrar_archive_close(&a);

	if (rc != DMC_UNRAR_INVALID_DATA) {
		fprintf(stderr, "expected DMC_UNRAR_INVALID_DATA (%d), got %d (%s)\n",
		        (int)DMC_UNRAR_INVALID_DATA, (int)rc, dmc_unrar_strerror(rc));
		return 1;
	}

	printf("ok: rejected with DMC_UNRAR_INVALID_DATA\n");
	return 0;
}
