/* Archive-bomb cap smoke test.
 *
 * Compiled three times with different -D overrides that each force
 * one of the DMC_UNRAR_MAX_* caps low enough to reject the golden
 * simple.rar at open time. Passes when dmc_unrar_archive_open_path
 * returns DMC_UNRAR_INVALID_DATA. See test/Makefile for the three
 * -D variants.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#include <stdio.h>
#include <stdlib.h>

#include "../dmc_unrar.c"

int main(void) {
	dmc_unrar_archive a;
	dmc_unrar_return rc;

	if (dmc_unrar_archive_init(&a) != DMC_UNRAR_OK) {
		fprintf(stderr, "init failed\n");
		return 1;
	}

	rc = dmc_unrar_archive_open_path(&a, "test/corpus/simple.rar");
	dmc_unrar_archive_close(&a);

	if (rc != DMC_UNRAR_INVALID_DATA) {
		fprintf(stderr, "expected DMC_UNRAR_INVALID_DATA (%d), got %d (%s)\n",
		        (int)DMC_UNRAR_INVALID_DATA, (int)rc, dmc_unrar_strerror(rc));
		return 1;
	}

	printf("ok: rejected with DMC_UNRAR_INVALID_DATA\n");
	return 0;
}
