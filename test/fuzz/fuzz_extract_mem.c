/* libFuzzer harness: exercise the decompressors + CRC path by
   extracting the first supported, non-directory file from each
   successfully-opened archive. */

#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../dmc_unrar.c"

/* Skip entries whose declared uncompressed size is larger than this,
   so a malformed size field can't burn fuzzer time on huge legitimate
   allocations. 16 MiB is well above the sizes our seed corpus uses. */
#define FUZZ_MAX_EXTRACT_SIZE ((uint64_t)(16 * 1024 * 1024))

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	dmc_unrar_archive a;
	dmc_unrar_size_t count, i;

	if (dmc_unrar_archive_init(&a) != DMC_UNRAR_OK)
		return 0;

	if (dmc_unrar_archive_open_mem(&a, data, (dmc_unrar_size_t)size) != DMC_UNRAR_OK) {
		dmc_unrar_archive_close(&a);
		return 0;
	}

	count = dmc_unrar_get_file_count(&a);
	for (i = 0; i < count; i++) {
		const dmc_unrar_file *stat;
		void *out = NULL;
		dmc_unrar_size_t out_size = 0;

		if (dmc_unrar_file_is_directory(&a, i))
			continue;
		if (dmc_unrar_file_is_supported(&a, i) != DMC_UNRAR_OK)
			continue;

		stat = dmc_unrar_get_file_stat(&a, i);
		if (!stat)
			continue;
		if (stat->uncompressed_size > FUZZ_MAX_EXTRACT_SIZE)
			continue;

		(void)dmc_unrar_extract_file_to_heap(&a, i, &out, &out_size, true);
		free(out);
		break;  /* one extraction per input is enough. */
	}

	dmc_unrar_archive_close(&a);
	return 0;
}
